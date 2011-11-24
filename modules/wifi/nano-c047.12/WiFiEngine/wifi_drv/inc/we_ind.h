
#ifndef __we_ind_h__
#define __we_ind_h__

#include "driverenv_kref.h"

/* require:
 *
 * containerof macro (offsetof derivative)
 * kref
 * tailq
 *
 */

/*
Example 1: (plain)
----------

void my_ind(wi_msg_param_t param, void* priv)
{
   // do stuff
}

void my_release(void* priv)
{
   free(priv);
}

handler = WiFiEngine_Register_Ind_Handler(
   WE_IND_TYPE, 
   WE_IND_TYPE#, 
   my_ind,
   my_release, 
   0,
   my_priv_data_that_will_be_freed_by_release_on_deregister);


we_ind_send(WE_IND_TYPE, param);

if (we_ind_deregister(handler)
      != WIFI_ENGINE_SUCCESS ) {
   // protect from concurrency
}


Example 2: (safe self-deregister from within an indication; not using deregister_on_ind flag)
---------- 
use this with caution for this is a potential well of race conditions

...
*ref = we_ind_register_ind(..., func_that_deregister,1, ref);
don't touch ref after this
...
void func_that_deregister(wi_msg_param_t param, void* priv)
{
   iobject_t *ref = (iobject_t*)priv;
   if(we_ind_deregister(ref)
         != WIFI_ENGINE_SUCCESS ) {
      // add this to protect from concurrency
      return;
   }

   // do stuff here

}

we_ind_send(WE_IND_TYPE, param);

*/

typedef we_indication_t wi_msg_id_t;
typedef char* wi_msg_string_t;

typedef void* wi_msg_param_t;
typedef void (i_func_t)(wi_msg_param_t param, void* priv);
typedef void (i_release_t)(void* priv);

#define RELEASE_IND_NEVER             0
#define RELEASE_IND_AFTER_EVENT   (1<<0)
#define RELEASE_IND_ON_UNPLUG     (1<<1)

typedef struct iobject {
   struct de_kref  kref;
   WEI_TQ_ENTRY(iobject) entry;
   char            *iname; /* for ease of debugging */
   int             release_on_mask;
   wi_msg_id_t     type;
   i_func_t        *func;
   i_release_t     *release;
   void            *priv;
} iobject_t;

/*
 * Init the indication handler
 */
void we_ind_init(void);

/* 
 * Register a new indication
 */
struct iobject* we_ind_register(
            wi_msg_id_t id,
            char* msg,
            i_func_t func,
            i_release_t release,
            int release_on_mask,
            void *priv);

/* 
 * Register a new indication
 */
int we_ind_cond_register(
            struct iobject **hhandler, 
            wi_msg_id_t id, 
            wi_msg_string_t msg, 
            i_func_t func,
            i_release_t release,
            int release_on_mask,
            void *priv);

/* 
 * Deregister an old indication
 */
int we_ind_deregister(struct iobject *handler);

/* 
 * Deregister and assign NULL to an old indication
 */
int we_ind_deregister_null(struct iobject **handler);

/* 
 * Send an indication
 *
 * The indication will be recived by all registed handlers at the point
 * in time that the signal was sent.
 *
 */
int we_ind_send(wi_msg_id_t id, wi_msg_param_t param);

/* 
 * Count number of registered indications
 *
 * for debugging only 
 */
int we_ind_count(wi_msg_id_t id);
int we_ind_count_all(void);


#endif /*  __we_ind_h__ */
