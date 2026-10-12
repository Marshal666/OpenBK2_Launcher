#pragma once
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
namespace launcher {
class SafeFile;
std::string sha256(std::string_view bytes);
std::string sha256_file(const std::filesystem::path &path, std::stop_token stop = {});
std::string sha256_file(SafeFile &file, std::stop_token stop = {});
} // namespace launcher
