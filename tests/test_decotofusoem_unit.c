#include "../decotofu/decotofusoem.c"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond)                                                       \
  do                                                                      \
  {                                                                       \
    if(!(cond))                                                           \
    {                                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      failures++;                                                         \
    }                                                                     \
  } while(0)

static void test_ec_sync_cycletime_nonpositive(void)
{
  int64  offsettime = 12345;
  int64  integral   = 77;

  ec_sync(100000, 0, &offsettime, &integral);
  CHECK(offsettime == 0);
  CHECK(integral == 77);

  offsettime = -999;
  ec_sync(100000, -1000000, &offsettime, &integral);
  CHECK(offsettime == 0);
  CHECK(integral == 77);
}

static void test_ec_sync_normal_pi(void)
{
  int64  offsettime = 0;
  int64  integral   = 0;

  /* reftime = 60000, ref = 50000 => delta = +10000 => integral=1, offset=-100 */
  ec_sync(60000, 1000000, &offsettime, &integral);
  CHECK(integral == 1);
  CHECK(offsettime == -100);

  /* reftime = 40000 => delta = -10000 => integral=0, offset=+100 */
  ec_sync(40000, 1000000, &offsettime, &integral);
  CHECK(integral == 0);
  CHECK(offsettime == 100);

  /* reftime = 990000 => r - 50000 = 940000 (> 500000) => wrapped delta = -60000 */
  ec_sync(990000, 1000000, &offsettime, &integral);
  CHECK(integral == -1);
  CHECK(offsettime == 600);
}

static void test_ec_sync_extreme_reftime_and_windup(void)
{
  int64  extremes[] = {
    INT64_MIN,
    INT64_MIN + 1,
    -(INT64_MAX - 1),
    -1LL,
    0LL,
    INT64_MAX - 1,
    INT64_MAX
  };
  size_t  i;
  int64   cycletimes[] = { 1000LL, 1000000LL, INT64_MAX };
  size_t  j;

  for(j = 0; j < sizeof(cycletimes) / sizeof(cycletimes[0]); j++)
  {
    int64  cycle = cycletimes[j];
    int64  half  = cycle / 2;

    for(i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++)
    {
      int64  offsettime = 0;
      int64  integral   = 0;

      ec_sync(extremes[i], cycle, &offsettime, &integral);
      CHECK(offsettime >= -half);
      CHECK(offsettime <= half);
    }
  }

  /* Verify positive/negative integrator clamping (cycle=1000 => max_integral=5000). */
  {
    int64  offsettime = 0;
    int64  integral   = 4999;

    ec_sync(50200, 1000, &offsettime, &integral);
    CHECK(integral == 5000);
    ec_sync(50200, 1000, &offsettime, &integral);
    CHECK(integral == 5000);
    CHECK(offsettime >= -500 && offsettime <= 500);

    integral = -4999;
    ec_sync(49800, 1000, &offsettime, &integral);
    CHECK(integral == -5000);
    ec_sync(49800, 1000, &offsettime, &integral);
    CHECK(integral == -5000);
    CHECK(offsettime >= -500 && offsettime <= 500);

    /* If cycletime shrinks dynamically, pre-existing integral windup clamps down. */
    integral = 1000000;
    ec_sync(50000, 1000, &offsettime, &integral);
    CHECK(integral == 5000);
    CHECK(offsettime >= -500 && offsettime <= 500);
  }
}

static void test_ec_cycle_step(void)
{
  CHECK(ec_cycle_step(0, 100) == 0);
  CHECK(ec_cycle_step(-1000, 500) == 0);
  CHECK(ec_cycle_step(INT64_MIN, 500) == 0);
  CHECK(ec_cycle_step(1000000, -1000001) == 0);
  CHECK(ec_cycle_step(1000000, INT64_MIN) == 0);
  CHECK(ec_cycle_step(1000000, -500) == 999500);
  CHECK(ec_cycle_step(1000000, 500) == 1000500);
  CHECK(ec_cycle_step(INT64_MAX, 1000) == EC_MAX_SLEEP_NSEC);
  CHECK(ec_cycle_step(EC_MAX_SLEEP_NSEC, 1) == EC_MAX_SLEEP_NSEC);
}

#if defined(__linux__) || defined(_WIN32)
static void test_ec_timespec_and_timer(void)
{
  struct timespec  ts;
  ec_timert        timer;
  struct timespec  before;
  struct timespec  after;

  ts.tv_sec = 1;
  ts.tv_nsec = 900000000L;
  ec_timespec_add(&ts, 200000000LL);
  CHECK(ts.tv_sec == 2 && ts.tv_nsec == 100000000L);

  ec_timespec_add(&ts, -200000000LL);
  CHECK(ts.tv_sec == 1 && ts.tv_nsec == 900000000L);

  ts.tv_sec = 10;
  ts.tv_nsec = 500000000L;
  ec_timespec_add(&ts, 2500000000LL);
  CHECK(ts.tv_sec == 13 && ts.tv_nsec == 0L);

  ts.tv_sec = 10;
  ts.tv_nsec = 0L;
  ec_timespec_add(&ts, -2500000000LL);
  CHECK(ts.tv_sec == 7 && ts.tv_nsec == 500000000L);

  ec_timer_start(&timer);
  CHECK(timer.deadline.tv_nsec >= 0L);
  CHECK(timer.deadline.tv_nsec < (long)EC_NSEC_PER_SEC);
  CHECK((timer.deadline.tv_nsec % (long)EC_NSEC_PER_MSEC) == 0L);

  /* Non-positive sleep step must resync cleanly without moving deadline backwards. */
  ec_monotonic_now(&before);
  ec_timer_sleep(&timer, 0, NULL);
  CHECK(ec_timespec_le(&before, &timer.deadline));

  ec_monotonic_now(&before);
  ec_timer_sleep(&timer, -1000000LL, NULL);
  CHECK(ec_timespec_le(&before, &timer.deadline));

  /* Overrun > 1 cycle must resync deadline instead of leaving it 2s in the past. */
  timer.deadline = before;
  timer.deadline.tv_sec -= 2;
  ec_timer_sleep(&timer, 1000000LL, NULL);
  CHECK(ec_timespec_le(&before, &timer.deadline));

  /* Short positive sleep advances deadline and waits until >= deadline. */
  ec_timer_start(&timer);
  ec_timer_sleep(&timer, 1000000LL, NULL);
  ec_monotonic_now(&after);
  CHECK(ec_timespec_le(&timer.deadline, &after));

  /* Long sleep interrupted via dorun = FALSE must return promptly (< 1 s). */
  {
    volatile boolean  dorun_false = FALSE;
    struct timespec   limit;

    ec_timer_start(&timer);
    ec_monotonic_now(&limit);
    ec_timespec_add(&limit, EC_NSEC_PER_SEC);
    ec_timer_sleep(&timer, 5000000000LL, &dorun_false);
    ec_monotonic_now(&after);
    CHECK(ec_timespec_le(&after, &limit));
  }
}
#endif

static void test_context_and_parcel_zero_init(void)
{
  ecx_contextt * ctx = ec_malloc_context();
  ecx_parcelt *  parcel;

  CHECK(ctx != NULL);
  if(ctx != NULL)
  {
    CHECK(ctx->port.redport != NULL);
    CHECK(ec_redport(ctx) == ctx->port.redport);
#if defined(__linux__)
    CHECK(ctx->port.sockhandle == -1);
    CHECK(ctx->port.redport->sockhandle == -1);
#endif
    CHECK(ctx->elist.head == 0);
    CHECK(ctx->elist.tail == 0);
    CHECK(ctx->idxstack.pushed == 0);
    CHECK(ctx->idxstack.pulled == 0);
    CHECK(ctx->ecaterror == FALSE);
    CHECK(ctx->DCtime == 0);

    parcel = ec_malloc_parcel(ctx);
    CHECK(parcel != NULL);
    if(parcel != NULL)
    {
      CHECK(parcel->context == ctx);
      CHECK(parcel->thread != NULL);
      CHECK(!ec_thread_is_active(parcel));
      CHECK(*parcel->wkc == 0);
      CHECK(*parcel->cycletime == 0);
      CHECK(*parcel->dorun == FALSE);
      CHECK(*parcel->isprocess == FALSE);
      ec_free_parcel(parcel);
    }
    ec_free_context(ctx);
  }

  CHECK(ec_redport(NULL) == NULL);
  ec_free_parcel(NULL);
  ec_free_context(NULL);
  ec_free_adapters_list(NULL);
}

static void test_version_and_abi_introspection(void)
{
  CHECK(ec_version() != NULL);
  CHECK(strcmp(ec_version(), DECOTOFUSOEM_VERSION_STRING) == 0);
  CHECK(ec_sizeof_context() == sizeof(ecx_contextt));
  CHECK(ec_sizeof_parcel() == sizeof(ecx_parcelt));
  CHECK(ec_sizeof_slave() == sizeof(ec_slavet));
  CHECK(ec_sizeof_group() == sizeof(ec_groupt));
  CHECK(ec_max_slave() == EC_MAXSLAVE);
  CHECK(ec_max_group() == EC_MAXGROUP);
}

int main(void)
{
  test_ec_sync_cycletime_nonpositive();
  test_ec_sync_normal_pi();
  test_ec_sync_extreme_reftime_and_windup();
  test_ec_cycle_step();
#if defined(__linux__) || defined(_WIN32)
  test_ec_timespec_and_timer();
#endif
  test_context_and_parcel_zero_init();
  test_version_and_abi_introspection();

  if(failures != 0)
  {
    fprintf(stderr, "decotofusoem_unit: %d failure(s)\n", failures);
    return 1;
  }
  printf("decotofusoem_unit: all tests passed\n");
  return 0;
}
