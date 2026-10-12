#include "launcher/error.hpp"
#include "launcher/package_reader.hpp"
#include "launcher/sha256.hpp"
#include <array>
#include <cstring>
#include <exception>
#include <miniz.h>
#include <picosha2.h>
#include <set>

namespace launcher {
namespace {
void require(bool ok, const char *why) {
    if (!ok)
        fail(ErrorCategory::PackageIntegrity, why);
}
unsigned u16(const char *p) {
    return static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8);
}
std::uint32_t u32(const char *p) {
    return u16(p) | (static_cast<std::uint32_t>(u16(p + 2)) << 16);
}
std::vector<char> read(SafeFile &file, std::uint64_t offset, std::size_t length) {
    require(offset <= file.size() && length <= file.size() - offset, "ZIP read outside archive");
    std::vector<char> data(length);
    std::size_t done = 0;
    while (done < length) {
        const auto count = file.read_at(offset + done, {data.data() + done, length - done});
        require(count > 0, "Truncated ZIP");
        done += count;
    }
    return data;
}
struct Directory {
    std::uint64_t offset;
    std::vector<char> bytes;
    unsigned entries;
};
Directory preflight(SafeFile &file) {
    require(file.size() >= 22, "Truncated ZIP header");
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(file.size(), 65557));
    const auto tail = read(file, file.size() - count, count);
    for (std::size_t i = tail.size() - 22;; --i) {
        if (u32(tail.data() + i) == 0x06054b50 && i + 22 + u16(tail.data() + i + 20) == tail.size()) {
            const auto *e = tail.data() + i;
            require(i < 20 || u32(e - 20) != 0x07064b50,
                    "ZIP64 directory records exceed the v1 archive profile");
            const unsigned entries = u16(e + 10);
            const auto size = u32(e + 12), offset = u32(e + 16);
            require(u16(e + 4) == 0 && u16(e + 6) == 0 && u16(e + 8) == entries,
                    "Multidisk ZIP not supported");
            require(entries > 0 && entries <= 60000 && size <= 32 * 1024 * 1024 && offset < file.size() &&
                        size <= file.size() - offset &&
                        static_cast<std::uint64_t>(offset) + size == file.size() - count + i,
                    "Unsafe ZIP directory bounds");
            return {offset, read(file, offset, size), entries};
        }
        if (i == 0)
            break;
    }
    fail(ErrorCategory::PackageIntegrity, "ZIP end record missing or invalid");
}
void extras(std::span<const char> data) {
    while (!data.empty()) {
        require(data.size() >= 4, "Truncated ZIP extra field");
        const auto id = u16(data.data()), size = u16(data.data() + 2);
        require(size <= data.size() - 4, "Invalid ZIP extra length");
        // ZIP64 lengths and timestamps only. Reject link extensions and alternate names.
        require(id == 0x0001 || id == 0x5455,
                "Unsupported ZIP extra field (links/alternate names forbidden)");
        data = data.subspan(4 + size);
    }
}
struct Input {
    SafeFile &file;
    std::exception_ptr error;
};
std::size_t input(void *context, mz_uint64 offset, void *bytes, std::size_t size) noexcept {
    auto &in = *static_cast<Input *>(context);
    try {
        return in.file.read_at(offset, {static_cast<char *>(bytes), size});
    } catch (...) {
        in.error = std::current_exception();
        return 0;
    }
}
struct Output {
    SafeFile &file;
    std::uint64_t size, done = 0;
    std::stop_token stop;
    ProgressSink progress;
    picosha2::hash256_one_by_one hash;
    std::exception_ptr error;
};
std::size_t output(void *context, mz_uint64 offset, const void *bytes, std::size_t size) noexcept {
    auto &out = *static_cast<Output *>(context);
    try {
        if (out.stop.stop_requested())
            fail(ErrorCategory::Cancelled, "Extraction cancelled");
        require(offset == out.done && out.done <= out.size && size <= out.size - out.done,
                "ZIP expands beyond signed file length");
        const auto *data = static_cast<const char *>(bytes);
        out.file.write({data, size});
        out.hash.process(data, data + size);
        out.done += size;
        if (out.progress)
            out.progress({"Extracting", out.done, out.size, 0});
        return size;
    } catch (...) {
        out.error = std::current_exception();
        return 0;
    }
}
struct Archive {
    mz_zip_archive zip{};
    ~Archive() {
        mz_zip_reader_end(&zip);
    }
};
} // namespace
void ZipPackageReader::extract(const std::filesystem::path &archive, const PackageRecord &p,
                               const SafeRoot &staging, std::stop_token stop, ProgressSink progress) const {
    SafeRoot source(archive.parent_path());
    const auto name = archive.filename().u8string();
    auto file = source.open(std::string(name.begin(), name.end()), FileMode::Read);
    require(file.size() == p.size && sha256_file(file, stop) == p.sha256,
            "ZIP archive length or SHA-256 mismatch");
    const auto central = preflight(file);
    Input in{file, {}};
    Archive holder;
    auto &zip = holder.zip;
    zip.m_pRead = input;
    zip.m_pIO_opaque = &in;
    require(mz_zip_reader_init(&zip, p.size, 0) != 0, "Malformed ZIP archive");
    require(mz_zip_reader_get_num_files(&zip) == central.entries, "ZIP entry count mismatch");
    std::set<std::string> seen, folded;
    std::vector<std::pair<mz_uint, std::string>> work;
    // Check every entry before writing any staged payload.
    for (mz_uint index = 0; index < central.entries; ++index) {
        mz_zip_archive_file_stat entry{};
        require(mz_zip_reader_file_stat(&zip, index, &entry) != 0, "Invalid ZIP entry");
        const auto length = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
        require(length > 1 && length <= 1026, "ZIP entry path too long");
        std::string path(length, '\0');
        require(mz_zip_reader_get_filename(&zip, index, path.data(), length) == length &&
                    std::strlen(path.c_str()) + 1 == length,
                "Embedded NUL in ZIP name");
        path.pop_back();
        const bool directory = entry.m_is_directory != 0;
        if (directory && path.ends_with('/'))
            path.pop_back();
        validate_relative_path(path);
        require(folded.insert(path_key(path)).second, "Duplicate or case-aliased ZIP entry");
        const auto mode = (entry.m_external_attr >> 16) & 0170000;
        require((mode == 0 || mode == (directory ? 0040000u : 0100000u)) && !(entry.m_external_attr & 0x400),
                "ZIP symlink/device/reparse entry rejected");
        require(!entry.m_is_encrypted && entry.m_is_supported && (entry.m_method == 0 || entry.m_method == 8),
                "Unsupported ZIP compression/encryption");
        require(entry.m_central_dir_ofs <= central.bytes.size() &&
                    central.bytes.size() - entry.m_central_dir_ofs >= 46,
                "Invalid central entry offset");
        const auto *c = central.bytes.data() + entry.m_central_dir_ofs;
        const auto namesize = u16(c + 28), extra = u16(c + 30);
        require(static_cast<std::size_t>(46) + namesize + extra <=
                    central.bytes.size() - entry.m_central_dir_ofs,
                "Invalid central field bounds");
        extras({c + 46 + namesize, extra});
        const auto local = read(file, entry.m_local_header_ofs, 30);
        require(u32(local.data()) == 0x04034b50, "Invalid local ZIP header");
        const auto localname = u16(local.data() + 26), localextra = u16(local.data() + 28);
        const auto localdata = read(file, entry.m_local_header_ofs + 30, localname + localextra);
        const std::string localpath(localdata.data(), localname);
        require(localpath == (directory ? path + "/" : path), "Local/central ZIP name mismatch");
        extras({localdata.data() + localname, localextra});
        if (directory) {
            require(entry.m_uncomp_size == 0, "Nonempty ZIP directory");
            bool allowed = false;
            for (const auto &[managed, f] : p.files) {
                (void)f;
                if (managed.starts_with(path + "/"))
                    allowed = true;
            }
            require(allowed, "Unlisted ZIP directory");
            continue;
        }
        require(p.files.contains(path), "Unlisted ZIP payload");
        const auto &expected = p.files.at(path);
        require(entry.m_uncomp_size == expected.size && entry.m_uncomp_size <= 1536ull * 1024 * 1024 &&
                    entry.m_uncomp_size <= std::max<std::uint64_t>(entry.m_comp_size, 1) * 2000,
                "ZIP expansion/size limit exceeded");
        seen.insert(path);
        work.emplace_back(index, path);
    }
    require(seen.size() == p.files.size(), "ZIP is missing signed payload files");
    for (const auto &[index, path] : work) {
        const auto &expected = p.files.at(path);
        // Stage into a newly created file; no overwrite of existing stage content.
        auto target = staging.open(path, FileMode::CreateNew);
        Output out{target, expected.size, 0, stop, progress, {}, {}};
        const bool extracted = mz_zip_reader_extract_to_callback(&zip, index, output, &out, 0) != 0;
        if (in.error)
            std::rethrow_exception(in.error);
        if (out.error)
            std::rethrow_exception(out.error);
        require(extracted && out.done == expected.size, "Truncated or corrupt ZIP stream");
        out.hash.finish();
        require(picosha2::get_hash_hex_string(out.hash) == expected.sha256,
                "Extracted file SHA-256 mismatch");
        target.flush();
    }
}
} // namespace launcher
