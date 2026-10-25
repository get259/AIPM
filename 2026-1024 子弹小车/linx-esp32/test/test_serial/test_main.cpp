#include <unity.h>
#include "serial_line.h"
#include <vector>

void setUp() {}
void tearDown() {}

static void FragmentedUtf8AndCrLf() {
    SerialLine line;
    std::string output;
    const std::string input = "你好，AI \"测试\" \\ 路径\r\n第二句\n第三句\r";
    std::vector<std::string> messages;
    int empty = 0;
    for (unsigned char c : input) {
        auto result = line.Push(c, output);
        if (result == SerialLine::Result::Line) messages.push_back(output);
        if (result == SerialLine::Result::Empty) ++empty;
    }
    TEST_ASSERT_EQUAL_UINT(3, messages.size());
    TEST_ASSERT_EQUAL_STRING("你好，AI \"测试\" \\ 路径", messages[0].c_str());
    TEST_ASSERT_EQUAL_STRING("第二句", messages[1].c_str());
    TEST_ASSERT_EQUAL_STRING("第三句", messages[2].c_str());
    TEST_ASSERT_EQUAL_INT(0, empty);
}

static void EmptyAndIncomplete() {
    SerialLine line;
    std::string output = "保持原值";
    for (unsigned char c : std::string(" \t")) TEST_ASSERT_TRUE(line.Push(c, output) == SerialLine::Result::None);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Empty);
    TEST_ASSERT_EQUAL_STRING("保持原值", output.c_str());
    TEST_ASSERT_TRUE(line.Push('a', output) == SerialLine::Result::None);
    TEST_ASSERT_TRUE(line.Push('\r', output) == SerialLine::Result::Line);
    TEST_ASSERT_EQUAL_STRING("a", output.c_str());
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::None);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Empty);
}

static void LimitsAndRecovery() {
    SerialLine line;
    std::string output;
    for (size_t i = 0; i < SerialLine::kMaxBytes; ++i) line.Push('a', output);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Line);
    TEST_ASSERT_EQUAL_UINT(1024, output.size());
    for (size_t i = 0; i < SerialLine::kMaxBytes + 100; ++i) line.Push('b', output);
    TEST_ASSERT_TRUE(line.Push('\r', output) == SerialLine::Result::TooLong);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::None);
    line.Push('c', output);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Line);
    TEST_ASSERT_EQUAL_STRING("c", output.c_str());
}

static void RejectInvalidUtf8() {
    const std::string invalid[] = {
        std::string("a\0b", 3), "\x80", "\xc0\xaf", "\xe0\x80\x80", "\xed\xa0\x80",
        "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe4\xb8", "\xe4x", "\x1b[0m"
    };
    for (const auto& text : invalid) {
        SerialLine line;
        std::string output;
        for (unsigned char c : text) line.Push(c, output);
        TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Invalid);
        line.Push('a', output);
        TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Line);
    }
    SerialLine line;
    std::string output;
    for (unsigned char c : std::string("\xf0\x9f\x9a\x97")) line.Push(c, output);
    TEST_ASSERT_TRUE(line.Push('\n', output) == SerialLine::Result::Line);
}

static void IndependentPorts() {
    SerialLine uart, usb;
    std::string output;
    uart.Push('a', output);
    usb.Push('b', output);
    TEST_ASSERT_TRUE(uart.Push('\n', output) == SerialLine::Result::Line);
    TEST_ASSERT_EQUAL_STRING("a", output.c_str());
    TEST_ASSERT_TRUE(usb.Push('\n', output) == SerialLine::Result::Line);
    TEST_ASSERT_EQUAL_STRING("b", output.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(FragmentedUtf8AndCrLf);
    RUN_TEST(EmptyAndIncomplete);
    RUN_TEST(LimitsAndRecovery);
    RUN_TEST(RejectInvalidUtf8);
    RUN_TEST(IndependentPorts);
    return UNITY_END();
}
