# DecoTofu-SOEM

BUILDING
========

Prerequisites for all platforms
-------------------------------
 * CMake 3.28 or later

Windows (Visual Studio)
-----------------------
 * Start a Visual Studio command prompt then:
   * download and copy Simple Open EtherCAT Master Library to SOEM directory
   * `cd SOEM`
   * `mkdir build`
   * `cd build`
   * `cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"`
   * `nmake`
   * `cd ../../`
   * `mkdir build`
   * `cd build`
   * `cmake .. -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"`
   * `nmake`

Linux
-----
   * download and copy Simple Open EtherCAT Master Library to SOEM directory
   * `cd SOEM`
   * `mkdir build`
   * `cd build`
   * `cmake ..`
   * `make`
   * `cd ../../`
   * `mkdir build`
   * `cd build`
   * `cmake ..`
   * `make`

EXPORTED SYMBOLS
================

 * `decotofu/decotofusoem.exports` is the single list of names this library
   exports: the functions of the shim plus the SOEM functions that are linked
   statically from `soem.lib` and re-exported. DecoTofu resolves them by name
   through jnr-ffi, so the list is an ABI.
 * CMake generates the platform specific export map from that list into the
   build directory - a module definition file (`decotofusoem.def`) on Windows,
   a version script (`decotofusoem.map`) on Linux. Edit the list, never the
   generated files.
 * `ec_wait_thread()` waits the cyclic thread started by `ec_run()` leaving
   `ecatthread()` : `*dorun = FALSE` only asks that thread to stop, the parcel
   and the context it uses have to live until it returned. DecoTofu joins the
   thread with it, and when the loaded library predates the symbol - or when
   the platform can not join a thread handle - DecoTofu falls back to waiting
   longer than one cycle, so an old library still works and rebuilding this
   library is what makes the shutdown exact. The thread is waited with
   `WaitForSingleObject()` on Windows and with `pthread_timedjoin_np()` on
   glibc.
 * `ec_adapters()` answers the network adapters of the machine as the linked
   list of `ec_adaptert`, in the order the machine reports them, and
   `ec_free_adapters_list()` releases that list from the head it was answered
   with. The `ec_find_adapters()` / `ec_free_adapters()` pair of SOEM is still
   exported but DecoTofu does not use it, because `oshw_find_adapters()` of the
   Windows flavour leaves `prev_adapter` uninitialised
   (`SOEM/oshw/win32/oshw.c:41`, the Linux flavour initialises it at
   `SOEM/oshw/linux/oshw.c:47`) : the first device then takes the
   `if (prev_adapter)` branch as soon as the stack holds garbage, the head of
   the list is never stored, the answer is `NULL` on a machine that has
   adapters, and the garbage pointer is written through. `ec_adapters()` asks
   pcap the same question done right on Windows and keeps the SOEM answer on
   the other platforms; `SOEM/` itself is left untouched so that updating it
   stays a plain checkout.

