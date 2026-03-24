#include <gtest/gtest.h>
#include "codec/MediaCodecDecoder.h"

// Tests for H.264 NAL unit handling in the codec input path.
//
// The production path: FreeRDP GFX callback → VirtualMonitor::SubmitFrame()
//   → MediaCodecDecoder::SubmitFrame() → AMediaCodec input buffer.
//
// These tests exercise MediaCodecDecoder::SubmitFrame() with real H.264-style
// payloads.  The host stubs return -1 for AMediaCodec_dequeueInputBuffer
// (no buffer available), so SubmitFrame will return false — but must never
// crash, corrupt state, or exhibit undefined behaviour.

// ── Common H.264 byte patterns ────────────────────────────────────────────────

static const uint8_t kStartCode3[]  = { 0x00, 0x00, 0x01 };
static const uint8_t kStartCode4[]  = { 0x00, 0x00, 0x00, 0x01 };
static const uint8_t kIdrNalUnit[]  = { 0x00, 0x00, 0x00, 0x01, 0x65 };  // IDR slice
static const uint8_t kSpsNalUnit[]  = { 0x00, 0x00, 0x00, 0x01, 0x67 };  // SPS
static const uint8_t kCorrupted[]   = { 0xFF, 0xAB, 0x12, 0x34 };         // no start code

// ── Fixture ───────────────────────────────────────────────────────────────────

class RdpParserTest : public ::testing::Test {
protected:
    // outputSurface = nullptr is valid for host tests (stub ignores it).
    MediaCodecDecoder decoder{nullptr, 0};

    void SetUpRunning() {
        ASSERT_TRUE(decoder.Configure(1920, 1080));
        ASSERT_TRUE(decoder.Start());
        ASSERT_TRUE(decoder.IsRunning());
    }
};

// ── State tests ───────────────────────────────────────────────────────────────

TEST_F(RdpParserTest, InitialState_NotRunning) {
    EXPECT_FALSE(decoder.IsRunning());
    EXPECT_FALSE(decoder.IsConfigured());
}

TEST_F(RdpParserTest, AfterConfigure_ConfiguredButNotRunning) {
    ASSERT_TRUE(decoder.Configure(1920, 1080));
    EXPECT_TRUE(decoder.IsConfigured());
    EXPECT_FALSE(decoder.IsRunning());
}

TEST_F(RdpParserTest, AfterStart_Running) {
    SetUpRunning();
    EXPECT_TRUE(decoder.IsRunning());
}

TEST_F(RdpParserTest, AfterStop_NotRunning) {
    SetUpRunning();
    EXPECT_TRUE(decoder.Stop());
    EXPECT_FALSE(decoder.IsRunning());
}

TEST_F(RdpParserTest, PauseResume_TogglesRunningState) {
    SetUpRunning();
    decoder.Pause();
    EXPECT_FALSE(decoder.IsRunning());
    decoder.Resume();
    EXPECT_TRUE(decoder.IsRunning());
}

// ── SubmitFrame before Start ───────────────────────────────────────────────────

TEST_F(RdpParserTest, SubmitFrame_WhenNotRunning_ReturnsFalse) {
    EXPECT_FALSE(decoder.SubmitFrame(kStartCode4, sizeof(kStartCode4), 0));
}

// ── SubmitFrame with H.264 start codes ────────────────────────────────────────
//
// The stub's dequeueInputBuffer returns -1 (no buffer), so SubmitFrame will
// return false.  The important property is that it does not crash.

TEST_F(RdpParserTest, SubmitFrame_4ByteStartCode_DoesNotCrash) {
    SetUpRunning();
    EXPECT_NO_FATAL_FAILURE(
        decoder.SubmitFrame(kStartCode4, sizeof(kStartCode4), 0));
}

TEST_F(RdpParserTest, SubmitFrame_3ByteStartCode_DoesNotCrash) {
    SetUpRunning();
    EXPECT_NO_FATAL_FAILURE(
        decoder.SubmitFrame(kStartCode3, sizeof(kStartCode3), 0));
}

TEST_F(RdpParserTest, SubmitFrame_IDRNalUnit_DoesNotCrash) {
    SetUpRunning();
    EXPECT_NO_FATAL_FAILURE(
        decoder.SubmitFrame(kIdrNalUnit, sizeof(kIdrNalUnit), 1000));
}

TEST_F(RdpParserTest, SubmitFrame_SPSNalUnit_DoesNotCrash) {
    SetUpRunning();
    EXPECT_NO_FATAL_FAILURE(
        decoder.SubmitFrame(kSpsNalUnit, sizeof(kSpsNalUnit), 0));
}

// ── SubmitFrame with corrupted / edge-case payloads ───────────────────────────

TEST_F(RdpParserTest, SubmitFrame_NoStartCode_DoesNotCrash) {
    SetUpRunning();
    EXPECT_NO_FATAL_FAILURE(
        decoder.SubmitFrame(kCorrupted, sizeof(kCorrupted), 0));
}

TEST_F(RdpParserTest, SubmitFrame_NoBufferAvailable_ReturnsFalse) {
    // Stub always returns -1 for dequeueInputBuffer — no buffer is ever available.
    SetUpRunning();
    EXPECT_FALSE(decoder.SubmitFrame(kIdrNalUnit, sizeof(kIdrNalUnit), 0));
}

TEST_F(RdpParserTest, SubmitFrame_MultipleCallsInSequence_NoCrash) {
    SetUpRunning();
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_FATAL_FAILURE(
            decoder.SubmitFrame(kIdrNalUnit, sizeof(kIdrNalUnit),
                                static_cast<int64_t>(i) * 33333));
    }
}
