#include "launcher/sha256.hpp"
#include "launcher/error.hpp"
#include "launcher/safe_files.hpp"
#include <array>
#include <fstream>
#include <picosha2.h>
namespace launcher {
std::string sha256_file(SafeFile &file, std::stop_token stop) {
    picosha2::hash256_one_by_one hash;
    std::array<char, 65536> buffer{};
    std::uint64_t offset = 0;
    const auto expected = file.size();
    while (offset < expected) {
        if (stop.stop_requested())
            fail(ErrorCategory::Cancelled, "Hashing cancelled");
        const auto count = file.read_at(
            offset, {buffer.data(),
                     static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), expected - offset))});
        if (!count)
            fail(ErrorCategory::Filesystem, "File truncated during hashing");
        hash.process(buffer.begin(), buffer.begin() + count);
        offset += count;
    }
    if (file.size() != expected)
        fail(ErrorCategory::Filesystem, "File changed during hashing");
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}
std::string sha256(std::string_view bytes) {
    return picosha2::hash256_hex_string(bytes.begin(), bytes.end());
}
std::string sha256_file(const std::filesystem::path &path, std::stop_token stop) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        fail(ErrorCategory::Filesystem, "Cannot open file for hashing");
    std::array<char, 65536> buffer{};
    picosha2::hash256_one_by_one hash;
    while (file) {
        if (stop.stop_requested())
            fail(ErrorCategory::Cancelled, "Hashing cancelled");
        file.read(buffer.data(), buffer.size());
        hash.process(buffer.begin(), buffer.begin() + file.gcount());
    }
    if (!file.eof())
        fail(ErrorCategory::Filesystem, "File read failed during hashing");
    hash.finish();
    return picosha2::get_hash_hex_string(hash);
}
} // namespace launcher
