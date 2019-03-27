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
/*! \file satimer.c
 *  \brief The file implements the timerTick function
 *
 */
/******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_FW_TEST_BUNCH_STARTS
void mpiMsgProduceBunch(  agsaLLRoot_t  *saRoot);
#endif /* SA_FW_TEST_BUNCH_STARTS */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'P'
#endif

/******************************************************************************/
/*! \brief TimerTick
 *
 *  TimerTick
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void saTimerTick(
  agsaRoot_t  *agRoot
  )
{
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaTimerDesc_t *pTimer;
  bit32           Event;
  void *          pParm;

  if(agNULL ==  saRoot)
  {
    SA_DBG1(("saTimerTick:agNULL ==  saRoot \n"));
    return;
  }

  /* (1) Acquire timer list lock */
  ossaSingleThreadedEnter(agRoot, LL_TIMER_LOCK);

  /* (2) Find the timers are timeout */
  pTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->validTimers));
  while ( agNULL != pTimer )
  {
    /* (2.1) Find the first timer is timeout */
    if ( pTimer->timeoutTick == saRoot->timeTick )
    {
      /* (2.1.1) remove the timer from valid timer list */
      saLlistRemove(&(saRoot->validTimers), &(pTimer->linkNode));
      /* (2.1.2) Invalid timer */
      pTimer->valid = agFALSE;
      /* (2.1.3) Get timer event and param */
      Event = pTimer->Event;
      pParm = pTimer->pParm;
      /* (2.1.4) Release timer list lock */
      ossaSingleThreadedLeave(agRoot, LL_TIMER_LOCK);

      /* (2.1.5) Timer Callback */
      pTimer->pfnTimeout(agRoot, Event, pParm);

      /* (2.1.6) Acquire timer list lock again */
      ossaSingleThreadedEnter(agRoot, LL_TIMER_LOCK);
      /* (2.1.7) return the timer to free timer list */
      saLlistAdd(&(saRoot->freeTimers), &(pTimer->linkNode));
    }
    /* (2.2) the first timer is not timeout */
    else
    {
      break;
    }
    pTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->validTimers));
  }

  /* (3) increment timeTick */
  saRoot->timeTick ++;

  if( saRoot->ResetFailed )
  {
    SA_DBG1(("saTimerTick: siChipResetV saRoot->ResetFailed\n"));
  }

#ifdef SA_FW_TEST_BUNCH_STARTS
  if (saRoot->BunchStarts_Enable &&
      saRoot->BunchStarts_Pending)
  {
      SA_DBG3(("saTimerTick: mpiMsgProduceBunch\n"));
      mpiMsgProduceBunch(  saRoot);
  }
#endif /* SA_FW_TEST_BUNCH_STARTS */


#ifdef SA_FW_TEST_INTERRUPT_REASSERT

  if(1)
  {
    mpiOCQueue_t         *circularQ;
    int i;
    SA_DBG4(("saTimerTick:SA_FW_TEST_INTERRUPT_REASSERT\n"));
    for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
    {
      circularQ = &saRoot->outboundQueue[i];
      OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
      if(circularQ->producerIdx != circularQ->consumerIdx)
      {
        if( saRoot->OldCi[i] == circularQ->consumerIdx && saRoot->OldPi[i] >= circularQ->producerIdx)
        {
          agsaEchoCmd_t       payload;
          payload.tag = 0xF0;
          payload.payload[0]= 0x0;
          if( ++saRoot->OldFlag[i] > 1 )
          {
            saRoot->CheckAll++;
          }
          SA_DBG1(("saTimerTick:Q %d (%d) PI 0x%03x CI 0x%03x (%d) CheckAll %d %d\n",i,
            saRoot->OldFlag[i],
            circularQ->producerIdx,
            circularQ->consumerIdx,
            (circularQ->producerIdx > circularQ->consumerIdx ? (circularQ->producerIdx - circularQ->consumerIdx) :   (circularQ->numElements -  circularQ->consumerIdx ) + circularQ->producerIdx),
            saRoot->CheckAll,
            saRoot->sysIntsActive ));

          if(smIS64bInt(agRoot))
          {
            SA_DBG1(("saTimerTick:CheckAll %d ODR 0x%08X%08X ODMR 0x%08X%08X our Int %x\n",
              saRoot->CheckAll,
              ossaHwRegReadExt(agRoot, 0, V_Outbound_Doorbell_Set_RegisterU),
              ossaHwRegReadExt(agRoot, 0, V_Outbound_Doorbell_Set_Register),
              ossaHwRegReadExt(agRoot, 0, V_Outbound_Doorbell_Mask_Set_RegisterU),
              ossaHwRegReadExt(agRoot, 0, V_Outbound_Doorbell_Mask_Set_Register),
              saRoot->OurInterrupt(agRoot,i)
              ));
          }
          else
          {
            SA_DBG1(("saTimerTick:CheckAll %d ODR 0x%08X ODMR 0x%08X our Int %x\n",
              saRoot->CheckAll,
              siHalRegReadExt(agRoot, GEN_MSGU_ODR,  V_Outbound_Doorbell_Set_Register),
              siHalRegReadExt(agRoot, GEN_MSGU_ODMR, V_Outbound_Doorbell_Mask_Set_Register),
              saRoot->OurInterrupt(agRoot,i)
              ));
          }


          if( saRoot->CheckAll > 1)
          {
            saEchoCommand(agRoot,agNULL, ((i << 16) & 0xFFFF0000 ), (void *)&payload);
          }

        }
        else
        {
          saRoot->OldFlag[i] = 0;
        }

        saRoot->OldPi[i] = circularQ->producerIdx;
        saRoot->OldCi[i] = circularQ->consumerIdx;

      }
    }
  }
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */

  /* (4) Release timer list lock */
  ossaSingleThreadedLeave(agRoot, LL_TIMER_LOCK);
#ifdef SA_FW_TEST_INTERRUPT_REASSERT
  if(saRoot->CheckAll )
  {
    int a;
    for(a=0; a < 32; a++ )
    {
      if (saRoot->interruptVecIndexBitMap[a] & (1 << a))
      {
        SA_DBG1(("saTimerTick DI %d\n",a));
        saSystemInterruptsEnable  ( agRoot, a );

      }
    }
  }
#endif /* SA_FW_TEST_INTERRUPT_REASSERT */
}

/******************************************************************************/
/*! \brief add a timer
 *
 *  add a timer
 *
 *  \param agRoot       handles for this instance of SAS/SATA hardware
 *  \param pTimer       the pointer to the timer being added
 *  \param timeout      the timeout ticks from now
 *  \param pfnTimeout   callback function when time is out
 *  \param Event        the Event code passed to callback function
 *  \param pParm        the pointer to parameter passed to callback function
 *
 *  \return If the timer is added successfully
 *          - \e AGSA_RC_SUCCESS timer is added successfully
 *          - \e AGSA_RC_FAILURE cannot add new timer, run out of resource
 */
/*******************************************************************************/
GLOBAL agsaTimerDesc_t *siTimerAdd(
  agsaRoot_t      *agRoot,
  bit32           timeout,
  agsaCallback_t  pfnTimeout,
  bit32           Event,
  void *          pParm
  )
{
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaTimerDesc_t *pTimer;
  agsaTimerDesc_t *pValidTimer;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "Ta");
  /* (1) Acquire timer list lock */
  ossaSingleThreadedEnter(agRoot, LL_TIMER_LOCK);

  /* (2) Get a free timer */
  pTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->freeTimers));

  /* (3) If the timer is availble  */
  if ( agNULL != pTimer )
  {
    saLlistRemove(&(saRoot->freeTimers), &(pTimer->linkNode));

    /* (3.1) Setup timer */
    saLlinkInitialize(&(pTimer->linkNode));
    /*--------------------------------------**
    ** the timeout shall greater than 0 **
    **--------------------------------------*/
    if ( 0 == timeout )
    {
      timeout = timeout + 1;
    }
    pTimer->valid = agTRUE;
    pTimer->timeoutTick = saRoot->timeTick + timeout;
    pTimer->pfnTimeout = pfnTimeout;
    pTimer->Event = Event;
    pTimer->pParm = pParm;

    /* (3.2) Add timer the timer to valid timer list */
    pValidTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->validTimers));
    /* (3.3) for each timer in the valid timer list */
    while ( agNULL != pValidTimer )
    {
      /* (3.3.1) If the timeoutTick is not wrapped around */
      if ( pTimer->timeoutTick > saRoot->timeTick )
      {
        /* (3.3.1.1) If validTimer wrapped around */
        if ( pValidTimer->timeoutTick < saRoot->timeTick )
        {
          saLlistInsert(&(saRoot->validTimers), &(pValidTimer->linkNode), &(pTimer->linkNode));
          break;
        }
        /* (3.3.1.2) If validTimer is not wrapped around */
        else
        {
          if ( pValidTimer->timeoutTick > pTimer->timeoutTick )
          {
            saLlistInsert(&(saRoot->validTimers), &(pValidTimer->linkNode), &(pTimer->linkNode));
            break;
          }
        }
      }
      /* (3.3.2) If the timeoutTick is wrapped around */
      else
      {
        /* (3.3.2.1) If validTimer is wrapped around */
        if ( pValidTimer->timeoutTick < saRoot->timeTick )
        {
          if ( pValidTimer->timeoutTick > pTimer->timeoutTick )
          {
            saLlistInsert(&(saRoot->validTimers), &(pValidTimer->linkNode), &(pTimer->linkNode));
            break;
          }
        }
      }
      /* (3.3.3) Continue to the next valid timer */
      pValidTimer = (agsaTimerDesc_t *) saLlistGetNext(&(saRoot->validTimers), &(pValidTimer->linkNode));
    }

    /* (3.4) No timers in the validtimer list is greater than this timer */
    if ( agNULL == pValidTimer )
    {
      saLlistAdd(&(saRoot->validTimers), &(pTimer->linkNode));
    }
  }

  /* (4) Release timer list lock */
  ossaSingleThreadedLeave(agRoot, LL_TIMER_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Ta");

  return pTimer;
}

/******************************************************************************/
/*! \brief remove a valid timer
 *
 *  remove a timer
 *
 *  \param agRoot       handles for this instance of SAS/SATA hardware
 *  \param pTimer       the timer to be removed
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siTimerRemove(
  agsaRoot_t      *agRoot,
  agsaTimerDesc_t *pTimer
  )
{
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  /* (1) Acquire timer list lock */
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Tb");
  ossaSingleThreadedEnter(agRoot, LL_TIMER_LOCK);

  /* (2) If the timer is still valid */
  if ( agTRUE == pTimer->valid )
  {
    /* (2.1) remove from the valid timer list */
    saLlistRemove(&(saRoot->validTimers), &(pTimer->linkNode));
    /* (2.2) Invalid the timer */
    pTimer->valid = agFALSE;
    /* (2.3) return the timer to the free timer list */
    saLlistAdd(&(saRoot->freeTimers), &(pTimer->linkNode));
  }
  /* (3) Release timer list lock */
  ossaSingleThreadedLeave(agRoot, LL_TIMER_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Tb");

  return;
}

/******************************************************************************/
/*! \brief remove all valid timer
 *
 *  remove all timer
 *
 *  \param agRoot       handles for this instance of SAS/SATA hardware
 *
 *  \return -void-
 */
/*******************************************************************************/
GLOBAL void siTimerRemoveAll(
  agsaRoot_t      *agRoot
  )
{
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaTimerDesc_t *pTimer;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"Tc");

  /* (1) Acquire timer list lock */
  ossaSingleThreadedEnter(agRoot, LL_TIMER_LOCK);

  /* (2) Get a valid timer */
  pTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->validTimers));

  /* (3) If the timer is valid  */
  while ( agNULL != pTimer )
  {
    /* (3.1) remove from the valid timer list */
    saLlistRemove(&(saRoot->validTimers), &(pTimer->linkNode));

    /* (3.2) Invalid timer */
    pTimer->valid = agFALSE;

    /* (3.3) return the timer to the free timer list */
    saLlistAdd(&(saRoot->freeTimers), &(pTimer->linkNode));

    /* (3.4) get next valid timer */
    pTimer = (agsaTimerDesc_t *) saLlistGetHead(&(saRoot->validTimers));
  }

  /* (4) Release timer list lock */
  ossaSingleThreadedLeave(agRoot, LL_TIMER_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Tc");

  return;
}
