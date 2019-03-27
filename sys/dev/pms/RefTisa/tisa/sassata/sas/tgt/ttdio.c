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
 * $RCSfile: ttdio.c,v $
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
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
#include <ttdglobl.h>
#include <ttdtxchg.h>
#include <ttdtypes.h>
#endif

#include <tdsatypes.h>
#include <tdproto.h>


/*  Start For trace only */
#ifdef REMOVED
unsigned __int64
GetHiResTimeStamp(void);
#endif
#undef TD_DEBUG_TRACE_ENABLE
#define TD_DEBUG_IO_TRACE_BUFFER_MAX  1024


typedef struct TDDebugTraceEntry_s
{
    bit64             Time;
    ttdsaXchg_t       ttdsaXchg;
    tdsaDeviceData_t  oneDeviceData;
} TDDebugTraceEntry_t;

typedef struct TDDebugTrace_s
{
    bit32                 Idx;
    bit32                 pad;
    TDDebugTraceEntry_t  Data[TD_DEBUG_IO_TRACE_BUFFER_MAX];
} TDDebugTrace_t;

void TDTraceInit(void);
void TDTraceAdd(ttdsaXchg_t *ttdsaXchg, tdsaDeviceData_t  *oneDeviceData);

#ifdef TD_DEBUG_TRACE_ENABLE
#define TD_DEBUG_TRACE(ttdsaXchg, oneDeviceData) TDTraceAdd(ttdsaXchg, oneDeviceData)
#else
#define TD_DEBUG_TRACE(ttdsaXchg, oneDeviceData)
#endif

TDDebugTrace_t TraceData;

void TDTraceInit(void)
{
    osti_memset(&TraceData, 0, sizeof(TraceData));
}

void TDTraceAdd(ttdsaXchg_t *ttdsaXchg, tdsaDeviceData_t  *oneDeviceData)
{
    static bit32 TraceIdx = 0;

    TraceData.Idx = TraceIdx;
#ifdef REMOVED
    TraceData.Data[TraceIdx].Time = GetHiResTimeStamp();
#endif
    osti_memcpy((bit8 *)&(TraceData.Data[TraceIdx].ttdsaXchg), (bit8 *)ttdsaXchg, sizeof(ttdsaXchg_t));
    osti_memcpy((bit8 *)&(TraceData.Data[TraceIdx].oneDeviceData), (bit8 *)oneDeviceData, sizeof(tdsaDeviceData_t));
#ifdef REMOVED
    TraceData.Data[TraceIdx].ttdsaXchg = ttdsaXchg;
    TraceData.Data[TraceIdx].oneDeviceData = oneDeviceData;
#endif

    TraceIdx++;
    if (TraceIdx >= TD_DEBUG_IO_TRACE_BUFFER_MAX)
    {
        TraceIdx = 0;
    }

    return;
}


/*  End For trace only */


osGLOBAL void
ttdsaSSPReqReceived(
        agsaRoot_t           *agRoot,
        agsaDevHandle_t      *agDevHandle,
        agsaFrameHandle_t    agFrameHandle,
        bit32                agInitiatorTag,
        bit32                parameter,
        bit32                      agFrameLen
)
{
    tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
    ttdsaXchg_t            *ttdsaXchg;
    /*  agsaSSPCmdInfoUnit_t   cmdIU; */
    tdsaDeviceData_t       *oneDeviceData = agNULL;
    bit32                  agFrameType, TLR;

    TD_XCHG_CONTEXT_NO_CMD_RCVD(tiRoot)        = TD_XCHG_CONTEXT_NO_CMD_RCVD(tiRoot)+1;

    TI_DBG4(("ttdsaSSPReqReceived: start\n"));

    agFrameType = TD_GET_FRAME_TYPE(parameter);
    TLR = TD_GET_TLR(parameter);


    /*note:
    in ini, agDevHandle->osData =  tdsaDeviceData_t
    is set in tdssAddDevicedataToSharedcontext()

    in tdsaDeviceDataInit()
    oneDeviceData->tiDeviceHandle.tdData has been initialized
     */
    oneDeviceData = (tdsaDeviceData_t *)agDevHandle->osData;

    if (oneDeviceData == agNULL)
    {
        TI_DBG1(("ttdsaSSPReqReceived: no device data\n"));
        return;
    }



    ttdsaXchg = ttdsaXchgGetStruct(agRoot);

    if (ttdsaXchg == agNULL)
    {
        TI_DBG1(("ttdsaSSPReqReceived: no free xchg structures\n"));
        //    ttdsaDumpallXchg(tiRoot);
        return;
    }

    if (ttdsaXchg->IORequestBody.tiIORequest == agNULL)
    {
        TI_DBG1(("ttdsaSSPReqReceived: tiIORequest is NULL\n"));
        //    ttdsaDumpallXchg(tiRoot);
        return;
    }

    oneDeviceData->agDevHandle = agDevHandle;
    oneDeviceData->agRoot = agRoot;

    /* saving the device */
    ttdsaXchg->DeviceData = oneDeviceData;

    ttdsaXchg->agRoot  = agRoot;
    ttdsaXchg->tiRoot  = tiRoot;

    ttdsaXchg->IORequestBody.agIORequest.sdkData = agNULL;

    /* initiator tag */
    ttdsaXchg->tag      = (bit16)agInitiatorTag;
    ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.agTag
    = ttdsaXchg->tag;
    ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.agTag
    = ttdsaXchg->tag;

    TI_DBG6(("ttdsaSSPReqReceived: initiator tag 0x%x\n", agInitiatorTag));

    if (agFrameType == OSSA_FRAME_TYPE_SSP_CMD)
    {
        TI_DBG4(("ttdsaSSPReqReceived: CMD frame type\n"));
        /* reads agsaSSPResponseInfoUnit_t */
        saFrameReadBlock(
                agRoot,
                agFrameHandle,
                0,
                &ttdsaXchg->agSSPCmndIU,
                agFrameLen
        );

        tdsaProcessCDB(&ttdsaXchg->agSSPCmndIU, ttdsaXchg);
        ttdsaXchg->FrameType = SAS_CMND;

        /*
         ** As the last thing we call the disk module to handle the SCSI CDB.
         ** The disk module will call tiTGTIOStart to start a data phase.
         */

        /* typedef struct
       {
       bit8      *reqCDB;
       bit8      *scsiLun,
       bit32     taskAttribute;
       bi32      taskId;
       bit32     crn;
       } tiTargetScsiCmnd_t;
         */
        /* what about reqCDB and scsiLun */

        /* coverting task attributes from SAS TISA */
        switch (SA_SSPCMD_GET_TASKATTRIB(&ttdsaXchg->agSSPCmndIU))
        {
        case 0:
            ttdsaXchg->tiTgtScsiCmnd.taskAttribute = TASK_SIMPLE;
            break;
        case 1:
            ttdsaXchg->tiTgtScsiCmnd.taskAttribute = TASK_HEAD_OF_QUEUE;
            break;
        case 2:
            ttdsaXchg->tiTgtScsiCmnd.taskAttribute = TASK_ORDERED;
            break;
        case 3:
            TI_DBG1(("ttdsaSSPReqReceived: reserved taskAttribute 0x%x\n",ttdsaXchg->agSSPCmndIU.efb_tp_taskAttribute));
            ttdsaXchg->tiTgtScsiCmnd.taskAttribute = TASK_SIMPLE;
            break;
        case 4:
            ttdsaXchg->tiTgtScsiCmnd.taskAttribute = TASK_ACA;
            break;
        default:
            TI_DBG1(("ttdsaSSPReqReceived: unknown taskAttribute 0x%x\n",ttdsaXchg->agSSPCmndIU.efb_tp_taskAttribute));
            ttdsaXchg->agSSPCmndIU.efb_tp_taskAttribute = TASK_SIMPLE;
            break;
        }

        ttdsaXchg->tiTgtScsiCmnd.taskId = agInitiatorTag;
        ttdsaXchg->tiTgtScsiCmnd.crn = 0;
        ttdsaXchg->TLR = TLR;

        /* call ostiProcessScsiReq */
        ostiProcessScsiReq( tiRoot,
                &ttdsaXchg->tiTgtScsiCmnd,
                agFrameHandle,
                0,
                ttdsaXchg->IORequestBody.tiIORequest,
                &ttdsaXchg->DeviceData->tiDeviceHandle);


    }
    else if (agFrameType == OSSA_FRAME_TYPE_SSP_TASK)
    {
        TI_DBG4(("ttdsaSSPReqReceived: TM frame type\n"));

        /*
      reads aagsaSSPScsiTaskMgntReq_t
      including lun
         */
        saFrameReadBlock(
                agRoot,
                agFrameHandle,
                0,
                &ttdsaXchg->agTMIU,
                agFrameLen
        );

        ttdsaXchg->FrameType = SAS_TM;
        /*
      call task process mangement fn
         */
        ttdsaTMProcess(tiRoot, ttdsaXchg);
        return;
    }
    else
    {
        TI_DBG1(("ttdsaSSPReqReceived: unknown frame type\n"));
        return;
    }

    return;
}

void
dumpCDB(bit8 *cdb)
{
    bit32 i;
    for(i=0;i<10;i++)
    {
        TI_DBG4(("cdb[%d] 0x%x\n", i, cdb[i]));
    }
    return;
}

osGLOBAL void
tdsaProcessCDB(
        agsaSSPCmdInfoUnit_t      *cmdIU,
        ttdsaXchg_t               *ttdsaXchg
)
{
    tdsaRoot_t    *tdsaRoot      = (tdsaRoot_t *) ttdsaXchg->tiRoot->tdData;
    tdsaContext_t *tdsaAllShared = (tdsaContext_t *) &tdsaRoot->tdsaAllShared;
    ttdsaTgt_t    *Target        = (ttdsaTgt_t *) tdsaAllShared->ttdsaTgt;
    bit8 group;
#ifdef TD_DEBUG_ENABLE
    CDB6_t *cdb6;
#endif
    CDB10_t *cdb10;
    CDB12_t *cdb12;
    CDB16_t *cdb16;
    bit32   unknown = agFALSE;
    bit32   len=0;
    group = cmdIU->cdb[0] & CDB_GRP_MASK;

    TI_DBG4(("tdsaProcessCDB: start\n"));

    switch (cmdIU->cdb[0])
    {
    case SCSIOPC_REPORT_LUN:
        TI_DBG4(("tdsaProcessCDB: REPORT_LUN\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;
    case SCSIOPC_INQUIRY:
        TI_DBG4(("tdsaProcessCDB: INQUIRY\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;

    case SCSIOPC_TEST_UNIT_READY:
        TI_DBG4(("tdsaProcessCDB: TEST_UNIT_READY\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;

    case SCSIOPC_READ_CAPACITY_10:
    case SCSIOPC_READ_CAPACITY_16:
        TI_DBG4(("tdsaProcessCDB: READ CAPACITY\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;

    case SCSIOPC_READ_6: /* fall through */
    case SCSIOPC_READ_10:
        TI_DBG4(("tdsaProcessCDB: READ\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;

    case SCSIOPC_WRITE_6: /* fall through */
    case SCSIOPC_WRITE_10:
        TI_DBG4(("tdsaProcessCDB: WRITE\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_WRITE_DATA;
        break;

    case SCSIOPC_MODE_SENSE_6: /* fall through */
    case SCSIOPC_MODE_SENSE_10:
        TI_DBG4(("tdsaProcessCDB: MODE SENSE\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;
    case SCSIOPC_SYNCHRONIZE_CACHE_10:
        TI_DBG4(("tdsaProcessCDB: SCSIOPC_SYNCHRONIZE_CACHE_10\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_CMD_OR_TASK_RSP;
        break;
    case SCSIOPC_REQUEST_SENSE:
        TI_DBG2(("tdsaProcessCDB: SCSIOPC_REQUEST_SENSE\n"));
        ttdsaXchg->XchType = AGSA_SSP_TGT_READ_DATA;
        break;
    default:
        TI_DBG4(("tdsaProcessCDB: UNKNOWN, cbd %d 0x%x\n", cmdIU->cdb[0], cmdIU->cdb[0]));
        ttdsaXchg->XchType = TargetUnknown;
        break;
    }

    /* parse datalen */
    switch (group)
    {
    case CDB_6BYTE:
        TI_DBG4(("tdsaProcessCDB: CDB 6 byte, not yet\n"));
#ifdef TD_DEBUG_ENABLE
        cdb6 = (CDB6_t *)(cmdIU->cdb);
#endif
        TI_DBG4(("tdsaProcessCDB: CDB len 0x%x\n", cdb6->len));
        break;
    case CDB_10BYTE1: /* fall through */
    case CDB_10BYTE2:
        TI_DBG4(("tdsaProcessCDB: CDB 10 byte\n"));
        cdb10 = (CDB10_t *)(cmdIU->cdb);
        OSSA_READ_BE_16(AGROOT, &len, cdb10->len, 0);
        TI_DBG4(("tdsaProcessCDB: CDB len 0x%x\n", len));
        dumpCDB(cmdIU->cdb);
        break;
    case CDB_12BYTE:
        TI_DBG4(("tdsaProcessCDB: CDB 12 byte, not yet\n"));
        cdb12 = (CDB12_t *)(cmdIU->cdb);
        OSSA_READ_BE_32(AGROOT, &len, cdb12->len, 0);
        TI_DBG4(("tdsaProcessCDB: CDB len 0x%x\n", len));
        break;
    case CDB_16BYTE:
        TI_DBG4(("tdsaProcessCDB: CDB 16 byte, not yet\n"));
        cdb16 = (CDB16_t *)(cmdIU->cdb);
        OSSA_READ_BE_32(AGROOT, &len, cdb16->len, 0);
        TI_DBG4(("tdsaProcessCDB: CDB len 0x%x\n", len));
        break;
    default:
        TI_DBG4(("tdsaProcessCDB: unknow CDB, group %d 0x%x\n", group, group));
        len = 0;
        unknown = agTRUE;
        break;
    }
    if (cmdIU->cdb[0] == SCSIOPC_READ_6  || cmdIU->cdb[0] == SCSIOPC_READ_10 ||
        cmdIU->cdb[0] == SCSIOPC_WRITE_6 || cmdIU->cdb[0] == SCSIOPC_WRITE_10  )
    {
      ttdsaXchg->dataLen  = len * Target->OperatingOption.BlockSize;
    }
    else
    {
      ttdsaXchg->dataLen  = len;
    }

    if (ttdsaXchg->dataLen == 0 && unknown == agFALSE)
    {
        /* this is needed because of min operation in tiTGTIOstart() */
        ttdsaXchg->dataLen      = 0xffffffff;
    }
    /*  TI_DBG4(("tdsaProcessCDB: datalen 0x%x %d\n", ttdsaXchg->dataLen, ttdsaXchg->dataLen)); */
    return;
}




/*****************************************************************************
 *
 *  tiTGTIOStart
 *
 *  Purpose: This function is called by the target OS Specific Module to start
 *           the next phase of a SCSI Request.
 *
 *  Parameters:
 *   tiRoot:         Pointer to driver Instance.
 *   tiIORequest:    Pointer to the I/O request context for this I/O.
 *                   This context was initially passed to the OS Specific Module
 *                   in ostiProcessScsiReq().
 *   dataOffset:     Offset into the buffer space for this phase.
 *   dataLength:     Length of data to move for this phase.
 *   dataSGL:        Length/Address pair of where the data is. The SGL list is
 *                   allocated and initialized by the OS Specific module.
 *   sglVirtualAddr: The virtual address of the first element in agSgl1 when
 *                   agSgl1 is used with the type tiSglList.
 *                   This field is needed for the TD Layer.
 *
 *  Return:
 *   tiSuccess:     I/O request successfully initiated.
 *   tiBusy:        No resources available, try again later.
 *   tiError:       Other errors that prevent the I/O request to be started.
 *
 *  Note:
 *
 *****************************************************************************/
osGLOBAL bit32
tiTGTIOStart( tiRoot_t         *tiRoot,
        tiIORequest_t    *tiIORequest,
        bit32             dataOffset,
        bit32             dataLength,
        tiSgl_t          *dataSGL,
        void             *sglVirtualAddr
)

{
    ttdsaXchg_t               *ttdsaXchg;
    agsaSSPTargetRequest_t    *agSSPTargetReq;
    bit32                     tiStatus;
    bit32                     saStatus;
    bit32                     tdStatus;
    tdsaPortContext_t         *onePortContext = agNULL;
    tdsaDeviceData_t          *oneDeviceData = agNULL;

    TI_DBG4(("tiTGTIOStart: start\n"));
    TI_DBG4(("tiTGTIOStart: dataLength 0x%x %d\n", dataLength, dataLength));
    TI_DBG4(("tiTGTIOStart: dataOffset 0x%x %d\n", dataOffset, dataOffset));

    /* save infor in ttdsaXchg */
    ttdsaXchg     = (ttdsaXchg_t *)tiIORequest->tdData;

    /* check the state of port */
    oneDeviceData = ttdsaXchg->DeviceData;
    onePortContext= oneDeviceData->tdPortContext;
    if (onePortContext->valid == agFALSE)
    {
        TI_DBG1(("tiTGTIOStart: portcontext pid %d is invalid\n", onePortContext->id));
        return tiError;
    }


    agSSPTargetReq
    = &(ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq);

    /* fills in agsaSASRequestBody_t.agsaSSPTargetRequest_t */
    agSSPTargetReq->dataLength = (bit32) MIN(dataLength, ttdsaXchg->dataLen);
    agSSPTargetReq->offset = dataOffset;
    agSSPTargetReq->agTag = ttdsaXchg->tag;
    /* SSPTargetReq->agTag has been set in ttdsaSSPReqReceived() */

    /* Process TLR */
    if (ttdsaXchg->TLR == 2)
    {
        /* diable TLR */
        agSSPTargetReq->sspOption = 0;
    }
    else
    {
        /* enable TLR */
        /* bit5: 0 1 11 11 :bit0 */
        agSSPTargetReq->sspOption = 0x1F;
    }

    ttdsaXchg->IORequestBody.IOType.TargetIO.TargetIOType.RegIO.sglVirtualAddr
    = sglVirtualAddr;

    if (agSSPTargetReq->dataLength != 0)
    {
        TI_DBG6(("tiTGTIOStart: pos 1\n"));
        ttdsaXchg->IORequestBody.IOType.TargetIO.TargetIOType.RegIO.tiSgl1
        = *dataSGL;
    }
    else
    {
        TI_DBG6(("tiTGTIOStart: pos 2\n"));
        ttdsaXchg->IORequestBody.IOType.TargetIO.TargetIOType.RegIO.tiSgl1.len
        = 0;
        ttdsaXchg->IORequestBody.IOType.TargetIO.TargetIOType.RegIO.tiSgl1.type
        = tiSgl;

        /* let's send response frame */
        if (ttdsaXchg->resp.length != 0)
        {
            /* senselen != 0, send respsonse */
            TI_DBG4(("tiTGTIOStart: send respsonse\n"));
            TI_DBG4(("tiTGTIOStart: resp.length 0x%x\n",
                    ttdsaXchg->resp.length));
            ttdsaXchg->responseSent = agTRUE;
            ttdsaXchg->DeviceData->IOResponse++;
            TD_DEBUG_TRACE(ttdsaXchg, ttdsaXchg->DeviceData);
            tdStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
            if (tdStatus == AGSA_RC_SUCCESS)
            {
                return tiSuccess;
            }
            else if (tdStatus == AGSA_RC_FAILURE)
            {
                TI_DBG1(("tiTGTIOStart: (ttdsaSendResp) sending not successful\n"));
                return tiError;
            }
            else
            {
                TI_DBG1(("tiTGTIOStart: (ttdsaSendResp) sending busy\n"));
                return tiBusy;
            }
        }
    }


    /* sets SSPTargetReq->agSgl */
    tiStatus = ttdssIOPrepareSGL(tiRoot, &ttdsaXchg->IORequestBody, dataSGL, NULL, sglVirtualAddr);

    if (tiStatus != tiSuccess)
    {
        TI_DBG1(("tiTGTIOStart: ttdIOPrepareSGL did not return success\n"));
        return tiStatus;
    }

    TI_DBG4(("tiTGTIOStart: agroot %p ttdsaXchg %p\n", ttdsaXchg->agRoot, ttdsaXchg));
    TI_DBG4(("tiTGTIOStart: agDevHanlde %p\n", ttdsaXchg->DeviceData->agDevHandle));

    if ( (ttdsaXchg->readRspCollapsed == agTRUE) || (ttdsaXchg->wrtRspCollapsed == agTRUE) )
    {
        /* collapse good response with read  */
        TI_DBG4(("tiTGTIOStart: read rsp collapse\n"));
        TI_DBG4(("tiTGTIOStart: initiator tag 0x%x\n", ttdsaXchg->tag));

        TD_XCHG_CONTEXT_NO_START_IO(tiRoot)        = TD_XCHG_CONTEXT_NO_START_IO(tiRoot)+1;
        ttdsaXchg->DeviceData->IOStart++;
        TD_DEBUG_TRACE(ttdsaXchg, ttdsaXchg->DeviceData);
        saStatus = saSSPStart(
                ttdsaXchg->agRoot, 
                &ttdsaXchg->IORequestBody.agIORequest, 
                tdsaRotateQnumber(tiRoot, oneDeviceData), 
                ttdsaXchg->DeviceData->agDevHandle, 
                ttdsaXchg->readRspCollapsed ? AGSA_SSP_TGT_READ_GOOD_RESP : AGSA_SSP_TGT_WRITE_GOOD_RESP,
                        &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                        agNULL,
                        &ossaSSPCompleted
        );
    }
    else
    {
        TI_DBG4(("tiTGTIOStart: normal\n"));
        TI_DBG4(("tiTGTIOStart: initiator tag 0x%x\n", ttdsaXchg->tag));
        TD_XCHG_CONTEXT_NO_START_IO(tiRoot)        = TD_XCHG_CONTEXT_NO_START_IO(tiRoot)+1;
        ttdsaXchg->DeviceData->IOStart++;
        TD_DEBUG_TRACE(ttdsaXchg, ttdsaXchg->DeviceData);
        saStatus = saSSPStart(
                ttdsaXchg->agRoot, /* agRoot, */
                &ttdsaXchg->IORequestBody.agIORequest, 
                tdsaRotateQnumber(tiRoot, oneDeviceData), 
                ttdsaXchg->DeviceData->agDevHandle, 
                ttdsaXchg->XchType,
                &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                agNULL,
                &ossaSSPCompleted
        );

    }

    if (saStatus == AGSA_RC_SUCCESS)
    {
        return tiSuccess;
    }
    else if (saStatus == AGSA_RC_FAILURE)
    {
        TI_DBG1(("tiTGTIOStart: sending not successful\n"));
        return tiError;
    }
    else
    {
        TI_DBG1(("tiTGTIOStart: sending busy\n"));
        return tiBusy;
    }

}

#ifdef EDC_ENABLE
/*****************************************************************************
 *
 *  tiTGTIOStart
 *
 *  Purpose: This function is called by the target OS Specific Module to start
 *           the next phase of a SCSI Request.
 *
 *  Parameters:
 *   tiRoot:         Pointer to driver Instance.
 *   tiIORequest:    Pointer to the I/O request context for this I/O.
 *                   This context was initially passed to the OS Specific Module
 *                   in ostiProcessScsiReq().
 *   dataOffset:     Offset into the buffer space for this phase.
 *   dataLength:     Length of data to move for this phase.
 *   dataSGL:        Length/Address pair of where the data is. The SGL list is
 *                   allocated and initialized by the OS Specific module.
 *   sglVirtualAddr: The virtual address of the first element in agSgl1 when
 *                   agSgl1 is used with the type tiSglList.
 *                   This field is needed for the TD Layer.
 *   difOption:      DIF option.
 *
 *  Return:
 *   tiSuccess:     I/O request successfully initiated.
 *   tiBusy:        No resources available, try again later.
 *   tiError:       Other errors that prevent the I/O request to be started.
 *
 *  Note:
 *
 *****************************************************************************/
osGLOBAL bit32 tiTGTIOStartDif(
        tiRoot_t        *tiRoot,
        tiIORequest_t   *tiIORequest,
        bit32           dataOffset,
        bit32           dataLength,
        tiSgl_t         *dataSGL,
        void            *sglVirtualAddr,
        tiDif_t         *difOption
)
{

    /* This function was never used by SAS/SATA. Use tiTGTSuperIOStart() instead. */
    return tiBusy;
}
#endif

osGLOBAL bit32
ttdssIOPrepareSGL(
        tiRoot_t                 *tiRoot,
        tdIORequestBody_t        *tdIORequestBody,
        tiSgl_t                  *tiSgl1,
        tiSgl_t                  *tiSgl2,
        void                     *sglVirtualAddr
)
{
    agsaSgl_t                 *agSgl;

    TI_DBG6(("ttdssIOPrepareSGL: start\n"));

    agSgl = &(tdIORequestBody->transport.SAS.agSASRequestBody.sspTargetReq.agSgl);

    agSgl->len = 0;

    if (tiSgl1 == agNULL)
    {
        TI_DBG1(("ttdssIOPrepareSGL: Error tiSgl1 is NULL\n"));
        return tiError;
    }

    agSgl->sgUpper = tiSgl1->upper;
    agSgl->sgLower = tiSgl1->lower;
    agSgl->len = tiSgl1->len;
    agSgl->extReserved = tiSgl1->type;

    return tiSuccess;
}

/* temp for debugging */
void
dumpresp(bit8 *resp, bit32 len)
{
    bit32 i;

    for(i=0;i<len;i++)
    {
        TI_DBG4(("resp[%d] 0x%x\n", i, resp[i]));
    }

    return;
}

osGLOBAL bit32
ttdsaSendResp(
        agsaRoot_t            *agRoot,
        ttdsaXchg_t           *ttdsaXchg
)
{
    tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
    tdsaDeviceData_t          *oneDeviceData = agNULL;
    bit32                     agRequestType;
    bit32                     saStatus;
    agsaSSPTargetResponse_t   *agSSPTargetResp;
    agRequestType = AGSA_SSP_TGT_CMD_OR_TASK_RSP;

    TI_DBG4(("ttdsaSendResp: start\n"));
    TI_DBG4(("ttdsaSendResp: agroot %p ttdsaXchg %p\n", ttdsaXchg->agRoot, ttdsaXchg));

    TI_DBG4(("ttdsaSendResp:: agDevHanlde %p\n", ttdsaXchg->DeviceData->agDevHandle));

    /* sas response */
    TI_DBG4(("ttdsaSendResp: len 0x%x \n",
            ttdsaXchg->resp.length));
    TI_DBG4(("ttdsaSendResp: upper 0x%x \n",
            ttdsaXchg->resp.phyAddrUpper));
    TI_DBG4(("ttdsaSendResp: lower 0x%x \n",
            ttdsaXchg->resp.phyAddrLower));
    TI_DBG4(("ttdsaSendResp: initiator tag 0x%x\n", ttdsaXchg->tag));

    agSSPTargetResp = &(ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse);
    agSSPTargetResp->agTag = ttdsaXchg->tag;
    agSSPTargetResp->respBufLength = ttdsaXchg->resp.length;
    agSSPTargetResp->respBufUpper = ttdsaXchg->resp.phyAddrUpper;
    agSSPTargetResp->respBufLower = ttdsaXchg->resp.phyAddrLower;
    agSSPTargetResp->respOption = 3; /* Retry on both ACK/NAK timeout and NAK received */
    /* temporary solution for T2D Combo*/
#if defined (INITIATOR_DRIVER) && defined (TARGET_DRIVER)
    /* nothing */
#else
    if (agSSPTargetResp->respBufLength <= AGSA_MAX_SSPPAYLOAD_VIA_SFO)
        agSSPTargetResp->frameBuf = ttdsaXchg->resp.virtAddr;
    else
        agSSPTargetResp->frameBuf = NULL;
#endif
    dumpresp((bit8 *)ttdsaXchg->resp.virtAddr, ttdsaXchg->resp.length);

    TD_XCHG_CONTEXT_NO_SEND_RSP(TD_GET_TIROOT(agRoot))        =
            TD_XCHG_CONTEXT_NO_SEND_RSP(TD_GET_TIROOT(agRoot))+1;

    oneDeviceData = ttdsaXchg->DeviceData;
    saStatus = saSSPStart(
            ttdsaXchg->agRoot, /* agRoot,*/
            &ttdsaXchg->IORequestBody.agIORequest, 
            tdsaRotateQnumber(tiRoot, oneDeviceData), 
            ttdsaXchg->DeviceData->agDevHandle, /* agDevHandle, */
            agRequestType,
            &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
            agNULL,
            &ossaSSPCompleted
    );

    if (saStatus == AGSA_RC_SUCCESS)
    {
        TI_DBG4(("ttdsaSendResp: sending successful\n"));
        return AGSA_RC_SUCCESS;
    }
    else if (saStatus == AGSA_RC_FAILURE)
    {
        TI_DBG1(("ttdsaSendResp: sending not successful\n"));
        return AGSA_RC_FAILURE;
    }
    else
    {
        TI_DBG1(("ttdsaSendResp: sending busy\n"));
        return AGSA_RC_BUSY;
    }

}

osGLOBAL void
ttdsaIOCompleted(
        agsaRoot_t             *agRoot,
        agsaIORequest_t        *agIORequest,
        bit32                  agIOStatus,
        bit32                  agIOInfoLen,
        agsaFrameHandle_t      agFrameHandle,
        bit32                  agOtherInfo
)
{

    ttdsaXchg_t       *ttdsaXchg    = (ttdsaXchg_t *)agIORequest->osData;
    /* done in ttdsaXchgInit() */
    bit32             IOFailed = agFALSE;
    bit32             status;
    bit32             statusDetail = 0;
    tiRoot_t          *tiRoot;
#ifdef REMOVED
    tdsaRoot_t        *tdsaRoot;
    tdsaContext_t     *tdsaAllShared;
#endif
    bit32             tdStatus;
    bit32             saStatus = AGSA_RC_FAILURE;
#ifdef  TD_DEBUG_ENABLE
    agsaDifDetails_t  *DifDetail;
#endif

    TI_DBG4(("ttdsaIOCompleted: start\n"));
    tiRoot = ((tdsaRootOsData_t *)agRoot->osData)->tiRoot;
#ifdef REMOVED
    tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
#endif
#ifdef  TD_DEBUG_ENABLE
    DifDetail = (agsaDifDetails_t *)agFrameHandle;
#endif

    if (tiRoot == agNULL)
    {
        TI_DBG1(("ttdsaIOCompleted: tiRoot is NULL\n"));
        return;
    }

    TD_XCHG_CONTEXT_NO_IO_COMPLETED(tiRoot)    = TD_XCHG_CONTEXT_NO_IO_COMPLETED(tiRoot)+1;

    if(TD_XCHG_GET_STATE(ttdsaXchg) != TD_XCHG_STATE_ACTIVE)
    {
        TI_DBG1(("ttdsaIOCompleted: XCHG is not active *****************\n"));
        return;
    }

    if (ttdsaXchg->isTMRequest != agTRUE)
    {
        TI_DBG6(("ttdsaIOCompleted: COMMAND \n"));
        TI_DBG6(("ttdsaIOCompleted: ttdsaXchg %p\n", ttdsaXchg));
        TI_DBG6(("ttdsaIOCompleted: ttdsaXchg->IORequestBody.EsglPageList %p\n", &ttdsaXchg->IORequestBody.EsglPageList));
        TI_DBG6(("ttdsaIOCompleted: command initiator tag 0x%x\n", ttdsaXchg->tag));

#ifdef REMOVED
        /* call tdsafreeesglpages only for xchg that used eslg */
        if (ttdsaXchg->usedEsgl == agTRUE)
        {
            tdsaFreeEsglPages(tiRoot, &ttdsaXchg->IORequestBody.EsglPageList);
            ttdsaXchg->usedEsgl = agFALSE;
        }
#endif

        /* successful case */
        if (agIOStatus ==  OSSA_IO_SUCCESS)
        {
            TI_DBG6(("ttdsaIOCompleted: osIOSuccess\n"));
            if ( (ttdsaXchg->readRspCollapsed == agTRUE) || (ttdsaXchg->wrtRspCollapsed == agTRUE) )
            {
                ttdsaXchg->responseSent = agTRUE;
                TI_DBG4(("ttdsaIOCompleted: read rsp collapse\n"));
            }

            if (ttdsaXchg->statusSent == agTRUE)
            {
                /*
          the response has already been set and ready
          but has NOT been sent
                 */
                if (ttdsaXchg->responseSent == agFALSE)
                {
                    /* let's send the response for IO */
                    TI_DBG6(("ttdsaIOCompleted: sending response\n"));
                    TD_DEBUG_TRACE(ttdsaXchg, ttdsaXchg->DeviceData);
                    tdStatus = ttdsaSendResp(agRoot, ttdsaXchg);
                    if (tdStatus != AGSA_RC_SUCCESS)
                    {
                        TI_DBG1(("ttdsaIOCompleted: attention needed\n"));
                        return;
                    }
                    ttdsaXchg->responseSent = agTRUE;
                }
                else
                {
                    TI_DBG4(("ttdsaIOCompleted: read rsp collapse and complete \n"));
                    /* the response has been sent */
                    TI_DBG6(("ttdsaIOCompleted: already sent response, notify OS\n"));

                    if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_INACTIVE)
                    {
                        TI_DBG1(("ttdsaIOCompleted: wrong DEQUEUE_THIS\n"));
                    }

                    /*
                     * Notify the OS Specific Module, so it can free its resource.
                     */
                    TI_DBG4(("ttdsaIOCompleted: calling ostiTargetIOCompleted\n"));
                    ostiTargetIOCompleted( tiRoot,
                            ttdsaXchg->IORequestBody.tiIORequest, 
                            tiIOSuccess );

                    /* clean up resources */
                    ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                }
            } /* sent */
            else
            {
                TI_DBG4(("ttdsaIOCompleted: osIOSuccess: nextphase\n"));
                /* the response has not been set; still in data phase */
                /* we need to tell the disk module to start the next phase */
                ostiNextDataPhase(ttdsaXchg->tiRoot,
                        ttdsaXchg->IORequestBody.tiIORequest );
            }
            return;
        } /* success */

        /* handle error cases */
        if (agIOStatus == OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH || agIOStatus == OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH
                || agIOStatus == OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH)
        {
            TI_DBG1(("ttdsaIOCompleted: DIF detail UpperLBA 0x%08x LowerLBA 0x%08x\n", DifDetail->UpperLBA, DifDetail->LowerLBA));
        }
        switch (agIOStatus)
        {
        case OSSA_IO_ABORTED:
            TI_DBG1(("ttdsaIOCompleted: ABORTED\n"));
            status        = tiIOFailed;
            statusDetail  = tiDetailAborted;
            IOFailed      = agTRUE;
            break;
#ifdef REMOVED
        case OSSA_IO_OVERFLOW:
            TI_DBG1(("ttdsaIOCompleted: OVERFLOW\n"));
            status        = tiIOOverRun;
            IOFailed      = agTRUE;
            break;
#endif
        case OSSA_IO_UNDERFLOW:
            TI_DBG1(("ttdsaIOCompleted: UNDERFLOW\n"));
            status        = tiIOUnderRun;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_ABORT_RESET:
            TI_DBG1(("ttdsaIOCompleted: ABORT_RESET\n"));
            status        = tiIOFailed;
            statusDetail  = tiDetailAbortReset;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
            TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS\n"));
            status        = tiIOEncryptError;
            statusDetail  = tiDetailDekKeyCacheMiss;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
            TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH\n"));
            status        = tiIOEncryptError;
            statusDetail  = tiDetailDekKeyCacheMiss;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
            TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH\n"));
            status        = tiIODifError;
            statusDetail  = tiDetailDifAppTagMismatch;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
            TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH\n"));
            status        = tiIODifError;
            statusDetail  = tiDetailDifRefTagMismatch;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
            TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH\n"));
            status        = tiIODifError;
            statusDetail  = tiDetailDifCrcMismatch;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_FAILED: /* fall through */
        case OSSA_IO_NO_DEVICE: /* fall through */
            //case OSSA_IO_NO_SUPPORT: /* fall through */       /*added to compile tgt_drv (TP)*/
        case OSSA_IO_LINK_FAILURE: /* fall through */
        case OSSA_IO_PROG_ERROR: /* fall through */
        case OSSA_IO_DS_NON_OPERATIONAL: /* fall through */
        case OSSA_IO_DS_IN_RECOVERY: /* fall through */
        case OSSA_IO_TM_TAG_NOT_FOUND: /* fall through */
        case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE: /* fall through */
        default:
            status        = tiIOFailed;
            statusDetail  = tiDetailOtherError;
            IOFailed      = agTRUE;
            TI_DBG1(("ttdsaIOCompleted: Fail!!!!!!! agIOStatus=0x%x  agIOInfoLen=0x%x agOtherInfo=0x%x\n", agIOStatus, agIOInfoLen, agOtherInfo));
            //      ttdsaDumpallXchg(tiRoot);
            if (agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT)
            {
                TI_DBG1(("ttdsaIOCompleted: OSSA_IO_XFER_OPEN_RETRY_TIMEOUT ttdsaXchg->id 0x%x datalen 0x%x offset 0x%x agTag 0x%x\n",
                        ttdsaXchg->id,
                        ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.dataLength,
                        ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.offset,
                        ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.agTag));
                TI_DBG1(("ttdsaIOCompleted: statusSent %d responseSent %d\n", ttdsaXchg->statusSent, ttdsaXchg->responseSent));

            }
            break;
        } /* switch */

        if (IOFailed == agTRUE)
        {
            if (agIORequest->sdkData == agNULL)
            {
                tiIORequest_t tiIORequest;
                TI_DBG1(("ttdsaIOCompleted: ERROR ttdsaXchg=%p agIOStatus= 0x%x\n",
                        ttdsaXchg,
                        agIOStatus ));
                TI_DBG1(("CDB= 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
                        ttdsaXchg->agSSPCmndIU.cdb[0],
                        ttdsaXchg->agSSPCmndIU.cdb[1],
                        ttdsaXchg->agSSPCmndIU.cdb[2],
                        ttdsaXchg->agSSPCmndIU.cdb[3],
                        ttdsaXchg->agSSPCmndIU.cdb[4],
                        ttdsaXchg->agSSPCmndIU.cdb[5],
                        ttdsaXchg->agSSPCmndIU.cdb[6],
                        ttdsaXchg->agSSPCmndIU.cdb[7],
                        ttdsaXchg->agSSPCmndIU.cdb[8],
                        ttdsaXchg->agSSPCmndIU.cdb[9],
                        ttdsaXchg->agSSPCmndIU.cdb[10],
                        ttdsaXchg->agSSPCmndIU.cdb[11],
                        ttdsaXchg->agSSPCmndIU.cdb[12],
                        ttdsaXchg->agSSPCmndIU.cdb[13],
                        ttdsaXchg->agSSPCmndIU.cdb[14],
                        ttdsaXchg->agSSPCmndIU.cdb[15] ));

                if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_INACTIVE)
                {
                    TI_DBG1(("ttdsaIOCompleted: wrong DEQUEUE_THIS  1\n"));
                }
                if (ttdsaXchg->retries <= OPEN_RETRY_RETRIES && agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT)
                {
                    TI_DBG2(("ttdsaIOCompleted: 1 loc retries on OSSA_IO_XFER_OPEN_RETRY_TIMEOUT\n"));
                    if ( (agOtherInfo & 0x1) == 1)
                    {
                        /* repsonse phase */
                        TI_DBG2(("ttdsaIOCompleted: 0 loc response retry\n"));
                        /* repsonse retry */
                        saStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG2(("ttdsaIOCompleted: 0 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 0 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                    else if ( (ttdsaXchg->readRspCollapsed == agTRUE) || (ttdsaXchg->wrtRspCollapsed == agTRUE) )
                    {
                        saStatus = saSSPStart(
                                ttdsaXchg->agRoot, /* agRoot, */
                                &ttdsaXchg->IORequestBody.agIORequest, 
                                0, 
                                ttdsaXchg->DeviceData->agDevHandle, /* agDevHandle, */
                                ttdsaXchg->readRspCollapsed ? AGSA_SSP_TGT_READ_GOOD_RESP : AGSA_SSP_TGT_WRITE_GOOD_RESP,
                                        &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                                        agNULL,
                                        &ossaSSPCompleted
                        );
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG1(("ttdsaIOCompleted: 1 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 1 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                    else
                    {
                        if (ttdsaXchg->responseSent == agFALSE)
                        {
                            saStatus = saSSPStart(
                                    ttdsaXchg->agRoot, /* agRoot, */
                                    &ttdsaXchg->IORequestBody.agIORequest, /*agIORequest, */
                                    0, /* queue number */
                                    ttdsaXchg->DeviceData->agDevHandle, /* agDevHandle, */
                                    ttdsaXchg->XchType,
                                    &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                                    agNULL,
                                    &ossaSSPCompleted
                            );
                        }
                        else
                        {
                            /* repsonse retry */
                            TI_DBG1(("ttdsaIOCompleted: 2 loc reponse retry\n"));
                            saStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
                        }
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG1(("ttdsaIOCompleted: 2 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 2 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                }
                else
                {
                    ttdsaXchg->retries = 0;
                    /*
                     * because we are freeing up the exchange
                     * we must let the oslayer know that
                     * we are releasing the resources by
                     * setting the tdData to NULL
                     */
                    tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                    tiIORequest.tdData = agNULL;

                    ostiTargetIOError(
                            tiRoot,
                            &tiIORequest,
                            status,
                            statusDetail
                    );

                    /* clean up resources */
                    ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                }
            } /* saData == agNULL */
            else
            {
                tiIORequest_t tiIORequest;

                TI_DBG1(("ttdsaIOCompleted: 2\n"));
                if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_INACTIVE)
                {
                    TI_DBG1(("ttdsaIOCompleted: wrong DEQUEUE_THIS  2\n"));
                }
                if (ttdsaXchg->retries <= OPEN_RETRY_RETRIES && agIOStatus == OSSA_IO_XFER_OPEN_RETRY_TIMEOUT)
                {
                    TI_DBG1(("ttdsaIOCompleted: 2 loc retries on OSSA_IO_XFER_OPEN_RETRY_TIMEOUT\n"));
                    if ( (agOtherInfo & 0x1) == 1)
                    {
                        /* repsonse phase */
                        TI_DBG2(("ttdsaIOCompleted: 0 loc response retry\n"));
                        /* repsonse retry */
                        saStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG2(("ttdsaIOCompleted: 0 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 0 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                    else if ( (ttdsaXchg->readRspCollapsed == agTRUE) || (ttdsaXchg->wrtRspCollapsed == agTRUE) )
                    {
                        saStatus = saSSPStart(
                                ttdsaXchg->agRoot, /* agRoot, */
                                &ttdsaXchg->IORequestBody.agIORequest, /* agIORequest, */
                                0, /* queue number */
                                ttdsaXchg->DeviceData->agDevHandle, /* agDevHandle, */
                                ttdsaXchg->readRspCollapsed ? AGSA_SSP_TGT_READ_GOOD_RESP : AGSA_SSP_TGT_WRITE_GOOD_RESP,
                                        &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                                        agNULL,
                                        &ossaSSPCompleted
                        );
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG1(("ttdsaIOCompleted: 1 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 1 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                    else
                    {
                        TI_DBG1(("ttdsaIOCompleted: 2 loc ttdsaXchg->id 0x%x datalen 0x%x offset 0x%x agTag 0x%x\n",
                                ttdsaXchg->id,
                                ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.dataLength,
                                ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.offset,
                                ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetReq.agTag));
                        if (ttdsaXchg->responseSent == agFALSE)
                        {
                            saStatus = saSSPStart(
                                    ttdsaXchg->agRoot, /* agRoot, */
                                    &ttdsaXchg->IORequestBody.agIORequest, /* agIORequest, */
                                    0, /* queue number */
                                    ttdsaXchg->DeviceData->agDevHandle, /* agDevHandle, */
                                    ttdsaXchg->XchType,
                                    &ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody,
                                    agNULL,
                                    &ossaSSPCompleted
                            );
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 2 loc response retry\n"));
                            /* repsonse retry */
                            saStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
                        }
                        if (saStatus == AGSA_RC_SUCCESS)
                        {
                            TI_DBG1(("ttdsaIOCompleted: 2 loc retried\n"));
                            ttdsaXchg->retries++;
                        }
                        else
                        {
                            TI_DBG1(("ttdsaIOCompleted: 2 loc retry failed\n"));
                            ttdsaXchg->retries = 0;
                            /*
                             * because we are freeing up the exchange
                             * we must let the oslayer know that
                             * we are releasing the resources by
                             * setting the tdData to NULL
                             */
                            tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                            tiIORequest.tdData = agNULL;

                            ostiTargetIOError(
                                    tiRoot,
                                    &tiIORequest,
                                    status,
                                    statusDetail
                            );

                            /* clean up resources */
                            ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                        }
                    }
                }
                else
                {
                    TI_DBG1(("ttdsaIOCompleted: retry is over\n"));
                    ttdsaXchg->retries = 0;

                    tiIORequest = ttdsaXchg->IORequestBody.IOType.TargetIO.tiIORequest;
                    tiIORequest.tdData = agNULL;

                    ostiTargetIOError(
                            tiRoot,
                            &tiIORequest,
                            status,
                            statusDetail
                    );

                    /* clean up resources */
                    ttdsaXchgFreeStruct(tiRoot,ttdsaXchg);
                }
            } /* saData != agNULL */
        }/* if (IOFailed == agTRUE) */
    } /* not TMrequest */
    else /* TMrequest */
    {
        TI_DBG1(("ttdsaIOCompleted: TM request\n"));
        TI_DBG1(("ttdsaIOCompleted: TM initiator tag 0x%x\n", ttdsaXchg->tag));

        switch(agIOStatus)
        {
        case OSSA_IO_SUCCESS:
            TI_DBG1(("ttdsaIOCompleted: success\n"));
            status = tiIOSuccess;
            break;
        case OSSA_IO_ABORTED:
            TI_DBG1(("ttdsaIOCompleted: ABORTED\n"));
            status        = tiIOFailed;
            statusDetail  = tiDetailAborted;
            IOFailed      = agTRUE;
            break;
        case OSSA_IO_ABORT_RESET:
            TI_DBG1(("ttdsaIOCompleted: ABORT_RESET\n"));
            status        = tiIOFailed;
            statusDetail  = tiDetailAbortReset;
            IOFailed      = agTRUE;
            break;
#ifdef REMOVED
        case OSSA_IO_OVERFLOW: /* fall through */
#endif
        case OSSA_IO_UNDERFLOW: /* fall through */
        case OSSA_IO_FAILED: /* fall through */
#ifdef REMOVED
        case OSSA_IO_NOT_VALID: /* fall through */
#endif
        case OSSA_IO_NO_DEVICE: /* fall through */
            //case OSSA_IO_NO_SUPPORT: /* fall through */       /*added to compile tgt_drv (TP)*/
        case OSSA_IO_LINK_FAILURE: /* fall through */
        case OSSA_IO_PROG_ERROR: /* fall through */
        case OSSA_IO_DS_NON_OPERATIONAL: /* fall through */
        case OSSA_IO_DS_IN_RECOVERY: /* fall through */
        case OSSA_IO_TM_TAG_NOT_FOUND: /* fall through */
        case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE: /* fall through */
        default:
            status        = tiIOFailed;
            statusDetail  = tiDetailOtherError;
            IOFailed      = agTRUE;
            break;
        } /* switch */

        /* for not found IO, we don't call OS */
        if (ttdsaXchg->io_found == agTRUE)
        {
            ostiTargetTmCompleted(
                    tiRoot,
                    ttdsaXchg->IORequestBody.tiIORequest,
                    status,
                    statusDetail
            );
        }

        /* clean up resources */
        ttdsaXchgFreeStruct(tiRoot, ttdsaXchg);


    } /* TM Request */
    return;
}

osGLOBAL void
ttdsaTMProcess(
        tiRoot_t    *tiRoot,
        ttdsaXchg_t *ttdsaXchg
)
{
    tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;

    ttdsaTgt_t                *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;
    agsaSSPScsiTaskMgntReq_t  *agTMIU;
    bit8                       TMFun;
    bit32                      tiTMFun;
    tiIORequest_t              *reftiIORequest = agNULL;
    tdList_t                   *IOList;
    bit32                      IOFound = agFALSE;
    ttdsaXchg_t                *tmp_ttdsaXchg = agNULL;
    agsaRoot_t                 *agRoot = (agsaRoot_t *)&(tdsaAllShared->agRootNonInt);
    agsaIORequest_t            *agIORequest = agNULL;
    agsaIORequest_t            *agIOAbortRequest = agNULL;
    tdsaDeviceData_t           *oneDeviceData = agNULL;
    agsaDevHandle_t            *agDevHandle = agNULL;

    TI_DBG1(("ttdsaTMProcess: start\n"));

    ttdsaXchg->isTMRequest = agTRUE;

    agTMIU = (agsaSSPScsiTaskMgntReq_t *)&(ttdsaXchg->agTMIU);
    TMFun = agTMIU->taskMgntFunction;

    switch (TMFun)
    {
    case AGSA_ABORT_TASK:
        TI_DBG1(("ttdsaTMProcess: ABORT_TASK\n"));
        tiTMFun = AG_ABORT_TASK;
        break;
    case AGSA_ABORT_TASK_SET:
        TI_DBG1(("ttdsaTMProcess: ABORT_TASK_SET\n"));
        tiTMFun = AG_ABORT_TASK_SET;
        break;
    case AGSA_CLEAR_TASK_SET:
        TI_DBG1(("ttdsaTMProcess: CLEAR_TASK_SET\n"));
        tiTMFun = AG_CLEAR_TASK_SET;
        break;
    case AGSA_LOGICAL_UNIT_RESET:
        TI_DBG1(("ttdsaTMProcess: LOGICAL_UNIT_RESET\n"));
        tiTMFun = AG_LOGICAL_UNIT_RESET;
        break;
    case AGSA_CLEAR_ACA:
        TI_DBG1(("ttdsaTMProcess: CLEAR_ACA\n"));
        tiTMFun = AG_CLEAR_ACA;
        break;
    case AGSA_QUERY_TASK:
        TI_DBG1(("ttdsaTMProcess: QUERY_TASK\n"));
        tiTMFun = AG_QUERY_TASK;
        break;
    default:
        TI_DBG1(("ttdsaTMProcess: RESERVED TM 0x%x %d\n", TMFun, TMFun));
        tiTMFun = 0xff; /* unknown task management request */
        break;
    }

    /*
     * Give the OS Specific module to apply it's Task management policy.
     */


    /*
     osGLOBAL void ostiTaskManagement (
                        tiRoot_t          *tiRoot,
                        bit32             task,
                        bit8              *scsiLun,
                        tiIORequest_t     *refTiIORequest,
                        tiIORequest_t     *tiTMRequest,
                        tiDeviceHandle_t  *tiDeviceHandle);
     */
    if (TMFun == AGSA_ABORT_TASK)
    {
        TI_DBG1(("ttdsaTMProcess: if abort task; to be tested \n"));
        /*
      needs to find a reftIIORequest and set it
         */

        IOList = Target->ttdsaXchgData.xchgBusyList.flink;
        IOFound = agFALSE;

        /* search through the current IOList */
        while (IOList != &Target->ttdsaXchgData.xchgBusyList)
        {

            tmp_ttdsaXchg = TDLIST_OBJECT_BASE(ttdsaXchg_t, XchgLinks, IOList);
            if (tmp_ttdsaXchg->tag == agTMIU->tagOfTaskToBeManaged)
            {
                TI_DBG1(("ttdsaTMProcess: tag 0x%x\n",tmp_ttdsaXchg->tag));
                IOFound = agTRUE;
                break;
            }
            IOList = IOList->flink;
        } /* while */

        if (IOFound == agTRUE)
        {

            TI_DBG1(("ttdsaTMProcess: found \n"));
            /* call saSSPAbort() */

            TI_DBG1(("ttdsaTMProcess: loc 1\n"));
            /* abort taskmanagement itself */
            agIOAbortRequest = (agsaIORequest_t *)&(ttdsaXchg->IORequestBody.agIORequest);

            /* IO to be aborted */
            agIORequest = (agsaIORequest_t *)&(tmp_ttdsaXchg->IORequestBody.agIORequest);
            oneDeviceData = tmp_ttdsaXchg->DeviceData;
            agDevHandle = oneDeviceData->agDevHandle;

            if (agIORequest == agNULL)
            {
                TI_DBG1(("ttdsaTMProcess: agIORequest is NULL\n"));
            }
            else
            {
              TI_DBG1(("ttdsaTMProcess: agIORequest is NOT NULL\n"));
              if (agIORequest->sdkData == agNULL)
              {
                TI_DBG1(("ttdsaTMProcess: agIORequest->saData is NULL\n"));
              }
              else
              {
                TI_DBG1(("ttdsaTMProcess: agIORequest->saData is NOT NULL\n"));
#ifdef RPM_SOC
                saSSPAbort(agRoot, agIORequest);
#else
                saSSPAbort(agRoot, agIOAbortRequest,0,agDevHandle,0,agIORequest, agNULL); 
#endif
              }
            }

        } /* FOUND */
        else
        {
            ttdsaXchg->io_found = agFALSE;
            tiTGTSendTmResp(tiRoot,
                    ttdsaXchg->IORequestBody.tiIORequest,
                    tiError /* this is FUNCTION_FAILED */ );
            TI_DBG1(("ttdsaTMProcess: ABORT_TASK not found\n"));
            return;
        }

    } /* ABORT_TASK */
    /*
    reftiIORequest: referred IO request.
    If found, not null. But not used in ramdisk
     */
    TI_DBG1(("ttdsaTMProcess: calling ostiTaskManagement\n"));
    ostiTaskManagement(
            tiRoot,
            tiTMFun,
            ttdsaXchg->agTMIU.lun,
            reftiIORequest,
            ttdsaXchg->IORequestBody.tiIORequest,
            &ttdsaXchg->DeviceData->tiDeviceHandle
    );



    return;
}

/*****************************************************************************
 *
 *  tiTGTIOAbort
 *
 *  Purpose: This function is called to abort an IO previously reported
 *           to oslayer through ostiProcessRequest() function.
 *
 *  Parameters:
 *   tiRoot:         Pointer to driver Instance.
 *   tiIORequest:    Pointer to the I/O request context for this I/O.
 *                   This context was initially passed to the OS Specific
 *                   Module in ostiProcessScsiReq().
 *  Return:
 *   tiSuccess:      Abort request was successfully initiated
 *   tiBusy:         No resources available, try again later
 *   tiError:        Other errors that prevent the abort request from being
 *                   started
 *  Note:
 *
 *****************************************************************************/
osGLOBAL bit32
tiTGTIOAbort (
        tiRoot_t            *tiRoot,
        tiIORequest_t       *taskTag
)
{
    ttdsaXchg_t                 *ttdsaXchg;
    ttdsaXchg_t                 *ttdsaIOAbortXchg;
    tdsaRoot_t                  *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t               *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    agsaRoot_t                  *agRoot = (agsaRoot_t *)&(tdsaAllShared->agRootNonInt);
    agsaIORequest_t             *agIORequest = agNULL;
    agsaIORequest_t             *agIOAbortRequest = agNULL;
    tdsaDeviceData_t            *oneDeviceData = agNULL;
    agsaDevHandle_t             *agDevHandle = agNULL;

    TI_DBG1(("tiTGTIOAbort: start\n"));

    ttdsaXchg        = (ttdsaXchg_t *)taskTag->tdData;

    if (ttdsaXchg == agNULL)
    {
        TI_DBG1(("tiTGTIOAbort: IOError 1 \n"));
        /*
         * this exchange has already been freed.
         * No need to free it
         */
        ostiTargetIOError(
                tiRoot,
                taskTag,
                tiIOFailed,
                tiDetailAborted
        );
    }
    else if (ttdsaXchg->IORequestBody.agIORequest.sdkData == agNULL)
    {
        TI_DBG1(("tiTGTIOAbort: IOError 2 \n"));
        /* We have not issued this IO to the salayer.
         * Abort it right here.
         */
        if (TD_XCHG_GET_STATE(ttdsaXchg) == TD_XCHG_STATE_INACTIVE)
        {
            TI_DBG1(("tiTGTIOAbort: wrong DEQUEUE_THIS\n"));
        }

        TI_DBG1(("tiTGTIOAbort: IOError 3\n"));

        ostiTargetIOError(
                tiRoot,
                taskTag,
                tiIOFailed,
                tiDetailAborted
        );
        TI_DBG1(("tiTGTIOAbort: IOError 4\n"));

        ttdsaXchgFreeStruct(
                ttdsaXchg->tiRoot,
                ttdsaXchg
        );
        TI_DBG1(("tiTGTIOAbort: IOError 5\n"));

    }
    else /* to be tested */
    {
        TI_DBG1(("tiTGTIOAbort: aborting; to be tested \n"));
        /* abort io request itself */
        ttdsaIOAbortXchg = ttdsaXchgGetStruct(agRoot);

        if (ttdsaIOAbortXchg == agNULL)
        {
            TI_DBG1(("tiTGTIOAbort: no free xchg structures\n"));
            //      ttdsaDumpallXchg(tiRoot);
            return tiError;
        }
        ttdsaIOAbortXchg->agRoot  = agRoot;
        ttdsaIOAbortXchg->tiRoot  = tiRoot;
        agIOAbortRequest= &(ttdsaXchg->IORequestBody.agIORequest);
        /* remember IO to be aborted */
        ttdsaIOAbortXchg->tiIOToBeAbortedRequest  = taskTag;
        ttdsaIOAbortXchg->XchgToBeAborted = ttdsaXchg;

        //    ttdsaIOAbortXchg->FrameType = SAS_TM;

        /* io is being aborted */
        ttdsaXchg->oslayerAborting = agTRUE;
        agIORequest = (agsaIORequest_t *)&(ttdsaXchg->IORequestBody.agIORequest);
        oneDeviceData = ttdsaXchg->DeviceData;
        if (oneDeviceData == agNULL)
        {
            TI_DBG1(("tiTGTIOAbort: oneDeviceData is null; wrong\n"));
        }
        else
        {
          agDevHandle = oneDeviceData->agDevHandle;
          ttdsaIOAbortXchg->DeviceData = oneDeviceData;
        }
#ifdef RPM_SOC
        saSSPAbort(agRoot, agIORequest);
#else
        saSSPAbort(agRoot, agIOAbortRequest,0,agDevHandle,0,agIORequest, agNULL); 
    }

    return tiSuccess;
}

osGLOBAL bit32
tiTGTIOAbortAll(
        tiRoot_t            *tiRoot,
        tiDeviceHandle_t    *tiDeviceHandle
)
{
    agsaRoot_t                *agRoot = agNULL;
    tdsaDeviceData_t          *oneDeviceData = agNULL;
    bit32                     status = tiError;

    TI_DBG3(("tiTGTIOAbortAll: start\n"));

    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

    if (oneDeviceData == agNULL)
    {
        TI_DBG1(("tiTGTIOAbortAll: oneDeviceData is NULL!!!\n"));
        return tiError;
    }

    /* for hotplug */
    if (oneDeviceData->valid != agTRUE || oneDeviceData->registered != agTRUE ||
            oneDeviceData->tdPortContext == agNULL )
    {
        TI_DBG1(("tiTGTIOAbortAll: NO Device did %d\n", oneDeviceData->id ));
        TI_DBG1(("tiTGTIOAbortAll: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG1(("tiTGTIOAbortAll: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        return tiError;
    }

    agRoot = oneDeviceData->agRoot;

    if (agRoot == agNULL)
    {
        TI_DBG1(("tiTGTIOAbortAll: agRoot is NULL!!!\n"));
        return tiError;
    }

    /* this is processed in ossaSSPAbortCB, ossaSATAAbortCB, ossaSMPAbortCB */
    oneDeviceData->OSAbortAll = agTRUE;

    status = tdsaAbortAll(tiRoot, agRoot, oneDeviceData);

    return status;

}


/*****************************************************************************
 *
 *  tiTGTSendTmResp
 *
 *  Purpose: This function is called to abort an IO previously reported
 *           to oslayer through ostiProcessRequest() function.
 *
 *  Parameters:
 *   tiRoot:         Pointer to driver Instance.
 *   tiIORequest:    Pointer to the I/O request context for this I/O.
 *                   This context was initially passed to the OS Specific
 *                   Module in ostiProcessScsiReq().
 *  Return:
 *   tiSuccess:      Abort request was successfully initiated
 *   tiBusy:         No resources available, try again later
 *   tiError:        Other errors that prevent the abort request from being
 *                   started
 *  Note:
 *
 *****************************************************************************/
osGLOBAL bit32
tiTGTSendTmResp(
        tiRoot_t          *tiRoot,
        tiIORequest_t     *tiTMRequest,
        bit32             status
)
{
    ttdsaXchg_t               *ttdsaXchg;
    sas_resp_t                *SASResp;
    bit32                     tdStatus;
    TI_DBG1(("tiTGTSendTmResp: start 1\n"));

    ttdsaXchg     = (ttdsaXchg_t *)tiTMRequest->tdData;
    /* set the response and send it */
    /* response status is 0 */
    /* status is TM status */

    TI_DBG1(("tiTGTSendTmResp: start 2\n"));
    SASResp = (sas_resp_t *)ttdsaXchg->resp.virtAddr;
    TI_DBG1(("tiTGTSendTmResp: start 3\n"));

    if (ttdsaXchg->FrameType == SAS_TM)
    {
        SASResp->agResp.status = 0;
        SASResp->agResp.dataPres = RESPONSE_DATA;
        OSSA_WRITE_BE_32(agRoot, SASResp->agResp.responsedataLen, 0, RESPONSE_DATA_LEN);
        OSSA_WRITE_BE_32(agRoot, SASResp->agResp.senseDataLen, 0, 0);
        switch (status)
        {
        case tiSuccess:
            TI_DBG2(("tiTGTSendTmResp: tiSuccess\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_SUCCEEDED;
            break;
        case tiError:
            TI_DBG1(("tiTGTSendTmResp: tiError\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiBusy:
            TI_DBG1(("tiTGTSendTmResp: tibusy\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiIONoDevice:
            TI_DBG1(("tiTGTSendTmResp: tiionodevicee\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiMemoryTooLarge:
            TI_DBG1(("tiTGTSendTmResp: timemorytoolarge\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiMemoryNotAvail:
            TI_DBG1(("tiTGTSendTmResp: timemorynotavail\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiInvalidHandle:
            TI_DBG1(("tiTGTSendTmResp: tiinvalidhandle\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiNotSupported:
            TI_DBG1(("tiTGTSendTmResp: tiNotsupported\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED;
            break;
        case tiReject:
            TI_DBG1(("tiTGTSendTmResp: tireject\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        case tiIncorrectLun:
            TI_DBG1(("tiTGTSendTmResp: tiincorrectlun\n"));
            SASResp->RespData[3] = AGSA_INCORRECT_LOGICAL_UNIT_NUMBER;
            break;
        default:
            TI_DBG1(("tiTGTSendTmResp: default\n"));
            SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_FAILED;
            break;
        }
        ttdsaXchg->resp.length = sizeof(agsaSSPResponseInfoUnit_t) + RESPONSE_DATA_LEN;
        ttdsaXchg->statusSent = agTRUE;
    }
    else
    {
        TI_DBG1(("tiTGTSendTmResp: not TM frame\n"));
        return tiError;
    }

    tdStatus = ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
    if (tdStatus == AGSA_RC_SUCCESS)
    {
        TI_DBG1(("tiTGTSendTmResp: send success\n"));
        return tiSuccess;
    }
    else if (tdStatus == AGSA_RC_FAILURE)
    {
        TI_DBG1(("tiTGTSendTmResp: sending not successful\n"));
        return tiError;
    }
    else
    {
        TI_DBG1(("tiTGTSendTmResp: send busy\n"));
        return tiBusy;
    }


#ifdef REMOVED

    tiTGTSetResp(tiRoot, tiTMRequest, 0, 0, 0);
#endif

#ifdef REMOVED

    if (ttdsaXchg->resp.length != 0)
    {
        TI_DBG1(("tiTGTSendTmResp: respsonse is set \n"));
        TI_DBG1(("tiTGTSendTmResp: resp.length 0x%x\n",
                ttdsaXchg->resp.length));
        ttdsaXchg->responseSent = agTRUE;
       
        ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
    }
    else
    {
        /* no respsonse is set, direct call */
        TI_DBG1(("tiTGTSendTmResp: direct call\n"));
        tiTGTSetResp(tiRoot, tiTMRequest, 0, 0, 0);
        ttdsaXchg->responseSent = agTRUE;
        ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
    }

#define TASK_MANAGEMENT_FUNCTION_COMPLETE         0x0
#define INVALID_FRAME                             0x2
#define TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED    0x4
#define TASK_MANAGEMENT_FUNCTION_FAILED           0x5
#define TASK_MANAGEMENT_FUNCTION_SUCCEEDED        0x8
#define INVALID_LOGICAL_UNIT_NUMBER               0x9
#endif

}



/*****************************************************************************
 *
 *  tiTGTSenseBufferGet
 *
 *  Purpose: This function is called to get the address of sense buffer from
 *           the target specific Transport Dependent Layer.
 *
 *  Parameters:
 *     tiRoot:        Pointer to driver/port instance.
 *     tiIORequest:   I/O request context.
 *     length:        Lenght in bytes of the sense buffer.
 *
 *  Return:  none
 *
 *  Note:
 *
 *****************************************************************************/
osGLOBAL void *tiTGTSenseBufferGet( tiRoot_t      *tiRoot,
        tiIORequest_t *tiIORequest,
        bit32          length
)
{

    ttdsaXchg_t         *ttdsaXchg;

    ttdsaXchg = (ttdsaXchg_t *)tiIORequest->tdData;

    TI_DBG4(("tiTGTSenseBufferGet: start\n"));
    OS_ASSERT((length <= 64), "length too big in tiTGTSenseBufferGet");

    return &ttdsaXchg->resp.virtAddr[sizeof(agsaSSPResponseInfoUnit_t)];
}

/*****************************************************************************
 *
 *  tiTGTSetResp
 *
 *  Purpose: This function is called when the target OS Specific Module is ready
 *           to send a response with the next tiTGTIOStart()
 *           function call. This function allows the TD Layer to setup its
 *           portion of the status and mark it to be sent on the next
 *           tiTGTIOStart() function call.
 *
 *  Parameters:
 *   tiRoot:         Pointer to driver Instance.
 *   tiIORequest:    Pointer to the I/O request context for this I/O.
 *                   This context was initially passed to the OS Specific Module
 *                   in ostiProcessScsiReq().
 *   dataSentLength: How much data sent or received for this Request.
 *   ScsiStatus:     Status for this SCSI command.
 *   senseLength:    Length of sense data if any.
 *
 *  Return: none
 *
 *  Note:
 *
 *****************************************************************************/
osGLOBAL void
tiTGTSetResp( tiRoot_t        *tiRoot,
        tiIORequest_t   *tiIORequest,
        bit32            dataSentLength,
        bit8             ScsiStatus,
        bit32            senseLength
)
{
    /* no call to saSSPStart() in this function */
    /*
    response is normally for task management
    sense is for command with error
    need to know this is for TM or cmd
     */
    /*
  tiTGTSetResp(rdRoot->pTiRoot,
               rdIORequest->tiIORequest,
               dataSentLength,
               ScsiStatus,
               senseLength);



     */
    ttdsaXchg_t               *ttdsaXchg;
    tdsaRoot_t                *tdsaRoot  = (tdsaRoot_t *)tiRoot->tdData;
#ifdef REMOVED
    agsaSSPTargetResponse_t   *agSSPTargetResp;
#endif
    sas_resp_t                *SASResp;
    bit32                      TotalRespLen = 0;

    TI_DBG4 (("tiTGTSetResp: start\n"));
    TI_DBG4 (("tiTGTSetResp: datelen %d senselen %d\n", dataSentLength, senseLength));

    ttdsaXchg = (ttdsaXchg_t *)tiIORequest->tdData;
    SASResp = (sas_resp_t *)ttdsaXchg->resp.virtAddr;

    SASResp->agResp.status = ScsiStatus;

    if (ttdsaXchg->FrameType == SAS_TM)
    {
      
        TI_DBG1(("tiTGTSetResp: TM\n"));
        if (senseLength != 0)
        {
            TI_DBG1 (("tiTGTSetResp: non-zero sensedatalen for TM\n"));
            return;
        }
        SASResp->agResp.dataPres = RESPONSE_DATA;
        OSSA_WRITE_BE_32(agRoot, SASResp->agResp.responsedataLen, 0, RESPONSE_DATA_LEN);
        OSSA_WRITE_BE_32(agRoot, SASResp->agResp.senseDataLen, 0, 0);
        SASResp->RespData[3] = AGSA_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED;
        TotalRespLen = sizeof(agsaSSPResponseInfoUnit_t) + RESPONSE_DATA_LEN;
    }
    else
    {
        if (senseLength == 0)
        {
            TI_DBG4 (("tiTGTSetResp: CMND, no data\n"));
            /* good and no data present */
            SASResp->agResp.dataPres = NO_DATA;
            OSSA_WRITE_BE_32(agRoot, SASResp->agResp.responsedataLen, 0, 0);
            OSSA_WRITE_BE_32(agRoot, SASResp->agResp.senseDataLen, 0, 0);
            TotalRespLen = sizeof(agsaSSPResponseInfoUnit_t);
            /* collapse good response with READ */
            if (ttdsaXchg->XchType == AGSA_SSP_TGT_READ_DATA)
            {
                TI_DBG4(("tiTGTSetResp: read rsp collapse\n"));

                if (tdsaRoot->autoGoodRSP & READ_GOOD_RESPONSE)
                    ttdsaXchg->readRspCollapsed = agTRUE;
            }
            /* collapse good response with WRITE */
            if (ttdsaXchg->XchType == AGSA_SSP_TGT_WRITE_DATA)
            {
                TI_DBG4(("tiTGTSetResp: write rsp collapse\n"));
                if (tdsaRoot->autoGoodRSP & WRITE_GOOD_RESPONSE)
                {
                  if (tiIS_SPC(TI_TIROOT_TO_AGROOT(tiRoot)))
                  {
                    ttdsaXchg->wrtRspCollapsed = agFALSE;
                  }
                  else
                  {
                    ttdsaXchg->wrtRspCollapsed = agTRUE;
                  }
    
                }
            }
        }
        else
        {
            TI_DBG4 (("tiTGTSetResp: CMND, sense data\n"));
            /* bad and sense data */
            SASResp->agResp.dataPres = SENSE_DATA;
            OSSA_WRITE_BE_32(agRoot, SASResp->agResp.responsedataLen, 0, 0);
            OSSA_WRITE_BE_32(agRoot, SASResp->agResp.senseDataLen, 0, senseLength);
            TotalRespLen = sizeof(agsaSSPResponseInfoUnit_t) + senseLength;
        }
    }

    ttdsaXchg->statusSent = agTRUE;

    TI_DBG4(("tiTGTSetResp: ttdsaXchg %p\n", ttdsaXchg));
    TI_DBG4(("tiTGTSetResp: TotalRespLen 0x%x \n", TotalRespLen));
    TI_DBG4(("tiTGTSetResp: upper 0x%x \n",
            ttdsaXchg->resp.phyAddrUpper));
    TI_DBG4(("tiTGTSetResp: lower 0x%x \n",
            ttdsaXchg->resp.phyAddrLower));



    /* set the correct response length */
    ttdsaXchg->resp.length = TotalRespLen;

    dumpresp((bit8 *)ttdsaXchg->resp.virtAddr, ttdsaXchg->resp.length);

#ifdef REMOVED
    /*
    send TM reponse (which has only  response data not sense data here
    since ramdisk does not call IOstart for this
     */

    if (ttdsaXchg->FrameType == SAS_TM)
    {
        TI_DBG1(("tiTGTSetResp: respsonse is set \n"));
        TI_DBG1(("tiTGTSetResp: resp.length 0x%x\n",
                ttdsaXchg->resp.length));
        ttdsaSendResp(ttdsaXchg->agRoot, ttdsaXchg);
    }
#endif
#ifdef REMOVED
    /* sas response */
    agSSPTargetResp =
            &(ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse);

    agSSPTargetResp->agTag = ttdsaXchg->tag;
    agSSPTargetResp->respBufLength = TotalRespLen;
    agSSPTargetResp->respBufUpper
    = ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.respBufUpper;
    agSSPTargetResp->respBufLower
    = ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.respBufLower;



    TI_DBG4(("tiTGTSetResp: len 0x%x \n",
            ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.respBufLength));
    TI_DBG4(("tiTGTSetResp: upper 0x%x \n",
            ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.respBufUpper));
    TI_DBG4(("tiTGTSetResp: lower 0x%x \n",
            ttdsaXchg->IORequestBody.transport.SAS.agSASRequestBody.sspTargetResponse.respBufLower));
#endif

    return;
}



/******************************************************************************
 *
 *  tiTGTGetDeviceHandles
 *
 *  Purpose: This routine is called to to return the device handles for each
 *           device currently available.
 *
 *  Parameters:
 *     tiRoot:   Pointer to driver Instance.
 *     agDev[]:  Array to receive pointers to the device handles.
 *     maxDevs:  Number of device handles which will fit in array pointed
 *               by agDev.
 *  Return:
 *    Number of device handle slots present (however, only maxDevs
 *    are copied into tiDev[]) which may be greater than the number of
 *    handles actually present.
 *
 *  Note:
 *
 ******************************************************************************/

osGLOBAL bit32
tiTGTGetDeviceHandles(
        tiRoot_t            *tiRoot,
        tiPortalContext_t   *tiPortalContext,
        tiDeviceHandle_t    *tiDev[],
        bit32               maxDevs
)
{
    tdsaRoot_t                *tdsaRoot        = (tdsaRoot_t *) tiRoot->tdData;
    tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
    ttdsaTgt_t                *Target = (ttdsaTgt_t *)tdsaAllShared->ttdsaTgt;
    bit32                     deviceToReturn;
    bit32                     devicePresent=0;
    bit32                     deviceIndex=0;
    tdList_t                  *PortContextList;
    tdsaPortContext_t         *onePortContext = agNULL;
    tdList_t                  *DeviceListList;
    tdsaDeviceData_t          *oneDeviceData = agNULL;
    bit32                     found = agFALSE;


    TI_DBG4 (("tiTGTGetDeviceHandles: start\n"));

    /* Check boundary condition */
    if (maxDevs > Target->OperatingOption.MaxTargets)
    {
        deviceToReturn = Target->OperatingOption.MaxTargets;
    }
    else
    {
        deviceToReturn = maxDevs;
    }


    /* make sure tiPortalContext is valid */
    PortContextList = tdsaAllShared->MainPortContextList.flink;
    while (PortContextList != &(tdsaAllShared->MainPortContextList))
    {
        onePortContext = TDLIST_OBJECT_BASE(tdsaPortContext_t, MainLink, PortContextList);
        if (onePortContext->tiPortalContext == tiPortalContext)
        {
            TI_DBG4(("tiTGTGetDeviceHandles: found; oneportContext ID %d\n", onePortContext->id));
            found = agTRUE;
            break;
        }
        PortContextList = PortContextList->flink;
    }

    if (found == agFALSE)
    {
        TI_DBG4(("tiTGTGetDeviceHandles: No corressponding tdsaPortContext\n"));
        return 0;
    }


    /* go through device list and returns them */
    DeviceListList = tdsaAllShared->MainDeviceList.flink;
    while (DeviceListList != &(tdsaAllShared->MainDeviceList))
    {
        oneDeviceData = TDLIST_OBJECT_BASE(tdsaDeviceData_t, MainLink, DeviceListList);
        TI_DBG4(("tiTGTGetDeviceHandles: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        TI_DBG4(("tiTGTGetDeviceHandles: device AddrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
        TI_DBG4(("tiTGTGetDeviceHandles: device AddrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        TI_DBG4(("tiTGTGetDeviceHandles: handle %p\n",  &(oneDeviceData->tiDeviceHandle)));
        if (oneDeviceData->valid == agTRUE)
        {
            TI_DBG4(("tiTGTGetDeviceHandles: valid deviceindex %d devicePresent %d\n", deviceIndex, devicePresent));

            tiDev[deviceIndex] = &(oneDeviceData->tiDeviceHandle);
            devicePresent++;
        }
        else
        {
            tiDev[deviceIndex] = agNULL;
            TI_DBG4(("tiTGTGetDeviceHandles: not valid deviceindex %d devicePresent %d\n", deviceIndex, devicePresent));
        }
        deviceIndex++;

        if (devicePresent >= deviceToReturn )
        {
            break;
        }
        DeviceListList = DeviceListList->flink;
    }

    return devicePresent;
}




/******************************************************************************
 *
 *  tiTGTGetDeviceInfo
 *
 *  Purpose: This routine is called to to return the device information for
 *           specified device handle.
 *
 *  Parameters:
 *     tiRoot:   Pointer to driver Instance.
 *     tiDeviceHandle:  device handle associated with the device for which
 *                      information is queried
 *     tiDeviceInfo:    device information structure containing address and name.
 *
 *  Return:
 *     tiSuccess: if the device handle is valid.
 *     tiError  : if the device handle is not valid.
 *
 *  Note:
 *
 ******************************************************************************/
osGLOBAL bit32
tiTGTGetDeviceInfo(
        tiRoot_t            *tiRoot,
        tiDeviceHandle_t    *tiDeviceHandle,
        tiDeviceInfo_t      *tiDeviceInfo)
{
    tdsaDeviceData_t       *oneDeviceData = agNULL;


    TI_DBG4 (("tiTGTGetDeviceInfo: start\n"));

    if (tiDeviceHandle == agNULL)
    {
        TI_DBG4 (("tiTGTGetDeviceInfo: tiDeviceHandle is NULL\n"));
        return tiError;
    }

    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;

    if (oneDeviceData == agNULL)
    {
        TI_DBG4 (("tiTGTGetDeviceInfo: oneDeviceData is NULL\n"));
        return tiError;
    }

    /* filling in the link rate */
    if (oneDeviceData->registered == agTRUE)
    {
        tiDeviceInfo->info.devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate;
    }
    else
    {
        tiDeviceInfo->info.devType_S_Rate = oneDeviceData->agDeviceInfo.devType_S_Rate & 0x0f;
    }

    /* temp just returning local and remote SAS address; doesn't have a name */
    tiDeviceInfo->remoteName    = (char *)&(oneDeviceData->tdPortContext->sasRemoteAddressHi);
    tiDeviceInfo->remoteAddress = (char *)&(oneDeviceData->tdPortContext->sasRemoteAddressLo);

    tiDeviceInfo->localName     = (char *)&(oneDeviceData->tdPortContext->sasLocalAddressHi);
    tiDeviceInfo->localAddress  = (char *)&(oneDeviceData->tdPortContext->sasLocalAddressLo);

    return tiSuccess;
}

/*****************************************************************************
 *! \brief ttdssIOAbortedHandler
 *
 *  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
 *            layer with agIOStatus = OSSA_IO_ABORTED
 *
 *  \param  agRoot:            pointer to port instance
 *  \param  agIORequest:       pointer to I/O request
 *  \param  agIOStatus:        I/O status given by LL layer
 *  \param  agIOInfoLen:       lenth of complete SAS RESP frame
 *  \param  agParam            A Handle used to refer to the response frame or handle
 *                             of abort request
 *  \param  agOtherInfo        Residual count
 *  \return: None
 *
 *
 *****************************************************************************/
/* see itdosIOCompleted() and itdinit.c and  itdIoAbortedHandler in itdio.c*/
osGLOBAL void
ttdssIOAbortedHandler (
        agsaRoot_t              *agRoot,
        agsaIORequest_t         *agIORequest,
        bit32                   agIOStatus,
        bit32                   agIOInfoLen,
        void                    *agParam,
        bit32                   agOtherInfo
)
{
    tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
    tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
    tdIORequestBody_t      *tdIORequestBody;

    TI_DBG1(("itdssIOAbortedHandler: start\n"));
    tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

    if (agIOStatus != OSSA_IO_ABORTED)
    {
        TI_DBG1(("itdssIOAbortedHandler: incorrect agIOStatus 0x%x\n", agIOStatus));

    }

    ostiTargetIOError(
            tiRoot,
            tdIORequestBody->tiIORequest,
            tiIOFailed,
            tiDetailAborted
    );

    return;
}


