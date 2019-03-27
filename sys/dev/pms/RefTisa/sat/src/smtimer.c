/*******************************************************************************
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

********************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>

#include <dev/pms/RefTisa/sat/src/smdefs.h>
#include <dev/pms/RefTisa/sat/src/smproto.h>
#include <dev/pms/RefTisa/sat/src/smtypes.h>

osGLOBAL void   
smTimerTick(smRoot_t 		*smRoot )
{
  SM_DBG6(("smTimerTick: start\n"));
  
  smProcessTimers(smRoot);

  return;
}	  																
				
osGLOBAL void
smInitTimerRequest(
                     smRoot_t                *smRoot, 
                     smTimerRequest_t        *timerRequest
                     )
{
  timerRequest->timeout       = 0;
  timerRequest->timerCBFunc   = agNULL;
  timerRequest->timerData1     = agNULL;
  timerRequest->timerData2     = agNULL;
  timerRequest->timerData3     = agNULL;
  SMLIST_INIT_ELEMENT((&timerRequest->timerLink));
}

osGLOBAL void
smSetTimerRequest(
                  smRoot_t            *smRoot,
                  smTimerRequest_t    *timerRequest,
                  bit32               timeout,
                  smTimerCBFunc_t     CBFunc,
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
smAddTimer(
           smRoot_t            *smRoot,
           smList_t            *timerListHdr, 
           smTimerRequest_t    *timerRequest
          )
{
  tdsmSingleThreadedEnter(smRoot, SM_TIMER_LOCK);
  SMLIST_ENQUEUE_AT_TAIL(&(timerRequest->timerLink), timerListHdr);
  timerRequest->timerRunning = agTRUE;
  tdsmSingleThreadedLeave(smRoot, SM_TIMER_LOCK);
}

osGLOBAL void
smKillTimer(
            smRoot_t            *smRoot,
            smTimerRequest_t    *timerRequest
           )
{
  tdsmSingleThreadedEnter(smRoot, SM_TIMER_LOCK);
  timerRequest->timerRunning = agFALSE;
  SMLIST_DEQUEUE_THIS(&(timerRequest->timerLink));
  tdsmSingleThreadedLeave(smRoot, SM_TIMER_LOCK);
}

osGLOBAL void 
smProcessTimers(
                smRoot_t *smRoot
                )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  smTimerRequest_t          *timerRequest_to_process = agNULL;
  smList_t                  *timerlist_to_process, *nexttimerlist = agNULL;

  
  timerlist_to_process = &smAllShared->timerlist;
  
  timerlist_to_process = timerlist_to_process->flink;

  while ((timerlist_to_process != agNULL) && (timerlist_to_process != &smAllShared->timerlist))
  {
    nexttimerlist = timerlist_to_process->flink;
    
    tdsmSingleThreadedEnter(smRoot, SM_TIMER_LOCK);
    timerRequest_to_process = SMLIST_OBJECT_BASE(smTimerRequest_t, timerLink, timerlist_to_process);
    tdsmSingleThreadedLeave(smRoot, SM_TIMER_LOCK);

    if (timerRequest_to_process == agNULL)
    {
      SM_DBG1(("smProcessTimers: timerRequest_to_process is NULL! Error!!!\n"));
      return;      
    }
    
    timerRequest_to_process->timeout--;
    
    if (timerRequest_to_process->timeout == 0)
    {
      timerRequest_to_process->timerRunning = agFALSE;
      
      tdsmSingleThreadedEnter(smRoot, SM_TIMER_LOCK);
      SMLIST_DEQUEUE_THIS(timerlist_to_process);
      tdsmSingleThreadedLeave(smRoot, SM_TIMER_LOCK);
      /* calling call back function */
      (timerRequest_to_process->timerCBFunc)(smRoot, 
                                             timerRequest_to_process->timerData1, 
                                             timerRequest_to_process->timerData2, 
                                             timerRequest_to_process->timerData3 
                                             );
    }
    timerlist_to_process = nexttimerlist;
  }

 return;
}

