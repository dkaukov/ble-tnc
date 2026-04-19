#pragma once

#include <stddef.h>
#include <stdint.h>

namespace blekiss {

static constexpr uint8_t KISS_FEND = 0xC0;
static constexpr uint8_t KISS_FESC = 0xDB;
static constexpr uint8_t KISS_TFEND = 0xDC;
static constexpr uint8_t KISS_TFESC = 0xDD;

constexpr uint8_t makeCmdPortByte(uint8_t command, uint8_t port) {
  return static_cast<uint8_t>(((port & 0x0Fu) << 4) | (command & 0x0Fu));
}

struct KissConsumeResult {
  bool frameReady = false;
  bool decodeError = false;
  bool frameOverflow = false;
  const uint8_t *frame = nullptr;
  size_t frameLen = 0;
};

template <size_t MAX_FRAME_SIZE>
class KissStreamDecoder {
public:
  static_assert(MAX_FRAME_SIZE > 0, "MAX_FRAME_SIZE must be > 0");

  void reset() {
    _inFrame = false;
    _escaped = false;
    _frameLen = 0;
  }

  KissConsumeResult consumeByte(uint8_t b) {
    KissConsumeResult r;

    if (b == KISS_FEND) {
      if (_inFrame && !_escaped && _frameLen > 0) {
        r.frameReady = true;
        r.frame = _frame;
        r.frameLen = _frameLen;
      } else if (_escaped) {
        r.decodeError = true;
      }

      _inFrame = true;
      _escaped = false;
      _frameLen = 0;
      return r;
    }

    if (!_inFrame) {
      return r;
    }

    uint8_t outByte = b;
    if (_escaped) {
      if (b == KISS_TFEND) {
        outByte = KISS_FEND;
      } else if (b == KISS_TFESC) {
        outByte = KISS_FESC;
      } else {
        r.decodeError = true;
        _inFrame = false;
        _escaped = false;
        _frameLen = 0;
        return r;
      }
      _escaped = false;
    } else if (b == KISS_FESC) {
      _escaped = true;
      return r;
    }

    if (_frameLen >= MAX_FRAME_SIZE) {
      r.frameOverflow = true;
      _inFrame = false;
      _escaped = false;
      _frameLen = 0;
      return r;
    }

    _frame[_frameLen++] = outByte;
    return r;
  }

private:
  uint8_t _frame[MAX_FRAME_SIZE];
  size_t _frameLen = 0;
  bool _inFrame = false;
  bool _escaped = false;
};

inline size_t encodeFrame(const uint8_t *payload,
                          size_t len,
                          uint8_t cmdPort,
                          uint8_t *out,
                          size_t outMax) {
  if (out == nullptr || outMax < 3) {
    return 0;
  }
  if (payload == nullptr && len != 0) {
    return 0;
  }

  size_t w = 0;
  out[w++] = KISS_FEND;
  out[w++] = cmdPort;

  for (size_t i = 0; i < len; ++i) {
    const uint8_t b = payload[i];
    if (b == KISS_FEND) {
      if ((w + 2) >= outMax) {
        return 0;
      }
      out[w++] = KISS_FESC;
      out[w++] = KISS_TFEND;
    } else if (b == KISS_FESC) {
      if ((w + 2) >= outMax) {
        return 0;
      }
      out[w++] = KISS_FESC;
      out[w++] = KISS_TFESC;
    } else {
      if ((w + 1) >= outMax) {
        return 0;
      }
      out[w++] = b;
    }
  }

  if (w >= outMax) {
    return 0;
  }
  out[w++] = KISS_FEND;
  return w;
}

} // namespace blekiss
