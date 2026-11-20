#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#include <time.h>
#endif

#include "decotofusoem.h"

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

static int is_symbol_line(const char * s)
{
  size_t  i;

  if(s[0] == '\0')
  {
    return 0;
  }
  if(!(isalpha((unsigned char)s[0]) || s[0] == '_'))
  {
    return 0;
  }
  for(i = 1; s[i] != '\0'; i++)
  {
    if(!(isalnum((unsigned char)s[i]) || s[i] == '_'))
    {
      return 0;
    }
  }
  return 1;
}

static void test_all_exports_resolve(void)
{
  FILE *  fp;
  char    line[256];
  int     count = 0;
#if defined(_WIN32)
  HMODULE handle;
#else
  void *  handle;
#endif

  fp = fopen(DECOTOFUSOEM_EXPORTS_PATH, "r");
  CHECK(fp != NULL);
  if(fp == NULL)
  {
    return;
  }

#if defined(_WIN32)
  handle = GetModuleHandleA(DECOTOFUSOEM_LIBRARY_NAME);
  if(handle == NULL)
  {
    handle = GetModuleHandleA("decotofusoem.dll");
  }
  CHECK(handle != NULL);
#else
  handle = dlopen(DECOTOFUSOEM_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
  CHECK(handle != NULL);
#endif

  if(handle != NULL)
  {
    while(fgets(line, sizeof(line), fp) != NULL)
    {
      size_t  len = strlen(line);

      while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      {
        line[--len] = '\0';
      }
      if(!is_symbol_line(line))
      {
        continue;
      }
      count++;
#if defined(_WIN32)
      if(GetProcAddress(handle, line) == NULL)
      {
        fprintf(stderr, "FAIL: missing exported symbol '%s'\n", line);
        failures++;
      }
#else
      if(dlsym(handle, line) == NULL)
      {
        fprintf(stderr, "FAIL: missing exported symbol '%s'\n", line);
        failures++;
      }
#endif
    }
#if !defined(_WIN32)
    (void)dlclose(handle);
#endif
  }

  (void)fclose(fp);
  CHECK(count == 117);
}

static void test_introspection(void)
{
  const char * ver = ec_version();

  CHECK(ver != NULL);
  CHECK(strcmp(ver, DECOTOFUSOEM_VERSION_STRING) == 0);
  CHECK(ec_sizeof_context() == sizeof(ecx_contextt));
  CHECK(ec_sizeof_parcel() == sizeof(ecx_parcelt));
  CHECK(ec_sizeof_slave() == sizeof(ec_slavet));
  CHECK(ec_sizeof_group() == sizeof(ec_groupt));
  CHECK(ec_max_slave() == EC_MAXSLAVE);
  CHECK(ec_max_group() == EC_MAXGROUP);
}

static void test_lifecycle_and_thread(void)
{
  ecx_contextt * ctx;
  ecx_parcelt *  parcel;
  ecx_parcelt *  parcel2;
  ecx_parcelt    bad;

  CHECK(ec_run(NULL) == 0);
  CHECK(ec_wait_thread(NULL, 0) == -1);
  CHECK(ec_stop(NULL, 0) == -1);
  CHECK(ec_redport(NULL) == NULL);
  ec_free_parcel(NULL);
  ec_free_context(NULL);
  ec_free_adapters_list(NULL);

  ctx = ec_malloc_context();
  CHECK(ctx != NULL);
  if(ctx == NULL)
  {
    return;
  }
  CHECK(ec_redport(ctx) != NULL);

  parcel = ec_malloc_parcel(ctx);
  CHECK(parcel != NULL);
  if(parcel != NULL)
  {
    CHECK(parcel->context == ctx);
    CHECK(*parcel->dorun == FALSE);
    CHECK(*parcel->isprocess == FALSE);
    CHECK(*parcel->wkc == 0);
    CHECK(*parcel->cycletime == 0);

    bad = *parcel;
    bad.context = NULL;
    CHECK(ec_run(&bad) == 0);

    bad = *parcel;
    bad.thread = NULL;
    CHECK(ec_run(&bad) == 0);

    /* Calling ec_wait_thread / ec_stop before ec_run reports no active thread
       and resets *parcel->dorun to FALSE. */
    CHECK(ec_wait_thread(parcel, 0) == 1);
    *parcel->dorun = TRUE;
    CHECK(ec_stop(parcel, 0) == 1);
    CHECK(*parcel->dorun == FALSE);

    /* Run 1: *dorun is FALSE, so ecatthread exits immediately without sending
       frames on any NIC; duplicate ec_run before wait must be rejected. */
    *parcel->cycletime = 1000000;
    *parcel->dorun = FALSE;
    CHECK(ec_run(parcel) != 0);
    CHECK(ec_run(parcel) == 0);
    CHECK(ec_wait_thread(parcel, 2000) == 1);
    CHECK(ec_wait_thread(parcel, 0) == 1);
    CHECK(*parcel->isprocess == FALSE);

    /* Run 2: ec_run followed by ec_stop joins cleanly. */
    CHECK(ec_run(parcel) != 0);
    CHECK(ec_stop(parcel, 2000) == 1);
    CHECK(*parcel->dorun == FALSE);

    ec_free_parcel(parcel);
  }

  /* ec_free_parcel must also release the native thread handle/pthread cleanly
     when a finished thread was not joined via ec_wait_thread. */
  parcel2 = ec_malloc_parcel(ctx);
  CHECK(parcel2 != NULL);
  if(parcel2 != NULL)
  {
    *parcel2->cycletime = 1000000;
    *parcel2->dorun = FALSE;
    CHECK(ec_run(parcel2) != 0);
#if defined(_WIN32)
    CHECK(*(HANDLE *)parcel2->thread != NULL);
    CHECK(WaitForSingleObject(*(HANDLE *)parcel2->thread, 2000) == WAIT_OBJECT_0);
#else
    {
      struct timespec  delay = { 0, 20000000L };
      (void)nanosleep(&delay, NULL);
    }
#endif
    ec_free_parcel(parcel2);
  }

  ec_free_context(ctx);
}

static void test_adapters_enumeration(void)
{
  ec_adaptert * list = ec_adapters();
  ec_adaptert * cur  = list;
  int           hops = 0;

  while(cur != NULL && hops < 1024)
  {
    CHECK(memchr(cur->name, '\0', EC_MAXLEN_ADAPTERNAME) != NULL);
    CHECK(memchr(cur->desc, '\0', EC_MAXLEN_ADAPTERNAME) != NULL);
    cur = cur->next;
    hops++;
  }
  CHECK(hops < 1024);
  ec_free_adapters_list(list);
}

int main(void)
{
  test_all_exports_resolve();
  test_introspection();
  test_lifecycle_and_thread();
  test_adapters_enumeration();

  if(failures != 0)
  {
    fprintf(stderr, "decotofusoem_abi: %d failure(s)\n", failures);
    return 1;
  }
  printf("decotofusoem_abi: all tests passed\n");
  return 0;
}
