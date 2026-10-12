#pragma once
#include <filesystem>
#include <string>
#include <vector>
int test_main(int argc, char **argv);
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> strings;
    std::vector<char *> pointers;
    for (int i = 0; i < argc; ++i) {
        auto s = std::filesystem::path(argv[i]).u8string();
        strings.emplace_back(s.begin(), s.end());
    }
    for (auto &s : strings)
        pointers.push_back(s.data());
    return test_main(argc, pointers.data());
}
#else
int main(int argc, char **argv) {
    return test_main(argc, argv);
}
#endif
