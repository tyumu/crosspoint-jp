# X3DP — Net Display(スマホ画面を X3 の外部ディスプレイにする)

このブランチ(`x3dp`)で追加した機能の説明です。X3 を Android スマホの外部モニタ + 物理ボタン付き
ページ送り機として使います。スマホ側アプリは [tyumu/x3dp-android](https://github.com/tyumu/x3dp-android)。

## フォーク元 / 系譜

このリポジトリは **2段のフォーク**です。差分は `x3dp` ブランチにのみ含まれ、元の著作権表示と
MIT ライセンス(`LICENSE`)はそのまま保持しています。

| 位置 | リポジトリ | 内容 |
| --- | --- | --- |
| 本家 | [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) | CrossPoint 本体(MIT, © 2025 CrossPoint Reader organization) |
| 日本語フォーク | [zrn-ns/crosspoint-jp](https://github.com/zrn-ns/crosspoint-jp) | 縦書き・日本語フォント・青空文庫など(MIT, © 2025 Dave Allie / aBER) |
| **このブランチ** | `tyumu/crosspoint-jp` の `x3dp` ブランチ | Net Display(下記) |

ベースは crosspoint-jp のコミット `e38baec`(README に動的壁紙・読書進捗・UIテーマ Vega を追記した時点)。

## 追加したもの

- ホームメニュー先頭に **Net Display** を追加(`HomeActivity` / `ActivityManager`)
- `src/activities/netdisplay/`
  - `NetDisplayActivity.*` — 受信ループ・フレーム検証・描画(`render()` 内で framebuffer へ memcpy)
  - `NetBle.*` — BLE ペリフェラル(GATT サーバ)。Nordic UART Service 互換
- `platformio.ini` — NimBLE ペリフェラル有効化(sdkconfig)と `NimBLE-Arduino` 追加、開発ビルドの版表記
- 内蔵 OpenDyslexic フォントの削除(フラッシュを約 800KB 空けるため。実測 -804KB)

## 動作

1. ホーム → `Net Display` を開くと **モード選択なしで即 BLE 待ち受け**(`ble X3DP-<MAC>` を表示)
2. スマホアプリが接続して制御バイト `K` を送ると、X3 が `W` を返して 1 枚要求
3. 受信した 22B ヘッダ + 1bit フレームを `renderer.getFrameBuffer()` に `memcpy` し、
   `displayBuffer(FAST_REFRESH)` で表示
4. X3 のボタン → verb(`N`/`P`/`R`/`0x10+id`)をスマホへ送信
5. Net Display を抜けると保存済みの Wi-Fi 資格情報で Wi-Fi を復帰(OTA・ファイル転送のため)

### プロトコル

フレームは 22 バイトのヘッダ + ペイロード(全リトルエンディアン)。

```
0  4B  "X3DP"      4  1B  version=1    5  1B  mode (0=1bit, 1=2bpp)
6  2B  width=792   8  2B  height=528
10 4B  seq(下位24bit) + flags(上位8bit)
14 4B  length (1bit = 52272)
18 4B  crc32(payload)
22 ... payload: row-major, 99B/row, MSB first, bit1=白
```

- パネルは物理 792×528。論理 528×792 からの回転は**送信側**で行い、ファームはバイト列を
  そのまま framebuffer へ置く(ファーム側で回すと `copyBufferToRegion()` の都合で化ける)。
- BLE は service `6E400001-…` / RX(write) `6E400002-…` / TX(notify) `6E400003-…`。
  MTU 517、送信側の 1 書き込みは 244B 以下。
- 制御バイト(`K` = スマホ準備完了、`W`/`N`/`P`/`R`)と再送ルールの詳細は
  [アプリ側 docs/protocol.md](https://github.com/tyumu/x3dp-android/blob/main/docs/protocol.md) を参照。

### 詰まりどころ(実装メモ)

- **受信中は描画しない**。半端なバッファを焼くとコントローラ内部の previous バッファが壊れ、
  以降の差分更新がズレ続ける。
- **`displayBuffer()` から戻ってもパネルは走査中**。`renderer.waitRefreshComplete()` までに
  届いたフレームは捨て、落ち着いてから `R` で再送させる(連打しても最後の絵だけが正しく出る)。
- **BLE と Wi-Fi は同時に動かせない**(ヒープ不足)。BLE 中は `WiFi.mode(WIFI_OFF)`。
  広告名に使う MAC は Wi-Fi を止める前に読む(止めた後は `00:00:00:00:00:00` になる)。
- `render()` の中で `R` を送らない(破棄ガードの時刻を立てた直後なので永久に固まる)。

## 個人設定 (`.env`)

環境ごとに変わる値はリポジトリに入れず、直下の `.env` から焼き込みます。
`.env.template` をコピーして作ってください(**`.env` は .gitignore 済み**、環境変数が優先)。

| key | 用途 | 未設定時 |
| --- | --- | --- |
| `X3DP_OTA_LATEST_URL` | 安定版の問い合わせ先(JSON) | 公開 GitHub の `crosspoint-jp/releases/latest` |
| `X3DP_OTA_LIST_URL` | RC/Dev 用のリリース一覧 | 公開 GitHub の `releases?per_page=10` |
| `X3DP_VERSION` | 設定画面に出る版表記 | `scripts/git_branch.py` が git から生成 |

`scripts/x3dp_env.py`(PlatformIO pre script)が `.env` を読んで `-D` に変換します
(`src/network/OtaUpdater.cpp` 側は `#ifndef` の既定値を持つ)。

## ビルド

```bash
git submodule update --init --recursive
pio run -e default            # 生成物: .pio/build/default/firmware.bin
```

- PlatformIO + pioarduino platform(ESP32-C3)。`platformio.local.ini` は不要(無ければ無視されます)
- pve など Python 3.14 環境で venv が汚れる場合: `env -u PYTHONPATH -u PYTHONHOME pio run -e default`
- 版表記は `platformio.ini` の `-DCROSSPOINT_VERSION=\"2.5.0-jp3\"`。焼いたビルドを設定画面で判別するため、
  ビルドごとに変えてください

## 書き込み

- **初回は SD カード**: 生成した `.bin` をカードのルートへ置き、設定 → System → "SD Card Firmware Update"、
  または電源 OFF から **Up を押したまま Power** でリカバリ起動
- 2 回目以降は **Wi-Fi モード**(Net Display を抜けて Wi-Fi に戻した状態)で本体の OTA を使用
- 自前の配信サーバを使う場合は `.env` の `X3DP_OTA_LATEST_URL` / `X3DP_OTA_LIST_URL` を設定してから
  ビルドしてください(未設定なら公開 GitHub の Releases を見ます)

## ライセンス

MIT。本家 crosspoint-reader(© 2025 CrossPoint Reader organization)と crosspoint-jp
(© 2025 Dave Allie / aBER)の MIT ライセンスを継承します。詳細は `LICENSE`。
