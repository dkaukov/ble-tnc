#pragma once

#include "BleKissCore.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <stddef.h>
#include <stdint.h>
#include <string>

template <
    size_t INCOMING_STREAM_SIZE = 1024,
    size_t DECODED_FRAME_SIZE = 768,
    size_t OUTGOING_FRAME_SIZE = 768,
    size_t OUTGOING_QUEUE_DEPTH = 4>
class BleKissTnc {
public:
  static_assert(INCOMING_STREAM_SIZE > 0, "INCOMING_STREAM_SIZE must be > 0");
  static_assert(DECODED_FRAME_SIZE > 0, "DECODED_FRAME_SIZE must be > 0");
  static_assert(OUTGOING_FRAME_SIZE >= 3, "OUTGOING_FRAME_SIZE must be >= 3");
  static_assert(OUTGOING_QUEUE_DEPTH > 0, "OUTGOING_QUEUE_DEPTH must be > 0");

  static constexpr uint8_t KISS_FEND = blekiss::KISS_FEND;
  static constexpr uint8_t KISS_FESC = blekiss::KISS_FESC;
  static constexpr uint8_t KISS_TFEND = blekiss::KISS_TFEND;
  static constexpr uint8_t KISS_TFESC = blekiss::KISS_TFESC;

  using FrameCallback = void (*)(const uint8_t *data, size_t len, void *ctx);
  using EventCallback = void (*)(void *ctx);

  struct Config {
    const char *deviceName = "BLE KISS TNC";
    uint16_t preferredMtu = 247;

    // BLE-KISS API spec UUIDs
    const char *serviceUuid = "00000001-ba2a-46c9-ae49-01b0961f68bb";
    const char *txCharUuid = "00000002-ba2a-46c9-ae49-01b0961f68bb"; // app -> TNC (write)
    const char *rxCharUuid = "00000003-ba2a-46c9-ae49-01b0961f68bb"; // TNC -> app (notify/read)

    bool autoStartAdvertising = true;
    bool restartAdvertisingOnDisconnect = true;
    bool requireNotifySubscription = true;
    uint8_t maxNotifyChunksPerLoop = 1;
    int8_t txPower = ESP_PWR_LVL_P9;
  };

  struct Stats {
    uint32_t rxBytes = 0;
    uint32_t rxFrames = 0;
    uint32_t rxDecodeErrors = 0;
    uint32_t rxFrameOverflows = 0;
    uint32_t rxIncomingOverflowDrops = 0;

    uint32_t txFramesQueued = 0;
    uint32_t txFramesSent = 0;
    uint32_t txNotifyChunks = 0;
    uint32_t txQueueFullDrops = 0;
    uint32_t txEncodeFailures = 0;
    uint32_t txNotifyFailures = 0;
  };

  explicit BleKissTnc(const Config &config = Config()) : _config(config) {}

  BleKissTnc(const BleKissTnc &) = delete;
  BleKissTnc &operator=(const BleKissTnc &) = delete;

  bool begin() {
    if (_begun) {
      return true;
    }
    if (instanceSlot() != nullptr && instanceSlot() != this) {
      return false;
    }

    resetRuntimeState();

    instanceSlot() = this;
    _mtu = 23;

    NimBLEDevice::init(_config.deviceName);
    NimBLEDevice::setMTU((_config.preferredMtu < 23) ? 23 : _config.preferredMtu);
    NimBLEDevice::setPower(_config.txPower);

    _server = NimBLEDevice::createServer();
    if (_server == nullptr) {
      instanceSlot() = nullptr;
      return false;
    }
    _server->setCallbacks(&_serverCallbacks);

    NimBLEService *service = _server->createService(_config.serviceUuid);
    if (service == nullptr) {
      instanceSlot() = nullptr;
      return false;
    }

    _rxChar = service->createCharacteristic(
        _config.rxCharUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    _txChar = service->createCharacteristic(
        _config.txCharUuid,
        NIMBLE_PROPERTY::WRITE);

    if (_rxChar == nullptr || _txChar == nullptr) {
      instanceSlot() = nullptr;
      return false;
    }

    _rxChar->setCallbacks(&_rxCallbacks);
    _txChar->setCallbacks(&_txCallbacks);

    _begun = true;

    if (_config.autoStartAdvertising) {
      if (!startAdvertising()) {
        _begun = false;
        instanceSlot() = nullptr;
        return false;
      }
    }

    return true;
  }

  void end() {
    if (!_begun) {
      return;
    }

    _connected = false;
    _notifySubscribed = false;
    _mtu = 23;

    clearIncomingStream();
    clearDecodeState();
    clearQueue();

    NimBLEDevice::stopAdvertising();

    _server = nullptr;
    _txChar = nullptr;
    _rxChar = nullptr;

    _begun = false;
    if (instanceSlot() == this) {
      instanceSlot() = nullptr;
    }
  }

  void loop() { (void)drainOutgoing(_config.maxNotifyChunksPerLoop); }

  bool startAdvertising() {
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv == nullptr) {
      return false;
    }

    adv->reset();
    adv->addServiceUUID(_config.serviceUuid);
    return adv->start();
  }

  bool isBegun() const { return _begun; }
  bool isConnected() const { return _connected; }
  bool isNotifySubscribed() const { return _notifySubscribed; }

  bool canSend() const {
    if (!_connected || _rxChar == nullptr) {
      return false;
    }
    if (_config.requireNotifySubscription && !_notifySubscribed) {
      return false;
    }
    return true;
  }

  uint16_t getMtu() const { return _mtu; }

  size_t outgoingQueueCount() const { return _queueCount; }
  size_t outgoingQueueCapacity() const { return OUTGOING_QUEUE_DEPTH; }
  size_t outgoingQueueFree() const { return OUTGOING_QUEUE_DEPTH - _queueCount; }
  bool outgoingQueueEmpty() const { return _queueCount == 0; }
  bool outgoingQueueFull() const { return _queueCount >= OUTGOING_QUEUE_DEPTH; }
  static constexpr size_t estimatedStaticBufferBytes() {
    return INCOMING_STREAM_SIZE + DECODED_FRAME_SIZE +
           (OUTGOING_QUEUE_DEPTH * (OUTGOING_FRAME_SIZE + sizeof(size_t)));
  }

  void setFrameCallback(FrameCallback cb, void *ctx = nullptr) {
    _frameCallback = cb;
    _frameCallbackCtx = ctx;
  }

  void setConnectCallback(EventCallback cb, void *ctx = nullptr) {
    _connectCallback = cb;
    _connectCallbackCtx = ctx;
  }

  void setDisconnectCallback(EventCallback cb, void *ctx = nullptr) {
    _disconnectCallback = cb;
    _disconnectCallbackCtx = ctx;
  }

  void setMaxNotifyChunksPerLoop(uint8_t chunks) {
    _config.maxNotifyChunksPerLoop = (chunks == 0) ? 1 : chunks;
  }

  const Stats &stats() const { return _stats; }

  void clearStats() { _stats = Stats{}; }

  // KISS command 0x00 (data frame) with explicit KISS port nibble.
  bool sendDataFrame(const uint8_t *payload, size_t len, uint8_t port = 0) {
    if (payload == nullptr && len != 0) {
      return false;
    }
    const uint8_t cmdPort = makeCmdPortByte(0x00, port);
    return enqueueEncodedFrame(payload, len, cmdPort);
  }

  // Pass a full KISS payload where first byte is cmd/port and the rest is command payload.
  bool sendKissPayload(const uint8_t *kissPayload, size_t len) {
    if (kissPayload == nullptr || len == 0) {
      return false;
    }
    return enqueueEncodedFrame(kissPayload + 1, len - 1, kissPayload[0]);
  }

  // Drain queued notifications (chunked to negotiated ATT payload size).
  // Returns number of notify chunks sent.
  size_t drainOutgoing(size_t maxChunks = 1) {
    if (maxChunks == 0) {
      return 0;
    }

    size_t sentChunks = 0;
    while (sentChunks < maxChunks) {
      if (!flushOneOutgoingChunk()) {
        break;
      }
      ++sentChunks;
    }
    return sentChunks;
  }

private:
  struct QueueSlot {
    uint8_t data[OUTGOING_FRAME_SIZE];
    size_t len = 0;
  };

  class InternalServerCallbacks : public NimBLEServerCallbacks {
  public:
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
      (void)server;
      (void)connInfo;
      if (instanceSlot() != nullptr) {
        instanceSlot()->handleConnect();
      }
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
      (void)server;
      (void)connInfo;
      if (instanceSlot() != nullptr) {
        instanceSlot()->handleDisconnect(reason);
      }
    }

    void onMTUChange(uint16_t mtu, NimBLEConnInfo &connInfo) override {
      (void)connInfo;
      if (instanceSlot() != nullptr) {
        instanceSlot()->handleMtuChange(mtu);
      }
    }
  };

  class InternalTxCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
      (void)connInfo;
      if (instanceSlot() == nullptr || c == nullptr) {
        return;
      }

      const std::string &value = c->getValue();
      if (!value.empty()) {
        instanceSlot()->handleBleWrite(reinterpret_cast<const uint8_t *>(value.data()), value.size());
      }
    }
  };

  class InternalRxCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onSubscribe(NimBLECharacteristic *c, NimBLEConnInfo &connInfo, uint16_t subValue) override {
      (void)c;
      (void)connInfo;
      if (instanceSlot() != nullptr) {
        instanceSlot()->_notifySubscribed = (subValue & 0x0001u) != 0;
      }
    }
  };

private:
  static BleKissTnc *&instanceSlot() {
    static BleKissTnc *instance = nullptr;
    return instance;
  }

  Config _config;
  Stats _stats{};

  NimBLEServer *_server = nullptr;
  NimBLECharacteristic *_txChar = nullptr;
  NimBLECharacteristic *_rxChar = nullptr;

  InternalServerCallbacks _serverCallbacks;
  InternalTxCallbacks _txCallbacks;
  InternalRxCallbacks _rxCallbacks;

  bool _begun = false;
  bool _connected = false;
  bool _notifySubscribed = false;
  uint16_t _mtu = 23;

  FrameCallback _frameCallback = nullptr;
  void *_frameCallbackCtx = nullptr;

  EventCallback _connectCallback = nullptr;
  void *_connectCallbackCtx = nullptr;

  EventCallback _disconnectCallback = nullptr;
  void *_disconnectCallbackCtx = nullptr;

  uint8_t _incomingBuf[INCOMING_STREAM_SIZE];
  size_t _incomingHead = 0;
  size_t _incomingTail = 0;
  size_t _incomingCount = 0;

  blekiss::KissStreamDecoder<DECODED_FRAME_SIZE> _streamDecoder;

  QueueSlot _queue[OUTGOING_QUEUE_DEPTH];
  size_t _queueHead = 0;
  size_t _queueTail = 0;
  size_t _queueCount = 0;
  size_t _currentChunkOffset = 0;

  static constexpr uint8_t makeCmdPortByte(uint8_t command, uint8_t port) {
    return blekiss::makeCmdPortByte(command, port);
  }

  void resetRuntimeState() {
    _connected = false;
    _notifySubscribed = false;
    _mtu = 23;

    clearIncomingStream();
    clearDecodeState();
    clearQueue();
  }

  void clearIncomingStream() {
    _incomingHead = 0;
    _incomingTail = 0;
    _incomingCount = 0;
  }

  void clearDecodeState() {
    _streamDecoder.reset();
  }

  void handleConnect() {
    _connected = true;
    _notifySubscribed = false;

    if (_connectCallback != nullptr) {
      _connectCallback(_connectCallbackCtx);
    }
  }

  void handleDisconnect(int reason) {
    (void)reason;

    _connected = false;
    _notifySubscribed = false;
    _mtu = 23;

    clearIncomingStream();
    clearDecodeState();
    clearQueue();

    if (_disconnectCallback != nullptr) {
      _disconnectCallback(_disconnectCallbackCtx);
    }

    if (_config.restartAdvertisingOnDisconnect) {
      (void)startAdvertising();
    }
  }

  void handleMtuChange(uint16_t mtu) {
    _mtu = (mtu < 23) ? 23 : mtu;
  }

  void handleBleWrite(const uint8_t *data, size_t len) { consumeIncomingBytes(data, len); }

  bool pushIncomingByte(uint8_t b) {
    if (_incomingCount >= INCOMING_STREAM_SIZE) {
      return false;
    }

    _incomingBuf[_incomingTail] = b;
    _incomingTail = (_incomingTail + 1) % INCOMING_STREAM_SIZE;
    ++_incomingCount;
    return true;
  }

  bool popIncomingByte(uint8_t &b) {
    if (_incomingCount == 0) {
      return false;
    }

    b = _incomingBuf[_incomingHead];
    _incomingHead = (_incomingHead + 1) % INCOMING_STREAM_SIZE;
    --_incomingCount;
    return true;
  }

  void consumeIncomingBytes(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
      return;
    }

    for (size_t i = 0; i < len; ++i) {
      if (!pushIncomingByte(data[i])) {
        processIncomingStream();
        if (!pushIncomingByte(data[i])) {
          ++_stats.rxIncomingOverflowDrops;
          continue;
        }
      }
      ++_stats.rxBytes;
    }

    processIncomingStream();
  }

  void processIncomingStream() {
    uint8_t b = 0;
    while (popIncomingByte(b)) {
      const blekiss::KissConsumeResult r = _streamDecoder.consumeByte(b);
      if (r.decodeError) {
        ++_stats.rxDecodeErrors;
      }
      if (r.frameOverflow) {
        ++_stats.rxFrameOverflows;
      }
      if (r.frameReady) {
        if (_frameCallback != nullptr) {
          _frameCallback(r.frame, r.frameLen, _frameCallbackCtx);
        }
        ++_stats.rxFrames;
      }
    }
  }

  size_t encodeFrame(const uint8_t *payload, size_t len, uint8_t *out, size_t outMax, uint8_t cmdPort) {
    return blekiss::encodeFrame(payload, len, cmdPort, out, outMax);
  }

  bool enqueueEncodedFrame(const uint8_t *payload, size_t len, uint8_t cmdPort) {
    if (_queueCount >= OUTGOING_QUEUE_DEPTH) {
      ++_stats.txQueueFullDrops;
      return false;
    }

    QueueSlot &slot = _queue[_queueTail];
    const size_t encodedLen = encodeFrame(payload, len, slot.data, OUTGOING_FRAME_SIZE, cmdPort);
    if (encodedLen == 0) {
      ++_stats.txEncodeFailures;
      return false;
    }

    slot.len = encodedLen;
    _queueTail = (_queueTail + 1) % OUTGOING_QUEUE_DEPTH;
    ++_queueCount;
    ++_stats.txFramesQueued;
    return true;
  }

  void popQueueHead() {
    if (_queueCount == 0) {
      return;
    }

    _queue[_queueHead].len = 0;
    _queueHead = (_queueHead + 1) % OUTGOING_QUEUE_DEPTH;
    --_queueCount;
    _currentChunkOffset = 0;
  }

  void clearQueue() {
    for (size_t i = 0; i < OUTGOING_QUEUE_DEPTH; ++i) {
      _queue[i].len = 0;
    }
    _queueHead = 0;
    _queueTail = 0;
    _queueCount = 0;
    _currentChunkOffset = 0;
  }

  bool flushOneOutgoingChunk() {
    if (_queueCount == 0 || !canSend() || _rxChar == nullptr) {
      return false;
    }

    QueueSlot &slot = _queue[_queueHead];
    if (_currentChunkOffset >= slot.len) {
      popQueueHead();
      return false;
    }

    size_t attPayload = (_mtu > 3) ? static_cast<size_t>(_mtu - 3) : 20;
    if (attPayload == 0) {
      attPayload = 20;
    }

    const size_t remaining = slot.len - _currentChunkOffset;
    const size_t chunkLen = (remaining < attPayload) ? remaining : attPayload;

    _rxChar->setValue(slot.data + _currentChunkOffset, chunkLen);
    if (!_rxChar->notify()) {
      ++_stats.txNotifyFailures;
      return false;
    }

    _currentChunkOffset += chunkLen;
    ++_stats.txNotifyChunks;

    if (_currentChunkOffset >= slot.len) {
      ++_stats.txFramesSent;
      popQueueHead();
    }

    return true;
  }
};
