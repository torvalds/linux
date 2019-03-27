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
 @File          fm_macsec.h

 @Description   FM MACSEC internal structures and definitions.
*//***************************************************************************/
#ifndef __FM_MACSEC_H
#define __FM_MACSEC_H

#include "error_ext.h"
#include "std_ext.h"
#include "fm_macsec_ext.h"

#include "fm_common.h"


#define __ERR_MODULE__  MODULE_FM_MACSEC


typedef struct
{
    t_Error (*f_FM_MACSEC_Init) (t_Handle h_FmMacsec);
    t_Error (*f_FM_MACSEC_Free) (t_Handle h_FmMacsec);

    t_Error (*f_FM_MACSEC_ConfigUnknownSciFrameTreatment) (t_Handle h_FmMacsec, e_FmMacsecUnknownSciFrameTreatment treatMode);
    t_Error (*f_FM_MACSEC_ConfigInvalidTagsFrameTreatment) (t_Handle h_FmMacsec, bool deliverUncontrolled);
    t_Error (*f_FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment) (t_Handle h_FmMacsec, bool discardUncontrolled);
    t_Error (*f_FM_MACSEC_ConfigChangedTextWithNoEncryptFrameTreatment) (t_Handle h_FmMacsec, bool deliverUncontrolled);
    t_Error (*f_FM_MACSEC_ConfigUntagFrameTreatment) (t_Handle h_FmMacsec, e_FmMacsecUntagFrameTreatment treatMode);
    t_Error (*f_FM_MACSEC_ConfigOnlyScbIsSetFrameTreatment) (t_Handle h_FmMacsec, bool deliverUncontrolled);
    t_Error (*f_FM_MACSEC_ConfigPnExhaustionThreshold) (t_Handle h_FmMacsec, uint32_t pnExhThr);
    t_Error (*f_FM_MACSEC_ConfigKeysUnreadable) (t_Handle h_FmMacsec);
    t_Error (*f_FM_MACSEC_ConfigSectagWithoutSCI) (t_Handle h_FmMacsec);
    t_Error (*f_FM_MACSEC_ConfigException) (t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable);

    t_Error (*f_FM_MACSEC_GetRevision) (t_Handle h_FmMacsec, uint32_t *p_MacsecRevision);
    t_Error (*f_FM_MACSEC_Enable) (t_Handle h_FmMacsec);
    t_Error (*f_FM_MACSEC_Disable) (t_Handle h_FmMacsec);
    t_Error (*f_FM_MACSEC_SetException) (t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable);

} t_FmMacsecControllerDriver;

t_Handle  FM_MACSEC_GUEST_Config(t_FmMacsecParams *p_FmMacsecParam);
t_Handle  FM_MACSEC_MASTER_Config(t_FmMacsecParams *p_FmMacsecParams);

/***********************************************************************/
/*  MACSEC internal routines                                              */
/***********************************************************************/

/**************************************************************************//**

 @Group         FM_MACSEC_InterModule_grp FM MACSEC Inter-Module Unit

 @Description   FM MACSEC Inter Module functions -
                These are not User API routines but routines that may be called
                from other modules. This will be the case in a single core environment,
                where instead of using the XX messaging mechanism, the routines may be
                called from other modules. In a multicore environment, the other modules may
                be run by other cores and therefore these routines may not be called directly.

 @{
*//***************************************************************************/

#define MAX_NUM_OF_SA_PER_SC        4

typedef enum
{
    e_SC_RX = 0,
    e_SC_TX
} e_ScType;

typedef enum
{
    e_SC_SA_A = 0,
    e_SC_SA_B ,
    e_SC_SA_C ,
    e_SC_SA_D
} e_ScSaId;

typedef struct
{
    uint32_t                        scId;
    macsecSCI_t                     sci;
    bool                            replayProtect;
    uint32_t                        replayWindow;
    e_FmMacsecValidFrameBehavior    validateFrames;
    uint16_t                        confidentialityOffset;
    e_FmMacsecSecYCipherSuite       cipherSuite;
} t_RxScParams;

typedef struct
{
    uint32_t                        scId;
    macsecSCI_t                     sci;
    bool                            protectFrames;
    e_FmMacsecSciInsertionMode      sciInsertionMode;
    bool                            confidentialityEnable;
    uint16_t                        confidentialityOffset;
    e_FmMacsecSecYCipherSuite       cipherSuite;
} t_TxScParams;

typedef enum e_FmMacsecGlobalExceptions {
    e_FM_MACSEC_EX_TX_SC,               /**< Tx Sc 0 frame discarded error. */
    e_FM_MACSEC_EX_ECC                  /**< MACSEC memory ECC multiple-bit error. */
} e_FmMacsecGlobalExceptions;

typedef enum e_FmMacsecGlobalEvents {
    e_FM_MACSEC_EV_TX_SC_NEXT_PN        /**< Tx Sc 0 Next Pn exhaustion threshold reached. */
} e_FmMacsecGlobalEvents;

/**************************************************************************//**
 @Description   Enum for inter-module interrupts registration
*//***************************************************************************/
typedef enum e_FmMacsecEventModules{
    e_FM_MACSEC_MOD_SC_TX,
    e_FM_MACSEC_MOD_DUMMY_LAST
} e_FmMacsecEventModules;

typedef enum e_FmMacsecInterModuleEvent {
    e_FM_MACSEC_EV_SC_TX,
    e_FM_MACSEC_EV_ERR_SC_TX,
    e_FM_MACSEC_EV_DUMMY_LAST
} e_FmMacsecInterModuleEvent;

#define NUM_OF_INTER_MODULE_EVENTS (NUM_OF_TX_SC * 2)

#define GET_MACSEC_MODULE_EVENT(mod, id, intrType, event) \
    switch(mod){                                          \
        case e_FM_MACSEC_MOD_SC_TX:                       \
             event = (intrType == e_FM_INTR_TYPE_ERR) ?   \
                        e_FM_MACSEC_EV_ERR_SC_TX:         \
                        e_FM_MACSEC_EV_SC_TX;             \
             event += (uint8_t)(2 * id);break;            \
            break;                                        \
        default:event = e_FM_MACSEC_EV_DUMMY_LAST;        \
        break;}

void FmMacsecRegisterIntr(t_Handle                h_FmMacsec,
                          e_FmMacsecEventModules  module,
                          uint8_t                 modId,
                          e_FmIntrType            intrType,
                          void (*f_Isr) (t_Handle h_Arg, uint32_t id),
                          t_Handle                h_Arg);

void FmMacsecUnregisterIntr(t_Handle                h_FmMacsec,
                            e_FmMacsecEventModules  module,
                            uint8_t                 modId,
                            e_FmIntrType            intrType);

t_Error FmMacsecAllocScs(t_Handle h_FmMacsec, e_ScType type, bool isPtp, uint32_t numOfScs, uint32_t *p_ScIds);
t_Error FmMacsecFreeScs(t_Handle h_FmMacsec, e_ScType type, uint32_t numOfScs, uint32_t *p_ScIds);
t_Error FmMacsecCreateRxSc(t_Handle h_FmMacsec, t_RxScParams *p_RxScParams);
t_Error FmMacsecDeleteRxSc(t_Handle h_FmMacsec, uint32_t scId);
t_Error FmMacsecCreateTxSc(t_Handle h_FmMacsec, t_TxScParams *p_RxScParams);
t_Error FmMacsecDeleteTxSc(t_Handle h_FmMacsec, uint32_t scId);
t_Error FmMacsecCreateRxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecAN_t an, uint32_t lowestPn, macsecSAKey_t key);
t_Error FmMacsecCreateTxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecSAKey_t key);
t_Error FmMacsecDeleteRxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId);
t_Error FmMacsecDeleteTxSa(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId);
t_Error FmMacsecRxSaSetReceive(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, bool enableReceive);
t_Error FmMacsecRxSaUpdateNextPn(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, uint32_t updtNextPN);
t_Error FmMacsecRxSaUpdateLowestPn(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, uint32_t updtLowestPN);
t_Error FmMacsecTxSaSetActive(t_Handle h_FmMacsec, uint32_t scId, e_ScSaId saId, macsecAN_t an);
t_Error FmMacsecTxSaGetActive(t_Handle h_FmMacsec, uint32_t scId, macsecAN_t *p_An);
t_Error FmMacsecSetPTP(t_Handle h_FmMacsec, bool enable);

t_Error FmMacsecSetException(t_Handle h_FmMacsec, e_FmMacsecGlobalExceptions exception, uint32_t scId, bool enable);
t_Error FmMacsecSetEvent(t_Handle h_FmMacsec, e_FmMacsecGlobalEvents event, uint32_t scId, bool enable);



#endif /* __FM_MACSEC_H */
