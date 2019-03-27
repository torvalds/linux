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
/*! \file saphy.c
 *  \brief The file implements the functions to Start, Stop a phy
 *
 *
 */
/******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'K'
#endif


extern bit32 gFPGA_TEST;
/******************************************************************************/
/*! \brief Start a Phy
 *
 *  Start a Phy
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param agContext
 *  \param phyId the phy id of the link will be started
 *  \param agPhyConfig the phy configuration
 *  \param agSASIdentify the SAS identify frame will be sent by the phy
 *
 *  \return If phy is started successfully
 *          - \e AGSA_RC_SUCCESS phy is started successfully
 *          - \e AGSA_RC_BUSY phy is already started or starting
 *          - \e AGSA_RC_FAILURE phy is not started successfully
 */
/*******************************************************************************/
GLOBAL bit32 saPhyStart(
  agsaRoot_t         *agRoot,
  agsaContext_t      *agContext,
  bit32              queueNum,
  bit32              phyId,
  agsaPhyConfig_t    *agPhyConfig,
  agsaSASIdentify_t  *agSASIdentify
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  bit32               ret = AGSA_RC_SUCCESS;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "7a");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agSASIdentify), "");

  SA_DBG3(("saPhyStart: phy%d started with ID %08X:%08X\n",
    phyId,
    SA_IDFRM_GET_SAS_ADDRESSHI(agSASIdentify),
    SA_IDFRM_GET_SAS_ADDRESSLO(agSASIdentify)));

  /* If phyId is invalid, return failure */
  if ( phyId >= saRoot->phyCount )
  {
    ret = AGSA_RC_FAILURE;
  }
  /* If phyId is valid */
  else
  {
    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /* */
    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
      /* If no LL Control request entry available */
      if(agNULL != pRequest)
      {
        using_reserved = agTRUE;
        SA_DBG1(("saPhyStart, using saRoot->freeReservedRequests\n"));
      }
      else
      {
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        SA_DBG1(("saPhyStart, No request from free list Not using saRoot->freeReservedRequests\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "7a");
        return AGSA_RC_BUSY;
      }
    }
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    pRequest->valid = agTRUE;
    /* If LL Control request entry avaliable */
    if( using_reserved )
    {
      saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* Remove the request from free list */
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* Build the Phy Start IOMB command and send to SPC */

    smTrace(hpDBG_VERY_LOUD,"P2", phyId);
    /* TP:P2 phyId */

    ret = mpiPhyStartCmd(agRoot, pRequest->HTag, phyId, agPhyConfig, agSASIdentify, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saPhyStart: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saPhyStart, sending IOMB failed\n" ));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "7a");

  return ret;
}

/******************************************************************************/
/*! \brief Stop a Phy
 *
 *  Stop a Phy
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param agContext the context of this API
 *  \param phyId the phy id of the link will be stopped
 *
 *  \return If phy is stopped successfully
 *          - \e AGSA_RC_SUCCESS phy is stopped successfully
 *          - \e AGSA_RC_FAILURE phy is not stopped successfully
 */
/*******************************************************************************/
GLOBAL bit32 saPhyStop(
  agsaRoot_t      *agRoot,
  agsaContext_t   *agContext,
  bit32           queueNum,
  bit32           phyId
  )
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  bit32               ret = AGSA_RC_SUCCESS;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"7b");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  SA_DBG2(("saPhyStop: phy%d stop\n", phyId));

  if(1)
  {
    mpiOCQueue_t         *circularQ;
    int i;
    SA_DBG4(("saPhyStop:\n"));
    for ( i = 0; i < saRoot->QueueConfig.numOutboundQueues; i++ )
    {
      circularQ = &saRoot->outboundQueue[i];
      OSSA_READ_LE_32(circularQ->agRoot, &circularQ->producerIdx, circularQ->piPointer, 0);
      if(circularQ->producerIdx != circularQ->consumerIdx)
      {
        SA_DBG1(("saPhyStop: PI 0x%03x CI 0x%03x\n",circularQ->producerIdx, circularQ->consumerIdx ));
      }
    }
  }

  if(smIS_SPC(agRoot))
  { 
    phyId &= 0xF;
  }
  /* If phyId is invalid, return failure */
  if ( (phyId & 0xF) >= saRoot->phyCount )
  {
    ret = AGSA_RC_FAILURE;
    SA_DBG1(("saPhyStop: phy%d - failure with phyId\n", phyId));
  }
  else
  {
    /* If phyId is valid */
    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/
    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
      /* If no LL Control request entry available */
      if(agNULL != pRequest)
      {
        using_reserved = agTRUE;
        SA_DBG1(("saPhyStop: using saRoot->freeReservedRequests\n"));
      }
      else
      {
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        SA_DBG1(("saPhyStop, No request from free list Not using saRoot->freeReservedRequests\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "7b");
        return AGSA_RC_BUSY;
      }
    }
    /* Remove the request from free list */
    if( using_reserved )
    {
      saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* build IOMB command and send to SPC */
    ret = mpiPhyStopCmd(agRoot, pRequest->HTag, phyId, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG2(("saPhyStop: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saPhyStop, sending IOMB failed\n" ));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "7b");

  return ret;
}

/******************************************************************************/
/*! \brief CallBack Routine to stop a Phy
 *
 *  CallBack for Stop a Phy
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param phyId the phy id of the link will be stopped
 *  \param status the status of the phy
 *  \param agContext the context of the saPhyStop
 *
 *  \return If phy is stopped successfully
 *          - \e AGSA_RC_SUCCESS phy is stopped successfully
 *          - \e AGSA_RC_FAILURE phy is not stopped successfully
 */
/*******************************************************************************/
GLOBAL bit32 siPhyStopCB(
  agsaRoot_t    *agRoot,
  bit32         phyId,
  bit32         status,
  agsaContext_t *agContext,
  bit32         portId,
  bit32         npipps
  )
{
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaPhy_t               *pPhy;
  agsaPort_t              *pPort;
  bit32                   ret = AGSA_RC_SUCCESS;
  bit32                   iomb_status = status;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"7c");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* If phyId is invalid, return failure */
  if ( phyId >= saRoot->phyCount )
  {
    ret = AGSA_RC_FAILURE;
    SA_DBG1(("siPhyStopCB: phy%d - failure with phyId\n", phyId));
    /* makeup for CB */
    status = (status << SHIFT8) | phyId;
    status |= ((npipps & PORT_STATE_MASK) << SHIFT16);
    ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_PHY_STOP_STATUS, status, agContext, agNULL);
  }
  /* If phyId is valid */
  else
  {
    pPhy = &(saRoot->phys[phyId]);

    /* get the port of the phy */
    pPort = pPhy->pPort;

    /* makeup for CB */
    status = (status << SHIFT8) | phyId;
    status |= ((npipps & PORT_STATE_MASK) << SHIFT16);
    /* Callback to stop phy */
    if ( agNULL != pPort )
    {
      if ( iomb_status == OSSA_SUCCESS && (OSSA_PORT_INVALID == (npipps & PORT_STATE_MASK) ))
      {
        SA_DBG1(("siPhyStopCB: phy%d invalidating port\n", phyId));
        /* invalid port state, remove the port */
        pPort->status |= PORT_INVALIDATING;
        saRoot->PortMap[portId].PortStatus  |= PORT_INVALIDATING;
        /* invalid the port */
        siPortInvalid(agRoot, pPort);
        /* map out the portmap */
        saRoot->PortMap[pPort->portId].PortContext = agNULL;
        saRoot->PortMap[pPort->portId].PortID = PORT_MARK_OFF;
        saRoot->PortMap[pPort->portId].PortStatus  |= PORT_INVALIDATING;
      }
      ossaHwCB(agRoot, &(pPort->portContext), OSSA_HW_EVENT_PHY_STOP_STATUS, status, agContext, agNULL);
    }
    else
    {
      SA_DBG1(("siPhyStopCB: phy%d - Port is not established\n", phyId));
      ossaHwCB(agRoot, agNULL, OSSA_HW_EVENT_PHY_STOP_STATUS, status, agContext, agNULL);
    }

    /* set PHY_STOPPED status */
    PHY_STATUS_SET(pPhy, PHY_STOPPED);

    /* Exclude the phy from a port */
    if ( agNULL != pPort )
    {
      /* Acquire port list lock */
      ossaSingleThreadedEnter(agRoot, LL_PORT_LOCK);

      /* Delete the phy from the port */
      pPort->phyMap[phyId] = agFALSE;
      saRoot->phys[phyId].pPort = agNULL;

      /* Release port list lock */
      ossaSingleThreadedLeave(agRoot, LL_PORT_LOCK);
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "7c");

  /* return */
  return ret;
}

/******************************************************************************/
/*! \brief Initiate a Local PHY control command
 *
 *  This function is called to initiate a PHY control command to the local PHY.
 *  The completion of this function is reported in ossaLocalPhyControlCB()

 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param agContext the context of this API
 *  \param phyId  phy number
 *  \param phyOperation
 *    one of AGSA_PHY_LINK_RESET, AGSA_PHY_HARD_RESET, AGSA_PHY_ENABLE_SPINUP
 *
 *  \return
 *          - none
 */
/*******************************************************************************/
GLOBAL bit32 saLocalPhyControl(
  agsaRoot_t             *agRoot,
  agsaContext_t          *agContext,
  bit32                   queueNum,
  bit32                   phyId,
  bit32                   phyOperation,
  ossaLocalPhyControlCB_t agCB
  )
{
  agsaLLRoot_t         *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t  *pRequest;
  agsaPhyErrCounters_t errorParam;
  bit32                ret = AGSA_RC_SUCCESS;
  bit32                value, value1, value2, copyPhyId;
  bit32                count = 100;
  bit32                using_reserved = agFALSE;


  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("saLocalPhyControl: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD,"7d");

  si_memset(&errorParam,0,sizeof(agsaPhyErrCounters_t));
  SA_DBG2(("saLocalPhyControl: phy%d operation %08X\n", phyId, phyOperation));

  switch(phyOperation)
  {
    case AGSA_PHY_LINK_RESET:
    case AGSA_PHY_HARD_RESET:
    case AGSA_PHY_NOTIFY_ENABLE_SPINUP:
    case AGSA_PHY_BROADCAST_ASYNCH_EVENT:
    case AGSA_PHY_COMINIT_OOB:
    {
      /* Get request from free IORequests */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/

      /* If no LL Control request entry available */
      if ( agNULL == pRequest )
      {
        pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
        /* If no LL Control request entry available */
        if(agNULL != pRequest)
        {
          using_reserved = agTRUE;
          SA_DBG1(("saLocalPhyControl, using saRoot->freeReservedRequests\n"));
        }
        else
        {
          ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
          SA_DBG1(("saLocalPhyControl, No request from free list Not using saRoot->freeReservedRequests\n"));
          smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "7d");
          return AGSA_RC_BUSY;
        }
      }
      if( using_reserved )
      {
        saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      /* Remove the request from free list */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      pRequest->completionCB = (void*)agCB;
      //  pRequest->abortCompletionCB = agCB;
      saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
      saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
      saRoot->IOMap[pRequest->HTag].agContext = agContext;
      pRequest->valid = agTRUE;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      /* Build the local phy control IOMB command and send to SPC */
      ret = mpiLocalPhyControlCmd(agRoot, pRequest->HTag, phyId, phyOperation, queueNum);
      if (AGSA_RC_SUCCESS != ret)
      {
        /* remove the request from IOMap */
        saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
        saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
        saRoot->IOMap[pRequest->HTag].agContext = agNULL;
        pRequest->valid = agFALSE;

        ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        /* return the request to free pool */
        if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
        {
          SA_DBG1(("saLocalPhyControl: saving pRequest (%p) for later use\n", pRequest));
          saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
        }
        else
        {
          /* return the request to free pool */
          saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
        }
        SA_DBG1(("saLocalPhyControl, sending IOMB failed\n" ));
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        return ret;
      }
    }
    break;
    case AGSA_PHY_GET_ERROR_COUNTS:
    {
      if(smIS_SPCV(agRoot))
      {

        SA_ASSERT((smIS_SPC(agRoot)), "SPC only");
        SA_DBG1(("saLocalPhyControl: V AGSA_PHY_GET_ERROR_COUNTS\n" ));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "7d");
        return AGSA_RC_FAILURE;
      }
      /* If phyId is invalid, return failure */
      if ( phyId >= saRoot->phyCount )
      {
        ret = AGSA_RC_FAILURE;
        si_memset(&errorParam, 0, sizeof(agsaPhyErrCounters_t));
        SA_DBG1(("saLocalPhyControl: phy%d - failure with phyId\n", phyId));
        /* call back with the status */

        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "7d");
        return ret;
      }
      /* save phyId */
      copyPhyId = phyId;
      /* map 0x030000 or 0x040000 based on phyId to BAR4(0x20), BAT2(win) to access the register  */
      if (phyId < 4)
      {
        /* for phyId = 0, 1, 2, 3 */
        value = 0x030000;
      }
      else
      {
        /* for phyId = 4, 5, 6, 7 */
        phyId = phyId - 4;
        value = 0x040000;
      }

      /* Need to make sure DEVICE_LCLK_GENERATION register bit 6 is 0 */
      value1 = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK);

      SA_DBG3(("saLocalPhyControl: TOP DEVICE LCLK Register value = %08X\n", value1));
      /* If LCLK_CLEAR bit set then disable it */
      if (value1 & DEVICE_LCLK_CLEAR)
      {
        ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK, (value1 & 0xFFFFFFBF) );
        SA_DBG3(("saLocalPhyControl: TOP DEVICE LCLK value = %08X\n", (value1 & 0xFFFFFFBF)));
      }

      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, value))
      {
        SA_DBG1(("saLocalPhyControl:Shift Bar4 to 0x%x failed\n", value));
        phyId = copyPhyId;
        /* call back with the status */

        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }

        smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "7d");
        return AGSA_RC_FAILURE;
      }

      /* set LCLK = 1 and LCLK_CLEAR = 0 */
      SPC_WRITE_COUNTER_CNTL(phyId, LCLK);

      /* LCLK bit should be low to be able to read error registers */
      while((value = SPC_READ_COUNTER_CNTL(phyId)) & LCLK)
      {
        if(--count == 0)
        {
          SA_DBG1(("saLocalPhyControl: Timeout,SPC_COUNTER_CNTL value = %08X\n", value));
          ret = AGSA_RC_FAILURE;
          break;
        }
      } /* while */

      value = SPC_READ_COUNTER_CNTL(phyId);
      SA_DBG3(("saLocalPhyControl: SPC_COUNTER_CNTL value = %08X\n", value));

      /* invalidDword */
      errorParam.invalidDword = SPC_READ_INV_DW_COUNT(phyId);
      /* runningDisparityError */
      errorParam.runningDisparityError = SPC_READ_DISP_ERR_COUNT(phyId);
      /* lossOfDwordSynch */
      errorParam.lossOfDwordSynch = SPC_READ_LOSS_DW_COUNT(phyId);
      /* phyResetProblem */
      errorParam.phyResetProblem = SPC_READ_PHY_RESET_COUNT(phyId);
      /* codeViolation */
      errorParam.codeViolation = SPC_READ_CODE_VIO_COUNT(phyId);
      /* never occurred in SPC8x6G */
      errorParam.elasticityBufferOverflow = 0;
      errorParam.receivedErrorPrimitive = 0;
      errorParam.inboundCRCError = 0;

      SA_DBG3(("saLocalPhyControl:INV_DW_COUNT         0x%x\n", SPC_READ_INV_DW_COUNT(phyId)));
      SA_DBG3(("saLocalPhyControl:DISP_ERR_COUNT       0x%x\n", SPC_READ_DISP_ERR_COUNT(phyId)));
      SA_DBG3(("saLocalPhyControl:LOSS_DW_COUNT        0x%x\n", SPC_READ_LOSS_DW_COUNT(phyId)));
      SA_DBG3(("saLocalPhyControl:PHY_RESET_COUNT      0x%x\n", SPC_READ_PHY_RESET_COUNT(phyId)));
      SA_DBG3(("saLocalPhyControl:CODE_VIOLATION_COUNT 0x%x\n", SPC_READ_CODE_VIO_COUNT(phyId)));

      /* Shift back to BAR4 original address */
      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, 0x0))
      {
        SA_DBG1(("saLocalPhyControl:Shift Bar4 to 0x%x failed\n", 0x0));
        ret = AGSA_RC_FAILURE;
      }

      /* restore back the Top Device LCLK generation register value */
      ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK, value1);

      /* restore phyId */
      phyId = copyPhyId;
      /* call back with the status */

      if (AGSA_RC_SUCCESS == ret)
      {
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, copyPhyId, phyOperation, OSSA_SUCCESS, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, copyPhyId, phyOperation, OSSA_SUCCESS, (void *)&errorParam);
        }
      }
      else
      {
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
      }
      break;
    }
    case AGSA_PHY_CLEAR_ERROR_COUNTS:
    {
      if(smIS_SPCV(agRoot))
      {

        SA_ASSERT((smIS_SPC(agRoot)), "SPC only");
        SA_DBG1(("saLocalPhyControl: V AGSA_PHY_CLEAR_ERROR_COUNTS\n" ));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "7d");
        return AGSA_RC_FAILURE;
      }
      /* If phyId is invalid, return failure */
      if ( phyId >= saRoot->phyCount )
      {
        si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
        SA_DBG3(("saLocalPhyControl(CLEAR): phy%d - failure with phyId\n", phyId));
        /* call back with the status */
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "7d");
        return AGSA_RC_FAILURE;
      }
      /* save phyId */
      copyPhyId = phyId;
      /* map 0x030000 or 0x040000 based on phyId to BAR4(0x20), BAT2(win) to access the register  */
      if (phyId < 4)
      {
        /* for phyId = 0, 1, 2, 3 */
        value = 0x030000;
      }
      else
      {
        /* for phyId = 4, 5, 6, 7 */
        phyId = phyId - 4;
        value = 0x040000;
      }
      /* Need to make sure DEVICE_LCLK_GENERATION register bit 6 is 1 */
      value2 = ossaHwRegReadExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK);

      SA_DBG3(("saLocalPhyControl: TOP DEVICE LCLK Register value = %08X\n", value2));
      /* If LCLK_CLEAR bit not set then set it */
      if ((value2 & DEVICE_LCLK_CLEAR) == 0)
      {
        ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK, (value2 | DEVICE_LCLK_CLEAR) );
        SA_DBG3(("saLocalPhyControl: TOP DEVICE LCLK value = %08X\n", (value2 & 0xFFFFFFBF)));
      }

      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, value))
      {
        SA_DBG1(("saLocalPhyControl(CLEAR):Shift Bar4 to 0x%x failed\n", value));
        phyId = copyPhyId;
        /* call back with the status */
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "7d");
        return AGSA_RC_FAILURE;
      }

      /* read Counter Control register */
      value1 = SPC_READ_COUNTER_CNTL(phyId);
      SA_DBG3(("saLocalPhyControl(CLEAR): SPC_COUNTER_CNTL value = %08X\n", value1));
      /* set LCLK and LCLK_CLEAR */
      SPC_WRITE_COUNTER_CNTL(phyId, (LCLK_CLEAR | LCLK));
      /* read back the value of register */
      /* poll LCLK bit = 0 */
      while((value = SPC_READ_COUNTER_CNTL(phyId)) & LCLK)
      {
        if(--count == 0)
        {
          SA_DBG1(("saLocalPhyControl: Timeout,SPC_COUNTER_CNTL value = %08X\n", value));
          ret = AGSA_RC_FAILURE;
          break;
        }
      } /* while */

      value = SPC_READ_COUNTER_CNTL(phyId);
      SA_DBG3(("saLocalPhyControl(CLEAR): SPC_COUNTER_CNTL value = %08X\n", value));

      /* restore the value */
      SPC_WRITE_COUNTER_CNTL(phyId, value1);

      /* Shift back to BAR4 original address */
      if (AGSA_RC_FAILURE == siBar4Shift(agRoot, 0x0))
      {
        SA_DBG1(("saLocalPhyControl:Shift Bar4 to 0x%x failed\n", 0x0));
        ret = AGSA_RC_FAILURE;
      }

      /* restore back the Top Device LCLK generation register value */
      ossaHwRegWriteExt(agRoot, PCIBAR2, SPC_REG_DEVICE_LCLK, value2);

      /* restore phyId */
      phyId = copyPhyId;
      /* call back with the status */
      if (AGSA_RC_SUCCESS == ret)
      {
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_SUCCESS, agNULL);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_SUCCESS, agNULL);
        }
      }
      else
      {
        if( agCB == agNULL )
        {
          ossaLocalPhyControlCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
        else
        {
          agCB(agRoot, agContext, phyId, phyOperation, OSSA_FAILURE, (void *)&errorParam);
        }
      }
      break;
    }
    case AGSA_PHY_GET_BW_COUNTS:
    {
      SA_ASSERT((smIS_SPC(agRoot)), "SPCv only");
      SA_DBG1(("saLocalPhyControl: AGSA_PHY_GET_BW_COUNTS\n" ));
      break;
    }

    default:
      ret = AGSA_RC_FAILURE;
      SA_ASSERT(agFALSE, "(saLocalPhyControl) Unknown operation");
      break;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "7d");
  return ret;
}


GLOBAL bit32 saGetPhyProfile(
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         queueNum,
                      bit32         ppc,
                      bit32         phyId
                      )
{
  bit32 ret = AGSA_RC_SUCCESS;

  agsaLLRoot_t            *saRoot = agNULL;
  agsaPhyErrCountersPage_t errorParam;

  ossaLocalPhyControlCB_t agCB = ossaGetPhyProfileCB;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
   
  if(saRoot == agNULL)
  {
    SA_DBG3(("saGetPhyProfile : saRoot is NULL"));
    return AGSA_RC_FAILURE;
  }
  
  SA_DBG1(("saGetPhyProfile: ppc 0x%x phyID %d\n", ppc,phyId));

  switch(ppc)
  {
    case AGSA_SAS_PHY_ERR_COUNTERS_PAGE:
    {
      if(smIS_SPCV(agRoot))
      {

        SA_DBG1(("saGetPhyProfile: V AGSA_SAS_PHY_ERR_COUNTERS_PAGE\n" ));

        ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
        smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "7d");
        return ret;
      }
    }
    case AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE:
    {
      /* If phyId is invalid, return failure */
      if ( phyId >= saRoot->phyCount )
      {
        si_memset(&errorParam, 0, sizeof(agsaPhyErrCountersPage_t));
        SA_DBG3(("saGetPhyProfile(CLEAR): phy%d - failure with phyId\n", phyId));
        /* call back with the status */
        ossaGetPhyProfileCB(agRoot, agContext, phyId, ppc, OSSA_FAILURE, (void *)&errorParam);
        smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "7d");
        return AGSA_RC_FAILURE;
      }
      if(smIS_SPCV(agRoot))
      {
        SA_DBG1(("saGetPhyProfile: V AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE\n" ));

        ret = mpiGetPhyProfileCmd( agRoot,agContext, ppc,phyId,agCB);
        smTraceFuncExit(hpDBG_VERY_LOUD, 'k', "7d");
        return ret;
      }

    }
    case AGSA_SAS_PHY_BW_COUNTERS_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_SAS_PHY_BW_COUNTERS_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }
    case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }

    case AGSA_SAS_PHY_GENERAL_STATUS_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_SAS_PHY_GENERAL_STATUS_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }
    case AGSA_PHY_SNW3_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_PHY_SNW3_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }
    case AGSA_PHY_RATE_CONTROL_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_PHY_RATE_CONTROL_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }
    case AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE:
    {
      SA_DBG1(("saGetPhyProfile: AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE\n" ));
      ret = mpiGetPhyProfileCmd( agRoot,agContext,ppc ,phyId,agCB);
      break;
    }

    default:
      SA_DBG1(("saGetPhyProfile: Unknown operation 0x%X\n",ppc ));
      SA_ASSERT(agFALSE, "saGetPhyProfile Unknown operation " );
      break;

  }
  return ret;

}


GLOBAL bit32 saSetPhyProfile (
                      agsaRoot_t    *agRoot,
                      agsaContext_t *agContext,
                      bit32         queueNum,
                      bit32         ppc,
                      bit32         length,
                      void          *buffer,
                      bit32         phyID
                      )
{
  bit32 ret = AGSA_RC_SUCCESS;

  SA_DBG1(("saSetPhyProfile: ppc 0x%x length 0x%x phyID %d\n", ppc,length,phyID));

  switch(ppc)
  {
    case AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE:
    {
      SA_DBG1(("saSetPhyProfile: AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE\n" ));
      ret = mpiSetPhyProfileCmd( agRoot,agContext,ppc ,phyID,length,buffer);
      break;
    }
    case AGSA_PHY_SNW3_PAGE:
    {
      SA_DBG1(("saSetPhyProfile: AGSA_PHY_SNW3_PAGE\n" ));
      ret = mpiSetPhyProfileCmd( agRoot,agContext,ppc ,phyID,length,buffer);
      break;
    }
    case AGSA_PHY_RATE_CONTROL_PAGE:
    {
      SA_DBG1(("saSetPhyProfile: AGSA_PHY_RATE_CONTROL_PAGE\n" ));
      ret = mpiSetPhyProfileCmd( agRoot,agContext,ppc ,phyID,length,buffer);
      break;
    }
    case AGSA_SAS_PHY_MISC_PAGE:
    {
      SA_DBG1(("saSetPhyProfile: AGSA_SAS_PHY_MISC_PAGE\n"));
      ret = mpiSetPhyProfileCmd( agRoot,agContext,ppc ,phyID,length,buffer);
      break;
    }

    default:
      SA_DBG1(("saSetPhyProfile: Unknown operation 0x%X\n",ppc ));
      SA_ASSERT(agFALSE, "saSetPhyProfile Unknown operation " );
      ret = AGSA_RC_FAILURE;
      break;
  }
  return ret;
}


/******************************************************************************/
/*! \brief Initiate a HW Event Ack command
 *
 *  This function is called to initiate a HW Event Ack command to the SPC.
 *  The completion of this function is reported in ossaHwEventAckCB().
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param agContext   the context of this API
 *  \param queueNum    queue number
 *  \param eventSource point to the event source structure
 *  \param param0
 *  \param param1
 *
 *  \return
 *          - none
 */
/*******************************************************************************/
GLOBAL bit32 saHwEventAck(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             queueNum,
                      agsaEventSource_t *eventSource,
                      bit32             param0,
                      bit32             param1
                      )
{
  agsaLLRoot_t           *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t    *pRequest;
  agsaPortContext_t      *agPortContext;
  agsaPort_t             *pPort = agNULL;
  agsaSASHwEventAckCmd_t payload;
  bit32                  phyportid;
  bit32                  ret = AGSA_RC_SUCCESS;
  bit32                  using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"7e");

  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("saHwEventAck: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }

  SA_DBG2(("saHwEventAck: agContext %p eventSource %p\n", agContext, eventSource));
  SA_DBG1(("saHwEventAck: event 0x%x param0 0x%x param1 0x%x\n", eventSource->event, param0, param1));

  agPortContext = eventSource->agPortContext;

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests)); /**/
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG1(("saHwEventAck, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* If no LL Control request entry available */
      SA_DBG1(("saHwEventAck, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "7e");
      return AGSA_RC_BUSY;
    }
  }
  if( using_reserved )
  {
    saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((!pRequest->valid), "The pRequest is in use");

  SA_DBG2(("saHwEventAck: queueNum 0x%x HTag 0x%x\n",queueNum ,pRequest->HTag));

  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;

  /* build IOMB command and send to SPC */
  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(agsaSASHwEventAckCmd_t));

  /* find port id */
  if (agPortContext)
  {
    pPort = (agsaPort_t *) (agPortContext->sdkData);
    if (pPort)
    {
      if(eventSource->event == OSSA_HW_EVENT_PHY_DOWN)
      {
        pPort->tobedeleted = agTRUE;
      }
      SA_DBG3(("saHwEventAck,pPort->portId %X\n",pPort->portId));

      if(smIS_SPC(agRoot))
      {
        /* fillup PORT_ID field */
        phyportid = pPort->portId & 0xF;
      }
      else
      {
        /* fillup PORT_ID field */
        phyportid = pPort->portId & 0xFF;

      }
    }
    else
    {
      /*  pPort is NULL - set PORT_ID to not intialized  */
      if(smIS_SPC(agRoot))
      {
        phyportid = 0xF;
      }
      else
      {
        phyportid = 0xFF;
      }
    }
  }
  else
  {
    /* agPortContext is NULL - set PORT_ID to not intialized  */
    if(smIS_SPC(agRoot))
    {
      phyportid = 0xF;
    }
    else
    {
      phyportid = 0xFF;
    }
  }

  pRequest->pPort = pPort;

  SA_DBG3(("saHwEventAck,eventSource->param 0x%X\n",eventSource->param));
  SA_DBG3(("saHwEventAck,eventSource->event 0x%X\n",eventSource->event));

  if(smIS_SPC(agRoot))
  {
    /* fillup up PHY_ID */
    phyportid |= ((eventSource->param & 0x0000000F) << 4);
    /* fillup SEA field */
    phyportid |= (eventSource->event & 0x0000FFFF) << 8;
    SA_DBG3(("saHwEventAck: portId 0x%x phyId 0x%x SEA 0x%x\n", phyportid & 0xF,
      eventSource->param & 0x0000000F, eventSource->event & 0x0000FFFF));
  }
  else
  {
    /* fillup up PHY_ID */
    phyportid |= ((eventSource->param & 0x000000FF) << SHIFT24);
    /* fillup SEA field */
    phyportid |= (eventSource->event & 0x00FFFFFF) << SHIFT8;
    SA_DBG3(("saHwEventAck: portId 0x%x phyId 0x%x SEA 0x%x\n", phyportid & 0xFF,
      eventSource->param & 0x0000000F, eventSource->event & 0x0000FFFF));
  }

  pRequest->HwAckType =  (bit16)phyportid;

  SA_DBG1(("saHwEventAck,phyportid 0x%X HwAckType 0x%X\n",phyportid,pRequest->HwAckType));
  /* set tag */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASHwEventAckCmd_t, tag), pRequest->HTag);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASHwEventAckCmd_t, sEaPhyIdPortId), phyportid);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASHwEventAckCmd_t, Param0), param0);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASHwEventAckCmd_t, Param1), param1);

  /* build IOMB command and send to SPC */

  if(smIS_SPC(agRoot))
  {
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SPC_SAS_HW_EVENT_ACK, IOMB_SIZE64, queueNum);
  }
  else
  {
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SAS_HW_EVENT_ACK, IOMB_SIZE64, queueNum);
  }

  if (AGSA_RC_SUCCESS != ret)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;
    pRequest->valid = agFALSE;

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("saHwEventAck: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saHwEventAck, sending IOMB failed\n" ));
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "7e");

  return ret;
}


GLOBAL bit32 saVhistCapture(
                          agsaRoot_t    *agRoot,
                          agsaContext_t *agContext,
                          bit32         queueNum,
                          bit32         Channel,
                          bit32         NumBitLo,
                          bit32         NumBitHi,
                          bit32         PcieAddrLo,
                          bit32         PcieAddrHi,
                          bit32         ByteCount )
{

  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  bit32               ret = AGSA_RC_SUCCESS;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3N");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  SA_DBG1(("saVhistCapture:Channel 0x%08X 0x%08X%08X 0x%08X%08X  count 0x%X\n",Channel, NumBitHi, NumBitLo ,PcieAddrHi,PcieAddrLo,ByteCount));

  {
    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /* */
    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
      /* If no LL Control request entry available */
      if(agNULL != pRequest)
      {
        using_reserved = agTRUE;
        SA_DBG1((", using saRoot->freeReservedRequests\n"));
      }
      else
      {
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
        SA_DBG1(("saVhistCapture: No request from free list Not using saRoot->freeReservedRequests\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3N");
        return AGSA_RC_BUSY;
      }
    }
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    pRequest->valid = agTRUE;
    /* If LL Control request entry avaliable */
    if( using_reserved )
    {
      saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* Remove the request from free list */
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* Build the VhisCapture IOMB command and send to SPCv */

    ret = mpiVHistCapCmd(agRoot,agContext, queueNum, Channel, NumBitLo, NumBitHi ,PcieAddrLo, PcieAddrHi, ByteCount);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saPhyStart: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saVhistCapture: sending IOMB failed\n" ));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3N");

  return ret;
}

