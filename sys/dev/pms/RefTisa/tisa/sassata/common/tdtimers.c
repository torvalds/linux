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
/*******************************************************************************/
/** \file
 *
 * This file contains timer functions in TD layer
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/freebsd/driver/common/osstring.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdutil.h>

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdglobl.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdxchg.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

/*****************************************************************************
*! \brief  tiCOMTimerTick
*
*  Purpose: This function is called to every usecsPerTick interval
*
*  \param tiRoot:   Pointer to initiator specific root data structure  for this
*                   instance of the driver.
*
*  \return:         None
*
*
*****************************************************************************/
osGLOBAL void 
tiCOMTimerTick (
                tiRoot_t * tiRoot
                )
{
  tdsaRoot_t *tdsaRoot = (tdsaRoot_t *)(tiRoot->tdData);
  agsaRoot_t *agRoot = &tdsaRoot->tdsaAllShared.agRootNonInt;
#ifdef FDS_DM  
  dmRoot_t   *dmRoot = &tdsaRoot->tdsaAllShared.dmRoot;
#endif

#ifdef FDS_SM_NOT_YET
  smRoot_t   *smRoot = &tdsaRoot->tdsaAllShared.smRoot;
#endif
  /* checking the lower layer */
  saTimerTick(agRoot);
  
#ifdef FDS_DM  
  /* checking the DM */
  dmTimerTick(dmRoot);
#endif

#ifdef FDS_SM_NOT_YET  
  /* checking the SM */
  smTimerTick(smRoot);
#endif
    
  /*
    timers for discovery 
    checking tdsaRoot_t timers 
  */
  
  tdsaProcessTimers(tiRoot);
 
}

/*****************************************************************************
*! \brief  tdsaInitTimerRequest
*
*  Purpose: This function initiallizes timer request
*
*  \param tiRoot:       Pointer to initiator specific root data structure
*                       for this instance of the driver.
*  \param timerrequest  Pointer to timer request
*
*  \return:             None
*
*
*****************************************************************************/
osGLOBAL void
tdsaInitTimerRequest(
                     tiRoot_t                *tiRoot,
                     tdsaTimerRequest_t      *timerRequest
                     )
{
  timerRequest->timeout       = 0;
  timerRequest->timerCBFunc   = agNULL;
  timerRequest->timerData1     = agNULL;
  timerRequest->timerData2     = agNULL;
  timerRequest->timerData3     = agNULL;
  TDLIST_INIT_ELEMENT((&timerRequest->timerLink));
}

/*****************************************************************************
*! \brief  tdsaSetTimerRequest
*
*  Purpose: This function sets timer request
*
*  \param tiRoot:       Pointer to initiator specific root data structure
*                       for this instance of the driver.
*  \param timerrequest  Pointer to timer request
*  \param timeout       timeout value
*  \param CBFunc        timer CB function
*  \param timerData1     Data associated with the timer
*  \param timerData2     Data associated with the timer
*  \param timerData3     Data associated with the timer
*
*  \return:             None
*
*
*****************************************************************************/
osGLOBAL void
tdsaSetTimerRequest(
                  tiRoot_t            *tiRoot,
                  tdsaTimerRequest_t  *timerRequest,
                  bit32               timeout,
                  tdsaTimerCBFunc_t   CBFunc,
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

/*****************************************************************************
*! \brief  tdsaAddTimer
*
*  Purpose: This function adds timer request to timer list
*
*  \param tiRoot:       Pointer to initiator specific root data structure
*                       for this instance of the driver.
*  \param timerListHdr  Pointer to the timer list
*  \param timerrequest  Pointer to timer request
*
*  \return:             None
*
*
*****************************************************************************/
osGLOBAL void
tdsaAddTimer(
             tiRoot_t            *tiRoot,
             tdList_t            *timerListHdr, 
             tdsaTimerRequest_t  *timerRequest
            )
{
  tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
  TDLIST_ENQUEUE_AT_TAIL(&(timerRequest->timerLink), timerListHdr);
  timerRequest->timerRunning = agTRUE;
  tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
}

/*****************************************************************************
*! \brief  tdsaKillTimer
*
*  Purpose: This function kills timer request.
*
*  \param tiRoot:       Pointer to initiator specific root data structure
*                       for this instance of the driver.
*  \param timerrequest  Pointer to timer request
*
*  \return:             None
*
*
*****************************************************************************/
osGLOBAL void
tdsaKillTimer(
              tiRoot_t            *tiRoot,
              tdsaTimerRequest_t  *timerRequest
              )
{
  tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
  timerRequest->timerRunning = agFALSE;
  TDLIST_DEQUEUE_THIS(&(timerRequest->timerLink));
  tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
}

/*****************************************************************************
*! \brief  tdsaProcessTimers
*
*  Purpose: This function processes timer request.
*
*  \param tiRoot:       Pointer to initiator specific root data structure
*                       for this instance of the driver.
*
*  \return:             None
*
*
*****************************************************************************/
osGLOBAL void 
tdsaProcessTimers(
                  tiRoot_t *tiRoot
                  )
{
  tdsaRoot_t     *tdsaRoot = (tdsaRoot_t *)(tiRoot->tdData);
  tdsaContext_t  *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  tdsaTimerRequest_t *timerRequest_to_process = agNULL;
  tdList_t *timerlist_to_process, *nexttimerlist = agNULL;

  
  timerlist_to_process = &tdsaAllShared->timerlist;
  
  timerlist_to_process = timerlist_to_process->flink;

  while ((timerlist_to_process != agNULL) && (timerlist_to_process != &tdsaAllShared->timerlist))
  {
    nexttimerlist = timerlist_to_process->flink;
    
    tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
    timerRequest_to_process = TDLIST_OBJECT_BASE(tdsaTimerRequest_t, timerLink, timerlist_to_process);
    tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);

    if (timerRequest_to_process == agNULL)
    {
      TI_DBG1(("tdsaProcessTimers: timerRequest_to_process is NULL! Error!!!\n"));
      return;      
    }
    
    timerRequest_to_process->timeout--;
    
    if (timerRequest_to_process->timeout == 0)
    {
      tdsaSingleThreadedEnter(tiRoot, TD_TIMER_LOCK);
      if (timerRequest_to_process->timerRunning == agTRUE)
      {
        timerRequest_to_process->timerRunning = agFALSE;
        TDLIST_DEQUEUE_THIS(timerlist_to_process);
      }
      tdsaSingleThreadedLeave(tiRoot, TD_TIMER_LOCK);
      /* calling call back function */
      (timerRequest_to_process->timerCBFunc)(tiRoot, 
                                             timerRequest_to_process->timerData1, 
                                             timerRequest_to_process->timerData2, 
                                             timerRequest_to_process->timerData3 
                                             );
    }
    timerlist_to_process = nexttimerlist;
  }
  return;
}

