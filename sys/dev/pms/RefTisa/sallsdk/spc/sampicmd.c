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
/*! \file sampicmd.c
 *  \brief The file implements the functions of MPI Inbound IOMB/Command to SPC
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
#define siTraceFileID 'I'
#endif

/******************************************************************************/
/*! \brief SAS/SATA LL API ECHO Command
 *
 *  This command used to test that MPI between host and SPC IOP is operational.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param agContext    Context of SPC FW Flash Update Command
 *  \param queueNum     Inbound/outbound queue number
 *  \param echoPayload  Pointer of Echo payload of IOMB
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 saEchoCommand(
  agsaRoot_t            *agRoot,
  agsaContext_t         *agContext,
  bit32                 queueNum,
  void                  *echoPayload
)
{
  bit32 ret = AGSA_RC_SUCCESS;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "xa");

  /* setup IOMB payload */
  ret = mpiEchoCmd(agRoot, queueNum, agContext, echoPayload);

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xa");

  return ret;
}

/******************************************************************************/
/*! \brief Build a IOMB command and send to SPC
 *
 *  Build an IOMB if there is a free message buffer and Send it to SPC
 *
 *  \param agRoot       Handles for this instance of SAS/SATA hardware
 *  \param payload      Pointer of payload in the IOMB
 *  \param category     Category of IOMB
 *  \param opcode       Opcode of IOMB
 *  \param size         Size of IOMB
 *  \param queueNum     Inbound/outbound queue number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 */
/*******************************************************************************/
GLOBAL bit32 mpiBuildCmd(
  agsaRoot_t        *agRoot,
  bit32             *payload,
  mpiMsgCategory_t  category,
  bit16             opcode,
  bit16             size,
  bit32             queueNum
  )
{
  agsaLLRoot_t      *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  mpiICQueue_t      *circularQ;
  void              *pMessage;
  bit32             ret = AGSA_RC_SUCCESS;
  bit32             retVal;
  bit8              inq, outq;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "xb");

  inq = (bit8)(queueNum & MPI_IB_NUM_MASK);
  outq = (bit8)((queueNum & MPI_OB_NUM_MASK) >> MPI_OB_SHIFT);
  SA_ASSERT((AGSA_MAX_INBOUND_Q > inq), "The IBQ Number is out of range.");
  SA_ASSERT((AGSA_MAX_OUTBOUND_Q > outq), "The OBQ Number is out of range.");

#ifdef SA_USE_MAX_Q
  outq = saRoot->QueueConfig.numOutboundQueues -1;
  SA_DBG1(("mpiBuildCmd, set OBQ to  %d\n",outq));
#endif /* SA_USE_MAX_Q */
  /* get a free inbound queue entry */

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

  circularQ = &saRoot->inboundQueue[inq];
  retVal    = mpiMsgFreeGet(circularQ, size, &pMessage);

  /* return FAILURE if error happened */
  if (AGSA_RC_FAILURE == retVal)
  {
#ifdef SA_LL_IBQ_PROTECT
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */
    /* the message size exceeds the inbound queue message size */
    SA_DBG1(("mpiBuildCmd, failure\n"));
    ret = AGSA_RC_FAILURE;
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xb");
    return ret;
  }

  /* return BUSY if no more inbound queue entry available */
  if (AGSA_RC_BUSY == retVal)
  {
    SA_DBG1(("mpiBuildCmd, no more IOMB\n"));
    ret = AGSA_RC_BUSY;
  }
  else
  {
    /* copy payload if it is necessary */
    if (agNULL != payload)
    {
      si_memcpy(pMessage, payload, (size - sizeof(mpiMsgHeader_t)));
    }

    /* post the message to SPC */
    if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pMessage, category, opcode, outq, (bit8)circularQ->priority))
    {
      ret = AGSA_RC_FAILURE;
    }
  }

#ifdef SA_LL_IBQ_PROTECT
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_IBQ0_LOCK + inq);
#endif /* SA_LL_IBQ_PROTECT */

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xb");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI ECHO Command
 *
 *  This command used to test that MPI between host and SPC IOP is operational.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param queueNum     Inbound/outbound queue number
 *  \param tag          Tag of this IOMB
 *  \param echoPayload  Pointer to the ECHO payload of inbound IOMB
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiEchoCmd(
  agsaRoot_t          *agRoot,
  bit32               queueNum,
  agsaContext_t       *agContext,
  void                *echoPayload
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaEchoCmd_t       payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "xc");

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("mpiEchoCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xc");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;

    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


    /* build IOMB command and send to SPC */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaEchoCmd_t, tag), pRequest->HTag);
    /* copy Echo payload */
    si_memcpy(&payload.payload[0], echoPayload, (sizeof(agsaEchoCmd_t) - 4));
    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_ECHO, IOMB_SIZE64, queueNum);
    SA_DBG3(("mpiEchoCmd, return value = %d\n", ret));
    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiEchoCmd, sending IOMB failed\n" ));
    }
#ifdef SALL_API_TEST
    else
    {
      saRoot->LLCounters.IOCounter.numEchoSent++;
    }
#endif

  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xc");
  return ret;
}


/******************************************************************************/
/*! \brief Get Phy Profile Command SPCv
 *
 *  This command is get # of phys and support speeds from SPCV.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agDevHandle  Handle of device
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/


GLOBAL bit32 mpiGetPhyProfileCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32                Operation,
  bit32                PhyId,
  void                *agCB
  )
{
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t     *pRequest;
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaGetPhyProfileCmd_V_t   payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD, "xd");

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistGetHead(&(saRoot->freeIORequests));

  SA_DBG1(("mpiGetPhyProfileCmd, Operation 0x%x PhyId %d \n",Operation ,PhyId ));

  /* If no LL Control request entry avalibale */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("mpiGetPhyProfileCmd, No request from free list\n" ));
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");

    /* Remove the request from free list */
    saLlistRemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;

    pRequest->valid = agTRUE;
    pRequest->completionCB  = agCB;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGetPhyProfileCmd_V_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetPhyProfileCmd_V_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetPhyProfileCmd_V_t, Reserved_Ppc_SOP_PHYID), (((Operation & 0xF) << SHIFT8 ) | (PhyId  & 0xFF) ) );
    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_PHY_PROFILE, IOMB_SIZE128, 0);
    if (AGSA_RC_SUCCESS != ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiGetPhyProfileCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiGetPhyProfileCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xd");
  /* return value */
  return ret;
}


GLOBAL bit32 mpiVHistCapCmd(
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
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t     *pRequest= agNULL;
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaGetVHistCap_V_t payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3C");
  SA_DBG1(("mpiVHistCapCmd\n"));

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistGetHead(&(saRoot->freeIORequests));
  /* If no LL Control request entry avalibale */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1((", No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3C");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    /* Remove the request from free list */
    saLlistRemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;

    pRequest->valid = agTRUE;
    pRequest->completionCB  = (void *)ossaGetPhyProfileCB;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGetVHistCap_V_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, tag),       pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, Channel),   Channel );
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, NumBitLo),  NumBitLo);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, NumBitHi),  NumBitHi);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, PcieAddrLo),PcieAddrLo);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, PcieAddrHi),PcieAddrHi);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetVHistCap_V_t, ByteCount), ByteCount );


    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_VHIST_CAP, IOMB_SIZE128,queueNum );
    if (AGSA_RC_SUCCESS != ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiVHistCapCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiVHistCapCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3C");
  /* return value */

  return(ret);
}

GLOBAL bit32 mpiSetPhyProfileCmd(
  agsaRoot_t    *agRoot,
  agsaContext_t *agContext,
  bit32         Operation,
  bit32         PhyId,
  bit32         length,
  void *        buffer
  )
{
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t     *pRequest;
  bit32                   ret = AGSA_RC_SUCCESS;
  bit32                   i;
  agsaSetPhyProfileCmd_V_t     payload;
  bit32               * PageData =(bit32 * )buffer;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2P");

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistGetHead(&(saRoot->freeIORequests));

  SA_DBG1(("mpiSetPhyProfileCmd, Operation 0x%x PhyId %d \n",Operation ,PhyId ));

  /* If no LL Control request entry avalibale */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    SA_DBG1(("mpiSetPhyProfileCmd, No request from free list\n" ));
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    /* Remove the request from free list */
    saLlistRemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;

    pRequest->valid = agTRUE;
    pRequest->SOP = (bit16) Operation;
    pRequest->completionCB  = (void *)ossaGetPhyProfileCB;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSetPhyProfileCmd_V_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetPhyProfileCmd_V_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetPhyProfileCmd_V_t, Reserved_Ppc_SOP_PHYID), (((Operation & 0xF) << SHIFT8 ) | (PhyId  & 0xFF) ) );

    for(i=0; i < (length / sizeof(bit32)); i++)
    {
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetPhyProfileCmd_V_t, PageSpecificArea[i]),* (PageData+i)  );
    }

    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_PHY_PROFILE, IOMB_SIZE128, 0);
    if (AGSA_RC_SUCCESS != ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      pRequest->valid = agFALSE;
      /* return the request to free pool */
      saLlistAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
      /* remove the request from IOMap */
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiSetPhyProfileCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiGetPhyProfileCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2P");
  /* return value */
  return ret;
}


/******************************************************************************/
/*! \brief Get Device Information Command
 *
 *  This command is get # of phys and support speeds from SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agDevHandle  Handle of device
 *  \param deviceid     Device Id
 *  \param opton        oprion
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDeviceInfoCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceid,
  bit32               option,
  bit32               queueNum
  )
{
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t     *pRequest;
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaGetDevInfoCmd_t     payload;

  SA_ASSERT((agNULL !=saRoot ), "");
  if(saRoot == agNULL)
  {
    SA_DBG1(("mpiGetDeviceInfoCmd: saRoot == agNULL\n"));
    return(AGSA_RC_FAILURE);
  }
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2K");

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* Get request from free IORequests */
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiGetDeviceInfoCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2K");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    pRequest->DeviceInfoCmdOption = (bit8)option;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGetDevInfoCmd_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDevInfoCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDevInfoCmd_t, DeviceId), deviceid);
    /* build IOMB command and send to SPC */
    if( smIS_SPC(agRoot))
    {
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SPC_GET_DEV_INFO, IOMB_SIZE64, queueNum);
    }
    else
    {
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_DEV_INFO, IOMB_SIZE64, queueNum);
    }
    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiGetDeviceInfoCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiGetDeviceInfoCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2K");
  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief Set Device Information Command
 *
 *  This command is Set Device Information to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agDevHandle  Handle of device
 *  \param deviceid     Device Id
 *  \param opton        oprion
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetDeviceInfoCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceid,
  bit32               option,
  bit32               queueNum,
  bit32               param,
  ossaSetDeviceInfoCB_t   agCB
  )
{
  agsaLLRoot_t            *saRoot = agNULL;
  agsaIORequestDesc_t     *pRequest = agNULL;
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaSetDevInfoCmd_t     payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xe");

  /* sanity check */
  SA_ASSERT((agNULL != agRoot), "");
  saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  SA_ASSERT((agNULL != saRoot), "");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  SA_DBG2(("mpiSetDeviceInfoCmd, param 0x%08X option 0x%08X\n",param,option ));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiSetDeviceInfoCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xe");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
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
    pRequest->completionCB = (ossaSSPCompletedCB_t)agCB;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSetDevInfoCmd_t));

    /* set tag field */

    if(smIS_SPC(agRoot))
    {
      option &= SET_DEV_INFO_SPC_DW3_MASK;
      param  &= SET_DEV_INFO_SPC_DW4_MASK;
    }
    else
    {
      option &= SET_DEV_INFO_V_DW3_MASK;
      param  &= SET_DEV_INFO_V_DW4_MASK;
    }

    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDevInfoCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDevInfoCmd_t, deviceId), deviceid);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDevInfoCmd_t, SA_SR_SI), option);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDevInfoCmd_t, DEVA_MCN_R_ITNT), param );

    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_DEV_INFO, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiSetDeviceInfoCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiSetDeviceInfoCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xe");
  /* return value */

  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Phy Start Command
 *
 *  This command sends to SPC for the I/O.
 *
 *  \param agRoot        Handles for this instance of SAS/SATA LLL
 *  \param tag           tage for IOMB
 *  \param phyId         the phy id of the link will be started
 *  \param agPhyConfig   the phy properity
 *  \param agSASIdentify the SAS identify frame will be sent by the phy
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiPhyStartCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  agsaPhyConfig_t     *agPhyConfig,
  agsaSASIdentify_t   *agSASIdentify,
  bit32               queueNum
  )
{
  bit32               ret = AGSA_RC_SUCCESS;
  agsaPhyStartCmd_t   payload;
  bit32               *pValue;
  bit32               *ptemp;
  bit32               index;
  bit32               dw2 = 0;

#if defined(SALLSDK_DEBUG)
  bit32               Sscd;
#endif  /* SALLSDK_DEBUG */
  smTraceFuncEnter(hpDBG_VERY_LOUD,"xg");

  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(agsaPhyStartCmd_t));

  pValue = (bit32 *)agSASIdentify;
  ptemp = (bit32 *)&payload.sasIdentify;
  index = (agPhyConfig->phyProperties & 0x0ff00) >> SHIFT8;

#if defined(SALLSDK_DEBUG)
  Sscd =  (agPhyConfig->phyProperties & 0xf0000) >> SHIFT16;
#endif  /* SALLSDK_DEBUG */

  SA_DBG1(("mpiPhyStartCmd,phyId = %d dw 2 0x%08X\n",phyId ,((phyId & SM_PHYID_MASK) | ((agPhyConfig->phyProperties & 0xfff) << SHIFT8) | (agPhyConfig->phyProperties & 0xf0000) )));


  SA_DBG2(("mpiPhyStartCmd,phyId 0x%x phyProperties 0x%x index 0x%x Sscd 0x%x\n",phyId, agPhyConfig->phyProperties,index,Sscd));

  dw2 = ((phyId & SM_PHYID_MASK)                             | /* PHY id */
        ((agPhyConfig->phyProperties & 0x000000FF) << SHIFT8)| /* SLR Mode */
         (agPhyConfig->phyProperties & 0x000f0000)           | /* SSCD */
         (agPhyConfig->phyProperties & 0x00700000)           | /* setting bit20, bit21 and bit22 for optical mode */
         (agPhyConfig->phyProperties & 0x00800000) );          /* bit23 active cable mode BCT Disable 12g only*/

  /* Haileah Phy analogsetting bit enable*/
  if(smIS_SPC(agRoot))
  {
    if( smIS_spc8081(agRoot))
    {
       dw2 = dw2 | 0x08000;
     }
  }

  SA_DBG1(("mpiPhyStartCmd,dw2 0x%08x\n",dw2));
  SA_ASSERT(((agSASIdentify->sasAddressHi[0] || agSASIdentify->sasAddressHi[1] ||
  agSASIdentify->sasAddressHi[2] || agSASIdentify->sasAddressHi[3] ||
  agSASIdentify->sasAddressLo[0] || agSASIdentify->sasAddressLo[1] ||
  agSASIdentify->sasAddressLo[2] || agSASIdentify->sasAddressLo[3])), "SAS Address Zero");

  SA_DBG1(("mpiPhyStartCmd,SAS addr Hi 0x%02X%02X%02X%02X Lo 0x%02X%02X%02X%02X\n",
                                                              agSASIdentify->sasAddressHi[0],agSASIdentify->sasAddressHi[1],
                                                              agSASIdentify->sasAddressHi[2],agSASIdentify->sasAddressHi[3],
                                                              agSASIdentify->sasAddressLo[0],agSASIdentify->sasAddressLo[1],
                                                              agSASIdentify->sasAddressLo[2],agSASIdentify->sasAddressLo[3]));

  /* setup phy ID field */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPhyStartCmd_t, SscdAseSHLmMlrPhyId),dw2);

  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPhyStartCmd_t, tag), tag);

  /* setup analog setting index field */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPhyStartCmd_t, analogSetupIdx), index);
  /* copy SASIdentify to payload of IOMB */
  si_memcpy(ptemp, pValue, sizeof(agsaSASIdentify_t));

  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_PHYSTART, IOMB_SIZE64, queueNum);

  SA_DBG3(("mpiPhyStartCmd, return value = %d\n", ret));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xg");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Phy Stop Command
 *
 *  This command sends to SPC for the I/O.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param tag          tag of IOMB
 *  \param phyId        To stop the phyId
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiPhyStopCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  bit32               queueNum
  )
{
  bit32 ret = AGSA_RC_SUCCESS;
  agsaPhyStopCmd_t    payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xh");

  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(agsaPhyStopCmd_t));

  /* set tag */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPhyStopCmd_t, tag), tag);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaPhyStopCmd_t, phyId), (phyId & SM_PHYID_MASK ));
  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_PHYSTOP, IOMB_SIZE64, queueNum);

  SA_DBG3(("mpiPhyStopCmd, return value = %d\n", ret));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xh");

  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI SMP Request Command
 *
 *  This command sends to SPC for the SMP.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param pIomb        pointer of IOMB
 *  \param opcode       opcode of IOMB
 *  \param payload      pointer of payload
 *  \param inq          inbound queue number
 *  \param outq         outbound queue number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSMPCmd(
  agsaRoot_t             *agRoot,
  void                   *pIomb,
  bit16                  opcode,
  agsaSMPCmd_t           *payload,
  bit8                   inq,
  bit8                   outq
  )
{
  agsaLLRoot_t   *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  mpiICQueue_t   *circularQ;
  bit32          ret = AGSA_RC_SUCCESS;
#if defined(SALLSDK_DEBUG)
 mpiMsgHeader_t *msgHeader;
  bit32                bc;
#endif /* SALLSDK_DEBUG */
  smTraceFuncEnter(hpDBG_VERY_LOUD,"xi");

  SA_DBG6(("mpiSMPCmd: start\n"));

#if defined(SALLSDK_DEBUG)
  msgHeader = (mpiMsgHeader_t*)(((bit8*)pIomb) - sizeof(mpiMsgHeader_t));
  bc = (((msgHeader->Header) >> SHIFT24) & BC_MASK);
#endif /* SALLSDK_DEBUG */
  SA_DBG6(("mpiSMPCmd: before msgHeader bc %d\n", bc));

  /* copy payload if it is necessary */
  if (agNULL != payload)
  {
    si_memcpy(pIomb, payload, sizeof(agsaSMPCmd_t));
  }

  SA_DBG6(("mpiSMPCmd: after msgHeader bc %d\n", bc));

  /* post the IOMB to SPC */
  circularQ = &saRoot->inboundQueue[inq];
  if (AGSA_RC_FAILURE == mpiMsgProduce(circularQ, (void *)pIomb, MPI_CATEGORY_SAS_SATA, opcode, outq, (bit8)circularQ->priority))
    ret = AGSA_RC_FAILURE;

  SA_DBG3(("mpiSMPCmd, return value = %d\n", ret));

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xi");
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Deregister Device Handle Command
 *
 *  This command used to deregister(remove) the device handle.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agDevHandle  Device Handle
 *  \param deviceId     index of device
 *  \param portId       index of port
 *  \param queueNum     IQ/OQ number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDeregDevHandleCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaDeviceDesc_t    *pDevice,
  bit32               deviceId,
  bit32               portId,
  bit32               queueNum
  )
{
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t            *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t     *pRequest;
  agsaDeregDevHandleCmd_t payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xp");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiDeregDevHandleCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xp");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    pRequest->pDevice = pDevice;
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    pRequest->valid = agTRUE;
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* clean the payload to zeros */
    si_memset(&payload, 0, sizeof(agsaDeregDevHandleCmd_t));

    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDeregDevHandleCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDeregDevHandleCmd_t, deviceId), deviceId);

    /* build IOMB command and send it to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_DEREG_DEV_HANDLE, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
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
      SA_DBG1(("mpiSetVPDCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiDeregDevHandleCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xp");

  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI Get Device Handle Command
 *
 *  This command used to get device handle.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context of Device Handle Command
 *  \param portId       index of port
 *  \param flags        flags
 *  \param maxDevs      Maximum Device Handles
 *  \param queueNum     IQ/OQ number
 *  \param skipCount    skip device entry count
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDeviceHandleCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               portId,
  bit32               flags,
  bit32               maxDevs,
  bit32               queueNum,
  bit32               skipCount
  )
{
  bit32                 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaGetDevHandleCmd_t payload;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xj");

  /* Get request from free CntrlRequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests)); /**/
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG1(("mpiGetDeviceHandleCmd, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      SA_DBG1(("mpiGetDeviceHandleCmd, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xj");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
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
  SA_ASSERT((!pRequest->valid), "The pRequest is in use");
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;
  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);


  /* clean the payload to zeros */
  si_memset(&payload, 0, sizeof(agsaGetDevHandleCmd_t));
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDevHandleCmd_t, tag), pRequest->HTag);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDevHandleCmd_t, DevADevTMaxDIDportId),
                   ((portId & PORTID_MASK) | (maxDevs << SHIFT8) | (flags << SHIFT24)));
    /* set starting Number */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDevHandleCmd_t, skipCount), skipCount);

  /* build IOMB command and send it to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_DEV_HANDLE, IOMB_SIZE64, queueNum);
  if (AGSA_RC_SUCCESS != ret)
  {
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    /* remove the request from IOMap */
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDeviceHandleCmd: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("mpiGetDeviceHandleCmd, sending IOMB failed\n" ));
  }
  SA_DBG3(("mpiGetDeviceHandleCmd, return value = %d\n", ret));

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xj");

  return ret;
}

/******************************************************************************/
/*! \brief SPC MPI LOCAL PHY CONTROL Command
 *
 *  This command used to do the SPC Phy operation.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param tag          tag of IOMB
 *  \param phyId        PHY Id
 *  \param operation    operation of PHY control
 *  \param queueNum     IQ/OQ number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiLocalPhyControlCmd(
  agsaRoot_t          *agRoot,
  bit32               tag,
  bit32               phyId,
  bit32               operation,
  bit32               queueNum
  )
{
  bit32                   ret = AGSA_RC_SUCCESS;
  agsaLocalPhyCntrlCmd_t  payload;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"xl");

  SA_DBG3(("mpiLocalPhyControlCmd, phyId 0x%X operation 0x%x dw2 0x%x\n",phyId, operation,(((operation & BYTE_MASK) << SHIFT8) | (phyId & SM_PHYID_MASK))));

  /* clean the payload field */
  si_memset(&payload, 0, sizeof(agsaLocalPhyCntrlCmd_t));

  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaLocalPhyCntrlCmd_t, phyOpPhyId),
    (((operation & BYTE_MASK) << SHIFT8) | (phyId & SM_PHYID_MASK)));
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaLocalPhyCntrlCmd_t, tag), tag);
  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_LOCAL_PHY_CONTROL, IOMB_SIZE64, queueNum);

  SA_DBG3(("mpiLocalPhyControlCmd, return value = %d\n", ret));

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xl");
  return ret;
}

/******************************************************************************/
/*! \brief Device Handle Accept Command
 *
 *  This command is Device Handle Accept IOMB to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the set VPD command
 *  \param ctag         controller tag
 *  \param deviceId     device Id
 *  \param action       action
 *  \param queueNum     queue Number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDevHandleAcceptCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               ctag,
  bit32               deviceId,
  bit32               action,
  bit32               flag,
  bit32               itlnx,
  bit32               queueNum
  )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;
  agsaDevHandleAcceptCmd_t payload;
  bit32                    DW4 =0;
  bit32                    mcn =0;
  bit32                    awt =0;
  bit32                    ha =0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xt");

  if(deviceId & 0xFFFF0000)
  {
    ha = 1;
  }
  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot,LL_IOREQ_LOCKEQ_LOCK );
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  SA_DBG2(("mpiDevHandleAcceptCmd, deviceId 0x%x action 0x%x flag 0x%x itlnx 0x%x\n",deviceId,action,flag,itlnx ));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    ossaSingleThreadedLeave(agRoot,LL_IOREQ_LOCKEQ_LOCK );
    SA_DBG1(("mpiDevHandleAcceptCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xt");
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* Do not mark as valid at this IOMB does not complete in OBQ */

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaDevHandleAcceptCmd_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDevHandleAcceptCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDevHandleAcceptCmd_t, deviceId), deviceId);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDevHandleAcceptCmd_t, Ctag), ctag);
    mcn = (flag & 0xF0000) >>SHIFT16;
    awt = (flag & 2)>>SHIFT1;
    DW4 = (action << SHIFT24) | \
             mcn << SHIFT20   | \
             awt << SHIFT17   | \
             ha  << SHIFT16   | \
                     itlnx;
    SA_DBG2(("mpiDevHandleAcceptCmd,DW4 0x%x\n",DW4 ));
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDevHandleAcceptCmd_t, DevA_MCN_R_R_HA_ITNT),DW4);
  }

  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_DEV_HANDLE_ACCEPT, IOMB_SIZE64, queueNum);
  if (AGSA_RC_SUCCESS != ret)
  {
    SA_DBG1(("mpiDevHandleAcceptCmd, sending IOMB failed\n" ));
  }
  else
  {
    SA_DBG1(("mpiDevHandleAcceptCmd, sending IOMB succeeded\n" ));
  }

  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  /* remove the request from IOMap */
  saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
  saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
  saRoot->IOMap[pRequest->HTag].agContext = agNULL;
  pRequest->valid = agFALSE;
  /* return the request to free pool */
  if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
  {
    SA_DBG1(("mpiDevHandleAcceptCmd: saving pRequest (%p) for later use\n", pRequest));
    saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    /* return the request to free pool */
    saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }

  /* return value */
  ossaSingleThreadedLeave(agRoot,LL_IOREQ_LOCKEQ_LOCK );
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xt");
  return ret;
}

/******************************************************************************/
/*! \brief SPC READ REGISTER DUMP Command
 *
 *  This command used to do the SPC Read Register Dump command.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param tag          tag of IOMB
 *  \param cpuId        CPU Id
 *  \param queueNum     IQ/OQ number
 *  \param cpuId        AAP1 or IOP
 *  \param cOffset      offset of the register dump data
 *  \param addrHi       Hi address if Register Dump data
 *  \param addrHi       Low address if Register Dump data
 *  \param len          the length of for read
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiNVMReadRegDumpCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               queueNum,
  bit32               cpuId,
  bit32               cOffset,
  bit32               addrHi,
  bit32               addrLo,
  bit32               len
  )
{
  bit32                 ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaGetNVMDataCmd_t   payload;
  bit32 nvmd = 0;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xk");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiNVMReadRegDumpCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xk");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* clean the payload field */
    si_memset(&payload, 0, sizeof(agsaGetNVMDataCmd_t));

    /* only indirect mode */
    if (cpuId <= 1)
    {
      if (cpuId == 0)
        nvmd = AAP1_RDUMP | IRMode;
      else
        nvmd = IOP_RDUMP | IRMode;

      /* setup IOMB */
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, tag), pRequest->HTag);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD), nvmd);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset), cOffset);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respAddrLo), addrLo);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respAddrHi), addrHi);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respLen), len);

      /* build IOMB command and send to SPC */
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_NVMD_DATA, IOMB_SIZE64, queueNum);
    }
    else
    {
      SA_DBG1(("mpiNVMReadRegDumpCmd, Wrong device type\n" ));
      ret = AGSA_RC_FAILURE;
    }

    if (AGSA_RC_SUCCESS != ret)
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
      SA_DBG1(("mpiNVMReadRegDumpCmd, sending IOMB failed\n" ));
    }
  }

  SA_DBG3(("mpiNVMReadRegDumpCmd, return value = %d\n", ret));

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xk");

  return ret;
}

/******************************************************************************/
/*! \brief Get NVM Data command
 *
 *  This command is get NVM Data from SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the VPD command
 *  \param VPDInfo      Pointer of VPD Information
 *  \param queueNum     Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetNVMDCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaNVMDData_t      *NVMDInfo,
  bit32               queueNum
  )
{
  bit32                 ret = AGSA_RC_FAILURE;
  agsaLLRoot_t          *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t   *pRequest;
  agsaGetNVMDataCmd_t   payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xr");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiGetNVMDCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xr");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG3(("mpiGetNVMDCmd, Build IOMB NVMDDevice= 0x%x\n", NVMDInfo->NVMDevice));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaGetNVMDataCmd_t));
    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, tag), pRequest->HTag);

    if (NVMDInfo->indirectPayload)
    {
      /* indirect payload IP = 1 */
      switch (NVMDInfo->NVMDevice)
      {
      case AGSA_NVMD_TWI_DEVICES:
        /* NVMD = 0 */
        /* indirect payload IP = 1 and 0x0 (TWI) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->TWIDeviceAddress << 16) | (NVMDInfo->TWIBusNumber << 12) |
          (NVMDInfo->TWIDevicePageSize << 8) | (NVMDInfo->TWIDeviceAddressSize << 4) |
          (NVMDInfo->indirectPayload << 31) | NVMDInfo->NVMDevice);
            OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      case AGSA_NVMD_CONFIG_SEEPROM:
        /* NVMD = 1 */
        /* Data Offset should be 0 */
        if (NVMDInfo->dataOffsetAddress != 0)
        {
          /* Error for Offset */
          SA_DBG1(("mpiGetNVMDCmd, (IP=1)wrong offset = 0x%x\n", NVMDInfo->dataOffsetAddress));
        }
        /* indirect payload IP = 1, NVMD = 0x1 (SEEPROM0) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | (NVMDInfo->NVMDevice));
        break;
      case AGSA_NVMD_VPD_FLASH:
        /* indirect payload IP = 1 and 0x4 (FLASH) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      case AGSA_NVMD_EXPANSION_ROM:
        /* indirect payload IP = 1 and 0x7 (EXPANSION ROM PARTITION) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      case  AGSA_NVMD_AAP1_REG_FLASH: /* AGSA_NVMD_REG_FLASH  SPCv uses 5 as well */
        /* indirect payload IP = 1 and 0x5 (AGSA_NVMD_AAP1_REG_FLASH ) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      case  AGSA_NVMD_IOP_REG_FLASH:
        /* indirect payload IP = 1 and 0x6 ( AGSA_NVMD_IOP_REG_FLASH ) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;

      default:
        SA_DBG1(("mpiGetNVMDCmd, (IP=1)wrong device type = 0x%x\n", NVMDInfo->NVMDevice));
        break;
      }

      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respAddrLo), NVMDInfo->indirectAddrLower32);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respAddrHi), NVMDInfo->indirectAddrUpper32);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, respLen), NVMDInfo->indirectLen);
      /* build IOMB command and send to SPC */
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_NVMD_DATA, IOMB_SIZE64, queueNum);
    }
    else
    {
      /* direct payload IP = 0 only for TWI device */
      if (AGSA_NVMD_TWI_DEVICES == NVMDInfo->NVMDevice)
      {
        /* NVMD = 0 */
        /* indirect payload IP = 0 and 0x0 (TWI) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->TWIDeviceAddress << SHIFT16) | (NVMDInfo->TWIBusNumber << SHIFT12) |
          (NVMDInfo->TWIDevicePageSize << SHIFT8) | (NVMDInfo->TWIDeviceAddressSize << SHIFT4) |
          NVMDInfo->NVMDevice);
            OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress | (NVMDInfo->directLen << SHIFT24));
        /* build IOMB command and send to SPC */
        ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_NVMD_DATA, IOMB_SIZE64, queueNum);
      }
      else
      {
        SA_DBG1(("mpiGetNVMDCmd, (IP=0)wrong device type = 0x%x\n", NVMDInfo->NVMDevice));
        ret = AGSA_RC_FAILURE;
        /* CB for NVMD with error */
        ossaGetNVMDResponseCB(agRoot, agContext, OSSA_NVMD_MODE_ERROR, 0, NVMDInfo->directLen, agNULL);
      }
    }

    if (AGSA_RC_SUCCESS != ret)
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
      SA_DBG1(("mpiGetNVMDCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiGetNVMDCmd, return value = %d\n", ret));
  }

  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xr");

  /* return value */
  return ret;
}

/******************************************************************************/
/*! \brief Set NVM Data Command
 *
 *  This command is set NVM Data to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the set VPD command
 *  \param NVMDInfo      pointer of VPD information
 *  \param queueNum     queue Number
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetNVMDCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaNVMDData_t      *NVMDInfo,
  bit32               queueNum
  )
{
  bit32               ret = AGSA_RC_FAILURE;
  agsaLLRoot_t        *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t *pRequest;
  agsaSetNVMDataCmd_t payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xm");


  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiSetNVMDCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xm");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG3(("mpiSetNVMDCmd, Build IOMB NVMDDevice= 0x%x\n", NVMDInfo->NVMDevice));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSetNVMDataCmd_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, tag), pRequest->HTag);

    if (NVMDInfo->indirectPayload)
    {
      /* indirect payload IP = 1 */
      switch (NVMDInfo->NVMDevice)
      {
      case AGSA_NVMD_TWI_DEVICES:
        /* NVMD = 0 */
        /* indirect payload IP = 1 and 0x0 (TWI) */
        /* set up signature */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, Data.indirectData.signature), NVMDInfo->signature);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->TWIDeviceAddress << SHIFT16) | (NVMDInfo->TWIBusNumber << SHIFT12) |
          (NVMDInfo->TWIDevicePageSize << SHIFT8) | (NVMDInfo->TWIDeviceAddressSize << SHIFT4) |
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      /* 0x01:SEEPROM-0 and 0x04:FLASH only in indirect mode */
      case AGSA_NVMD_CONFIG_SEEPROM:
        /* NVMD=1 */
        /* Data Offset should be 0 */
        /* set up signature */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, Data.indirectData.signature), NVMDInfo->signature);
        /* indirect payload IP = 1, NVMD = 0x1 (SEEPROM0) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        break;
      case AGSA_NVMD_VPD_FLASH:
        /* indirect payload IP = 1, NVMD=0x4 (FLASH) */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->indirectPayload << SHIFT31) | NVMDInfo->NVMDevice);
        /* set up Offset */
            OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress);
        break;
      default:
        SA_DBG1(("mpiSetNVMDCmd, (IP=1)wrong device type = 0x%x\n", NVMDInfo->NVMDevice));
        ret = AGSA_RC_FAILURE;
        ossaSetNVMDResponseCB(agRoot, agContext, OSSA_NVMD_MODE_ERROR);
        break;
      }

      /* set up SGL field */
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, Data.indirectData.ISglAL), (NVMDInfo->indirectAddrLower32));
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, Data.indirectData.ISglAH), (NVMDInfo->indirectAddrUpper32));
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, Data.indirectData.ILen), (NVMDInfo->indirectLen));
      /* build IOMB command and send to SPC */
      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_NVMD_DATA, IOMB_SIZE64, queueNum);
    }
    else
    {
      /* direct payload IP = 0 */
      if (AGSA_NVMD_TWI_DEVICES == NVMDInfo->NVMDevice)
      {
        /* NVMD = 0 */
        /* indirect payload IP = 0 and 0x0 (TWI) */
        /* not allow write to Config SEEPROM for direct mode, so don't set singature */
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, LEN_IR_VPDD),
          (NVMDInfo->TWIDeviceAddress << SHIFT16) | (NVMDInfo->TWIBusNumber << SHIFT12) |
          (NVMDInfo->TWIDevicePageSize << SHIFT8) | (NVMDInfo->TWIDeviceAddressSize << SHIFT4) |
          NVMDInfo->NVMDevice);
        OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetNVMDataCmd_t, VPDOffset),
          NVMDInfo->dataOffsetAddress | (NVMDInfo->directLen << SHIFT24));
        si_memcpy(&payload.Data.NVMData[0], NVMDInfo->directData, NVMDInfo->directLen);
        /* build IOMB command and send to SPC */
        ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_NVMD_DATA, IOMB_SIZE64, queueNum);
      }
      else
      {
        SA_DBG1(("mpiSetNVMDCmd, (IP=0)wrong device type = 0x%x\n", NVMDInfo->NVMDevice));
        ret = AGSA_RC_FAILURE;
        ossaSetNVMDResponseCB(agRoot, agContext, OSSA_NVMD_MODE_ERROR);
      }
    }

    if (AGSA_RC_SUCCESS != ret)
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
      SA_DBG1(("mpiSetVPDCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiSetNVMDCmd, return value = %d\n", ret));
  }


  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xm");
  return ret;
}

/******************************************************************************/
/*! \brief Set Device State command
 *
 *  This command is set Device State to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the Set Nexus State command
 *  \param deviceId     DeviceId
 *  \param queueNum     Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSetDeviceStateCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceId,
  bit32               nds,
  bit32               queueNum
  )
{
  bit32                  ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t           *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t    *pRequest;
  agsaSetDeviceStateCmd_t payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xn");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiSetDeviceStateCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xn");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG3(("mpiSetDeviceStateCmd, Build IOMB DeviceId= 0x%x\n", deviceId));
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSetDeviceStateCmd_t));
    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDeviceStateCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDeviceStateCmd_t, deviceId), deviceId);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSetDeviceStateCmd_t, NDS), nds);

    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_DEVICE_STATE, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiSetNexusStateCmd, sending IOMB failed\n" ));
    }
   SA_DBG3(("mpiSetDeviceStateCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xn");

  return ret;
}

/******************************************************************************/
/*! \brief Get Device State command
 *
 *  This command is get device State to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the Get Nexus State command
 *  \param deviceId     DeviceId
 *  \param queueNum     Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetDeviceStateCmd(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               deviceId,
  bit32               queueNum
  )
{
  bit32                  ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t           *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t    *pRequest;
  agsaGetDeviceStateCmd_t payload;
  bit32               using_reserved = agFALSE;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xf");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeReservedRequests)); /**/
    /* If no LL Control request entry available */
    if(agNULL != pRequest)
    {
      using_reserved = agTRUE;
      SA_DBG1(("mpiGetDeviceStateCmd, using saRoot->freeReservedRequests\n"));
    }
    else
    {
      SA_DBG1(("mpiGetDeviceStateCmd, No request from free list Not using saRoot->freeReservedRequests\n"));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xf");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return AGSA_RC_BUSY;
    }

  }
  /* If LL Control request entry avaliable */
  SA_DBG3(("mpiGetDeviceStateCmd, Build IOMB DeviceId= 0x%x\n", deviceId));
  /* Remove the request from free list */
  if( using_reserved )
  {
    saLlistIORemove(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
  }
  else
  {
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
  }
  saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
  saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
  saRoot->IOMap[pRequest->HTag].agContext = agContext;
  pRequest->valid = agTRUE;

  ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

  /* set payload to zeros */
  si_memset(&payload, 0, sizeof(agsaGetDeviceStateCmd_t));
  /* set tag field */
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDeviceStateCmd_t, tag), pRequest->HTag);
  OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaGetDeviceStateCmd_t, deviceId), deviceId);

  /* build IOMB command and send to SPC */
  ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_DEVICE_STATE, IOMB_SIZE64, queueNum);
  if (AGSA_RC_SUCCESS != ret)
  {
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    /* remove the request from IOMap */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
    saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
    saRoot->IOMap[pRequest->HTag].agContext = agNULL;
    pRequest->valid = agFALSE;
    /* return the request to free pool */
    if(saLlistIOGetCount(&(saRoot->freeReservedRequests)) < SA_RESERVED_REQUEST_COUNT)
    {
      SA_DBG1(("mpiGetDeviceStateCmd: saving pRequest (%p) for later use\n", pRequest));
      saLlistIOAdd(&(saRoot->freeReservedRequests), &(pRequest->linkNode));
    }
    else
    {
      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));
    }
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("mpiGetDeviceStateCmd, sending IOMB failed\n" ));
  }
  SA_DBG3(("mpiGetDeviceStateCmd, return value = %d\n", ret));

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xf");

  return ret;
}

/******************************************************************************/
/*! \brief SAS ReInitialize command
 *
 *  This command is Reinitialize SAS paremeters to SPC.
 *
 *  \param agRoot       Handles for this instance of SAS/SATA LLL
 *  \param agContext    Context for the Get Nexus State command
 *  \param agSASConfig  SAS Configuration Parameters
 *  \param queueNum     Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiSasReinitializeCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaSASReconfig_t *agSASConfig,
   bit32             queueNum
   )
{
  bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;
  agsaSasReInitializeCmd_t payload;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"xo");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiSasReinitializeCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xo");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG3(("mpiSasReinitializeCmd, Build IOMB SAS_RE_INITIALIZE\n"));
    /* Remove the request from free list */
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    /* set payload to zeros */
    si_memset(&payload, 0, sizeof(agsaSasReInitializeCmd_t));

    /* set tag field */
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSasReInitializeCmd_t, tag), pRequest->HTag);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSasReInitializeCmd_t, setFlags), agSASConfig->flags);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSasReInitializeCmd_t, MaxPorts), agSASConfig->maxPorts);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSasReInitializeCmd_t, openRejReCmdData),
                    (agSASConfig->openRejectRetriesCmd << SHIFT16) | agSASConfig->openRejectRetriesData);
    OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaSasReInitializeCmd_t, sataHOLTMO), agSASConfig->sataHolTmo);


    /* build IOMB command and send to SPC */
    ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_SAS_RE_INITIALIZE, IOMB_SIZE64, queueNum);
    if (AGSA_RC_SUCCESS != ret)
    {
      ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      /* remove the request from IOMap */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      saRoot->IOMap[pRequest->HTag].Tag = MARK_OFF;
      saRoot->IOMap[pRequest->HTag].IORequest = agNULL;
      saRoot->IOMap[pRequest->HTag].agContext = agNULL;
      pRequest->valid = agFALSE;

      /* return the request to free pool */
      saLlistIOAdd(&(saRoot->freeIORequests), &(pRequest->linkNode));

      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiSasReinitializeCmd, sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiSasReinitializeCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xo");

  return ret;
}

/******************************************************************************/
/*! \brief SAS Set Controller Configuration Command
 *
 *  This command updates the contents of a controller mode page.
 *
 *  \param agRoot               Handles for this instance of SAS/SATA LLL
 *  \param agContext            Context for the Get Nexus State command
 *  \param agControllerConfig   Mode page being sent to the controller
 *  \param queueNum             Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32
mpiSetControllerConfigCmd(
   agsaRoot_t                   *agRoot,
   agsaContext_t                *agContext,
   agsaSetControllerConfigCmd_t *agControllerConfig,
   bit32                         queueNum,
   bit8                          modePageContext
   )
{
    bit32                    ret = AGSA_RC_SUCCESS;
    agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
    agsaIORequestDesc_t      *pRequest;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"x1");

    SA_DBG2(("mpiSetControllerConfigCmd: agControllerConfig 0x%08x 0x%08x 0x%08x 0x%08x\n",
                                     agControllerConfig->pageCode,agControllerConfig->configPage[0],
                                     agControllerConfig->configPage[1], agControllerConfig->configPage[2]));
    SA_DBG2(("mpiSetControllerConfigCmd: agControllerConfig 0x%08x 0x%08x 0x%08x 0x%08x\n",
                                     agControllerConfig->configPage[3],agControllerConfig->configPage[4],
                                     agControllerConfig->configPage[5], agControllerConfig->configPage[6]));
    SA_DBG2(("mpiSetControllerConfigCmd: agControllerConfig 0x%08x 0x%08x 0x%08x 0x%08x\n",
                                     agControllerConfig->configPage[7],agControllerConfig->configPage[8],
                                     agControllerConfig->configPage[9], agControllerConfig->configPage[10]));
    SA_DBG2(("mpiSetControllerConfigCmd: agControllerConfig 0x%08x 0x%08x\n",
                                     agControllerConfig->configPage[11],agControllerConfig->configPage[12]));

    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    /* Get request from free IORequests */
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      SA_DBG1(("mpiSetControllerConfigCmd, No request from free list\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "x1");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return AGSA_RC_BUSY;
    }
    /* If LL Control request entry avaliable */
    else
    {
      SA_DBG2(("mpiSetControllerConfigCmd, Build IOMB pageCode 0x%x configPage[0] 0x%x\n",agControllerConfig->pageCode,agControllerConfig->configPage[0]));
      /* Remove the request from free list */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
      saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
      saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
      saRoot->IOMap[pRequest->HTag].agContext = agContext;
      pRequest->valid = agTRUE;
      pRequest->modePageContext = modePageContext;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      /* set tag field */
      agControllerConfig->tag =  pRequest->HTag;
      ret = mpiBuildCmd(agRoot, (bit32 *)agControllerConfig,
                        MPI_CATEGORY_SAS_SATA, OPC_INB_SET_CONTROLLER_CONFIG, IOMB_SIZE64, 0);

      if (AGSA_RC_SUCCESS != ret)
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

          SA_DBG1(("mpiSetControllerConfigCmd, sending IOMB failed\n" ));
      }
      SA_DBG3(("mpiSetControllerConfigCmd, return value = %d\n", ret));
    }

    /* return value */
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "x1");

    return ret;
}

/******************************************************************************/
/*! \brief SAS Get Controller Configuration Command
 *
 *  This command retrieves the contents of a controller mode page.
 *
 *  \param agRoot               Handles for this instance of SAS/SATA LLL
 *  \param agContext            Context for the Get Nexus State command
 *  \param agControllerConfig   Mode page to retrieve from the controller
 *  \param queueNum             Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiGetControllerConfigCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaGetControllerConfigCmd_t *agControllerConfig,
   bit32             queueNum
   )
{
    bit32                    ret = AGSA_RC_SUCCESS;
    agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
    agsaIORequestDesc_t      *pRequest;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"xq");

    SA_DBG1(("mpiGetControllerConfigCmd: Tag 0x%0X Page Code %0X\n",agControllerConfig->tag,agControllerConfig->pageCode ));
    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      SA_DBG1(("mpiGetControllerConfigCmd, No request from free list\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xq");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return AGSA_RC_BUSY;
    }
    /* If LL Control request entry avaliable */
    else
    {
      SA_DBG3(("mpiGetControllerConfig, Build IOMB mpiGetControllerConfigCmd\n"));
      /* Remove the request from free list */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
      saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
      saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
      saRoot->IOMap[pRequest->HTag].agContext = agContext;
      pRequest->valid = agTRUE;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      /* set tag field */
      agControllerConfig->tag =  pRequest->HTag;

      ret = mpiBuildCmd(agRoot, (bit32 *) agControllerConfig,
                        MPI_CATEGORY_SAS_SATA, OPC_INB_GET_CONTROLLER_CONFIG, IOMB_SIZE64, 0);

      if (AGSA_RC_SUCCESS != ret)
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

          SA_DBG1(("mpiGetControllerConfigCmd, sending IOMB failed\n" ));
      }
      else
      {
        SA_DBG3(("mpiGetControllerConfigCmd, set OK\n"));
      }
      SA_DBG3(("mpiGetControllerConfigCmd, return value = %d\n", ret));
    }

    /* return value */
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xq");

    return ret;
}

/******************************************************************************/
/*! \brief SAS Encryption KEK command
 *
 *  This command updates one or more KEK in a controller that supports encryption.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context for the Get Nexus State command
 *  \param agKekMgmt   Kek information that will be sent to the controller
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiKekManagementCmd(
   agsaRoot_t        *agRoot,
   agsaContext_t     *agContext,
   agsaKekManagementCmd_t *agKekMgmt,
   bit32             queueNum
   )
{
    bit32                    ret = AGSA_RC_SUCCESS;
    agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
    agsaIORequestDesc_t      *pRequest;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"x2");

    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      SA_DBG1(("mpiKekManagementCmd, No request from free list\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "x2");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return AGSA_RC_BUSY;
    }
    /* If LL Control request entry avaliable */
    else
    {
      SA_DBG3(("mpiKekManagementCmd, Build OPC_INB_KEK_MANAGEMENT\n"));
      /* Remove the request from free list */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
      saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
      saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
      saRoot->IOMap[pRequest->HTag].agContext = agContext;
      pRequest->valid = agTRUE;
      agKekMgmt->tag = pRequest->HTag;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiKekManagementCmd, 0x%X 0x%X 0x%X\n", agKekMgmt->tag,agKekMgmt->NEWKIDX_CURKIDX_KBF_Reserved_SKNV_KSOP, agKekMgmt->reserved ));

      ret = mpiBuildCmd(agRoot, (bit32 *)agKekMgmt, MPI_CATEGORY_SAS_SATA, OPC_INB_KEK_MANAGEMENT, IOMB_SIZE64, 0);

      if (AGSA_RC_SUCCESS != ret)
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
          SA_DBG1(("mpiKekManagementCmd, sending IOMB failed\n" ));
      }
      SA_DBG3(("mpiKekManagementCmd, return value = %d\n", ret));
    }

    /* return value */
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "x2");

    return ret;
}

/******************************************************************************/
/*! \brief SAS Encryption DEK management command
 *
 *  This command updates one or more DEK in a controller that supports encryption.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context for the Get Nexus State command
 *  \param agDekMgmt   DEK information that will be sent to the controller
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiDekManagementCmd(
   agsaRoot_t                *agRoot,
   agsaContext_t             *agContext,
   agsaDekManagementCmd_t    *agDekMgmt,
   bit32                     queueNum
   )
{
     bit32                    ret = AGSA_RC_SUCCESS;
    agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
    agsaIORequestDesc_t      *pRequest;

    smTraceFuncEnter(hpDBG_VERY_LOUD,"xs");

    /* Get request from free IORequests */
    ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

    /* If no LL Control request entry available */
    if ( agNULL == pRequest )
    {
      SA_DBG1(("mpiDekManagementCmd, No request from free list\n" ));
      smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "xs");
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
      return AGSA_RC_BUSY;
    }
    /* If LL Control request entry avaliable */
    else
    {
      SA_DBG1(("mpiDekManagementCmd, Build OPC_INB_DEK_MANAGEMENT pRequest %p\n",pRequest));
      /* Remove the request from free list */
      SA_ASSERT((!pRequest->valid), "The pRequest is in use");
      saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
      saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
      saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
      saRoot->IOMap[pRequest->HTag].agContext = agContext;
      pRequest->valid = agTRUE;
      agDekMgmt->tag = pRequest->HTag;
      ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

      SA_DBG1(("mpiDekManagementCmd: 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n",
                                    agDekMgmt->tag,
                                    agDekMgmt->KEKIDX_Reserved_TBLS_DSOP,
                                    agDekMgmt->dekIndex,
                                    agDekMgmt->tableAddrLo,
                                    agDekMgmt->tableAddrHi,
                                    agDekMgmt->tableEntries,
                                    agDekMgmt->Reserved_DBF_TBL_SIZE ));
      ret = mpiBuildCmd(agRoot, (bit32 *) agDekMgmt, MPI_CATEGORY_SAS_SATA, OPC_INB_DEK_MANAGEMENT, IOMB_SIZE64, 0);

      if (AGSA_RC_SUCCESS != ret)
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

        SA_DBG1(("mpiDekManagementCmd, sending IOMB failed\n" ));
      }
      SA_DBG3(("mpiDekManagementCmd, return value = %d\n", ret));
    }

    /* return value */
    smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "xs");

    return ret;
}

/******************************************************************************/
/*! \brief
 *
 *  This command sends operator management command.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiOperatorManagementCmd(
  agsaRoot_t                *agRoot,
  bit32                     queueNum,
  agsaContext_t             *agContext,
  agsaOperatorMangmentCmd_t *operatorcode )
{
   bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2q");

  SA_DBG1(("mpiOperatorManagementCmd, enter\n" ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiOperatorManagementCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2q");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG1(("mpiOperatorManagementCmd, Build OPC_INB_OPR_MGMT\n"));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    operatorcode->tag = pRequest->HTag;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    ret = mpiBuildCmd(agRoot, (bit32 *)operatorcode , MPI_CATEGORY_SAS_SATA, OPC_INB_OPR_MGMT, IOMB_SIZE128, 0);

    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiOperatorManagementCmd, sending IOMB failed\n" ));
    }
    SA_DBG1(("mpiOperatorManagementCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2q");

  return ret;
}

/******************************************************************************/
/*! \brief
 *
 *  This command sends encrypt self test command.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32 mpiEncryptBistCmd(
  agsaRoot_t        *agRoot,
  bit32              queueNum,
  agsaContext_t     *agContext,
  agsaEncryptBist_t *bist )
{
   bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"2z");

  SA_DBG1(("mpiEncryptBistCmd, enter\n" ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiEncryptBistCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2z");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG1(("mpiEncryptBistCmd, Build OPC_INB_ENC_TEST_EXECUTE\n"));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    bist->tag = pRequest->HTag;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    SA_DBG1(("mpiEncryptBistCmd: 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n",
                                  bist->tag,
                                  bist->r_subop,
                                  bist->testDiscption[0],
                                  bist->testDiscption[1],
                                  bist->testDiscption[2],
                                  bist->testDiscption[3],
                                  bist->testDiscption[4] ));
    ret = mpiBuildCmd(agRoot, (bit32 *)bist , MPI_CATEGORY_SAS_SATA, OPC_INB_ENC_TEST_EXECUTE, IOMB_SIZE64, 0);

    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiEncryptBistCmd, sending IOMB failed\n" ));
    }
    SA_DBG1(("mpiEncryptBistCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2z");

  return ret;
}

/******************************************************************************/
/*! \brief
 *
 *  This command sends set operator command.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32
mpiSetOperatorCmd(
  agsaRoot_t                *agRoot,
  bit32                      queueNum,
  agsaContext_t             *agContext,
  agsaSetOperatorCmd_t      *operatorcode
  )
{
   bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"39");

  SA_DBG1(("mpiSetOperatorCmd, enter\n" ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiSetOperatorCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "39");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG1(("mpiSetOperatorCmd, Build OPC_INB_SET_OPERATOR\n"));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    operatorcode->tag = pRequest->HTag;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    ret = mpiBuildCmd(agRoot, (bit32 *)operatorcode, MPI_CATEGORY_SAS_SATA, OPC_INB_SET_OPERATOR, IOMB_SIZE64, 0);

    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiSetOperatorCmd, sending IOMB failed\n" ));
    }
    SA_DBG1(("mpiSetOperatorCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "39");

  return ret;
}

/******************************************************************************/
/*! \brief
 *
 *  This command sends get operator command.
 *
 *  \param agRoot      Handles for this instance of SAS/SATA LLL
 *  \param agContext   Context
 *  \param queueNum    Queue Number of inbound/outbound queue
 *
 *  \return If the MPI command is sent to SPC successfully
 *          - \e AGSA_RC_SUCCESS the MPI command is successfully
 *          - \e AGSA_RC_BUSY the SPC is no resource, cannot send now
 *          - \e AGSA_RC_FAILURE the MPI command is failure
 *
 */
/*******************************************************************************/
GLOBAL bit32
mpiGetOperatorCmd(
  agsaRoot_t                *agRoot,
  bit32                      queueNum,
  agsaContext_t             *agContext,
  agsaGetOperatorCmd_t      *operatorcode
  )
{
   bit32                    ret = AGSA_RC_SUCCESS;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;

  smTraceFuncEnter(hpDBG_VERY_LOUD,"3e");

  SA_DBG1(("mpiGetOperatorCmd, enter\n" ));

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiGetOperatorCmd, No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "3e");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG1(("mpiGetOperatorCmd, Build OPC_INB_GET_OPERATOR\n"));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    operatorcode->tag = pRequest->HTag;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    ret = mpiBuildCmd(agRoot, (bit32 *)operatorcode, MPI_CATEGORY_SAS_SATA, OPC_INB_GET_OPERATOR, IOMB_SIZE64, 0);

    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiGetOperatorCmd, sending IOMB failed\n" ));
    }
    SA_DBG1(("mpiGetOperatorCmd, return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "3e");

  return ret;
}

GLOBAL bit32 mpiDIFEncryptionOffloadCmd(
   agsaRoot_t                      *agRoot,
   agsaContext_t                   *agContext,
   bit32                            queueNum,
   bit32                            op,
   agsaDifEncPayload_t             *agDifEncOffload,
   ossaDIFEncryptionOffloadStartCB_t agCB
   )
{
  bit32 ret = AGSA_RC_SUCCESS;
  bit32 dw8=0;
  bit32 dw9=0;
  bit32 dw10=0;
  bit32 dw14=0;
  bit32 dw15=0;
  agsaLLRoot_t             *saRoot = (agsaLLRoot_t *) (agRoot->sdkData);
  agsaIORequestDesc_t      *pRequest;
  agsaDifEncOffloadCmd_t   payload;
  smTraceFuncEnter(hpDBG_VERY_LOUD,"2b");

  /* Get request from free IORequests */
  ossaSingleThreadedEnter(agRoot, LL_IOREQ_LOCKEQ_LOCK);
  pRequest = (agsaIORequestDesc_t *)saLlistIOGetHead(&(saRoot->freeIORequests));

  /* If no LL Control request entry available */
  if ( agNULL == pRequest )
  {
    SA_DBG1(("mpiDIFEncryptionOffloadCmd: No request from free list\n" ));
    smTraceFuncExit(hpDBG_VERY_LOUD, 'a', "2b");
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);
    return AGSA_RC_BUSY;
  }
  /* If LL Control request entry avaliable */
  else
  {
    SA_DBG1(("mpiDIFEncryptionOffloadCmd: Build OPC_INB_DIF_ENC_OFFLOAD_CMD pRequest %p\n",pRequest));
    /* Remove the request from free list */
    SA_ASSERT((!pRequest->valid), "The pRequest is in use");
    saLlistIORemove(&(saRoot->freeIORequests), &(pRequest->linkNode));
    saRoot->IOMap[pRequest->HTag].Tag = pRequest->HTag;
    saRoot->IOMap[pRequest->HTag].IORequest = (void *)pRequest;
    saRoot->IOMap[pRequest->HTag].agContext = agContext;
    pRequest->valid = agTRUE;
    pRequest->completionCB = (ossaSSPCompletedCB_t)agCB;
    ossaSingleThreadedLeave(agRoot, LL_IOREQ_LOCKEQ_LOCK);

    si_memset(&payload, 0, sizeof(agsaDifEncOffloadCmd_t));
    SA_DBG1(("mpiDIFEncryptionOffloadCmd: op %d\n",op));

    if(smIS_SPCV(agRoot))
    {
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, tag),            pRequest->HTag);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, option),         op);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, Src_Data_Len),   agDifEncOffload->SrcDL);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, Dst_Data_Len),   agDifEncOffload->DstDL);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, flags),          agDifEncOffload->dif.flags);

      dw8 = agDifEncOffload->dif.udrtArray[1] << SHIFT24 | 
            agDifEncOffload->dif.udrtArray[0] << SHIFT16 | 
            agDifEncOffload->dif.udtArray[1]  << SHIFT8  | 
            agDifEncOffload->dif.udtArray[0];
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, UDTR01UDT01), dw8);

      dw9 = agDifEncOffload->dif.udtArray[5]  << SHIFT24 |
            agDifEncOffload->dif.udtArray[4] << SHIFT16  |
            agDifEncOffload->dif.udtArray[3] << SHIFT8   | 
            agDifEncOffload->dif.udtArray[2];
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, UDT2345), dw9);
      dw10 = agDifEncOffload->dif.udrtArray[5] << SHIFT24 |
             agDifEncOffload->dif.udrtArray[4] << SHIFT16 |
             agDifEncOffload->dif.udrtArray[3] << SHIFT8  |
             agDifEncOffload->dif.udrtArray[2];

      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, UDTR2345), dw10);

      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, DPLR0SecCnt_IOSeed), 
               agDifEncOffload->dif.DIFPerLARegion0SecCount << SHIFT16 | 
               agDifEncOffload->dif.initialIOSeed);
      
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, DPL_Addr_Lo)        , agDifEncOffload->dif.DIFPerLAAddrLo);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, DPL_Addr_Hi)        , agDifEncOffload->dif.DIFPerLAAddrHi);

      dw14 =  agDifEncOffload->encrypt.dekInfo.dekIndex          << SHIFT8 |
             (agDifEncOffload->encrypt.dekInfo.dekTable & 0x3)   << SHIFT2 | 
             (agDifEncOffload->encrypt.keyTagCheck & 0x1)        << SHIFT1;

      if (agDifEncOffload->encrypt.cipherMode == agsaEncryptCipherModeXTS)
      {
        dw14 |= AGSA_ENCRYPT_XTS_Mode << SHIFT4;
      }
      else
      {
        dw14 |= (agDifEncOffload->encrypt.cipherMode & 0xF) << SHIFT4;
      }

      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, KeyIndex_CMode_KTS_ENT_R), dw14);
      
      dw15 = agDifEncOffload->encrypt.EncryptionPerLRegion0SecCount << SHIFT16 | 
                           (agDifEncOffload->encrypt.kekIndex & 0xF) << SHIFT5 | 
                           (agDifEncOffload->encrypt.sectorSizeIndex & 0x1F);

      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, EPLR0SecCnt_KS_ENSS), dw15);
      
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, keyTag_W0),   agDifEncOffload->encrypt.keyTag_W0);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, keyTag_W1),   agDifEncOffload->encrypt.keyTag_W1);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, tweakVal_W0), agDifEncOffload->encrypt.tweakVal_W0);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, tweakVal_W1), agDifEncOffload->encrypt.tweakVal_W1);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, tweakVal_W2), agDifEncOffload->encrypt.tweakVal_W2);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, tweakVal_W3), agDifEncOffload->encrypt.tweakVal_W3);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, EPL_Addr_Lo), agDifEncOffload->encrypt.EncryptionPerLAAddrLo);
      OSSA_WRITE_LE_32(agRoot, &payload, OSSA_OFFSET_OF(agsaDifEncOffloadCmd_t, EPL_Addr_Hi), agDifEncOffload->encrypt.EncryptionPerLAAddrHi);
      
      si_memcpy((bit32 *) &(payload.SrcSgl), (bit32 *) &(agDifEncOffload->SrcSgl), sizeof(agsaSgl_t));
      si_memcpy((bit32 *) &(payload.DstSgl), (bit32 *) &(agDifEncOffload->DstSgl), sizeof(agsaSgl_t));

      ret = mpiBuildCmd(agRoot, (bit32 *)&payload, MPI_CATEGORY_SAS_SATA, OPC_INB_DIF_ENC_OFFLOAD_CMD, IOMB_SIZE128, queueNum);

    }
    else
    {
      /* SPC does not support this command */
      ret = AGSA_RC_FAILURE;
    }

    if (AGSA_RC_SUCCESS != ret)
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

      SA_DBG1(("mpiDIFEncryptionOffloadCmd: sending IOMB failed\n" ));
    }
    SA_DBG3(("mpiDIFEncryptionOffloadCmd: return value = %d\n", ret));
  }

  /* return value */
  smTraceFuncExit(hpDBG_VERY_LOUD, 'b', "2b");

  return ret;
}

