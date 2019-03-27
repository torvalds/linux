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
 * $RCSfile: ttdtxchg.h,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 *
 * #define and data structures for SAS target in SAS/SATA TD layer
 *
 */

typedef struct sas_resp_s
{
  agsaSSPResponseInfoUnit_t    agResp;
  bit8                         RespData[4];
  bit8                         SenseData[64]; 
} sas_resp_t;

typedef struct smp_resp_s
{
  bit8                         RespData[1024]; /* SAS Spec */
} smp_resp_t;


typedef struct
{
  bit8   *virtAddr;
  bit32  phyAddrUpper;
  bit32  phyAddrLower;
  bit32  length;
} ttdsaDmaMemoryArea_t;

struct tdsaDeviceData_s;

/* I/O structurre */
typedef struct ttdsaXchg_s
{

  tdIORequestBody_t              IORequestBody; /* has to be at the top */
  tdssSMPRequestBody_t           SMPRequestBody; /* has to be at the second top */

  tdList_t                       XchgLinks;
  /* pointer to device(initiator) for which the I/O was initiated */
  struct tdsaDeviceData_s        *DeviceData;
  struct ttdsaXchg_s             *pTMResp;
  bit32                          oustandingIos;
  bit32                          isAborting;
  bit32                          oslayerAborting;
  bit32                          isTMRequest;
  bit32                          index;         /* index of structure */
  agsaSSPCmdInfoUnit_t           agSSPCmndIU;
  agsaSSPScsiTaskMgntReq_t       agTMIU;
  /* SSPTargetRead/SSPTargetWrite             */
  bit32                          XchType;
  bit32                          FrameType; /* cmnd or TM */
  agsaRoot_t                     *agRoot;
  tiRoot_t                       *tiRoot;
  /* indicates that at the completion of this data phase, this
     exchange structure will be freed */
  bit32                          statusSent;
  bit32                          responseSent;
  bit32                          readRspCollapsed : 1;
  bit32                          wrtRspCollapsed : 1;
  bit32                          readWrtCollapsedRes : 30;
  tiTargetScsiCmnd_t             tiTgtScsiCmnd;

  /* initiator tag a target received */
  bit16                          tag;
  bit64                          dataLen;
  bit32                          respLen;
  bit32                          smprespLen;
  ttdsaDmaMemoryArea_t           resp; /* sas response */
  ttdsaDmaMemoryArea_t           smpresp; /* sas smp response */
  bit32                          usedEsgl;
  /* for abort task io which is not founded in TD */
  bit32                          io_found;
  /* for debugging only */
  bit32                          id;
  /* PhyId for SMP*/
  bit32                          SMPphyId;
  bit32                          state;
  bit32                          TLR; /* Transport Layer Retransmit bits */
  bit32                          retries; /* retries */
  tiIORequest_t                  *tiIOToBeAbortedRequest; /* IO to be aborted */
  struct ttdsaXchg_s             *XchgToBeAborted; /* Xchg to be aborted */
} ttdsaXchg_t;

/*************************************************************************
** now ttdssIOData_t and old tgtXchgData_t -
**************************************************************************/

typedef struct ttdsaXchgData_s
{
  bit32           maxNumXchgs;
  tdList_t        xchgFreeList;
  tdList_t        xchgBusyList;
  bit32           noUsed;
  bit32           noFreed;
  bit32           noCmdRcvd;
  bit32           noStartIo;
  bit32           noSendRsp;
  bit32           noCompleted;
} ttdsaXchgData_t;


