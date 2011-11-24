/* $Id: de_common.c,v 1.4 2008-03-01 15:09:37 ulla Exp $ */
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
This module implements the platform independent support functions for the
driverenvironment module.

*****************************************************************************/

/** @defgroup de_common Common code for DriverEnvironment
 *
 *  @{
 */
#include "sysdef.h" /* Must be first include */
#include "registry.h"
#include "registryAccess.h"
#include "driverenv.h"
#include "de_common.h"

struct timers_head active_timers;

int __de_init_event(de_event_t *ev);
int __de_uninit_event(de_event_t *ev);
int __de_signal_event(de_event_t *ev);
int __de_reset_event(de_event_t *ev);
int __de_wait_on_event(de_event_t *ev, int ms_to_wait);


void DriverEnvironment_DisableTimers(void)
{
   struct de_timer *p;

   WEI_TQ_FOREACH(p, &active_timers, tq)
      {
         DE_TRACE3(TR_NOISE, "%s : Found timer %p\n", __func__, p);
         if (__de_stop_timer(p) != 0)
         {
            DE_TRACE3(TR_NOISE, "%s : Failed to stop timer %p (lock busy?)\n", __func__, p);
         }
      }
}

void DriverEnvironment_EnableTimers(void)
{
   struct de_timer *p;

   WEI_TQ_FOREACH(p, &active_timers, tq)
      {
         DE_TRACE3(TR_NOISE, "%s : Found timer %p\n", __func__, p);
         if (__de_start_timer(p) != 0)
         {
            DE_TRACE3(TR_NOISE, "%s : Failed to restart timer %p (lock busy?)\n", __func__, p);
         }
      }
}

/***** Platform-independent part of event implementation *****/

int DriverEnvironment_InitializeEvent(de_event_t *ev)
{
   ev->state = DE_SIG_CLEAR;
   return __de_init_event(ev);
}

void DriverEnvironment_UninitializeEvent(de_event_t *ev)
{
   if (ev)
   {
      __de_uninit_event(ev);
   }
}

void DriverEnvironment_SignalEvent(de_event_t *ev)
{
   DE_ASSERT(ev != NULL);
   ev->state = DE_SIG_SIGNALLED;
   __de_signal_event(ev);
   __de_reset_event(ev);
}

int DriverEnvironment_WaitOnEvent(de_event_t *ev, int ms_to_wait)
{
   DE_ASSERT(ev != NULL);
   if (! DriverEnvironment_IsEventWaitAllowed())
   {
      return DRIVERENVIRONMENT_FAILURE_NOT_ALLOWED;
   }
   do 
   {
      if (DE_SIG_SIGNALLED == ev->state)
      {
         return DRIVERENVIRONMENT_SUCCESS;
      }
   } while (__de_wait_on_event(ev, ms_to_wait));

   return DRIVERENVIRONMENT_SUCCESS_TIMEOUT;
}

/** @} */ /* End of de_common group */
