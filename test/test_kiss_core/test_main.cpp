#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "BleKissCore.h"

static void test_make_cmd_port_byte(void) {
  TEST_ASSERT_EQUAL_HEX8(0x00, blekiss::makeCmdPortByte(0x00, 0x00));
  TEST_ASSERT_EQUAL_HEX8(0x21, blekiss::makeCmdPortByte(0x01, 0x02));
  TEST_ASSERT_EQUAL_HEX8(0xFA, blekiss::makeCmdPortByte(0x0A, 0x0F));
  TEST_ASSERT_EQUAL_HEX8(0x5B, blekiss::makeCmdPortByte(0x1B, 0x15));
}

static void test_encode_frame_escapes_reserved_bytes(void) {
  const uint8_t payload[] = {0x01, blekiss::KISS_FEND, blekiss::KISS_FESC, 0x02};
  uint8_t out[32] = {0};

  const size_t len = blekiss::encodeFrame(payload, sizeof(payload), 0x00, out, sizeof(out));

  const uint8_t expected[] = {
      blekiss::KISS_FEND,
      0x00,
      0x01,
      blekiss::KISS_FESC,
      blekiss::KISS_TFEND,
      blekiss::KISS_FESC,
      blekiss::KISS_TFESC,
      0x02,
      blekiss::KISS_FEND,
  };

  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, len);
}

static void test_stream_decoder_reassembles_split_frame(void) {
  blekiss::KissStreamDecoder<16> decoder;
  decoder.reset();

  const uint8_t chunk1[] = {blekiss::KISS_FEND, 0x00, 0x11, blekiss::KISS_FESC};
  const uint8_t chunk2[] = {blekiss::KISS_TFEND, 0x22, blekiss::KISS_FEND};

  bool gotFrame = false;
  uint8_t frame[16] = {0};
  size_t frameLen = 0;

  for (size_t i = 0; i < sizeof(chunk1); ++i) {
    const blekiss::KissConsumeResult r = decoder.consumeByte(chunk1[i]);
    TEST_ASSERT_FALSE(r.frameReady);
    TEST_ASSERT_FALSE(r.decodeError);
    TEST_ASSERT_FALSE(r.frameOverflow);
  }

  for (size_t i = 0; i < sizeof(chunk2); ++i) {
    const blekiss::KissConsumeResult r = decoder.consumeByte(chunk2[i]);
    if (r.frameReady) {
      gotFrame = true;
      frameLen = r.frameLen;
      memcpy(frame, r.frame, r.frameLen);
    }
  }

  const uint8_t expected[] = {0x00, 0x11, blekiss::KISS_FEND, 0x22};
  TEST_ASSERT_TRUE(gotFrame);
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), frameLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, frameLen);
}

static void test_stream_decoder_handles_multiple_frames_in_one_chunk(void) {
  blekiss::KissStreamDecoder<16> decoder;
  decoder.reset();

  const uint8_t stream[] = {
      blekiss::KISS_FEND, 0x00, 0x01, blekiss::KISS_FEND,
      blekiss::KISS_FEND, 0x02, 0x03, blekiss::KISS_FEND,
  };

  uint8_t frames[2][16] = {{0}};
  size_t lengths[2] = {0, 0};
  size_t count = 0;

  for (size_t i = 0; i < sizeof(stream); ++i) {
    const blekiss::KissConsumeResult r = decoder.consumeByte(stream[i]);
    if (r.frameReady) {
      TEST_ASSERT_LESS_THAN_UINT32(2, count);
      lengths[count] = r.frameLen;
      memcpy(frames[count], r.frame, r.frameLen);
      ++count;
    }
  }

  const uint8_t expected0[] = {0x00, 0x01};
  const uint8_t expected1[] = {0x02, 0x03};

  TEST_ASSERT_EQUAL_UINT32(2, count);
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected0), lengths[0]);
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected1), lengths[1]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected0, frames[0], lengths[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected1, frames[1], lengths[1]);
}

static void test_stream_decoder_detects_malformed_escape(void) {
  blekiss::KissStreamDecoder<16> decoder;
  decoder.reset();

  const uint8_t stream[] = {
      blekiss::KISS_FEND,
      0x00,
      blekiss::KISS_FESC,
      0x00,
      blekiss::KISS_FEND,
  };

  bool sawDecodeError = false;
  bool sawFrame = false;

  for (size_t i = 0; i < sizeof(stream); ++i) {
    const blekiss::KissConsumeResult r = decoder.consumeByte(stream[i]);
    if (r.decodeError) {
      sawDecodeError = true;
    }
    if (r.frameReady) {
      sawFrame = true;
    }
  }

  TEST_ASSERT_TRUE(sawDecodeError);
  TEST_ASSERT_FALSE(sawFrame);
}

static void test_stream_decoder_detects_frame_overflow(void) {
  blekiss::KissStreamDecoder<4> decoder;
  decoder.reset();

  const uint8_t stream[] = {
      blekiss::KISS_FEND,
      0x00,
      0x01,
      0x02,
      0x03,
      0x04,
      blekiss::KISS_FEND,
  };

  bool sawOverflow = false;
  bool sawFrame = false;

  for (size_t i = 0; i < sizeof(stream); ++i) {
    const blekiss::KissConsumeResult r = decoder.consumeByte(stream[i]);
    if (r.frameOverflow) {
      sawOverflow = true;
    }
    if (r.frameReady) {
      sawFrame = true;
    }
  }

  TEST_ASSERT_TRUE(sawOverflow);
  TEST_ASSERT_FALSE(sawFrame);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_make_cmd_port_byte);
  RUN_TEST(test_encode_frame_escapes_reserved_bytes);
  RUN_TEST(test_stream_decoder_reassembles_split_frame);
  RUN_TEST(test_stream_decoder_handles_multiple_frames_in_one_chunk);
  RUN_TEST(test_stream_decoder_detects_malformed_escape);
  RUN_TEST(test_stream_decoder_detects_frame_overflow);
  return UNITY_END();
}
