#pragma once

#include <cstddef>
#include <string>

// 按字节收集 UTF-8；超长或非法输入整行丢弃，不能上传截断的指令。
class SerialLine {
public:
    static constexpr size_t kMaxBytes = 1024;
    enum class Result { None, Line, Empty, TooLong, Invalid };

    Result Push(unsigned char ch, std::string& output) {
        if (ch == '\n' && previous_cr_) {
            previous_cr_ = false;
            return Result::None;
        }
        previous_cr_ = ch == '\r';
        if (ch != '\r' && ch != '\n') {
            if (buffer_.size() < kMaxBytes && !overflow_) buffer_ += static_cast<char>(ch);
            else overflow_ = true;
            return Result::None;
        }
        Result result = Result::Line;
        if (overflow_) result = Result::TooLong;
        else if (!ValidUtf8(buffer_)) result = Result::Invalid;
        else if (buffer_.find_first_not_of(" \t") == std::string::npos) result = Result::Empty;
        if (result == Result::Line) output = buffer_;
        buffer_.clear();
        overflow_ = false;
        return result;
    }

private:
    static bool ValidUtf8(const std::string& text) {
        for (size_t i = 0; i < text.size();) {
            auto c = static_cast<unsigned char>(text[i++]);
            if (c < 0x80) {
                if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
                continue;
            }
            unsigned code = 0;
            unsigned minimum = 0;
            size_t extra = 0;
            if (c >= 0xc2 && c <= 0xdf) { extra = 1; code = c & 0x1f; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { extra = 2; code = c & 0x0f; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { extra = 3; code = c & 7; minimum = 0x10000; }
            else return false;
            if (i + extra > text.size()) return false;
            while (extra--) {
                c = static_cast<unsigned char>(text[i++]);
                if ((c & 0xc0) != 0x80) return false;
                code = (code << 6) | (c & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        }
        return true;
    }

    std::string buffer_;
    bool overflow_ = false;
    bool previous_cr_ = false;
};
