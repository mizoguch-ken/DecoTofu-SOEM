#include <stdlib.h>
#include "ethercat.h"

#define EC_NSEC_PER_SEC   1000000000
#define EC_TIMEOUTMON     500

typedef struct ecx_parcel
{
  ecx_contextt        *context;
  OSAL_THREAD_HANDLE  *thread;
  int                 *wkc;
  int64               *cycletime;
  boolean             *dorun;
  boolean             *isprocess;
} ecx_parcelt;

ecx_contextt * ec_malloc_context(void)
{
  ec_slavet       *ec_slave         = malloc(sizeof(ec_slavet) * EC_MAXSLAVE);
  int             *ec_slavecount    = malloc(sizeof(int));
  ec_groupt       *ec_group         = malloc(sizeof(ec_groupt) * EC_MAXGROUP);
  uint8           *ec_esibuf        = malloc(sizeof(uint8) * EC_MAXEEPBUF);
  uint32          *ec_esimap        = malloc(sizeof(uint32) * EC_MAXEEPBITMAP);
  ec_eringt       *ec_elist         = malloc(sizeof(ec_eringt));
  ec_idxstackT    *ec_idxstack      = malloc(sizeof(ec_idxstackT));
  ec_SMcommtypet  *ec_SMcommtype    = malloc(sizeof(ec_SMcommtypet) * EC_MAX_MAPT);
  ec_PDOassignt   *ec_PDOassign     = malloc(sizeof(ec_PDOassignt) * EC_MAX_MAPT);
  ec_PDOdesct     *ec_PDOdesc       = malloc(sizeof(ec_PDOdesct) * EC_MAX_MAPT);
  ec_eepromSMt    *ec_SM            = malloc(sizeof(ec_eepromSMt));
  ec_eepromFMMUt  *ec_FMMU          = malloc(sizeof(ec_eepromFMMUt));
  boolean         *EcatError        = malloc(sizeof(boolean));
  int64           *ec_DCtime        = malloc(sizeof(int64));
  ecx_portt       *ecx_port         = malloc(sizeof(ecx_portt));
  ecx_redportt    *ecx_redport      = malloc(sizeof(ecx_redportt));
  ecx_contextt    *ecx_context      = malloc(sizeof(ecx_contextt));
  ecx_redport->sockhandle           = 0;
  ecx_port->redport                 = ecx_redport;
  ecx_context->port                 = ecx_port;
  ecx_context->slavelist            = ec_slave;
  ecx_context->slavecount           = ec_slavecount;
  ecx_context->maxslave             = EC_MAXSLAVE;
  ecx_context->grouplist            = ec_group;
  ecx_context->maxgroup             = EC_MAXGROUP;
  ecx_context->esibuf               = ec_esibuf;
  ecx_context->esimap               = ec_esimap;
  ecx_context->esislave             = 0;
  ecx_context->elist                = ec_elist;
  ecx_context->idxstack             = ec_idxstack;
  ecx_context->ecaterror            = EcatError;
  ecx_context->DCtime               = ec_DCtime;
  ecx_context->SMcommtype           = ec_SMcommtype;
  ecx_context->PDOassign            = ec_PDOassign;
  ecx_context->PDOdesc              = ec_PDOdesc;
  ecx_context->eepSM                = ec_SM;
  ecx_context->eepFMMU              = ec_FMMU;
  ecx_context->FOEhook              = NULL;
  ecx_context->EOEhook              = NULL;
  ecx_context->manualstatechange    = 0;
  ecx_context->userdata             = NULL;
  return ecx_context;
}

void ec_free_context(ecx_contextt * context)
{
  if(context != NULL)
  {
    free(context->port->redport);
    free(context->port);
    free(context->slavelist);
    free(context->slavecount);
    free(context->grouplist);
    free(context->esibuf);
    free(context->esimap);
    free(context->elist);
    free(context->idxstack);
    free(context->ecaterror);
    free(context->DCtime);
    free(context->SMcommtype);
    free(context->PDOassign);
    free(context->PDOdesc);
    free(context->eepSM);
    free(context->eepFMMU);
    free(context);
  }
}

ecx_redportt * ec_redport(ecx_contextt * context)
{
  return context->port->redport;
}

ecx_parcelt * ec_malloc_parcel(ecx_contextt * context)
{
  OSAL_THREAD_HANDLE  *thread     = malloc(sizeof(OSAL_THREAD_HANDLE));
  int                 *wkc        = malloc(sizeof(int));
  int64               *cycletime  = malloc(sizeof(int64));
  boolean             *dorun      = malloc(sizeof(boolean));
  boolean             *isprocess  = malloc(sizeof(boolean));
  ecx_parcelt         *parcel     = malloc(sizeof(ecx_parcelt));
  parcel->context                 = context;
  parcel->thread                  = thread;
  parcel->wkc                     = wkc;
  parcel->cycletime               = cycletime;
  parcel->dorun                   = dorun;
  parcel->isprocess               = isprocess;
  return parcel;
}

void ec_free_parcel(ecx_parcelt * parcel)
{
  if(parcel != NULL)
  {
    free(parcel->thread);
    free(parcel->wkc);
    free(parcel->cycletime);
    free(parcel->dorun);
    free(parcel->isprocess);
    free(parcel);
  }
}

void add_timespec(struct timespec * ts, int64 addtime)
{
  int64 sec, nsec;

  nsec = addtime % EC_NSEC_PER_SEC;
  sec = (addtime - nsec) / EC_NSEC_PER_SEC;
  ts->tv_sec += sec;
  ts->tv_nsec += (long)nsec;
  if(ts->tv_nsec > EC_NSEC_PER_SEC)
  {
    nsec = ts->tv_nsec % EC_NSEC_PER_SEC;
    ts->tv_sec += (ts->tv_nsec - nsec) / EC_NSEC_PER_SEC;
    ts->tv_nsec = (long)nsec;
  }
}

void ec_sync(int64 *reftime, int64 *cycletime , int64 *offsettime, int64 *integral)
{
  int64 delta;

  delta = (*reftime - 50000) % *cycletime;
  if(delta > (*cycletime / 2))
  {
    delta -= *cycletime;
  }
  if(delta > 0)
  {
    (*integral)++;
  }
  if(delta < 0)
  {
    (*integral)--;
  }
  *offsettime = -(delta / 100) - (*integral / 20);
}

OSAL_THREAD_FUNC_RT ecatthread(void * ptr)
{
  ecx_parcelt *parcel = (ecx_parcelt *)ptr;
  ecx_contextt *context = parcel->context;
  int *wkc = parcel->wkc;
  int64 *cycletime = parcel->cycletime;
  boolean *dorun = parcel->dorun;
  boolean *isprocess = parcel->isprocess;

  int64 toff = 0;
  int64 integral = 0;
  int64 *DCtime = context->DCtime;
  uint8 *hasdc = &(context->slavelist[0].hasdc);

#ifdef __linux__
  struct timespec ts, tleft;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ts.tv_nsec = ((ts.tv_nsec / 1000000) + 1) * 1000000;
#endif

  ecx_send_processdata(context);
  while(*dorun)
  {
    *wkc = ecx_receive_processdata(context, EC_TIMEOUTRET);
    *isprocess = FALSE;

#ifdef _WIN32
    osal_usleep((int32)(((*cycletime) + toff) / 1000));
#elif __linux__
    add_timespec(&ts, (*cycletime) + toff);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, &tleft);
#endif

    *isprocess = TRUE;
    if(*hasdc)
    {
      ec_sync(DCtime, cycletime, &toff, &integral);
    }
    ecx_send_processdata(context);
  }
}

int ec_run(ecx_parcelt * parcel)
{
  int ret = 0;

  if(parcel)
  {
    ret = osal_thread_create_rt(parcel->thread, 128000, &ecatthread, parcel);
  }
  return ret;
}
