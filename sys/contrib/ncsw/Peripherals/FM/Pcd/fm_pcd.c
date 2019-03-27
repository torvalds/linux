/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_pcd.c

 @Description   FM PCD ...
*//***************************************************************************/
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "xx_ext.h"
#include "sprint_ext.h"
#include "debug_ext.h"
#include "net_ext.h"
#include "fm_ext.h"
#include "fm_pcd_ext.h"

#include "fm_common.h"
#include "fm_pcd.h"
#include "fm_pcd_ipc.h"
#include "fm_hc.h"
#include "fm_muram_ext.h"


/****************************************/
/*       static functions               */
/****************************************/

static t_Error CheckFmPcdParameters(t_FmPcd *p_FmPcd)
{
    if (!p_FmPcd->h_Fm)
         RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("h_Fm has to be initialized"));

    if (p_FmPcd->guestId == NCSW_MASTER_ID)
    {
        if (p_FmPcd->p_FmPcdKg && !p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Something WRONG"));

        if (p_FmPcd->p_FmPcdPlcr && !p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Something WRONG"));

        if (!p_FmPcd->f_Exception)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("f_FmPcdExceptions has to be initialized"));

        if ((!p_FmPcd->f_FmPcdIndexedException) && (p_FmPcd->p_FmPcdPlcr || p_FmPcd->p_FmPcdKg))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("f_FmPcdIndexedException has to be initialized"));

        if (p_FmPcd->p_FmPcdDriverParam->prsMaxParseCycleLimit > PRS_MAX_CYCLE_LIMIT)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("prsMaxParseCycleLimit has to be less than 8191"));
    }

    return E_OK;
}

static volatile bool blockingFlag = FALSE;
static void IpcMsgCompletionCB(t_Handle   h_FmPcd,
                               uint8_t    *p_Msg,
                               uint8_t    *p_Reply,
                               uint32_t   replyLength,
                               t_Error    status)
{
    UNUSED(h_FmPcd);UNUSED(p_Msg);UNUSED(p_Reply);UNUSED(replyLength);UNUSED(status);
    blockingFlag = FALSE;
}

static t_Error IpcMsgHandlerCB(t_Handle  h_FmPcd,
                               uint8_t   *p_Msg,
                               uint32_t  msgLength,
                               uint8_t   *p_Reply,
                               uint32_t  *p_ReplyLength)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error             err = E_OK;
    t_FmPcdIpcMsg       *p_IpcMsg   = (t_FmPcdIpcMsg*)p_Msg;
    t_FmPcdIpcReply     *p_IpcReply = (t_FmPcdIpcReply*)p_Reply;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((msgLength >= sizeof(uint32_t)), E_INVALID_VALUE);

#ifdef DISABLE_SANITY_CHECKS
    UNUSED(msgLength);
#endif /* DISABLE_SANITY_CHECKS */

    ASSERT_COND(p_Msg);

    memset(p_IpcReply, 0, (sizeof(uint8_t) * FM_PCD_MAX_REPLY_SIZE));
    *p_ReplyLength = 0;

    switch (p_IpcMsg->msgId)
    {
        case (FM_PCD_MASTER_IS_ALIVE):
            *(uint8_t*)(p_IpcReply->replyBody) = 1;
            p_IpcReply->error = E_OK;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case (FM_PCD_MASTER_IS_ENABLED):
            /* count partitions registrations */
            if (p_FmPcd->enabled)
                p_FmPcd->numOfEnabledGuestPartitionsPcds++;
            *(uint8_t*)(p_IpcReply->replyBody)  = (uint8_t)p_FmPcd->enabled;
            p_IpcReply->error = E_OK;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case (FM_PCD_GUEST_DISABLE):
            if (p_FmPcd->numOfEnabledGuestPartitionsPcds)
            {
                p_FmPcd->numOfEnabledGuestPartitionsPcds--;
                p_IpcReply->error = E_OK;
            }
            else
            {
                REPORT_ERROR(MINOR, E_INVALID_STATE,("Trying to disable an unregistered partition"));
                p_IpcReply->error = E_INVALID_STATE;
            }
            *p_ReplyLength = sizeof(uint32_t);
            break;
        case (FM_PCD_GET_COUNTER):
        {
            e_FmPcdCounters inCounter;
            uint32_t        outCounter;

            memcpy((uint8_t*)&inCounter, p_IpcMsg->msgBody, sizeof(uint32_t));
            outCounter = FM_PCD_GetCounter(h_FmPcd, inCounter);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&outCounter, sizeof(uint32_t));
            p_IpcReply->error = E_OK;
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (FM_PCD_ALLOC_KG_SCHEMES):
        {
            t_FmPcdIpcKgSchemesParams   ipcSchemesParams;

            memcpy((uint8_t*)&ipcSchemesParams, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcKgSchemesParams));
            err = FmPcdKgAllocSchemes(h_FmPcd,
                                      ipcSchemesParams.numOfSchemes,
                                      ipcSchemesParams.guestId,
                                      p_IpcReply->replyBody);
            p_IpcReply->error = err;
            *p_ReplyLength = sizeof(uint32_t) + ipcSchemesParams.numOfSchemes*sizeof(uint8_t);
            break;
        }
        case (FM_PCD_FREE_KG_SCHEMES):
        {
            t_FmPcdIpcKgSchemesParams   ipcSchemesParams;

            memcpy((uint8_t*)&ipcSchemesParams, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcKgSchemesParams));
            err = FmPcdKgFreeSchemes(h_FmPcd,
                                     ipcSchemesParams.numOfSchemes,
                                     ipcSchemesParams.guestId,
                                     ipcSchemesParams.schemesIds);
            p_IpcReply->error = err;
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_PCD_ALLOC_KG_CLSPLAN):
        {
            t_FmPcdIpcKgClsPlanParams   ipcKgClsPlanParams;

            memcpy((uint8_t*)&ipcKgClsPlanParams, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcKgClsPlanParams));
            err = KgAllocClsPlanEntries(h_FmPcd,
                                        ipcKgClsPlanParams.numOfClsPlanEntries,
                                        ipcKgClsPlanParams.guestId,
                                        p_IpcReply->replyBody);
            p_IpcReply->error = err;
            *p_ReplyLength =  sizeof(uint32_t) + sizeof(uint8_t);
            break;
        }
        case (FM_PCD_FREE_KG_CLSPLAN):
        {
            t_FmPcdIpcKgClsPlanParams   ipcKgClsPlanParams;

            memcpy((uint8_t*)&ipcKgClsPlanParams, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcKgClsPlanParams));
            KgFreeClsPlanEntries(h_FmPcd,
                                 ipcKgClsPlanParams.numOfClsPlanEntries,
                                 ipcKgClsPlanParams.guestId,
                                 ipcKgClsPlanParams.clsPlanBase);
            *p_ReplyLength = sizeof(uint32_t);
            break;
        }
        case (FM_PCD_ALLOC_PROFILES):
        {
            t_FmIpcResourceAllocParams      ipcAllocParams;
            uint16_t                        base;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            base =  PlcrAllocProfilesForPartition(h_FmPcd,
                                                  ipcAllocParams.base,
                                                  ipcAllocParams.num,
                                                  ipcAllocParams.guestId);
            memcpy(p_IpcReply->replyBody, (uint16_t*)&base, sizeof(uint16_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint16_t);
            break;
        }
        case (FM_PCD_FREE_PROFILES):
        {
            t_FmIpcResourceAllocParams   ipcAllocParams;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            PlcrFreeProfilesForPartition(h_FmPcd,
                                         ipcAllocParams.base,
                                         ipcAllocParams.num,
                                         ipcAllocParams.guestId);
            break;
        }
        case (FM_PCD_SET_PORT_PROFILES):
        {
            t_FmIpcResourceAllocParams   ipcAllocParams;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            PlcrSetPortProfiles(h_FmPcd,
                                ipcAllocParams.guestId,
                                ipcAllocParams.num,
                                ipcAllocParams.base);
            break;
        }
        case (FM_PCD_CLEAR_PORT_PROFILES):
        {
            t_FmIpcResourceAllocParams   ipcAllocParams;
            memcpy(&ipcAllocParams, p_IpcMsg->msgBody, sizeof(t_FmIpcResourceAllocParams));
            PlcrClearPortProfiles(h_FmPcd,
                                  ipcAllocParams.guestId);
            break;
        }
        case (FM_PCD_GET_SW_PRS_OFFSET):
        {
            t_FmPcdIpcSwPrsLable   ipcSwPrsLable;
            uint32_t               swPrsOffset;

            memcpy((uint8_t*)&ipcSwPrsLable, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcSwPrsLable));
            swPrsOffset =
                FmPcdGetSwPrsOffset(h_FmPcd,
                                    (e_NetHeaderType)ipcSwPrsLable.enumHdr,
                                    ipcSwPrsLable.indexPerHdr);
            memcpy(p_IpcReply->replyBody, (uint8_t*)&swPrsOffset, sizeof(uint32_t));
            *p_ReplyLength = sizeof(uint32_t) + sizeof(uint32_t);
            break;
        }
        case (FM_PCD_PRS_INC_PORT_STATS):
        {
            t_FmPcdIpcPrsIncludePort   ipcPrsIncludePort;

            memcpy((uint8_t*)&ipcPrsIncludePort, p_IpcMsg->msgBody, sizeof(t_FmPcdIpcPrsIncludePort));
            PrsIncludePortInStatistics(h_FmPcd,
                                       ipcPrsIncludePort.hardwarePortId,
                                       ipcPrsIncludePort.include);
           break;
        }
        default:
            *p_ReplyLength = 0;
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("command not found!!!"));
    }
    return E_OK;
}

static uint32_t NetEnvLock(t_Handle h_NetEnv)
{
    ASSERT_COND(h_NetEnv);
    return XX_LockIntrSpinlock(((t_FmPcdNetEnv*)h_NetEnv)->h_Spinlock);
}

static void NetEnvUnlock(t_Handle h_NetEnv, uint32_t intFlags)
{
    ASSERT_COND(h_NetEnv);
    XX_UnlockIntrSpinlock(((t_FmPcdNetEnv*)h_NetEnv)->h_Spinlock, intFlags);
}

static void EnqueueLockToFreeLst(t_FmPcd *p_FmPcd, t_FmPcdLock *p_Lock)
{
    uint32_t   intFlags;

    intFlags = XX_LockIntrSpinlock(p_FmPcd->h_Spinlock);
    NCSW_LIST_AddToTail(&p_Lock->node, &p_FmPcd->freeLocksLst);
    XX_UnlockIntrSpinlock(p_FmPcd->h_Spinlock, intFlags);
}

static t_FmPcdLock * DequeueLockFromFreeLst(t_FmPcd *p_FmPcd)
{
    t_FmPcdLock *p_Lock = NULL;
    uint32_t    intFlags;

    intFlags = XX_LockIntrSpinlock(p_FmPcd->h_Spinlock);
    if (!NCSW_LIST_IsEmpty(&p_FmPcd->freeLocksLst))
    {
        p_Lock = FM_PCD_LOCK_OBJ(p_FmPcd->freeLocksLst.p_Next);
        NCSW_LIST_DelAndInit(&p_Lock->node);
    }
    if (p_FmPcd->h_Spinlock)
    	XX_UnlockIntrSpinlock(p_FmPcd->h_Spinlock, intFlags);

    return p_Lock;
}

static void EnqueueLockToAcquiredLst(t_FmPcd *p_FmPcd, t_FmPcdLock *p_Lock)
{
    uint32_t   intFlags;

    intFlags = XX_LockIntrSpinlock(p_FmPcd->h_Spinlock);
    NCSW_LIST_AddToTail(&p_Lock->node, &p_FmPcd->acquiredLocksLst);
    XX_UnlockIntrSpinlock(p_FmPcd->h_Spinlock, intFlags);
}

static t_Error FillFreeLocksLst(t_FmPcd *p_FmPcd)
{
    t_FmPcdLock *p_Lock;
    int         i;

    for (i=0; i<10; i++)
    {
        p_Lock = (t_FmPcdLock *)XX_Malloc(sizeof(t_FmPcdLock));
        if (!p_Lock)
            RETURN_ERROR(MINOR, E_NO_MEMORY, ("FM-PCD lock obj!"));
        memset(p_Lock, 0, sizeof(t_FmPcdLock));
        INIT_LIST(&p_Lock->node);
        p_Lock->h_Spinlock = XX_InitSpinlock();
        if (!p_Lock->h_Spinlock)
        {
            XX_Free(p_Lock);
            RETURN_ERROR(MINOR, E_INVALID_STATE, ("FM-PCD spinlock obj!"));
        }
        EnqueueLockToFreeLst(p_FmPcd, p_Lock);
    }

    return E_OK;
}

static void ReleaseFreeLocksLst(t_FmPcd *p_FmPcd)
{
    t_FmPcdLock *p_Lock;

    p_Lock = DequeueLockFromFreeLst(p_FmPcd);
    while (p_Lock)
    {
        XX_FreeSpinlock(p_Lock->h_Spinlock);
        XX_Free(p_Lock);
        p_Lock = DequeueLockFromFreeLst(p_FmPcd);
    }
}



/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/

void FmPcdSetClsPlanGrpId(t_FmPcd *p_FmPcd, uint8_t netEnvId, uint8_t clsPlanGrpId)
{
    ASSERT_COND(p_FmPcd);
    p_FmPcd->netEnvs[netEnvId].clsPlanGrpId = clsPlanGrpId;
}

t_Error PcdGetClsPlanGrpParams(t_FmPcd *p_FmPcd, t_FmPcdKgInterModuleClsPlanGrpParams *p_GrpParams)
{
    uint8_t netEnvId = p_GrpParams->netEnvId;
    int     i, k, j;

    ASSERT_COND(p_FmPcd);
    if (p_FmPcd->netEnvs[netEnvId].clsPlanGrpId != ILLEGAL_CLS_PLAN)
    {
        p_GrpParams->grpExists = TRUE;
        p_GrpParams->clsPlanGrpId = p_FmPcd->netEnvs[netEnvId].clsPlanGrpId;
        return E_OK;
    }

    for (i=0; ((i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS) &&
              (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE)); i++)
    {
        for (k=0; ((k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS) &&
                   (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE)); k++)
        {
            /* if an option exists, add it to the opts list */
            if (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].opt)
            {
                /* check if this option already exists, add if it doesn't */
                for (j = 0;j<p_GrpParams->numOfOptions;j++)
                {
                    if (p_GrpParams->options[j] == p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].opt)
                        break;
                }
                p_GrpParams->optVectors[j] |= p_FmPcd->netEnvs[netEnvId].unitsVectors[i];
                if (j == p_GrpParams->numOfOptions)
                {
                    p_GrpParams->options[p_GrpParams->numOfOptions] = p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].opt;
                    p_GrpParams->numOfOptions++;
                }
            }
        }
    }

    if (p_GrpParams->numOfOptions == 0)
    {
        if (p_FmPcd->p_FmPcdKg->emptyClsPlanGrpId != ILLEGAL_CLS_PLAN)
        {
            p_GrpParams->grpExists = TRUE;
            p_GrpParams->clsPlanGrpId = p_FmPcd->p_FmPcdKg->emptyClsPlanGrpId;
        }
    }

    return E_OK;

}

t_Error PcdGetVectorForOpt(t_FmPcd *p_FmPcd, uint8_t netEnvId, protocolOpt_t opt, uint32_t *p_Vector)
{
    uint8_t     j,k;

    *p_Vector = 0;

    ASSERT_COND(p_FmPcd);
    for (j=0; ((j < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS) &&
              (p_FmPcd->netEnvs[netEnvId].units[j].hdrs[0].hdr != HEADER_TYPE_NONE)); j++)
    {
        for (k=0; ((k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS) &&
                  (p_FmPcd->netEnvs[netEnvId].units[j].hdrs[k].hdr != HEADER_TYPE_NONE)); k++)
        {
            if (p_FmPcd->netEnvs[netEnvId].units[j].hdrs[k].opt == opt)
                *p_Vector |= p_FmPcd->netEnvs[netEnvId].unitsVectors[j];
        }
    }

    if (!*p_Vector)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Requested option was not defined for this Network Environment Characteristics module"));
    else
        return E_OK;
}

t_Error PcdGetUnitsVector(t_FmPcd *p_FmPcd, t_NetEnvParams *p_Params)
{
    int                     i;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(p_Params->netEnvId < FM_MAX_NUM_OF_PORTS);

    p_Params->vector = 0;
    for (i=0; i<p_Params->numOfDistinctionUnits ;i++)
    {
        if (p_FmPcd->netEnvs[p_Params->netEnvId].units[p_Params->unitIds[i]].hdrs[0].hdr == HEADER_TYPE_NONE)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Requested unit was not defined for this Network Environment Characteristics module"));
        ASSERT_COND(p_FmPcd->netEnvs[p_Params->netEnvId].unitsVectors[p_Params->unitIds[i]]);
        p_Params->vector |= p_FmPcd->netEnvs[p_Params->netEnvId].unitsVectors[p_Params->unitIds[i]];
    }

    return E_OK;
}

bool PcdNetEnvIsUnitWithoutOpts(t_FmPcd *p_FmPcd, uint8_t netEnvId, uint32_t unitVector)
{
    int     i=0, k;

    ASSERT_COND(p_FmPcd);
    /* check whether a given unit may be used by non-clsPlan users. */
    /* first, recognize the unit by its vector */
    while (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE)
    {
        if (p_FmPcd->netEnvs[netEnvId].unitsVectors[i] == unitVector)
        {
            for (k=0;
                 ((k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS) &&
                  (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE));
                 k++)
                /* check that no option exists */
                if ((protocolOpt_t)p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].opt)
                    return FALSE;
            break;
        }
        i++;
    }
    /* assert that a unit was found to mach the vector */
    ASSERT_COND(p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE);

    return TRUE;
}
bool  FmPcdNetEnvIsHdrExist(t_Handle h_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    int         i, k;

    ASSERT_COND(p_FmPcd);

    for (i=0; ((i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS) &&
              (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE)); i++)
    {
        for (k=0; ((k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS) &&
                  (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE)); k++)
            if (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr == hdr)
                return TRUE;
    }
    for (i=0; ((i < FM_PCD_MAX_NUM_OF_ALIAS_HDRS) &&
              (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr != HEADER_TYPE_NONE)); i++)
    {
        if (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr == hdr)
            return TRUE;
    }

    return FALSE;
}

uint8_t FmPcdNetEnvGetUnitId(t_FmPcd *p_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr, bool interchangeable, protocolOpt_t opt)
{
    uint8_t     i, k;

    ASSERT_COND(p_FmPcd);

    if (interchangeable)
    {
        for (i=0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS) &&
                 (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
        {
            for (k=0; (k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS) &&
                     (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE); k++)
            {
                if ((p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].hdr == hdr) &&
                    (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[k].opt == opt))

                return i;
            }
        }
    }
    else
    {
        for (i=0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS) &&
                 (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
            if ((p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].hdr == hdr) &&
                (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[0].opt == opt) &&
                (p_FmPcd->netEnvs[netEnvId].units[i].hdrs[1].hdr == HEADER_TYPE_NONE))
                    return i;

        for (i=0; (i < FM_PCD_MAX_NUM_OF_ALIAS_HDRS) &&
                 (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr != HEADER_TYPE_NONE); i++)
            if ((p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr == hdr) &&
                (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].opt == opt))
                return p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].aliasHdr;
    }

    return FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS;
}

t_Error FmPcdUnregisterReassmPort(t_Handle h_FmPcd, t_Handle h_ReasmCommonPramTbl)
{
    t_FmPcd                         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmPcdCcReassmTimeoutParams    ccReassmTimeoutParams = {0};
    uint8_t                         result;
    t_Error                         err = E_OK;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(h_ReasmCommonPramTbl);

    ccReassmTimeoutParams.iprcpt   = (uint32_t)(XX_VirtToPhys(h_ReasmCommonPramTbl) - p_FmPcd->physicalMuramBase);
    ccReassmTimeoutParams.activate = FALSE; /*Disable Timeout Task*/

    if ((err = FmHcPcdCcTimeoutReassm(p_FmPcd->h_Hc, &ccReassmTimeoutParams, &result)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    switch (result)
    {
        case (0):
            return E_OK;
        case (1):
            RETURN_ERROR(MAJOR, E_INVALID_STATE, (""));
        case (2):
            RETURN_ERROR(MAJOR, E_INVALID_STATE, (""));
        case (3):
            RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("Disable Timeout Task with invalid IPRCPT"));
        default:
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
    }

    return E_OK;
}

e_NetHeaderType FmPcdGetAliasHdr(t_FmPcd *p_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr)
{
    int         i;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(netEnvId < FM_MAX_NUM_OF_PORTS);

    for (i=0; (i < FM_PCD_MAX_NUM_OF_ALIAS_HDRS)
        && (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr != HEADER_TYPE_NONE); i++)
    {
        if (p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].hdr == hdr)
            return p_FmPcd->netEnvs[netEnvId].aliasHdrs[i].aliasHdr;
    }

    return HEADER_TYPE_NONE;
}

void   FmPcdPortRegister(t_Handle h_FmPcd, t_Handle h_FmPort, uint8_t hardwarePortId)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint16_t        swPortIndex = 0;

    ASSERT_COND(h_FmPcd);
    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);
    p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].h_FmPort = h_FmPort;
}

uint32_t FmPcdGetLcv(t_Handle h_FmPcd, uint32_t netEnvId, uint8_t hdrNum)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

    ASSERT_COND(h_FmPcd);
    return p_FmPcd->netEnvs[netEnvId].lcvs[hdrNum];
}

uint32_t FmPcdGetMacsecLcv(t_Handle h_FmPcd, uint32_t netEnvId)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

    ASSERT_COND(h_FmPcd);
    return p_FmPcd->netEnvs[netEnvId].macsecVector;
}

uint8_t FmPcdGetNetEnvId(t_Handle h_NetEnv)
{
    return ((t_FmPcdNetEnv*)h_NetEnv)->netEnvId;
}

void FmPcdIncNetEnvOwners(t_Handle h_FmPcd, uint8_t netEnvId)
{
    uint32_t    intFlags;

    ASSERT_COND(h_FmPcd);

    intFlags = NetEnvLock(&((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId]);
    ((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId].owners++;
    NetEnvUnlock(&((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId], intFlags);
}

void FmPcdDecNetEnvOwners(t_Handle h_FmPcd, uint8_t netEnvId)
{
    uint32_t    intFlags;

    ASSERT_COND(h_FmPcd);
    ASSERT_COND(((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId].owners);

    intFlags = NetEnvLock(&((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId]);
    ((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId].owners--;
    NetEnvUnlock(&((t_FmPcd*)h_FmPcd)->netEnvs[netEnvId], intFlags);
}

uint32_t FmPcdLock(t_Handle h_FmPcd)
{
    ASSERT_COND(h_FmPcd);
    return XX_LockIntrSpinlock(((t_FmPcd*)h_FmPcd)->h_Spinlock);
}

void FmPcdUnlock(t_Handle h_FmPcd, uint32_t intFlags)
{
    ASSERT_COND(h_FmPcd);
    XX_UnlockIntrSpinlock(((t_FmPcd*)h_FmPcd)->h_Spinlock, intFlags);
}

t_FmPcdLock * FmPcdAcquireLock(t_Handle h_FmPcd)
{
    t_FmPcdLock *p_Lock;
    ASSERT_COND(h_FmPcd);
    p_Lock = DequeueLockFromFreeLst((t_FmPcd*)h_FmPcd);
    if (!p_Lock)
    {
        FillFreeLocksLst(h_FmPcd);
        p_Lock = DequeueLockFromFreeLst((t_FmPcd*)h_FmPcd);
    }

    if (p_Lock)
        EnqueueLockToAcquiredLst((t_FmPcd*)h_FmPcd, p_Lock);
    return p_Lock;
}

void FmPcdReleaseLock(t_Handle h_FmPcd, t_FmPcdLock *p_Lock)
{
    uint32_t intFlags;
    ASSERT_COND(h_FmPcd);
    intFlags = FmPcdLock(h_FmPcd);
    NCSW_LIST_DelAndInit(&p_Lock->node);
    FmPcdUnlock(h_FmPcd, intFlags);
    EnqueueLockToFreeLst((t_FmPcd*)h_FmPcd, p_Lock);
}

bool FmPcdLockTryLockAll(t_Handle h_FmPcd)
{
    uint32_t    intFlags;
    t_List      *p_Pos, *p_SavedPos=NULL;

    ASSERT_COND(h_FmPcd);
    intFlags = FmPcdLock(h_FmPcd);
    NCSW_LIST_FOR_EACH(p_Pos, &((t_FmPcd*)h_FmPcd)->acquiredLocksLst)
    {
        t_FmPcdLock *p_Lock = FM_PCD_LOCK_OBJ(p_Pos);
        if (!FmPcdLockTryLock(p_Lock))
        {
            p_SavedPos = p_Pos;
            break;
        }
    }
    if (p_SavedPos)
    {
        NCSW_LIST_FOR_EACH(p_Pos, &((t_FmPcd*)h_FmPcd)->acquiredLocksLst)
        {
            t_FmPcdLock *p_Lock = FM_PCD_LOCK_OBJ(p_Pos);
            if (p_Pos == p_SavedPos)
                break;
            FmPcdLockUnlock(p_Lock);
        }
    }
    FmPcdUnlock(h_FmPcd, intFlags);

    CORE_MemoryBarrier();

    if (p_SavedPos)
        return FALSE;

    return TRUE;
}

void FmPcdLockUnlockAll(t_Handle h_FmPcd)
{
    uint32_t    intFlags;
    t_List      *p_Pos;

    ASSERT_COND(h_FmPcd);
    intFlags = FmPcdLock(h_FmPcd);
    NCSW_LIST_FOR_EACH(p_Pos, &((t_FmPcd*)h_FmPcd)->acquiredLocksLst)
    {
        t_FmPcdLock *p_Lock = FM_PCD_LOCK_OBJ(p_Pos);
        p_Lock->flag = FALSE;
    }
    FmPcdUnlock(h_FmPcd, intFlags);

    CORE_MemoryBarrier();
}

t_Error FmPcdHcSync(t_Handle h_FmPcd)
{
    ASSERT_COND(h_FmPcd);
    ASSERT_COND(((t_FmPcd*)h_FmPcd)->h_Hc);

    return FmHcPcdSync(((t_FmPcd*)h_FmPcd)->h_Hc);
}

t_Handle FmPcdGetHcHandle(t_Handle h_FmPcd)
{
    ASSERT_COND(h_FmPcd);
    return ((t_FmPcd*)h_FmPcd)->h_Hc;
}

bool FmPcdIsAdvancedOffloadSupported(t_Handle h_FmPcd)
{
    ASSERT_COND(h_FmPcd);
    return ((t_FmPcd*)h_FmPcd)->advancedOffloadSupport;
}
/*********************** End of inter-module routines ************************/


/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle FM_PCD_Config(t_FmPcdParams *p_FmPcdParams)
{
    t_FmPcd             *p_FmPcd = NULL;
    t_FmPhysAddr        physicalMuramBase;
    uint8_t             i;

    SANITY_CHECK_RETURN_VALUE(p_FmPcdParams, E_INVALID_HANDLE,NULL);

    p_FmPcd = (t_FmPcd *) XX_Malloc(sizeof(t_FmPcd));
    if (!p_FmPcd)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD"));
        return NULL;
    }
    memset(p_FmPcd, 0, sizeof(t_FmPcd));

    p_FmPcd->p_FmPcdDriverParam = (t_FmPcdDriverParam *) XX_Malloc(sizeof(t_FmPcdDriverParam));
    if (!p_FmPcd->p_FmPcdDriverParam)
    {
        XX_Free(p_FmPcd);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD Driver Param"));
        return NULL;
    }
    memset(p_FmPcd->p_FmPcdDriverParam, 0, sizeof(t_FmPcdDriverParam));

    p_FmPcd->h_Fm = p_FmPcdParams->h_Fm;
    p_FmPcd->guestId = FmGetGuestId(p_FmPcd->h_Fm);
    p_FmPcd->h_FmMuram = FmGetMuramHandle(p_FmPcd->h_Fm);
    if (p_FmPcd->h_FmMuram)
    {
        FmGetPhysicalMuramBase(p_FmPcdParams->h_Fm, &physicalMuramBase);
        p_FmPcd->physicalMuramBase = (uint64_t)((uint64_t)(&physicalMuramBase)->low | ((uint64_t)(&physicalMuramBase)->high << 32));
    }

    for (i = 0; i<FM_MAX_NUM_OF_PORTS; i++)
        p_FmPcd->netEnvs[i].clsPlanGrpId = ILLEGAL_CLS_PLAN;

    if (p_FmPcdParams->useHostCommand)
    {
        t_FmHcParams    hcParams;

        memset(&hcParams, 0, sizeof(hcParams));
        hcParams.h_Fm = p_FmPcd->h_Fm;
        hcParams.h_FmPcd = (t_Handle)p_FmPcd;
        memcpy((uint8_t*)&hcParams.params, (uint8_t*)&p_FmPcdParams->hc, sizeof(t_FmPcdHcParams));
        p_FmPcd->h_Hc = FmHcConfigAndInit(&hcParams);
        if (!p_FmPcd->h_Hc)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD HC"));
            FM_PCD_Free(p_FmPcd);
            return NULL;
        }
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("No Host Command defined for a guest partition."));

    if (p_FmPcdParams->kgSupport)
    {
        p_FmPcd->p_FmPcdKg = (t_FmPcdKg *)KgConfig(p_FmPcd, p_FmPcdParams);
        if (!p_FmPcd->p_FmPcdKg)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD Keygen"));
            FM_PCD_Free(p_FmPcd);
            return NULL;
        }
    }

    if (p_FmPcdParams->plcrSupport)
    {
        p_FmPcd->p_FmPcdPlcr = (t_FmPcdPlcr *)PlcrConfig(p_FmPcd, p_FmPcdParams);
        if (!p_FmPcd->p_FmPcdPlcr)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD Policer"));
            FM_PCD_Free(p_FmPcd);
            return NULL;
        }
    }

    if (p_FmPcdParams->prsSupport)
    {
        p_FmPcd->p_FmPcdPrs = (t_FmPcdPrs *)PrsConfig(p_FmPcd, p_FmPcdParams);
        if (!p_FmPcd->p_FmPcdPrs)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD Parser"));
            FM_PCD_Free(p_FmPcd);
            return NULL;
        }
    }

    p_FmPcd->h_Spinlock = XX_InitSpinlock();
    if (!p_FmPcd->h_Spinlock)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD spinlock"));
        FM_PCD_Free(p_FmPcd);
        return NULL;
    }
    INIT_LIST(&p_FmPcd->freeLocksLst);
    INIT_LIST(&p_FmPcd->acquiredLocksLst);

    p_FmPcd->numOfEnabledGuestPartitionsPcds = 0;

    p_FmPcd->f_Exception                = p_FmPcdParams->f_Exception;
    p_FmPcd->f_FmPcdIndexedException    = p_FmPcdParams->f_ExceptionId;
    p_FmPcd->h_App                      = p_FmPcdParams->h_App;

    p_FmPcd->p_CcShadow                 = NULL;
    p_FmPcd->ccShadowSize               = 0;
    p_FmPcd->ccShadowAlign              = 0;

    p_FmPcd->h_ShadowSpinlock = XX_InitSpinlock();
    if (!p_FmPcd->h_ShadowSpinlock)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM PCD shadow spinlock"));
        FM_PCD_Free(p_FmPcd);
        return NULL;
    }

    return p_FmPcd;
}

t_Error FM_PCD_Init(t_Handle h_FmPcd)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error         err = E_OK;
    t_FmPcdIpcMsg   msg;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);

    FM_GetRevision(p_FmPcd->h_Fm, &p_FmPcd->fmRevInfo);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
    {
        memset(p_FmPcd->fmPcdIpcHandlerModuleName, 0, (sizeof(char)) * MODULE_NAME_SIZE);
        if (Sprint (p_FmPcd->fmPcdIpcHandlerModuleName, "FM_PCD_%d_%d", FmGetId(p_FmPcd->h_Fm), NCSW_MASTER_ID) != 10)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        memset(p_FmPcd->fmPcdModuleName, 0, (sizeof(char)) * MODULE_NAME_SIZE);
        if (Sprint (p_FmPcd->fmPcdModuleName, "FM_PCD_%d_%d",FmGetId(p_FmPcd->h_Fm), p_FmPcd->guestId) != (p_FmPcd->guestId<10 ? 10:11))
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));

        p_FmPcd->h_IpcSession = XX_IpcInitSession(p_FmPcd->fmPcdIpcHandlerModuleName, p_FmPcd->fmPcdModuleName);
        if (p_FmPcd->h_IpcSession)
        {
            t_FmPcdIpcReply         reply;
            uint32_t                replyLength;
            uint8_t                 isMasterAlive = 0;

            memset(&msg, 0, sizeof(msg));
            memset(&reply, 0, sizeof(reply));
            msg.msgId = FM_PCD_MASTER_IS_ALIVE;
            msg.msgBody[0] = p_FmPcd->guestId;
            blockingFlag = TRUE;

            do
            {
                replyLength = sizeof(uint32_t) + sizeof(isMasterAlive);
                if ((err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                             (uint8_t*)&msg,
                                             sizeof(msg.msgId)+sizeof(p_FmPcd->guestId),
                                             (uint8_t*)&reply,
                                             &replyLength,
                                             IpcMsgCompletionCB,
                                             h_FmPcd)) != E_OK)
                    REPORT_ERROR(MAJOR, err, NO_MSG);
                while (blockingFlag) ;
                if (replyLength != (sizeof(uint32_t) + sizeof(isMasterAlive)))
                    REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
                isMasterAlive = *(uint8_t*)(reply.replyBody);
            } while (!isMasterAlive);
        }
    }

    CHECK_INIT_PARAMETERS(p_FmPcd, CheckFmPcdParameters);

    if (p_FmPcd->p_FmPcdKg)
    {
        err = KgInit(p_FmPcd);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_FmPcd->p_FmPcdPlcr)
    {
        err = PlcrInit(p_FmPcd);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_FmPcd->p_FmPcdPrs)
    {
        err = PrsInit(p_FmPcd);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_FmPcd->guestId == NCSW_MASTER_ID)
    {
         /* register to inter-core messaging mechanism */
        memset(p_FmPcd->fmPcdModuleName, 0, (sizeof(char)) * MODULE_NAME_SIZE);
        if (Sprint (p_FmPcd->fmPcdModuleName, "FM_PCD_%d_%d",FmGetId(p_FmPcd->h_Fm),NCSW_MASTER_ID) != 10)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        err = XX_IpcRegisterMsgHandler(p_FmPcd->fmPcdModuleName, IpcMsgHandlerCB, p_FmPcd, FM_PCD_MAX_REPLY_SIZE);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /* IPv6 Frame-Id used for fragmentation */
    p_FmPcd->ipv6FrameIdAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram, 4, 4));
    if (!p_FmPcd->ipv6FrameIdAddr)
    {
        FM_PCD_Free(p_FmPcd);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for IPv6 Frame-Id"));
    }
    IOMemSet32(UINT_TO_PTR(p_FmPcd->ipv6FrameIdAddr), 0,  4);

    /* CAPWAP Frame-Id used for fragmentation */
    p_FmPcd->capwapFrameIdAddr = PTR_TO_UINT(FM_MURAM_AllocMem(p_FmPcd->h_FmMuram, 2, 4));
    if (!p_FmPcd->capwapFrameIdAddr)
    {
        FM_PCD_Free(p_FmPcd);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for CAPWAP Frame-Id"));
    }
    IOMemSet32(UINT_TO_PTR(p_FmPcd->capwapFrameIdAddr), 0,  2);

    XX_Free(p_FmPcd->p_FmPcdDriverParam);
    p_FmPcd->p_FmPcdDriverParam = NULL;

    FmRegisterPcd(p_FmPcd->h_Fm, p_FmPcd);

    return E_OK;
}

t_Error FM_PCD_Free(t_Handle h_FmPcd)
{
    t_FmPcd                             *p_FmPcd =(t_FmPcd *)h_FmPcd;
    t_Error                             err = E_OK;

    if (p_FmPcd->ipv6FrameIdAddr)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, UINT_TO_PTR(p_FmPcd->ipv6FrameIdAddr));

    if (p_FmPcd->capwapFrameIdAddr)
        FM_MURAM_FreeMem(p_FmPcd->h_FmMuram, UINT_TO_PTR(p_FmPcd->capwapFrameIdAddr));

    if (p_FmPcd->enabled)
        FM_PCD_Disable(p_FmPcd);

    if (p_FmPcd->p_FmPcdDriverParam)
    {
        XX_Free(p_FmPcd->p_FmPcdDriverParam);
        p_FmPcd->p_FmPcdDriverParam = NULL;
    }

    if (p_FmPcd->p_FmPcdKg)
    {
        if ((err = KgFree(p_FmPcd)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
        XX_Free(p_FmPcd->p_FmPcdKg);
        p_FmPcd->p_FmPcdKg = NULL;
    }

    if (p_FmPcd->p_FmPcdPlcr)
    {
        PlcrFree(p_FmPcd);
        XX_Free(p_FmPcd->p_FmPcdPlcr);
        p_FmPcd->p_FmPcdPlcr = NULL;
    }

    if (p_FmPcd->p_FmPcdPrs)
    {
        if (p_FmPcd->guestId == NCSW_MASTER_ID)
            PrsFree(p_FmPcd);
        XX_Free(p_FmPcd->p_FmPcdPrs);
        p_FmPcd->p_FmPcdPrs = NULL;
    }

    if (p_FmPcd->h_Hc)
    {
        FmHcFree(p_FmPcd->h_Hc);
        p_FmPcd->h_Hc = NULL;
    }

    XX_IpcUnregisterMsgHandler(p_FmPcd->fmPcdModuleName);

    FmUnregisterPcd(p_FmPcd->h_Fm);

    ReleaseFreeLocksLst(p_FmPcd);

    if (p_FmPcd->h_Spinlock)
        XX_FreeSpinlock(p_FmPcd->h_Spinlock);

    if (p_FmPcd->h_ShadowSpinlock)
        XX_FreeSpinlock(p_FmPcd->h_ShadowSpinlock);

    XX_Free(p_FmPcd);

    return E_OK;
}

t_Error FM_PCD_ConfigException(t_Handle h_FmPcd, e_FmPcdExceptions exception, bool enable)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t        bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_ConfigException - guest mode!"));

    GET_FM_PCD_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_FmPcd->exceptions |= bitMask;
        else
            p_FmPcd->exceptions &= ~bitMask;
   }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

t_Error FM_PCD_ConfigHcFramesDataMemory(t_Handle h_FmPcd, uint8_t memId)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    return FmHcSetFramesDataMemory(p_FmPcd->h_Hc, memId);
}

t_Error FM_PCD_Enable(t_Handle h_FmPcd)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error             err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);

    if (p_FmPcd->enabled)
        return E_OK;

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        p_FmPcd->h_IpcSession)
    {
        uint8_t         enabled;
        t_FmPcdIpcMsg   msg;
        t_FmPcdIpcReply reply;
        uint32_t        replyLength;

        memset(&reply, 0, sizeof(reply));
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_PCD_MASTER_IS_ENABLED;
        replyLength = sizeof(uint32_t) + sizeof(enabled);
        if ((err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t) + sizeof(enabled))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        p_FmPcd->enabled = (bool)!!(*(uint8_t*)(reply.replyBody));
        if (!p_FmPcd->enabled)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("FM-PCD master should be enabled first!"));

        return E_OK;
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    if (p_FmPcd->p_FmPcdKg)
        KgEnable(p_FmPcd);

    if (p_FmPcd->p_FmPcdPlcr)
        PlcrEnable(p_FmPcd);

    if (p_FmPcd->p_FmPcdPrs)
        PrsEnable(p_FmPcd);

    p_FmPcd->enabled = TRUE;

    return E_OK;
}

t_Error FM_PCD_Disable(t_Handle h_FmPcd)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error             err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);

    if (!p_FmPcd->enabled)
        return E_OK;

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        p_FmPcd->h_IpcSession)
    {
        t_FmPcdIpcMsg       msg;
        t_FmPcdIpcReply     reply;
        uint32_t            replyLength;

        memset(&reply, 0, sizeof(reply));
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_PCD_GUEST_DISABLE;
        replyLength = sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
        if (reply.error == E_OK)
            p_FmPcd->enabled = FALSE;

        return (t_Error)(reply.error);
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    if (p_FmPcd->numOfEnabledGuestPartitionsPcds != 0)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("Trying to disable a master partition PCD while"
                      "guest partitions are still enabled!"));

    if (p_FmPcd->p_FmPcdKg)
         KgDisable(p_FmPcd);

    if (p_FmPcd->p_FmPcdPlcr)
        PlcrDisable(p_FmPcd);

    if (p_FmPcd->p_FmPcdPrs)
        PrsDisable(p_FmPcd);

    p_FmPcd->enabled = FALSE;

    return E_OK;
}

t_Handle FM_PCD_NetEnvCharacteristicsSet(t_Handle h_FmPcd, t_FmPcdNetEnvParams  *p_NetEnvParams)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t                intFlags, specialUnits = 0;
    uint8_t                 bitId = 0;
    uint8_t                 i, j, k;
    uint8_t                 netEnvCurrId;
    uint8_t                 ipsecAhUnit = 0,ipsecEspUnit = 0;
    bool                    ipsecAhExists = FALSE, ipsecEspExists = FALSE, shim1Selected = FALSE;
    uint8_t                 hdrNum;
    t_FmPcdNetEnvParams     *p_ModifiedNetEnvParams;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_STATE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_NetEnvParams, E_NULL_POINTER, NULL);

    intFlags = FmPcdLock(p_FmPcd);

    /* find a new netEnv */
    for (i = 0; i < FM_MAX_NUM_OF_PORTS; i++)
        if (!p_FmPcd->netEnvs[i].used)
            break;

    if (i== FM_MAX_NUM_OF_PORTS)
    {
        REPORT_ERROR(MAJOR, E_FULL,("No more than %d netEnv's allowed.", FM_MAX_NUM_OF_PORTS));
        FmPcdUnlock(p_FmPcd, intFlags);
        return NULL;
    }

    p_FmPcd->netEnvs[i].used = TRUE;
    FmPcdUnlock(p_FmPcd, intFlags);

    /* As anyone doesn't have handle of this netEnv yet, no need
       to protect it with spinlocks */

    p_ModifiedNetEnvParams = (t_FmPcdNetEnvParams *)XX_Malloc(sizeof(t_FmPcdNetEnvParams));
    if (!p_ModifiedNetEnvParams)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FmPcdNetEnvParams"));
        return NULL;
    }

    memcpy(p_ModifiedNetEnvParams, p_NetEnvParams, sizeof(t_FmPcdNetEnvParams));
    p_NetEnvParams = p_ModifiedNetEnvParams;

    netEnvCurrId = (uint8_t)i;

    /* clear from previous use */
    memset(&p_FmPcd->netEnvs[netEnvCurrId].units, 0, FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS * sizeof(t_FmPcdIntDistinctionUnit));
    memset(&p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs, 0, FM_PCD_MAX_NUM_OF_ALIAS_HDRS * sizeof(t_FmPcdNetEnvAliases));
    memcpy(&p_FmPcd->netEnvs[netEnvCurrId].units, p_NetEnvParams->units, p_NetEnvParams->numOfDistinctionUnits*sizeof(t_FmPcdIntDistinctionUnit));

    p_FmPcd->netEnvs[netEnvCurrId].netEnvId = netEnvCurrId;
    p_FmPcd->netEnvs[netEnvCurrId].h_FmPcd = p_FmPcd;

    p_FmPcd->netEnvs[netEnvCurrId].clsPlanGrpId = ILLEGAL_CLS_PLAN;

    /* check that header with opt is not interchanged with the same header */
    for (i = 0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
            && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
    {
        for (k = 0; (k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS)
            && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE); k++)
        {
            /* if an option exists, check that other headers are not the same header
            without option */
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt)
            {
                for (j = 0; (j < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS)
                        && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[j].hdr != HEADER_TYPE_NONE); j++)
                {
                    if ((p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[j].hdr == p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr) &&
                        !p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[j].opt)
                    {
                        REPORT_ERROR(MINOR, E_FULL,
                                ("Illegal unit - header with opt may not be interchangeable with the same header without opt"));
                        XX_Free(p_ModifiedNetEnvParams);
                        return NULL;
                    }
                }
            }
        }
    }

    /* Specific headers checking  */
    for (i = 0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
        && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
    {
        for (k = 0; (k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS)
            && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE); k++)
        {
            /* Some headers pairs may not be defined on different units as the parser
            doesn't distinguish */
            /* IPSEC_AH and IPSEC_SPI can't be 2 units,  */
            /* check that header with opt is not interchanged with the same header */
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_IPSEC_AH)
            {
                if (ipsecEspExists && (ipsecEspUnit != i))
                {
                    REPORT_ERROR(MINOR, E_INVALID_STATE, ("HEADER_TYPE_IPSEC_AH and HEADER_TYPE_IPSEC_ESP may not be defined in separate units"));
                    XX_Free(p_ModifiedNetEnvParams);
                    return NULL;
                }
                else
                {
                    ipsecAhUnit = i;
                    ipsecAhExists = TRUE;
                }
            }
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_IPSEC_ESP)
            {
                if (ipsecAhExists && (ipsecAhUnit != i))
                {
                    REPORT_ERROR(MINOR, E_INVALID_STATE, ("HEADER_TYPE_IPSEC_AH and HEADER_TYPE_IPSEC_ESP may not be defined in separate units"));
                    XX_Free(p_ModifiedNetEnvParams);
                    return NULL;
                }
                else
                {
                    ipsecEspUnit = i;
                    ipsecEspExists = TRUE;
                }
            }
            /* ENCAP_ESP  */
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_UDP_ENCAP_ESP)
            {
                /* IPSec UDP encapsulation is currently set to use SHIM1 */
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].hdr = HEADER_TYPE_UDP_ENCAP_ESP;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits++].aliasHdr = HEADER_TYPE_USER_DEFINED_SHIM1;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr = HEADER_TYPE_USER_DEFINED_SHIM1;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt = 0;
            }
#if (DPAA_VERSION >= 11) || ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
            /* UDP_LITE  */
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_UDP_LITE)
            {
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].hdr = HEADER_TYPE_UDP_LITE;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits++].aliasHdr = HEADER_TYPE_UDP;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr = HEADER_TYPE_UDP;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt = 0;
            }
#endif /* (DPAA_VERSION >= 11) || ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */

            /* IP FRAG  */
            if ((p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_IPv4) &&
                (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt == IPV4_FRAG_1))
            {
                /* If IPv4+Frag, we need to set 2 units - SHIM 2 and IPv4. We first set SHIM2, and than check if
                 * IPv4 exists. If so we don't need to set an extra unit
                 * We consider as "having IPv4" any IPv4 without interchangable headers
                 * but including any options.  */
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].hdr = HEADER_TYPE_IPv4;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].opt = IPV4_FRAG_1;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits++].aliasHdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt = 0;

                /* check if IPv4 header exists by itself */
                if (FmPcdNetEnvGetUnitId(p_FmPcd, netEnvCurrId, HEADER_TYPE_IPv4, FALSE, 0) == FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
                {
                    p_FmPcd->netEnvs[netEnvCurrId].units[p_NetEnvParams->numOfDistinctionUnits].hdrs[0].hdr = HEADER_TYPE_IPv4;
                    p_FmPcd->netEnvs[netEnvCurrId].units[p_NetEnvParams->numOfDistinctionUnits++].hdrs[0].opt = 0;
                }
            }
            if ((p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_IPv6) &&
                (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt == IPV6_FRAG_1))
            {
                /* If IPv6+Frag, we need to set 2 units - SHIM 2 and IPv6. We first set SHIM2, and than check if
                 * IPv4 exists. If so we don't need to set an extra unit
                 * We consider as "having IPv6" any IPv6 without interchangable headers
                 * but including any options.  */
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].hdr = HEADER_TYPE_IPv6;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].opt = IPV6_FRAG_1;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits++].aliasHdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt = 0;

                /* check if IPv6 header exists by itself */
                if (FmPcdNetEnvGetUnitId(p_FmPcd, netEnvCurrId, HEADER_TYPE_IPv6, FALSE, 0) == FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
                {
                    p_FmPcd->netEnvs[netEnvCurrId].units[p_NetEnvParams->numOfDistinctionUnits].hdrs[0].hdr = HEADER_TYPE_IPv6;
                    p_FmPcd->netEnvs[netEnvCurrId].units[p_NetEnvParams->numOfDistinctionUnits++].hdrs[0].opt = 0;
                }
            }
#if (DPAA_VERSION >= 11)
            /* CAPWAP FRAG  */
            if ((p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr == HEADER_TYPE_CAPWAP) &&
                (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt == CAPWAP_FRAG_1))
            {
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].hdr = HEADER_TYPE_CAPWAP;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits].opt = CAPWAP_FRAG_1;
                p_FmPcd->netEnvs[netEnvCurrId].aliasHdrs[specialUnits++].aliasHdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr = HEADER_TYPE_USER_DEFINED_SHIM2;
                p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].opt = 0;
            }
#endif /* (DPAA_VERSION >= 11) */
        }
    }

    /* if private header (shim), check that no other headers specified */
    for (i = 0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
        && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
    {
        if (IS_PRIVATE_HEADER(p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr))
            if (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[1].hdr != HEADER_TYPE_NONE)
            {
                REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("SHIM header may not be interchanged with other headers"));
                XX_Free(p_ModifiedNetEnvParams);
                return NULL;
            }
    }

    for (i = 0; i < p_NetEnvParams->numOfDistinctionUnits; i++)
    {
        if (IS_PRIVATE_HEADER(p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr))
            switch (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr)
            {
                case (HEADER_TYPE_USER_DEFINED_SHIM1):
                    if (shim1Selected)
                    {
                        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("SHIM header cannot be selected with UDP_IPSEC_ESP"));
                        XX_Free(p_ModifiedNetEnvParams);
                        return NULL;
                    }
                    shim1Selected = TRUE;
                    p_FmPcd->netEnvs[netEnvCurrId].unitsVectors[i] = 0x00000001;
                    break;
                case (HEADER_TYPE_USER_DEFINED_SHIM2):
                    p_FmPcd->netEnvs[netEnvCurrId].unitsVectors[i] = 0x00000002;
                    break;
                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Requested SHIM not supported"));
            }
        else
        {
            p_FmPcd->netEnvs[netEnvCurrId].unitsVectors[i] = (uint32_t)(0x80000000 >> bitId++);

            if (IS_SPECIAL_HEADER(p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr))
                p_FmPcd->netEnvs[netEnvCurrId].macsecVector = p_FmPcd->netEnvs[netEnvCurrId].unitsVectors[i];
        }
    }

    /* define a set of hardware parser LCV's according to the defined netenv */

    /* set an array of LCV's for each header in the netEnv */
    for (i = 0; (i < FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS)
        && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr != HEADER_TYPE_NONE); i++)
    {
        /* private headers have no LCV in the hard parser */
        if (!IS_PRIVATE_HEADER(p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[0].hdr))
        {
            for (k = 0; (k < FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS)
                    && (p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr != HEADER_TYPE_NONE); k++)
            {
                hdrNum = GetPrsHdrNum(p_FmPcd->netEnvs[netEnvCurrId].units[i].hdrs[k].hdr);
                if ((hdrNum == ILLEGAL_HDR_NUM) || (hdrNum == NO_HDR_NUM))
                {
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, NO_MSG);
                    XX_Free(p_ModifiedNetEnvParams);
                    return NULL;
                }
                p_FmPcd->netEnvs[netEnvCurrId].lcvs[hdrNum] |= p_FmPcd->netEnvs[netEnvCurrId].unitsVectors[i];
            }
        }
    }
    XX_Free(p_ModifiedNetEnvParams);

    p_FmPcd->netEnvs[netEnvCurrId].h_Spinlock = XX_InitSpinlock();
    if (!p_FmPcd->netEnvs[netEnvCurrId].h_Spinlock)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Pcd NetEnv spinlock"));
        return NULL;
    }
    return &p_FmPcd->netEnvs[netEnvCurrId];
}

t_Error FM_PCD_NetEnvCharacteristicsDelete(t_Handle h_NetEnv)
{
    t_FmPcdNetEnv   *p_NetEnv = (t_FmPcdNetEnv*)h_NetEnv;
    t_FmPcd         *p_FmPcd = p_NetEnv->h_FmPcd;
    uint32_t        intFlags;
    uint8_t         netEnvId = p_NetEnv->netEnvId;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);

    /* check that no port is bound to this netEnv */
    if (p_FmPcd->netEnvs[netEnvId].owners)
    {
        RETURN_ERROR(MINOR, E_INVALID_STATE,
                ("Trying to delete a netEnv that has ports/schemes/trees/clsPlanGrps bound to"));
    }

    intFlags = FmPcdLock(p_FmPcd);

    p_FmPcd->netEnvs[netEnvId].used = FALSE;
    p_FmPcd->netEnvs[netEnvId].clsPlanGrpId = ILLEGAL_CLS_PLAN;

    memset(p_FmPcd->netEnvs[netEnvId].units, 0, sizeof(t_FmPcdIntDistinctionUnit)*FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS);
    memset(p_FmPcd->netEnvs[netEnvId].unitsVectors, 0, sizeof(uint32_t)*FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS);
    memset(p_FmPcd->netEnvs[netEnvId].lcvs, 0, sizeof(uint32_t)*FM_PCD_PRS_NUM_OF_HDRS);

    if (p_FmPcd->netEnvs[netEnvId].h_Spinlock)
        XX_FreeSpinlock(p_FmPcd->netEnvs[netEnvId].h_Spinlock);

    FmPcdUnlock(p_FmPcd, intFlags);
    return E_OK;
}

void FM_PCD_HcTxConf(t_Handle h_FmPcd, t_DpaaFD *p_Fd)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN(h_FmPcd, E_INVALID_STATE);

    FmHcTxConf(p_FmPcd->h_Hc, p_Fd);
}

t_Error FM_PCD_SetAdvancedOffloadSupport(t_Handle h_FmPcd)
{
    t_FmPcd                     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmCtrlCodeRevisionInfo    revInfo;
    t_Error                     err;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->enabled, E_INVALID_STATE);

    if ((err = FM_GetFmanCtrlCodeRevision(p_FmPcd->h_Fm, &revInfo)) != E_OK)
    {
        DBG(WARNING, ("FM in guest-mode without IPC, can't validate firmware revision."));
        revInfo.packageRev = IP_OFFLOAD_PACKAGE_NUMBER;
    }
    if (!IS_OFFLOAD_PACKAGE(revInfo.packageRev))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Fman ctrl code package"));

    if (!p_FmPcd->h_Hc)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("HC must be initialized in this mode"));

    p_FmPcd->advancedOffloadSupport = TRUE;

    return E_OK;
}

uint32_t FM_PCD_GetCounter(t_Handle h_FmPcd, e_FmPcdCounters counter)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t                outCounter = 0;
    t_Error                 err;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE, 0);

    switch (counter)
    {
        case (e_FM_PCD_KG_COUNTERS_TOTAL):
            if (!p_FmPcd->p_FmPcdKg)
            {
                REPORT_ERROR(MAJOR, E_INVALID_STATE, ("KeyGen is not activated"));
                return 0;
            }
            if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
                !p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs &&
                !p_FmPcd->h_IpcSession)
            {
                REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                             ("running in guest-mode without neither IPC nor mapped register!"));
                return 0;
            }
            break;

        case (e_FM_PCD_PLCR_COUNTERS_YELLOW):
        case (e_FM_PCD_PLCR_COUNTERS_RED):
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_RED):
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_YELLOW):
        case (e_FM_PCD_PLCR_COUNTERS_TOTAL):
        case (e_FM_PCD_PLCR_COUNTERS_LENGTH_MISMATCH):
            if (!p_FmPcd->p_FmPcdPlcr)
            {
                REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Policer is not activated"));
                return 0;
            }
            if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
                !p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs &&
                !p_FmPcd->h_IpcSession)
            {
                REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                             ("running in \"guest-mode\" without neither IPC nor mapped register!"));
                return 0;
            }

            /* check that counters are enabled */
            if (p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs &&
                !(GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_gcr) & FM_PCD_PLCR_GCR_STEN))
            {
                REPORT_ERROR(MINOR, E_INVALID_STATE, ("Requested counter was not enabled"));
                return 0;
            }
            ASSERT_COND(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs ||
                        ((p_FmPcd->guestId != NCSW_MASTER_ID) && p_FmPcd->h_IpcSession));
            break;

        case (e_FM_PCD_PRS_COUNTERS_PARSE_DISPATCH):
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_HARD_PRS_CYCLE_INCL_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_FPM_COMMAND_STALL_CYCLES):
            if (!p_FmPcd->p_FmPcdPrs)
            {
                REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Parser is not activated"));
                return 0;
            }
            if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
                !p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs &&
                !p_FmPcd->h_IpcSession)
            {
                REPORT_ERROR(MINOR, E_NOT_SUPPORTED,
                             ("running in guest-mode without neither IPC nor mapped register!"));
                return 0;
            }
            break;
        default:
            REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Unsupported type of counter"));
            return 0;
    }

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        p_FmPcd->h_IpcSession)
    {
        t_FmPcdIpcMsg           msg;
        t_FmPcdIpcReply         reply;
        uint32_t                replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        msg.msgId = FM_PCD_GET_COUNTER;
        memcpy(msg.msgBody, (uint8_t *)&counter, sizeof(uint32_t));
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        if ((err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                     (uint8_t*)&msg,
                                     sizeof(msg.msgId) +sizeof(uint32_t),
                                     (uint8_t*)&reply,
                                     &replyLength,
                                     NULL,
                                     NULL)) != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t) + sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        memcpy((uint8_t*)&outCounter, reply.replyBody, sizeof(uint32_t));
        return outCounter;
    }

    switch (counter)
    {
        /* Parser statistics */
        case (e_FM_PCD_PRS_COUNTERS_PARSE_DISPATCH):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_pds);
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l2rrs);
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l3rrs);
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l4rrs);
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_srrs);
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED_WITH_ERR):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l2rres);
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED_WITH_ERR):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l3rres);
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED_WITH_ERR):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l4rres);
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED_WITH_ERR):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_srres);
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_spcs);
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_STALL_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_spscs);
        case (e_FM_PCD_PRS_COUNTERS_HARD_PRS_CYCLE_INCL_STALL_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_hxscs);
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mrcs);
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_STALL_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mrscs);
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mwcs);
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_STALL_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mwscs);
        case (e_FM_PCD_PRS_COUNTERS_FPM_COMMAND_STALL_CYCLES):
               return GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_fcscs);
        case (e_FM_PCD_KG_COUNTERS_TOTAL):
               return GET_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_tpc);

        /* Policer statistics */
        case (e_FM_PCD_PLCR_COUNTERS_YELLOW):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ypcnt);
        case (e_FM_PCD_PLCR_COUNTERS_RED):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rpcnt);
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_RED):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rrpcnt);
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_YELLOW):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rypcnt);
        case (e_FM_PCD_PLCR_COUNTERS_TOTAL):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_tpcnt);
        case (e_FM_PCD_PLCR_COUNTERS_LENGTH_MISMATCH):
                return GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_flmcnt);
    }
    return 0;
}

t_Error FM_PCD_SetException(t_Handle h_FmPcd, e_FmPcdExceptions exception, bool enable)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t        bitMask = 0, tmpReg;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_SetException - guest mode!"));

    GET_FM_PCD_EXCEPTION_FLAG(bitMask, exception);

    if (bitMask)
    {
        if (enable)
            p_FmPcd->exceptions |= bitMask;
        else
            p_FmPcd->exceptions &= ~bitMask;

        switch (exception)
        {
            case (e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC):
            case (e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW):
                if (!p_FmPcd->p_FmPcdKg)
                    RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt - keygen is not working"));
                break;
            case (e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC):
            case (e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR):
            case (e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE):
            case (e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE):
                if (!p_FmPcd->p_FmPcdPlcr)
                    RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt - policer is not working"));
                break;
            case (e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC):
            case (e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC):
                if (!p_FmPcd->p_FmPcdPrs)
                    RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt - parser is not working"));
                break;
        }

        switch (exception)
        {
            case (e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_eeer);
                if (enable)
                    tmpReg |= FM_EX_KG_DOUBLE_ECC;
                else
                    tmpReg &= ~FM_EX_KG_DOUBLE_ECC;
                WRITE_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_eeer, tmpReg);
                break;
            case (e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_eeer);
                if (enable)
                    tmpReg |= FM_EX_KG_KEYSIZE_OVERFLOW;
                else
                    tmpReg &= ~FM_EX_KG_KEYSIZE_OVERFLOW;
                WRITE_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_eeer, tmpReg);
                break;
            case (e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_perer);
                if (enable)
                    tmpReg |= FM_PCD_PRS_DOUBLE_ECC;
                else
                    tmpReg &= ~FM_PCD_PRS_DOUBLE_ECC;
                WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_perer, tmpReg);
                break;
            case (e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_pever);
                if (enable)
                    tmpReg |= FM_PCD_PRS_SINGLE_ECC;
                else
                    tmpReg &= ~FM_PCD_PRS_SINGLE_ECC;
                WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_pever, tmpReg);
                break;
            case (e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eier);
                if (enable)
                    tmpReg |= FM_PCD_PLCR_DOUBLE_ECC;
                else
                    tmpReg &= ~FM_PCD_PLCR_DOUBLE_ECC;
                WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eier, tmpReg);
                break;
            case (e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eier);
                if (enable)
                    tmpReg |= FM_PCD_PLCR_INIT_ENTRY_ERROR;
                else
                    tmpReg &= ~FM_PCD_PLCR_INIT_ENTRY_ERROR;
                WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eier, tmpReg);
                break;
            case (e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ier);
                if (enable)
                    tmpReg |= FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE;
                else
                    tmpReg &= ~FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE;
                WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ier, tmpReg);
                break;
            case (e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE):
                tmpReg = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ier);
                if (enable)
                    tmpReg |= FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE;
                else
                    tmpReg &= ~FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE;
                WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ier, tmpReg);
                break;
        }
        /* for ECC exceptions driver automatically enables ECC mechanism, if disabled.
           Driver may disable them automatically, depending on driver's status */
        if (enable && ((exception == e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC)))
            FmEnableRamsEcc(p_FmPcd->h_Fm);
        if (!enable && ((exception == e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC) |
                       (exception == e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC)))
            FmDisableRamsEcc(p_FmPcd->h_Fm);
    }

    return E_OK;
}

t_Error FM_PCD_ForceIntr (t_Handle h_FmPcd, e_FmPcdExceptions exception)
{
    t_FmPcd            *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_ForceIntr - guest mode!"));

    switch (exception)
    {
        case (e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC):
        case (e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW):
            if (!p_FmPcd->p_FmPcdKg)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt - keygen is not working"));
            break;
        case (e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC):
        case (e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR):
        case (e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE):
        case (e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE):
            if (!p_FmPcd->p_FmPcdPlcr)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt - policer is not working"));
            break;
        case (e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC):
        case (e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC):
           if (!p_FmPcd->p_FmPcdPrs)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Can't ask for this interrupt -parsrer is not working"));
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid interrupt requested"));
    }
    switch (exception)
    {
        case e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PRS_DOUBLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PRS_SINGLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            break;
        case e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC:
            if (!(p_FmPcd->exceptions & FM_EX_KG_DOUBLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_feer, FM_EX_KG_DOUBLE_ECC);
            break;
        case e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW:
            if (!(p_FmPcd->exceptions & FM_EX_KG_KEYSIZE_OVERFLOW))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_feer, FM_EX_KG_KEYSIZE_OVERFLOW);
            break;
        case e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PLCR_DOUBLE_ECC))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eifr, FM_PCD_PLCR_DOUBLE_ECC);
            break;
        case e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PLCR_INIT_ENTRY_ERROR))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eifr, FM_PCD_PLCR_INIT_ENTRY_ERROR);
            break;
        case e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PLCR_PRAM_SELF_INIT_COMPLETE))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ifr, FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE);
            break;
        case e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE:
            if (!(p_FmPcd->exceptions & FM_PCD_EX_PLCR_ATOMIC_ACTION_COMPLETE))
                RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("The selected exception is masked"));
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ifr, FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE);
            break;
    }

    return E_OK;
}


t_Error FM_PCD_ModifyCounter(t_Handle h_FmPcd, e_FmPcdCounters counter, uint32_t value)
{
    t_FmPcd            *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_ModifyCounter - guest mode!"));

    switch (counter)
    {
        case (e_FM_PCD_KG_COUNTERS_TOTAL):
            if (!p_FmPcd->p_FmPcdKg)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Invalid counters - KeyGen is not working"));
            break;
        case (e_FM_PCD_PLCR_COUNTERS_YELLOW):
        case (e_FM_PCD_PLCR_COUNTERS_RED):
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_RED):
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_YELLOW):
        case (e_FM_PCD_PLCR_COUNTERS_TOTAL):
        case (e_FM_PCD_PLCR_COUNTERS_LENGTH_MISMATCH):
            if (!p_FmPcd->p_FmPcdPlcr)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Invalid counters - Policer is not working"));
            if (!(GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_gcr) & FM_PCD_PLCR_GCR_STEN))
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Requested counter was not enabled"));
            break;
        case (e_FM_PCD_PRS_COUNTERS_PARSE_DISPATCH):
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED):
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED_WITH_ERR):
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_HARD_PRS_CYCLE_INCL_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_STALL_CYCLES):
        case (e_FM_PCD_PRS_COUNTERS_FPM_COMMAND_STALL_CYCLES):
            if (!p_FmPcd->p_FmPcdPrs)
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("Unsupported type of counter"));
            break;
        default:
            RETURN_ERROR(MINOR, E_INVALID_STATE, ("Unsupported type of counter"));
    }
    switch (counter)
    {
        case (e_FM_PCD_PRS_COUNTERS_PARSE_DISPATCH):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_pds, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l2rrs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l3rrs, value);
             break;
       case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l4rrs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_srrs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED_WITH_ERR):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l2rres, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED_WITH_ERR):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l3rres, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED_WITH_ERR):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_l4rres, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED_WITH_ERR):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_srres, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_spcs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_SOFT_PRS_STALL_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_spscs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_HARD_PRS_CYCLE_INCL_STALL_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_hxscs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mrcs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_MURAM_READ_STALL_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mrscs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mwcs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_STALL_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_mwscs, value);
            break;
        case (e_FM_PCD_PRS_COUNTERS_FPM_COMMAND_STALL_CYCLES):
               WRITE_UINT32(p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs->fmpr_fcscs, value);
             break;
        case (e_FM_PCD_KG_COUNTERS_TOTAL):
            WRITE_UINT32(p_FmPcd->p_FmPcdKg->p_FmPcdKgRegs->fmkg_tpc,value);
            break;

        /*Policer counters*/
        case (e_FM_PCD_PLCR_COUNTERS_YELLOW):
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ypcnt, value);
            break;
        case (e_FM_PCD_PLCR_COUNTERS_RED):
            WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rpcnt, value);
            break;
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_RED):
             WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rrpcnt, value);
            break;
        case (e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_YELLOW):
              WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_rypcnt, value);
            break;
        case (e_FM_PCD_PLCR_COUNTERS_TOTAL):
              WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_tpcnt, value);
            break;
        case (e_FM_PCD_PLCR_COUNTERS_LENGTH_MISMATCH):
              WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_flmcnt, value);
            break;
    }

    return E_OK;
}

t_Handle FM_PCD_GetHcPort(t_Handle h_FmPcd)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    return FmHcGetPort(p_FmPcd->h_Hc);
}

