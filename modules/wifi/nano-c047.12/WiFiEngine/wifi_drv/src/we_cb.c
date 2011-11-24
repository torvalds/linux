/* $Id: we_cb.c,v 1.61 2008-05-19 14:57:31 peek Exp $ */
/*****************************************************************************

Copyright (c) 2004 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
*/

/** @defgroup we_cb WiFiEngine Asynch callback interface
 *
 * @brief This module implements the WiFiEngine Asynchronous callback interface

Asynchronous completions are requested by passing argument
 <pre>..., we_cb_container_t *cbc</pre>
to selected API functions that support asynchronous completion.
Completion callbacks are queued upon
request and are moved to the callback queue 
upon completion of the operation. 

Completions are transaction-ID based. A completion is registered for a
transaction ID and is scheduled for completion when a confirm with the
matching transaction-ID is received. This only works for data frames
and MIB operations (right now).

The data passed to the callback is defined by the asynch call that
posted the callback. The data buffer is allocated by WiFiEngine
and it is freed after the completion callback has executed.

*****************************************************************************
*  @{
*/
#include "driverenv.h"
#include "wifi_engine_internal.h"

WEI_TQ_HEAD(we_cb_list, we_cb_container_s);

/*! Scheduled completions (ready to dispatch) */
static struct we_cb_list cb_list_cbq;

/*! List of registered callback containers, used to keep track of
 *  (un-)freed cbcs */
static struct we_cb_list cb_list_all;

/* Transid-based callback table */
static struct we_cb_list cb_list_pending;


/* lock for all three queues, does not lock the cbc itself */
static driver_lock_t cb_lock;

#define CBLOCK() DriverEnvironment_acquire_lock(&cb_lock)
#define CBUNLOCK() DriverEnvironment_release_lock(&cb_lock);

static void wei_cb_clear_cbc_registry_list(void)
{
   we_cb_container_t *cbc;
 
   DE_TRACE_STATIC(TR_WEI, "Clearing cb registry\n");
 
   CBLOCK();
   while((cbc = WEI_TQ_FIRST(&cb_list_all)) != NULL) {
      CBUNLOCK();
      DE_TRACE_PTR(TR_CB, "Clearing cbc %p\n", cbc);
      WiFiEngine_FreeCBC(cbc);
      CBLOCK();
   }
   CBUNLOCK();
}

void wei_cb_init(void)
{
   DriverEnvironment_initialize_lock(&cb_lock);

   WEI_TQ_INIT(&cb_list_all);
   WEI_TQ_INIT(&cb_list_cbq);
   WEI_TQ_INIT(&cb_list_pending);
}

void wei_cb_shutdown(void)
{
   wei_cb_flush_pending_cb_tab();
   wei_cb_clear_cbc_registry_list();

   DriverEnvironment_free_lock(&cb_lock);
}

void wei_cb_unplug(void)
{
   wei_cb_flush_pending_cb_tab();
   wei_cb_clear_cbc_registry_list();
}

void wei_cb_flush_pending_cb_tab(void)
{
   we_cb_container_t *cbc;

   DE_TRACE_STATIC(TR_WEI, "Flushing pending cb tab\n");
   CBLOCK();
   while((cbc = WEI_TQ_FIRST(&cb_list_pending)) != NULL) {
      WEI_TQ_REMOVE(&cb_list_pending, cbc, cb_pending);
      CBUNLOCK();
      WiFiEngine_CancelCallback(cbc);
      CBLOCK();
   }
   CBUNLOCK();
}


/*! Queue a callback on a list for pending callbacks.  It will be
 *  scheduled for completion from this list by some other code, for
 *  example when a packet with a matching ID has been received.
 * @param cbc The input buffer. The trans_id member is used
 *            for ID purposes and must be filled in.
 * @return nothing, always succeeds
 */
void wei_cb_queue_pending_callback(we_cb_container_t *cbc)
{
   DE_TRACE_INT2(TR_CB, "Queueing cbc " TR_FPTR " for trans_id %d\n", 
                 TR_APTR(cbc), cbc->trans_id);
   CBLOCK();
   WEI_TQ_INSERT_TAIL(&cb_list_pending, cbc, cb_pending);
   CBUNLOCK();
}


/*! Find a callback on a pending callback list.
 *  The callback is removed from the list.
 * @param trans_id The transaction ID to look for.
 * @retval a callback handle matching trans_id, or NULL if none was found
 */
we_cb_container_t* wei_cb_find_pending_callback(uint32_t trans_id)
{
   we_cb_container_t *cbc;
   CBLOCK();
   WEI_TQ_FOREACH(cbc, &cb_list_pending, cb_pending) {
      if(cbc->trans_id == trans_id) {
         WEI_TQ_REMOVE(&cb_list_pending, cbc, cb_pending);
         CBUNLOCK();
         return cbc;
      }
   }
   CBUNLOCK();
   return NULL;
}


/*! 
 * Check if the cbc is still on the list. Used in cases it might have been deleted.
 *  
 * @param cbc Pointer to a callback container.
 * @return 
 * - 1 when cbc can be used.
 * - 0 when cbc is no longer valid.
 */
int wei_cb_still_valid(we_cb_container_t *cbc)
{
   we_cb_container_t *cbc2;

   CBLOCK();
   WEI_TQ_FOREACH(cbc2, &cb_list_all, cb_all) {
      if(cbc == cbc2) {
         CBUNLOCK();
         return TRUE;
      }
   }
   CBUNLOCK();
   return FALSE;
}


/*!
 * Cancel a registered callback. This will free the callback container (cbc).
 *
 * @param cbc Pointer to a callback container that was previously registered.
 * @retval WIFI_ENGINE_SUCCESS always succeeds
 */
int WiFiEngine_CancelCallback(we_cb_container_t *cbc)
{
   we_cb_container_t *cbc2;
   
   DE_TRACE_PTR(TR_WEI, "Cancel cbc %p\n", cbc);

   /* cbc == NULL is necessary, but the other conditions just handle a symptom of an earlier bug. */
   if (cbc == NULL || cbc->cb == NULL)
      return WIFI_ENGINE_SUCCESS;
   
   DE_TRACE_PTR(TR_CB, "invoking callback %p\n", cbc->cb);
   
   /* remove from callback queue */
   CBLOCK();
   WEI_TQ_FOREACH(cbc2, &cb_list_cbq, cb_cbq)
      if(cbc == cbc2) {
         WEI_TQ_REMOVE(&cb_list_cbq, cbc2, cb_cbq);
         break;
      }
   CBUNLOCK();
   cbc->status = WIFI_ENGINE_FAILURE_ABORT;
   if (cbc->cb) {
      cbc->cb(cbc);
   }
   /* Clear pending callbacks */
   cbc2 = wei_cb_find_pending_callback(cbc->trans_id);
   WiFiEngine_FreeCBC(cbc);
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Dispatch queued callbacks on the specified callback queue.
 * Data buffers are freed by this call.
 *
 * @return nothing
 */
void WiFiEngine_DispatchCallbacks(void)
{
   we_cb_container_t *cbc;

   CBLOCK();
   while((cbc = WEI_TQ_FIRST(&cb_list_cbq)) != NULL) {
      WEI_TQ_REMOVE(&cb_list_cbq, cbc, cb_cbq);
      CBUNLOCK();
      WiFiEngine_RunCallback(cbc);
      CBLOCK();
   }

   CBUNLOCK();
}

/*!
 * /brief Schedule a callback for execution.
 *
 * This function inserts a callback on a callback queue.
 *
 * @param cbc The callback container describing the callback.
 * @return nothing
 */
void WiFiEngine_ScheduleCallback(we_cb_container_t *cbc)
{
   DE_TRACE_PTR(TR_WEI, "Schedule cbc %p\n", cbc);
   CBLOCK();
   WEI_TQ_INSERT_TAIL(&cb_list_cbq, cbc, cb_cbq);
   CBUNLOCK();
}

/*!
 * @brief Execute a callback.
 *
 * Execute the callback and free the data buffer. If the callback
 * is single shot then also free the cbc.
 * 
 * @param cbc Callback container to execute.
 * @retval WIFI_ENGINE_SUCCESS always succeeds
 */
int WiFiEngine_RunCallback(we_cb_container_t *cbc)
{
   if (cbc)
   {
      DE_TRACE_INT2(TR_CB, "invoking cbc " TR_FPTR " (callback " TR_FPTR ")\n",
                    TR_APTR(cbc), TR_APTR(cbc->cb));
      cbc->cb(cbc);
      if ( ! wei_cb_still_valid(cbc)) {
         /* this happens if callback itself calls WiFiEngine_CancelCallback */
         return WIFI_ENGINE_SUCCESS;
      }
      if ( ! cbc->repeating )
      {
         WiFiEngine_FreeCBC(cbc);
      }
      else if (cbc->data != NULL)
      {
         DriverEnvironment_Nonpaged_Free(cbc->data);
         cbc->data = NULL;
         cbc->data_len = 0;
      }
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Build a callback container.
 * @param cb Callback function that will be registered with the container.
 * @param ctx A data pointer that will be passed to the callback when it is executed.
 * @param ctx_len Length in bytes of the ctx data.
 * @param repeating 1 if the callback should be repeating, that is it will stay
 *                  on the pending callback queue and will be rescheduled
 *                  every time the trigger condition is satisfied.
 * @return A allocated, initialized callback container. Ready to use with 
 *         WiFiEngine functions with asynchronous completions. NULL on failure.
 */
we_cb_container_t *WiFiEngine_BuildCBC(we_callback_t cb, 
                                       void *ctx, 
                                       size_t ctx_len,
                                       int repeating)
{
   we_cb_container_t *cbc;

   cbc = (we_cb_container_t *)DriverEnvironment_Malloc(sizeof(*cbc));
   if (cbc == NULL)
   {
      return NULL;
   }
   cbc->cb = cb;
   cbc->ctx = ctx;
   cbc->ctx_len = ctx_len;
   cbc->data = NULL;
   cbc->data_len = 0;
   cbc->repeating = repeating;
   CBLOCK();
   WEI_TQ_INSERT_TAIL(&cb_list_all, cbc, cb_all);
   CBUNLOCK();
   DE_TRACE_PTR(TR_WEI, "Allocated cbc %p\n", cbc);
   return cbc;
}

/*!
 * Deallocate callback container that was allocated with WiFiEngine_BuildCBC().
 * @param cbc Callback container to be freed.
 * @return nothing
 */
void WiFiEngine_FreeCBC(we_cb_container_t *cbc)
{
   DE_TRACE_PTR(TR_WEI, "Trying to free cbc %p\n", cbc);
   CBLOCK();
   WEI_TQ_REMOVE(&cb_list_all, cbc, cb_all);
   CBUNLOCK();
   if (cbc->data)
   {
      DriverEnvironment_Nonpaged_Free(cbc->data);
      cbc->data = NULL;
   }

   DE_MEMSET(cbc, 0x5A, sizeof(we_cb_container_t));
   DriverEnvironment_Free(cbc);
}

/** @} */ /* End of we_cb group */
