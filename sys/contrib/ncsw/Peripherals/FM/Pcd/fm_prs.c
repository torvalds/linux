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
#include <linux/math64.h>
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "net_ext.h"

#include "fm_common.h"
#include "fm_pcd.h"
#include "fm_pcd_ipc.h"
#include "fm_prs.h"
#include "fsl_fman_prs.h"


static void PcdPrsErrorException(t_Handle h_FmPcd)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint32_t                event, ev_mask;
    struct fman_prs_regs     *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    ev_mask = fman_prs_get_err_ev_mask(PrsRegs);

    event = fman_prs_get_err_event(PrsRegs, ev_mask);

    fman_prs_ack_err_event(PrsRegs, event);

    DBG(TRACE, ("parser error - 0x%08x\n",event));

    if(event & FM_PCD_PRS_DOUBLE_ECC)
        p_FmPcd->f_Exception(p_FmPcd->h_App,e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC);
}

static void PcdPrsException(t_Handle h_FmPcd)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint32_t            event, ev_mask;
    struct fman_prs_regs     *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    ev_mask = fman_prs_get_expt_ev_mask(PrsRegs);
    event = fman_prs_get_expt_event(PrsRegs, ev_mask);

    ASSERT_COND(event & FM_PCD_PRS_SINGLE_ECC);

    DBG(TRACE, ("parser event - 0x%08x\n",event));

    fman_prs_ack_expt_event(PrsRegs, event);

    p_FmPcd->f_Exception(p_FmPcd->h_App,e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC);
}

t_Handle PrsConfig(t_FmPcd *p_FmPcd,t_FmPcdParams *p_FmPcdParams)
{
    t_FmPcdPrs  *p_FmPcdPrs;
    uintptr_t   baseAddr;

    UNUSED(p_FmPcd);
    UNUSED(p_FmPcdParams);

    p_FmPcdPrs = (t_FmPcdPrs *) XX_Malloc(sizeof(t_FmPcdPrs));
    if (!p_FmPcdPrs)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Parser structure allocation FAILED"));
        return NULL;
    }
    memset(p_FmPcdPrs, 0, sizeof(t_FmPcdPrs));
    fman_prs_defconfig(&p_FmPcd->p_FmPcdDriverParam->dfltCfg);

    if (p_FmPcd->guestId == NCSW_MASTER_ID)
    {
        baseAddr = FmGetPcdPrsBaseAddr(p_FmPcdParams->h_Fm);
        p_FmPcdPrs->p_SwPrsCode  = (uint32_t *)UINT_TO_PTR(baseAddr);
        p_FmPcdPrs->p_FmPcdPrsRegs  = (struct fman_prs_regs *)UINT_TO_PTR(baseAddr + PRS_REGS_OFFSET);
    }

    p_FmPcdPrs->fmPcdPrsPortIdStatistics = p_FmPcd->p_FmPcdDriverParam->dfltCfg.port_id_stat;
    p_FmPcd->p_FmPcdDriverParam->prsMaxParseCycleLimit = p_FmPcd->p_FmPcdDriverParam->dfltCfg.max_prs_cyc_lim;
    p_FmPcd->exceptions |= p_FmPcd->p_FmPcdDriverParam->dfltCfg.prs_exceptions;

    return p_FmPcdPrs;
}

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
    static uint8_t             swPrsPatch[] = SW_PRS_UDP_LITE_PATCH;
#else
    static uint8_t             swPrsPatch[] = SW_PRS_OFFLOAD_PATCH;
#endif /* FM_CAPWAP_SUPPORT */

t_Error PrsInit(t_FmPcd *p_FmPcd)
{
    t_FmPcdDriverParam  *p_Param = p_FmPcd->p_FmPcdDriverParam;
    uint32_t            *p_TmpCode;
    uint32_t            *p_LoadTarget = (uint32_t *)PTR_MOVE(p_FmPcd->p_FmPcdPrs->p_SwPrsCode,
                                                             FM_PCD_SW_PRS_SIZE-FM_PCD_PRS_SW_PATCHES_SIZE);
    struct fman_prs_regs *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;
    uint32_t            i;

    ASSERT_COND(sizeof(swPrsPatch) <= (FM_PCD_PRS_SW_PATCHES_SIZE-FM_PCD_PRS_SW_TAIL_SIZE));

    /* nothing to do in guest-partition */
    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        return E_OK;

    p_TmpCode = (uint32_t *)XX_MallocSmart(ROUND_UP(sizeof(swPrsPatch),4), 0, sizeof(uint32_t));
    if (!p_TmpCode)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Tmp Sw-Parser code allocation FAILED"));
    memset((uint8_t *)p_TmpCode, 0, ROUND_UP(sizeof(swPrsPatch),4));
    memcpy((uint8_t *)p_TmpCode, (uint8_t *)swPrsPatch, sizeof(swPrsPatch));

    fman_prs_init(PrsRegs, &p_Param->dfltCfg);

    /* register even if no interrupts enabled, to allow future enablement */
    FmRegisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PRS, 0, e_FM_INTR_TYPE_ERR, PcdPrsErrorException, p_FmPcd);

    /* register even if no interrupts enabled, to allow future enablement */
    FmRegisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PRS, 0, e_FM_INTR_TYPE_NORMAL, PcdPrsException, p_FmPcd);

    if(p_FmPcd->exceptions & FM_PCD_EX_PRS_SINGLE_ECC)
        FmEnableRamsEcc(p_FmPcd->h_Fm);

    if(p_FmPcd->exceptions & FM_PCD_EX_PRS_DOUBLE_ECC)
        FmEnableRamsEcc(p_FmPcd->h_Fm);

    /* load sw parser Ip-Frag patch */
    for (i=0; i<DIV_CEIL(sizeof(swPrsPatch), 4); i++)
        WRITE_UINT32(p_LoadTarget[i], GET_UINT32(p_TmpCode[i]));

    XX_FreeSmart(p_TmpCode);

    return E_OK;
}

void PrsFree(t_FmPcd *p_FmPcd)
{
    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    FmUnregisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PRS, 0, e_FM_INTR_TYPE_ERR);
    /* register even if no interrupts enabled, to allow future enablement */
    FmUnregisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PRS, 0, e_FM_INTR_TYPE_NORMAL);
}

void PrsEnable(t_FmPcd *p_FmPcd)
{
    struct fman_prs_regs *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    fman_prs_enable(PrsRegs);
}

void PrsDisable(t_FmPcd *p_FmPcd)
{
    struct fman_prs_regs *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    fman_prs_disable(PrsRegs);
}

int PrsIsEnabled(t_FmPcd *p_FmPcd)
{
    struct fman_prs_regs *PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    ASSERT_COND(p_FmPcd->guestId == NCSW_MASTER_ID);
    return fman_prs_is_enabled(PrsRegs);
}

t_Error PrsIncludePortInStatistics(t_FmPcd *p_FmPcd, uint8_t hardwarePortId, bool include)
{
    struct fman_prs_regs *PrsRegs;
    uint32_t    bitMask = 0;
    uint8_t     prsPortId;

    SANITY_CHECK_RETURN_ERROR((hardwarePortId >=1 && hardwarePortId <= 16), E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPrs, E_INVALID_HANDLE);

    PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;

    GET_FM_PCD_PRS_PORT_ID(prsPortId, hardwarePortId);
    GET_FM_PCD_INDEX_FLAG(bitMask, prsPortId);

    if (include)
        p_FmPcd->p_FmPcdPrs->fmPcdPrsPortIdStatistics |= bitMask;
    else
        p_FmPcd->p_FmPcdPrs->fmPcdPrsPortIdStatistics &= ~bitMask;

    fman_prs_set_stst_port_msk(PrsRegs,
            p_FmPcd->p_FmPcdPrs->fmPcdPrsPortIdStatistics);

    return E_OK;
}

t_Error FmPcdPrsIncludePortInStatistics(t_Handle h_FmPcd, uint8_t hardwarePortId, bool include)
{
    t_FmPcd                     *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_Error                     err;

    SANITY_CHECK_RETURN_ERROR((hardwarePortId >=1 && hardwarePortId <= 16), E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPrs, E_INVALID_HANDLE);

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        p_FmPcd->h_IpcSession)
    {
        t_FmPcdIpcPrsIncludePort    prsIncludePortParams;
        t_FmPcdIpcMsg               msg;

        prsIncludePortParams.hardwarePortId = hardwarePortId;
        prsIncludePortParams.include = include;
        memset(&msg, 0, sizeof(msg));
        msg.msgId = FM_PCD_PRS_INC_PORT_STATS;
        memcpy(msg.msgBody, &prsIncludePortParams, sizeof(prsIncludePortParams));
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) +sizeof(prsIncludePortParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        return E_OK;
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    return PrsIncludePortInStatistics(p_FmPcd, hardwarePortId, include);
}

uint32_t FmPcdGetSwPrsOffset(t_Handle h_FmPcd, e_NetHeaderType hdr, uint8_t indexPerHdr)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd *)h_FmPcd;
    t_FmPcdPrsLabelParams   *p_Label;
    int                     i;

    SANITY_CHECK_RETURN_VALUE(p_FmPcd, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE, 0);

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        p_FmPcd->h_IpcSession)
    {
        t_Error                 err = E_OK;
        t_FmPcdIpcSwPrsLable    labelParams;
        t_FmPcdIpcMsg           msg;
        uint32_t                prsOffset = 0;
        t_FmPcdIpcReply         reply;
        uint32_t                replyLength;

        memset(&reply, 0, sizeof(reply));
        memset(&msg, 0, sizeof(msg));
        labelParams.enumHdr = (uint32_t)hdr;
        labelParams.indexPerHdr = indexPerHdr;
        msg.msgId = FM_PCD_GET_SW_PRS_OFFSET;
        memcpy(msg.msgBody, &labelParams, sizeof(labelParams));
        replyLength = sizeof(uint32_t) + sizeof(uint32_t);
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) +sizeof(labelParams),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        if (replyLength != sizeof(uint32_t) + sizeof(uint32_t))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));

        memcpy((uint8_t*)&prsOffset, reply.replyBody, sizeof(uint32_t));
        return prsOffset;
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("running in guest-mode without IPC!"));

    ASSERT_COND(p_FmPcd->p_FmPcdPrs->currLabel < FM_PCD_PRS_NUM_OF_LABELS);

    for (i=0; i<p_FmPcd->p_FmPcdPrs->currLabel; i++)
    {
        p_Label = &p_FmPcd->p_FmPcdPrs->labelsTable[i];

        if ((hdr == p_Label->hdr) && (indexPerHdr == p_Label->indexPerHdr))
            return p_Label->instructionOffset;
    }

    REPORT_ERROR(MAJOR, E_NOT_FOUND, ("Sw Parser attachment Not found"));
    return (uint32_t)ILLEGAL_BASE;
}

void FM_PCD_SetPrsStatistics(t_Handle h_FmPcd, bool enable)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd*)h_FmPcd;
    struct fman_prs_regs *PrsRegs;

    SANITY_CHECK_RETURN(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(p_FmPcd->p_FmPcdPrs, E_INVALID_HANDLE);

    PrsRegs = (struct fman_prs_regs *)p_FmPcd->p_FmPcdPrs->p_FmPcdPrsRegs;


    if(p_FmPcd->guestId != NCSW_MASTER_ID)
    {
        REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_SetPrsStatistics - guest mode!"));
        return;
    }

    fman_prs_set_stst(PrsRegs, enable);
}

t_Error FM_PCD_PrsLoadSw(t_Handle h_FmPcd, t_FmPcdPrsSwParams *p_SwPrs)
{
    t_FmPcd                 *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t                *p_LoadTarget;
    uint32_t                *p_TmpCode;
    int                     i;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPrs, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_SwPrs, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->enabled, E_INVALID_HANDLE);

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM in guest-mode!"));

    if (!p_SwPrs->override)
    {
        if(p_FmPcd->p_FmPcdPrs->p_CurrSwPrs > p_FmPcd->p_FmPcdPrs->p_SwPrsCode + p_SwPrs->base*2/4)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("SW parser base must be larger than current loaded code"));
    }
    else
        p_FmPcd->p_FmPcdPrs->currLabel = 0;

    if (p_SwPrs->size > FM_PCD_SW_PRS_SIZE - FM_PCD_PRS_SW_TAIL_SIZE - p_SwPrs->base*2)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("p_SwPrs->size may not be larger than MAX_SW_PRS_CODE_SIZE"));

    if (p_FmPcd->p_FmPcdPrs->currLabel + p_SwPrs->numOfLabels > FM_PCD_PRS_NUM_OF_LABELS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceeded number of labels allowed "));

    p_TmpCode = (uint32_t *)XX_MallocSmart(ROUND_UP(p_SwPrs->size,4), 0, sizeof(uint32_t));
    if (!p_TmpCode)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Tmp Sw-Parser code allocation FAILED"));
    memset((uint8_t *)p_TmpCode, 0, ROUND_UP(p_SwPrs->size,4));
    memcpy((uint8_t *)p_TmpCode, p_SwPrs->p_Code, p_SwPrs->size);

    /* save sw parser labels */
    memcpy(&p_FmPcd->p_FmPcdPrs->labelsTable[p_FmPcd->p_FmPcdPrs->currLabel],
           p_SwPrs->labelsTable,
           p_SwPrs->numOfLabels*sizeof(t_FmPcdPrsLabelParams));
    p_FmPcd->p_FmPcdPrs->currLabel += p_SwPrs->numOfLabels;

    /* load sw parser code */
    p_LoadTarget = p_FmPcd->p_FmPcdPrs->p_SwPrsCode + p_SwPrs->base*2/4;

    for(i=0; i<DIV_CEIL(p_SwPrs->size, 4); i++)
        WRITE_UINT32(p_LoadTarget[i], GET_UINT32(p_TmpCode[i]));

    p_FmPcd->p_FmPcdPrs->p_CurrSwPrs =
        p_FmPcd->p_FmPcdPrs->p_SwPrsCode + p_SwPrs->base*2/4 + ROUND_UP(p_SwPrs->size,4);

    /* copy data parameters */
    for (i=0;i<FM_PCD_PRS_NUM_OF_HDRS;i++)
        WRITE_UINT32(*(p_FmPcd->p_FmPcdPrs->p_SwPrsCode+PRS_SW_DATA/4+i), p_SwPrs->swPrsDataParams[i]);

    /* Clear last 4 bytes */
    WRITE_UINT32(*(p_FmPcd->p_FmPcdPrs->p_SwPrsCode+(PRS_SW_DATA-FM_PCD_PRS_SW_TAIL_SIZE)/4), 0);

    XX_FreeSmart(p_TmpCode);

    return E_OK;
}

t_Error FM_PCD_ConfigPrsMaxCycleLimit(t_Handle h_FmPcd,uint16_t value)
{
    t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);

    if(p_FmPcd->guestId != NCSW_MASTER_ID)
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_ConfigPrsMaxCycleLimit - guest mode!"));

    p_FmPcd->p_FmPcdDriverParam->prsMaxParseCycleLimit = value;

    return E_OK;
}
