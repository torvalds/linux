/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
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
 @File          fm_macsec.c

 @Description   FM MACSEC driver routines implementation.
*//***************************************************************************/

#include "std_ext.h"
#include "error_ext.h"
#include "xx_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "fm_mac_ext.h"

#include "fm_macsec_master.h"


extern uint16_t    FM_MAC_GetMaxFrameLength(t_Handle FmMac);


/****************************************/
/*       static functions               */
/****************************************/
static t_Error CheckFmMacsecParameters(t_FmMacsec *p_FmMacsec)
{
    if (!p_FmMacsec->f_Exception)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));

    return E_OK;
}

static void UnimplementedIsr(t_Handle h_Arg, uint32_t id)
{
    UNUSED(h_Arg); UNUSED(id);

    REPORT_ERROR(MAJOR, E_NOT_SUPPORTED, ("Unimplemented Isr!"));
}

static void MacsecEventIsr(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    events,event,i;

    SANITY_CHECK_RETURN(p_FmMacsec, E_INVALID_HANDLE);

    events = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->evr);
    events |= GET_UINT32(p_FmMacsec->p_FmMacsecRegs->ever);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->evr,events);

    for (i=0; i<NUM_OF_TX_SC; i++)
        if (events & FM_MACSEC_EV_TX_SC_NEXT_PN(i))
        {
            GET_MACSEC_MODULE_EVENT(e_FM_MACSEC_MOD_SC_TX, i, e_FM_INTR_TYPE_NORMAL, event);
            p_FmMacsec->intrMng[event].f_Isr(p_FmMacsec->intrMng[event].h_SrcHandle, i);
        }
}

static void MacsecErrorIsr(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    errors,error,i;

    SANITY_CHECK_RETURN(p_FmMacsec, E_INVALID_HANDLE);

    errors = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->err);
    errors |= GET_UINT32(p_FmMacsec->p_FmMacsecRegs->erer);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->err,errors);

    for (i=0; i<NUM_OF_TX_SC; i++)
        if (errors & FM_MACSEC_EX_TX_SC(i))
        {
            GET_MACSEC_MODULE_EVENT(e_FM_MACSEC_MOD_SC_TX, i, e_FM_INTR_TYPE_ERR, error);
            p_FmMacsec->intrMng[error].f_Isr(p_FmMacsec->intrMng[error].h_SrcHandle, i);
        }

    if (errors & FM_MACSEC_EX_ECC)
    {
        uint8_t     eccType;
        uint32_t    tmpReg;

        tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->meec);
        ASSERT_COND(tmpReg & MECC_CAP);
        eccType = (uint8_t)((tmpReg & MECC_CET) >> MECC_CET_SHIFT);

        if (!eccType && (p_FmMacsec->userExceptions & FM_MACSEC_USER_EX_SINGLE_BIT_ECC))
            p_FmMacsec->f_Exception(p_FmMacsec->h_App,e_FM_MACSEC_EX_SINGLE_BIT_ECC);
        else if (eccType && (p_FmMacsec->userExceptions & FM_MACSEC_USER_EX_MULTI_BIT_ECC))
            p_FmMacsec->f_Exception(p_FmMacsec->h_App,e_FM_MACSEC_EX_MULTI_BIT_ECC);
        else
            WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->meec,tmpReg);
    }
}

static t_Error MacsecInit(t_Handle h_FmMacsec)
{
    t_FmMacsec                  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_FmMacsecDriverParam       *p_FmMacsecDriverParam = NULL;
    uint32_t                    tmpReg,i,macId;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    CHECK_INIT_PARAMETERS(p_FmMacsec, CheckFmMacsecParameters);

    p_FmMacsecDriverParam = p_FmMacsec->p_FmMacsecDriverParam;

    for (i=0;i<e_FM_MACSEC_EV_DUMMY_LAST;i++)
        p_FmMacsec->intrMng[i].f_Isr = UnimplementedIsr;

    tmpReg = 0;
    tmpReg |= (p_FmMacsecDriverParam->changedTextWithNoEncryptDeliverUncontrolled << CFG_UECT_SHIFT)|
              (p_FmMacsecDriverParam->onlyScbIsSetDeliverUncontrolled << CFG_ESCBT_SHIFT)           |
              (p_FmMacsecDriverParam->unknownSciTreatMode << CFG_USFT_SHIFT)                        |
              (p_FmMacsecDriverParam->invalidTagsDeliverUncontrolled << CFG_ITT_SHIFT)              |
              (p_FmMacsecDriverParam->encryptWithNoChangedTextDiscardUncontrolled << CFG_KFT_SHIFT) |
              (p_FmMacsecDriverParam->untagTreatMode << CFG_UFT_SHIFT)                              |
              (p_FmMacsecDriverParam->keysUnreadable << CFG_KSS_SHIFT)                              |
              (p_FmMacsecDriverParam->reservedSc0 << CFG_S0I_SHIFT)                                 |
              (p_FmMacsecDriverParam->byPassMode << CFG_BYPN_SHIFT);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg, tmpReg);

    tmpReg = FM_MAC_GetMaxFrameLength(p_FmMacsec->h_FmMac);
    /* At least Ethernet FCS (4 bytes) overhead must be subtracted from MFL.
     * In addition, the SCI (8 bytes) overhead might be subtracted as well. */
    tmpReg -= p_FmMacsecDriverParam->mflSubtract;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->mfl, tmpReg);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->tpnet, p_FmMacsecDriverParam->pnExhThr);

    if (!p_FmMacsec->userExceptions)
        p_FmMacsec->exceptions &= ~FM_MACSEC_EX_ECC;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->erer, p_FmMacsec->exceptions);

    p_FmMacsec->numRxScAvailable = NUM_OF_RX_SC;
    if (p_FmMacsecDriverParam->reservedSc0)
        p_FmMacsec->numRxScAvailable --;
    p_FmMacsec->numTxScAvailable = NUM_OF_TX_SC;

    XX_Free(p_FmMacsecDriverParam);
    p_FmMacsec->p_FmMacsecDriverParam = NULL;

    FM_MAC_GetId(p_FmMacsec->h_FmMac, &macId);
    FmRegisterIntr(p_FmMacsec->h_Fm,
                   e_FM_MOD_MACSEC,
                   (uint8_t)macId,
                   e_FM_INTR_TYPE_NORMAL,
                   MacsecEventIsr,
                   p_FmMacsec);

    FmRegisterIntr(p_FmMacsec->h_Fm,
                   e_FM_MOD_MACSEC,
                   0,
                   e_FM_INTR_TYPE_ERR,
                   MacsecErrorIsr,
                   p_FmMacsec);

    return E_OK;
}

static t_Error MacsecFree(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    macId;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    FM_MAC_GetId(p_FmMacsec->h_FmMac, &macId);
    FmUnregisterIntr(p_FmMacsec->h_Fm,
                   e_FM_MOD_MACSEC,
                   (uint8_t)macId,
                   e_FM_INTR_TYPE_NORMAL);

    FmUnregisterIntr(p_FmMacsec->h_Fm,
                   e_FM_MOD_MACSEC,
                   0,
                   e_FM_INTR_TYPE_ERR);

    if (p_FmMacsec->rxScSpinLock)
        XX_FreeSpinlock(p_FmMacsec->rxScSpinLock);
    if (p_FmMacsec->txScSpinLock)
        XX_FreeSpinlock(p_FmMacsec->txScSpinLock);

    XX_Free(p_FmMacsec);

    return E_OK;
}

static t_Error MacsecConfigUnknownSciFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUnknownSciFrameTreatment treatMode)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->unknownSciTreatMode = treatMode;

    return E_OK;
}

static t_Error MacsecConfigInvalidTagsFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->invalidTagsDeliverUncontrolled = deliverUncontrolled;

    return E_OK;
}

static t_Error MacsecConfigChangedTextWithNoEncryptFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->changedTextWithNoEncryptDeliverUncontrolled = deliverUncontrolled;

    return E_OK;
}

static t_Error MacsecConfigOnlyScbIsSetFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->onlyScbIsSetDeliverUncontrolled = deliverUncontrolled;

    return E_OK;
}

static t_Error MacsecConfigEncryptWithNoChangedTextFrameTreatment(t_Handle h_FmMacsec, bool discardUncontrolled)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->encryptWithNoChangedTextDiscardUncontrolled = discardUncontrolled;

    return E_OK;
}

static t_Error MacsecConfigUntagFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUntagFrameTreatment treatMode)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->untagTreatMode = treatMode;

    return E_OK;
}

static t_Error MacsecConfigPnExhaustionThreshold(t_Handle h_FmMacsec, uint32_t pnExhThr)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->pnExhThr = pnExhThr;

    return E_OK;
}

static t_Error MacsecConfigKeysUnreadable(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->keysUnreadable = TRUE;

    return E_OK;
}

static t_Error MacsecConfigSectagWithoutSCI(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    p_FmMacsec->p_FmMacsecDriverParam->sectagOverhead -= MACSEC_SCI_SIZE;
    p_FmMacsec->p_FmMacsecDriverParam->mflSubtract += MACSEC_SCI_SIZE;

    return E_OK;
}

static t_Error MacsecConfigException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    GET_USER_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_FmMacsec->userExceptions |= bitMask;
        else
            p_FmMacsec->userExceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

static t_Error MacsecGetRevision(t_Handle h_FmMacsec, uint32_t *p_MacsecRevision)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    *p_MacsecRevision = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->ip_rev1);

    return E_OK;
}

static t_Error MacsecEnable(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    tmpReg  = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg);
    tmpReg |= CFG_BYPN;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg,tmpReg);

    return E_OK;
}

static t_Error MacsecDisable(t_Handle h_FmMacsec)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    tmpReg;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    tmpReg  = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg);
    tmpReg &= ~CFG_BYPN;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg,tmpReg);

    return E_OK;
}

static t_Error MacsecSetException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    bitMask;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    GET_USER_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_FmMacsec->userExceptions |= bitMask;
        else
            p_FmMacsec->userExceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    if (!p_FmMacsec->userExceptions)
        p_FmMacsec->exceptions &= ~FM_MACSEC_EX_ECC;
    else
        p_FmMacsec->exceptions |= FM_MACSEC_EX_ECC;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->erer, p_FmMacsec->exceptions);

    return E_OK;
}

static void InitFmMacsecControllerDriver(t_FmMacsecControllerDriver *p_FmMacsecControllerDriver)
{
    p_FmMacsecControllerDriver->f_FM_MACSEC_Init                                            = MacsecInit;
    p_FmMacsecControllerDriver->f_FM_MACSEC_Free                                            = MacsecFree;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUnknownSciFrameTreatment                  = MacsecConfigUnknownSciFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigInvalidTagsFrameTreatment                 = MacsecConfigInvalidTagsFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment    = MacsecConfigEncryptWithNoChangedTextFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigUntagFrameTreatment                       = MacsecConfigUntagFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigChangedTextWithNoEncryptFrameTreatment    = MacsecConfigChangedTextWithNoEncryptFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigOnlyScbIsSetFrameTreatment                = MacsecConfigOnlyScbIsSetFrameTreatment;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigPnExhaustionThreshold                     = MacsecConfigPnExhaustionThreshold;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigKeysUnreadable                            = MacsecConfigKeysUnreadable;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigSectagWithoutSCI                          = MacsecConfigSectagWithoutSCI;
    p_FmMacsecControllerDriver->f_FM_MACSEC_ConfigException                                 = MacsecConfigException;
    p_FmMacsecControllerDriver->f_FM_MACSEC_GetRevision                                     = MacsecGetRevision;
    p_FmMacsecControllerDriver->f_FM_MACSEC_Enable                                          = MacsecEnable;
    p_FmMacsecControllerDriver->f_FM_MACSEC_Disable                                         = MacsecDisable;
    p_FmMacsecControllerDriver->f_FM_MACSEC_SetException                                    = MacsecSetException;
}

/****************************************/
/*       Inter-Module functions         */
/****************************************/

void FmMacsecRegisterIntr(t_Handle                h_FmMacsec,
                          e_FmMacsecEventModules  module,
                          uint8_t                 modId,
                          e_FmIntrType            intrType,
                          void (*f_Isr) (t_Handle h_Arg, uint32_t id),
                          t_Handle                h_Arg)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint8_t     event= 0;

    SANITY_CHECK_RETURN(p_FmMacsec, E_INVALID_HANDLE);

    GET_MACSEC_MODULE_EVENT(module, modId, intrType, event);

    ASSERT_COND(event != e_FM_MACSEC_EV_DUMMY_LAST);
    p_FmMacsec->intrMng[event].f_Isr = f_Isr;
    p_FmMacsec->intrMng[event].h_SrcHandle = h_Arg;
}

void FmMacsecUnregisterIntr(t_Handle                h_FmMacsec,
                            e_FmMacsecEventModules  module,
                            uint8_t                 modId,
                            e_FmIntrType            intrType)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint8_t     event= 0;

    SANITY_CHECK_RETURN(p_FmMacsec, E_INVALID_HANDLE);

    GET_MACSEC_MODULE_EVENT(module, modId,intrType, event);

    ASSERT_COND(event != e_FM_MACSEC_EV_DUMMY_LAST);
    p_FmMacsec->intrMng[event].f_Isr = NULL;
    p_FmMacsec->intrMng[event].h_SrcHandle = NULL;
}

t_Error FmMacsecAllocScs(t_Handle h_FmMacsec, e_ScType type, bool isPtp, uint32_t numOfScs, uint32_t *p_ScIds)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    bool        *p_ScTable;
    uint32_t    *p_ScAvailable,i;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ScIds, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(numOfScs, E_INVALID_HANDLE);

    if (type == e_SC_RX)
    {
        p_ScTable       = (bool *)p_FmMacsec->rxScTable;
        p_ScAvailable   = &p_FmMacsec->numRxScAvailable;
        i               = (NUM_OF_RX_SC - 1);
    }
    else
    {
        p_ScTable       = (bool *)p_FmMacsec->txScTable;
        p_ScAvailable   = &p_FmMacsec->numTxScAvailable;
        i               = (NUM_OF_TX_SC - 1);

    }
    if (*p_ScAvailable < numOfScs)
        RETURN_ERROR(MINOR, E_NOT_AVAILABLE, ("Not enough SCs available"));

    if (isPtp)
    {
        i = 0;
        if (p_ScTable[i])
            RETURN_ERROR(MINOR, E_NOT_AVAILABLE, ("Sc 0 Not available"));
    }

    for (;numOfScs;i--)
    {
        if (p_ScTable[i])
            continue;
        numOfScs --;
        (*p_ScAvailable)--;
        p_ScIds[numOfScs] = i;
        p_ScTable[i] = TRUE;
    }

    return err;
}

t_Error FmMacsecFreeScs(t_Handle h_FmMacsec, e_ScType type, uint32_t numOfScs, uint32_t *p_ScIds)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    bool        *p_ScTable;
    uint32_t    *p_ScAvailable,maxNumOfSc,i;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_ScIds, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(numOfScs, E_INVALID_HANDLE);

    if (type == e_SC_RX)
    {
        p_ScTable       = (bool *)p_FmMacsec->rxScTable;
        p_ScAvailable   = &p_FmMacsec->numRxScAvailable;
        maxNumOfSc      = NUM_OF_RX_SC;
    }
    else
    {
        p_ScTable       = (bool *)p_FmMacsec->txScTable;
        p_ScAvailable   = &p_FmMacsec->numTxScAvailable;
        maxNumOfSc      = NUM_OF_TX_SC;
    }

    if ((*p_ScAvailable + numOfScs) > maxNumOfSc)
        RETURN_ERROR(MINOR, E_FULL, ("Too much SCs"));

    for (i=0;i<numOfScs;i++)
    {
        p_ScTable[p_ScIds[i]] = FALSE;
        (*p_ScAvailable)++;
    }

    return err;

}

t_Error FmMacsecSetPTP(t_Handle h_FmMacsec, bool enable)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    tmpReg = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);

    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg);
    if (enable && (tmpReg & CFG_S0I))
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("MACSEC already in point-to-point mode"));

    if (enable)
        tmpReg |= CFG_S0I;
    else
        tmpReg &= ~CFG_S0I;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->cfg, tmpReg);

    return E_OK;
}

t_Error FmMacsecCreateRxSc(t_Handle h_FmMacsec, t_RxScParams *p_RxScParams)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_RxScParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_RxScParams->scId < NUM_OF_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, p_RxScParams->scId);
    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsccfg);
    if (tmpReg & RX_SCCFG_SCI_EN_MASK)
    {
        XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("Rx Sc %d must be disable",p_RxScParams->scId));
    }

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsci1h, GET_SCI_FIRST_HALF(p_RxScParams->sci));
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsci2h, GET_SCI_SECOND_HALF(p_RxScParams->sci));
    tmpReg |= ((p_RxScParams->replayProtect << RX_SCCFG_RP_SHIFT) & RX_SCCFG_RP_MASK);
    tmpReg |= ((p_RxScParams->validateFrames << RX_SCCFG_VF_SHIFT) & RX_SCCFG_VF_MASK);
    tmpReg |= ((p_RxScParams->confidentialityOffset << RX_SCCFG_CO_SHIFT) & RX_SCCFG_CO_MASK);
    tmpReg |= RX_SCCFG_SCI_EN_MASK;
    tmpReg |= (p_RxScParams->cipherSuite << RX_SCCFG_CS_SHIFT);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsccfg, tmpReg);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rpw, p_RxScParams->replayWindow);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecDeleteRxSc(t_Handle h_FmMacsec, uint32_t scId)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    tmpReg &= ~RX_SCCFG_SCI_EN_MASK;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsccfg, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecCreateTxSc(t_Handle h_FmMacsec, t_TxScParams *p_TxScParams)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;
    bool        alwaysIncludeSCI = FALSE, useES = FALSE, useSCB = FALSE;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_TxScParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_TxScParams->scId < NUM_OF_TX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, p_TxScParams->scId);

    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg);
    if (tmpReg & TX_SCCFG_SCE_MASK)
    {
        XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("Tx Sc %d must be disable",p_TxScParams->scId));
    }

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsci1h, GET_SCI_FIRST_HALF(p_TxScParams->sci));
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsci2h, GET_SCI_SECOND_HALF(p_TxScParams->sci));
    alwaysIncludeSCI = (p_TxScParams->sciInsertionMode == e_FM_MACSEC_SCI_INSERTION_MODE_EXPLICIT_SECTAG);
    useES            = (p_TxScParams->sciInsertionMode == e_FM_MACSEC_SCI_INSERTION_MODE_EXPLICIT_MAC_SA);

    tmpReg |= ((p_TxScParams->protectFrames << TX_SCCFG_PF_SHIFT) & TX_SCCFG_PF_MASK);
    tmpReg |= ((alwaysIncludeSCI << TX_SCCFG_AIS_SHIFT) & TX_SCCFG_AIS_MASK);
    tmpReg |= ((useES << TX_SCCFG_UES_SHIFT) & TX_SCCFG_UES_MASK);
    tmpReg |= ((useSCB << TX_SCCFG_USCB_SHIFT) & TX_SCCFG_USCB_MASK);
    tmpReg |= ((p_TxScParams->confidentialityEnable << TX_SCCFG_CE_SHIFT) & TX_SCCFG_CE_MASK);
    tmpReg |= ((p_TxScParams->confidentialityOffset << TX_SCCFG_CO_SHIFT) & TX_SCCFG_CO_MASK);
    tmpReg |= TX_SCCFG_SCE_MASK;
    tmpReg |= (p_TxScParams->cipherSuite << TX_SCCFG_CS_SHIFT);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecDeleteTxSc(t_Handle h_FmMacsec, uint32_t scId)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_TX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    tmpReg &= ~TX_SCCFG_SCE_MASK;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecCreateRxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecAN_t an, uint32_t lowestPn, macsecSAKey_t key)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsanpn, DEFAULT_initNextPn);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsalpn, lowestPn);
    MemCpy8((void*)p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsak, key, sizeof(macsecSAKey_t));

    tmpReg |= RX_SACFG_ACTIVE;
    tmpReg |= ((an << RX_SACFG_AN_SHIFT) & RX_SACFG_AN_MASK);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsacs, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecCreateTxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecSAKey_t key)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_TX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsanpn, DEFAULT_initNextPn);
    MemCpy8((void*)p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsak, key, sizeof(macsecSAKey_t));

    tmpReg |= TX_SACFG_ACTIVE;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsacs, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecDeleteRxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, i, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsanpn, 0x0);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsalpn, 0x0);
    for (i=0; i<4; i++)
        WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsak[i], 0x0);

    tmpReg |= RX_SACFG_ACTIVE;
    tmpReg &= ~RX_SACFG_EN_MASK;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsacs, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecDeleteTxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, i, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_TX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsanpn, 0x0);
    for (i=0; i<4; i++)
        WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsak[i], 0x0);

    tmpReg |= TX_SACFG_ACTIVE;
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecTxScSa[saId].txsacs, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecRxSaSetReceive(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, bool enableReceive)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsacs);
    if (enableReceive)
        tmpReg |= RX_SACFG_EN_MASK;
    else
        tmpReg &= ~RX_SACFG_EN_MASK;

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsacs, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecRxSaUpdateNextPn(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, uint32_t updtNextPN)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsanpn, updtNextPN);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecRxSaUpdateLowestPn(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, uint32_t updtLowestPN)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_RX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->rxScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->rxsca, scId);
    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->fmMacsecRxScSa[saId].rxsalpn, updtLowestPN);

    XX_UnlockIntrSpinlock(p_FmMacsec->rxScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecTxSaSetActive(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecAN_t an)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(saId < NUM_OF_SA_PER_TX_SC, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, scId);

    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg);

    tmpReg |= ((an << TX_SCCFG_AN_SHIFT) & TX_SCCFG_AN_MASK);
    tmpReg |= ((saId << TX_SCCFG_ASA_SHIFT) & TX_SCCFG_ASA_MASK);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg, tmpReg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    return err;
}

t_Error FmMacsecTxSaGetActive(t_Handle h_FmMacsec, uint32_t scId, macsecAN_t *p_An)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    t_Error     err = E_OK;
    uint32_t    tmpReg = 0, intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(scId < NUM_OF_RX_SC, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_An, E_INVALID_HANDLE);

    intFlags = XX_LockIntrSpinlock(p_FmMacsec->txScSpinLock);

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->txsca, scId);

    tmpReg = GET_UINT32(p_FmMacsec->p_FmMacsecRegs->txsccfg);

    XX_UnlockIntrSpinlock(p_FmMacsec->txScSpinLock, intFlags);

    *p_An = (macsecAN_t)((tmpReg & TX_SCCFG_AN_MASK) >> TX_SCCFG_AN_SHIFT);

    return err;
}

t_Error FmMacsecSetException(t_Handle h_FmMacsec, e_FmMacsecGlobalExceptions exception, uint32_t scId, bool enable)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    bitMask;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    GET_EXCEPTION_FLAG(bitMask, exception, scId);
    if (bitMask)
    {
        if (enable)
            p_FmMacsec->exceptions |= bitMask;
        else
            p_FmMacsec->exceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->erer, p_FmMacsec->exceptions);

    return E_OK;
}

t_Error FmMacsecSetEvent(t_Handle h_FmMacsec, e_FmMacsecGlobalEvents event, uint32_t scId, bool enable)
{
    t_FmMacsec  *p_FmMacsec = (t_FmMacsec*)h_FmMacsec;
    uint32_t    bitMask;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsec->p_FmMacsecDriverParam, E_INVALID_HANDLE);

    GET_EVENT_FLAG(bitMask, event, scId);
    if (bitMask)
    {
        if (enable)
            p_FmMacsec->events |= bitMask;
        else
            p_FmMacsec->events &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined event"));

    WRITE_UINT32(p_FmMacsec->p_FmMacsecRegs->ever, p_FmMacsec->events);

    return E_OK;
}

/****************************************/
/*       API Init unit functions        */
/****************************************/
t_Handle FM_MACSEC_MASTER_Config(t_FmMacsecParams *p_FmMacsecParam)
{
    t_FmMacsec  *p_FmMacsec;
    uint32_t    macId;

    /* Allocate FM MACSEC structure */
    p_FmMacsec = (t_FmMacsec *) XX_Malloc(sizeof(t_FmMacsec));
    if (!p_FmMacsec)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC driver structure"));
        return NULL;
    }
    memset(p_FmMacsec, 0, sizeof(t_FmMacsec));
    InitFmMacsecControllerDriver(&p_FmMacsec->fmMacsecControllerDriver);

    /* Allocate the FM MACSEC driver's parameters structure */
    p_FmMacsec->p_FmMacsecDriverParam = (t_FmMacsecDriverParam *)XX_Malloc(sizeof(t_FmMacsecDriverParam));
    if (!p_FmMacsec->p_FmMacsecDriverParam)
    {
        XX_Free(p_FmMacsec);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC driver parameters"));
        return NULL;
    }
    memset(p_FmMacsec->p_FmMacsecDriverParam, 0, sizeof(t_FmMacsecDriverParam));

    /* Initialize FM MACSEC parameters which will be kept by the driver */
    p_FmMacsec->h_Fm            = p_FmMacsecParam->h_Fm;
    p_FmMacsec->h_FmMac         = p_FmMacsecParam->nonGuestParams.h_FmMac;
    p_FmMacsec->p_FmMacsecRegs  = (t_FmMacsecRegs *)UINT_TO_PTR(p_FmMacsecParam->nonGuestParams.baseAddr);
    p_FmMacsec->f_Exception     = p_FmMacsecParam->nonGuestParams.f_Exception;
    p_FmMacsec->h_App           = p_FmMacsecParam->nonGuestParams.h_App;
    p_FmMacsec->userExceptions  = DEFAULT_userExceptions;
    p_FmMacsec->exceptions      = DEFAULT_exceptions;
    p_FmMacsec->events          = DEFAULT_events;
    p_FmMacsec->rxScSpinLock    = XX_InitSpinlock();
    p_FmMacsec->txScSpinLock    = XX_InitSpinlock();

    /* Initialize FM MACSEC driver parameters parameters (for initialization phase only) */
    p_FmMacsec->p_FmMacsecDriverParam->unknownSciTreatMode                           = DEFAULT_unknownSciFrameTreatment;
    p_FmMacsec->p_FmMacsecDriverParam->invalidTagsDeliverUncontrolled                = DEFAULT_invalidTagsFrameTreatment;
    p_FmMacsec->p_FmMacsecDriverParam->encryptWithNoChangedTextDiscardUncontrolled   = DEFAULT_encryptWithNoChangedTextFrameTreatment;
    p_FmMacsec->p_FmMacsecDriverParam->untagTreatMode                                = DEFAULT_untagFrameTreatment;
    p_FmMacsec->p_FmMacsecDriverParam->keysUnreadable                                = DEFAULT_keysUnreadable;
    p_FmMacsec->p_FmMacsecDriverParam->reservedSc0                                   = DEFAULT_sc0ReservedForPTP;
    p_FmMacsec->p_FmMacsecDriverParam->byPassMode                                    = !DEFAULT_normalMode;
    p_FmMacsec->p_FmMacsecDriverParam->pnExhThr                                      = DEFAULT_pnExhThr;
    p_FmMacsec->p_FmMacsecDriverParam->sectagOverhead                                = DEFAULT_sectagOverhead;
    p_FmMacsec->p_FmMacsecDriverParam->mflSubtract                                   = DEFAULT_mflSubtract;
    /* build the FM MACSEC master IPC address */
    memset(p_FmMacsec->fmMacsecModuleName, 0, (sizeof(char))*MODULE_NAME_SIZE);
    FM_MAC_GetId(p_FmMacsec->h_FmMac,&macId);
    if (Sprint (p_FmMacsec->fmMacsecModuleName, "FM-%d-MAC-%d-MACSEC-Master",
        FmGetId(p_FmMacsec->h_Fm),macId) != 24)
    {
        XX_Free(p_FmMacsec->p_FmMacsecDriverParam);
        XX_Free(p_FmMacsec);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        return NULL;
    }
    return p_FmMacsec;
}
