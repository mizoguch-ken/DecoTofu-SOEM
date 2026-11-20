# DecoTofu-SOEM

## Overview / 概要

* **DecoTofu-SOEM** is the native shared library (`decotofusoem.dll` on Windows, `libdecotofusoem.so` on Linux, `libdecotofusoem.dylib` on macOS) that bridges [DecoTofu](https://github.com/mizoguch-ken/DecoTofu) (`jnr-ffi`) and the **Simple Open EtherCAT Master (SOEM 2.0)** library.
* **DecoTofu-SOEM** は、[DecoTofu](https://github.com/mizoguch-ken/DecoTofu)（`jnr-ffi`）と **Simple Open EtherCAT Master (SOEM 2.0)** ライブラリを接続するネイティブ共有ライブラリ（Windows: `decotofusoem.dll`、Linux: `libdecotofusoem.so`、macOS: `libdecotofusoem.dylib`）です。
* It links SOEM statically, re-exports SOEM's `ecx_*` / `ec_*` functions through a single canonical symbol table (`decotofu/decotofusoem.exports`), installs a public C header (`decotofu/decotofusoem.h`) and CMake package config, and provides 17 C shim functions for heap-allocated SOEM contexts, Distributed Clocks (DC) cyclic process data and mailbox handling, clean thread termination, ABI introspection, and safe network adapter enumeration.
* SOEM を静的リンクした上で、単一のシンボル定義ファイル（`decotofu/decotofusoem.exports`）から SOEM 関数を再エクスポートし、公開 C ヘッダ（`decotofu/decotofusoem.h`）および CMake パッケージ設定ファイル、さらにヒープ上でのコンテキスト管理、DC（Distributed Clocks）同期およびサイクリックメールボックス処理付き周期プロセスデータ通信スレッド、安全なスレッド終了待機、ABI イントロスペクション、およびネットワークアダプタ列挙を行う 17 個の C シム関数を提供します。

---

## Prerequisites / ビルド要件

* **CMake**: 3.28 or later (`CMAKE_BUILD_TYPE` defaults to `Release` if not specified).
  * **CMake**: 3.28 以上（`CMAKE_BUILD_TYPE` 未指定時は自動的に `Release` が選択されます）。
* **SOEM Source Tree**: Download or clone the [Simple Open EtherCAT Master (SOEM)](https://github.com/OpenEtherCATsociety/SOEM) source code into the `SOEM/` directory at the root of this repository.
  * **SOEM ソースコード**: [Simple Open EtherCAT Master (SOEM)](https://github.com/OpenEtherCATsociety/SOEM) を取得し、本リポジトリ直下の `SOEM/` ディレクトリに配置してください。
* **Platform Compilers & Libraries / プラットフォーム別要件**:
  * **Windows**: Visual Studio (MSVC / `nmake`) and [Npcap](https://npcap.com/) installed in **WinPcap API-compatible Mode**.
    * **Windows**: Visual Studio（MSVC / `nmake`）および **WinPcap API 互換モードでインストールした Npcap**。
  * **Linux**: GCC or Clang, `make`, `pthread`, and `rt` (build SOEM static library with `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`).
    * **Linux**: GCC または Clang、`make`、`pthread`、`rt`（`SOEM` 静的ライブラリは `-DCMAKE_POSITION_INDEPENDENT_CODE=ON` を付けてビルドしてください）。
  * **macOS**: Xcode Command Line Tools (`clang`, `make`), `pthread`, and `pcap` (upstream SOEM 2.0 provides `win32`/`linux`/`rtk` backends; macOS builds require a macOS-enabled SOEM fork/backend).
    * **macOS**: Xcode Command Line Tools（`clang`, `make`）、`pthread`、`pcap`（本家 SOEM 2.0 が標準提供するのは `win32`/`linux`/`rtk` バックエンドのため、macOS でビルドする場合は macOS 対応バックエンドを含む SOEM ツリーが必要です）。

---

## Building / ビルド手順

### Windows (Visual Studio Command Prompt)

Open a **Visual Studio Developer Command Prompt** (x64 or x86 matching your target JDK) and run:
対象の JDK（64-bit / 32-bit）に対応する **Visual Studio 開発者コマンドプロンプト** を開き、以下を実行します。

```bat
REM 1. Build SOEM static library (SOEM/build/soem.lib)
cd SOEM
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake

REM 2. Build DecoTofu-SOEM shared library & tests (build/decotofusoem.dll)
cd ..\..
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake
ctest --output-on-failure
```

*(Multi-config Visual Studio generator / Visual Studio ジェネレータ使用時: `cmake --build . --config Release` / `ctest -C Release --output-on-failure`)*

### Linux

```bash
# 1. Build SOEM static library with PIC (SOEM/build/libsoem.a)
cd SOEM
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make

# 2. Build DecoTofu-SOEM shared library & tests (build/libdecotofusoem.so)
cd ../..
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
ctest --output-on-failure
```

### CMake Options & Installation / CMake オプションとインストール

* **`SOEM_DIR`** (default: `<repo>/SOEM`): Path to the SOEM source tree. / SOEM ソースツリーへのパス。
* **`SOEM_BUILD_DIR`** (default: `${SOEM_DIR}/build`): Path to the built SOEM static library (`soem.lib` / `libsoem.a`) and generated `ec_options.h`. / ビルド済み SOEM 出力ディレクトリ。
* **`DECOTOFUSOEM_WARNINGS_AS_ERRORS`** (default: `ON`): Treat compiler warnings as errors (`/WX` / `-Werror`). / コンパイル警告をエラーとして扱う設定。
* **Install (`cmake --install`)**: Installs `decotofusoem` shared/import library, `decotofu/decotofusoem.h` (`include/decotofusoem/decotofusoem.h`), and CMake package files (`decotofusoem::decotofusoem` via `decotofusoemConfig.cmake` / `decotofusoemConfigVersion.cmake` / `decotofusoemTargets.cmake`).
  * 共有ライブラリ・インポートライブラリ・公開ヘッダ `decotofu/decotofusoem.h`・CMake パッケージ設定（`find_package(decotofusoem)` 用）をインストールします。

---

## Architecture & ABI / アーキテクチャと ABI

### Canonical Export List (`decotofu/decotofusoem.exports`)

* `decotofu/decotofusoem.exports` is the single source of truth for all 117 symbols exported by this library (17 shim functions implemented in `decotofu/decotofusoem.c` + 100 SOEM functions statically linked and re-exported).
  * `decotofu/decotofusoem.exports` は、本ライブラリがエクスポートする全 117 シンボル（`decotofu/decotofusoem.c` で実装された 17 個のシム関数＋静的リンクされた SOEM から再エクスポートされる 100 個の関数）の単一の定義リストです。
* DecoTofu resolves these symbols by name via `jnr-ffi` (`SoemLibrary.java`), and native C/C++ callers can include `decotofu/decotofusoem.h`, making this list the binary ABI contract.
  * DecoTofu は `jnr-ffi`（`SoemLibrary.java`）を通じて、ネイティブ C/C++ 呼び出し側は `decotofu/decotofusoem.h` を通じてこれらのシンボルを参照するため、このリスト自体がバイナリ ABI となります。
* At CMake configure time, CMake automatically generates:
  * Windows: `${CMAKE_BINARY_DIR}/decotofusoem.def` (module definition file)
  * Linux: `${CMAKE_BINARY_DIR}/decotofusoem.map` (GNU `ld` version script)
  * Always edit `decotofu/decotofusoem.exports` directly—never edit the generated `.def` or `.map` files.
  * CMake の構成時に Windows 用のモジュール定義ファイル（`decotofusoem.def`）や Linux 用のバージョンマップ（`decotofusoem.map`）が自動生成されます。シンボルを変更する際は生成ファイルではなく `decotofu/decotofusoem.exports` を編集してください。

### Shim Functions (`decotofu/decotofusoem.h` / `decotofu/decotofusoem.c`)

| Function / 関数 | Description / 説明 |
| :--- | :--- |
| `ecx_contextt *ec_malloc_context(void)` | Allocates and zero-initializes a complete `ecx_contextt` structure (including its inlined `port`, `slavelist`, `grouplist`, `elist`, `idxstack`, `mbxpool`, etc.) and `port.redport` on the heap via `osal_malloc` (setting Linux `sockhandle`s to `-1`), rolling back completely on out-of-memory.<br>`osal_malloc` を用いてゼロ初期化された `ecx_contextt` と冗長ポート `port.redport` をヒープ上に確保します（Linux では未開通ソケット FD を `-1` で初期化、メモリ不足時は全解放して `NULL`）。 |
| `void ec_free_context(ecx_contextt *context)` | Frees `context->port.redport` and `context`. Call after `ecx_close(context)` (which destroys `grouplist[lp].mbxtxqueue.mbxmutex` and `mbxpool.mbxmutex`).<br>`context->port.redport` および `context` を解放します（ミューテックスを破棄する `ecx_close(context)` の後に呼び出します）。 |
| `ecx_redportt *ec_redport(ecx_contextt *context)` | Returns `context->port.redport` for configuring redundant EtherCAT network interfaces (`ecx_init_redundant`).<br>冗長 EtherCAT ポート構成（`ecx_init_redundant`）のために `context->port.redport` を返します。 |
| `ecx_parcelt *ec_malloc_parcel(ecx_contextt *context)` | Allocates and zero-initializes an `ecx_parcelt` communication parcel sharing `context`, `thread`, `wkc`, `cycletime`, `dorun`, and `isprocess` between the JVM and the native cyclic thread.<br>JVM とネイティブ周期スレッド間で共有される `ecx_parcelt` パケット構造体をゼロ初期化して確保します。 |
| `void ec_free_parcel(ecx_parcelt *parcel)` | Releases any unjoined thread handle (`CloseHandle` on Windows, `pthread_detach` on Linux), frees sub-fields of `parcel` (excluding `context`, which is owned separately), and frees `parcel` itself.<br>未解放のスレッドハンドル（Windows: `CloseHandle`、Linux: `pthread_detach`）と `parcel` の各サブフィールド（`context` を除く）を解放します。 |
| `int ec_run(ecx_parcelt *parcel)` | Validates `parcel` and its inner pointers, guards against duplicate thread launches, and starts the real-time cyclic EtherCAT process data thread (`ecatthread`) via `osal_thread_create_rt`.<br>`parcel` と内部ポインタの妥当性および二重起動を検査した上で、`osal_thread_create_rt` によりリアルタイム周期スレッド（`ecatthread`）を起動します。 |
| `int ec_wait_thread(ecx_parcelt *parcel, int timeoutMillis)` | Waits for the cyclic thread started by `ec_run()` to exit cleanly after `*dorun = FALSE`. Uses `WaitForSingleObject` on Windows and `pthread_join` / `pthread_tryjoin_np` / `pthread_timedjoin_np` on Linux, guaranteeing `parcel` and `context` are never freed while still in use.<br>`*dorun = FALSE` 設定後に周期スレッドが完全に終了するのを待機します（Windows: `WaitForSingleObject`、Linux: `pthread_join` / `pthread_tryjoin_np` / `pthread_timedjoin_np`）。 |
| `int ec_stop(ecx_parcelt *parcel, int timeoutMillis)` | Atomically requests shutdown (`*parcel->dorun = FALSE`) and waits via `ec_wait_thread(parcel, timeoutMillis)` in a single call (`-1` when `parcel == NULL`).<br>`*parcel->dorun = FALSE` の停止要求と `ec_wait_thread(parcel, timeoutMillis)` による終了待機を 1 回で実行します（`parcel == NULL` の場合は `-1`）。 |
| `ec_adaptert *ec_adapters(void)` | Safely enumerates network adapters in machine order into zero-initialized `osal_malloc` nodes with explicit OOM and NULL handling (Windows via `pcap_findalldevs`, Linux via `if_nameindex`).<br>マシンのネットワークアダプタ一覧（Windows: `pcap_findalldevs`、Linux: `if_nameindex`）を OOM/NULL 安全な検査付きのゼロ初期化ノードの連結リストで返します。 |
| `void ec_free_adapters_list(ec_adaptert *adapter)` | Frees the linked list returned by `ec_adapters()`.<br>`ec_adapters()` が返したアダプタ連結リストを解放します。 |
| `const char *ec_version(void)` | Returns the DecoTofu-SOEM library version string (e.g. `"2.0.0"`).<br>DecoTofu-SOEM ライブラリのバージョン文字列（例: `"2.0.0"`）を返します。 |
| `size_t ec_sizeof_context(void)` | Returns `sizeof(ecx_contextt)` compiled into this shared library for FFI ABI verification.<br>FFI ABI レイアウト検証用に `sizeof(ecx_contextt)` を返します。 |
| `size_t ec_sizeof_parcel(void)` | Returns `sizeof(ecx_parcelt)` compiled into this shared library for FFI ABI verification.<br>FFI ABI レイアウト検証用に `sizeof(ecx_parcelt)` を返します。 |
| `size_t ec_sizeof_slave(void)` | Returns `sizeof(ec_slavet)` compiled into this shared library for FFI ABI verification.<br>FFI ABI レイアウト検証用に `sizeof(ec_slavet)` を返します。 |
| `size_t ec_sizeof_group(void)` | Returns `sizeof(ec_groupt)` compiled into this shared library for FFI ABI verification.<br>FFI ABI レイアウト検証用に `sizeof(ec_groupt)` を返します。 |
| `int ec_max_slave(void)` | Returns `EC_MAXSLAVE` compiled into SOEM.<br>SOEM に組み込まれた `EC_MAXSLAVE` を返します。 |
| `int ec_max_group(void)` | Returns `EC_MAXGROUP` compiled into SOEM.<br>SOEM に組み込まれた `EC_MAXGROUP` を返します。 |

### Cyclic Thread & Distributed Clocks (`ecatthread` & `ec_sync`)

* **Cyclic Mailbox Handling (`ecx_mbxhandler`)**: After receiving process data (`last_wkc > 0`) and when group 0 cyclic mailbox mapping (`mbxstatus` and `mbxtxqueue.mbxmutex`) is active, `ecatthread` services up to `4` SOEM 2.0 cyclic mailbox operations per cycle before sleeping.
  * **サイクリックメールボックス処理 (`ecx_mbxhandler`)**: `last_wkc > 0` かつグループ 0 のサイクリックメールボックス（`mbxstatus` および `mbxtxqueue.mbxmutex`）が構成されている場合、スリープ前の空き時間で 1 周期あたり最大 `4` 件の `ecx_mbxhandler` 処理を実行します。
* **PI Drift Compensation (`ec_sync`)**: When reference clock slave 1 has Distributed Clocks enabled (`hasdc`) and `wkc > 0`, `ec_sync` computes a signed phase correction (`delta`) against `EC_SYNC_REF_NSEC` (`50,000 ns`) using overflow-free modulo arithmetic and clamps integrator windup and offset output within `[-cycletime / 2, +cycletime / 2]` so extreme timestamps or bus stalls never corrupt or collapse the cycle timer.
  * **PI ドリフト補正 (`ec_sync`)**: スレーブ 1 で DC（Distributed Clocks）が有効かつ `wkc > 0` の場合、`EC_SYNC_REF_NSEC`（`50,000 ns`）を基準にオーバーフロー安全な剰余演算で位相誤差から補正量を算出し、積分ワインドアップと補正量を `[-cycletime / 2, +cycletime / 2]` にクランプしてホスト側の周期タイマーを同期させます。
* **High-Resolution Interruptible Monotonic Sleep (`ec_timer_sleep`)**:
  * **Linux & Windows**: Both maintain an absolute monotonic deadline (`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` with `EINTR` retry on Linux; `osal_get_monotonic_time()` + `osal_monotonic_sleep()` on Windows) so sub-cycle OS jitter does not drift the schedule, while cycle overruns past the deadline resync cleanly to current monotonic time instead of bursting un-throttled catch-up frames. Normal cycles (`<= 50 ms`) wake once directly at `deadline`, while long cycle intervals sleep in `50 ms` slices checking `*dorun` so `ec_stop()` exits promptly.
  * **高精度・中断可能スリープ (`ec_timer_sleep`)**: Linux（`clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`）と Windows（`osal_get_monotonic_time()` + `osal_monotonic_sleep()`）の双方で絶対モノトニック時刻のデッドラインを維持してドリフトを抑えつつ、1周期を超える遅延時は現在時刻へ再同期して連続バーストを防ぎます。通常周期（50 ms 以下）では余分な起床なしで `deadline` まで直接待機し、長周期指定時は 50 ms スライスで `*dorun` を監視して即時停止可能にしています。
* **Clean Cycle Exit**: `ecatthread` verifies `*dorun` before the first send, immediately after receive, and immediately after sleep so `*dorun = FALSE` terminates cleanly without emitting orphaned `ecx_send_processdata` frames on the wire.
  * **クリーンな周期スレッド終了**: 送信前・受信直後・スリープ復帰直後の各時点で `*dorun` を確認し、`*dorun = FALSE` 時に未回収のプロセスデータフレームを送信することなく安全に終了します。
