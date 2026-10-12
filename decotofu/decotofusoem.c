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

   Facts about SOEM 2.0.0 this file depends on
   -------------------------------------------
   F1  SOEM is written against statically initialised buffers (see
       SOEM/test/linux/simple_test.c:29-45). malloc() hands out garbage, and
       ecx_pusherror() (SOEM/soem/ethercatmain.c:90) indexes
       ec_eringt.Error[] with ec_eringt.head before any range check, while the
       LRW helpers index ec_idxstackT with its uint8 pushed/pulled counters.
       Every block below is therefore allocated zeroed, and allocated through
       the OSAL allocator so that this shim and SOEM stay consistent on the
       non-libc OSAL targets (intime/rtk/vxworks).
   F2  ecx_init_context() (SOEM/soem/ethercatconfig.c:47-62, called by
       ecx_config_init() at ethercatconfig.c:198) resets slavecount and
       memsets slavelist and grouplist, but never ec_eringt.head/tail,
       ec_idxstackT.pushed/pulled, *ecaterror or *DCtime. Those have to start
       at 0/FALSE here, otherwise ecx_pusherror() writes
       elist.Error[garbage head] out of bounds and the v2 ecx_LRWDC FPRMW
       payload (SOEM/soem/ethercatbase.c:523, DCtE = htoell(*DCtime)) puts a
       garbage DC time on the wire.
   F3  ecx_close() destroys the mutex inside context->mbxpool
       (SOEM/soem/ethercatmain.c:259), and ecx_init() has to have created that
       mutex first: the caller has to run ecx_init(), then ecx_close(), then
       ec_free_context().
   F4  ecx_config_init() -> ecx_initmbxqueue() (ethercatconfig.c:60,
       ethercatmain.c:318) creates one mutex per group and nothing in SOEM ever
       destroys it, while the handle lives inside the grouplist block. That is
       why ec_free_context() walks the groups before releasing the blocks.
   F5  osal_thread_create_rt() on win32 (SOEM/osal/win32/osal.c:145-154) tests
       the wrong operand - if(!thandle) instead of the handle it just wrote -
       and then returns the result of SetThreadPriority(), so its return value
       says nothing about whether CreateThread() succeeded. ec_run() therefore
       validates the handle it produced.
   F6  osal_usleep() takes a uint32 number of microseconds (SOEM/osal/osal.h:51,
       SOEM/osal/win32/osal.c:113), so a negative sleep length wraps into an
       enormous delay. The DC correction of ec_sync() is signed, hence the
       clamp in ec_timer_sleep().
   F7  EC_TIMEOUTMON (500 ms, the monitor-thread timeout of the SOEM examples)
       has no meaning in this file. The timeout used for the cyclic receive is
       EC_TIMEOUTRET, 2000 us (SOEM/soem/ethercattype.h:58).
    F8  oshw_find_adapters() on win32 (SOEM/oshw/win32/oshw.c:37-97) leaves
        prev_adapter uninitialised - the initialisation the linux flavour has
        (SOEM/oshw/linux/oshw.c:47) is missing - so the first device takes the
        "if (prev_adapter)" branch as soon as the stack holds garbage : the
        head of the list is never stored, the function answers NULL for a
        machine that has adapters, and it writes through the garbage pointer
        on the way. ec_adapters() below is that walk done right. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ethercat.h"

/* SOEM 2.0.0 uses struct timespec / clock_gettime on the ELF targets, which
   only come from <time.h>; TIMER_ABSTIME / clock_nanosleep come from
   <pthread.h>. ethercat.h -> ethercattype.h -> osal.h pulls in osal_defs.h,
   stdint.h and stddef.h only, so make the <time.h> dependency explicit.
   pthread_timedjoin_np(), used by ec_wait_thread(), is a GNU extension. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#if defined(__linux__)
#include <pthread.h>
#include <time.h>
#endif

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

/* Stack size of the cyclic thread, the size the SOEM examples use. */
#define ECAT_THREAD_STACKSIZE   128000

/* DC drift correction, the same constants as the reference loop in
   SOEM/test/linux/red_test/red_test.c:183-187. */
#define EC_SYNC_REF_NSEC        50000   /* phase reference inside a cycle  */
#define EC_SYNC_ERROR_SCALE     100     /* ns of drift per integrator step */
#define EC_SYNC_INTEGRATOR_DIV  20      /* leak factor of the integrator   */

/* Parcel: the small set of pointers the JVM and the cyclic thread share.
   The member order is part of the ABI, it is mirrored by SoemEtherCAT.
   ecx_parcelt in DecoTofu: not one member may be added, removed or moved. */
typedef struct ecx_parcel
{
  ecx_contextt        *context;
  OSAL_THREAD_HANDLE  *thread;
  int                 *wkc;
  int64               *cycletime;
  boolean             *dorun;
  boolean             *isprocess;
} ecx_parcelt;

/* Forward declarations: the allocation rollback paths below call the free
   functions that are defined further down. */
void ec_free_context(ecx_contextt * context);
void ec_free_parcel(ecx_parcelt * parcel);

/* One heap block referenced by a pointer member of a dynamically allocated
   SOEM object. Describing the blocks in one table means the allocation, the
   "did something fail?" check and the release cannot drift apart when
   ecx_contextt gains a member: adding one row is the whole change. */
typedef struct
{
  size_t  offset;   /* offsetof(anchor, pointer member) inside the object  */
  size_t  size;     /* bytes to allocate for that member                   */
} ec_blockt;

#define EC_BLOCK(anchor, field, bytes)  { offsetof(anchor, field), (bytes) }
#define EC_BLOCKS(table)                (sizeof(table) / sizeof((table)[0]))

static const ec_blockt ec_context_blocks[] =
{
  EC_BLOCK(ecx_contextt, mbxpool,    sizeof(ec_mbxpoolt)),
  EC_BLOCK(ecx_contextt, port,       sizeof(ecx_portt)),
  EC_BLOCK(ecx_contextt, slavelist,  sizeof(ec_slavet) * EC_MAXSLAVE),
  EC_BLOCK(ecx_contextt, slavecount, sizeof(int)),
  EC_BLOCK(ecx_contextt, grouplist,  sizeof(ec_groupt) * EC_MAXGROUP),
  EC_BLOCK(ecx_contextt, esibuf,     EC_MAXEEPBUF),
  EC_BLOCK(ecx_contextt, esimap,     sizeof(uint32) * EC_MAXEEPBITMAP),
  EC_BLOCK(ecx_contextt, elist,      sizeof(ec_eringt)),
  EC_BLOCK(ecx_contextt, idxstack,   sizeof(ec_idxstackT)),
  EC_BLOCK(ecx_contextt, ecaterror,  sizeof(boolean)),
  EC_BLOCK(ecx_contextt, DCtime,     sizeof(int64)),
  EC_BLOCK(ecx_contextt, SMcommtype, sizeof(ec_SMcommtypet) * EC_MAX_MAPT),
  EC_BLOCK(ecx_contextt, PDOassign,  sizeof(ec_PDOassignt) * EC_MAX_MAPT),
  EC_BLOCK(ecx_contextt, PDOdesc,    sizeof(ec_PDOdesct) * EC_MAX_MAPT),
  EC_BLOCK(ecx_contextt, eepSM,      sizeof(ec_eepromSMt)),
  EC_BLOCK(ecx_contextt, eepFMMU,    sizeof(ec_eepromFMMUt))
};

/* The blocks of the parcel, same mechanism. parcel->context points at memory
   owned by the context, so it has no row here. */
static const ec_blockt ec_parcel_blocks[] =
{
  EC_BLOCK(ecx_parcelt, thread,     sizeof(OSAL_THREAD_HANDLE)),
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

/* Address of the pointer member one row of a block table describes. Every
   data pointer of ecx_contextt / ecx_parcelt has the same representation on
   every target this shim is built for, which is what makes the offset lookup
   safe. */
static void ** ec_block_slot(void * object, const ec_blockt * block)
{
  return (void **)((char *)object + block->offset);
}

/* Allocate every block of a table. Returns the number of rows that could not
   be filled; the members left NULL are released as a no-op afterwards, so the
   rollback of a half built object is complete. */
static size_t ec_alloc_blocks(void * object, const ec_blockt * table, size_t rows)
{
  size_t  row;
  size_t  missing = 0;

  for(row = 0; row < rows; row++)
  {
    void ** slot = ec_block_slot(object, &table[row]);

    *slot = ec_alloc(table[row].size);
    if(*slot == NULL)
    {
      missing++;
    }
  }
  return missing;
}

/* Release every block of a table and clear the member, so the object cannot
   hand out a pointer that has already been released. */
static void ec_free_blocks(void * object, const ec_blockt * table, size_t rows)
{
  size_t  row;

  for(row = 0; row < rows; row++)
  {
    void ** slot = ec_block_slot(object, &table[row]);

    osal_free(*slot);
    *slot = NULL;
  }
}

/* ------------------------------------------------------------------exported
   Context lifecycle                                                       */

ecx_contextt * ec_malloc_context(void)
{
  ecx_contextt * context = ec_alloc(sizeof(ecx_contextt));

  if(context == NULL)
  {
    return NULL;
  }
  if(ec_alloc_blocks(context, ec_context_blocks,
                     EC_BLOCKS(ec_context_blocks)) != 0)
  {
    /* ec_free_context() takes a half built context apart: the members the
       table could not fill are still NULL and osal_free(NULL) does nothing. */
    ec_free_context(context);
    return NULL;
  }
  /* The redundancy bookkeeping hangs off ecx_portt instead of ecx_contextt,
     so it cannot be a row of the table. */
  context->port->redport = ec_alloc(sizeof(ecx_redportt));
  if(context->port->redport == NULL)
  {
    ec_free_context(context);
    return NULL;
  }
  /* The sizes the table cannot express, plus the members SOEM inspects without
     ever initialising them (fact F2, the zeroed allocation covers them all). */
  context->maxslave          = EC_MAXSLAVE;
  context->maxgroup          = EC_MAXGROUP;
  context->esislave          = 0;
  context->FOEhook           = NULL;
  context->EOEhook           = NULL;
  context->manualstatechange = 0;
  context->userdata          = NULL;
  return context;
}

void ec_free_context(ecx_contextt * context)
{
  int  group;

  if(context == NULL)
  {
    return;
  }
  if(context->grouplist != NULL)
  {
    /* Fact F4: one mailbox transmit mutex per group, created by
       ecx_config_init() and never destroyed by SOEM. The blocks were
       allocated zeroed, so a NULL handle means "this group was never
       configured", and a half built context has maxgroup == 0. */
    for(group = 0; group < context->maxgroup; group++)
    {
      if(context->grouplist[group].mbxtxqueue.mbxmutex != NULL)
      {
        osal_mutex_destroy(context->grouplist[group].mbxtxqueue.mbxmutex);
        context->grouplist[group].mbxtxqueue.mbxmutex = NULL;
      }
    }
  }
  if(context->port != NULL)
  {
    osal_free(context->port->redport);
    context->port->redport = NULL;
  }
  ec_free_blocks(context, ec_context_blocks, EC_BLOCKS(ec_context_blocks));
  osal_free(context);
}

/* ecx_init_redundant() wants the redport pointer, and the JVM must not have to
   know where it lives inside ecx_portt. */
ecx_redportt * ec_redport(ecx_contextt * context)
{
  if(context == NULL || context->port == NULL)
  {
    return NULL;
  }
  return context->port->redport;
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
    ec_free_blocks(parcel, ec_parcel_blocks, EC_BLOCKS(ec_parcel_blocks));
    parcel->context = NULL;
    osal_free(parcel);
  }
}

/* -------------------------------------------------------------------static
   Cycle timer. The cyclic thread has to wake up once per cycle. Linux gets an
   absolute CLOCK_MONOTONIC deadline so that one slow cycle does not shift the
   whole series, the other OSAL targets only have a relative sleep.           */

#if defined(__linux__)

typedef struct
{
  struct timespec  deadline;   /* next wake-up, absolute CLOCK_MONOTONIC */
  struct timespec  remaining;  /* output of clock_nanosleep, unused      */
} ec_timert;

/* Add nanoseconds to an absolute timespec. */
static void ec_timespec_add(struct timespec * ts, int64 nsec)
{
  int64  sec;
  int64  rest;

  rest = nsec % EC_NSEC_PER_SEC;
  sec  = (nsec - rest) / EC_NSEC_PER_SEC;
  ts->tv_sec  += sec;
  ts->tv_nsec += (long)rest;
  while(ts->tv_nsec < 0)
  {
    /* the correction ec_sync() produces is signed, so tv_nsec can go below
       zero */
    ts->tv_nsec += (long)EC_NSEC_PER_SEC;
    ts->tv_sec  -= 1;
  }
  if(ts->tv_nsec >= (long)EC_NSEC_PER_SEC)
  {
    /* clock_nanosleep() rejects a tv_nsec of exactly one second with EINVAL,
       so normalise with '>=' like the SOEM reference does
       (SOEM/test/linux/red_test/red_test.c:161-175). */
    rest = ts->tv_nsec % (long)EC_NSEC_PER_SEC;
    ts->tv_sec  += (ts->tv_nsec - rest) / (long)EC_NSEC_PER_SEC;
    ts->tv_nsec  = (long)rest;
  }
}

/* Round the first deadline up to the next whole millisecond, as the SOEM
   reference does (SOEM/test/linux/red_test/red_test.c:199), so the series of
   cycles starts on a clean millisecond boundary. */
static void ec_timer_start(ec_timert * timer)
{
  clock_gettime(CLOCK_MONOTONIC, &timer->deadline);
  timer->deadline.tv_nsec = ((timer->deadline.tv_nsec / (long)EC_NSEC_PER_MSEC)
                             + 1) * (long)EC_NSEC_PER_MSEC;
}

static void ec_timer_sleep(ec_timert * timer, int64 nsec)
{
  ec_timespec_add(&timer->deadline, nsec);
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &timer->deadline,
                  &timer->remaining);
}

#else  /* no absolute monotonic sleep available through libc here */

typedef struct
{
  int  unused_;   /* a relative sleep keeps no state between cycles */
} ec_timert;

static void ec_timer_start(ec_timert * timer)
{
  (void)timer;
}

static void ec_timer_sleep(ec_timert * timer, int64 nsec)
{
  (void)timer;
  if(nsec > 0)
  {
    osal_usleep((uint32)(nsec / EC_NSEC_PER_USEC));
  }
  /* nsec <= 0 means "this cycle is already late": run the next cycle at once
     instead of sleeping, and never hand a negative length to osal_usleep()
     (fact F6). */
}

#endif

/* -------------------------------------------------------------------static
   DC drift correction. Same loop filter as the SOEM reference examples:
   reftime is the DC time reported by the slaves, *offsettime is what the next
   cycle has to be shorter or longer by, and integral is the integrator state
   the caller keeps between cycles.                                          */

static void ec_sync(int64 * reftime, int64 cycletime, int64 * offsettime,
                    int64 * integral)
{
  int64  delta;

  if(cycletime <= 0)
  {
    /* cycletime comes from the JVM through the parcel: 0 is a division by
       zero and a negative value selects the wrong side of the half cycle. */
    *offsettime = 0;
    return;
  }
  delta = (*reftime - EC_SYNC_REF_NSEC) % cycletime;
  if(delta > (cycletime / 2))
  {
    delta -= cycletime;
  }
  if(delta > 0)
  {
    (*integral)++;
  }
  if(delta < 0)
  {
    (*integral)--;
  }
  *offsettime = -(delta / EC_SYNC_ERROR_SCALE) - (*integral / EC_SYNC_INTEGRATOR_DIV);
}

/* ------------------------------------------------------------------exported
   The cyclic thread. Started by ec_run(), stopped by the JVM clearing
   *parcel->dorun; the parcel is released only after this returned (see
   ec_free_parcel).                                                          */

static OSAL_THREAD_FUNC_RT ecatthread(void * ptr)
{
  ecx_parcelt *      parcel = (ecx_parcelt *)ptr;
  ecx_contextt *     context;
  volatile int *     wkc;
  volatile int64 *   cycletime;
  volatile boolean * dorun;
  volatile boolean * isprocess;
  int64 *            dctime;
  uint8 *            hasdc;
  int64              toff     = 0;
  int64              integral = 0;
  ec_timert          timer;

  /* Every pointer of the parcel has to be there: this thread runs inside the
     JVM process, so a fault here takes DecoTofu down with it. */
  if(parcel == NULL || parcel->context == NULL || parcel->wkc == NULL
     || parcel->cycletime == NULL || parcel->dorun == NULL
     || parcel->isprocess == NULL
     || parcel->context->slavelist == NULL
     || parcel->context->DCtime == NULL)
  {
    return;
  }
  context   = parcel->context;
  wkc       = parcel->wkc;
  cycletime = parcel->cycletime;
  dorun     = parcel->dorun;
  isprocess = parcel->isprocess;
  dctime    = context->DCtime;
  hasdc     = &(context->slavelist[0].hasdc);

  ec_timer_start(&timer);

  ecx_send_processdata(context);
  while(*dorun)
  {
    *wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
    *isprocess = FALSE;

    ec_timer_sleep(&timer, *cycletime + toff);

    *isprocess = TRUE;
    if(*hasdc)
    {
      ec_sync(dctime, *cycletime, &toff, &integral);
    }
    ecx_send_processdata(context);
  }
}

/* Start the cyclic thread. Returns 0 when the thread did not start. */
int ec_run(ecx_parcelt * parcel)
{
  if(parcel == NULL || parcel->context == NULL || parcel->thread == NULL)
  {
    return 0;
  }
  /* Clear the handle first, then trust the handle and not the return value
     (fact F5). The cast is needed because osal_thread_create_rt() takes the
     entry point as a void *, which is not a function pointer (C4152). */
  *parcel->thread = 0;
  (void)osal_thread_create_rt(parcel->thread, ECAT_THREAD_STACKSIZE,
                              (void *)&ecatthread, parcel);
  return (*parcel->thread != 0);
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
  *parcel->thread = 0;
  return 1;
#elif defined(__linux__) && defined(__GLIBC__)
  /* On the ELF targets OSAL_THREAD_HANDLE is "pthread_t *", so the handle of
     the parcel is the storage pthread_create() wrote the thread id in. */
  pthread_t      *threadp;
  struct timespec deadline;
  long long       millis;

  if(parcel == NULL || parcel->thread == NULL)
  {
    return -1;
  }
  threadp = (pthread_t *)(void *)parcel->thread;
  if(*threadp == 0)
  {
    return 1;
  }
  if(clock_gettime(CLOCK_REALTIME, &deadline) != 0)
  {
    return -1;
  }
  millis = (timeoutMillis < 0) ? 0x7FFFFFFFLL : (long long)timeoutMillis;
  deadline.tv_sec  += (time_t)(millis / 1000);
  deadline.tv_nsec += (long)((millis % 1000) * 1000000L);
  if(deadline.tv_nsec >= 1000000000L)
  {
    deadline.tv_sec  += 1;
    deadline.tv_nsec -= 1000000000L;
  }
  if(pthread_timedjoin_np(*threadp, NULL, &deadline) != 0)
  {
    /* ETIMEDOUT: the thread is still inside ecatthread(). */
    return 0;
  }
  *threadp = 0;
  return 1;
#else
  /* macOS (pthread_t without a timed join), intime, rtk and vxworks give no
     portable way to wait a thread handle: the caller has to fall back to
     waiting longer than one cycle. */
  (void)parcel;
  (void)timeoutMillis;
  return -1;
#endif
}

/* Adapter list ---------------------------------------------------------------

   ec_adapters() answers the linked list of the adapters of the machine, in the
   order the platform reports them, and ec_free_adapters_list() releases it.
   The pair is exported instead of SOEM's ec_find_adapters() /
   ec_free_adapters() pair, which is broken on Windows by fact F8; the ELF
   targets have no such flaw, so they keep the SOEM answer there. A list
   answered by ec_adapters() is released by ec_free_adapters_list() only: the
   nodes of the Windows list are allocated here, and SOEM would release them
   with its own allocator. */
ec_adaptert * ec_adapters(void)
{
#if defined(_WIN32)
  pcap_if_t   * alldevs;
  pcap_if_t   * device;
  ec_adaptert * adapter;
  ec_adaptert * prev_adapter = NULL;
  ec_adaptert * ret_adapter  = NULL;
  char          errbuf[PCAP_ERRBUF_SIZE];

  if (pcap_findalldevs(&alldevs, errbuf) == -1)
  {
    return NULL;
  }
  for (device = alldevs; device != NULL; device = device->next)
  {
    /* fact F1: allocated zeroed, so an unused name or description stays the
       empty string the JVM reads instead of heap garbage. */
    adapter = (ec_adaptert *)calloc(1, sizeof(ec_adaptert));
    if (adapter == NULL)
    {
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
    }
    if (device->description != NULL)
    {
      strncpy(adapter->desc, device->description, EC_MAXLEN_ADAPTERNAME - 1);
    }
    prev_adapter = adapter;
  }
  pcap_freealldevs(alldevs);
  return ret_adapter;
#else
  return ec_find_adapters();
#endif
}

void ec_free_adapters_list(ec_adaptert * list)
{
#if defined(_WIN32)
  while (list != NULL)
  {
    ec_adaptert * next = list->next;
    free(list);
    list = next;
  }
#else
  ec_free_adapters(list);
#endif
}