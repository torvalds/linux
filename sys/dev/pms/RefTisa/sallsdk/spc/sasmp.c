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
/*! \file sasmp.c
 *  \brief The file implements the functions for SMP request/response
 *
 */
/*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/RefTisa/sallsdk/spc/saglobal.h>
#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef siTraceFileID
#undef siTraceFileID
#endif
#define siTraceFileID 'N'
#endif

/******************************************************************************/
/*! \brief Start SMP request
 *
 *  Start SMP request
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param queueNum
 *  \param agIORequest
 *  \param agDevHandle
 *  \param agRequestType
 *  \param agRequestBody
 *  \param agCB
 * Spc - support    direct mode direct response
 * SpcV - support   direct mode direct response
 * SpcV - support indirect mode  direct response
 * SpcV - support indirect mode indirect response
 *
 *  \return If request is started successfully
 *          - \e AGSA_RC_SUCCESS request is started successfully
 *          - \e AGSA_RC_BUSY No resource available, try again later
 */
/*******************************************************************************/
GLOBAL bit32 saSMPStart(
  agsaRoot_t            *agRoot,
  agsaIORequest_t       *agIORequest,
  bit32                 queueNum,
  agsaDevHandle_t       *agDevHandle,
  bit32                 agRequestType,
  agsaSASRequestBody_t  *agRequestBody,
  ossaSMPCompletedCB_t  agCB
  )
{
  bit32                     ret = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t              *saRoot = agNULL;
  mpiICQueue_t              *circularQ;
  agsaDeviceDesc_t          *pDevice;
  agsaPort_t                *pPort;
  agsaIORequestDesc_t       *pRequest;
  void                      *pMessage;
  bit8                      i, inq, outq;
  bit8                      using_reserved = agFALSE;
  bit8                      *payload_ptr;
  agsaSMPFrame_t            *pSMPFrame;

  SA_DBG4(("saSMPStart: start\n"));

  smTraceFuncEnter(hpDBG_VERY_LOUD, "9a");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agIORequest), "");
  SA_ASSERT((agNULL != agDevHandle), "");
  SA_ASSERT((agNULL != agRequestBody), "");

  /* sanity check */
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  if(saRoot == agNULL)
  {
    SA_DBG1(("saSMPStart : saRoot is NULL!!\n"));
    return AGSA_RC_FAILURE;
  }
  
  /* Assign inbound and outbound queue number */
  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

  /* Find the outgoing port for the device */
  if (agNULL == agDevHandle->sdkData)
  {
    /* Device has been removed */
    SA_DBG1(("saSMPStart, Device has been removed. agDevHandle=%p\n", agDevHandle));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "9a");
    return AGSA_RC_FAILURE;
  }

  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);

  pPort = pDevice->pPort;

  /* Get request from free IO Requests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/

  /* If no LL IO request entry available */
  if ( agNULL == pRequest )
  {

    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));

    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG1(("saSMPStart, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saSMPStart, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "9a");
      return AGSA_RC_BUSY;
    }
  }

  /* If free IOMB avaliable */
  /* Remove the request from free list */
  if( using_reserved )
  {
    saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  /* Add the request to the pendingSMPRequests list of the device */
  saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));
  SA_ASSERT((!pRequest->valid), "The pRequest is in use");
  pRequest->valid             = agTRUE;
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* set up pRequest */
  pRequest->pIORequestContext = agIORequest;
  pRequest->pDevice           = pDevice;
  pRequest->pPort             = pPort;
  pRequest->requestType       = agRequestType;
  pRequest->startTick         = saRoot->timeTick;
  pRequest->completionCB      = (ossaSSPCompletedCB_t)agCB;

  /* Set request to the sdkData of agIORequest */
  agIORequest->sdkData        = pRequest;

  /* save tag to IOMap */
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

  /* If LL IO request entry avaliable */
  /* Get a free inbound queue entry */
  circularQ = &saRoot->inboundQueue[inq];
  retVal    = mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);

  if (AGSA_RC_FAILURE == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
    /* if not sending return to free list rare */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSMPStart, error when get free IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "9a");
    return AGSA_RC_FAILURE;
  }

  /* return busy if inbound queue is full */
  if (AGSA_RC_BUSY == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
    /* if not sending return to free list rare */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSMPStart, no more IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "9a");
    return AGSA_RC_BUSY;
  }

  /* Setup SMP Frame */
  pSMPFrame = (agsaSMPFrame_t *) &(agRequestBody->smpFrame);

  SA_DBG2(("saSMPStart:DeviceMapIndex 0x%x portId 0x%x portId 0x%x\n",pDevice->DeviceMapIndex,pPort->portId,pPort->portId));

#if defined(SALLSDK_DEBUG)

  SA_DBG2(("saSMPStart: outFrameBuf  %p\n",pSMPFrame->outFrameBuf));

  if(pSMPFrame->outFrameBuf )
  {
    SA_DBG2(("saSMPStart: outFrameBuf 0  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+0) ));
    SA_DBG2(("saSMPStart: outFrameBuf 1  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+1) ));
    SA_DBG2(("saSMPStart: outFrameBuf 2  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+2) ));
    SA_DBG2(("saSMPStart: outFrameBuf 3  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+3) ));
    SA_DBG2(("saSMPStart: outFrameBuf 4  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+4) ));
    SA_DBG2(("saSMPStart: outFrameBuf 5  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+5) ));
    SA_DBG2(("saSMPStart: outFrameBuf 6  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+6) ));
    SA_DBG2(("saSMPStart: outFrameBuf 7  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+7) ));
    SA_DBG2(("saSMPStart: outFrameBuf 8  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+8) ));
    SA_DBG2(("saSMPStart: outFrameBuf 9  0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+9) ));
    SA_DBG2(("saSMPStart: outFrameBuf 11 0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+10) ));
    SA_DBG2(("saSMPStart: outFrameBuf 11 0x%08X\n",*((bit32*)pSMPFrame->outFrameBuf+11) ));
  }
  SA_DBG2(("saSMPStart: outFrameAddrUpper32 0x%08X\n",pSMPFrame->outFrameAddrUpper32));
  SA_DBG2(("saSMPStart: outFrameAddrLower32 0x%08X\n",pSMPFrame->outFrameAddrLower32));
  SA_DBG2(("saSMPStart: outFrameLen         0x%08X\n",pSMPFrame->outFrameLen));
  SA_DBG2(("saSMPStart: inFrameAddrUpper32  0x%08X\n",pSMPFrame->inFrameAddrUpper32));
  SA_DBG2(("saSMPStart: inFrameAddrLower32  0x%08X\n",pSMPFrame->inFrameAddrLower32));
  SA_DBG2(("saSMPStart: inFrameLen          0x%08X\n",pSMPFrame->inFrameLen));
  SA_DBG2(("saSMPStart: expectedRespLen     0x%08X\n",pSMPFrame->expectedRespLen));
  SA_DBG2(("saSMPStart: flag                0x%08X\n",pSMPFrame->flag));
#endif /* SALLSDK_DEBUG */

  if(smIS_SPC(agRoot))
  // if(1)
  {
    agsaSMPCmd_t payload;
    switch ( agRequestType )
    {
      case AGSA_SMP_INIT_REQ:
      {
        bit32 IR_IP_OV_res_phyId_DPdLen_res = 0;
        /* Prepare the payload of IOMB */
        si_memset(&payload, 0, sizeof(agsaSMPCmd_t));
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, tag), pRequest->HTag);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, deviceId), pDevice->DeviceMapIndex);

        /* check SMP Response Frame with IR mode */
        /* check if the SMP Response is indirect mode */
        if (0 == pSMPFrame->inFrameLen)
        {
          /* PHY override not support */
          /* Direct Response mode */
          pRequest->IRmode = DIRECT_MODE;
        }
        else
        {
          /* Indirect Response mode */
          pRequest->IRmode = INDIRECT_MODE;
          IR_IP_OV_res_phyId_DPdLen_res = 1;
          /* check SMP direct payload mode len */
          if (pSMPFrame->outFrameLen > 32)
          {
#ifdef SA_LL_IBQ_PROTECT
            ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
            /* if not sending return to free list rare */
            ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
            saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
            pRequest->valid = agFALSE;
            saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
            ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
            /* can not handle SMP frame length > 32 bytes it if IP=0 and IR=1 */
            SA_DBG1(("saSMPStart, outFrameLen > 32 bytes error.\n"));
            smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "9a");
            return AGSA_RC_FAILURE;
          }
        }

        /* check Direct mode or Indirect mode for IP mode */
        if ( (pSMPFrame->outFrameBuf &&
              (pSMPFrame->outFrameLen <= AGSA_MAX_SMPPAYLOAD_VIA_SFO)) ||
             ((pSMPFrame->outFrameBuf == agNULL) &&
              (pSMPFrame->outFrameLen == 0) )
           )
        {
          SA_DBG4(("saSMPStart: DIRECT Request SMP\n"));

          IR_IP_OV_res_phyId_DPdLen_res = (DIRECT_MODE << 1) | IR_IP_OV_res_phyId_DPdLen_res;

          /* Direct payload length */
          IR_IP_OV_res_phyId_DPdLen_res |= (((pSMPFrame->outFrameLen) & 0xff) << SHIFT16);

          /* copy payload - upto 48 bytes */
          si_memcpy(&(payload.SMPCmd[0]),pSMPFrame->outFrameBuf,pSMPFrame->outFrameLen);
          for ( i = 0; i < pSMPFrame->outFrameLen / sizeof(bit32)+1; i ++ )
          {
            SA_DBG4(("saSMPStart: payload.SMPCmd[%d] %x\n", i, payload.SMPCmd[i]));
          }
        }
        else
        {
          SA_DBG4(("saSMPStart: INDIRECT Request SMP\n"));
          /* use physical address */
          IR_IP_OV_res_phyId_DPdLen_res = (INDIRECT_MODE << 1) | IR_IP_OV_res_phyId_DPdLen_res;

          /* Direct payload length = 0 */
          IR_IP_OV_res_phyId_DPdLen_res = IR_IP_OV_res_phyId_DPdLen_res & 0xff00ffff;

          /* payload */
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[4]), (pSMPFrame->outFrameAddrLower32));
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[5]), (pSMPFrame->outFrameAddrUpper32));
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[6]), (pSMPFrame->outFrameLen));
        }
        /* Write IR_IP_OV_res_phyId_DPdLen_res field in the payload*/
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);

        /* check IR bit */
        if (IR_IP_OV_res_phyId_DPdLen_res & INDIRECT_MODE)
        {
          /* setup indirect response frame address */
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[8]), (pSMPFrame->inFrameAddrLower32));
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[9]), (pSMPFrame->inFrameAddrUpper32));
          OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[10]), (pSMPFrame->inFrameLen));
        }

        /* Build IOMB command and send it to SPC */
        payload_ptr = (bit8 *)&payload;
        ret = mpiSMPCmd(agRoot, pMessage, OPC_INB_SMP_REQUEST, (agsaSMPCmd_t *)payload_ptr, inq, outq);

  #ifdef SA_LL_IBQ_PROTECT
        ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
  #endif /* SA_LL_IBQ_PROTECT */

        break;
      }
      default:
      {
        SA_DBG1(("saSMPStart: SPC unknown agRequestType  %x\n",agRequestType));
        break;
      }
    }

#ifdef SALL_API_TEST
  if (ret == AGSA_RC_SUCCESS)
    saRoot->LLCounters.IOCounter.numSMPStarted++;
#endif
  }
  else /* IOMB is different for SPCV SMP */
  {
   agsaSMPCmd_V_t vpayload;

    switch ( agRequestType )
    {
      case AGSA_SMP_INIT_REQ:
      {
        bit32 IR_IP_OV_res_phyId_DPdLen_res = 0;
        /* Prepare the payload of IOMB */
        si_memset(&vpayload, 0, sizeof(agsaSMPCmd_V_t));
        OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, tag), pRequest->HTag);
        OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, deviceId), pDevice->DeviceMapIndex);

        /* Request header must be valid regardless of IP bit */
        OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMPHDR ), *((bit32*)pSMPFrame->outFrameBuf+0) );

        /* check SMP Response Frame with IR mode */
        /* check if the SMP Response is indirect mode */
        // smpFrameFlagDirectResponse smpFrameFlagDirectPayload
        if ( 0 == pSMPFrame->flag && pSMPFrame->outFrameBuf )
        {
          /* PHY override not support */
          /* Direct Response mode */
          pRequest->IRmode = DIRECT_MODE;
          SA_DBG2(("saSMPStart:V DIRECT Request SMP\n"));

          IR_IP_OV_res_phyId_DPdLen_res = (DIRECT_MODE << 1) | IR_IP_OV_res_phyId_DPdLen_res;

          /* Direct payload length */
          IR_IP_OV_res_phyId_DPdLen_res |= (((pSMPFrame->outFrameLen) & 0xff) << SHIFT16);
          /* Write IR_IP_OV_res_phyId_DPdLen_res field in the payload*/
          /* fatal error if missing */
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);
          /* fatal error if missing */

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP3_0 ), *((bit32*)pSMPFrame->outFrameBuf+1) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP7_4 ), *((bit32*)pSMPFrame->outFrameBuf+2) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP11_8), *((bit32*)pSMPFrame->outFrameBuf+3) );

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirL_SMPRF15_12 ),     *((bit32*)pSMPFrame->outFrameBuf+4) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirH_or_SMPRF19_16 ),  *((bit32*)pSMPFrame->outFrameBuf+5) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirLen_or_SMPRF23_20 ),*((bit32*)pSMPFrame->outFrameBuf+6) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,R_or_SMPRF27_24),        *((bit32*)pSMPFrame->outFrameBuf+7) );

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRAL_or_SMPRF31_28 ),   *((bit32*)pSMPFrame->outFrameBuf+8) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRAH_or_SMPRF35_32 ),   *((bit32*)pSMPFrame->outFrameBuf+9) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRL_or_SMPRF39_36 ),    *((bit32*)pSMPFrame->outFrameBuf+10) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,R_or_SMPRF43_40 ),       *((bit32*)pSMPFrame->outFrameBuf+11) );

        }
        else if (smpFrameFlagIndirectResponse & pSMPFrame->flag && smpFrameFlagIndirectPayload & pSMPFrame->flag) /* */
        {
          /* IR IP */
          SA_DBG2(("saSMPStart:V smpFrameFlagIndirectResponse smpFrameFlagIndirectPayload SMP\n"));

          pRequest->IRmode = INDIRECT_MODE;
          IR_IP_OV_res_phyId_DPdLen_res = 3;

          /* Indirect payload mode */
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirL_SMPRF15_12 ), pSMPFrame->outFrameAddrLower32);
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirH_or_SMPRF19_16 ), pSMPFrame->outFrameAddrUpper32);
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirLen_or_SMPRF23_20 ), pSMPFrame->outFrameLen);
          /* Indirect Response mode */
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAL_or_SMPRF31_28 ), (pSMPFrame->inFrameAddrLower32));
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAH_or_SMPRF35_32 ), (pSMPFrame->inFrameAddrUpper32));
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRL_or_SMPRF39_36 ), (pSMPFrame->inFrameLen));
        }
        else if (smpFrameFlagIndirectPayload & pSMPFrame->flag ) /* */
        {
          /* IP */
          SA_DBG2(("saSMPStart:V  smpFrameFlagIndirectPayload SMP\n"));
          pRequest->IRmode = DIRECT_MODE;
          IR_IP_OV_res_phyId_DPdLen_res = 2;

          /* Indirect payload mode */
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirL_SMPRF15_12 ), pSMPFrame->outFrameAddrLower32);
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirH_or_SMPRF19_16 ), pSMPFrame->outFrameAddrUpper32);
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirLen_or_SMPRF23_20 ), pSMPFrame->outFrameLen);
        }
        else if (smpFrameFlagIndirectResponse & pSMPFrame->flag ) /* */
        {
          /* check IR bit */
          /* Indirect Response mode */
          pRequest->IRmode = INDIRECT_MODE;
          SA_DBG2(("saSMPStart:V smpFrameFlagIndirectResponse SMP\n"));
          /* use physical address */
          IR_IP_OV_res_phyId_DPdLen_res = 1;
          /* Direct payload length */
          IR_IP_OV_res_phyId_DPdLen_res |= (((pSMPFrame->outFrameLen) & 0xff) << SHIFT16);

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP3_0 ), *((bit32*)pSMPFrame->outFrameBuf+1) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP7_4 ), *((bit32*)pSMPFrame->outFrameBuf+2) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMP11_8), *((bit32*)pSMPFrame->outFrameBuf+3) );

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirL_SMPRF15_12 ),     *((bit32*)pSMPFrame->outFrameBuf+4) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirH_or_SMPRF19_16 ),  *((bit32*)pSMPFrame->outFrameBuf+5) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirLen_or_SMPRF23_20 ),*((bit32*)pSMPFrame->outFrameBuf+6) );
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,R_or_SMPRF27_24),        *((bit32*)pSMPFrame->outFrameBuf+7) );

          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAL_or_SMPRF31_28 ), (pSMPFrame->inFrameAddrLower32));
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAH_or_SMPRF35_32 ), (pSMPFrame->inFrameAddrUpper32));
          OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRL_or_SMPRF39_36 ), (pSMPFrame->inFrameLen));
        }
        IR_IP_OV_res_phyId_DPdLen_res |= (pSMPFrame->flag & 3);
        /* fatal error if missing */
        OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);
        /* fatal error if missing */
      }
      /* Build IOMB command and send it to SPCv */
      payload_ptr = (bit8 *)&vpayload;
      ret = mpiSMPCmd(agRoot, pMessage, OPC_INB_SMP_REQUEST, (agsaSMPCmd_t *)payload_ptr, inq, outq);

#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

      break;
      default:
      {
        SA_DBG1(("saSMPStart: SPCv unknown agRequestType  %x\n",agRequestType));
        break;
      }
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "9a");

  /* return */
  return ret;
}

/******************************************************************************/
/*! \brief Abort SMP request
 *
 *  Abort SMP request
 *
 *  \param agRoot handles for this instance of SAS/SATA hardware
 *  \param queueNum
 *  \param agIORequest
 *
 *  \return If request is aborted successfully
 *          - \e AGSA_RC_SUCCESS request is aborted successfully
 *          - \e AGSA_RC_FAILURE request is not aborted successfully
 */
/*******************************************************************************/
GLOBAL bit32 saSMPAbort(
  agsaRoot_t           *agRoot,
  agsaIORequest_t      *agIORequest,
  bit32                 queueNum,
  agsaDevHandle_t      *agDevHandle,
  bit32                 flag,
  void                 *abortParam,
  ossaGenericAbortCB_t  agCB
  )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaIORequestDesc_t *pRequestABT = NULL;
  agsaIORequest_t     *agIOToBeAborted;
  agsaDeviceDesc_t    *pDevice;
  agsaSMPAbortCmd_t   payload;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"9b");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agIORequest), "");
  SA_ASSERT((agNULL != agDevHandle), "");

  SA_DBG3(("saSMPAbort: Aborting request %p\n", agIORequest));

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    agIOToBeAborted = (agsaIORequest_t *)abortParam;
    /* Get LL IORequest entry for saSMPAbort() */
    pRequestABT = (agsaIORequestDesc_t *) (agIOToBeAborted->sdkData);
    if (agNULL == pRequestABT)
    {
      /* The IO to Be Abort is no longer exist - can not Abort */
      SA_DBG1(("saSMPAbort: pRequestABT AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "9b");
      return AGSA_RC_FAILURE;
    }

    /* Find the device the request Abort to */
    pDevice = pRequestABT->pDevice;

    if (agNULL == pDevice)
    {
      /* no deviceID - can not build IOMB */
      SA_DBG1(("saSMPAbort: pDevice AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "9b");
      return AGSA_RC_FAILURE;
    }
  }
  else
  {
    if (ABORT_ALL == (flag & ABORT_MASK))
    {
      /* abort All with Device or Port */
      /* Find the outgoing port for the device */
      pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
      if (agNULL == pDevice)
      {
        /* no deviceID - can not build IOMB */
        SA_DBG1(("saSMPAbort:ABORT_ALL pDevice AGSA_RC_FAILURE\n"));
        smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "9b");
        return AGSA_RC_FAILURE;
      }
    }
    else
    {
      /* only support 00 and 01 for flag */
      SA_DBG1(("saSMPAbort:flag  AGSA_RC_FAILURE\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "9b");
      return AGSA_RC_FAILURE;
    }
  }

  /* Get LL IORequest entry */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL IO request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests)); /**/
    /* If no LL Control request entry available */
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG1(("saSMPAbort, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saSMPAbort, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "9b");
      return AGSA_RC_BUSY;
    }
  }

  /* If free IOMB avaliable */
  /* Remove the request from free list */
  if( using_reserved )
  {
    saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  /* Add the request to the pendingSMPRequests list of the device */
  saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));
  SA_ASSERT((!pRequest->valid), "The pRequest is in use");
  pRequest->valid             = agTRUE;
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* set up pRequest */
  pRequest->pIORequestContext = agIORequest;
  pRequest->requestType       = AGSA_SMP_REQTYPE;
  pRequest->completionCB = (void*)agCB;
  pRequest->pDevice           = pDevice;
  pRequest->startTick         = saRoot->timeTick;

  /* Set request to the sdkData of agIORequest */
  agIORequest->sdkData        = pRequest;

  /* save tag to IOMap */
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;

  /* setup payload */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPAbortCmd_t, tag), pRequest->HTag);

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    if (agNULL == pRequestABT)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      /* Delete the request from the pendingSMPRequests */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saSMPAbort: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saSMPAbort, agNULL == pRequestABT\n"));
      /* The IO to Be Abort is no longer exist - can not Abort */
      smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "9b");
      return AGSA_RC_FAILURE;
    }
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPAbortCmd_t, HTagAbort), pRequestABT->HTag);
  }
  else
  {
    /* abort all */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPAbortCmd_t, HTagAbort), 0);
  }
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPAbortCmd_t, deviceId), pDevice->DeviceMapIndex);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPAbortCmd_t, Scp), flag);

  SA_DBG1(("saSMPAbort, HTag 0x%x HTagABT 0x%x deviceId 0x%x\n", payload.tag, payload.HTagAbort, payload.deviceId));

  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SMP_ABORT, IOMB_SIZE64, queueNum);
  if (AGSA_RC_SUCCESS != ret)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;
    /* Delete the request from the pendingSMPRequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("saSMPAbort: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSMPAbort, sending IOMB failed\n" ));
  }
#ifdef SALL_API_TEST
  else
  {
    saRoot->LLCounters.IOCounter.numSMPAborted++;
  }
#endif

  smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "9b");

  return ret;
}



