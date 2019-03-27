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
 @File          fm_cc.c

 @Description   FM Coarse Classifier implementation
 *//***************************************************************************/
#include <sys/cdefs.h>
#include <sys/endian.h>
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "fm_pcd_ext.h"
#include "fm_muram_ext.h"

#include "fm_common.h"
#include "fm_pcd.h"
#include "fm_hc.h"
#include "fm_cc.h"
#include "crc64.h"

/****************************************/
/*       static functions               */
/****************************************/


static t_Error CcRootTryLock(t_Handle h_FmPcdCcTree)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;

    ASSERT_COND(h_FmPcdCcTree);

    if (FmPcdLockTryLock(p_FmPcdCcTree->p_Lock))
        return E_OK;

    return ERROR_CODE(E_BUSY);
}

static void CcRootReleaseLock(t_Handle h_FmPcdCcTree)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;

    ASSERT_COND(h_FmPcdCcTree);

    FmPcdLockUnlock(p_FmPcdCcTree->p_Lock);
}

static void UpdateNodeOwner(t_FmPcdCcNode *p_CcNode, bool add)
{
    uint32_t intFlags;

    ASSERT_COND(p_CcNode);

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    if (add)
        p_CcNode->owners++;
    else
    {
        ASSERT_COND(p_CcNode->owners);
        p_CcNode->owners--;
    }

    XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);
}

static __inline__ t_FmPcdStatsObj* DequeueStatsObj(t_List *p_List)
{
    t_FmPcdStatsObj *p_StatsObj = NULL;
    t_List *p_Next;

    if (!NCSW_LIST_IsEmpty(p_List))
    {
        p_Next = NCSW_LIST_FIRST(p_List);
        p_StatsObj = NCSW_LIST_OBJECT(p_Next, t_FmPcdStatsObj, node);
        ASSERT_COND(p_StatsObj);
        NCSW_LIST_DelAndInit(p_Next);
    }

    return p_StatsObj;
}

static __inline__ void EnqueueStatsObj(t_List *p_List,
                                       t_FmPcdStatsObj *p_StatsObj)
{
    NCSW_LIST_AddToTail(&p_StatsObj->node, p_List);
}

static void FreeStatObjects(t_List *p_List, t_Handle h_FmMuram)
{
    t_FmPcdStatsObj *p_StatsObj;

    while (!NCSW_LIST_IsEmpty(p_List))
    {
        p_StatsObj = DequeueStatsObj(p_List);
        ASSERT_COND(p_StatsObj);

        FM_MURAM_FreeMem(h_FmMuram, p_StatsObj->h_StatsAd);
        FM_MURAM_FreeMem(h_FmMuram, p_StatsObj->h_StatsCounters);

        XX_Free(p_StatsObj);
    }
}

static t_FmPcdStatsObj* GetStatsObj(t_FmPcdCcNode *p_CcNode)
{
    t_FmPcdStatsObj* p_StatsObj;
    t_Handle h_FmMuram;

    ASSERT_COND(p_CcNode);

    /* If 'maxNumOfKeys' was passed, all statistics object were preallocated
     upon node initialization */
    if (p_CcNode->maxNumOfKeys)
    {
        p_StatsObj = DequeueStatsObj(&p_CcNode->availableStatsLst);
    }
    else
    {
        h_FmMuram = ((t_FmPcd *)(p_CcNode->h_FmPcd))->h_FmMuram;
        ASSERT_COND(h_FmMuram);

        p_StatsObj = XX_Malloc(sizeof(t_FmPcdStatsObj));
        if (!p_StatsObj)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("statistics object"));
            return NULL;
        }

        p_StatsObj->h_StatsAd = (t_Handle)FM_MURAM_AllocMem(
                h_FmMuram, FM_PCD_CC_AD_ENTRY_SIZE, FM_PCD_CC_AD_TABLE_ALIGN);
        if (!p_StatsObj->h_StatsAd)
        {
            XX_Free(p_StatsObj);
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for statistics ADs"));
            return NULL;
        }
        MemSet8(p_StatsObj->h_StatsAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

        p_StatsObj->h_StatsCounters = (t_Handle)FM_MURAM_AllocMem(
                h_FmMuram, p_CcNode->countersArraySize,
                FM_PCD_CC_AD_TABLE_ALIGN);
        if (!p_StatsObj->h_StatsCounters)
        {
            FM_MURAM_FreeMem(h_FmMuram, p_StatsObj->h_StatsAd);
            XX_Free(p_StatsObj);
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for statistics counters"));
            return NULL;
        }
        MemSet8(p_StatsObj->h_StatsCounters, 0, p_CcNode->countersArraySize);
    }

    return p_StatsObj;
}

static void PutStatsObj(t_FmPcdCcNode *p_CcNode, t_FmPcdStatsObj *p_StatsObj)
{
    t_Handle h_FmMuram;

    ASSERT_COND(p_CcNode);
    ASSERT_COND(p_StatsObj);

    /* If 'maxNumOfKeys' was passed, all statistics object were preallocated
     upon node initialization and now will be enqueued back to the list */
    if (p_CcNode->maxNumOfKeys)
    {
        /* Nullify counters */
        MemSet8(p_StatsObj->h_StatsCounters, 0, p_CcNode->countersArraySize);

        EnqueueStatsObj(&p_CcNode->availableStatsLst, p_StatsObj);
    }
    else
    {
        h_FmMuram = ((t_FmPcd *)(p_CcNode->h_FmPcd))->h_FmMuram;
        ASSERT_COND(h_FmMuram);

        FM_MURAM_FreeMem(h_FmMuram, p_StatsObj->h_StatsAd);
        FM_MURAM_FreeMem(h_FmMuram, p_StatsObj->h_StatsCounters);

        XX_Free(p_StatsObj);
    }
}

static void SetStatsCounters(t_AdOfTypeStats *p_StatsAd,
                             uint32_t statsCountersAddr)
{
    uint32_t tmp = (statsCountersAddr & FM_PCD_AD_STATS_COUNTERS_ADDR_MASK);

    WRITE_UINT32(p_StatsAd->statsTableAddr, tmp);
}


static void UpdateStatsAd(t_FmPcdCcStatsParams *p_FmPcdCcStatsParams,
                          t_Handle h_Ad, uint64_t physicalMuramBase)
{
    t_AdOfTypeStats *p_StatsAd;
    uint32_t statsCountersAddr, nextActionAddr, tmp;
#if (DPAA_VERSION >= 11)
    uint32_t frameLengthRangesAddr;
#endif /* (DPAA_VERSION >= 11) */

    p_StatsAd = (t_AdOfTypeStats *)p_FmPcdCcStatsParams->h_StatsAd;

    tmp = FM_PCD_AD_STATS_TYPE;

#if (DPAA_VERSION >= 11)
    if (p_FmPcdCcStatsParams->h_StatsFLRs)
    {
        frameLengthRangesAddr = (uint32_t)((XX_VirtToPhys(
                p_FmPcdCcStatsParams->h_StatsFLRs) - physicalMuramBase));
        tmp |= (frameLengthRangesAddr & FM_PCD_AD_STATS_FLR_ADDR_MASK);
    }
#endif /* (DPAA_VERSION >= 11) */
    WRITE_UINT32(p_StatsAd->profileTableAddr, tmp);

    nextActionAddr = (uint32_t)((XX_VirtToPhys(h_Ad) - physicalMuramBase));
    tmp = 0;
    tmp |= (uint32_t)((nextActionAddr << FM_PCD_AD_STATS_NEXT_ACTION_SHIFT)
            & FM_PCD_AD_STATS_NEXT_ACTION_MASK);
    tmp |= (FM_PCD_AD_STATS_NAD_EN | FM_PCD_AD_STATS_OP_CODE);

#if (DPAA_VERSION >= 11)
    if (p_FmPcdCcStatsParams->h_StatsFLRs)
        tmp |= FM_PCD_AD_STATS_FLR_EN;
#endif /* (DPAA_VERSION >= 11) */

    WRITE_UINT32(p_StatsAd->nextActionIndx, tmp);

    statsCountersAddr = (uint32_t)((XX_VirtToPhys(
            p_FmPcdCcStatsParams->h_StatsCounters) - physicalMuramBase));
    SetStatsCounters(p_StatsAd, statsCountersAddr);
}

static void FillAdOfTypeContLookup(t_Handle h_Ad,
                                   t_FmPcdCcStatsParams *p_FmPcdCcStatsParams,
                                   t_Handle h_FmPcd, t_Handle p_CcNode,
                                   t_Handle h_Manip, t_Handle h_FrmReplic)
{
    t_FmPcdCcNode *p_Node = (t_FmPcdCcNode *)p_CcNode;
    t_AdOfTypeContLookup *p_AdContLookup = (t_AdOfTypeContLookup *)h_Ad;
    t_Handle h_TmpAd;
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t tmpReg32;
    t_Handle p_AdNewPtr = NULL;

    UNUSED(h_Manip);
    UNUSED(h_FrmReplic);

    /* there are 3 cases handled in this routine of building a "Continue lookup" type AD.
     * Case 1: No Manip. The action descriptor is built within the match table.
     *         p_AdResult = p_AdNewPtr;
     * Case 2: Manip exists. A new AD is created - p_AdNewPtr. It is initialized
     *         either in the FmPcdManipUpdateAdResultForCc routine or it was already
     *         initialized and returned here.
     *         p_AdResult (within the match table) will be initialized after
     *         this routine returns and point to the existing AD.
     * Case 3: Manip exists. The action descriptor is built within the match table.
     *         FmPcdManipUpdateAdContLookupForCc returns a NULL p_AdNewPtr.
     */

    /* As default, the "new" ptr is the current one. i.e. the content of the result
     * AD will be written into the match table itself (case (1))*/
    p_AdNewPtr = p_AdContLookup;

    /* Initialize an action descriptor, if current statistics mode requires an Ad */
    if (p_FmPcdCcStatsParams)
    {
        ASSERT_COND(p_FmPcdCcStatsParams->h_StatsAd);
        ASSERT_COND(p_FmPcdCcStatsParams->h_StatsCounters);

        /* Swapping addresses between statistics Ad and the current lookup AD */
        h_TmpAd = p_FmPcdCcStatsParams->h_StatsAd;
        p_FmPcdCcStatsParams->h_StatsAd = h_Ad;
        h_Ad = h_TmpAd;

        p_AdNewPtr = h_Ad;
        p_AdContLookup = h_Ad;

        /* Init statistics Ad and connect current lookup AD as 'next action' from statistics Ad */
        UpdateStatsAd(p_FmPcdCcStatsParams, h_Ad, p_FmPcd->physicalMuramBase);
    }

#if DPAA_VERSION >= 11
    if (h_Manip && h_FrmReplic)
        FmPcdManipUpdateAdContLookupForCc(
                h_Manip,
                h_Ad,
                &p_AdNewPtr,
                (uint32_t)((XX_VirtToPhys(
                        FrmReplicGroupGetSourceTableDescriptor(h_FrmReplic))
                        - p_FmPcd->physicalMuramBase)));
    else
        if (h_FrmReplic)
            FrmReplicGroupUpdateAd(h_FrmReplic, h_Ad, &p_AdNewPtr);
        else
#endif /* (DPAA_VERSION >= 11) */
            if (h_Manip)
                FmPcdManipUpdateAdContLookupForCc(
                        h_Manip,
                        h_Ad,
                        &p_AdNewPtr,

#ifdef FM_CAPWAP_SUPPORT
                        /*no check for opcode of manip - this step can be reached only with capwap_applic_specific*/
                        (uint32_t)((XX_VirtToPhys(p_Node->h_AdTable) - p_FmPcd->physicalMuramBase))
#else  /* not FM_CAPWAP_SUPPORT */
                        (uint32_t)((XX_VirtToPhys(p_Node->h_Ad)
                                - p_FmPcd->physicalMuramBase))
#endif /* not FM_CAPWAP_SUPPORT */
                        );

    /* if (p_AdNewPtr = NULL) --> Done. (case (3)) */
    if (p_AdNewPtr)
    {
        /* cases (1) & (2) */
        tmpReg32 = 0;
        tmpReg32 |= FM_PCD_AD_CONT_LOOKUP_TYPE;
        tmpReg32 |=
                p_Node->sizeOfExtraction ? ((p_Node->sizeOfExtraction - 1) << 24) :
                        0;
        tmpReg32 |= (uint32_t)(XX_VirtToPhys(p_Node->h_AdTable)
                - p_FmPcd->physicalMuramBase);
        WRITE_UINT32(p_AdContLookup->ccAdBase, tmpReg32);

        tmpReg32 = 0;
        tmpReg32 |= p_Node->numOfKeys << 24;
        tmpReg32 |= (p_Node->lclMask ? FM_PCD_AD_CONT_LOOKUP_LCL_MASK : 0);
        tmpReg32 |=
                p_Node->h_KeysMatchTable ? (uint32_t)(XX_VirtToPhys(
                        p_Node->h_KeysMatchTable) - p_FmPcd->physicalMuramBase) :
                        0;
        WRITE_UINT32(p_AdContLookup->matchTblPtr, tmpReg32);

        tmpReg32 = 0;
        tmpReg32 |= p_Node->prsArrayOffset << 24;
        tmpReg32 |= p_Node->offset << 16;
        tmpReg32 |= p_Node->parseCode;
        WRITE_UINT32(p_AdContLookup->pcAndOffsets, tmpReg32);

        MemCpy8((void*)&p_AdContLookup->gmask, p_Node->p_GlblMask,
                    CC_GLBL_MASK_SIZE);
    }
}

static t_Error AllocAndFillAdForContLookupManip(t_Handle h_CcNode)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint32_t intFlags;

    ASSERT_COND(p_CcNode);

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    if (!p_CcNode->h_Ad)
    {
        if (p_CcNode->maxNumOfKeys)
            p_CcNode->h_Ad = p_CcNode->h_TmpAd;
        else
            p_CcNode->h_Ad = (t_Handle)FM_MURAM_AllocMem(
                    ((t_FmPcd *)(p_CcNode->h_FmPcd))->h_FmMuram,
                    FM_PCD_CC_AD_ENTRY_SIZE, FM_PCD_CC_AD_TABLE_ALIGN);

        XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

        if (!p_CcNode->h_Ad)
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for CC action descriptor"));

        MemSet8(p_CcNode->h_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);

        FillAdOfTypeContLookup(p_CcNode->h_Ad, NULL, p_CcNode->h_FmPcd,
                               p_CcNode, NULL, NULL);
    }
    else
        XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

    return E_OK;
}

static t_Error SetRequiredAction1(
        t_Handle h_FmPcd, uint32_t requiredAction,
        t_FmPcdCcKeyAndNextEngineParams *p_CcKeyAndNextEngineParamsTmp,
        t_Handle h_AdTmp, uint16_t numOfEntries, t_Handle h_Tree)
{
    t_AdOfTypeResult *p_AdTmp = (t_AdOfTypeResult *)h_AdTmp;
    uint32_t tmpReg32;
    t_Error err;
    t_FmPcdCcNode *p_CcNode;
    int i = 0;
    uint16_t tmp = 0;
    uint16_t profileId;
    uint8_t relativeSchemeId, physicalSchemeId;
    t_CcNodeInformation ccNodeInfo;

    for (i = 0; i < numOfEntries; i++)
    {
        if (i == 0)
            h_AdTmp = PTR_MOVE(h_AdTmp, i*FM_PCD_CC_AD_ENTRY_SIZE);
        else
            h_AdTmp = PTR_MOVE(h_AdTmp, FM_PCD_CC_AD_ENTRY_SIZE);

        switch (p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.nextEngine)
        {
            case (e_FM_PCD_CC):
                if (requiredAction)
                {
                    p_CcNode =
                            p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.ccParams.h_CcNode;
                    ASSERT_COND(p_CcNode);
                    if (p_CcNode->shadowAction == requiredAction)
                        break;
                    if ((requiredAction & UPDATE_CC_WITH_TREE)
                            && !(p_CcNode->shadowAction & UPDATE_CC_WITH_TREE))
                    {

                        memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                        ccNodeInfo.h_CcNode = h_Tree;
                        EnqueueNodeInfoToRelevantLst(&p_CcNode->ccTreesLst,
                                                     &ccNodeInfo, NULL);
                        p_CcKeyAndNextEngineParamsTmp[i].shadowAction |=
                                UPDATE_CC_WITH_TREE;
                    }
                    if ((requiredAction & UPDATE_CC_SHADOW_CLEAR)
                            && !(p_CcNode->shadowAction & UPDATE_CC_SHADOW_CLEAR))
                    {

                        p_CcNode->shadowAction = 0;
                    }

                    if ((requiredAction & UPDATE_CC_WITH_DELETE_TREE)
                            && !(p_CcNode->shadowAction
                                    & UPDATE_CC_WITH_DELETE_TREE))
                    {
                        DequeueNodeInfoFromRelevantLst(&p_CcNode->ccTreesLst,
                                                       h_Tree, NULL);
                        p_CcKeyAndNextEngineParamsTmp[i].shadowAction |=
                                UPDATE_CC_WITH_DELETE_TREE;
                    }
                    if (p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.nextEngine
                            != e_FM_PCD_INVALID)
                        tmp = (uint8_t)(p_CcNode->numOfKeys + 1);
                    else
                        tmp = p_CcNode->numOfKeys;
                    err = SetRequiredAction1(h_FmPcd, requiredAction,
                                             p_CcNode->keyAndNextEngineParams,
                                             p_CcNode->h_AdTable, tmp, h_Tree);
                    if (err != E_OK)
                        return err;
                    if (requiredAction != UPDATE_CC_SHADOW_CLEAR)
                        p_CcNode->shadowAction |= requiredAction;
                }
                break;

            case (e_FM_PCD_KG):
                if ((requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA)
                        && !(p_CcKeyAndNextEngineParamsTmp[i].shadowAction
                                & UPDATE_NIA_ENQ_WITHOUT_DMA))
                {
                    physicalSchemeId =
                            FmPcdKgGetSchemeId(
                                    p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.kgParams.h_DirectScheme);
                    relativeSchemeId = FmPcdKgGetRelativeSchemeId(
                            h_FmPcd, physicalSchemeId);
                    if (relativeSchemeId == FM_PCD_KG_NUM_OF_SCHEMES)
                        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);
                    if (!FmPcdKgIsSchemeValidSw(
                            p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.kgParams.h_DirectScheme))
                        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                                     ("Invalid direct scheme."));
                    if (!KgIsSchemeAlwaysDirect(h_FmPcd, relativeSchemeId))
                        RETURN_ERROR(
                                MAJOR, E_INVALID_STATE,
                                ("For this action scheme has to be direct."));
                    err =
                            FmPcdKgCcGetSetParams(
                                    h_FmPcd,
                                    p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.kgParams.h_DirectScheme,
                                    requiredAction, 0);
                    if (err != E_OK)
                        RETURN_ERROR(MAJOR, err, NO_MSG);
                    p_CcKeyAndNextEngineParamsTmp[i].shadowAction |=
                            requiredAction;
                }
                break;

            case (e_FM_PCD_PLCR):
                if ((requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA)
                        && !(p_CcKeyAndNextEngineParamsTmp[i].shadowAction
                                & UPDATE_NIA_ENQ_WITHOUT_DMA))
                {
                    if (!p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.plcrParams.overrideParams)
                        RETURN_ERROR(
                                MAJOR,
                                E_NOT_SUPPORTED,
                                ("In this initialization only overrideFqid can be initialized"));
                    if (!p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.plcrParams.sharedProfile)
                        RETURN_ERROR(
                                MAJOR,
                                E_NOT_SUPPORTED,
                                ("In this initialization only overrideFqid can be initialized"));
                    err =
                            FmPcdPlcrGetAbsoluteIdByProfileParams(
                                    h_FmPcd,
                                    e_FM_PCD_PLCR_SHARED,
                                    NULL,
                                    p_CcKeyAndNextEngineParamsTmp[i].nextEngineParams.params.plcrParams.newRelativeProfileId,
                                    &profileId);
                    if (err != E_OK)
                        RETURN_ERROR(MAJOR, err, NO_MSG);
                    err = FmPcdPlcrCcGetSetParams(h_FmPcd, profileId,
                                                  requiredAction);
                    if (err != E_OK)
                        RETURN_ERROR(MAJOR, err, NO_MSG);
                    p_CcKeyAndNextEngineParamsTmp[i].shadowAction |=
                            requiredAction;
                }
                break;

            case (e_FM_PCD_DONE):
                if ((requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA)
                        && !(p_CcKeyAndNextEngineParamsTmp[i].shadowAction
                                & UPDATE_NIA_ENQ_WITHOUT_DMA))
                {
                    tmpReg32 = GET_UINT32(p_AdTmp->nia);
                    if ((tmpReg32 & GET_NIA_BMI_AC_ENQ_FRAME(h_FmPcd))
                            != GET_NIA_BMI_AC_ENQ_FRAME(h_FmPcd))
                        RETURN_ERROR(
                                MAJOR,
                                E_INVALID_STATE,
                                ("Next engine was previously assigned not as PCD_DONE"));
                    tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;
                    WRITE_UINT32(p_AdTmp->nia, tmpReg32);
                    p_CcKeyAndNextEngineParamsTmp[i].shadowAction |=
                            requiredAction;
                }
                break;

            default:
                break;
        }
    }

    return E_OK;
}

static t_Error SetRequiredAction(
        t_Handle h_FmPcd, uint32_t requiredAction,
        t_FmPcdCcKeyAndNextEngineParams *p_CcKeyAndNextEngineParamsTmp,
        t_Handle h_AdTmp, uint16_t numOfEntries, t_Handle h_Tree)
{
    t_Error err = SetRequiredAction1(h_FmPcd, requiredAction,
                                     p_CcKeyAndNextEngineParamsTmp, h_AdTmp,
                                     numOfEntries, h_Tree);
    if (err != E_OK)
        return err;
    return SetRequiredAction1(h_FmPcd, UPDATE_CC_SHADOW_CLEAR,
                              p_CcKeyAndNextEngineParamsTmp, h_AdTmp,
                              numOfEntries, h_Tree);
}

static t_Error ReleaseModifiedDataStructure(
        t_Handle h_FmPcd, t_List *h_FmPcdOldPointersLst,
        t_List *h_FmPcdNewPointersLst,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalParams,
        bool useShadowStructs)
{
    t_List *p_Pos;
    t_Error err = E_OK;
    t_CcNodeInformation ccNodeInfo, *p_CcNodeInformation;
    t_Handle h_Muram;
    t_FmPcdCcNode *p_FmPcdCcNextNode, *p_FmPcdCcWorkingOnNode;
    t_List *p_UpdateLst;
    uint32_t intFlags;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_AdditionalParams->h_CurrentNode,
                              E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcdOldPointersLst, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcdNewPointersLst, E_INVALID_HANDLE);

    /* We don't update subtree of the new node with new tree because it was done in the previous stage */
    if (p_AdditionalParams->h_NodeForAdd)
    {
        p_FmPcdCcNextNode = (t_FmPcdCcNode*)p_AdditionalParams->h_NodeForAdd;

        if (!p_AdditionalParams->tree)
            p_UpdateLst = &p_FmPcdCcNextNode->ccPrevNodesLst;
        else
            p_UpdateLst = &p_FmPcdCcNextNode->ccTreeIdLst;

        p_CcNodeInformation = FindNodeInfoInReleventLst(
                p_UpdateLst, p_AdditionalParams->h_CurrentNode,
                p_FmPcdCcNextNode->h_Spinlock);

        if (p_CcNodeInformation)
            p_CcNodeInformation->index++;
        else
        {
            memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
            ccNodeInfo.h_CcNode = (t_Handle)p_AdditionalParams->h_CurrentNode;
            ccNodeInfo.index = 1;
            EnqueueNodeInfoToRelevantLst(p_UpdateLst, &ccNodeInfo,
                                         p_FmPcdCcNextNode->h_Spinlock);
        }
        if (p_AdditionalParams->h_ManipForAdd)
        {
            p_CcNodeInformation = FindNodeInfoInReleventLst(
                    FmPcdManipGetNodeLstPointedOnThisManip(
                            p_AdditionalParams->h_ManipForAdd),
                    p_AdditionalParams->h_CurrentNode,
                    FmPcdManipGetSpinlock(p_AdditionalParams->h_ManipForAdd));

            if (p_CcNodeInformation)
                p_CcNodeInformation->index++;
            else
            {
                memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                ccNodeInfo.h_CcNode =
                        (t_Handle)p_AdditionalParams->h_CurrentNode;
                ccNodeInfo.index = 1;
                EnqueueNodeInfoToRelevantLst(
                        FmPcdManipGetNodeLstPointedOnThisManip(
                                p_AdditionalParams->h_ManipForAdd),
                        &ccNodeInfo,
                        FmPcdManipGetSpinlock(
                                p_AdditionalParams->h_ManipForAdd));
            }
        }
    }

    if (p_AdditionalParams->h_NodeForRmv)
    {
        p_FmPcdCcNextNode = (t_FmPcdCcNode*)p_AdditionalParams->h_NodeForRmv;

        if (!p_AdditionalParams->tree)
        {
            p_UpdateLst = &p_FmPcdCcNextNode->ccPrevNodesLst;
            p_FmPcdCcWorkingOnNode =
                    (t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode);

            for (p_Pos = NCSW_LIST_FIRST(&p_FmPcdCcWorkingOnNode->ccTreesLst);
                    p_Pos != (&p_FmPcdCcWorkingOnNode->ccTreesLst); p_Pos =
                            NCSW_LIST_NEXT(p_Pos))
            {
                p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);

                ASSERT_COND(p_CcNodeInformation->h_CcNode);

                err =
                        SetRequiredAction(
                                h_FmPcd,
                                UPDATE_CC_WITH_DELETE_TREE,
                                &((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->keyAndNextEngineParams[p_AdditionalParams->savedKeyIndex],
                                PTR_MOVE(((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->h_AdTable, p_AdditionalParams->savedKeyIndex*FM_PCD_CC_AD_ENTRY_SIZE),
                                1, p_CcNodeInformation->h_CcNode);
            }
        }
        else
        {
            p_UpdateLst = &p_FmPcdCcNextNode->ccTreeIdLst;

            err =
                    SetRequiredAction(
                            h_FmPcd,
                            UPDATE_CC_WITH_DELETE_TREE,
                            &((t_FmPcdCcTree *)(p_AdditionalParams->h_CurrentNode))->keyAndNextEngineParams[p_AdditionalParams->savedKeyIndex],
                            UINT_TO_PTR(((t_FmPcdCcTree *)(p_AdditionalParams->h_CurrentNode))->ccTreeBaseAddr + p_AdditionalParams->savedKeyIndex*FM_PCD_CC_AD_ENTRY_SIZE),
                            1, p_AdditionalParams->h_CurrentNode);
        }
        if (err)
            return err;

        /* We remove from the subtree of the removed node tree because it wasn't done in the previous stage
         Update ccPrevNodesLst or ccTreeIdLst of the removed node
         Update of the node owner */
        p_CcNodeInformation = FindNodeInfoInReleventLst(
                p_UpdateLst, p_AdditionalParams->h_CurrentNode,
                p_FmPcdCcNextNode->h_Spinlock);

        ASSERT_COND(p_CcNodeInformation);
        ASSERT_COND(p_CcNodeInformation->index);

        p_CcNodeInformation->index--;

        if (p_CcNodeInformation->index == 0)
            DequeueNodeInfoFromRelevantLst(p_UpdateLst,
                                           p_AdditionalParams->h_CurrentNode,
                                           p_FmPcdCcNextNode->h_Spinlock);

        UpdateNodeOwner(p_FmPcdCcNextNode, FALSE);

        if (p_AdditionalParams->h_ManipForRmv)
        {
            p_CcNodeInformation = FindNodeInfoInReleventLst(
                    FmPcdManipGetNodeLstPointedOnThisManip(
                            p_AdditionalParams->h_ManipForRmv),
                    p_AdditionalParams->h_CurrentNode,
                    FmPcdManipGetSpinlock(p_AdditionalParams->h_ManipForRmv));

            ASSERT_COND(p_CcNodeInformation);
            ASSERT_COND(p_CcNodeInformation->index);

            p_CcNodeInformation->index--;

            if (p_CcNodeInformation->index == 0)
                DequeueNodeInfoFromRelevantLst(
                        FmPcdManipGetNodeLstPointedOnThisManip(
                                p_AdditionalParams->h_ManipForRmv),
                        p_AdditionalParams->h_CurrentNode,
                        FmPcdManipGetSpinlock(
                                p_AdditionalParams->h_ManipForRmv));
        }
    }

    if (p_AdditionalParams->h_ManipForRmv)
        FmPcdManipUpdateOwner(p_AdditionalParams->h_ManipForRmv, FALSE);

    if (p_AdditionalParams->p_StatsObjForRmv)
        PutStatsObj((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode),
                    p_AdditionalParams->p_StatsObjForRmv);

#if (DPAA_VERSION >= 11)
    if (p_AdditionalParams->h_FrmReplicForRmv)
        FrmReplicGroupUpdateOwner(p_AdditionalParams->h_FrmReplicForRmv,
                                  FALSE/* remove */);
#endif /* (DPAA_VERSION >= 11) */

    if (!useShadowStructs)
    {
        h_Muram = FmPcdGetMuramHandle(h_FmPcd);
        ASSERT_COND(h_Muram);

        if ((p_AdditionalParams->tree && !((t_FmPcd *)h_FmPcd)->p_CcShadow)
                || (!p_AdditionalParams->tree
                        && !((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->maxNumOfKeys))
        {
            /* We release new AD which was allocated and updated for copy from to actual AD */
            for (p_Pos = NCSW_LIST_FIRST(h_FmPcdNewPointersLst);
                    p_Pos != (h_FmPcdNewPointersLst); p_Pos = NCSW_LIST_NEXT(p_Pos))
            {

                p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);
                ASSERT_COND(p_CcNodeInformation->h_CcNode);
                FM_MURAM_FreeMem(h_Muram, p_CcNodeInformation->h_CcNode);
            }
        }

        /* Free Old data structure if it has to be freed - new data structure was allocated*/
        if (p_AdditionalParams->p_AdTableOld)
            FM_MURAM_FreeMem(h_Muram, p_AdditionalParams->p_AdTableOld);

        if (p_AdditionalParams->p_KeysMatchTableOld)
            FM_MURAM_FreeMem(h_Muram, p_AdditionalParams->p_KeysMatchTableOld);
    }

    /* Update current modified node with changed fields if it's required*/
    if (!p_AdditionalParams->tree)
    {
        if (p_AdditionalParams->p_AdTableNew)
            ((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->h_AdTable =
                    p_AdditionalParams->p_AdTableNew;

        if (p_AdditionalParams->p_KeysMatchTableNew)
            ((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->h_KeysMatchTable =
                    p_AdditionalParams->p_KeysMatchTableNew;

        /* Locking node's spinlock before updating 'keys and next engine' structure,
         as it maybe used to retrieve keys statistics */
        intFlags =
                XX_LockIntrSpinlock(
                        ((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->h_Spinlock);

        ((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->numOfKeys =
                p_AdditionalParams->numOfKeys;

        memcpy(((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->keyAndNextEngineParams,
               &p_AdditionalParams->keyAndNextEngineParams,
               sizeof(t_FmPcdCcKeyAndNextEngineParams) * (CC_MAX_NUM_OF_KEYS));

        XX_UnlockIntrSpinlock(
                ((t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode))->h_Spinlock,
                intFlags);
    }
    else
    {
        uint8_t numEntries =
                ((t_FmPcdCcTree *)(p_AdditionalParams->h_CurrentNode))->numOfEntries;
        ASSERT_COND(numEntries < FM_PCD_MAX_NUM_OF_CC_GROUPS);
        memcpy(&((t_FmPcdCcTree *)(p_AdditionalParams->h_CurrentNode))->keyAndNextEngineParams,
               &p_AdditionalParams->keyAndNextEngineParams,
               sizeof(t_FmPcdCcKeyAndNextEngineParams) * numEntries);
    }

    ReleaseLst(h_FmPcdOldPointersLst);
    ReleaseLst(h_FmPcdNewPointersLst);

    XX_Free(p_AdditionalParams);

    return E_OK;
}

static t_Handle BuildNewAd(
        t_Handle h_Ad,
        t_FmPcdModifyCcKeyAdditionalParams *p_FmPcdModifyCcKeyAdditionalParams,
        t_FmPcdCcNode *p_CcNode,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_FmPcdCcNodeTmp;
    t_Handle h_OrigAd = NULL;

    p_FmPcdCcNodeTmp = (t_FmPcdCcNode*)XX_Malloc(sizeof(t_FmPcdCcNode));
    if (!p_FmPcdCcNodeTmp)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("p_FmPcdCcNodeTmp"));
        return NULL;
    }
    memset(p_FmPcdCcNodeTmp, 0, sizeof(t_FmPcdCcNode));

    p_FmPcdCcNodeTmp->numOfKeys = p_FmPcdModifyCcKeyAdditionalParams->numOfKeys;
    p_FmPcdCcNodeTmp->h_KeysMatchTable =
            p_FmPcdModifyCcKeyAdditionalParams->p_KeysMatchTableNew;
    p_FmPcdCcNodeTmp->h_AdTable =
            p_FmPcdModifyCcKeyAdditionalParams->p_AdTableNew;

    p_FmPcdCcNodeTmp->lclMask = p_CcNode->lclMask;
    p_FmPcdCcNodeTmp->parseCode = p_CcNode->parseCode;
    p_FmPcdCcNodeTmp->offset = p_CcNode->offset;
    p_FmPcdCcNodeTmp->prsArrayOffset = p_CcNode->prsArrayOffset;
    p_FmPcdCcNodeTmp->ctrlFlow = p_CcNode->ctrlFlow;
    p_FmPcdCcNodeTmp->ccKeySizeAccExtraction = p_CcNode->ccKeySizeAccExtraction;
    p_FmPcdCcNodeTmp->sizeOfExtraction = p_CcNode->sizeOfExtraction;
    p_FmPcdCcNodeTmp->glblMaskSize = p_CcNode->glblMaskSize;
    p_FmPcdCcNodeTmp->p_GlblMask = p_CcNode->p_GlblMask;

    if (p_FmPcdCcNextEngineParams->nextEngine == e_FM_PCD_CC)
    {
        if (p_FmPcdCcNextEngineParams->h_Manip)
        {
            h_OrigAd = p_CcNode->h_Ad;
            if (AllocAndFillAdForContLookupManip(
                    p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode)
                    != E_OK)
            {
                REPORT_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
                XX_Free(p_FmPcdCcNodeTmp);
                return NULL;
            }
        }
        FillAdOfTypeContLookup(h_Ad, NULL, p_CcNode->h_FmPcd, p_FmPcdCcNodeTmp,
                               h_OrigAd ? NULL : p_FmPcdCcNextEngineParams->h_Manip, NULL);
    }

#if (DPAA_VERSION >= 11)
    if ((p_FmPcdCcNextEngineParams->nextEngine == e_FM_PCD_FR)
            && (p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic))
    {
        FillAdOfTypeContLookup(
                h_Ad, NULL, p_CcNode->h_FmPcd, p_FmPcdCcNodeTmp,
                p_FmPcdCcNextEngineParams->h_Manip,
                p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic);
    }
#endif /* (DPAA_VERSION >= 11) */

    XX_Free(p_FmPcdCcNodeTmp);

    return E_OK;
}

static t_Error DynamicChangeHc(
        t_Handle h_FmPcd, t_List *h_OldPointersLst, t_List *h_NewPointersLst,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalParams,
        bool useShadowStructs)
{
    t_List *p_PosOld, *p_PosNew;
    uint32_t oldAdAddrOffset, newAdAddrOffset;
    uint16_t i = 0;
    t_Error err = E_OK;
    uint8_t numOfModifiedPtr;

    ASSERT_COND(h_FmPcd);
    ASSERT_COND(h_OldPointersLst);
    ASSERT_COND(h_NewPointersLst);

    numOfModifiedPtr = (uint8_t)NCSW_LIST_NumOfObjs(h_OldPointersLst);

    if (numOfModifiedPtr)
    {
        p_PosNew = NCSW_LIST_FIRST(h_NewPointersLst);
        p_PosOld = NCSW_LIST_FIRST(h_OldPointersLst);

        /* Retrieve address of new AD */
        newAdAddrOffset = FmPcdCcGetNodeAddrOffsetFromNodeInfo(h_FmPcd,
                                                               p_PosNew);
        if (newAdAddrOffset == (uint32_t)ILLEGAL_BASE)
        {
            ReleaseModifiedDataStructure(h_FmPcd, h_OldPointersLst,
                                         h_NewPointersLst,
                                         p_AdditionalParams, useShadowStructs);
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("New AD address"));
        }

        for (i = 0; i < numOfModifiedPtr; i++)
        {
            /* Retrieve address of current AD */
            oldAdAddrOffset = FmPcdCcGetNodeAddrOffsetFromNodeInfo(h_FmPcd,
                                                                   p_PosOld);
            if (oldAdAddrOffset == (uint32_t)ILLEGAL_BASE)
            {
                ReleaseModifiedDataStructure(h_FmPcd, h_OldPointersLst,
                                             h_NewPointersLst,
                                             p_AdditionalParams,
                                             useShadowStructs);
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Old AD address"));
            }

            /* Invoke host command to copy from new AD to old AD */
            err = FmHcPcdCcDoDynamicChange(((t_FmPcd *)h_FmPcd)->h_Hc,
                                           oldAdAddrOffset, newAdAddrOffset);
            if (err)
            {
                ReleaseModifiedDataStructure(h_FmPcd, h_OldPointersLst,
                                             h_NewPointersLst,
                                             p_AdditionalParams,
                                             useShadowStructs);
                RETURN_ERROR(
                        MAJOR,
                        err,
                        ("For part of nodes changes are done - situation is danger"));
            }

            p_PosOld = NCSW_LIST_NEXT(p_PosOld);
        }
    }
    return E_OK;
}

static t_Error DoDynamicChange(
        t_Handle h_FmPcd, t_List *h_OldPointersLst, t_List *h_NewPointersLst,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalParams,
        bool useShadowStructs)
{
    t_FmPcdCcNode *p_CcNode =
            (t_FmPcdCcNode *)(p_AdditionalParams->h_CurrentNode);
    t_List *p_PosNew;
    t_CcNodeInformation *p_CcNodeInfo;
    t_FmPcdCcNextEngineParams nextEngineParams;
    t_Handle h_Ad;
    uint32_t keySize;
    t_Error err = E_OK;
    uint8_t numOfModifiedPtr;

    ASSERT_COND(h_FmPcd);

    memset(&nextEngineParams, 0, sizeof(t_FmPcdCcNextEngineParams));

    numOfModifiedPtr = (uint8_t)NCSW_LIST_NumOfObjs(h_OldPointersLst);

    if (numOfModifiedPtr)
    {

        p_PosNew = NCSW_LIST_FIRST(h_NewPointersLst);

        /* Invoke host-command to copy from the new Ad to existing Ads */
        err = DynamicChangeHc(h_FmPcd, h_OldPointersLst, h_NewPointersLst,
                              p_AdditionalParams, useShadowStructs);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);

		if (useShadowStructs)
		{
			/* When the host-command above has ended, the old structures are 'free'and we can update
			 them by copying from the new shadow structures. */
			if (p_CcNode->lclMask)
				keySize = (uint32_t)(2 * p_CcNode->ccKeySizeAccExtraction);
			else
				keySize = p_CcNode->ccKeySizeAccExtraction;

			MemCpy8(p_AdditionalParams->p_KeysMatchTableOld,
					   p_AdditionalParams->p_KeysMatchTableNew,
					   p_CcNode->maxNumOfKeys * keySize * sizeof(uint8_t));

			MemCpy8(
					p_AdditionalParams->p_AdTableOld,
					p_AdditionalParams->p_AdTableNew,
					(uint32_t)((p_CcNode->maxNumOfKeys + 1)
							* FM_PCD_CC_AD_ENTRY_SIZE));

			/* Retrieve the address of the allocated Ad */
			p_CcNodeInfo = CC_NODE_F_OBJECT(p_PosNew);
			h_Ad = p_CcNodeInfo->h_CcNode;

			/* Build a new Ad that holds the old (now updated) structures */
			p_AdditionalParams->p_KeysMatchTableNew =
					p_AdditionalParams->p_KeysMatchTableOld;
			p_AdditionalParams->p_AdTableNew = p_AdditionalParams->p_AdTableOld;

			nextEngineParams.nextEngine = e_FM_PCD_CC;
			nextEngineParams.params.ccParams.h_CcNode = (t_Handle)p_CcNode;

			BuildNewAd(h_Ad, p_AdditionalParams, p_CcNode, &nextEngineParams);

			/* HC to copy from the new Ad (old updated structures) to current Ad (uses shadow structures) */
			err = DynamicChangeHc(h_FmPcd, h_OldPointersLst, h_NewPointersLst,
								  p_AdditionalParams, useShadowStructs);
			if (err)
				RETURN_ERROR(MAJOR, err, NO_MSG);
		}
    }

    err = ReleaseModifiedDataStructure(h_FmPcd, h_OldPointersLst,
                                       h_NewPointersLst,
                                       p_AdditionalParams, useShadowStructs);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

#ifdef FM_CAPWAP_SUPPORT
static bool IsCapwapApplSpecific(t_Handle h_Node)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_Node;
    bool isManipForCapwapApplSpecificBuild = FALSE;
    int i = 0;

    ASSERT_COND(h_Node);
    /* assumption that this function called only for INDEXED_FLOW_ID - so no miss*/
    for (i = 0; i < p_CcNode->numOfKeys; i++)
    {
        if ( p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip &&
                FmPcdManipIsCapwapApplSpecific(p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip))
        {
            isManipForCapwapApplSpecificBuild = TRUE;
            break;
        }
    }
    return isManipForCapwapApplSpecificBuild;

}
#endif /* FM_CAPWAP_SUPPORT */

static t_Error CcUpdateParam(
        t_Handle h_FmPcd, t_Handle h_PcdParams, t_Handle h_FmPort,
        t_FmPcdCcKeyAndNextEngineParams *p_CcKeyAndNextEngineParams,
        uint16_t numOfEntries, t_Handle h_Ad, bool validate, uint16_t level,
        t_Handle h_FmTree, bool modify)
{
    t_FmPcdCcNode *p_CcNode;
    t_Error err;
    uint16_t tmp = 0;
    int i = 0;
    t_FmPcdCcTree *p_CcTree = (t_FmPcdCcTree *)h_FmTree;

    level++;

    if (p_CcTree->h_IpReassemblyManip)
    {
        err = FmPcdManipUpdate(h_FmPcd, h_PcdParams, h_FmPort,
                               p_CcTree->h_IpReassemblyManip, NULL, validate,
                               level, h_FmTree, modify);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_CcTree->h_CapwapReassemblyManip)
    {
        err = FmPcdManipUpdate(h_FmPcd, h_PcdParams, h_FmPort,
                               p_CcTree->h_CapwapReassemblyManip, NULL, validate,
                               level, h_FmTree, modify);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (numOfEntries)
    {
        for (i = 0; i < numOfEntries; i++)
        {
            if (i == 0)
                h_Ad = PTR_MOVE(h_Ad, i*FM_PCD_CC_AD_ENTRY_SIZE);
            else
                h_Ad = PTR_MOVE(h_Ad, FM_PCD_CC_AD_ENTRY_SIZE);

            if (p_CcKeyAndNextEngineParams[i].nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
            {
                p_CcNode =
                        p_CcKeyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode;
                ASSERT_COND(p_CcNode);

                if (p_CcKeyAndNextEngineParams[i].nextEngineParams.h_Manip)
                {
                    err =
                            FmPcdManipUpdate(
                                    h_FmPcd,
                                    NULL,
                                    h_FmPort,
                                    p_CcKeyAndNextEngineParams[i].nextEngineParams.h_Manip,
                                    h_Ad, validate, level, h_FmTree, modify);
                    if (err)
                        RETURN_ERROR(MAJOR, err, NO_MSG);
                }

                if (p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.nextEngine
                        != e_FM_PCD_INVALID)
                    tmp = (uint8_t)(p_CcNode->numOfKeys + 1);
                else
                    tmp = p_CcNode->numOfKeys;

                err = CcUpdateParam(h_FmPcd, h_PcdParams, h_FmPort,
                                    p_CcNode->keyAndNextEngineParams, tmp,
                                    p_CcNode->h_AdTable, validate, level,
                                    h_FmTree, modify);
                if (err)
                    RETURN_ERROR(MAJOR, err, NO_MSG);
            }
            else
            {
                if (p_CcKeyAndNextEngineParams[i].nextEngineParams.h_Manip)
                {
                    err =
                            FmPcdManipUpdate(
                                    h_FmPcd,
                                    NULL,
                                    h_FmPort,
                                    p_CcKeyAndNextEngineParams[i].nextEngineParams.h_Manip,
                                    h_Ad, validate, level, h_FmTree, modify);
                    if (err)
                        RETURN_ERROR(MAJOR, err, NO_MSG);
                }
            }
        }
    }

    return E_OK;
}

static ccPrivateInfo_t IcDefineCode(t_FmPcdCcNodeParams *p_CcNodeParam)
{
    switch (p_CcNodeParam->extractCcParams.extractNonHdr.action)
    {
        case (e_FM_PCD_ACTION_EXACT_MATCH):
            switch (p_CcNodeParam->extractCcParams.extractNonHdr.src)
            {
                case (e_FM_PCD_EXTRACT_FROM_KEY):
                    return CC_PRIVATE_INFO_IC_KEY_EXACT_MATCH;
                case (e_FM_PCD_EXTRACT_FROM_HASH):
                    return CC_PRIVATE_INFO_IC_HASH_EXACT_MATCH;
                default:
                    return CC_PRIVATE_INFO_NONE;
            }

        case (e_FM_PCD_ACTION_INDEXED_LOOKUP):
            switch (p_CcNodeParam->extractCcParams.extractNonHdr.src)
            {
                case (e_FM_PCD_EXTRACT_FROM_HASH):
                    return CC_PRIVATE_INFO_IC_HASH_INDEX_LOOKUP;
                case (e_FM_PCD_EXTRACT_FROM_FLOW_ID):
                    return CC_PRIVATE_INFO_IC_DEQ_FQID_INDEX_LOOKUP;
                default:
                    return CC_PRIVATE_INFO_NONE;
            }

        default:
            break;
    }

    return CC_PRIVATE_INFO_NONE;
}

static t_CcNodeInformation * DequeueAdditionalInfoFromRelevantLst(
        t_List *p_List)
{
    t_CcNodeInformation *p_CcNodeInfo = NULL;

    if (!NCSW_LIST_IsEmpty(p_List))
    {
        p_CcNodeInfo = CC_NODE_F_OBJECT(p_List->p_Next);
        NCSW_LIST_DelAndInit(&p_CcNodeInfo->node);
    }

    return p_CcNodeInfo;
}

void ReleaseLst(t_List *p_List)
{
    t_CcNodeInformation *p_CcNodeInfo = NULL;

    if (!NCSW_LIST_IsEmpty(p_List))
    {
        p_CcNodeInfo = DequeueAdditionalInfoFromRelevantLst(p_List);
        while (p_CcNodeInfo)
        {
            XX_Free(p_CcNodeInfo);
            p_CcNodeInfo = DequeueAdditionalInfoFromRelevantLst(p_List);
        }
    }

    NCSW_LIST_Del(p_List);
}

static void DeleteNode(t_FmPcdCcNode *p_CcNode)
{
    uint32_t i;

    if (!p_CcNode)
        return;

    if (p_CcNode->p_GlblMask)
    {
        XX_Free(p_CcNode->p_GlblMask);
        p_CcNode->p_GlblMask = NULL;
    }

    if (p_CcNode->h_KeysMatchTable)
    {
        FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_CcNode->h_FmPcd),
                         p_CcNode->h_KeysMatchTable);
        p_CcNode->h_KeysMatchTable = NULL;
    }

    if (p_CcNode->h_AdTable)
    {
        FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_CcNode->h_FmPcd),
                         p_CcNode->h_AdTable);
        p_CcNode->h_AdTable = NULL;
    }

    if (p_CcNode->h_Ad)
    {
        FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_CcNode->h_FmPcd),
                         p_CcNode->h_Ad);
        p_CcNode->h_Ad = NULL;
        p_CcNode->h_TmpAd = NULL;
    }

    if (p_CcNode->h_StatsFLRs)
    {
        FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_CcNode->h_FmPcd),
                         p_CcNode->h_StatsFLRs);
        p_CcNode->h_StatsFLRs = NULL;
    }

    if (p_CcNode->h_Spinlock)
    {
        XX_FreeSpinlock(p_CcNode->h_Spinlock);
        p_CcNode->h_Spinlock = NULL;
    }

    /* Restore the original counters pointer instead of the mutual pointer (mutual to all hash buckets) */
    if (p_CcNode->isHashBucket
            && (p_CcNode->statisticsMode != e_FM_PCD_CC_STATS_MODE_NONE))
        p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].p_StatsObj->h_StatsCounters =
                p_CcNode->h_PrivMissStatsCounters;

    /* Releasing all currently used statistics objects, including 'miss' entry */
    for (i = 0; i < p_CcNode->numOfKeys + 1; i++)
        if (p_CcNode->keyAndNextEngineParams[i].p_StatsObj)
            PutStatsObj(p_CcNode,
                        p_CcNode->keyAndNextEngineParams[i].p_StatsObj);

    if (!NCSW_LIST_IsEmpty(&p_CcNode->availableStatsLst))
    {
        t_Handle h_FmMuram = FmPcdGetMuramHandle(p_CcNode->h_FmPcd);
        ASSERT_COND(h_FmMuram);

        FreeStatObjects(&p_CcNode->availableStatsLst, h_FmMuram);
    }

    NCSW_LIST_Del(&p_CcNode->availableStatsLst);

    ReleaseLst(&p_CcNode->ccPrevNodesLst);
    ReleaseLst(&p_CcNode->ccTreeIdLst);
    ReleaseLst(&p_CcNode->ccTreesLst);

    XX_Free(p_CcNode);
}

static void DeleteTree(t_FmPcdCcTree *p_FmPcdTree, t_FmPcd *p_FmPcd)
{
    if (p_FmPcdTree)
    {
        if (p_FmPcdTree->ccTreeBaseAddr)
        {
            FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_FmPcd),
                             UINT_TO_PTR(p_FmPcdTree->ccTreeBaseAddr));
            p_FmPcdTree->ccTreeBaseAddr = 0;
        }

        ReleaseLst(&p_FmPcdTree->fmPortsLst);

        XX_Free(p_FmPcdTree);
    }
}

static void GetCcExtractKeySize(uint8_t parseCodeRealSize,
                                uint8_t *parseCodeCcSize)
{
    if ((parseCodeRealSize > 0) && (parseCodeRealSize < 2))
        *parseCodeCcSize = 1;
    else
        if (parseCodeRealSize == 2)
            *parseCodeCcSize = 2;
        else
            if ((parseCodeRealSize > 2) && (parseCodeRealSize <= 4))
                *parseCodeCcSize = 4;
            else
                if ((parseCodeRealSize > 4) && (parseCodeRealSize <= 8))
                    *parseCodeCcSize = 8;
                else
                    if ((parseCodeRealSize > 8) && (parseCodeRealSize <= 16))
                        *parseCodeCcSize = 16;
                    else
                        if ((parseCodeRealSize > 16)
                                && (parseCodeRealSize <= 24))
                            *parseCodeCcSize = 24;
                        else
                            if ((parseCodeRealSize > 24)
                                    && (parseCodeRealSize <= 32))
                                *parseCodeCcSize = 32;
                            else
                                if ((parseCodeRealSize > 32)
                                        && (parseCodeRealSize <= 40))
                                    *parseCodeCcSize = 40;
                                else
                                    if ((parseCodeRealSize > 40)
                                            && (parseCodeRealSize <= 48))
                                        *parseCodeCcSize = 48;
                                    else
                                        if ((parseCodeRealSize > 48)
                                                && (parseCodeRealSize <= 56))
                                            *parseCodeCcSize = 56;
                                        else
                                            *parseCodeCcSize = 0;
}

static void GetSizeHeaderField(e_NetHeaderType hdr, t_FmPcdFields field,
		                       uint8_t *parseCodeRealSize)
{
    switch (hdr)
    {
        case (HEADER_TYPE_ETH):
            switch (field.eth)
            {
                case (NET_HEADER_FIELD_ETH_DA):
                    *parseCodeRealSize = 6;
                    break;

                case (NET_HEADER_FIELD_ETH_SA):
                    *parseCodeRealSize = 6;
                    break;

                case (NET_HEADER_FIELD_ETH_TYPE):
                    *parseCodeRealSize = 2;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported1"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_PPPoE):
            switch (field.pppoe)
            {
                case (NET_HEADER_FIELD_PPPoE_PID):
                    *parseCodeRealSize = 2;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported1"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_VLAN):
            switch (field.vlan)
            {
                case (NET_HEADER_FIELD_VLAN_TCI):
                    *parseCodeRealSize = 2;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported2"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_MPLS):
            switch (field.mpls)
            {
                case (NET_HEADER_FIELD_MPLS_LABEL_STACK):
                    *parseCodeRealSize = 4;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported3"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_IPv4):
            switch (field.ipv4)
            {
                case (NET_HEADER_FIELD_IPv4_DST_IP):
                case (NET_HEADER_FIELD_IPv4_SRC_IP):
                    *parseCodeRealSize = 4;
                    break;

                case (NET_HEADER_FIELD_IPv4_TOS):
                case (NET_HEADER_FIELD_IPv4_PROTO):
                    *parseCodeRealSize = 1;
                    break;

                case (NET_HEADER_FIELD_IPv4_DST_IP
                        | NET_HEADER_FIELD_IPv4_SRC_IP):
                    *parseCodeRealSize = 8;
                    break;

                case (NET_HEADER_FIELD_IPv4_TTL):
                    *parseCodeRealSize = 1;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported4"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_IPv6):
            switch (field.ipv6)
            {
                case (NET_HEADER_FIELD_IPv6_VER | NET_HEADER_FIELD_IPv6_FL
                        | NET_HEADER_FIELD_IPv6_TC):
                    *parseCodeRealSize = 4;
                    break;

                case (NET_HEADER_FIELD_IPv6_NEXT_HDR):
                case (NET_HEADER_FIELD_IPv6_HOP_LIMIT):
                    *parseCodeRealSize = 1;
                    break;

                case (NET_HEADER_FIELD_IPv6_DST_IP):
                case (NET_HEADER_FIELD_IPv6_SRC_IP):
                    *parseCodeRealSize = 16;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported5"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_IP):
            switch (field.ip)
            {
                case (NET_HEADER_FIELD_IP_DSCP):
                case (NET_HEADER_FIELD_IP_PROTO):
                    *parseCodeRealSize = 1;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported5"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_GRE):
            switch (field.gre)
            {
                case (NET_HEADER_FIELD_GRE_TYPE):
                    *parseCodeRealSize = 2;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported6"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_MINENCAP):
            switch (field.minencap)
            {
                case (NET_HEADER_FIELD_MINENCAP_TYPE):
                    *parseCodeRealSize = 1;
                    break;

                case (NET_HEADER_FIELD_MINENCAP_DST_IP):
                case (NET_HEADER_FIELD_MINENCAP_SRC_IP):
                    *parseCodeRealSize = 4;
                    break;

                case (NET_HEADER_FIELD_MINENCAP_SRC_IP
                        | NET_HEADER_FIELD_MINENCAP_DST_IP):
                    *parseCodeRealSize = 8;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported7"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_TCP):
            switch (field.tcp)
            {
                case (NET_HEADER_FIELD_TCP_PORT_SRC):
                case (NET_HEADER_FIELD_TCP_PORT_DST):
                    *parseCodeRealSize = 2;
                    break;

                case (NET_HEADER_FIELD_TCP_PORT_SRC
                        | NET_HEADER_FIELD_TCP_PORT_DST):
                    *parseCodeRealSize = 4;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported8"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        case (HEADER_TYPE_UDP):
            switch (field.udp)
            {
                case (NET_HEADER_FIELD_UDP_PORT_SRC):
                case (NET_HEADER_FIELD_UDP_PORT_DST):
                    *parseCodeRealSize = 2;
                    break;

                case (NET_HEADER_FIELD_UDP_PORT_SRC
                        | NET_HEADER_FIELD_UDP_PORT_DST):
                    *parseCodeRealSize = 4;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported9"));
                    *parseCodeRealSize = CC_SIZE_ILLEGAL;
                    break;
            }
            break;

        default:
            REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported10"));
            *parseCodeRealSize = CC_SIZE_ILLEGAL;
            break;
    }
}

t_Error ValidateNextEngineParams(
        t_Handle h_FmPcd, t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams,
        e_FmPcdCcStatsMode statsMode)
{
    uint16_t absoluteProfileId;
    t_Error err = E_OK;
    uint8_t relativeSchemeId;

    if ((statsMode == e_FM_PCD_CC_STATS_MODE_NONE)
            && (p_FmPcdCcNextEngineParams->statisticsEn))
        RETURN_ERROR(
                MAJOR,
                E_CONFLICT,
                ("Statistics are requested for a key, but statistics mode was set"
                "to 'NONE' upon initialization"));

    switch (p_FmPcdCcNextEngineParams->nextEngine)
    {
        case (e_FM_PCD_INVALID):
            err = E_NOT_SUPPORTED;
            break;

        case (e_FM_PCD_DONE):
            if ((p_FmPcdCcNextEngineParams->params.enqueueParams.action
                    == e_FM_PCD_ENQ_FRAME)
                    && p_FmPcdCcNextEngineParams->params.enqueueParams.overrideFqid)
            {
                if (!p_FmPcdCcNextEngineParams->params.enqueueParams.newFqid)
                    RETURN_ERROR(
                            MAJOR,
                            E_CONFLICT,
                            ("When overrideFqid is set, newFqid must not be zero"));
                if (p_FmPcdCcNextEngineParams->params.enqueueParams.newFqid
                        & ~0x00FFFFFF)
                    RETURN_ERROR(
                            MAJOR, E_INVALID_VALUE,
                            ("fqidForCtrlFlow must be between 1 and 2^24-1"));
            }
            break;

        case (e_FM_PCD_KG):
            relativeSchemeId =
                    FmPcdKgGetRelativeSchemeId(
                            h_FmPcd,
                            FmPcdKgGetSchemeId(
                                    p_FmPcdCcNextEngineParams->params.kgParams.h_DirectScheme));
            if (relativeSchemeId == FM_PCD_KG_NUM_OF_SCHEMES)
                RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);
            if (!FmPcdKgIsSchemeValidSw(
                    p_FmPcdCcNextEngineParams->params.kgParams.h_DirectScheme))
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("not valid schemeIndex in KG next engine param"));
            if (!KgIsSchemeAlwaysDirect(h_FmPcd, relativeSchemeId))
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_STATE,
                        ("CC Node may point only to a scheme that is always direct."));
            break;

        case (e_FM_PCD_PLCR):
            if (p_FmPcdCcNextEngineParams->params.plcrParams.overrideParams)
            {
                /* if private policer profile, it may be uninitialized yet, therefore no checks are done at this stage */
                if (p_FmPcdCcNextEngineParams->params.plcrParams.sharedProfile)
                {
                    err =
                            FmPcdPlcrGetAbsoluteIdByProfileParams(
                                    h_FmPcd,
                                    e_FM_PCD_PLCR_SHARED,
                                    NULL,
                                    p_FmPcdCcNextEngineParams->params.plcrParams.newRelativeProfileId,
                                    &absoluteProfileId);
                    if (err)
                        RETURN_ERROR(MAJOR, err,
                                     ("Shared profile offset is out of range"));
                    if (!FmPcdPlcrIsProfileValid(h_FmPcd, absoluteProfileId))
                        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                                     ("Invalid profile"));
                }
            }
            break;

        case (e_FM_PCD_HASH):
            p_FmPcdCcNextEngineParams->nextEngine = e_FM_PCD_CC;
        case (e_FM_PCD_CC):
            if (!p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode)
                RETURN_ERROR(MAJOR, E_NULL_POINTER,
                             ("handler to next Node is NULL"));
            break;

#if (DPAA_VERSION >= 11)
        case (e_FM_PCD_FR):
            if (!p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic)
                err = E_NOT_SUPPORTED;
            break;
#endif /* (DPAA_VERSION >= 11) */

        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("Next engine is not correct"));
    }


    return err;
}

static uint8_t GetGenParseCode(e_FmPcdExtractFrom src,
                               uint32_t offset, bool glblMask,
                               uint8_t *parseArrayOffset, bool fromIc,
                               ccPrivateInfo_t icCode)
{
    if (!fromIc)
    {
        switch (src)
        {
            case (e_FM_PCD_EXTRACT_FROM_FRAME_START):
                if (glblMask)
                    return CC_PC_GENERIC_WITH_MASK;
                else
                    return CC_PC_GENERIC_WITHOUT_MASK;

            case (e_FM_PCD_EXTRACT_FROM_CURR_END_OF_PARSE):
                *parseArrayOffset = CC_PC_PR_NEXT_HEADER_OFFSET;
                if (offset)
                    return CC_PR_OFFSET;
                else
                    return CC_PR_WITHOUT_OFFSET;

            default:
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 'extract from' src"));
                return CC_PC_ILLEGAL;
        }
    }
    else
    {
        switch (icCode)
        {
            case (CC_PRIVATE_INFO_IC_KEY_EXACT_MATCH):
                *parseArrayOffset = 0x50;
                return CC_PC_GENERIC_IC_GMASK;

            case (CC_PRIVATE_INFO_IC_HASH_EXACT_MATCH):
                *parseArrayOffset = 0x48;
                return CC_PC_GENERIC_IC_GMASK;

            case (CC_PRIVATE_INFO_IC_HASH_INDEX_LOOKUP):
                *parseArrayOffset = 0x48;
                return CC_PC_GENERIC_IC_HASH_INDEXED;

            case (CC_PRIVATE_INFO_IC_DEQ_FQID_INDEX_LOOKUP):
                *parseArrayOffset = 0x16;
                return CC_PC_GENERIC_IC_HASH_INDEXED;

            default:
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 'extract from' src"));
                break;
        }
    }

    return CC_PC_ILLEGAL;
}

static uint8_t GetFullFieldParseCode(e_NetHeaderType hdr, e_FmPcdHdrIndex index,
                                     t_FmPcdFields field)
{
    switch (hdr)
    {
        case (HEADER_TYPE_NONE):
            ASSERT_COND(FALSE);
            return CC_PC_ILLEGAL;

        case (HEADER_TYPE_ETH):
            switch (field.eth)
            {
                case (NET_HEADER_FIELD_ETH_DA):
                    return CC_PC_FF_MACDST;
                case (NET_HEADER_FIELD_ETH_SA):
                    return CC_PC_FF_MACSRC;
                case (NET_HEADER_FIELD_ETH_TYPE):
                    return CC_PC_FF_ETYPE;
                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_VLAN):
            switch (field.vlan)
            {
                case (NET_HEADER_FIELD_VLAN_TCI):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_TCI1;
                    if (index == e_FM_PCD_HDR_INDEX_LAST)
                        return CC_PC_FF_TCI2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_MPLS):
            switch (field.mpls)
            {
                case (NET_HEADER_FIELD_MPLS_LABEL_STACK):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_MPLS1;
                    if (index == e_FM_PCD_HDR_INDEX_LAST)
                        return CC_PC_FF_MPLS_LAST;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal MPLS index"));
                    return CC_PC_ILLEGAL;
                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_IPv4):
            switch (field.ipv4)
            {
                case (NET_HEADER_FIELD_IPv4_DST_IP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV4DST1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV4DST2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv4 index"));
                    return CC_PC_ILLEGAL;
                case (NET_HEADER_FIELD_IPv4_TOS):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV4IPTOS_TC1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV4IPTOS_TC2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv4 index"));
                    return CC_PC_ILLEGAL;
                case (NET_HEADER_FIELD_IPv4_PROTO):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV4PTYPE1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV4PTYPE2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv4 index"));
                    return CC_PC_ILLEGAL;
                case (NET_HEADER_FIELD_IPv4_SRC_IP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV4SRC1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV4SRC2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv4 index"));
                    return CC_PC_ILLEGAL;
                case (NET_HEADER_FIELD_IPv4_SRC_IP
                        | NET_HEADER_FIELD_IPv4_DST_IP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV4SRC1_IPV4DST1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV4SRC2_IPV4DST2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv4 index"));
                    return CC_PC_ILLEGAL;
                case (NET_HEADER_FIELD_IPv4_TTL):
                    return CC_PC_FF_IPV4TTL;
                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_IPv6):
            switch (field.ipv6)
            {
                case (NET_HEADER_FIELD_IPv6_VER | NET_HEADER_FIELD_IPv6_FL
                        | NET_HEADER_FIELD_IPv6_TC):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPTOS_IPV6TC1_IPV6FLOW1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPTOS_IPV6TC2_IPV6FLOW2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv6 index"));
                    return CC_PC_ILLEGAL;

                case (NET_HEADER_FIELD_IPv6_NEXT_HDR):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV6PTYPE1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV6PTYPE2;
                    if (index == e_FM_PCD_HDR_INDEX_LAST)
                        return CC_PC_FF_IPPID;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv6 index"));
                    return CC_PC_ILLEGAL;

                case (NET_HEADER_FIELD_IPv6_DST_IP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV6DST1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV6DST2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv6 index"));
                    return CC_PC_ILLEGAL;

                case (NET_HEADER_FIELD_IPv6_SRC_IP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPV6SRC1;
                    if (index == e_FM_PCD_HDR_INDEX_2)
                        return CC_PC_FF_IPV6SRC2;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IPv6 index"));
                    return CC_PC_ILLEGAL;

                case (NET_HEADER_FIELD_IPv6_HOP_LIMIT):
                    return CC_PC_FF_IPV6HOP_LIMIT;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_IP):
            switch (field.ip)
            {
                case (NET_HEADER_FIELD_IP_DSCP):
                    if ((index == e_FM_PCD_HDR_INDEX_NONE)
                            || (index == e_FM_PCD_HDR_INDEX_1))
                        return CC_PC_FF_IPDSCP;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IP index"));
                    return CC_PC_ILLEGAL;

                case (NET_HEADER_FIELD_IP_PROTO):
                    if (index == e_FM_PCD_HDR_INDEX_LAST)
                        return CC_PC_FF_IPPID;
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IP index"));
                    return CC_PC_ILLEGAL;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_GRE):
            switch (field.gre)
            {
                case (NET_HEADER_FIELD_GRE_TYPE):
                    return CC_PC_FF_GREPTYPE;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_MINENCAP):
            switch (field.minencap)
            {
                case (NET_HEADER_FIELD_MINENCAP_TYPE):
                    return CC_PC_FF_MINENCAP_PTYPE;

                case (NET_HEADER_FIELD_MINENCAP_DST_IP):
                    return CC_PC_FF_MINENCAP_IPDST;

                case (NET_HEADER_FIELD_MINENCAP_SRC_IP):
                    return CC_PC_FF_MINENCAP_IPSRC;

                case (NET_HEADER_FIELD_MINENCAP_SRC_IP
                        | NET_HEADER_FIELD_MINENCAP_DST_IP):
                    return CC_PC_FF_MINENCAP_IPSRC_IPDST;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_TCP):
            switch (field.tcp)
            {
                case (NET_HEADER_FIELD_TCP_PORT_SRC):
                    return CC_PC_FF_L4PSRC;

                case (NET_HEADER_FIELD_TCP_PORT_DST):
                    return CC_PC_FF_L4PDST;

                case (NET_HEADER_FIELD_TCP_PORT_DST
                        | NET_HEADER_FIELD_TCP_PORT_SRC):
                    return CC_PC_FF_L4PSRC_L4PDST;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_PPPoE):
            switch (field.pppoe)
            {
                case (NET_HEADER_FIELD_PPPoE_PID):
                    return CC_PC_FF_PPPPID;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        case (HEADER_TYPE_UDP):
            switch (field.udp)
            {
                case (NET_HEADER_FIELD_UDP_PORT_SRC):
                    return CC_PC_FF_L4PSRC;

                case (NET_HEADER_FIELD_UDP_PORT_DST):
                    return CC_PC_FF_L4PDST;

                case (NET_HEADER_FIELD_UDP_PORT_DST
                        | NET_HEADER_FIELD_UDP_PORT_SRC):
                    return CC_PC_FF_L4PSRC_L4PDST;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }

        default:
            REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
            return CC_PC_ILLEGAL;
    }
}

static uint8_t GetPrParseCode(e_NetHeaderType hdr, e_FmPcdHdrIndex hdrIndex,
                              uint32_t offset, bool glblMask,
                              uint8_t *parseArrayOffset)
{
    bool offsetRelevant = FALSE;

    if (offset)
        offsetRelevant = TRUE;

    switch (hdr)
    {
        case (HEADER_TYPE_NONE):
            ASSERT_COND(FALSE);
            return CC_PC_ILLEGAL;

        case (HEADER_TYPE_ETH):
            *parseArrayOffset = (uint8_t)CC_PC_PR_ETH_OFFSET;
            break;

        case (HEADER_TYPE_USER_DEFINED_SHIM1):
            if (offset || glblMask)
                *parseArrayOffset = (uint8_t)CC_PC_PR_USER_DEFINED_SHIM1_OFFSET;
            else
                return CC_PC_PR_SHIM1;
            break;

        case (HEADER_TYPE_USER_DEFINED_SHIM2):
            if (offset || glblMask)
                *parseArrayOffset = (uint8_t)CC_PC_PR_USER_DEFINED_SHIM2_OFFSET;
            else
                return CC_PC_PR_SHIM2;
            break;

        case (HEADER_TYPE_LLC_SNAP):
            *parseArrayOffset = CC_PC_PR_USER_LLC_SNAP_OFFSET;
            break;

        case (HEADER_TYPE_PPPoE):
            *parseArrayOffset = CC_PC_PR_PPPOE_OFFSET;
            break;

        case (HEADER_TYPE_MPLS):
            if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                    || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                *parseArrayOffset = CC_PC_PR_MPLS1_OFFSET;
            else
                if (hdrIndex == e_FM_PCD_HDR_INDEX_LAST)
                    *parseArrayOffset = CC_PC_PR_MPLS_LAST_OFFSET;
                else
                {
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal MPLS header index"));
                    return CC_PC_ILLEGAL;
                }
            break;

        case (HEADER_TYPE_IPv4):
        case (HEADER_TYPE_IPv6):
            if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                    || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                *parseArrayOffset = CC_PC_PR_IP1_OFFSET;
            else
                if (hdrIndex == e_FM_PCD_HDR_INDEX_2)
                    *parseArrayOffset = CC_PC_PR_IP_LAST_OFFSET;
                else
                {
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IP header index"));
                    return CC_PC_ILLEGAL;
                }
            break;

        case (HEADER_TYPE_MINENCAP):
            *parseArrayOffset = CC_PC_PR_MINENC_OFFSET;
            break;

        case (HEADER_TYPE_GRE):
            *parseArrayOffset = CC_PC_PR_GRE_OFFSET;
            break;

        case (HEADER_TYPE_TCP):
        case (HEADER_TYPE_UDP):
        case (HEADER_TYPE_IPSEC_AH):
        case (HEADER_TYPE_IPSEC_ESP):
        case (HEADER_TYPE_DCCP):
        case (HEADER_TYPE_SCTP):
            *parseArrayOffset = CC_PC_PR_L4_OFFSET;
            break;

        default:
            REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal IP header for this type of operation"));
            return CC_PC_ILLEGAL;
    }

    if (offsetRelevant)
        return CC_PR_OFFSET;
    else
        return CC_PR_WITHOUT_OFFSET;
}

static uint8_t GetFieldParseCode(e_NetHeaderType hdr, t_FmPcdFields field,
                                 uint32_t offset, uint8_t *parseArrayOffset,
                                 e_FmPcdHdrIndex hdrIndex)
{
    bool offsetRelevant = FALSE;

    if (offset)
        offsetRelevant = TRUE;

    switch (hdr)
    {
        case (HEADER_TYPE_NONE):
            ASSERT_COND(FALSE);
                break;
        case (HEADER_TYPE_ETH):
            switch (field.eth)
            {
                case (NET_HEADER_FIELD_ETH_TYPE):
                    *parseArrayOffset = CC_PC_PR_ETYPE_LAST_OFFSET;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }
            break;

        case (HEADER_TYPE_VLAN):
            switch (field.vlan)
            {
                case (NET_HEADER_FIELD_VLAN_TCI):
                    if ((hdrIndex == e_FM_PCD_HDR_INDEX_NONE)
                            || (hdrIndex == e_FM_PCD_HDR_INDEX_1))
                        *parseArrayOffset = CC_PC_PR_VLAN1_OFFSET;
                    else
                        if (hdrIndex == e_FM_PCD_HDR_INDEX_LAST)
                            *parseArrayOffset = CC_PC_PR_VLAN2_OFFSET;
                    break;

                default:
                    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Extraction not supported"));
                    return CC_PC_ILLEGAL;
            }
            break;

        default:
            REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Illegal header "));
            return CC_PC_ILLEGAL;
    }

    if (offsetRelevant)
        return CC_PR_OFFSET;
    else
        return CC_PR_WITHOUT_OFFSET;
}

static void FillAdOfTypeResult(t_Handle h_Ad,
                               t_FmPcdCcStatsParams *p_FmPcdCcStatsParams,
                               t_FmPcd *p_FmPcd,
                               t_FmPcdCcNextEngineParams *p_CcNextEngineParams)
{
    t_AdOfTypeResult *p_AdResult = (t_AdOfTypeResult *)h_Ad;
    t_Handle h_TmpAd;
    uint32_t tmp = 0, tmpNia = 0;
    uint16_t profileId;
    t_Handle p_AdNewPtr = NULL;
    t_Error err = E_OK;

    /* There are 3 cases handled in this routine of building a "result" type AD.
     * Case 1: No Manip. The action descriptor is built within the match table.
     * Case 2: Manip exists. A new AD is created - p_AdNewPtr. It is initialized
     *         either in the FmPcdManipUpdateAdResultForCc routine or it was already
     *         initialized and returned here.
     *         p_AdResult (within the match table) will be initialized after
     *         this routine returns and point to the existing AD.
     * Case 3: Manip exists. The action descriptor is built within the match table.
     *         FmPcdManipUpdateAdResultForCc returns a NULL p_AdNewPtr.
     *
     * If statistics were enabled and the statistics mode of this node requires
     * a statistics Ad, it will be placed after the result Ad and before the
     * manip Ad, if manip Ad exists here.
     */

    /* As default, the "new" ptr is the current one. i.e. the content of the result
     * AD will be written into the match table itself (case (1))*/
    p_AdNewPtr = p_AdResult;

    /* Initialize an action descriptor, if current statistics mode requires an Ad */
    if (p_FmPcdCcStatsParams)
    {
        ASSERT_COND(p_FmPcdCcStatsParams->h_StatsAd);
        ASSERT_COND(p_FmPcdCcStatsParams->h_StatsCounters);

        /* Swapping addresses between statistics Ad and the current lookup AD addresses */
        h_TmpAd = p_FmPcdCcStatsParams->h_StatsAd;
        p_FmPcdCcStatsParams->h_StatsAd = h_Ad;
        h_Ad = h_TmpAd;

        p_AdNewPtr = h_Ad;
        p_AdResult = h_Ad;

        /* Init statistics Ad and connect current lookup AD as 'next action' from statistics Ad */
        UpdateStatsAd(p_FmPcdCcStatsParams, h_Ad, p_FmPcd->physicalMuramBase);
    }

    /* Create manip and return p_AdNewPtr to either a new descriptor or NULL */
    if (p_CcNextEngineParams->h_Manip)
        FmPcdManipUpdateAdResultForCc(p_CcNextEngineParams->h_Manip,
                                      p_CcNextEngineParams, h_Ad, &p_AdNewPtr);

    /* if (p_AdNewPtr = NULL) --> Done. (case (3)) */
    if (p_AdNewPtr)
    {
        /* case (1) and (2) */
        switch (p_CcNextEngineParams->nextEngine)
        {
            case (e_FM_PCD_DONE):
                if (p_CcNextEngineParams->params.enqueueParams.action
                        == e_FM_PCD_ENQ_FRAME)
                {
                    if (p_CcNextEngineParams->params.enqueueParams.overrideFqid)
                    {
                        tmp = FM_PCD_AD_RESULT_CONTRL_FLOW_TYPE;
                        tmp |=
                                p_CcNextEngineParams->params.enqueueParams.newFqid;
#if (DPAA_VERSION >= 11)
                        tmp |=
                                (p_CcNextEngineParams->params.enqueueParams.newRelativeStorageProfileId
                                        & FM_PCD_AD_RESULT_VSP_MASK)
                                        << FM_PCD_AD_RESULT_VSP_SHIFT;
#endif /* (DPAA_VERSION >= 11) */
                    }
                    else
                    {
                        tmp = FM_PCD_AD_RESULT_DATA_FLOW_TYPE;
                        tmp |= FM_PCD_AD_RESULT_PLCR_DIS;
                    }
                }

                if (p_CcNextEngineParams->params.enqueueParams.action
                        == e_FM_PCD_DROP_FRAME)
                    tmpNia |= GET_NIA_BMI_AC_DISCARD_FRAME(p_FmPcd);
                else
                    tmpNia |= GET_NIA_BMI_AC_ENQ_FRAME(p_FmPcd);
                break;

            case (e_FM_PCD_KG):
                if (p_CcNextEngineParams->params.kgParams.overrideFqid)
                {
                    tmp = FM_PCD_AD_RESULT_CONTRL_FLOW_TYPE;
                    tmp |= p_CcNextEngineParams->params.kgParams.newFqid;
#if (DPAA_VERSION >= 11)
                    tmp |=
                            (p_CcNextEngineParams->params.kgParams.newRelativeStorageProfileId
                                    & FM_PCD_AD_RESULT_VSP_MASK)
                                    << FM_PCD_AD_RESULT_VSP_SHIFT;
#endif /* (DPAA_VERSION >= 11) */
                }
                else
                {
                    tmp = FM_PCD_AD_RESULT_DATA_FLOW_TYPE;
                    tmp |= FM_PCD_AD_RESULT_PLCR_DIS;
                }
                tmpNia = NIA_KG_DIRECT;
                tmpNia |= NIA_ENG_KG;
                tmpNia |= NIA_KG_CC_EN;
                tmpNia |= FmPcdKgGetSchemeId(
                        p_CcNextEngineParams->params.kgParams.h_DirectScheme);
                break;

            case (e_FM_PCD_PLCR):
                if (p_CcNextEngineParams->params.plcrParams.overrideParams)
                {
                    tmp = FM_PCD_AD_RESULT_CONTRL_FLOW_TYPE;

                    /* if private policer profile, it may be uninitialized yet, therefore no checks are done at this stage */
                    if (p_CcNextEngineParams->params.plcrParams.sharedProfile)
                    {
                        tmpNia |= NIA_PLCR_ABSOLUTE;
                        err = FmPcdPlcrGetAbsoluteIdByProfileParams(
                                (t_Handle)p_FmPcd,
                                e_FM_PCD_PLCR_SHARED,
                                NULL,
                                p_CcNextEngineParams->params.plcrParams.newRelativeProfileId,
                                &profileId);
			if (err != E_OK)
				return;

                    }
                    else
                        profileId =
                                p_CcNextEngineParams->params.plcrParams.newRelativeProfileId;

                    tmp |= p_CcNextEngineParams->params.plcrParams.newFqid;
#if (DPAA_VERSION >= 11)
                    tmp |=
                            (p_CcNextEngineParams->params.plcrParams.newRelativeStorageProfileId
                                    & FM_PCD_AD_RESULT_VSP_MASK)
                                    << FM_PCD_AD_RESULT_VSP_SHIFT;
#endif /* (DPAA_VERSION >= 11) */
                    WRITE_UINT32(
                            p_AdResult->plcrProfile,
                            (uint32_t)((uint32_t)profileId << FM_PCD_AD_PROFILEID_FOR_CNTRL_SHIFT));
                }
                else
                    tmp = FM_PCD_AD_RESULT_DATA_FLOW_TYPE;

                tmpNia |=
                        NIA_ENG_PLCR
                                | p_CcNextEngineParams->params.plcrParams.newRelativeProfileId;
                break;

            default:
                return;
        }WRITE_UINT32(p_AdResult->fqid, tmp);

        if (p_CcNextEngineParams->h_Manip)
        {
            tmp = GET_UINT32(p_AdResult->plcrProfile);
            tmp |= (uint32_t)(XX_VirtToPhys(p_AdNewPtr)
                    - (p_FmPcd->physicalMuramBase)) >> 4;
            WRITE_UINT32(p_AdResult->plcrProfile, tmp);

            tmpNia |= FM_PCD_AD_RESULT_EXTENDED_MODE;
            tmpNia |= FM_PCD_AD_RESULT_NADEN;
        }

#if (DPAA_VERSION >= 11)
        tmpNia |= FM_PCD_AD_RESULT_NO_OM_VSPE;
#endif /* (DPAA_VERSION >= 11) */
        WRITE_UINT32(p_AdResult->nia, tmpNia);
    }
}

static t_Error CcUpdateParams(t_Handle h_FmPcd, t_Handle h_PcdParams,
                              t_Handle h_FmPort, t_Handle h_FmTree,
                              bool validate)
{
    t_FmPcdCcTree *p_CcTree = (t_FmPcdCcTree *)h_FmTree;

    return CcUpdateParam(h_FmPcd, h_PcdParams, h_FmPort,
                         p_CcTree->keyAndNextEngineParams,
                         p_CcTree->numOfEntries,
                         UINT_TO_PTR(p_CcTree->ccTreeBaseAddr), validate, 0,
                         h_FmTree, FALSE);
}


static void ReleaseNewNodeCommonPart(
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo)
{
    if (p_AdditionalInfo->p_AdTableNew)
        FM_MURAM_FreeMem(
                FmPcdGetMuramHandle(
                        ((t_FmPcdCcNode *)(p_AdditionalInfo->h_CurrentNode))->h_FmPcd),
                p_AdditionalInfo->p_AdTableNew);

    if (p_AdditionalInfo->p_KeysMatchTableNew)
        FM_MURAM_FreeMem(
                FmPcdGetMuramHandle(
                        ((t_FmPcdCcNode *)(p_AdditionalInfo->h_CurrentNode))->h_FmPcd),
                p_AdditionalInfo->p_KeysMatchTableNew);
}

static t_Error UpdateGblMask(t_FmPcdCcNode *p_CcNode, uint8_t keySize,
                             uint8_t *p_Mask)
{
    uint8_t prvGlblMaskSize = p_CcNode->glblMaskSize;

    if (p_Mask && !p_CcNode->glblMaskUpdated && (keySize <= 4)
            && !p_CcNode->lclMask)
    {
        if (p_CcNode->parseCode && (p_CcNode->parseCode != CC_PC_FF_TCI1)
                && (p_CcNode->parseCode != CC_PC_FF_TCI2)
                && (p_CcNode->parseCode != CC_PC_FF_MPLS1)
                && (p_CcNode->parseCode != CC_PC_FF_MPLS_LAST)
                && (p_CcNode->parseCode != CC_PC_FF_IPV4IPTOS_TC1)
                && (p_CcNode->parseCode != CC_PC_FF_IPV4IPTOS_TC2)
                && (p_CcNode->parseCode != CC_PC_FF_IPTOS_IPV6TC1_IPV6FLOW1)
                && (p_CcNode->parseCode != CC_PC_FF_IPDSCP)
                && (p_CcNode->parseCode != CC_PC_FF_IPTOS_IPV6TC2_IPV6FLOW2))
        {
            p_CcNode->glblMaskSize = 0;
            p_CcNode->lclMask = TRUE;
        }
        else
        {
            memcpy(p_CcNode->p_GlblMask, p_Mask, (sizeof(uint8_t)) * keySize);
            p_CcNode->glblMaskUpdated = TRUE;
            p_CcNode->glblMaskSize = 4;
        }
    }
    else
        if (p_Mask && (keySize <= 4) && !p_CcNode->lclMask)
        {
            if (memcmp(p_CcNode->p_GlblMask, p_Mask, keySize) != 0)
            {
                p_CcNode->lclMask = TRUE;
                p_CcNode->glblMaskSize = 0;
            }
        }
        else
            if (!p_Mask && p_CcNode->glblMaskUpdated && (keySize <= 4))
            {
                uint32_t tmpMask = 0xffffffff;
                if (memcmp(p_CcNode->p_GlblMask, &tmpMask, 4) != 0)
                {
                    p_CcNode->lclMask = TRUE;
                    p_CcNode->glblMaskSize = 0;
                }
            }
            else
                if (p_Mask)
                {
                    p_CcNode->lclMask = TRUE;
                    p_CcNode->glblMaskSize = 0;
                }

    /* In static mode (maxNumOfKeys > 0), local mask is supported
     only is mask support was enabled at initialization */
    if (p_CcNode->maxNumOfKeys && (!p_CcNode->maskSupport) && p_CcNode->lclMask)
    {
        p_CcNode->lclMask = FALSE;
        p_CcNode->glblMaskSize = prvGlblMaskSize;
        return ERROR_CODE(E_NOT_SUPPORTED);
    }

    return E_OK;
}

static __inline__ t_Handle GetNewAd(t_Handle h_FmPcdCcNodeOrTree, bool isTree)
{
    t_FmPcd *p_FmPcd;
    t_Handle h_Ad;

    if (isTree)
        p_FmPcd = (t_FmPcd *)(((t_FmPcdCcTree *)h_FmPcdCcNodeOrTree)->h_FmPcd);
    else
        p_FmPcd = (t_FmPcd *)(((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->h_FmPcd);

    if ((isTree && p_FmPcd->p_CcShadow)
            || (!isTree && ((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->maxNumOfKeys))
    {
        /* The allocated shadow is divided as follows:
         0 . . .       16 . . .
         ---------------------------------------------------
         |   Shadow   |   Shadow Keys   |   Shadow Next    |
         |     Ad     |   Match Table   |   Engine Table   |
         | (16 bytes) | (maximal size)  |  (maximal size)  |
         ---------------------------------------------------
         */
        if (!p_FmPcd->p_CcShadow)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("CC Shadow not allocated"));
            return NULL;
        }

        h_Ad = p_FmPcd->p_CcShadow;
    }
    else
    {
        h_Ad = (t_Handle)FM_MURAM_AllocMem(FmPcdGetMuramHandle(p_FmPcd),
                                           FM_PCD_CC_AD_ENTRY_SIZE,
                                           FM_PCD_CC_AD_TABLE_ALIGN);
        if (!h_Ad)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for CC node action descriptor"));
            return NULL;
        }
    }

    return h_Ad;
}

static t_Error BuildNewNodeCommonPart(
        t_FmPcdCcNode *p_CcNode, int *size,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    if (p_CcNode->lclMask)
        *size = 2 * p_CcNode->ccKeySizeAccExtraction;
    else
        *size = p_CcNode->ccKeySizeAccExtraction;

    if (p_CcNode->maxNumOfKeys == 0)
    {
        p_AdditionalInfo->p_AdTableNew = (t_Handle)FM_MURAM_AllocMem(
                FmPcdGetMuramHandle(p_FmPcd),
                (uint32_t)((p_AdditionalInfo->numOfKeys + 1)
                        * FM_PCD_CC_AD_ENTRY_SIZE),
                FM_PCD_CC_AD_TABLE_ALIGN);
        if (!p_AdditionalInfo->p_AdTableNew)
            RETURN_ERROR(
                    MAJOR, E_NO_MEMORY,
                    ("MURAM allocation for CC node action descriptors table"));

        p_AdditionalInfo->p_KeysMatchTableNew = (t_Handle)FM_MURAM_AllocMem(
                FmPcdGetMuramHandle(p_FmPcd),
                (uint32_t)(*size * sizeof(uint8_t)
                        * (p_AdditionalInfo->numOfKeys + 1)),
                FM_PCD_CC_KEYS_MATCH_TABLE_ALIGN);
        if (!p_AdditionalInfo->p_KeysMatchTableNew)
        {
            FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_CcNode->h_FmPcd),
                             p_AdditionalInfo->p_AdTableNew);
            p_AdditionalInfo->p_AdTableNew = NULL;
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for CC node key match table"));
        }

        MemSet8(
                (uint8_t*)p_AdditionalInfo->p_AdTableNew,
                0,
                (uint32_t)((p_AdditionalInfo->numOfKeys + 1)
                        * FM_PCD_CC_AD_ENTRY_SIZE));
        MemSet8((uint8_t*)p_AdditionalInfo->p_KeysMatchTableNew, 0,
                   *size * sizeof(uint8_t) * (p_AdditionalInfo->numOfKeys + 1));
    }
    else
    {
        /* The allocated shadow is divided as follows:
         0 . . .       16 . . .
         ---------------------------------------------------
         |   Shadow   |   Shadow Keys   |   Shadow Next    |
         |     Ad     |   Match Table   |   Engine Table   |
         | (16 bytes) | (maximal size)  |  (maximal size)  |
         ---------------------------------------------------
         */

        if (!p_FmPcd->p_CcShadow)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("CC Shadow not allocated"));

        p_AdditionalInfo->p_KeysMatchTableNew =
                PTR_MOVE(p_FmPcd->p_CcShadow, FM_PCD_CC_AD_ENTRY_SIZE);
        p_AdditionalInfo->p_AdTableNew =
                PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, p_CcNode->keysMatchTableMaxSize);

        MemSet8(
                (uint8_t*)p_AdditionalInfo->p_AdTableNew,
                0,
                (uint32_t)((p_CcNode->maxNumOfKeys + 1)
                        * FM_PCD_CC_AD_ENTRY_SIZE));
        MemSet8((uint8_t*)p_AdditionalInfo->p_KeysMatchTableNew, 0,
                   (*size) * sizeof(uint8_t) * (p_CcNode->maxNumOfKeys));
    }

    p_AdditionalInfo->p_AdTableOld = p_CcNode->h_AdTable;
    p_AdditionalInfo->p_KeysMatchTableOld = p_CcNode->h_KeysMatchTable;

    return E_OK;
}

static t_Error BuildNewNodeAddOrMdfyKeyAndNextEngine(
        t_Handle h_FmPcd, t_FmPcdCcNode *p_CcNode, uint16_t keyIndex,
        t_FmPcdCcKeyParams *p_KeyParams,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo, bool add)
{
    t_Error err = E_OK;
    t_Handle p_AdTableNewTmp, p_KeysMatchTableNewTmp;
    t_Handle p_KeysMatchTableOldTmp, p_AdTableOldTmp;
    int size;
    int i = 0, j = 0;
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t requiredAction = 0;
    bool prvLclMask;
    t_CcNodeInformation *p_CcNodeInformation;
    t_FmPcdCcStatsParams statsParams = { 0 };
    t_List *p_Pos;
    t_FmPcdStatsObj *p_StatsObj;

    /* Check that new NIA is legal */
    err = ValidateNextEngineParams(h_FmPcd, &p_KeyParams->ccNextEngineParams,
                                   p_CcNode->statisticsMode);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    prvLclMask = p_CcNode->lclMask;

    /* Check that new key is not require update of localMask */
    err = UpdateGblMask(p_CcNode, p_CcNode->ccKeySizeAccExtraction,
                        p_KeyParams->p_Mask);
    if (err)
        RETURN_ERROR(MAJOR, err, (NO_MSG));

    /* Update internal data structure with new next engine for the given index */
    memcpy(&p_AdditionalInfo->keyAndNextEngineParams[keyIndex].nextEngineParams,
           &p_KeyParams->ccNextEngineParams, sizeof(t_FmPcdCcNextEngineParams));

    memcpy(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].key,
           p_KeyParams->p_Key, p_CcNode->userSizeOfExtraction);

    if ((p_AdditionalInfo->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
            == e_FM_PCD_CC)
            && p_AdditionalInfo->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip)
    {
        err =
                AllocAndFillAdForContLookupManip(
                        p_AdditionalInfo->keyAndNextEngineParams[keyIndex].nextEngineParams.params.ccParams.h_CcNode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    if (p_KeyParams->p_Mask)
        memcpy(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].mask,
               p_KeyParams->p_Mask, p_CcNode->userSizeOfExtraction);
    else
        memset(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].mask, 0xFF,
               p_CcNode->userSizeOfExtraction);

    /* Update numOfKeys */
    if (add)
        p_AdditionalInfo->numOfKeys = (uint8_t)(p_CcNode->numOfKeys + 1);
    else
        p_AdditionalInfo->numOfKeys = (uint8_t)p_CcNode->numOfKeys;

    /* Allocate new tables in MURAM: keys match table and action descriptors table */
    err = BuildNewNodeCommonPart(p_CcNode, &size, p_AdditionalInfo);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /* Check that manip is legal and what requiredAction is necessary for this manip */
    if (p_KeyParams->ccNextEngineParams.h_Manip)
    {
        err = FmPcdManipCheckParamsForCcNextEngine(
                &p_KeyParams->ccNextEngineParams, &requiredAction);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction =
            requiredAction;
    p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction |=
            UPDATE_CC_WITH_TREE;

    /* Update new Ad and new Key Table according to new requirement */
    i = 0;
    for (j = 0; j < p_AdditionalInfo->numOfKeys; j++)
    {
        p_AdTableNewTmp =
                PTR_MOVE(p_AdditionalInfo->p_AdTableNew, j*FM_PCD_CC_AD_ENTRY_SIZE);

        if (j == keyIndex)
        {
            if (p_KeyParams->ccNextEngineParams.statisticsEn)
            {
                /* Allocate a statistics object that holds statistics AD and counters.
                 - For added key - New statistics AD and counters pointer need to be allocated
                 new statistics object. If statistics were enabled, we need to replace the
                 existing descriptor with a new descriptor with nullified counters.
                 */
                p_StatsObj = GetStatsObj(p_CcNode);
                ASSERT_COND(p_StatsObj);

                /* Store allocated statistics object */
                ASSERT_COND(keyIndex < CC_MAX_NUM_OF_KEYS);
                p_AdditionalInfo->keyAndNextEngineParams[keyIndex].p_StatsObj =
                        p_StatsObj;

                statsParams.h_StatsAd = p_StatsObj->h_StatsAd;
                statsParams.h_StatsCounters = p_StatsObj->h_StatsCounters;
#if (DPAA_VERSION >= 11)
                statsParams.h_StatsFLRs = p_CcNode->h_StatsFLRs;

#endif /* (DPAA_VERSION >= 11) */

                /* Building action descriptor for the received new key */
                NextStepAd(p_AdTableNewTmp, &statsParams,
                           &p_KeyParams->ccNextEngineParams, p_FmPcd);
            }
            else
            {
                /* Building action descriptor for the received new key */
                NextStepAd(p_AdTableNewTmp, NULL,
                           &p_KeyParams->ccNextEngineParams, p_FmPcd);
            }

            /* Copy the received new key into keys match table */
            p_KeysMatchTableNewTmp =
                    PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, j*size*sizeof(uint8_t));

            MemCpy8((void*)p_KeysMatchTableNewTmp, p_KeyParams->p_Key,
                        p_CcNode->userSizeOfExtraction);

            /* Update mask for the received new key */
            if (p_CcNode->lclMask)
            {
                if (p_KeyParams->p_Mask)
                {
                    MemCpy8(PTR_MOVE(p_KeysMatchTableNewTmp,
                            p_CcNode->ccKeySizeAccExtraction),
                                p_KeyParams->p_Mask,
                                p_CcNode->userSizeOfExtraction);
                }
                else
                    if (p_CcNode->ccKeySizeAccExtraction > 4)
                    {
                        MemSet8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                   0xff, p_CcNode->userSizeOfExtraction);
                    }
                    else
                    {
                        MemCpy8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                    p_CcNode->p_GlblMask,
                                    p_CcNode->userSizeOfExtraction);
                    }
            }

            /* If key modification requested, the old entry is omitted and replaced by the new parameters */
            if (!add)
                i++;
        }
        else
        {
            /* Copy existing action descriptors to the newly allocated Ad table */
            p_AdTableOldTmp =
                    PTR_MOVE(p_AdditionalInfo->p_AdTableOld, i*FM_PCD_CC_AD_ENTRY_SIZE);
            MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp,
                       FM_PCD_CC_AD_ENTRY_SIZE);

            /* Copy existing keys and their masks to the newly allocated keys match table */
            p_KeysMatchTableNewTmp =
                    PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, j * size * sizeof(uint8_t));
            p_KeysMatchTableOldTmp =
                    PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableOld, i * size * sizeof(uint8_t));

            if (p_CcNode->lclMask)
            {
                if (prvLclMask)
                {
                    MemCpy8(
                            PTR_MOVE(p_KeysMatchTableNewTmp, p_CcNode->ccKeySizeAccExtraction),
                            PTR_MOVE(p_KeysMatchTableOldTmp, p_CcNode->ccKeySizeAccExtraction),
                            p_CcNode->ccKeySizeAccExtraction);
                }
                else
                {
                    p_KeysMatchTableOldTmp =
                            PTR_MOVE(p_CcNode->h_KeysMatchTable,
                                    i * (int)p_CcNode->ccKeySizeAccExtraction * sizeof(uint8_t));

                    if (p_CcNode->ccKeySizeAccExtraction > 4)
                    {
                        MemSet8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                   0xff, p_CcNode->userSizeOfExtraction);
                    }
                    else
                    {
                        MemCpy8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                   p_CcNode->p_GlblMask,
                                   p_CcNode->userSizeOfExtraction);
                    }
                }
            }

            MemCpy8(p_KeysMatchTableNewTmp, p_KeysMatchTableOldTmp,
                       p_CcNode->ccKeySizeAccExtraction);

            i++;
        }
    }

    /* Miss action descriptor */
    p_AdTableNewTmp =
            PTR_MOVE(p_AdditionalInfo->p_AdTableNew, j * FM_PCD_CC_AD_ENTRY_SIZE);
    p_AdTableOldTmp =
            PTR_MOVE(p_AdditionalInfo->p_AdTableOld, i * FM_PCD_CC_AD_ENTRY_SIZE);
    MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp, FM_PCD_CC_AD_ENTRY_SIZE);

    if (!NCSW_LIST_IsEmpty(&p_CcNode->ccTreesLst))
    {
        NCSW_LIST_FOR_EACH(p_Pos, &p_CcNode->ccTreesLst)
        {
            p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);
            ASSERT_COND(p_CcNodeInformation->h_CcNode);
            /* Update the manipulation which has to be updated from parameters of the port */
            /* It's has to be updated with restrictions defined in the function */
            err =
                    SetRequiredAction(
                            p_CcNode->h_FmPcd,
                            p_CcNode->shadowAction
                                    | p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction,
                            &p_AdditionalInfo->keyAndNextEngineParams[keyIndex],
                            PTR_MOVE(p_AdditionalInfo->p_AdTableNew, keyIndex*FM_PCD_CC_AD_ENTRY_SIZE),
                            1, p_CcNodeInformation->h_CcNode);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));

            err =
                    CcUpdateParam(
                            p_CcNode->h_FmPcd,
                            NULL,
                            NULL,
                            &p_AdditionalInfo->keyAndNextEngineParams[keyIndex],
                            1,
                            PTR_MOVE(p_AdditionalInfo->p_AdTableNew, keyIndex*FM_PCD_CC_AD_ENTRY_SIZE),
                            TRUE, p_CcNodeInformation->index,
                            p_CcNodeInformation->h_CcNode, TRUE);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));
        }
    }

    if (p_CcNode->lclMask)
        memset(p_CcNode->p_GlblMask, 0xff, CC_GLBL_MASK_SIZE * sizeof(uint8_t));

    if (p_KeyParams->ccNextEngineParams.nextEngine == e_FM_PCD_CC)
        p_AdditionalInfo->h_NodeForAdd =
                p_KeyParams->ccNextEngineParams.params.ccParams.h_CcNode;
    if (p_KeyParams->ccNextEngineParams.h_Manip)
        p_AdditionalInfo->h_ManipForAdd =
                p_KeyParams->ccNextEngineParams.h_Manip;

#if (DPAA_VERSION >= 11)
    if ((p_KeyParams->ccNextEngineParams.nextEngine == e_FM_PCD_FR)
            && (p_KeyParams->ccNextEngineParams.params.frParams.h_FrmReplic))
        p_AdditionalInfo->h_FrmReplicForAdd =
                p_KeyParams->ccNextEngineParams.params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */

    if (!add)
    {
        if (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
            p_AdditionalInfo->h_NodeForRmv =
                    p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.ccParams.h_CcNode;

        if (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip)
            p_AdditionalInfo->h_ManipForRmv =
                    p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip;

        /* If statistics were previously enabled, store the old statistics object to be released */
        if (p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj)
        {
            p_AdditionalInfo->p_StatsObjForRmv =
                    p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj;
        }

#if (DPAA_VERSION >= 11)
        if ((p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic))
            p_AdditionalInfo->h_FrmReplicForRmv =
                    p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */
    }

    return E_OK;
}

static t_Error BuildNewNodeRemoveKey(
        t_FmPcdCcNode *p_CcNode, uint16_t keyIndex,
        t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo)
{
    int i = 0, j = 0;
    t_Handle p_AdTableNewTmp, p_KeysMatchTableNewTmp;
    t_Handle p_KeysMatchTableOldTmp, p_AdTableOldTmp;
    int size;
    t_Error err = E_OK;

    /*save new numOfKeys*/
    p_AdditionalInfo->numOfKeys = (uint16_t)(p_CcNode->numOfKeys - 1);

    /*function which allocates in the memory new KeyTbl, AdTbl*/
    err = BuildNewNodeCommonPart(p_CcNode, &size, p_AdditionalInfo);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /*update new Ad and new Key Table according to new requirement*/
    for (i = 0, j = 0; j < p_CcNode->numOfKeys; i++, j++)
    {
        if (j == keyIndex)
            j++;

        if (j == p_CcNode->numOfKeys)
            break;
        p_AdTableNewTmp =
                PTR_MOVE(p_AdditionalInfo->p_AdTableNew, i * FM_PCD_CC_AD_ENTRY_SIZE);
        p_AdTableOldTmp =
                PTR_MOVE(p_AdditionalInfo->p_AdTableOld, j * FM_PCD_CC_AD_ENTRY_SIZE);
        MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp, FM_PCD_CC_AD_ENTRY_SIZE);

        p_KeysMatchTableOldTmp =
                PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableOld, j * size * sizeof(uint8_t));
        p_KeysMatchTableNewTmp =
                PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, i * size * sizeof(uint8_t));
        MemCpy8(p_KeysMatchTableNewTmp, p_KeysMatchTableOldTmp,
                   size * sizeof(uint8_t));
    }

    p_AdTableNewTmp =
            PTR_MOVE(p_AdditionalInfo->p_AdTableNew, i * FM_PCD_CC_AD_ENTRY_SIZE);
    p_AdTableOldTmp =
            PTR_MOVE(p_AdditionalInfo->p_AdTableOld, j * FM_PCD_CC_AD_ENTRY_SIZE);
    MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp, FM_PCD_CC_AD_ENTRY_SIZE);

    if (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
            == e_FM_PCD_CC)
        p_AdditionalInfo->h_NodeForRmv =
                p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.ccParams.h_CcNode;

    if (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip)
        p_AdditionalInfo->h_ManipForRmv =
                p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip;

    /* If statistics were previously enabled, store the old statistics object to be released */
    if (p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj)
    {
        p_AdditionalInfo->p_StatsObjForRmv =
                p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj;
    }

#if (DPAA_VERSION >= 11)
    if ((p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
            == e_FM_PCD_FR)
            && (p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic))
        p_AdditionalInfo->h_FrmReplicForRmv =
                p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */

    return E_OK;
}

static t_Error BuildNewNodeModifyKey(
        t_FmPcdCcNode *p_CcNode, uint16_t keyIndex, uint8_t *p_Key,
        uint8_t *p_Mask, t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    t_Error err = E_OK;
    t_Handle p_AdTableNewTmp, p_KeysMatchTableNewTmp;
    t_Handle p_KeysMatchTableOldTmp, p_AdTableOldTmp;
    int size;
    int i = 0, j = 0;
    bool prvLclMask;
    t_FmPcdStatsObj *p_StatsObj, tmpStatsObj;
    p_AdditionalInfo->numOfKeys = p_CcNode->numOfKeys;

    prvLclMask = p_CcNode->lclMask;

    /* Check that new key is not require update of localMask */
    err = UpdateGblMask(p_CcNode, p_CcNode->ccKeySizeAccExtraction, p_Mask);
    if (err)
        RETURN_ERROR(MAJOR, err, (NO_MSG));

    /* Update internal data structure with new next engine for the given index */
    memcpy(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].key, p_Key,
           p_CcNode->userSizeOfExtraction);

    if (p_Mask)
        memcpy(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].mask, p_Mask,
               p_CcNode->userSizeOfExtraction);
    else
        memset(p_AdditionalInfo->keyAndNextEngineParams[keyIndex].mask, 0xFF,
               p_CcNode->userSizeOfExtraction);

    /*function which build in the memory new KeyTbl, AdTbl*/
    err = BuildNewNodeCommonPart(p_CcNode, &size, p_AdditionalInfo);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /*fill the New AdTable and New KeyTable*/
    for (j = 0, i = 0; j < p_AdditionalInfo->numOfKeys; j++, i++)
    {
        p_AdTableNewTmp =
                PTR_MOVE(p_AdditionalInfo->p_AdTableNew, j*FM_PCD_CC_AD_ENTRY_SIZE);
        p_AdTableOldTmp =
                PTR_MOVE(p_AdditionalInfo->p_AdTableOld, i*FM_PCD_CC_AD_ENTRY_SIZE);

        MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp, FM_PCD_CC_AD_ENTRY_SIZE);

        if (j == keyIndex)
        {
            ASSERT_COND(keyIndex < CC_MAX_NUM_OF_KEYS);
            if (p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj)
            {
                /* As statistics were enabled, we need to update the existing
                 statistics descriptor with a new nullified counters. */
                p_StatsObj = GetStatsObj(p_CcNode);
                ASSERT_COND(p_StatsObj);

                SetStatsCounters(
                        p_AdTableNewTmp,
                        (uint32_t)((XX_VirtToPhys(p_StatsObj->h_StatsCounters)
                                - p_FmPcd->physicalMuramBase)));

                tmpStatsObj.h_StatsAd = p_StatsObj->h_StatsAd;
                tmpStatsObj.h_StatsCounters = p_StatsObj->h_StatsCounters;

                /* As we need to replace only the counters, we build a new statistics
                 object that holds the old AD and the new counters - this will be the
                 currently used statistics object.
                 The newly allocated AD is not required and may be released back to
                 the available objects with the previous counters pointer. */
                p_StatsObj->h_StatsAd =
                        p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj->h_StatsAd;

                p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj->h_StatsAd =
                        tmpStatsObj.h_StatsAd;

                /* Store allocated statistics object */
                p_AdditionalInfo->keyAndNextEngineParams[keyIndex].p_StatsObj =
                        p_StatsObj;

                /* As statistics were previously enabled, store the old statistics object to be released */
                p_AdditionalInfo->p_StatsObjForRmv =
                        p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj;
            }

            p_KeysMatchTableNewTmp =
                    PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, j * size * sizeof(uint8_t));

            MemCpy8(p_KeysMatchTableNewTmp, p_Key,
                        p_CcNode->userSizeOfExtraction);

            if (p_CcNode->lclMask)
            {
                if (p_Mask)
                    MemCpy8(PTR_MOVE(p_KeysMatchTableNewTmp,
                            p_CcNode->ccKeySizeAccExtraction),
                                p_Mask, p_CcNode->userSizeOfExtraction);
                else
                    if (p_CcNode->ccKeySizeAccExtraction > 4)
                        MemSet8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                   0xff, p_CcNode->userSizeOfExtraction);
                    else
                        MemCpy8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                    p_CcNode->p_GlblMask,
                                    p_CcNode->userSizeOfExtraction);
            }
        }
        else
        {
            p_KeysMatchTableNewTmp =
                    PTR_MOVE(p_AdditionalInfo->p_KeysMatchTableNew, j * size * sizeof(uint8_t));
            p_KeysMatchTableOldTmp =
                    PTR_MOVE(p_CcNode->h_KeysMatchTable, i * size * sizeof(uint8_t));

            if (p_CcNode->lclMask)
            {
                if (prvLclMask)
                    MemCpy8(
                            PTR_MOVE(p_KeysMatchTableNewTmp, p_CcNode->ccKeySizeAccExtraction),
                            PTR_MOVE(p_KeysMatchTableOldTmp, p_CcNode->ccKeySizeAccExtraction),
                            p_CcNode->userSizeOfExtraction);
                else
                {
                    p_KeysMatchTableOldTmp =
                            PTR_MOVE(p_CcNode->h_KeysMatchTable,
                                     i * (int)p_CcNode->ccKeySizeAccExtraction * sizeof(uint8_t));

                    if (p_CcNode->ccKeySizeAccExtraction > 4)
                        MemSet8(PTR_MOVE(p_KeysMatchTableNewTmp,
                                p_CcNode->ccKeySizeAccExtraction),
                                   0xff, p_CcNode->userSizeOfExtraction);
                    else
                        MemCpy8(
                                PTR_MOVE(p_KeysMatchTableNewTmp, p_CcNode->ccKeySizeAccExtraction),
                                p_CcNode->p_GlblMask,
                                p_CcNode->userSizeOfExtraction);
                }
            }
            MemCpy8((void*)p_KeysMatchTableNewTmp, p_KeysMatchTableOldTmp,
                       p_CcNode->ccKeySizeAccExtraction);
        }
    }

    p_AdTableNewTmp =
            PTR_MOVE(p_AdditionalInfo->p_AdTableNew, j * FM_PCD_CC_AD_ENTRY_SIZE);
    p_AdTableOldTmp = PTR_MOVE(p_CcNode->h_AdTable, i * FM_PCD_CC_AD_ENTRY_SIZE);

    MemCpy8(p_AdTableNewTmp, p_AdTableOldTmp, FM_PCD_CC_AD_ENTRY_SIZE);

    return E_OK;
}

static t_Error BuildNewNodeModifyNextEngine(
        t_Handle h_FmPcd, t_Handle h_FmPcdCcNodeOrTree, uint16_t keyIndex,
        t_FmPcdCcNextEngineParams *p_CcNextEngineParams, t_List *h_OldLst,
        t_List *h_NewLst, t_FmPcdModifyCcKeyAdditionalParams *p_AdditionalInfo)
{
    t_Error err = E_OK;
    uint32_t requiredAction = 0;
    t_List *p_Pos;
    t_CcNodeInformation *p_CcNodeInformation, ccNodeInfo;
    t_Handle p_Ad;
    t_FmPcdCcNode *p_FmPcdCcNode1 = NULL;
    t_FmPcdCcTree *p_FmPcdCcTree = NULL;
    t_FmPcdStatsObj *p_StatsObj;
    t_FmPcdCcStatsParams statsParams = { 0 };

    ASSERT_COND(p_CcNextEngineParams);

    /* check that new NIA is legal */
    if (!p_AdditionalInfo->tree)
        err = ValidateNextEngineParams(
                h_FmPcd, p_CcNextEngineParams,
                ((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->statisticsMode);
    else
        /* Statistics are not supported for CC root */
        err = ValidateNextEngineParams(h_FmPcd, p_CcNextEngineParams,
                                       e_FM_PCD_CC_STATS_MODE_NONE);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /* Update internal data structure for next engine per index (index - key) */
    memcpy(&p_AdditionalInfo->keyAndNextEngineParams[keyIndex].nextEngineParams,
           p_CcNextEngineParams, sizeof(t_FmPcdCcNextEngineParams));

    /* Check that manip is legal and what requiredAction is necessary for this manip */
    if (p_CcNextEngineParams->h_Manip)
    {
        err = FmPcdManipCheckParamsForCcNextEngine(p_CcNextEngineParams,
                                                   &requiredAction);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    if (!p_AdditionalInfo->tree)
    {
        p_FmPcdCcNode1 = (t_FmPcdCcNode *)h_FmPcdCcNodeOrTree;
        p_AdditionalInfo->numOfKeys = p_FmPcdCcNode1->numOfKeys;
        p_Ad = p_FmPcdCcNode1->h_AdTable;

        if (p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
            p_AdditionalInfo->h_NodeForRmv =
                    p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.params.ccParams.h_CcNode;

        if (p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip)
            p_AdditionalInfo->h_ManipForRmv =
                    p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip;

#if (DPAA_VERSION >= 11)
        if ((p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && (p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic))
            p_AdditionalInfo->h_FrmReplicForRmv =
                    p_FmPcdCcNode1->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */
    }
    else
    {
        p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcNodeOrTree;
        p_Ad = UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr);

        if (p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
            p_AdditionalInfo->h_NodeForRmv =
                    p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.params.ccParams.h_CcNode;

        if (p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip)
            p_AdditionalInfo->h_ManipForRmv =
                    p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.h_Manip;

#if (DPAA_VERSION >= 11)
        if ((p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && (p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic))
            p_AdditionalInfo->h_FrmReplicForRmv =
                    p_FmPcdCcTree->keyAndNextEngineParams[keyIndex].nextEngineParams.params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */
    }

    if ((p_CcNextEngineParams->nextEngine == e_FM_PCD_CC)
            && p_CcNextEngineParams->h_Manip)
    {
        err = AllocAndFillAdForContLookupManip(
                p_CcNextEngineParams->params.ccParams.h_CcNode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    ASSERT_COND(p_Ad);

    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
    ccNodeInfo.h_CcNode = PTR_MOVE(p_Ad, keyIndex * FM_PCD_CC_AD_ENTRY_SIZE);

    /* If statistics were enabled, this Ad is the statistics Ad. Need to follow its
     nextAction to retrieve the actual Nia-Ad. If statistics should remain enabled,
     only the actual Nia-Ad should be modified. */
    if ((!p_AdditionalInfo->tree)
            && (((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->keyAndNextEngineParams[keyIndex].p_StatsObj)
            && (p_CcNextEngineParams->statisticsEn))
        ccNodeInfo.h_CcNode =
                ((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->keyAndNextEngineParams[keyIndex].p_StatsObj->h_StatsAd;

    EnqueueNodeInfoToRelevantLst(h_OldLst, &ccNodeInfo, NULL);

    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
    p_Ad = GetNewAd(h_FmPcdCcNodeOrTree, p_AdditionalInfo->tree);
    if (!p_Ad)
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM allocation for CC node action descriptor"));
    MemSet8((uint8_t *)p_Ad, 0, FM_PCD_CC_AD_ENTRY_SIZE);

    /* If statistics were not enabled before, but requested now -  Allocate a statistics
     object that holds statistics AD and counters. */
    if ((!p_AdditionalInfo->tree)
            && (!((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->keyAndNextEngineParams[keyIndex].p_StatsObj)
            && (p_CcNextEngineParams->statisticsEn))
    {
        p_StatsObj = GetStatsObj((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree);
        ASSERT_COND(p_StatsObj);

        /* Store allocated statistics object */
        p_AdditionalInfo->keyAndNextEngineParams[keyIndex].p_StatsObj =
                p_StatsObj;

        statsParams.h_StatsAd = p_StatsObj->h_StatsAd;
        statsParams.h_StatsCounters = p_StatsObj->h_StatsCounters;

#if (DPAA_VERSION >= 11)
        statsParams.h_StatsFLRs =
                ((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->h_StatsFLRs;

#endif /* (DPAA_VERSION >= 11) */

        NextStepAd(p_Ad, &statsParams, p_CcNextEngineParams, h_FmPcd);
    }
    else
        NextStepAd(p_Ad, NULL, p_CcNextEngineParams, h_FmPcd);

    ccNodeInfo.h_CcNode = p_Ad;
    EnqueueNodeInfoToRelevantLst(h_NewLst, &ccNodeInfo, NULL);

    p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction =
            requiredAction;
    p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction |=
            UPDATE_CC_WITH_TREE;

    if (!p_AdditionalInfo->tree)
    {
        ASSERT_COND(p_FmPcdCcNode1);
        if (!NCSW_LIST_IsEmpty(&p_FmPcdCcNode1->ccTreesLst))
        {
            NCSW_LIST_FOR_EACH(p_Pos, &p_FmPcdCcNode1->ccTreesLst)
            {
                p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);

                ASSERT_COND(p_CcNodeInformation->h_CcNode);
                /* Update the manipulation which has to be updated from parameters of the port
                 it's has to be updated with restrictions defined in the function */

                err =
                        SetRequiredAction(
                                p_FmPcdCcNode1->h_FmPcd,
                                p_FmPcdCcNode1->shadowAction
                                        | p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction,
                                &p_AdditionalInfo->keyAndNextEngineParams[keyIndex],
                                p_Ad, 1, p_CcNodeInformation->h_CcNode);
                if (err)
                    RETURN_ERROR(MAJOR, err, (NO_MSG));

                err = CcUpdateParam(
                        p_FmPcdCcNode1->h_FmPcd, NULL, NULL,
                        &p_AdditionalInfo->keyAndNextEngineParams[keyIndex], 1,
                        p_Ad, TRUE, p_CcNodeInformation->index,
                        p_CcNodeInformation->h_CcNode, TRUE);
                if (err)
                    RETURN_ERROR(MAJOR, err, (NO_MSG));
            }
        }
    }
    else
    {
        ASSERT_COND(p_FmPcdCcTree);

        err =
                SetRequiredAction(
                        h_FmPcd,
                        p_FmPcdCcTree->requiredAction
                                | p_AdditionalInfo->keyAndNextEngineParams[keyIndex].requiredAction,
                        &p_AdditionalInfo->keyAndNextEngineParams[keyIndex],
                        p_Ad, 1, (t_Handle)p_FmPcdCcTree);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));

        err = CcUpdateParam(h_FmPcd, NULL, NULL,
                            &p_AdditionalInfo->keyAndNextEngineParams[keyIndex],
                            1, p_Ad, TRUE, 0, (t_Handle)p_FmPcdCcTree, TRUE);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    if (p_CcNextEngineParams->nextEngine == e_FM_PCD_CC)
        p_AdditionalInfo->h_NodeForAdd =
                p_CcNextEngineParams->params.ccParams.h_CcNode;
    if (p_CcNextEngineParams->h_Manip)
        p_AdditionalInfo->h_ManipForAdd = p_CcNextEngineParams->h_Manip;

    /* If statistics were previously enabled, but now are disabled,
     store the old statistics object to be released */
    if ((!p_AdditionalInfo->tree)
            && (((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->keyAndNextEngineParams[keyIndex].p_StatsObj)
            && (!p_CcNextEngineParams->statisticsEn))
    {
        p_AdditionalInfo->p_StatsObjForRmv =
                ((t_FmPcdCcNode *)h_FmPcdCcNodeOrTree)->keyAndNextEngineParams[keyIndex].p_StatsObj;


        p_AdditionalInfo->keyAndNextEngineParams[keyIndex].p_StatsObj = NULL;
    }
#if (DPAA_VERSION >= 11)
    if ((p_CcNextEngineParams->nextEngine == e_FM_PCD_FR)
            && (p_CcNextEngineParams->params.frParams.h_FrmReplic))
        p_AdditionalInfo->h_FrmReplicForAdd =
                p_CcNextEngineParams->params.frParams.h_FrmReplic;
#endif /* (DPAA_VERSION >= 11) */

    return E_OK;
}

static void UpdateAdPtrOfNodesWhichPointsOnCrntMdfNode(
        t_FmPcdCcNode *p_CrntMdfNode, t_List *h_OldLst,
        t_FmPcdCcNextEngineParams **p_NextEngineParams)
{
    t_CcNodeInformation *p_CcNodeInformation;
    t_FmPcdCcNode *p_NodePtrOnCurrentMdfNode = NULL;
    t_List *p_Pos;
    int i = 0;
    t_Handle p_AdTablePtOnCrntCurrentMdfNode/*, p_AdTableNewModified*/;
    t_CcNodeInformation ccNodeInfo;

    NCSW_LIST_FOR_EACH(p_Pos, &p_CrntMdfNode->ccPrevNodesLst)
    {
        p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);
        p_NodePtrOnCurrentMdfNode =
                (t_FmPcdCcNode *)p_CcNodeInformation->h_CcNode;

        ASSERT_COND(p_NodePtrOnCurrentMdfNode);

        /* Search in the previous node which exact index points on this current modified node for getting AD */
        for (i = 0; i < p_NodePtrOnCurrentMdfNode->numOfKeys + 1; i++)
        {
            if (p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
            {
                if (p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode
                        == (t_Handle)p_CrntMdfNode)
                {
                    if (p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip)
                        p_AdTablePtOnCrntCurrentMdfNode = p_CrntMdfNode->h_Ad;
                    else
                        if (p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].p_StatsObj)
                            p_AdTablePtOnCrntCurrentMdfNode =
                                    p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].p_StatsObj->h_StatsAd;
                        else
                            p_AdTablePtOnCrntCurrentMdfNode =
                                    PTR_MOVE(p_NodePtrOnCurrentMdfNode->h_AdTable, i*FM_PCD_CC_AD_ENTRY_SIZE);

                    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                    ccNodeInfo.h_CcNode = p_AdTablePtOnCrntCurrentMdfNode;
                    EnqueueNodeInfoToRelevantLst(h_OldLst, &ccNodeInfo, NULL);

                    if (!(*p_NextEngineParams))
                        *p_NextEngineParams =
                                &p_NodePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams;
                }
            }
        }

        ASSERT_COND(i != p_NodePtrOnCurrentMdfNode->numOfKeys);
    }
}

static void UpdateAdPtrOfTreesWhichPointsOnCrntMdfNode(
        t_FmPcdCcNode *p_CrntMdfNode, t_List *h_OldLst,
        t_FmPcdCcNextEngineParams **p_NextEngineParams)
{
    t_CcNodeInformation *p_CcNodeInformation;
    t_FmPcdCcTree *p_TreePtrOnCurrentMdfNode = NULL;
    t_List *p_Pos;
    int i = 0;
    t_Handle p_AdTableTmp;
    t_CcNodeInformation ccNodeInfo;

    NCSW_LIST_FOR_EACH(p_Pos, &p_CrntMdfNode->ccTreeIdLst)
    {
        p_CcNodeInformation = CC_NODE_F_OBJECT(p_Pos);
        p_TreePtrOnCurrentMdfNode =
                (t_FmPcdCcTree *)p_CcNodeInformation->h_CcNode;

        ASSERT_COND(p_TreePtrOnCurrentMdfNode);

        /*search in the trees which exact index points on this current modified node for getting AD */
        for (i = 0; i < p_TreePtrOnCurrentMdfNode->numOfEntries; i++)
        {
            if (p_TreePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
            {
                if (p_TreePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode
                        == (t_Handle)p_CrntMdfNode)
                {
                    p_AdTableTmp =
                            UINT_TO_PTR(p_TreePtrOnCurrentMdfNode->ccTreeBaseAddr + i*FM_PCD_CC_AD_ENTRY_SIZE);
                    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                    ccNodeInfo.h_CcNode = p_AdTableTmp;
                    EnqueueNodeInfoToRelevantLst(h_OldLst, &ccNodeInfo, NULL);

                    if (!(*p_NextEngineParams))
                        *p_NextEngineParams =
                                &p_TreePtrOnCurrentMdfNode->keyAndNextEngineParams[i].nextEngineParams;
                }
            }
        }

        ASSERT_COND(i == p_TreePtrOnCurrentMdfNode->numOfEntries);
    }
}

static t_FmPcdModifyCcKeyAdditionalParams * ModifyNodeCommonPart(
        t_Handle h_FmPcdCcNodeOrTree, uint16_t keyIndex,
        e_ModifyState modifyState, bool ttlCheck, bool hashCheck, bool tree)
{
    t_FmPcdModifyCcKeyAdditionalParams *p_FmPcdModifyCcKeyAdditionalParams;
    int i = 0, j = 0;
    bool wasUpdate = FALSE;
    t_FmPcdCcNode *p_CcNode = NULL;
    t_FmPcdCcTree *p_FmPcdCcTree;
    uint16_t numOfKeys;
    t_FmPcdCcKeyAndNextEngineParams *p_KeyAndNextEngineParams;

    SANITY_CHECK_RETURN_VALUE(h_FmPcdCcNodeOrTree, E_INVALID_HANDLE, NULL);

    if (!tree)
    {
        p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNodeOrTree;
        numOfKeys = p_CcNode->numOfKeys;

        /* node has to be pointed by another node or tree */

        p_KeyAndNextEngineParams = (t_FmPcdCcKeyAndNextEngineParams *)XX_Malloc(
                sizeof(t_FmPcdCcKeyAndNextEngineParams) * (numOfKeys + 1));
        if (!p_KeyAndNextEngineParams)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Next engine and required action structure"));
            return NULL;
        }
        memcpy(p_KeyAndNextEngineParams, p_CcNode->keyAndNextEngineParams,
               (numOfKeys + 1) * sizeof(t_FmPcdCcKeyAndNextEngineParams));

        if (ttlCheck)
        {
            if ((p_CcNode->parseCode == CC_PC_FF_IPV4TTL)
                    || (p_CcNode->parseCode == CC_PC_FF_IPV6HOP_LIMIT))
            {
                XX_Free(p_KeyAndNextEngineParams);
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("nodeId of CC_PC_FF_IPV4TTL or CC_PC_FF_IPV6HOP_LIMIT can not be used for this operation"));
                return NULL;
            }
        }

        if (hashCheck)
        {
            if (p_CcNode->parseCode == CC_PC_GENERIC_IC_HASH_INDEXED)
            {
                XX_Free(p_KeyAndNextEngineParams);
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("nodeId of CC_PC_GENERIC_IC_HASH_INDEXED can not be used for this operation"));
                return NULL;
            }
        }
    }
    else
    {
        p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcNodeOrTree;
        numOfKeys = p_FmPcdCcTree->numOfEntries;

        p_KeyAndNextEngineParams = (t_FmPcdCcKeyAndNextEngineParams *)XX_Malloc(
                sizeof(t_FmPcdCcKeyAndNextEngineParams)
                        * FM_PCD_MAX_NUM_OF_CC_GROUPS);
        if (!p_KeyAndNextEngineParams)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Next engine and required action structure"));
            return NULL;
        }
        memcpy(p_KeyAndNextEngineParams,
               p_FmPcdCcTree->keyAndNextEngineParams,
               FM_PCD_MAX_NUM_OF_CC_GROUPS
                       * sizeof(t_FmPcdCcKeyAndNextEngineParams));
    }

    p_FmPcdModifyCcKeyAdditionalParams =
            (t_FmPcdModifyCcKeyAdditionalParams *)XX_Malloc(
                    sizeof(t_FmPcdModifyCcKeyAdditionalParams));
    if (!p_FmPcdModifyCcKeyAdditionalParams)
    {
        XX_Free(p_KeyAndNextEngineParams);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Allocation of internal data structure FAILED"));
        return NULL;
    }
    memset(p_FmPcdModifyCcKeyAdditionalParams, 0,
           sizeof(t_FmPcdModifyCcKeyAdditionalParams));

    p_FmPcdModifyCcKeyAdditionalParams->h_CurrentNode = h_FmPcdCcNodeOrTree;
    p_FmPcdModifyCcKeyAdditionalParams->savedKeyIndex = keyIndex;

    while (i < numOfKeys)
    {
        if ((j == keyIndex) && !wasUpdate)
        {
            if (modifyState == e_MODIFY_STATE_ADD)
                j++;
            else
                if (modifyState == e_MODIFY_STATE_REMOVE)
                    i++;
            wasUpdate = TRUE;
        }
        else
        {
            memcpy(&p_FmPcdModifyCcKeyAdditionalParams->keyAndNextEngineParams[j],
                   p_KeyAndNextEngineParams + i,
                   sizeof(t_FmPcdCcKeyAndNextEngineParams));
            i++;
            j++;
        }
    }

    if (keyIndex == numOfKeys)
    {
        if (modifyState == e_MODIFY_STATE_ADD)
            j++;
    }

    memcpy(&p_FmPcdModifyCcKeyAdditionalParams->keyAndNextEngineParams[j],
           p_KeyAndNextEngineParams + numOfKeys,
           sizeof(t_FmPcdCcKeyAndNextEngineParams));

    XX_Free(p_KeyAndNextEngineParams);

    return p_FmPcdModifyCcKeyAdditionalParams;
}

static t_Error UpdatePtrWhichPointOnCrntMdfNode(
        t_FmPcdCcNode *p_CcNode,
        t_FmPcdModifyCcKeyAdditionalParams *p_FmPcdModifyCcKeyAdditionalParams,
        t_List *h_OldLst, t_List *h_NewLst)
{
    t_FmPcdCcNextEngineParams *p_NextEngineParams = NULL;
    t_CcNodeInformation ccNodeInfo = { 0 };
    t_Handle h_NewAd;
    t_Handle h_OrigAd = NULL;

    /* Building a list of all action descriptors that point to the previous node */
    if (!NCSW_LIST_IsEmpty(&p_CcNode->ccPrevNodesLst))
        UpdateAdPtrOfNodesWhichPointsOnCrntMdfNode(p_CcNode, h_OldLst,
                                                   &p_NextEngineParams);

    if (!NCSW_LIST_IsEmpty(&p_CcNode->ccTreeIdLst))
        UpdateAdPtrOfTreesWhichPointsOnCrntMdfNode(p_CcNode, h_OldLst,
                                                   &p_NextEngineParams);

    /* This node must be found as next engine of one of its previous nodes or trees*/
    if (p_NextEngineParams)
    {
        /* Building a new action descriptor that points to the modified node */
        h_NewAd = GetNewAd(p_CcNode, FALSE);
        if (!h_NewAd)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);
        MemSet8(h_NewAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

        h_OrigAd = p_CcNode->h_Ad;
        BuildNewAd(h_NewAd, p_FmPcdModifyCcKeyAdditionalParams, p_CcNode,
                   p_NextEngineParams);

        ccNodeInfo.h_CcNode = h_NewAd;
        EnqueueNodeInfoToRelevantLst(h_NewLst, &ccNodeInfo, NULL);

        if (p_NextEngineParams->h_Manip && !h_OrigAd)
            FmPcdManipUpdateOwner(p_NextEngineParams->h_Manip, FALSE);
    }
    return E_OK;
}

static void UpdateCcRootOwner(t_FmPcdCcTree *p_FmPcdCcTree, bool add)
{
    ASSERT_COND(p_FmPcdCcTree);

    /* this routine must be protected by the calling routine! */

    if (add)
        p_FmPcdCcTree->owners++;
    else
    {
        ASSERT_COND(p_FmPcdCcTree->owners);
        p_FmPcdCcTree->owners--;
    }
}

static t_Error CheckAndSetManipParamsWithCcNodeParams(t_FmPcdCcNode *p_CcNode)
{
    t_Error err = E_OK;
    int i = 0;

    for (i = 0; i < p_CcNode->numOfKeys; i++)
    {
        if (p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip)
        {
            err =
                    FmPcdManipCheckParamsWithCcNodeParams(
                            p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip,
                            (t_Handle)p_CcNode);
            if (err)
                return err;
        }
    }

    return err;
}
static t_Error ValidateAndCalcStatsParams(t_FmPcdCcNode *p_CcNode,
                                          t_FmPcdCcNodeParams *p_CcNodeParam,
                                          uint32_t *p_NumOfRanges,
                                          uint32_t *p_CountersArraySize)
{
    e_FmPcdCcStatsMode statisticsMode = p_CcNode->statisticsMode;
    uint32_t i;

    UNUSED(p_CcNodeParam);

    switch (statisticsMode)
    {
        case e_FM_PCD_CC_STATS_MODE_NONE:
            for (i = 0; i < p_CcNode->numOfKeys; i++)
                if (p_CcNodeParam->keysParams.keyParams[i].ccNextEngineParams.statisticsEn)
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_VALUE,
                            ("Statistics cannot be enabled for key %d when statistics mode was set to 'NONE'", i));
            return E_OK;

        case e_FM_PCD_CC_STATS_MODE_FRAME:
        case e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME:
            *p_NumOfRanges = 1;
            *p_CountersArraySize = 2 * FM_PCD_CC_STATS_COUNTER_SIZE;
            return E_OK;

#if (DPAA_VERSION >= 11)
        case e_FM_PCD_CC_STATS_MODE_RMON:
        {
            uint16_t *p_FrameLengthRanges =
                    p_CcNodeParam->keysParams.frameLengthRanges;
            uint32_t i;

            if (p_FrameLengthRanges[0] <= 0)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Statistics mode"));

            if (p_FrameLengthRanges[0] == 0xFFFF)
            {
                *p_NumOfRanges = 1;
                *p_CountersArraySize = 2 * FM_PCD_CC_STATS_COUNTER_SIZE;
                return E_OK;
            }

            for (i = 1; i < FM_PCD_CC_STATS_MAX_NUM_OF_FLR; i++)
            {
                if (p_FrameLengthRanges[i - 1] >= p_FrameLengthRanges[i])
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_VALUE,
                            ("Frame length range must be larger at least by 1 from preceding range"));

                /* Stop when last range is reached */
                if (p_FrameLengthRanges[i] == 0xFFFF)
                    break;
            }

            if ((i >= FM_PCD_CC_STATS_MAX_NUM_OF_FLR)
                    || (p_FrameLengthRanges[i] != 0xFFFF))
                RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                             ("Last Frame length range must be 0xFFFF"));

            *p_NumOfRanges = i + 1;

            /* Allocate an extra counter for byte count, as counters
             array always begins with byte count */
            *p_CountersArraySize = (*p_NumOfRanges + 1)
                    * FM_PCD_CC_STATS_COUNTER_SIZE;

        }
            return E_OK;
#endif /* (DPAA_VERSION >= 11) */

        default:
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Statistics mode"));
    }
}

static t_Error CheckParams(t_Handle h_FmPcd, t_FmPcdCcNodeParams *p_CcNodeParam,
                           t_FmPcdCcNode *p_CcNode, bool *isKeyTblAlloc)
{
    int tmp = 0;
    t_FmPcdCcKeyParams *p_KeyParams;
    t_Error err;
    uint32_t requiredAction = 0;

    /* Validate statistics parameters */
    err = ValidateAndCalcStatsParams(p_CcNode, p_CcNodeParam,
                                     &(p_CcNode->numOfStatsFLRs),
                                     &(p_CcNode->countersArraySize));
    if (err)
        RETURN_ERROR(MAJOR, err, ("Invalid statistics parameters"));

    /* Validate next engine parameters on Miss */
    err = ValidateNextEngineParams(
            h_FmPcd, &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
            p_CcNode->statisticsMode);
    if (err)
        RETURN_ERROR(MAJOR, err,
                     ("For this node MissNextEngineParams are not valid"));

    if (p_CcNodeParam->keysParams.ccNextEngineParamsForMiss.h_Manip)
    {
        err = FmPcdManipCheckParamsForCcNextEngine(
                &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
                &requiredAction);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    memcpy(&p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams,
           &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
           sizeof(t_FmPcdCcNextEngineParams));

    p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].requiredAction =
            requiredAction;

    if ((p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.nextEngine
            == e_FM_PCD_CC)
            && p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.h_Manip)
    {
        err =
                AllocAndFillAdForContLookupManip(
                        p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.params.ccParams.h_CcNode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    for (tmp = 0; tmp < p_CcNode->numOfKeys; tmp++)
    {
        p_KeyParams = &p_CcNodeParam->keysParams.keyParams[tmp];

        if (!p_KeyParams->p_Key)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("p_Key is not initialized"));

        err = ValidateNextEngineParams(h_FmPcd,
                                       &p_KeyParams->ccNextEngineParams,
                                       p_CcNode->statisticsMode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));

        err = UpdateGblMask(p_CcNode, p_CcNodeParam->keysParams.keySize,
                            p_KeyParams->p_Mask);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));

        if (p_KeyParams->ccNextEngineParams.h_Manip)
        {
            err = FmPcdManipCheckParamsForCcNextEngine(
                    &p_KeyParams->ccNextEngineParams, &requiredAction);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));
        }

        /* Store 'key' parameters - key, mask (if passed by the user) */
        memcpy(p_CcNode->keyAndNextEngineParams[tmp].key, p_KeyParams->p_Key,
               p_CcNodeParam->keysParams.keySize);

        if (p_KeyParams->p_Mask)
            memcpy(p_CcNode->keyAndNextEngineParams[tmp].mask,
                   p_KeyParams->p_Mask, p_CcNodeParam->keysParams.keySize);
        else
            memset((void *)(p_CcNode->keyAndNextEngineParams[tmp].mask), 0xFF,
                   p_CcNodeParam->keysParams.keySize);

        /* Store next engine parameters */
        memcpy(&p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams,
               &p_KeyParams->ccNextEngineParams,
               sizeof(t_FmPcdCcNextEngineParams));

        p_CcNode->keyAndNextEngineParams[tmp].requiredAction = requiredAction;

        if ((p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
                && p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.h_Manip)
        {
            err =
                    AllocAndFillAdForContLookupManip(
                            p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.params.ccParams.h_CcNode);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));
        }
    }

    if (p_CcNode->maxNumOfKeys)
    {
        if (p_CcNode->maxNumOfKeys < p_CcNode->numOfKeys)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("Number of keys exceed the provided maximal number of keys"));
    }

    *isKeyTblAlloc = TRUE;

    return E_OK;
}

static t_Error Ipv4TtlOrIpv6HopLimitCheckParams(
        t_Handle h_FmPcd, t_FmPcdCcNodeParams *p_CcNodeParam,
        t_FmPcdCcNode *p_CcNode, bool *isKeyTblAlloc)
{
    int tmp = 0;
    t_FmPcdCcKeyParams *p_KeyParams;
    t_Error err;
    uint8_t key = 0x01;
    uint32_t requiredAction = 0;

    if (p_CcNode->numOfKeys != 1)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For node of the type IPV4_TTL or IPV6_HOP_LIMIT the maximal supported 'numOfKeys' is 1"));

    if ((p_CcNodeParam->keysParams.maxNumOfKeys)
            && (p_CcNodeParam->keysParams.maxNumOfKeys != 1))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For node of the type IPV4_TTL or IPV6_HOP_LIMIT the maximal supported 'maxNumOfKeys' is 1"));

    /* Validate statistics parameters */
    err = ValidateAndCalcStatsParams(p_CcNode, p_CcNodeParam,
                                     &(p_CcNode->numOfStatsFLRs),
                                     &(p_CcNode->countersArraySize));
    if (err)
        RETURN_ERROR(MAJOR, err, ("Invalid statistics parameters"));

    err = ValidateNextEngineParams(
            h_FmPcd, &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
            p_CcNodeParam->keysParams.statisticsMode);
    if (err)
        RETURN_ERROR(MAJOR, err,
                     ("For this node MissNextEngineParams are not valid"));

    if (p_CcNodeParam->keysParams.ccNextEngineParamsForMiss.h_Manip)
    {
        err = FmPcdManipCheckParamsForCcNextEngine(
                &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
                &requiredAction);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    memcpy(&p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams,
           &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
           sizeof(t_FmPcdCcNextEngineParams));

    p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].requiredAction =
            requiredAction;

    if ((p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.nextEngine
            == e_FM_PCD_CC)
            && p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.h_Manip)
    {
        err =
                AllocAndFillAdForContLookupManip(
                        p_CcNode->keyAndNextEngineParams[p_CcNode->numOfKeys].nextEngineParams.params.ccParams.h_CcNode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));
    }

    for (tmp = 0; tmp < p_CcNode->numOfKeys; tmp++)
    {
        p_KeyParams = &p_CcNodeParam->keysParams.keyParams[tmp];

        if (p_KeyParams->p_Mask)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("For node of the type IPV4_TTL or IPV6_HOP_LIMIT p_Mask can not be initialized"));

        if (memcmp(p_KeyParams->p_Key, &key, 1) != 0)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("For node of the type IPV4_TTL or IPV6_HOP_LIMIT p_Key has to be 1"));

        err = ValidateNextEngineParams(h_FmPcd,
                                       &p_KeyParams->ccNextEngineParams,
                                       p_CcNode->statisticsMode);
        if (err)
            RETURN_ERROR(MAJOR, err, (NO_MSG));

        if (p_KeyParams->ccNextEngineParams.h_Manip)
        {
            err = FmPcdManipCheckParamsForCcNextEngine(
                    &p_KeyParams->ccNextEngineParams, &requiredAction);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));
        }

        /* Store 'key' parameters - key (fixed to 0x01), key size of 1 byte and full mask */
        p_CcNode->keyAndNextEngineParams[tmp].key[0] = key;
        p_CcNode->keyAndNextEngineParams[tmp].mask[0] = 0xFF;

        /* Store NextEngine parameters */
        memcpy(&p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams,
               &p_KeyParams->ccNextEngineParams,
               sizeof(t_FmPcdCcNextEngineParams));

        if ((p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
                && p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.h_Manip)
        {
            err =
                    AllocAndFillAdForContLookupManip(
                            p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.params.ccParams.h_CcNode);
            if (err)
                RETURN_ERROR(MAJOR, err, (NO_MSG));
        }
        p_CcNode->keyAndNextEngineParams[tmp].requiredAction = requiredAction;
    }

    *isKeyTblAlloc = FALSE;

    return E_OK;
}

static t_Error IcHashIndexedCheckParams(t_Handle h_FmPcd,
                                        t_FmPcdCcNodeParams *p_CcNodeParam,
                                        t_FmPcdCcNode *p_CcNode,
                                        bool *isKeyTblAlloc)
{
    int tmp = 0, countOnes = 0;
    t_FmPcdCcKeyParams *p_KeyParams;
    t_Error err;
    uint16_t glblMask = p_CcNodeParam->extractCcParams.extractNonHdr.icIndxMask;
    uint16_t countMask = (uint16_t)(glblMask >> 4);
    uint32_t requiredAction = 0;

    if (glblMask & 0x000f)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("icIndxMask has to be with last nibble 0"));

    while (countMask)
    {
        countOnes++;
        countMask = (uint16_t)(countMask >> 1);
    }

    if (!POWER_OF_2(p_CcNode->numOfKeys))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For Node of the type INDEXED numOfKeys has to be powerOfTwo"));

    if (p_CcNode->numOfKeys != ((uint32_t)1 << countOnes))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For Node of the type IC_HASH_INDEXED numOfKeys has to be powerOfTwo"));

    if (p_CcNodeParam->keysParams.maxNumOfKeys
            && (p_CcNodeParam->keysParams.maxNumOfKeys != p_CcNode->numOfKeys))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For Node of the type INDEXED 'maxNumOfKeys' should be 0 or equal 'numOfKeys'"));

    /* Validate statistics parameters */
    err = ValidateAndCalcStatsParams(p_CcNode, p_CcNodeParam,
                                     &(p_CcNode->numOfStatsFLRs),
                                     &(p_CcNode->countersArraySize));
    if (err)
        RETURN_ERROR(MAJOR, err, ("Invalid statistics parameters"));

    err = ValidateNextEngineParams(
            h_FmPcd, &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
            p_CcNode->statisticsMode);
    if (GET_ERROR_TYPE(err) != E_NOT_SUPPORTED)
        RETURN_ERROR(
                MAJOR,
                err,
                ("MissNextEngineParams for the node of the type IC_INDEX_HASH has to be UnInitialized"));

    for (tmp = 0; tmp < p_CcNode->numOfKeys; tmp++)
    {
        p_KeyParams = &p_CcNodeParam->keysParams.keyParams[tmp];

        if (p_KeyParams->p_Mask || p_KeyParams->p_Key)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("For Node of the type IC_HASH_INDEXED p_Key or p_Mask has to be NULL"));

        if ((glblMask & (tmp * 16)) == (tmp * 16))
        {
            err = ValidateNextEngineParams(h_FmPcd,
                                           &p_KeyParams->ccNextEngineParams,
                                           p_CcNode->statisticsMode);
            if (err)
                RETURN_ERROR(
                        MAJOR,
                        err,
                        ("This index has to be initialized for the node of the type IC_INDEX_HASH according to settings of GlobalMask "));

            if (p_KeyParams->ccNextEngineParams.h_Manip)
            {
                err = FmPcdManipCheckParamsForCcNextEngine(
                        &p_KeyParams->ccNextEngineParams, &requiredAction);
                if (err)
                    RETURN_ERROR(MAJOR, err, (NO_MSG));
                p_CcNode->keyAndNextEngineParams[tmp].requiredAction =
                        requiredAction;
            }

            memcpy(&p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams,
                   &p_KeyParams->ccNextEngineParams,
                   sizeof(t_FmPcdCcNextEngineParams));

            if ((p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
                    && p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.h_Manip)
            {
                err =
                        AllocAndFillAdForContLookupManip(
                                p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.params.ccParams.h_CcNode);
                if (err)
                    RETURN_ERROR(MAJOR, err, (NO_MSG));
            }
        }
        else
        {
            err = ValidateNextEngineParams(h_FmPcd,
                                           &p_KeyParams->ccNextEngineParams,
                                           p_CcNode->statisticsMode);
            if (GET_ERROR_TYPE(err) != E_NOT_SUPPORTED)
                RETURN_ERROR(
                        MAJOR,
                        err,
                        ("This index has to be UnInitialized for the node of the type IC_INDEX_HASH according to settings of GlobalMask"));
        }
    }

    *isKeyTblAlloc = FALSE;
    glblMask = htobe16(glblMask);
    memcpy(PTR_MOVE(p_CcNode->p_GlblMask, 2), &glblMask, 2);

    return E_OK;
}

static t_Error ModifyNextEngineParamNode(
        t_Handle h_FmPcd, t_Handle h_FmPcdCcNode, uint16_t keyIndex,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_List h_OldPointersLst, h_NewPointersLst;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);

    if (keyIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("keyIndex > previously cleared last index + 1"));

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_CHANGE, FALSE,
                                             FALSE, FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys
            && !TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
    {
        XX_Free(p_ModifyKeyParams);
        return ERROR_CODE(E_BUSY);
    }

    err = BuildNewNodeModifyNextEngine(h_FmPcd, p_CcNode, keyIndex,
                                       p_FmPcdCcNextEngineParams,
                                       &h_OldPointersLst, &h_NewPointersLst,
                                       p_ModifyKeyParams);
    if (err)
    {
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, FALSE);

    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

static t_Error FindKeyIndex(t_Handle h_CcNode, uint8_t keySize, uint8_t *p_Key,
                            uint8_t *p_Mask, uint16_t *p_KeyIndex)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint8_t tmpMask[FM_PCD_MAX_SIZE_OF_KEY];
    uint16_t i;

    ASSERT_COND(p_Key);
    ASSERT_COND(p_KeyIndex);
    ASSERT_COND(keySize < FM_PCD_MAX_SIZE_OF_KEY);

    if (keySize != p_CcNode->userSizeOfExtraction)
        RETURN_ERROR(
                MINOR, E_INVALID_VALUE,
                ("Key size doesn't match the extraction size of the node"));

    /* If user didn't pass a mask for this key, we'll look for full extraction mask */
    if (!p_Mask)
        memset(tmpMask, 0xFF, keySize);

    for (i = 0; i < p_CcNode->numOfKeys; i++)
    {
        /* Comparing received key */
        if (memcmp(p_Key, p_CcNode->keyAndNextEngineParams[i].key, keySize)
                == 0)
        {
            if (p_Mask)
            {
                /* If a user passed a mask for this key, it must match to the existing key's mask for a correct match */
                if (memcmp(p_Mask, p_CcNode->keyAndNextEngineParams[i].mask,
                           keySize) == 0)
                {
                    *p_KeyIndex = i;
                    return E_OK;
                }
            }
            else
            {
                /* If user didn't pass a mask for this key, check if the existing key mask is full extraction */
                if (memcmp(tmpMask, p_CcNode->keyAndNextEngineParams[i].mask,
                           keySize) == 0)
                {
                    *p_KeyIndex = i;
                    return E_OK;
                }
            }
        }
    }

    return ERROR_CODE(E_NOT_FOUND);
}

static t_Error CalcAndUpdateCcShadow(t_FmPcdCcNode *p_CcNode,
                                     bool isKeyTblAlloc,
                                     uint32_t *p_MatchTableSize,
                                     uint32_t *p_AdTableSize)
{
    uint32_t shadowSize;
    t_Error err;

    /* Calculate keys table maximal size - each entry consists of a key and a mask,
     (if local mask support is requested) */
    *p_MatchTableSize = p_CcNode->ccKeySizeAccExtraction * sizeof(uint8_t)
            * p_CcNode->maxNumOfKeys;

    if (p_CcNode->maskSupport)
        *p_MatchTableSize *= 2;

    /* Calculate next action descriptors table, including one more entry for miss */
    *p_AdTableSize = (uint32_t)((p_CcNode->maxNumOfKeys + 1)
            * FM_PCD_CC_AD_ENTRY_SIZE);

    /* Calculate maximal shadow size of this node.
     All shadow structures will be used for runtime modifications host command. If
     keys table was allocated for this node, the keys table and next engines table may
     be modified in run time (entries added or removed), so shadow tables are requires.
     Otherwise, the only supported runtime modification is a specific next engine update
     and this requires shadow memory of a single AD */

    /* Shadow size should be enough to hold the following 3 structures:
     * 1 - an action descriptor */
    shadowSize = FM_PCD_CC_AD_ENTRY_SIZE;

    /* 2 - keys match table, if was allocated for the current node */
    if (isKeyTblAlloc)
        shadowSize += *p_MatchTableSize;

    /* 3 - next action descriptors table */
    shadowSize += *p_AdTableSize;

    /* Update shadow to the calculated size */
    err = FmPcdUpdateCcShadow(p_CcNode->h_FmPcd, (uint32_t)shadowSize,
                              FM_PCD_CC_AD_TABLE_ALIGN);
    if (err != E_OK)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for CC node shadow"));
    }

    return E_OK;
}

static t_Error AllocStatsObjs(t_FmPcdCcNode *p_CcNode)
{
    t_FmPcdStatsObj *p_StatsObj;
    t_Handle h_FmMuram, h_StatsAd, h_StatsCounters;
    uint32_t i;

    h_FmMuram = FmPcdGetMuramHandle(p_CcNode->h_FmPcd);
    if (!h_FmMuram)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("FM MURAM"));

    /* Allocate statistics ADs and statistics counter. An extra pair (AD + counters)
     will be allocated to support runtime modifications */
    for (i = 0; i < p_CcNode->maxNumOfKeys + 2; i++)
    {
        /* Allocate list object structure */
        p_StatsObj = XX_Malloc(sizeof(t_FmPcdStatsObj));
        if (!p_StatsObj)
        {
            FreeStatObjects(&p_CcNode->availableStatsLst, h_FmMuram);
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Statistics object"));
        }
        memset(p_StatsObj, 0, sizeof(t_FmPcdStatsObj));

        /* Allocate statistics AD from MURAM */
        h_StatsAd = (t_Handle)FM_MURAM_AllocMem(h_FmMuram,
                                                FM_PCD_CC_AD_ENTRY_SIZE,
                                                FM_PCD_CC_AD_TABLE_ALIGN);
        if (!h_StatsAd)
        {
            FreeStatObjects(&p_CcNode->availableStatsLst, h_FmMuram);
            XX_Free(p_StatsObj);
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for statistics ADs"));
        }
        MemSet8(h_StatsAd, 0, FM_PCD_CC_AD_ENTRY_SIZE);

        /* Allocate statistics counters from MURAM */
        h_StatsCounters = (t_Handle)FM_MURAM_AllocMem(
                h_FmMuram, p_CcNode->countersArraySize,
                FM_PCD_CC_AD_TABLE_ALIGN);
        if (!h_StatsCounters)
        {
            FreeStatObjects(&p_CcNode->availableStatsLst, h_FmMuram);
            FM_MURAM_FreeMem(h_FmMuram, h_StatsAd);
            XX_Free(p_StatsObj);
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for statistics counters"));
        }
        MemSet8(h_StatsCounters, 0, p_CcNode->countersArraySize);

        p_StatsObj->h_StatsAd = h_StatsAd;
        p_StatsObj->h_StatsCounters = h_StatsCounters;

        EnqueueStatsObj(&p_CcNode->availableStatsLst, p_StatsObj);
    }

    return E_OK;
}

static t_Error MatchTableGetKeyStatistics(
        t_FmPcdCcNode *p_CcNode, uint16_t keyIndex,
        t_FmPcdCcKeyStatistics *p_KeyStatistics)
{
    uint32_t *p_StatsCounters, i;

    if (p_CcNode->statisticsMode == e_FM_PCD_CC_STATS_MODE_NONE)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("Statistics were not enabled for this match table"));

    if (!p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("Statistics were not enabled for this key"));

    memset(p_KeyStatistics, 0, sizeof(t_FmPcdCcKeyStatistics));

    p_StatsCounters =
            p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj->h_StatsCounters;
    ASSERT_COND(p_StatsCounters);

    p_KeyStatistics->byteCount = GET_UINT32(*p_StatsCounters);

    for (i = 1; i <= p_CcNode->numOfStatsFLRs; i++)
    {
        p_StatsCounters =
                PTR_MOVE(p_StatsCounters, FM_PCD_CC_STATS_COUNTER_SIZE);

        p_KeyStatistics->frameCount += GET_UINT32(*p_StatsCounters);

#if (DPAA_VERSION >= 11)
        p_KeyStatistics->frameLengthRangeCount[i - 1] =
                GET_UINT32(*p_StatsCounters);
#endif /* (DPAA_VERSION >= 11) */
    }

    return E_OK;
}

static t_Error MatchTableSet(t_Handle h_FmPcd, t_FmPcdCcNode *p_CcNode,
                             t_FmPcdCcNodeParams *p_CcNodeParam)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_FmPcdCcNode *p_FmPcdCcNextNode;
    t_Error err = E_OK;
    uint32_t tmp, keySize;
    bool glblMask = FALSE;
    t_FmPcdCcKeyParams *p_KeyParams;
    t_Handle h_FmMuram, p_KeysMatchTblTmp, p_AdTableTmp;
#if (DPAA_VERSION >= 11)
    t_Handle h_StatsFLRs;
#endif /* (DPAA_VERSION >= 11) */
    bool fullField = FALSE;
    ccPrivateInfo_t icCode = CC_PRIVATE_INFO_NONE;
    bool isKeyTblAlloc, fromIc = FALSE;
    uint32_t matchTableSize, adTableSize;
    t_CcNodeInformation ccNodeInfo, *p_CcInformation;
    t_FmPcdStatsObj *p_StatsObj;
    t_FmPcdCcStatsParams statsParams = { 0 };
    t_Handle h_Manip;

    ASSERT_COND(h_FmPcd);
    ASSERT_COND(p_CcNode);
    ASSERT_COND(p_CcNodeParam);

    p_CcNode->p_GlblMask = (t_Handle)XX_Malloc(
            CC_GLBL_MASK_SIZE * sizeof(uint8_t));
    memset(p_CcNode->p_GlblMask, 0, CC_GLBL_MASK_SIZE * sizeof(uint8_t));

    p_CcNode->h_FmPcd = h_FmPcd;
    p_CcNode->numOfKeys = p_CcNodeParam->keysParams.numOfKeys;
    p_CcNode->maxNumOfKeys = p_CcNodeParam->keysParams.maxNumOfKeys;
    p_CcNode->maskSupport = p_CcNodeParam->keysParams.maskSupport;
    p_CcNode->statisticsMode = p_CcNodeParam->keysParams.statisticsMode;

    /* For backward compatibility - even if statistics mode is nullified,
     we'll fix it to frame mode so we can support per-key request for
     statistics using 'statisticsEn' in next engine parameters */
    if (!p_CcNode->maxNumOfKeys
            && (p_CcNode->statisticsMode == e_FM_PCD_CC_STATS_MODE_NONE))
        p_CcNode->statisticsMode = e_FM_PCD_CC_STATS_MODE_FRAME;

    h_FmMuram = FmPcdGetMuramHandle(h_FmPcd);
    if (!h_FmMuram)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("FM MURAM"));

    INIT_LIST(&p_CcNode->ccPrevNodesLst);
    INIT_LIST(&p_CcNode->ccTreeIdLst);
    INIT_LIST(&p_CcNode->ccTreesLst);
    INIT_LIST(&p_CcNode->availableStatsLst);

    p_CcNode->h_Spinlock = XX_InitSpinlock();
    if (!p_CcNode->h_Spinlock)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("CC node spinlock"));
    }

    if ((p_CcNodeParam->extractCcParams.type == e_FM_PCD_EXTRACT_BY_HDR)
            && ((p_CcNodeParam->extractCcParams.extractByHdr.hdr
                    == HEADER_TYPE_IPv4)
                    || (p_CcNodeParam->extractCcParams.extractByHdr.hdr
                            == HEADER_TYPE_IPv6))
            && (p_CcNodeParam->extractCcParams.extractByHdr.type
                    == e_FM_PCD_EXTRACT_FULL_FIELD)
            && ((p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fullField.ipv6
                    == NET_HEADER_FIELD_IPv6_HOP_LIMIT)
                    || (p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fullField.ipv4
                            == NET_HEADER_FIELD_IPv4_TTL)))
    {
        err = Ipv4TtlOrIpv6HopLimitCheckParams(h_FmPcd, p_CcNodeParam, p_CcNode,
                                               &isKeyTblAlloc);
        glblMask = FALSE;
    }
    else
        if ((p_CcNodeParam->extractCcParams.type == e_FM_PCD_EXTRACT_NON_HDR)
                && ((p_CcNodeParam->extractCcParams.extractNonHdr.src
                        == e_FM_PCD_EXTRACT_FROM_KEY)
                        || (p_CcNodeParam->extractCcParams.extractNonHdr.src
                                == e_FM_PCD_EXTRACT_FROM_HASH)
                        || (p_CcNodeParam->extractCcParams.extractNonHdr.src
                                == e_FM_PCD_EXTRACT_FROM_FLOW_ID)))
        {
            if ((p_CcNodeParam->extractCcParams.extractNonHdr.src
                    == e_FM_PCD_EXTRACT_FROM_FLOW_ID)
                    && (p_CcNodeParam->extractCcParams.extractNonHdr.offset != 0))
            {
                DeleteNode(p_CcNode);
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("In the case of the extraction from e_FM_PCD_EXTRACT_FROM_FLOW_ID offset has to be 0"));
            }

            icCode = IcDefineCode(p_CcNodeParam);
            fromIc = TRUE;
            if (icCode == CC_PRIVATE_INFO_NONE)
            {
                DeleteNode(p_CcNode);
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_STATE,
                        ("user asked extraction from IC and field in internal context or action wasn't initialized in the right way"));
            }

            if ((icCode == CC_PRIVATE_INFO_IC_DEQ_FQID_INDEX_LOOKUP)
                    || (icCode == CC_PRIVATE_INFO_IC_HASH_INDEX_LOOKUP))
            {
                err = IcHashIndexedCheckParams(h_FmPcd, p_CcNodeParam, p_CcNode,
                                               &isKeyTblAlloc);
                glblMask = TRUE;
            }
            else
            {
                err = CheckParams(h_FmPcd, p_CcNodeParam, p_CcNode,
                                  &isKeyTblAlloc);
                if (p_CcNode->glblMaskSize)
                    glblMask = TRUE;
            }
        }
        else
        {
            err = CheckParams(h_FmPcd, p_CcNodeParam, p_CcNode, &isKeyTblAlloc);
            if (p_CcNode->glblMaskSize)
                glblMask = TRUE;
        }

    if (err)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    switch (p_CcNodeParam->extractCcParams.type)
    {
        case (e_FM_PCD_EXTRACT_BY_HDR):
            switch (p_CcNodeParam->extractCcParams.extractByHdr.type)
            {
                case (e_FM_PCD_EXTRACT_FULL_FIELD):
                    p_CcNode->parseCode =
                            GetFullFieldParseCode(
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdr,
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdrIndex,
                                    p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fullField);
                    GetSizeHeaderField(
                            p_CcNodeParam->extractCcParams.extractByHdr.hdr,
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fullField,
                            &p_CcNode->sizeOfExtraction);
                    fullField = TRUE;
                    if ((p_CcNode->parseCode != CC_PC_FF_TCI1)
                            && (p_CcNode->parseCode != CC_PC_FF_TCI2)
                            && (p_CcNode->parseCode != CC_PC_FF_MPLS1)
                            && (p_CcNode->parseCode != CC_PC_FF_MPLS_LAST)
                            && (p_CcNode->parseCode != CC_PC_FF_IPV4IPTOS_TC1)
                            && (p_CcNode->parseCode != CC_PC_FF_IPV4IPTOS_TC2)
                            && (p_CcNode->parseCode
                                    != CC_PC_FF_IPTOS_IPV6TC1_IPV6FLOW1)
                            && (p_CcNode->parseCode != CC_PC_FF_IPDSCP)
                            && (p_CcNode->parseCode
                                    != CC_PC_FF_IPTOS_IPV6TC2_IPV6FLOW2)
                            && glblMask)
                    {
                        glblMask = FALSE;
                        p_CcNode->glblMaskSize = 4;
                        p_CcNode->lclMask = TRUE;
                    }
                    break;

                case (e_FM_PCD_EXTRACT_FROM_HDR):
                    p_CcNode->sizeOfExtraction =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromHdr.size;
                    p_CcNode->offset =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromHdr.offset;
                    p_CcNode->userOffset =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromHdr.offset;
                    p_CcNode->parseCode =
                            GetPrParseCode(
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdr,
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdrIndex,
                                    p_CcNode->offset, glblMask,
                                    &p_CcNode->prsArrayOffset);
                    break;

                case (e_FM_PCD_EXTRACT_FROM_FIELD):
                    p_CcNode->offset =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromField.offset;
                    p_CcNode->userOffset =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromField.offset;
                    p_CcNode->sizeOfExtraction =
                            p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromField.size;
                    p_CcNode->parseCode =
                            GetFieldParseCode(
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdr,
                                    p_CcNodeParam->extractCcParams.extractByHdr.extractByHdrType.fromField.field,
                                    p_CcNode->offset,
                                    &p_CcNode->prsArrayOffset,
                                    p_CcNodeParam->extractCcParams.extractByHdr.hdrIndex);
                    break;

                default:
                    DeleteNode(p_CcNode);
                    RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            }
            break;

        case (e_FM_PCD_EXTRACT_NON_HDR):
            /* get the field code for the generic extract */
            p_CcNode->sizeOfExtraction =
                    p_CcNodeParam->extractCcParams.extractNonHdr.size;
            p_CcNode->offset =
                    p_CcNodeParam->extractCcParams.extractNonHdr.offset;
            p_CcNode->userOffset =
                    p_CcNodeParam->extractCcParams.extractNonHdr.offset;
            p_CcNode->parseCode = GetGenParseCode(
                    p_CcNodeParam->extractCcParams.extractNonHdr.src,
                    p_CcNode->offset, glblMask, &p_CcNode->prsArrayOffset,
                    fromIc, icCode);

            if (p_CcNode->parseCode == CC_PC_GENERIC_IC_HASH_INDEXED)
            {
                if ((p_CcNode->offset + p_CcNode->sizeOfExtraction) > 8)
                {
                    DeleteNode(p_CcNode);
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_SELECTION,
                            ("when node of the type CC_PC_GENERIC_IC_HASH_INDEXED offset + size can not be bigger then size of HASH 64 bits (8 bytes)"));
                }
            }
            if ((p_CcNode->parseCode == CC_PC_GENERIC_IC_GMASK)
                    || (p_CcNode->parseCode == CC_PC_GENERIC_IC_HASH_INDEXED))
            {
                p_CcNode->offset += p_CcNode->prsArrayOffset;
                p_CcNode->prsArrayOffset = 0;
            }
            break;

        default:
            DeleteNode(p_CcNode);
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    if (p_CcNode->parseCode == CC_PC_ILLEGAL)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("illegal extraction type"));
    }

    if ((p_CcNode->sizeOfExtraction > FM_PCD_MAX_SIZE_OF_KEY)
            || !p_CcNode->sizeOfExtraction)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("sizeOfExatrction can not be greater than 56 and not 0"));
    }

    if (p_CcNodeParam->keysParams.keySize != p_CcNode->sizeOfExtraction)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("keySize has to be equal to sizeOfExtraction"));
    }

    p_CcNode->userSizeOfExtraction = p_CcNode->sizeOfExtraction;

    if (!glblMask)
        memset(p_CcNode->p_GlblMask, 0xff, CC_GLBL_MASK_SIZE * sizeof(uint8_t));

    err = CheckAndSetManipParamsWithCcNodeParams(p_CcNode);
    if (err != E_OK)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("keySize has to be equal to sizeOfExtraction"));
    }

    /* Calculating matching table entry size by rounding up the user-defined size of extraction to valid entry size */
    GetCcExtractKeySize(p_CcNode->sizeOfExtraction,
                        &p_CcNode->ccKeySizeAccExtraction);

    /* If local mask is used, it is stored next to each key in the keys match table */
    if (p_CcNode->lclMask)
        keySize = (uint32_t)(2 * p_CcNode->ccKeySizeAccExtraction);
    else
        keySize = p_CcNode->ccKeySizeAccExtraction;

    /* Update CC shadow with maximal size required by this node */
    if (p_CcNode->maxNumOfKeys)
    {
        err = CalcAndUpdateCcShadow(p_CcNode, isKeyTblAlloc, &matchTableSize,
                                    &adTableSize);
        if (err != E_OK)
        {
            DeleteNode(p_CcNode);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }

        p_CcNode->keysMatchTableMaxSize = matchTableSize;

        if (p_CcNode->statisticsMode != e_FM_PCD_CC_STATS_MODE_NONE)
        {
            err = AllocStatsObjs(p_CcNode);
            if (err != E_OK)
            {
                DeleteNode(p_CcNode);
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        }

        /* If manipulation will be initialized before this node, it will use the table
         descriptor in the AD table of previous node and this node will need an extra
         AD as his table descriptor. */
        p_CcNode->h_TmpAd = (t_Handle)FM_MURAM_AllocMem(
                h_FmMuram, FM_PCD_CC_AD_ENTRY_SIZE, FM_PCD_CC_AD_TABLE_ALIGN);
        if (!p_CcNode->h_TmpAd)
        {
            DeleteNode(p_CcNode);
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for CC action descriptor"));
        }
    }
    else
    {
        matchTableSize = (uint32_t)(keySize * sizeof(uint8_t)
                * (p_CcNode->numOfKeys + 1));
        adTableSize = (uint32_t)(FM_PCD_CC_AD_ENTRY_SIZE
                * (p_CcNode->numOfKeys + 1));
    }

#if (DPAA_VERSION >= 11)
    switch (p_CcNode->statisticsMode)
    {

        case e_FM_PCD_CC_STATS_MODE_RMON:
            /* If RMON statistics or RMON conditional statistics modes are requested,
             allocate frame length ranges array */
            p_CcNode->h_StatsFLRs = FM_MURAM_AllocMem(
                    h_FmMuram,
                    (uint32_t)(p_CcNode->numOfStatsFLRs)
                            * FM_PCD_CC_STATS_FLR_SIZE,
                    FM_PCD_CC_AD_TABLE_ALIGN);

            if (!p_CcNode->h_StatsFLRs)
            {
                DeleteNode(p_CcNode);
                RETURN_ERROR(
                        MAJOR, E_NO_MEMORY,
                        ("MURAM allocation for CC frame length ranges array"));
            }

            /* Initialize using value received from the user */
            for (tmp = 0; tmp < p_CcNode->numOfStatsFLRs; tmp++)
            {
                uint16_t flr =
                         cpu_to_be16(p_CcNodeParam->keysParams.frameLengthRanges[tmp]);

                h_StatsFLRs =
                        PTR_MOVE(p_CcNode->h_StatsFLRs, tmp * FM_PCD_CC_STATS_FLR_SIZE);

                MemCpy8(h_StatsFLRs,
                            &flr,
                            FM_PCD_CC_STATS_FLR_SIZE);
            }
            break;

        default:
            break;
    }
#endif /* (DPAA_VERSION >= 11) */

    /* Allocate keys match table. Not required for some CC nodes, for example for IPv4 TTL
     identification, IPv6 hop count identification, etc. */
    if (isKeyTblAlloc)
    {
        p_CcNode->h_KeysMatchTable = (t_Handle)FM_MURAM_AllocMem(
                h_FmMuram, matchTableSize, FM_PCD_CC_KEYS_MATCH_TABLE_ALIGN);
        if (!p_CcNode->h_KeysMatchTable)
        {
            DeleteNode(p_CcNode);
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for CC node key match table"));
        }
        MemSet8((uint8_t *)p_CcNode->h_KeysMatchTable, 0, matchTableSize);
    }

    /* Allocate action descriptors table */
    p_CcNode->h_AdTable = (t_Handle)FM_MURAM_AllocMem(h_FmMuram, adTableSize,
                                                      FM_PCD_CC_AD_TABLE_ALIGN);
    if (!p_CcNode->h_AdTable)
    {
        DeleteNode(p_CcNode);
        RETURN_ERROR(MAJOR, E_NO_MEMORY,
                     ("MURAM allocation for CC node action descriptors table"));
    }
    MemSet8((uint8_t *)p_CcNode->h_AdTable, 0, adTableSize);

    p_KeysMatchTblTmp = p_CcNode->h_KeysMatchTable;
    p_AdTableTmp = p_CcNode->h_AdTable;

    /* For each key, create the key and the next step AD */
    for (tmp = 0; tmp < p_CcNode->numOfKeys; tmp++)
    {
        p_KeyParams = &p_CcNodeParam->keysParams.keyParams[tmp];

        if (p_KeysMatchTblTmp)
        {
            /* Copy the key */
            MemCpy8((void*)p_KeysMatchTblTmp, p_KeyParams->p_Key,
                        p_CcNode->sizeOfExtraction);

            /* Copy the key mask or initialize it to 0xFF..F */
            if (p_CcNode->lclMask && p_KeyParams->p_Mask)
            {
                MemCpy8(PTR_MOVE(p_KeysMatchTblTmp,
                        p_CcNode->ccKeySizeAccExtraction), /* User's size of extraction rounded up to a valid matching table entry size */
                            p_KeyParams->p_Mask, p_CcNode->sizeOfExtraction); /* Exact size of extraction as received from the user */
            }
            else
                if (p_CcNode->lclMask)
                {
                    MemSet8(PTR_MOVE(p_KeysMatchTblTmp,
                            p_CcNode->ccKeySizeAccExtraction), /* User's size of extraction rounded up to a valid matching table entry size */
                               0xff, p_CcNode->sizeOfExtraction); /* Exact size of extraction as received from the user */
                }

            p_KeysMatchTblTmp =
                    PTR_MOVE(p_KeysMatchTblTmp, keySize * sizeof(uint8_t));
        }

        /* Create the next action descriptor in the match table */
        if (p_KeyParams->ccNextEngineParams.statisticsEn)
        {
            p_StatsObj = GetStatsObj(p_CcNode);
            ASSERT_COND(p_StatsObj);

            statsParams.h_StatsAd = p_StatsObj->h_StatsAd;
            statsParams.h_StatsCounters = p_StatsObj->h_StatsCounters;
#if (DPAA_VERSION >= 11)
            statsParams.h_StatsFLRs = p_CcNode->h_StatsFLRs;

#endif /* (DPAA_VERSION >= 11) */
            NextStepAd(p_AdTableTmp, &statsParams,
                       &p_KeyParams->ccNextEngineParams, p_FmPcd);

            p_CcNode->keyAndNextEngineParams[tmp].p_StatsObj = p_StatsObj;
        }
        else
        {
            NextStepAd(p_AdTableTmp, NULL, &p_KeyParams->ccNextEngineParams,
                       p_FmPcd);

            p_CcNode->keyAndNextEngineParams[tmp].p_StatsObj = NULL;
        }

        p_AdTableTmp = PTR_MOVE(p_AdTableTmp, FM_PCD_CC_AD_ENTRY_SIZE);
    }

    /* Update next engine for the 'miss' entry */
    if (p_CcNodeParam->keysParams.ccNextEngineParamsForMiss.statisticsEn)
    {
        p_StatsObj = GetStatsObj(p_CcNode);
        ASSERT_COND(p_StatsObj);

        /* All 'bucket' nodes of a hash table should share the same statistics counters,
         allocated by the hash table. So, if this node is a bucket of a hash table,
         we'll replace the locally allocated counters with the shared counters. */
        if (p_CcNode->isHashBucket)
        {
            ASSERT_COND(p_CcNode->h_MissStatsCounters);

            /* Store original counters pointer and replace it with mutual preallocated pointer */
            p_CcNode->h_PrivMissStatsCounters = p_StatsObj->h_StatsCounters;
            p_StatsObj->h_StatsCounters = p_CcNode->h_MissStatsCounters;
        }

        statsParams.h_StatsAd = p_StatsObj->h_StatsAd;
        statsParams.h_StatsCounters = p_StatsObj->h_StatsCounters;
#if (DPAA_VERSION >= 11)
        statsParams.h_StatsFLRs = p_CcNode->h_StatsFLRs;

#endif /* (DPAA_VERSION >= 11) */

        NextStepAd(p_AdTableTmp, &statsParams,
                   &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
                   p_FmPcd);

        p_CcNode->keyAndNextEngineParams[tmp].p_StatsObj = p_StatsObj;
    }
    else
    {
        NextStepAd(p_AdTableTmp, NULL,
                   &p_CcNodeParam->keysParams.ccNextEngineParamsForMiss,
                   p_FmPcd);

        p_CcNode->keyAndNextEngineParams[tmp].p_StatsObj = NULL;
    }

    /* This parameter will be used to initialize the "key length" field in the action descriptor
     that points to this node and it should be 0 for full field extraction */
    if (fullField == TRUE)
        p_CcNode->sizeOfExtraction = 0;

    for (tmp = 0; tmp < MIN(p_CcNode->numOfKeys + 1, CC_MAX_NUM_OF_KEYS); tmp++)
    {
        if (p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
        {
            p_FmPcdCcNextNode =
                    (t_FmPcdCcNode*)p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.params.ccParams.h_CcNode;
            p_CcInformation = FindNodeInfoInReleventLst(
                    &p_FmPcdCcNextNode->ccPrevNodesLst, (t_Handle)p_CcNode,
                    p_FmPcdCcNextNode->h_Spinlock);
            if (!p_CcInformation)
            {
                memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                ccNodeInfo.h_CcNode = (t_Handle)p_CcNode;
                ccNodeInfo.index = 1;
                EnqueueNodeInfoToRelevantLst(&p_FmPcdCcNextNode->ccPrevNodesLst,
                                             &ccNodeInfo,
                                             p_FmPcdCcNextNode->h_Spinlock);
            }
            else
                p_CcInformation->index++;

            if (p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.h_Manip)
            {
                h_Manip =
                        p_CcNode->keyAndNextEngineParams[tmp].nextEngineParams.h_Manip;
                p_CcInformation = FindNodeInfoInReleventLst(
                        FmPcdManipGetNodeLstPointedOnThisManip(h_Manip),
                        (t_Handle)p_CcNode, FmPcdManipGetSpinlock(h_Manip));
                if (!p_CcInformation)
                {
                    memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                    ccNodeInfo.h_CcNode = (t_Handle)p_CcNode;
                    ccNodeInfo.index = 1;
                    EnqueueNodeInfoToRelevantLst(
                            FmPcdManipGetNodeLstPointedOnThisManip(h_Manip),
                            &ccNodeInfo, FmPcdManipGetSpinlock(h_Manip));
                }
                else
                    p_CcInformation->index++;
            }
        }
    }

    p_AdTableTmp = p_CcNode->h_AdTable;

    if (!FmPcdLockTryLockAll(h_FmPcd))
    {
        FM_PCD_MatchTableDelete((t_Handle)p_CcNode);
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    /* Required action for each next engine */
    for (tmp = 0; tmp < MIN(p_CcNode->numOfKeys + 1, CC_MAX_NUM_OF_KEYS); tmp++)
    {
        if (p_CcNode->keyAndNextEngineParams[tmp].requiredAction)
        {
            err = SetRequiredAction(
                    h_FmPcd,
                    p_CcNode->keyAndNextEngineParams[tmp].requiredAction,
                    &p_CcNode->keyAndNextEngineParams[tmp], p_AdTableTmp, 1,
                    NULL);
            if (err)
            {
                FmPcdLockUnlockAll(h_FmPcd);
                FM_PCD_MatchTableDelete((t_Handle)p_CcNode);
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
            p_AdTableTmp = PTR_MOVE(p_AdTableTmp, FM_PCD_CC_AD_ENTRY_SIZE);
        }
    }

    FmPcdLockUnlockAll(h_FmPcd);

    return E_OK;
}
/************************** End of static functions **************************/

/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/

t_CcNodeInformation* FindNodeInfoInReleventLst(t_List *p_List, t_Handle h_Info,
                                               t_Handle h_Spinlock)
{
    t_CcNodeInformation *p_CcInformation;
    t_List *p_Pos;
    uint32_t intFlags;

    intFlags = XX_LockIntrSpinlock(h_Spinlock);

    for (p_Pos = NCSW_LIST_FIRST(p_List); p_Pos != (p_List);
            p_Pos = NCSW_LIST_NEXT(p_Pos))
    {
        p_CcInformation = CC_NODE_F_OBJECT(p_Pos);

        ASSERT_COND(p_CcInformation->h_CcNode);

        if (p_CcInformation->h_CcNode == h_Info)
        {
            XX_UnlockIntrSpinlock(h_Spinlock, intFlags);
            return p_CcInformation;
        }
    }

    XX_UnlockIntrSpinlock(h_Spinlock, intFlags);

    return NULL;
}

void EnqueueNodeInfoToRelevantLst(t_List *p_List, t_CcNodeInformation *p_CcInfo,
                                  t_Handle h_Spinlock)
{
    t_CcNodeInformation *p_CcInformation;
    uint32_t intFlags = 0;

    p_CcInformation = (t_CcNodeInformation *)XX_Malloc(
            sizeof(t_CcNodeInformation));

    if (p_CcInformation)
    {
        memset(p_CcInformation, 0, sizeof(t_CcNodeInformation));
        memcpy(p_CcInformation, p_CcInfo, sizeof(t_CcNodeInformation));
        INIT_LIST(&p_CcInformation->node);

        if (h_Spinlock)
            intFlags = XX_LockIntrSpinlock(h_Spinlock);

        NCSW_LIST_AddToTail(&p_CcInformation->node, p_List);

        if (h_Spinlock)
            XX_UnlockIntrSpinlock(h_Spinlock, intFlags);
    }
    else
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("CC Node Information"));
}

void DequeueNodeInfoFromRelevantLst(t_List *p_List, t_Handle h_Info,
                                    t_Handle h_Spinlock)
{
    t_CcNodeInformation *p_CcInformation = NULL;
    uint32_t intFlags = 0;
    t_List *p_Pos;

    if (h_Spinlock)
        intFlags = XX_LockIntrSpinlock(h_Spinlock);

    if (NCSW_LIST_IsEmpty(p_List))
    {
        XX_RestoreAllIntr(intFlags);
        return;
    }

    for (p_Pos = NCSW_LIST_FIRST(p_List); p_Pos != (p_List);
            p_Pos = NCSW_LIST_NEXT(p_Pos))
    {
        p_CcInformation = CC_NODE_F_OBJECT(p_Pos);
        ASSERT_COND(p_CcInformation);
        ASSERT_COND(p_CcInformation->h_CcNode);
        if (p_CcInformation->h_CcNode == h_Info)
            break;
    }

    if (p_CcInformation)
    {
        NCSW_LIST_DelAndInit(&p_CcInformation->node);
        XX_Free(p_CcInformation);
    }

    if (h_Spinlock)
        XX_UnlockIntrSpinlock(h_Spinlock, intFlags);
}

void NextStepAd(t_Handle h_Ad, t_FmPcdCcStatsParams *p_FmPcdCcStatsParams,
                t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams,
                t_FmPcd *p_FmPcd)
{
    switch (p_FmPcdCcNextEngineParams->nextEngine)
    {
        case (e_FM_PCD_KG):
        case (e_FM_PCD_PLCR):
        case (e_FM_PCD_DONE):
            /* if NIA is not CC, create a "result" type AD */
            FillAdOfTypeResult(h_Ad, p_FmPcdCcStatsParams, p_FmPcd,
                               p_FmPcdCcNextEngineParams);
            break;
#if (DPAA_VERSION >= 11)
        case (e_FM_PCD_FR):
            if (p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic)
            {
                FillAdOfTypeContLookup(
                        h_Ad, p_FmPcdCcStatsParams, p_FmPcd,
                        p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode,
                        p_FmPcdCcNextEngineParams->h_Manip,
                        p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic);
                FrmReplicGroupUpdateOwner(
                        p_FmPcdCcNextEngineParams->params.frParams.h_FrmReplic,
                        TRUE/* add */);
            }
            break;
#endif /* (DPAA_VERSION >= 11) */

        case (e_FM_PCD_CC):
            /* if NIA is not CC, create a TD to continue the CC lookup */
            FillAdOfTypeContLookup(
                    h_Ad, p_FmPcdCcStatsParams, p_FmPcd,
                    p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode,
                    p_FmPcdCcNextEngineParams->h_Manip, NULL);

            UpdateNodeOwner(p_FmPcdCcNextEngineParams->params.ccParams.h_CcNode,
                            TRUE);
            break;

        default:
            return;
    }
}

t_Error FmPcdCcTreeAddIPR(t_Handle h_FmPcd, t_Handle h_FmTree,
                          t_Handle h_NetEnv, t_Handle h_IpReassemblyManip,
                          bool createSchemes)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmTree;
    t_FmPcdCcNextEngineParams nextEngineParams;
    t_NetEnvParams netEnvParams;
    t_Handle h_Ad;
    bool isIpv6Present;
    uint8_t ipv4GroupId, ipv6GroupId;
    t_Error err;

    ASSERT_COND(p_FmPcdCcTree);

    /* this routine must be protected by the calling routine! */

    memset(&nextEngineParams, 0, sizeof(t_FmPcdCcNextEngineParams));
    memset(&netEnvParams, 0, sizeof(t_NetEnvParams));

    h_Ad = UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr);

    isIpv6Present = FmPcdManipIpReassmIsIpv6Hdr(h_IpReassemblyManip);

    if (isIpv6Present
            && (p_FmPcdCcTree->numOfEntries > (FM_PCD_MAX_NUM_OF_CC_GROUPS - 2)))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("need two free entries for IPR"));

    if (p_FmPcdCcTree->numOfEntries > (FM_PCD_MAX_NUM_OF_CC_GROUPS - 1))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("need two free entries for IPR"));

    nextEngineParams.nextEngine = e_FM_PCD_DONE;
    nextEngineParams.h_Manip = h_IpReassemblyManip;

    /* Lock tree */
    err = CcRootTryLock(p_FmPcdCcTree);
    if (err)
        return ERROR_CODE(E_BUSY);

    if (p_FmPcdCcTree->h_IpReassemblyManip == h_IpReassemblyManip)
    {
        CcRootReleaseLock(p_FmPcdCcTree);
        return E_OK;
    }

    if ((p_FmPcdCcTree->h_IpReassemblyManip)
            && (p_FmPcdCcTree->h_IpReassemblyManip != h_IpReassemblyManip))
    {
        CcRootReleaseLock(p_FmPcdCcTree);
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("This tree was previously updated with different IPR"));
    }

    /* Initialize IPR for the first time for this tree */
    if (isIpv6Present)
    {
        ipv6GroupId = p_FmPcdCcTree->numOfGrps++;
        p_FmPcdCcTree->fmPcdGroupParam[ipv6GroupId].baseGroupEntry =
                (FM_PCD_MAX_NUM_OF_CC_GROUPS - 2);

        if (createSchemes)
        {
            err = FmPcdManipBuildIpReassmScheme(h_FmPcd, h_NetEnv,
                                                p_FmPcdCcTree,
                                                h_IpReassemblyManip, FALSE,
                                                ipv6GroupId);
            if (err)
            {
                p_FmPcdCcTree->numOfGrps--;
                CcRootReleaseLock(p_FmPcdCcTree);
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        }

        NextStepAd(
                PTR_MOVE(h_Ad, (FM_PCD_MAX_NUM_OF_CC_GROUPS-2) * FM_PCD_CC_AD_ENTRY_SIZE),
                NULL, &nextEngineParams, h_FmPcd);
    }

    ipv4GroupId = p_FmPcdCcTree->numOfGrps++;
    p_FmPcdCcTree->fmPcdGroupParam[ipv4GroupId].totalBitsMask = 0;
    p_FmPcdCcTree->fmPcdGroupParam[ipv4GroupId].baseGroupEntry =
            (FM_PCD_MAX_NUM_OF_CC_GROUPS - 1);

    if (createSchemes)
    {
        err = FmPcdManipBuildIpReassmScheme(h_FmPcd, h_NetEnv, p_FmPcdCcTree,
                                            h_IpReassemblyManip, TRUE,
                                            ipv4GroupId);
        if (err)
        {
            p_FmPcdCcTree->numOfGrps--;
            if (isIpv6Present)
            {
                p_FmPcdCcTree->numOfGrps--;
                FmPcdManipDeleteIpReassmSchemes(h_IpReassemblyManip);
            }
            CcRootReleaseLock(p_FmPcdCcTree);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
    }

    NextStepAd(
            PTR_MOVE(h_Ad, (FM_PCD_MAX_NUM_OF_CC_GROUPS-1) * FM_PCD_CC_AD_ENTRY_SIZE),
            NULL, &nextEngineParams, h_FmPcd);

    p_FmPcdCcTree->h_IpReassemblyManip = h_IpReassemblyManip;

    CcRootReleaseLock(p_FmPcdCcTree);

    return E_OK;
}

t_Error FmPcdCcTreeAddCPR(t_Handle h_FmPcd, t_Handle h_FmTree,
                          t_Handle h_NetEnv, t_Handle h_ReassemblyManip,
                          bool createSchemes)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmTree;
    t_FmPcdCcNextEngineParams nextEngineParams;
    t_NetEnvParams netEnvParams;
    t_Handle h_Ad;
    uint8_t groupId;
    t_Error err;

    ASSERT_COND(p_FmPcdCcTree);

    /* this routine must be protected by the calling routine! */
    memset(&nextEngineParams, 0, sizeof(t_FmPcdCcNextEngineParams));
    memset(&netEnvParams, 0, sizeof(t_NetEnvParams));

    h_Ad = UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr);

    if (p_FmPcdCcTree->numOfEntries > (FM_PCD_MAX_NUM_OF_CC_GROUPS - 1))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("need one free entries for CPR"));

    nextEngineParams.nextEngine = e_FM_PCD_DONE;
    nextEngineParams.h_Manip = h_ReassemblyManip;

    /* Lock tree */
    err = CcRootTryLock(p_FmPcdCcTree);
    if (err)
        return ERROR_CODE(E_BUSY);

    if (p_FmPcdCcTree->h_CapwapReassemblyManip == h_ReassemblyManip)
    {
        CcRootReleaseLock(p_FmPcdCcTree);
        return E_OK;
    }

    if ((p_FmPcdCcTree->h_CapwapReassemblyManip)
            && (p_FmPcdCcTree->h_CapwapReassemblyManip != h_ReassemblyManip))
    {
        CcRootReleaseLock(p_FmPcdCcTree);
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("This tree was previously updated with different CPR"));
    }

    groupId = p_FmPcdCcTree->numOfGrps++;
    p_FmPcdCcTree->fmPcdGroupParam[groupId].baseGroupEntry =
            (FM_PCD_MAX_NUM_OF_CC_GROUPS - 1);

    if (createSchemes)
    {
        err = FmPcdManipBuildCapwapReassmScheme(h_FmPcd, h_NetEnv,
                                                p_FmPcdCcTree,
                                                h_ReassemblyManip, groupId);
        if (err)
        {
            p_FmPcdCcTree->numOfGrps--;
            CcRootReleaseLock(p_FmPcdCcTree);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
    }

    NextStepAd(
            PTR_MOVE(h_Ad, (FM_PCD_MAX_NUM_OF_CC_GROUPS-1) * FM_PCD_CC_AD_ENTRY_SIZE),
            NULL, &nextEngineParams, h_FmPcd);

    p_FmPcdCcTree->h_CapwapReassemblyManip = h_ReassemblyManip;

    CcRootReleaseLock(p_FmPcdCcTree);

    return E_OK;
}

t_Handle FmPcdCcTreeGetSavedManipParams(t_Handle h_FmTree)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmTree;

    ASSERT_COND(p_FmPcdCcTree);

    return p_FmPcdCcTree->h_FmPcdCcSavedManipParams;
}

void FmPcdCcTreeSetSavedManipParams(t_Handle h_FmTree,
                                    t_Handle h_SavedManipParams)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmTree;

    ASSERT_COND(p_FmPcdCcTree);

    p_FmPcdCcTree->h_FmPcdCcSavedManipParams = h_SavedManipParams;
}

uint8_t FmPcdCcGetParseCode(t_Handle h_CcNode)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;

    ASSERT_COND(p_CcNode);

    return p_CcNode->parseCode;
}

uint8_t FmPcdCcGetOffset(t_Handle h_CcNode)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;

    ASSERT_COND(p_CcNode);

    return p_CcNode->offset;
}

uint16_t FmPcdCcGetNumOfKeys(t_Handle h_CcNode)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;

    ASSERT_COND(p_CcNode);

    return p_CcNode->numOfKeys;
}

t_Error FmPcdCcModifyNextEngineParamTree(
        t_Handle h_FmPcd, t_Handle h_FmPcdCcTree, uint8_t grpId, uint8_t index,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;
    t_FmPcd *p_FmPcd;
    t_List h_OldPointersLst, h_NewPointersLst;
    uint16_t keyIndex;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcdCcTree, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((grpId <= 7), E_INVALID_VALUE);

    if (grpId >= p_FmPcdCcTree->numOfGrps)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE,
                     ("grpId you asked > numOfGroup of relevant tree"));

    if (index >= p_FmPcdCcTree->fmPcdGroupParam[grpId].numOfEntriesInGroup)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("index > numOfEntriesInGroup"));

    p_FmPcd = (t_FmPcd *)h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    keyIndex = (uint16_t)(p_FmPcdCcTree->fmPcdGroupParam[grpId].baseGroupEntry
            + index);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_FmPcdCcTree, keyIndex,
                                             e_MODIFY_STATE_CHANGE, FALSE,
                                             FALSE, TRUE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    p_ModifyKeyParams->tree = TRUE;

    if (p_FmPcd->p_CcShadow
            && !TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
    {
        XX_Free(p_ModifyKeyParams);
        return ERROR_CODE(E_BUSY);
    }

    err = BuildNewNodeModifyNextEngine(p_FmPcd, p_FmPcdCcTree, keyIndex,
                                       p_FmPcdCcNextEngineParams,
                                       &h_OldPointersLst, &h_NewPointersLst,
                                       p_ModifyKeyParams);
    if (err)
    {
        XX_Free(p_ModifyKeyParams);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, FALSE);

    if (p_FmPcd->p_CcShadow)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;

}

t_Error FmPcdCcRemoveKey(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
                         uint16_t keyIndex)
{

    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    t_List h_OldPointersLst, h_NewPointersLst;
    bool useShadowStructs = FALSE;
    t_Error err = E_OK;

    if (keyIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("impossible to remove key when numOfKeys <= keyIndex"));

    if (p_CcNode->h_FmPcd != h_FmPcd)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("handler to FmPcd is different from the handle provided at node initialization time"));

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_REMOVE, TRUE, TRUE,
                                             FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys)
    {
        if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        {
            XX_Free(p_ModifyKeyParams);
            return ERROR_CODE(E_BUSY);
        }

        useShadowStructs = TRUE;
    }

    err = BuildNewNodeRemoveKey(p_CcNode, keyIndex, p_ModifyKeyParams);
    if (err)
    {
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = UpdatePtrWhichPointOnCrntMdfNode(p_CcNode, p_ModifyKeyParams,
                                           &h_OldPointersLst,
                                           &h_NewPointersLst);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, useShadowStructs);

    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

t_Error FmPcdCcModifyKey(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
                         uint16_t keyIndex, uint8_t keySize, uint8_t *p_Key,
                         uint8_t *p_Mask)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_List h_OldPointersLst, h_NewPointersLst;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    uint16_t tmpKeyIndex;
    bool useShadowStructs = FALSE;
    t_Error err = E_OK;

    if (keyIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("keyIndex > previously cleared last index + 1"));

    if (keySize != p_CcNode->userSizeOfExtraction)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("size for ModifyKey has to be the same as defined in SetNode"));

    if (p_CcNode->h_FmPcd != h_FmPcd)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("handler to FmPcd is different from the handle provided at node initialization time"));

    err = FindKeyIndex(h_FmPcdCcNode, keySize, p_Key, p_Mask, &tmpKeyIndex);
    if (GET_ERROR_TYPE(err) != E_NOT_FOUND)
        RETURN_ERROR(
                MINOR,
                E_ALREADY_EXISTS,
                ("The received key and mask pair was already found in the match table of the provided node"));

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_CHANGE, TRUE, TRUE,
                                             FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys)
    {
        if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        {
            XX_Free(p_ModifyKeyParams);
            return ERROR_CODE(E_BUSY);
        }

        useShadowStructs = TRUE;
    }

    err = BuildNewNodeModifyKey(p_CcNode, keyIndex, p_Key, p_Mask,
                                p_ModifyKeyParams);
    if (err)
    {
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = UpdatePtrWhichPointOnCrntMdfNode(p_CcNode, p_ModifyKeyParams,
                                           &h_OldPointersLst,
                                           &h_NewPointersLst);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, useShadowStructs);

    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

t_Error FmPcdCcModifyMissNextEngineParamNode(
        t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_List h_OldPointersLst, h_NewPointersLst;
    uint16_t keyIndex;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_VALUE);

    keyIndex = p_CcNode->numOfKeys;

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_CHANGE, FALSE, TRUE,
                                             FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys
            && !TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
    {
        XX_Free(p_ModifyKeyParams);
        return ERROR_CODE(E_BUSY);
    }

    err = BuildNewNodeModifyNextEngine(h_FmPcd, p_CcNode, keyIndex,
                                       p_FmPcdCcNextEngineParams,
                                       &h_OldPointersLst, &h_NewPointersLst,
                                       p_ModifyKeyParams);
    if (err)
    {
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, FALSE);

    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

t_Error FmPcdCcAddKey(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
                      uint16_t keyIndex, uint8_t keySize,
                      t_FmPcdCcKeyParams *p_FmPcdCcKeyParams)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    t_List h_OldPointersLst, h_NewPointersLst;
    bool useShadowStructs = FALSE;
    uint16_t tmpKeyIndex;
    t_Error err = E_OK;

    if (keyIndex > p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE,
                     ("keyIndex > previously cleared last index + 1"));

    if (keySize != p_CcNode->userSizeOfExtraction)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("keySize has to be defined as it was defined in initialization step"));

    if (p_CcNode->h_FmPcd != h_FmPcd)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("handler to FmPcd is different from the handle provided at node initialization time"));

    if (p_CcNode->maxNumOfKeys)
    {
        if (p_CcNode->numOfKeys == p_CcNode->maxNumOfKeys)
            RETURN_ERROR(
                    MAJOR,
                    E_FULL,
                    ("number of keys exceeds the maximal number of keys provided at node initialization time"));
    }
    else
        if (p_CcNode->numOfKeys == FM_PCD_MAX_NUM_OF_KEYS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("number of keys can not be larger than %d", FM_PCD_MAX_NUM_OF_KEYS));

    err = FindKeyIndex(h_FmPcdCcNode, keySize, p_FmPcdCcKeyParams->p_Key,
                       p_FmPcdCcKeyParams->p_Mask, &tmpKeyIndex);
    if (GET_ERROR_TYPE(err) != E_NOT_FOUND)
        RETURN_ERROR(
                MAJOR,
                E_ALREADY_EXISTS,
                ("The received key and mask pair was already found in the match table of the provided node"));

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_ADD, TRUE, TRUE,
                                             FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys)
    {
        if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        {
            XX_Free(p_ModifyKeyParams);
            return ERROR_CODE(E_BUSY);
        }

        useShadowStructs = TRUE;
    }

    err = BuildNewNodeAddOrMdfyKeyAndNextEngine(h_FmPcd, p_CcNode, keyIndex,
                                                p_FmPcdCcKeyParams,
                                                p_ModifyKeyParams, TRUE);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = UpdatePtrWhichPointOnCrntMdfNode(p_CcNode, p_ModifyKeyParams,
                                           &h_OldPointersLst,
                                           &h_NewPointersLst);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, useShadowStructs);
    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

t_Error FmPcdCcModifyKeyAndNextEngine(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
                                      uint16_t keyIndex, uint8_t keySize,
                                      t_FmPcdCcKeyParams *p_FmPcdCcKeyParams)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_FmPcd *p_FmPcd;
    t_List h_OldPointersLst, h_NewPointersLst;
    t_FmPcdModifyCcKeyAdditionalParams *p_ModifyKeyParams;
    uint16_t tmpKeyIndex;
    bool useShadowStructs = FALSE;
    t_Error err = E_OK;

    if (keyIndex > p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("keyIndex > previously cleared last index + 1"));

    if (keySize != p_CcNode->userSizeOfExtraction)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("keySize has to be defined as it was defined in initialization step"));

    if (p_CcNode->h_FmPcd != h_FmPcd)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("handler to FmPcd is different from the handle provided at node initialization time"));

    err = FindKeyIndex(h_FmPcdCcNode, keySize, p_FmPcdCcKeyParams->p_Key,
                       p_FmPcdCcKeyParams->p_Mask, &tmpKeyIndex);
    if (GET_ERROR_TYPE(err) != E_NOT_FOUND)
        RETURN_ERROR(
                MINOR,
                E_ALREADY_EXISTS,
                ("The received key and mask pair was already found in the match table of the provided node"));

    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;

    INIT_LIST(&h_OldPointersLst);
    INIT_LIST(&h_NewPointersLst);

    p_ModifyKeyParams = ModifyNodeCommonPart(p_CcNode, keyIndex,
                                             e_MODIFY_STATE_CHANGE, TRUE, TRUE,
                                             FALSE);
    if (!p_ModifyKeyParams)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_CcNode->maxNumOfKeys)
    {
        if (!TRY_LOCK(p_FmPcd->h_ShadowSpinlock, &p_FmPcd->shadowLock))
        {
            XX_Free(p_ModifyKeyParams);
            return ERROR_CODE(E_BUSY);
        }

        useShadowStructs = TRUE;
    }

    err = BuildNewNodeAddOrMdfyKeyAndNextEngine(h_FmPcd, p_CcNode, keyIndex,
                                                p_FmPcdCcKeyParams,
                                                p_ModifyKeyParams, FALSE);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = UpdatePtrWhichPointOnCrntMdfNode(p_CcNode, p_ModifyKeyParams,
                                           &h_OldPointersLst,
                                           &h_NewPointersLst);
    if (err)
    {
        ReleaseNewNodeCommonPart(p_ModifyKeyParams);
        XX_Free(p_ModifyKeyParams);
        if (p_CcNode->maxNumOfKeys)
            RELEASE_LOCK(p_FmPcd->shadowLock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    err = DoDynamicChange(p_FmPcd, &h_OldPointersLst, &h_NewPointersLst,
                          p_ModifyKeyParams, useShadowStructs);

    if (p_CcNode->maxNumOfKeys)
        RELEASE_LOCK(p_FmPcd->shadowLock);

    return err;
}

uint32_t FmPcdCcGetNodeAddrOffsetFromNodeInfo(t_Handle h_FmPcd,
                                              t_Handle h_Pointer)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_CcNodeInformation *p_CcNodeInfo;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE,
                              (uint32_t)ILLEGAL_BASE);

    p_CcNodeInfo = CC_NODE_F_OBJECT(h_Pointer);

    return (uint32_t)(XX_VirtToPhys(p_CcNodeInfo->h_CcNode)
            - p_FmPcd->physicalMuramBase);
}

t_Error FmPcdCcGetGrpParams(t_Handle h_FmPcdCcTree, uint8_t grpId,
                            uint32_t *p_GrpBits, uint8_t *p_GrpBase)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;

    SANITY_CHECK_RETURN_ERROR(h_FmPcdCcTree, E_INVALID_HANDLE);

    if (grpId >= p_FmPcdCcTree->numOfGrps)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE,
                     ("grpId you asked > numOfGroup of relevant tree"));

    *p_GrpBits = p_FmPcdCcTree->fmPcdGroupParam[grpId].totalBitsMask;
    *p_GrpBase = p_FmPcdCcTree->fmPcdGroupParam[grpId].baseGroupEntry;

    return E_OK;
}

t_Error FmPcdCcBindTree(t_Handle h_FmPcd, t_Handle h_PcdParams,
                        t_Handle h_FmPcdCcTree, uint32_t *p_Offset,
                        t_Handle h_FmPort)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_FmPcdCcTree, E_INVALID_HANDLE);

    /* this routine must be protected by the calling routine by locking all PCD modules! */

    err = CcUpdateParams(h_FmPcd, h_PcdParams, h_FmPort, h_FmPcdCcTree, TRUE);

    if (err == E_OK)
        UpdateCcRootOwner(p_FmPcdCcTree, TRUE);

    *p_Offset = (uint32_t)(XX_VirtToPhys(
            UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr))
            - p_FmPcd->physicalMuramBase);

    return err;
}

t_Error FmPcdCcUnbindTree(t_Handle h_FmPcd, t_Handle h_FmPcdCcTree)
{
    t_FmPcdCcTree *p_FmPcdCcTree = (t_FmPcdCcTree *)h_FmPcdCcTree;

    /* this routine must be protected by the calling routine by locking all PCD modules! */

    UNUSED(h_FmPcd);

    SANITY_CHECK_RETURN_ERROR(h_FmPcdCcTree, E_INVALID_HANDLE);

    UpdateCcRootOwner(p_FmPcdCcTree, FALSE);

    return E_OK;
}

t_Error FmPcdCcNodeTreeTryLock(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode,
                               t_List *p_List)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_FmPcdCcNode;
    t_List *p_Pos, *p_Tmp;
    t_CcNodeInformation *p_CcNodeInfo, nodeInfo;
    uint32_t intFlags;
    t_Error err = E_OK;

    intFlags = FmPcdLock(h_FmPcd);

    NCSW_LIST_FOR_EACH(p_Pos, &p_CcNode->ccTreesLst)
    {
        p_CcNodeInfo = CC_NODE_F_OBJECT(p_Pos);
        ASSERT_COND(p_CcNodeInfo->h_CcNode);

        err = CcRootTryLock(p_CcNodeInfo->h_CcNode);

        if (err)
        {
            NCSW_LIST_FOR_EACH(p_Tmp, &p_CcNode->ccTreesLst)
            {
                if (p_Tmp == p_Pos)
                    break;

                CcRootReleaseLock(p_CcNodeInfo->h_CcNode);
            }
            break;
        }

        memset(&nodeInfo, 0, sizeof(t_CcNodeInformation));
        nodeInfo.h_CcNode = p_CcNodeInfo->h_CcNode;
        EnqueueNodeInfoToRelevantLst(p_List, &nodeInfo, NULL);
    }

    FmPcdUnlock(h_FmPcd, intFlags);
    CORE_MemoryBarrier();

    return err;
}

void FmPcdCcNodeTreeReleaseLock(t_Handle h_FmPcd, t_List *p_List)
{
    t_List *p_Pos;
    t_CcNodeInformation *p_CcNodeInfo;
    t_Handle h_FmPcdCcTree;
    uint32_t intFlags;

    intFlags = FmPcdLock(h_FmPcd);

    NCSW_LIST_FOR_EACH(p_Pos, p_List)
    {
        p_CcNodeInfo = CC_NODE_F_OBJECT(p_Pos);
        h_FmPcdCcTree = p_CcNodeInfo->h_CcNode;
        CcRootReleaseLock(h_FmPcdCcTree);
    }

    ReleaseLst(p_List);

    FmPcdUnlock(h_FmPcd, intFlags);
    CORE_MemoryBarrier();
}

t_Error FmPcdUpdateCcShadow(t_FmPcd *p_FmPcd, uint32_t size, uint32_t align)
{
    uint32_t intFlags;
    uint32_t newSize = 0, newAlign = 0;
    bool allocFail = FALSE;

    ASSERT_COND(p_FmPcd);

    if (!size)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("size must be larger then 0"));

    if (!POWER_OF_2(align))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("alignment must be power of 2"));

    newSize = p_FmPcd->ccShadowSize;
    newAlign = p_FmPcd->ccShadowAlign;

    /* Check if current shadow is large enough to hold the requested size */
    if (size > p_FmPcd->ccShadowSize)
        newSize = size;

    /* Check if current shadow matches the requested alignment */
    if (align > p_FmPcd->ccShadowAlign)
        newAlign = align;

    /* If a bigger shadow size or bigger shadow alignment are required,
     a new shadow will be allocated */
    if ((newSize != p_FmPcd->ccShadowSize)
            || (newAlign != p_FmPcd->ccShadowAlign))
    {
        intFlags = FmPcdLock(p_FmPcd);

        if (p_FmPcd->p_CcShadow)
        {
            FM_MURAM_FreeMem(FmPcdGetMuramHandle(p_FmPcd), p_FmPcd->p_CcShadow);
            p_FmPcd->ccShadowSize = 0;
            p_FmPcd->ccShadowAlign = 0;
        }

        p_FmPcd->p_CcShadow = FM_MURAM_AllocMem(FmPcdGetMuramHandle(p_FmPcd),
                                                newSize, newAlign);
        if (!p_FmPcd->p_CcShadow)
        {
            allocFail = TRUE;

            /* If new shadow size allocation failed,
             re-allocate with previous parameters */
            p_FmPcd->p_CcShadow = FM_MURAM_AllocMem(
                    FmPcdGetMuramHandle(p_FmPcd), p_FmPcd->ccShadowSize,
                    p_FmPcd->ccShadowAlign);
        }

        FmPcdUnlock(p_FmPcd, intFlags);

        if (allocFail)
            RETURN_ERROR(MAJOR, E_NO_MEMORY,
                         ("MURAM allocation for CC Shadow memory"));

        p_FmPcd->ccShadowSize = newSize;
        p_FmPcd->ccShadowAlign = newAlign;
    }

    return E_OK;
}

#if (DPAA_VERSION >= 11)
void FmPcdCcGetAdTablesThatPointOnReplicGroup(t_Handle h_Node,
                                              t_Handle h_ReplicGroup,
                                              t_List *p_AdTables,
                                              uint32_t *p_NumOfAdTables)
{
    t_FmPcdCcNode *p_CurrentNode = (t_FmPcdCcNode *)h_Node;
    int i = 0;
    void * p_AdTable;
    t_CcNodeInformation ccNodeInfo;

    ASSERT_COND(h_Node);
    *p_NumOfAdTables = 0;

    /* search in the current node which exact index points on this current replicator group for getting AD */
    for (i = 0; i < p_CurrentNode->numOfKeys + 1; i++)
    {
        if ((p_CurrentNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && ((p_CurrentNode->keyAndNextEngineParams[i].nextEngineParams.params.frParams.h_FrmReplic
                        == (t_Handle)h_ReplicGroup)))
        {
            /* save the current ad table in the list */
            /* this entry uses the input replicator group */
            p_AdTable =
                    PTR_MOVE(p_CurrentNode->h_AdTable, i*FM_PCD_CC_AD_ENTRY_SIZE);
            memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
            ccNodeInfo.h_CcNode = p_AdTable;
            EnqueueNodeInfoToRelevantLst(p_AdTables, &ccNodeInfo, NULL);
            (*p_NumOfAdTables)++;
        }
    }

    ASSERT_COND(i != p_CurrentNode->numOfKeys);
}
#endif /* (DPAA_VERSION >= 11) */
/*********************** End of inter-module routines ************************/

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle FM_PCD_CcRootBuild(t_Handle h_FmPcd,
                            t_FmPcdCcTreeParams *p_PcdGroupsParam)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_Error err = E_OK;
    int i = 0, j = 0, k = 0;
    t_FmPcdCcTree *p_FmPcdCcTree;
    uint8_t numOfEntries;
    t_Handle p_CcTreeTmp;
    t_FmPcdCcGrpParams *p_FmPcdCcGroupParams;
    t_FmPcdCcKeyAndNextEngineParams *p_Params, *p_KeyAndNextEngineParams;
    t_NetEnvParams netEnvParams;
    uint8_t lastOne = 0;
    uint32_t requiredAction = 0;
    t_FmPcdCcNode *p_FmPcdCcNextNode;
    t_CcNodeInformation ccNodeInfo, *p_CcInformation;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_PcdGroupsParam, E_INVALID_HANDLE, NULL);

    if (p_PcdGroupsParam->numOfGrps > FM_PCD_MAX_NUM_OF_CC_GROUPS)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("numOfGrps should not exceed %d", FM_PCD_MAX_NUM_OF_CC_GROUPS));
        return NULL;
    }

    p_FmPcdCcTree = (t_FmPcdCcTree*)XX_Malloc(sizeof(t_FmPcdCcTree));
    if (!p_FmPcdCcTree)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("PCD tree structure"));
        return NULL;
    }
    memset(p_FmPcdCcTree, 0, sizeof(t_FmPcdCcTree));
    p_FmPcdCcTree->h_FmPcd = h_FmPcd;

    p_Params = (t_FmPcdCcKeyAndNextEngineParams*)XX_Malloc(
            FM_PCD_MAX_NUM_OF_CC_GROUPS
                    * sizeof(t_FmPcdCcKeyAndNextEngineParams));
    memset(p_Params,
           0,
           FM_PCD_MAX_NUM_OF_CC_GROUPS
                   * sizeof(t_FmPcdCcKeyAndNextEngineParams));

    INIT_LIST(&p_FmPcdCcTree->fmPortsLst);

#ifdef FM_CAPWAP_SUPPORT
    if ((p_PcdGroupsParam->numOfGrps == 1) &&
            (p_PcdGroupsParam->ccGrpParams[0].numOfDistinctionUnits == 0) &&
            (p_PcdGroupsParam->ccGrpParams[0].nextEnginePerEntriesInGrp[0].nextEngine == e_FM_PCD_CC) &&
            p_PcdGroupsParam->ccGrpParams[0].nextEnginePerEntriesInGrp[0].params.ccParams.h_CcNode &&
            IsCapwapApplSpecific(p_PcdGroupsParam->ccGrpParams[0].nextEnginePerEntriesInGrp[0].params.ccParams.h_CcNode))
    {
        p_PcdGroupsParam->ccGrpParams[0].nextEnginePerEntriesInGrp[0].h_Manip = FmPcdManipApplSpecificBuild();
        if (!p_PcdGroupsParam->ccGrpParams[0].nextEnginePerEntriesInGrp[0].h_Manip)
        {
            DeleteTree(p_FmPcdCcTree,p_FmPcd);
            XX_Free(p_Params);
            REPORT_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
            return NULL;
        }
    }
#endif /* FM_CAPWAP_SUPPORT */

    numOfEntries = 0;
    p_FmPcdCcTree->netEnvId = FmPcdGetNetEnvId(p_PcdGroupsParam->h_NetEnv);

    for (i = 0; i < p_PcdGroupsParam->numOfGrps; i++)
    {
        p_FmPcdCcGroupParams = &p_PcdGroupsParam->ccGrpParams[i];

        if (p_FmPcdCcGroupParams->numOfDistinctionUnits
                > FM_PCD_MAX_NUM_OF_CC_UNITS)
        {
            DeleteTree(p_FmPcdCcTree, p_FmPcd);
            XX_Free(p_Params);
            REPORT_ERROR(MAJOR, E_INVALID_VALUE,
                    ("numOfDistinctionUnits (group %d) should not exceed %d", i, FM_PCD_MAX_NUM_OF_CC_UNITS));
            return NULL;
        }

        p_FmPcdCcTree->fmPcdGroupParam[i].baseGroupEntry = numOfEntries;
        p_FmPcdCcTree->fmPcdGroupParam[i].numOfEntriesInGroup = (uint8_t)(0x01
                << p_FmPcdCcGroupParams->numOfDistinctionUnits);
        numOfEntries += p_FmPcdCcTree->fmPcdGroupParam[i].numOfEntriesInGroup;
        if (numOfEntries > FM_PCD_MAX_NUM_OF_CC_GROUPS)
        {
            DeleteTree(p_FmPcdCcTree, p_FmPcd);
            XX_Free(p_Params);
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("numOfEntries can not be larger than %d", FM_PCD_MAX_NUM_OF_CC_GROUPS));
            return NULL;
        }

        if (lastOne)
        {
            if (p_FmPcdCcTree->fmPcdGroupParam[i].numOfEntriesInGroup > lastOne)
            {
                DeleteTree(p_FmPcdCcTree, p_FmPcd);
                XX_Free(p_Params);
                REPORT_ERROR(MAJOR, E_CONFLICT, ("numOfEntries per group must be set in descending order"));
                return NULL;
            }
        }

        lastOne = p_FmPcdCcTree->fmPcdGroupParam[i].numOfEntriesInGroup;

        netEnvParams.netEnvId = p_FmPcdCcTree->netEnvId;
        netEnvParams.numOfDistinctionUnits =
                p_FmPcdCcGroupParams->numOfDistinctionUnits;

        memcpy(netEnvParams.unitIds, &p_FmPcdCcGroupParams->unitIds,
               (sizeof(uint8_t)) * p_FmPcdCcGroupParams->numOfDistinctionUnits);

        err = PcdGetUnitsVector(p_FmPcd, &netEnvParams);
        if (err)
        {
            DeleteTree(p_FmPcdCcTree, p_FmPcd);
            XX_Free(p_Params);
            REPORT_ERROR(MAJOR, err, NO_MSG);
            return NULL;
        }

        p_FmPcdCcTree->fmPcdGroupParam[i].totalBitsMask = netEnvParams.vector;
        for (j = 0; j < p_FmPcdCcTree->fmPcdGroupParam[i].numOfEntriesInGroup;
                j++)
        {
            err = ValidateNextEngineParams(
                    h_FmPcd,
                    &p_FmPcdCcGroupParams->nextEnginePerEntriesInGrp[j],
                    e_FM_PCD_CC_STATS_MODE_NONE);
            if (err)
            {
                DeleteTree(p_FmPcdCcTree, p_FmPcd);
                XX_Free(p_Params);
                REPORT_ERROR(MAJOR, err, (NO_MSG));
                return NULL;
            }

            if (p_FmPcdCcGroupParams->nextEnginePerEntriesInGrp[j].h_Manip)
            {
                err = FmPcdManipCheckParamsForCcNextEngine(
                        &p_FmPcdCcGroupParams->nextEnginePerEntriesInGrp[j],
                        &requiredAction);
                if (err)
                {
                    DeleteTree(p_FmPcdCcTree, p_FmPcd);
                    XX_Free(p_Params);
                    REPORT_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
                    return NULL;
                }
            }
            p_KeyAndNextEngineParams = p_Params + k;

            memcpy(&p_KeyAndNextEngineParams->nextEngineParams,
                   &p_FmPcdCcGroupParams->nextEnginePerEntriesInGrp[j],
                   sizeof(t_FmPcdCcNextEngineParams));

            if ((p_KeyAndNextEngineParams->nextEngineParams.nextEngine
                    == e_FM_PCD_CC)
                    && p_KeyAndNextEngineParams->nextEngineParams.h_Manip)
            {
                err =
                        AllocAndFillAdForContLookupManip(
                                p_KeyAndNextEngineParams->nextEngineParams.params.ccParams.h_CcNode);
                if (err)
                {
                    DeleteTree(p_FmPcdCcTree, p_FmPcd);
                    XX_Free(p_Params);
                    REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for CC Tree"));
                    return NULL;
                }
            }

            requiredAction |= UPDATE_CC_WITH_TREE;
            p_KeyAndNextEngineParams->requiredAction = requiredAction;

            k++;
        }
    }

    p_FmPcdCcTree->numOfEntries = (uint8_t)k;
    p_FmPcdCcTree->numOfGrps = p_PcdGroupsParam->numOfGrps;

    p_FmPcdCcTree->ccTreeBaseAddr =
            PTR_TO_UINT(FM_MURAM_AllocMem(FmPcdGetMuramHandle(h_FmPcd),
                            (uint32_t)( FM_PCD_MAX_NUM_OF_CC_GROUPS * FM_PCD_CC_AD_ENTRY_SIZE),
                            FM_PCD_CC_TREE_ADDR_ALIGN));
    if (!p_FmPcdCcTree->ccTreeBaseAddr)
    {
        DeleteTree(p_FmPcdCcTree, p_FmPcd);
        XX_Free(p_Params);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for CC Tree"));
        return NULL;
    }
    MemSet8(
            UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr), 0,
            (uint32_t)(FM_PCD_MAX_NUM_OF_CC_GROUPS * FM_PCD_CC_AD_ENTRY_SIZE));

    p_CcTreeTmp = UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr);

    for (i = 0; i < numOfEntries; i++)
    {
        p_KeyAndNextEngineParams = p_Params + i;

        NextStepAd(p_CcTreeTmp, NULL,
                   &p_KeyAndNextEngineParams->nextEngineParams, p_FmPcd);

        p_CcTreeTmp = PTR_MOVE(p_CcTreeTmp, FM_PCD_CC_AD_ENTRY_SIZE);

        memcpy(&p_FmPcdCcTree->keyAndNextEngineParams[i],
               p_KeyAndNextEngineParams,
               sizeof(t_FmPcdCcKeyAndNextEngineParams));

        if (p_FmPcdCcTree->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
        {
            p_FmPcdCcNextNode =
                    (t_FmPcdCcNode*)p_FmPcdCcTree->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode;
            p_CcInformation = FindNodeInfoInReleventLst(
                    &p_FmPcdCcNextNode->ccTreeIdLst, (t_Handle)p_FmPcdCcTree,
                    p_FmPcdCcNextNode->h_Spinlock);

            if (!p_CcInformation)
            {
                memset(&ccNodeInfo, 0, sizeof(t_CcNodeInformation));
                ccNodeInfo.h_CcNode = (t_Handle)p_FmPcdCcTree;
                ccNodeInfo.index = 1;
                EnqueueNodeInfoToRelevantLst(&p_FmPcdCcNextNode->ccTreeIdLst,
                                             &ccNodeInfo,
                                             p_FmPcdCcNextNode->h_Spinlock);
            }
            else
                p_CcInformation->index++;
        }
    }

    FmPcdIncNetEnvOwners(h_FmPcd, p_FmPcdCcTree->netEnvId);
    p_CcTreeTmp = UINT_TO_PTR(p_FmPcdCcTree->ccTreeBaseAddr);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        FM_PCD_CcRootDelete(p_FmPcdCcTree);
        XX_Free(p_Params);
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return NULL;
    }

    for (i = 0; i < numOfEntries; i++)
    {
        if (p_FmPcdCcTree->keyAndNextEngineParams[i].requiredAction)
        {
            err = SetRequiredAction(
                    h_FmPcd,
                    p_FmPcdCcTree->keyAndNextEngineParams[i].requiredAction,
                    &p_FmPcdCcTree->keyAndNextEngineParams[i], p_CcTreeTmp, 1,
                    p_FmPcdCcTree);
            if (err)
            {
                FmPcdLockUnlockAll(p_FmPcd);
                FM_PCD_CcRootDelete(p_FmPcdCcTree);
                XX_Free(p_Params);
                REPORT_ERROR(MAJOR, E_NO_MEMORY, ("No memory"));
                return NULL;
            }
            p_CcTreeTmp = PTR_MOVE(p_CcTreeTmp, FM_PCD_CC_AD_ENTRY_SIZE);
        }
    }

    FmPcdLockUnlockAll(p_FmPcd);
    p_FmPcdCcTree->p_Lock = FmPcdAcquireLock(p_FmPcd);
    if (!p_FmPcdCcTree->p_Lock)
    {
        FM_PCD_CcRootDelete(p_FmPcdCcTree);
        XX_Free(p_Params);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM CC lock"));
        return NULL;
    }

    XX_Free(p_Params);

    return p_FmPcdCcTree;
}

t_Error FM_PCD_CcRootDelete(t_Handle h_CcTree)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcTree *p_CcTree = (t_FmPcdCcTree *)h_CcTree;
    int i = 0;

    SANITY_CHECK_RETURN_ERROR(p_CcTree, E_INVALID_STATE);
    p_FmPcd = (t_FmPcd *)p_CcTree->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    FmPcdDecNetEnvOwners(p_FmPcd, p_CcTree->netEnvId);

    if (p_CcTree->owners)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_SELECTION,
                ("the tree with this ID can not be removed because this tree is occupied, first - unbind this tree"));

    /* Delete ip-reassembly schemes if exist */
    if (p_CcTree->h_IpReassemblyManip)
    {
        FmPcdManipDeleteIpReassmSchemes(p_CcTree->h_IpReassemblyManip);
        FmPcdManipUpdateOwner(p_CcTree->h_IpReassemblyManip, FALSE);
    }

    /* Delete capwap-reassembly schemes if exist */
    if (p_CcTree->h_CapwapReassemblyManip)
    {
        FmPcdManipDeleteCapwapReassmSchemes(p_CcTree->h_CapwapReassemblyManip);
        FmPcdManipUpdateOwner(p_CcTree->h_CapwapReassemblyManip, FALSE);
    }

    for (i = 0; i < p_CcTree->numOfEntries; i++)
    {
        if (p_CcTree->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
            UpdateNodeOwner(
                    p_CcTree->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode,
                    FALSE);

        if (p_CcTree->keyAndNextEngineParams[i].nextEngineParams.h_Manip)
            FmPcdManipUpdateOwner(
                    p_CcTree->keyAndNextEngineParams[i].nextEngineParams.h_Manip,
                    FALSE);

#ifdef FM_CAPWAP_SUPPORT
        if ((p_CcTree->numOfGrps == 1) &&
                (p_CcTree->fmPcdGroupParam[0].numOfEntriesInGroup == 1) &&
                (p_CcTree->keyAndNextEngineParams[0].nextEngineParams.nextEngine == e_FM_PCD_CC) &&
                p_CcTree->keyAndNextEngineParams[0].nextEngineParams.params.ccParams.h_CcNode &&
                IsCapwapApplSpecific(p_CcTree->keyAndNextEngineParams[0].nextEngineParams.params.ccParams.h_CcNode))
        {
            if (FM_PCD_ManipNodeDelete(p_CcTree->keyAndNextEngineParams[0].nextEngineParams.h_Manip) != E_OK)
            return E_INVALID_STATE;
        }
#endif /* FM_CAPWAP_SUPPORT */

#if (DPAA_VERSION >= 11)
        if ((p_CcTree->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && (p_CcTree->keyAndNextEngineParams[i].nextEngineParams.params.frParams.h_FrmReplic))
            FrmReplicGroupUpdateOwner(
                    p_CcTree->keyAndNextEngineParams[i].nextEngineParams.params.frParams.h_FrmReplic,
                    FALSE);
#endif /* (DPAA_VERSION >= 11) */
    }

    if (p_CcTree->p_Lock)
        FmPcdReleaseLock(p_CcTree->h_FmPcd, p_CcTree->p_Lock);

    DeleteTree(p_CcTree, p_FmPcd);

    return E_OK;
}

t_Error FM_PCD_CcRootModifyNextEngine(
        t_Handle h_CcTree, uint8_t grpId, uint8_t index,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcTree *p_CcTree = (t_FmPcdCcTree *)h_CcTree;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcTree, E_INVALID_STATE);
    p_FmPcd = (t_FmPcd *)p_CcTree->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcModifyNextEngineParamTree(p_FmPcd, p_CcTree, grpId, index,
                                           p_FmPcdCcNextEngineParams);
    FmPcdLockUnlockAll(p_FmPcd);

    if (err)
    {
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    return E_OK;
}

t_Handle FM_PCD_MatchTableSet(t_Handle h_FmPcd,
                              t_FmPcdCcNodeParams *p_CcNodeParam)
{
    t_FmPcdCcNode *p_CcNode;
    t_Error err;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_CcNodeParam, E_NULL_POINTER, NULL);

    p_CcNode = (t_FmPcdCcNode*)XX_Malloc(sizeof(t_FmPcdCcNode));
    if (!p_CcNode)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("No memory"));
        return NULL;
    }
    memset(p_CcNode, 0, sizeof(t_FmPcdCcNode));

    err = MatchTableSet(h_FmPcd, p_CcNode, p_CcNodeParam);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        break;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return NULL;

        default:
        REPORT_ERROR(MAJOR, err, NO_MSG);
        return NULL;
    }

    return p_CcNode;
}

t_Error FM_PCD_MatchTableDelete(t_Handle h_CcNode)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    int i = 0;

    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_CcNode->h_FmPcd, E_INVALID_HANDLE);

    if (p_CcNode->owners)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("This node cannot be removed because it is occupied; first unbind this node"));

    for (i = 0; i < p_CcNode->numOfKeys; i++)
        if (p_CcNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_CC)
            UpdateNodeOwner(
                    p_CcNode->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode,
                    FALSE);

    if (p_CcNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
            == e_FM_PCD_CC)
        UpdateNodeOwner(
                p_CcNode->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode,
                FALSE);

    /* Handle also Miss entry */
    for (i = 0; i < p_CcNode->numOfKeys + 1; i++)
    {
        if (p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip)
            FmPcdManipUpdateOwner(
                    p_CcNode->keyAndNextEngineParams[i].nextEngineParams.h_Manip,
                    FALSE);

#if (DPAA_VERSION >= 11)
        if ((p_CcNode->keyAndNextEngineParams[i].nextEngineParams.nextEngine
                == e_FM_PCD_FR)
                && (p_CcNode->keyAndNextEngineParams[i].nextEngineParams.params.frParams.h_FrmReplic))
        {
            FrmReplicGroupUpdateOwner(
                    p_CcNode->keyAndNextEngineParams[i].nextEngineParams.params.frParams.h_FrmReplic,
                    FALSE);
        }
#endif /* (DPAA_VERSION >= 11) */
    }

    DeleteNode(p_CcNode);

    return E_OK;
}

t_Error FM_PCD_MatchTableAddKey(t_Handle h_CcNode, uint16_t keyIndex,
                                uint8_t keySize,
                                t_FmPcdCcKeyParams *p_KeyParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_KeyParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (keyIndex == FM_PCD_LAST_KEY_INDEX)
        keyIndex = p_CcNode->numOfKeys;

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcAddKey(p_FmPcd, p_CcNode, keyIndex, keySize, p_KeyParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableRemoveKey(t_Handle h_CcNode, uint16_t keyIndex)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcRemoveKey(p_FmPcd, p_CcNode, keyIndex);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    return E_OK;
}

t_Error FM_PCD_MatchTableModifyKey(t_Handle h_CcNode, uint16_t keyIndex,
                                   uint8_t keySize, uint8_t *p_Key,
                                   uint8_t *p_Mask)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);


    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcModifyKey(p_FmPcd, p_CcNode, keyIndex, keySize, p_Key, p_Mask);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableModifyNextEngine(
        t_Handle h_CcNode, uint16_t keyIndex,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = ModifyNextEngineParamNode(p_FmPcd, p_CcNode, keyIndex,
                                    p_FmPcdCcNextEngineParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableModifyMissNextEngine(
        t_Handle h_CcNode, t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcModifyMissNextEngineParamNode(p_FmPcd, p_CcNode,
                                               p_FmPcdCcNextEngineParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableModifyKeyAndNextEngine(t_Handle h_CcNode,
                                                uint16_t keyIndex,
                                                uint8_t keySize,
                                                t_FmPcdCcKeyParams *p_KeyParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_KeyParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdCcModifyKeyAndNextEngine(p_FmPcd, p_CcNode, keyIndex, keySize,
                                        p_KeyParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableFindNRemoveKey(t_Handle h_CcNode, uint8_t keySize,
                                        uint8_t *p_Key, uint8_t *p_Mask)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint16_t keyIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FindKeyIndex(p_CcNode, keySize, p_Key, p_Mask, &keyIndex);
    if (GET_ERROR_TYPE(err) != E_OK)
    {
        FmPcdLockUnlockAll(p_FmPcd);
        RETURN_ERROR(
                MAJOR,
                err,
                ("The received key and mask pair was not found in the match table of the provided node"));
    }

    err = FmPcdCcRemoveKey(p_FmPcd, p_CcNode, keyIndex);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableFindNModifyNextEngine(
        t_Handle h_CcNode, uint8_t keySize, uint8_t *p_Key, uint8_t *p_Mask,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint16_t keyIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FindKeyIndex(p_CcNode, keySize, p_Key, p_Mask, &keyIndex);
    if (GET_ERROR_TYPE(err) != E_OK)
    {
        FmPcdLockUnlockAll(p_FmPcd);
        RETURN_ERROR(
                MAJOR,
                err,
                ("The received key and mask pair was not found in the match table of the provided node"));
    }

    err = ModifyNextEngineParamNode(p_FmPcd, p_CcNode, keyIndex,
                                    p_FmPcdCcNextEngineParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableFindNModifyKeyAndNextEngine(
        t_Handle h_CcNode, uint8_t keySize, uint8_t *p_Key, uint8_t *p_Mask,
        t_FmPcdCcKeyParams *p_KeyParams)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint16_t keyIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_KeyParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    if (!FmPcdLockTryLockAll(p_FmPcd))
    {
        DBG(TRACE, ("FmPcdLockTryLockAll failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FindKeyIndex(p_CcNode, keySize, p_Key, p_Mask, &keyIndex);
    if (GET_ERROR_TYPE(err) != E_OK)
    {
        FmPcdLockUnlockAll(p_FmPcd);
        RETURN_ERROR(
                MAJOR,
                err,
                ("The received key and mask pair was not found in the match table of the provided node"));
    }

    err = FmPcdCcModifyKeyAndNextEngine(p_FmPcd, h_CcNode, keyIndex, keySize,
                                        p_KeyParams);

    FmPcdLockUnlockAll(p_FmPcd);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableFindNModifyKey(t_Handle h_CcNode, uint8_t keySize,
                                        uint8_t *p_Key, uint8_t *p_Mask,
                                        uint8_t *p_NewKey, uint8_t *p_NewMask)
{
    t_FmPcd *p_FmPcd;
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    t_List h_List;
    uint16_t keyIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_NewKey, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    p_FmPcd = (t_FmPcd *)p_CcNode->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->h_Hc, E_INVALID_HANDLE);

    INIT_LIST(&h_List);

    err = FmPcdCcNodeTreeTryLock(p_FmPcd, p_CcNode, &h_List);
    if (err)
    {
        DBG(TRACE, ("Node's trees lock failed"));
        return ERROR_CODE(E_BUSY);
    }

    err = FindKeyIndex(p_CcNode, keySize, p_Key, p_Mask, &keyIndex);
    if (GET_ERROR_TYPE(err) != E_OK)
    {
        FmPcdCcNodeTreeReleaseLock(p_FmPcd, &h_List);
        RETURN_ERROR(MAJOR, err,
                     ("The received key and mask pair was not found in the "
                     "match table of the provided node"));
    }

    err = FmPcdCcModifyKey(p_FmPcd, p_CcNode, keyIndex, keySize, p_NewKey,
                           p_NewMask);

    FmPcdCcNodeTreeReleaseLock(p_FmPcd, &h_List);

    switch(GET_ERROR_TYPE(err)
)    {
        case E_OK:
        return E_OK;

        case E_BUSY:
        DBG(TRACE, ("E_BUSY error"));
        return ERROR_CODE(E_BUSY);

        default:
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }
}

t_Error FM_PCD_MatchTableGetNextEngine(
        t_Handle h_CcNode, uint16_t keyIndex,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;

    SANITY_CHECK_RETURN_ERROR(p_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);

    if (keyIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("keyIndex exceeds current number of keys"));

    if (keyIndex > (FM_PCD_MAX_NUM_OF_KEYS - 1))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("keyIndex can not be larger than %d", (FM_PCD_MAX_NUM_OF_KEYS - 1)));

    memcpy(p_FmPcdCcNextEngineParams,
           &p_CcNode->keyAndNextEngineParams[keyIndex].nextEngineParams,
           sizeof(t_FmPcdCcNextEngineParams));

    return E_OK;
}


uint32_t FM_PCD_MatchTableGetKeyCounter(t_Handle h_CcNode, uint16_t keyIndex)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint32_t *p_StatsCounters, frameCount;
    uint32_t intFlags;

    SANITY_CHECK_RETURN_VALUE(p_CcNode, E_INVALID_HANDLE, 0);

    if (p_CcNode->statisticsMode == e_FM_PCD_CC_STATS_MODE_NONE)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Statistics were not enabled for this match table"));
        return 0;
    }

    if ((p_CcNode->statisticsMode != e_FM_PCD_CC_STATS_MODE_FRAME)
            && (p_CcNode->statisticsMode
                    != e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME))
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Frame count is not supported in the statistics mode of this match table"));
        return 0;
    }

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    if (keyIndex >= p_CcNode->numOfKeys)
    {
        XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("The provided keyIndex exceeds the number of keys in this match table"));
        return 0;
    }

    if (!p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj)
    {
        XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Statistics were not enabled for this key"));
        return 0;
    }

    p_StatsCounters =
            p_CcNode->keyAndNextEngineParams[keyIndex].p_StatsObj->h_StatsCounters;
    ASSERT_COND(p_StatsCounters);

    /* The first counter is byte counter, so we need to advance to the next counter */
    frameCount = GET_UINT32(*(uint32_t *)(PTR_MOVE(p_StatsCounters,
                            FM_PCD_CC_STATS_COUNTER_SIZE)));

    XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

    return frameCount;
}

t_Error FM_PCD_MatchTableGetKeyStatistics(
        t_Handle h_CcNode, uint16_t keyIndex,
        t_FmPcdCcKeyStatistics *p_KeyStatistics)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint32_t intFlags;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(h_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_KeyStatistics, E_NULL_POINTER);

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    if (keyIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("The provided keyIndex exceeds the number of keys in this match table"));

    err = MatchTableGetKeyStatistics(p_CcNode, keyIndex, p_KeyStatistics);

    XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FM_PCD_MatchTableGetMissStatistics(
        t_Handle h_CcNode, t_FmPcdCcKeyStatistics *p_MissStatistics)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint32_t intFlags;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(h_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_MissStatistics, E_NULL_POINTER);

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    err = MatchTableGetKeyStatistics(p_CcNode, p_CcNode->numOfKeys,
                                     p_MissStatistics);

    XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FM_PCD_MatchTableFindNGetKeyStatistics(
        t_Handle h_CcNode, uint8_t keySize, uint8_t *p_Key, uint8_t *p_Mask,
        t_FmPcdCcKeyStatistics *p_KeyStatistics)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint16_t keyIndex;
    uint32_t intFlags;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_KeyStatistics, E_NULL_POINTER);

    intFlags = XX_LockIntrSpinlock(p_CcNode->h_Spinlock);

    err = FindKeyIndex(p_CcNode, keySize, p_Key, p_Mask, &keyIndex);
    if (GET_ERROR_TYPE(err) != E_OK)
    {
        XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);
        RETURN_ERROR(MAJOR, err,
                     ("The received key and mask pair was not found in the "
                     "match table of the provided node"));
    }

    ASSERT_COND(keyIndex < p_CcNode->numOfKeys);

    err = MatchTableGetKeyStatistics(p_CcNode, keyIndex, p_KeyStatistics);

    XX_UnlockIntrSpinlock(p_CcNode->h_Spinlock, intFlags);

    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FM_PCD_MatchTableGetIndexedHashBucket(t_Handle h_CcNode,
                                              uint8_t keySize, uint8_t *p_Key,
                                              uint8_t hashShift,
                                              t_Handle *p_CcNodeBucketHandle,
                                              uint8_t *p_BucketIndex,
                                              uint16_t *p_LastIndex)
{
    t_FmPcdCcNode *p_CcNode = (t_FmPcdCcNode *)h_CcNode;
    uint16_t glblMask;
    uint64_t crc64 = 0;

    SANITY_CHECK_RETURN_ERROR(h_CcNode, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(
            p_CcNode->parseCode == CC_PC_GENERIC_IC_HASH_INDEXED,
            E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_CcNodeBucketHandle, E_NULL_POINTER);

    memcpy(&glblMask, PTR_MOVE(p_CcNode->p_GlblMask, 2), 2);
    glblMask = be16toh(glblMask);

    crc64 = crc64_init();
    crc64 = crc64_compute(p_Key, keySize, crc64);
    crc64 >>= hashShift;

    *p_BucketIndex = (uint8_t)(((crc64 >> (8 * (6 - p_CcNode->userOffset)))
            & glblMask) >> 4);
    if (*p_BucketIndex >= p_CcNode->numOfKeys)
        RETURN_ERROR(MINOR, E_NOT_IN_RANGE, ("bucket index!"));

    *p_CcNodeBucketHandle =
            p_CcNode->keyAndNextEngineParams[*p_BucketIndex].nextEngineParams.params.ccParams.h_CcNode;
    if (!*p_CcNodeBucketHandle)
        RETURN_ERROR(MINOR, E_NOT_FOUND, ("bucket!"));

    *p_LastIndex = ((t_FmPcdCcNode *)*p_CcNodeBucketHandle)->numOfKeys;

    return E_OK;
}

t_Handle FM_PCD_HashTableSet(t_Handle h_FmPcd, t_FmPcdHashTableParams *p_Param)
{
    t_FmPcdCcNode *p_CcNodeHashTbl;
    t_FmPcdCcNodeParams *p_IndxHashCcNodeParam, *p_ExactMatchCcNodeParam;
    t_FmPcdCcNode *p_CcNode;
    t_Handle h_MissStatsCounters = NULL;
    t_FmPcdCcKeyParams *p_HashKeyParams;
    int i;
    uint16_t numOfSets, numOfWays, countMask, onesCount = 0;
    bool statsEnForMiss = FALSE;
    t_Error err;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_Param, E_NULL_POINTER, NULL);

    if (p_Param->maxNumOfKeys == 0)
    {
        REPORT_ERROR(MINOR, E_INVALID_VALUE, ("Max number of keys must be higher then 0"));
        return NULL;
    }

    if (p_Param->hashResMask == 0)
    {
        REPORT_ERROR(MINOR, E_INVALID_VALUE, ("Hash result mask must differ from 0"));
        return NULL;
    }

    /*Fix: QorIQ SDK / QSDK-2131*/
    if (p_Param->ccNextEngineParamsForMiss.nextEngine == e_FM_PCD_INVALID)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Next PCD Engine for on-miss entry is invalid. On-miss entry is always required. You can use e_FM_PCD_DONE."));
        return NULL;
    }

#if (DPAA_VERSION >= 11)
    if (p_Param->statisticsMode == e_FM_PCD_CC_STATS_MODE_RMON)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE,
                ("RMON statistics mode is not supported for hash table"));
        return NULL;
    }
#endif /* (DPAA_VERSION >= 11) */

    p_ExactMatchCcNodeParam = (t_FmPcdCcNodeParams*)XX_Malloc(
            sizeof(t_FmPcdCcNodeParams));
    if (!p_ExactMatchCcNodeParam)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("p_ExactMatchCcNodeParam"));
        return NULL;
    }
    memset(p_ExactMatchCcNodeParam, 0, sizeof(t_FmPcdCcNodeParams));

    p_IndxHashCcNodeParam = (t_FmPcdCcNodeParams*)XX_Malloc(
            sizeof(t_FmPcdCcNodeParams));
    if (!p_IndxHashCcNodeParam)
    {
        XX_Free(p_ExactMatchCcNodeParam);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("p_IndxHashCcNodeParam"));
        return NULL;
    }
    memset(p_IndxHashCcNodeParam, 0, sizeof(t_FmPcdCcNodeParams));

    /* Calculate number of sets and number of ways of the hash table */
    countMask = (uint16_t)(p_Param->hashResMask >> 4);
    while (countMask)
    {
        onesCount++;
        countMask = (uint16_t)(countMask >> 1);
    }

    numOfSets = (uint16_t)(1 << onesCount);
    numOfWays = (uint16_t)DIV_CEIL(p_Param->maxNumOfKeys, numOfSets);

    if (p_Param->maxNumOfKeys % numOfSets)
        DBG(INFO, ("'maxNumOfKeys' is not a multiple of hash number of ways, so number of ways will be rounded up"));

    if ((p_Param->statisticsMode == e_FM_PCD_CC_STATS_MODE_FRAME)
            || (p_Param->statisticsMode == e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME))
    {
        /* Allocating a statistics counters table that will be used by all
         'miss' entries of the hash table */
        h_MissStatsCounters = (t_Handle)FM_MURAM_AllocMem(
                FmPcdGetMuramHandle(h_FmPcd), 2 * FM_PCD_CC_STATS_COUNTER_SIZE,
                FM_PCD_CC_AD_TABLE_ALIGN);
        if (!h_MissStatsCounters)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("MURAM allocation for statistics table for hash miss"));
            XX_Free(p_IndxHashCcNodeParam);
            XX_Free(p_ExactMatchCcNodeParam);
            return NULL;
        }
        memset(h_MissStatsCounters, 0, (2 * FM_PCD_CC_STATS_COUNTER_SIZE));

        /* Always enable statistics for 'miss', so that a statistics AD will be
         initialized from the start. We'll store the requested 'statistics enable'
         value and it will be used when statistics are read by the user. */
        statsEnForMiss = p_Param->ccNextEngineParamsForMiss.statisticsEn;
        p_Param->ccNextEngineParamsForMiss.statisticsEn = TRUE;
    }

    /* Building exact-match node params, will be used to create the hash buckets */
    p_ExactMatchCcNodeParam->extractCcParams.type = e_FM_PCD_EXTRACT_NON_HDR;

    p_ExactMatchCcNodeParam->extractCcParams.extractNonHdr.src =
            e_FM_PCD_EXTRACT_FROM_KEY;
    p_ExactMatchCcNodeParam->extractCcParams.extractNonHdr.action =
            e_FM_PCD_ACTION_EXACT_MATCH;
    p_ExactMatchCcNodeParam->extractCcParams.extractNonHdr.offset = 0;
    p_ExactMatchCcNodeParam->extractCcParams.extractNonHdr.size =
            p_Param->matchKeySize;

    p_ExactMatchCcNodeParam->keysParams.maxNumOfKeys = numOfWays;
    p_ExactMatchCcNodeParam->keysParams.maskSupport = FALSE;
    p_ExactMatchCcNodeParam->keysParams.statisticsMode =
            p_Param->statisticsMode;
    p_ExactMatchCcNodeParam->keysParams.numOfKeys = 0;
    p_ExactMatchCcNodeParam->keysParams.keySize = p_Param->matchKeySize;
    p_ExactMatchCcNodeParam->keysParams.ccNextEngineParamsForMiss =
            p_Param->ccNextEngineParamsForMiss;

    p_HashKeyParams = p_IndxHashCcNodeParam->keysParams.keyParams;

    for (i = 0; i < numOfSets; i++)
    {
        /* Each exact-match node will be marked as a 'bucket' and provided with
           a pointer to statistics counters, to be used for 'miss' entry
           statistics */
        p_CcNode = (t_FmPcdCcNode *)XX_Malloc(sizeof(t_FmPcdCcNode));
        if (!p_CcNode)
            break;
        memset(p_CcNode, 0, sizeof(t_FmPcdCcNode));

        p_CcNode->isHashBucket = TRUE;
        p_CcNode->h_MissStatsCounters = h_MissStatsCounters;

        err = MatchTableSet(h_FmPcd, p_CcNode, p_ExactMatchCcNodeParam);
        if (err)
            break;

        p_HashKeyParams[i].ccNextEngineParams.nextEngine = e_FM_PCD_CC;
        p_HashKeyParams[i].ccNextEngineParams.statisticsEn = FALSE;
        p_HashKeyParams[i].ccNextEngineParams.params.ccParams.h_CcNode =
                p_CcNode;
    }

    if (i < numOfSets)
    {
        for (i = i - 1; i >= 0; i--)
            FM_PCD_MatchTableDelete(
                    p_HashKeyParams[i].ccNextEngineParams.params.ccParams.h_CcNode);

        FM_MURAM_FreeMem(FmPcdGetMuramHandle(h_FmPcd), h_MissStatsCounters);

        REPORT_ERROR(MAJOR, E_NULL_POINTER, NO_MSG);
        XX_Free(p_IndxHashCcNodeParam);
        XX_Free(p_ExactMatchCcNodeParam);
        return NULL;
    }

    /* Creating indexed-hash CC node */
    p_IndxHashCcNodeParam->extractCcParams.type = e_FM_PCD_EXTRACT_NON_HDR;
    p_IndxHashCcNodeParam->extractCcParams.extractNonHdr.src =
            e_FM_PCD_EXTRACT_FROM_HASH;
    p_IndxHashCcNodeParam->extractCcParams.extractNonHdr.action =
            e_FM_PCD_ACTION_INDEXED_LOOKUP;
    p_IndxHashCcNodeParam->extractCcParams.extractNonHdr.icIndxMask =
            p_Param->hashResMask;
    p_IndxHashCcNodeParam->extractCcParams.extractNonHdr.offset =
            p_Param->hashShift;
    p_IndxHashCcNodeParam->extractCcParams.extractNonHdr.size = 2;

    p_IndxHashCcNodeParam->keysParams.maxNumOfKeys = numOfSets;
    p_IndxHashCcNodeParam->keysParams.maskSupport = FALSE;
    p_IndxHashCcNodeParam->keysParams.statisticsMode =
            e_FM_PCD_CC_STATS_MODE_NONE;
    /* Number of keys of this node is number of sets of the hash */
    p_IndxHashCcNodeParam->keysParams.numOfKeys = numOfSets;
    p_IndxHashCcNodeParam->keysParams.keySize = 2;

    p_CcNodeHashTbl = FM_PCD_MatchTableSet(h_FmPcd, p_IndxHashCcNodeParam);

    if (p_CcNodeHashTbl)
    {
        p_CcNodeHashTbl->kgHashShift = p_Param->kgHashShift;

        /* Storing the allocated counters for buckets 'miss' in the hash table
         and if statistics for miss were enabled. */
        p_CcNodeHashTbl->h_MissStatsCounters = h_MissStatsCounters;
        p_CcNodeHashTbl->statsEnForMiss = statsEnForMiss;
    }

    XX_Free(p_IndxHashCcNodeParam);
    XX_Free(p_ExactMatchCcNodeParam);

    return p_CcNodeHashTbl;
}

t_Error FM_PCD_HashTableDelete(t_Handle h_HashTbl)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_FmPcd;
    t_Handle *p_HashBuckets, h_MissStatsCounters;
    uint16_t i, numOfBuckets;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);

    /* Store all hash buckets before the hash is freed */
    numOfBuckets = p_HashTbl->numOfKeys;

    p_HashBuckets = (t_Handle *)XX_Malloc(numOfBuckets * sizeof(t_Handle));
    if (!p_HashBuckets)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, NO_MSG);

    for (i = 0; i < numOfBuckets; i++)
        p_HashBuckets[i] =
                p_HashTbl->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode;

    h_FmPcd = p_HashTbl->h_FmPcd;
    h_MissStatsCounters = p_HashTbl->h_MissStatsCounters;

    /* Free the hash */
    err = FM_PCD_MatchTableDelete(p_HashTbl);

    /* Free each hash bucket */
    for (i = 0; i < numOfBuckets; i++)
        err |= FM_PCD_MatchTableDelete(p_HashBuckets[i]);

    XX_Free(p_HashBuckets);

    /* Free statistics counters for 'miss', if these were allocated */
    if (h_MissStatsCounters)
        FM_MURAM_FreeMem(FmPcdGetMuramHandle(h_FmPcd), h_MissStatsCounters);

    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FM_PCD_HashTableAddKey(t_Handle h_HashTbl, uint8_t keySize,
                               t_FmPcdCcKeyParams *p_KeyParams)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;
    uint8_t bucketIndex;
    uint16_t lastIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_KeyParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_KeyParams->p_Key, E_NULL_POINTER);

    if (p_KeyParams->p_Mask)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("Keys masks not supported for hash table"));

    err = FM_PCD_MatchTableGetIndexedHashBucket(p_HashTbl, keySize,
                                                p_KeyParams->p_Key,
                                                p_HashTbl->kgHashShift,
                                                &h_HashBucket, &bucketIndex,
                                                &lastIndex);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return FM_PCD_MatchTableAddKey(h_HashBucket, FM_PCD_LAST_KEY_INDEX, keySize,
                                   p_KeyParams);
}

t_Error FM_PCD_HashTableRemoveKey(t_Handle h_HashTbl, uint8_t keySize,
                                  uint8_t *p_Key)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;
    uint8_t bucketIndex;
    uint16_t lastIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);

    err = FM_PCD_MatchTableGetIndexedHashBucket(p_HashTbl, keySize, p_Key,
                                                p_HashTbl->kgHashShift,
                                                &h_HashBucket, &bucketIndex,
                                                &lastIndex);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return FM_PCD_MatchTableFindNRemoveKey(h_HashBucket, keySize, p_Key, NULL);
}

t_Error FM_PCD_HashTableModifyNextEngine(
        t_Handle h_HashTbl, uint8_t keySize, uint8_t *p_Key,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;
    uint8_t bucketIndex;
    uint16_t lastIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);

    err = FM_PCD_MatchTableGetIndexedHashBucket(p_HashTbl, keySize, p_Key,
                                                p_HashTbl->kgHashShift,
                                                &h_HashBucket, &bucketIndex,
                                                &lastIndex);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return FM_PCD_MatchTableFindNModifyNextEngine(h_HashBucket, keySize, p_Key,
                                                  NULL,
                                                  p_FmPcdCcNextEngineParams);
}

t_Error FM_PCD_HashTableModifyMissNextEngine(
        t_Handle h_HashTbl,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;
    uint8_t i;
    bool nullifyMissStats = FALSE;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(h_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcdCcNextEngineParams, E_NULL_POINTER);

    if ((!p_HashTbl->h_MissStatsCounters)
            && (p_FmPcdCcNextEngineParams->statisticsEn))
        RETURN_ERROR(
                MAJOR,
                E_CONFLICT,
                ("Statistics are requested for a key, but statistics mode was set"
                "to 'NONE' upon initialization"));

    if (p_HashTbl->h_MissStatsCounters)
    {
        if ((!p_HashTbl->statsEnForMiss)
                && (p_FmPcdCcNextEngineParams->statisticsEn))
            nullifyMissStats = TRUE;

        if ((p_HashTbl->statsEnForMiss)
                && (!p_FmPcdCcNextEngineParams->statisticsEn))
        {
            p_HashTbl->statsEnForMiss = FALSE;
            p_FmPcdCcNextEngineParams->statisticsEn = TRUE;
        }
    }

    for (i = 0; i < p_HashTbl->numOfKeys; i++)
    {
        h_HashBucket =
                p_HashTbl->keyAndNextEngineParams[i].nextEngineParams.params.ccParams.h_CcNode;

        err = FM_PCD_MatchTableModifyMissNextEngine(h_HashBucket,
                                                    p_FmPcdCcNextEngineParams);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (nullifyMissStats)
    {
        memset(p_HashTbl->h_MissStatsCounters, 0,
               (2 * FM_PCD_CC_STATS_COUNTER_SIZE));
        memset(p_HashTbl->h_MissStatsCounters, 0,
               (2 * FM_PCD_CC_STATS_COUNTER_SIZE));
        p_HashTbl->statsEnForMiss = TRUE;
    }

    return E_OK;
}


t_Error FM_PCD_HashTableGetMissNextEngine(
        t_Handle h_HashTbl,
        t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_FmPcdCcNode *p_HashBucket;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);

    /* Miss next engine of each bucket was initialized with the next engine of the hash table */
    p_HashBucket =
            p_HashTbl->keyAndNextEngineParams[0].nextEngineParams.params.ccParams.h_CcNode;

    memcpy(p_FmPcdCcNextEngineParams,
           &p_HashBucket->keyAndNextEngineParams[p_HashBucket->numOfKeys].nextEngineParams,
           sizeof(t_FmPcdCcNextEngineParams));

    return E_OK;
}

t_Error FM_PCD_HashTableFindNGetKeyStatistics(
        t_Handle h_HashTbl, uint8_t keySize, uint8_t *p_Key,
        t_FmPcdCcKeyStatistics *p_KeyStatistics)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;
    uint8_t bucketIndex;
    uint16_t lastIndex;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Key, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_KeyStatistics, E_NULL_POINTER);

    err = FM_PCD_MatchTableGetIndexedHashBucket(p_HashTbl, keySize, p_Key,
                                                p_HashTbl->kgHashShift,
                                                &h_HashBucket, &bucketIndex,
                                                &lastIndex);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return FM_PCD_MatchTableFindNGetKeyStatistics(h_HashBucket, keySize, p_Key,
                                                  NULL, p_KeyStatistics);
}

t_Error FM_PCD_HashTableGetMissStatistics(
        t_Handle h_HashTbl, t_FmPcdCcKeyStatistics *p_MissStatistics)
{
    t_FmPcdCcNode *p_HashTbl = (t_FmPcdCcNode *)h_HashTbl;
    t_Handle h_HashBucket;

    SANITY_CHECK_RETURN_ERROR(p_HashTbl, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_MissStatistics, E_NULL_POINTER);

    if (!p_HashTbl->statsEnForMiss)
        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                     ("Statistics were not enabled for miss"));

    h_HashBucket =
            p_HashTbl->keyAndNextEngineParams[0].nextEngineParams.params.ccParams.h_CcNode;

    return FM_PCD_MatchTableGetMissStatistics(h_HashBucket, p_MissStatistics);
}
