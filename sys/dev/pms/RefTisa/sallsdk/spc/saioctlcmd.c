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
/*! \file saioctlcmd.c
 *  \brief The file implements the functions of IOCTL MPI Command/Response to/from SPC
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
#define siTraceFileID 'H'
#endif

extern bit32 gFPGA_TEST;

extern bit32 gWait_3;
extern bit32 gWait_2;



LOCAL bit32 siGSMDump(
                      agsaRoot_t     *agRoot,
                      bit32          gsmDumpOffset,
                      bit32          length,
                      void           *directData);

#ifdef SPC_ENABLE_PROFILE
/******************************************************************************/
/*! \brief SPC FW Profile Command
 *
 *  This command sends FW Flash Update Command to SPC.
 *
 *  \param agRoot          Handles for this instance of SAS/SATA LL
 *  \param agContext       Context of SPC FW Flash Update Command
 *  \param queueNum        Inbound/outbound queue number
 *  \param flashUpdateInfo Pointer of flash update information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saFwProfile(
  agsaRoot_t                *agRoot,
  agsaContext_t             *agContext,
  bit32                     queueNum,
  agsaFwProfile_t         *fwProfileInfo
  )
{
  bit32 ret           = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  mpiICQueue_t        *circularQ;
  void                *pMessage;
  agsaFwProfileIOMB_t *pPayload;
  bit8                inq, outq;
  bit32               i, tcid_processor_cmd = 0;


  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry avaliable */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saFwProfile, No request from free list\n" ));
     return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Assign inbound and outbound Ring Buffer */
    inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
    outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
    SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

    /* Remove the request from free list */
    saLlistRemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
    /* Get a free inbound queue entry */
    circularQ = &saRoot->inboundQueue[inq];
    retVal    = mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);

    /* if message size is too large return failure */
    if (AGSA_RC_FAILURE == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif  /* SA_LL_IBQ_PROTECT */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("saFwProfile, error when get free IOMB\n"));
      return AGSA_RC_FAILURE;
    }

    /* return busy if inbound queue is full */
    if (AGSA_RC_BUSY == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saFwProfile, no more IOMB\n"));
      return AGSA_RC_BUSY;
    }

    pPayload = (agsaFwProfileIOMB_t *)pMessage;
    tcid_processor_cmd = (((fwProfileInfo->tcid)<< 16) | ((fwProfileInfo->processor)<< 8) | fwProfileInfo->cmd);
  /* Prepare the FW_FLASH_UPDATE IOMB payload */
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwProfileIOMB_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwProfileIOMB_t, tcid_processor_cmd), tcid_processor_cmd);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwProfileIOMB_t, codeStartAdd), fwProfileInfo->codeStartAdd);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwProfileIOMB_t, codeEndAdd), fwProfileInfo->codeEndAdd);

    pPayload->SGLAL = fwProfileInfo->agSgl.sgLower;
    pPayload->SGLAH = fwProfileInfo->agSgl.sgUpper;
    pPayload->Len = fwProfileInfo->agSgl.len;
    pPayload->extReserved = fwProfileInfo->agSgl.extReserved;

    /* fill up the reserved bytes with zero */
    for (i = 0; i < FWPROFILE_IOMB_RESERVED_LEN; i ++)
    {
      pPayload->reserved0[i] = 0;
    }

    /* post the IOMB to SPC */
    ret = mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_FW_PROFILE, outq, (bit8)circularQ->priority);

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

    if (AGSA_RC_FAILURE == ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saFwProfile, error when post FW_PROFILE IOMB\n"));
    }
  }
  return ret;
}
#endif
/******************************************************************************/
/*! \brief SPC FW Flash Update Command
 *
 *  This command sends FW Flash Update Command to SPC.
 *
 *  \param agRoot          Handles for this instance of SAS/SATA LL
 *  \param agContext       Context of SPC FW Flash Update Command
 *  \param queueNum        Inbound/outbound queue number
 *  \param flashUpdateInfo Pointer of flash update information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saFwFlashUpdate(
  agsaRoot_t                *agRoot,
  agsaContext_t             *agContext,
  bit32                     queueNum,
  agsaUpdateFwFlash_t       *flashUpdateInfo
  )
{
  bit32 ret = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t        *saRoot;
  agsaIORequestDesc_t *pRequest;
  mpiICQueue_t        *circularQ;
  void                *pMessage;
  agsaFwFlashUpdate_t *pPayload;
  bit8                inq, outq;
  bit32               i;

  SA_ASSERT((agNULL != agRoot), "");
  if (agRoot == agNULL)
  {
    SA_DBG1(("saFwFlashUpdate: agRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  if (saRoot == agNULL)
  {
    SA_DBG1(("saFwFlashUpdate: saRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }


  smTraceFuncEnter(hpDBG_VERY_LOUD, "6a");
  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));
  /* If no LL Control request entry available */
  if ( agNULL == pRequest ) {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saFwFlashUpdate, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6a");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Assign inbound and outbound Ring Buffer */
    inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
    outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
    SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
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
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saFwFlashUpdate, error when get free IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6a");
      return AGSA_RC_FAILURE;
    }
    /* return busy if inbound queue is full */
    if (AGSA_RC_BUSY == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saFwFlashUpdate, no more IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "6a");
      return AGSA_RC_BUSY;
    }
    pPayload = (agsaFwFlashUpdate_t *)pMessage;
    /* Prepare the FW_FLASH_UPDATE IOMB payload */
    OSSA_WRITE_LE_32( agRoot, pPayload,
                      OSSA_OFFSET_OF(agsaFwFlashUpdate_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32( agRoot, pPayload,
                      OSSA_OFFSET_OF(agsaFwFlashUpdate_t, curImageOffset),
                      flashUpdateInfo->currentImageOffset);
    OSSA_WRITE_LE_32( agRoot, pPayload,
                      OSSA_OFFSET_OF(agsaFwFlashUpdate_t, curImageLen),
                      flashUpdateInfo->currentImageLen);
    OSSA_WRITE_LE_32( agRoot, pPayload,
                      OSSA_OFFSET_OF(agsaFwFlashUpdate_t, totalImageLen),
                      flashUpdateInfo->totalImageLen);
    pPayload->SGLAL = flashUpdateInfo->agSgl.sgLower;
    pPayload->SGLAH = flashUpdateInfo->agSgl.sgUpper;
    pPayload->Len   = flashUpdateInfo->agSgl.len;
    pPayload->extReserved = flashUpdateInfo->agSgl.extReserved;
    /* fill up the reserved bytes with zero */
    for (i = 0; i < FWFLASH_IOMB_RESERVED_LEN; i ++) {
      pPayload->reserved0[i] = 0;
    }
    /* post the IOMB to SPC */
    ret = mpiMsgProduce( circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA,
                         OPC_INB_FW_FLASH_UPDATE, outq, (bit8)circularQ->priority);
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave( agRoot, LL_IOREQ_IBQ0_LOCK + inq );
#endif /* SA_LL_IBQ_PROTECT */
    if (AGSA_RC_FAILURE == ret) {
      ossaSingleThreadedEnter( agRoot, LL_IOREQ_LOCKEQ_LOCK );
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd( &(saRoot->freeIORequests), &(pRequest->linkNode) );
      ossaSingleThreadedLeave( agRoot, LL_IOREQ_LOCKEQ_LOCK );
      SA_DBG1( ("saFwFlashUpdate, error when post FW_FLASH_UPDATE IOMB\n") );
    }
  }
  smTraceFuncExit( hpDBG_VERY_LOUD, 'd', "6a" );
  return ret;
}


GLOBAL bit32 saFlashExtExecute (
                  agsaRoot_t            *agRoot,
                  agsaContext_t         *agContext,
                  bit32                 queueNum,
                  agsaFlashExtExecute_t *agFlashExtExe)
{

  bit32 ret           = AGSA_RC_SUCCESS, retVal;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  mpiICQueue_t        *circularQ;
  void                *pMessage;
  agsaFwFlashOpExt_t *pPayload;
  bit8                inq, outq;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2R");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saFlashExtExecute, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2R");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Assign inbound and outbound Ring Buffer */
    inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
    outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
    SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");

    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
    /* Get a free inbound queue entry */
    circularQ = &saRoot->inboundQueue[inq];
    retVal    = mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);

    /* if message size is too large return failure */
    if (AGSA_RC_FAILURE == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif   /* SA_LL_IBQ_PROTECT */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("saFlashExtExecute, error when get free IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2R");
      return AGSA_RC_FAILURE;
    }

    /* return busy if inbound queue is full */
    if (AGSA_RC_BUSY == retVal)
    {
#ifdef SA_LL_IBQ_PROTECT
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;

      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG3(("saFlashExtExecute, no more IOMB\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2R");
      return AGSA_RC_BUSY;
    }

    pPayload = (agsaFwFlashOpExt_t *)pMessage;

    si_memset(pPayload, 0, sizeof(agsaFwFlashOpExt_t));


    /* Prepare the FW_FLASH_UPDATE IOMB payload */
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,Command ), agFlashExtExe->command);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,PartOffset ), agFlashExtExe->partOffset);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,DataLength ), agFlashExtExe->dataLen);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,SGLAL ), agFlashExtExe->agSgl->sgLower);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,SGLAH ), agFlashExtExe->agSgl->sgUpper);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,Len ), agFlashExtExe->agSgl->len);
    OSSA_WRITE_LE_32(agRoot, pPayload, OSSA_OFFSET_OF(agsaFwFlashOpExt_t,E_sgl ), agFlashExtExe->agSgl->extReserved);

    /* post the IOMB to SPC */
    ret = mpiMsgProduce(circularQ, (void *)pMessage, MPI_CATEGORY_SAS_SATA, OPC_INB_FLASH_OP_EXT, outq, (bit8)circularQ->priority);

#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */


    if (AGSA_RC_FAILURE == ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      SA_DBG1(("saFlashExtExecute, error when post FW_FLASH_UPDATE IOMB\n"));
    }
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2R");

  return ret;

}


#ifdef SPC_ENABLE_PROFILE
/******************************************************************************/
/*! \brief SPC FW_PROFILE Respond
 *
 *  This command sends FW Profile Status to TD layer.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param payload      FW download response payload
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiFwProfileRsp(
  agsaRoot_t             *agRoot,
  agsaFwProfileRsp_t *payload
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;

  bit32     status, tag, len;

  /* get request from IOMap */
  OSSA_READ_LE_32(AGROOT, &tag, payload, OSSA_OFFSET_OF(agsaFwProfileRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, payload, OSSA_OFFSET_OF(agsaFwProfileRsp_t, status));
  OSSA_READ_LE_32(AGROOT, &len, payload, OSSA_OFFSET_OF(agsaFwProfileRsp_t, len));
  pRequest = saRoot->IOMap[tag].IORequest;
  if (agNULL == pRequest)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[tag].Tag = MARK_OFF;
    saRoot->IOMap[tag].IORequest = agNULL;
    SA_DBG1(("mpiFwProfileRsp: the request is NULL. Tag=%x\n", tag));
    return AGSA_RC_FAILURE;
  }
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);


  if(!pRequest->valid)
  {
    SA_DBG1(("mpiPortControlRsp: pRequest->valid %d not set\n", pRequest->valid));
  }

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  pRequest->valid = agFALSE;
  /* return the request to free pool */
  saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  ossaFwProfileCB(agRoot, agContext, status, len);

 return ret;
}
#endif
/******************************************************************************/
/*! \brief SPC FW_FLASH_UPDATE Respond
 *
 *  This command sends FW Flash Update Status to TD layer.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param payload      FW download response payload
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiFwFlashUpdateRsp(
  agsaRoot_t             *agRoot,
  agsaFwFlashUpdateRsp_t *payload
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;

  bit32     status, tag;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"6b");

  /* get request from IOMap */
  OSSA_READ_LE_32(AGROOT, &tag, payload, OSSA_OFFSET_OF(agsaFwFlashUpdateRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &status, payload, OSSA_OFFSET_OF(agsaFwFlashUpdateRsp_t, status));
  pRequest = saRoot->IOMap[tag].IORequest;
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiFwFlashUpdateRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  if(status > 1)
  {
    SA_DBG1(("mpiFwFlashUpdateRsp: status = 0x%x\n",status));
  }

  ossaFwFlashUpdateCB(agRoot, agContext, status);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6b");

  return ret;
}

GLOBAL bit32 mpiFwExtFlashUpdateRsp(
  agsaRoot_t             *agRoot,
  agsaFwFlashOpExtRsp_t *payload
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaContext_t       *agContext;

  agsaFlashExtResponse_t FlashExtRsp;

  bit32     Command,Status, tag;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2T");

  /* get request from IOMap */
  OSSA_READ_LE_32(AGROOT, &tag, payload, OSSA_OFFSET_OF(agsaFwFlashOpExtRsp_t, tag));
  OSSA_READ_LE_32(AGROOT, &Command, payload, OSSA_OFFSET_OF(agsaFwFlashOpExtRsp_t,Command ));
  OSSA_READ_LE_32(AGROOT, &Status, payload, OSSA_OFFSET_OF(agsaFwFlashOpExtRsp_t,Status ));
  OSSA_READ_LE_32(AGROOT, &FlashExtRsp.epart_sect_size, payload, OSSA_OFFSET_OF(agsaFwFlashOpExtRsp_t,Epart_Size ));
  OSSA_READ_LE_32(AGROOT, &FlashExtRsp.epart_size, payload, OSSA_OFFSET_OF(agsaFwFlashOpExtRsp_t,EpartSectSize ));

  pRequest = saRoot->IOMap[tag].IORequest;
  agContext = saRoot->IOMap[tag].agContext;
  /* remove the request from IOMap */
  saRoot->IOMap[tag].Tag = MARK_OFF;
  saRoot->IOMap[tag].IORequest = agNULL;
  saRoot->IOMap[tag].agContext = agNULL;
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  SA_ASSERT((pRequest->valid), "pRequest->valid");
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiFwExtFlashUpdateRsp: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  if(Status > 1)
  {
    SA_DBG1(("mpiFwExtFlashUpdateRsp: status = 0x%x\n",Status));
  }

  ossaFlashExtExecuteCB(agRoot, agContext, Status,Command,&FlashExtRsp);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2T");

  return ret;

}


/******************************************************************************/
/*! \brief SPC Get Controller Information Command
 *
 *  This command sends Get Controller Information Command to SPC.
 *
 *  \param agRoot         Handles for this instance of SAS/SATA LL
 *  \param controllerInfo Controller Information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/

GLOBAL bit32 saGetControllerInfo(
                        agsaRoot_t                *agRoot,
                        agsaControllerInfo_t      *controllerInfo
                        )
{

  bit32     ret = AGSA_RC_SUCCESS;
  bit32     max_wait_time;
  bit32     max_wait_count;
  bit32     ContrlCapFlag, MSGUCfgTblBase, CfgTblDWIdx;
  bit32     value = 0, value1 = 0;
  bit8      pcibar;

  if (agNULL != agRoot->sdkData)
  {
    smTraceFuncEnter(hpDBG_VERY_LOUD,"6e");
  }
  /* clean the structure */
  si_memset(controllerInfo, 0, sizeof(agsaControllerInfo_t));

  if(smIS_SPC6V(agRoot))
  {
    controllerInfo->sdkInterfaceRev = STSDK_LL_INTERFACE_VERSION;
    controllerInfo->sdkRevision     = STSDK_LL_VERSION;
    controllerInfo->hwRevision = (ossaHwRegReadConfig32(agRoot,8) & 0xFF);
  }else  if(smIS_SPC12V(agRoot))
  {
    controllerInfo->sdkInterfaceRev = STSDK_LL_12G_INTERFACE_VERSION;
    controllerInfo->sdkRevision     = STSDK_LL_12G_VERSION;
    controllerInfo->hwRevision = (ossaHwRegReadConfig32(agRoot,8) & 0xFF);
  } else if(smIS_SPC(agRoot))
  {
    controllerInfo->hwRevision = SPC_READ_DEV_REV;
    controllerInfo->sdkInterfaceRev = MATCHING_SPC_FW_VERSION;
    controllerInfo->sdkRevision     = STSDK_LL_SPC_VERSION;
  }
  else
  {
    controllerInfo->hwRevision = (ossaHwRegReadConfig32(agRoot,8) & 0xFF);
  }

  SA_DBG1(("saGetControllerInfo: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0)));
  SA_DBG1(("saGetControllerInfo: SCRATCH_PAD1 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1)));
  SA_DBG1(("saGetControllerInfo: SCRATCH_PAD2 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_2,  MSGU_SCRATCH_PAD_2)));
  SA_DBG1(("saGetControllerInfo: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_3,  MSGU_SCRATCH_PAD_3)));
  SA_DBG1(("saGetControllerInfo: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_3,  MSGU_SCRATCH_PAD_3)));

  if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0) == 0xFFFFFFFF)
  {
    SA_DBG1(("saGetControllerInfo:AGSA_RC_FAILURE SCRATCH_PAD0 value = 0x%x\n",
            siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0) ) );
    return AGSA_RC_FAILURE;
  }

  if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0) == 0xFFFFFFFF)
  {
    SA_DBG1(("saGetControllerInfo:AGSA_RC_FAILURE SCRATCH_PAD0 value = 0x%x\n",
            siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0) ) );
    return AGSA_RC_FAILURE;
  }

  if( SCRATCH_PAD1_V_ERROR_STATE(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1)) )
  {
    SA_DBG1(("saGetControllerInfo: SCRATCH_PAD1 (0x%x) in error state ila %d raae %d Iop0 %d Iop1 %d\n",
      siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1),
    ( SCRATCH_PAD1_V_ILA_ERROR_STATE(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1)) ? 1 : 0),
    ( SCRATCH_PAD1_V_RAAE_ERROR_STATE(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1)) ? 1 : 0),
    ( SCRATCH_PAD1_V_IOP0_ERROR_STATE(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1)) ? 1 : 0),
    ( SCRATCH_PAD1_V_IOP1_ERROR_STATE(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,MSGU_SCRATCH_PAD_1)) ? 1 : 0) ));

  }

  if(smIS_SPC(agRoot))
  {
    /* check HDA mode */
    value = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;

    if (value == BOOTTLOADERHDA_IDLE)
    {
      /* HDA mode */
      SA_DBG1(("saGetControllerInfo: HDA mode, value = 0x%x\n", value));
      return AGSA_RC_HDA_NO_FW_RUNNING;
    }
  }
  else
  {
    if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1) &   SCRATCH_PAD1_V_RESERVED )
    {
      SA_DBG1(("saGetControllerInfo: Warning SCRATCH_PAD1 reserved bits set value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1)));
    }
    if( si_check_V_HDA(agRoot))
    {
      /*  Check HDA */
      SA_DBG1(("saGetControllerInfo: HDA mode AGSA_RC_HDA_NO_FW_RUNNING\n" ));
      return AGSA_RC_HDA_NO_FW_RUNNING;
    }


  }

  /* checking the fw AAP and IOP in ready state */
  max_wait_time = WAIT_SECONDS(gWait_2);  /* 2 sec timeout */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
  /* wait until scratch pad 1 and 2 registers in ready state  */
  if(smIS_SPCV(agRoot))
  {
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
      value1 =siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2);
      if(smIS_SPCV(agRoot))
      {
        if((value & SCRATCH_PAD1_V_RESERVED) )
        {
          SA_DBG1(("saGetControllerInfo: V reserved SCRATCH_PAD1 value = 0x%x (0x%x)\n", value, SCRATCH_PAD1_V_RESERVED));
          ret = AGSA_RC_FW_NOT_IN_READY_STATE;
          break;
        }
      }

      if ((max_wait_count -= WAIT_INCREMENT) == 0)
      {
        SA_DBG1(("saGetControllerInfo:  timeout SCRATCH_PAD1_V_READY !! SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));
        break;
      }

    } while (((value & SCRATCH_PAD1_V_READY) != SCRATCH_PAD1_V_READY) || (value == 0xffffffff));

  }
  else
  {
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);
      /* checking bit 4 to 7 for reserved in case we get 0xFFFFFFFF */
      if (value & SCRATCH_PAD1_RESERVED)
      {
        SA_DBG1(("saGetControllerInfo: SCRATCH_PAD1 value = 0x%x\n", value));
        ret = AGSA_RC_FW_NOT_IN_READY_STATE;
        break;
      }
      value1 =siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2,MSGU_SCRATCH_PAD_2);
      /* checking bit 4 to 7 for reserved in case we get 0xFFFFFFFF */
      if (value1 & SCRATCH_PAD2_RESERVED)
      {
        SA_DBG1(("saGetControllerInfo: SCRATCH_PAD2 value = 0x%x\n", value1));
        ret = AGSA_RC_FW_NOT_IN_READY_STATE;
        break;
      }
      if ((max_wait_count -= WAIT_INCREMENT) == 0)
      {
        SA_DBG1(("saGetControllerInfo: Timeout!! SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));
        break;
      }
    } while (((value & SCRATCH_PAD_STATE_MASK) != SCRATCH_PAD1_RDY) || ((value1 & SCRATCH_PAD_STATE_MASK) != SCRATCH_PAD2_RDY));
  }

  if (!max_wait_count)
  {
    SA_DBG1(("saGetControllerInfo: timeout failure\n"));
    ret = AGSA_RC_FW_NOT_IN_READY_STATE;
  }

  if (ret == AGSA_RC_SUCCESS)
  {
    SA_DBG1(("saGetControllerInfo: FW Ready, SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));

    /* read scratch pad0 to get PCI BAR and offset of configuration table */
     MSGUCfgTblBase = siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0);
    /* get offset */
    CfgTblDWIdx = MSGUCfgTblBase & SCRATCH_PAD0_OFFSET_MASK;
    /* get PCI BAR */
    MSGUCfgTblBase = (MSGUCfgTblBase & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;

    /* convert the PCI BAR to logical bar number */
    pcibar = (bit8)mpiGetPCIBarIndex(agRoot, MSGUCfgTblBase);

    /* get controller information */
    controllerInfo->signature =         ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx);
    controllerInfo->fwInterfaceRev =    ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_INTERFACE_REVISION);
    controllerInfo->fwRevision =        ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_FW_REVISION);
    controllerInfo->ilaRevision =       ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_ILAT_ILAV_ILASMRN_ILAMRN_ILAMJN);
    controllerInfo->maxPendingIO =      ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_MAX_OUTSTANDING_IO_OFFSET);
    controllerInfo->maxDevices =       (ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_MAX_SGL_OFFSET) & MAIN_MAX_DEV_BITS);
    controllerInfo->maxDevices =        controllerInfo->maxDevices >> SHIFT16;
    controllerInfo->maxSgElements =    (ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_MAX_SGL_OFFSET) & MAIN_MAX_SGL_BITS);

    if( smIS_SPC(agRoot))
    {
      SA_DBG2(("saGetControllerInfo: LINK_CTRL 0x%08x Speed 0x%X Lanes 0x%X \n", ossaHwRegReadConfig32(agRoot,128),
        ((ossaHwRegReadConfig32(agRoot,128) & 0x000F0000) >> 16),
        ((ossaHwRegReadConfig32(agRoot,128) & 0x0FF00000) >> 20) ));
      controllerInfo->PCILinkRate =  ((ossaHwRegReadConfig32(agRoot,128) & 0x000F0000) >> 16);
      controllerInfo->PCIWidth =   ((ossaHwRegReadConfig32(agRoot,128) & 0x0FF00000) >> 20);
    }
    else
    {
      SA_DBG2(("saGetControllerInfo: LINK_CTRL 0x%08x Speed 0x%X Lanes 0x%X \n", ossaHwRegReadConfig32(agRoot,208),
        ((ossaHwRegReadConfig32(agRoot,208) & 0x000F0000) >> 16),
        ((ossaHwRegReadConfig32(agRoot,208) & 0x0FF00000) >> 20) ));
      controllerInfo->PCILinkRate =  ((ossaHwRegReadConfig32(agRoot,208) & 0x000F0000) >> 16);
      controllerInfo->PCIWidth =   ((ossaHwRegReadConfig32(agRoot,208) & 0x0FF00000) >> 20);
    }


    ContrlCapFlag =                     ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_CNTRL_CAP_OFFSET);
    controllerInfo->queueSupport =      ContrlCapFlag & MAIN_QSUPPORT_BITS;
    controllerInfo->phyCount =         (bit8)((ContrlCapFlag & MAIN_PHY_COUNT_MASK) >> SHIFT19);


    if(smIS_SPCV(agRoot))
    {
      controllerInfo->controllerSetting = (bit8)((siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1) & SCRATCH_PAD1_V_BOOTSTATE_MASK ) >> SHIFT4);
    }
    else
    {
      controllerInfo->controllerSetting = (bit8)(ossaHwRegReadExt(agRoot, pcibar, (bit32)CfgTblDWIdx + MAIN_HDA_FLAGS_OFFSET) & MAIN_HDA_FLAG_BITS);
    }
    controllerInfo->sasSpecsSupport =   (ContrlCapFlag & MAIN_SAS_SUPPORT_BITS) >> SHIFT25;
  }

  SA_DBG1(("saGetControllerInfo: signature         0x%X\n", controllerInfo->signature));
  SA_DBG1(("saGetControllerInfo: fwInterfaceRev    0x%X\n", controllerInfo->fwInterfaceRev));
  SA_DBG1(("saGetControllerInfo: hwRevision        0x%X\n", controllerInfo->hwRevision));
  SA_DBG1(("saGetControllerInfo: fwRevision        0x%X\n", controllerInfo->fwRevision));
  SA_DBG1(("saGetControllerInfo: ilaRevision       0x%X\n", controllerInfo->ilaRevision));
  SA_DBG1(("saGetControllerInfo: maxPendingIO      0x%X\n", controllerInfo->maxPendingIO));
  SA_DBG1(("saGetControllerInfo: maxDevices        0x%X\n", controllerInfo->maxDevices));
  SA_DBG1(("saGetControllerInfo: maxSgElements     0x%X\n", controllerInfo->maxSgElements));
  SA_DBG1(("saGetControllerInfo: queueSupport      0x%X\n", controllerInfo->queueSupport));
  SA_DBG1(("saGetControllerInfo: phyCount          0x%X\n", controllerInfo->phyCount));
  SA_DBG1(("saGetControllerInfo: controllerSetting 0x%X\n", controllerInfo->controllerSetting));
  SA_DBG1(("saGetControllerInfo: PCILinkRate       0x%X\n", controllerInfo->PCILinkRate));
  SA_DBG1(("saGetControllerInfo: PCIWidth          0x%X\n", controllerInfo->PCIWidth));
  SA_DBG1(("saGetControllerInfo: sasSpecsSupport   0x%X\n", controllerInfo->sasSpecsSupport));
  SA_DBG1(("saGetControllerInfo: sdkInterfaceRev   0x%X\n", controllerInfo->sdkInterfaceRev));
  SA_DBG1(("saGetControllerInfo: sdkRevision       0x%X\n", controllerInfo->sdkRevision));
  if (agNULL != agRoot->sdkData)
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6e");
  }
  return ret;
}

/******************************************************************************/
/*! \brief SPC Get Controller Status Command
 *
 *  This command sends Get Controller Status Command to SPC.
 *
 *  \param agRoot           Handles for this instance of SAS/SATA LL
 *  \param controllerStatus controller status
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetControllerStatus(
                        agsaRoot_t                *agRoot,
                        agsaControllerStatus_t    *controllerStatus
                        )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  spc_GSTableDescriptor_t GSTable;
  bit32 max_wait_time;
  bit32 max_wait_count;
  bit32 i, value, value1;

  if (agNULL != saRoot)
  {
    smTraceFuncEnter(hpDBG_VERY_LOUD,"6f");
  }
  /* clean the structure */
  si_memset(controllerStatus, 0, sizeof(agsaControllerStatus_t));
  si_memset(&GSTable, 0, sizeof(spc_GSTableDescriptor_t));
  if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0) == 0xFFFFFFFF)
  {
    SA_DBG1(("saGetControllerStatus:AGSA_RC_FAILURE SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0)));
    return AGSA_RC_FAILURE;
  }

  if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_3)  & (OSSA_ENCRYPT_ENGINE_FAILURE_MASK | OSSA_DIF_ENGINE_FAILURE_MASK))
  {
    SA_DBG1(("saGetControllerStatus: BIST error in SCRATCHPAD 3 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_3,  MSGU_SCRATCH_PAD_3)));
  }

  if(smIS_SPC(agRoot))
  {

    /* read detail fatal errors */
    controllerStatus->fatalErrorInfo.errorInfo0 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0);
    controllerStatus->fatalErrorInfo.errorInfo1 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
    controllerStatus->fatalErrorInfo.errorInfo2 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
    controllerStatus->fatalErrorInfo.errorInfo3 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_3);

#if defined(SALLSDK_DEBUG)
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD0 value = 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo0));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD1 value = 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo1));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD2 value = 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo2));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD3 value = 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo3));
#endif

    /* check HDA mode */
    value = ossaHwRegReadExt(agRoot, PCIBAR3, HDA_RSP_OFFSET1MB+HDA_CMD_CODE_OFFSET) & HDA_STATUS_BITS;

    if (value == BOOTTLOADERHDA_IDLE)
    {
      /* HDA mode */
      SA_DBG1(("saGetControllerStatus: HDA mode, value = 0x%x\n", value));
      return AGSA_RC_HDA_NO_FW_RUNNING;
    }

    /* check error state */
    value = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
    value1 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);

    /* check AAP or IOP error */
    if ((SCRATCH_PAD1_ERR == (value & SCRATCH_PAD_STATE_MASK)) || (SCRATCH_PAD2_ERR == (value1 & SCRATCH_PAD_STATE_MASK)))
    {
      if (agNULL != saRoot)
      {
        controllerStatus->fatalErrorInfo.regDumpBusBaseNum0 = saRoot->mainConfigTable.regDumpPCIBAR;
        controllerStatus->fatalErrorInfo.regDumpOffset0 = saRoot->mainConfigTable.FatalErrorDumpOffset0;
        controllerStatus->fatalErrorInfo.regDumpLen0 = saRoot->mainConfigTable.FatalErrorDumpLength0;
        controllerStatus->fatalErrorInfo.regDumpBusBaseNum1 = saRoot->mainConfigTable.regDumpPCIBAR;
        controllerStatus->fatalErrorInfo.regDumpOffset1 = saRoot->mainConfigTable.FatalErrorDumpOffset1;
        controllerStatus->fatalErrorInfo.regDumpLen1 = saRoot->mainConfigTable.FatalErrorDumpLength1;
      }
      else
      {
        controllerStatus->fatalErrorInfo.regDumpBusBaseNum0 = 0;
        controllerStatus->fatalErrorInfo.regDumpOffset0 = 0;
        controllerStatus->fatalErrorInfo.regDumpLen0 = 0;
        controllerStatus->fatalErrorInfo.regDumpBusBaseNum1 = 0;
        controllerStatus->fatalErrorInfo.regDumpOffset1 = 0;
        controllerStatus->fatalErrorInfo.regDumpLen1 = 0;
      }

      if (agNULL != saRoot)
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6f");
      }
      return AGSA_RC_FW_NOT_IN_READY_STATE;
    }

    /* checking the fw AAP and IOP in ready state */
    max_wait_time = WAIT_SECONDS(2);  /* 2 sec timeout */
    max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT);
    /* wait until scratch pad 1 and 2 registers in ready state  */
    do
    {
      ossaStallThread(agRoot, WAIT_INCREMENT);
      value = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_1);
      /* checking bit 4 to 7 for reserved in case we get 0xFFFFFFFF */
      if (value & SCRATCH_PAD1_RESERVED)
      {
        SA_DBG1(("saGetControllerStatus: (Reserved bit not 0) SCRATCH_PAD1 value = 0x%x\n", value));
        ret = AGSA_RC_FAILURE;
        break;
      }

      value1 = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_2);
      /* checking bit 4 to 7 for reserved in case we get 0xFFFFFFFF */
      if (value1 & SCRATCH_PAD2_RESERVED)
      {
        SA_DBG1(("saGetControllerStatus: (Reserved bit not 0) SCRATCH_PAD2 value = 0x%x\n", value1));
        ret = AGSA_RC_FAILURE;
        break;
      }

      if ((max_wait_count -=WAIT_INCREMENT) == 0)
      {
        SA_DBG1(("saGetControllerStatus: Timeout!! SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));
        break;
      }
    } while (((value & SCRATCH_PAD_STATE_MASK) != SCRATCH_PAD1_RDY) || ((value1 & SCRATCH_PAD_STATE_MASK) != SCRATCH_PAD2_RDY));

    if (!max_wait_count)
    {
      SA_DBG1(("saGetControllerStatus: timeout failure\n"));
      ret = AGSA_RC_FAILURE;
    }

    if (ret == AGSA_RC_SUCCESS)
    {
      SA_DBG1(("saGetControllerStatus: FW Ready, SCRATCH_PAD1/2 value = 0x%x 0x%x\n", value, value1));

      /* read scratch pad0 to get PCI BAR and offset of configuration table */
      value = ossaHwRegRead(agRoot, MSGU_SCRATCH_PAD_0);
      /* get offset */
      value1 = value & SCRATCH_PAD0_OFFSET_MASK;
      /* get PCI BAR */
      value = (value & SCRATCH_PAD0_BAR_MASK) >> SHIFT26;

      /* read GST Table state */
      mpiReadGSTable(agRoot, &GSTable);

      /* read register dump information */
      controllerStatus->fatalErrorInfo.regDumpBusBaseNum0 = value;
      controllerStatus->fatalErrorInfo.regDumpBusBaseNum1 = value;
      /* convert the PCI BAR to logical bar number */
      value = (bit8)mpiGetPCIBarIndex(agRoot, value);
      controllerStatus->fatalErrorInfo.regDumpOffset0 = ossaHwRegReadExt(agRoot, value, value1 + MAIN_FATAL_ERROR_RDUMP0_OFFSET);
      controllerStatus->fatalErrorInfo.regDumpLen0    = ossaHwRegReadExt(agRoot, value, value1 + MAIN_FATAL_ERROR_RDUMP0_LENGTH);
      controllerStatus->fatalErrorInfo.regDumpOffset1 = ossaHwRegReadExt(agRoot, value, value1 + MAIN_FATAL_ERROR_RDUMP1_OFFSET);
      controllerStatus->fatalErrorInfo.regDumpLen1    = ossaHwRegReadExt(agRoot, value, value1 + MAIN_FATAL_ERROR_RDUMP1_LENGTH);

      /* AAP/IOP error state */
      SA_DBG2(("saGetControllerStatus: SCRATCH PAD0 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo0));
      SA_DBG2(("saGetControllerStatus: SCRATCH PAD1 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo1));
      SA_DBG2(("saGetControllerStatus: SCRATCH PAD2 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo2));
      SA_DBG2(("saGetControllerStatus: SCRATCH PAD3 0x%x\n", controllerStatus->fatalErrorInfo.errorInfo3));
      /* Register Dump information */
      SA_DBG2(("saGetControllerStatus: RegDumpOffset0 0x%x\n", controllerStatus->fatalErrorInfo.regDumpOffset0));
      SA_DBG2(("saGetControllerStatus: RegDumpLen0    0x%x\n", controllerStatus->fatalErrorInfo.regDumpLen0));
      SA_DBG2(("saGetControllerStatus: RegDumpOffset1 0x%x\n", controllerStatus->fatalErrorInfo.regDumpOffset1));
      SA_DBG2(("saGetControllerStatus: RegDumpLen1    0x%x\n", controllerStatus->fatalErrorInfo.regDumpLen1));

      controllerStatus->interfaceState = GSTable.GSTLenMPIS & GST_INF_STATE_BITS;
      controllerStatus->iqFreezeState0 = GSTable.IQFreezeState0;
      controllerStatus->iqFreezeState1 = GSTable.IQFreezeState1;
      for (i = 0; i < 8; i++)
      {
        controllerStatus->phyStatus[i] = GSTable.PhyState[i];
        controllerStatus->recoverableErrorInfo[i] = GSTable.recoverErrInfo[i];
      }
      controllerStatus->tickCount0 = GSTable.MsguTcnt;
      controllerStatus->tickCount1 = GSTable.IopTcnt;
      controllerStatus->tickCount2 = GSTable.Iop1Tcnt;
    }
  }
  else
  {

    SA_DBG1(("saGetControllerStatus: SPCv\n" ));


    if(siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1) &   SCRATCH_PAD1_V_RESERVED )
    {
      SA_DBG1(("saGetControllerStatus: Warning SCRATCH_PAD1 reserved bits set value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1)));
    }
    if( si_check_V_HDA(agRoot))
    {
      /*  Check HDA */

      controllerStatus->fatalErrorInfo.errorInfo0 = ossaHwRegRead(agRoot,V_Scratchpad_0_Register );
      controllerStatus->fatalErrorInfo.errorInfo1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register );
      controllerStatus->fatalErrorInfo.errorInfo2 = ossaHwRegRead(agRoot,V_Scratchpad_2_Register );
      controllerStatus->fatalErrorInfo.errorInfo3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register );
      SA_DBG1(("saGetControllerStatus: HDA mode, AGSA_RC_HDA_NO_FW_RUNNING errorInfo1  = 0x%x\n",controllerStatus->fatalErrorInfo.errorInfo1 ));
      return AGSA_RC_HDA_NO_FW_RUNNING;
    }

    ret = si_check_V_Ready(agRoot);
    /* Check ready */
    if (ret == AGSA_RC_SUCCESS)
    {
      /* read GST Table state */
      mpiReadGSTable(agRoot, &GSTable);
      controllerStatus->interfaceState = GSTable.GSTLenMPIS & GST_INF_STATE_BITS;
      controllerStatus->iqFreezeState0 = GSTable.IQFreezeState0;
      controllerStatus->iqFreezeState1 = GSTable.IQFreezeState1;
      for (i = 0; i < 8; i++)
      {
        controllerStatus->phyStatus[i] = GSTable.PhyState[i];
        controllerStatus->recoverableErrorInfo[i] = GSTable.recoverErrInfo[i];
      }
      controllerStatus->tickCount0 = GSTable.MsguTcnt;
      controllerStatus->tickCount1 = GSTable.IopTcnt;
      controllerStatus->tickCount2 = GSTable.Iop1Tcnt;

      controllerStatus->interfaceState = GSTable.GSTLenMPIS & GST_INF_STATE_BITS;
      controllerStatus->iqFreezeState0 = GSTable.IQFreezeState0;
      controllerStatus->iqFreezeState1 = GSTable.IQFreezeState1;
      for (i = 0; i < 8; i++)
      {
        if( IS_SDKDATA(agRoot))
        {
          if (agNULL != saRoot)
          {
            controllerStatus->phyStatus[i] = ((saRoot->phys[i+8].linkstatus << SHIFT8) | saRoot->phys[i].linkstatus);
          }
        }
        else
        {
          controllerStatus->phyStatus[i] = 0;
        }
        controllerStatus->recoverableErrorInfo[i] = GSTable.recoverErrInfo[i];
      }
      controllerStatus->tickCount0 = GSTable.MsguTcnt;
      controllerStatus->tickCount1 = GSTable.IopTcnt;
      controllerStatus->tickCount2 = GSTable.Iop1Tcnt;

    }

    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD0 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_0_Register)));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD1 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_1_Register)));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD2 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_2_Register)));
    SA_DBG1(("saGetControllerStatus: SCRATCH_PAD3 value = 0x%x\n", ossaHwRegRead(agRoot, V_Scratchpad_3_Register)));

    controllerStatus->fatalErrorInfo.errorInfo0 = ossaHwRegRead(agRoot,V_Scratchpad_0_Register );
    controllerStatus->fatalErrorInfo.errorInfo1 = ossaHwRegRead(agRoot,V_Scratchpad_1_Register );
    controllerStatus->fatalErrorInfo.errorInfo2 = ossaHwRegRead(agRoot,V_Scratchpad_2_Register );
    controllerStatus->fatalErrorInfo.errorInfo3 = ossaHwRegRead(agRoot,V_Scratchpad_3_Register );

    controllerStatus->bootStatus = ( (( controllerStatus->fatalErrorInfo.errorInfo1 >>  SHIFT9) & 1 )                | /* bit 1  */
                                     (( controllerStatus->fatalErrorInfo.errorInfo3 & 0x3)               << SHIFT16) | /* bit 16 17 */
                                    ((( controllerStatus->fatalErrorInfo.errorInfo3 >>  SHIFT14) & 0x7)  << SHIFT18) | /* bit 18 19 20 */
                                    ((( controllerStatus->fatalErrorInfo.errorInfo3 >>  SHIFT4 ) & 0x1)  << SHIFT23) | /* bit 23 */
                                    ((( controllerStatus->fatalErrorInfo.errorInfo3 >>  SHIFT16) & 0xFF) << SHIFT24) );/* bit 24 31 */

    controllerStatus->bootComponentState[0] = (bit16) (( controllerStatus->fatalErrorInfo.errorInfo1               & 3 ) | 0x8000); /* RAAE_STATE */
    controllerStatus->bootComponentState[1] = (bit16) ((( controllerStatus->fatalErrorInfo.errorInfo1 >>  SHIFT10) & 3 ) | 0x8000); /* IOP0_STATE */
    controllerStatus->bootComponentState[2] = (bit16) ((( controllerStatus->fatalErrorInfo.errorInfo1 >>  SHIFT12) & 3 ) | 0x8000); /* IOP1_STATE */
    controllerStatus->bootComponentState[3] = (bit16) ((( controllerStatus->fatalErrorInfo.errorInfo1 >>  SHIFT4)  & 7 ) | 0x8000); /* BOOTLDR_STATE  */
    controllerStatus->bootComponentState[4] = (bit16) ((( controllerStatus->fatalErrorInfo.errorInfo1 >>  SHIFT2)  & 3 ) | 0x8000); /* ILA State */
    controllerStatus->bootComponentState[5] = 0;
    controllerStatus->bootComponentState[6] = 0;
    controllerStatus->bootComponentState[7] = 0;



    if(controllerStatus->fatalErrorInfo.errorInfo0 == 0xFFFFFFFF)
    {
      ret = AGSA_RC_FAILURE;
    }

  }

  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.errorInfo0          0x%x\n", controllerStatus->fatalErrorInfo.errorInfo0));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.errorInfo1          0x%x\n", controllerStatus->fatalErrorInfo.errorInfo1));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.errorInfo2          0x%x\n", controllerStatus->fatalErrorInfo.errorInfo2));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.errorInfo3          0x%x\n", controllerStatus->fatalErrorInfo.errorInfo3));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpBusBaseNum0  0x%x\n", controllerStatus->fatalErrorInfo.regDumpBusBaseNum0));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpOffset0      0x%x\n", controllerStatus->fatalErrorInfo.regDumpOffset0));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpLen0         0x%x\n", controllerStatus->fatalErrorInfo.regDumpLen0));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpBusBaseNum1  0x%x\n", controllerStatus->fatalErrorInfo.regDumpBusBaseNum1));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpOffset1      0x%x\n", controllerStatus->fatalErrorInfo.regDumpOffset1));
  SA_DBG1(("saGetControllerStatus: fatalErrorInfo.regDumpLen1         0x%x\n", controllerStatus->fatalErrorInfo.regDumpLen1));

  SA_DBG1(("saGetControllerStatus: interfaceState                     0x%x\n", controllerStatus->interfaceState));
  SA_DBG1(("saGetControllerStatus: iqFreezeState0                     0x%x\n", controllerStatus->iqFreezeState0));
  SA_DBG1(("saGetControllerStatus: iqFreezeState1                     0x%x\n", controllerStatus->iqFreezeState1));
  SA_DBG1(("saGetControllerStatus: tickCount0                         0x%x\n", controllerStatus->tickCount0));
  SA_DBG1(("saGetControllerStatus: tickCount1                         0x%x\n", controllerStatus->tickCount1));
  SA_DBG1(("saGetControllerStatus: tickCount2                         0x%x\n", controllerStatus->tickCount2));

  SA_DBG1(("saGetControllerStatus: phyStatus[0]                       0x%08x\n", controllerStatus->phyStatus[0]));
  SA_DBG1(("saGetControllerStatus: phyStatus[1]                       0x%08x\n", controllerStatus->phyStatus[1]));
  SA_DBG1(("saGetControllerStatus: phyStatus[2]                       0x%08x\n", controllerStatus->phyStatus[2]));
  SA_DBG1(("saGetControllerStatus: phyStatus[3]                       0x%08x\n", controllerStatus->phyStatus[3]));
  SA_DBG1(("saGetControllerStatus: phyStatus[4]                       0x%08x\n", controllerStatus->phyStatus[4]));
  SA_DBG1(("saGetControllerStatus: phyStatus[5]                       0x%08x\n", controllerStatus->phyStatus[5]));
  SA_DBG1(("saGetControllerStatus: phyStatus[6]                       0x%08x\n", controllerStatus->phyStatus[6]));
  SA_DBG1(("saGetControllerStatus: phyStatus[7]                       0x%08x\n", controllerStatus->phyStatus[7]));

  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[0]            0x%08x\n", controllerStatus->recoverableErrorInfo[0]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[1]            0x%08x\n", controllerStatus->recoverableErrorInfo[1]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[2]            0x%08x\n", controllerStatus->recoverableErrorInfo[2]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[3]            0x%08x\n", controllerStatus->recoverableErrorInfo[3]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[4]            0x%08x\n", controllerStatus->recoverableErrorInfo[4]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[5]            0x%08x\n", controllerStatus->recoverableErrorInfo[5]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[6]            0x%08x\n", controllerStatus->recoverableErrorInfo[6]));
  SA_DBG1(("saGetControllerStatus: recoverableErrorInfo[7]            0x%08x\n", controllerStatus->recoverableErrorInfo[7]));

  SA_DBG1(("saGetControllerStatus: bootStatus                         0x%08x\n", controllerStatus->bootStatus));
  SA_DBG1(("saGetControllerStatus: bootStatus  Active FW Image        %x\n", (controllerStatus->bootStatus & 1 ) ? 1 : 0 ));
  SA_DBG1(("saGetControllerStatus: bootStatus  Encryption Cap         %x\n", ((controllerStatus->bootStatus & 0x30000 ) >> SHIFT16) ));
  SA_DBG1(("saGetControllerStatus: bootStatus  Encryption Sec Mode    %x\n", ((controllerStatus->bootStatus & 0xC0000 ) >> SHIFT18) ));
  SA_DBG1(("saGetControllerStatus: bootStatus  Encryption AES XTS     %x\n", (controllerStatus->bootStatus & 0x800000 ) ? 1 : 0 ));
  SA_DBG1(("saGetControllerStatus: bootStatus  Encryption Engine Stat 0x%x\n", ((controllerStatus->bootStatus & 0xFF000000 ) >> SHIFT24)  ));

/*

Bit 0 : Active FW Image
0b: Primary Image
1b: Secondary Image

Bit 16-17 :  Encryption Capability
00: Not supported. Controller firmware version doesn't support encryption functionality.
01: Disabled due to error. Controller firmware supports encryption however, the functionality is currently disabled due to an error. The actual cause of the error is indicated in the error code field (bits [23:16]).
10: Enabled with Error. Encryption is currently enabled however, firmware encountered encryption-related error during initialization which might have caused the controller to enter SMF Security mode and/or disabled access to non-volatile memory for encryption-related information. The actual cause of the error is indicated in the error code field (bits [23:16]).
11: Enabled. Encryption functionality is enabled and fully functional.
Bit 18-21 : Encryption Current Security Mode
0000: Security Mode Factory
0001: Security Mode A
0010: Security Mode B
All other values are reserved.
Bit22: Reserved
Bit 23 : Encryption AES XTS Enabled
0: AES XTS is disabled.
1: AES XTS is enabled
Bit 24-31 : Encryption Engine Status
*/


  SA_DBG1(("saGetControllerStatus: bootComponentState[0] RAAE_STATE   0x%x\n", controllerStatus->bootComponentState[0]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[1] IOP0_STATE   0x%x\n", controllerStatus->bootComponentState[1]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[2] IOP1_STATE   0x%x\n", controllerStatus->bootComponentState[2]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[3] BOOTLDR_     0x%x\n", controllerStatus->bootComponentState[3]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[4] ILA State    0x%x\n", controllerStatus->bootComponentState[4]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[5]              0x%x\n", controllerStatus->bootComponentState[5]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[6]              0x%x\n", controllerStatus->bootComponentState[6]));
  SA_DBG1(("saGetControllerStatus: bootComponentState[7]              0x%x\n", controllerStatus->bootComponentState[7]));

  if (agNULL != saRoot)
  {
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6f");
  }

  return ret;
}

/******************************************************************************/
/*! \brief SPC Get Controller Event Log Information Command
 *
 *  This command sends Get Controller Event Log Information Command to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param eventLogInfo event log information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGetControllerEventLogInfo(
                        agsaRoot_t                *agRoot,
                        agsaControllerEventLog_t  *eventLogInfo
                        )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6g");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  eventLogInfo->eventLog1 = saRoot->memoryAllocated.agMemory[MPI_MEM_INDEX + MPI_EVENTLOG_INDEX];
  eventLogInfo->eventLog1Option = saRoot->mainConfigTable.eventLogOption;
  eventLogInfo->eventLog2 = saRoot->memoryAllocated.agMemory[MPI_MEM_INDEX + MPI_IOP_EVENTLOG_INDEX];
  eventLogInfo->eventLog2Option = saRoot->mainConfigTable.IOPeventLogOption;

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6g");

  return ret;
}

/******************************************************************************/
/*! \brief SPC Set GPIO Event Setup Command
 *
 *  This command sends GPIO Event Setup Command to SPC.
 *
 *  \param agRoot             Handles for this instance of SAS/SATA LL
 *  \param agsaContext        Context of this command
 *  \param queueNum           Queue number of inbound/outbound queue
 *  \param gpioEventSetupInfo Pointer of Event Setup Information structure
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGpioEventSetup(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaGpioEventSetupInfo_t  *gpioEventSetupInfo
                        )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaGPIOCmd_t       payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6h");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGpioEventSetup, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6h");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGPIOCmd_t));
    /* build IOMB command and send to SPC */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, eOBIDGeGsGrGw), GPIO_GE_BIT);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GPIEVChange), gpioEventSetupInfo->gpioEventLevel);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GPIEVFall), gpioEventSetupInfo->gpioEventFallingEdge);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GPIEVRise), gpioEventSetupInfo->gpioEventRisingEdge);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GPIO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saGpioEventSetup: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saGpioEventSetup, sending IOMB failed\n" ));
    }
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6h");

  return ret;
}

/******************************************************************************/
/*! \brief SPC Set GPIO Pin Setup Command
 *
 *  This command sends GPIO Pin Setup Command to SPC.
 *
 *  \param agRoot             Handles for this instance of SAS/SATA LL
 *  \param agsaContext        Context of this command
 *  \param queueNum           Queue number of inbound/outbound queue
 *  \param gpioPinSetupInfo   Pointer of Event Setup Information structure
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGpioPinSetup(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        agsaGpioPinSetupInfo_t    *gpioPinSetupInfo
                        )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaGPIOCmd_t       payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6i");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGpioPinSetup, No request from free list\n" ));
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGPIOCmd_t));
    /* build IOMB command and send to SPC */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, eOBIDGeGsGrGw), GPIO_GS_BIT);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GpioIe), gpioPinSetupInfo->gpioInputEnabled);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, OT11_0), gpioPinSetupInfo->gpioTypePart1);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, OT19_12), gpioPinSetupInfo->gpioTypePart2);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GPIO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saGpioPinSetup: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saGpioPinSetup, sending IOMB failed\n" ));
    }
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6i");

  return ret;
}

/******************************************************************************/
/*! \brief SPC GPIO Read Command
 *
 *  This command sends GPIO Read Command to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param agsaContext  Context of this command
 *  \param queueNum     Queue number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGpioRead(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum
                        )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaGPIOCmd_t       payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6j");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGpioRead, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6j");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGPIOCmd_t));
    /* build IOMB command and send to SPC */
    /* set GR bit */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, eOBIDGeGsGrGw), GPIO_GR_BIT);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GPIO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saGpioRead: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saGpioRead, sending IOMB failed\n" ));
    }
  }
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6j");

  return ret;
}

/******************************************************************************/
/*! \brief SPC GPIO Write Command
 *
 *  This command sends GPIO Write Command to SPC.
 *
 *  \param agRoot         Handles for this instance of SAS/SATA LL
 *  \param agsaContext    Context of this command
 *  \param queueNum       Queue number of inbound/outbound queue
 *  \param gpioWriteMask  GPIO Write Mask
 *  \param gpioWriteValue GPIO Write Value
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saGpioWrite(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        bit32                     gpioWriteMask,
                        bit32                     gpioWriteValue
                        )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaGPIOCmd_t       payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6k");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGpioWrite, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6k");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGPIOCmd_t));
    /* build IOMB command and send to SPC */
    /* set GW bit */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, eOBIDGeGsGrGw), GPIO_GW_BIT);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GpioWrMsk), gpioWriteMask);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGPIOCmd_t, GpioWrVal), gpioWriteValue);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GPIO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saGpioWrite: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saGpioWrite, sending IOMB failed\n" ));
    }
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6k");

  return ret;
}

/******************************************************************************/
/*! \brief SPC SAS Diagnostic Execute Command
 *
 *  This command sends SAS Diagnostic Execute Command to SPC.
 *
 *  \param agRoot         Handles for this instance of SAS/SATA LL
 *  \param agsaContext    Context of this command
 *  \param queueNum       Queue number of inbound/outbound queue
 *  \param diag           Pointer of SAS Diag Execute Structure
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saSASDiagExecute(
                        agsaRoot_t              *agRoot,
                        agsaContext_t           *agContext,
                        bit32                    queueNum,
                        agsaSASDiagExecute_t    *diag
                        )
{
  bit32                     ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = agNULL;
  agsaIORequestDesc_t      *pRequest = agNULL;
  bit32  payload[32];
  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6m");

  SA_DBG2(("saSASDiagExecute,command 0x%X\n", diag->command ));
  SA_DBG2(("saSASDiagExecute,param0 0x%X\n", diag->param0 ));
  SA_DBG2(("saSASDiagExecute,param2 0x%X\n", diag->param2 ));
  SA_DBG2(("saSASDiagExecute,param3 0x%X\n", diag->param3 ));
  SA_DBG2(("saSASDiagExecute,param4 0x%X\n", diag->param4 ));
  SA_DBG2(("saSASDiagExecute,param5 0x%X\n", diag->param5 ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSASDiagExecute, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6m");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    if(smIS_SPC(agRoot))
    {
      diag->param5 = 0; /* Reserved for SPC */
    }

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(payload));
    /* set payload to zeros */
    if(smIS_SPCV(agRoot))
    {
      /* build IOMB command and send to SPC */
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, tag),             pRequest->HTag);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, CmdTypeDescPhyId),diag->command );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, Pat1Pat2),        diag->param0 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, Threshold),       diag->param1 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, CodePatErrMsk),   diag->param2 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, Pmon),            diag->param3 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, PERF1CTL),        diag->param4 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagExecuteCmd_t, THRSHLD1),        diag->param5 );
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SAS_DIAG_EXECUTE, IOMB_SIZE128, queueNum);
    }
    else
    {
      /* build IOMB command and send to SPC */
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, tag),             pRequest->HTag);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, CmdTypeDescPhyId),diag->command );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, Pat1Pat2),        diag->param0 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, Threshold),       diag->param1 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, CodePatErrMsk),   diag->param2 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, Pmon),            diag->param3 );
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_SASDiagExecuteCmd_t, PERF1CTL),        diag->param4 );
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SAS_DIAG_EXECUTE, IOMB_SIZE64, queueNum);
    }
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saSASDiagExecute: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saSASDiagExecute, sending IOMB failed\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6m");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return ret;
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "6m");
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  return ret;
}

/******************************************************************************/
/*! \brief SPC SAS Diagnostic Start/End Command
 *
 *  This command sends SAS Diagnostic Start/End Command to SPC.
 *
 *  \param agRoot         Handles for this instance of SAS/SATA LL
 *  \param agsaContext    Context of this command
 *  \param queueNum       Queue number of inbound/outbound queue
 *  \param phyId          Phy ID
 *  \param operation      Operation of SAS Diagnostic
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saSASDiagStartEnd(
                        agsaRoot_t                *agRoot,
                        agsaContext_t             *agContext,
                        bit32                     queueNum,
                        bit32                     phyId,
                        bit32                     operation
                        )
{
  bit32 ret                = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot;
  agsaIORequestDesc_t      *pRequest;
  agsaSASDiagStartEndCmd_t payload;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  if (agRoot == agNULL)
  {
    SA_DBG1(("saSASDiagStartEnd: agRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  if (saRoot == agNULL)
  {
    SA_DBG1(("saSASDiagStartEnd: saRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6n");

  SA_DBG3(("saSASDiagStartEnd, phyId 0x%x operation 0x%x\n",phyId,operation ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saSASDiagStartEnd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6n");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSASDiagStartEndCmd_t));
    /* build IOMB command and send to SPC */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagStartEndCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSASDiagStartEndCmd_t, OperationPhyId), ((phyId & SM_PHYID_MASK) | (operation << SHIFT8)));
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SAS_DIAG_MODE_START_END, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saSASDiagStartEnd: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saSASDiagStartEnd, sending IOMB failed\n" ));
    }
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6n");
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  return ret;
}

/******************************************************************************/
/*! \brief Initiate a GET TIME STAMP command
 *
 *  This function is called to initiate a Get Time Stamp command to the SPC.
 *  The completion of this function is reported in ossaGetTimeStampCB().
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param agContext   the context of this API
 *  \param queueNum    queue number
 *
 *  \return
 *          - SUCCESS or FAILURE
 */
/*******************************************************************************/
GLOBAL bit32 saGetTimeStamp(
                      agsaRoot_t        *agRoot,
                      agsaContext_t     *agContext,
                      bit32             queueNum
                      )
{
  agsaIORequestDesc_t   *pRequest;
  agsaGetTimeStampCmd_t payload;
  bit32                 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t          *saRoot;
  SA_ASSERT((agNULL != agRoot), "");
  if (agRoot == agNULL)
  {
    SA_DBG1(("saGetTimeStamp: agRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  if (saRoot == agNULL)
  {
    SA_DBG1(("saGetTimeStamp: saRoot == agNULL\n"));
    return AGSA_RC_FAILURE;
  }

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6o");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  SA_DBG3(("saGetTimeStamp: agContext %p\n", agContext));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGetTimeStamp, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6o");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    /* build IOMB command and send to SPC */
    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGetTimeStampCmd_t));

    /* set tag */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetTimeStampCmd_t, tag), pRequest->HTag);

    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_TIME_STAMP, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
      {
        SA_DBG1(("saGetTimeStamp: saving pRequest (%p) for later use\n", pRequest));
        saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
      }
      else
      {
        /* return the request to free pool */
        saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      }
      SA_DBG1(("saGetTimeStamp, sending IOMB failed\n" ));
    }
  }

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6o");

  return ret;
}

/******************************************************************************/
/*! \brief Update IOMap Entry  
 *
 *  This function is called to update certain fields of IOMap Entry
 * 
 *  \param pIOMap       IOMap Entry
 *  \param HTag         Host Tag
 *  \param pRequest     Request
 *  \parma agContext    Context of this API
 *
 *  \return             NA
 */         
/*******************************************************************************/
static void saUpdateIOMap(
                        agsaIOMap_t         *pIOMap,
                        bit32               HTag,
                        agsaIORequestDesc_t *pRequest,
                        agsaContext_t       *agContext
                        )
{
  pIOMap->Tag = HTag;
  pIOMap->IORequest = (void *)pRequest;
  pIOMap->agContext = agContext;
}

/******************************************************************************/
/*! \brief Get a request from free pool
 *
 *  This function gets a request from free pool
 * 
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param agsaContext  Context of this command
 *
 *  \return
 *          - \e Pointer to request, in case of success
 *          - \e NULL, in case of failure 
 *
 */
/*******************************************************************************/
agsaIORequestDesc_t* saGetRequestFromFreePool(
                                            agsaRoot_t      *agRoot,
                                            agsaContext_t   *agContext
                                            )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest = agNULL;
  
  /* Acquire LL_IOREQ_LOCKEQ_LOCK */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));
  if (pRequest != agNULL)
  {
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));

    /* Release LL_IOREQ_LOCKEQ_LOCK */
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* Add the request to IOMap */
    saUpdateIOMap(&saRoot->IOMap[pRequest->HTag], pRequest->HTag, pRequest, agContext);
    pRequest->valid = agTRUE;
  }
  else
  {
    /* Release LL_IOREQ_LOCKEQ_LOCK */
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  }
  
  return pRequest;
}

/******************************************************************************/
/*! \brief Return request to free pool
 *
 *  This function returns the request to free pool
 * 
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param pRequest     Request to be returned
 *
 *  \return             NA             
 *
 */
/*******************************************************************************/
void saReturnRequestToFreePool(
                            agsaRoot_t          *agRoot,
                            agsaIORequestDesc_t *pRequest
                            )
{
  agsaLLRoot_t *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  SA_ASSERT((pRequest->valid), "pRequest->valid");

  /* Remove the request from IOMap */
  saUpdateIOMap(&saRoot->IOMap[pRequest->HTag], MARK_OFF, agNULL, agNULL);
  pRequest->valid = agFALSE;
  
  /* Acquire LL_IOREQ_LOCKEQ_LOCK */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  if (saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("saReturnRequestToFreePool: saving pRequest (%p) for later use\n", pRequest));	
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* Return the request to free pool */      
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  
  /* Release LL_IOREQ_LOCKEQ_LOCK */
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
}
/******************************************************************************/
/*! \brief Initiate a serial GPIO command
 *
 *  This function is called to initiate a serial GPIO command to the SPC.
 *  The completion of this function is reported in ossaSgpioCB().
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param agContext   the context of this API
 *  \param queueNum    queue number
 *  \param pSGpioReq   Pointer to the serial GPIO fields
 *
 *  \return 
 *          - SUCCESS or FAILURE
 */         
/*******************************************************************************/
GLOBAL bit32 saSgpio(
                agsaRoot_t              *agRoot,
                agsaContext_t           *agContext,
                bit32                   queueNum,
                agsaSGpioReqResponse_t  *pSGpioReq
                )
{
  bit32                 i;
  agsaIORequestDesc_t   *pRequest = agNULL;
  agsaSGpioCmd_t        payload = {0};
  bit32                 ret = AGSA_RC_BUSY;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6t");

  /* Sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  SA_DBG3(("saSgpio: agContext %p\n", agContext));

  /* Get request from free pool */
  pRequest = saGetRequestFromFreePool(agRoot, agContext);
  if (agNULL == pRequest)
  {
    SA_DBG1(("saSgpio, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6t");
  }
  else
  {
    /* Set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSGpioCmd_t));
	
    /* set tag */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSGpioCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSGpioCmd_t, regIndexRegTypeFunctionFrameType),
                        (pSGpioReq->smpFrameType | 
                        ((bit32)pSGpioReq->function << 8)  |
                        ((bit32)pSGpioReq->registerType << 16) |
                        ((bit32)pSGpioReq->registerIndex << 24)));
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSGpioCmd_t, regCount), pSGpioReq->registerCount);
	
    if (SA_SAS_SMP_WRITE_GPIO_REGISTER == pSGpioReq->function)
    {
      for (i = 0; i < pSGpioReq->registerCount; i++)
      {
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSGpioCmd_t, writeData) + (i * 4), pSGpioReq->readWriteData[i]);
      }
    }

    /* Build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SGPIO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      /* Return the request to free pool */
      saReturnRequestToFreePool(agRoot, pRequest);
      SA_DBG1(("saSgpio, sending IOMB failed\n" ));
    }

    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6t");
  }

  return ret;
}

/******************************************************************************/
/*! \brief for spc card read Error Registers to memory if error occur
 *
 *  This function is called to get erorr registers content to memory if error occur.
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *
 *  \return
 */
/*******************************************************************************/
LOCAL void siSpcGetErrorContent(
                                agsaRoot_t *agRoot
                               )
{

  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32       value, value1;

  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1) & SCRATCH_PAD_STATE_MASK;
  value1 = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_2, MSGU_SCRATCH_PAD_2) & SCRATCH_PAD_STATE_MASK;
      /* check AAP error */
  if ((SCRATCH_PAD1_ERR == value) || (SCRATCH_PAD2_ERR == value1))
  {
        /* fatal error */
        /* get register dump from GSM and save it to LL local memory */
      siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump0[0],
           REG_DUMP_NUM0, 0, saRoot->mainConfigTable.FatalErrorDumpLength0);
      siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump1[0],
           REG_DUMP_NUM1, 0, saRoot->mainConfigTable.FatalErrorDumpLength1);
  }
}


/******************************************************************************/
/*! \brief for spcv card read Error Registers to memory if error occur
 *
 *  This function is called to get erorr registers content to memory if error occur.
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *
 *  \return
 */
/*******************************************************************************/
LOCAL void siSpcvGetErrorContent(
                                 agsaRoot_t *agRoot
                                 )
{

  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32                 value;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2d");
  value = siHalRegReadExt(agRoot, GEN_MSGU_SCRATCH_PAD_1, MSGU_SCRATCH_PAD_1);

  if(((value & SPCV_RAAE_STATE_MASK) == SPCV_ERROR_VALUE) ||
     ((value & SPCV_IOP0_STATE_MASK) == SPCV_ERROR_VALUE) ||
     ((value & SPCV_IOP1_STATE_MASK) == SPCV_ERROR_VALUE)
    )
  {
        /* fatal error */
        /* get register dump from GSM and save it to LL local memory */
    siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump0[0],
       REG_DUMP_NUM0, 0, saRoot->mainConfigTable.FatalErrorDumpLength0);
    siGetRegisterDumpGSM(agRoot, (void *)&saRoot->registerDump1[0],
       REG_DUMP_NUM1, 0, saRoot->mainConfigTable.FatalErrorDumpLength1);
  }
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2d");
}

#define LEFT_BYTE_FAIL(x, v)   \
     do {if( (x) < (v) ) return AGSA_RC_FAILURE; } while(0);

LOCAL bit32 siDumpInboundQueue(
          void *  buffer,
          bit32   length,
          mpiICQueue_t  *q
          )
{
  bit8  * _buf = buffer;
  si_memcpy( _buf, (bit8*)(q->memoryRegion.virtPtr) + length, 128*256);
  return AGSA_RC_SUCCESS;
}

LOCAL bit32 siDumpOutboundQueue(
          void *  buffer,
          bit32   length,
          mpiOCQueue_t  *q)
{
  bit8  * _buf   = buffer;
  si_memcpy( _buf, (bit8*)(q->memoryRegion.virtPtr) + length, 128*256);
  return AGSA_RC_SUCCESS;
}


LOCAL bit32 siWaitForNonFatalTransfer( agsaRoot_t *agRoot,bit32 pcibar)
{
  bit32 status = AGSA_RC_SUCCESS;
  bit32 ready;
  bit32 max_wait_time;
  bit32 max_wait_count;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2c");

  SA_DBG4(("siWaitForNonFatalTransfer:0 IBDBS 0x%x\n",ossaHwRegReadExt(agRoot,0 ,V_Inbound_Doorbell_Set_Register ) ));
  /* Write FDDHSHK  */


  /* Write bit7 of inbound doorbell set register  step 3 */
  ossaHwRegWriteExt(agRoot, 0,V_Inbound_Doorbell_Set_Register, SPCV_MSGU_CFG_TABLE_TRANSFER_DEBUG_INFO );
  SA_DBG4(("siWaitForNonFatalTransfer:1 IBDBS 0x%x\n",ossaHwRegReadExt(agRoot,0 ,V_Inbound_Doorbell_Set_Register ) ));

  /* Poll bit7 of inbound doorbell set register until clear step 4 */
  max_wait_time = (2000 * 1000); /* wait 2 seconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    ready = ossaHwRegReadExt(agRoot,0 ,V_Inbound_Doorbell_Set_Register );
  } while ( (ready & SPCV_MSGU_CFG_TABLE_TRANSFER_DEBUG_INFO)  && (max_wait_count -= WAIT_INCREMENT));
  if(max_wait_count == 0)
  {
    SA_DBG1(("siWaitForNonFatalTransfer:Timeout IBDBS 0x%x\n",ossaHwRegReadExt(agRoot,0 ,V_Inbound_Doorbell_Set_Register ) ));
    status = AGSA_RC_FAILURE;
  }

  SA_DBG4(("siWaitForNonFatalTransfer:3 IBDBS 0x%x\n",ossaHwRegReadExt(agRoot,0 ,V_Inbound_Doorbell_Set_Register ) ));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2c");
  return(status);
}

LOCAL bit32 siWaitForFatalTransfer( agsaRoot_t *agRoot,bit32 pcibar)
{
  bit32 status = AGSA_RC_SUCCESS;
  bit32 ready;
  bit32 ErrorTableOffset;
  bit32 max_wait_time;
  bit32 max_wait_count;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2o");

  ErrorTableOffset = siGetTableOffset( agRoot, MAIN_MERRDCTO_MERRDCES );

  SA_DBG4(("siWaitForFatalTransfer: MPI_FATAL_EDUMP_TABLE_STATUS    Offset 0x%x 0x%x\n",ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS, ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS )));
  SA_DBG4(("siWaitForFatalTransfer: MPI_FATAL_EDUMP_TABLE_ACCUM_LEN Offset 0x%x 0x%x\n",ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_ACCUM_LEN, ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));
  /*
  2. Write 0x1 to the Fatal Error Debug Dump Handshake control [FDDHSHK] field in Table 73 and
  read back the same field (by polling) until it is 0. This prompts the debug agent to copy the next
  part of the debug data into GSM shared memory. To check the completion of the copy process, the
  host must poll the Fatal/Non Fatal Debug Data Transfer Status [FDDTSTAT] field in the Table
  Table 73.
  */

  /* Write FDDHSHK  */
  ossaHwRegWriteExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_HANDSHAKE, MPI_FATAL_EDUMP_HANDSHAKE_RDY );
  SA_DBG4(("siWaitForFatalTransfer:1 MPI_FATAL_EDUMP_TABLE_HANDSHAKE 0x%x\n",ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_HANDSHAKE ) ));

  /* Poll FDDHSHK  until clear  */
  max_wait_time = (2000 * 1000); /* wait 2 seconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    ready = ossaHwRegReadExt(agRoot,0 ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_HANDSHAKE );
  } while (ready   && (max_wait_count -= WAIT_INCREMENT));
  if(max_wait_count == 0)
  {
    SA_DBG1(("siWaitForFatalTransfer : 1 Timeout\n"));
    status = AGSA_RC_FAILURE;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2o");
  return(status);
}



LOCAL bit32 siFatalErrorBuffer(
                  agsaRoot_t *agRoot,
                  agsaForensicData_t *forensicData
                  )
{
  bit32 status = AGSA_RC_FAILURE;
  bit32 pcibar;
  bit32 ErrorTableOffset;
  bit32 Accum_len = 0;

  agsaLLRoot_t      *saRoot;
  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT( (agNULL != saRoot), "saRoot");
  if(agNULL == saRoot )
  {
    SA_DBG1(("siFatalErrorBuffer: agNULL  saRoot\n"));
    return(status);
  }

  if(saRoot->ResetFailed )
  {
    SA_DBG1(("siFatalErrorBuffer: saRoot->ResetFailed\n"));
    return(status);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2a");
  SA_DBG2(("siFatalErrorBuffer:In %p Offset 0x%08x Len 0x%08x Totel len 0x%x\n",
                        forensicData->BufferType.dataBuf.directData,
                        forensicData->BufferType.dataBuf.directOffset,
                        forensicData->BufferType.dataBuf.directLen,
			forensicData->BufferType.dataBuf.readLen ));

  pcibar = siGetPciBar(agRoot);
  ErrorTableOffset = siGetTableOffset( agRoot, MAIN_MERRDCTO_MERRDCES );

  SA_DBG3(("siFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_STATUS  0x%x LEN 0x%x\n",
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS),
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN) ));

  /*
  This section describes sequence for the host to capture debug data under fatal error conditions.
  A fatal error is an error condition that stops the SPCv controller from normal operation and causes it
  to be unresponsive to host requests. Since the firmware is non-operational, the host needs to pull the
  debug dump information using PCIe MEMBASE II with the assistance of the debug agent which becomes
  active when the main controller firmware fails.
  */
  /*
  To capture the fatal error debug data, the host must:
  1. Upon detecting the fatal error condition through a fatal error interrupt or by the MSGU scratchpad
  registers, capture the first part of the fatal error debug data. Upon fatal error, the first part of the
  debug data is located GSM shared memory and its length is updated in the Accumulative Debug
  Data Length Transferred [ACCDDLEN] field in Table Table 82. To capture the first part:
  */
  if(forensicData->BufferType.dataBuf.directOffset == 0)
  {
    /* start to get data */
    /*
    a. Program the MEMBASE II Shifting Register with 0x00.
    */
    ossaHwRegWriteExt(agRoot, pcibar,V_MEMBASE_II_ShiftRegister, saRoot->FatalForensicShiftOffset); // set base to zero

    saRoot->ForensicLastOffset =0;
    saRoot->FatalForensicStep = 0;
    saRoot->FatalBarLoc = 0;
    saRoot->FatalForensicShiftOffset = 0;

    SA_DBG1(("siFatalErrorBuffer: directOffset zero SCRATCH_PAD1 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1) ));
  }

  /* Read until Accum_len is retrived */
  Accum_len = ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN);

  SA_DBG2(("siFatalErrorBuffer: Accum_len 0x%x\n", Accum_len));
  if(Accum_len == 0xFFFFFFFF)
  {
    SA_DBG1(("siFatalErrorBuffer: Possible PCI issue 0x%x not expected\n", Accum_len));
    return(status);
  }

  if( Accum_len == 0 || Accum_len >=0x100000 )
  {
    SA_DBG1(("siFatalErrorBuffer: Accum_len == saRoot->FatalCurrentLength 0x%x\n", Accum_len));
    return(IOCTL_ERROR_NO_FATAL_ERROR);
  }

  if(saRoot->FatalForensicStep == 0) /* PM Step 1a and 1b */
  {
    moreData:
	  if(forensicData->BufferType.dataBuf.directData)
	  {
      		  siPciCpyMem(agRoot,saRoot->FatalBarLoc ,forensicData->BufferType.dataBuf.directData,forensicData->BufferType.dataBuf.directLen ,1 );
	  }
	  saRoot->FatalBarLoc += forensicData->BufferType.dataBuf.directLen;
	  forensicData->BufferType.dataBuf.directOffset += forensicData->BufferType.dataBuf.directLen;
	  saRoot->ForensicLastOffset  += forensicData->BufferType.dataBuf.directLen;
	  forensicData->BufferType.dataBuf.readLen = forensicData->BufferType.dataBuf.directLen;

	  if(saRoot->ForensicLastOffset  >= Accum_len)
    {
      /*
      e. Repeat the above 2 steps until all debug data is retrieved as specified in the Accumulative Debug
      Data Length Transferred [ACCDDLEN] field.
      NOTE: The ACCDDLEN field is cumulative so the host needs to take the difference from the
      previous step.
      */
      /* This section data ends get next section */
      SA_DBG1(("siFatalErrorBuffer: Accum_len reached 0x%x directOffset 0x%x\n",Accum_len,forensicData->BufferType.dataBuf.directOffset ));
      saRoot->FatalBarLoc = 0;
      saRoot->FatalForensicStep = 1;
      saRoot->FatalForensicShiftOffset = 0;
		  status = AGSA_RC_COMPLETE;
		  return status;
    }
    if(saRoot->FatalBarLoc < (64*1024))
    {
      SA_DBG2(("siFatalErrorBuffer: In same 64k FatalBarLoc 0x%x\n",saRoot->FatalBarLoc ));
      status = AGSA_RC_SUCCESS;
		  return status;
    }
    /*
    c. Increment the MEMBASE II Shifting Register value by 0x100.
    */
    saRoot->FatalForensicShiftOffset+= 0x100;
    	  ossaHwRegWriteExt(agRoot, pcibar,V_MEMBASE_II_ShiftRegister, saRoot->FatalForensicShiftOffset);
    saRoot->FatalBarLoc = 0;

	  SA_DBG1(("siFatalErrorBuffer: Get next bar data 0x%x\n",saRoot->FatalForensicShiftOffset));

    status = AGSA_RC_SUCCESS;

	  SA_DBG1(("siFatalErrorBuffer:Offset 0x%x BarLoc 0x%x\n",saRoot->FatalForensicShiftOffset,saRoot->FatalBarLoc  )); 
	  SA_DBG1(("siFatalErrorBuffer: step 0 status %d %p Offset 0x%x Len 0x%x total_len 0x%x\n",
                        status,
                        forensicData->BufferType.dataBuf.directData,
				  forensicData->BufferType.dataBuf.directOffset, 
                        forensicData->BufferType.dataBuf.directLen,
				  forensicData->BufferType.dataBuf.readLen )); 
	  return(status);
  }

  if(saRoot->FatalForensicStep == 1)
  {

    /*
    3. If Fatal/Non Fatal Debug Data Transfer Status [FDDTSTAT] field indicates status value of
    0x00000002 or 0x00000003, read the next part of the fatal debug data by taking the difference
    between the preserved ACCDDLEN value from step 2 and the new ACCDDLEN value.To capture
    the second part:
    a. Program the MEMBASE II Shifting Register with 0x00.
    */
    SA_DBG1(("siFatalErrorBuffer: FatalForensicStep 1 Accum_len 0x%X MPI_FATAL_EDUMP_TABLE_ACCUM_LEN 0x%x\n",
                Accum_len,
                ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));

    saRoot->FatalForensicShiftOffset = 0; /* location in 64k region */
    /*
    b. Read 64K of the debug data.
    */
    ossaHwRegWriteExt(agRoot, pcibar,V_MEMBASE_II_ShiftRegister  ,saRoot->FatalForensicShiftOffset);
    SA_DBG1(("siFatalErrorBuffer: FatalForensicStep 1\n" ));
    /*
    2.Write 0x1 to the Fatal Error Debug Dump Handshake control [FDDHSHK]
    field inTable 82 and read back the same field (by polling for 2 seconds) until it is 0. This prompts
    the debug agent to copy the next part of the debug data into GSM shared memory. To check the
    completion of the copy process, the host must poll the Fatal/Non Fatal Debug Data Transfer Status
    [FDDTSTAT] field for 2 secondsin the MPI Fatal and Non-Fatal Error Dump Capture Table Table 82.
    */
    siWaitForFatalTransfer( agRoot,pcibar);

    /*
    d. Read the next 64K of the debug data.
    */
    saRoot->FatalForensicStep = 0;

    if( ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_STATUS) != MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE )
    {

      SA_DBG3(("siFatalErrorBuffer:Step 3\n" ));
      SA_DBG3(("siFatalErrorBuffer:Step 3 MPI_FATAL_EDUMP_TABLE_STATUS 0x%x\n", ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_STATUS )));
      /*
      2. Write FDDSTAT to 0x00000000 but preserve the Accumulative Debug Data Length Transferred
      [ACCDDLEN] field.
      */
      ossaHwRegWriteExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS, 0 );
      /*
      4. If FDDSTAT is 0x00000002, repeat steps 2 and 3 until you reach this step with FDDSTAT being
      equal to 0x00000003.
      */
      goto moreData;
    }
    else
    {
      /*
         When FDDSTAT equals 0x00000003 and ACCDDLEN is unchanged, then
      */
      /*
      the fatal error dump is complete. If ACCDDLEN increases, one more read step is required.
      The content and format of the debug data is opaque to the host and must be forwarded to PMC-Sierra
      Applications support for failure analysis. Debug data is retrieved in several iterations which enables
      the host to use a smaller buffer and store the captured debug data in secondary storage during the process.
      */

      SA_DBG3(("siFatalErrorBuffer:Step 4\n" ));
      SA_DBG1(("siFatalErrorBuffer:  Done  Read 0x%x accum 0x%x\n",
                forensicData->BufferType.dataBuf.directOffset,
                ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));

#if defined(SALLSDK_DEBUG)
      SA_DBG1(("siFatalErrorBuffer: SCRATCH_PAD1_V_ERROR_STATE 0x%x\n",SCRATCH_PAD1_V_ERROR_STATE( siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1) )));
      SA_DBG1(("siFatalErrorBuffer: SCRATCH_PAD0 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_0,  MSGU_SCRATCH_PAD_0)));
      SA_DBG1(("siFatalErrorBuffer: SCRATCH_PAD1 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_1,  MSGU_SCRATCH_PAD_1)));
      SA_DBG1(("siFatalErrorBuffer: SCRATCH_PAD2 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_2,  MSGU_SCRATCH_PAD_2)));
      SA_DBG1(("siFatalErrorBuffer: SCRATCH_PAD3 value = 0x%x\n", siHalRegReadExt(agRoot,GEN_MSGU_SCRATCH_PAD_3,  MSGU_SCRATCH_PAD_3)));
#endif
      forensicData->BufferType.dataBuf.readLen = 0xFFFFFFFF;
      status = AGSA_RC_SUCCESS;

    }
  }


  SA_DBG3(("siFatalErrorBuffer:status 0x%x %p directOffset 0x%x directLen 0x%x readLen 0x%x\n",
                        status,
                        forensicData->BufferType.dataBuf.directData,
                        forensicData->BufferType.dataBuf.directOffset,
                        forensicData->BufferType.dataBuf.directLen,
                        forensicData->BufferType.dataBuf.readLen )); 

      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2a");
  return(status);
}

LOCAL bit32 siNonFatalErrorBuffer(
              agsaRoot_t *agRoot,
              agsaForensicData_t *forensicData
              )
{
  bit32 status = AGSA_RC_FAILURE;
  bit32 pcibar;
  bit32 ErrorTableOffset;

  //bit32 i;
  bit32 ready;
  bit32 biggest;
  bit32 max_wait_time;
  bit32 max_wait_count;
  agsaLLRoot_t      *saRoot;
  /* sanity check */
  SA_ASSERT( (agNULL != agRoot), "agRoot");
  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT( (agNULL != saRoot), "saRoot");
  if(agNULL == saRoot )
  {
    SA_DBG1(("siNonFatalErrorBuffer: agNULL  saRoot\n"));
    return(status);
  }

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2b");
  pcibar = siGetPciBar(agRoot);
  ErrorTableOffset = siGetTableOffset( agRoot, MAIN_MERRDCTO_MERRDCES );

  SA_DBG4(("siNonFatalErrorBuffer: ErrorTableOffset 0x%x\n",ErrorTableOffset ));

  SA_DBG4(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_STATUS Offset 0x%x   0x%x\n",
            ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS,
            ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS)));
  SA_DBG4(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_ACCUM_LEN Offset 0x%x   0x%x\n",
            ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN,
            ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));

  biggest = saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].totalLength;

  if(biggest >= forensicData->BufferType.dataBuf.directLen )
  {
    biggest = forensicData->BufferType.dataBuf.directLen;
  }
  else
  {
    SA_DBG1(("siNonFatalErrorBuffer: directLen larger than DMA Buffer 0x%x < 0x%x\n",
              biggest, forensicData->BufferType.dataBuf.directLen));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2b");
    return(AGSA_RC_FAILURE);
  }

  if(saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].virtPtr)
  {
    si_memset(saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].virtPtr, 0, biggest);
  }
  else
  {
    SA_DBG1(("siNonFatalErrorBuffer: Error\n" ));
    return(AGSA_RC_FAILURE);
  }


  if(forensicData->BufferType.dataBuf.directOffset)
  {
    /* Write FDDSTAT and ACCDDLEN to zero step 2 */
    ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS, 0);
    goto skip_setup;
  }

  SA_DBG1(("siNonFatalErrorBuffer: %p Offset 0x%x Len 0x%x total_len 0x%x\n",
                        forensicData->BufferType.dataBuf.directData,
                        forensicData->BufferType.dataBuf.directOffset,
                        forensicData->BufferType.dataBuf.directLen,
                        forensicData->BufferType.dataBuf.readLen ));

  SA_DBG1(("siNonFatalErrorBuffer: directOffset zero setup\n" ));
  SA_DBG1(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_STATUS  0x%x LEN 0x%x\n",
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS),
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN) ));

  SA_DBG1(("siNonFatalErrorBuffer: Clear V_Scratchpad_Rsvd_0_Register 0x%x\n",
          ossaHwRegReadExt(agRoot, 0,V_Scratchpad_Rsvd_0_Register) ));
  ossaHwRegWriteExt(agRoot, 0,V_Scratchpad_Rsvd_0_Register ,0);

  saRoot->ForensicLastOffset = 0;

  /* WriteACCDDLEN  for error interface Step 0 */
  /*ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN ,0);*/

  /* Write DMA get Offset for error interface Step 1 */
  ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_LO_OFFSET, saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].phyAddrLower);
  ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_HI_OFFSET, saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].phyAddrUpper);
  ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_LENGTH, biggest);

  /* Write FDDSTAT and ACCDDLEN to zero step 2 */
  ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS, 0);
  ossaHwRegWriteExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN, 0);

  SA_DBG4(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_STATUS Offset 0x%x   0x%x\n",
           ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS,
           ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS )));
  SA_DBG4(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_ACCUM_LEN Offset 0x%x   0x%x\n",
           ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN,
           ossaHwRegReadExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));

  if( 0 != ossaHwRegReadExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN))
  {
    SA_DBG1(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_ACCUM_LEN  0x%x   0x%x\n",
             forensicData->BufferType.dataBuf.directOffset,
             ossaHwRegReadExt(agRoot, pcibar, ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN)));
  }
  skip_setup:

  if( saRoot->ForensicLastOffset == 0xFFFFFFFF)
  {
    forensicData->BufferType.dataBuf.readLen = 0xFFFFFFFF;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2b");
    return(AGSA_RC_SUCCESS);
  }


  /* Write bit7 of inbound doorbell set register and wait for complete step 3 and 4*/
  siWaitForNonFatalTransfer(agRoot,pcibar);

  SA_DBG3(("siNonFatalErrorBuffer: MPI_FATAL_EDUMP_TABLE_STATUS  0x%x LEN 0x%x\n",
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+MPI_FATAL_EDUMP_TABLE_STATUS),
      ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN) ));



  max_wait_time = (2000 * 1000); /* wait 2 seconds */
  max_wait_count = MAKE_MODULO(max_wait_time,WAIT_INCREMENT) - WAIT_INCREMENT;
  ready = ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS );
  do
  {
    ossaStallThread(agRoot, WAIT_INCREMENT);
    ready =  ossaHwRegReadExt(agRoot,pcibar ,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_STATUS );
    forensicData->BufferType.dataBuf.directOffset = ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset + MPI_FATAL_EDUMP_TABLE_ACCUM_LEN);
    if( ready == MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_MORE_DATA )
    {
      SA_DBG2(("siNonFatalErrorBuffer: More data available MPI_FATAL_EDUMP_TABLE_ACCUM_LEN 0x%x\n", ossaHwRegReadExt(agRoot,pcibar,ErrorTableOffset+ MPI_FATAL_EDUMP_TABLE_ACCUM_LEN) )); 
      break;
    }
  } while ( ready != MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE && (max_wait_count -= WAIT_INCREMENT));


  if(max_wait_count == 0 || ready == MPI_FATAL_EDUMP_TABLE_STAT_DMA_FAILED)
  {
    status = AGSA_RC_FAILURE;
    SA_DBG1(("siNonFatalErrorBuffer: timeout waiting ready\n"));
  }
  else
  {
    forensicData->BufferType.dataBuf.readLen = forensicData->BufferType.dataBuf.directOffset - saRoot->ForensicLastOffset;
    if( ready == MPI_FATAL_EDUMP_TABLE_STAT_NF_SUCCESS_DONE && forensicData->BufferType.dataBuf.readLen == 0)
    {
      SA_DBG1(("siNonFatalErrorBuffer:ready 0x%x readLen 0x%x\n",ready ,forensicData->BufferType.dataBuf.readLen));
      saRoot->ForensicLastOffset = 0xFFFFFFFF;
    }
    else
    {
      saRoot->ForensicLastOffset = forensicData->BufferType.dataBuf.directOffset;
    }

    if(forensicData->BufferType.dataBuf.directData )
    {
      si_memcpy(forensicData->BufferType.dataBuf.directData, saRoot->memoryAllocated.agMemory[HDA_DMA_BUFFER].virtPtr,biggest);
    }
    status = AGSA_RC_SUCCESS;
  }
  /* step 5 */
  SA_DBG3(("siNonFatalErrorBuffer: %p directOffset 0x%x directLen 0x%x readLen 0x%x\n",
                        forensicData->BufferType.dataBuf.directData,
                        forensicData->BufferType.dataBuf.directOffset,
                        forensicData->BufferType.dataBuf.directLen,
                        forensicData->BufferType.dataBuf.readLen ));
  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2b");
  return(status);
}


LOCAL bit32 siGetForensicData(
    agsaRoot_t         *agRoot,
    agsaContext_t      *agContext,
    agsaForensicData_t *forensicData
    )
{
  bit32 status = AGSA_RC_FAILURE;
	agsaLLRoot_t        *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2Z");

  if(forensicData->DataType == TYPE_GSM_SPACE)
	{
#define _1M 0x100000
		if( forensicData->BufferType.gsmBuf.directLen >= _1M )
  {
			return AGSA_RC_FAILURE;
		}

		if(forensicData->BufferType.dataBuf.readLen)
    {
			SA_DBG1(("siGetForensicData: Incorrect readLen 0x%08X\n", forensicData->BufferType.dataBuf.readLen));
			forensicData->BufferType.dataBuf.readLen = forensicData->BufferType.dataBuf.directLen;
		}
		if( forensicData->BufferType.dataBuf.directOffset >= ONE_MEGABYTE )
		{
			SA_DBG1(("siGSMDump:	total length > ONE_MEGABYTE  0x%x\n",forensicData->BufferType.dataBuf.directOffset));
			forensicData->BufferType.dataBuf.readLen = 0xFFFFFFFF;
			return(AGSA_RC_SUCCESS);
    }
		if(smIS_SPC(agRoot))
		{
    if( forensicData->BufferType.dataBuf.directLen >= SIXTYFOURKBYTE )
    {
      SA_DBG1(("siGetForensicData directLen too large !\n"));
      return AGSA_RC_FAILURE;
    }
    SA_DBG1(("siGetForensicData: TYPE_GSM_SPACE directLen 0x%X directOffset 0x%08X %p\n",
                  forensicData->BufferType.dataBuf.directLen,
                  forensicData->BufferType.dataBuf.directOffset,
                  forensicData->BufferType.dataBuf.directData ));


    /* Shift BAR4 original address */
    if (AGSA_RC_FAILURE == siBar4Shift(agRoot, BAR_SHIFT_GSM_OFFSET + forensicData->BufferType.dataBuf.directOffset))
    {
      SA_DBG1(("siGSMDump:Shift Bar4 to 0x%x failed\n", 0x0));
      return AGSA_RC_FAILURE;
    }

  
			//if( forensicData->BufferType.dataBuf.directOffset >= ONE_MEGABYTE )
			//{
			//SA_DBG1(("siGSMDump:  total length > ONE_MEGABYTE  0x%x\n",forensicData->BufferType.dataBuf.directOffset));
			//forensicData->BufferType.dataBuf.readLen = 0xFFFFFFFF;
			//return(AGSA_RC_SUCCESS); 
			//}
			forensicData->BufferType.gsmBuf.directOffset = 0;
    }
    status = siGSMDump( agRoot,
				forensicData->BufferType.gsmBuf.directOffset, 
				forensicData->BufferType.gsmBuf.directLen, 
				forensicData->BufferType.gsmBuf.directData );

    if(status == AGSA_RC_SUCCESS)
    {
      forensicData->BufferType.dataBuf.readLen = forensicData->BufferType.dataBuf.directLen;
    }

    if( forensicData->BufferType.dataBuf.directOffset == 0 )
    {
      SA_DBG1(("siGetForensicData: TYPE_GSM_SPACE readLen 0x%08X\n", forensicData->BufferType.dataBuf.readLen));
    }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2Z");

    return status;
  }
	else if(forensicData->DataType == TYPE_INBOUND_QUEUE )
  {
      mpiICQueue_t        *circularQ = NULL;
		SA_DBG2(("siGetForensicData: TYPE_INBOUND \n")); 

      if(forensicData->BufferType.queueBuf.queueIndex >=AGSA_MAX_INBOUND_Q )
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2Z");
        return AGSA_RC_FAILURE;
      }
      circularQ = &saRoot->inboundQueue[forensicData->BufferType.queueBuf.queueIndex];
      status = siDumpInboundQueue( forensicData->BufferType.queueBuf.directData,
                                 forensicData->BufferType.queueBuf.directLen,
                                 circularQ );
		smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2Z");
		return status;
    }
	else if(forensicData->DataType == TYPE_OUTBOUND_QUEUE )
	//else if( forensicData->BufferType.queueBuf.queueType == TYPE_OUTBOUND_QUEUE )
    {
      mpiOCQueue_t        *circularQ = NULL;
		SA_DBG2(("siGetForensicData: TYPE_OUTBOUND\n")); 

      if(forensicData->BufferType.queueBuf.queueIndex >= AGSA_MAX_OUTBOUND_Q )
      {
        smTraceFuncExit(hpDBG_VERY_LOUD, 'd', "2Z");
        return AGSA_RC_FAILURE;
      }

      circularQ = &saRoot->outboundQueue[forensicData->BufferType.queueBuf.queueIndex];
      status = siDumpOutboundQueue(forensicData->BufferType.queueBuf.directData,
                                 forensicData->BufferType.queueBuf.directLen,
                                 circularQ );
    smTraceFuncExit(hpDBG_VERY_LOUD, 'e', "2Z");

    return status;
  }
  else if(forensicData->DataType == TYPE_NON_FATAL  )
  {
		// if(smIS_SPCV(agRoot))
		// {
		SA_DBG2(("siGetForensicData:TYPE_NON_FATAL \n")); 
      status = siNonFatalErrorBuffer(agRoot,forensicData);
		// }
    smTraceFuncExit(hpDBG_VERY_LOUD, 'f', "2Z");
    return status;
  }
  else if(forensicData->DataType == TYPE_FATAL  )
  {
		// if(smIS_SPCV(agRoot))
		//{
		SA_DBG2(("siGetForensicData:TYPE_NON_FATAL \n")); 
      status = siFatalErrorBuffer(agRoot,forensicData );
		// }
		smTraceFuncExit(hpDBG_VERY_LOUD, 'g', "2Z");
		return status;
	}
	else
	{
		SA_DBG1(("siGetForensicData receive error parameter!\n"));
		smTraceFuncExit(hpDBG_VERY_LOUD, 'h', "2Z");
		return AGSA_RC_FAILURE;
	}
	smTraceFuncExit(hpDBG_VERY_LOUD, 'i', "2Z");

	return status;
}


//GLOBAL bit32 saGetForensicData(
bit32 saGetForensicData(
    agsaRoot_t         *agRoot,
    agsaContext_t      *agContext,
    agsaForensicData_t *forensicData
    )
{
  bit32 status;
  status = siGetForensicData(agRoot, agContext, forensicData);
  ossaGetForensicDataCB(agRoot, agContext, status, forensicData);
  return status;
}

bit32 saGetIOErrorStats(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32              flag
                         )
{
  agsaLLRoot_t  *saRoot = (agsaLLRoot_t*)agRoot->sdkData;
  bit32          status = AGSA_RC_SUCCESS;

  ossaGetIOErrorStatsCB(agRoot, agContext, status, &saRoot->IoErrorCount);

  if (flag)
  {
    /* clear IO error counter */
    si_memset(&saRoot->IoErrorCount, 0, sizeof(agsaIOErrorEventStats_t));
  }

  return status;
}

bit32 saGetIOEventStats(
                         agsaRoot_t        *agRoot,
                         agsaContext_t     *agContext,
                         bit32              flag
                         )
{
  agsaLLRoot_t  *saRoot = (agsaLLRoot_t*)agRoot->sdkData;
  bit32          status = AGSA_RC_SUCCESS;

  ossaGetIOEventStatsCB(agRoot, agContext, status, &saRoot->IoEventCount);

  if (flag)
  {
    /* clear IO event counter */
    si_memset(&saRoot->IoEventCount, 0, sizeof(agsaIOErrorEventStats_t));
  }

  return status;
}

/******************************************************************************/
/*! \brief Initiate a GET REGISTER DUMP command
 *
 *  This function is called to Get Register Dump from the SPC.
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param agContext   the context of this API
 *  \param queueNum    queue number
 *  \param regDumpInfo register dump information
 *
 *  \return
 *          - SUCCESS or FAILURE
 */
/*******************************************************************************/
//GLOBAL bit32 saGetRegisterDump(
bit32 saGetRegisterDump(
              agsaRoot_t        *agRoot,
              agsaContext_t     *agContext,
              bit32             queueNum,
              agsaRegDumpInfo_t *regDumpInfo
              )
{
  agsaLLRoot_t          *saRoot = agNULL;
  bit32                 ret = AGSA_RC_SUCCESS;
//  bit32                 value, value1;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6p");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");

  /* sanity check */
  SA_ASSERT((agNULL != regDumpInfo), "");

  SA_DBG3(("saGetRegisterDump: agContext %p\n", agContext));

  if (regDumpInfo->regDumpSrc > 3)
  {
    SA_DBG1(("saGetRegisterDump, regDumpSrc %d or regDumpNum %d invalid\n",
            regDumpInfo->regDumpNum, regDumpInfo->regDumpNum));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6p");
    /* CB error for Register Dump */
    ossaGetRegisterDumpCB(agRoot, agContext, OSSA_FAILURE);
    return AGSA_RC_FAILURE;
  }

  switch(regDumpInfo->regDumpSrc)
  {
  case REG_DUMP_NONFLASH:
    /*First 6 64k data from GSMDUMP, contains IOST and RB info*/
    if (regDumpInfo->regDumpNum == GET_IOST_RB_INFO)
    {
      regDumpInfo->regDumpOffset = regDumpInfo->regDumpOffset + 0;
      ret = siGSMDump(agRoot, regDumpInfo->regDumpOffset, regDumpInfo->directLen, regDumpInfo->directData);
      /* CB error for Register Dump */
      ossaGetRegisterDumpCB(agRoot, agContext, ret);
      return ret;
    }
    /* Last 1MB data from GSMDUMP, contains GSM_SM info*/

    if (regDumpInfo->regDumpNum == GET_GSM_SM_INFO)
    {
      /* GSM_SM - total 1 Mbytes */
      bit32    offset;
      if(smIS_SPC(agRoot))
      {
        offset = regDumpInfo->regDumpOffset + SPC_GSM_SM_OFFSET;
      }else if(smIS_SPCV(agRoot))
      {
        offset = regDumpInfo->regDumpOffset + SPCV_GSM_SM_OFFSET;
      } else
      {
        SA_DBG1(("saGetRegisterDump: the device type is not support\n"));
        return AGSA_RC_FAILURE;
      }

      ret = siGSMDump(agRoot, offset, regDumpInfo->directLen, regDumpInfo->directData);
      /* CB error for Register Dump */
      ossaGetRegisterDumpCB(agRoot, agContext, ret);
      return ret;
    }

    /* check fatal errors */
    if(smIS_SPC(agRoot)) {
      siSpcGetErrorContent(agRoot);
    }
    else if(smIS_SPCV(agRoot)) {
      siSpcvGetErrorContent(agRoot);
    }
    /* Then read from local copy */
    if (regDumpInfo->directLen > REGISTER_DUMP_BUFF_SIZE)
    {
      SA_DBG1(("saGetRegisterDump, Request too many bytes %d\n",
              regDumpInfo->directLen));
      regDumpInfo->directLen = REGISTER_DUMP_BUFF_SIZE;
    }

    if (regDumpInfo->regDumpNum == 0)
    {
      /* Copy the LL Local register dump0 data to the destination */
      si_memcpy(regDumpInfo->directData, (bit8 *)&saRoot->registerDump0[0] +
                regDumpInfo->regDumpOffset, regDumpInfo->directLen);
    }
    else if( regDumpInfo->regDumpNum == 1)
    {
      /* Copy the LL Local register dump1 data to the destination */
      si_memcpy(regDumpInfo->directData, (bit8 *)&saRoot->registerDump1[0] +
                regDumpInfo->regDumpOffset, regDumpInfo->directLen);
    } else {
      SA_DBG1(("saGetRegisterDump, the regDumpNum value is wrong %x\n",
              regDumpInfo->regDumpNum));
    }

    /* CB for Register Dump */
    ossaGetRegisterDumpCB(agRoot, agContext, OSSA_SUCCESS);
    break;

  case REG_DUMP_FLASH:
    /* build IOMB command and send to SPC */
    ret = mpiNVMReadRegDumpCmd(agRoot, agContext, queueNum,
                            regDumpInfo->regDumpNum,
                            regDumpInfo->regDumpOffset,
                            regDumpInfo->indirectAddrUpper32,
                            regDumpInfo->indirectAddrLower32,
                            regDumpInfo->indirectLen);

    break;

  default:
    break;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6p");

  return ret;
}

/******************************************************************************/
/*! \brief Initiate a GET REGISTER DUMP from GSM command
 *
 *  This function is called to Get Register Dump from the GSM of SPC.
 *
 *  \param agRoot      handles for this instance of SAS/SATA hardware
 *  \param destinationAddress address of the register dump data copied to
 *  \param regDumpNum  Register Dump # 0 or 1
 *  \param regDumpOffset Offset within the register dump area
 *  \param len         Length in bytes of the register dump data to copy
 *
 *  \return
 *          - SUCCESS or FAILURE
 */
/*******************************************************************************/
//GLOBAL bit32 siGetRegisterDumpGSM(
bit32 siGetRegisterDumpGSM(
                        agsaRoot_t        *agRoot,
                        void              *destinationAddress,
                        bit32             regDumpNum,
                        bit32             regDumpOffset,
                        bit32             len
                        )
{
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  bit32                 ret = AGSA_RC_SUCCESS;
  bit32                 rDumpOffset, rDumpLen; //, rDumpValue;
  bit8                  *dst;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2V");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  dst = (bit8 *)destinationAddress;

  if (regDumpNum > 1)
  {
    SA_DBG1(("siGetRegisterDump, regDumpNum %d is invalid\n", regDumpNum));
    return AGSA_RC_FAILURE;
  }

  if (!regDumpNum)
  {
    rDumpOffset = saRoot->mainConfigTable.FatalErrorDumpOffset0;
    rDumpLen = saRoot->mainConfigTable.FatalErrorDumpLength0;
  }
  else
  {
    rDumpOffset = saRoot->mainConfigTable.FatalErrorDumpOffset1;
    rDumpLen = saRoot->mainConfigTable.FatalErrorDumpLength1;
  }

  if (len > rDumpLen)
  {
    SA_DBG1(("siGetRegisterDump, Request too many bytes %d, rDumpLen %d\n", len, rDumpLen));
    len = rDumpLen;
  }

  if (regDumpOffset >= len)
  {
    SA_DBG1(("siGetRegisterDump, Offset is not within the area %d, regDumpOffset%d\n", rDumpLen, regDumpOffset));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2V");
    return AGSA_RC_FAILURE;
  }

  /* adjust length to dword boundary */
  if ((len % 4) > 0)
  {
    len = (len/4 + 1) * 4;
  }

  ret = siGSMDump(agRoot, rDumpOffset, len, dst);
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2V");

  return ret;
}

/******************************************************************************/
/*! \brief SPC Get NVMD Command
 *
 *  This command sends GET_NVMD_DATA Command to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param agContext    Context of SPC FW Flash Update Command
 *  \param queueNum     Inbound/outbound queue number
 *  \param NVMDInfo     Pointer of NVM Device information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
//GLOBAL bit32 saGetNVMDCommand(
bit32 saGetNVMDCommand(
  agsaRoot_t                *agRoot,
  agsaContext_t             *agContext,
  bit32                     queueNum,
  agsaNVMDData_t            *NVMDInfo
  )
{
  bit32 ret           = AGSA_RC_SUCCESS;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* build IOMB command and send to SPC */
  ret = mpiGetNVMDCmd(agRoot, agContext, NVMDInfo, queueNum);

  return ret;
}

/******************************************************************************/
/*! \brief SPC Set NVMD Command
 *
 *  This command sends SET_NVMD_DATA Command to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param agContext    Context of SPC FW Flash Update Command
 *  \param queueNum     Inbound/outbound queue number
 *  \param NVMDInfo     Pointer of NVM Device information
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
//GLOBAL bit32 saSetNVMDCommand(
bit32 saSetNVMDCommand(
  agsaRoot_t                *agRoot,
  agsaContext_t             *agContext,
  bit32                     queueNum,
  agsaNVMDData_t            *NVMDInfo
  )
{
  bit32 ret           = AGSA_RC_SUCCESS;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  /* build IOMB command and send to SPC */
  ret = mpiSetNVMDCmd(agRoot, agContext, NVMDInfo, queueNum);

  return ret;
}


GLOBAL bit32 saSendSMPIoctl(
  agsaRoot_t                *agRoot,
  agsaDevHandle_t           *agDevHandle,
  bit32                      queueNum,
  agsaSMPFrame_t            *pSMPFrame,  
  ossaSMPCompletedCB_t       agCB
  )
{
  bit32 ret           = AGSA_RC_SUCCESS;
  //bit32 IR_IP_OV_res_phyId_DPdLen_res = 0;
  bit32 retVal;
  bit8                      inq, outq;
  agsaIORequestDesc_t       *pRequest;
  void                      *pMessage;
  bit8                      *payload_ptr;
  agsaDeviceDesc_t          *pDevice;
  bit8                      using_reserved = agFALSE;  
  agsaPort_t                *pPort;  
  mpiICQueue_t              *circularQ;
  agsaLLRoot_t              *saRoot = agNULL;
//  agsaDevHandle_t       	*agDevHandle;

  saRoot = (agsaLLRoot_t *)(agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");


  
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

  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);



  
  SA_ASSERT((agNULL != agDevHandle), "");
  /* Find the outgoing port for the device */
  if (agNULL == agDevHandle->sdkData)
  {
	/* Device has been removed */
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
	SA_DBG1(("saSMPStart, Device has been removed. agDevHandle=%p\n", agDevHandle));
	smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "9a");
	return AGSA_RC_FAILURE;
  }
	  
  pDevice = (agsaDeviceDesc_t *) (agDevHandle->sdkData);
	 
  pPort = pDevice->pPort;
	

	
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
	  pRequest->valid			  = agTRUE;
	  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
	
	  /* set up pRequest */
	  pRequest->pIORequestContext = (agsaIORequest_t *)pRequest;
	  pRequest->pDevice 		  = pDevice;
	  pRequest->pPort			  = pPort;		
	  pRequest->startTick		  = saRoot->timeTick;
	  pRequest->completionCB	  = (ossaSSPCompletedCB_t)agCB;
	  pRequest->requestType		  = AGSA_SMP_IOCTL_REQUEST;
	
	  /* Set request to the sdkData of agIORequest */
	 // agIORequest->sdkData		  = pRequest;
	
	  /* save tag to IOMap */
	  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
	  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
	
#ifdef SA_LL_IBQ_PROTECT
	  ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
	
	  /* If LL IO request entry avaliable */
	  /* Get a free inbound queue entry */
	  circularQ = &saRoot->inboundQueue[inq];
	  retVal	= mpiMsgFreeGet(circularQ, IOMB_SIZE64, &pMessage);
	 
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
#ifdef SA_LL_IBQ_PROTECT
			  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */


	if(smIS_SPC(agRoot))
	{
	 agsaSMPCmd_t payload;
  

		  bit32 IR_IP_OV_res_phyId_DPdLen_res = 0;
		  /* Prepare the payload of IOMB */
		  si_memset(&payload, 0, sizeof(agsaSMPCmd_V_t));
		  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, tag), pRequest->HTag);
		  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, deviceId), pDevice->DeviceMapIndex);



		  /*Indirect request and response*/
		  if (smpFrameFlagIndirectResponse & pSMPFrame->flag && smpFrameFlagIndirectPayload & pSMPFrame->flag) /* */
		  {
  
			SA_DBG2(("saSMPStart:V Indirect payload and indirect response\n"));
  
			/* Indirect Response mode */
			pRequest->IRmode = INDIRECT_MODE;
			IR_IP_OV_res_phyId_DPdLen_res = 3;
  
  
			/* payload */
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[4]), (pSMPFrame->outFrameAddrLower32));
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[5]), (pSMPFrame->outFrameAddrUpper32));
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[6]), (pSMPFrame->outFrameLen));
			
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[8]), (pSMPFrame->inFrameAddrLower32));
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[9]), (pSMPFrame->inFrameAddrUpper32));
			OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, SMPCmd[10]), (pSMPFrame->inFrameLen));
			
		  }
		  

		  IR_IP_OV_res_phyId_DPdLen_res |= (pSMPFrame->flag & 3);
		  /* fatal error if missing */
		  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSMPCmd_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);
		  /* fatal error if missing */
  
			  
		/* check IR bit */
  
		/* Build IOMB command and send it to SPC */
		payload_ptr = (bit8 *)&payload;
#ifdef SA_LL_IBQ_PROTECT
				ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

		ret = mpiSMPCmd(agRoot, pMessage, OPC_INB_SMP_REQUEST, (agsaSMPCmd_t *)payload_ptr, inq, outq);

#ifdef SA_LL_IBQ_PROTECT
			  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

		
  }
	else /* IOMB is different for SPCV SMP */
	{
	 agsaSMPCmd_V_t vpayload;
  

		  bit32 IR_IP_OV_res_phyId_DPdLen_res = 0;
		  /* Prepare the payload of IOMB */
		  si_memset(&vpayload, 0, sizeof(agsaSMPCmd_V_t));
		  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, tag), pRequest->HTag);
		  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, deviceId), pDevice->DeviceMapIndex);
		  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,SMPHDR ), *((bit32*)pSMPFrame->outFrameBuf+0) );

		  /*Indirect request and response*/
		  if (smpFrameFlagIndirectResponse & pSMPFrame->flag && smpFrameFlagIndirectPayload & pSMPFrame->flag) /* */
		  {
  
			SA_DBG2(("saSMPStart:V Indirect payload and indirect response\n"));
  
			/* Indirect Response mode */
			pRequest->IRmode = INDIRECT_MODE;
			IR_IP_OV_res_phyId_DPdLen_res = 3;
  
  
			/* payload */
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirL_SMPRF15_12 ), (pSMPFrame->outFrameAddrLower32));
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirH_or_SMPRF19_16 ), (pSMPFrame->outFrameAddrUpper32));
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,IndirLen_or_SMPRF23_20 ), (pSMPFrame->outFrameLen));
			
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRAL_or_SMPRF31_28), (pSMPFrame->inFrameAddrLower32));
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRAH_or_SMPRF35_32), (pSMPFrame->inFrameAddrUpper32));
			OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t,ISRL_or_SMPRF39_36), (pSMPFrame->inFrameLen));
			
		  }
		  
		  /*Direct request and indirect response*/
		  else if (smpFrameFlagIndirectResponse & pSMPFrame->flag ) /* */
		  {
  
  			SA_DBG2(("saSMPStart:V Direct payload and indirect response\n"));
			IR_IP_OV_res_phyId_DPdLen_res = (pSMPFrame->outFrameLen << SHIFT16) | pSMPFrame->flag;
  
			
			  /* Write IR_IP_OV_res_phyId_DPdLen_res field in the payload*/  
			  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);
			  /* setup indirect response frame address */
			  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAL_or_SMPRF31_28 ), (pSMPFrame->inFrameAddrLower32));
			  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRAH_or_SMPRF35_32 ), (pSMPFrame->inFrameAddrUpper32));
			  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, ISRL_or_SMPRF39_36 ), (pSMPFrame->inFrameLen));		 
			
		  }
		  IR_IP_OV_res_phyId_DPdLen_res |= (pSMPFrame->flag & 3);
		  /* fatal error if missing */
		  OSSA_WRITE_LE_32(agRoot, &vpayload, OSSA_OFFSET_OF(agsaSMPCmd_V_t, IR_IP_OV_res_phyId_DPdLen_res), IR_IP_OV_res_phyId_DPdLen_res);
		  /* fatal error if missing */
  
			  
		/* check IR bit */
  
#ifdef SA_LL_IBQ_PROTECT
				ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
		/* Build IOMB command and send it to SPCv */
		payload_ptr = (bit8 *)&vpayload;
		ret = mpiSMPCmd(agRoot, pMessage, OPC_INB_SMP_REQUEST, (agsaSMPCmd_t *)payload_ptr, inq, outq);

#ifdef SA_LL_IBQ_PROTECT
			  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

		
  }
  

  return ret;
}


/******************************************************************************/
/*! \brief Reconfiguration of SAS Parameters Command
 *
 *  This command Reconfigure the SAS parameters to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LL
 *  \param agContext    Context of SPC FW Flash Update Command
 *  \param queueNum     Inbound/outbound queue number
 *  \param agSASConfig  Pointer of SAS Configuration Parameters
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
//GLOBAL bit32 saReconfigSASParams(
bit32 saReconfigSASParams(
  agsaRoot_t        *agRoot,
  agsaContext_t     *agContext,
  bit32             queueNum ,
  agsaSASReconfig_t *agSASConfig
  )
{
  bit32 ret           = AGSA_RC_SUCCESS;

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  if(smIS_SPCV(agRoot))
  {
    SA_DBG1(("saReconfigSASParams: AGSA_RC_FAILURE for SPCv\n" ));
    return(AGSA_RC_FAILURE);
  }

  /* build IOMB command and send to SPC */
  ret = mpiSasReinitializeCmd(agRoot, agContext, agSASConfig, queueNum);

  return ret;
}

/******************************************************************************/
/*! \brief Dump GSM registers from the controller
 *
 *  \param agRoot         Handles for this instance of SAS/SATA hardware
 *  \param gsmDumpOffset  Offset of GSM
 *  \param length         Max is 1 MB
 *  \param directData     address of GSM data dump to
 *
 *  \return
 *          - \e AGSA_RC_SUCCESS saGSMDump is successfully
 *          - \e AGSA_RC_FAILURE saGSMDump is not successfully
 *
 */
/*******************************************************************************/
//LOCAL bit32 siGSMDump(
bit32 siGSMDump(
  agsaRoot_t     *agRoot,
  bit32          gsmDumpOffset,
  bit32          length,
  void           *directData)
{
  bit8  *dst;
  bit32 value, rem, offset = 0;
  bit32 i, workOffset, dwLength;
  bit32 bar = 0;

  SA_DBG1(("siGSMDump: gsmDumpOffset 0x%x length 0x%x\n", gsmDumpOffset, length));

  /* check max is 64k chunks */
  if (length > (64 * 1024))  
  {
    SA_DBG1(("siGSMDump: Max length is greater than 64K  bytes 0x%x\n", length));
    return AGSA_RC_FAILURE;
  }

  if (gsmDumpOffset & 3)
  {
    SA_DBG1(("siGSMDump: Not allow NON_DW Boundary 0x%x\n", gsmDumpOffset));
    return AGSA_RC_FAILURE;
  }

  if ((gsmDumpOffset + length) > ONE_MEGABYTE)
  {
    SA_DBG1(("siGSMDump: Out of GSM end address boundary 0x%x\n", (gsmDumpOffset+length)));
    return AGSA_RC_FAILURE;
  }

  if( smIS_SPCV(agRoot))
  {
    bar = PCIBAR1;
  }
  else if( smIS_SPC(agRoot))
  {
    bar = PCIBAR2;
  }
  else
  {
    SA_DBG1(("siGSMDump: device type is not supported"));
    return AGSA_RC_FAILURE;
  }

  workOffset = gsmDumpOffset & 0xFFFF0000;
  offset = gsmDumpOffset & 0x0000FFFF;
  gsmDumpOffset = workOffset;

  dst = (bit8 *)directData;

  /* adjust length to dword boundary */
  rem = length & 3;
  dwLength = length >> 2;

  for (i =0; i < dwLength; i++)
  {
    if((workOffset + offset) > length )
    {
      break;
    }
    value = ossaHwRegReadExt(agRoot, bar, (workOffset + offset) & 0x0000FFFF);
    /* xfr for dw */
    si_memcpy(dst, &value, 4);
    dst += 4;
    offset += 4;
  }

  if (rem != 0)
  {
    value = ossaHwRegReadExt(agRoot, bar, (workOffset + offset) & 0x0000FFFF);
    /* xfr for non_dw */
    if(dst)
    {
      si_memcpy(dst, &value, rem);
    }
  }

  /* Shift back to BAR4 original address */
  if (AGSA_RC_FAILURE == siBar4Shift(agRoot, 0x0))
  {
    SA_DBG1(("siGSMDump:Shift Bar4 to 0x%x failed\n", 0x0));
    return AGSA_RC_FAILURE;
  }

  return AGSA_RC_SUCCESS;
}

//GLOBAL bit32 saPCIeDiagExecute(
bit32 saPCIeDiagExecute(
            agsaRoot_t            *agRoot,
            agsaContext_t         *agContext,
            bit32                 queueNum,
            agsaPCIeDiagExecute_t *diag)
{
  bit32                    ret    = AGSA_RC_SUCCESS;
  agsaLLRoot_t            *saRoot = agNULL;
  agsaIORequestDesc_t     *pRequest;
  bit32  payload[32];

  smTraceFuncEnter(hpDBG_VERY_LOUD,"6r");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");

  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  /* sanity check */
  SA_ASSERT((agNULL != saRoot), "");
  SA_ASSERT((agNULL != diag), "");

  if(diag->len == 0)
  {
    SA_DBG1(("saPCIeDiagExecute,  diag->len Zero\n"));
  }
  SA_DBG1(("saPCIeDiagExecute, diag->command  0x%X\n", diag->command ));
  SA_DBG1(("saPCIeDiagExecute, diag->flags  0x%X\n",diag->flags ));
  SA_DBG1(("saPCIeDiagExecute,  diag->initialIOSeed  0x%X\n", diag->initialIOSeed));
  SA_DBG1(("saPCIeDiagExecute, diag->reserved   0x%X\n",diag->reserved ));
  SA_DBG1(("saPCIeDiagExecute, diag->rdAddrLower   0x%X\n", diag->rdAddrLower));
  SA_DBG1(("saPCIeDiagExecute, diag->rdAddrUpper   0x%X\n", diag->rdAddrUpper ));
  SA_DBG1(("saPCIeDiagExecute, diag->wrAddrLower   0x%X\n", diag->wrAddrLower));
  SA_DBG1(("saPCIeDiagExecute, diag->wrAddrUpper   0x%X\n",diag->wrAddrUpper ));
  SA_DBG1(("saPCIeDiagExecute,  diag->len   0x%X\n",diag->len  ));
  SA_DBG1(("saPCIeDiagExecute, diag->pattern  0x%X\n",diag->pattern ));
  SA_DBG1(("saPCIeDiagExecute, %02X %02X %02X %02X %02X %02X\n",
                  diag->udtArray[0],
                  diag->udtArray[1],
                  diag->udtArray[2],
                  diag->udtArray[3],
                  diag->udtArray[4],
                  diag->udtArray[5] ));

   SA_DBG1(("saPCIeDiagExecute, %02X %02X %02X %02X %02X %02X\n",
                  diag->udrtArray[0],
                  diag->udrtArray[1],
                  diag->udrtArray[2],
                  diag->udrtArray[3],
                  diag->udrtArray[4],
                  diag->udrtArray[5]));


  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saPCIeDiagExecute, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "6r");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  /* Remove the request from free list */
  saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(payload));

  if(smIS_SPCV(agRoot))
  {
    bit32      UDTR1_UDT0 ,UDT5_UDT2,UDTR5_UDTR2;

    UDTR5_UDTR2 = (( diag->udrtArray[5] << SHIFT24) | (diag->udrtArray[4] << SHIFT16) | (diag->udrtArray[3] << SHIFT8) | diag->udrtArray[2]);
    UDT5_UDT2 =   ((  diag->udtArray[5] << SHIFT24) |  (diag->udtArray[4] << SHIFT16) |  (diag->udtArray[3] << SHIFT8) |  diag->udtArray[2]);
    UDTR1_UDT0 =  (( diag->udrtArray[1] << SHIFT24) | (diag->udrtArray[0] << SHIFT16) |  (diag->udtArray[1] << SHIFT8) |  diag->udtArray[0]);

    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, tag)        , pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, CmdTypeDesc), diag->command );
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, UUM_EDA)    , diag->flags);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, UDTR1_UDT0) , UDTR1_UDT0);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, UDT5_UDT2)  , UDT5_UDT2);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, UDTR5_UDTR2), UDTR5_UDTR2);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, Res_IOS)    , diag->initialIOSeed);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, rdAddrLower), diag->rdAddrLower);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, rdAddrUpper), diag->rdAddrUpper);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, wrAddrLower), diag->wrAddrLower);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, wrAddrUpper), diag->wrAddrUpper);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, len),         diag->len);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPCIeDiagExecuteCmd_t, pattern),     diag->pattern);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_PCIE_DIAG_EXECUTE, IOMB_SIZE128, queueNum);
  }
  else
  {
    /* build IOMB command and send to SPC */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, tag),         pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, CmdTypeDesc), diag->command );
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, rdAddrLower), diag->rdAddrLower);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, rdAddrUpper), diag->rdAddrUpper);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, wrAddrLower), diag->wrAddrLower);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, wrAddrUpper), diag->wrAddrUpper);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, len),         diag->len);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsa_SPC_PCIDiagExecuteCmd_t, pattern),     diag->pattern);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_PCIE_DIAG_EXECUTE, IOMB_SIZE64, queueNum);
  }

  if (AGSA_RC_SUCCESS != ret)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;

    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));

    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saPCIeDiagExecute, sending IOMB failed\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "6r");

    return ret;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "6r");
  return ret;
}

//GLOBAL bit32 saGetDFEData(
bit32 saGetDFEData(
                          agsaRoot_t     *agRoot,
                          agsaContext_t  *agContext,
                          bit32           queueNum,
                          bit32           interface,
                          bit32           laneNumber,
                          bit32           interations,
                          agsaSgl_t      *agSgl)
{
  bit32                    ret    = AGSA_RC_SUCCESS;
  agsaLLRoot_t            *saRoot = agNULL;
  agsaIORequestDesc_t     *pRequest = agNULL;
  bit32  payload[32];
  bit32 reserved_In_Ln;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2X");
  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");
  SA_ASSERT((agNULL != agSgl), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("saGetDFEData, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2X");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  /* Remove the request from free list */
  saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(payload));

  if(smIS_SPCV(agRoot))
  {
    reserved_In_Ln = ((interface & 0x1) << SHIFT7) | (laneNumber & 0x7F);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, tag)        , pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, reserved_In_Ln)        , reserved_In_Ln);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, MCNT)        , interations);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, Buf_AddrL)        , agSgl->sgLower);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, Buf_AddrH)        , agSgl->sgUpper);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, Buf_Len)        , agSgl->len);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDDEFDataCmd_t, E_reserved)        , agSgl->extReserved);
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_DFE_DATA, IOMB_SIZE128, queueNum);

  }
  else
  {
    /* SPC does not support this command */
    ret = AGSA_RC_FAILURE;
  }

  if (AGSA_RC_SUCCESS != ret)
  {
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("saPCIeDiagExecute, sending IOMB failed\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2X");
    return ret;
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'c', "2X");
  return ret;
}

