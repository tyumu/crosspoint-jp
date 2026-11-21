#include "activities/netdisplay/NetDisplayActivity.h"

#include <Logging.h>

#include <cstring>

#include "HalDisplay.h"
#include "WifiCredentialStore.h"
#include "fontIds.h"

namespace {
constexpr const char* kMagic = "X3DP";
}

void NetDisplayActivity::onEnter() {
  Activity::onEnter();
  startBle();
}

void NetDisplayActivity::onExit() {
  // BLEでWiFiを止めているので、抜けるときは元のWiFiへ戻す(OTA等のため)
  WiFi.mode(WIFI_STA);
  const std::string& last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) {
    const auto* cred = WIFI_STORE.findCredential(last);
    if (cred != nullptr) {
      WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
    }
  }
  ble.end();
  Activity::onExit();
}

void NetDisplayActivity::startBle() {
  char macBuf[20];
  {
    const String macStr = WiFi.macAddress();
    snprintf(macBuf, sizeof(macBuf), "%s", macStr.c_str());
  }
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  char name[24];
  snprintf(name, sizeof(name), "X3DP-%s", macBuf);
  if (!ble.begin(name)) {
    snprintf(status, sizeof(status), "ble init failed");
    requestUpdate();
    return;
  }
  snprintf(status, sizeof(status), "ble %s", name);
  LOG_INF("X3DP", "ble advertising as %s heap %u", name, static_cast<unsigned>(ESP.getFreeHeap()));
  requestUpdate();
}

void NetDisplayActivity::sendVerb(char v) {
  if (ble.connected()) ble.send(reinterpret_cast<const uint8_t*>(&v), 1);
}

void NetDisplayActivity::feed(const uint8_t* data, size_t len) {
  lastByteMs = millis();
  size_t i = 0;
  while (i < len) {
    if (!inFrame) {
      const uint8_t b = data[i++];
      if (headerGot == 0 && b == 0x4B) {   // 'K' = スマホ準備完了
        phoneReady = true;
        continue;
      }
      header[headerGot++] = b;
      if (headerGot < 4) {
        if (memcmp(header, kMagic, headerGot) != 0) headerGot = 0;
        continue;
      }
      if (memcmp(header, kMagic, 4) != 0) {
        memmove(header, header + 1, headerGot - 1);
        headerGot--;
        continue;
      }
      if (headerGot < static_cast<int>(sizeof(header))) continue;

      const uint8_t version = header[4];
      const uint8_t fmode = header[5];
      const uint16_t w = static_cast<uint16_t>(header[6] | (header[7] << 8));
      const uint16_t h = static_cast<uint16_t>(header[8] | (header[9] << 8));
      const uint32_t seqField = static_cast<uint32_t>(header[10]) | (static_cast<uint32_t>(header[11]) << 8) |
                                (static_cast<uint32_t>(header[12]) << 16) | (static_cast<uint32_t>(header[13]) << 24);
      const uint32_t lenField = static_cast<uint32_t>(header[14]) | (static_cast<uint32_t>(header[15]) << 8) |
                                (static_cast<uint32_t>(header[16]) << 16) | (static_cast<uint32_t>(header[17]) << 24);
      if (version != 1 || w != kWidth || h != kHeight || fmode != 0 || lenField != kFrameBytes) {
        snprintf(status, sizeof(status), "reject v%u m%u %ux%u len%u", version, fmode, w, h,
                 static_cast<unsigned>(lenField));
        memmove(header, header + 1, sizeof(header) - 1);
        headerGot = sizeof(header) - 1;
        continue;
      }
      refreshHalf = ((seqField >> 24) & 0x01) != 0;
      headerGot = 0;
      inFrame = true;
      payloadGot = 0;
      if (refreshing || millis() < refreshUntilMs || millis() < refreshEndMs + 400) {
        discarding = true;
        discardPending = true;
      } else {
        discarding = false;
      }
      continue;
    }

    const uint32_t remain = kFrameBytes - payloadGot;
    const size_t n = (len - i < remain) ? (len - i) : remain;
    if (!discarding) {
      uint8_t* fb = renderer.getFrameBuffer();
      if (fb != nullptr) {
        memcpy(fb + payloadGot, data + i, n);
      }
    }
    payloadGot += static_cast<uint32_t>(n);
    i += n;
    if (payloadGot >= kFrameBytes) {
      if (discarding) {
        inFrame = false;
        payloadGot = 0;
      } else {
        finishFrame();
      }
    }
  }
}

void NetDisplayActivity::finishFrame() {
  inFrame = false;
  payloadGot = 0;
  haveFrame = true;
  frames++;
  snprintf(status, sizeof(status), "frame %u", static_cast<unsigned>(frames));
  requestUpdate();
}

void NetDisplayActivity::pollBle() {
  if (ble.takeDropped() > 0) {
    inFrame = false;
    payloadGot = 0;
    const char r = 'R';
    ble.send(reinterpret_cast<const uint8_t*>(&r), 1);
    snprintf(status, sizeof(status), "overflow -> resend");
  }
  uint8_t buf[1024];
  while (ble.available() > 0) {
    const size_t got = ble.read(buf, sizeof(buf));
    if (got == 0) break;
    feed(buf, got);
  }
}

void NetDisplayActivity::loop() {
  pollBle();

  // 受信が途切れたら中断(半端なバッファを焼かない)
  if (inFrame && millis() - lastByteMs > 2000) {
    inFrame = false;
    payloadGot = 0;
    haveFrame = false;
    const char r = 'R';
    if (ble.connected()) ble.send(reinterpret_cast<const uint8_t*>(&r), 1);
    snprintf(status, sizeof(status), "frame timeout -> resend");
    requestUpdate();
  }

  // リフレッシュ中に捨てたフレームがあれば、落ち着いてから再送を要求する
  if (discardPending && millis() > refreshEndMs + 600 && millis() > refreshUntilMs + 600) {
    discardPending = false;
    const char r = 'R';
    if (ble.connected()) ble.send(reinterpret_cast<const uint8_t*>(&r), 1);
    snprintf(status, sizeof(status), "resend requested");
  }

  const bool up = ble.connected();
  if (up) {
    if (connectedAtMs == 0) connectedAtMs = millis();
    const bool ready = phoneReady || (millis() - connectedAtMs > 2500);
    if (!greeted && ready) {
      greeted = true;
      greetSentAtMs = millis();
      sendVerb('W');
      snprintf(status, sizeof(status), phoneReady ? "sent W (ready)" : "sent W (timeout)");
    } else if (greeted && frames == 0 && millis() - greetSentAtMs > 3000 && greetRetries < 9) {
      greetRetries++;
      greetSentAtMs = millis();
      sendVerb('W');
      snprintf(status, sizeof(status), "resend W (%d)", greetRetries);
    }
  } else {
    greeted = false;
    connectedAtMs = 0;
    greetRetries = 0;
    phoneReady = false;
    if (inFrame) {
      inFrame = false;
      payloadGot = 0;
      haveFrame = false;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmPressedAt = millis();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool longPress = confirmPressedAt != 0 && (millis() - confirmPressedAt) >= 700;
    confirmPressedAt = 0;
    sendVerb(longPress ? 'W' : static_cast<char>(0x10 + 3));
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    sendVerb('S');
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    sendVerb('N');
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    sendVerb('P');
    return;
  }
}

void NetDisplayActivity::render(RenderLock&&) {
  if (inFrame) return;   // 半端なバッファは焼かない
  if (haveFrame) {
    refreshing = true;
    refreshUntilMs = millis() + 400;
    renderer.displayBuffer(refreshHalf ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    refreshing = false;
    refreshEndMs = millis();
    return;
  }
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, 250, "X3DP receiver", true);
  renderer.drawCenteredText(UI_10_FONT_ID, 320, status, true);
  renderer.drawCenteredText(UI_10_FONT_ID, 380, "waiting for phone", true);
  renderer.displayBuffer();
}
