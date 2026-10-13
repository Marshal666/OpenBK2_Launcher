#include "build_config.hpp"
#include "launcher/downloader.hpp"
#include "launcher/error.hpp"
#include "launcher/sha256.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <curl/curl.h>
#include <exception>
#include <mutex>
#include <regex>

namespace launcher {
namespace {
struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            fail(ErrorCategory::Network, "Initialize HTTPS transport");
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};
using Curl = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
struct Request {
    SafeFile *output = nullptr;
    std::string memory;
    std::uint64_t expected = 16 * 1024 * 1024, offset = 0, written = 0;
    std::size_t discarded = 0;
    bool package = false, restart = false, range_seen = false, range_ok = false;
    long status = 0;
    std::stop_token stop;
    ProgressSink progress;
    std::exception_ptr error;
    std::array<char, CURL_ERROR_SIZE> detail{};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};
std::size_t header(char *bytes, std::size_t size, std::size_t count, void *user) noexcept {
    auto &r = *static_cast<Request *>(user);
    const auto length = size * count;
    try {
        std::string text(bytes, length);
        if (text.starts_with("HTTP/")) {
            r.range_seen = r.range_ok = false;
            const auto space = text.find(' ');
            if (space != text.npos)
                std::from_chars(text.data() + space + 1, text.data() + text.size(), r.status);
        } else if (path_key(text).starts_with("content-range:")) {
            if (r.range_seen) {
                r.range_ok = false;
                return length;
            }
            r.range_seen = true;
            std::smatch parts;
            if (std::regex_match(text, parts,
                                 std::regex("Content-Range: *bytes ([0-9]+)-([0-9]+)/([0-9]+)\\r\\n",
                                            std::regex::icase))) {
                const auto start = std::stoull(parts[1]), end = std::stoull(parts[2]),
                           total = std::stoull(parts[3]);
                r.range_ok = start == r.offset && total == r.expected && end >= start && end < total &&
                             end + 1 == total;
            }
        }
        return length;
    } catch (...) {
        r.error = std::current_exception();
        return 0;
    }
}
std::size_t write_body(char *bytes, std::size_t size, std::size_t count, void *user) noexcept {
    auto &r = *static_cast<Request *>(user);
    const auto length = size * count;
    try {
        if (r.stop.stop_requested())
            return 0;
        if (r.status != 200 && r.status != 206) {
            if (length > 65536 - r.discarded)
                fail(ErrorCategory::Network, "HTTP error body exceeds safety limit");
            r.discarded += length;
            return length;
        }
        if (r.package &&
            ((r.offset > 0 && (r.status != 206 || !r.range_ok)) || (r.offset == 0 && r.status != 200))) {
            r.restart = true;
            return 0;
        }
        if (r.offset > r.expected || r.written > r.expected - r.offset ||
            length > r.expected - r.offset - r.written)
            fail(ErrorCategory::PackageIntegrity, "HTTP response exceeds expected size");
        if (r.output)
            r.output->write({bytes, length});
        else
            r.memory.append(bytes, length);
        r.written += length;
        if (r.progress) {
            const auto seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - r.started).count();
            r.progress({"Downloading", r.offset + r.written, r.package ? r.expected : 0,
                        seconds > 0 ? r.written / seconds : 0});
        }
        return length;
    } catch (...) {
        r.error = std::current_exception();
        return 0;
    }
}
int transfer(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    return static_cast<Request *>(user)->stop.stop_requested() ? 1 : 0;
}
template <class T> void option(CURL *curl, CURLoption key, T value) {
    if (curl_easy_setopt(curl, key, value) != CURLE_OK)
        fail(ErrorCategory::Network, "Unsupported HTTPS transport option");
}
CURLcode perform(const std::string &url, Request &r, const DownloadOptions &config) {
    if (!safe_url(url, config.policy))
        fail(ErrorCategory::Network, "Only permitted HTTPS endpoints can be requested");
    Curl curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        fail(ErrorCategory::Network, "Allocate HTTPS transfer");
    option(curl.get(), CURLOPT_ERRORBUFFER, r.detail.data());
    option(curl.get(), CURLOPT_URL, url.c_str());
    option(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    option(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    option(curl.get(), CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
#if OBK_FIXTURE_BUILD
    if (!config.test_ca_bundle.empty())
        option(curl.get(), CURLOPT_CAINFO, config.test_ca_bundle.c_str());
#endif
    const bool local = config.policy.allow_loopback_http && url.starts_with("http://127.0.0.1");
    // Local HTTP is available only through explicit test policy; HTTPS never downgrades.
    option(curl.get(), CURLOPT_PROTOCOLS_STR, local ? "http,https" : "https");
    option(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    option(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    option(curl.get(), CURLOPT_MAXREDIRS, 5L);
    option(curl.get(), CURLOPT_CONNECTTIMEOUT, config.connect_timeout);
    option(curl.get(), CURLOPT_TIMEOUT, config.transfer_timeout);
    option(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    option(curl.get(), CURLOPT_LOW_SPEED_TIME, config.low_speed_time);
    option(curl.get(), CURLOPT_NOSIGNAL, 1L);
    option(curl.get(), CURLOPT_USERAGENT, build::user_agent.data());
    option(curl.get(), CURLOPT_HEADERFUNCTION, header);
    option(curl.get(), CURLOPT_HEADERDATA, &r);
    option(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    option(curl.get(), CURLOPT_WRITEDATA, &r);
    option(curl.get(), CURLOPT_NOPROGRESS, 0L);
    option(curl.get(), CURLOPT_XFERINFOFUNCTION, transfer);
    option(curl.get(), CURLOPT_XFERINFODATA, &r);
    std::string range;
    if (r.offset) {
        range = std::to_string(r.offset) + "-";
        option(curl.get(), CURLOPT_RANGE, range.c_str());
    }
    const auto result = curl_easy_perform(curl.get());
    if (r.stop.stop_requested())
        fail(ErrorCategory::Cancelled, "Download cancelled; partial data retained");
    if (r.error)
        std::rethrow_exception(r.error);
    return result;
}
bool transient(CURLcode code, long status) {
    return status == 429 || status >= 500 || code == CURLE_OPERATION_TIMEDOUT ||
           code == CURLE_COULDNT_CONNECT || code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_RECV_ERROR ||
           code == CURLE_PARTIAL_FILE;
}
void backoff(unsigned attempt, std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any signal;
    std::unique_lock lock(mutex);
    signal.wait_for(lock, stop, std::chrono::milliseconds(250 * (1u << std::min(attempt, 4u))),
                    [] { return false; });
    if (stop.stop_requested())
        fail(ErrorCategory::Cancelled, "Download cancelled");
}
void network_error(CURLcode code, const Request &r) {
    fail(ErrorCategory::Network, "Download failed: HTTP " + std::to_string(r.status) + ", " +
                                     curl_easy_strerror(code) +
                                     (r.detail[0] ? std::string("; ") + r.detail.data() : ""));
}
} // namespace
HttpDownloader::HttpDownloader(DownloadOptions options) : options_(options) {
    static CurlGlobal global;
#if !OBK_FIXTURE_BUILD
    if (options_.policy.allow_loopback_http || !options_.test_ca_bundle.empty())
        fail(ErrorCategory::Network, "Fixture transport policy is unavailable in production");
#endif
    if (options_.retries > 5 || options_.connect_timeout < 1 || options_.transfer_timeout < 1 ||
        options_.low_speed_time < 1)
        fail(ErrorCategory::Network, "Invalid transport limits");
}
std::string HttpDownloader::fetch_envelope(const std::string &url, std::stop_token stop,
                                           ProgressSink progress) const {
    for (unsigned attempt = 0;; ++attempt) {
        Request r;
        r.stop = stop;
        r.progress = progress;
        const auto result = perform(url, r, options_);
        if (result == CURLE_OK && r.status == 200)
            return r.memory;
        if (attempt >= options_.retries || !transient(result, r.status))
            network_error(result, r);
        backoff(attempt, stop);
    }
}
std::filesystem::path HttpDownloader::download(const PackageRecord &p, const SafeRoot &cache,
                                               std::stop_token stop, ProgressSink progress) const {
    if (!std::regex_match(p.sha256, std::regex("[0-9a-f]{64}")) || !p.size || p.size > 1536ull * 1024 * 1024)
        fail(ErrorCategory::PackageIntegrity, "Invalid package digest or size");
    const auto complete = p.sha256 + ".zip", part = p.sha256 + ".part", meta = p.sha256 + ".meta";
    const auto metadata = p.sha256 + "\n" + std::to_string(p.size) + "\n" + p.url;
    if (cache.exists(complete)) {
        auto file = cache.open(complete, FileMode::Read);
        if (file.size() == p.size && sha256_file(file, stop) == p.sha256)
            return cache.path() / complete;
    }
    if (cache.exists(complete))
        cache.remove(complete);
    if (!cache.exists(meta) || cache.read(meta, 8192) != metadata) {
        cache.remove(part);
        cache.write_atomic(meta, metadata);
    }
    bool restarted = false;
    for (unsigned attempt = 0;;) {
        std::uint64_t offset = 0;
        if (cache.exists(part)) {
            auto file = cache.open(part, FileMode::Read);
            offset = file.size();
        }
        if (offset > p.size) {
            cache.remove(part);
            offset = 0;
        }
        if (offset == p.size) {
            bool verified = false;
            {
                auto file = cache.open(part, FileMode::Read);
                verified = sha256_file(file, stop) == p.sha256;
            }
            if (verified) {
                cache.rename(part, complete);
                cache.remove(meta);
                return cache.path() / complete;
            }
            cache.remove(part);
            fail(ErrorCategory::PackageIntegrity, "Downloaded ZIP SHA-256 mismatch");
        }
        Request r;
        r.package = true;
        r.expected = p.size;
        r.offset = offset;
        r.stop = stop;
        r.progress = progress;
        CURLcode result;
        {
            auto file = cache.open(part, offset ? FileMode::Append : FileMode::Rewrite);
            r.output = &file;
            result = perform(p.url, r, options_);
            file.flush();
        }
        if (r.restart || r.status == 416) {
            if (restarted)
                fail(ErrorCategory::Network, "Server returned inconsistent Range responses after restart");
            cache.remove(part);
            restarted = true;
            continue;
        }
        if (result == CURLE_OK && (r.status == 200 || r.status == 206)) {
            if (r.offset + r.written != p.size)
                fail(ErrorCategory::PackageIntegrity, "Downloaded ZIP size mismatch");
            if (progress)
                progress({"Verifying", 0, p.size, 0});
            bool valid = false;
            {
                auto file = cache.open(part, FileMode::Read);
                valid = sha256_file(file, stop) == p.sha256;
            }
            if (!valid) {
                cache.remove(part);
                fail(ErrorCategory::PackageIntegrity, "Downloaded ZIP SHA-256 mismatch");
            }
            cache.rename(part, complete);
            cache.remove(meta);
            return cache.path() / complete;
        }
        if (attempt >= options_.retries || !transient(result, r.status))
            network_error(result, r);
        backoff(attempt++, stop);
    }
}
} // namespace launcher
