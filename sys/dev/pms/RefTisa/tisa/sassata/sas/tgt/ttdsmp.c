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
 * $RCSfile: ttdsmp.c,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 * $Author: hasungwo $
 * $Revision: 112322 $
 * $Date: 2012-01-04 19:23:42 -0800 (Wed, 04 Jan 2012) $
 *
 * This file contains initiator IO related functions in TD layer
 *
 */
#include <osenv.h>
#include <ostypes.h>
#include <osdebug.h>

#include <sa.h>
#include <saapi.h>
#include <saosapi.h>

#include <titypes.h>
#include <ostiapi.h>
#include <tiapi.h>
#include <tiglobal.h>

#include <tdtypes.h>
#include <osstring.h>
#include <tdutil.h>

#ifdef INITIATOR_DRIVER
#include <itdtypes.h>
#include <itddefs.h>
#include <itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include "ttdglobl.h"
#include "ttdtxchg.h"
#include "ttdtypes.h"
#endif

#include <tdsatypes.h>
#include <tdproto.h>

osGLOBAL void
ttdsaSMPCompleted(
                  agsaRoot_t            *agRoot,
                  agsaIORequest_t       *agIORequest,
                  bit32                 agIOStatus,
                  //agsaSMPFrameHeader_t  *agFrameHeader, //(TP)
                  bit32                 agIOInfoLen,
                  agsaFrameHandle_t     agFrameHandle
                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  ttdsaXchg_t            *ttdsaXchg    = (ttdsaXchg_t *)agIORequest->osData;

  /* cf) ttdsaIOCompleted */
  TI_DBG1(("ttdsaSMPCompleted: start\n"));
  if (tiRoot == agNULL)
  {
    TI_DBG1(("ttdsaSMPCompleted: tiRoot is NULL, wrong\n"));
    return;
  }

  if (ttdsaXchg == agNULL)
  {
    TI_DBG1(("ttdsaSMPCompleted: ttdsaXchg is NULL, wrong\n"));
    return;
  }

  ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);


  /* to-do: no callback to OS layer */
  return;
}

osGLOBAL void
ttdsaNotSupportRespSend(
                        agsaRoot_t            *agRoot,
                        agsaDevHandle_t       *agDevHandle,
                        ttdsaXchg_t           *ttdsaXchg,
                        bit8                  smpfn
                        )
{
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody;
  agsaSMPFrame_t            *agSMPFrame;
  agsaIORequest_t           *agIORequest;
  bit8                       SMPPayload[SMP_DIRECT_PAYLOAD_LIMIT];    /*(TP)*/
  tdssSMPFrameHeader_t       tdSMPFrameHeader;              /*(TP)*/

  TI_DBG1(("ttdsaNotSupportSend:\n"));
  agRequestType = AGSA_SMP_TGT_RESPONSE;

  agIORequest = &(ttdsaXchg->SMPRequestBody.agIORequest);

  agSASRequestBody = &(ttdsaXchg->SMPRequestBody.agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));    /*(TP)*/

  /* smp header */                            /*(TP)*/
  tdSMPFrameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  tdSMPFrameHeader.smpFunction = smpfn;
  tdSMPFrameHeader.smpFunctionResult = UNKNOWN_SMP_FUNCTION; /* unknown smp */
  tdSMPFrameHeader.smpReserved = 0;

  /*old*/
  //agSMPFrame->frameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  //agSMPFrame->frameHeader.smpFunction = smpfn;
  //agSMPFrame->frameHeader.smpFunctionResult = UNKNOWN_SMP_FUNCTION; /* unknown smp */

  osti_memcpy(SMPPayload, &tdSMPFrameHeader, 4);            /*TP)*/

  agSMPFrame->outFrameBuf = SMPPayload;                 /*(TP)*/
  agSMPFrame->outFrameAddrUpper32 = ttdsaXchg->smpresp.phyAddrUpper;
  agSMPFrame->outFrameAddrLower32 = ttdsaXchg->smpresp.phyAddrLower;
  agSMPFrame->outFrameLen = 0; /* no smp response payload */

  //agSMPFrame->phyId = ttdsaXchg->SMPphyId;

#ifdef RPM_SOC
  /* not work yet because of high priority q */
  saSMPStart(
             agRoot,
             agIORequest,
             agDevHandle,
             agRequestType,
             agSASRequestBody,
             &ossaSMPCompleted
             );
#else
  saSMPStart(
             agRoot,
             agIORequest,
             0, /* queue number */
             agDevHandle,
             agRequestType,
             agSASRequestBody,
             &ossaSMPCompleted
             );
#endif
  return;
}

osGLOBAL void
ttdsaDiscoverRespSend(
                      agsaRoot_t            *agRoot,
                      agsaDevHandle_t       *agDevHandle,
                      ttdsaXchg_t           *ttdsaXchg
                      )
{
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody;
  agsaSMPFrame_t            *agSMPFrame;
  smpRespDiscover_t         *Resp;
  smp_resp_t                *SMPResp;
  agsaIORequest_t           *agIORequest;
  bit8                       SMPPayload[SMP_DIRECT_PAYLOAD_LIMIT];    /*(TP)*/
  tdssSMPFrameHeader_t       tdSMPFrameHeader;              /*(TP)*/

  TI_DBG1(("ttdsaDiscoverRespSend:\n"));

  agRequestType = AGSA_SMP_TGT_RESPONSE;

  SMPResp = (smp_resp_t *)ttdsaXchg->smpresp.virtAddr;

  agIORequest = &(ttdsaXchg->SMPRequestBody.agIORequest);

  agSASRequestBody = &(ttdsaXchg->SMPRequestBody.agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));    /*(TP)*/

  /* smp header */                          /*(TP)*/
  tdSMPFrameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  tdSMPFrameHeader.smpFunction = SMP_DISCOVER; /* discover */
  tdSMPFrameHeader.smpFunctionResult = SMP_FUNCTION_ACCEPTED;
  tdSMPFrameHeader.smpReserved = 0;

  /*old*/
  //agSMPFrame->frameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  //agSMPFrame->frameHeader.smpFunction = SMP_DISCOVER; /* discover */
  //agSMPFrame->frameHeader.smpFunctionResult = SMP_FUNCTION_ACCEPTED;

  osti_memcpy(SMPPayload, &tdSMPFrameHeader, 4);            /*TP)*/

  agSMPFrame->outFrameBuf = SMPPayload;                 /*(TP)*/
  agSMPFrame->outFrameAddrUpper32 = ttdsaXchg->smpresp.phyAddrUpper;
  agSMPFrame->outFrameAddrLower32 = ttdsaXchg->smpresp.phyAddrLower;
  agSMPFrame->outFrameLen = sizeof(smpRespDiscover_t);

  //agSMPFrame->phyId = ttdsaXchg->SMPphyId;

  /* smp response payload */
  Resp = (smpRespDiscover_t *)&(SMPResp->RespData);
  osti_memset(Resp, 0, sizeof(smpRespDiscover_t));
  /* temp, hardcode smp discover response */
  /* needs to read contents from ID frame */
  /* assumption: for now, attached to edge expander */
  Resp->phyIdentifier = 0;
  Resp->attachedDeviceType = SAS_EDGE_EXPANDER_DEVICE;
  Resp->negotiatedPhyLinkRate = 0x9; /* enabled, 1.5G */
  Resp->attached_Ssp_Stp_Smp_Sata_Initiator = 0;
  Resp->attached_SataPS_Ssp_Stp_Smp_Sata_Target = 0x2; /* SMP target */
  Resp->sasAddressHi[3] = 0x01;
  Resp->sasAddressHi[2] = 0x02;
  Resp->sasAddressHi[1] = 0x03;
  Resp->sasAddressHi[0] = 0x04;
  Resp->sasAddressLo[3] = 0x05;
  Resp->sasAddressLo[2] = 0x06;
  Resp->sasAddressLo[1] = 0x07;
  Resp->sasAddressLo[0] = 0x08;

  Resp->attachedSasAddressHi[3] = 0x01;
  Resp->attachedSasAddressHi[2] = 0x01;
  Resp->attachedSasAddressHi[1] = 0x01;
  Resp->attachedSasAddressHi[0] = 0x01;
  Resp->attachedSasAddressLo[3] = 0x02;
  Resp->attachedSasAddressLo[2] = 0x02;
  Resp->attachedSasAddressLo[1] = 0x02;
  Resp->attachedSasAddressLo[0] = 0x02;

  Resp->attachedPhyIdentifier = 0;
  Resp->programmedAndHardware_MinPhyLinkRate = 0x8; /* not programmable and 1.5 G */
  Resp->programmedAndHardware_MaxPhyLinkRate = 0x8; /* not programmable and 1.5 G */
  Resp->phyChangeCount = 0; /* No broadcast(Change) received */
  Resp->virtualPhy_partialPathwayTimeout = 0x7; /* no virutal phy and see spec 10.4.3.5, p 404 rev 7 */
  Resp->routingAttribute = 0;
  osti_memset(&Resp->reserved13, 0, 5);
  osti_memset(&Resp->vendorSpecific, 0, 2);

#ifdef RPM_SOC
  /* not work yet because of high priority q */
  saSMPStart(
             agRoot,
             agIORequest,
             agDevHandle,
             agRequestType,
             agSASRequestBody,
             &ossaSMPCompleted
             );
#else
  saSMPStart(
             agRoot,
             agIORequest,
             0, /* queue number */
             agDevHandle,
             agRequestType,
             agSASRequestBody,
             &ossaSMPCompleted
             );
#endif
  return;
}

osGLOBAL void
ttdsaReportGeneralRespSend(
                           agsaRoot_t            *agRoot,
                           agsaDevHandle_t       *agDevHandle,
                           ttdsaXchg_t           *ttdsaXchg
                           )
{
  bit32                     agRequestType;
  agsaSASRequestBody_t      *agSASRequestBody;
  agsaSMPFrame_t            *agSMPFrame;
  smpRespReportGeneral_t    *Resp;
  smp_resp_t                *SMPResp;
  agsaIORequest_t           *agIORequest;
  bit8                       SMPPayload[SMP_DIRECT_PAYLOAD_LIMIT];    /*(TP)*/
  tdssSMPFrameHeader_t       tdSMPFrameHeader;              /*(TP)*/

  TI_DBG1(("ttdsaReportGeneralRespSend:\n"));

  agRequestType = AGSA_SMP_TGT_RESPONSE;

  SMPResp = (smp_resp_t *)ttdsaXchg->smpresp.virtAddr;

  agIORequest = &(ttdsaXchg->SMPRequestBody.agIORequest);

  agSASRequestBody = &(ttdsaXchg->SMPRequestBody.agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

  osti_memset(&tdSMPFrameHeader, 0, sizeof(tdssSMPFrameHeader_t));    /*(TP)*/

  tdSMPFrameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  tdSMPFrameHeader.smpFunction = SMP_REPORT_GENERAL; /* report general */
  tdSMPFrameHeader.smpFunctionResult = SMP_FUNCTION_ACCEPTED;
  tdSMPFrameHeader.smpReserved = 0;

  /*old*/
  //agSMPFrame->frameHeader.smpFrameType = SMP_RESPONSE; /* SMP response */
  //agSMPFrame->frameHeader.smpFunction = SMP_REPORT_GENERAL; /* report general */
  //agSMPFrame->frameHeader.smpFunctionResult = SMP_FUNCTION_ACCEPTED;

  osti_memcpy(SMPPayload, &tdSMPFrameHeader, 4);            /*(TP)*/

  agSMPFrame->outFrameBuf = SMPPayload;                 /*(TP)*/
  agSMPFrame->outFrameAddrUpper32 = ttdsaXchg->smpresp.phyAddrUpper;
  agSMPFrame->outFrameAddrLower32 = ttdsaXchg->smpresp.phyAddrLower;
  agSMPFrame->outFrameLen = sizeof(smpRespReportGeneral_t);

  //agSMPFrame->phyId = ttdsaXchg->SMPphyId;

  /* smp response payload */
  Resp = (smpRespReportGeneral_t *)&(SMPResp->RespData);
  osti_memset(Resp, 0, sizeof(smpRespReportGeneral_t));
  /* temp, hardcode smp general response */
  Resp->expanderChangeCount16[0] = 1;
  Resp->expanderRouteIndexes16[0] = 2;
  Resp->numOfPhys = 0x5; /* 0x1; */
  Resp->configuring_configurable = 0;
  tdhexdump("smp general response", (bit8 *)Resp, sizeof(smpRespReportGeneral_t));

#ifdef RPM_SOC
  /* not work yet because of high priority q */
  saSMPStart(
             agRoot,
             agIORequest,
             agDevHandle,
             agRequestType,
             agSASRequestBody,
              &ossaSMPCompleted
             );
 #else
  saSMPStart(
             agRoot,
             agIORequest,
             0, /* queue number */
             agDevHandle,
             agRequestType,
             agSASRequestBody,
             &ossaSMPCompleted
             );
#endif
  return;
}


osGLOBAL void
ttdsaSMPReqReceived(
                    agsaRoot_t            *agRoot,
                    agsaDevHandle_t       *agDevHandle,
                    agsaSMPFrameHeader_t  *agFrameHeader,
                    agsaFrameHandle_t     agFrameHandle,
                    bit32                 agFrameLength,
                    bit32                 phyId
                    )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  ttdsaXchg_t            *ttdsaXchg;
  tdsaDeviceData_t       *oneDeviceData = agNULL;


  TI_DBG1(("ttdsaSMPReqReceived: start\n"));

  oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

  if (oneDeviceData == agNULL)
  {
    TI_DBG1(("ttdsaSMPReqReceived: no device data\n"));
    return;
  }

  ttdsaXchg = ttdsaXchgGetStruct(agRoot);

  if (ttdsaXchg == agNULL)
  {
    TI_DBG1(("ttdsaSMPReqReceived: no free xchg structures\n"));
    return;
  }


  oneDeviceData->agDevHandle = agDevHandle;
  oneDeviceData->agRoot = agRoot;

  /* saving the device */
  ttdsaXchg->DeviceData = oneDeviceData;

  ttdsaXchg->agRoot  = agRoot;
  ttdsaXchg->tiRoot  = tiRoot;

  ttdsaXchg->SMPRequestBody.agIORequest.sdkData = agNULL;

  ttdsaXchg->SMPphyId = phyId;

  switch ( agFrameHeader->smpFunction )
  {
  case SMP_REPORT_GENERAL:
  {
    /* must spec p392, rev7*/
    TI_DBG1(("ttdsaSMPReqReceived: REPORT_GENERAL\n"));
    ttdsaReportGeneralRespSend(agRoot, agDevHandle, ttdsaXchg);
    break;
  }
  case SMP_REPORT_MANUFACTURE_INFORMATION:
  {
    /* optional, spec p394, rev7*/
    TI_DBG1(("ttdsaSMPReqReceived: REPORT_MANUFACTURE_INFORMATION\n"));
    ttdsaNotSupportRespSend(agRoot, agDevHandle, ttdsaXchg, SMP_REPORT_MANUFACTURE_INFORMATION);
    break;
  }
  case SMP_DISCOVER:
  {
    /* must, spec p398, rev7*/
    TI_DBG1(("ttdsaSMPReqReceived: DISCOVER\n"));
    ttdsaDiscoverRespSend(agRoot, agDevHandle, ttdsaXchg);
    break;
  }
  default:
  {
    TI_DBG1(("ttdsaSMPReqReceived: UKNOWN or not yet supported 0x%x\n", agFrameHeader->smpFunction));
    ttdsaNotSupportRespSend(agRoot, agDevHandle, ttdsaXchg, (bit8) agFrameHeader->smpFunction);
    break;
  }
  }

  return;
}
