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

********************************************************************************/
/*******************************************************************************/
/*! \file sassp.c
 *  \brief The file implements the functions for SSP request/response
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
#define siTraceFileID 'O'
#endif

#ifdef LOOPBACK_MPI
extern int loopback;
#endif

#ifdef SALLSDK_DEBUG
LOCAL void siDumpSSPStartIu(
  agsaDevHandle_t       *agDevHandle,
  bit32                 agRequestType,
  agsaSASRequestBody_t  *agRequestBody
  );
#endif

#ifdef FAST_IO_TEST
LOCAL bit32 saGetIBQPI(agsaRoot_t *agRoot,
                       bit32 queueNum)
{
  bit8         inq;
  mpiICQueue_t *circularQ;
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  inq = INQ(queueNum);
  circularQ = &saRoot->inboundQueue[inq];
  return circularQ->producerIdx;
}

LOCAL void saSetIBQPI(agsaRoot_t *agRoot,
                      bit32      queueNum,
                      bit32      pi)
{
  bit8         inq;
  mpiICQueue_t *circularQ;
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  inq = INQ(queueNum);
  circularQ = &saRoot->inboundQueue[inq];
  circularQ->producerIdx = pi;
}

osLOCAL void*
siFastSSPReqAlloc(agsaRoot_t *agRoot)
{
  int             idx;
  agsaLLRoot_t    *saRoot = (agsaLLRoot_t*)(agRoot->sdkData);
  saFastRequest_t *fr;

  if (!saRoot->freeFastIdx)
  {
    SA_DBG1(("saSuperSSPReqAlloc: no memory ERROR\n"));
    SA_ASSERT((0), "");
    return 0;
  }

  ossaSingleThreadedEnter(agRoot, LL_FAST_IO_LOCK);
  saRoot->freeFastIdx--;
  idx = saRoot->freeFastIdx;
  ossaSingleThreadedLeave(agRoot, LL_FAST_IO_LOCK);

  fr = saRoot->freeFastReq[idx];
  SA_ASSERT((fr), "");
  fr->valid = 1;

  return fr;
}

LOCAL void
siFastSSPReqFree(
             agsaRoot_t *agRoot,
             void       *freq)
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  saFastRequest_t *fr = (saFastRequest_t*)freq;

  SA_DBG2(("siFastSSPReqFree: enter\n"));
  SA_ASSERT((fr->valid), "");
  if (saRoot->freeFastIdx >= sizeof(saRoot->freeFastReq) /
                             sizeof(saRoot->freeFastReq[0]))
  {
    SA_DBG1(("siFastSSPReqFree: too many handles %d / %d ERROR\n",
             saRoot->freeFastIdx, (int)(sizeof(saRoot->freeFastReq) /
             sizeof(saRoot->freeFastReq[0]))));
    SA_ASSERT((0), "");
    return;
  }
  ossaSingleThreadedEnter(agRoot, LL_FAST_IO_LOCK);
  /* not need if only one entry */
  /* saRoot->freeFastReq[saRoot->freeFastIdx] = freq;  */
  saRoot->freeFastIdx++;
  ossaSingleThreadedLeave(agRoot, LL_FAST_IO_LOCK);

  fr->valid = 0;
  SA_DBG6(("siFastSSPReqFree: leave\n"));
}

LOCAL bit32 siFastSSPResAlloc(
  agsaRoot_t             *agRoot,
  bit32                  queueNum,
  bit32                  agRequestType,
  agsaDeviceDesc_t       *pDevice,
  agsaIORequestDesc_t    **pRequest,
  void                   **pPayload
  )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t*)(agRoot->sdkData);
  mpiICQueue_t *circularQ;
  bit8  inq;
  bit16 size = IOMB_SIZE64;
  bit32 ret = AGSA_RC_SUCCESS, retVal;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2D");

  SA_DBG4(("Entering function siFastSSPResAlloc:\n"));

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  *pRequest = (agsaIORequestDesc_t*)saLlistIOGetHead(&saRoot->freeIORequests);

  /* If no LL IO request entry available */
  if (agNULL == *pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("siFastSSPResAlloc: No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2D");
    ret = AGSA_RC_BUSY;
    goto ext;
  }

  /* Get IO request from free IORequests */
  /* Assign inbound and outbound Buffer */
  inq = INQ(queueNum);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

  /* SSP_INI_IO_START_EXT IOMB need at least 80 bytes to support 32 CDB */
  if (agRequestType & AGSA_SSP_EXT_BIT)
  {
    size = IOMB_SIZE96;
  }
  /* If LL IO request entry avaliable */
  /* Get a free inbound queue entry */
  circularQ = &saRoot->inboundQueue[inq];
  retVal = mpiMsgFreeGet(circularQ, size, pPayload);

  /* if message size is too large return failure */
  if (AGSA_RC_SUCCESS != retVal)
  {
    if (AGSA_RC_FAILURE == retVal)
    {
      SA_DBG1(("siFastSSPResAlloc: error when get free IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2D");
    }

    /* return busy if inbound queue is full */
    if (AGSA_RC_BUSY == retVal)
    {
      SA_DBG3(("siFastSSPResAlloc: no more IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2D");
    }
    ret = retVal;
    goto ext;
  }

  /* But add it to the pending queue during FastStart */
  /* If free IOMB avaliable */
  /* Remove the request from free list */
  saLlistIORemove(&saRoot->freeIORequests, &(*pRequest)->linkNode);

  /* Add the request to the pendingIORequests list of the device */
  saLlistIOAdd(&pDevice->pendingIORequests, &(*pRequest)->linkNode);

ext:
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  if (AGSA_RC_SUCCESS == ret)
  {
    /* save tag and IOrequest pointer to IOMap */
    saRoot->IOMap[(*pRequest)->HTag].Tag = (*pRequest)->HTag;
    saRoot->IOMap[(*pRequest)->HTag].IORequest = (void *)*pRequest;
  }

  return ret;
} /* siFastSSPResAlloc */


GLOBAL bit32 saFastSSPCancel(void *ioHandle)
{
  agsaRoot_t      *agRoot;
  agsaLLRoot_t    *saRoot;
  saFastRequest_t *fr;
  bit32            i;
  agsaIORequestDesc_t *ior;

  SA_ASSERT((ioHandle), "");
  fr = (saFastRequest_t*)ioHandle;
  SA_ASSERT((fr->valid), "");
  agRoot = (agsaRoot_t*)fr->agRoot;
  SA_ASSERT((agRoot), "");
  saRoot = (agsaLLRoot_t*)(agRoot->sdkData);
  SA_ASSERT((saRoot), "");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2E");

  /* rollback the previously set IBQ PI */
  for (i = 0; i < fr->inqMax - 1; i++)
    saSetIBQPI(agRoot, fr->inqList[i], fr->beforePI[fr->inqList[i]]);

  /* free all the previous Fast IO Requests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* at least one entry, no need to check for NULL saLlistIOGetHead() */
  ior = (agsaIORequestDesc_t*)((char*)saLlistIOGetHead(&fr->requests) -
                              OSSA_OFFSET_OF(agsaIORequestDesc_t, fastLink));
  do
  {
    agsaDeviceDesc_t *pDevice;
    void             *tmp;

    pDevice = ior->pDevice;
    saLlistIORemove(&pDevice->pendingIORequests, &ior->linkNode);
    saLlistIOAdd(&saRoot->freeIORequests, &ior->linkNode);

    tmp = (void*)saLlistGetNext(&fr->requests, &ior->fastLink);
    if (!tmp)
    {
      break; /* end of list */
    }
    ior = (agsaIORequestDesc_t*)((char*)tmp -
                                 OSSA_OFFSET_OF(agsaIORequestDesc_t, fastLink));
  } while (1);

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* free the IBQ PI tracking struct */
  siFastSSPReqFree(agRoot, fr);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2E");
  return AGSA_RC_SUCCESS;
} /* saFastSSPCancel */

GLOBAL void *saFastSSPPrepare(
                 void                 *ioh,
                 agsaFastCommand_t    *fc,
                 ossaSSPCompletedCB_t cb,
                 void                 *cbArg)
{
  bit32            ret = AGSA_RC_SUCCESS;
  agsaRoot_t       *agRoot;
  agsaLLRoot_t     *saRoot;
  mpiICQueue_t     *circularQ;
  agsaDeviceDesc_t *pDevice;
  agsaSgl_t        *pSgl;
  bit32            Dir = 0;
  bit8             inq, outq;
  saFastRequest_t  *fr;
  void             *pMessage;
  agsaIORequestDesc_t *pRequest;
  bit16            opCode;
  bitptr           offsetTag;
  bitptr           offsetDeviceId;
  bitptr           offsetDataLen;
  bitptr           offsetDir;

  agRoot = (agsaRoot_t*)fc->agRoot;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2G");

  OSSA_INP_ENTER(agRoot);

  saRoot = (agsaLLRoot_t*)(agRoot->sdkData);
  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");

  SA_DBG4(("Entering function saFastSSPPrepare:\n"));

  fr = (saFastRequest_t*)ioh;
  if (!fr)
  {
    int i;
    fr = siFastSSPReqAlloc(agRoot);
    if (!fr)
    {
      SA_ASSERT((0), "");
      goto ext;
    }

    saLlistIOInitialize(&fr->requests);
    for (i = 0; i < AGSA_MAX_INBOUND_Q; i++)
      fr->beforePI[i] = (bit32)-1;

    fr->inqMax = 0;
    fr->agRoot = agRoot;
    ioh = fr;
  }

  /* Find the outgoing port for the device */
  pDevice = (agsaDeviceDesc_t*)(((agsaDevHandle_t*)fc->devHandle)->sdkData);

  ret = siFastSSPResAlloc(agRoot, fc->queueNum, fc->agRequestType,
                          pDevice, &pRequest, &pMessage);
  if (ret != AGSA_RC_SUCCESS)
  {
    SA_ASSERT((0), "");
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2G");
    goto ext;
  }

  /* Assign inbound and outbound Buffer */
  inq = INQ(fc->queueNum);
  outq = OUQ(fc->queueNum);
  circularQ = &saRoot->inboundQueue[inq];

  SA_DBG3(("saFastSSPPrepare: deviceId %d\n", pDevice->DeviceMapIndex));

  /* set up pRequest */
  pRequest->valid = agTRUE;
  pRequest->pDevice = pDevice;
  pRequest->requestType = fc->agRequestType;

  pRequest->completionCB = cb;
  pRequest->pIORequestContext = (agsaIORequest_t*)cbArg;

  pSgl = fc->agSgl;

  switch (fc->agRequestType)
  {
    /* case AGSA_SSP_INIT_NONDATA: */
    case AGSA_SSP_INIT_READ:
    case AGSA_SSP_INIT_WRITE:
    case AGSA_SSP_INIT_READ_M:
    case AGSA_SSP_INIT_WRITE_M:
    {
      agsaSSPIniIOStartCmd_t *pPayload = (agsaSSPIniIOStartCmd_t *)pMessage;
      agsaSSPCmdInfoUnit_t   *piu;

      /* SSPIU less equal 28 bytes */
      offsetTag = OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, tag);
      offsetDeviceId = OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, deviceId);
      offsetDataLen = OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, dataLen);
      offsetDir = OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, dirMTlr);

      piu = &pPayload->SSPInfoUnit;

      si_memcpy(piu->lun, fc->lun, sizeof(piu->lun));
      si_memcpy(piu->cdb, fc->cdb, sizeof(piu->cdb));
      piu->efb_tp_taskAttribute = fc->taskAttribute;
      piu->additionalCdbLen = fc->additionalCdbLen;

      /* Mask DIR for Read/Write command */
      Dir = fc->agRequestType & AGSA_DIR_MASK;

      /* set TLR */
      Dir |= fc->flag & TLR_MASK;
      if (fc->agRequestType & AGSA_MSG)
      {
        /* set M bit */
        Dir |= AGSA_MSG_BIT;
      }

      /* Setup SGL */
      if (fc->dataLength)
      {
        SA_DBG5(("saFastSSPPrepare: agSgl %08x:%08x (%x/%x)\n",
                 pSgl->sgUpper, pSgl->sgLower, pSgl->len, pSgl->extReserved));
        /*
        pPayload->AddrLow0 = pSgl->sgLower;
        pPayload->AddrHi0 = pSgl->sgUpper;
        pPayload->Len0 = pSgl->len;
        pPayload->E0 = pSgl->extReserved;
        */
        si_memcpy(&pPayload->AddrLow0, pSgl, sizeof(*pSgl));
      }
      else
      {
        /* no data transfer */
        si_memset(&pPayload->AddrLow0, 0, sizeof(*pSgl));
      }

      opCode = OPC_INB_SSPINIIOSTART;
      break;
    }

    case AGSA_SSP_INIT_READ_EXT:
    case AGSA_SSP_INIT_WRITE_EXT:
    case AGSA_SSP_INIT_READ_EXT_M:
    case AGSA_SSP_INIT_WRITE_EXT_M:
    {
      agsaSSPIniExtIOStartCmd_t *pPayload =
                                    (agsaSSPIniExtIOStartCmd_t *)pMessage;
      agsaSSPCmdInfoUnitExt_t   *piu;
      bit32 sspiul;

      /* CDB > 16 bytes */
      offsetTag = OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, tag);
      offsetDeviceId = OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, deviceId);
      offsetDataLen = OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, dataLen);
      offsetDir = OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, SSPIuLendirMTlr);

      /* dword (bit7-bit2) ==> bytes (bit7-bit0) */
      /* setup standard CDB bytes + additional CDB bytes in length field */
      sspiul = sizeof(agsaSSPCmdInfoUnit_t) + (fc->additionalCdbLen & 0xFC);

      Dir = sspiul << 16;
      piu = (agsaSSPCmdInfoUnitExt_t*)pPayload->SSPIu;

      si_memcpy(piu->lun, fc->lun, sizeof(piu->lun));
      si_memcpy(piu->cdb, fc->cdb, MIN(sizeof(piu->cdb),
                                       16 + fc->additionalCdbLen));
      piu->efb_tp_taskAttribute = fc->taskAttribute;
      piu->additionalCdbLen = fc->additionalCdbLen;

      /* Mask DIR for Read/Write command */
      Dir |= fc->agRequestType & AGSA_DIR_MASK;

      /* set TLR */
      Dir |= fc->flag & TLR_MASK;
      if (fc->agRequestType & AGSA_MSG)
      {
        /* set M bit */
        Dir |= AGSA_MSG_BIT;
      }

      /* Setup SGL */
      if (fc->dataLength)
      {
        SA_DBG5(("saSuperSSPSend: Ext mode, agSgl %08x:%08x (%x/%x)\n",
          pSgl->sgUpper, pSgl->sgLower, pSgl->len, pSgl->extReserved));

        si_memcpy((&(pPayload->SSPIu[0]) + sspiul), pSgl, sizeof(*pSgl));
      }
      else //?
      {
        /* no data transfer */
        //pPayload->dataLen = 0;
        si_memset((&(pPayload->SSPIu[0]) + sspiul), 0, sizeof(*pSgl));
      }
      SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
      opCode = OPC_INB_SSPINIEXTIOSTART;
      break;
    }

    default:
    {
      SA_DBG1(("saSuperSSPSend: Unsupported Request IOMB\n"));
      ret = AGSA_RC_FAILURE;
      SA_ASSERT((0), "");
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2G");
      goto ext;
    }
  }

  OSSA_WRITE_LE_32(agRoot, pMessage, offsetTag, pRequest->HTag);
  OSSA_WRITE_LE_32(agRoot, pMessage, offsetDeviceId, pDevice->DeviceMapIndex);
  OSSA_WRITE_LE_32(agRoot, pMessage, offsetDataLen, fc->dataLength);
  OSSA_WRITE_LE_32(agRoot, pMessage, offsetDir, Dir);

  if (fr->beforePI[inq] == -1)
  {
    /* save the new IBQ' PI */
    fr->beforePI[inq] = saGetIBQPI(agRoot, inq);
    fr->inqList[fr->inqMax++] = inq;
  }

  /* post the IOMB to SPC */
  ret = mpiMsgPrepare(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA,
                      opCode, outq, 0);
  if (AGSA_RC_SUCCESS != ret)
  {
    SA_ASSERT((0), "");
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    /* Remove the request from pendingIORequests list */
    saLlistIORemove(&pDevice->pendingIORequests, &pRequest->linkNode);

    /* Add the request to the free list of the device */
    saLlistIOAdd(&saRoot->freeIORequests, &pRequest->linkNode);
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saFastSSPPrepare: error when post SSP IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2G");
    goto ext;
  }

  /* Add the request to the pendingFastIORequests list of the device */
  saLlistIOAdd(&fr->requests, &pRequest->fastLink);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2G");

ext:
  if (fr && ret != AGSA_RC_SUCCESS)
  {
    saFastSSPCancel(fr);
    ioh = 0;
  }
  OSSA_INP_LEAVE(agRoot);
  return ioh;
} /* saFastSSPPrepare */

GLOBAL bit32 saFastSSPSend(void *ioHandle)
{
  bit8            inq;
  agsaRoot_t      *agRoot;
  agsaLLRoot_t    *saRoot;
  saFastRequest_t *fr;
  bit32           i;

  SA_ASSERT((ioHandle), "");
  fr = (saFastRequest_t*)ioHandle;
  agRoot = (agsaRoot_t*)fr->agRoot;
  SA_ASSERT((agRoot), "");
  saRoot = (agsaLLRoot_t*)agRoot->sdkData;
  SA_ASSERT((saRoot), "");

  SA_DBG4(("Entering function saFastSSPSend:\n"));

  for (i = 0; i < fr->inqMax; i++)
  {
    inq = INQ(fr->inqList[i]);
    /* FW interrupt */
    mpiIBQMsgSend(&saRoot->inboundQueue[inq]);
  }
  /* IORequests are freed in siIODone() */

  siFastSSPReqFree(agRoot, fr);
  return AGSA_RC_SUCCESS;
} /* saFastSSPSend */
#endif

/******************************************************************************/
/*! \brief Start SSP request
 *
 *  Start SSP request
 *
 *  \param agRoot handles for this instance of SAS/SATA LLL
 *  \param queueNum
 *  \param agIORequest
 *  \param agDevHandle
 *  \param agRequestType
 *  \param agRequestBody
 *  \param agTMRequest valid for task management
 *  \param agCB
 *
 *  \return If request is started successfully
 *          - \e AGSA_RC_SUCCESS request is started successfully
 *          - \e AGSA_RC_BUSY request is not started successfully
 */
/******************************************************************************/
GLOBAL bit32 saSSPStart(
  agsaRoot_t            *agRoot,
  agsaIORequest_t       *agIORequest,
  bit32                 queueNum,
  agsaDevHandle_t       *agDevHandle,
  bit32                 agRequestType,
  agsaSASRequestBody_t  *agRequestBody,
  agsaIORequest_t       *agTMRequest,
  ossaSSPCompletedCB_t  agCB)
{
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
#ifdef LOOPBACK_MPI
  mpiOCQueue_t        *circularOQ = agNULL;
#endif
  mpiICQueue_t        *circularQ  = agNULL;
  agsaDeviceDesc_t    *pDevice    = agNULL;
  agsaPort_t          *pPort      = agNULL;
  agsaIORequestDesc_t *pRequest   = agNULL;
  agsaSgl_t           *pSgl       = agNULL;
  void                *pMessage   = agNULL;
  bit32               ret = AGSA_RC_SUCCESS, retVal = 0;
  bit32               DirDW4 = 0;    /* no data and no AutoGR */
  bit32               encryptFlags = 0;
  bit16               size = 0;
  bit16               opCode = 0;
  bit8                inq = 0, outq = 0;


  OSSA_INP_ENTER(agRoot);
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Sa");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agIORequest), "");
  SA_ASSERT((agNULL != agDevHandle), "");
  SA_ASSERT((agNULL != agRequestBody), "");

  DBG_DUMP_SSPSTART_CMDIU(agDevHandle,agRequestType,agRequestBody);

  /* Find the outgoing port for the device */
  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);

  if(pDevice == agNULL )
  {
    SA_ASSERT((pDevice), "pDevice");
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Sa");
    goto ext;
  }

  pPort = pDevice->pPort;
  /* Assign inbound and outbound Buffer */
  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

  SA_DBG3(("saSSPStart: inq %d outq %d deviceId 0x%x\n", inq,outq,pDevice->DeviceMapIndex));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL IO request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSSPStart, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Sa");
    ret = AGSA_RC_BUSY;
    goto ext;
  }
  /* If LL IO request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    /* Add the request to the pendingIORequests list of the device */
    saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));

    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_ASSERT((!pRequest->valid), "The pRequest is in use");

    SA_DBG3(("saSSPStart, request %p\n", pRequest ));

    /* Decode the flag settings in the standard I/O requests to  decide what size we need. */
    /* All other requests will be fine with only 64 byte messages. */
    switch ( agRequestType )
    {
    case AGSA_SSP_INIT_READ:
    case AGSA_SSP_INIT_WRITE:
    case AGSA_SSP_INIT_NONDATA:
    case AGSA_SSP_INIT_READ_M:
    case AGSA_SSP_INIT_WRITE_M:
        {
            agsaSSPInitiatorRequest_t *pIRequest = &(agRequestBody->sspInitiatorReq);

            if ((pIRequest->flag & AGSA_SAS_ENABLE_ENCRYPTION)   ||
#ifdef SAFLAG_USE_DIF_ENC_IOMB
               (pIRequest->flag & AGSA_SAS_USE_DIF_ENC_OPSTART)  ||
#endif /* SAFLAG_USE_DIF_ENC_IOMB */
                (pIRequest->flag & AGSA_SAS_ENABLE_DIF) )
            {
                opCode = OPC_INB_SSP_DIF_ENC_OPSTART;
                size = IOMB_SIZE128;
            }
            else
            {
                opCode = OPC_INB_SSPINIIOSTART;
                size = IOMB_SIZE64;
            }
            break;
        }
    case AGSA_SSP_INIT_READ_EXT:
    case AGSA_SSP_INIT_WRITE_EXT:
    case AGSA_SSP_INIT_READ_EXT_M:
    case AGSA_SSP_INIT_WRITE_EXT_M:
        {
          agsaSSPInitiatorRequestExt_t *pIRequest = &(agRequestBody->sspInitiatorReqExt);

          if ((pIRequest->flag & AGSA_SAS_ENABLE_ENCRYPTION)   ||
              (pIRequest->flag & AGSA_SAS_ENABLE_DIF)          ||
#ifdef SAFLAG_USE_DIF_ENC_IOMB
              (pIRequest->flag & AGSA_SAS_USE_DIF_ENC_OPSTART) ||
#endif /* SAFLAG_USE_DIF_ENC_IOMB */
              (pIRequest->flag & AGSA_SAS_ENABLE_SKIP_MASK))
          {
              opCode = OPC_INB_SSP_DIF_ENC_OPSTART;
              size = IOMB_SIZE128;
          }
          else
          {
              SA_ASSERT((smIS_SPC(agRoot)), "smIS_SPC");
              opCode = OPC_INB_SSPINIEXTIOSTART;
              size = IOMB_SIZE96;
          }
          break;
      }
      case  AGSA_SSP_INIT_READ_INDIRECT:
      case  AGSA_SSP_INIT_WRITE_INDIRECT:
      case  AGSA_SSP_INIT_READ_INDIRECT_M:
      case  AGSA_SSP_INIT_WRITE_INDIRECT_M:
          {
            SA_DBG3(("saSSPStart: agRequestType  0x%X INDIRECT\n", agRequestType));
            opCode = OPC_INB_SSP_DIF_ENC_OPSTART;
            size = IOMB_SIZE128;
            break;
          }
      case (AGSA_SSP_REQTYPE | AGSA_SSP_TASK_MGNT):
      case AGSA_SSP_TASK_MGNT_REQ_M:
      case AGSA_SSP_TGT_READ_DATA:
      case AGSA_SSP_TGT_READ_GOOD_RESP:
      case AGSA_SSP_TGT_WRITE_DATA:
      case AGSA_SSP_TGT_WRITE_GOOD_RESP:
      case AGSA_SSP_TGT_CMD_OR_TASK_RSP:

        SA_DBG3(("saSSPStart: agRequestType  0x%X (was default)\n", agRequestType));
        opCode = OPC_INB_SSPINIIOSTART;
        size = IOMB_SIZE64;
         break;
    default:
        SA_DBG1(("saSSPStart: agRequestType UNKNOWN 0x%X\n", agRequestType));
        /* OpCode is not used in this case, but Linux complains if it is not initialized. */
        opCode = OPC_INB_SSPINIIOSTART;
        size = IOMB_SIZE64;
        break;
    }

    /* If free IOMB avaliable,  set up pRequest*/
    pRequest->valid = agTRUE;
    pRequest->pIORequestContext = agIORequest;
    pRequest->pDevice = pDevice;
    pRequest->requestType = agRequestType;
    pRequest->pPort = pPort;
    pRequest->startTick = saRoot->timeTick;
    pRequest->completionCB = agCB;

    /* Set request to the sdkData of agIORequest */
    agIORequest->sdkData = pRequest;

    /* save tag and IOrequest pointer to IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    /* Get a free inbound queue entry */
#ifdef LOOPBACK_MPI
    if (loopback)
    {
      SA_DBG2(("saSSPStart: did %d ioq %d / %d tag %d\n", pDevice->DeviceMapIndex, inq, outq, pRequest->HTag));
      circularOQ = &saRoot->outboundQueue[outq];
      retVal = mpiMsgFreeGetOQ(circularOQ, size, &pMessage);
    }
    else
#endif /* LOOPBACK_MPI */
    {
      circularQ = &saRoot->inboundQueue[inq];
      retVal = mpiMsgFreeGet(circularQ, size, &pMessage);
    }

    /* if message size is too large return failure */
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

      SA_DBG1(("saSSPStart, error when get free IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Sa");
      ret = AGSA_RC_FAILURE;
      goto ext;
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

      SA_DBG1(("saSSPStart, no more IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Sa");
      ret = AGSA_RC_BUSY;
      goto ext;
    }
    SA_DBG3(("saSSPStart:agRequestType %X\n" ,agRequestType));

    switch ( agRequestType )
    {
      case AGSA_SSP_INIT_READ:
      case AGSA_SSP_INIT_WRITE:
      case AGSA_SSP_INIT_NONDATA:
      case AGSA_SSP_INIT_READ_EXT:
      case AGSA_SSP_INIT_WRITE_EXT:
      case AGSA_SSP_INIT_READ_M:
      case AGSA_SSP_INIT_WRITE_M:
      case AGSA_SSP_INIT_READ_EXT_M:
      case AGSA_SSP_INIT_WRITE_EXT_M:
      case AGSA_SSP_INIT_READ_INDIRECT:
      case AGSA_SSP_INIT_WRITE_INDIRECT:
      case AGSA_SSP_INIT_READ_INDIRECT_M:
      case AGSA_SSP_INIT_WRITE_INDIRECT_M:
      {
        if (!(agRequestType & AGSA_SSP_EXT_BIT))
        {
          agsaSSPInitiatorRequest_t     *pIRequest = &(agRequestBody->sspInitiatorReq);
          agsaSSPIniIOStartCmd_t        *pPayload = (agsaSSPIniIOStartCmd_t *)pMessage;
          agsaSSPIniEncryptIOStartCmd_t *pEncryptPayload = (agsaSSPIniEncryptIOStartCmd_t *)pMessage;

          /* Most fields for the SAS IOMB have the same offset regardless of the actual IOMB used. */
          /* Be careful with the scatter/gather lists, encryption and DIF options. */

/*          if( pIRequest->sspCmdIU.cdb[ 0] ==  0x28 || pIRequest->sspCmdIU.cdb[0]== 0x2A)
          {
            pRequest->requestBlock = ((pIRequest->sspCmdIU.cdb[2] << 24 ) | 
                            (pIRequest->sspCmdIU.cdb[3] << 16 ) | 
                            (pIRequest->sspCmdIU.cdb[4] <<  8 ) |  
                            (pIRequest->sspCmdIU.cdb[5] ) );
          }
*/
#ifdef LOOPBACK_MPI
          if (loopback)
          {
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, tag), pRequest->HTag);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, status), OSSA_IO_SUCCESS);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, param), 0);
          //OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPCompletionRsp_t, SSPTag), 0);
          }
          else
#endif /* LOOPBACK_MPI */
          {
            /* SSPIU less equal 28 bytes */
            /* Configure DWORD 1 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, tag), pRequest->HTag);
            /* Configure DWORD 2 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, deviceId), pDevice->DeviceMapIndex);
            /* Configure DWORD 3 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, dataLen), pIRequest->dataLength);
          }

#ifdef SA_TESTBASE_EXTRA
          /* TestBase - Set the host BST entry  */
          DirDW4 |= ((UINT32)pIRequest->bstIndex) << 16;
#endif /*  SA_TESTBASE_EXTRA */

          if (!(agRequestType & AGSA_SSP_INDIRECT_BIT))
          {
            /* Configure DWORD 5-12  */
            si_memcpy(&pPayload->SSPInfoUnit, &pIRequest->sspCmdIU, sizeof(pPayload->SSPInfoUnit));
            pPayload->dirMTlr     = 0;
            /* Mask DIR for Read/Write command */
            /* Configure DWORD 4 bit 8-9 */
            DirDW4 |= agRequestType & AGSA_DIR_MASK;
          }
          else /* AGSA_SSP_INDIRECT_BIT was set */
          {

            agsaSSPInitiatorRequestIndirect_t *pIndRequest = &(agRequestBody->sspInitiatorReqIndirect);

            /* Configure DWORD 5 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_0_3_indcdbalL ),pIndRequest->sspInitiatorReqAddrLower32);
            /* Configure DWORD 6 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_4_7_indcdbalH ),pIndRequest->sspInitiatorReqAddrUpper32 );
            /* Configure DWORD 7 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_8_11 ), 0);
            /* Configure DWORD 8 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_12_15 ), 0);
            /* Configure DWORD 9 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_16_19 ), 0);
            /* Configure DWORD 10 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_19_23), 0);
            /* Configure DWORD 11 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,sspiu_24_27 ), 0);
            /* Mask DIR for Read/Write command */
            /* Configure DWORD 4 bit 8-9 */
            DirDW4 |= agRequestType & AGSA_DIR_MASK;
            /* Configure DWORD 4 bit 24-31 */
            DirDW4 |= ((pIndRequest->sspInitiatorReqLen >> 2) & 0xFF) << SHIFT24;
            /* Configure DWORD 4 bit 4 */
            DirDW4 |= 1 << SHIFT3;
          }

          /* set TLR */
          DirDW4 |= pIRequest->flag & TLR_MASK;
          if (agRequestType & AGSA_MSG)
          {
            /* set M bit */
            DirDW4 |= AGSA_MSG_BIT;
          }

          /* check for skipmask operation */
          if (pIRequest->flag & AGSA_SAS_ENABLE_SKIP_MASK)
          {
            DirDW4 |= AGSA_SKIP_MASK_BIT;
            /* agsaSSPInitiatorRequestIndirect_t skip mask in flag is offset 5  */
            DirDW4 |= (pIRequest->flag & AGSA_SAS_SKIP_MASK_OFFSET) << SHIFT8;
          }


         /* Configure DWORDS 12-14 */
         if( pIRequest->encrypt.enableEncryptionPerLA && pIRequest->dif.enableDIFPerLA)
         {
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 12 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,epl_descL ),
                             pIRequest->encrypt.EncryptionPerLAAddrLo );
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 13 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,dpl_descL ),
                             pIRequest->dif.DIFPerLAAddrLo );

            SA_ASSERT(pIRequest->encrypt.EncryptionPerLAAddrHi == pIRequest->dif.DIFPerLAAddrHi, "EPL DPL hi region must be equal");

            if( pIRequest->encrypt.EncryptionPerLAAddrHi != pIRequest->dif.DIFPerLAAddrHi )
            {

              SA_DBG1(("saSSPStart: EPL DPL hi region must be equal AGSA_RC_FAILURE\n" ));
              smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "Sa");
              ret = AGSA_RC_FAILURE;
              goto ext;
            }

            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 14 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,edpl_descH ),
                             pIRequest->encrypt.EncryptionPerLAAddrHi );
          }
          else if( pIRequest->encrypt.enableEncryptionPerLA)
          {
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 12 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,epl_descL ),
                             pIRequest->encrypt.EncryptionPerLAAddrLo );
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 13 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,dpl_descL ),
                             0);
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 14 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,edpl_descH ),
                             pIRequest->encrypt.EncryptionPerLAAddrHi );
          }
          else if (pIRequest->dif.enableDIFPerLA) /* configure DIF */
          {
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 12 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,epl_descL ),
                             0);
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 13 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,dpl_descL ),
                             pIRequest->dif.DIFPerLAAddrLo );
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 14 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,edpl_descH ),
                             pIRequest->dif.DIFPerLAAddrHi);
          }
          else /* Not EPL or DPL  */
          {
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 12 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,epl_descL ),
                             0);
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 13 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,dpl_descL ),
                             0);
            OSSA_WRITE_LE_32(agRoot, pPayload,                      /* DWORD 14 */
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t,edpl_descH ),
                             0);
          }

          if (pIRequest->flag & AGSA_SAS_ENABLE_DIF)
          {
            bit32 UDTR1_UDTR0_UDT1_UDT0  =  0;
            bit32 UDT5_UDT4_UDT3_UDT2     = 0;
            bit32 UDTR5_UDTR4_UDTR3_UDTR2 = 0;

            SA_DBG3(("saSSPStart,DIF enableRefBlockCount ref %d enableRefBlockCount  %d enableCrc  %d enableCrcInversion %d\n",
                pIRequest->dif.flags & DIF_FLAG_BITS_UDTR_REF_BLKCOUNT ? 1 : 0,
                pIRequest->dif.flags & DIF_FLAG_BITS_UDTR_REF_BLKCOUNT ? 1 : 0,
                pIRequest->dif.flags & DIF_FLAG_BITS_CRC_VER           ? 1 : 0,
                pIRequest->dif.flags & DIF_FLAG_BITS_CRC_INV           ? 1 : 0  ));

            SA_DBG3(("saSSPStart,DIF initialIOSeed %X lbSize %X difAction %X\n",
                pIRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED ? 1 : 0,
                (pIRequest->dif.flags & DIF_FLAG_BITS_BLOCKSIZE_MASK) >> DIF_FLAG_BITS_BLOCKSIZE_SHIFT,
                pIRequest->dif.flags & DIF_FLAG_BITS_ACTION  ));

            SA_DBG3(("saSSPStart,DIF udtArray %2X %2X %2X %2X %2X %2X\n",
                pIRequest->dif.udtArray[0],
                pIRequest->dif.udtArray[1],
                pIRequest->dif.udtArray[2],
                pIRequest->dif.udtArray[3],
                pIRequest->dif.udtArray[4],
                pIRequest->dif.udtArray[5]));

            SA_DBG3(("saSSPStart,DIF udrtArray %2X %2X %2X %2X %2X %2X\n",
                pIRequest->dif.udrtArray[0],
                pIRequest->dif.udrtArray[1],
                pIRequest->dif.udrtArray[2],
                pIRequest->dif.udrtArray[3],
                pIRequest->dif.udrtArray[4],
                pIRequest->dif.udrtArray[5]));

            SA_DBG3(("saSSPStart,DIF tagUpdateMask %X tagVerifyMask %X DIFPerLAAddrLo %X DIFPerLAAddrHi %X\n",
                (pIRequest->dif.flags & DIF_FLAG_BITS_UDTVMASK) >> DIF_FLAG_BITS_UDTV_SHIFT,
                (pIRequest->dif.flags & DIF_FLAG_BITS_UDTUPMASK) >> DIF_FLAG_BITS_UDTUPSHIFT,
                pIRequest->dif.DIFPerLAAddrLo,
                pIRequest->dif.DIFPerLAAddrHi));

            DirDW4 |= AGSA_DIF_BIT;

            /* DWORD 15 */
            SA_DBG3(("saSSPStart, DW 15 DIF_flags 0x%08X\n", pIRequest->dif.flags ));

            OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, DIF_flags),
                               pIRequest->dif.flags);

            /* Populate the UDT and UDTR bytes as necessary. */
            if ((pIRequest->dif.flags & DIF_FLAG_BITS_ACTION) != AGSA_DIF_INSERT)
            {
                UDTR1_UDTR0_UDT1_UDT0 = (pIRequest->dif.udtArray[1] << SHIFT8 |
                                         pIRequest->dif.udtArray[0]);
                UDT5_UDT4_UDT3_UDT2   = (pIRequest->dif.udtArray[5] << SHIFT24 |
                                         pIRequest->dif.udtArray[4] << SHIFT16 |
                                         pIRequest->dif.udtArray[3] << SHIFT8  |
                                         pIRequest->dif.udtArray[2]);
            }

            if ((pIRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_INSERT ||
                (pIRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_VERIFY_REPLACE ||
                (pIRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_REPLACE_UDT_REPLACE_CRC)
            {
                UDTR1_UDTR0_UDT1_UDT0 |= (pIRequest->dif.udrtArray[1] << SHIFT24 |
                                          pIRequest->dif.udrtArray[0] << SHIFT16 );
                UDTR5_UDTR4_UDTR3_UDTR2 = (pIRequest->dif.udrtArray[5] << SHIFT24 |
                                           pIRequest->dif.udrtArray[4] << SHIFT16 |
                                           pIRequest->dif.udrtArray[3] << SHIFT8  |
                                           pIRequest->dif.udrtArray[2]);
            }

            /* DWORD 16 is UDT3, UDT2, UDT1 and UDT0 */
            OSSA_WRITE_LE_32(agRoot, pPayload,
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, udt),
                             UDTR1_UDTR0_UDT1_UDT0);

            /* DWORD 17 is UDT5, UDT4, UDT3 and UDT2 */
            OSSA_WRITE_LE_32(agRoot, pPayload,
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, udtReplacementLo),
                             UDT5_UDT4_UDT3_UDT2);

            /* DWORD 18 is UDTR5, UDTR4, UDTR3 and UDTR2 */
            OSSA_WRITE_LE_32(agRoot, pPayload,
                             OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, udtReplacementHi),
                             UDTR5_UDTR4_UDTR3_UDTR2);

            /* DWORD 19 */
            /* Get IOS IOSeed enable bit */
            if( pIRequest->dif.enableDIFPerLA ||
               (pIRequest->dif.flags & DIF_FLAG_BITS_CUST_APP_TAG) )
            {
                OSSA_WRITE_LE_32(agRoot, pPayload,
                                 OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, DIF_seed),
                                ((pIRequest->dif.DIFPerLARegion0SecCount << SHIFT16) |
                                 (pIRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED ? pIRequest->dif.initialIOSeed : 0 )));
            }
            else
            {
              if (pIRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED)
              {
                OSSA_WRITE_LE_32(agRoot, pPayload,
                                 OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, DIF_seed),
                                 pIRequest->dif.initialIOSeed );
              }
              else
              {
                OSSA_WRITE_LE_32(agRoot, pPayload,
                                 OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, DIF_seed),  0 );
              }
            }
          }

          /* configure encryption */
          if (pIRequest->flag & AGSA_SAS_ENABLE_ENCRYPTION)
          {

            SA_DBG3(("saSSPStart,ENC dekTable 0x%08X dekIndex 0x%08X\n",
                pIRequest->encrypt.dekInfo.dekTable,
                pIRequest->encrypt.dekInfo.dekIndex));

            SA_DBG3(("saSSPStart,ENC kekIndex 0x%08X sectorSizeIndex 0x%08X cipherMode 0x%08X\n",
                pIRequest->encrypt.kekIndex,
                pIRequest->encrypt.sectorSizeIndex,
                pIRequest->encrypt.cipherMode));

            SA_DBG3(("saSSPStart,ENC keyTag_W0 0x%08X keyTag_W1 0x%08X\n",
                pIRequest->encrypt.keyTag_W0,
                pIRequest->encrypt.keyTag_W1));
            SA_DBG3(("saSSPStart,ENC tweakVal_W0 0x%08X tweakVal_W1 0x%08X\n",
                pIRequest->encrypt.tweakVal_W0,
                pIRequest->encrypt.tweakVal_W1));
            SA_DBG3(("saSSPStart,ENC tweakVal_W2 0x%08X tweakVal_W3 0x%08X\n",
                pIRequest->encrypt.tweakVal_W2,
                pIRequest->encrypt.tweakVal_W3));

              DirDW4 |= AGSA_ENCRYPT_BIT;

              encryptFlags = 0;

              if (pIRequest->encrypt.keyTagCheck == agTRUE)
              {
                 encryptFlags |= AGSA_ENCRYPT_KEY_TAG_BIT;
              }

              if( pIRequest->encrypt.cipherMode == agsaEncryptCipherModeXTS )
              {
                encryptFlags |= AGSA_ENCRYPT_XTS_Mode << SHIFT4;
              }

              encryptFlags |= pIRequest->encrypt.dekInfo.dekTable << SHIFT2;

              /* Always use encryption for DIF fields, skip SKPD */

              encryptFlags |= (pIRequest->encrypt.dekInfo.dekIndex & 0xFFFFFF) << SHIFT8;
              /* Configure DWORD 20 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, encryptFlagsLo),
                               encryptFlags);

              encryptFlags = pIRequest->encrypt.sectorSizeIndex;

              encryptFlags |= (pIRequest->encrypt.kekIndex) << SHIFT5;

              encryptFlags |= (pIRequest->encrypt.EncryptionPerLRegion0SecCount) << SHIFT16;
              /* Configure DWORD 21 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, encryptFlagsHi),
                               encryptFlags);

              /* Configure DWORD 22 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, keyTag_W0),
                               pIRequest->encrypt.keyTag_W0);
              /* Configure DWORD 23 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, keyTag_W1),
                               pIRequest->encrypt.keyTag_W1);

              /* Configure DWORD 24 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, tweakVal_W0),
                               pIRequest->encrypt.tweakVal_W0);
              /* Configure DWORD 25 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, tweakVal_W1),
                               pIRequest->encrypt.tweakVal_W1);
              /* Configure DWORD 26 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, tweakVal_W2),
                               pIRequest->encrypt.tweakVal_W2);
              /* Configure DWORD 27 */
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPIniEncryptIOStartCmd_t, tweakVal_W3),
                               pIRequest->encrypt.tweakVal_W3);
          }

          /* Setup SGL */
          if (pIRequest->dataLength)
          {
            pSgl = &(pIRequest->agSgl);

            SA_DBG3(("saSSPStart:opCode %X agSgl %08x:%08x (%x/%x)\n",opCode,
                pSgl->sgUpper, pSgl->sgLower, pSgl->len, pSgl->extReserved));

            /* Get DIF PER LA flag */
            DirDW4 |= (pIRequest->dif.enableDIFPerLA ? (1 << SHIFT7) : 0);
            DirDW4 |= (pIRequest->encrypt.enableEncryptionPerLA ? ( 1 << SHIFT12 ) : 0);
            /* Configure DWORD 4 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, dirMTlr), DirDW4);

            if (opCode == OPC_INB_SSP_DIF_ENC_OPSTART)
            {
              /* Configure DWORD 28 */
              pEncryptPayload->AddrLow0 = pSgl->sgLower;
              /* Configure DWORD 29 */
              pEncryptPayload->AddrHi0 = pSgl->sgUpper;
              /* Configure DWORD 30 */
              pEncryptPayload->Len0 = pSgl->len;
              /* Configure DWORD 31 */
              pEncryptPayload->E0 = pSgl->extReserved;
            }
            else
            {
              pPayload->AddrLow0 = pSgl->sgLower;
              pPayload->AddrHi0 = pSgl->sgUpper;
              pPayload->Len0 = pSgl->len;
              pPayload->E0 = pSgl->extReserved;
            }
          }
          else
          {
            /* no data transfer */
            /* Configure DWORD 4 */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniIOStartCmd_t, dirMTlr), DirDW4);

            if (opCode == OPC_INB_SSP_DIF_ENC_OPSTART)
            {
                  pEncryptPayload = (agsaSSPIniEncryptIOStartCmd_t *) pPayload;

                  pEncryptPayload->AddrLow0 = 0;
                  pEncryptPayload->AddrHi0 = 0;
                  pEncryptPayload->Len0 = 0;
                  pEncryptPayload->E0 = 0;
            }
            else
            {
                pPayload->AddrLow0 = 0;
                pPayload->AddrHi0 = 0;
                pPayload->Len0 = 0;
                pPayload->E0 = 0;
            }
          }

          /* post the IOMB to SPC */
#ifdef LOOPBACK_MPI
          if (loopback)
            ret = mpiMsgProduceOQ(circularOQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_OUB_SSP_COMP, outq, (bit8)circularQ->priority);
          else
#endif /* LOOPBACK_MPI */
          ret = mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, opCode, outq, (bit8)circularQ->priority);
          if (AGSA_RC_FAILURE == ret)
          {
            SA_DBG1(("saSSPStart, error when post SSP IOMB\n"));
            ret = AGSA_RC_FAILURE;
          }
        }
        else
        {
          /* additionalCdbLen is not zero and type is Ext - use EXT mode */
          agsaSSPInitiatorRequestExt_t *pIRequest = &(agRequestBody->sspInitiatorReqExt);
          agsaSSPIniExtIOStartCmd_t *pPayload = (agsaSSPIniExtIOStartCmd_t *)pMessage;
          bit32 sspiul;

          /*
           * Most fields for the SAS IOMB have the same offset regardless of the actual IOMB used.
           * Be careful with the scatter/gather lists, encryption and DIF options.
           */
          /* CDB > 16 bytes */
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, tag), pRequest->HTag);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, deviceId), pDevice->DeviceMapIndex);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, dataLen), pIRequest->dataLength);
          /* dword (bit7-bit2) ==> bytes (bit7-bit0) */
          /* setup standard CDB bytes + additional CDB bytes in length field */
          sspiul = sizeof(agsaSSPCmdInfoUnit_t) +
                    (pIRequest->sspCmdIUExt.additionalCdbLen & 0xFC);
          DirDW4 = sspiul << 16;
          si_memcpy(&pPayload->SSPIu[0], &pIRequest->sspCmdIUExt, sspiul);
          pPayload->SSPIuLendirMTlr = 0;

          /* Mask DIR for Read/Write command */
          DirDW4 |= agRequestType & AGSA_DIR_MASK;

          /* set TLR */
          DirDW4 |= pIRequest->flag & TLR_MASK;
          if (agRequestType & AGSA_MSG)
          {
            /* set M bit */
            DirDW4 |= AGSA_MSG_BIT;
          }

          /* check for skipmask operation */
          if (pIRequest->flag & AGSA_SAS_ENABLE_SKIP_MASK)
          {
            SA_ASSERT(0, "Mode not supported");
          }

          /* configure DIF */
          if (pIRequest->flag & AGSA_SAS_ENABLE_DIF)
          {
            SA_ASSERT(0, "Mode not supported");
          }

          /* configure encryption */
          if (pIRequest->flag & AGSA_SAS_ENABLE_ENCRYPTION)
          {
            SA_ASSERT(0, "Mode not supported");
          }
          /* Setup SGL */
          if (pIRequest->dataLength)
          {
            pSgl = &(pIRequest->agSgl);

            SA_DBG3(("saSSPStart: Ext mode, agSgl %08x:%08x (%x/%x)\n",
              pSgl->sgUpper, pSgl->sgLower, pSgl->len, pSgl->extReserved));

            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, SSPIuLendirMTlr), DirDW4);

             if (opCode == OPC_INB_SSP_DIF_ENC_OPSTART)
            {
                si_memcpy((&((agsaSSPIniEncryptIOStartCmd_t *)(pPayload))->AddrLow0), pSgl, sizeof(agsaSgl_t));
            }
            else
            {
                si_memcpy((&(pPayload->SSPIu[0]) + sspiul), pSgl, sizeof(agsaSgl_t));
            }
          }
          else
          {
            /* no data transfer */
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniExtIOStartCmd_t, SSPIuLendirMTlr), DirDW4);
            pPayload->dataLen = 0;
          }

          /* post the IOMB to SPC */
          if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, opCode, outq,(bit8)circularQ->priority ))
          {
            SA_DBG1(("saSSPStart, error when post SSP Ext IOMB\n"));
            ret = AGSA_RC_FAILURE;
          }
        }
        break;
      }
      case AGSA_SSP_TASK_MGNT_REQ:
      case AGSA_SSP_TASK_MGNT_REQ_M:
      {
        agsaIORequestDesc_t *pTMRequestToAbort = agNULL;
        agsaSSPIniTMStartCmd_t *pPayload = (agsaSSPIniTMStartCmd_t *)pMessage;

        if (agRequestType & AGSA_MSG)
        {
          /* set M bit */
          DirDW4 = AGSA_MSG_BIT;
        }

        /* set DS and ADS bit */
        DirDW4 |= (agRequestBody->sspTaskMgntReq.tmOption & 0x3) << 3;

        /* Prepare the SSP TASK Management payload */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, tag), pRequest->HTag);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, deviceId), pDevice->DeviceMapIndex);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, relatedTag), agRequestBody->sspTaskMgntReq.tagOfTaskToBeManaged);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, TMfunction), agRequestBody->sspTaskMgntReq.taskMgntFunction);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, dsAdsMReport), DirDW4);
        pPayload->lun[0] = agRequestBody->sspTaskMgntReq.lun[0];
        pPayload->lun[1] = agRequestBody->sspTaskMgntReq.lun[1];
        pPayload->lun[2] = agRequestBody->sspTaskMgntReq.lun[2];
        pPayload->lun[3] = agRequestBody->sspTaskMgntReq.lun[3];
        pPayload->lun[4] = agRequestBody->sspTaskMgntReq.lun[4];
        pPayload->lun[5] = agRequestBody->sspTaskMgntReq.lun[5];
        pPayload->lun[6] = agRequestBody->sspTaskMgntReq.lun[6];
        pPayload->lun[7] = agRequestBody->sspTaskMgntReq.lun[7];

        if (agTMRequest)
        {
          pTMRequestToAbort = (agsaIORequestDesc_t *)agTMRequest->sdkData;
          if (pTMRequestToAbort)
          {
            OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPIniTMStartCmd_t, relatedTag), pTMRequestToAbort->HTag);
          }
        }

        SA_DBG1(("saSSPStart, HTAG 0x%x TM function 0x%x Tag-to-be-aborted 0x%x deviceId 0x%x\n",
                  pPayload->tag, pPayload->TMfunction, pPayload->relatedTag, pPayload->deviceId));

        siDumpActiveIORequests(agRoot, saRoot->swConfig.maxActiveIOs);

        /* post the IOMB to SPC */
        if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_SSPINITMSTART, outq, (bit8)circularQ->priority))
        {
          SA_DBG1(("saSSPStart, error when post TM IOMB\n"));
          ret = AGSA_RC_FAILURE;
        }

        break;
      }
      case AGSA_SSP_TGT_READ_DATA:
      case AGSA_SSP_TGT_READ_GOOD_RESP:
      case AGSA_SSP_TGT_WRITE_DATA:
      case AGSA_SSP_TGT_WRITE_GOOD_RESP:
      {
        agsaSSPTargetRequest_t *pTRequest = &(agRequestBody->sspTargetReq);
        agsaSSPTgtIOStartCmd_t *pPayload = (agsaSSPTgtIOStartCmd_t *)pMessage;
        bit32 DirDW5 = 0;
        /* Prepare the SSP TGT IO Start payload */
        /* Configure DWORD 1 */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, tag), pRequest->HTag);
        /* Configure DWORD 2 */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, deviceId), pDevice->DeviceMapIndex);
        /* Configure DWORD 3 */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, dataLen), pTRequest->dataLength);
        /* Configure DWORD 4 */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, dataOffset), pTRequest->offset);

        SA_DBG3(("saSSPStart, sspOption %08X\n", pTRequest->sspOption ));

        /* Mask DIR and AutoGR bits for Read/Write command */
        DirDW5 = (agRequestType & (AGSA_DIR_MASK | AGSA_AUTO_MASK)) | (pTRequest->agTag << 16);

        if (pTRequest->sspOption & SSP_OPTION_DIF )
        {
          bit32 UDTR1_UDTR0_UDT1_UDT0   = 0;
          bit32 UDT5_UDT4_UDT3_UDT2     = 0;
          bit32 UDTR5_UDTR4_UDTR3_UDTR2 = 0;
          SA_DBG3(("saSSPStart,tgt DIF enableRefBlockCount ref %d enableRefBlockCount  %d enableCrc  %d enableCrcInversion %d\n",
              pTRequest->dif.flags & DIF_FLAG_BITS_UDTR_REF_BLKCOUNT ? 1 : 0,
              pTRequest->dif.flags & DIF_FLAG_BITS_UDTR_REF_BLKCOUNT ? 1 : 0,
              pTRequest->dif.flags & DIF_FLAG_BITS_CRC_VER           ? 1 : 0,
              pTRequest->dif.flags & DIF_FLAG_BITS_CRC_INV           ? 1 : 0  ));

          SA_DBG3(("saSSPStart,tgt DIF initialIOSeed %X lbSize %X difAction %X\n",
              pTRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED ? 1 : 0,
              (pTRequest->dif.flags & DIF_FLAG_BITS_BLOCKSIZE_MASK ) >> DIF_FLAG_BITS_BLOCKSIZE_SHIFT,
              pTRequest->dif.flags & DIF_FLAG_BITS_ACTION  ));

          SA_DBG3(("saSSPStart,tgt DIF udtArray %2X %2X %2X %2X %2X %2X\n",
              pTRequest->dif.udtArray[0],
              pTRequest->dif.udtArray[1],
              pTRequest->dif.udtArray[2],
              pTRequest->dif.udtArray[3],
              pTRequest->dif.udtArray[4],
              pTRequest->dif.udtArray[5]));

          SA_DBG3(("saSSPStart,tgt DIF udrtArray %2X %2X %2X %2X %2X %2X\n",
              pTRequest->dif.udrtArray[0],
              pTRequest->dif.udrtArray[1],
              pTRequest->dif.udrtArray[2],
              pTRequest->dif.udrtArray[3],
              pTRequest->dif.udrtArray[4],
              pTRequest->dif.udrtArray[5]));

          SA_DBG3(("saSSPStart,tgt DIF tagUpdateMask %X tagVerifyMask %X DIFPerLAAddrLo %X DIFPerLAAddrHi %X\n",
              (pTRequest->dif.flags & DIF_FLAG_BITS_UDTVMASK) >> DIF_FLAG_BITS_UDTV_SHIFT,
              (pTRequest->dif.flags & DIF_FLAG_BITS_UDTUPMASK) >> DIF_FLAG_BITS_UDTUPSHIFT,
              pTRequest->dif.DIFPerLAAddrLo,
              pTRequest->dif.DIFPerLAAddrHi));

          DirDW5 |= AGSA_SSP_TGT_BITS_DEE_DIF;


          SA_DBG3(("saSSPStart,tgt  DW 15 DIF_flags 0x%08X\n", pTRequest->dif.flags ));

          OSSA_WRITE_LE_32(agRoot, pPayload,
                             OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, DIF_flags),
                             pTRequest->dif.flags);

            /* Populate the UDT and UDTR bytes as necessary. */
            if ((pTRequest->dif.flags & DIF_FLAG_BITS_ACTION) != AGSA_DIF_INSERT)
            {
                UDTR1_UDTR0_UDT1_UDT0 = (pTRequest->dif.udtArray[1] << SHIFT8 |
                                         pTRequest->dif.udtArray[0]);
                UDT5_UDT4_UDT3_UDT2   = (pTRequest->dif.udtArray[5] << SHIFT24 |
                                         pTRequest->dif.udtArray[4] << SHIFT16 |
                                         pTRequest->dif.udtArray[3] << SHIFT8  |
                                         pTRequest->dif.udtArray[2]);
            }

            if ((pTRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_INSERT ||
                (pTRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_VERIFY_REPLACE ||
                (pTRequest->dif.flags & DIF_FLAG_BITS_ACTION) == AGSA_DIF_REPLACE_UDT_REPLACE_CRC)
            {
                UDTR1_UDTR0_UDT1_UDT0 |= (pTRequest->dif.udrtArray[1] << SHIFT24 |
                                          pTRequest->dif.udrtArray[0] << SHIFT16 );
                UDTR5_UDTR4_UDTR3_UDTR2 = (pTRequest->dif.udrtArray[5] << SHIFT24 |
                                           pTRequest->dif.udrtArray[4] << SHIFT16 |
                                           pTRequest->dif.udrtArray[3] << SHIFT8  |
                                           pTRequest->dif.udrtArray[2]);
            }
          /* DWORD 8 is UDTR1, UDTR0, UDT1 and UDT0 */
          OSSA_WRITE_LE_32(agRoot, pPayload,
                           OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, udt),
                           UDTR1_UDTR0_UDT1_UDT0);

          /* DWORD 9 is UDT5, UDT4, UDT3 and UDT2 */
          OSSA_WRITE_LE_32(agRoot, pPayload,
                           OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, udtReplacementLo),
                           UDT5_UDT4_UDT3_UDT2);

          /* DWORD 10 is UDTR5, UDTR4, UDTR3 and UDTR2 */
          OSSA_WRITE_LE_32(agRoot, pPayload,
                           OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, udtReplacementHi),
                           UDTR5_UDTR4_UDTR3_UDTR2);
          /* DWORD 11 */
          /* Get IOS IOSeed enable bit */
          if( pTRequest->dif.flags & DIF_FLAG_BITS_CUST_APP_TAG)
          {
              OSSA_WRITE_LE_32(agRoot, pPayload,
                               OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, DIF_seed),
                               ((pTRequest->dif.DIFPerLARegion0SecCount << SHIFT16) |
                               (pTRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED ? pTRequest->dif.initialIOSeed : 0 )));
          }
          else
          {
              /* Get IOS IOSeed enable bit */
              if (pTRequest->dif.flags & DIF_FLAG_BITS_CRC_SEED)
              {
                  OSSA_WRITE_LE_32(agRoot, pPayload,
                                   OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, DIF_seed),
                                   pTRequest->dif.initialIOSeed );
              }
              else
              {
                  OSSA_WRITE_LE_32(agRoot, pPayload,
                                   OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, DIF_seed),  0 );
              }
          }
        }

        /* Mask DIR and AutoGR bits for Read/Write command */
        if(pTRequest->sspOption & SSP_OPTION_AUTO_GOOD_RESPONSE)
        {
          DirDW5 |= AGSA_SSP_TGT_BITS_AGR;
        }

        /* AN, RTE, RDF bits */
        DirDW5 |= (pTRequest->sspOption & SSP_OPTION_BITS) << 2;

        /* ODS */
        if(pTRequest->sspOption & SSP_OPTION_ODS)
        {
          DirDW5 |= AGSA_SSP_TGT_BITS_ODS;
        }

        /* Setup SGL */
        if (pTRequest->dataLength)
        {
          pSgl = &(pTRequest->agSgl);

          SA_DBG5(("saSSPStart: agSgl %08x:%08x (%x/%x)\n",
          pSgl->sgUpper, pSgl->sgLower, pSgl->len, pSgl->extReserved));

          /* set up dir on the payload */
          /* Configure DWORD 5 */
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, INITagAgrDir), DirDW5);

          pPayload->AddrLow0 = pSgl->sgLower;
          pPayload->AddrHi0 = pSgl->sgUpper;
          pPayload->Len0 = pSgl->len;
          pPayload->E0 = pSgl->extReserved;
        }
        else
        {
          /* no data transfer */
          /* Configure DWORD 5 */
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t, INITagAgrDir), DirDW5);
          pPayload->AddrLow0 = 0;
          pPayload->AddrHi0 = 0;
          pPayload->Len0 = 0;
        }
        /* Configure DWORD 6 */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtIOStartCmd_t,reserved ), 0);

        /* Build TGT IO START command and send it to SPC */
        if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_SSPTGTIOSTART, outq, (bit8)circularQ->priority))
        {
          SA_DBG1(("saSSPStart, error when post TGT IOMB\n"));
          ret = AGSA_RC_FAILURE;
        }

        break;
      }
      case AGSA_SSP_TGT_CMD_OR_TASK_RSP:
      {
        agsaSSPTargetResponse_t *pTResponse = &(agRequestBody->sspTargetResponse);
        agsaSSPTgtRspStartCmd_t *pPayload = (agsaSSPTgtRspStartCmd_t *)pMessage;
        bit32 ip, an, ods;

        if (pTResponse->frameBuf && (pTResponse->respBufLength <= AGSA_MAX_SSPPAYLOAD_VIA_SFO))
        {
          ip = 1;
          si_memcpy(pPayload->reserved, pTResponse->frameBuf, pTResponse->respBufLength);
        }
        else
        {
          ip = 0;
          /* NOTE:
           * 1. reserved field must be ZEROED out. FW depends on it
           * 2. trusted interface. indirect response buffer must be valid.
           */
          si_memset(pPayload->reserved, 0, sizeof(pPayload->reserved));
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, AddrLow0), pTResponse->respBufLower);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, AddrHi0), pTResponse->respBufUpper);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, Len0), pTResponse->respBufLength);
          OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, E0), 0);
        }

        /* TLR setting */
        an = (pTResponse->respOption & RESP_OPTION_BITS);
        /* ODS */
        ods = (pTResponse->respOption & RESP_OPTION_ODS);

        /* Prepare the SSP TGT RESPONSE Start payload */
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, tag), pRequest->HTag);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, deviceId), pDevice->DeviceMapIndex);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, RspLen), pTResponse->respBufLength);
        OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaSSPTgtRspStartCmd_t, INITag_IP_AN),
          (pTResponse->agTag << SHIFT16) | ods | (ip << SHIFT10) | (an << SHIFT2));

        /* Build TGT RESPONSE START command and send it to SPC */
        if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_SSPTGTRSPSTART, outq, (bit8)circularQ->priority))
        {
          SA_DBG1(("saSSPStart, error when post TGT RSP IOMB\n"));
          ret = AGSA_RC_FAILURE;
        }

        break;
      }
      default:
      {
        SA_DBG1(("saSSPStart, Unsupported Request IOMB\n"));
        ret = AGSA_RC_FAILURE;
        break;
      }
    }

  } /* LL IOrequest available */

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

#ifdef SALL_API_TEST
  if (ret == AGSA_RC_SUCCESS)
    saRoot->LLCounters.IOCounter.numSSPStarted++;
#endif /*SALL_API_TEST  */

#ifdef LOOPBACK_MPI
  if (loopback)
    saRoot->interruptVecIndexBitMap[0] |= (1 << outq);
#endif /* LOOPBACK_MPI */
  /* goto have leave and trace point info */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "Sa");
ext:

  OSSA_INP_LEAVE(agRoot);
  return ret;
}

/******************************************************************************/
/*! \brief Abort SSP request
 *
 *  Abort SSP request
 *
 *  \param agRoot handles for this instance of SAS/SATA LLL
 *  \param queueNum
 *  \param agIORequest
 *  \param agIOToBeAborted
 *
 *  \return If request is aborted successfully
 *          - \e AGSA_RC_SUCCESS request is aborted successfully
 *          - \e AGSA_RC_FAILURE request is not aborted successfully
 */
/*******************************************************************************/
GLOBAL bit32 saSSPAbort(
  agsaRoot_t        *agRoot,
  agsaIORequest_t   *agIORequest,
  bit32             queueNum,
  agsaDevHandle_t   *agDevHandle,
  bit32             flag,
  void              *abortParam,
  ossaGenericAbortCB_t  agCB
  )
{
  bit32 ret = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaIORequestDesc_t *pRequestABT = NULL;
  agsaDeviceDesc_t    *pDevice = NULL;
  agsaDeviceDesc_t    *pDeviceABT = NULL;
  agsaPort_t          *pPort = NULL;
  mpiICQueue_t        *circularQ;
  void                *pMessage;
  agsaSSPAbortCmd_t   *payload;
  agsaIORequest_t     *agIOToBeAborted;
  bit8                inq, outq;
  bit32               using_reserved = agFALSE;
  bit32               flag_copy = flag;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"Sb");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  SA_ASSERT((agNULL != agIORequest), "");

  SA_DBG2(("saSSPAbort: agIORequest %p agDevHandle %p abortParam %p flag 0x%x\n", agIORequest,agDevHandle,abortParam,flag));

  /* Assign inbound and outbound Buffer */
  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

#ifdef SA_PRINTOUT_IN_WINDBG
#ifndef DBG
        DbgPrint("saSSPAbort flag %d\n", flag );
#endif /* DBG  */
#endif /* SA_PRINTOUT_IN_WINDBG  */

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    agIOToBeAborted = (agsaIORequest_t *)abortParam;
    /* Get LL IORequest entry for saSSPAbort() */
    pRequest = (agsaIORequestDesc_t *) (agIOToBeAborted->sdkData);
    if (agNULL == pRequest)
    {
      /* no pRequest found - can not Abort */
      SA_DBG1(("saSSPAbort: ABORT_ALL no pRequest\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "Sb");
      return AGSA_RC_FAILURE;
    }
    /* Find the device the request sent to */
    pDevice = pRequest->pDevice;
    /* Get LL IORequest entry for IOToBeAborted */
    pRequestABT = (agsaIORequestDesc_t *) (agIOToBeAborted->sdkData);
    if (agNULL == pRequestABT)
    {
      /* The IO to Be Abort is no longer exist */
      SA_DBG1(("saSSPAbort: ABORT_ALL no pRequestABT\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "Sb");
      return AGSA_RC_FAILURE;
    }
    /* Find the device the request Abort to */
    pDeviceABT = pRequestABT->pDevice;

    if (agNULL == pDeviceABT)
    {
      /* no deviceID - can not build IOMB */
      SA_DBG1(("saSSPAbort: ABORT_ALL no pRequestABT->deviceID\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "Sb");
      return AGSA_RC_FAILURE;
    }

    if (agNULL != pDevice)
    {
      /* Find the port the request was sent to */
      pPort = pDevice->pPort;
    }
    else
    {
      /* no deviceID - can not build IOMB */
      SA_DBG1(("saSSPAbort: ABORT_ALL no deviceID\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "Sb");
      return AGSA_RC_FAILURE;
    }

    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/
  }
  else
  {
    if (ABORT_ALL == (flag & ABORT_MASK))
    {
      /* abort All with Device or Port */
      /* Find the outgoing port for the device */
      if (agDevHandle == agNULL)
      {
        /* no deviceID - can not build IOMB */
        SA_DBG1(("saSSPAbort: agDevHandle == agNULL!!!\n"));
        return AGSA_RC_FAILURE;
      }
      pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
      if (agNULL == pDevice)
      {
        /* no deviceID - can not build IOMB */
        SA_DBG1(("saSSPAbort: ABORT_ALL agNULL == pDevice\n"));
        return AGSA_RC_FAILURE;
      }
      pPort = pDevice->pPort;
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests)); /**/
    }
    else
    {
      /* only support 00, 01 and 02 for flag */
      SA_DBG1(("saSSPAbort: ABORT_ALL type not supported 0x%X\n",flag));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "Sb");
      return AGSA_RC_FAILURE;
    }
  }

  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests));
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG2(("saSSPAbort: using saRoot->freeReservedRequests\n"));
    }
    else
    {
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* If no LL IO request entry available */
      SA_DBG1(("saSSPAbort: No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "Sb");
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

  /* Add the request to the pendingIORequests list of the device */
  pRequest->valid = agTRUE;
  saLlistIOAdd(&(pDevice->pendingIORequests), &(pRequest->linkNode));

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* set up pRequest */
  pRequest->pIORequestContext = agIORequest;
  pRequest->requestType = AGSA_SSP_REQTYPE;
  pRequest->pDevice = pDevice;
  pRequest->pPort = pPort;
  pRequest->completionCB = (void*)agCB;
/*  pRequest->abortCompletionCB = agCB;*/
  pRequest->startTick = saRoot->timeTick;

  /* Set request to the sdkData of agIORequest */
  agIORequest->sdkData = pRequest;

  /* save tag and IOrequest pointer to IOMap */
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;


#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

  /* If LL IO request entry avaliable */
  /* Get a free inbound queue entry */
  circularQ = &saRoot->inboundQueue[inq];
  retVal    = mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);

  /* if message size is too large return failure */
  if (AGSA_RC_FAILURE == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("saSSPAbort: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSSPAbort: error when get free IOMB\n"));

    smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "Sb");
    return AGSA_RC_FAILURE;
  }

  /* return busy if inbound queue is full */
  if (AGSA_RC_BUSY == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    saLlistIORemove(&(pDevice->pendingIORequests), &(pRequest->linkNode));
    pRequest->valid = agFALSE;
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("saSSPAbort: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saSSPAbort: no more IOMB\n"));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "Sb");
    return AGSA_RC_BUSY;
  }

  /* setup payload */
  payload = (agsaSSPAbortCmd_t*)pMessage;
  OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, tag), pRequest->HTag);

  if( ABORT_SINGLE == (flag & ABORT_MASK) )
  {
    if ( agNULL == pDeviceABT )
    {
      SA_DBG1(("saSSPSAbort: no device\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "Sb");
      return AGSA_RC_FAILURE;
    }
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, deviceId), pDeviceABT->DeviceMapIndex);
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, HTagAbort), pRequestABT->HTag);
  }
  else
  {
    /* abort all */
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, deviceId), pDevice->DeviceMapIndex);
    OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, HTagAbort), 0);
  }

  if(flag & ABORT_TSDK_QUARANTINE)
  {
    if(smIS_SPCV(agRoot))
    {
      flag_copy &= ABORT_SCOPE;
      flag_copy |= ABORT_QUARANTINE_SPCV;
    }
  }
  OSSA_WRITE_LE_32(agRoot, payload, OSSA_OFFSET_OF(agsaSSPAbortCmd_t, abortAll), flag_copy);

  SA_DBG1(("saSSPAbort: HTag 0x%x HTagABT 0x%x deviceId 0x%x flag 0x%x\n", payload->tag, payload->HTagAbort, payload->deviceId,flag));

  siCountActiveIORequestsOnDevice( agRoot,   payload->deviceId );

  /* post the IOMB to SPC */
  ret = mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_SSP_ABORT, outq, (bit8)circularQ->priority);

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

#ifdef SALL_API_TEST
  if (AGSA_RC_SUCCESS == ret)
  {
    saRoot->LLCounters.IOCounter.numSSPAborted++;
  }
#endif

  smTraceFuncExit(hpDBG_VERY_LOUD, 'j', "Sb");

  return ret;
}


#if defined(SALLSDK_DEBUG)
/******************************************************************************/
/*! \brief
 *
 *  Dump StartSSP information
 *
 *  Debug helper routine
 *
 *  \return -none -
 */
/*******************************************************************************/
LOCAL void siDumpSSPStartIu(
  agsaDevHandle_t       *agDevHandle,
  bit32                 agRequestType,
  agsaSASRequestBody_t  *agRequestBody
  )
 {
  switch ( agRequestType )
  {
    case AGSA_SSP_INIT_READ:
    case AGSA_SSP_INIT_WRITE:
    {
      agsaSSPInitiatorRequest_t *pIRequest = &(agRequestBody->sspInitiatorReq);

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - len=%x - attr=%x - CDB:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
        agDevHandle,
        (agRequestType==AGSA_SSP_INIT_READ)? "AGSA_SSP_INIT_READ" : "AGSA_SSP_INIT_WRITE",
        pIRequest->dataLength,
        pIRequest->sspCmdIU.efb_tp_taskAttribute,
        pIRequest->sspCmdIU.cdb[0],
        pIRequest->sspCmdIU.cdb[1],
        pIRequest->sspCmdIU.cdb[2],
        pIRequest->sspCmdIU.cdb[3],
        pIRequest->sspCmdIU.cdb[4],
        pIRequest->sspCmdIU.cdb[5],
        pIRequest->sspCmdIU.cdb[6],
        pIRequest->sspCmdIU.cdb[7],
        pIRequest->sspCmdIU.cdb[8],
        pIRequest->sspCmdIU.cdb[9]
        ));
      break;
    }

    case  AGSA_SSP_INIT_READ_EXT:
    case  AGSA_SSP_INIT_WRITE_EXT:
    {
      agsaSSPInitiatorRequestExt_t *pIRequest = &(agRequestBody->sspInitiatorReqExt);

      SA_DBG3(("siDumpSSPStartIu: dev=%p - %s - len=%x - attr=%x - CDB:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
        agDevHandle,
        (agRequestType==AGSA_SSP_INIT_READ_EXT)? "AGSA_SSP_INIT_READ_EXT" : "AGSA_SSP_INIT_WRITE_EXT",
        pIRequest->dataLength,
        pIRequest->sspCmdIUExt.efb_tp_taskAttribute,
        pIRequest->sspCmdIUExt.cdb[0],
        pIRequest->sspCmdIUExt.cdb[1],
        pIRequest->sspCmdIUExt.cdb[2],
        pIRequest->sspCmdIUExt.cdb[3],
        pIRequest->sspCmdIUExt.cdb[4],
        pIRequest->sspCmdIUExt.cdb[5],
        pIRequest->sspCmdIUExt.cdb[6],
        pIRequest->sspCmdIUExt.cdb[7],
        pIRequest->sspCmdIUExt.cdb[8],
        pIRequest->sspCmdIUExt.cdb[9]
        ));
      break;
    }

    case  AGSA_SSP_INIT_READ_EXT_M:
    case  AGSA_SSP_INIT_WRITE_EXT_M:
    {
      agsaSSPInitiatorRequestExt_t *pIRequest = &(agRequestBody->sspInitiatorReqExt);

      SA_DBG3(("siDumpSSPStartIu: dev=%p - %s - len=%x - attr=%x - CDB:%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
        agDevHandle,
        (agRequestType==AGSA_SSP_INIT_READ_EXT_M)? "AGSA_SSP_INIT_READ_EXT_M" : "AGSA_SSP_INIT_WRITE_EXT_M",
        pIRequest->dataLength,
        pIRequest->sspCmdIUExt.efb_tp_taskAttribute,
        pIRequest->sspCmdIUExt.cdb[0],
        pIRequest->sspCmdIUExt.cdb[1],
        pIRequest->sspCmdIUExt.cdb[2],
        pIRequest->sspCmdIUExt.cdb[3],
        pIRequest->sspCmdIUExt.cdb[4],
        pIRequest->sspCmdIUExt.cdb[5],
        pIRequest->sspCmdIUExt.cdb[6],
        pIRequest->sspCmdIUExt.cdb[7],
        pIRequest->sspCmdIUExt.cdb[8],
        pIRequest->sspCmdIUExt.cdb[9]
        ));
      break;
    }

    case  AGSA_SSP_INIT_READ_INDIRECT:
    case  AGSA_SSP_INIT_WRITE_INDIRECT:
    case  AGSA_SSP_INIT_READ_INDIRECT_M:
    case  AGSA_SSP_INIT_WRITE_INDIRECT_M:
    {
     agsaSSPInitiatorRequestIndirect_t *pIRequest = &(agRequestBody->sspInitiatorReqIndirect);

      SA_DBG3(("siDumpSSPStartIu: dev=%p - %s - len=%x - cdblen=%d CDB:U %08x L %08x\n",
        agDevHandle,
        (agRequestType==AGSA_SSP_INIT_READ_INDIRECT ||
         agRequestType==AGSA_SSP_INIT_READ_INDIRECT_M) ? "AGSA_SSP_INIT_READ_INDIRECT" : "AGSA_SSP_INIT_WRITE_INDIRECT",
        pIRequest->dataLength,
        pIRequest->sspInitiatorReqLen,
        pIRequest->sspInitiatorReqAddrUpper32,
        pIRequest->sspInitiatorReqAddrLower32 ));
      break;
    }


    case AGSA_SSP_TASK_MGNT_REQ:
    {
      agsaSSPScsiTaskMgntReq_t  *pTaskCmd =&agRequestBody->sspTaskMgntReq;
      /* copy payload */

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - Task Function=%x - Tag to managed=%x",
        agDevHandle,
        "AGSA_SSP_TASK_MGNT_REQ",
        pTaskCmd->taskMgntFunction,
        pTaskCmd->tagOfTaskToBeManaged
        ));
      break;
    }
    case AGSA_SSP_TGT_READ_DATA:
    {
      agsaSSPTargetRequest_t *pTRequest = &(agRequestBody->sspTargetReq);

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - dmaSize=%x dmaOffset=%x\n",
                  agDevHandle,
                  "AGSA_SSP_TGT_READ_DATA",
                  pTRequest->dataLength,
                  pTRequest->offset ));
      break;
    }
    case AGSA_SSP_TGT_READ_GOOD_RESP:
    {
      agsaSSPTargetRequest_t *pTRequest = &(agRequestBody->sspTargetReq);

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - dmaSize=%x dmaOffset=%x\n",
                  agDevHandle,
                  "AGSA_SSP_TGT_READ_GOOD_RESP",
                  pTRequest->dataLength,
                  pTRequest->offset));
      break;
    }
    case AGSA_SSP_TGT_WRITE_GOOD_RESP:
    {
      agsaSSPTargetRequest_t  *pTRequest = &(agRequestBody->sspTargetReq);
      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - dmaSize=%x dmaOffset=%x\n",
                  agDevHandle,
                  "AGSA_SSP_TGT_WRITE_GOOD_RESP",
                  pTRequest->dataLength,
                  pTRequest->offset ));

      break;
    }
    case AGSA_SSP_TGT_WRITE_DATA:
    {
      agsaSSPTargetRequest_t  *pTRequest = &(agRequestBody->sspTargetReq);

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - dmaSize=%x dmaOffset=%x\n",
        agDevHandle,
        "AGSA_SSP_TGT_WRITE_DATA",
        pTRequest->dataLength,
        pTRequest->offset ));
      break;
    }
    case AGSA_SSP_TGT_CMD_OR_TASK_RSP:
    {
      agsaSSPTargetResponse_t *pTResponse = &(agRequestBody->sspTargetResponse);

      SA_DBG5(("siDumpSSPStartIu: dev=%p - %s - len=%x PAddr=%08x:%08x  Tag=%x\n",
        agDevHandle,
        "AGSA_SSP_TGT_CMD_OR_TASK_RSP",
        pTResponse->respBufLength,
        pTResponse->respBufUpper,
        pTResponse->respBufLower,
        pTResponse->agTag  ));
      break;
    }

    default:
    {
      SA_DBG1(("siDumpSSPStartIu: dev=%p - %s %X\n",
        agDevHandle,
        "Unknown SSP cmd type",
        agRequestType
        ));
      break;
    }
  }
  return;
}
#endif /* SALLSDK_DEBUG */
