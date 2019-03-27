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
 @File          qm.c

 @Description   QM & Portal implementation
*//***************************************************************************/

#include <sys/cdefs.h>
#include <sys/types.h>
#include <machine/atomic.h>
#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "mm_ext.h"
#include "core_ext.h"
#include "debug_ext.h"

#include "qm.h"


static volatile bool blockingFlag = FALSE;
static void QmIpcMsgCompletionCB(t_Handle   h_Module,
                                 uint8_t    *p_Msg,
                                 uint8_t    *p_Reply,
                                 uint32_t   replyLength,
                                 t_Error    status)
{
    SANITY_CHECK_RETURN(h_Module, E_INVALID_HANDLE);

    UNUSED(p_Msg);UNUSED(p_Reply);UNUSED(replyLength);UNUSED(status);UNUSED(h_Module);
    blockingFlag = FALSE;
}

static t_Error QmHandleIpcMsgCB(t_Handle  h_Qm,
                                uint8_t   *p_Msg,
                                uint32_t  msgLength,
                                uint8_t   *p_Reply,
                                uint32_t  *p_ReplyLength)
{
    t_Qm            *p_Qm           = (t_Qm*)h_Qm;
    t_QmIpcMsg      *p_IpcMsg       = (t_QmIpcMsg*)p_Msg;
    t_QmIpcReply    *p_IpcReply   = (t_QmIpcReply *)p_Reply;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((msgLength >= sizeof(uint32_t)), E_INVALID_VALUE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(msgLength);
#endif /* DISABLE_SANITY_CHECKS */

    ASSERT_COND(p_IpcMsg);

    memset(p_IpcReply, 0, (sizeof(uint8_t) * QM_IPC_MAX_REPLY_SIZE));
    *p_ReplyLength = 0;

    switch(p_IpcMsg->msgId)
    {
        case (QM_MASTER_IS_ALIVE):
            *(uint8_t*)p_IpcReply->replyBody = 1;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case (QM_FORCE_FQID):
        {
            t_QmIpcFqidParams   ipcFqid;
            uint32_t            fqid;

            memcpy((uint8_t*)&ipcFqid, p_IpcMsg->msgBody, sizeof(t_QmIpcFqidParams));
            fqid = QmFqidGet(p_Qm, ipcFqid.size, 1, TRUE, ipcFqid.fqid);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&fqid, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (QM_PUT_FQID):
        {
            t_Error             err;
            t_QmIpcFqidParams   ipcFqid;

            memcpy((uint8_t*)&ipcFqid, p_IpcMsg->msgBody, sizeof(t_QmIpcFqidParams));
            if ((err = QmFqidPut(p_Qm, ipcFqid.fqid)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
        case (QM_GET_COUNTER):
        {
            t_QmIpcGetCounter   ipcCounter;
            uint32_t            count;

            memcpy((uint8_t*)&ipcCounter, p_IpcMsg->msgBody, sizeof(t_QmIpcGetCounter));
            count = QmGetCounter(p_Qm, (e_QmInterModuleCounters)ipcCounter.enumId);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&count, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (QM_GET_SET_PORTAL_PARAMS):
        {
            t_Error                         err;
            t_QmIpcPortalInitParams         ipcPortalInitParams;
            t_QmInterModulePortalInitParams portalInitParams;

            memcpy((uint8_t*)&ipcPortalInitParams, p_IpcMsg->msgBody, sizeof(t_QmIpcPortalInitParams));
            portalInitParams.portalId       = ipcPortalInitParams.portalId;
            portalInitParams.stashDestQueue = ipcPortalInitParams.stashDestQueue;
            portalInitParams.liodn          = ipcPortalInitParams.liodn;
            portalInitParams.dqrrLiodn      = ipcPortalInitParams.dqrrLiodn;
            portalInitParams.fdFqLiodn      = ipcPortalInitParams.fdFqLiodn;
            if ((err = QmGetSetPortalParams(p_Qm, &portalInitParams)) != E_OK)
                REPORT_ERROR(MINOR, err, NO_MSG);
            break;
        }
        case (QM_GET_REVISION):
        {
            t_QmRevisionInfo    revInfo;
            t_QmIpcRevisionInfo ipcRevInfo;

            p_IpcReply->error = (uint32_t)QmGetRevision(h_Qm, &revInfo);
            ipcRevInfo.majorRev = revInfo.majorRev;
            ipcRevInfo.minorRev = revInfo.minorRev;
            memcpy(p_IpcReply->replyBody, (uint8_t*)&ipcRevInfo, sizeof(t_QmIpcRevisionInfo));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(t_QmIpcRevisionInfo);
            break;
        }
        default:
            *p_ReplyLength = 0;
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("command not found!!!"));
    }
    return E_OK;
}

static t_Error CheckQmParameters(t_Qm *p_Qm)
{
    if ((p_Qm->p_QmDriverParams->partFqidBase + p_Qm->p_QmDriverParams->partNumOfFqids) > QM_MAX_NUM_OF_FQIDS)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("partFqidBase+partNumOfFqids out of range!!!"));
    if ((p_Qm->partCgsBase + p_Qm->partNumOfCgs) > QM_MAX_NUM_OF_CGS)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("partCgsBase+partNumOfCgs out of range!!!"));

    if (p_Qm->guestId == NCSW_MASTER_ID)
    {
        uint64_t            phyAddr;

        phyAddr = XX_VirtToPhys(UINT_TO_PTR(p_Qm->p_QmDriverParams->swPortalsBaseAddress));

        if (phyAddr & 0x00000000001fffffLL)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("swPortalsBaseAddress isn't properly aligned"));
        if (!p_Qm->p_QmDriverParams->rtFramesDepth)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("rtFramesDepth must be larger than '0'!!!"));
        if (p_Qm->p_QmDriverParams->rtFramesDepth > ((16*MEGABYTE)*3))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("rtFramesDepth must be equal or smaller than 48MB!!!"));
        if (!p_Qm->p_QmDriverParams->totalNumOfFqids)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalNumOfFqids must be larger than '0'!!!"));
        if (p_Qm->p_QmDriverParams->totalNumOfFqids > (16*MEGABYTE))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("totalNumOfFqids must be equal or smaller than 16MB!!!"));
        if(!p_Qm->f_Exception)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));
    }

    return E_OK;
}

static t_Error QmInitPfdr(t_Qm *p_Qm, uint32_t pfdr_start, uint32_t num)
{
    uint8_t     rslt;
    uint32_t    timeout = 100000;

    ASSERT_COND(p_Qm);

    ASSERT_COND(pfdr_start && !(pfdr_start & 7) && !(num & 7) && num);

    /* Make sure te command interface is 'idle' */
    rslt = MCR_get_rslt(GET_UINT32(p_Qm->p_QmRegs->mcr));
    if (!MCR_rslt_idle(rslt))
        RETURN_ERROR(CRITICAL,E_INVALID_STATE,("QMAN_MCR isn't idle"));

    /* Write the MCR command params then the verb */
    WRITE_UINT32(p_Qm->p_QmRegs->mcp0, pfdr_start);
    /* TODO: remove this - it's a workaround for a model bug that is
     * corrected in more recent versions. We use the workaround until
     * everyone has upgraded. */
    WRITE_UINT32(p_Qm->p_QmRegs->mcp1, (pfdr_start + num - 16));
    WRITE_UINT32(p_Qm->p_QmRegs->mcp1, (pfdr_start + num - 1));

    mb();
    WRITE_UINT32(p_Qm->p_QmRegs->mcr, MCR_INIT_PFDR);

    /* Poll for the result */
    do {
        XX_UDelay(1);
        rslt = MCR_get_rslt(GET_UINT32(p_Qm->p_QmRegs->mcr));
    } while(!MCR_rslt_idle(rslt) && --timeout);

    if (MCR_rslt_ok(rslt))
        return E_OK;
    WRITE_UINT32(p_Qm->p_QmRegs->mcr, 0);
    if (!timeout)
        RETURN_ERROR(MAJOR, E_TIMEOUT, NO_MSG);
    if (MCR_rslt_eaccess(rslt))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
    if (MCR_rslt_inval(rslt))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Unexpected result from MCR_INIT_PFDR: %02x\n", rslt));
}

static __inline__ void QmSetWqScheduling(t_Qm *p_Qm,
                                         e_QmWqClass wqClass,
                                         uint8_t csElev,
                                         uint8_t csw2,
                                         uint8_t csw3,
                                         uint8_t csw4,
                                         uint8_t csw5,
                                         uint8_t csw6,
                                         uint8_t csw7)
{
    ASSERT_COND(p_Qm);

    WRITE_UINT32(p_Qm->p_QmRegs->wq_cs_cfg[wqClass],
                 (uint32_t)(((csElev & 0xff) << 24) |
                 ((csw2 & 0x7) << 20) |
                 ((csw3 & 0x7) << 16) |
                 ((csw4 & 0x7) << 12) |
                 ((csw5 & 0x7) << 8) |
                 ((csw6 & 0x7) << 4) |
                 (csw7 & 0x7)));
}

static uint32_t ReserveFqids(t_Qm *p_Qm, uint32_t size, uint32_t alignment, bool force, uint32_t base)
{
    uint64_t    ans;
    uint32_t    intFlags;

    intFlags = XX_LockIntrSpinlock(p_Qm->lock);
    if (force)
        ans = MM_GetForce(p_Qm->h_FqidMm,
                          (uint64_t)base,
                          (uint64_t)size,
                          "QM FQID MEM");
    else
        ans = MM_Get(p_Qm->h_FqidMm,
                     (uint64_t)size,
                     alignment,
                     "QM FQID MEM");
    if (ans == ILLEGAL_BASE)
    {
        XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
        return (uint32_t)ans;
    }
    base = (uint32_t)ans;
    ans = MM_GetForce(p_Qm->h_RsrvFqidMm,
                      (uint64_t)base,
                      (uint64_t)size,
                      "QM rsrv FQID MEM");
    if (ans == ILLEGAL_BASE)
    {
        MM_Put(p_Qm->h_FqidMm, (uint64_t)base);
        XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
        return (uint32_t)ans;
    }
    XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);

    return (uint32_t)base;
}

static void FreeInitResources(t_Qm *p_Qm)
{
    if (p_Qm->p_FqdBase)
        XX_FreeSmart(p_Qm->p_FqdBase);
    if (p_Qm->p_PfdrBase)
        XX_FreeSmart(p_Qm->p_PfdrBase);
    if (p_Qm->h_Session)
        XX_IpcFreeSession(p_Qm->h_Session);
    if (p_Qm->h_RsrvFqidMm)
        MM_Free(p_Qm->h_RsrvFqidMm);
    if (p_Qm->h_FqidMm)
        MM_Free(p_Qm->h_FqidMm);
}


/****************************************/
/*       Inter-Module functions         */
/****************************************/

uint32_t QmGetCounter(t_Handle h_Qm, e_QmInterModuleCounters counter)
{
    t_Qm *p_Qm = (t_Qm*)h_Qm;

    SANITY_CHECK_RETURN_VALUE(p_Qm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE((((p_Qm->guestId == NCSW_MASTER_ID) && p_Qm->p_QmRegs) ||
                               (p_Qm->guestId != NCSW_MASTER_ID)), E_INVALID_STATE, 0);

    if ((p_Qm->guestId == NCSW_MASTER_ID) ||
        (!p_Qm->h_Session && p_Qm->p_QmRegs))
    {
        switch(counter)
        {
            case(e_QM_IM_COUNTERS_SFDR_IN_USE):
                return GET_UINT32(p_Qm->p_QmRegs->sfdr_in_use);
            case(e_QM_IM_COUNTERS_PFDR_IN_USE):
                return (p_Qm->numOfPfdr - GET_UINT32(p_Qm->p_QmRegs->pfdr_fpc));
            case(e_QM_IM_COUNTERS_PFDR_FREE_POOL):
                return (GET_UINT32(p_Qm->p_QmRegs->pfdr_fpc) - GET_UINT32(p_Qm->p_QmRegs->pfdr_cfg));
            default:
                break;
        }
        /* should never get here */
        ASSERT_COND(FALSE);
    }
    else if (p_Qm->h_Session)
    {
        t_QmIpcMsg              msg;
        t_QmIpcReply            reply;
        t_QmIpcGetCounter       ipcCounter;
        uint32_t                replyLength, count;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_QmIpcMsg));
        memset(&reply, 0, sizeof(t_QmIpcReply));
        ipcCounter.enumId       = (uint32_t)counter;
        msg.msgId               = QM_GET_COUNTER;
        memcpy(msg.msgBody, &ipcCounter, sizeof(t_QmIpcGetCounter));
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        if ((errCode = XX_IpcSendMessage(p_Qm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_QmIpcGetCounter),
                                         (uint8_t*)&reply,
                                         &replyLength,
                                         NULL,
                                         NULL)) != E_OK)
            REPORT_ERROR(MAJOR, errCode, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        if ((errCode == E_OK) && (replyLength == (sizeof(uint32_t) + sizeof(uint32_t))))
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

t_Error QmGetRevision(t_Handle h_Qm, t_QmRevisionInfo *p_QmRevisionInfo)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmRevisionInfo, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR((((p_Qm->guestId == NCSW_MASTER_ID) && p_Qm->p_QmRegs) ||
                               (p_Qm->guestId != NCSW_MASTER_ID)), E_INVALID_STATE);

    if ((p_Qm->guestId == NCSW_MASTER_ID) ||
        (!p_Qm->h_Session && p_Qm->p_QmRegs))
    {
        /* read revision register 1 */
        tmpReg = GET_UINT32(p_Qm->p_QmRegs->ip_rev_1);
        p_QmRevisionInfo->majorRev = (uint8_t)((tmpReg & REV1_MAJOR_MASK) >> REV1_MAJOR_SHIFT);
        p_QmRevisionInfo->minorRev = (uint8_t)((tmpReg & REV1_MINOR_MASK) >> REV1_MINOR_SHIFT);
    }
    else if (p_Qm->h_Session)
    {
        t_QmIpcMsg          msg;
        t_QmIpcReply        reply;
        t_QmIpcRevisionInfo ipcRevInfo;
        uint32_t            replyLength;
        t_Error             errCode = E_OK;

        memset(&msg, 0, sizeof(t_QmIpcMsg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId           = QM_GET_REVISION;
        replyLength = sizeof(uint32_t) + sizeof(t_QmIpcRevisionInfo);
        if ((errCode = XX_IpcSendMessage(p_Qm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId),
                                         (uint8_t*)&reply,
                                         &replyLength,
                                         NULL,
                                         NULL)) != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        if (replyLength != (sizeof(uint32_t) + sizeof(t_QmIpcRevisionInfo)))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        memcpy((uint8_t*)&ipcRevInfo, reply.replyBody, sizeof(t_QmIpcRevisionInfo));
        p_QmRevisionInfo->majorRev = ipcRevInfo.majorRev;
        p_QmRevisionInfo->minorRev = ipcRevInfo.minorRev;

        return (t_Error)(reply.error);
    }
    else
        RETURN_ERROR(WARNING, E_NOT_SUPPORTED,
                     ("In 'guest', either IPC or 'baseAddress' is required!"));

    return E_OK;
}

t_Error QmGetSetPortalParams(t_Handle h_Qm, t_QmInterModulePortalInitParams *p_PortalParams)
{
    t_Qm                *p_Qm = (t_Qm *)h_Qm;
    t_QmRevisionInfo    revInfo;
    uint32_t            lioReg,ioReg;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_PortalParams, E_NULL_POINTER);

    if (p_Qm->guestId == NCSW_MASTER_ID)
    {
        QmGetRevision(p_Qm, &revInfo);

        if ((revInfo.majorRev == 1) && (revInfo.minorRev == 0))
        {
            lioReg  = (uint32_t)(p_PortalParams->stashDestQueue << 24) |
                      (p_PortalParams->liodn << 16) |
                      (p_PortalParams->dqrrLiodn);
            ioReg   = (p_PortalParams->fdFqLiodn);
        }
        else
        {
            lioReg  = (uint32_t)(p_PortalParams->liodn << 16) |
                      (p_PortalParams->dqrrLiodn);
            ioReg   = (uint32_t)(p_PortalParams->stashDestQueue << 16) |
                      (p_PortalParams->fdFqLiodn);
        }

        WRITE_UINT32(p_Qm->p_QmRegs->swpConfRegs[p_PortalParams->portalId].lio_cfg, lioReg);
        WRITE_UINT32(p_Qm->p_QmRegs->swpConfRegs[p_PortalParams->portalId].io_cfg, ioReg);
    }
    else if (p_Qm->h_Session)
    {
        t_QmIpcMsg                  msg;
        t_QmIpcPortalInitParams     portalParams;
        t_Error                     errCode;

        memset(&msg, 0, sizeof(t_QmIpcMsg));
        portalParams.portalId       = p_PortalParams->portalId;
        portalParams.stashDestQueue = p_PortalParams->stashDestQueue;
        portalParams.liodn          = p_PortalParams->liodn;
        portalParams.dqrrLiodn      = p_PortalParams->dqrrLiodn;
        portalParams.fdFqLiodn      = p_PortalParams->fdFqLiodn;
        msg.msgId           = QM_GET_SET_PORTAL_PARAMS;
        memcpy(msg.msgBody, &portalParams, sizeof(t_QmIpcPortalInitParams));
        XX_LockSpinlock(p_Qm->lock);
        if ((errCode = XX_IpcSendMessage(p_Qm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_QmIpcPortalInitParams),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL)) != E_OK)
        {
            XX_UnlockSpinlock(p_Qm->lock);
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        }
        XX_UnlockSpinlock(p_Qm->lock);
    }
    else
        DBG(WARNING, ("Can't set portal parameters (e.g. liodns). " \
                      "probably QM is running in guest-mode with no IPC!"));

    return E_OK;
}

uint32_t QmFqidGet(t_Qm *p_Qm, uint32_t size, uint32_t alignment, bool force, uint32_t base)
{
    uint64_t    ans;
    uint32_t    intFlags;

    intFlags = XX_LockIntrSpinlock(p_Qm->lock);
    if (force)
    {
        ans = MM_GetForce(p_Qm->h_FqidMm,
                          (uint64_t)base,
                          (uint64_t)size,
                          "QM FQID MEM");
        if (ans == ILLEGAL_BASE)
        {
            ans = MM_GetForce(p_Qm->h_RsrvFqidMm,
                              (uint64_t)base,
                              (uint64_t)size,
                              "QM rsrv FQID MEM");
            if (ans == ILLEGAL_BASE)
                ans = base;
            else if (p_Qm->h_Session)
            {
                t_QmIpcMsg              msg;
                t_QmIpcReply            reply;
                uint32_t                replyLength;
                t_QmIpcFqidParams       ipcFqid;
                t_Error                 errCode = E_OK;

                memset(&msg, 0, sizeof(t_QmIpcMsg));
                memset(&reply, 0, sizeof(t_QmIpcReply));
                ipcFqid.fqid        = base;
                ipcFqid.size        = size;
                msg.msgId           = QM_FORCE_FQID;
                memcpy(msg.msgBody, &ipcFqid, sizeof(t_QmIpcFqidParams));
                replyLength = sizeof(uint32_t) + sizeof(uint32_t);
                if ((errCode = XX_IpcSendMessage(p_Qm->h_Session,
                                                 (uint8_t*)&msg,
                                                 sizeof(msg.msgId) + sizeof(t_QmIpcFqidParams),
                                                 (uint8_t*)&reply,
                                                 &replyLength,
                                                 NULL,
                                                 NULL)) != E_OK)
                    REPORT_ERROR(MAJOR, errCode, NO_MSG);
                if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
                   REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

                if ((errCode != E_OK) ||
                    (replyLength != (sizeof(uint32_t) + sizeof(uint32_t))))
                    ans = ILLEGAL_BASE;
                else
                    memcpy((uint8_t*)&ans, reply.replyBody, sizeof(uint32_t));
            }
            else
            {
                DBG(WARNING, ("No Ipc - can't validate fqid."));
                ans = base;
            }
        }
    }
    else
        ans = MM_Get(p_Qm->h_FqidMm,
                     size,
                     alignment,
                     "QM FQID MEM");
    XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);

    KASSERT(ans < UINT32_MAX, ("Oops, %lx > UINT32_MAX!\n", ans));
    return (uint32_t)ans;
}

t_Error QmFqidPut(t_Qm *p_Qm, uint32_t base)
{
    uint32_t    intFlags;

    intFlags = XX_LockIntrSpinlock(p_Qm->lock);
    /* Check maybe this fqid was reserved in the past */
    if (MM_GetForce(p_Qm->h_RsrvFqidMm,
                    (uint64_t)base,
                    (uint64_t)1,
                    "QM rsrv FQID MEM") == ILLEGAL_BASE)
    {
        XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
        return E_OK;
    }
    else
        MM_PutForce(p_Qm->h_RsrvFqidMm,
                    (uint64_t)base,
                    (uint64_t)1);
    if (MM_InRange(p_Qm->h_FqidMm, (uint64_t)base))
    {
        if (MM_Put(p_Qm->h_FqidMm, (uint64_t)base) != 0)
        {
            XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
            return E_OK;
        }
        else
        {
            XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
            return ERROR_CODE(E_NOT_FOUND);
        }
    }
    else if (p_Qm->h_Session)
    {
        t_QmIpcMsg              msg;
        t_QmIpcFqidParams       ipcFqid;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_QmIpcMsg));
        ipcFqid.fqid        = (uint8_t)base;
        ipcFqid.size        = 0;
        msg.msgId           = QM_PUT_FQID;
        memcpy(msg.msgBody, &ipcFqid, sizeof(t_QmIpcFqidParams));
        if ((errCode = XX_IpcSendMessage(p_Qm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_QmIpcFqidParams),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL)) != E_OK)
        {
            XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        }
    }
    else
        DBG(WARNING, ("No Ipc - can't validate fqid."));
    XX_UnlockIntrSpinlock(p_Qm->lock, intFlags);

    return E_OK;
}

t_Error QmGetCgId(t_Handle h_Qm, uint8_t *p_CgId)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;
    uint16_t    i;

    for(i = p_Qm->partCgsBase;i<p_Qm->partCgsBase+p_Qm->partNumOfCgs;i++)
        if (!p_Qm->cgsUsed[i])
        {
            p_Qm->cgsUsed[i] = (uint8_t)TRUE;
            *p_CgId = (uint8_t)i;
            break;
        }
    if(i == (p_Qm->partCgsBase+p_Qm->partNumOfCgs))
        RETURN_ERROR(MINOR, E_BUSY, ("No available CG"));
    else
        return E_OK;
}

t_Error QmFreeCgId(t_Handle h_Qm, uint8_t cgId)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;

    if (!p_Qm->cgsUsed[cgId])
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("CG is not in use"));
    else
        p_Qm->cgsUsed[cgId] = (uint8_t)FALSE;

    return E_OK;
}


/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle QM_Config(t_QmParam *p_QmParam)
{
    t_Qm    *p_Qm;
    uint8_t i;

    SANITY_CHECK_RETURN_VALUE(p_QmParam, E_INVALID_HANDLE, NULL);

    p_Qm = (t_Qm *)XX_Malloc(sizeof(t_Qm));
    if (!p_Qm)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("QM obj!!!"));
        return NULL;
    }
    memset(p_Qm, 0, sizeof(t_Qm));
    p_Qm->p_QmDriverParams = (t_QmDriverParams *)XX_Malloc(sizeof(t_QmDriverParams));
    if (!p_Qm->p_QmDriverParams)
    {
        XX_Free(p_Qm);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Qm driver parameters"));
        return NULL;
    }
    memset(p_Qm->p_QmDriverParams, 0, sizeof(t_QmDriverParams));

    p_Qm->guestId                           = p_QmParam->guestId;
    p_Qm->p_QmDriverParams->partFqidBase    = p_QmParam->partFqidBase;
    p_Qm->p_QmDriverParams->partNumOfFqids  = p_QmParam->partNumOfFqids;
    p_Qm->partCgsBase                       = p_QmParam->partCgsBase;
    p_Qm->partNumOfCgs                      = p_QmParam->partNumOfCgs;
    p_Qm->p_QmRegs                          = (t_QmRegs *)UINT_TO_PTR(p_QmParam->baseAddress);

    if (p_Qm->guestId == NCSW_MASTER_ID)
    {
        p_Qm->exceptions        = DEFAULT_exceptions;
        p_Qm->f_Exception       = p_QmParam->f_Exception;
        p_Qm->h_App             = p_QmParam->h_App;
        p_Qm->errIrq            = p_QmParam->errIrq;
        p_Qm->p_QmDriverParams->liodn                   = p_QmParam->liodn;
        p_Qm->p_QmDriverParams->rtFramesDepth           = DEFAULT_rtFramesDepth;
        p_Qm->p_QmDriverParams->fqdMemPartitionId       = p_QmParam->fqdMemPartitionId;
        p_Qm->p_QmDriverParams->pfdrMemPartitionId      = p_QmParam->pfdrMemPartitionId;
        p_Qm->p_QmDriverParams->swPortalsBaseAddress    = p_QmParam->swPortalsBaseAddress;
        p_Qm->p_QmDriverParams->totalNumOfFqids         = p_QmParam->totalNumOfFqids;
        p_Qm->p_QmDriverParams->pfdrThreshold           = DEFAULT_pfdrThreshold;
        p_Qm->p_QmDriverParams->sfdrThreshold           = DEFAULT_sfdrThreshold;
        p_Qm->p_QmDriverParams->pfdrBaseConstant        = DEFAULT_pfdrBaseConstant;
        for(i= 0;i<DPAA_MAX_NUM_OF_DC_PORTALS;i++)
            p_Qm->p_QmDriverParams->dcPortalsParams[i].sendToSw =
                (bool)((i < e_DPAA_DCPORTAL2) ? FALSE : TRUE);

#ifdef QMAN_SFDR_LEAK_ERRATA_QMAN5
        {
#define WORKAROUND_TMP_VAL  0x00000003
        t_QmRevisionInfo revInfo;
        QmGetRevision(p_Qm, &revInfo);
        if ((revInfo.majorRev == 1) && (revInfo.minorRev == 0))
        {
            uint32_t *tmp = (uint32_t *)UINT_TO_PTR(p_QmParam->baseAddress + 0xbf0);
            uint32_t tmpReg = WORKAROUND_TMP_VAL;
            WRITE_UINT32(*tmp, tmpReg);
            while ((tmpReg = GET_UINT32(*tmp)) != WORKAROUND_TMP_VAL) ;
        }
        }
#endif /* QMAN_SFDR_LEAK_ERRATA_QMAN5 */
    }

    /* build the QM partition IPC address */
    memset(p_Qm->moduleName, 0, MODULE_NAME_SIZE);
    if(Sprint (p_Qm->moduleName, "QM_0_%d",p_Qm->guestId) != (p_Qm->guestId<10 ? 6:7))
    {
        XX_Free(p_Qm->p_QmDriverParams);
        XX_Free(p_Qm);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        return NULL;
    }

    return p_Qm;
}

t_Error QM_Init(t_Handle h_Qm)
{
    t_Qm                *p_Qm = (t_Qm *)h_Qm;
    t_QmDriverParams    *p_QmDriverParams;
    t_Error             err;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    CHECK_INIT_PARAMETERS(p_Qm, CheckQmParameters);

    p_QmDriverParams        = p_Qm->p_QmDriverParams;

    if (p_QmDriverParams->partNumOfFqids)
    {
        if (MM_Init(&p_Qm->h_FqidMm, p_QmDriverParams->partFqidBase, p_QmDriverParams->partNumOfFqids) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("QM-FQIDS-MEM partition!!!"));
        if (MM_Init(&p_Qm->h_RsrvFqidMm, p_QmDriverParams->partFqidBase, p_QmDriverParams->partNumOfFqids) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("QM-Reserve-FQIDS-MEM partition!!!"));
    }

    if (p_Qm->guestId == NCSW_MASTER_ID)
    {
        uint64_t            phyAddr;
        t_QmRevisionInfo    revInfo;
        uint32_t            dsSize, exp, i;

        QmGetRevision(p_Qm, &revInfo);
        DBG(TRACE, ("Qman ver:%02x,%02x", revInfo.majorRev, revInfo.minorRev));

        phyAddr = XX_VirtToPhys(UINT_TO_PTR(p_QmDriverParams->swPortalsBaseAddress));
        WRITE_UINT32(p_Qm->p_QmRegs->qcsp_bare, ((uint32_t)(phyAddr >> 32) & 0x000000ff));
        WRITE_UINT32(p_Qm->p_QmRegs->qcsp_bar, (uint32_t)phyAddr);
        WRITE_UINT32(p_Qm->p_QmRegs->liodnr, (uint16_t)p_QmDriverParams->liodn);

        /* FQD memory */
        dsSize = (uint32_t)(p_QmDriverParams->totalNumOfFqids * FQD_ENTRY_SIZE);
        LOG2(dsSize, exp);
        if (!POWER_OF_2(dsSize)) (exp++);
        dsSize = (uint32_t)(1 << exp);
        if (dsSize < (4*KILOBYTE))
        {
            dsSize = (4*KILOBYTE);
            LOG2(dsSize, exp);
        }
        p_Qm->p_FqdBase = XX_MallocSmart(dsSize, (int)p_QmDriverParams->fqdMemPartitionId, dsSize);
        if (!p_Qm->p_FqdBase)
        {
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FQD obj!!!"));
        }
        memset(p_Qm->p_FqdBase, 0, dsSize);
	mb();
        for (i=0; i<dsSize; i+=64)
            dcbf(PTR_MOVE(p_Qm->p_FqdBase, i));
	mb();

        phyAddr = XX_VirtToPhys(p_Qm->p_FqdBase);
        WRITE_UINT32(p_Qm->p_QmRegs->fqd_bare, ((uint32_t)(phyAddr >> 32) & 0x000000ff));
        WRITE_UINT32(p_Qm->p_QmRegs->fqd_bar, (uint32_t)phyAddr);
        WRITE_UINT32(p_Qm->p_QmRegs->fqd_ar, AR_ENABLE | (exp - 1));

        /* PFDR memory */
        dsSize = (uint32_t)(p_QmDriverParams->rtFramesDepth * (PFDR_ENTRY_SIZE/3));
        LOG2(dsSize, exp);
        if (!POWER_OF_2(dsSize)) (exp++);
        dsSize = (uint32_t)(1 << exp);
        if (dsSize < (4*KILOBYTE))
        {
            dsSize = (4*KILOBYTE);
            LOG2(dsSize, exp);
        }

        p_Qm->p_PfdrBase = XX_MallocSmart(dsSize, (int)p_QmDriverParams->pfdrMemPartitionId, dsSize);
        if (!p_Qm->p_PfdrBase)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("PFDR obj!!!"));

        phyAddr = XX_VirtToPhys(p_Qm->p_PfdrBase);
        WRITE_UINT32(p_Qm->p_QmRegs->pfdr_bare, ((uint32_t)(phyAddr >> 32) & 0x000000ff));
        WRITE_UINT32(p_Qm->p_QmRegs->pfdr_bar, (uint32_t)phyAddr);
        WRITE_UINT32(p_Qm->p_QmRegs->pfdr_ar, AR_ENABLE | (exp - 1));

        if (QmInitPfdr(p_Qm, 8, dsSize / 64 - 8) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("PFDR init failed!!!"));

        /* thresholds */
        WRITE_UINT32(p_Qm->p_QmRegs->pfdr_fp_lwit, (p_Qm->p_QmDriverParams->pfdrThreshold & 0xffffff));
        WRITE_UINT32(p_Qm->p_QmRegs->pfdr_cfg, p_Qm->p_QmDriverParams->pfdrBaseConstant);
        WRITE_UINT32(p_Qm->p_QmRegs->sfdr_cfg, (p_Qm->p_QmDriverParams->sfdrThreshold & 0x3ff));

        p_Qm->numOfPfdr = GET_UINT32(p_Qm->p_QmRegs->pfdr_fpc);

        /* corenet initiator settings */
        WRITE_UINT32(p_Qm->p_QmRegs->ci_sched_cfg,
                     (CI_SCHED_CFG_EN |
                     (DEFAULT_initiatorSrcciv << CI_SCHED_CFG_SRCCIV_SHIFT) |
                     (DEFAULT_initiatorSrqW << CI_SCHED_CFG_SRQ_W_SHIFT) |
                     (DEFAULT_initiatorRwW << CI_SCHED_CFG_RW_W_SHIFT) |
                     (DEFAULT_initiatorBmanW << CI_SCHED_CFG_BMAN_W_SHIFT)));

        /* HID settings */
        if ((revInfo.majorRev == 1) && (revInfo.minorRev == 0))
            /* offset 0x0bf0 */
            WRITE_UINT32(p_Qm->p_QmRegs->res23[144], 0x3);
        else
            WRITE_UINT32(p_Qm->p_QmRegs->res23[144], 0x0);

        for(i=0;i<DPAA_MAX_NUM_OF_DC_PORTALS;i++)
        {
            if(p_Qm->p_QmDriverParams->dcPortalsParams[i].sendToSw)
                WRITE_UINT32(p_Qm->p_QmRegs->dcpConfRegs[i].cfg,
                    p_Qm->p_QmDriverParams->dcPortalsParams[i].swPortalId);
            else
                WRITE_UINT32(p_Qm->p_QmRegs->dcpConfRegs[i].cfg, QM_DCP_CFG_ED);
        }

#ifdef QMAN_WQ_CS_CFG_ERRATA_QMAN4
        {
            t_QmRevisionInfo revInfo;
            QmGetRevision(p_Qm, &revInfo);
            if ((revInfo.majorRev == 1) && (revInfo.minorRev == 0))
            {
                QmSetWqScheduling(p_Qm, e_QM_WQ_SW_PORTALS,0,1,1,1,1,1,1);
                QmSetWqScheduling(p_Qm, e_QM_WQ_POOLS,0,1,1,1,1,1,1);
                QmSetWqScheduling(p_Qm, e_QM_WQ_DCP0,0,1,1,1,1,1,1);
                QmSetWqScheduling(p_Qm, e_QM_WQ_DCP1,0,1,1,1,1,1,1);
                QmSetWqScheduling(p_Qm, e_QM_WQ_DCP2,0,1,1,1,1,1,1);
                QmSetWqScheduling(p_Qm, e_QM_WQ_DCP3,0,1,1,1,1,1,1);
            }
        }
#endif /* QMAN_WQ_CS_CFG_ERRATA_QMAN4 */

        WRITE_UINT32(p_Qm->p_QmRegs->err_isr, p_Qm->exceptions);
        WRITE_UINT32(p_Qm->p_QmRegs->err_ier, p_Qm->exceptions);
        WRITE_UINT32(p_Qm->p_QmRegs->err_isdr, 0x0);
        if (p_Qm->errIrq != NO_IRQ)
        {
            XX_SetIntr(p_Qm->errIrq, QM_ErrorIsr, p_Qm);
            XX_EnableIntr(p_Qm->errIrq);
        }
        if ((err = XX_IpcRegisterMsgHandler(p_Qm->moduleName, QmHandleIpcMsgCB, p_Qm, QM_IPC_MAX_REPLY_SIZE)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }
    else /* guest mode */
    {
        char                    masterModuleName[MODULE_NAME_SIZE];

        memset(masterModuleName, 0, MODULE_NAME_SIZE);
        if(Sprint (masterModuleName, "QM_0_%d", NCSW_MASTER_ID) != (NCSW_MASTER_ID<10 ? 6:7))
        {
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        }

        p_Qm->h_Session     = XX_IpcInitSession(masterModuleName, p_Qm->moduleName);
        if (p_Qm->h_Session)
        {
            t_QmIpcMsg              msg;
            uint8_t                 isMasterAlive = 0;
            t_QmIpcReply            reply;
            uint32_t                replyLength;

            memset(&msg, 0, sizeof(t_QmIpcMsg));
            memset(&reply, 0, sizeof(t_QmIpcReply));
            msg.msgId   = QM_MASTER_IS_ALIVE;
            do
            {
                blockingFlag = TRUE;
                replyLength = sizeof(uint32_t) + sizeof(uint8_t);
                if ((err = XX_IpcSendMessage(p_Qm->h_Session,
                                             (uint8_t*)&msg,
                                             sizeof(msg.msgId),
                                             (uint8_t*)&reply,
                                             &replyLength,
                                             QmIpcMsgCompletionCB,
                                             p_Qm)) != E_OK)
                    REPORT_ERROR(MAJOR, err, NO_MSG);
                while(blockingFlag) ;
                if(replyLength != (sizeof(uint32_t) + sizeof(uint8_t)))
                    REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
                isMasterAlive = *(uint8_t*)(reply.replyBody);
            } while (!isMasterAlive);
        }
    }

    p_Qm->lock = XX_InitSpinlock();
    XX_Free(p_Qm->p_QmDriverParams);
    p_Qm->p_QmDriverParams = NULL;

    return E_OK;
}

t_Error QM_Free(t_Handle h_Qm)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);

    if (p_Qm->lock)
        XX_FreeSpinlock(p_Qm->lock);

    if (p_Qm->guestId == NCSW_MASTER_ID)
    {
        XX_IpcUnregisterMsgHandler(p_Qm->moduleName);
        if (p_Qm->errIrq  != NO_IRQ)
        {
            XX_DisableIntr(p_Qm->errIrq);
            XX_FreeIntr(p_Qm->errIrq);
        }
    }
    FreeInitResources(p_Qm);

    if (p_Qm->p_QmDriverParams)
        XX_Free(p_Qm->p_QmDriverParams);

    XX_Free(p_Qm);

    return E_OK;
}

t_Error QM_ConfigRTFramesDepth(t_Handle h_Qm, uint32_t rtFramesDepth)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    p_Qm->p_QmDriverParams->rtFramesDepth = rtFramesDepth;

    return E_OK;
}

t_Error QM_ConfigPfdrThreshold(t_Handle h_Qm, uint32_t threshold)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    p_Qm->p_QmDriverParams->pfdrThreshold = threshold;

    return E_OK;
}

t_Error QM_ConfigSfdrReservationThreshold(t_Handle h_Qm, uint32_t threshold)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    p_Qm->p_QmDriverParams->sfdrThreshold = threshold;

    return E_OK;
}


t_Error QM_ConfigErrorRejectionNotificationDest(t_Handle h_Qm, e_DpaaDcPortal id, t_QmDcPortalParams *p_Params)
{
    UNUSED(h_Qm); UNUSED(id); UNUSED(p_Params);

    RETURN_ERROR(INFO, E_NOT_SUPPORTED, ("Only default ERN destination available."));
}


t_Error QM_Poll(t_Handle h_Qm, e_QmPortalPollSource source)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;
    t_QmPortal  *p_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    p_QmPortal = QmGetPortalHandle(p_Qm);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);

    return QM_PORTAL_Poll(p_QmPortal, source);
}

uint32_t QM_GetCounter(t_Handle h_Qm, e_QmCounters counter)
{
    t_Qm    *p_Qm = (t_Qm *)h_Qm;

    SANITY_CHECK_RETURN_VALUE(p_Qm, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Qm->p_QmDriverParams, E_INVALID_STATE, 0);

    switch(counter)
    {
        case(e_QM_COUNTERS_SFDR_IN_USE):
            return QmGetCounter(p_Qm, e_QM_IM_COUNTERS_SFDR_IN_USE);
        case(e_QM_COUNTERS_PFDR_IN_USE):
            return QmGetCounter(p_Qm, e_QM_IM_COUNTERS_PFDR_IN_USE);
        case(e_QM_COUNTERS_PFDR_FREE_POOL):
            return QmGetCounter(p_Qm, e_QM_IM_COUNTERS_PFDR_FREE_POOL);
        default:
            break;
    }
    /* should never get here */
    ASSERT_COND(FALSE);

    return 0;
}

void QM_ErrorIsr(t_Handle h_Qm)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN(p_Qm, E_INVALID_HANDLE);

    if (p_Qm->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(WARNING, E_INVALID_OPERATION, ("Master Only"));
        return;
    }

    tmpReg = GET_UINT32(p_Qm->p_QmRegs->err_isr);
    tmpReg &= GET_UINT32(p_Qm->p_QmRegs->err_ier);
    WRITE_UINT32(p_Qm->p_QmRegs->err_isr, tmpReg);

    if (tmpReg & QM_EX_CORENET_INITIATOR_DATA)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_CORENET_INITIATOR_DATA);
    if (tmpReg & QM_EX_CORENET_TARGET_DATA)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_CORENET_TARGET_DATA);
    if (tmpReg & QM_EX_CORENET_INVALID_TARGET_TRANSACTION)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_CORENET_INVALID_TARGET_TRANSACTION);
    if (tmpReg & QM_EX_PFDR_THRESHOLD)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_PFDR_THRESHOLD);
    if (tmpReg & QM_EX_MULTI_ECC)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_MULTI_ECC);
    if (tmpReg & QM_EX_SINGLE_ECC)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_SINGLE_ECC);
    if (tmpReg & QM_EX_PFDR_ENQUEUE_BLOCKED)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_PFDR_ENQUEUE_BLOCKED);
    if (tmpReg & QM_EX_INVALID_COMMAND)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_INVALID_COMMAND);
    if (tmpReg & QM_EX_DEQUEUE_DCP)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_DEQUEUE_DCP);
    if (tmpReg & QM_EX_DEQUEUE_FQ)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_DEQUEUE_FQ);
    if (tmpReg & QM_EX_DEQUEUE_SOURCE)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_DEQUEUE_SOURCE);
    if (tmpReg & QM_EX_DEQUEUE_QUEUE)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_DEQUEUE_QUEUE);
    if (tmpReg & QM_EX_ENQUEUE_OVERFLOW)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_ENQUEUE_OVERFLOW);
    if (tmpReg & QM_EX_ENQUEUE_STATE)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_ENQUEUE_STATE);
    if (tmpReg & QM_EX_ENQUEUE_CHANNEL)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_ENQUEUE_CHANNEL);
    if (tmpReg & QM_EX_ENQUEUE_QUEUE)
        p_Qm->f_Exception(p_Qm->h_App, e_QM_EX_ENQUEUE_QUEUE);
}

t_Error QM_SetException(t_Handle h_Qm, e_QmExceptions exception, bool enable)
{
    t_Qm                *p_Qm = (t_Qm*)h_Qm;
    t_Error             err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    if ((err = SetException(p_Qm, exception, enable)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    WRITE_UINT32(p_Qm->p_QmRegs->err_ier, p_Qm->exceptions);

    return E_OK;
}

t_Error QM_GetRevision(t_Handle h_Qm, t_QmRevisionInfo *p_QmRevisionInfo)
{
    t_Qm        *p_Qm = (t_Qm*)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmRevisionInfo, E_NULL_POINTER);

    return QmGetRevision(p_Qm, p_QmRevisionInfo);
}

t_Error QM_ReserveQueues(t_Handle h_Qm, t_QmRsrvFqrParams *p_QmFqrParams, uint32_t  *p_BaseFqid)
{
    t_Qm                *p_Qm = (t_Qm*)h_Qm;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Qm->p_QmDriverParams, E_INVALID_HANDLE);

    *p_BaseFqid = ReserveFqids(p_Qm,
                               (uint32_t)((p_QmFqrParams->useForce && !p_QmFqrParams->numOfFqids) ?
                                          1 : p_QmFqrParams->numOfFqids),
                               p_QmFqrParams->qs.nonFrcQs.align,
                               p_QmFqrParams->useForce,
                               p_QmFqrParams->qs.frcQ.fqid);
    if (*p_BaseFqid == ILLEGAL_BASE)
        RETURN_ERROR(CRITICAL,E_INVALID_STATE,("can't allocate a fqid"));

    return E_OK;
}

t_Error QM_GetErrorInformation(t_Handle h_Qm, t_QmErrorInfo *p_errInfo)
{
    uint32_t            ecsr, ecir;
    t_Qm                *p_Qm = (t_Qm*)h_Qm;
    t_Error             err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Qm->p_QmDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_errInfo, E_NULL_POINTER);

    ecsr = GET_UINT32(p_Qm->p_QmRegs->ecsr);
    ecir = GET_UINT32(p_Qm->p_QmRegs->ecir);
    if ((ecsr & QM_EX_MULTI_ECC) ||
        (ecsr & QM_EX_SINGLE_ECC))
    {
        err = E_NOT_SUPPORTED;
        REPORT_ERROR(INFO, E_NOT_SUPPORTED, ("single and multi ecc, use QM_DumpRegs"));
    }
    if ((ecsr & QM_EX_ENQUEUE_QUEUE)    ||
        (ecsr & QM_EX_ENQUEUE_STATE)    ||
        (ecsr & QM_EX_ENQUEUE_OVERFLOW) ||
        (ecsr & QM_EX_DEQUEUE_DCP)      ||
        (ecsr & QM_EX_DEQUEUE_FQ)       ||
        (ecsr & QM_EX_DEQUEUE_QUEUE)    ||
        (ecsr & QM_EX_DEQUEUE_SOURCE)   ||
        (ecsr & QM_EX_INVALID_COMMAND))
    {
        p_errInfo->portalValid = TRUE;
        p_errInfo->hwPortal = (bool)(ecir & ECIR_PORTAL_TYPE);
        if (p_errInfo->hwPortal)
            p_errInfo->dcpId = (e_DpaaDcPortal)((ecir & ECIR_PORTAL_MASK) >> ECIR_PORTAL_SHIFT);
        else
            p_errInfo->swPortalId = (e_DpaaSwPortal)((ecir & ECIR_PORTAL_MASK) >> ECIR_PORTAL_SHIFT);
    }

    if ((ecsr & QM_EX_ENQUEUE_QUEUE)    ||
        (ecsr & QM_EX_ENQUEUE_STATE)    ||
        (ecsr & QM_EX_ENQUEUE_OVERFLOW) ||
        (ecsr & QM_EX_ENQUEUE_CHANNEL)  ||
        (ecsr & QM_EX_DEQUEUE_QUEUE)    ||
        (ecsr & QM_EX_DEQUEUE_FQ))
    {
        p_errInfo->fqidValid = TRUE;
        p_errInfo->fqid = ((ecir & ECIR_FQID_MASK) >> ECIR_FQID_SHIFT);
    }

    WRITE_UINT32(p_Qm->p_QmRegs->ecsr, ecsr);

    return ERROR_CODE(err);
}

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
t_Error QM_DumpRegs(t_Handle h_Qm)
{
    t_Qm        *p_Qm = (t_Qm *)h_Qm;
    uint8_t     i = 0;

    DECLARE_DUMP;

    SANITY_CHECK_RETURN_ERROR(p_Qm, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Qm->p_QmDriverParams, E_INVALID_STATE);

    DUMP_SUBTITLE(("\n"));
    DUMP_TITLE(p_Qm->p_QmRegs, ("QmRegs Regs"));

    DUMP_SUBSTRUCT_ARRAY(i, QM_NUM_OF_SWP)
    {
        DUMP_VAR(&p_Qm->p_QmRegs->swpConfRegs[i], lio_cfg);
        DUMP_VAR(&p_Qm->p_QmRegs->swpConfRegs[i], io_cfg);
        DUMP_VAR(&p_Qm->p_QmRegs->swpConfRegs[i], dd_cfg);
    }
    DUMP_VAR(p_Qm->p_QmRegs, qman_dd_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, qcsp_dd_ihrsr);
    DUMP_VAR(p_Qm->p_QmRegs, qcsp_dd_ihrfr);
    DUMP_VAR(p_Qm->p_QmRegs, qcsp_dd_hasr);
    DUMP_VAR(p_Qm->p_QmRegs, dcp_dd_ihrsr);
    DUMP_VAR(p_Qm->p_QmRegs, dcp_dd_ihrfr);
    DUMP_VAR(p_Qm->p_QmRegs, dcp_dd_hasr);
    DUMP_SUBSTRUCT_ARRAY(i, QM_NUM_OF_DCP)
    {
        DUMP_VAR(&p_Qm->p_QmRegs->dcpConfRegs[i], cfg);
        DUMP_VAR(&p_Qm->p_QmRegs->dcpConfRegs[i], dd_cfg);
        DUMP_VAR(&p_Qm->p_QmRegs->dcpConfRegs[i], dlm_cfg);
        DUMP_VAR(&p_Qm->p_QmRegs->dcpConfRegs[i], dlm_avg);
    }
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_fpc);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_fp_head);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_fp_tail);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_fp_lwit);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, sfdr_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, sfdr_in_use);
    DUMP_ARR(p_Qm->p_QmRegs, wq_cs_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, wq_def_enq_wqid);
    DUMP_ARR(p_Qm->p_QmRegs, wq_sc_dd_cfg);
    DUMP_ARR(p_Qm->p_QmRegs, wq_pc_dd_cs_cfg);
    DUMP_ARR(p_Qm->p_QmRegs, wq_dc0_dd_cs_cfg);
    DUMP_ARR(p_Qm->p_QmRegs, wq_dc1_dd_cs_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, wq_dc2_dd_cs_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, wq_dc3_dd_cs_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, cm_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, ecsr);
    DUMP_VAR(p_Qm->p_QmRegs, ecir);
    DUMP_VAR(p_Qm->p_QmRegs, eadr);
    DUMP_ARR(p_Qm->p_QmRegs, edata);
    DUMP_VAR(p_Qm->p_QmRegs, sbet);
    DUMP_ARR(p_Qm->p_QmRegs, sbec);
    DUMP_VAR(p_Qm->p_QmRegs, mcr);
    DUMP_VAR(p_Qm->p_QmRegs, mcp0);
    DUMP_VAR(p_Qm->p_QmRegs, mcp1);
    DUMP_ARR(p_Qm->p_QmRegs, mr);
    DUMP_VAR(p_Qm->p_QmRegs, idle_stat);
    DUMP_VAR(p_Qm->p_QmRegs, ip_rev_1);
    DUMP_VAR(p_Qm->p_QmRegs, ip_rev_2);
    DUMP_VAR(p_Qm->p_QmRegs, fqd_bare);
    DUMP_VAR(p_Qm->p_QmRegs, fqd_bar);
    DUMP_VAR(p_Qm->p_QmRegs, fqd_ar);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_bare);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_bar);
    DUMP_VAR(p_Qm->p_QmRegs, pfdr_ar);
    DUMP_VAR(p_Qm->p_QmRegs, qcsp_bare);
    DUMP_VAR(p_Qm->p_QmRegs, qcsp_bar);
    DUMP_VAR(p_Qm->p_QmRegs, ci_sched_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, srcidr);
    DUMP_VAR(p_Qm->p_QmRegs, liodnr);
    DUMP_VAR(p_Qm->p_QmRegs, ci_rlm_cfg);
    DUMP_VAR(p_Qm->p_QmRegs, ci_rlm_avg);
    DUMP_VAR(p_Qm->p_QmRegs, err_isr);
    DUMP_VAR(p_Qm->p_QmRegs, err_ier);
    DUMP_VAR(p_Qm->p_QmRegs, err_isdr);
    DUMP_VAR(p_Qm->p_QmRegs, err_iir);
    DUMP_VAR(p_Qm->p_QmRegs, err_her);

    return E_OK;
}
#endif /* (defined(DEBUG_ERRORS) && ... */
