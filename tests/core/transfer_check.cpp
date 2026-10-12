#include "launcher/downloader.hpp"
#include "launcher/error.hpp"
#include "launcher/package_reader.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 10)
        return 2;
    try {
        std::ifstream input(std::filesystem::u8path(argv[2]), std::ios::binary);
        std::string envelope((std::istreambuf_iterator<char>(input)), {});
        auto manifest =
            launcher::verify_manifest(envelope, {{"fixture-only", launcher::public_key_from_hex(argv[3])}});
        auto package = manifest.packages.at(argv[4]);
        launcher::SafeRoot cache(std::filesystem::u8path(argv[5]), true);
        launcher::SafeRoot stage(std::filesystem::u8path(argv[6]), true);
        std::stop_source stop;
        const auto threshold = std::stoull(argv[8]);
        launcher::DownloadOptions options;
        options.policy.allow_loopback_http = true;
        options.connect_timeout = 2;
        options.transfer_timeout = 3;
        options.low_speed_time = 2;
        if (std::string(argv[9]) != "-")
            options.test_ca_bundle = argv[9];
        launcher::HttpDownloader downloader(options);
        std::uint64_t last = 0;
        auto progress = [&](const launcher::Progress &p) {
            if (p.stage == "Downloading") {
                last = p.completed;
                if (threshold && p.completed >= threshold)
                    stop.request_stop();
            }
        };
        std::filesystem::path archive;
        if (std::string(argv[1]) == "extract")
            archive = std::filesystem::u8path(argv[7]);
        else {
            package.url = argv[7];
            archive = downloader.download(package, cache, stop.get_token(), progress);
        }
        if (std::string(argv[1]) != "download")
            launcher::ZipPackageReader{}.extract(archive, package, stage, stop.get_token(), progress);
        std::cout << "verified bytes=" << package.size << " observed=" << last << '\n';
        return 0;
    } catch (const launcher::Error &error) {
        std::cerr << static_cast<int>(error.category) << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
