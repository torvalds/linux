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

osGLOBAL bit32
dmSMPStart(
           dmRoot_t              *dmRoot,
           agsaRoot_t            *agRoot,
           dmDeviceData_t        *oneDeviceData,
           bit32                 functionCode,
           bit8                  *pSmpBody,
           bit32                 smpBodySize,
           bit32                 agRequestType
           )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmIntPortContext_t        *onePortContext = agNULL;
  dmSMPRequestBody_t        *dmSMPRequestBody = agNULL;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t        *dmSMPResponseBody = agNULL;
#endif
  agsaSASRequestBody_t      *agSASRequestBody;
  dmList_t                  *SMPList;
  agsaDevHandle_t           *agDevHandle;
  agsaIORequest_t           *agIORequest;
  agsaSMPFrame_t            *agSMPFrame;
  bit32                     expectedRspLen = 0;
  dmSMPFrameHeader_t        dmSMPFrameHeader;
  dmExpander_t              *oneExpander = agNULL;
  bit32                     status;

  DM_DBG5(("dmSMPStart: start\n"));
  DM_DBG5(("dmSMPStart: 2nd sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG5(("dmSMPStart: 2nd sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  dm_memset(&dmSMPFrameHeader, 0, sizeof(dmSMPFrameHeader_t));

  onePortContext = oneDeviceData->dmPortContext;

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmSMPStart: invalid port or aborted discovery!!!\n"));
    return DM_RC_FAILURE;
  }

  oneExpander = oneDeviceData->dmExpander;
  if (oneExpander == agNULL)
  {
    DM_DBG1(("dmSMPStart: Wrong!!! oneExpander is NULL!!!\n"));
    return DM_RC_FAILURE;
  }

  if (onePortContext != agNULL)
  {
    DM_DBG5(("dmSMPStart: pid %d\n", onePortContext->id));
    /* increment the number of pending SMP */
    onePortContext->discovery.pendingSMP++;
  }
  else
  {
    DM_DBG1(("dmSMPStart: Wrong, onePortContext is NULL!!!\n"));
    return DM_RC_FAILURE;
  }

  /* get an smp REQUEST from the free list */
  tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->freeSMPList)))
  {
    DM_DBG1(("dmSMPStart: no free SMP!!!\n"));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
    /* undo increment the number of pending SMP */
    onePortContext->discovery.pendingSMP--;
    return DM_RC_FAILURE;
  }
  else
  {
    DMLIST_DEQUEUE_FROM_HEAD(&SMPList, &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
    dmSMPRequestBody = DMLIST_OBJECT_BASE(dmSMPRequestBody_t, Link, SMPList);
  }

  if (dmSMPRequestBody == agNULL)
  {
    DM_DBG1(("dmSMPStart: dmSMPRequestBody is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;
  }
  DM_DBG5(("dmSMPStart: SMP id %d\n", dmSMPRequestBody->id));

  dmSMPRequestBody->dmRoot = dmRoot;
  dmSMPRequestBody->dmDevice = oneDeviceData;
  dmSMPRequestBody->dmPortContext = onePortContext;

  agDevHandle = oneExpander->agDevHandle;

  /* save the callback funtion */
  dmSMPRequestBody->SMPCompletionFunc = dmSMPCompleted; /* in dmsmp.c */

  dmSMPRequestBody->retries = 0;

  agIORequest = &(dmSMPRequestBody->agIORequest);
  agIORequest->osData = (void *) dmSMPRequestBody;
  agIORequest->sdkData = agNULL; /* SALL takes care of this */

  agSASRequestBody = &(dmSMPRequestBody->agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  /* sets dmSMPFrameHeader values */
  if (oneExpander->SAS2 == 0)
  {
    DM_DBG5(("dmSMPStart: SAS 1.1\n"));
    switch (functionCode)
    {
    case SMP_REPORT_GENERAL:
      expectedRspLen = sizeof(smpRespReportGeneral_t) + 4;
      break;
    case SMP_REPORT_MANUFACTURE_INFORMATION:
      expectedRspLen = sizeof(smpRespReportManufactureInfo_t) + 4;
      break;
    case SMP_DISCOVER:
      expectedRspLen = sizeof(smpRespDiscover_t) + 4;
      break;
    case SMP_REPORT_PHY_ERROR_LOG:
      expectedRspLen = 32 - 4;
      break;
    case SMP_REPORT_PHY_SATA:
      expectedRspLen = sizeof(smpRespReportPhySata_t) + 4;
      break;
    case SMP_REPORT_ROUTING_INFORMATION:
      expectedRspLen = sizeof(smpRespReportRouteTable_t) + 4;
      break;
    case SMP_CONFIGURE_ROUTING_INFORMATION:
      expectedRspLen = 4;
      break;
    case SMP_PHY_CONTROL:
      expectedRspLen = 4;
      break;
    case SMP_PHY_TEST_FUNCTION:
      expectedRspLen = 4;
      break;
    case SMP_PMC_SPECIFIC:
      expectedRspLen = 4;
      break;
    default:
      expectedRspLen = 0;
      DM_DBG1(("dmSMPStart: SAS 1.1 error, undefined or unused smp function code 0x%x !!!\n", functionCode));
      return DM_RC_FAILURE;
    }
    /* SMP 1.1 header */
    dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
    dmSMPFrameHeader.smpFunction = (bit8)functionCode;
    dmSMPFrameHeader.smpFunctionResult = 0;
    dmSMPFrameHeader.smpReserved = 0;
  }
  else /* SAS 2 */
  {
    DM_DBG2(("dmSMPStart: SAS 2\n"));
    switch (functionCode)
    {
    case SMP_REPORT_GENERAL:
      expectedRspLen = sizeof(smpRespReportGeneral2_t) + 4;
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
      dmSMPFrameHeader.smpFunctionResult = 0x11;
      dmSMPFrameHeader.smpReserved = 0;
      break;
    case SMP_REPORT_MANUFACTURE_INFORMATION:
      expectedRspLen = sizeof(smpRespReportManufactureInfo2_t) + 4;
      break;
    case SMP_DISCOVER:
      expectedRspLen = sizeof(smpRespDiscover2_t) + 4;
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
//      dmSMPFrameHeader.smpFunctionResult = 0x6c;
      dmSMPFrameHeader.smpFunctionResult = 0x1b;
      dmSMPFrameHeader.smpReserved = 0x02;
      break;
    case SMP_REPORT_PHY_ERROR_LOG:
      expectedRspLen = 32 - 4;
      break;
    case SMP_REPORT_PHY_SATA:
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
      dmSMPFrameHeader.smpFunctionResult = 0x10;
      dmSMPFrameHeader.smpReserved = 0x02;
      expectedRspLen = sizeof(smpRespReportPhySata2_t) + 4;
      break;
    case SMP_REPORT_ROUTING_INFORMATION:
      expectedRspLen = sizeof(smpRespReportRouteTable2_t) + 4;
      break;
    case SMP_CONFIGURE_ROUTING_INFORMATION:
      expectedRspLen = 4;
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
      dmSMPFrameHeader.smpFunctionResult = 0;
      dmSMPFrameHeader.smpReserved = 0x09;
      break;
    case SMP_PHY_CONTROL:
      expectedRspLen = 4;
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
      dmSMPFrameHeader.smpFunctionResult = 0;
      dmSMPFrameHeader.smpReserved = 0x09;
     break;
    case SMP_PHY_TEST_FUNCTION:
      expectedRspLen = 4;
      break;
    case SMP_DISCOVER_LIST:
      expectedRspLen = SMP_MAXIMUM_PAYLOAD; /* 1024 without CRC */
      /* SMP 2.0 header */
      dmSMPFrameHeader.smpFrameType = SMP_REQUEST; /* SMP request */
      dmSMPFrameHeader.smpFunction = (bit8)functionCode;
      dmSMPFrameHeader.smpFunctionResult = 0xFF;
      dmSMPFrameHeader.smpReserved = 0x06;
      break;
    case SMP_PMC_SPECIFIC:
      expectedRspLen = 4;
      break;
    default:
      expectedRspLen = 0;
      DM_DBG1(("dmSMPStart: SAS 2 error!!! undefined or unused smp function code 0x%x!!!\n", functionCode));
      return DM_RC_FAILURE;
    }
  }

  if (DMIsSPC(agRoot))
  {
#ifdef DIRECT_SMP  /* direct SMP with 48 or less payload */
  if ( (smpBodySize + 4) <= SMP_DIRECT_PAYLOAD_LIMIT) /* 48 */
  {
    DM_DBG5(("dmSMPStart: DIRECT smp payload\n"));
    dm_memset(dmSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);
    dm_memcpy(dmSMPRequestBody->smpPayload, &dmSMPFrameHeader, 4);
    dm_memcpy((dmSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);

    /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
    agSMPFrame->outFrameBuf = dmSMPRequestBody->smpPayload;
    agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
    /* to specify DIRECT SMP response */
    agSMPFrame->inFrameLen = 0;

    /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* force smp repsonse to be direct */
    agSMPFrame->expectedRespLen = 0;
#else
    agSMPFrame->expectedRespLen = expectedRspLen;
#endif
  }
  else
  {
    DM_DBG5(("dmSMPStart: INDIRECT smp payload, TBD\n"));
  }

#else

  /*
     dmSMPRequestBody is SMP request
     dmSMPResponsebody is SMP response
  */

  /* get an smp RESPONSE from the free list */
  tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->freeSMPList)))
  {
    DM_DBG1(("dmSMPStart: no free SMP!!!\n"));
    /* puy back dmSMPRequestBody to the freelist ???*/
//    DMLIST_DEQUEUE_THIS(&(dmSMPRequestBody->Link));
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

    /* undo increment the number of pending SMP */
    onePortContext->discovery.pendingSMP--;
    return DM_RC_FAILURE;
  }
  else
  {
    DMLIST_DEQUEUE_FROM_HEAD(&SMPList, &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
    dmSMPResponseBody = DMLIST_OBJECT_BASE(dmSMPRequestBody_t, Link, SMPList);
    DM_DBG5(("dmSMPStart: SMP id %d\n", dmSMPResponseBody->id));
  }

  if (dmSMPResponseBody == agNULL)
  {
    DM_DBG1(("dmSMPStart: dmSMPResponseBody is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;
  }

  /* fill in indirect SMP request fields */
  DM_DBG5(("dmSMPStart: INDIRECT smp payload\n"));

  /* save the pointer to SMP response in SMP request */
  dmSMPRequestBody->IndirectSMPResponse = dmSMPResponseBody;
  /* SMP request and response initialization */
  dm_memset(dmSMPRequestBody->IndirectSMP, 0, smpBodySize + 4);
  dm_memset(dmSMPResponseBody->IndirectSMP, 0, expectedRspLen);

  dm_memcpy(dmSMPRequestBody->IndirectSMP, &dmSMPFrameHeader, 4);
  dm_memcpy(dmSMPRequestBody->IndirectSMP+4, pSmpBody, smpBodySize);

  /* Indirect SMP request */
  agSMPFrame->outFrameBuf = agNULL;
  agSMPFrame->outFrameAddrUpper32 = dmSMPRequestBody->IndirectSMPUpper32;
  agSMPFrame->outFrameAddrLower32 = dmSMPRequestBody->IndirectSMPLower32;
  agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */

  /* Indirect SMP response */
  agSMPFrame->expectedRespLen = expectedRspLen;
  agSMPFrame->inFrameAddrUpper32 = dmSMPResponseBody->IndirectSMPUpper32;
  agSMPFrame->inFrameAddrLower32 = dmSMPResponseBody->IndirectSMPLower32;
  agSMPFrame->inFrameLen = expectedRspLen; /* without last 4 byte crc */

#endif
  }
  else /* SPCv controller */
  {
    /* only direct mode for both request and response */
    DM_DBG5(("dmSMPStart: DIRECT smp payload\n"));
    agSMPFrame->flag = 0;
    dm_memset(dmSMPRequestBody->smpPayload, 0, SMP_DIRECT_PAYLOAD_LIMIT);
    dm_memcpy(dmSMPRequestBody->smpPayload, &dmSMPFrameHeader, 4);
    dm_memcpy((dmSMPRequestBody->smpPayload)+4, pSmpBody, smpBodySize);

    /* direct SMP payload eg) REPORT_GENERAL, DISCOVER etc */
    agSMPFrame->outFrameBuf = dmSMPRequestBody->smpPayload;
    agSMPFrame->outFrameLen = smpBodySize + 4; /* without last 4 byte crc */
    /* to specify DIRECT SMP response */
    agSMPFrame->inFrameLen = 0;

      /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* force smp repsonse to be direct */
    agSMPFrame->expectedRespLen = 0;
#else
    agSMPFrame->expectedRespLen = expectedRspLen;
#endif
  //    tdhexdump("tdSMPStart", (bit8*)agSMPFrame->outFrameBuf, agSMPFrame->outFrameLen);
  //    tdhexdump("tdSMPStart new", (bit8*)tdSMPRequestBody->smpPayload, agSMPFrame->outFrameLen);
  //    tdhexdump("tdSMPStart - tdSMPRequestBody", (bit8*)tdSMPRequestBody, sizeof(tdssSMPRequestBody_t));
  }

  if (agDevHandle == agNULL)
  {
    DM_DBG1(("dmSMPStart: !!! agDevHandle is NULL !!! \n"));
  }
  else
  {
    status = saSMPStart(
                      agRoot,
                      agIORequest,
                      0,
                      agDevHandle,
                      agRequestType,
                      agSASRequestBody,
                      &dmsaSMPCompleted
                      );

    if (status == AGSA_RC_SUCCESS)
    {
      /* start SMP timer */
      if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
          functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION
        )
      {
        dmDiscoverySMPTimer(dmRoot, onePortContext, functionCode, dmSMPRequestBody);
      }
      return DM_RC_SUCCESS;
    }
    else if (status == AGSA_RC_BUSY)
    {
      /* set timer */
      if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
          functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION)
      {
        /* only for discovery related SMPs*/
        dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
        return DM_RC_SUCCESS;
      }
      else
      {
        DM_DBG1(("dmSMPStart: return DM_RC_BUSY!!! \n"));
#ifdef DIRECT_SMP
        tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
        DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
        tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#else
        tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
        DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
        DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
        tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
        return DM_RC_BUSY;
      }
    }
    else /* AGSA_RC_FAILURE */
    {
      DM_DBG1(("dmSMPStart: return DM_RC_FAILURE!!! \n"));
      /* discovery failure or task management failure */
      if (functionCode == SMP_REPORT_GENERAL || functionCode == SMP_DISCOVER ||
          functionCode == SMP_REPORT_PHY_SATA || functionCode == SMP_CONFIGURE_ROUTING_INFORMATION)
      {
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      }
#ifdef DIRECT_SMP
      tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
      DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
      tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#else
      tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
      DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
      DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
      tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

      return DM_RC_FAILURE;
    }
  }
  return DM_RC_SUCCESS;
}

osGLOBAL void
dmsaSMPCompleted(
                 agsaRoot_t            *agRoot,
                 agsaIORequest_t       *agIORequest,
                 bit32                 agIOStatus,
                 bit32                 agIOInfoLen,
                 agsaFrameHandle_t     agFrameHandle
                 )
{
  dmSMPRequestBody_t   *pSMPRequestBody = (dmSMPRequestBody_t *) agIORequest->osData;

  /* SPC can't be SMP target */

  DM_DBG5(("dmsaSMPCompleted: start\n"));

  if (pSMPRequestBody == agNULL)
  {
    DM_DBG1(("dmsaSMPCompleted: pSMPRequestBody is NULL!!! \n"));
    return;
  }

  if (pSMPRequestBody->SMPCompletionFunc == agNULL)
  {
    DM_DBG1(("dmsaSMPCompleted: pSMPRequestBody->SMPCompletionFunc is NULL!!!\n"));
    return;
  }

#ifdef DM_INTERNAL_DEBUG /* debugging */
  DM_DBG3(("dmsaSMPCompleted: agIOrequest %p\n", agIORequest->osData));
  DM_DBG3(("dmsaSMPCompleted: sizeof(tdIORequestBody_t) %d 0x%x\n", sizeof(tdIORequestBody_t),
           sizeof(tdIORequestBody_t)));
  DM_DBG3(("dmsaSMPCompleted: SMPRequestbody %p\n", pSMPRequestBody));
  DM_DBG3(("dmsaSMPCompleted: calling callback fn\n"));
  DM_DBG3(("dmsaSMPCompleted: callback fn %p\n",pSMPRequestBody->SMPCompletionFunc));
#endif /* TD_INTERNAL_DEBUG */
  /*
    if initiator, calling dmSMPCompleted() in dmsmp.c
  */
  pSMPRequestBody->SMPCompletionFunc(
                                     agRoot,
                                     agIORequest,
                                     agIOStatus,
                                     agIOInfoLen,
                                     agFrameHandle
                                     );

  return;

}

osGLOBAL bit32
dmPhyControlSend(
                   dmRoot_t             *dmRoot,
//                   dmDeviceData_t     *oneDeviceData, /* taget disk */
                   dmDeviceData_t     *oneExpDeviceData, /* taget disk */                   
                   bit8                 phyOp,
bit8 phyID // added
                   )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t                *agRoot = dmAllShared->agRoot;
//  thenil
//  dmDeviceData_t      *oneExpDeviceData; 
  smpReqPhyControl_t    smpPhyControlReq;
//  bit8                  phyID;
  bit32                 status;
  
  DM_DBG3(("dmPhyControlSend: start\n"));
  
  
  
  osti_memset(&smpPhyControlReq, 0, sizeof(smpReqPhyControl_t));

  /* fill in SMP payload */
  smpPhyControlReq.phyIdentifier = phyID;
  smpPhyControlReq.phyOperation = phyOp;
  
  status = dmSMPStart(
                      dmRoot,
                      agRoot,
                      oneExpDeviceData,
                      SMP_PHY_CONTROL,
                      (bit8 *)&smpPhyControlReq,
                      sizeof(smpReqPhyControl_t),
                      AGSA_SMP_INIT_REQ
                     );
  return status;
}

osGLOBAL void
dmReportGeneralSend(
                    dmRoot_t             *dmRoot,
                    dmDeviceData_t       *oneDeviceData
                    )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t                *agRoot = dmAllShared->agRoot;

  DM_DBG3(("dmReportGeneralSend: start\n"));
  DM_DBG3(("dmReportGeneralSend: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  DM_DBG3(("dmReportGeneralSend: oneExpander %p did %d\n", oneDeviceData->dmExpander, oneDeviceData->dmExpander->id));

  if (agRoot == agNULL)
  {
    DM_DBG1(("dmReportGeneralSend: agRoot is NULL!!!\n"));
    return;
  }

  dmSMPStart(
             dmRoot,
             agRoot,
             oneDeviceData,
             SMP_REPORT_GENERAL,
             agNULL,
             0,
             AGSA_SMP_INIT_REQ
             );
  return;
}
osGLOBAL void
dmReportGeneralRespRcvd(
                        dmRoot_t              *dmRoot,
                        agsaRoot_t            *agRoot,
                        agsaIORequest_t       *agIORequest,
                        dmDeviceData_t        *oneDeviceData,
                        dmSMPFrameHeader_t    *frameHeader,
                        agsaFrameHandle_t     frameHandle
                        )
{
  smpRespReportGeneral_t       dmSMPReportGeneralResp;
  smpRespReportGeneral_t       *pdmSMPReportGeneralResp;
  dmIntPortContext_t           *onePortContext = agNULL;
  dmDiscovery_t                *discovery;
  dmExpander_t                 *oneExpander = agNULL;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t           *dmSMPRequestBody;
  dmSMPRequestBody_t           *dmSMPResponseBody = agNULL;
#endif
  dmIntRoot_t         *dmIntRoot   = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t      *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;

  DM_DBG3(("dmReportGeneralRespRcvd: start\n"));
  DM_DBG3(("dmReportGeneralRespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmReportGeneralRespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifndef DIRECT_SMP
  dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
  pdmSMPReportGeneralResp = &dmSMPReportGeneralResp;

  dm_memset(&dmSMPReportGeneralResp, 0, sizeof(smpRespReportGeneral_t));

#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pdmSMPReportGeneralResp, sizeof(smpRespReportGeneral_t));
#else
  dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
  saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 4, pdmSMPReportGeneralResp, sizeof(smpRespReportGeneral_t));
#endif

  onePortContext = oneDeviceData->dmPortContext;
  discovery = &(onePortContext->discovery);

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmReportGeneralRespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    oneDeviceData->numOfPhys = (bit8) pdmSMPReportGeneralResp->numOfPhys;
    oneExpander = oneDeviceData->dmExpander;
    oneExpander->routingIndex = (bit16) REPORT_GENERAL_GET_ROUTEINDEXES(pdmSMPReportGeneralResp);
    oneExpander->configReserved = 0;
    oneExpander->configRouteTable = REPORT_GENERAL_IS_CONFIGURABLE(pdmSMPReportGeneralResp) ? 1 : 0;
    oneExpander->configuring = REPORT_GENERAL_IS_CONFIGURING(pdmSMPReportGeneralResp) ? 1 : 0;
    DM_DBG2(("dmReportGeneralRespRcvd: SAS 2 is %d\n", oneExpander->SAS2));
    DM_DBG3(("dmReportGeneralRespRcvd: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
    DM_DBG3(("dmReportGeneralRespRcvd: oneExpander %p did %d\n", oneExpander, oneExpander->id));

    if ( oneExpander->SAS2 == 0 && REPORT_GENERAL_IS_LONG_RESPONSE(pdmSMPReportGeneralResp) == 1)
    {
      oneExpander->SAS2 = REPORT_GENERAL_IS_LONG_RESPONSE(pdmSMPReportGeneralResp);
      DM_DBG2(("dmReportGeneralRespRcvd: SAS 2 Long Response=%d\n", REPORT_GENERAL_IS_LONG_RESPONSE(pdmSMPReportGeneralResp)));
      dmReportGeneralSend(dmRoot, oneDeviceData);
      return;
    }

    DM_DBG3(("dmReportGeneralRespRcvd: oneExpander=%p numberofPhys=0x%x RoutingIndex=0x%x\n",
      oneExpander, oneDeviceData->numOfPhys, oneExpander->routingIndex));
    DM_DBG3(("dmReportGeneralRespRcvd: configRouteTable=%d configuring=%d\n",
      oneExpander->configRouteTable, oneExpander->configuring));

    if (oneExpander->configuring == 1)
    {
      discovery->retries++;
      if (discovery->retries >= dmAllShared->MaxRetryDiscovery)
      {
        DM_DBG1(("dmReportGeneralRespRcvd: retries are over!!!\n"));
        DM_DBG1(("dmReportGeneralRespRcvd: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
        discovery->retries = 0;
        /* failed the discovery */
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      }
      else
      {
        DM_DBG3(("dmReportGeneralRespRcvd: keep retrying\n"));
        DM_DBG1(("dmReportGeneralRespRcvd: Prep222389 RETRY at %d Maximum Retry is %d\n", discovery->retries, dmAllShared->MaxRetryDiscovery));
        DM_DBG1(("dmReportGeneralRespRcvd: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
        // start timer for sending ReportGeneral
        dmDiscoveryConfiguringTimer(dmRoot, onePortContext, oneDeviceData);
      }
    }
    else
    {
      discovery->retries = 0;
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
  }
  else
  {
     DM_DBG1(("dmReportGeneralRespRcvd: SMP failed; fn result 0x%x; stopping discovery !!!\n", frameHeader->smpFunctionResult));
     dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }
  return;
}

osGLOBAL void
dmReportGeneral2RespRcvd(
                        dmRoot_t              *dmRoot,
                        agsaRoot_t            *agRoot,
                        agsaIORequest_t       *agIORequest,
                        dmDeviceData_t        *oneDeviceData,
                        dmSMPFrameHeader_t    *frameHeader,
                        agsaFrameHandle_t     frameHandle
                        )
{
  smpRespReportGeneral2_t      dmSMPReportGeneral2Resp;
  smpRespReportGeneral2_t      *pdmSMPReportGeneral2Resp;
  dmExpander_t                 *oneExpander = agNULL;
  dmIntPortContext_t           *onePortContext = agNULL;
  dmDiscovery_t                *discovery;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t           *dmSMPRequestBody;
  dmSMPRequestBody_t           *dmSMPResponseBody = agNULL;
#endif
  bit32                        ConfiguresOthers = agFALSE;
  dmIntRoot_t         *dmIntRoot   = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t      *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;


  DM_DBG2(("dmReportGeneral2RespRcvd: start\n"));
  DM_DBG2(("dmReportGeneral2RespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG2(("dmReportGeneral2RespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifndef DIRECT_SMP
  dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
  pdmSMPReportGeneral2Resp = &dmSMPReportGeneral2Resp;

  dm_memset(&dmSMPReportGeneral2Resp, 0, sizeof(smpRespReportGeneral2_t));

#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pdmSMPReportGeneral2Resp, sizeof(smpRespReportGeneral2_t));
#else
  dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
  saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 4, pdmSMPReportGeneral2Resp, sizeof(smpRespReportGeneral2_t));
#endif

  onePortContext = oneDeviceData->dmPortContext;
  discovery = &(onePortContext->discovery);
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmReportGeneral2RespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

/* ??? start here */
  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    oneDeviceData->numOfPhys = (bit8) pdmSMPReportGeneral2Resp->numOfPhys;
    oneExpander = oneDeviceData->dmExpander;
    oneExpander->routingIndex = (bit16) SAS2_REPORT_GENERAL_GET_ROUTEINDEXES(pdmSMPReportGeneral2Resp);
    oneExpander->configReserved = 0;
    oneExpander->configRouteTable = SAS2_REPORT_GENERAL_IS_CONFIGURABLE(pdmSMPReportGeneral2Resp) ? 1 : 0;
    oneExpander->configuring = SAS2_REPORT_GENERAL_IS_CONFIGURING(pdmSMPReportGeneral2Resp) ? 1 : 0;
    oneExpander->TTTSupported = SAS2_REPORT_GENERAL_IS_TABLE_TO_TABLE_SUPPORTED(pdmSMPReportGeneral2Resp) ? 1 : 0;
    ConfiguresOthers = SAS2_REPORT_GENERAL_IS_CONFIGURES_OTHERS(pdmSMPReportGeneral2Resp) ? 1 : 0;

    DM_DBG2(("dmReportGeneral2RespRcvd: SAS 2 is %d\n", oneExpander->SAS2));
    DM_DBG3(("dmReportGeneral2RespRcvd: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
    DM_DBG3(("dmReportGeneral2RespRcvd: oneExpander %p did %d\n", oneExpander, oneExpander->id));


    DM_DBG2(("dmReportGeneral2RespRcvd: oneExpander=%p numberofPhys=0x%x RoutingIndex=0x%x\n",
      oneExpander, oneDeviceData->numOfPhys, oneExpander->routingIndex));
    DM_DBG2(("dmReportGeneral2RespRcvd: configRouteTable=%d configuring=%d\n",
      oneExpander->configRouteTable, oneExpander->configuring));
    if (ConfiguresOthers)
    {
      DM_DBG2(("dmReportGeneral2RespRcvd: ConfiguresOthers is true\n"));
      discovery->ConfiguresOthers = agTRUE;
    }
    if (oneExpander->configuring == 1)
    {
      discovery->retries++;
      if (discovery->retries >= dmAllShared->MaxRetryDiscovery)
      {
        DM_DBG1(("dmReportGeneral2RespRcvd: retries are over!!!\n"));
        DM_DBG1(("dmReportGeneral2RespRcvd: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));

        discovery->retries = 0;
        /* failed the discovery */
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      }
      else
      {
        DM_DBG2(("dmReportGeneral2RespRcvd: keep retrying\n"));
        DM_DBG1(("dmReportGeneral2RespRcvd: Prep222389 RETRY at %d Maximum Retry is %d\n", discovery->retries, dmAllShared->MaxRetryDiscovery));
        DM_DBG1(("dmReportGeneral2RespRcvd: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));
        // start timer for sending ReportGeneral
        dmDiscoveryConfiguringTimer(dmRoot, onePortContext, oneDeviceData);
      }
    }
    else
    {
      discovery->retries = 0;
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
  }
  else
  {
     DM_DBG2(("dmReportGeneral2RespRcvd: SMP failed, stopping discovery\n"));
     dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }

  return;
}


osGLOBAL void
dmDiscoverSend(
               dmRoot_t             *dmRoot,
               dmDeviceData_t       *oneDeviceData
              )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t                *agRoot = dmAllShared->agRoot;
  smpReqDiscover_t          smpDiscoverReq;
  dmExpander_t              *oneExpander;

  DM_DBG3(("dmDiscoverSend: start\n"));
  DM_DBG3(("dmDiscoverSend: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  oneExpander = oneDeviceData->dmExpander;
  DM_DBG3(("dmDiscoverSend: oneExpander %p did %d\n", oneExpander, oneExpander->id));
  DM_DBG3(("dmDiscoverSend: phyID 0x%x\n", oneExpander->discoveringPhyId));

  dm_memset(&smpDiscoverReq, 0, sizeof(smpReqDiscover_t));

  smpDiscoverReq.reserved1 = 0;
  smpDiscoverReq.reserved2 = 0;
  smpDiscoverReq.phyIdentifier = oneExpander->discoveringPhyId;
  smpDiscoverReq.reserved3 = 0;

  dmSMPStart(
             dmRoot,
             agRoot,
             oneDeviceData,
             SMP_DISCOVER,
             (bit8 *)&smpDiscoverReq,
             sizeof(smpReqDiscover_t),
             AGSA_SMP_INIT_REQ
             );
  return;
}

osGLOBAL void
dmDiscoverRespRcvd(
                   dmRoot_t              *dmRoot,
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   dmDeviceData_t        *oneDeviceData,
                   dmSMPFrameHeader_t    *frameHeader,
                   agsaFrameHandle_t     frameHandle
                  )
{
  dmIntPortContext_t           *onePortContext = agNULL;
  dmDiscovery_t                *discovery;
  smpRespDiscover_t            *pdmSMPDiscoverResp;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t           *dmSMPRequestBody;
  dmSMPRequestBody_t           *dmSMPResponseBody = agNULL;
#endif
  dmExpander_t                 *oneExpander = agNULL;

  DM_DBG3(("dmDiscoverRespRcvd: start\n"));
  DM_DBG3(("dmDiscoverRespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDiscoverRespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->dmPortContext;
  oneExpander = oneDeviceData->dmExpander;
  discovery = &(onePortContext->discovery);
#ifndef DIRECT_SMP
  dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
  DM_DBG3(("dmDiscoverRespRcvd: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  DM_DBG3(("dmDiscoverRespRcvd: oneExpander %p did %d\n", oneExpander, oneExpander->id));

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDiscoverRespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  pdmSMPDiscoverResp = &(discovery->SMPDiscoverResp);

#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pdmSMPDiscoverResp, sizeof(smpRespDiscover_t));
#else
  dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
  saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 4, pdmSMPDiscoverResp, sizeof(smpRespDiscover_t));
#endif

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      dmUpStreamDiscoverExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscoverResp);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      dmDownStreamDiscoverExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscoverResp);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      DM_DBG3(("dmDiscoverRespRcvd: still configuring routing; setting timer\n"));
      DM_DBG3(("dmDiscoverRespRcvd: onePortContext %p oneDeviceData %p pdmSMPDiscoverResp %p\n", onePortContext, oneDeviceData, pdmSMPDiscoverResp));
      dmhexdump("dmDiscoverRespRcvd", (bit8*)pdmSMPDiscoverResp, sizeof(smpRespDiscover_t));

      dmConfigureRouteTimer(dmRoot, onePortContext, oneExpander, pdmSMPDiscoverResp, agNULL);
    }
    else
    {
      /* nothing */
    }
  }
  else if (frameHeader->smpFunctionResult == PHY_VACANT)
  {
    DM_DBG3(("dmDiscoverRespRcvd: smpFunctionResult is PHY_VACANT, phyid %d\n", oneExpander->discoveringPhyId));
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      dmUpStreamDiscoverExpanderPhySkip(dmRoot, onePortContext, oneExpander);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      dmDownStreamDiscoverExpanderPhySkip(dmRoot, onePortContext, oneExpander);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      DM_DBG3(("dmDiscoverRespRcvd: still configuring routing; setting timer\n"));
      DM_DBG3(("dmDiscoverRespRcvd: onePortContext %p oneDeviceData %p pdmSMPDiscoverResp %p\n", onePortContext, oneDeviceData, pdmSMPDiscoverResp));
      dmhexdump("dmDiscoverRespRcvd", (bit8*)pdmSMPDiscoverResp, sizeof(smpRespDiscover_t));

      dmConfigureRouteTimer(dmRoot, onePortContext, oneExpander, pdmSMPDiscoverResp, agNULL);
    }
  }
  else
  {
     DM_DBG1(("dmDiscoverRespRcvd: Discovery Error SMP function return result error=0x%x !!!\n",
               frameHeader->smpFunctionResult));
     dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }



  return;
}

osGLOBAL void
dmDiscover2RespRcvd(
                   dmRoot_t              *dmRoot,
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   dmDeviceData_t        *oneDeviceData,
                   dmSMPFrameHeader_t    *frameHeader,
                   agsaFrameHandle_t     frameHandle
                  )
{
  dmIntPortContext_t           *onePortContext = agNULL;
  dmDiscovery_t                *discovery;
  smpRespDiscover2_t           *pdmSMPDiscover2Resp;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t           *dmSMPRequestBody;
  dmSMPRequestBody_t           *dmSMPResponseBody = agNULL;
#endif
  dmExpander_t                 *oneExpander = agNULL;

  DM_DBG2(("dmDiscover2RespRcvd: start\n"));
  DM_DBG2(("dmDiscover2RespRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG2(("dmDiscover2RespRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->dmPortContext;
  oneExpander = oneDeviceData->dmExpander;
  discovery = &(onePortContext->discovery);
#ifndef DIRECT_SMP
  dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
  DM_DBG3(("dmDiscoverRespRcvd: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  DM_DBG3(("dmDiscoverRespRcvd: oneExpander %p did %d\n", oneExpander, oneExpander->id));

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDiscover2RespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  pdmSMPDiscover2Resp = &(discovery->SMPDiscover2Resp);

#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pdmSMPDiscover2Resp, sizeof(smpRespDiscover2_t));
#else
  dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
  saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 4, pdmSMPDiscover2Resp, sizeof(smpRespDiscover2_t));
#endif

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED )
  {
    DM_DBG2(("dmDiscover2RespRcvd: phyIdentifier %d\n", pdmSMPDiscover2Resp->phyIdentifier));
    DM_DBG2(("dmDiscover2RespRcvd: NegotiatedSSCHWMuxingSupported %d\n", pdmSMPDiscover2Resp->NegotiatedSSCHWMuxingSupported));
    DM_DBG2(("dmDiscover2RespRcvd: SAS2_MUXING_SUPPORTED %d\n", SAS2_DISCRSP_IS_MUXING_SUPPORTED(pdmSMPDiscover2Resp)));
    DM_DBG2(("dmDiscover2RespRcvd: NegotiatedLogicalLinkRate %d\n", pdmSMPDiscover2Resp->NegotiatedLogicalLinkRate));
    DM_DBG2(("dmDiscover2RespRcvd: ReasonNegotiatedPhysicalLinkRate %d\n", pdmSMPDiscover2Resp->ReasonNegotiatedPhysicalLinkRate));
    DM_DBG2(("dmDiscover2RespRcvd: SAS2_DISCRSP_GET_LOGICAL_LINKRATE %d\n", SAS2_DISCRSP_GET_LOGICAL_LINKRATE(pdmSMPDiscover2Resp)));
    DM_DBG2(("dmDiscover2RespRcvd: SAS2_DISCRSP_GET_LINKRATE %d\n", SAS2_DISCRSP_GET_LINKRATE(pdmSMPDiscover2Resp)));

//NegotiatedLogicalLinkRate 13
//ReasonNegotiatedPhysicalLinkRate 94
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      dmUpStreamDiscover2ExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscover2Resp);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      dmDownStreamDiscover2ExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscover2Resp);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      DM_DBG2(("dmDiscover2RespRcvd: still configuring routing; setting timer\n"));
      DM_DBG2(("dmDiscover2RespRcvd: onePortContext %p oneDeviceData %p pdmSMPDiscover2Resp %p\n", onePortContext, oneDeviceData, pdmSMPDiscover2Resp));
      dmhexdump("dmDiscover2RespRcvd", (bit8*)pdmSMPDiscover2Resp, sizeof(smpRespDiscover2_t));
      dmConfigureRouteTimer(dmRoot, onePortContext, oneExpander, agNULL, pdmSMPDiscover2Resp);
    }
    else
    {
      /* nothing */
    }
  }
  else if (frameHeader->smpFunctionResult == PHY_VACANT)
  {
    DM_DBG2(("dmDiscover2RespRcvd: smpFunctionResult is PHY_VACANT, phyid %d\n", oneExpander->discoveringPhyId));
    if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      dmUpStreamDiscover2ExpanderPhySkip(dmRoot, onePortContext, oneExpander);
    }
    else if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      dmDownStreamDiscover2ExpanderPhySkip(dmRoot, onePortContext, oneExpander);
    }
    else if (onePortContext->discovery.status == DISCOVERY_CONFIG_ROUTING)
    {
      /* not done with configuring routing
         1. set the timer
         2. on timer expiration, call tdsaSASDownStreamDiscoverExpanderPhy()
      */
      DM_DBG2(("dmDiscover2RespRcvd: still configuring routing; setting timer\n"));
      DM_DBG2(("dmDiscover2RespRcvd: onePortContext %p oneDeviceData %p pdmSMPDiscover2Resp %p\n", onePortContext, oneDeviceData, pdmSMPDiscover2Resp));
      dmhexdump("dmDiscover2RespRcvd", (bit8*)pdmSMPDiscover2Resp, sizeof(smpRespDiscover2_t));
      dmConfigureRouteTimer(dmRoot, onePortContext, oneExpander, agNULL, pdmSMPDiscover2Resp);
    }
    else
    {
      /* nothing */
    }
  }
  else
  {
     DM_DBG1(("dmDiscover2RespRcvd: Discovery Error SMP function return result error=0x%x\n",
               frameHeader->smpFunctionResult));
     dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }
  return;
}

#ifdef NOT_YET
osGLOBAL void
tdsaDiscoverList2Send(
                 tiRoot_t             *tiRoot,
                 tdsaDeviceData_t     *oneDeviceData
                 )
{
  agsaRoot_t            *agRoot;
  tdsaExpander_t        *oneExpander;
  smpReqDiscoverList2_t smpDiscoverListReq;

  DM_DBG1(("tdsaDiscoverList2Send: start\n"));
  DM_DBG1(("tdsaDiscoverList2Send: device %p did %d\n", oneDeviceData, oneDeviceData->id));
  agRoot = oneDeviceData->agRoot;
  oneExpander = oneDeviceData->dmExpander;
  DM_DBG1(("tdsaDiscoverList2Send: phyID 0x%x\n", oneExpander->discoveringPhyId));


  osti_memset(&smpDiscoverListReq, 0, sizeof(smpReqDiscoverList2_t));

  smpDiscoverListReq.reserved1 = 0;
  smpDiscoverListReq.StartingPhyID = 0;
  smpDiscoverListReq.MaxNumDiscoverDesc = 40; /* 40 for SHORT FORMAT; 8 for Long Format; SAS2 p630 */
  smpDiscoverListReq.byte10 = 0x2; /* phy filter; all but "no device attached" */
  smpDiscoverListReq.byte11 = 0x1; /* descriptor type; SHORT FORMAT */


  dmSMPStart(
             dmRoot,
             agRoot,
             oneDeviceData,
             SMP_DISCOVER_LIST,
             (bit8 *)&smpDiscoverListReq,
             sizeof(smpReqDiscoverList2_t),
             AGSA_SMP_INIT_REQ,
             agNULL
             );
  return;
}

osGLOBAL void
tdsaDiscoverList2RespRcvd(
                     tiRoot_t              *tiRoot,
                     agsaRoot_t            *agRoot,
                     tdsaDeviceData_t      *oneDeviceData,
                     tdssSMPFrameHeader_t  *frameHeader,
                     agsaFrameHandle_t     frameHandle
                     )
{
  return;
}
#endif /* not yet */

/*****************************************************************************
*! \brief  dmReportPhySataSend
*
*  Purpose:  This function sends Report Phy SATA to a device.
*
*  \param   dmRoot: Pointer to the OS Specific module allocated dmRoot_t
*                   instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   phyId: Phy Identifier.
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
dmReportPhySataSend(
                    dmRoot_t           *dmRoot,
                    dmDeviceData_t     *oneDeviceData,
                    bit8               phyId
                    )
{
  dmIntRoot_t        *dmIntRoot   = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t     *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t         *agRoot      = dmAllShared->agRoot;
  dmExpander_t       *oneExpander;
  smpReqReportPhySata_t  smpReportPhySataReq;

  DM_DBG3(("dmReportPhySataSend: start\n"));
  DM_DBG3(("dmReportPhySataSend: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
  DM_DBG3(("dmReportPhySataSend: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmReportPhySataSend: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  oneExpander = oneDeviceData->dmExpander;

  if (oneExpander == agNULL)
  {
    DM_DBG1(("dmReportPhySataSend: Error!!! expander is NULL\n"));
    return;
  }
  DM_DBG3(("dmReportPhySataSend: device %p did %d\n", oneDeviceData, oneDeviceData->id));
  DM_DBG3(("dmReportPhySataSend: phyid %d\n", phyId));

  dm_memset(&smpReportPhySataReq, 0, sizeof(smpReqReportPhySata_t));

  smpReportPhySataReq.phyIdentifier = phyId;

  dmSMPStart(
             dmRoot,
             agRoot,
             oneExpander->dmDevice,
             SMP_REPORT_PHY_SATA,
             (bit8 *)&smpReportPhySataReq,
             sizeof(smpReqReportPhySata_t),
             AGSA_SMP_INIT_REQ
             );

  return;
}
/*****************************************************************************
*! \brief  dmReportPhySataRcvd
*
*  Purpose:  This function processes Report Phy SATA response.
*
*  \param   dmRoot_t: Pointer to the OS Specific module allocated dmRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/

osGLOBAL void
dmReportPhySataRcvd(
                    dmRoot_t              *dmRoot,
                    agsaRoot_t            *agRoot,
                    agsaIORequest_t       *agIORequest,
                    dmDeviceData_t        *oneDeviceData,
                    dmSMPFrameHeader_t    *frameHeader,
                    agsaFrameHandle_t     frameHandle
                   )
{
  smpRespReportPhySata_t      SMPreportPhySataResp;
  smpRespReportPhySata_t      *pSMPReportPhySataResp;
  dmExpander_t                *oneExpander = oneDeviceData->dmExpander;
  dmIntPortContext_t          *onePortContext = agNULL;
  agsaFisRegDeviceToHost_t    *fis;
  dmDeviceData_t              *SataDevice = agNULL;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t          *tdSMPRequestBody;
#endif
  bit8                        sataDeviceType;
  bit8                        *bit8fis;
  bit8                        i = 0;
  bit32                       a = 0;
  bit8                        bit8fisarray[20];

  DM_DBG3(("dmReportPhySataRcvd: start\n"));
  DM_DBG3(("dmReportPhySataRcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmReportPhySataRcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifndef DIRECT_SMP
  tdSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
  /* get the current sata device hanlde stored in the expander structure */
  if (oneExpander != agNULL)
  {
      SataDevice = oneExpander->dmDeviceToProcess;
  }

  if (SataDevice != agNULL)
  {
    DM_DBG3(("dmReportPhySataRcvd: sasAddressHi 0x%08x\n", SataDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmReportPhySataRcvd: sasAddressLo 0x%08x\n", SataDevice->SASAddressID.sasAddressLo));
  }
  else
  {
    DM_DBG3(("dmReportPhySataRcvd: SataDevice is NULL\n"));
  }

  pSMPReportPhySataResp = &SMPreportPhySataResp;

#ifdef DIRECT_SMP
  saFrameReadBlock(agRoot, frameHandle, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#endif

  /* tdhexdump("dmReportPhySataRcvd", (bit8 *)pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));*/

#ifndef DIRECT_SMP
  ostiFreeMemory(
                 dmRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                );
  ostiFreeMemory(
                 dmRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                );
#endif

  onePortContext = oneDeviceData->dmPortContext;

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmReportPhySataRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  if (SataDevice == agNULL)
  {
    DM_DBG1(("dmReportPhySataRcvd: SataDevice is NULL, wrong\n"));
    dmDiscoverAbort(dmRoot, onePortContext);
    return;
  }

  if (frameHeader->smpFunctionResult == PHY_VACANT )
  {
     DM_DBG1(("dmReportPhySataRcvd: smpFunctionResult == PHY_VACANT, wrong\n"));
     return;
  }

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED  )
  {
    fis = (agsaFisRegDeviceToHost_t*) &SMPreportPhySataResp.regDevToHostFis;
    if (fis->h.fisType == REG_DEV_TO_HOST_FIS)
    {
      /* save signature */
      DM_DBG3(("dmReportPhySataRcvd: saves the signature\n"));
      /* saves signature */
      SataDevice->satSignature[0] = fis->d.sectorCount;
      SataDevice->satSignature[1] = fis->d.lbaLow;
      SataDevice->satSignature[2] = fis->d.lbaMid;
      SataDevice->satSignature[3] = fis->d.lbaHigh;
      SataDevice->satSignature[4] = fis->d.device;
      SataDevice->satSignature[5] = 0;
      SataDevice->satSignature[6] = 0;
      SataDevice->satSignature[7] = 0;

      DM_DBG3(("dmReportPhySataRcvd: SATA Signature = %02x %02x %02x %02x %02x\n",
       SataDevice->satSignature[0],
       SataDevice->satSignature[1],
       SataDevice->satSignature[2],
       SataDevice->satSignature[3],
       SataDevice->satSignature[4]));

       sataDeviceType = tddmSATADeviceTypeDecode(SataDevice->satSignature);
       if( sataDeviceType == SATA_ATAPI_DEVICE)
       {
          SataDevice->agDeviceInfo.flag |=  ATAPI_DEVICE_FLAG;
       }
       SataDevice->dmDeviceInfo.sataDeviceType = sataDeviceType;
    }
    /* Handling DataDomain buggy FIS */
    else if (fis->h.error == REG_DEV_TO_HOST_FIS)
    {
      /* needs to flip fis to host order */
      bit8fis = (bit8*)fis;
      for (i=0;i<5;i++)
      {
        a = DMA_LEBIT32_TO_BIT32(*(bit32*)bit8fis);
        DM_DBG3(("dmReportPhySataRcvd: a 0x%8x\n", a));
        bit8fisarray[4*i] = (a & 0xFF000000) >> 24;
        bit8fisarray[4*i+1] = (a & 0x00FF0000) >> 16;
        bit8fisarray[4*i+2] = (a & 0x0000FF00) >> 8;
        bit8fisarray[4*i+3] = (a & 0x000000FF);
        bit8fis = bit8fis + 4;
      }
      fis = (agsaFisRegDeviceToHost_t*) bit8fisarray;
      /* save signature */
      DM_DBG3(("dmReportPhySataRcvd: DataDomain ATAPI saves the signature\n"));
      /* saves signature */
      SataDevice->satSignature[0] = fis->d.sectorCount;
      SataDevice->satSignature[1] = fis->d.lbaLow;
      SataDevice->satSignature[2] = fis->d.lbaMid;
      SataDevice->satSignature[3] = fis->d.lbaHigh;
      SataDevice->satSignature[4] = fis->d.device;
      SataDevice->satSignature[5] = 0;
      SataDevice->satSignature[6] = 0;
      SataDevice->satSignature[7] = 0;

      DM_DBG3(("dmReportPhySataRcvd: SATA Signature = %02x %02x %02x %02x %02x\n",
       SataDevice->satSignature[0],
       SataDevice->satSignature[1],
       SataDevice->satSignature[2],
       SataDevice->satSignature[3],
       SataDevice->satSignature[4]));

       sataDeviceType = tddmSATADeviceTypeDecode(SataDevice->satSignature);
       if( sataDeviceType == SATA_ATAPI_DEVICE)
       {
          SataDevice->agDeviceInfo.flag |=  ATAPI_DEVICE_FLAG;
       }
       SataDevice->dmDeviceInfo.sataDeviceType = sataDeviceType;
    }
    else
    {
      DM_DBG3(("dmReportPhySataRcvd: getting next stp bride\n"));
    }

    /* Continure to report this STP device to TD*/
    if (SataDevice->ExpDevice != agNULL)
    {
       tddmReportDevice(dmRoot, onePortContext->dmPortContext, &SataDevice->dmDeviceInfo, &SataDevice->ExpDevice->dmDeviceInfo, dmDeviceArrival);
    }
    else
    {
       tddmReportDevice(dmRoot, onePortContext->dmPortContext, &SataDevice->dmDeviceInfo, agNULL, dmDeviceArrival);
    }
  }
  else
  {
    DM_DBG3(("dmReportPhySataRcvd: siReportPhySataRcvd SMP function return result %x\n",
             frameHeader->smpFunctionResult));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }

  return;
}

/*****************************************************************************
*! \brief  dmReportPhySata2Rcvd
*
*  Purpose:  This function processes SAS2.0 Report Phy SATA response.
*
*  \param   dmRoot_t: Pointer to the OS Specific module allocated dmRoot_t
*                   instance.
*  \param   agRoot: Pointer to chip/driver Instance.
*  \param   oneDeviceData: Pointer to the device data.
*  \param   frameHeader: Pointer to SMP frame header.
*  \param   frameHandle: A Handle used to refer to the response frame
*
*  \return:
*           None
*
*   \note:
*
*****************************************************************************/
osGLOBAL void
dmReportPhySata2Rcvd(
                    dmRoot_t              *dmRoot,
                    agsaRoot_t            *agRoot,
                    agsaIORequest_t       *agIORequest,
                    dmDeviceData_t        *oneDeviceData,
                    dmSMPFrameHeader_t    *frameHeader,
                    agsaFrameHandle_t     frameHandle
                   )
{
   smpRespReportPhySata2_t      SMPreportPhySataResp;
   smpRespReportPhySata2_t      *pSMPReportPhySataResp;
   dmExpander_t                *oneExpander = oneDeviceData->dmExpander;
   dmIntPortContext_t          *onePortContext = agNULL;
   agsaFisRegDeviceToHost_t    *fis;
   dmDeviceData_t              *SataDevice = agNULL;
#ifndef DIRECT_SMP
   dmSMPRequestBody_t          *tdSMPRequestBody;
#endif
   bit8                         sataDeviceType = 0;
   bit8                        *bit8fis;
   bit8                        i = 0;
   bit32                       a = 0;
   bit8                        bit8fisarray[20];

   DM_DBG3(("dmReportPhySata2Rcvd: start\n"));
   DM_DBG3(("dmReportPhySata2Rcvd: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
   DM_DBG3(("dmReportPhySata2Rcvd: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

#ifndef DIRECT_SMP
   tdSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
#endif
   /* get the current sata device hanlde stored in the expander structure */
   if (oneExpander != agNULL)
   {
     SataDevice = oneExpander->dmDeviceToProcess;
   }

   if (SataDevice != agNULL)
   {
     DM_DBG3(("dmReportPhySata2Rcvd: sasAddressHi 0x%08x\n", SataDevice->SASAddressID.sasAddressHi));
     DM_DBG3(("dmReportPhySata2Rcvd: sasAddressLo 0x%08x\n", SataDevice->SASAddressID.sasAddressLo));
   }
   else
   {
     DM_DBG3(("dmReportPhySataRcvd: SataDevice is NULL\n"));
   }

  pSMPReportPhySataResp = &SMPreportPhySataResp;

#ifdef DIRECT_SMP
   saFrameReadBlock(agRoot, frameHandle, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#else
   saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 4, pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));
#endif

   /* tdhexdump("dmReportPhySataRcvd", (bit8 *)pSMPReportPhySataResp, sizeof(smpRespReportPhySata_t));*/

#ifndef DIRECT_SMP
   ostiFreeMemory(
                  dmRoot,
                  tdSMPRequestBody->IndirectSMPReqosMemHandle,
                  tdSMPRequestBody->IndirectSMPReqLen
                 );
   ostiFreeMemory(
                  dmRoot,
                  tdSMPRequestBody->IndirectSMPResposMemHandle,
                  tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif

   onePortContext = oneDeviceData->dmPortContext;

   if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
   {
     DM_DBG1(("dmReportPhySata2Rcvd: invalid port or aborted discovery!!!\n"));
     return;
   }

   if (SataDevice == agNULL)
   {
     DM_DBG1(("dmReportPhySata2Rcvd: SataDevice is NULL, wrong\n"));
     dmDiscoverAbort(dmRoot, onePortContext);
     return;
   }

   if ( frameHeader->smpFunctionResult == PHY_VACANT )
   {
      DM_DBG1(("dmReportPhySata2Rcvd: smpFunctionResult == PHY_VACANT, wrong\n"));
      return;
   }

   if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED )
   {
     fis = (agsaFisRegDeviceToHost_t*) &SMPreportPhySataResp.regDevToHostFis;
     if (fis->h.fisType == REG_DEV_TO_HOST_FIS)
     {
       /* save signature */
       DM_DBG3(("dmReportPhySata2Rcvd: saves the signature\n"));
       /* saves signature */
       SataDevice->satSignature[0] = fis->d.sectorCount;
       SataDevice->satSignature[1] = fis->d.lbaLow;
       SataDevice->satSignature[2] = fis->d.lbaMid;
       SataDevice->satSignature[3] = fis->d.lbaHigh;
       SataDevice->satSignature[4] = fis->d.device;
       SataDevice->satSignature[5] = 0;
       SataDevice->satSignature[6] = 0;
       SataDevice->satSignature[7] = 0;
       DM_DBG3(("dmReportPhySata2Rcvd: SATA Signature = %02x %02x %02x %02x %02x\n",
        SataDevice->satSignature[0],
        SataDevice->satSignature[1],
        SataDevice->satSignature[2],
        SataDevice->satSignature[3],
        SataDevice->satSignature[4]));
       sataDeviceType = tddmSATADeviceTypeDecode(SataDevice->satSignature);
       if( sataDeviceType == SATA_ATAPI_DEVICE)
       {
          SataDevice->agDeviceInfo.flag |=  ATAPI_DEVICE_FLAG;
       }
       SataDevice->dmDeviceInfo.sataDeviceType = sataDeviceType;
    }
    /* Handling DataDomain buggy FIS */
    else if (fis->h.error == REG_DEV_TO_HOST_FIS)
    {
      /* needs to flip fis to host order */
      bit8fis = (bit8*)fis;
      for (i=0;i<5;i++)
      {
        a = DMA_LEBIT32_TO_BIT32(*(bit32*)bit8fis);
        DM_DBG3(("dmReportPhySata2Rcvd: a 0x%8x\n", a));
        bit8fisarray[4*i] = (a & 0xFF000000) >> 24;
        bit8fisarray[4*i+1] = (a & 0x00FF0000) >> 16;
        bit8fisarray[4*i+2] = (a & 0x0000FF00) >> 8;
        bit8fisarray[4*i+3] = (a & 0x000000FF);
        bit8fis = bit8fis + 4;
      }
      fis = (agsaFisRegDeviceToHost_t*) bit8fisarray;
      /* save signature */
      DM_DBG3(("dmReportPhySata2Rcvd: DataDomain ATAPI saves the signature\n"));
      /* saves signature */
      SataDevice->satSignature[0] = fis->d.sectorCount;
      SataDevice->satSignature[1] = fis->d.lbaLow;
      SataDevice->satSignature[2] = fis->d.lbaMid;
      SataDevice->satSignature[3] = fis->d.lbaHigh;
      SataDevice->satSignature[4] = fis->d.device;
      SataDevice->satSignature[5] = 0;
      SataDevice->satSignature[6] = 0;
      SataDevice->satSignature[7] = 0;
      DM_DBG3(("dmReportPhySata2Rcvd: SATA Signature = %02x %02x %02x %02x %02x\n",
       SataDevice->satSignature[0],
       SataDevice->satSignature[1],
       SataDevice->satSignature[2],
       SataDevice->satSignature[3],
       SataDevice->satSignature[4]));

       sataDeviceType = tddmSATADeviceTypeDecode(SataDevice->satSignature);
       if( sataDeviceType == SATA_ATAPI_DEVICE)
       {
          SataDevice->agDeviceInfo.flag |=  ATAPI_DEVICE_FLAG;
       }
       SataDevice->dmDeviceInfo.sataDeviceType = sataDeviceType;
    }
    else
    {
      DM_DBG3(("dmReportPhySata2Rcvd: getting next stp bride\n"));
    }

    /* Continue to report this STP device to TD*/
    if (SataDevice->ExpDevice != agNULL)
    {
       tddmReportDevice(dmRoot, onePortContext->dmPortContext, &SataDevice->dmDeviceInfo, &SataDevice->ExpDevice->dmDeviceInfo, dmDeviceArrival);
    }
    else
    {
       tddmReportDevice(dmRoot, onePortContext->dmPortContext, &SataDevice->dmDeviceInfo, agNULL, dmDeviceArrival);
    }

   }
   else
   {
     DM_DBG3(("dmReportPhySata2Rcvd: siReportPhySataRcvd SMP function return result %x\n",
              frameHeader->smpFunctionResult));
     dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
   }

   return;
}



osGLOBAL bit32
dmRoutingEntryAdd(
                  dmRoot_t          *dmRoot,
                  dmExpander_t      *oneExpander,
                  bit32             phyId,
                  bit32             configSASAddressHi,
                  bit32             configSASAddressLo
                 )
{
  dmIntRoot_t                             *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t                          *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t                              *agRoot = dmAllShared->agRoot;
  bit32                                   ret = agTRUE;
  dmIntPortContext_t                      *onePortContext;
  smpReqConfigureRouteInformation_t       confRoutingInfo;
  bit32                                   i;

  DM_DBG3(("dmRoutingEntryAdd: start\n"));
  DM_DBG3(("dmRoutingEntryAdd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmRoutingEntryAdd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmRoutingEntryAdd: phyid %d\n", phyId));

  if (oneExpander->dmDevice->SASAddressID.sasAddressHi == configSASAddressHi &&
      oneExpander->dmDevice->SASAddressID.sasAddressLo == configSASAddressLo
     )
  {
    DM_DBG3(("dmRoutingEntryAdd: unnecessary\n"));
    return ret;
  }
  if (oneExpander->routingAttribute[phyId] != SAS_ROUTING_TABLE)
  {
    DM_DBG3(("dmRoutingEntryAdd: not table routing, routing is %d\n", oneExpander->routingAttribute[phyId]));
    return ret;
  }

  onePortContext = oneExpander->dmDevice->dmPortContext;

  onePortContext->discovery.status = DISCOVERY_CONFIG_ROUTING;

  /* reset smpReqConfigureRouteInformation_t */
  dm_memset(&confRoutingInfo, 0, sizeof(smpReqConfigureRouteInformation_t));
  if ( oneExpander->currentIndex[phyId] < oneExpander->routingIndex )
  {
    DM_DBG3(("dmRoutingEntryAdd: adding sasAddressHi 0x%08x\n", configSASAddressHi));
    DM_DBG3(("dmRoutingEntryAdd: adding sasAddressLo 0x%08x\n", configSASAddressLo));
    DM_DBG3(("dmRoutingEntryAdd: phyid %d currentIndex[phyid] %d\n", phyId, oneExpander->currentIndex[phyId]));

    oneExpander->configSASAddressHi = configSASAddressHi;
    oneExpander->configSASAddressLo = configSASAddressLo;
    confRoutingInfo.reserved1[0] = 0;
    confRoutingInfo.reserved1[1] = 0;
    OSSA_WRITE_BE_16(agRoot, confRoutingInfo.expanderRouteIndex, 0, (oneExpander->currentIndex[phyId]));
    confRoutingInfo.reserved2 = 0;
    confRoutingInfo.phyIdentifier = (bit8)phyId;
    confRoutingInfo.reserved3[0] = 0;
    confRoutingInfo.reserved3[1] = 0;
    confRoutingInfo.disabledBit_reserved4 = 0;
    confRoutingInfo.reserved5[0] = 0;
    confRoutingInfo.reserved5[1] = 0;
    confRoutingInfo.reserved5[2] = 0;
    OSSA_WRITE_BE_32(agRoot, confRoutingInfo.routedSasAddressHi, 0, configSASAddressHi);
    OSSA_WRITE_BE_32(agRoot, confRoutingInfo.routedSasAddressLo, 0, configSASAddressLo);
    for ( i = 0; i < 16; i ++ )
    {
      confRoutingInfo.reserved6[i] = 0;
    }
    dmSMPStart(dmRoot, agRoot, oneExpander->dmDevice, SMP_CONFIGURE_ROUTING_INFORMATION, (bit8 *)&confRoutingInfo, sizeof(smpReqConfigureRouteInformation_t), AGSA_SMP_INIT_REQ);

    oneExpander->currentIndex[phyId] ++;
  }
  else
  {
    DM_DBG3(("dmRoutingEntryAdd: Discovery Error routing index overflow for currentIndex=%d, routingIndex=%d\n", oneExpander->currentIndex[phyId], oneExpander->routingIndex));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

    ret = agFALSE;
  }
  return ret;
}


osGLOBAL void
dmConfigRoutingInfoRespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            agsaIORequest_t       *agIORequest,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           )
{
  dmIntPortContext_t                    *onePortContext;
  dmExpander_t                          *oneExpander = oneDeviceData->dmExpander;
  dmExpander_t                          *UpStreamExpander;
  dmExpander_t                          *DownStreamExpander;
  dmExpander_t                          *ReturningExpander;
  dmExpander_t                          *ConfigurableExpander;
  dmDeviceData_t                        *ReturningExpanderDeviceData = agNULL;
  bit32                                 dupConfigSASAddr = agFALSE;


  DM_DBG3(("dmConfigRoutingInfoRespRcvd: start\n"));
  DM_DBG3(("dmConfigRoutingInfoRespRcvd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmConfigRoutingInfoRespRcvd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->dmPortContext;

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmConfigRoutingInfoRespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED ||
       frameHeader->smpFunctionResult == PHY_VACANT
     )
  {
    DownStreamExpander = oneExpander->dmCurrentDownStreamExpander;
    if (DownStreamExpander != agNULL)
    {
      DownStreamExpander->currentUpStreamPhyIndex ++;
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander->numOfUpStreamPhys %d\n", DownStreamExpander->numOfUpStreamPhys));
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander addrHi 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander addrLo 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressLo));

    }

    oneExpander->currentDownStreamPhyIndex++;
    DM_DBG3(("dmConfigRoutingInfoRespRcvd: oneExpander->currentDownStreamPhyIndex %d oneExpander->numOfDownStreamPhys %d\n", oneExpander->currentDownStreamPhyIndex, oneExpander->numOfDownStreamPhys));

    if ( (DownStreamExpander != agNULL) &&
         (DownStreamExpander->currentUpStreamPhyIndex < DownStreamExpander->numOfUpStreamPhys)
       )
    {
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: first if\n"));
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));

      DM_DBG3(("dmConfigRoutingInfoRespRcvd: DownStreamExpander->upStreamPhys[] %d\n", DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex]));

      dmRoutingEntryAdd(dmRoot,
                           oneExpander,
                           DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex],
                           oneExpander->configSASAddressHi,
                           oneExpander->configSASAddressLo
                          );
    }
    else
    {
      /* traversing up till discovery Root onePortContext->discovery.RootExp */
      DM_DBG3(("dmConfigRoutingInfoRespRcvd: else\n"));

      UpStreamExpander = oneExpander->dmUpStreamExpander;
      ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
      if (UpStreamExpander != agNULL)
      {
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: UpStreamExpander addrHi 0x%08x\n", UpStreamExpander->dmDevice->SASAddressID.sasAddressHi));
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: UpStreamExpander addrLo 0x%08x\n", UpStreamExpander->dmDevice->SASAddressID.sasAddressLo));
      }
      else
      {
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: UpStreamExpander is NULL\n"));
      }
      dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot,
                                                  ConfigurableExpander,
                                                  oneExpander->configSASAddressHi,
                                                  oneExpander->configSASAddressLo
                                                  );

      if ( ConfigurableExpander != agNULL && dupConfigSASAddr == agFALSE)
      {
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: else if\n"));

        DM_DBG3(("dmConfigRoutingInfoRespRcvd: ConfigurableExpander addrHi 0x%08x\n", ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi));
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: ConfigurableExpander addrLo 0x%08x\n", ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo));

        if ( UpStreamExpander != agNULL)
        {
          UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
        }
        ConfigurableExpander->currentDownStreamPhyIndex =
                dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
        ConfigurableExpander->dmReturnginExpander = oneExpander->dmReturnginExpander;
        if ( DownStreamExpander != agNULL)
        {
          DownStreamExpander->currentUpStreamPhyIndex = 0;
        }
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: ConfigurableExpander->currentDownStreamPhyIndex %d\n", ConfigurableExpander->currentDownStreamPhyIndex));

        DM_DBG3(("dmConfigRoutingInfoRespRcvd: ConfigurableExpander->downStreamPhys[] %d\n", ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex]));
        dmRoutingEntryAdd(dmRoot,
                             ConfigurableExpander,
                             ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                             oneExpander->configSASAddressHi,
                             oneExpander->configSASAddressLo
                            );
      }
      else
      {
        /* going back to where it was */
        /* ConfigRoutingInfo is done for a target */
        DM_DBG3(("dmConfigRoutingInfoRespRcvd: $$$$$$ my change $$$$$ \n"));
        ReturningExpander = oneExpander->dmReturnginExpander;
        if ( DownStreamExpander != agNULL)
        {
          DownStreamExpander->currentUpStreamPhyIndex = 0;
        }
        /* debugging */
        if (ReturningExpander != agNULL)
        {
          DM_DBG3(("dmConfigRoutingInfoRespRcvd: ReturningExpander addrHi 0x%08x\n", ReturningExpander->dmDevice->SASAddressID.sasAddressHi));
          DM_DBG3(("dmConfigRoutingInfoRespRcvd: ReturningExpander addrLo 0x%08x\n", ReturningExpander->dmDevice->SASAddressID.sasAddressLo));
          ReturningExpanderDeviceData = ReturningExpander->dmDevice;
        }

        /* No longer in DISCOVERY_CONFIG_ROUTING */
        onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

        if (ReturningExpander != agNULL && ReturningExpanderDeviceData != agNULL)
        {
      /* If not the last phy */
          if ( ReturningExpander->discoveringPhyId < ReturningExpanderDeviceData->numOfPhys )
          {
            DM_DBG3(("dmConfigRoutingInfoRespRcvd: More Phys to discover\n"));
            /* continue discovery for the next phy */
            /* needs to send only one Discovery not multiple times */
            if (ReturningExpander->discoverSMPAllowed == agTRUE)
            {
              dmDiscoverSend(dmRoot, ReturningExpanderDeviceData);
            }
            if (ReturningExpander != agNULL)
            {
              ReturningExpander->discoverSMPAllowed = agFALSE;
            }
          }
          /* If the last phy */
          else
          {
            DM_DBG3(("dmConfigRoutingInfoRespRcvd: No More Phys\n"));
            ReturningExpander->discoverSMPAllowed = agTRUE;

            /* remove the expander from the discovering list */
            dmDiscoveringExpanderRemove(dmRoot, onePortContext, ReturningExpander);
            /* continue downstream discovering */
            dmDownStreamDiscovering(dmRoot, onePortContext, ReturningExpanderDeviceData);

            //DownStreamExpander
          }
    }
      }
    }
  }
  else
  {
    DM_DBG1(("dmConfigRoutingInfoRespRcvd: Discovery Error SMP function return result error=0x%x !!!\n", frameHeader->smpFunctionResult));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }
  return;
}

osGLOBAL void
dmConfigRoutingInfo2RespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            agsaIORequest_t       *agIORequest,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           )
{
  dmExpander_t                            *oneExpander = oneDeviceData->dmExpander;
  dmExpander_t                            *UpStreamExpander;
  dmExpander_t                            *DownStreamExpander;
  dmExpander_t                            *ReturningExpander;
  dmExpander_t                            *ConfigurableExpander;

  dmIntPortContext_t                      *onePortContext;
  dmDeviceData_t                          *ReturningExpanderDeviceData = agNULL;
  bit32                                   dupConfigSASAddr = agFALSE;

  DM_DBG2(("dmConfigRoutingInfo2RespRcvd: start\n"));
  DM_DBG2(("dmConfigRoutingInfo2RespRcvd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG2(("dmConfigRoutingInfo2RespRcvd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));

  onePortContext = oneDeviceData->dmPortContext;

  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmConfigRoutingInfo2RespRcvd: invalid port or aborted discovery!!!\n"));
    return;
  }

  if (frameHeader->smpFunctionResult == PHY_VACANT)
  {
    DM_DBG1(("dmConfigRoutingInfo2RespRcvd: smpFunctionResult is PHY_VACANT\n"));
  }

  if ( frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED ||
       frameHeader->smpFunctionResult == PHY_VACANT
     )
  {
    DownStreamExpander = oneExpander->dmCurrentDownStreamExpander;
    if (DownStreamExpander != agNULL)
    {
      DownStreamExpander->currentUpStreamPhyIndex ++;
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander->numOfUpStreamPhys %d\n", DownStreamExpander->numOfUpStreamPhys));
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander addrHi 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander addrLo 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressLo));

    }

    oneExpander->currentDownStreamPhyIndex++;
    DM_DBG2(("dmConfigRoutingInfo2RespRcvd: oneExpander->currentDownStreamPhyIndex %d oneExpander->numOfDownStreamPhys %d\n", oneExpander->currentDownStreamPhyIndex, oneExpander->numOfDownStreamPhys));

    if ( (DownStreamExpander != agNULL) &&
         (DownStreamExpander->currentUpStreamPhyIndex < DownStreamExpander->numOfUpStreamPhys)
       )
    {
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: first if\n"));
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander->currentUpStreamPhyIndex %d\n", DownStreamExpander->currentUpStreamPhyIndex));

      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: DownStreamExpander->upStreamPhys[] %d\n", DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex]));

      dmRoutingEntryAdd(dmRoot,
                        oneExpander,
                        DownStreamExpander->upStreamPhys[DownStreamExpander->currentUpStreamPhyIndex],
                        oneExpander->configSASAddressHi,
                        oneExpander->configSASAddressLo
                       );
    }
    else
    {
      /* traversing up till discovery Root onePortContext->discovery.RootExp */
      DM_DBG2(("dmConfigRoutingInfo2RespRcvd: else\n"));

      UpStreamExpander = oneExpander->dmUpStreamExpander;
      ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
      if (UpStreamExpander != agNULL)
      {
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: UpStreamExpander addrHi 0x%08x\n", UpStreamExpander->dmDevice->SASAddressID.sasAddressHi));
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: UpStreamExpander addrLo 0x%08x\n", UpStreamExpander->dmDevice->SASAddressID.sasAddressLo));
      }
      else
      {
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: UpStreamExpander is NULL\n"));
      }
      dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot,
                                                  ConfigurableExpander,
                                                  oneExpander->configSASAddressHi,
                                                  oneExpander->configSASAddressLo
                                                  );

      if ( ConfigurableExpander != agNULL && dupConfigSASAddr == agFALSE)
      {
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: else if\n"));

        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ConfigurableExpander addrHi 0x%08x\n", ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi));
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ConfigurableExpander addrLo 0x%08x\n", ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo));

        if ( UpStreamExpander != agNULL)
        {
    UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
        }
        ConfigurableExpander->currentDownStreamPhyIndex =
                dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
        ConfigurableExpander->dmReturnginExpander = oneExpander->dmReturnginExpander;
        if ( DownStreamExpander != agNULL)
        {
          DownStreamExpander->currentUpStreamPhyIndex = 0;
        }
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ConfigurableExpander->currentDownStreamPhyIndex %d\n", ConfigurableExpander->currentDownStreamPhyIndex));

        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ConfigurableExpander->downStreamPhys[] %d\n", ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex]));
        dmRoutingEntryAdd(dmRoot,
                          ConfigurableExpander,
                          ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                          oneExpander->configSASAddressHi,
                          oneExpander->configSASAddressLo
                         );
      }
      else
      {
        /* going back to where it was */
        /* ConfigRoutingInfo is done for a target */
        DM_DBG2(("dmConfigRoutingInfo2RespRcvd: $$$$$$ my change $$$$$ \n"));
        ReturningExpander = oneExpander->dmReturnginExpander;
        if ( DownStreamExpander != agNULL)
        {
          DownStreamExpander->currentUpStreamPhyIndex = 0;
        }
        /* debugging */
        if (ReturningExpander != agNULL)
        {
           DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ReturningExpander addrHi 0x%08x\n", ReturningExpander->dmDevice->SASAddressID.sasAddressHi));
           DM_DBG2(("dmConfigRoutingInfo2RespRcvd: ReturningExpander addrLo 0x%08x\n", ReturningExpander->dmDevice->SASAddressID.sasAddressLo));
           ReturningExpanderDeviceData = ReturningExpander->dmDevice;
        }

        /* No longer in DISCOVERY_CONFIG_ROUTING */
        onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

        if (ReturningExpander != agNULL && ReturningExpanderDeviceData != agNULL)
        {
      /* If not the last phy */
          if ( ReturningExpander->discoveringPhyId < ReturningExpanderDeviceData->numOfPhys )
          {
            DM_DBG2(("dmConfigRoutingInfo2RespRcvd: More Phys to discover\n"));
            /* continue discovery for the next phy */
            /* needs to send only one Discovery not multiple times */
            if (ReturningExpander->discoverSMPAllowed == agTRUE)
            {
              dmDiscoverSend(dmRoot, ReturningExpanderDeviceData);
            }
            if (ReturningExpander != agNULL)
            {
              ReturningExpander->discoverSMPAllowed = agFALSE;
            }
          }
          /* If the last phy */
          else
          {
            DM_DBG2(("dmConfigRoutingInfo2RespRcvd: No More Phys\n"));
            ReturningExpander->discoverSMPAllowed = agTRUE;

            /* remove the expander from the discovering list */
            dmDiscoveringExpanderRemove(dmRoot, onePortContext, ReturningExpander);
            /* continue downstream discovering */
            dmDownStreamDiscovering(dmRoot, onePortContext, ReturningExpanderDeviceData);

            //DownStreamExpander
          }
        }
      }
    }
  }
  else
  {
    DM_DBG1(("dmConfigRoutingInfo2RespRcvd: Discovery Error SMP function return result error=0x%x!!!\n", frameHeader->smpFunctionResult));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }
  return;
}


/* no task management case here for phyControl*/

/* no task management case here for phyControl*/
osGLOBAL void
dmPhyControlRespRcvd(
                     dmRoot_t              *dmRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     dmDeviceData_t        *oneDeviceData,
                     dmSMPFrameHeader_t    *frameHeader,
                     agsaFrameHandle_t     frameHandle
                    )
{
  DM_DBG3(("dmPhyControlRespRcvd: start\n"));
  DM_DBG3(("dmPhyControlRespRcvd: expander device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmPhyControlRespRcvd: expander device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    DM_DBG3(("dmPhyControlRespRcvd: SMP success\n"));
  }
  else
  {
    DM_DBG1(("dmPhyControlRespRcvd: SMP failure; result 0x%x !!!\n", frameHeader->smpFunctionResult));
  }

  return;
}

/* no task management case here for phyControl*/
osGLOBAL void
dmPhyControl2RespRcvd(
                     dmRoot_t              *dmRoot,
                     agsaRoot_t            *agRoot,
                     agsaIORequest_t       *agIORequest,
                     dmDeviceData_t        *oneDeviceData,
                     dmSMPFrameHeader_t    *frameHeader,
                     agsaFrameHandle_t     frameHandle
                    )
{
  DM_DBG2(("dmPhyControl2RespRcvd: start\n"));
  DM_DBG2(("dmPhyControl2RespRcvd: expander device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG2(("dmPhyControl2RespRcvd: expander device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  if (frameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
  {
    DM_DBG2(("dmPhyControl2RespRcvd: SMP success\n"));
  }
  else
  {
    DM_DBG1(("dmPhyControl2RespRcvd: SMP failure; result 0x%x !!!\n", frameHeader->smpFunctionResult));
  }

  return;
}

osGLOBAL void
dmPhyControlFailureRespRcvd(
                            dmRoot_t              *dmRoot,
                            agsaRoot_t            *agRoot,
                            dmDeviceData_t        *oneDeviceData,
                            dmSMPFrameHeader_t    *frameHeader,
                            agsaFrameHandle_t     frameHandle
                           )
{
  DM_DBG1(("dmPhyControlFailureRespRcvd: start\n"));
  return;
}

GLOBAL void dmSetDeviceInfoCB(
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             option,
                                bit32             param
                                )
{
  dmRoot_t                  *dmRoot = agNULL;
  agsaIORequest_t           *agIORequest;
  bit32                     smstatus;
  agsaSASRequestBody_t      *agSASRequestBody;
  dmSMPRequestBody_t        *dmSMPRequestBody = agNULL;
  dmIntPortContext_t        *onePortContext = agNULL;
  dmDeviceData_t            *oneDeviceData;
  bit8                      SMPRequestFunction;
  bit8                      devType_S_Rate;
  DM_DBG1(("dmSetDeviceInfoCB: start\n"));
  DM_DBG4(("dmSetDeviceInfoCB: status 0x%x\n", status));
  DM_DBG4(("dmSetDeviceInfoCB: option 0x%x\n", option));
  DM_DBG4(("dmSetDeviceInfoCB: param 0x%x\n", param));
  if (status != OSSA_SUCCESS)
  {
    DM_DBG1(("dmSetDeviceInfoCB: status %d\n", status));
    DM_DBG1(("dmSetDeviceInfoCB: option 0x%x\n", option));
    DM_DBG1(("dmSetDeviceInfoCB: param 0x%x\n", param));
    if (option == 32) /* set connection rate */
    {
      DM_DBG1(("dmSetDeviceInfoCB: IO failure\n"));
      agIORequest = (agsaIORequest_t *)agContext->osData;
      dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
      dmRoot = dmSMPRequestBody->dmRoot;
      oneDeviceData = dmSMPRequestBody->dmDevice;
      onePortContext = oneDeviceData->dmPortContext;
      SMPRequestFunction = dmSMPRequestBody->smpPayload[1];
      if (SMPRequestFunction == SMP_REPORT_GENERAL ||
          SMPRequestFunction == SMP_DISCOVER ||
          SMPRequestFunction == SMP_REPORT_PHY_SATA ||
          SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
        )
      {
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      }
      else if (SMPRequestFunction == SMP_PHY_CONTROL)
      {
        /* task management failure */
        dmPhyControlFailureRespRcvd(
                                    dmRoot,
                                    agRoot,
                                    oneDeviceData,
                                    agNULL,
                                    agNULL
                                   );
      }
    }
  }
  if (agDevHandle == agNULL)
  {
    DM_DBG1(("dmSetDeviceInfoCB: agDevHandle is NULL\n"));
    return;
  }

  /* retry SMP */
  if (option == 32) /* set connection rate */
  {
    DM_DBG1(("dmSetDeviceInfoCB: set connection rate option\n"));
    agIORequest = (agsaIORequest_t *)agContext->osData;
    dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;
    dmRoot = dmSMPRequestBody->dmRoot;
    agSASRequestBody = &(dmSMPRequestBody->agSASRequestBody);
    oneDeviceData = dmSMPRequestBody->dmDevice;
    onePortContext = oneDeviceData->dmPortContext;
    devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate;
    devType_S_Rate = (devType_S_Rate & 0xF0) | (param >> 28);
    oneDeviceData->agDeviceInfo.devType_S_Rate =  devType_S_Rate;
    SMPRequestFunction = dmSMPRequestBody->smpPayload[1];
    DM_DBG1(("dmSetDeviceInfoCB: SMPRequestFunction 0x%x\n", SMPRequestFunction));
    DM_DBG1(("dmSetDeviceInfoCB: new rate is 0x%x\n", DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo)));
    smstatus = saSMPStart(
                          agRoot,
                          agIORequest,
                          0,
                          agDevHandle,
                          AGSA_SMP_INIT_REQ,
                          agSASRequestBody,
                          &dmsaSMPCompleted
                         );
    if (status == AGSA_RC_SUCCESS)
    {
      /* increment the number of pending SMP */
      onePortContext->discovery.pendingSMP++;
//          dmSMPRequestBody->retries++;
      if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
          SMPRequestFunction == SMP_REPORT_PHY_SATA ||
          SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
         )
      {
        /* start discovery-related SMP timer */
        dmDiscoverySMPTimer(dmRoot, onePortContext, (bit32)SMPRequestFunction, dmSMPRequestBody);
      }
      return;
    }
    else if (status == AGSA_RC_BUSY)
    {
      onePortContext->discovery.pendingSMP++;
//          dmSMPRequestBody->retries++;
      if (SMPRequestFunction == SMP_REPORT_GENERAL ||
          SMPRequestFunction == SMP_DISCOVER ||
          SMPRequestFunction == SMP_REPORT_PHY_SATA ||
          SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
         )
      {
        dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
      }
      else if (SMPRequestFunction == SMP_PHY_CONTROL)
      {
        /* For taskmanagement SMP, let's fail task management failure */
        dmPhyControlFailureRespRcvd(
                                    dmRoot,
                                    agRoot,
                                    oneDeviceData,
                                    agNULL,
                                    agNULL
                                   );
      }
      else
      {
      }
    }
    else /* AGSA_RC_FAILURE */
    {
      if (SMPRequestFunction == SMP_REPORT_GENERAL ||
          SMPRequestFunction == SMP_DISCOVER ||
          SMPRequestFunction == SMP_REPORT_PHY_SATA ||
          SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
         )
      {
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      }
      else if (SMPRequestFunction == SMP_PHY_CONTROL)
      {
        /* task management failure */
        dmPhyControlFailureRespRcvd(
                                    dmRoot,
                                    agRoot,
                                    oneDeviceData,
                                    agNULL,
                                    agNULL
                                   );
      }
      else
      {
      }
    }
  }
  return;
}
/* smp completion */
osGLOBAL void
dmSMPCompleted(
               agsaRoot_t            *agRoot,
               agsaIORequest_t       *agIORequest,
               bit32                 agIOStatus,
               bit32                 agIOInfoLen,
               agsaFrameHandle_t     agFrameHandle
              )
{
  dmIntRoot_t               *dmIntRoot    = agNULL;
  dmIntContext_t            *dmAllShared = agNULL;
  dmSMPRequestBody_t        *dmSMPRequestBody = agNULL;
  agsaSMPFrame_t            *agSMPFrame;
  dmRoot_t                  *dmRoot = agNULL;
  dmIntPortContext_t        *onePortContext = agNULL;
  dmIntPortContext_t        *oldonePortContext;
  dmExpander_t              *oneExpander = agNULL;
  dmDeviceData_t            *oneDeviceData;
  agsaDevHandle_t           *agDevHandle = agNULL;
  agsaSASRequestBody_t      *agSASRequestBody;
  bit8                      smpHeader[4];
  bit8                      SMPRequestFunction;
  dmSMPFrameHeader_t        *dmResponseSMPFrameHeader;
  dmSMPFrameHeader_t        *dmSMPFrameHeader;
  bit8                      *dmSMPPayload;
  smpReqPhyControl_t        *smpPhyControlReq;
  smpReqPhyControl2_t       *smpPhyControl2Req;
#ifndef DIRECT_SMP
  dmSMPRequestBody_t        *dmSMPResponseBody = agNULL;
  dmSMPFrameHeader_t        *dmRequestSMPFrameHeader;
  bit8                      smpRequestHeader[4];
#endif
  bit32                     status;
  bit32                     ConnRate = SAS_CONNECTION_RATE_12_0G;
  agsaContext_t             *agContext = agNULL;

  DM_DBG3(("dmSMPCompleted: start\n"));

  dmSMPRequestBody = (dmSMPRequestBody_t *)agIORequest->osData;

  dmRoot = dmSMPRequestBody->dmRoot;
  dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;

  oneDeviceData = dmSMPRequestBody->dmDevice;
  agSASRequestBody = &(dmSMPRequestBody->agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  if (oneDeviceData->valid == agFALSE &&
      oneDeviceData->valid2 == agFALSE &&
      oneDeviceData->dmPortContext == agNULL &&
      dmSMPRequestBody->dmPortContext->valid == agFALSE
      )
  {
    DM_DBG3(("dmSMPCompleted: port has been destroyed\n"));
    /* all device, port information have been reset
       just put smp to freeList
    */
    /* SMP request */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
    /* SMP response */
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    if (dmSMPResponseBody == agNULL)
    {
      DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
      return;
    }
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
    return;
  }

  onePortContext = oneDeviceData->dmPortContext;
  oneExpander = oneDeviceData->dmExpander;
  agDevHandle = oneExpander->agDevHandle;


#ifdef DIRECT_SMP
  SMPRequestFunction = dmSMPRequestBody->smpPayload[1];
#else
  saFrameReadBlock(agRoot, dmSMPRequestBody->IndirectSMP, 0, smpRequestHeader, 4);
  dmRequestSMPFrameHeader = (dmSMPFrameHeader_t *)smpRequestHeader;
  SMPRequestFunction = dmRequestSMPFrameHeader->smpFunction;
#endif

#ifdef NOT_IN_USE
  /* for debugging; dump SMP request payload */
  dmhexdump("smp payload",
            (bit8 *)agSASRequestBody->smpFrame.outFrameBuf,
            agSASRequestBody->smpFrame.outFrameLen
           );
  dmhexdump("smp payload new",
            (bit8 *)dmSMPRequestBody->smpPayload,
            agSASRequestBody->smpFrame.outFrameLen
           );
#endif

  /* sanity check */
  if (onePortContext != agNULL)
  {
    DM_DBG5(("dmSMPCompleted: pid %d\n", onePortContext->id));
  }
  else
  {
    DM_DBG1(("dmSMPCompleted: Wrong, onePortContext is NULL!!!\n"));
    /* SMP request */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
    /* SMP response */
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    if (dmSMPResponseBody == agNULL)
    {
      DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
      return;
    }
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
    return;
  }

  oldonePortContext = dmSMPRequestBody->dmPortContext;
  if (oldonePortContext != agNULL)
  {
    DM_DBG5(("dmSMPCompleted: old pid %d\n", oldonePortContext->id));
  }
  else
  {
    DM_DBG1(("dmSMPCompleted: Wrong, oldonePortContext is NULL!!!\n"));
    /* SMP request */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
    /* SMP response */
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    if (dmSMPResponseBody == agNULL)
    {
      DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
      return;
    }
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
    return;
  }

  /* decrement the number of pending SMP */
  onePortContext->discovery.pendingSMP--;


  /* for port invalid case;
     full discovery -> full discovery; incremental discovery -> full discovery
   */
  if (onePortContext != oldonePortContext)
  {
    DM_DBG1(("dmSMPCompleted: portcontext has changed!!!\n"));
    if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
        SMPRequestFunction == SMP_REPORT_PHY_SATA ||
        SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
        )
    {
      /* stop SMP timer */
      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
        dmKillTimer(
                      dmRoot,
                      &(onePortContext->discovery.DiscoverySMPTimer)
                     );
      }
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      }

      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
        dmKillTimer(
                      dmRoot,
                      &(oldonePortContext->discovery.DiscoverySMPTimer)
                     );
      }
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      }
    }
    /* clean up expanders data strucures; move to free exp when device is cleaned */
    dmCleanAllExp(dmRoot, oldonePortContext);
    /* remove devices */
    dmInternalRemovals(dmRoot, oldonePortContext);

    /* SMP request */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
    /* SMP response */
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    if (dmSMPResponseBody == agNULL)
    {
      DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
      return;
    }
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif


    return;
  }

  if (onePortContext->valid == agFALSE ||
      onePortContext->DiscoveryState == DM_DSTATE_COMPLETED ||
      onePortContext->discovery.status == DISCOVERY_SAS_DONE  ||
      onePortContext->DiscoveryAbortInProgress == agTRUE
     )
  {
    if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
        SMPRequestFunction == SMP_REPORT_PHY_SATA ||
        SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
        )
    {
      /* stop SMP timer */
      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
        dmKillTimer(
                    dmRoot,
                    &(onePortContext->discovery.DiscoverySMPTimer)
                   );
      }
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      }



      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
        dmKillTimer(
                    dmRoot,
                    &(oldonePortContext->discovery.DiscoverySMPTimer)
                   );
      }
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      }
    }

    /* SMP request */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
    /* SMP response */
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    if (dmSMPResponseBody == agNULL)
    {
      DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
      return;
    }
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
    tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

    if (onePortContext->discovery.pendingSMP == 0)
    {
      DM_DBG1(("dmSMPCompleted: aborting discovery\n"));
      if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED ||
          onePortContext->discovery.status == DISCOVERY_SAS_DONE ||
          onePortContext->DiscoveryAbortInProgress == agTRUE
         )
      {
        onePortContext->DiscoveryAbortInProgress = agFALSE;
        onePortContext->DiscoveryState = DM_DSTATE_COMPLETED;
        onePortContext->discovery.status = DISCOVERY_SAS_DONE;
        dmCleanAllExp(dmRoot, onePortContext);
        if ( onePortContext->DiscoveryAbortInProgress == agTRUE)
        {
          tddmDiscoverCB(
                         dmRoot,
                         onePortContext->dmPortContext,
                         dmDiscAborted
                  );
        }
      }
    }
    else
    {
      DM_DBG3(("dmSMPCompleted: not yet abort; non zero pendingSMP %d\n", onePortContext->discovery.pendingSMP));
    }
    return;
  }

  if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
      SMPRequestFunction == SMP_REPORT_PHY_SATA ||
      SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
      )
  {
    /* stop SMP timer */
    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      dmKillTimer(
                  dmRoot,
                  &(onePortContext->discovery.DiscoverySMPTimer)
                 );
    }
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    }


    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      dmKillTimer(
                  dmRoot,
                  &(oldonePortContext->discovery.DiscoverySMPTimer)
                 );
    }
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    }
  }

  if (oneExpander->SAS2 == 0)
  {
    DM_DBG3(("dmSMPCompleted: SAS 1.1\n"));
    if (agIOStatus == OSSA_IO_SUCCESS)
    {
      //tdhexdump("dmSMPCompleted", (bit8*)agFrameHandle, agIOInfoLen);
      /* parsing SMP payload */
#ifdef DIRECT_SMP
      saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
#else
      dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
      saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 0, smpHeader, 4);
#endif
      dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;

      /* SMP function dependent payload */
      switch (dmResponseSMPFrameHeader->smpFunction)
      {
      case SMP_REPORT_GENERAL:
        DM_DBG3(("dmSMPCompleted: report general\n"));
        if (agIOInfoLen != sizeof(smpRespReportGeneral_t) + 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
        {
          DM_DBG3(("dmSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespReportGeneral_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
          return;
        }
        /* start here */
        dmReportGeneralRespRcvd(
                                dmRoot,
                                agRoot,
                                agIORequest,
                                oneDeviceData,
                                dmResponseSMPFrameHeader,
                                agFrameHandle
                                );
        break;
      case SMP_DISCOVER:
        DM_DBG3(("dmSMPCompleted: discover\n"));
        if (agIOInfoLen != sizeof(smpRespDiscover_t) + 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
        {
          DM_DBG3(("dmSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespDiscover_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
          return;
        }
        dmDiscoverRespRcvd(
                           dmRoot,
                           agRoot,
                           agIORequest,
                           oneDeviceData,
                           dmResponseSMPFrameHeader,
                           agFrameHandle
                           );
        break;
      case SMP_REPORT_PHY_SATA:
        DM_DBG3(("dmSMPCompleted: report phy sata\n"));
        if (agIOInfoLen != sizeof(smpRespReportPhySata_t) + 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
        {
          DM_DBG3(("dmSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespReportPhySata_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
          return;
        }
        dmReportPhySataRcvd(
                            dmRoot,
                            agRoot,
                            agIORequest,
                            oneDeviceData,
                            dmResponseSMPFrameHeader,
                            agFrameHandle
                            );
        break;
      case SMP_CONFIGURE_ROUTING_INFORMATION:
        DM_DBG3(("dmSMPCompleted: configure routing information\n"));
        if (agIOInfoLen != 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
        {
          DM_DBG3(("dmSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
          return;
        }
        dmConfigRoutingInfoRespRcvd(
                                    dmRoot,
                                    agRoot,
                                    agIORequest,
                                    oneDeviceData,
                                    dmResponseSMPFrameHeader,
                                    agFrameHandle
                                    );

        break;
      case SMP_PHY_CONTROL:
        DM_DBG3(("dmSMPCompleted: phy control\n"));
        if (agIOInfoLen != 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED) /*zero length is expected */
        {
          DM_DBG3(("dmSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif
          return;
        }
        dmPhyControlRespRcvd(
                             dmRoot,
                             agRoot,
                             agIORequest,
                             oneDeviceData,
                             dmResponseSMPFrameHeader,
                             agFrameHandle
                            );
        break;
      case SMP_REPORT_ROUTING_INFORMATION: /* fall through */
      case SMP_REPORT_PHY_ERROR_LOG: /* fall through */
      case SMP_PHY_TEST_FUNCTION: /* fall through */
      case SMP_REPORT_MANUFACTURE_INFORMATION: /* fall through */
      case SMP_READ_GPIO_REGISTER: /* fall through */
      case SMP_WRITE_GPIO_REGISTER: /* fall through */
      default:
        DM_DBG1(("dmSMPCompleted: wrong SMP function 0x%x !!!\n", dmResponseSMPFrameHeader->smpFunction));
        DM_DBG1(("dmSMPCompleted: smpFrameType 0x%x !!!\n", dmResponseSMPFrameHeader->smpFrameType));
        DM_DBG1(("dmSMPCompleted: smpFunctionResult 0x%x !!!\n", dmResponseSMPFrameHeader->smpFunctionResult));
        DM_DBG1(("dmSMPCompleted: smpReserved 0x%x !!!\n", dmResponseSMPFrameHeader->smpReserved));
        dmhexdump("dmSMPCompleted: SMP payload !!!", (bit8 *)agFrameHandle, agIOInfoLen);
        break;
      } /* switch */
    } /* OSSA_IO_SUCCESS */
    else if (agIOStatus == OSSA_IO_ABORTED || agIOStatus == OSSA_IO_INVALID_LENGTH)
    {
      /* no retry this case */
      DM_DBG1(("dmSMPCompleted: OSSA_IO_ABORTED or OSSA_IO_INVALID_LENGTH, status 0x%x\n", agIOStatus));
    }
    else if (agIOStatus == OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE)
    {
      DM_DBG3(("dmSMPCompleted: OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE\n"));
      saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
      dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;

      status = saSMPStart(
                 agRoot,
                 agIORequest,
                 0,
                 agDevHandle,
                 AGSA_SMP_INIT_REQ,
                 agSASRequestBody,
                 &dmsaSMPCompleted
                 );

      if (status == AGSA_RC_SUCCESS)
      {
        /* increment the number of pending SMP */
        onePortContext->discovery.pendingSMP++;
        if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
            SMPRequestFunction == SMP_REPORT_PHY_SATA ||
            SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
           )
        {
          /* start discovery-related SMP timer */
          dmDiscoverySMPTimer(dmRoot, onePortContext, (bit32)(dmResponseSMPFrameHeader->smpFunction), dmSMPRequestBody);
        }
        return;
      }
      else if (status == AGSA_RC_BUSY)
      {
        if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
            dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
            dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
            dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
           )
        {
          dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
        }
        else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
        {
          /* For taskmanagement SMP, let's fail task management failure */
          dmPhyControlFailureRespRcvd(
                                      dmRoot,
                                      agRoot,
                                      oneDeviceData,
                                      dmResponseSMPFrameHeader,
                                      agFrameHandle
                                     );
        }
        else
        {
        }
      }
      else /* AGSA_RC_FAILURE */
      {
        if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
            dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
            dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
            dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
           )
        {
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
        }
        else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
        {
          /* task management failure */
          dmPhyControlFailureRespRcvd(
                                      dmRoot,
                                      agRoot,
                                      oneDeviceData,
                                      dmResponseSMPFrameHeader,
                                      agFrameHandle
                                     );
        }
        else
        {
        }
      }
    }   /* OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE*/
    else
    {
      if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED ||
          agIOStatus == OSSA_IO_DS_NON_OPERATIONAL )
      {
        DM_DBG1(("dmSMPCompleted: setting back to operational\n"));
        saSetDeviceState(agRoot, agNULL, 0, agDevHandle, SA_DS_OPERATIONAL);
      }
      if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED && dmAllShared->RateAdjust)
      {
        DM_DBG1(("dmSMPCompleted: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n"));
        DM_DBG1(("dmSMPCompleted: SMPRequestFunction 0x%x\n", SMPRequestFunction));
        ConnRate = DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo);
        if (ConnRate == SAS_CONNECTION_RATE_1_5G)
        {
          /* no retry; failure ??? */
          if (SMPRequestFunction == SMP_REPORT_GENERAL ||
              SMPRequestFunction == SMP_DISCOVER ||
              SMPRequestFunction == SMP_REPORT_PHY_SATA ||
              SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
             )
          {
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          else if (SMPRequestFunction == SMP_PHY_CONTROL)
          {
            /* task management failure */
            dmPhyControlFailureRespRcvd(
                                        dmRoot,
                                        agRoot,
                                        oneDeviceData,
                                        agNULL,
                                        agNULL
                                       );
          }
          else
          {
          }
        }
        else
        {
          ConnRate = ConnRate - 1;
        }
        agContext = &(dmSMPRequestBody->agContext);
        agContext->osData = agIORequest;
        saSetDeviceInfo(agRoot, agContext, 0, agDevHandle, 32, ConnRate << 28, dmSetDeviceInfoCB);
      }
      else
      {
        if (dmSMPRequestBody->retries < SMP_RETRIES) /* 5 */
        {
          /* retry the SMP again */
          DM_DBG1(("dmSMPCompleted: failed, but retries %d agIOStatus 0x%x %d agIOInfoLen %d !!!\n",
                   dmSMPRequestBody->retries, agIOStatus, agIOStatus, agIOInfoLen));
          saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
          dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;
          status = saSMPStart(
                              agRoot,
                              agIORequest,
                              0,
                              agDevHandle,
                              AGSA_SMP_INIT_REQ,
                              agSASRequestBody,
                              &dmsaSMPCompleted
                             );
          if (status == AGSA_RC_SUCCESS)
          {
            /* increment the number of pending SMP */
            onePortContext->discovery.pendingSMP++;
            dmSMPRequestBody->retries++;
            if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
                SMPRequestFunction == SMP_REPORT_PHY_SATA ||
                SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              /* start discovery-related SMP timer */
              dmDiscoverySMPTimer(dmRoot, onePortContext, (bit32)(dmResponseSMPFrameHeader->smpFunction), dmSMPRequestBody);
            }
            return;
          }
          else if (status == AGSA_RC_BUSY)
          {
            onePortContext->discovery.pendingSMP++;
            dmSMPRequestBody->retries++;
            if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
                dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
                dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
                dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
              return;
            }
            else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
            {
              /* For taskmanagement SMP, let's fail task management failure */
              dmPhyControlFailureRespRcvd(
                                          dmRoot,
                                          agRoot,
                                          oneDeviceData,
                                          dmResponseSMPFrameHeader,
                                          agFrameHandle
                                         );
            }
            else
            {
            }
          }
          else /* AGSA_RC_FAILURE */
          {
            if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
                dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
                dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
                dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
            else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
            {
              /* task management failure */
              dmPhyControlFailureRespRcvd(
                                          dmRoot,
                                          agRoot,
                                          oneDeviceData,
                                          dmResponseSMPFrameHeader,
                                          agFrameHandle
                                         );
            }
            else
            {
            }
          }
        }
        else
        {
          dmSMPFrameHeader = (dmSMPFrameHeader_t *)agSMPFrame->outFrameBuf;
          dmSMPPayload = (bit8 *)agSMPFrame->outFrameBuf + 4;
          DM_DBG1(("dmSMPCompleted: failed. no more retry. agIOStatus 0x%x %d !!!\n", agIOStatus, agIOStatus));
          if (agIOStatus == OSSA_IO_DS_NON_OPERATIONAL)
          {
            DM_DBG1(("dmSMPCompleted: failed, agIOStatus is OSSA_IO_DS_NON_OPERATIONAL!!!\n"));
          }
          if (agIOStatus == OSSA_IO_DS_IN_RECOVERY)
          {
            DM_DBG1(("dmSMPCompleted: failed, agIOStatus is OSSA_IO_DS_IN_RECOVERY!!!\n"));
          }
          if (dmSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
              dmSMPFrameHeader->smpFunction == SMP_DISCOVER ||
              dmSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
              dmSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
             )
          {
            /* discovery failure */
            DM_DBG1(("dmSMPCompleted: SMP function 0x%x\n", dmSMPFrameHeader->smpFunction));
            DM_DBG1(("dmSMPCompleted: discover done with error\n"));
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          else if (dmSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
          {
            DM_DBG3(("dmSMPCompleted: SMP_PHY_CONTROL\n"));
            smpPhyControlReq = (smpReqPhyControl_t *)dmSMPPayload;
            if (smpPhyControlReq->phyOperation == SMP_PHY_CONTROL_CLEAR_AFFILIATION)
            {
              DM_DBG3(("dmSMPCompleted: discover done with error\n"));
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
            else
            {
              DM_DBG3(("dmSMPCompleted: unknown phy operation 0x%x\n", smpPhyControlReq->phyOperation));
            }
          } /* SMP_PHY_CONTROL */
          else
          {
            DM_DBG3(("dmSMPCompleted: SMP function 0x%x\n", dmSMPFrameHeader->smpFunction));
          }
        } /* else */
      } /* for RateAdjust */
    } /* outer else */
  } /* SAS 1.1 */
  /************************************     SAS 2     ***********************************************/
  else
  {
    DM_DBG2(("dmSMPCompleted: SAS 2\n"));
    if (agIOStatus == OSSA_IO_SUCCESS)
    {
      //tdhexdump("dmSMPCompleted", (bit8*)agFrameHandle, agIOInfoLen);
      /* parsing SMP payload */
#ifdef DIRECT_SMP
    saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
#else
    dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
    saFrameReadBlock(agRoot, dmSMPResponseBody->IndirectSMP, 0, smpHeader, 4);
#endif
    dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;

      /* SMP function dependent payload */
      switch (dmResponseSMPFrameHeader->smpFunction)
      {
      case SMP_REPORT_GENERAL:
        DM_DBG2(("dmSMPCompleted: report general\n"));
        if ((agIOInfoLen != sizeof(smpRespReportGeneral2_t) + 4) &&
             dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED
           )
        {
          DM_DBG1(("dmSMPCompleted: report general mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (int)sizeof(smpRespReportGeneral2_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

          return;
        }

        dmReportGeneral2RespRcvd(
                                  dmRoot,
                                  agRoot,
                                  agIORequest,
                                  oneDeviceData,
                                  dmResponseSMPFrameHeader,
                                  agFrameHandle
                                  );
        break;
      case SMP_DISCOVER:
        DM_DBG2(("dmSMPCompleted: discover\n"));
        if ((agIOInfoLen != sizeof(smpRespDiscover2_t) + 4) &&
             dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED
           )
        {
          DM_DBG1(("dmSMPCompleted: discover mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (int)sizeof(smpRespDiscover2_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

          return;
        }
        dmDiscover2RespRcvd(
                             dmRoot,
                             agRoot,
                             agIORequest,
                                oneDeviceData,
                             dmResponseSMPFrameHeader,
                             agFrameHandle
                             );
        break;
      case SMP_REPORT_PHY_SATA:
        DM_DBG2(("dmSMPCompleted: report phy sata\n"));
        if ((agIOInfoLen != sizeof(smpRespReportPhySata2_t) + 4) &&
             dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED
           )
        {
          DM_DBG1(("dmSMPCompleted: report phy sata mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (int)sizeof(smpRespReportPhySata2_t) + 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

          return;
        }
        dmReportPhySata2Rcvd(
                              dmRoot,
                              agRoot,
                              agIORequest,
                              oneDeviceData,
                              dmResponseSMPFrameHeader,
                              agFrameHandle
                              );
        break;
      case SMP_CONFIGURE_ROUTING_INFORMATION:
        DM_DBG2(("dmSMPCompleted: configure routing information\n"));
        if (agIOInfoLen != 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED
           )
        {
          DM_DBG1(("dmSMPCompleted: configure routing information mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

          return;
        }
        dmConfigRoutingInfo2RespRcvd(
                                      dmRoot,
                                      agRoot,
                                      agIORequest,
                                      oneDeviceData,
                                      dmResponseSMPFrameHeader,
                                      agFrameHandle
                                      );

        break;
      case SMP_PHY_CONTROL:
        DM_DBG2(("dmSMPCompleted: phy control\n"));
        if (agIOInfoLen != 4 &&
            dmResponseSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED
           ) /*zero length is expected */
        {
          DM_DBG1(("dmSMPCompleted: phy control mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

          return;
        }
        dmPhyControl2RespRcvd(
                               dmRoot,
                               agRoot,
                               agIORequest,
             oneDeviceData,
                               dmResponseSMPFrameHeader,
                               agFrameHandle
            );


        break;
#ifdef NOT_YET
      case SMP_DISCOVER_LIST:
        DM_DBG1(("dmSMPCompleted: SMP_DISCOVER_LIST\n"));
        DM_DBG1(("dmSMPCompleted: agIOInfoLen 0x%x \n", agIOInfoLen));
        tdhexdump("dmSMPCompleted", (bit8*)agFrameHandle, agIOInfoLen);
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

          /* SMP request */
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
          /* SMP response */
          dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
          if (dmSMPResponseBody == agNULL)
          {
            DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
            return;
          }
          tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
          DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
          tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

        return;
        break;
#endif
      case SMP_REPORT_ROUTING_INFORMATION: /* fall through */
      case SMP_REPORT_PHY_ERROR_LOG: /* fall through */
      case SMP_PHY_TEST_FUNCTION: /* fall through */
      case SMP_REPORT_MANUFACTURE_INFORMATION: /* fall through */
      case SMP_READ_GPIO_REGISTER: /* fall through */
      case SMP_WRITE_GPIO_REGISTER: /* fall through */
      default:
        DM_DBG1(("dmSMPCompleted: wrong SMP function 0x%x\n", dmResponseSMPFrameHeader->smpFunction));
        DM_DBG1(("dmSMPCompleted: smpFrameType 0x%x\n", dmResponseSMPFrameHeader->smpFrameType));
        DM_DBG1(("dmSMPCompleted: smpFunctionResult 0x%x\n", dmResponseSMPFrameHeader->smpFunctionResult));
        DM_DBG1(("dmSMPCompleted: smpReserved 0x%x\n", dmResponseSMPFrameHeader->smpReserved));
        dmhexdump("dmSMPCompleted: SMP payload", (bit8 *)agFrameHandle, agIOInfoLen);
        break;
      }
    } /* agIOStatus == OSSA_IO_SUCCESS */
    else if (agIOStatus == OSSA_IO_ABORTED || agIOStatus == OSSA_IO_INVALID_LENGTH)
    {
      /* no retry this case */
      DM_DBG1(("dmSMPCompleted: OSSA_IO_ABORTED or OSSA_IO_INVALID_LENGTH, status 0x%x\n", agIOStatus));
    }
    else if (agIOStatus == OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE)
    {
      DM_DBG1(("dmSMPCompleted: OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE\n"));
      saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
      dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;

      status = saSMPStart(
                          agRoot,
                          agIORequest,
                          0,
                          agDevHandle,
                          AGSA_SMP_INIT_REQ,
                          agSASRequestBody,
                          &dmsaSMPCompleted
                         );


      if (status == AGSA_RC_SUCCESS)
      {
        /* increment the number of pending SMP */
        onePortContext->discovery.pendingSMP++;
        if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
            SMPRequestFunction == SMP_REPORT_PHY_SATA ||
            SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
           )
        {
          /* start discovery-related SMP timer */
          dmDiscoverySMPTimer(dmRoot, onePortContext, (bit32)(dmResponseSMPFrameHeader->smpFunction), dmSMPRequestBody);
        }
        return;
      }
      else if (status == AGSA_RC_BUSY)
      {
        if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
            dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
            dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
            dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
           )
        {
          dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
        }
        else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
        {
          /* For taskmanagement SMP, let's fail task management failure */
          dmPhyControlFailureRespRcvd(
                                      dmRoot,
                                      agRoot,
                                      oneDeviceData,
                                      dmResponseSMPFrameHeader,
                                      agFrameHandle
                                     );
        }
        else
        {
        }
      }
      else /* AGSA_RC_FAILURE */
      {
        if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
            dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
            dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
            dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
      )
        {
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
        }
        else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
        {
          /* task management failure */
          dmPhyControlFailureRespRcvd(
                                      dmRoot,
                                      agRoot,
                                      oneDeviceData,
                                      dmResponseSMPFrameHeader,
                                      agFrameHandle
                                     );
        }
        else
        {
        }
      }
    }
    else if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION)
    {
      DM_DBG1(("dmSMPCompleted: OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION\n"));
      /*
         skip to the next expander
      */
      dmHandleZoneViolation(
                           dmRoot,
                           agRoot,
                           agIORequest,
                           oneDeviceData,
                           agNULL,
                           agFrameHandle
                           );
    }
    else
    {
      if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED ||
          agIOStatus == OSSA_IO_DS_NON_OPERATIONAL )
      {
        DM_DBG1(("dmSMPCompleted: setting back to operational\n"));
        saSetDeviceState(agRoot, agNULL, 0, agDevHandle, SA_DS_OPERATIONAL);
      }
      if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED && dmAllShared->RateAdjust)
      {
        DM_DBG1(("dmSMPCompleted: OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED\n"));
        DM_DBG1(("dmSMPCompleted: SMPRequestFunction 0x%x\n", SMPRequestFunction));
        ConnRate = DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo);
        if (ConnRate == SAS_CONNECTION_RATE_1_5G)
        {
          /* no retry; failure ??? */
          if (SMPRequestFunction == SMP_REPORT_GENERAL ||
              SMPRequestFunction == SMP_DISCOVER ||
              SMPRequestFunction == SMP_REPORT_PHY_SATA ||
              SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
             )
          {
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          else if (SMPRequestFunction == SMP_PHY_CONTROL)
          {
            /* task management failure */
            dmPhyControlFailureRespRcvd(
                                        dmRoot,
                                        agRoot,
                                        oneDeviceData,
                                        agNULL,
                                        agNULL
                                       );
          }
          else
          {
          }
        }
        else
        {
          ConnRate = ConnRate - 1;
        }
        agContext = &(dmSMPRequestBody->agContext);
        agContext->osData = agIORequest;
        saSetDeviceInfo(agRoot, agContext, 0, agDevHandle, 32, ConnRate << 28, dmSetDeviceInfoCB);
      }
      else
      {
        if (dmSMPRequestBody->retries < SMP_RETRIES) /* 5 */
        {
          /* retry the SMP again */
          DM_DBG1(("dmSMPCompleted: failed! but retries %d agIOStatus 0x%x %d agIOInfoLen %d\n",
                   dmSMPRequestBody->retries, agIOStatus, agIOStatus, agIOInfoLen));
          saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
          dmResponseSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;
          status = saSMPStart(
                              agRoot,
                              agIORequest,
                              0,
                              agDevHandle,
                              AGSA_SMP_INIT_REQ,
                              agSASRequestBody,
                              &dmsaSMPCompleted
                             );

          if (status == AGSA_RC_SUCCESS)
          {
            /* increment the number of pending SMP */
            onePortContext->discovery.pendingSMP++;
            dmSMPRequestBody->retries++;
            if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
                SMPRequestFunction == SMP_REPORT_PHY_SATA ||
                SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              /* start discovery-related SMP timer */
              dmDiscoverySMPTimer(dmRoot, onePortContext, (bit32)(dmResponseSMPFrameHeader->smpFunction), dmSMPRequestBody);
            }
            return;
          }
          else if (status == AGSA_RC_BUSY)
          {
            onePortContext->discovery.pendingSMP++;
            dmSMPRequestBody->retries++;
            if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
                dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
                dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
                dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
              return;
            }
            else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
            {
              /* For taskmanagement SMP, let's fail task management failure */
              dmPhyControlFailureRespRcvd(
                                          dmRoot,
                                          agRoot,
                                          oneDeviceData,
                                          dmResponseSMPFrameHeader,
                                          agFrameHandle
                                         );
            }
            else
            {
            }
          }
          else /* AGSA_RC_FAILURE */
          {
            if (dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
                dmResponseSMPFrameHeader->smpFunction == SMP_DISCOVER ||
                dmResponseSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
                dmResponseSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
               )
            {
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
            else if (dmResponseSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
            {
              /* task management failure */
              dmPhyControlFailureRespRcvd(
                                          dmRoot,
                                          agRoot,
                                          oneDeviceData,
                                          dmResponseSMPFrameHeader,
                                          agFrameHandle
                                         );
            }
            else
            {
            }
          }
        }
        else
        {
          dmSMPFrameHeader = (dmSMPFrameHeader_t *)agSMPFrame->outFrameBuf;
          dmSMPPayload = (bit8 *)agSMPFrame->outFrameBuf + 4;
          DM_DBG1(("dmSMPCompleted: failed! no more retry! agIOStatus 0x%x %d\n", agIOStatus, agIOStatus));
          if (agIOStatus == OSSA_IO_DS_NON_OPERATIONAL)
          {
            DM_DBG1(("dmSMPCompleted: failed! agIOStatus is OSSA_IO_DS_NON_OPERATIONAL\n"));
          }
          if (agIOStatus == OSSA_IO_DS_IN_RECOVERY)
          {
            DM_DBG1(("dmSMPCompleted: failed! agIOStatus is OSSA_IO_DS_IN_RECOVERY\n"));
          }
          if (dmSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
              dmSMPFrameHeader->smpFunction == SMP_DISCOVER ||
              dmSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
              dmSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
             )
          {
            /* discovery failure */
            DM_DBG1(("dmSMPCompleted: SMP function 0x%x\n", dmSMPFrameHeader->smpFunction));
            DM_DBG1(("dmSMPCompleted: discover done with error\n"));
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          else if (dmSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
          {
            DM_DBG1(("dmSMPCompleted: SMP_PHY_CONTROL\n"));
            smpPhyControl2Req = (smpReqPhyControl2_t *)dmSMPPayload;
            if (smpPhyControl2Req->phyOperation == SMP_PHY_CONTROL_CLEAR_AFFILIATION)
            {
              DM_DBG1(("dmSMPCompleted: discover done with error\n"));
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
            else
            {
              DM_DBG1(("dmSMPCompleted: unknown phy operation 0x%x\n", smpPhyControl2Req->phyOperation));
            }
          } /* SMP_PHY_CONTROL */
          else
          {
            DM_DBG1(("dmSMPCompleted: SMP function 0x%x\n", dmSMPFrameHeader->smpFunction));
          }
        } /* else */
      } /* for RateAdjust */
    } /* outer else */
  } /* SAS 2 else */

  /* SMP request */
  tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
  DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
  tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

#ifndef DIRECT_SMP
  /* SMP response */
  dmSMPResponseBody = (dmSMPRequestBody_t *)dmSMPRequestBody->IndirectSMPResponse;
  if (dmSMPResponseBody == agNULL)
  {
    DM_DBG1(("dmSMPCompleted: Wrong, dmSMPResponseBody is NULL!!!\n"));
    return;
  }
  tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
  DMLIST_ENQUEUE_AT_TAIL(&(dmSMPResponseBody->Link), &(dmAllShared->freeSMPList));
  tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
#endif

  return;
}

osGLOBAL void
dmSMPAbortCB(
             agsaRoot_t           *agRoot,
             agsaIORequest_t      *agIORequest,
             bit32                flag,
             bit32                status)
{
  dmRoot_t             *dmRoot = agNULL;
  dmIntRoot_t          *dmIntRoot    = agNULL;
  dmIntContext_t       *dmAllShared = agNULL;
  dmSMPRequestBody_t   *dmSMPRequestBody = (dmSMPRequestBody_t *) agIORequest->osData;

  DM_DBG5(("dmSMPAbortCB: start\n"));

  if (dmSMPRequestBody == agNULL)
  {
    DM_DBG1(("dmSMPAbortCB: pSMPRequestBody is NULL!!! \n"));
    return;
  }

  dmRoot = dmSMPRequestBody->dmRoot;
  dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;


  /* put back into free smplist */
  tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
  DMLIST_ENQUEUE_AT_TAIL(&(dmSMPRequestBody->Link), &(dmAllShared->freeSMPList));
  tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);

  /* start here */
  if (flag == 2)
  {
    /* abort all per port */
    DM_DBG1(("dmSMPAbortCB: abort per port; not used!!!\n"));
  }
  else if (flag == 1)
  {
    /* abort all */
    DM_DBG1(("dmSMPAbortCB: abort all; not used!!!\n"));
  }
  else if (flag == 0)
  {
    /* abort one */
    DM_DBG1(("ossaSMPAbortCB: abort one\n"));
    if (status != OSSA_IO_SUCCESS)
    {
      DM_DBG1(("dmSMPAbortCB: abort one, status 0x%x\n", status));
    }
  }
  else
  {
    DM_DBG1(("dmSMPAbortCB: not allowed case, flag 0x%x!!!\n", flag));
  }

  return;
}


#endif /* FDS_DM */


