# Broadcast Sink - コード構造説明書

## 1. 概要

Broadcast Sink は、broadcast_source が Periodic Advertising（PA）に格納して送信するカスタムデータを受信し、RTT（SEGGER J-Link Real-Time Transfer）に表示するファームウェアアプリケーションである。

BLE Auracast の BIS 音声ストリームの受信と並行して、PA 内の Vendor-Specific AD element（`0xFF`）からカスタムデータを抽出する。

### ビルド方法

```
# Audio DK
west build --build-dir build_sink --pristine \
  --board nrf5340_audio_dk/nrf5340/cpuapp \
  -- -DEXTRA_CONF_FILE=broadcast_sink/overlay-broadcast_sink.conf

# nRF5340 DK
west build --build-dir build_sink_dk --pristine \
  --board nrf5340dk/nrf5340/cpuapp \
  -- -DEXTRA_CONF_FILE=broadcast_sink/overlay-broadcast_sink.conf
```


## 2. ファイル構成

```
broadcast_sink/
├── main.c                          # エントリポイント + PA データ受信
├── CMakeLists.txt                  # ビルド定義
└── overlay-broadcast_sink.conf     # Kconfig overlay
```

### 既存モジュールとの関係

```
broadcast_sink/main.c
  ├── src/bluetooth/bt_stream/broadcast/broadcast_sink.c   ← BIS 音声同期
  ├── src/bluetooth/bt_management/bt_mgmt_scan_for_broadcast.c  ← スキャン + PA sync
  ├── src/audio/le_audio_rx.c                              ← 音声受信パス
  └── src/core/pa_protocol.h                               ← PA_MAX_DATA_SIZE 定数
```


## 3. main.c の構造

### 3.1 機能ブロック

main.c は以下の 5 つの機能ブロックから構成される。

| ブロック | 行範囲 | 役割 |
|----------|--------|------|
| PA データ受信 | 74-107 | PA レポートから VS AD element を抽出し RTT に表示 |
| ボタン処理 | 109-208 | 再生/停止、音量、ブロードキャスト切替 |
| LE Audio イベント処理 | 210-342 | ストリーミング状態管理、コーデック設定、同期喪失処理 |
| BT 管理イベント処理 | 344-436 | PA sync、PA sync lost、ブロードキャストコード受信 |
| 初期化（`main()`） | 578-634 | ペリフェラル、BT、音声、zbus の初期化とスキャン開始 |

### 3.2 zbus サブスクライバスレッド

main.c は 3 つの専用スレッドで zbus イベントを購読する。

| スレッド名 | 購読チャネル | 処理内容 |
|------------|-------------|----------|
| `BUTTON_MSG_SUB` | `button_chan` | ボタン押下イベント → 再生/停止/音量制御 |
| `LE_AUDIO_MSG_SUB` | `le_audio_chan` | 音声ストリーミング状態変化 → 音声システム制御 + LED |
| `BT_MGMT_MSG_SUB` | `bt_mgmt_chan` | PA sync/lost イベント → BAP シンク制御 |


## 4. PA データ受信の仕組み

### 4.1 コールバック登録

Zephyr BT スタックは `bt_le_per_adv_sync_cb` コールバック構造体をリンクリストで管理しており、複数の登録が可能である。main.c では PA データ受信専用のコールバックを登録する。

```c
static struct bt_le_per_adv_sync_cb pa_data_sync_cbs = {
    .recv = pa_recv_cb,
};

// main() 内で登録
bt_le_per_adv_sync_cb_register(&pa_data_sync_cbs);
```

このコールバックは `bt_mgmt_scan_for_broadcast.c` が登録する `.synced` / `.term` コールバックとは独立して動作する。両方が同一の PA sync に対して呼び出される。

### 4.2 受信フロー

```
BLE Controller (cpunet)
  └── PA レポート受信
      └── bt_le_per_adv_sync_cb.recv 発火
          └── pa_recv_cb()
              └── bt_data_parse(buf, pa_data_parse_cb, NULL)
                  ├── AD Type ≠ 0xFF → return true （次の AD element へ）
                  └── AD Type = 0xFF （VS）
                      ├── 前回データと比較
                      ├── 変化あり → LOG_HEXDUMP_INF() で RTT に表示
                      └── return false （パース終了）
```

### 4.3 データ変化検出

PA レポートは PA インターバル（通常 100ms 程度）ごとに受信されるため、同一データが繰り返し到着する。RTT の出力が洪水状態にならないよう、前回受信データとの比較を行い、変化がある場合のみ hexdump を出力する。

```c
static uint8_t prev_pa_data[PA_MAX_DATA_SIZE];
static uint8_t prev_pa_data_len;
static bool pa_data_received_once;
```

PA sync 確立時および同期喪失時に `pa_data_received_once = false` にリセットし、再同期後の最初のデータを確実に表示する。

### 4.4 RTT 出力例

```
[00:00:05.123,456] <inf> main: PA custom data received (12 bytes)
[00:00:05.123,789] <inf> main: PA data
                                48 65 6c 6c 6f 20 57 6f  72 6c 64 21              |Hello Wo rld!    |
```


## 5. PA データフォーマット（Source / Sink 共通）

broadcast_source が設定する PA データは以下の構造を持つ。

```
PA PDU ペイロード (最大 252 bytes)
┌─────────────────────────────────────────────────────────┐
│ AD Element 0: BASE (音声 metadata)                       │
│   Length (1B) | Type (1B) | BASE Data (可変)              │
├─────────────────────────────────────────────────────────┤
│ AD Element 1: Vendor-Specific (カスタムデータ)            │
│   Length (1B) | Type=0xFF (1B) | Data (可変)              │
└─────────────────────────────────────────────────────────┘
```

Sink 側では `bt_data_parse()` が AD element を順次走査し、`type == 0xFF` に一致する要素のペイロードを抽出する。


## 6. 初期化フロー

`main()` の実行順序を以下に示す。

```
main()
  1. peripherals_init()              ペリフェラル初期化
  2. fw_info_app_print()             FW 情報出力
  3. bt_mgmt_init()                  Bluetooth 管理初期化
  4. audio_system_init()             音声システム初期化
  5. bt_le_per_adv_sync_cb_register()  ★ PA データ受信コールバック登録
  6. zbus_subscribers_create()       zbus サブスクライバスレッド生成
  7. zbus_link_producers_observers() zbus チャネル接続
  8. le_audio_rx_init()              音声受信パス初期化
  9. broadcast_sink_enable()         BAP シンク有効化（BIS 音声同期）
 10. [分岐]
     ├── Scan Delegator 有効時:
     │   ├── bt_mgmt_scan_delegator_init()
     │   ├── bt_r_and_c_init()
     │   ├── ext_adv_populate()      拡張アドバタイジング構成
     │   └── bt_mgmt_adv_start()     アドバタイジング開始
     └── Scan Delegator 無効時:
         └── bt_mgmt_scan_start()    ブロードキャスト検索開始
```

### broadcast_source との初期化の違い

| 項目 | broadcast_source | broadcast_sink |
|------|-----------------|----------------|
| PA データ方向 | 送信（`bt_le_per_adv_set_data`） | 受信（`bt_le_per_adv_sync_cb.recv`） |
| UART | 使用（PC からコマンド受信） | 不使用 |
| PA Service | あり（オーケストレータ） | なし（コールバックで直接処理） |
| 音声 | BIS エンコード + 送信 | BIS デコード + 受信 |
| スキャン | なし（アドバタイズ側） | あり（ブロードキャスト検索） |


## 7. イベント処理詳細

### 7.1 BT 管理イベント（`bt_mgmt_msg_sub_thread`）

| イベント | 処理 |
|----------|------|
| `BT_MGMT_PA_SYNCED` | PA 同期確立。`broadcast_sink_pa_sync_set()` で BAP シンクを生成。PA データ変化検出をリセット |
| `BT_MGMT_PA_SYNC_LOST` | PA 同期喪失。再スキャン開始。PA データ変化検出をリセット |
| `BT_MGMT_BROADCAST_SINK_DISABLE` | BAP シンク無効化 |
| `BT_MGMT_BROADCAST_CODE_RECEIVED` | 暗号化ブロードキャストのコード設定 |

### 7.2 LE Audio イベント（`le_audio_msg_sub_thread`）

| イベント | 処理 |
|----------|------|
| `LE_AUDIO_EVT_STREAMING` | 音声システム開始、LED 点滅 |
| `LE_AUDIO_EVT_NOT_STREAMING` | 音声システム停止、LED 点灯 |
| `LE_AUDIO_EVT_CONFIG_RECEIVED` | サンプルレート設定、プレゼンテーション遅延設定 |
| `LE_AUDIO_EVT_SYNC_LOST` | 音声停止、PA sync 削除、再スキャン開始 |
| `LE_AUDIO_EVT_NO_VALID_CFG` | 有効な設定なし → シンク無効化 |

### 7.3 ボタンイベント（`button_msg_sub_thread`）

| ボタン | 動作 |
|--------|------|
| PLAY_PAUSE | ストリーミング開始/停止トグル |
| VOLUME_UP | 音量アップ |
| VOLUME_DOWN | 音量ダウン |
| BUTTON_5 | ブロードキャスト名の切替（通常名 ↔ 代替名） |


## 8. 全体シーケンス

```
     Sink                                 Source
      |                                     |
      |--- bt_mgmt_scan_start() ---------->|  ブロードキャスト検索
      |         (スキャン中...)              |
      |<-- scan_recv_cb() [名前一致] -------|  EXT_ADV 受信
      |                                     |
      |--- bt_le_per_adv_sync_create() --->|  PA 同期作成
      |                                     |
      |<-- pa_synced_cb() ----------------- |  PA 同期確立
      |    → BT_MGMT_PA_SYNCED (zbus)      |
      |    → broadcast_sink_pa_sync_set()   |
      |                                     |
      |<== PA レポート (BASE + VS AD) ======|  PA データ受信（繰り返し）
      |    → pa_recv_cb()                   |
      |    → bt_data_parse()                |
      |    → VS(0xFF) 抽出                  |
      |    → LOG_HEXDUMP_INF() [変化時]     |
      |                                     |
      |<-- base_recv_cb() [BAP] -----------|  BASE 受信 → コーデック設定
      |<-- syncable_cb() [BAP] ------------|  BIS 同期可能
      |--- bt_bap_broadcast_sink_sync() -->|  BIS 同期開始
      |                                     |
      |<== BIS 音声データ ==================|  音声ストリーミング
      |    → stream_recv_cb()               |
      |    → audio_datapath 処理            |
      |                                     |
```


## 9. Kconfig 設定（overlay-broadcast_sink.conf）

### 主要設定項目

| カテゴリ | 設定 | 値 | 説明 |
|----------|------|-----|------|
| デバイス種別 | `CONFIG_AUDIO_DEV` | 1 (HEADSET) | ヘッドセットとしてコンパイル |
| BT ロール | `CONFIG_BT_OBSERVER` | y | スキャナ（オブザーバ）有効 |
| BT ロール | `CONFIG_BT_PERIPHERAL` | y | ペリフェラル有効（Scan Delegator 用） |
| BAP | `CONFIG_BT_BAP_BROADCAST_SINK` | y | BAP ブロードキャストシンク有効 |
| ISO | `CONFIG_BT_ISO_SYNC_RECEIVER` | y | ISO 同期受信有効 |
| PA sync | `CONFIG_BT_PER_ADV_SYNC_MAX` | 2 | 最大 PA 同期数 |
| Scan Delegator | `CONFIG_BT_BAP_SCAN_DELEGATOR` | y | スキャンデリゲータ有効 |
| RTT | `CONFIG_LOG_BACKEND_RTT_BUFFER` | 1 | ログ用 RTT バッファ（チャネル 1） |

### RTT バッファチャネル分離

Shell と Log が同一 RTT チャネルを使用すると `BUILD_ASSERT` で競合エラーとなるため、ログバックエンドをチャネル 1 に割り当てている。

```
RTT チャネル 0: Shell (CONFIG_SHELL_BACKEND_RTT_BUFFER=0, デフォルト)
RTT チャネル 1: Log   (CONFIG_LOG_BACKEND_RTT_BUFFER=1)
```
