#include "launcher/error.hpp"
#include "launcher/manifest.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    try {
        auto path = std::filesystem::u8path(argv[1]);
        if (std::filesystem::file_size(path) > 16 * 1024 * 1024)
            return 1;
        std::ifstream in(path, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)), {});
        auto manifest = launcher::verify_manifest(data, {{argv[2], launcher::public_key_from_hex(argv[3])}});
        if (argc == 6)
            launcher::enforce_sequence(manifest, std::stoull(argv[4]), argv[5]);
        std::cout << manifest.sequence << " " << manifest.payload_hash << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
