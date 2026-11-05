#pragma once
#include <string>
#include <cstdlib>
#include <cctype>

// 仅在客户端明确接受 gzip 时压缩，带 q=0 的编码必须排除。
inline bool RemoteAcceptsGzip(const std::string& header) {
    bool wildcard = false;
    size_t offset = 0;
    while (offset < header.size()) {
        const size_t end = header.find(',', offset);
        std::string token = header.substr(offset, end == std::string::npos ? end : end - offset);
        for (char& ch : token) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const auto first = token.find_first_not_of(" \t");
        const auto name_end = token.find_first_of("; \t", first);
        const std::string name = first == std::string::npos ? "" : token.substr(first, name_end - first);
        double quality = 1;
        const auto q = token.find("q=", name_end == std::string::npos ? token.size() : name_end);
        if (q != std::string::npos) {
            char* tail = nullptr;
            quality = std::strtod(token.c_str() + q + 2, &tail);
            if (tail == token.c_str() + q + 2 || quality < 0 || quality > 1) quality = 0;
        }
        if (name == "gzip") return quality > 0;
        if (name == "*") wildcard = quality > 0;
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return wildcard;
}
