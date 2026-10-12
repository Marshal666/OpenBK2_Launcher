#pragma once
#include "launcher/error.hpp"
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <yyjson.h>

namespace launcher::json {
inline std::string quote(std::string_view text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else
            result += static_cast<char>(c);
    }
    return result + '"';
}
using Value = yyjson_val *;
using Document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
inline void require(bool ok, const std::string &why) {
    if (!ok)
        fail(ErrorCategory::Manifest, why);
}
inline std::string string(Value v) {
    require(yyjson_is_str(v), "Expected JSON string");
    std::string s(yyjson_get_str(v), yyjson_get_len(v));
    require(s.find('\0') == std::string::npos && s.size() <= 16 * 1024 * 1024, "Invalid string");
    return s;
}
inline std::uint64_t number(Value v) {
    require(yyjson_is_uint(v), "Expected nonnegative integer");
    return yyjson_get_uint(v);
}
inline std::vector<Value> array(Value v) {
    require(yyjson_is_arr(v), "Expected array");
    require(yyjson_arr_size(v) <= 100000, "Array limit exceeded");
    std::vector<Value> out;
    size_t i, max;
    Value item;
    yyjson_arr_foreach(v, i, max, item) out.push_back(item);
    return out;
}
inline std::vector<std::pair<std::string, Value>> object(Value v) {
    require(yyjson_is_obj(v), "Expected object");
    std::vector<std::pair<std::string, Value>> out;
    size_t i, max;
    Value key, value;
    yyjson_obj_foreach(v, i, max, key, value) out.emplace_back(string(key), value);
    return out;
}
inline Value get(Value v, const char *key) {
    require(yyjson_is_obj(v), "Expected object");
    auto *r = yyjson_obj_get(v, key);
    require(r != nullptr, std::string("Missing field: ") + key);
    return r;
}
inline void fields(Value v, std::initializer_list<const char *> required,
                   std::initializer_list<const char *> optional = {}) {
    std::set<std::string> allowed;
    for (const auto *k : required) {
        get(v, k);
        allowed.emplace(k);
    }
    for (const auto *k : optional)
        allowed.emplace(k);
    for (const auto &[key, value] : object(v)) {
        (void)value;
        require(allowed.contains(key), "Unknown field: " + key);
    }
}
inline void inspect(Value v, unsigned depth, std::size_t &remaining) {
    require(depth <= 48 && remaining-- > 0, "JSON complexity limit exceeded");
    if (yyjson_is_obj(v)) {
        std::set<std::string> seen;
        for (const auto &[key, value] : object(v)) {
            require(seen.insert(key).second, "Duplicate JSON field: " + key);
            inspect(value, depth + 1, remaining);
        }
    } else if (yyjson_is_arr(v)) {
        for (auto item : array(v))
            inspect(item, depth + 1, remaining);
    }
}
inline Document parse(std::string_view data, std::size_t limit = 16 * 1024 * 1024) {
    require(!data.empty() && data.size() <= limit, "JSON size limit exceeded");
    // yyjson validates UTF-8 and consumes the whole document without permissive flags.
    Document doc(yyjson_read(data.data(), data.size(), 0), yyjson_doc_free);
    require(doc != nullptr, "Malformed JSON or invalid UTF-8");
    std::size_t remaining = 1000000;
    inspect(yyjson_doc_get_root(doc.get()), 0, remaining);
    return doc;
}
} // namespace launcher::json
