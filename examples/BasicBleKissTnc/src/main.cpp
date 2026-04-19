#include <Arduino.h>

#if defined(BLEKISS_EMPTY_BASELINE)

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("BLE KISS baseline build (empty app)");
}

void loop() {
  delay(1000);
}

#else

#include <esp_heap_caps.h>

#include "BleKissTnc.h"

// Low-RAM profile example:
// IN=512, DECODED=384, OUT_FRAME=384, OUT_QUEUE=3
using MyBleKiss = BleKissTnc<512, 384, 384, 3>;

static MyBleKiss::Config makeBleKissConfig() {
  MyBleKiss::Config cfg;
  cfg.preferredMtu = 185;
  return cfg;
}

MyBleKiss bleKiss(makeBleKissConfig());
static uint32_t gFreeHeapAtBoot = 0;

static void printHeapSnapshot(const char *tag) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minFreeHeap = ESP.getMinFreeHeap();
  const uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

  Serial.printf("[heap] %s free=%lu min=%lu largest=%lu deltaBoot=%ld\n",
                tag,
                (unsigned long)freeHeap,
                (unsigned long)minFreeHeap,
                (unsigned long)largestBlock,
                (long)(freeHeap - gFreeHeapAtBoot));
}

static void onKissFrame(const uint8_t *data, size_t len, void *ctx) {
  (void)ctx;
  if (len == 0) {
    return;
  }

  const uint8_t cmdPort = data[0];
  const uint8_t command = cmdPort & 0x0F;
  const uint8_t port = (cmdPort >> 4) & 0x0F;

  Serial.printf("KISS RX: cmd=%u port=%u payload=%u\n", command, port,
                (unsigned)((len > 0) ? (len - 1) : 0));

  if (command == 0x00 && len > 1) {
    // data[1..] contains raw AX.25 bytes from the app.
  }
}

static void onConnect(void *ctx) {
  (void)ctx;
  Serial.println("BLE connected");
  printHeapSnapshot("onConnect");
}

static void onDisconnect(void *ctx) {
  (void)ctx;
  Serial.println("BLE disconnected");
  printHeapSnapshot("onDisconnect");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  gFreeHeapAtBoot = ESP.getFreeHeap();
  printHeapSnapshot("boot");

  bleKiss.setFrameCallback(onKissFrame);
  bleKiss.setConnectCallback(onConnect);
  bleKiss.setDisconnectCallback(onDisconnect);

  // Keep notify draining conservative for low-RAM profile.
  bleKiss.setMaxNotifyChunksPerLoop(1);

  if (!bleKiss.begin()) {
    Serial.println("BLE KISS start failed");
    while (true) {
      delay(1000);
    }
  }
  printHeapSnapshot("afterBegin");

  Serial.printf("BleKiss fixed buffers: %u bytes\n",
                (unsigned)MyBleKiss::estimatedStaticBufferBytes());

  Serial.println("BLE KISS ready");
}

void loop() {
  bleKiss.loop();

  static uint32_t lastFrameMs = 0;
  if (millis() - lastFrameMs >= 3000) {
    lastFrameMs = millis();

    // Example AX.25 bytes for demo traffic.
    const uint8_t testAx25[] = {0x82, 0xA0, 0xA4, 0xA6, 0x40, 0x40, 0x60};
    if (bleKiss.canSend()) {
      if (!bleKiss.sendDataFrame(testAx25, sizeof(testAx25), 0)) {
        Serial.println("TX enqueue failed (queue full or frame too large)");
      }
    } else {
      Serial.println("TX skipped (not connected or notify not subscribed)");
    }
  }

  static uint32_t lastStatsMs = 0;
  if (millis() - lastStatsMs >= 10000) {
    lastStatsMs = millis();
    const MyBleKiss::Stats &s = bleKiss.stats();

    Serial.printf("stats: rxFrames=%lu decodeErr=%lu inDrop=%lu txQueued=%lu txSent=%lu q=%u/%u\n",
                  (unsigned long)s.rxFrames,
                  (unsigned long)s.rxDecodeErrors,
                  (unsigned long)s.rxIncomingOverflowDrops,
                  (unsigned long)s.txFramesQueued,
                  (unsigned long)s.txFramesSent,
                  (unsigned)bleKiss.outgoingQueueCount(),
                  (unsigned)bleKiss.outgoingQueueCapacity());
    printHeapSnapshot("periodic");
  }
}

#endif
