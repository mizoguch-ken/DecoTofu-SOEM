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

 * `dekotofu/dekotofusoem.exports` is the single list of names this library
   exports: the functions of the shim plus the SOEM functions that are linked
   statically from `soem.lib` and re-exported. DecoTofu resolves them by name
   through jnr-ffi, so the list is an ABI.
 * CMake generates the platform specific export map from that list into the
   build directory - a module definition file (`dekotofusoem.def`) on Windows,
   a version script (`dekotofusoem.map`) on Linux. Edit the list, never the
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

