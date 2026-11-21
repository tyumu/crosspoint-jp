#include "activities/netdisplay/NetBle.h"

#include <Logging.h>
#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>

namespace {
constexpr char kServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kRxUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kTxUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

NetBle* gBle = nullptr;
NimBLEServer* gServer = nullptr;
NimBLECharacteristic* gTx = nullptr;
NimBLECharacteristic* gRx = nullptr;

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    LOG_INF("X3DP-BLE", "central connected, mtu=%u bonded=%d", info.getMTU(),
            static_cast<int>(info.isBonded()));
    if (gBle) gBle->onConnect();
    // 接続中も広告を続けて2台目を待つ必要はないので止める
    NimBLEDevice::getAdvertising()->stop();
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    LOG_INF("X3DP-BLE", "central disconnected (reason %d)", reason);
    if (gBle) gBle->onDisconnect();
    if (gServer) gServer->startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& info) override {
    const auto& v = ch->getValue();
    if (gBle) gBle->pushFromIsr(reinterpret_cast<const uint8_t*>(v.data()), v.size());
  }
};
}  // namespace

void NetBle::onConnect() { connCount++; }
void NetBle::onDisconnect() { if (connCount > 0) connCount--; }

void NetBle::pushFromIsr(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  portENTER_CRITICAL_SAFE(&gMux);
  for (size_t i = 0; i < len; i++) {
    const size_t next = (head + 1) % kRingSize;
    if (next == tail) {           // full: 古いデータを捨てる
      tail = (tail + 1) % kRingSize;
      dropped++;
    }
    ring[head] = data[i];
    head = next;
  }
  portEXIT_CRITICAL_SAFE(&gMux);
}

size_t NetBle::available() const {
  const size_t h = head;
  const size_t t = tail;
  return (h >= t) ? (h - t) : (kRingSize - t + h);
}

size_t NetBle::read(uint8_t* dst, size_t len) {
  size_t n = 0;
  portENTER_CRITICAL_SAFE(&gMux);
  while (n < len && tail != head) {
    dst[n++] = ring[tail];
    tail = (tail + 1) % kRingSize;
  }
  portEXIT_CRITICAL_SAFE(&gMux);
  return n;
}

bool NetBle::send(const uint8_t* data, size_t len) {
  if (!connected() || gTx == nullptr) return false;
  size_t sent = 0;
  while (sent < len) {
    const size_t chunk = (len - sent > 244) ? 244 : (len - sent);
    gTx->notify(data + sent, chunk);
    sent += chunk;
    // notify バッファが詰まるので少し待つ
    if ((sent % (244 * 4)) == 0) delay(2);
  }
  return true;
}

bool NetBle::begin(const char* name) {
  if (running) return true;
  gBle = this;
  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);
  // Smart Lock の「信頼できるデバイス」に登録してもらうためボンディングを有効化。
  // Just Works(MITMなし)なので画面なしデバイスでもペアリングできる。
  NimBLEDevice::setSecurityAuth(true /*bonding*/, false /*mitm*/, true /*sc*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());
  NimBLEService* svc = gServer->createService(kServiceUuid);
  // WRITE_ENC: 未ペアリングなら書き込み時にOSがペアリングを促す
  gRx = svc->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                                                    NIMBLE_PROPERTY::WRITE_ENC);
  gRx->setCallbacks(new RxCallbacks());
  gTx = svc->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kServiceUuid);
  adv->setName(name);
  adv->start();
  running = true;
  LOG_INF("X3DP-BLE", "advertising as '%s' free heap %u", name, static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

void NetBle::end() {
  if (!running) return;
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::deinit(true);
  gServer = nullptr;
  gTx = nullptr;
  gRx = nullptr;
  gBle = nullptr;
  running = false;
  connCount = 0;
}
