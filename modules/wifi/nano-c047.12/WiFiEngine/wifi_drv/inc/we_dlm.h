
#ifndef _WE_DLM_H_
#define _WE_DLM_H_

#include "driverenv.h"
#include "ucos.h"

#include "wei_list.h"
#include "wei_netlist.h"
#include "wifi_engine_internal.h"
#include "m80211_stddefs.h"
#include "hmg_defs.h"
#include "registry.h"
#include "registryAccess.h"
#include "mlme_proxy.h"
#include "wei_asscache.h"
#include "we_dump.h"
#include "macWrapper.h"
#include "hicWrapper.h"
#include "mlme_proxy.h"
#include "wifi_engine.h"

struct dlm_ops_t {
   int (*get_data)(struct dlm_ops_t* ops, int offset, int len, char **p, uint32_t *remaining);
   int (*free_data)(struct dlm_ops_t* ops, char* data);
   void *priv;
};

typedef struct dlm_ops_t* (*dlm_ops_find)(uint32_t id, uint32_t size);

void we_dlm_initialize(void);
void we_dlm_shutdown(void);
void we_dlm_register_adapter(dlm_ops_find cb);

#ifdef WE_DLM_DYNAMIC
void we_dlm_flush(void);

char* we_dlm_register(
      const char*   dlm_name, 
      unsigned int  dlm_id, 
      unsigned int  dlm_size);
char *dynamic_dlm_ops_find_mib_table(const char* dlm_name);
#endif

#endif /* _WE_DLM_H_ */
