#pragma once

#include <cstddef>
#include <cstdint>

// BLE(GATT) でスマホから 22Bヘッダ+ペイロード を受け取るトランスポート。
// Nordic UART Service 互換の UUID を使うので、汎用BLEシリアルアプリでも確認できる。
//  - RX (write / write-no-response) : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
//  - TX (notify)                    : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
class NetBle {
 public:
  static constexpr size_t kRingSize = 16384;

  bool begin(const char* name);
  void end();
  bool isRunning() const { return running; }
  bool connected() const { return connCount > 0; }

  size_t available() const;
  size_t read(uint8_t* dst, size_t len);
  bool send(const uint8_t* data, size_t len);   // 244B ずつ notify に分割
  uint32_t droppedBytes() const { return dropped; }
  /** 取りこぼし回数を読んで0に戻す */
  uint32_t takeDropped() { const uint32_t d = dropped; dropped = 0; return d; }

  void pushFromIsr(const uint8_t* data, size_t len);   // コールバックから呼ぶ
  void onConnect();
  void onDisconnect();

 private:
  bool running = false;
  volatile int connCount = 0;
  volatile uint32_t dropped = 0;

  uint8_t ring[kRingSize] = {};
  volatile size_t head = 0;   // write index
  volatile size_t tail = 0;   // read index
};
