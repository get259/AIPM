#include "ActionJson.h"
#include <string.h>

// 只解析本项目约定的动作对象，不构造通用 JSON 文档。
bool parseActionJson(const char* buf, size_t length, uint8_t& output) {
  if (buf == nullptr || length == 0) return false;

  const char* p = buf;
  const char* end = buf + length;
  uint8_t states = 0;  // 暂存本轮状态，校验完成前不影响应用层。
  uint8_t seen = 0;    // 记录已出现的字段，检查重复和缺失。

  // 仅跳过 JSON 允许的四种空白字符。
  auto skipWhitespace = [&]() {
    while (p < end &&
           (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      ++p;
    }
  };

  // 检查并消耗结构字符，每次解引用前确认仍在输入范围内。
  auto consume = [&](char expected) -> bool {
    skipWhitespace();
    if (p == end || *p != expected) return false;
    ++p;
    return true;
  };

  // 引用原输入中的字符串片段，不创建 String；本协议不接受转义形式。
  auto readString = [&](const char*& start, size_t& size) -> bool {
    if (!consume('"')) return false;
    start = p;
    while (p < end && *p != '"') {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c == '\\' || c < 0x20) return false;
      ++p;
    }
    if (p == end) return false;
    size = static_cast<size_t>(p - start);
    ++p;  // 跳过结束引号。
    return true;
  };

  if (!consume('{')) return false;

  // 每条对象恰好含五个字段；各字段可以按任意顺序到达。
  for (uint8_t i = 0; i < 5; ++i) {
    const char* key = nullptr;
    size_t keyLength = 0;
    if (!readString(key, keyLength) || keyLength == 0) return false;

    uint8_t bit = 0;
    // 首字母筛选候选项，再检查完整名称和长度，避免前缀误判。
    switch (key[0]) {
      case 'f':
        if (keyLength == 7 && memcmp(key, "forward", 7) == 0) {
          bit = ActionBits::Forward;
        } else if (keyLength == 4 && memcmp(key, "fire", 4) == 0) {
          bit = ActionBits::Fire;
        }
        break;
      case 'b':
        if (keyLength == 8 && memcmp(key, "backward", 8) == 0) {
          bit = ActionBits::Backward;
        }
        break;
      case 't':
        // 两种拼写映射到同一位，后续 seen 检查仍会拒绝别名重复。
        if ((keyLength == 9 && memcmp(key, "turn_left", 9) == 0) ||
            (keyLength == 8 && memcmp(key, "turnleft", 8) == 0)) {
          bit = ActionBits::TurnLeft;
        } else if ((keyLength == 10 && memcmp(key, "turn_right", 10) == 0) ||
                   (keyLength == 9 && memcmp(key, "turnright", 9) == 0)) {
          bit = ActionBits::TurnRight;
        }
        break;
    }

    if (bit == 0 || (seen & bit) != 0) return false;
    seen |= bit;
    if (!consume(':')) return false;

    const char* value = nullptr;
    size_t valueLength = 0;
    if (!readString(value, valueLength)) return false;

    if (valueLength == 2 && memcmp(value, "on", 2) == 0) {
      states |= bit;
    } else if (!(valueLength == 3 && memcmp(value, "off", 3) == 0)) {
      return false;
    }

    // 最后一个字段必须由右大括号结束，拒绝多余字段和尾随逗号。
    if (!consume(i < 4 ? ',' : '}')) return false;
  }

  skipWhitespace();
  if (p != end || seen != ActionBits::All) return false;

  output = states;  // 整条消息有效后一次性提交状态。
  return true;
}
