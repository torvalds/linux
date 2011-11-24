#include "driverenv.h"
#include "state_trace.h"
#include "registryAccess.h"

struct tl trace_log;
struct th transid_hist;

#define TRACE_LOG_INITIALIZED 0xDD

#ifdef ENABLE_STATE_TRACE

void init_state_trace(int size)
{
   if (trace_log.initialized != TRACE_LOG_INITIALIZED)
   {
      trace_log.log = (char*)DriverEnvironment_Malloc(size);
      DE_MEMSET(trace_log.log, TRACE_STATE_UNUSED, size);
      trace_log.size = size;
      trace_log.current_p = trace_log.log;
      trace_log.initialized = TRACE_LOG_INITIALIZED;
   }
}

void deinit_state_trace(void)
{
   if (trace_log.initialized == TRACE_LOG_INITIALIZED && trace_log.log)
   {
      DriverEnvironment_Free(trace_log.log);
      trace_log.initialized = 0x00;
   }
}

void state_trace(char state)
{
   char *p;


   if (trace_log.initialized != TRACE_LOG_INITIALIZED)
      init_state_trace(TRACE_LOG_SIZE);
   p = trace_log.current_p;

   *p = state;
   p++;
   if (trace_log.log + trace_log.size == p)
      p = trace_log.log;
   trace_log.current_p = p;
}


void transid_hist_init(void)
{
   DE_MEMSET(transid_hist.trans_ids, 0xff, sizeof transid_hist.trans_ids);
   transid_hist.idx = 0;
}

void log_transid(uint32_t id)
{
   int oidx;
   volatile struct th *foo;

   foo = &transid_hist;
   oidx = transid_hist.idx - 1;
   if (oidx < 0)
   {
      oidx = 31;
   }
   transid_hist.trans_ids[transid_hist.idx] = id;
   DE_ASSERT(transid_hist.trans_ids[oidx] != transid_hist.trans_ids[transid_hist.idx]);
                
   transid_hist.idx++;
   transid_hist.idx = transid_hist.idx % 32;
   transid_hist.trans_ids[transid_hist.idx] = 0xFFFFFFFF;
}

#endif /* ENABLE_STATE_TRACE */
