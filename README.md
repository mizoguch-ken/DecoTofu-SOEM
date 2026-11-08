# DecoTofu-SOEM

## Overview / 概要

* **DecoTofu-SOEM** is the native shared library (`decotofusoem.dll` on Windows, `libdecotofusoem.so` on Linux, `libdecotofusoem.dylib` on macOS) that bridges [DecoTofu](https://github.com/mizoguch-ken/DecoTofu) (`jnr-ffi`) and the **Simple Open EtherCAT Master (SOEM 2.0.0)** library.
* **DecoTofu-SOEM** は、[DecoTofu](https://github.com/mizoguch-ken/DecoTofu)（`jnr-ffi`）と **Simple Open EtherCAT Master (SOEM 2.0.0)** ライブラリを接続するネイティブ共有ライブラリ（Windows: `decotofusoem.dll`、Linux: `libdecotofusoem.so`、macOS: `libdecotofusoem.dylib`）です。
* It links SOEM statically, re-exports SOEM's `ecx_*` / `ec_*` functions through a single canonical symbol table (`decotofu/decotofusoem.exports`), and provides 9 C shim functions for heap-allocated SOEM contexts, Distributed Clocks (DC) cyclic process data threads, clean thread termination, and safe network adapter enumeration.
* SOEM を静的リンクした上で、単一のシンボル定義ファイル（`decotofu/decotofusoem.exports`）から SOEM 関数を再エクスポートし、さらにヒープ上でのコンテキスト管理、DC（Distributed Clocks）同期付きの周期プロセスデータ通信スレッド、安全なスレッド終了待機、およびネットワークアダプタ列挙を行う 9 個の C シム関数を提供します。

---

## Prerequisites / ビルド要件

* **CMake**: 3.28 or later (`CMAKE_BUILD_TYPE` defaults to `Release` if not specified).
  * **CMake**: 3.28 以上（`CMAKE_BUILD_TYPE` 未指定時は自動的に `Release` が選択されます）。
* **SOEM Source Tree**: Download or clone the [Simple Open EtherCAT Master (SOEM)](https://github.com/OpenEtherCATsociety/SOEM) source code into the `SOEM/` directory at the root of this repository.
  * **SOEM ソースコード**: [Simple Open EtherCAT Master (SOEM)](https://github.com/OpenEtherCATsociety/SOEM) を取得し、本リポジトリ直下の `SOEM/` ディレクトリに配置してください。
* **Platform Compilers & Libraries / プラットフォーム別要件**:
  * **Windows**: Visual Studio (MSVC / `nmake`) and [Npcap](https://npcap.com/) installed in **WinPcap API-compatible Mode**.
    * **Windows**: Visual Studio（MSVC / `nmake`）および **WinPcap API 互換モードでインストールした Npcap**。
  * **Linux**: GCC or Clang, `make`, `pthread`, and `rt`.
    * **Linux**: GCC または Clang、`make`、`pthread`、`rt`。
  * **macOS**: Xcode Command Line Tools (`clang`, `make`), `pthread`, and `pcap`.
    * **macOS**: Xcode Command Line Tools（`clang`, `make`）、`pthread`、`pcap`。

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

REM 2. Build DecoTofu-SOEM shared library (build/decotofusoem.dll)
cd ..\..
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
nmake
```

### Linux / macOS

```bash
# 1. Build SOEM static library (SOEM/build/libsoem.a)
cd SOEM
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# 2. Build DecoTofu-SOEM shared library (build/libdecotofusoem.so or .dylib)
cd ../..
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

---

## Architecture & ABI / アーキテクチャと ABI

### Canonical Export List (`decotofu/decotofusoem.exports`)

* `decotofu/decotofusoem.exports` is the single source of truth for all 112 symbols exported by this library (9 shim functions implemented in `decotofu/decotofusoem.c` + 103 SOEM functions statically linked and re-exported).
  * `decotofu/decotofusoem.exports` は、本ライブラリがエクスポートする全 112 シンボル（`decotofu/decotofusoem.c` で実装された 9 個のシム関数＋静的リンクされた SOEM から再エクスポートされる 103 個の関数）の単一の定義リストです。
* DecoTofu resolves these symbols by name via `jnr-ffi` (`SoemLibrary.java`), making this list the binary ABI contract.
  * DecoTofu は `jnr-ffi`（`SoemLibrary.java`）を通じてこれらのシンボルを名前解決するため、このリスト自体がバイナリ ABI となります。
* At CMake configure time, CMake automatically generates:
  * Windows: `${CMAKE_BINARY_DIR}/decotofusoem.def` (module definition file)
  * Linux: `${CMAKE_BINARY_DIR}/decotofusoem.map` (GNU `ld` version script)
  * Always edit `decotofu/decotofusoem.exports` directly—never edit the generated `.def` or `.map` files.
  * CMake の構成時に Windows 用のモジュール定義ファイル（`decotofusoem.def`）や Linux 用のバージョンマップ（`decotofusoem.map`）が自動生成されます。シンボルを変更する際は生成ファイルではなく `decotofu/decotofusoem.exports` を編集してください。

### Shim Functions (`decotofu/decotofusoem.c`)

| Function / 関数 | Description / 説明 |
| :--- | :--- |
| `ecx_contextt *ec_malloc_context(void)` | Allocates and zero-initializes a complete `ecx_contextt` structure and all sub-blocks on the heap via `osal_malloc`, ensuring `elist`, `idxstack`, `ecaterror`, and `DCtime` start cleanly at `0` / `FALSE` and rolling back completely on out-of-memory.<br>`osal_malloc` を用いてゼロ初期化された `ecx_contextt` と全サブブロックをヒープ上に確保します（メモリ不足時は確保済みブロックを全て解放して `NULL` を返します）。 |
| `void ec_free_context(ecx_contextt *context)` | Destroys per-group mailbox queue mutexes (`pthread_mutex_destroy` / `CloseHandle`) created by `ecx_config_init()` and frees all sub-blocks of `context`. Call after `ecx_close(context)`.<br>`ecx_config_init()` が生成したグループごとのメールボックスミューテックスを破棄し、`context` の全ブロックを解放します（`ecx_close(context)` の後に呼び出します）。 |
| `ecx_redportt *ec_redport(ecx_contextt *context)` | Returns `context->port->redport` for configuring redundant EtherCAT network interfaces (`ecx_init_redundant`).<br>冗長 EtherCAT ポート構成（`ecx_init_redundant`）のために `context->port->redport` を返します。 |
| `ecx_parcelt *ec_malloc_parcel(ecx_contextt *context)` | Allocates and zero-initializes an `ecx_parcelt` communication parcel sharing `context`, `thread`, `wkc`, `cycletime`, `dorun`, and `isprocess` between the JVM and the native cyclic thread.<br>JVM とネイティブ周期スレッド間で共有される `ecx_parcelt` パケット構造体をゼロ初期化して確保します。 |
| `void ec_free_parcel(ecx_parcelt *parcel)` | Releases the sub-fields of `parcel` (excluding `context`, which is owned separately) and frees `parcel` itself. On Windows, closes the native thread handle if still open.<br>`parcel` の各サブフィールド（`context` を除く）および Windows のスレッドハンドルを解放します。 |
| `int ec_run(ecx_parcelt *parcel)` | Validates `parcel` and its inner pointers, guards against duplicate thread launches, and starts the real-time cyclic EtherCAT process data thread (`ecatthread`) via `osal_thread_create_rt`.<br>`parcel` と内部ポインタの妥当性および二重起動を検査した上で、`osal_thread_create_rt` によりリアルタイム周期スレッド（`ecatthread`）を起動します。 |
| `int ec_wait_thread(ecx_parcelt *parcel, int timeoutMillis)` | Waits for the cyclic thread started by `ec_run()` to exit cleanly after `*dorun = FALSE`. Uses `WaitForSingleObject` on Windows and `pthread_join` / `pthread_timedjoin_np` on Linux, guaranteeing `parcel` and `context` are never freed while still in use.<br>`*dorun = FALSE` 設定後に周期スレッドが完全に終了するのを待機します（Windows: `WaitForSingleObject`、Linux: `pthread_join` / `pthread_timedjoin_np`）。 |
| `ec_adaptert *ec_adapters(void)` | Safely enumerates network adapters in machine order. Works around an uninitialized pointer bug in SOEM 2.0.0's Windows `oshw_find_adapters()` (`SOEM/oshw/win32/oshw.c:41`) without modifying the `SOEM/` checkout.<br>マシンのネットワークアダプタ一覧を連結リストで返します。`SOEM/` ツリーを変更することなく、SOEM 2.0.0 の Windows 版 `oshw_find_adapters()` における未初期化ポインタ不具合を回避します。 |
| `void ec_free_adapters_list(ec_adaptert *adapter)` | Frees the linked list returned by `ec_adapters()`.<br>`ec_adapters()` が返したアダプタ連結リストを解放します。 |

### Cyclic Thread & Distributed Clocks (`ecatthread` & `ec_sync`)

* **PI Drift Compensation (`ec_sync`)**: When reference clock slave 1 has Distributed Clocks enabled (`hasdc`), `ec_sync` computes a signed phase correction (`delta`) against `EC_SYNC_REF_NSEC` (`50,000 ns`) to lock the host timer to the EtherCAT reference clock without overflow or signed modulo bias.
  * **PI ドリフト補正 (`ec_sync`)**: スレーブ 1 で DC（Distributed Clocks）が有効な場合、`EC_SYNC_REF_NSEC`（`50,000 ns`）を基準に位相誤差から PI 制御で補正量 `delta` を算出し、ホスト側の周期タイマーを EtherCAT リファレンスクロックに同期させます。
* **High-Resolution Monotonic Sleep (`ec_timer_sleep`)**:
  * **Linux**: Sleeps until the absolute `CLOCK_MONOTONIC` deadline via `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` and automatically resumes with the same target deadline if interrupted by a POSIX signal (`EINTR`).
  * **Windows**: Computes remaining microseconds from `osal_current_time()`, clamps negative/overflowing values safely to `[0, UINT32_MAX]`, and calls `osal_usleep()`.
  * **高精度スリープ (`ec_timer_sleep`)**: Linux では `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` により絶対時刻まで待機し、シグナル割り込み（`EINTR`）時も目標時刻を維持して待機を継続します。Windows では残りマイクロ秒を `[0, UINT32_MAX]` に安全にクランプして `osal_usleep()` を呼び出します。
* **Clean Cycle Exit**: When `*dorun` is set to `FALSE`, `ecatthread` exits immediately after waking from sleep without sending an orphaned `ecx_send_processdata` datagram on the wire.
  * **クリーンな周期スレッド終了**: `*dorun` が `FALSE` に設定されると、スリープ復帰直後にループを抜け、未回収のプロセスデータフレームを送信することなく安全に終了します。
