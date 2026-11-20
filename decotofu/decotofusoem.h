#ifndef DECOTOFUSOEM_H
#define DECOTOFUSOEM_H

#include <stddef.h>
#include "soem/soem.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DECOTOFUSOEM_VERSION_STRING
#define DECOTOFUSOEM_VERSION_STRING "2.0.0"
#endif

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

/* Context lifecycle */
ecx_contextt * ec_malloc_context(void);
void ec_free_context(ecx_contextt * context);
ecx_redportt * ec_redport(ecx_contextt * context);

/* Parcel and cyclic thread lifecycle */
ecx_parcelt * ec_malloc_parcel(ecx_contextt * context);
void ec_free_parcel(ecx_parcelt * parcel);
int ec_run(ecx_parcelt * parcel);
int ec_wait_thread(ecx_parcelt * parcel, int timeoutMillis);
int ec_stop(ecx_parcelt * parcel, int timeoutMillis);

/* Adapter enumeration */
ec_adaptert * ec_adapters(void);
void ec_free_adapters_list(ec_adaptert * list);

/* ABI / version introspection */
const char * ec_version(void);
size_t ec_sizeof_context(void);
size_t ec_sizeof_parcel(void);
size_t ec_sizeof_slave(void);
size_t ec_sizeof_group(void);
int ec_max_slave(void);
int ec_max_group(void);

#ifdef __cplusplus
}
#endif

#endif /* DECOTOFUSOEM_H */
