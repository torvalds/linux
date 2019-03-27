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


#include "std_ext.h"
#include "error_ext.h"
#include "sprint_ext.h"
#include "string_ext.h"

#include "fm_common.h"
#include "fm_hc.h"


/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/
#define DEFAULT_dataMemId                                       0

#define HC_HCOR_OPCODE_PLCR_PRFL                                0x0
#define HC_HCOR_OPCODE_KG_SCM                                   0x1
#define HC_HCOR_OPCODE_SYNC                                     0x2
#define HC_HCOR_OPCODE_CC                                       0x3
#define HC_HCOR_OPCODE_CC_AGE_MASK                              0x4
#define HC_HCOR_OPCODE_CC_CAPWAP_REASSM_TIMEOUT                 0x5
#define HC_HCOR_OPCODE_CC_REASSM_TIMEOUT                        0x10
#define HC_HCOR_OPCODE_CC_IP_FRAG_INITIALIZATION                0x11
#define HC_HCOR_OPCODE_CC_UPDATE_WITH_AGING                     0x13
#define HC_HCOR_ACTION_REG_REASSM_TIMEOUT_ACTIVE_SHIFT          24
#define HC_HCOR_EXTRA_REG_REASSM_TIMEOUT_TSBS_SHIFT             24
#define HC_HCOR_EXTRA_REG_CC_AGING_ADD                          0x80000000
#define HC_HCOR_EXTRA_REG_CC_AGING_REMOVE                       0x40000000
#define HC_HCOR_EXTRA_REG_CC_AGING_CHANGE_MASK                  0xC0000000
#define HC_HCOR_EXTRA_REG_CC_REMOVE_INDX_SHIFT                  24
#define HC_HCOR_EXTRA_REG_CC_REMOVE_INDX_MASK                   0x1F000000
#define HC_HCOR_ACTION_REG_REASSM_TIMEOUT_RES_SHIFT             16
#define HC_HCOR_ACTION_REG_REASSM_TIMEOUT_RES_MASK              0xF
#define HC_HCOR_ACTION_REG_IP_FRAG_SCRATCH_POOL_CMD_SHIFT       24
#define HC_HCOR_ACTION_REG_IP_FRAG_SCRATCH_POOL_BPID            16

#define HC_HCOR_GBL                         0x20000000

#define HC_HCOR_KG_SCHEME_COUNTER           0x00000400

#if (DPAA_VERSION == 10)
#define HC_HCOR_KG_SCHEME_REGS_MASK         0xFFFFF800
#else
#define HC_HCOR_KG_SCHEME_REGS_MASK         0xFFFFFE00
#endif /* (DPAA_VERSION == 10) */

#define SIZE_OF_HC_FRAME_PORT_REGS          (sizeof(t_HcFrame)-sizeof(struct fman_kg_scheme_regs)+sizeof(t_FmPcdKgPortRegs))
#define SIZE_OF_HC_FRAME_SCHEME_REGS        sizeof(t_HcFrame)
#define SIZE_OF_HC_FRAME_PROFILES_REGS      (sizeof(t_HcFrame)-sizeof(struct fman_kg_scheme_regs)+sizeof(t_FmPcdPlcrProfileRegs))
#define SIZE_OF_HC_FRAME_PROFILE_CNT        (sizeof(t_HcFrame)-sizeof(t_FmPcdPlcrProfileRegs)+sizeof(uint32_t))
#define SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC 16

#define HC_CMD_POOL_SIZE                    (INTG_MAX_NUM_OF_CORES)

#define BUILD_FD(len)                     \
do {                                      \
    memset(&fmFd, 0, sizeof(t_DpaaFD));   \
    DPAA_FD_SET_ADDR(&fmFd, p_HcFrame);   \
    DPAA_FD_SET_OFFSET(&fmFd, 0);         \
    DPAA_FD_SET_LENGTH(&fmFd, len);       \
} while (0)


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

typedef struct t_FmPcdKgPortRegs {
    volatile uint32_t                       spReg;
    volatile uint32_t                       cppReg;
} t_FmPcdKgPortRegs;

typedef struct t_HcFrame {
    volatile uint32_t                           opcode;
    volatile uint32_t                           actionReg;
    volatile uint32_t                           extraReg;
    volatile uint32_t                           commandSequence;
    union {
        struct fman_kg_scheme_regs              schemeRegs;
        struct fman_kg_scheme_regs              schemeRegsWithoutCounter;
        t_FmPcdPlcrProfileRegs                  profileRegs;
        volatile uint32_t                       singleRegForWrite;    /* for writing SP, CPP, profile counter */
        t_FmPcdKgPortRegs                       portRegsForRead;
        volatile uint32_t                       clsPlanEntries[CLS_PLAN_NUM_PER_GRP];
        t_FmPcdCcCapwapReassmTimeoutParams      ccCapwapReassmTimeout;
        t_FmPcdCcReassmTimeoutParams            ccReassmTimeout;
    } hcSpecificData;
} t_HcFrame;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


typedef struct t_FmHc {
    t_Handle                    h_FmPcd;
    t_Handle                    h_HcPortDev;
    t_FmPcdQmEnqueueCallback    *f_QmEnqueue;     /**< A callback for enqueuing frames to the QM */
    t_Handle                    h_QmArg;          /**< A handle to the QM module */
    uint8_t                     dataMemId;        /**< Memory partition ID for data buffers */

    uint32_t                    seqNum[HC_CMD_POOL_SIZE];   /* FIFO of seqNum to use when
                                                               taking buffer */
    uint32_t                    nextSeqNumLocation;         /* seqNum location in seqNum[] for next buffer */
    volatile bool               enqueued[HC_CMD_POOL_SIZE]; /* HC is active - frame is enqueued
                                                               and not confirmed yet */
    t_HcFrame                   *p_Frm[HC_CMD_POOL_SIZE];
} t_FmHc;


static t_Error FillBufPool(t_FmHc *p_FmHc)
{
    uint32_t i;

    ASSERT_COND(p_FmHc);

    for (i = 0; i < HC_CMD_POOL_SIZE; i++)
    {
#ifdef FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004
        p_FmHc->p_Frm[i] = (t_HcFrame *)XX_MallocSmart((sizeof(t_HcFrame) + (16 - (sizeof(t_FmHc) % 16))),
                                                       p_FmHc->dataMemId,
                                                       16);
#else
        p_FmHc->p_Frm[i] = (t_HcFrame *)XX_MallocSmart(sizeof(t_HcFrame),
                                                       p_FmHc->dataMemId,
                                                       16);
#endif /* FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004 */
        if (!p_FmHc->p_Frm[i])
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FM HC frames!"));
    }

    /* Initialize FIFO of seqNum to use during GetBuf */
    for (i = 0; i < HC_CMD_POOL_SIZE; i++)
    {
        p_FmHc->seqNum[i] = i;
    }
    p_FmHc->nextSeqNumLocation = 0;

    return E_OK;
}

static __inline__ t_HcFrame * GetBuf(t_FmHc *p_FmHc, uint32_t *p_SeqNum)
{
    uint32_t    intFlags;

    ASSERT_COND(p_FmHc);

    intFlags = FmPcdLock(p_FmHc->h_FmPcd);

    if (p_FmHc->nextSeqNumLocation == HC_CMD_POOL_SIZE)
    {
        /* No more buffers */
        FmPcdUnlock(p_FmHc->h_FmPcd, intFlags);
        return NULL;
    }

    *p_SeqNum = p_FmHc->seqNum[p_FmHc->nextSeqNumLocation];
    p_FmHc->nextSeqNumLocation++;

    FmPcdUnlock(p_FmHc->h_FmPcd, intFlags);
    return p_FmHc->p_Frm[*p_SeqNum];
}

static __inline__ void PutBuf(t_FmHc *p_FmHc, t_HcFrame *p_Buf, uint32_t seqNum)
{
    uint32_t    intFlags;

    UNUSED(p_Buf);

    intFlags = FmPcdLock(p_FmHc->h_FmPcd);
    ASSERT_COND(p_FmHc->nextSeqNumLocation);
    p_FmHc->nextSeqNumLocation--;
    p_FmHc->seqNum[p_FmHc->nextSeqNumLocation] = seqNum;
    FmPcdUnlock(p_FmHc->h_FmPcd, intFlags);
}

static __inline__ t_Error EnQFrm(t_FmHc *p_FmHc, t_DpaaFD *p_FmFd, uint32_t seqNum)
{
    t_Error     err = E_OK;
    uint32_t    intFlags;
    uint32_t    timeout=100;

    intFlags = FmPcdLock(p_FmHc->h_FmPcd);
    ASSERT_COND(!p_FmHc->enqueued[seqNum]);
    p_FmHc->enqueued[seqNum] = TRUE;
    FmPcdUnlock(p_FmHc->h_FmPcd, intFlags);
    DBG(TRACE, ("Send Hc, SeqNum %d, buff@0x%x, fd offset 0x%x",
                seqNum,
                DPAA_FD_GET_ADDR(p_FmFd),
                DPAA_FD_GET_OFFSET(p_FmFd)));
    err = p_FmHc->f_QmEnqueue(p_FmHc->h_QmArg, (void *)p_FmFd);
    if (err)
        RETURN_ERROR(MINOR, err, ("HC enqueue failed"));

    while (p_FmHc->enqueued[seqNum] && --timeout)
        XX_UDelay(100);

    if (!timeout)
        RETURN_ERROR(MINOR, E_TIMEOUT, ("HC Callback, timeout exceeded"));

    return err;
}


t_Handle FmHcConfigAndInit(t_FmHcParams *p_FmHcParams)
{
    t_FmHc          *p_FmHc;
    t_FmPortParams  fmPortParam;
    t_Error         err;

    p_FmHc = (t_FmHc *)XX_Malloc(sizeof(t_FmHc));
    if (!p_FmHc)
    {
        REPORT_ERROR(MINOR, E_NO_MEMORY, ("HC obj"));
        return NULL;
    }
    memset(p_FmHc,0,sizeof(t_FmHc));

    p_FmHc->h_FmPcd             = p_FmHcParams->h_FmPcd;
    p_FmHc->f_QmEnqueue         = p_FmHcParams->params.f_QmEnqueue;
    p_FmHc->h_QmArg             = p_FmHcParams->params.h_QmArg;
    p_FmHc->dataMemId           = DEFAULT_dataMemId;

    err = FillBufPool(p_FmHc);
    if (err != E_OK)
    {
        REPORT_ERROR(MAJOR, err, NO_MSG);
        FmHcFree(p_FmHc);
        return NULL;
    }

    if (!FmIsMaster(p_FmHcParams->h_Fm))
        return (t_Handle)p_FmHc;

    memset(&fmPortParam, 0, sizeof(fmPortParam));
    fmPortParam.baseAddr    = p_FmHcParams->params.portBaseAddr;
    fmPortParam.portType    = e_FM_PORT_TYPE_OH_HOST_COMMAND;
    fmPortParam.portId      = p_FmHcParams->params.portId;
    fmPortParam.liodnBase   = p_FmHcParams->params.liodnBase;
    fmPortParam.h_Fm        = p_FmHcParams->h_Fm;

    fmPortParam.specificParams.nonRxParams.errFqid      = p_FmHcParams->params.errFqid;
    fmPortParam.specificParams.nonRxParams.dfltFqid     = p_FmHcParams->params.confFqid;
    fmPortParam.specificParams.nonRxParams.qmChannel    = p_FmHcParams->params.qmChannel;

    p_FmHc->h_HcPortDev = FM_PORT_Config(&fmPortParam);
    if (!p_FmHc->h_HcPortDev)
    {
        REPORT_ERROR(MAJOR, E_INVALID_HANDLE, ("FM HC port!"));
        XX_Free(p_FmHc);
        return NULL;
    }

    err = FM_PORT_ConfigMaxFrameLength(p_FmHc->h_HcPortDev,
                                       (uint16_t)sizeof(t_HcFrame));

    if (err != E_OK)
    {
        REPORT_ERROR(MAJOR, err, ("FM HC port init!"));
        FmHcFree(p_FmHc);
        return NULL;
    }

    /* final init */
    err = FM_PORT_Init(p_FmHc->h_HcPortDev);
    if (err != E_OK)
    {
        REPORT_ERROR(MAJOR, err, ("FM HC port init!"));
        FmHcFree(p_FmHc);
        return NULL;
    }

    err = FM_PORT_Enable(p_FmHc->h_HcPortDev);
    if (err != E_OK)
    {
        REPORT_ERROR(MAJOR, err, ("FM HC port enable!"));
        FmHcFree(p_FmHc);
        return NULL;
    }

    return (t_Handle)p_FmHc;
}

void FmHcFree(t_Handle h_FmHc)
{
    t_FmHc  *p_FmHc = (t_FmHc*)h_FmHc;
    int     i;

    if (!p_FmHc)
        return;

    for (i=0; i<HC_CMD_POOL_SIZE; i++)
        if (p_FmHc->p_Frm[i])
            XX_FreeSmart(p_FmHc->p_Frm[i]);
        else
            break;

    if (p_FmHc->h_HcPortDev)
        FM_PORT_Free(p_FmHc->h_HcPortDev);

    XX_Free(p_FmHc);
}

/*****************************************************************************/
t_Error FmHcSetFramesDataMemory(t_Handle h_FmHc,
                                uint8_t  memId)
{
    t_FmHc  *p_FmHc = (t_FmHc*)h_FmHc;
    int     i;

    SANITY_CHECK_RETURN_ERROR(p_FmHc, E_INVALID_HANDLE);

    p_FmHc->dataMemId            = memId;

    for (i=0; i<HC_CMD_POOL_SIZE; i++)
        if (p_FmHc->p_Frm[i])
            XX_FreeSmart(p_FmHc->p_Frm[i]);

    return FillBufPool(p_FmHc);
}

void FmHcTxConf(t_Handle h_FmHc, t_DpaaFD *p_Fd)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame   *p_HcFrame;
    uint32_t    intFlags;

    ASSERT_COND(p_FmHc);

    intFlags = FmPcdLock(p_FmHc->h_FmPcd);
    p_HcFrame  = (t_HcFrame *)PTR_MOVE(DPAA_FD_GET_ADDR(p_Fd), DPAA_FD_GET_OFFSET(p_Fd));

    DBG(TRACE, ("Hc Conf, SeqNum %d, FD@0x%x, fd offset 0x%x",
                p_HcFrame->commandSequence, DPAA_FD_GET_ADDR(p_Fd), DPAA_FD_GET_OFFSET(p_Fd)));

    if (!(p_FmHc->enqueued[p_HcFrame->commandSequence]))
        REPORT_ERROR(MINOR, E_INVALID_FRAME, ("Not an Host-Command frame received!"));
    else
        p_FmHc->enqueued[p_HcFrame->commandSequence] = FALSE;
    FmPcdUnlock(p_FmHc->h_FmPcd, intFlags);
}

t_Error FmHcPcdKgSetScheme(t_Handle                    h_FmHc,
                           t_Handle                    h_Scheme,
                           struct fman_kg_scheme_regs  *p_SchemeRegs,
                           bool                        updateCounter)
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error                             err = E_OK;
    t_HcFrame                           *p_HcFrame;
    t_DpaaFD                            fmFd;
    uint8_t                             physicalSchemeId;
    uint32_t                            seqNum;

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    physicalSchemeId = FmPcdKgGetSchemeId(h_Scheme);

    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildWriteSchemeActionReg(physicalSchemeId, updateCounter);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
    memcpy(&p_HcFrame->hcSpecificData.schemeRegs, p_SchemeRegs, sizeof(struct fman_kg_scheme_regs));
    if (!updateCounter)
    {
        p_HcFrame->hcSpecificData.schemeRegs.kgse_dv0   = p_SchemeRegs->kgse_dv0;
        p_HcFrame->hcSpecificData.schemeRegs.kgse_dv1   = p_SchemeRegs->kgse_dv1;
        p_HcFrame->hcSpecificData.schemeRegs.kgse_ccbs  = p_SchemeRegs->kgse_ccbs;
        p_HcFrame->hcSpecificData.schemeRegs.kgse_mv    = p_SchemeRegs->kgse_mv;
    }
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcPcdKgDeleteScheme(t_Handle h_FmHc, t_Handle h_Scheme)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error     err = E_OK;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint8_t     physicalSchemeId = FmPcdKgGetSchemeId(h_Scheme);
    uint32_t    seqNum;

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildWriteSchemeActionReg(physicalSchemeId, TRUE);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
    memset(&p_HcFrame->hcSpecificData.schemeRegs, 0, sizeof(struct fman_kg_scheme_regs));
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcPcdKgCcGetSetParams(t_Handle h_FmHc, t_Handle  h_Scheme, uint32_t requiredAction, uint32_t value)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error     err = E_OK;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint8_t     relativeSchemeId;
    uint8_t     physicalSchemeId = FmPcdKgGetSchemeId(h_Scheme);
    uint32_t    tmpReg32 = 0;
    uint32_t    seqNum;

    /* Scheme is locked by calling routine */
    /* WARNING - this lock will not be efficient if other HC routine will attempt to change
     * "kgse_mode" or "kgse_om" without locking scheme !
     */

    relativeSchemeId = FmPcdKgGetRelativeSchemeId(p_FmHc->h_FmPcd, physicalSchemeId);
    if ( relativeSchemeId == FM_PCD_KG_NUM_OF_SCHEMES)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);

    if (!FmPcdKgGetRequiredActionFlag(p_FmHc->h_FmPcd, relativeSchemeId) ||
       !(FmPcdKgGetRequiredAction(p_FmHc->h_FmPcd, relativeSchemeId) & requiredAction))
    {
        if ((requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA) &&
            (FmPcdKgGetNextEngine(p_FmHc->h_FmPcd, relativeSchemeId) == e_FM_PCD_PLCR))
            {
                if ((FmPcdKgIsDirectPlcr(p_FmHc->h_FmPcd, relativeSchemeId) == FALSE) ||
                    (FmPcdKgIsDistrOnPlcrProfile(p_FmHc->h_FmPcd, relativeSchemeId) == TRUE))
                    RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("In this situation PP can not be with distribution and has to be shared"));
                err = FmPcdPlcrCcGetSetParams(p_FmHc->h_FmPcd, FmPcdKgGetRelativeProfileId(p_FmHc->h_FmPcd, relativeSchemeId), requiredAction);
                if (err)
                    RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        else /* From here we deal with KG-Schemes only */
        {
            /* Pre change general code */
            p_HcFrame = GetBuf(p_FmHc, &seqNum);
            if (!p_HcFrame)
                RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
            memset(p_HcFrame, 0, sizeof(t_HcFrame));
            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
            p_HcFrame->actionReg  = FmPcdKgBuildReadSchemeActionReg(physicalSchemeId);
            p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
            p_HcFrame->commandSequence = seqNum;
            BUILD_FD(SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC);
            if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MINOR, err, NO_MSG);
            }

            /* specific change */
            if ((requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA) &&
                ((FmPcdKgGetNextEngine(p_FmHc->h_FmPcd, relativeSchemeId) == e_FM_PCD_DONE) &&
                 (FmPcdKgGetDoneAction(p_FmHc->h_FmPcd, relativeSchemeId) ==  e_FM_PCD_ENQ_FRAME)))
            {
                tmpReg32 = p_HcFrame->hcSpecificData.schemeRegs.kgse_mode;
                ASSERT_COND(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME));
                p_HcFrame->hcSpecificData.schemeRegs.kgse_mode =  tmpReg32 | NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;
            }

            if ((requiredAction & UPDATE_KG_NIA_CC_WA) &&
                (FmPcdKgGetNextEngine(p_FmHc->h_FmPcd, relativeSchemeId) == e_FM_PCD_CC))
            {
                tmpReg32 = p_HcFrame->hcSpecificData.schemeRegs.kgse_mode;
                ASSERT_COND(tmpReg32 & (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_CC));
                tmpReg32 &= ~NIA_FM_CTL_AC_CC;
                p_HcFrame->hcSpecificData.schemeRegs.kgse_mode =  tmpReg32 | NIA_FM_CTL_AC_PRE_CC;
            }

            if (requiredAction & UPDATE_KG_OPT_MODE)
                p_HcFrame->hcSpecificData.schemeRegs.kgse_om = value;

            if (requiredAction & UPDATE_KG_NIA)
            {
                tmpReg32 = p_HcFrame->hcSpecificData.schemeRegs.kgse_mode;
                tmpReg32 &= ~(NIA_ENG_MASK | NIA_AC_MASK);
                tmpReg32 |= value;
                p_HcFrame->hcSpecificData.schemeRegs.kgse_mode = tmpReg32;
            }

            /* Post change general code */
            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
            p_HcFrame->actionReg  = FmPcdKgBuildWriteSchemeActionReg(physicalSchemeId, FALSE);
            p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;

            BUILD_FD(sizeof(t_HcFrame));
            err = EnQFrm(p_FmHc, &fmFd, seqNum);

            PutBuf(p_FmHc, p_HcFrame, seqNum);

            if (err != E_OK)
                RETURN_ERROR(MINOR, err, NO_MSG);
        }
    }

    return E_OK;
}

uint32_t  FmHcPcdKgGetSchemeCounter(t_Handle h_FmHc, t_Handle h_Scheme)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error     err;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint32_t    retVal;
    uint8_t     relativeSchemeId;
    uint8_t     physicalSchemeId = FmPcdKgGetSchemeId(h_Scheme);
    uint32_t    seqNum;

    relativeSchemeId = FmPcdKgGetRelativeSchemeId(p_FmHc->h_FmPcd, physicalSchemeId);
    if ( relativeSchemeId == FM_PCD_KG_NUM_OF_SCHEMES)
    {
        REPORT_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);
        return 0;
    }

    /* first read scheme and check that it is valid */
    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
    {
        REPORT_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
        return 0;
    }
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildReadSchemeActionReg(physicalSchemeId);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC);

    err = EnQFrm(p_FmHc, &fmFd, seqNum);
    if (err != E_OK)
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        REPORT_ERROR(MINOR, err, NO_MSG);
        return 0;
    }

    if (!FmPcdKgHwSchemeIsValid(p_HcFrame->hcSpecificData.schemeRegs.kgse_mode))
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        REPORT_ERROR(MAJOR, E_ALREADY_EXISTS, ("Scheme is invalid"));
        return 0;
    }

    retVal = p_HcFrame->hcSpecificData.schemeRegs.kgse_spc;
    PutBuf(p_FmHc, p_HcFrame, seqNum);

    return retVal;
}

t_Error  FmHcPcdKgSetSchemeCounter(t_Handle h_FmHc, t_Handle h_Scheme, uint32_t value)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error     err = E_OK;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint8_t     relativeSchemeId, physicalSchemeId;
    uint32_t    seqNum;

    physicalSchemeId = FmPcdKgGetSchemeId(h_Scheme);
    relativeSchemeId = FmPcdKgGetRelativeSchemeId(p_FmHc->h_FmPcd, physicalSchemeId);
    if ( relativeSchemeId == FM_PCD_KG_NUM_OF_SCHEMES)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);

    /* first read scheme and check that it is valid */
    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildWriteSchemeActionReg(physicalSchemeId, TRUE);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_COUNTER;
    /* write counter */
    p_HcFrame->hcSpecificData.singleRegForWrite = value;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return err;
}

t_Error FmHcPcdKgSetClsPlan(t_Handle h_FmHc, t_FmPcdKgInterModuleClsPlanSet *p_Set)
{
    t_FmHc                  *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame               *p_HcFrame;
    t_DpaaFD                fmFd;
    uint8_t                 i, idx;
    uint32_t                seqNum;
    t_Error                 err = E_OK;

    ASSERT_COND(p_FmHc);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    for (i = p_Set->baseEntry; i < (p_Set->baseEntry+p_Set->numOfClsPlanEntries); i+=8)
    {
        memset(p_HcFrame, 0, sizeof(t_HcFrame));
        p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
        p_HcFrame->actionReg  = FmPcdKgBuildWriteClsPlanBlockActionReg((uint8_t)(i / CLS_PLAN_NUM_PER_GRP));
        p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;

        idx = (uint8_t)(i - p_Set->baseEntry);
        memcpy(&p_HcFrame->hcSpecificData.clsPlanEntries, &p_Set->vectors[idx], CLS_PLAN_NUM_PER_GRP*sizeof(uint32_t));
        p_HcFrame->commandSequence = seqNum;

        BUILD_FD(sizeof(t_HcFrame));

        if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
        {
            PutBuf(p_FmHc, p_HcFrame, seqNum);
            RETURN_ERROR(MINOR, err, NO_MSG);
        }
    }

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return err;
}

t_Error FmHcPcdKgDeleteClsPlan(t_Handle h_FmHc, uint8_t  grpId)
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_FmPcdKgInterModuleClsPlanSet      *p_ClsPlanSet;

    p_ClsPlanSet = (t_FmPcdKgInterModuleClsPlanSet *)XX_Malloc(sizeof(t_FmPcdKgInterModuleClsPlanSet));
    if (!p_ClsPlanSet)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Classification plan set"));

    memset(p_ClsPlanSet, 0, sizeof(t_FmPcdKgInterModuleClsPlanSet));

    p_ClsPlanSet->baseEntry = FmPcdKgGetClsPlanGrpBase(p_FmHc->h_FmPcd, grpId);
    p_ClsPlanSet->numOfClsPlanEntries = FmPcdKgGetClsPlanGrpSize(p_FmHc->h_FmPcd, grpId);
    ASSERT_COND(p_ClsPlanSet->numOfClsPlanEntries <= FM_PCD_MAX_NUM_OF_CLS_PLANS);

    if (FmHcPcdKgSetClsPlan(p_FmHc, p_ClsPlanSet) != E_OK)
    {
        XX_Free(p_ClsPlanSet);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);
    }

    XX_Free(p_ClsPlanSet);
    FmPcdKgDestroyClsPlanGrp(p_FmHc->h_FmPcd, grpId);

    return E_OK;
}

t_Error FmHcPcdCcCapwapTimeoutReassm(t_Handle h_FmHc, t_FmPcdCcCapwapReassmTimeoutParams *p_CcCapwapReassmTimeoutParams )
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame                           *p_HcFrame;
    t_DpaaFD                            fmFd;
    t_Error                             err;
    uint32_t                            seqNum;

    SANITY_CHECK_RETURN_VALUE(h_FmHc, E_INVALID_HANDLE,0);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_CC_CAPWAP_REASSM_TIMEOUT);
    memcpy(&p_HcFrame->hcSpecificData.ccCapwapReassmTimeout, p_CcCapwapReassmTimeoutParams, sizeof(t_FmPcdCcCapwapReassmTimeoutParams));
    p_HcFrame->commandSequence = seqNum;
    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return err;
}

t_Error FmHcPcdCcIpFragScratchPollCmd(t_Handle h_FmHc, bool fill, t_FmPcdCcFragScratchPoolCmdParams *p_FmPcdCcFragScratchPoolCmdParams)
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame                           *p_HcFrame;
    t_DpaaFD                            fmFd;
    t_Error                             err;
    uint32_t                            seqNum;

    SANITY_CHECK_RETURN_VALUE(h_FmHc, E_INVALID_HANDLE,0);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    memset(p_HcFrame, 0, sizeof(t_HcFrame));

    p_HcFrame->opcode     = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_CC_IP_FRAG_INITIALIZATION);
    p_HcFrame->actionReg  = (uint32_t)(((fill == TRUE) ? 0 : 1) << HC_HCOR_ACTION_REG_IP_FRAG_SCRATCH_POOL_CMD_SHIFT);
    p_HcFrame->actionReg |= p_FmPcdCcFragScratchPoolCmdParams->bufferPoolId << HC_HCOR_ACTION_REG_IP_FRAG_SCRATCH_POOL_BPID;
    if (fill == TRUE)
    {
        p_HcFrame->extraReg   = p_FmPcdCcFragScratchPoolCmdParams->numOfBuffers;
    }
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));
    if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    p_FmPcdCcFragScratchPoolCmdParams->numOfBuffers = p_HcFrame->extraReg;

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return E_OK;
}

t_Error FmHcPcdCcTimeoutReassm(t_Handle h_FmHc, t_FmPcdCcReassmTimeoutParams *p_CcReassmTimeoutParams, uint8_t *p_Result)
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame                           *p_HcFrame;
    t_DpaaFD                            fmFd;
    t_Error                             err;
    uint32_t                            seqNum;

    SANITY_CHECK_RETURN_VALUE(h_FmHc, E_INVALID_HANDLE,0);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_CC_REASSM_TIMEOUT);
    p_HcFrame->actionReg = (uint32_t)((p_CcReassmTimeoutParams->activate ? 0 : 1) << HC_HCOR_ACTION_REG_REASSM_TIMEOUT_ACTIVE_SHIFT);
    p_HcFrame->extraReg = (p_CcReassmTimeoutParams->tsbs << HC_HCOR_EXTRA_REG_REASSM_TIMEOUT_TSBS_SHIFT) | p_CcReassmTimeoutParams->iprcpt;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));
    if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    *p_Result = (uint8_t)
        ((p_HcFrame->actionReg >> HC_HCOR_ACTION_REG_REASSM_TIMEOUT_RES_SHIFT) & HC_HCOR_ACTION_REG_REASSM_TIMEOUT_RES_MASK);

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return E_OK;
}

t_Error FmHcPcdPlcrCcGetSetParams(t_Handle h_FmHc,uint16_t absoluteProfileId, uint32_t requiredAction)
{
    t_FmHc              *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame           *p_HcFrame;
    t_DpaaFD            fmFd;
    t_Error             err;
    uint32_t            tmpReg32 = 0;
    uint32_t            requiredActionTmp, requiredActionFlag;
    uint32_t            seqNum;

    SANITY_CHECK_RETURN_VALUE(h_FmHc, E_INVALID_HANDLE,0);

    /* Profile is locked by calling routine */
    /* WARNING - this lock will not be efficient if other HC routine will attempt to change
     * "fmpl_pegnia" "fmpl_peynia" or "fmpl_pernia" without locking Profile !
     */

    requiredActionTmp = FmPcdPlcrGetRequiredAction(p_FmHc->h_FmPcd, absoluteProfileId);
    requiredActionFlag = FmPcdPlcrGetRequiredActionFlag(p_FmHc->h_FmPcd, absoluteProfileId);

    if (!requiredActionFlag || !(requiredActionTmp & requiredAction))
    {
        if (requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA)
        {
            p_HcFrame = GetBuf(p_FmHc, &seqNum);
            if (!p_HcFrame)
                RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
            /* first read scheme and check that it is valid */
            memset(p_HcFrame, 0, sizeof(t_HcFrame));
            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
            p_HcFrame->actionReg  = FmPcdPlcrBuildReadPlcrActionReg(absoluteProfileId);
            p_HcFrame->extraReg = 0x00008000;
            p_HcFrame->commandSequence = seqNum;

            BUILD_FD(SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC);

            if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MINOR, err, NO_MSG);
            }

            tmpReg32 = p_HcFrame->hcSpecificData.profileRegs.fmpl_pegnia;
            if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
            }

            tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;

            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
            p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionReg(absoluteProfileId);
            p_HcFrame->actionReg |= FmPcdPlcrBuildNiaProfileReg(TRUE, FALSE, FALSE);
            p_HcFrame->extraReg = 0x00008000;
            p_HcFrame->hcSpecificData.singleRegForWrite = tmpReg32;

            BUILD_FD(SIZE_OF_HC_FRAME_PROFILE_CNT);

            if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MINOR, err, NO_MSG);
            }

            tmpReg32 = p_HcFrame->hcSpecificData.profileRegs.fmpl_peynia;
            if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
            }

            tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;

            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
            p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionReg(absoluteProfileId);
            p_HcFrame->actionReg |= FmPcdPlcrBuildNiaProfileReg(FALSE, TRUE, FALSE);
            p_HcFrame->extraReg = 0x00008000;
            p_HcFrame->hcSpecificData.singleRegForWrite = tmpReg32;

            BUILD_FD(SIZE_OF_HC_FRAME_PROFILE_CNT);

            if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MINOR, err, NO_MSG);
            }

            tmpReg32 = p_HcFrame->hcSpecificData.profileRegs.fmpl_pernia;
            if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
            }

            tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;

            p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
            p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionReg(absoluteProfileId);
            p_HcFrame->actionReg |= FmPcdPlcrBuildNiaProfileReg(FALSE, FALSE, TRUE);
            p_HcFrame->extraReg = 0x00008000;
            p_HcFrame->hcSpecificData.singleRegForWrite = tmpReg32;

            BUILD_FD(SIZE_OF_HC_FRAME_PROFILE_CNT);

            if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
            {
                PutBuf(p_FmHc, p_HcFrame, seqNum);
                RETURN_ERROR(MINOR, err, NO_MSG);
            }

            PutBuf(p_FmHc, p_HcFrame, seqNum);
        }
    }

    return E_OK;
}

t_Error FmHcPcdPlcrSetProfile(t_Handle h_FmHc, t_Handle h_Profile, t_FmPcdPlcrProfileRegs *p_PlcrRegs)
{
    t_FmHc                              *p_FmHc = (t_FmHc*)h_FmHc;
    t_Error                             err = E_OK;
    uint16_t                            profileIndx;
    t_HcFrame                           *p_HcFrame;
    t_DpaaFD                            fmFd;
    uint32_t                            seqNum;

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));

    profileIndx = FmPcdPlcrProfileGetAbsoluteId(h_Profile);

    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
    p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionRegs(profileIndx);
    p_HcFrame->extraReg = 0x00008000;
    memcpy(&p_HcFrame->hcSpecificData.profileRegs, p_PlcrRegs, sizeof(t_FmPcdPlcrProfileRegs));
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcPcdPlcrDeleteProfile(t_Handle h_FmHc, t_Handle h_Profile)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    uint16_t    absoluteProfileId = FmPcdPlcrProfileGetAbsoluteId(h_Profile);
    t_Error     err = E_OK;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint32_t    seqNum;

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
    p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionReg(absoluteProfileId);
    p_HcFrame->actionReg  |= 0x00008000;
    p_HcFrame->extraReg = 0x00008000;
    memset(&p_HcFrame->hcSpecificData.profileRegs, 0, sizeof(t_FmPcdPlcrProfileRegs));
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error  FmHcPcdPlcrSetProfileCounter(t_Handle h_FmHc, t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter, uint32_t value)
{

    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    uint16_t    absoluteProfileId = FmPcdPlcrProfileGetAbsoluteId(h_Profile);
    t_Error     err = E_OK;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint32_t    seqNum;

    /* first read scheme and check that it is valid */
    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
    p_HcFrame->actionReg  = FmPcdPlcrBuildWritePlcrActionReg(absoluteProfileId);
    p_HcFrame->actionReg |= FmPcdPlcrBuildCounterProfileReg(counter);
    p_HcFrame->extraReg = 0x00008000;
    p_HcFrame->hcSpecificData.singleRegForWrite = value;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(SIZE_OF_HC_FRAME_PROFILE_CNT);

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

uint32_t FmHcPcdPlcrGetProfileCounter(t_Handle h_FmHc, t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter)
{
    t_FmHc      *p_FmHc = (t_FmHc*)h_FmHc;
    uint16_t    absoluteProfileId = FmPcdPlcrProfileGetAbsoluteId(h_Profile);
    t_Error     err;
    t_HcFrame   *p_HcFrame;
    t_DpaaFD    fmFd;
    uint32_t    retVal = 0;
    uint32_t    seqNum;

    SANITY_CHECK_RETURN_VALUE(h_FmHc, E_INVALID_HANDLE,0);

    /* first read scheme and check that it is valid */
    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
    {
        REPORT_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
        return 0;
    }
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_PLCR_PRFL);
    p_HcFrame->actionReg  = FmPcdPlcrBuildReadPlcrActionReg(absoluteProfileId);
    p_HcFrame->extraReg = 0x00008000;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC);

    err = EnQFrm(p_FmHc, &fmFd, seqNum);
    if (err != E_OK)
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        REPORT_ERROR(MINOR, err, NO_MSG);
        return 0;
    }

    switch (counter)
    {
        case e_FM_PCD_PLCR_PROFILE_GREEN_PACKET_TOTAL_COUNTER:
            retVal = p_HcFrame->hcSpecificData.profileRegs.fmpl_pegpc;
            break;
        case e_FM_PCD_PLCR_PROFILE_YELLOW_PACKET_TOTAL_COUNTER:
            retVal = p_HcFrame->hcSpecificData.profileRegs.fmpl_peypc;
            break;
        case e_FM_PCD_PLCR_PROFILE_RED_PACKET_TOTAL_COUNTER:
            retVal = p_HcFrame->hcSpecificData.profileRegs.fmpl_perpc;
            break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_YELLOW_PACKET_TOTAL_COUNTER:
            retVal = p_HcFrame->hcSpecificData.profileRegs.fmpl_perypc;
            break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_RED_PACKET_TOTAL_COUNTER:
            retVal = p_HcFrame->hcSpecificData.profileRegs.fmpl_perrpc;
            break;
        default:
            REPORT_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    PutBuf(p_FmHc, p_HcFrame, seqNum);
    return retVal;
}

t_Error FmHcKgWriteSp(t_Handle h_FmHc, uint8_t hardwarePortId, uint32_t spReg, bool add)
{
    t_FmHc                  *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame               *p_HcFrame;
    t_DpaaFD                fmFd;
    t_Error                 err = E_OK;
    uint32_t                seqNum;

    ASSERT_COND(p_FmHc);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    /* first read SP register */
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildReadPortSchemeBindActionReg(hardwarePortId);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(SIZE_OF_HC_FRAME_PORT_REGS);

    if ((err = EnQFrm(p_FmHc, &fmFd, seqNum)) != E_OK)
    {
        PutBuf(p_FmHc, p_HcFrame, seqNum);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    /* spReg is the first reg, so we can use it both for read and for write */
    if (add)
        p_HcFrame->hcSpecificData.portRegsForRead.spReg |= spReg;
    else
        p_HcFrame->hcSpecificData.portRegsForRead.spReg &= ~spReg;

    p_HcFrame->actionReg  = FmPcdKgBuildWritePortSchemeBindActionReg(hardwarePortId);

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcKgWriteCpp(t_Handle h_FmHc, uint8_t hardwarePortId, uint32_t cppReg)
{
    t_FmHc                  *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame               *p_HcFrame;
    t_DpaaFD                fmFd;
    t_Error                 err = E_OK;
    uint32_t                seqNum;

    ASSERT_COND(p_FmHc);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    /* first read SP register */
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_KG_SCM);
    p_HcFrame->actionReg  = FmPcdKgBuildWritePortClsPlanBindActionReg(hardwarePortId);
    p_HcFrame->extraReg = HC_HCOR_KG_SCHEME_REGS_MASK;
    p_HcFrame->hcSpecificData.singleRegForWrite = cppReg;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcPcdCcDoDynamicChange(t_Handle h_FmHc, uint32_t oldAdAddrOffset, uint32_t newAdAddrOffset)
{
    t_FmHc                  *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame               *p_HcFrame;
    t_DpaaFD                fmFd;
    t_Error                 err = E_OK;
    uint32_t                seqNum;

    SANITY_CHECK_RETURN_ERROR(p_FmHc, E_INVALID_HANDLE);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));

    p_HcFrame->opcode     = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_CC);
    p_HcFrame->actionReg  = newAdAddrOffset;
    p_HcFrame->actionReg |= 0xc0000000;
    p_HcFrame->extraReg   = oldAdAddrOffset;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(SIZE_OF_HC_FRAME_READ_OR_CC_DYNAMIC);

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FmHcPcdSync(t_Handle h_FmHc)
{
    t_FmHc                  *p_FmHc = (t_FmHc*)h_FmHc;
    t_HcFrame               *p_HcFrame;
    t_DpaaFD                fmFd;
    t_Error                 err = E_OK;
    uint32_t                seqNum;

    ASSERT_COND(p_FmHc);

    p_HcFrame = GetBuf(p_FmHc, &seqNum);
    if (!p_HcFrame)
        RETURN_ERROR(MINOR, E_NO_MEMORY, ("HC Frame object"));
    memset(p_HcFrame, 0, sizeof(t_HcFrame));
    /* first read SP register */
    p_HcFrame->opcode = (uint32_t)(HC_HCOR_GBL | HC_HCOR_OPCODE_SYNC);
    p_HcFrame->actionReg = 0;
    p_HcFrame->extraReg = 0;
    p_HcFrame->commandSequence = seqNum;

    BUILD_FD(sizeof(t_HcFrame));

    err = EnQFrm(p_FmHc, &fmFd, seqNum);

    PutBuf(p_FmHc, p_HcFrame, seqNum);

    if (err != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return E_OK;
}

t_Handle    FmHcGetPort(t_Handle h_FmHc)
{
    t_FmHc *p_FmHc = (t_FmHc*)h_FmHc;
    return p_FmHc->h_HcPortDev;
}
