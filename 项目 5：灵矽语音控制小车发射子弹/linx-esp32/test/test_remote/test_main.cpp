#include <unity.h>
#include "remote_audio.h"
#include "remote_wire.h"
#include <vector>

void setUp() {}
void tearDown() {}

static void PcmLittleEndianAndFullFrame() {
    RemoteAudio audio; audio.Reset(17);
    std::vector<uint8_t> bytes(1920, 0);
    bytes[0] = 0x00; bytes[1] = 0x80; bytes[2] = 0xff; bytes[3] = 0x7f;
    std::vector<int16_t> frame;
    TEST_ASSERT_TRUE(audio.Append(17, 0, bytes.data(), bytes.size(), frame));
    TEST_ASSERT_EQUAL_UINT(960, frame.size());
    TEST_ASSERT_EQUAL_INT16(-32768, frame[0]); TEST_ASSERT_EQUAL_INT16(32767, frame[1]);
    TEST_ASSERT_TRUE(audio.Finish().empty());
}
static void FragmentedTailAndZeroPadding() {
    RemoteAudio audio; audio.Reset(1);
    const uint8_t pcm[] = {0x34, 0x12, 0xfe, 0xff}; std::vector<int16_t> frame;
    TEST_ASSERT_TRUE(audio.Append(1, 0, pcm, 2, frame)); TEST_ASSERT_TRUE(frame.empty());
    TEST_ASSERT_TRUE(audio.Append(1, 1, pcm + 2, 2, frame)); TEST_ASSERT_TRUE(frame.empty());
    auto tail = audio.Finish(); TEST_ASSERT_EQUAL_UINT(960, tail.size());
    TEST_ASSERT_EQUAL_INT16(0x1234, tail[0]); TEST_ASSERT_EQUAL_INT16(-2, tail[1]);
    for (size_t i = 2; i < tail.size(); ++i) TEST_ASSERT_EQUAL_INT16(0, tail[i]);
    TEST_ASSERT_FALSE(audio.Append(1, 2, pcm, 2, frame));
}
static void RejectStaleSequenceAndMalformedAudio() {
    RemoteAudio audio; audio.Reset(33); std::vector<int16_t> frame;
    uint8_t pcm[1922] = {};
    TEST_ASSERT_FALSE(audio.Append(32, 0, pcm, 2, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 1, pcm, 2, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 0, pcm, 1, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 0, pcm, sizeof(pcm), frame));
    TEST_ASSERT_FALSE(audio.Append(33, 0, nullptr, 2, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 0, pcm, 0, frame));
    TEST_ASSERT_TRUE(audio.Append(33, 0, pcm, 2, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 0, pcm, 2, frame));
    TEST_ASSERT_FALSE(audio.Append(33, 1, pcm, 1920, frame));
}
static void ResetClearsPreviousMicrophoneTail() {
    RemoteAudio audio; audio.Reset(5); uint8_t pcm[] = {1, 0}; std::vector<int16_t> frame;
    TEST_ASSERT_TRUE(audio.Append(5, 0, pcm, 2, frame)); audio.Reset(6);
    TEST_ASSERT_FALSE(audio.Append(5, 1, pcm, 2, frame));
    TEST_ASSERT_TRUE(audio.Finish().empty());
}
static void RepeatedWebSocketMessagesDoNotRetainPreviousPayload() {
    RemoteWire wire;
    const uint8_t status[] = {'s','t','a','t','u','s'};
    const uint8_t enable[] = {'e','n','a','b','l','e'};
    for (int i = 0; i < 50; ++i) {
        TEST_ASSERT_TRUE(wire.Append(1, true, status, sizeof(status)) == RemoteWire::Result::Complete);
        const auto first = wire.Take(); TEST_ASSERT_EQUAL_UINT(sizeof(status), first.size());
        TEST_ASSERT_EQUAL_MEMORY(status, first.data(), sizeof(status));
        TEST_ASSERT_TRUE(wire.Append(1, true, enable, sizeof(enable)) == RemoteWire::Result::Complete);
        const auto second = wire.Take(); TEST_ASSERT_EQUAL_MEMORY(enable, second.data(), sizeof(enable));
    }
}
static void WebSocketFragmentationBoundsAndEmptyFirstFragment() {
    RemoteWire wire; uint8_t pcm[1932] = {};
    TEST_ASSERT_TRUE(wire.Append(2, false, nullptr, 0) == RemoteWire::Result::More);
    TEST_ASSERT_TRUE(wire.Append(0, false, pcm, 12) == RemoteWire::Result::More);
    TEST_ASSERT_TRUE(wire.Append(0, true, pcm + 12, 1920) == RemoteWire::Result::Complete);
    TEST_ASSERT_EQUAL_UINT(2, wire.Type()); TEST_ASSERT_EQUAL_UINT(1932, wire.Take().size());
    TEST_ASSERT_TRUE(wire.Append(0, true, pcm, 2) == RemoteWire::Result::Invalid);
    TEST_ASSERT_TRUE(wire.Append(2, false, pcm, 1932) == RemoteWire::Result::More);
    TEST_ASSERT_TRUE(wire.Append(0, true, pcm, 1) == RemoteWire::Result::Invalid);
}
static void WebSocketRejectsReplayedRequestsAndInterleavedMessages() {
    RemoteWire wire; const uint8_t byte = 0;
    TEST_ASSERT_FALSE(wire.AcceptRequest(0)); TEST_ASSERT_TRUE(wire.AcceptRequest(1));
    TEST_ASSERT_FALSE(wire.AcceptRequest(1)); TEST_ASSERT_TRUE(wire.AcceptRequest(3));
    TEST_ASSERT_FALSE(wire.AcceptRequest(2));
    TEST_ASSERT_TRUE(wire.Append(1, false, &byte, 1) == RemoteWire::Result::More);
    TEST_ASSERT_TRUE(wire.Append(2, true, &byte, 1) == RemoteWire::Result::Invalid);
}
int main() {
    UNITY_BEGIN(); RUN_TEST(PcmLittleEndianAndFullFrame); RUN_TEST(FragmentedTailAndZeroPadding);
    RUN_TEST(RejectStaleSequenceAndMalformedAudio); RUN_TEST(ResetClearsPreviousMicrophoneTail);
    RUN_TEST(RepeatedWebSocketMessagesDoNotRetainPreviousPayload);
    RUN_TEST(WebSocketFragmentationBoundsAndEmptyFirstFragment);
    RUN_TEST(WebSocketRejectsReplayedRequestsAndInterleavedMessages);
    return UNITY_END();
}
