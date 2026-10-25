#include <unity.h>
#include "ActionJson.h"
#include "car_reply.h"
#include "car_frame.h"

#include <cstring>
#include <string>

void setUp() {}
void tearDown() {}

static std::string JsonFor(uint8_t state, unsigned aliases = 0) {
    const char* keys[] = {"forward", "backward", (aliases & 1) ? "turnleft" : "turn_left",
                          (aliases & 2) ? "turnright" : "turn_right", "fire"};
    std::string text = "{";
    for (int i = 0; i < 5; ++i) {
        if (i) text += ',';
        text += '"';
        text += keys[i];
        text += "\":\"";
        text += (state & (1 << i)) ? "on" : "off";
        text += '"';
    }
    return text + '}';
}

static void AllActionCombinations() {
    for (uint8_t i = 0; i < 32; ++i) {
        const auto text = JsonFor(i);
        uint8_t result = 0xFF;
        TEST_ASSERT_TRUE(parseActionJson(text.data(), text.size(), result));
        TEST_ASSERT_EQUAL_UINT8(i, result);
    }
}

static void OrderAndWhitespace() {
    const char* text = " \n{\"fire\":\"on\",\"turn_right\":\"off\",\"turn_left\":\"on\",\"backward\":\"off\",\"forward\":\"on\"}\t";
    uint8_t state = 0;
    TEST_ASSERT_TRUE(parseActionJson(text, std::strlen(text), state));
    TEST_ASSERT_EQUAL_UINT8(0x15, state);
}

static void AllTurnAliasCombinations() {
    // 左右字段可分别选择任一种写法，32 种状态全部保持原位映射。
    for (unsigned aliases = 0; aliases < 4; ++aliases) {
        for (uint8_t expected = 0; expected < 32; ++expected) {
            const auto text = JsonFor(expected, aliases);
            uint8_t parsed = 0xff;
            TEST_ASSERT_TRUE(parseActionJson(text.data(), text.size(), parsed));
            TEST_ASSERT_EQUAL_HEX8(expected, parsed);
        }
    }
    const char* shuffled = " { \"turnright\":\"on\",\"fire\":\"off\",\"backward\":\"off\",\"turn_left\":\"on\",\"forward\":\"off\" } ";
    uint8_t parsed = 0;
    TEST_ASSERT_TRUE(parseActionJson(shuffled, std::strlen(shuffled), parsed));
    TEST_ASSERT_EQUAL_HEX8(0x0c, parsed);
}

static void RejectDuplicateAliasesAndMisspellings() {
    const char* invalid[] = {
        "{\"forward\":\"off\",\"backward\":\"off\",\"turn_left\":\"on\",\"turnleft\":\"on\",\"fire\":\"off\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turnright\":\"off\",\"turn_right\":\"on\",\"fire\":\"off\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turn_left\":\"on\",\"turnleft\":\"on\",\"turnright\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turnleftx\":\"on\",\"turnright\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turnLeft\":\"on\",\"turnright\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turnleft\":true,\"turnright\":\"off\",\"fire\":\"off\"}"
    };
    for (const auto text : invalid) {
        uint8_t parsed = 0x15;
        TEST_ASSERT_FALSE(parseActionJson(text, std::strlen(text), parsed));
        TEST_ASSERT_EQUAL_HEX8(0x15, parsed);
        CarReply reply;
        reply.Begin();
        reply.Append(text);
        TEST_ASSERT_TRUE(reply.Finish(true, parsed) == CarReply::Result::Invalid);
        TEST_ASSERT_EQUAL_HEX8(0, parsed);
    }
}

static void ReportedFragmentedBackwardReply() {
    const std::string first = "{\"forward\":\"off\",";
    const std::string second = "\"backward\":\"on\",\"turnleft\":\"off\",\"turnright\":\"off\",\"fire\":\"off\"}";
    const auto full = first + second;
    // 包含用户实际分段位置，也覆盖任意字节边界与重复 start/stop。
    for (size_t split = 0; split <= full.size(); ++split) {
        CarReply reply;
        uint8_t parsed = 0xff;
        reply.Begin();
        reply.Append(full.data(), split);
        reply.Begin();
        reply.Append(full.data() + split, full.size() - split);
        TEST_ASSERT_TRUE(reply.Finish(true, parsed) == CarReply::Result::Valid);
        TEST_ASSERT_EQUAL_HEX8(0x02, parsed);
        TEST_ASSERT_TRUE(reply.Finish(true, parsed) == CarReply::Result::Ignored);
    }
    CarReply aborted;
    uint8_t parsed = 0x15;
    aborted.Begin();
    aborted.Append(full);
    TEST_ASSERT_TRUE(aborted.Finish(false, parsed) == CarReply::Result::Ignored);
    TEST_ASSERT_EQUAL_HEX8(0x15, parsed);
}

static void RejectMalformedAndTruncated() {
    const char* invalid[] = {
        "{}", "{\"forward\":\"on\"}",
        "{\"forward\":\"on\",\"forward\":\"off\",\"turn_left\":\"off\",\"turn_right\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":true,\"backward\":\"off\",\"turn_left\":\"off\",\"turn_right\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":\"only\",\"backward\":\"off\",\"turn_left\":\"off\",\"turn_right\":\"off\",\"fire\":\"off\"}",
        "{\"forward\":\"on\",\"backward\":\"off\",\"turn_left\":\"off\",\"turn_right\":\"off\",\"unknown\":\"off\"}",
        "{\"forw\\u0061rd\":\"on\",\"backward\":\"off\",\"turn_left\":\"off\",\"turn_right\":\"off\",\"fire\":\"off\"}",
    };
    for (const auto text : invalid) {
        uint8_t state = 0x15;
        TEST_ASSERT_FALSE(parseActionJson(text, std::strlen(text), state));
        TEST_ASSERT_EQUAL_UINT8(0x15, state);
    }
    auto valid = JsonFor(0x15);
    for (size_t n = 0; n < valid.size(); ++n) {
        uint8_t state = 0x15;
        TEST_ASSERT_FALSE(parseActionJson(valid.data(), n, state));
        TEST_ASSERT_EQUAL_UINT8(0x15, state);
    }
    const auto trailing = valid + " extra";
    uint8_t state = 0x15;
    TEST_ASSERT_FALSE(parseActionJson(trailing.data(), trailing.size(), state));
    TEST_ASSERT_FALSE(parseActionJson(nullptr, 100, state));
}

static void FragmentedReplyAndDuplicateEvents() {
    CarReply reply;
    const auto text = JsonFor(0x15);
    uint8_t state = 0;
    reply.Begin();
    reply.Append(text.data(), 20);
    reply.Begin();  // 重复开始不能丢失前半段。
    reply.Append(text.data() + 20, text.size() - 20);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Valid), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0x15, state);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Ignored), static_cast<int>(reply.Finish(true, state)));
}

static void InvalidReplyClearsAndAllOffIsValid() {
    CarReply reply;
    uint8_t state = 0x15;
    reply.Begin();
    reply.Append("not JSON");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Invalid), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0, state);
    reply.Begin();
    reply.Append(JsonFor(0));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Valid), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0, state);
}

static void DiscardAbortedOverflowAndStrayText() {
    CarReply reply;
    uint8_t state = 0x15;
    const auto text = JsonFor(0x1F);
    reply.Append(text);  // 没有 start 的文本不能执行。
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Ignored), static_cast<int>(reply.Finish(true, state)));
    reply.Begin();
    reply.Append(text);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Ignored), static_cast<int>(reply.Finish(false, state)));
    TEST_ASSERT_EQUAL_UINT8(0x15, state);
    reply.Begin();
    reply.Append(text);
    reply.Reset();  // 断线清理后，旧 stop 不能执行旧命令。
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Ignored), static_cast<int>(reply.Finish(true, state)));
    reply.Begin();
    reply.Append(std::string(CarReply::kMaxBytes + 1, ' '));
    reply.Append(text);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Ignored), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0x15, state);
    reply.Begin();  // 上轮溢出不能污染新一轮。
    reply.Append(text);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Valid), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0x1F, state);
}

static void BufferExactBoundary() {
    CarReply reply;
    uint8_t state = 0;
    const auto text = JsonFor(0x15);
    reply.Begin();
    reply.Append(text);
    reply.Append(std::string(CarReply::kMaxBytes - text.size(), ' '));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CarReply::Result::Valid), static_cast<int>(reply.Finish(true, state)));
    TEST_ASSERT_EQUAL_UINT8(0x15, state);
}

static void ImmediateActionAtEveryFragmentBoundary() {
    for (unsigned aliases = 0; aliases < 4; ++aliases) {
        for (uint8_t expected = 0; expected < 32; ++expected) {
            const auto text = JsonFor(expected, aliases);
            for (size_t split = 0; split <= text.size(); ++split) {
                CarReply reply;
                uint8_t state = 0xff;
                unsigned actions = 0;
                reply.Begin();
                reply.Append(text.data(), split);
                if (reply.TakeAction(state)) ++actions;
                TEST_ASSERT_EQUAL_UINT(split == text.size() ? 1 : 0, actions);
                reply.Begin();  // 重复 start 不能擦除缓冲或重置本轮执行标志。
                reply.Append(text.data() + split, text.size() - split);
                if (reply.TakeAction(state)) ++actions;
                TEST_ASSERT_EQUAL_UINT(1, actions);  // 无需等待 stop，已取得动作。
                TEST_ASSERT_EQUAL_HEX8(expected, state);
                TEST_ASSERT_FALSE(reply.TakeAction(state));
                TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
                TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
                TEST_ASSERT_EQUAL_HEX8(expected, state);
            }
        }
    }
}

static void EarlyActionIsNotRepeatedOrReverted() {
    CarReply reply;
    uint8_t state = 0xff;
    reply.Begin();
    reply.Append(JsonFor(1));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(1, state);
    reply.Begin();
    reply.Append(" extra");
    reply.Append(JsonFor(2));
    TEST_ASSERT_FALSE(reply.TakeAction(state));
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
    TEST_ASSERT_EQUAL_HEX8(1, state);  // 后续内容不能把已发送动作覆盖为停止或另一个动作。

    reply.Begin();
    reply.Append(JsonFor(0));  // 下一轮合法全 off 必须立即发停止状态。
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(0, state);
    TEST_ASSERT_TRUE(reply.Finish(false, state) == CarReply::Result::Ignored);
    TEST_ASSERT_EQUAL_HEX8(0, state);  // 之后到达的中断也不再次输出。
}

static void EarlyActionRejectsInvalidAndTrailingText() {
    const std::string invalid[] = {
        JsonFor(1) + " extra", JsonFor(1) + JsonFor(2),
        "{\"forward\":\"on\"}",
        "{\"forward\":\"off\",\"backward\":\"off\",\"turn_left\":\"on\",\"turnleft\":\"on\",\"fire\":\"off\"}",
        "```json\n" + JsonFor(1) + "\n```"
    };
    for (const auto& text : invalid) {
        CarReply reply;
        uint8_t state = 0x15;
        reply.Begin();
        reply.Append(text);
        TEST_ASSERT_FALSE(reply.TakeAction(state));
        TEST_ASSERT_EQUAL_HEX8(0x15, state);
        TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Invalid);
        TEST_ASSERT_EQUAL_HEX8(0, state);  // 未提前执行时，正常结束的非法回复仍清零。
    }
}

static void EarlyActionRespectsResetOverflowAndSizeLimit() {
    CarReply reply;
    const auto text = JsonFor(0x15);
    uint8_t state = 0xff;
    reply.Append(text);
    TEST_ASSERT_FALSE(reply.TakeAction(state));  // 无 start 的片段不执行。
    reply.Begin();
    reply.Append(text.data(), text.size() - 1);
    TEST_ASSERT_FALSE(reply.TakeAction(state));
    reply.Reset();  // 取消/断线清理后，最后一段不得补成旧动作。
    reply.Append("}");
    TEST_ASSERT_FALSE(reply.TakeAction(state));
    TEST_ASSERT_TRUE(reply.Finish(false, state) == CarReply::Result::Ignored);
    TEST_ASSERT_EQUAL_HEX8(0xff, state);

    reply.Begin();
    reply.Append(std::string(CarReply::kMaxBytes + 1, ' '));
    reply.Append(text);
    TEST_ASSERT_FALSE(reply.TakeAction(state));
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);

    reply.Begin();
    reply.Append(std::string(CarReply::kMaxBytes - text.size(), ' ') + text);
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(0x15, state);
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
}

static void NextCommandDoesNotWaitForStop() {
    CarReply reply;
    uint8_t state = 0xff;
    reply.NextInput();
    reply.Begin();
    reply.Append(JsonFor(1));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(1, state);
    TEST_ASSERT_TRUE(reply.HasPendingStops());

    // 前进尚未收到 stop，下一句“停止”已能独立解析并下发。
    reply.NextInput();
    reply.Begin();
    reply.Append(JsonFor(0));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(0, state);
    TEST_ASSERT_FALSE(reply.TakeAction(state));
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
    TEST_ASSERT_TRUE(reply.HasPendingStops());
    TEST_ASSERT_EQUAL_HEX8(0, state);
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
    TEST_ASSERT_FALSE(reply.HasPendingStops());
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);

    reply.NextInput();
    reply.Begin();
    reply.Append(JsonFor(2));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_EQUAL_HEX8(2, state);
}

static void LateStopPreservesNextPartialJson() {
    const auto next = JsonFor(0);
    for (size_t split = 0; split < next.size(); ++split) {
        CarReply reply;
        uint8_t state = 0xff;
        reply.Begin();
        reply.Append(JsonFor(1));
        TEST_ASSERT_TRUE(reply.TakeAction(state));
        reply.NextInput();
        reply.Begin();
        reply.Append(next.data(), split);
        TEST_ASSERT_FALSE(reply.TakeAction(state));
        // 上轮正常或中断 stop 都不能清空新轮次的半包。
        TEST_ASSERT_TRUE(reply.Finish((split & 1) == 0, state) == CarReply::Result::Ignored);
        TEST_ASSERT_EQUAL_HEX8(1, state);
        reply.Begin();  // 同轮重复 start 仍保留缓冲。
        reply.Append(next.data() + split, next.size() - split);
        TEST_ASSERT_TRUE(reply.TakeAction(state));
        TEST_ASSERT_EQUAL_HEX8(0, state);
        TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
    }
}

static void ReplacedIncompleteReplyDoesNotPoisonNextInput() {
    CarReply reply;
    uint8_t state = 0x15;
    reply.Begin();
    reply.Append("{\"forward\":");
    reply.NextInput();
    TEST_ASSERT_TRUE(reply.HasPendingStops());
    reply.Begin();
    reply.Append(JsonFor(0));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    TEST_ASSERT_TRUE(reply.Finish(false, state) == CarReply::Result::Ignored);
    TEST_ASSERT_EQUAL_HEX8(0, state);
    TEST_ASSERT_TRUE(reply.HasPendingStops());
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Ignored);
    TEST_ASSERT_FALSE(reply.HasPendingStops());

    reply.Begin();
    reply.Append(JsonFor(1));
    TEST_ASSERT_TRUE(reply.TakeAction(state));
    reply.Reset();  // 断线时必须同时清除旧 stop 记录。
    TEST_ASSERT_FALSE(reply.HasPendingStops());
    reply.Begin();
    reply.Append("invalid JSON");
    TEST_ASSERT_TRUE(reply.Finish(true, state) == CarReply::Result::Invalid);
    TEST_ASSERT_EQUAL_HEX8(0, state);
}

static void CrcAndFrameGoldenVectors() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX8(0xF4, CarCrc8(check, sizeof(check)));
    const uint8_t expected[][4] = {{0xAA,0x55,0x00,0x82}, {0xAA,0x55,0x15,0xE9}, {0xAA,0x55,0x1F,0xDF}};
    for (const auto& frame : expected) {
        const auto actual = BuildCarFrame(frame[2]);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, actual.data(), 4);
    }
    const auto reserved = BuildCarFrame(0xFF);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected[2], reserved.data(), 4);
}

static void BinaryFrameGoldenVectors() {
    const uint8_t states[] = {0x00, 0x02, 0x15, 0x1f};
    const char* expected[] = {
        "10101010 01010101 00000000 10000010",
        "10101010 01010101 00000010 10001100",
        "10101010 01010101 00010101 11101001",
        "10101010 01010101 00011111 11011111"
    };
    for (size_t i = 0; i < 4; ++i) {
        const auto binary = FormatCarFrameBinary(BuildCarFrame(states[i]));
        TEST_ASSERT_EQUAL_STRING(expected[i], binary.data());
        TEST_ASSERT_EQUAL_UINT(35, std::strlen(binary.data()));
    }
    // 合法的全 off 必须能转换为零状态帧，不能误判成格式错误。
    const auto json = JsonFor(0);
    uint8_t parsed = 0xff;
    TEST_ASSERT_TRUE(parseActionJson(json.data(), json.size(), parsed));
    const auto binary = FormatCarFrameBinary(BuildCarFrame(parsed));
    TEST_ASSERT_EQUAL_STRING(expected[0], binary.data());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(AllActionCombinations);
    RUN_TEST(AllTurnAliasCombinations);
    RUN_TEST(RejectDuplicateAliasesAndMisspellings);
    RUN_TEST(ReportedFragmentedBackwardReply);
    RUN_TEST(OrderAndWhitespace);
    RUN_TEST(RejectMalformedAndTruncated);
    RUN_TEST(FragmentedReplyAndDuplicateEvents);
    RUN_TEST(InvalidReplyClearsAndAllOffIsValid);
    RUN_TEST(DiscardAbortedOverflowAndStrayText);
    RUN_TEST(BufferExactBoundary);
    RUN_TEST(ImmediateActionAtEveryFragmentBoundary);
    RUN_TEST(EarlyActionIsNotRepeatedOrReverted);
    RUN_TEST(EarlyActionRejectsInvalidAndTrailingText);
    RUN_TEST(EarlyActionRespectsResetOverflowAndSizeLimit);
    RUN_TEST(NextCommandDoesNotWaitForStop);
    RUN_TEST(LateStopPreservesNextPartialJson);
    RUN_TEST(ReplacedIncompleteReplyDoesNotPoisonNextInput);
    RUN_TEST(CrcAndFrameGoldenVectors);
    RUN_TEST(BinaryFrameGoldenVectors);
    return UNITY_END();
}
