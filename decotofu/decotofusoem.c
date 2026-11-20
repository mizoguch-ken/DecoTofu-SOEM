/* DecoTofu-SOEM: the native layer DecoTofu loads through jnr-ffi.

   DecoTofu binds this library by symbol name and mirrors the struct layouts by
   hand (DecoTofu/src/main/java/ken/mizoguch/soem/SoemLibrary.java and
   SoemEtherCAT.ecx_parcelt), so the exported names - the single list in
   decotofusoem.exports, which CMake turns into a module definition file
   (Windows) or a version script (Linux) - and the member order of ecx_parcelt
   are an ABI. Everything in this file that is static may be refactored
   freely, everything exported has to keep its name, its signature and its
   meaning.

   SOEM itself is linked statically (SOEM/build/soem.lib) and its own functions
   are re-exported through that same list, so this file only adds what SOEM
   does not provide: context objects that live on the heap instead of in
   file scope globals, and a cyclic EtherCAT thread the JVM can start and stop
   without owning a C thread handle.

   Facts about SOEM 2.0 this file depends on
   -----------------------------------------
   F1  SOEM is written against statically initialised buffers (see
       SOEM/samples/ec_sample/ec_sample.c:18-27). malloc() hands out garbage,
       and ecx_pusherror() (SOEM/src/ec_main.c:88) indexes
       ec_eringt.Error[] with ec_eringt.head before any range check, while the
       LRW helpers index ec_idxstackT with its uint8 pushed/pulled counters.
       Every block below is therefore allocated zeroed, and allocated through
       the OSAL allocator so that this shim and SOEM stay consistent on the
       non-libc OSAL targets (rtk).
   F2  ecx_init_context() (SOEM/src/ec_config.c:41-56, called by
       ecx_config_init() at ec_config.c:187) resets slavecount and
       memsets slavelist and grouplist, but never ec_eringt.head/tail,
       ec_idxstackT.pushed/pulled, ecaterror or DCtime. Those have to start
       at 0/FALSE here, otherwise ecx_pusherror() writes
       elist.Error[garbage head] out of bounds and the v2 ecx_LRWDC FPRMW
       payload puts a garbage DC time on the wire.
   F3  ecx_close() (SOEM/src/ec_main.c:287-299) destroys the per-group
       grouplist[lp].mbxtxqueue.mbxmutex mutexes created by ecx_config_init()
       and the context->mbxpool.mbxmutex mutex created by ecx_init(), and closes
       the NIC: the caller has to run ecx_init(), then ecx_close(), then
       ec_free_context().
   F4  osal_thread_create_rt() on win32 (SOEM/osal/win32/osal.c:145-156)
       returns the result of SetThreadPriority() after CreateThread(), so a
       priority failure reports 0 even when the thread is running. ec_run()
       therefore validates the handle it produced.
   F5  osal_usleep() takes a uint32 number of microseconds (SOEM/osal/osal.h:96,
       SOEM/osal/win32/osal.c:74), so a negative sleep length wraps into an
       enormous delay. osal_monotonic_sleep() on win32 takes an absolute
       deadline backed by QueryPerformanceCounter().
   F6  EC_TIMEOUTMON (500 ms, the monitor-thread timeout of the SOEM examples)
       has no meaning in this file. The timeout used for the cyclic receive is
       EC_TIMEOUTRET, 2000 us (SOEM/include/soem/ec_options.h.in:87).
   F7  SOEM's oshw_find_adapters() allocates nodes with libc malloc() (without
       OOM or NULL checks in some implementations) rather than zero-initialised
       OSAL allocations. ec_adapters() allocates zeroed nodes via ec_alloc() on
       Windows and Linux and releases them via osal_free(). */

/* SOEM 2.0 uses struct timespec / clock_gettime on the ELF targets, which
   only come from <time.h>; TIMER_ABSTIME / clock_nanosleep come from
   <pthread.h>. pthread_timedjoin_np() and pthread_tryjoin_np(), used by
   ec_wait_thread(), are GNU extensions and require _GNU_SOURCE before any
   system header is included. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <net/if.h>
#include <pthread.h>
#include <time.h>
#endif

#include "decotofusoem.h"

/* ec_adapters() asks pcap for the adapters of the machine on Windows. The
   header and the import library come from the wpcap SDK inside the SOEM tree,
   which CMake puts on the include path and links (CMakeLists.txt); pcap.h
   selects its Windows flavour on the WIN32 CMake defines for MSVC. */
#if defined(_WIN32)
#include <pcap.h>
#endif

/* Time units: everything SOEM handles is nanoseconds. */
#define EC_NSEC_PER_SEC         1000000000LL
#define EC_NSEC_PER_MSEC        1000000LL
#define EC_NSEC_PER_USEC        1000LL
#define EC_MAX_SLEEP_NSEC       ((int64)0xFFFFFFFFU * EC_NSEC_PER_USEC)
#define EC_SLEEP_SLICE_NSEC     (50LL * EC_NSEC_PER_MSEC)
#define EC_SLEEP_SLICE_USEC     50000LL

/* Stack size of the cyclic thread, the size the SOEM examples use. */
#define ECAT_THREAD_STACKSIZE   128000

/* Max cyclic mailbox messages handled per EtherCAT cycle (SOEM ec_sample). */
#define ECAT_MBX_HANDLER_LIMIT  4

/* DC drift correction, the same constants as the reference loop in
   SOEM reference code. */
#define EC_SYNC_REF_NSEC        50000   /* phase reference inside a cycle  */
#define EC_SYNC_ERROR_SCALE     100     /* ns of drift per integrator step */
#define EC_SYNC_INTEGRATOR_DIV  20      /* leak factor of the integrator   */

#if defined(__linux__)
#define EC_THREAD_STORAGE_SIZE \
  (sizeof(pthread_t) > sizeof(OSAL_THREAD_HANDLE) ? sizeof(pthread_t) : sizeof(OSAL_THREAD_HANDLE))
#else
#define EC_THREAD_STORAGE_SIZE  sizeof(OSAL_THREAD_HANDLE)
#endif

/* One heap block referenced by a pointer member of a dynamically allocated
   parcel object. Describing the blocks in one table means the allocation, the
   "did something fail?" check and the release cannot drift apart.
   parcel->context points at memory owned by the context, so it has no row
   here. */
typedef struct
{
  size_t  offset;   /* offsetof(anchor, pointer member) inside the object  */
  size_t  size;     /* bytes to allocate for that member                   */
} ec_blockt;

#define EC_BLOCK(anchor, field, bytes)  { offsetof(anchor, field), (bytes) }
#define EC_BLOCKS(table)                (sizeof(table) / sizeof((table)[0]))

static const ec_blockt ec_parcel_blocks[] =
{
  EC_BLOCK(ecx_parcelt, thread,     EC_THREAD_STORAGE_SIZE),
  EC_BLOCK(ecx_parcelt, wkc,        sizeof(int)),
  EC_BLOCK(ecx_parcelt, cycletime,  sizeof(int64)),
  EC_BLOCK(ecx_parcelt, dorun,      sizeof(boolean)),
  EC_BLOCK(ecx_parcelt, isprocess,  sizeof(boolean))
};

/* Allocate one zeroed block (facts F1 and F2). */
static void * ec_alloc(size_t size)
{
  void * p = osal_malloc(size);

  if(p != NULL)
  {
    memset(p, 0, size);
  }
  return p;
}

/* Read/write pointer slots via memcpy to avoid strict-aliasing UB across
   typed pointer fields inside ecx_parcelt. */
static void * ec_block_load(const void * object, const ec_blockt * block)
{
  void * ptr;

  memcpy(&ptr, (const char *)object + block->offset, sizeof(ptr));
  return ptr;
}

static void ec_block_store(void * object, const ec_blockt * block, void * ptr)
{
  memcpy((char *)object + block->offset, &ptr, sizeof(ptr));
}

/* Allocate every block of a table. Returns 0 when all rows were filled, or 1
   on the first allocation failure; the members left NULL are released as a
   no-op afterwards, so the rollback of a half built object is complete. */
static size_t ec_alloc_blocks(void * object, const ec_blockt * table, size_t rows)
{
  size_t  row;

  for(row = 0; row < rows; row++)
  {
    void * ptr = ec_alloc(table[row].size);

    ec_block_store(object, &table[row], ptr);
    if(ptr == NULL)
    {
      return 1;
    }
  }
  return 0;
}

/* Release every block of a table and clear the member, so the object cannot
   hand out a pointer that has already been released. */
static void ec_free_blocks(void * object, const ec_blockt * table, size_t rows)
{
  size_t  row;

  for(row = 0; row < rows; row++)
  {
    void * ptr = ec_block_load(object, &table[row]);

    osal_free(ptr);
    ec_block_store(object, &table[row], NULL);
  }
}

/* -------------------------------------------------------------------static
   Thread handle helpers                                                   */

#if defined(_WIN32)

static int ec_thread_is_active(const ecx_parcelt * parcel)
{
  return *parcel->thread != NULL;
}

static void ec_thread_clear(ecx_parcelt * parcel)
{
  *parcel->thread = NULL;
}

#elif defined(__linux__)

/* In SOEM's linux OSAL, OSAL_THREAD_HANDLE is "pthread_t *", so parcel->thread
   is accessed through memcpy to match pthread_create()/pthread_join() without
   strict-aliasing violations. */
static pthread_t ec_thread_load(const ecx_parcelt * parcel)
{
  pthread_t  thread;

  memcpy(&thread, parcel->thread, sizeof(thread));
  return thread;
}

static int ec_thread_is_active(const ecx_parcelt * parcel)
{
  return ec_thread_load(parcel) != (pthread_t)0;
}

static void ec_thread_clear(ecx_parcelt * parcel)
{
  memset(parcel->thread, 0, sizeof(pthread_t));
}

#else

static int ec_thread_is_active(const ecx_parcelt * parcel)
{
  return *parcel->thread != 0;
}

static void ec_thread_clear(ecx_parcelt * parcel)
{
  *parcel->thread = 0;
}

#endif

/* ------------------------------------------------------------------exported
   Context lifecycle                                                       */

ecx_contextt * ec_malloc_context(void)
{
  ecx_contextt * context = ec_alloc(sizeof(ecx_contextt));

  if(context == NULL)
  {
    return NULL;
  }
  /* In SOEM 2.0, port, slavelist, grouplist, esibuf, esimap, elist, idxstack,
     SMcommtype, PDOassign, PDOdesc, eepSM, eepFMMU and mbxpool are inline
     members of ecx_contextt and are zero-initialised by ec_alloc() above
     (facts F1 and F2). Only port.redport is a separate heap block. */
  context->port.redport = ec_alloc(sizeof(ecx_redportt));
  if(context->port.redport == NULL)
  {
    ec_free_context(context);
    return NULL;
  }
#if defined(__linux__)
  /* On Linux, socket handles default to 0 after memset, which would close
     stdin in ecx_closenic() if ecx_init() failed or wasn't redundant. */
  context->port.sockhandle = -1;
  context->port.redport->sockhandle = -1;
#endif
  return context;
}

void ec_free_context(ecx_contextt * context)
{
  if(context == NULL)
  {
    return;
  }
  /* Fact F3: ecx_close() already destroys grouplist[lp].mbxtxqueue.mbxmutex
     and mbxpool.mbxmutex before ec_free_context() is called. */
  osal_free(context->port.redport);
  context->port.redport = NULL;
  osal_free(context);
}

/* ecx_init_redundant() wants the redport pointer, and the JVM must not have to
   know where it lives inside ecx_portt. */
ecx_redportt * ec_redport(ecx_contextt * context)
{
  if(context == NULL)
  {
    return NULL;
  }
  return context->port.redport;
}

/* ------------------------------------------------------------------exported
   Parcel lifecycle                                                        */

ecx_parcelt * ec_malloc_parcel(ecx_contextt * context)
{
  ecx_parcelt * parcel = ec_alloc(sizeof(ecx_parcelt));

  if(parcel == NULL)
  {
    return NULL;
  }
  if(ec_alloc_blocks(parcel, ec_parcel_blocks,
                     EC_BLOCKS(ec_parcel_blocks)) != 0)
  {
    ec_free_parcel(parcel);
    return NULL;
  }
  /* ec_alloc() zeroed every block, so *dorun and *isprocess start FALSE and
     *wkc and *cycletime start at 0; the caller sets cycletime and dorun
     before calling ec_run(). */
  parcel->context = context;
  return parcel;
}

/* Contract: the caller has to have set *dorun = FALSE and waited for the
   cyclic thread to leave ecatthread() before calling this, otherwise it
   releases memory that the live thread is still writing. ec_wait_thread()
   below is what makes that wait possible; where the platform can not join the
   thread the caller has to wait longer than one cycle. */
void ec_free_parcel(ecx_parcelt * parcel)
{
  if(parcel != NULL)
  {
    if(parcel->thread != NULL)
    {
#if defined(_WIN32)
      HANDLE  handle = *parcel->thread;

      if(handle != NULL)
      {
        (void)CloseHandle(handle);
        ec_thread_clear(parcel);
      }
#elif defined(__linux__)
      pthread_t  thread = ec_thread_load(parcel);

      if(thread != (pthread_t)0)
      {
        (void)pthread_detach(thread);
        ec_thread_clear(parcel);
      }
#endif
    }
    ec_free_blocks(parcel, ec_parcel_blocks, EC_BLOCKS(ec_parcel_blocks));
    parcel->context = NULL;
    osal_free(parcel);
  }
}

/* -------------------------------------------------------------------static
   Cycle timer. Both Linux and Windows keep an absolute monotonic deadline so
   that sub-cycle jitter does not accumulate into cycle drift, while overruns
   greater than a cycle resync cleanly instead of bursting back-to-back.     */

#if defined(__linux__) || defined(_WIN32)

typedef struct
{
  struct timespec  deadline;   /* next wake-up, absolute monotonic clock */
} ec_timert;

static void ec_monotonic_now(struct timespec * ts)
{
#if defined(__linux__)
  clock_gettime(CLOCK_MONOTONIC, ts);
#else
  osal_get_monotonic_time(ts);
#endif
}

/* Add nanoseconds to an absolute timespec. */
static void ec_timespec_add(struct timespec * ts, int64 nsec)
{
  int64  sec;
  int64  rest;

  sec  = nsec / EC_NSEC_PER_SEC;
  rest = nsec % EC_NSEC_PER_SEC;
  ts->tv_sec  += (time_t)sec;
  ts->tv_nsec += (long)rest;
  if(ts->tv_nsec < 0)
  {
    /* a signed offset can cross the zero nanosecond boundary */
    ts->tv_nsec += (long)EC_NSEC_PER_SEC;
    ts->tv_sec  -= 1;
  }
  else if(ts->tv_nsec >= (long)EC_NSEC_PER_SEC)
  {
    /* clock_nanosleep() rejects tv_nsec >= 1s with EINVAL */
    ts->tv_nsec -= (long)EC_NSEC_PER_SEC;
    ts->tv_sec  += 1;
  }
}

static int ec_timespec_le(const struct timespec * a, const struct timespec * b)
{
  if(a->tv_sec != b->tv_sec)
  {
    return a->tv_sec < b->tv_sec;
  }
  return a->tv_nsec <= b->tv_nsec;
}

/* Round the first deadline up to the next whole millisecond so cycles begin
   on a clean millisecond boundary. */
static void ec_timer_start(ec_timert * timer)
{
  long  next_msec;

  ec_monotonic_now(&timer->deadline);
  next_msec = (timer->deadline.tv_nsec / (long)EC_NSEC_PER_MSEC) + 1L;
  if(next_msec >= 1000L)
  {
    timer->deadline.tv_nsec = 0L;
    timer->deadline.tv_sec += 1;
  }
  else
  {
    timer->deadline.tv_nsec = next_msec * (long)EC_NSEC_PER_MSEC;
  }
}

static void ec_timer_sleep(ec_timert * timer, int64 nsec,
                           const volatile boolean * dorun)
{
  struct timespec  now;

  ec_monotonic_now(&now);
  if(nsec <= 0)
  {
    /* Do not walk the absolute deadline backwards; resync to monotonic now. */
    timer->deadline = now;
    return;
  }
  if(nsec > EC_MAX_SLEEP_NSEC)
  {
    nsec = EC_MAX_SLEEP_NSEC;
  }
  ec_timespec_add(&timer->deadline, nsec);
  if(ec_timespec_le(&timer->deadline, &now))
  {
    /* This cycle overran past the next deadline: resync instead of firing
       un-throttled catch-up bursts. */
    timer->deadline = now;
    return;
  }
  while(!ec_timespec_le(&timer->deadline, &now))
  {
    struct timespec        slice;
    const struct timespec *target;

    if(dorun != NULL && !*dorun)
    {
      return;
    }
    slice = now;
    ec_timespec_add(&slice, EC_SLEEP_SLICE_NSEC);
    target = ec_timespec_le(&timer->deadline, &slice) ? &timer->deadline : &slice;
#if defined(__linux__)
    while(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, target, NULL) == EINTR)
    {
    }
#else
    {
      struct timespec  deadline_copy = *target;
      (void)osal_monotonic_sleep(&deadline_copy);
    }
#endif
    if(target == &timer->deadline)
    {
      return;
    }
    ec_monotonic_now(&now);
  }
}

#else  /* no absolute monotonic sleep available here */

typedef struct
{
  int  unused_;   /* a relative sleep keeps no state between cycles */
} ec_timert;

static void ec_timer_start(ec_timert * timer)
{
  (void)timer;
}

static void ec_timer_sleep(ec_timert * timer, int64 nsec,
                           const volatile boolean * dorun)
{
  int64  remaining_usec;

  (void)timer;
  if(nsec <= 0)
  {
    return;
  }
  if(nsec > EC_MAX_SLEEP_NSEC)
  {
    nsec = EC_MAX_SLEEP_NSEC;
  }
  remaining_usec = nsec / EC_NSEC_PER_USEC;
  while(remaining_usec > 0)
  {
    int64  step_usec;

    if(dorun != NULL && !*dorun)
    {
      return;
    }
    step_usec = (remaining_usec > EC_SLEEP_SLICE_USEC)
                ? EC_SLEEP_SLICE_USEC
                : remaining_usec;
    osal_usleep((uint32)step_usec);
    remaining_usec -= step_usec;
  }
}

#endif

/* Combine cycle and toff without signed overflow, clamped to
   [0, EC_MAX_SLEEP_NSEC]. */
static int64 ec_cycle_step(int64 cycle, int64 toff)
{
  if(cycle <= 0)
  {
    return 0;
  }
  if(cycle > EC_MAX_SLEEP_NSEC)
  {
    cycle = EC_MAX_SLEEP_NSEC;
  }
  if(toff < -cycle)
  {
    return 0;
  }
  if(toff > (EC_MAX_SLEEP_NSEC - cycle))
  {
    return EC_MAX_SLEEP_NSEC;
  }
  return cycle + toff;
}

/* -------------------------------------------------------------------static
   DC drift correction. PI controller that locks the host cycle timer to the
   slave reference clock DCtime without signed overflow or integrator windup. */

static void ec_sync(int64 reftime, int64 cycletime, int64 * offsettime,
                    int64 * integral)
{
  int64  r;
  int64  delta;
  int64  half_cycle;
  int64  max_integral;
  int64  correction;

  if(cycletime <= 0)
  {
    /* cycletime comes from the JVM through the parcel: 0 is a division by
       zero and a negative value selects the wrong side of the half cycle. */
    *offsettime = 0;
    return;
  }

  r = reftime % cycletime;
  if(r < 0)
  {
    r += cycletime;
  }
  delta = r - (EC_SYNC_REF_NSEC % cycletime);
  if(delta < 0)
  {
    delta += cycletime;
  }
  half_cycle = cycletime / 2;
  if(delta > half_cycle)
  {
    delta -= cycletime;
  }

  max_integral = ((cycletime / 4) > (INT64_MAX / EC_SYNC_INTEGRATOR_DIV))
                 ? INT64_MAX
                 : (cycletime / 4) * EC_SYNC_INTEGRATOR_DIV;
  if(delta > 0)
  {
    if(*integral < max_integral)
    {
      (*integral)++;
    }
  }
  else if(delta < 0)
  {
    if(*integral > -max_integral)
    {
      (*integral)--;
    }
  }
  if(*integral > max_integral)
  {
    *integral = max_integral;
  }
  else if(*integral < -max_integral)
  {
    *integral = -max_integral;
  }

  correction = -(delta / EC_SYNC_ERROR_SCALE) - (*integral / EC_SYNC_INTEGRATOR_DIV);
  if(correction > half_cycle)
  {
    correction = half_cycle;
  }
  else if(correction < -half_cycle)
  {
    correction = -half_cycle;
  }
  *offsettime = correction;
}

/* ------------------------------------------------------------------exported
   The cyclic thread. Started by ec_run(), stopped by the JVM clearing
   *parcel->dorun; the parcel is released only after this returned (see
   ec_free_parcel).                                                          */

#if defined(_WIN32)
static DWORD WINAPI ecatthread(LPVOID ptr)
#elif defined(__linux__)
static void * ecatthread(void * ptr)
#else
static OSAL_THREAD_FUNC_RT ecatthread(void * ptr)
#endif
{
  ecx_parcelt *      parcel = (ecx_parcelt *)ptr;
  ecx_contextt *     context;
  volatile int *     wkc;
  volatile int64 *   cycletime;
  volatile boolean * dorun;
  volatile boolean * isprocess;
  int64 *            dctime;
  uint8 *            hasdc;
  int                last_wkc;
  int64              cycle;
  int64              toff     = 0;
  int64              integral = 0;
  ec_timert          timer;

  /* Every pointer of the parcel has to be there: this thread runs inside the
     JVM process, so a fault here takes DecoTofu down with it. */
  if(parcel == NULL || parcel->context == NULL || parcel->wkc == NULL
     || parcel->cycletime == NULL || parcel->dorun == NULL
     || parcel->isprocess == NULL)
  {
    goto done;
  }
  context   = parcel->context;
  wkc       = parcel->wkc;
  cycletime = parcel->cycletime;
  dorun     = parcel->dorun;
  isprocess = parcel->isprocess;
  dctime    = &context->DCtime;
  hasdc     = &(context->slavelist[0].hasdc);

  ec_timer_start(&timer);

  if(*dorun)
  {
    *isprocess = TRUE;
    ecx_send_processdata(context);
    for(;;)
    {
      last_wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
      *wkc = last_wkc;
      *isprocess = FALSE;
      if(!*dorun)
      {
        break;
      }

      if(last_wkc > 0 && context->grouplist[0].mbxstatus != NULL
         && context->grouplist[0].mbxtxqueue.mbxmutex != NULL)
      {
        (void)ecx_mbxhandler(context, 0, ECAT_MBX_HANDLER_LIMIT);
      }

      cycle = *cycletime;
      ec_timer_sleep(&timer, ec_cycle_step(cycle, toff), dorun);
      if(!*dorun)
      {
        break;
      }

      *isprocess = TRUE;
      if(*hasdc)
      {
        if(last_wkc > 0)
        {
          ec_sync(*dctime, cycle, &toff, &integral);
        }
      }
      else
      {
        toff = 0;
        integral = 0;
      }
      ecx_send_processdata(context);
    }
  }
  *isprocess = FALSE;

done:
#if defined(_WIN32)
  return 0;
#elif defined(__linux__)
  return NULL;
#else
  return;
#endif
}

/* Start the cyclic thread. Returns 0 when the thread did not start. */
int ec_run(ecx_parcelt * parcel)
{
  if(parcel == NULL || parcel->context == NULL || parcel->thread == NULL
     || parcel->wkc == NULL || parcel->cycletime == NULL
     || parcel->dorun == NULL || parcel->isprocess == NULL
     || ec_thread_is_active(parcel))
  {
    return 0;
  }
  /* Clear the handle first, then trust the handle and not the return value
     (fact F4). The cast avoids C4152 function-pointer conversion warnings
     when passing the entry point through osal_thread_create_rt(). */
  ec_thread_clear(parcel);
  (void)osal_thread_create_rt(parcel->thread, ECAT_THREAD_STACKSIZE,
                              (void *)(uintptr_t)&ecatthread, parcel);
  return ec_thread_is_active(parcel);
}

/* Wait until the cyclic thread left ecatthread(), for at most timeoutMillis
   milliseconds. *dorun = FALSE only asks that thread to leave its loop: it
   keeps reading the parcel and the context until its last datagram is
   answered, so a caller that wants to release the parcel or to close the
   context has to wait this first.

   Returns 1 when the thread is known to have left ecatthread() - the handle is
   released, so calling it twice answers 1 -, 0 when the thread is still
   running, and -1 when this build can not wait the thread of the platform. */
int ec_wait_thread(ecx_parcelt * parcel, int timeoutMillis)
{
#if defined(_WIN32)
  HANDLE  handle;
  DWORD   millis;

  if(parcel == NULL || parcel->thread == NULL)
  {
    return -1;
  }
  handle = *parcel->thread;
  if(handle == NULL)
  {
    /* ec_run() started no thread: there is nothing to wait for. */
    return 1;
  }
  millis = (timeoutMillis < 0) ? INFINITE : (DWORD)timeoutMillis;
  if(WaitForSingleObject(handle, millis) != WAIT_OBJECT_0)
  {
    return 0;
  }
  /* ec_run() owns the handle CreateThread() handed back and nothing in SOEM
     closes it, so closing it here is also what keeps one kernel object from
     leaking every time the ecat is started. */
  (void)CloseHandle(handle);
  ec_thread_clear(parcel);
  return 1;
#elif defined(__linux__)
  pthread_t  thread;

  if(parcel == NULL || parcel->thread == NULL)
  {
    return -1;
  }
  thread = ec_thread_load(parcel);
  if(thread == (pthread_t)0)
  {
    return 1;
  }
  if(timeoutMillis < 0)
  {
    if(pthread_join(thread, NULL) != 0)
    {
      return 0;
    }
    ec_thread_clear(parcel);
    return 1;
  }
#if defined(__GLIBC__)
  if(timeoutMillis == 0)
  {
    if(pthread_tryjoin_np(thread, NULL) != 0)
    {
      return 0;
    }
    ec_thread_clear(parcel);
    return 1;
  }
  {
    struct timespec  deadline;

    if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
      return -1;
    }
    ec_timespec_add(&deadline, (int64)timeoutMillis * EC_NSEC_PER_MSEC);
    if(pthread_timedjoin_np(thread, NULL, &deadline) != 0)
    {
      /* ETIMEDOUT: the thread is still inside ecatthread(). */
      return 0;
    }
    ec_thread_clear(parcel);
    return 1;
  }
#else
  return -1;
#endif
#else
  /* macOS / rtk / other targets give no portable timed-join here: the caller
     falls back to waiting longer than one cycle. */
  (void)parcel;
  (void)timeoutMillis;
  return -1;
#endif
}

/* Adapter list ---------------------------------------------------------------

   ec_adapters() answers the linked list of the adapters of the machine, in the
   order the platform reports them, and ec_free_adapters_list() releases it.
   Windows and Linux enumerate interfaces into zeroed ec_alloc() nodes with
   explicit OOM/NULL handling and release them via osal_free(). Other targets
   delegate to SOEM's ec_find_adapters() / ec_free_adapters(). */
ec_adaptert * ec_adapters(void)
{
#if defined(_WIN32)
  pcap_if_t   * alldevs = NULL;
  pcap_if_t   * device;
  ec_adaptert * adapter;
  ec_adaptert * prev_adapter = NULL;
  ec_adaptert * ret_adapter  = NULL;
  char          errbuf[PCAP_ERRBUF_SIZE];

  if (pcap_findalldevs(&alldevs, errbuf) != 0)
  {
    return NULL;
  }
  for (device = alldevs; device != NULL; device = device->next)
  {
    /* fact F1: allocated zeroed, so an unused name or description stays the
       empty string the JVM reads instead of heap garbage. */
    adapter = (ec_adaptert *)ec_alloc(sizeof(ec_adaptert));
    if (adapter == NULL)
    {
      ec_free_adapters_list(ret_adapter);
      ret_adapter = NULL;
      break;
    }
    if (prev_adapter != NULL)
    {
      prev_adapter->next = adapter;
    }
    else
    {
      ret_adapter = adapter;
    }
    if (device->name != NULL)
    {
      strncpy(adapter->name, device->name, EC_MAXLEN_ADAPTERNAME - 1);
      adapter->name[EC_MAXLEN_ADAPTERNAME - 1] = '\0';
    }
    if (device->description != NULL)
    {
      strncpy(adapter->desc, device->description, EC_MAXLEN_ADAPTERNAME - 1);
      adapter->desc[EC_MAXLEN_ADAPTERNAME - 1] = '\0';
    }
    prev_adapter = adapter;
  }
  if (alldevs != NULL)
  {
    pcap_freealldevs(alldevs);
  }
  return ret_adapter;
#elif defined(__linux__)
  struct if_nameindex * ids;
  size_t                i;
  ec_adaptert         * adapter;
  ec_adaptert         * prev_adapter = NULL;
  ec_adaptert         * ret_adapter  = NULL;

  ids = if_nameindex();
  if (ids == NULL)
  {
    return NULL;
  }
  for (i = 0; ids[i].if_index != 0; i++)
  {
    adapter = (ec_adaptert *)ec_alloc(sizeof(ec_adaptert));
    if (adapter == NULL)
    {
      ec_free_adapters_list(ret_adapter);
      ret_adapter = NULL;
      break;
    }
    if (prev_adapter != NULL)
    {
      prev_adapter->next = adapter;
    }
    else
    {
      ret_adapter = adapter;
    }
    if (ids[i].if_name != NULL)
    {
      strncpy(adapter->name, ids[i].if_name, EC_MAXLEN_ADAPTERNAME - 1);
      adapter->name[EC_MAXLEN_ADAPTERNAME - 1] = '\0';
      strncpy(adapter->desc, ids[i].if_name, EC_MAXLEN_ADAPTERNAME - 1);
      adapter->desc[EC_MAXLEN_ADAPTERNAME - 1] = '\0';
    }
    prev_adapter = adapter;
  }
  if_freenameindex(ids);
  return ret_adapter;
#else
  return ec_find_adapters();
#endif
}

void ec_free_adapters_list(ec_adaptert * list)
{
#if defined(_WIN32) || defined(__linux__)
  while (list != NULL)
  {
    ec_adaptert * next = list->next;
    osal_free(list);
    list = next;
  }
#else
  ec_free_adapters(list);
#endif
}

int ec_stop(ecx_parcelt * parcel, int timeoutMillis)
{
  if(parcel == NULL)
  {
    return -1;
  }
  if(parcel->dorun != NULL)
  {
    *((volatile boolean *)parcel->dorun) = FALSE;
  }
  return ec_wait_thread(parcel, timeoutMillis);
}

const char * ec_version(void)
{
  return DECOTOFUSOEM_VERSION_STRING;
}

size_t ec_sizeof_context(void)
{
  return sizeof(ecx_contextt);
}

size_t ec_sizeof_parcel(void)
{
  return sizeof(ecx_parcelt);
}

size_t ec_sizeof_slave(void)
{
  return sizeof(ec_slavet);
}

size_t ec_sizeof_group(void)
{
  return sizeof(ec_groupt);
}

int ec_max_slave(void)
{
  return EC_MAXSLAVE;
}

int ec_max_group(void)
{
  return EC_MAXGROUP;
}
