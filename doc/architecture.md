# PA Data Broadcaster - コード構造説明書

## 1. アプリケーション概要

PA Data Broadcaster は、BLE Auracast 音声ブロードキャスト（BIS）と Periodic Advertising（PA）によるデータ送信を両立するファームウェアアプリケーションである。

PC 上の制御アプリから UART 経由で受信したデータを PA パケットに格納し、BLE でブロードキャストする。音声データは Auracast 標準の BIS で送出しつつ、PA の空き領域にアプリケーションデータを動的に挿入できる。

### ターゲットボード

| ボード | 識別子 | 備考 |
|--------|--------|------|
| nRF5340 Audio DK | PCA10121 | CS47L63 コーデック搭載、SD カード対応 |
| nRF5340 DK | PCA10095 | USB オーディオ入力、I2S 経由 |

### ビルドシステム

- nRF Connect SDK (NCS) v3.1.0 / Zephyr RTOS 4.1.99
- Sysbuild によるデュアルコアビルド（cpuapp + ipc_radio on cpunet）

```
# Audio DK
west build --build-dir build --pristine \
  --board nrf5340_audio_dk/nrf5340/cpuapp \
  -- -DEXTRA_CONF_FILE=broadcast_source/overlay-broadcast_source.conf

# nRF5340 DK
west build --build-dir build_dk --pristine \
  --board nrf5340dk/nrf5340/cpuapp \
  -- -DEXTRA_CONF_FILE=broadcast_source/overlay-broadcast_source.conf
```


## 2. アーキテクチャ

本アプリケーションは **ヘキサゴナルアーキテクチャ（Ports & Adapters）** を採用している。

コアドメインロジックを純粋な C で記述し、プラットフォーム依存部分をポート（インターフェース）とアダプタ（実装）に分離することで、他プラットフォーム（STM32WBA55G 等）への移植性を確保している。

```
+-----------------------------------------------------------------+
|                        main.c (DI配線)                           |
|    アダプタ生成 → ポート組み立て → pa_service に注入              |
+-----------------------------------------------------------------+
         |               |               |           |         |
   +-----+-----+  +------+------+  +-----+-----+  +-+-+  +---+---+
   | port_uart  |  | port_ble_pa |  | port_timer |  |LED|  |Button |
   | (vtable)   |  | (vtable)    |  | (vtable)   |  |   |  |       |
   +-----+------+  +------+------+  +-----+------+  +-+-+  +---+---+
         |                |                |           |         |
   +-----+------+  +------+------+  +-----+------+  +-+---+ +--+----+
   | zephyr_uart |  | zephyr_ble  |  | zephyr_timer | |LED  | |Button|
   | _adapter    |  | _pa_adapter |  | _adapter     | |adapt| |adapt |
   +-------------+  +-------------+  +--------------+ +-----+ +------+
         |                |                |
   +-----------+    +-----------+    +-----------+
   | Zephyr    |    | Zephyr    |    | Zephyr    |
   | UART IRQ  |    | BT API    |    | k_timer   |
   +-----------+    +-----------+    +-----------+

+-----------------------------------------------------------------+
|                  pa_service (オーケストレータ)                    |
|   UART受信 → パース → 状態管理 → PA設定 → 応答送信               |
+----------------+------------------------+-----------------------+
                 |                        |
        +--------+--------+     +--------+--------+
        | pa_cmd_parser    |     | pa_data_manager  |
        | (パース/直列化)  |     | (状態マシン)     |
        +------------------+     +------------------+
```

### レイヤー構成

| レイヤー | ディレクトリ | プラットフォーム依存 | 役割 |
|----------|-------------|---------------------|------|
| Core | `src/core/` | なし | プロトコル定義、コマンドパーサ、状態マシン |
| Ports | `src/ports/` | なし | 関数ポインタ vtable によるインターフェース定義 |
| Adapters | `src/adapters/` | Zephyr 依存 | ポートインターフェースの Zephyr 向け実装 |
| App | `src/app/` | なし | コアとポートを接続するオーケストレータ |
| Entry | `broadcast_source/` | Zephyr 依存 | DI 配線、Auracast 初期化、イベントハンドリング |
| Platform | `src/audio/` `src/bluetooth/` 他 | Zephyr 依存 | 音声処理、BLE スタック、ドライバ等 |


## 3. ディレクトリ構成

```
pa_data_broadcaster/
├── CMakeLists.txt                      # トップレベルビルド定義
├── Kconfig / Kconfig.defaults          # 設定オプション
├── Kconfig.sysbuild                    # Sysbuild 設定
├── prj.conf                           # プロジェクト設定
│
├── boards/                             # ボード別設定
│   ├── nrf5340_audio_dk_nrf5340_cpuapp.conf
│   ├── nrf5340_audio_dk_nrf5340_cpuapp.overlay
│   ├── nrf5340dk_nrf5340_cpuapp.conf
│   └── nrf5340dk_nrf5340_cpuapp.overlay
│
├── broadcast_source/                   # エントリポイント
│   ├── main.c                          # DI配線 + Auracast初期化
│   ├── overlay-broadcast_source.conf   # ブロードキャスト固有設定
│   └── CMakeLists.txt
│
├── include/
│   └── zbus_common.h                   # zbus メッセージ型定義
│
├── src/
│   ├── core/                           # ★ 純粋C（移植可能）
│   │   ├── pa_protocol.h               #   プロトコル定数・構造体
│   │   ├── pa_cmd_parser.h / .c        #   コマンドパース・直列化
│   │   └── pa_data_manager.h / .c      #   PA送信状態マシン
│   │
│   ├── ports/                          # ★ インターフェース定義（移植可能）
│   │   ├── port_uart.h                 #   UART 送受信
│   │   ├── port_ble_pa.h               #   BLE PA データ設定
│   │   ├── port_timer.h                #   ワンショットタイマー
│   │   ├── port_log.h                  #   ログ出力
│   │   ├── port_os.h                   #   OS プリミティブ
│   │   ├── port_led.h                  #   LED 制御
│   │   └── port_button.h              #   ボタン入力
│   │
│   ├── adapters/                       # Zephyr 固有アダプタ
│   │   ├── zephyr_uart_adapter.h / .c
│   │   ├── zephyr_ble_pa_adapter.h / .c
│   │   ├── zephyr_timer_adapter.h / .c
│   │   ├── zephyr_os_adapter.h / .c
│   │   ├── zephyr_led_adapter.h / .c
│   │   ├── zephyr_button_adapter.h / .c
│   │   └── zephyr_log.h               #   PA_LOG → Zephyr LOG マッピング
│   │
│   ├── app/                            # ★ アプリケーション層（移植可能）
│   │   ├── pa_service.h / .c           #   オーケストレータ
│   │   └── CMakeLists.txt
│   │
│   ├── audio/                          # 音声処理モジュール
│   ├── bluetooth/                      # BLE スタック管理
│   ├── drivers/                        # CS47L63 コーデックドライバ
│   ├── modules/                        # LED, ボタン, SD カード等
│   └── utils/                          # ペリフェラル初期化, エラーハンドラ
│
└── sysbuild/
    └── ipc_radio/prj.conf              # cpunet 側の設定
```


## 4. 各モジュール詳細

### 4.1 Core ドメイン（`src/core/`）

Zephyr を含む一切の外部依存がない純粋 C。使用するヘッダは `<stdint.h>`, `<string.h>`, `<stdbool.h>` のみ。

#### `pa_protocol.h` — プロトコル定義

UART コマンドプロトコルの全定数と構造体を定義する。

```
OpCode 一覧:
  0x00  PA Status Get Request    (PC → FW)
  0x01  PA Status Get Response   (FW → PC)
  0x10  PA Data Send Request     (PC → FW)
  0x11  PA Data Send Response    (FW → PC)
  0x12  PA Data Send Complete    (FW → PC)

Result コード（0x11 応答で使用）:
  0x00  SUCCESS
  0x01  BUSY
  0x02  INVALID_SID
  0x03  DATA_TOO_LARGE
  0x04  NOT_READY

定数:
  PA_MAX_ADV_SETS  = 4     最大アドバタイジングセット数
  PA_MAX_DATA_SIZE = 252   PA データ最大バイト数
```

コマンド構造体:

```c
struct pa_status_get_rsp {
    uint8_t num_advertising;
    uint8_t adv_sid[PA_MAX_ADV_SETS];
    uint8_t pa_available_bytes[PA_MAX_ADV_SETS];
};

struct pa_data_send_req {
    uint8_t advertising_sid;
    uint8_t data_length;
    uint8_t data[PA_MAX_DATA_SIZE];
};
```

#### `pa_cmd_parser` — コマンドパーサ / シリアライザ

ステートレスな関数群。UART フレームのバイト列と構造体の相互変換を担当する。

| 関数 | 方向 | 説明 |
|------|------|------|
| `pa_cmd_parse()` | バイト列→構造体 | 受信フレームを OpCode + ペイロードに分解 |
| `pa_cmd_serialize_status_rsp()` | 構造体→バイト列 | Status Get Response (0x01) を直列化 |
| `pa_cmd_serialize_data_send_rsp()` | 値→バイト列 | Data Send Response (0x11) を直列化 |
| `pa_cmd_serialize_data_send_complete()` | なし→バイト列 | Data Send Complete (0x12) を直列化 |

#### `pa_data_manager` — PA 送信状態マシン

PA データの送信ライフサイクルを管理する。ハードウェアには一切触れず、状態の検証と遷移のみを行う。

```
状態遷移:

  IDLE ──(submit)──→ PENDING ──(on_pa_interval)──→ COMPLETE ──(reset)──→ IDLE
   ↑                                                                       │
   └───────────────────────────────────────────────────────────────────────┘
```

| 関数 | 説明 |
|------|------|
| `pa_data_mgr_init()` | コンテキスト初期化、BAASOオーバーヘッド設定 |
| `pa_data_mgr_submit()` | データ検証・格納、IDLE→PENDING 遷移。状態が IDLE 以外なら BUSY を返す |
| `pa_data_mgr_on_pa_interval()` | PA インターバル経過通知、PENDING→COMPLETE 遷移 |
| `pa_data_mgr_reset()` | 状態を IDLE に戻す |
| `pa_data_mgr_available_bytes()` | PA で使用可能な残りバイト数を返す |


### 4.2 ポートインターフェース（`src/ports/`）

各ポートは関数ポインタの構造体（vtable）として定義された純粋 C ヘッダ。プラットフォーム固有の型への依存を持たない。

#### `port_uart.h` — UART 送受信ポート

```c
typedef void (*pa_uart_rx_cb_t)(const uint8_t *data, size_t len, void *user_data);

struct pa_port_uart {
    int (*init)(void *ctx, pa_uart_rx_cb_t rx_cb, void *user_data);
    int (*send)(void *ctx, const uint8_t *data, size_t len);
    void *ctx;
};
```

- `init`: コールバック登録と UART ペリフェラルの初期化
- `send`: バイト列を UART へ送信
- `rx_cb`: 完全なフレームを受信した際に呼び出されるコールバック

#### `port_ble_pa.h` — BLE Periodic Advertising ポート

```c
struct pa_port_ble_pa {
    int (*set_data)(void *ctx, uint8_t adv_sid,
                    const uint8_t *data, uint8_t data_len);
    int (*get_status)(void *ctx, uint8_t *num_adv,
                      struct pa_adv_info *info, uint8_t max_entries);
    bool (*is_ready)(void *ctx);
    void *ctx;
};
```

- `set_data`: 指定 SID の PA データを更新
- `get_status`: アクティブなアドバタイジングセットの一覧と空き容量を取得
- `is_ready`: BLE PA が使用可能な状態か確認

#### `port_timer.h` — ワンショットタイマーポート

```c
struct pa_port_timer {
    int (*start_oneshot)(void *ctx, uint32_t interval_ms,
                         pa_timer_cb_t cb, void *user_data);
    int (*stop)(void *ctx);
    void *ctx;
};
```

PA インターバル経過の検出に使用。PA データ設定後、約 100ms のワンショットタイマーを起動し、期限到来時にコールバックで完了通知を発行する。

#### `port_log.h` — ログポート

```c
PA_LOG_ERR(...)   // エラー
PA_LOG_WRN(...)   // 警告
PA_LOG_INF(...)   // 情報
PA_LOG_DBG(...)   // デバッグ
PA_LOG_MODULE_REGISTER(name)
```

マクロベースの抽象化。`PA_LOG_PLATFORM_HEADER` の定義によりプラットフォーム固有のログ実装にディスパッチされる。未定義の場合は全マクロが no-op となる。

#### `port_os.h` — OS プリミティブポート

```c
struct pa_port_work     // 遅延実行（ISR から安全に呼び出し可能）
struct pa_port_critical // クリティカルセクション（割り込みロック）
struct pa_port_sleep    // ミリ秒スリープ
```

#### `port_led.h` — LED ポート

```c
enum pa_led_id { PA_LED_CONN_STATUS, PA_LED_SYNC_STATUS, PA_LED_APP_STATUS };

struct pa_port_led {
    int (*on)(void *ctx, enum pa_led_id led);
    int (*off)(void *ctx, enum pa_led_id led);
    int (*blink)(void *ctx, enum pa_led_id led);
    void *ctx;
};
```

論理 LED ID を使用し、ボードごとの物理 LED マッピングはアダプタが吸収する。

#### `port_button.h` — ボタンポート

```c
enum pa_button_id { PA_BUTTON_PLAY_PAUSE, PA_BUTTON_VOLUME_UP,
                    PA_BUTTON_VOLUME_DOWN, PA_BUTTON_ACTION };

struct pa_port_button {
    int (*init)(void *ctx, pa_button_cb_t cb, void *user_data);
    void *ctx;
};
```

論理ボタン ID を使用し、GPIO ピンから論理 ID へのマッピングはアダプタが行う。


### 4.3 Zephyr アダプタ（`src/adapters/`）

各ポートインターフェースの Zephyr RTOS 向け実装。

#### `zephyr_uart_adapter` — UART1 IRQ 駆動

- **ペリフェラル**: `uart1`（TX=P1.9, RX=P1.8, 115200 baud）
- **受信**: ISR 内で 1 バイトずつ読み取り、`Command_Length` フレーミングで蓄積。フレーム完成時に `k_work_submit()` で ISR からスレッドコンテキストへ遷移してコールバックを呼び出す
- **送信**: `uart_poll_out()` による同期送信

```
受信フレーミング:
  1バイト目 = Command_Length → 残り Command_Length バイトを蓄積
  蓄積完了 → k_work 経由で pa_service_on_command() を呼び出し
```

#### `zephyr_ble_pa_adapter` — Periodic Advertising データ設定

- `bt_le_per_adv_set_data()` をラップ
- BASE AD element（既存の音声 metadata）と カスタム Vendor-Specific AD element を結合して PA データ全体を再設定する
- `ext_adv` ポインタは zbus の `BT_MGMT_EXT_ADV_WITH_PA_READY` イベントから取得

```
PA データ構成:
  [ BASE AD element (音声metadata) ] + [ VS AD element (カスタムデータ) ]
                                         ↑ UART から受信したデータ
```

#### `zephyr_timer_adapter` — k_timer ワンショット

- Zephyr の `k_timer` API で 100ms のワンショットタイマーを実装
- 期限到来時に `k_work` 経由でコールバックを呼び出し

#### `zephyr_led_adapter` — LED マッピング

- 論理 LED ID → ボード固有の物理 LED ID のマッピングテーブル `led_id_map[]` を保持
- 既存の `led.h` API（`led_on()`, `led_off()`, `led_blink()`）に委譲

#### `zephyr_button_adapter` — ボタン zbus サブスクライバ

- `zbus` の `button_chan` を購読する専用スレッドを起動
- GPIO ピン番号から論理ボタン ID へのマッピングを行い、コールバックで通知
- ボタン押下（`BUTTON_PRESS`）のみをフィルタ

#### `zephyr_log.h` — ログマッピング

`PA_LOG_*` マクロを Zephyr の `LOG_*` マクロに展開する。CMakeLists.txt で `PA_LOG_PLATFORM_HEADER="zephyr_log.h"` として設定。

#### `zephyr_os_adapter` — OS プリミティブ

- `pa_port_work` → Zephyr `k_work`
- `pa_port_critical` → `irq_lock()` / `irq_unlock()`
- `pa_port_sleep` → `k_msleep()`


### 4.4 アプリケーション層（`src/app/`）

#### `pa_service` — オーケストレータ

Core ドメインとポートを接続する中心的なモジュール。UART 受信コールバックとタイマーコールバックを起点として、全体のデータフローを制御する。

**保持するリソース:**

```c
struct pa_service {
    struct pa_port_uart *uart;       // UART ポート
    struct pa_port_ble_pa *ble_pa;   // BLE PA ポート
    struct pa_port_timer *timer;     // タイマーポート
    struct pa_data_context pa_ctx;   // データマネージャの状態
    uint8_t tx_buf[260];             // 送信バッファ
};
```

**初期化フロー:**

```
pa_service_init()
  ├── pa_data_mgr_init()    状態マシン初期化
  └── uart->init()          UART 初期化、受信コールバック登録
```


### 4.5 エントリポイント（`broadcast_source/main.c`）

DI（Dependency Injection）配線と Auracast ブロードキャスト初期化を行うエントリポイント。

**`main()` の実行フロー:**

```
1. peripherals_init()         ペリフェラル初期化
2. bt_mgmt_init()             Bluetooth管理初期化
3. audio_system_init()        音声システム初期化

4. アダプタ生成 & ポート組み立て（DI配線）
   ├── zephyr_uart_adapter_create()
   ├── zephyr_ble_pa_adapter_create()
   ├── zephyr_timer_adapter_create()
   ├── zephyr_led_adapter_create()
   └── zephyr_button_adapter_create()

5. pa_service_init()          PA サービス初期化（ポートを注入）
6. button_port.init()         ボタンコールバック登録

7. zbus_subscribers_create()  zbus サブスクライバスレッド生成
8. zbus_link_producers_observers()  zbus チャネル接続

9. broadcast_source_enable()  ブロードキャスト有効化
10. audio_system_config_set() サンプルレート・ビットレート設定
11. ext_adv_populate()        拡張アドバタイジング構成
12. per_adv_populate()        PA データ（BASE）構成
13. zephyr_ble_pa_adapter_set_base_per_adv()  BASE参照をアダプタに登録
14. bt_mgmt_adv_start()       アドバタイジング開始
```

**zbus イベントハンドリング:**

| イベント | ハンドラ | 動作 |
|----------|----------|------|
| `BT_MGMT_EXT_ADV_WITH_PA_READY` | `bt_mgmt_evt_handler` | ext_adv を PA アダプタに登録、ブロードキャスト開始 |
| `LE_AUDIO_EVT_STREAMING` | `le_audio_msg_sub_thread` | エンコーダ開始、LED 点滅 |
| `LE_AUDIO_EVT_NOT_STREAMING` | `le_audio_msg_sub_thread` | エンコーダ停止、LED 点灯 |

**ボタンイベント:**

| ボタン | 動作 |
|--------|------|
| PLAY_PAUSE / VOLUME_DOWN | ブロードキャスト開始/停止トグル |
| ACTION / VOLUME_UP | テストトーン再生（ストリーミング中のみ） |


## 5. UART コマンドプロトコル

### 5.1 フレームフォーマット

```
+----------------+--------+------------------+
| Command_Length | OpCode | Payload (可変長) |
| (1 byte)       | (1 byte)|                  |
+----------------+--------+------------------+

Command_Length = OpCode + Payload の合計バイト数（自身を含まない）
```

### 5.2 コマンド一覧

#### PA Status Get Request (0x00) — PC → FW

```
| Command_Length | OpCode |
|     0x01       |  0x00  |
```

ペイロードなし。アクティブなアドバタイジングセットの状態を問い合わせる。

#### PA Status Get Response (0x01) — FW → PC

```
| Cmd_Len | OpCode | Num_Adv | SID[0] | ... | SID[N-1] | Available[0] | ... | Available[N-1] |
```

- `Num_Adv`: アクティブなアドバタイジングセット数
- `SID[i]`: 各セットの Advertising SID
- `Available[i]`: 各セットの PA 空き容量（バイト）

#### PA Data Send Request (0x10) — PC → FW

```
| Cmd_Len | OpCode | Adv_SID | Data_Length | Data[0] | ... | Data[N-1] |
```

- `Adv_SID`: 対象のアドバタイジング SID
- `Data_Length`: Data フィールドのバイト数
- `Data`: PA に格納するアプリケーションデータ

#### PA Data Send Response (0x11) — FW → PC

```
| Cmd_Len | OpCode | Result |
|  0x02   |  0x11  |        |
```

- `Result`: 処理結果コード（0x00=成功, 0x01=BUSY, 0x02=無効SID, 0x03=データ超過, 0x04=未準備）

#### PA Data Send Complete (0x12) — FW → PC

```
| Cmd_Len | OpCode |
|  0x01   |  0x12  |
```

PA インターバルが経過し、データが PA パケットとしてブロードキャストされたことを通知する。


### 5.3 通信シーケンス

```
      PC                                    FW
       |                                     |
       |--- [0x01, 0x00] ------------------>|  PA Status Get Request
       |                                     |
       |<-- [CmdLen, 0x01, N, SIDs, Avail]--|  PA Status Get Response
       |                                     |
       |--- [CmdLen, 0x10, SID, Len, Data]->|  PA Data Send Request
       |                                     |
       |<-- [0x02, 0x11, Result] -----------|  PA Data Send Response (即時)
       |                                     |
       |         (PA interval ≈ 100ms)       |
       |                                     |
       |<-- [0x01, 0x12] -------------------|  PA Data Send Complete
       |                                     |
       |--- (次のコマンド送信可能) ---------->|
```

PC 側は Response (0x11) と Complete (0x12) の両方を受信してから次のコマンドを送信する。


## 6. UART 受信からPA データ設定までのデータフロー

```
1. UART1 ISR (zephyr_uart_adapter.c)
   └── uart_isr_callback()
       ├── 1バイト目で Command_Length を取得
       ├── 残りバイトを rx_buf に蓄積
       └── フレーム完成 → k_work_submit(&rx_work)

2. スレッドコンテキスト (zephyr_uart_adapter.c)
   └── rx_work_handler()
       └── rx_cb() = pa_service_on_command()  ← コールバック呼び出し

3. pa_service_on_command() (pa_service.c)
   ├── pa_cmd_parse()           バイト列 → opcode + payload 構造体
   └── switch (opcode)
       ├── 0x00 → handle_status_get_req()
       └── 0x10 → handle_data_send_req()

4. handle_data_send_req() (pa_service.c)
   ├── ble_pa->is_ready()       PA が利用可能か確認
   ├── pa_data_mgr_submit()     状態検証、データ格納 (IDLE → PENDING)
   ├── ble_pa->set_data()       PA データを BLE スタックに設定
   ├── timer->start_oneshot()   100ms タイマー開始
   └── uart->send(0x11 応答)    結果コードを PC に返送

5. zephyr_ble_pa_adapter (zephyr_ble_pa_adapter.c)
   └── adapter_set_data()
       ├── BASE AD element + VS AD element を結合
       └── bt_le_per_adv_set_data()   Zephyr BLE API で PA 更新

6. タイマー期限到来 (100ms 後)
   └── pa_service_on_pa_interval()
       ├── pa_data_mgr_on_pa_interval()  PENDING → COMPLETE
       ├── uart->send(0x12 完了通知)     PC に完了を通知
       └── pa_data_mgr_reset()           IDLE に復帰
```


## 7. 移植性

### 移植可能な層（プラットフォーム非依存）

以下のファイルは `<stdint.h>`, `<string.h>`, `<stdbool.h>` のみに依存し、そのまま他プラットフォームで使用できる。

| ファイル | 行数 | 役割 |
|----------|------|------|
| `src/core/pa_protocol.h` | 44 | プロトコル定義 |
| `src/core/pa_cmd_parser.h` + `.c` | 111 | コマンドパース・直列化 |
| `src/core/pa_data_manager.h` + `.c` | 81 | 状態マシン |
| `src/ports/port_uart.h` | 15 | UART インターフェース |
| `src/ports/port_ble_pa.h` | 22 | BLE PA インターフェース |
| `src/ports/port_timer.h` | 15 | タイマーインターフェース |
| `src/ports/port_log.h` | 39 | ログインターフェース |
| `src/ports/port_os.h` | 41 | OS プリミティブインターフェース |
| `src/ports/port_led.h` | 20 | LED インターフェース |
| `src/ports/port_button.h` | 26 | ボタンインターフェース |
| `src/app/pa_service.h` + `.c` | 149 | オーケストレータ |

### 新プラットフォームへの移植手順

1. 各 `port_*.h` のインターフェースに対応するアダプタを新規実装
2. `port_log.h` 用のプラットフォームログヘッダを作成
3. `main.c` 相当のエントリポイントで DI 配線を行う
4. Core 層と App 層はそのまま再利用


## 8. ボード別設定

### nRF5340 Audio DK (`boards/nrf5340_audio_dk_nrf5340_cpuapp.*`)

| 設定項目 | 値 | 備考 |
|----------|-----|------|
| CS47L63 コーデック | 有効 | ハードウェアコーデック搭載 |
| SD カード | 有効 | LC3 ファイル再生用 |
| 電力測定 (INA231) | 無効 | i2c1 無効化に伴い無効 |
| i2c1 | 無効 | uart1 とペリフェラルインスタンス共有のため |
| INA231 ノード | 削除 | i2c1 無効に伴うビルドエラー回避 |
| uart1 | 有効 | TX=P1.9, RX=P1.8, 115200 baud |

### nRF5340 DK (`boards/nrf5340dk_nrf5340_cpuapp.*`)

| 設定項目 | 値 | 備考 |
|----------|-----|------|
| CS47L63 コーデック | 無効 | DK にはコーデック未搭載 |
| SD カード | 無効 | DK には SD スロット未搭載 |
| 電力測定 | 無効 | INA231 未搭載 |
| USB オーディオ | 有効 | USB 経由で音声入力 |
| I2S | 有効 | USB→I2S 経由でオーディオ出力 |
| RAMDISK | 有効 | SD カード代替 |
| i2c1 | 無効 | uart1 とペリフェラルインスタンス共有のため |
| uart1 | 有効 | TX=P1.01, RX=P1.00, 115200 baud |
| LED | GPIO_ACTIVE_LOW | DK の LED 極性に合わせて反転 |


## 9. ハードウェア制約

### UART1 / I2C1 ペリフェラル競合

nRF5340 では UART1, SPI1, TWI1（I2C1）が同一ペリフェラルインスタンス（Instance 1）を共有する。Audio DK のデフォルト DTS では INA231 電力測定センサ用に i2c1 が有効化されているため、uart1 を使用するにあたり以下の対応を行っている:

1. overlay で `&i2c1 { status = "disabled"; }` を設定
2. INA231 ノード 4 個を `/delete-node/` で削除
3. `CONFIG_NRF5340_AUDIO_POWER_MEASUREMENT=n` で電力測定機能を無効化

### PA データサイズ制限

PA PDU の最大ペイロードは 252 バイト。BASE AD element（音声 metadata）が一定のオーバーヘッドを占有するため、カスタムデータで使用できる実効容量は `252 - BASE長 - 2 (AD ヘッダ)` となる。この値は PA Status Get Response の `Available` フィールドで PC に通知される。
