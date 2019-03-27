/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *


 **************************************************************************/
/******************************************************************************
 @File          bm.c

 @Description   BM
*//***************************************************************************/
#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "debug_ext.h"
#include "mm_ext.h"

#include "bm.h"


t_Error BM_ConfigException(t_Handle h_Bm, e_BmExceptions exception, bool enable);


/****************************************/
/*       static functions               */
/****************************************/

static volatile bool blockingFlag = FALSE;
static void BmIpcMsgCompletionCB(t_Handle   h_Module,
                                 uint8_t    *p_Msg,
                                 uint8_t    *p_Reply,
                                 uint32_t   replyLength,
                                 t_Error    status)
{
    SANITY_CHECK_RETURN(h_Module, E_INVALID_HANDLE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(h_Module);
#endif /* DISABLE_SANITY_CHECKS */
    UNUSED(p_Msg);UNUSED(p_Reply);UNUSED(replyLength);UNUSED(status);

    blockingFlag = FALSE;
}

static t_Error BmHandleIpcMsgCB(t_Handle  h_Bm,
                                uint8_t   *p_Msg,
                                uint32_t  msgLength,
                                uint8_t   *p_Reply,
                                uint32_t  *p_ReplyLength)
{
    t_Bm                    *p_Bm           = (t_Bm*)h_Bm;
    t_BmIpcMsg              *p_IpcMsg       = (t_BmIpcMsg*)p_Msg;
    t_BmIpcReply            *p_IpcReply     = (t_BmIpcReply *)p_Reply;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((msgLength >= sizeof(uint32_t)), E_INVALID_VALUE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(msgLength);
#endif /* DISABLE_SANITY_CHECKS */

    ASSERT_COND(p_IpcMsg);

    memset(p_IpcReply, 0, (sizeof(uint8_t) * BM_IPC_MAX_REPLY_SIZE));
    *p_ReplyLength = 0;

    switch(p_IpcMsg->msgId)
    {
        case (BM_MASTER_IS_ALIVE):
            *(uint8_t*)p_IpcReply->replyBody = 1;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case (BM_SET_POOL_THRESH):
        {
            t_Error                 err;
            t_BmIpcPoolThreshParams ipcPoolThresh;

            memcpy((uint8_t*)&ipcPoolThresh, p_IpcMsg->msgBody, sizeof(t_BmIpcPoolThreshParams));
            if ((err = BmSetPoolThresholds(p_Bm,
                                           ipcPoolThresh.bpid,
                                           ipcPoolThresh.thresholds)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
        case (BM_UNSET_POOL_THRESH):
        {
            t_Error                 err;
            t_BmIpcPoolThreshParams ipcPoolThresh;

            memcpy((uint8_t*)&ipcPoolThresh, p_IpcMsg->msgBody, sizeof(t_BmIpcPoolThreshParams));
            if ((err = BmUnSetPoolThresholds(p_Bm,
                                             ipcPoolThresh.bpid)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
        case (BM_GET_COUNTER):
        {
            t_BmIpcGetCounter   ipcCounter;
            uint32_t            count;

            memcpy((uint8_t*)&ipcCounter, p_IpcMsg->msgBody, sizeof(t_BmIpcGetCounter));
            count = BmGetCounter(p_Bm,
                                 (e_BmInterModuleCounters)ipcCounter.enumId,
                                 ipcCounter.bpid);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&count, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (BM_GET_REVISION):
        {
            t_BmRevisionInfo    revInfo;
            t_BmIpcRevisionInfo ipcRevInfo;

            p_IpcReply->error = (uint32_t)BmGetRevision(h_Bm, &revInfo);
            ipcRevInfo.majorRev = revInfo.majorRev;
            ipcRevInfo.minorRev = revInfo.minorRev;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcRevInfo, sizeof(t_BmIpcRevisionInfo));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_BmIpcRevisionInfo);
            break;
        }
        case (BM_FORCE_BPID):
        {
            t_BmIpcBpidParams   ipcBpid;
            uint32_t            tmp;

            memcpy((uint8_t*)&ipcBpid, p_IpcMsg->msgBody, sizeof(t_BmIpcBpidParams));
            tmp = BmBpidGet(p_Bm, TRUE, ipcBpid.bpid);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&tmp, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (BM_PUT_BPID):
        {
            t_Error             err;
            t_BmIpcBpidParams   ipcBpid;

            memcpy((uint8_t*)&ipcBpid, p_IpcMsg->msgBody, sizeof(t_BmIpcBpidParams));
            if ((err = BmBpidPut(p_Bm, ipcBpid.bpid)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
        default:
            *p_ReplyLength = 0;
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("command not found!!!"));
    }

    return E_OK;
}

static t_Error CheckBmParameters(t_Bm *p_Bm)
{
    if ((p_Bm->p_BmDriverParams->partBpidBase + p_Bm->p_BmDriverParams->partNumOfPools) > BM_MAX_NUM_OF_POOLS)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("partBpidBase+partNumOfPools out of range!!!"));

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        if (!p_Bm->p_BmDriverParams->totalNumOfBuffers)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalNumOfBuffers must be larger than '0'!!!"));
        if (p_Bm->p_BmDriverParams->totalNumOfBuffers > (128*MEGABYTE))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalNumOfBuffers must be equal or smaller than 128M!!!"));
        if(!p_Bm->f_Exception)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));
    }

    return E_OK;
}

static __inline__ uint32_t GenerateThresh(uint32_t val, int roundup)
{
    uint32_t e = 0;    /* co-efficient, exponent */
    uint32_t oddbit = 0;
    while(val > 0xff) {
        oddbit = val & 1;
        val >>= 1;
        e++;
        if(roundup && oddbit)
            val++;
    }
    return (val | (e << 8));
}

static t_Error BmSetPool(t_Handle   h_Bm,
                         uint8_t    bpid,
                         uint32_t   swdet,
                         uint32_t   swdxt,
                         uint32_t   hwdet,
                         uint32_t   hwdxt)
{
    t_Bm    *p_Bm = (t_Bm*)h_Bm;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(bpid < BM_MAX_NUM_OF_POOLS, E_INVALID_VALUE);

    WRITE_UINT32(p_Bm->p_BmRegs->swdet[bpid], GenerateThresh(swdet, 0));
    WRITE_UINT32(p_Bm->p_BmRegs->swdxt[bpid], GenerateThresh(swdxt, 1));
    WRITE_UINT32(p_Bm->p_BmRegs->hwdet[bpid], GenerateThresh(hwdet, 0));
    WRITE_UINT32(p_Bm->p_BmRegs->hwdxt[bpid], GenerateThresh(hwdxt, 1));

    return E_OK;
}

/****************************************/
/*       Inter-Module functions        */
/****************************************/

t_Error BmSetPoolThresholds(t_Handle h_Bm, uint8_t bpid, const uint32_t *thresholds)
{
    t_Bm *p_Bm = (t_Bm*)h_Bm;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(bpid < BM_MAX_NUM_OF_POOLS, E_INVALID_VALUE);

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        return BmSetPool(h_Bm,
                         bpid,
                         thresholds[0],
                         thresholds[1],
                         thresholds[2],
                         thresholds[3]);
    }
    else if (p_Bm->h_Session)
    {
        t_BmIpcMsg              msg;
        t_BmIpcPoolThreshParams ipcPoolThresh;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_BmIpcMsg));
        ipcPoolThresh.bpid  = bpid;
        memcpy(ipcPoolThresh.thresholds, thresholds, sizeof(uintptr_t) * MAX_DEPLETION_THRESHOLDS);
        msg.msgId           = BM_SET_POOL_THRESH;
        memcpy(msg.msgBody, &ipcPoolThresh, sizeof(t_BmIpcPoolThreshParams));
        if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_BmIpcPoolThreshParams),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL)) != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        return E_OK;
    }
    else
        RETURN_ERROR(WARNING, E_NOT_SUPPORTED, ("IPC"));
}

t_Error BmUnSetPoolThresholds(t_Handle h_Bm, uint8_t bpid)
{
    t_Bm *p_Bm = (t_Bm*)h_Bm;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(bpid < BM_MAX_NUM_OF_POOLS, E_INVALID_VALUE);

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        return BmSetPool(h_Bm,
                         bpid,
                         0,
                         0,
                         0,
                         0);
    }
    else if (p_Bm->h_Session)
    {
        t_BmIpcMsg              msg;
        t_BmIpcPoolThreshParams ipcPoolThresh;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_BmIpcMsg));
        memset(&ipcPoolThresh, 0, sizeof(t_BmIpcPoolThreshParams));
        ipcPoolThresh.bpid  = bpid;
        msg.msgId           = BM_UNSET_POOL_THRESH;
        memcpy(msg.msgBody, &ipcPoolThresh, sizeof(t_BmIpcPoolThreshParams));
        if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_BmIpcPoolThreshParams),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL)) != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        return E_OK;
    }
    else
        RETURN_ERROR(WARNING, E_NOT_SUPPORTED, ("IPC"));
}

uint32_t BmGetCounter(t_Handle h_Bm, e_BmInterModuleCounters counter, uint8_t bpid)
{
    t_Bm *p_Bm = (t_Bm*)h_Bm;

    SANITY_CHECK_RETURN_VALUE(p_Bm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(bpid < BM_MAX_NUM_OF_POOLS, E_INVALID_VALUE, 0);
    SANITY_CHECK_RETURN_VALUE((((p_Bm->guestId == NCSW_MASTER_ID) && p_Bm->p_BmRegs) ||
                               (p_Bm->guestId != NCSW_MASTER_ID)), E_INVALID_STATE, 0);

    if ((p_Bm->guestId == NCSW_MASTER_ID) ||
        (!p_Bm->h_Session && p_Bm->p_BmRegs))
    {
        switch(counter)
        {
            case(e_BM_IM_COUNTERS_POOL_CONTENT):
                return GET_UINT32(p_Bm->p_BmRegs->content[bpid]);
            case(e_BM_IM_COUNTERS_POOL_SW_DEPLETION):
                return GET_UINT32(p_Bm->p_BmRegs->sdcnt[bpid]);
            case(e_BM_IM_COUNTERS_POOL_HW_DEPLETION):
                return GET_UINT32(p_Bm->p_BmRegs->hdcnt[bpid]);
            case(e_BM_IM_COUNTERS_FBPR):
                return GET_UINT32(p_Bm->p_BmRegs->fbpr_fpc);
            default:
                break;
        }
        /* should never get here */
        ASSERT_COND(FALSE);
    }
    else if (p_Bm->h_Session)
    {
        t_BmIpcMsg              msg;
        t_BmIpcReply            reply;
        t_BmIpcGetCounter       ipcCounter;
        uint32_t                replyLength;
        uint32_t                count;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_BmIpcMsg));
        memset(&reply, 0, sizeof(t_BmIpcReply));
        ipcCounter.bpid         = bpid;
        ipcCounter.enumId       = (uint32_t)counter;
        msg.msgId               = BM_GET_COUNTER;
        memcpy(msg.msgBody, &ipcCounter, sizeof(t_BmIpcGetCounter));
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_BmIpcGetCounter),
                                         (uint8_t*)&reply,
                                         &replyLength,
                                         NULL,
                                         NULL)) != E_OK)
            REPORT_ERROR(MAJOR, errCode, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
        {
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
            errCode = E_INVALID_VALUE;
        }
        if (errCode == E_OK)
        {
            memcpy((uint8_t*)&count, reply.replyBody, sizeof(uint32_t));
            return count;
        }
    }
    else
        REPORT_ERROR(WARNING, E_NOT_SUPPORTED,
                     ("In 'guest', either IPC or 'baseAddress' is required!"));

    return 0;
}

t_Error BmGetRevision(t_Handle h_Bm, t_BmRevisionInfo *p_BmRevisionInfo)
{
    t_Bm        *p_Bm = (t_Bm*)h_Bm;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmRevisionInfo, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR((((p_Bm->guestId == NCSW_MASTER_ID) && p_Bm->p_BmRegs) ||
                               (p_Bm->guestId != NCSW_MASTER_ID)), E_INVALID_STATE);

    if ((p_Bm->guestId == NCSW_MASTER_ID) ||
        (!p_Bm->h_Session && p_Bm->p_BmRegs))
    {
        /* read revision register 1 */
        tmpReg = GET_UINT32(p_Bm->p_BmRegs->ip_rev_1);
        p_BmRevisionInfo->majorRev = (uint8_t)((tmpReg & REV1_MAJOR_MASK) >> REV1_MAJOR_SHIFT);
        p_BmRevisionInfo->minorRev = (uint8_t)((tmpReg & REV1_MINOR_MASK) >> REV1_MINOR_SHIFT);
    }
    else if (p_Bm->h_Session)
    {
        t_BmIpcMsg              msg;
        t_BmIpcReply            reply;
        t_BmIpcRevisionInfo     ipcRevInfo;
        uint32_t                replyLength;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_BmIpcMsg));
        memset(&reply, 0, sizeof(t_BmIpcReply));
        msg.msgId           = BM_GET_REVISION;
        replyLength = sizeof(uint32_t) + sizeof(t_BmIpcRevisionInfo);
        if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId),
                                         (uint8_t*)&reply,
                                         &replyLength,
                                         NULL,
                                         NULL)) != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(t_BmIpcRevisionInfo)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        memcpy((uint8_t*)&ipcRevInfo, reply.replyBody, sizeof(t_BmIpcRevisionInfo));
        p_BmRevisionInfo->majorRev = ipcRevInfo.majorRev;
        p_BmRevisionInfo->minorRev = ipcRevInfo.minorRev;
        return (t_Error)(reply.error);
    }
    else
        RETURN_ERROR(WARNING, E_NOT_SUPPORTED,
                     ("In 'guest', either IPC or 'baseAddress' is required!"));

    return E_OK;
}

static void FreeInitResources(t_Bm *p_Bm)
{
    if (p_Bm->p_FbprBase)
        XX_FreeSmart(p_Bm->p_FbprBase);
    if (p_Bm->h_Session)
        XX_IpcFreeSession(p_Bm->h_Session);
    if (p_Bm->h_BpidMm)
        MM_Free(p_Bm->h_BpidMm);
}

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle BM_Config(t_BmParam *p_BmParam)
{
    t_Bm        *p_Bm;

    SANITY_CHECK_RETURN_VALUE(p_BmParam, E_INVALID_HANDLE, NULL);

    p_Bm = (t_Bm *)XX_Malloc(sizeof(t_Bm));
    if (!p_Bm)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("BM obj!!!"));
        return NULL;
    }
    memset(p_Bm, 0, sizeof(t_Bm));

    p_Bm->p_BmDriverParams = (t_BmDriverParams *)XX_Malloc(sizeof(t_BmDriverParams));
    if (!p_Bm->p_BmDriverParams)
    {
        XX_Free(p_Bm);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Bm driver parameters"));
        return NULL;
    }
    memset(p_Bm->p_BmDriverParams, 0, sizeof(t_BmDriverParams));

    p_Bm->guestId                               = p_BmParam->guestId;
    p_Bm->p_BmDriverParams->partNumOfPools      = p_BmParam->partNumOfPools;
    p_Bm->p_BmDriverParams->partBpidBase        = p_BmParam->partBpidBase;
    p_Bm->p_BmRegs                              = (t_BmRegs *)UINT_TO_PTR(p_BmParam->baseAddress);

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        p_Bm->exceptions                            = DEFAULT_exceptions;
        p_Bm->f_Exception                           = p_BmParam->f_Exception;
        p_Bm->h_App                                 = p_BmParam->h_App;
        p_Bm->errIrq                                = p_BmParam->errIrq;
        p_Bm->p_BmDriverParams->totalNumOfBuffers   = p_BmParam->totalNumOfBuffers;
        p_Bm->p_BmDriverParams->fbprMemPartitionId  = p_BmParam->fbprMemPartitionId;
        p_Bm->p_BmDriverParams->fbprThreshold       = DEFAULT_fbprThreshold;
        p_Bm->p_BmDriverParams->liodn               = p_BmParam->liodn;

    }
    /* build the BM partition IPC address */
    memset(p_Bm->moduleName, 0, MODULE_NAME_SIZE);
    if(Sprint (p_Bm->moduleName, "BM_0_%d",p_Bm->guestId) != (p_Bm->guestId<10 ? 6:7))
    {
        XX_Free(p_Bm->p_BmDriverParams);
        XX_Free(p_Bm);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        return NULL;
    }
    return p_Bm;
}

t_Error BM_Init(t_Handle h_Bm)
{
    t_Bm                *p_Bm = (t_Bm *)h_Bm;
    t_Error             err;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Bm->p_BmDriverParams, E_INVALID_HANDLE);

    CHECK_INIT_PARAMETERS(p_Bm, CheckBmParameters);

    if (p_Bm->p_BmDriverParams->partNumOfPools)
        if (MM_Init(&p_Bm->h_BpidMm, p_Bm->p_BmDriverParams->partBpidBase, p_Bm->p_BmDriverParams->partNumOfPools) != E_OK)
        {
            FreeInitResources(p_Bm);
            RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("BM-BPIDS-MEM partition!!!"));
        }

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        uint64_t            phyAddr;
        t_BmRevisionInfo    revInfo;
        uint32_t            dsSize, exp;

        BmGetRevision(p_Bm, &revInfo);
        DBG(TRACE, ("Bman ver:%02x,%02x", revInfo.majorRev, revInfo.minorRev));

        WRITE_UINT32(p_Bm->p_BmRegs->liodnr, (uint16_t)p_Bm->p_BmDriverParams->liodn);

        /* FBPR memory */
        dsSize = (uint32_t)(p_Bm->p_BmDriverParams->totalNumOfBuffers * (FBPR_ENTRY_SIZE / 8));
        LOG2(dsSize, exp);
        if (!POWER_OF_2(dsSize)) (exp++);
        dsSize = (uint32_t)(1 << exp);
        if (dsSize < (4*KILOBYTE))
        {
            dsSize = (4*KILOBYTE);
            LOG2(dsSize, exp);
        }
        p_Bm->p_FbprBase = XX_MallocSmart(dsSize, (int)p_Bm->p_BmDriverParams->fbprMemPartitionId, dsSize);
        if (!p_Bm->p_FbprBase)
        {
            FreeInitResources(p_Bm);
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FBPR obj!!!"));
        }
        phyAddr = XX_VirtToPhys(p_Bm->p_FbprBase);
        WRITE_UINT32(p_Bm->p_BmRegs->fbpr_bare, ((uint32_t)(phyAddr >> 32) & 0xffff));
        WRITE_UINT32(p_Bm->p_BmRegs->fbpr_bar, (uint32_t)phyAddr);
        WRITE_UINT32(p_Bm->p_BmRegs->fbpr_ar, (exp - 1));

        WRITE_UINT32(p_Bm->p_BmRegs->fbpr_fp_lwit, p_Bm->p_BmDriverParams->fbprThreshold);
        WRITE_UINT32(p_Bm->p_BmRegs->err_isr, p_Bm->exceptions);
        WRITE_UINT32(p_Bm->p_BmRegs->err_ier, p_Bm->exceptions);
        WRITE_UINT32(p_Bm->p_BmRegs->err_isdr, 0x0);
        if (p_Bm->errIrq  != NO_IRQ)
        {
            XX_SetIntr(p_Bm->errIrq, BM_ErrorIsr, p_Bm);
            XX_EnableIntr(p_Bm->errIrq);
        }

        if ((err = XX_IpcRegisterMsgHandler(p_Bm->moduleName, BmHandleIpcMsgCB, p_Bm, BM_IPC_MAX_REPLY_SIZE)) != E_OK)
        {
            FreeInitResources(p_Bm);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
    }
    else /* guest mode */
    {
        char                    masterModuleName[MODULE_NAME_SIZE];

        memset(masterModuleName, 0, MODULE_NAME_SIZE);
        if(Sprint (masterModuleName, "BM_0_%d", NCSW_MASTER_ID) != (NCSW_MASTER_ID<10 ? 6:7))
        {
            FreeInitResources(p_Bm);
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        }

        p_Bm->h_Session     = XX_IpcInitSession(masterModuleName, p_Bm->moduleName);
        if (p_Bm->h_Session)
        {
            t_BmIpcMsg              msg;
            uint8_t                 isMasterAlive = 0;
            t_BmIpcReply            reply;
            uint32_t                replyLength;

            memset(&msg, 0, sizeof(t_BmIpcMsg));
            memset(&reply, 0, sizeof(t_BmIpcReply));
            msg.msgId           = BM_MASTER_IS_ALIVE;
            replyLength = sizeof(uint32_t) + sizeof(uint8_t);
            do
            {
                blockingFlag = TRUE;
                if ((err = XX_IpcSendMessage(p_Bm->h_Session,
                                             (uint8_t*)&msg,
                                             sizeof(msg.msgId),
                                             (uint8_t*)&reply,
                                             &replyLength,
                                             BmIpcMsgCompletionCB,
                                             p_Bm)) != E_OK)
                    REPORT_ERROR(MAJOR, err, NO_MSG);
                while(blockingFlag) ;
                if(replyLength != (sizeof(uint32_t) + sizeof(uint8_t)))
                    REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
                isMasterAlive = *(uint8_t*)(reply.replyBody);
            } while (!isMasterAlive);
        }
    }

    XX_Free(p_Bm->p_BmDriverParams);
    p_Bm->p_BmDriverParams = NULL;

    return E_OK;
}

t_Error BM_Free(t_Handle h_Bm)
{
    t_Bm    *p_Bm = (t_Bm *)h_Bm;

    if (!p_Bm)
       return ERROR_CODE(E_INVALID_HANDLE);

    if (p_Bm->guestId == NCSW_MASTER_ID)
    {
        XX_IpcUnregisterMsgHandler(p_Bm->moduleName);
        if (p_Bm->errIrq  != NO_IRQ)
        {
            XX_DisableIntr(p_Bm->errIrq);
            XX_FreeIntr(p_Bm->errIrq);
        }
    }
    FreeInitResources(p_Bm);

    if(p_Bm->p_BmDriverParams)
        XX_Free(p_Bm->p_BmDriverParams);

    XX_Free(p_Bm);
    return E_OK;
}

t_Error BM_ConfigException(t_Handle h_Bm, e_BmExceptions exception, bool enable)
{
    t_Bm                *p_Bm = (t_Bm*)h_Bm;
    uint32_t            bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Bm->p_BmDriverParams, E_INVALID_HANDLE);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if(bitMask)
    {
        if (enable)
            p_Bm->exceptions |= bitMask;
        else
            p_Bm->exceptions &= ~bitMask;
   }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

t_Error BM_ConfigFbprThreshold(t_Handle h_Bm, uint32_t threshold)
{
    t_Bm        *p_Bm = (t_Bm *)h_Bm;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Bm->p_BmDriverParams, E_INVALID_HANDLE);

    p_Bm->p_BmDriverParams->fbprThreshold = threshold;

    return E_OK;
}

void BM_ErrorIsr(t_Handle h_Bm)
{
    t_Bm        *p_Bm = (t_Bm *)h_Bm;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN(p_Bm, E_INVALID_HANDLE);

    if (p_Bm->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(WARNING, E_INVALID_OPERATION, ("Master Only"));
        return;
    }

    tmpReg = GET_UINT32(p_Bm->p_BmRegs->err_isr);
    tmpReg &= GET_UINT32(p_Bm->p_BmRegs->err_ier);
    WRITE_UINT32(p_Bm->p_BmRegs->err_isr, tmpReg);

    if (tmpReg & BM_EX_INVALID_COMMAND)
        p_Bm->f_Exception(p_Bm->h_App, e_BM_EX_INVALID_COMMAND);
    if (tmpReg & BM_EX_FBPR_THRESHOLD)
        p_Bm->f_Exception(p_Bm->h_App, e_BM_EX_FBPR_THRESHOLD);
    if (tmpReg & BM_EX_MULTI_ECC)
        p_Bm->f_Exception(p_Bm->h_App, e_BM_EX_MULTI_ECC);
    if (tmpReg & BM_EX_SINGLE_ECC)
        p_Bm->f_Exception(p_Bm->h_App, e_BM_EX_SINGLE_ECC);
}

uint32_t BM_GetCounter(t_Handle h_Bm, e_BmCounters counter)
{
    t_Bm    *p_Bm = (t_Bm*)h_Bm;
 
    SANITY_CHECK_RETURN_VALUE(p_Bm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Bm->p_BmDriverParams, E_INVALID_STATE, 0);

    switch(counter)
    {
        case(e_BM_COUNTERS_FBPR):
            return BmGetCounter(p_Bm, e_BM_IM_COUNTERS_FBPR, 0);
        default:
            break;
    }
    /* should never get here */
    ASSERT_COND(FALSE);

    return 0;
}

t_Error BM_SetException(t_Handle h_Bm, e_BmExceptions exception, bool enable)
{
    t_Bm                *p_Bm = (t_Bm*)h_Bm;
    uint32_t            tmpReg, bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);

    if (p_Bm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(WARNING, E_INVALID_OPERATION, ("Master Only"));

    BM_ConfigException(p_Bm, exception, enable);

    tmpReg = GET_UINT32(p_Bm->p_BmRegs->err_ier);

    if(enable)
        tmpReg |= bitMask;
    else
        tmpReg &= ~bitMask;
    WRITE_UINT32(p_Bm->p_BmRegs->err_ier, tmpReg);

    return E_OK;
}

t_Error BM_GetRevision(t_Handle h_Bm, t_BmRevisionInfo *p_BmRevisionInfo)
{
    t_Bm        *p_Bm = (t_Bm*)h_Bm;

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmRevisionInfo, E_NULL_POINTER);

    return BmGetRevision(p_Bm, p_BmRevisionInfo);
}

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
t_Error BM_DumpRegs(t_Handle h_Bm)
{
    t_Bm    *p_Bm = (t_Bm *)h_Bm;

    DECLARE_DUMP;

    if (p_Bm->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(WARNING, E_INVALID_OPERATION, ("Master Only"));

    SANITY_CHECK_RETURN_ERROR(p_Bm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Bm->p_BmDriverParams, E_INVALID_STATE);

    DUMP_SUBTITLE(("\n"));

    DUMP_TITLE(p_Bm->p_BmRegs, ("BmRegs Regs"));

    DUMP_ARR(p_Bm->p_BmRegs, swdet);
    DUMP_ARR(p_Bm->p_BmRegs, hwdet);
    DUMP_ARR(p_Bm->p_BmRegs, swdxt);
    DUMP_ARR(p_Bm->p_BmRegs, hwdxt);
    DUMP_ARR(p_Bm->p_BmRegs, sdcnt);
    DUMP_ARR(p_Bm->p_BmRegs, hdcnt);
    DUMP_ARR(p_Bm->p_BmRegs, content);
    DUMP_ARR(p_Bm->p_BmRegs, hdptr);

    DUMP_VAR(p_Bm->p_BmRegs,fbpr_fpc);
    DUMP_VAR(p_Bm->p_BmRegs,fbpr_fp_lwit);

    DUMP_ARR(p_Bm->p_BmRegs, cmd_pm_cfg);
    DUMP_ARR(p_Bm->p_BmRegs, fl_pm_cfg);
    DUMP_VAR(p_Bm->p_BmRegs, ecsr);
    DUMP_VAR(p_Bm->p_BmRegs, ecir);
    DUMP_VAR(p_Bm->p_BmRegs, eadr);
    DUMP_ARR(p_Bm->p_BmRegs, edata);
    DUMP_VAR(p_Bm->p_BmRegs,sbet);
    DUMP_VAR(p_Bm->p_BmRegs,efcr);
    DUMP_VAR(p_Bm->p_BmRegs,efar);
    DUMP_VAR(p_Bm->p_BmRegs,sbec0);
    DUMP_VAR(p_Bm->p_BmRegs,sbec1);
    DUMP_VAR(p_Bm->p_BmRegs,ip_rev_1);
    DUMP_VAR(p_Bm->p_BmRegs,ip_rev_2);
    DUMP_VAR(p_Bm->p_BmRegs,fbpr_bare);
    DUMP_VAR(p_Bm->p_BmRegs,fbpr_bar);
    DUMP_VAR(p_Bm->p_BmRegs,fbpr_ar);
    DUMP_VAR(p_Bm->p_BmRegs,srcidr);
    DUMP_VAR(p_Bm->p_BmRegs,liodnr);
    DUMP_VAR(p_Bm->p_BmRegs,err_isr);
    DUMP_VAR(p_Bm->p_BmRegs,err_ier);
    DUMP_VAR(p_Bm->p_BmRegs,err_isdr);
    DUMP_VAR(p_Bm->p_BmRegs,err_iir);
    DUMP_VAR(p_Bm->p_BmRegs,err_ifr);

    return E_OK;
}
#endif /* (defined(DEBUG_ERRORS) && ... */
