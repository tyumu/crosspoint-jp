#pragma once

#include <WiFi.h>

#include <memory>

#include "activities/Activity.h"
#include "activities/netdisplay/NetBle.h"

// X3DP receiver (BLE only): phone pushes 22B header + payload.
// ペイロードは受信しながらフレームバッファへ直接書く(52KBのステージングを持たない)。
class NetDisplayActivity final : public Activity {
 public:
  explicit NetDisplayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NetDisplay", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

 private:
  static constexpr int kWidth = 792;                              // 物理パネル
  static constexpr int kHeight = 528;
  static constexpr int kRowBytes = kWidth / 8;                     // 99
  static constexpr uint32_t kFrameBytes = kRowBytes * kHeight;     // 52272

  void startBle();
  void pollBle();
  void feed(const uint8_t* data, size_t len);
  void finishFrame();
  void sendVerb(char v);

  NetBle ble;

  uint8_t header[22] = {};
  int headerGot = 0;
  uint32_t payloadGot = 0;
  bool inFrame = false;
  bool haveFrame = false;
  bool refreshHalf = false;

  volatile bool refreshing = false;
  bool discarding = false;
  bool discardPending = false;
  uint32_t refreshUntilMs = 0;
  uint32_t refreshEndMs = 0;
  uint32_t lastByteMs = 0;

  bool phoneReady = false;
  bool greeted = false;
  uint32_t connectedAtMs = 0;
  uint32_t greetSentAtMs = 0;
  int greetRetries = 0;
  uint32_t confirmPressedAt = 0;

  uint32_t frames = 0;
  char status[80] = "starting";
};
