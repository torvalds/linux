
#include "driverenv.h"
#include "driverenv_kref.h"
#include "we_ind.h"

struct iobject_ctx_s {
   WEI_TQ_HEAD(iobject_head_s, iobject) head;
   driver_lock_t lock;
};

/* TODO: don't use static */
/* global static struct */
#define WE_IND_CTX (struct iobject_ctx_s*)&iobject_ctx
static struct iobject_ctx_s iobject_ctx;


static struct iobject*
iobject_new(void)
{
   struct iobject *obj = (struct iobject*) DriverEnvironment_Nonpaged_Malloc(sizeof(struct iobject));

   if (obj==NULL)
      return NULL;

   DE_MEMSET(obj,0,sizeof(struct iobject));
   DriverEnvironment_kref_init(&obj->kref);
   return obj;
}


static void 
iobject_cleanup(struct de_kref *obj)
{
   struct iobject *iobj = NULL;

   iobj = container_of(obj, struct iobject, kref);

   if (iobj->release) {
      iobj->release(iobj->priv);
   }

   DriverEnvironment_Nonpaged_Free(iobj);
}


void
we_ind_init(void)
{
   struct iobject_ctx_s *ic = WE_IND_CTX;
   DriverEnvironment_initialize_lock(&ic->lock);
   WEI_TQ_INIT(&ic->head);
}


struct iobject*
we_ind_register(
            wi_msg_id_t id,
            char* msg,
            i_func_t func,
            i_release_t release,
            int release_on_mask,
            void *priv)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm = NULL;

   elm = iobject_new();
   if (elm==NULL)
      return NULL;

   elm->type = id;
   elm->iname = msg;
   elm->func = func;
   elm->priv = priv;
   elm->release = release;
   elm->release_on_mask = release_on_mask;

   ic = WE_IND_CTX;

   DriverEnvironment_acquire_lock(&ic->lock);
   WEI_TQ_INSERT_TAIL(&ic->head, elm, entry);
   DriverEnvironment_release_lock(&ic->lock);

   return elm;
}


int
we_ind_cond_register(
            struct iobject **hhandler, 
            wi_msg_id_t id, 
            wi_msg_string_t msg, 
            i_func_t func,
            i_release_t release,
            int release_on_mask,
            void *priv)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm;

   if(hhandler == NULL) {
      return WIFI_ENGINE_FAILURE;
   }

   if(*hhandler == NULL) {
      *hhandler = we_ind_register( id, msg, func, release, release_on_mask, priv);
      if(*hhandler)
         return WIFI_ENGINE_SUCCESS;
      else
         return WIFI_ENGINE_FAILURE;
   }

   ic = WE_IND_CTX;

   DriverEnvironment_acquire_lock(&ic->lock); 
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      if(elm == *hhandler) {
         DriverEnvironment_release_lock(&ic->lock); 
         return WIFI_ENGINE_SUCCESS;
      }
   }

   /* keep holding the lock so we don't cause a race condition here */

   elm = iobject_new();
   if(elm==NULL) {
      DriverEnvironment_release_lock(&ic->lock); 
      return WIFI_ENGINE_FAILURE;
   }

   elm->type = id;
   elm->iname = msg;
   elm->func = func;
   elm->priv = priv;
   elm->release = release;
   elm->release_on_mask = release_on_mask;

   WEI_TQ_INSERT_TAIL(&ic->head, elm, entry);
   *hhandler = elm;
   DriverEnvironment_release_lock(&ic->lock); 

   return WIFI_ENGINE_SUCCESS;
}

static int
__we_ind_deregister(struct iobject **h, int nullify)
{
   struct iobject *handler;
   struct iobject_ctx_s *ic;
   struct iobject *elm = NULL;
   int num = 0;
   int found = 0;

   if(h == NULL) 
     return WIFI_ENGINE_FAILURE;

   handler = *h;
   if(handler == NULL)
     return WIFI_ENGINE_FAILURE;

   ic = WE_IND_CTX;

   DriverEnvironment_acquire_lock(&ic->lock);
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      num++;
      if (handler==elm) {
         found = 1;
         break;
      }
   }
   if (found)
     WEI_TQ_REMOVE(&ic->head, handler, entry);
   DriverEnvironment_release_lock(&ic->lock);

   //printf("%s: %p num: %d refcount: %d\n",__func__,handler, num, handler->kref.refcount.c);

   if (!found)
     return WIFI_ENGINE_FAILURE;

   DriverEnvironment_kref_put(&handler->kref, iobject_cleanup);

   if(nullify)
     *h = NULL;

   return WIFI_ENGINE_SUCCESS;
}

int
we_ind_deregister(struct iobject *handler) {
   return __we_ind_deregister(&handler, 0);
}

int
we_ind_deregister_null(struct iobject **handler) {
   return __we_ind_deregister(handler, 1);
}

int
we_ind_send(
   wi_msg_id_t id,
   wi_msg_param_t param)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm = NULL;
   int num, i;
   struct iobject **list;

   ic = WE_IND_CTX;

   num=0;
   DriverEnvironment_acquire_lock(&ic->lock);
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      if (elm->type==id) {
         num++;
      }
   }
   if(num==0) {
      DriverEnvironment_release_lock(&ic->lock);
      return 0;
   }
   /* num is only up to date as long as we are holding the lock */
   list = (struct iobject**)DriverEnvironment_Nonpaged_Malloc(num*sizeof(struct iobject*));
   if (list==NULL) {
      /* Oops! only one thing to do... */
      DriverEnvironment_release_lock(&ic->lock);
      return 0;
   }
   i=0;
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      if (elm->type==id) {
         /* make sure that no one try to free elm before we have operated on it */
         DriverEnvironment_kref_get(&elm->kref);
         list[i] = elm;
         if(elm->release_on_mask & RELEASE_IND_AFTER_EVENT) {
            WEI_TQ_REMOVE(&ic->head, elm, entry);
         }
         //printf("%s:inc %p[%d] refcount: %d\n",__func__,elm,i, elm->kref.refcount.c);
         i++;
      }
   }
   DriverEnvironment_release_lock(&ic->lock);

   for (i=0;i<num;i++) {
      elm = list[i];
      //printf("%s:call %p[%d] refcount: %d\n",__func__,elm,i, elm->kref.refcount.c);
      elm->func((wi_msg_param_t*)param, elm->priv);
      if(elm->release_on_mask & RELEASE_IND_AFTER_EVENT) {
         DriverEnvironment_kref_put(&elm->kref, iobject_cleanup);
      }
      DriverEnvironment_kref_put(&elm->kref, iobject_cleanup);
   }

   DriverEnvironment_Nonpaged_Free(list);

   return num;
}


void
wei_ind_unplug(void)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm, *next;
   WEI_TQ_HEAD(, iobject) tmp_list = WEI_TQ_HEAD_INITIALIZER(tmp_list);

   ic = WE_IND_CTX;

   /* move removable entries to separate list */
   DriverEnvironment_acquire_lock(&ic->lock);
   for(elm = WEI_TQ_FIRST(&ic->head); elm != NULL; elm = next) {
      next = WEI_TQ_NEXT(elm, entry);
      if(elm->release_on_mask & RELEASE_IND_ON_UNPLUG)
      {
         WEI_TQ_REMOVE(&ic->head, elm, entry);
         WEI_TQ_INSERT_TAIL(&tmp_list, elm, entry);
      }
      elm = next;
   }
   DriverEnvironment_release_lock(&ic->lock);
   
   /* free removed entries */
   while((elm = WEI_TQ_FIRST(&tmp_list)) != NULL) {
      WEI_TQ_REMOVE(&tmp_list, elm, entry);
      DriverEnvironment_kref_put(&elm->kref, iobject_cleanup);
   }
}


void
wei_ind_shutdown(void)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm;

   ic = WE_IND_CTX;

   DriverEnvironment_acquire_lock(&ic->lock);
   while((elm = WEI_TQ_FIRST(&ic->head)) != NULL) {
      WEI_TQ_REMOVE(&ic->head, elm, entry);
      /* the release function may need the lock, so we relinquish it
       * here */
      DriverEnvironment_release_lock(&ic->lock);
      DriverEnvironment_kref_put(&elm->kref, iobject_cleanup);
      DriverEnvironment_acquire_lock(&ic->lock);
   }
   DriverEnvironment_release_lock(&ic->lock);
}


/* for debugging */
int we_ind_count(wi_msg_id_t id)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm = NULL;
   int num;

   ic = WE_IND_CTX;

   num=0;
   DriverEnvironment_acquire_lock(&ic->lock);
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      if (elm->type==id) {
         num++;
      }
   }
   DriverEnvironment_release_lock(&ic->lock);
   return num;
}

/* for debugging */
int we_ind_count_all(void)
{
   struct iobject_ctx_s *ic;
   struct iobject *elm = NULL;
   int num;

   ic = WE_IND_CTX;

   num=0;
   DriverEnvironment_acquire_lock(&ic->lock);
   WEI_TQ_FOREACH(elm, &ic->head, entry) {
      num++;
   }
   DriverEnvironment_release_lock(&ic->lock);
   return num;
}

