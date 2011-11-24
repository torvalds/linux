/* Trigger handler  for Nanoradio Linux WiFi driver */
/* Copyright (C) 2007 Nanoradio AB */
/* $Id: we_trig.c,v 1.28 2008-04-07 14:25:52 joda Exp $ */

/** @defgroup we_trig WiFiEngine MIB trigger interface
 *
 * @brief Generalized trigger handling, which abstracts limitations in
 * firmware where a limited number of triggers may be used.  
 * By using virtual triggers, an arbitrary amount of triggers can
 * be used simultaneously by different modules without affecting
 * each other.
 *
 *  @{
 */

/* Internal stuff
 * ==============
 *
 * Firmware (fw) has a mechnism for MIB triggers based on 
 * MIB id, threshold value and direction (aka event). The 
 * firmware logic has no knowledge what so ever about what kind of 
 * data the MIB contain. It's treated as a volatile integer that 
 * changes and sometimes passes a threshold.
 * 
 * Since firmware doesn't care about the meaning of the MIB value,
 * the available triggers can be used for the same MIB parameter 
 * or for different MIB parameters, whatever pleases the client.
 *
 * The reason for virtualisation of triggers is to hide from the 
 * user the limited number of triggers available in firmware. 
 * This is done by limiting the use of firmware triggers to one 
 * per MIB and event. E.g only one single trigger will ever be 
 * registered per {mib_id, event} tuple. 
 *
 * The virtual triggers enables registration of several trigger levels 
 * of the same tuple in the driver, whereof only one is registered in fw. 
 * This hiding of fw limitations make it necessary to have different 
 * identifiers for the virtual triggers and for the fw triggers.
 * These are called "virtual_id" and "fw_id" throughout the code. 
 * An algorithm has been worked out, such that the behaviour is similar 
 * to that all triggers were registered in fw.
 * 
 * Init phase: As some mib triggers are only activated under certain
 * circumstances and as some mibs are only updated under certain
 * circumstances, the mib's value is unknown until the first trigger
 * is received. One can probably read the value of the mib, but it
 * might as well be a default value and not a true one. Hence, one
 * cannot know the starting value of the mib when the triggers, at
 * least the first one for each mib, are registered. Until the value
 * becomes known, the possibility of receiving a trigger is maximized
 * by registering the triggers with most extreme trigger levels in fw,
 * i.e. the rising trigger with the lowest trigger level and the
 * falling trigger with the highest trigger level.
 *
 * Normal operation: When a trigger indication is received from fw the
 * current value becomes known. All virtual triggers whose levels has
 * been passed will have their triggers dispatched to user level
 * (by invoking the callback provided upon registration). A
 * re-registration of triggers in fw will occur, such that the
 * triggers closest to the received value will be registered, i.e. the
 * rising trigger slightly above the value and the falling trigger
 * slightly below (a trigger with the same limit as the value will not
 * be used).
 * 
 * To be able to follow the movement of a values in fw, each rising
 * trigger will need a corresponding falling trigger at the same
 * level. For example would it otherwise not be possible to
 * re-register properly to lower levels in a mib that has rising
 * triggers only when the fw value falls.
 * 
 */
 
#include "driverenv.h"
#include "wifi_engine_internal.h"
#ifndef INT_MAX
#include <limits.h>
#endif

struct virtual_mib_trigger {
      we_cb_container_t *cbc;

      int32_t  virtual_id;          /* trigger id sent to userspace */
      int32_t  fw_id;               /* trigger id sent to firmware (if this is used, otherwise 0) */
      char     mib_id[10];
      int32_t  gating_virtual_id;   /* dependent on other trigger? May affect other triggers!! */
      uint32_t supv_interval;       /* in milliseconds, smallest for mib_id will be used. May affect other triggers!! */
      int32_t  thr_level;           /* threshold */
      int32_t  last_level;          /* only trig if level was below thr prev */
      uint32_t event;               /* 0 = falling */
      uint16_t event_count;         /* times level is fullfilled before trigger expires. May affect other triggers!! */
      uint16_t forever;             /* 0 = one shot (a.k.a. trigmode) */
      we_ind_cb_t cb;               /* Where to send trigger info. NULL for non-active triggers */

      WEI_TQ_ENTRY(virtual_mib_trigger) next;
};

/* Internal functions */
static int handle_vir_mib_triggning(const char *mib_id, we_mib_trig_data_t *td);
static int handle_fw_failed(struct virtual_mib_trigger *vmt);
static int do_del_vir_mib_trigger(int32_t virtual_id, int reason);

#define TRIG_LOCK()     DriverEnvironment_mutex_down(&wifiEngineState.trig_sem)
#define TRIG_UNLOCK()   DriverEnvironment_mutex_up(&wifiEngineState.trig_sem)

void DISPATCH_EVENT(we_ind_cb_t cb, uint32_t virtual_id, int32_t value) 
{                         
   we_vir_trig_data_t data; 

   data.trig_id = virtual_id; 
   data.type = WE_TRIG_TYPE_IND; 
   data.value = value; 
   data.reason = 0;     
   cb((void *)&data, sizeof(data));
}

void DISPATCH_CANCEL(we_ind_cb_t cb, uint32_t virtual_id, uint32_t reason) 
{
   we_vir_trig_data_t data;
   data.trig_id = virtual_id; 
   data.type = WE_TRIG_TYPE_CANCEL; 
   data.value = 0; 
   data.reason = reason;     
   cb((void *)&data, sizeof(data)); 
}

/**************************************
 *
 *         MIB Trigger Stuff
 *
 * Functions in this section are front 
 * ends towards fw.
 **************************************/

/*! 
 * This callback function is the entry point for fw messages.  
 *  - Confirmation of registration in fw.
 *  - Trigger indications. 
 *  - Also some WiFiEngine stuff, as cancel of callback.
 */
static int mib_trigger_callback(we_cb_container_t *cbc)
{
   struct virtual_mib_trigger *vmt = (struct virtual_mib_trigger *)cbc->ctx;
   we_mib_trig_data_t *td;

   DE_TRACE_PTR(TR_MIB, "vmt addr %p\n", vmt);
   
   if (vmt == NULL)
   {
      /* guard */
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }

   td = (we_mib_trig_data_t *)cbc->data;

   if (cbc->status >= 0) 
   {
      switch (td->type) 
      {
         case WE_TRIG_TYPE_CFM: 
            if (td->result == 0) {
               DE_TRACE_STATIC(TR_MIB, "Firmware success\n");
            }
            else {
               DE_TRACE_STATIC(TR_MIB, "Firmware failed\n");
               handle_fw_failed(vmt);
            }
            break;
         case WE_TRIG_TYPE_IND: {
            handle_vir_mib_triggning(vmt->mib_id, td);
            break;
         }
         default:
            DE_BUG_ON(1, "unknown status %d\n", cbc->status);
            break;
      }
   }
   else {                       /* WiFiEngine failure */
      switch(cbc->status) {
         case WIFI_ENGINE_FAILURE_ABORT: /* Got here after cancel callback => don't do cancel callback here! */
            DE_TRACE_INT(TR_MIB, "Trigger with fw_id %d has been cancelled\n", vmt->fw_id);
            cbc->ctx = NULL;
            vmt->fw_id = 0;
            break;
         case WIFI_ENGINE_FAILURE_RESOURCES:
            DE_TRACE_STATIC(TR_MIB, "WIFI_ENGINE_FAILURE_RESOURCES\n");
            WiFiEngine_CancelCallback(cbc);
            break;
         default:
            DE_BUG_ON(1, "unknown status %d\n", cbc->status);
            break;
      }
   }
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Does registration of a trigger in fw
 * @param vmt Trigger to register
 * @param supv_interval Periodic interval to check trigger. This may
 *        be used when several triggers belonging to the same tuple
 *        has different intervals.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success, 
 * - error code otherwise
 */
static int reg_mib_trigger(struct virtual_mib_trigger *vmt,
                           uint32_t          supv_interval)
{
   int ret;
   struct virtual_mib_trigger *p;
   int gating_fw_id = 0;

   /* Check if a gating trigger is set (>0) and thats I'm one myself (-1) */
   if (vmt->gating_virtual_id     != 0 
    && vmt->gating_virtual_id + 1 != 0) 
   { 
      /* Find the firmware id for the gating trigger. */
      WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
      {
         if (p->virtual_id == vmt->gating_virtual_id) 
         {
            gating_fw_id = p->fw_id;
            break;
         }
      }
      DE_ASSERT(gating_fw_id != 0);
      DE_TRACE_INT3(TR_MIB, "This trigger (virtual id %d) is gated with trigger %d (fw id %d)\n", vmt->virtual_id, p->virtual_id, p->fw_id);
   }

   vmt->cbc = WiFiEngine_BuildCBC(mib_trigger_callback, vmt, 0, TRUE); /* Trigger => need repeating cbc */
   ret = WiFiEngine_RegisterMIBTrigger(&vmt->fw_id,
                                       vmt->mib_id, 
                                       gating_fw_id,
                                       supv_interval,
                                       vmt->thr_level,
                                       vmt->event,
                                       vmt->event_count,
                                       vmt->forever,
                                       vmt->cbc);
   if(ret != WIFI_ENGINE_SUCCESS) 
   {
      WiFiEngine_FreeCBC(vmt->cbc);
      DE_TRACE_STATIC(TR_MIB, "EXIT EIO\n");
      return ret;
   }
   
   DE_TRACE_INT2(TR_MIB, "Created trigger with fw id %d (vmt addr %p)", vmt->fw_id, vmt);
   DE_TRACE_STRING(TR_MIB, " for MIB %s \n", vmt->mib_id);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Will remove a previously registered trigger from fw.
 * @param vmt Pointer to the trigger which is to be removed.
 * @return
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */
static int deregister_mib_trigger(struct virtual_mib_trigger *vmt)
{
   int ret = WIFI_ENGINE_SUCCESS;

   DE_ASSERT(vmt != NULL);

   if (vmt->fw_id != 0)
   {
      vmt->cbc = NULL;
      ret = WiFiEngine_DeregisterMIBTrigger(vmt->fw_id);
      vmt->fw_id = 0;
   
      if (ret != WIFI_ENGINE_SUCCESS)
         DE_TRACE_STATIC(TR_MIB, "Failure\n");
   }

   return ret;
}


/**************************************
 *
 *        Virtual triggers
 *
 **************************************/

/*!
 * Find last level (i.e. seen at last trigger indication) for a
 * mib_id. Value is stored in last_level.
 *
 * @param mib_id  zero-terminated string with the mib id.
 * @param event   MIB trigger supervision events, used when setting default value
 * @param last_level Storage of the value.
 * @return
 * - WIFI_ENGINE_SUCCESS at success and non-zero on failure.
 * - WIFI_ENGINE_FAILURE otherwise
 */
static int find_last_level(const char *mib_id, uint32_t event, int *last_level) 
{
   struct virtual_mib_trigger *p;

   /* Set default value. */
   if (event == RISING) {
      *last_level = INT_MIN;
   }
   else 
   {
      *last_level = INT_MAX;
   }

   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
   {
      if (strcmp(mib_id, p->mib_id) == 0)
      {
         if (p->last_level != INT_MIN && p->last_level != INT_MAX) 
         {
            *last_level = p->last_level;
            break;
         }
      }
   }
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Re-registration of rising mib-trigger.
 *
 * @param mib_id     zero-terminated string with the mib id.
 * @param curr_value current known value
 * @return
 * - WIFI_ENGINE_SUCCESS at success and non-zero on failure.
 * - error code otherwise
 */
static int reregister_vir_mib_triggers_rising(const char *mib_id, int curr_value)
{
   int ret;
   struct virtual_mib_trigger *p;
   struct virtual_mib_trigger *best;
   struct virtual_mib_trigger *prev = NULL;
   uint32_t supv_interval = 0xffffffff;

   best = NULL;
   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next) 
   {
      if ( (p->event == RISING) && (strcmp(mib_id, p->mib_id) == 0) ) 
      {
         if (best == NULL)
            best = p;
         else if (best->thr_level < curr_value && p->thr_level > best->thr_level)    /* choose highest when best < current */
            best = p;
         else if (p->thr_level >= curr_value && p->thr_level < best->thr_level)    /* choose closest when both higher */
            best = p;

         if (p->supv_interval < supv_interval) /* use hardest demand */
         {
            supv_interval = p->supv_interval;
         }

         if (p->fw_id != 0) 
         {
            prev = p;
         }
      }
   }

   if (best == NULL) 
   {
      DE_TRACE_STRING(TR_MIB, "No rising triggers in list for MIB %s\n", mib_id);
      return WIFI_ENGINE_SUCCESS;
   }

   if (best == prev) 
   {
      DE_TRACE_INT(TR_MIB, "Keep prev rising thr_level %d\n", best->thr_level);
   }
   else
   {
      if (prev != NULL) 
      {
         DE_TRACE_INT3(TR_MIB, "Inactivating previous rising thr_level %d: virtual_id %d, fw_id %d\n", prev->thr_level, prev->virtual_id, prev->fw_id);        
         deregister_mib_trigger(prev);

         /* Dispatch previous MIB trigger if the current value has rised above the threshold. */
         if (prev->cb)
         {
            if (curr_value >= prev->thr_level  &&  prev->last_level < prev->thr_level) 
            {
               prev->last_level = curr_value;
               DE_TRACE_STRING2(TR_MIB, "Dispatch current %s trigger for %s",
                      prev->event == RISING ? "rising":"falling",
                      prev->mib_id);
               DE_TRACE_INT3(TR_MIB, ": virtual_id %d, thr_level %d (current %d)\n",
                      prev->virtual_id,
                      prev->thr_level,
                      curr_value);
               DISPATCH_EVENT(prev->cb, prev->virtual_id, curr_value);
               if (prev->forever == 0)
               {
                  /* One shot trigger => clean up */
                  do_del_vir_mib_trigger(prev->virtual_id, NRX_REASON_ONE_SHOT);
               }
            }
         }
      }
      
      DE_TRACE_INT2(TR_MIB, "Update rising thr_level to %d (current %d)\n", best->thr_level, curr_value);

      ret = reg_mib_trigger(best, supv_interval);
      if (ret != WIFI_ENGINE_SUCCESS) 
      {
         DE_TRACE_STATIC(TR_MIB, "Could not re-register rising trigger\n");
         best->fw_id = 0;
         /* Here the entire list for this mib should be cleaned up */
         return ret;
      }
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Re-registration of falling mib-trigger.
 *
 * @param mib_id     zero-terminated string with the mib id.
 * @param curr_value current known value
 * @return
 * - WIFI_ENGINE_SUCCESS at success and non-zero on failure.
 * - error code otherwise
 */
static int reregister_vir_mib_triggers_falling(const char *mib_id, int curr_value)
{
   int ret;
   struct virtual_mib_trigger *p;
   struct virtual_mib_trigger *best;
   struct virtual_mib_trigger *prev = NULL;
   uint32_t supv_interval = 0xffffffff;

   best = NULL;
   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next) 
   {
      if ( (p->event == FALLING) && (strcmp(mib_id, p->mib_id) == 0) ) 
      {
         if (best == NULL)
            best = p;
         else if (best->thr_level > curr_value && p->thr_level < best->thr_level)  /* choose lowest when best > current */
            best = p;
         else if (p->thr_level <= curr_value && p->thr_level > best->thr_level)    /* choose closest when both lower */
            best = p;
         
         if (p->supv_interval < supv_interval) /* use hardest demand */
         {
            supv_interval = p->supv_interval;
         }
         
         if (p->fw_id != 0) 
         {
            prev = p;
         }
      }
   }

   if (best == NULL) 
   {
      DE_TRACE_STRING(TR_MIB, "No falling triggers in list for MIB %s\n", mib_id);
      return WIFI_ENGINE_SUCCESS;
   }

   if (best == prev) 
   {
      DE_TRACE_INT(TR_MIB, "Keep prev falling thr_level %d\n", best->thr_level);
   }
   else
   {
      if (prev != NULL) 
      {
         DE_TRACE_INT3(TR_MIB, "Inactivating previous falling thr_level %d: virtual_id %d, fw_id %d\n", prev->thr_level, prev->virtual_id, prev->fw_id);
         deregister_mib_trigger(prev);

         /* Dispatch previous MIB trigger if the current value has falled below the threshold. */
         if (prev->cb)
         {
           if (curr_value <= prev->thr_level  &&  prev->last_level > prev->thr_level) 
           {
               prev->last_level = curr_value;
               DE_TRACE_STRING2(TR_MIB, "Dispatch current %s trigger for %s",
                      prev->event == RISING ? "rising":"falling",
                      prev->mib_id);
               DE_TRACE_INT3(TR_MIB, ": virtual_id %d, thr_level %d (current %d)\n",
                      prev->virtual_id,
                      prev->thr_level,
                      curr_value);
               DISPATCH_EVENT(prev->cb, prev->virtual_id, curr_value);
               if (prev->forever == 0)
               {
                  /* One shot trigger => clean up */
                  do_del_vir_mib_trigger(prev->virtual_id, NRX_REASON_ONE_SHOT);
               }
            }
         }

      }
      
      DE_TRACE_INT2(TR_MIB, "Update falling thr_level to %d (current %d)\n", best->thr_level, curr_value);

      ret = reg_mib_trigger(best, supv_interval);
      if (ret != WIFI_ENGINE_SUCCESS) 
      {
         DE_TRACE_STATIC(TR_MIB, "Could not re-register falling trigger\n");
         best->fw_id = 0;
         /* Here the entire list for this mib should be cleaned up */
         return ret;
      }
   }

   return WIFI_ENGINE_SUCCESS;
}

static int reregister_vir_mib_triggers(const char *mib_id, bool_t event, int curr_value)
{
   if (event == RISING) 
      return reregister_vir_mib_triggers_rising(mib_id, curr_value);
   else
      return reregister_vir_mib_triggers_falling(mib_id, curr_value);    
}


/*!
 * Should the interval supplied be lower than any in certain 
 * virtual trigger, the current fw trigger will be unregistered. 
 *
 * This is used when creation, or deletion, of a trigger
 * will change supervision interval.
 *
 * @param mib_id        zero-terminated string with the mib id.
 * @param event         MIB trigger supervision events
 * @param this_interval interval to compare against. 
 * @return
 * - WIFI_ENGINE_SUCCESS at success and non-zero on failure.
 * - WIFI_ENGINE_FAILURE otherwise
 */
static int clean_up_if_lowest_interval(const char *mib_id, bool_t event, uint32_t this_interval) 
{
   struct virtual_mib_trigger *p;
   struct virtual_mib_trigger *curr = NULL;

   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
   {
      if ( (strcmp(mib_id, p->mib_id) == 0) && (event == p->event) )
      {
         if (p->fw_id != 0)
         {
            curr = p;
         }
         if (this_interval >= p->supv_interval)
         {
            /* Ok, the current trigger has a lower interval. */
            curr = NULL;
            break;
         }
      }
   }

   /* When we get here => we're the lowest interval */
   if (curr == NULL) 
      return WIFI_ENGINE_SUCCESS;

   /* Unregister current fw trigger */
   DE_TRACE_INT2(TR_MIB, "A trigger of the same type has lower interval, unrigister current trigger in firmware (virtual_id %d, fw_id %d)\n", curr->virtual_id, curr->fw_id);

   return deregister_mib_trigger(curr);
}


/*!
 * Internal function for removing a virtual trigger. Depending on the
 * reason parameter the trigger may be removed from these levels
 *  - FW 
 *  - Callback 
 *  - List 
 *  - Memory clean-up 
 *  + Re-registration may be done 
 *
 * @param virtual_id MIB trigger to be removed
 * @param reason Reason the trigger is deleted. 
 * @return 
 * - WIFI_ENGINE_SUCCESS when at least one trigger was foundn
 * - WIFI_ENGINE_FAILURE otherwise
 */
static int do_del_vir_mib_trigger(int32_t virtual_id, int reason)
{
   struct virtual_mib_trigger *p;
   int ret_val = WIFI_ENGINE_FAILURE;
   int was_found;
   int gating_virtual_id = 0;
   int last_level;
   we_ind_cb_t cb = NULL;

   do {                         /* search for several triggers */
      was_found = 0;
      WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next) 
      {
         if (p->virtual_id == virtual_id) 
         {
            WEI_TQ_REMOVE(&wifiEngineState.vir_trig_head, p, next);

            if ( reason == NRX_REASON_RM_BY_USER || reason == NRX_REASON_ONE_SHOT ) 
            {
               clean_up_if_lowest_interval(p->mib_id, p->event, p->supv_interval);
               deregister_mib_trigger(p);
            }
            else /* NRX_REASON_SHUTDOWN or NRX_REASON_REG_FAILED */
            { 
               if (p->fw_id)
               {
                  WiFiEngine_CancelCallback(p->cbc); /* don't touch fw */
               }
            }

            if (reason == NRX_REASON_RM_BY_USER 
             || reason == NRX_REASON_ONE_SHOT 
             || reason == NRX_REASON_REG_FAILED) 
            { 
               /* reregister */
               find_last_level(p->mib_id, p->event, &last_level);
               reregister_vir_mib_triggers(p->mib_id, p->event, last_level);
            }

            if (p->cb != NULL)
            {
               cb = p->cb;
            }
            gating_virtual_id = p->gating_virtual_id;
            DriverEnvironment_Nonpaged_Free(p);
            ret_val = WIFI_ENGINE_SUCCESS;
            was_found = 1;
            break;              /* loop variable p not valid anymore */
         }
      }
   } while (was_found);
   if (cb != NULL) 
   {
      if (gating_virtual_id != -1)    /* it isn't a gating trig */
      {
         DISPATCH_CANCEL(cb, virtual_id, reason);
      }
   }

   return ret_val;
}


/*!
 * Internal function to be called by callback function when a mib
 * trigger indication has been rececived.
 *
 * @param mib_id zero-terminated string with mib id.
 * @param td Trigger data
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */ 
static int handle_vir_mib_triggning(const char *mib_id, we_mib_trig_data_t *td) 
{
   int32_t level = 0;
   struct virtual_mib_trigger *p;

   DE_TRACE_INT(TR_MIB, "Handle virtual trigger with fw_id: %d\n", td->trig_id);

   switch(td->len) {
      case 1:
         level = ((int8_t*)td->data)[0];
         break;
      case 2:
         level = *((int16_t*)td->data); /* le16_to_cpup(td->data); */
         break;
      case 4:
         level = *((int32_t*)td->data); /* le32_to_cpup(td->data); */
         break;
      default:
         DE_BUG_ON(1, "unexpected length " TR_FSIZE_T "\n", 
                   TR_ASIZE_T(td->len));
   }

   DE_TRACE_INT(TR_MIB, "Current level: %d\n", level);


   /* Dispatch */
   TRIG_LOCK();
   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
   {
      if (p->cb != NULL && strcmp(mib_id, p->mib_id) == 0)
      {
         int dispatch = 0;
         if ( p->event == RISING  &&  level >= p->thr_level  &&  p->last_level < p->thr_level) /* rising */
            dispatch = 1;
         if ( p->event == FALLING &&  level <= p->thr_level  &&  p->last_level > p->thr_level) /* falling */
            dispatch = 1;
         if (dispatch) 
         {
            DE_TRACE_STRING2(TR_MIB, "Dispatch %s trigger for MIB: %s",
                             p->event == RISING ? "rising" : "falling",
                             p->mib_id);
            DE_TRACE_INT4(TR_MIB, " with virtual id %d. thr_level %d (current %d, last_level %d)\n",
                      p->virtual_id,
                      p->thr_level,
                      level,
                      p->last_level);
            DISPATCH_EVENT(p->cb, p->virtual_id, level);
            /* Clean up if one shot trigger */
            if (p->forever == 0) 
            {
               do_del_vir_mib_trigger(p->virtual_id, NRX_REASON_ONE_SHOT);
            }
         }
         p->last_level = level;
      }
   }
   
   /* Ignore special case with infinite re-registration of */
   /* highest/lowest levels. Handled by functions below.   */
   reregister_vir_mib_triggers_rising(mib_id, level);
   reregister_vir_mib_triggers_falling(mib_id, level);
   TRIG_UNLOCK();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * Internal function to be called by callback function when a
 * registration of a mib trigger failed. Will clean up all triggers
 * with same id up.
 *
 * @param vmt Trigger that failed.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */ 
static int handle_fw_failed(struct virtual_mib_trigger *vmt)
{
   int err;

   TRIG_LOCK();
   err = do_del_vir_mib_trigger(vmt->virtual_id, NRX_REASON_REG_FAILED);
   TRIG_UNLOCK();
   
   return err;
}


/*!
 * Internal function to create a virtual trigger. If appropriate, the
 * trigger will will be registered in fw or dispatched.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */
static int do_reg_vir_mib_trigger(int32_t       virtual_id,
                                  const char   *mib_id, 
                                  uint32_t      gating_virtual_id,
                                  uint32_t      supv_interval,
                                  int32_t       thr_level,
                                  uint8_t       event,
                                  uint16_t      event_count,
                                  uint16_t      triggmode,
                                  we_ind_cb_t   cb)
{
   int ret;
   int last_level;

   struct virtual_mib_trigger *vmt;

   vmt = (struct virtual_mib_trigger *)DriverEnvironment_Nonpaged_Malloc(sizeof(*vmt));
   if (vmt == NULL) {
      DE_TRACE_STATIC(TR_MIB, "Failed to allocate memory for MIB trigger\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }

   DE_STRCPY(vmt->mib_id, mib_id);
   vmt->virtual_id         = virtual_id;
   vmt->fw_id              = 0;                 /* trigger is not yet in use by firmware */
   vmt->gating_virtual_id  = gating_virtual_id; /* its id up */
   vmt->supv_interval      = supv_interval;
   vmt->thr_level          = thr_level;         /* threshold */
   vmt->event              = event;
   vmt->event_count        = event_count;
   vmt->forever            = triggmode;
   vmt->cb                 = cb;

   TRIG_LOCK();
   clean_up_if_lowest_interval(vmt->mib_id, vmt->event, vmt->supv_interval);
   WEI_TQ_INSERT_TAIL(&wifiEngineState.vir_trig_head, vmt, next);

   /* All setup of the vmt struct must have been done before call to register */
   /* function (or be passed to it), as trigger might happen before function returns */ 

   /* Find out last level */
   find_last_level(vmt->mib_id, vmt->event, &last_level); 
   ret = reregister_vir_mib_triggers(mib_id, vmt->event, last_level);
   if (ret != WIFI_ENGINE_SUCCESS) 
   {
      TRIG_UNLOCK();
      DE_TRACE_STATIC(TR_MIB, "EXIT (failed)\n");
      return ret;
   }

   DE_TRACE_STRING2(TR_MIB, "New %s trigger for MIB %s", vmt->event == RISING ? "rising" : "falling", vmt->mib_id); 
   DE_TRACE_INT4(TR_MIB, ": virtual_id %d, fw_id %d, thr_level %d, last_level %d\n", 
                 vmt->virtual_id, vmt->fw_id, vmt->thr_level, last_level);

   /* Check if not registered as real trigger and first trigging is not automatic */
   if (vmt->fw_id == 0) 
   {       
      /* Skip triggers that are gated */
      if (vmt->gating_virtual_id == 0)  
      {
         if (vmt->cb)
         {
            /* Check that prev level exists */
            if (last_level != INT_MIN && last_level != INT_MAX) 
            {
               if ( ( vmt->event == RISING  && last_level >= vmt->thr_level)
                 || ( vmt->event == FALLING && last_level <= vmt->thr_level) ) 
               {
                  DE_TRACE_STATIC(TR_MIB, "Dispatch trigger on register\n");
                  vmt->last_level = last_level;
                  DISPATCH_EVENT(vmt->cb, vmt->virtual_id, last_level);
               }
            }
         }
      }
   }
   else 
   {
      DE_TRACE_STRING2(TR_MIB, "This trigger set the current %s threshold for MIB %s", (vmt->event == RISING ? "rising" : "falling"), vmt->mib_id);
      DE_TRACE_INT(TR_MIB, " to thr_level %d\n", vmt->thr_level);
   }
   TRIG_UNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief External interface to register a standard trigger
 *
 * Both a rising and a falling trigger will be registered, even if 
 * only one of them are used (in that case the other one will not be active).
 * 
 * @param trig_id The MIB trigger id will be stored in this variable.
 * @param mib_id Specify which mib is to be used here.
 * @param gating_virtual_id Should normally not be used and set to 0. Is
 *        there an other trigger that this trigger depends upon this
 *        parameter is set to the former's thr_id. If this trigger itself is
 *        a gating trigger it's set to -1. Only 0 is supported. Other inputs
 *        are not tested and probably not working.
 * @param supv_interval Time of interval between checks of mib.
 * @param thr_level Threshold level.
 * @param dir Determines direction for which the callback will get
 *        notification. Use WE_TRIG_THR_RISING to get notification when the
 *        mib increases above threshold; use WE_TRIG_THR_FALLING to get
 *        notification when it gets below; and (WE_TRIG_THR_FALLING |
 *        WE_TRIG_THR_RISING) to get notification when the threshold
 *        is passed independently of direction.
 * @param event_count Number of times triggers event should happen
 *        before notification to callback. Only 1 is supported.
 * @param trigmode Set to 0 for one-shot trigger, which is cleaned up
 *        after its first and only notification. Set to 1 for a trigger that 
 *        is used repeatedly (until deleted).
 * @param cb The callback will be sent a we_vir_trig_data_t structure
 *        containing information regarding the trigger or cancel. The
 *        function should return WIFI_ENGINE_SUCCESS on success.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS on success 
 * - error code otherwise
 */
int WiFiEngine_RegisterVirtualTrigger(int32_t         *trig_id,
                                     const char       *mib_id, 
                                     uint32_t          gating_virtual_id,
                                     uint32_t          supv_interval,
                                     int32_t           thr_level,
                                     uint8_t           dir, /* rising/falling */
                                     uint16_t          event_count,
                                     uint16_t          triggmode,
                                     we_ind_cb_t       cb)
{
   int err;

   TRIG_LOCK();
   *trig_id = ++wifiEngineState.vir_mib_trig_count;
   TRIG_UNLOCK();

   DE_TRACE_STRING3(TR_MIB, "Register %s %s trigger for MIB: %s\n", (triggmode ? "continous" : "one shot"), (dir ? "falling" : "rising"), mib_id);
   DE_TRACE_INT5(TR_MIB, "virtual_id %d, gating_virtual_id %d, supv_interval %d, thr_level %d, event_count %d\n", *trig_id, gating_virtual_id, supv_interval, thr_level, event_count);

   /* Register trigger pair */
   err = do_reg_vir_mib_trigger(*trig_id,
                                 mib_id,
                                 gating_virtual_id,
                                 supv_interval,
                                 thr_level,
                                 RISING,
                                 event_count,
                                 1,
                                 dir & WE_TRIG_THR_RISING ? cb : NULL);
   if (err != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT(TR_MIB, "Could not register rising MIB trigger , error %d\n", err);
   }

   err = do_reg_vir_mib_trigger(*trig_id,
                                 mib_id,
                                 gating_virtual_id,
                                 supv_interval,
                                 thr_level,
                                 FALLING,
                                 event_count,
                                 1,
                                 dir & WE_TRIG_THR_FALLING ? cb : NULL);
   if (err != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT(TR_MIB, "Could not register falling MIB trigger , error %d\n", err);
   }
   
   return err;
}


/*!
 * @brief External interface to remove a virtual trigger. 
 *
 * @param trig_id Trigger to be removed (id up).
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise.
 */
int WiFiEngine_DelVirtualTrigger(int32_t trig_id)
{
   int err;

   TRIG_LOCK();
   err = do_del_vir_mib_trigger(trig_id, NRX_REASON_RM_BY_USER);
   TRIG_UNLOCK();

   return err;
}


/*!
 * @brief External interface to check if a trigger exists.
 *
 * @param trig_id Virtual trigger id.
 * @param mib_id Specify mib trigger is related to. If this is NULL,
 *        no check on mib_id will be performed, i.e. only trig_id is 
 *        verified.
 * @return 
 * - TRUE if trigger exist
 * - FALSE if trigger doesn't exist (or failure has occurred).
 */
bool_t WiFiEngine_DoesVirtualTriggerExist(int32_t trig_id, const char *mib_id)
{
   struct virtual_mib_trigger *p;

   TRIG_LOCK();

   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
   {
      if (p->virtual_id == trig_id) 
      {
         if (mib_id == NULL 
             || DE_STRCMP(p->mib_id, mib_id) == 0)
            break;
      }
   }
   
   TRIG_UNLOCK();

   return p == NULL ? FALSE : TRUE;
}


/*! 
 * @brief Initializes virtual trigger variables. 
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */
void wei_virt_trig_init(void)
{
   DriverEnvironment_mutex_init(&wifiEngineState.trig_sem);
   wifiEngineState.vir_mib_trig_count = 0;
   WEI_TQ_INIT(&wifiEngineState.vir_trig_head);
}


/*! 
 * @brief External interface to wipe entire list and free memory. 
 *
 * This function is intended to be used at unloading of driver.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */
void wei_virt_trig_unplug(void)
{
   struct virtual_mib_trigger *p;

   TRIG_LOCK();
   while ((p = WEI_TQ_FIRST(&wifiEngineState.vir_trig_head)) != NULL) 
   {
      do_del_vir_mib_trigger(p->virtual_id, NRX_REASON_SHUTDOWN);
   }
   TRIG_UNLOCK();
}


/*!
 * @brief External interface to re-register all virtual triggers. 
 *
 * This function assumes fw has been restarted, that all triggers
 * previously registered in fw has been lost and that they all need to
 * be re-registered.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - error code otherwise
 */
int WiFiEngine_ReregisterAllVirtualTriggers()
{
   struct virtual_mib_trigger *p;
   struct virtual_mib_trigger *p2;
   int last_level;

   TRIG_LOCK();

   /* Set all fw_id to 0 */
   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next)
   {
      if (p->fw_id != 0) 
      {
         DE_TRACE_INT2(TR_MIB, "Warning (ignored): trigger wasn't removed from fw: virtual_id %d, fw_id %d\n", p->virtual_id, p->fw_id);
         p->fw_id = 0;
      }
   }

   /* Each mib_id needs one fw mib trig per event {RISING, FALLING} */
   WEI_TQ_FOREACH(p, &wifiEngineState.vir_trig_head, next) 
   {
      WEI_TQ_FOREACH(p2, &wifiEngineState.vir_trig_head, next) 
      {
         if ( (strcmp(p->mib_id, p2->mib_id) == 0)
           && (p->event == p2->event) )
         {
            if (p2->fw_id != 0) /* already registered */
               break;
         }
      }
      if (p2 == NULL) {         /* none registered */
         find_last_level(p->mib_id, p->event, &last_level);
         reregister_vir_mib_triggers(p->mib_id, p->event, last_level);
      } 
   } /* loop */

   TRIG_UNLOCK();

   return WIFI_ENGINE_SUCCESS;
}


/**************************************
 *
 *  Depreciated functions
 *
 **************************************/

int WiFiEngine_RegisterVirtualIERTrigger(int *thr_id,
                                         uint32_t ier_thr,
                                         uint32_t per_thr,
                                         uint32_t chk_period,
                                         uint8_t dir,
                                         we_ind_cb_t cb) 
{
   return WIFI_ENGINE_FAILURE_NOT_SUPPORTED;
}


/*!
 * @brief Depreciated, not supported functionality
 *
 * @return
 * - WIFI_ENGINE_FAILURE_NOT_SUPPORTED 
 */
int WiFiEngine_DelVirtualIERTrigger(int thr_id)
{
   return WIFI_ENGINE_FAILURE_NOT_SUPPORTED;
}

/** @} */ /* End of we_trig group */

