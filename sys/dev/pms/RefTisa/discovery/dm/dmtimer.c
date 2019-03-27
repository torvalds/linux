/*******************************************************************************
**
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

**
********************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>

#include <dev/pms/RefTisa/discovery/dm/dmdefs.h>
#include <dev/pms/RefTisa/discovery/dm/dmtypes.h>
#include <dev/pms/RefTisa/discovery/dm/dmproto.h>

osGLOBAL void   
dmTimerTick(dmRoot_t 		*dmRoot )
{
  DM_DBG6(("dmTimerTick: start\n"));
  
  dmProcessTimers(dmRoot);

  return;
}	  																
				
osGLOBAL void
dmInitTimerRequest(
                     dmRoot_t                *dmRoot, 
                     dmTimerRequest_t        *timerRequest
                     )
{
  timerRequest->timeout       = 0;
  timerRequest->timerCBFunc   = agNULL;
  timerRequest->timerData1     = agNULL;
  timerRequest->timerData2     = agNULL;
  timerRequest->timerData3     = agNULL;
  DMLIST_INIT_ELEMENT((&timerRequest->timerLink));
}

osGLOBAL void
dmSetTimerRequest(
                  dmRoot_t            *dmRoot,
                  dmTimerRequest_t    *timerRequest,
                  bit32               timeout,
                  dmTimerCBFunc_t     CBFunc,
                  void                *timerData1,
                  void                *timerData2,
                  void                *timerData3
                  )
{
  timerRequest->timeout     = timeout;
  timerRequest->timerCBFunc = CBFunc;
  timerRequest->timerData1   = timerData1;
  timerRequest->timerData2   = timerData2;
  timerRequest->timerData3   = timerData3;
}

osGLOBAL void
dmAddTimer(
           dmRoot_t            *dmRoot,
           dmList_t            *timerListHdr, 
           dmTimerRequest_t    *timerRequest
          )
{
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  DMLIST_ENQUEUE_AT_TAIL(&(timerRequest->timerLink), timerListHdr);
  timerRequest->timerRunning = agTRUE;
  tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
}

osGLOBAL void
dmKillTimer(
            dmRoot_t            *dmRoot,
            dmTimerRequest_t    *timerRequest
           )
{
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  timerRequest->timerRunning = agFALSE;
  DMLIST_DEQUEUE_THIS(&(timerRequest->timerLink));
  tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
}


osGLOBAL void 
dmProcessTimers(
                dmRoot_t *dmRoot
                )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmTimerRequest_t          *timerRequest_to_process = agNULL;
  dmList_t                  *timerlist_to_process, *nexttimerlist = agNULL;

  
  timerlist_to_process = &dmAllShared->timerlist;
  
  timerlist_to_process = timerlist_to_process->flink;

  while ((timerlist_to_process != agNULL) && (timerlist_to_process != &dmAllShared->timerlist))
  {
    nexttimerlist = timerlist_to_process->flink;
    
    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    timerRequest_to_process = DMLIST_OBJECT_BASE(dmTimerRequest_t, timerLink, timerlist_to_process);
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    
    if (timerRequest_to_process == agNULL)
    {
      DM_DBG1(("dmProcessTimers: timerRequest_to_process is NULL! Error!!!\n"));
      return;      
    }
    
    timerRequest_to_process->timeout--;
    
    if (timerRequest_to_process->timeout == 0)
    {      
      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      timerRequest_to_process->timerRunning = agFALSE;
      DMLIST_DEQUEUE_THIS(timerlist_to_process);
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      /* calling call back function */
      (timerRequest_to_process->timerCBFunc)(dmRoot, 
                                             timerRequest_to_process->timerData1, 
                                             timerRequest_to_process->timerData2, 
                                             timerRequest_to_process->timerData3 
                                             );
    }
    timerlist_to_process = nexttimerlist;
  }

 return;
}
#endif /* FDS_ DM */

