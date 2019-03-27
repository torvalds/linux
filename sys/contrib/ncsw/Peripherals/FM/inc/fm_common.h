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
 @File          fm_common.h

 @Description   FM internal structures and definitions.
*//***************************************************************************/
#ifndef __FM_COMMON_H
#define __FM_COMMON_H

#include "error_ext.h"
#include "std_ext.h"
#include "fm_pcd_ext.h"
#include "fm_ext.h"
#include "fm_port_ext.h"


#define e_FM_PORT_TYPE_OH_HOST_COMMAND      e_FM_PORT_TYPE_DUMMY

#define CLS_PLAN_NUM_PER_GRP                        8

#define IP_OFFLOAD_PACKAGE_NUMBER                   106
#define CAPWAP_OFFLOAD_PACKAGE_NUMBER               108
#define IS_OFFLOAD_PACKAGE(num) ((num == IP_OFFLOAD_PACKAGE_NUMBER) || (num == CAPWAP_OFFLOAD_PACKAGE_NUMBER))



/**************************************************************************//**
 @Description       Modules registers offsets
*//***************************************************************************/
#define FM_MM_MURAM             0x00000000
#define FM_MM_BMI               0x00080000
#define FM_MM_QMI               0x00080400
#define FM_MM_PRS               0x000c7000
#define FM_MM_KG                0x000C1000
#define FM_MM_DMA               0x000C2000
#define FM_MM_FPM               0x000C3000
#define FM_MM_PLCR              0x000C0000
#define FM_MM_IMEM              0x000C4000
#define FM_MM_CGP               0x000DB000
#define FM_MM_TRB(i)            (0x000D0200 + 0x400 * (i))
#if (DPAA_VERSION >= 11)
#define FM_MM_SP                0x000dc000
#endif /* (DPAA_VERSION >= 11) */


/**************************************************************************//**
 @Description   Enum for inter-module interrupts registration
*//***************************************************************************/
typedef enum e_FmEventModules{
    e_FM_MOD_PRS,                   /**< Parser event */
    e_FM_MOD_KG,                    /**< Keygen event */
    e_FM_MOD_PLCR,                  /**< Policer event */
    e_FM_MOD_10G_MAC,               /**< 10G MAC event */
    e_FM_MOD_1G_MAC,                /**< 1G MAC event */
    e_FM_MOD_TMR,                   /**< Timer event */
    e_FM_MOD_FMAN_CTRL,             /**< FMAN Controller  Timer event */
    e_FM_MOD_MACSEC,
    e_FM_MOD_DUMMY_LAST
} e_FmEventModules;

/**************************************************************************//**
 @Description   Enum for interrupts types
*//***************************************************************************/
typedef enum e_FmIntrType {
    e_FM_INTR_TYPE_ERR,
    e_FM_INTR_TYPE_NORMAL
} e_FmIntrType;

/**************************************************************************//**
 @Description   Enum for inter-module interrupts registration
*//***************************************************************************/
typedef enum e_FmInterModuleEvent
{
    e_FM_EV_PRS = 0,                /**< Parser event */
    e_FM_EV_ERR_PRS,                /**< Parser error event */
    e_FM_EV_KG,                     /**< Keygen event */
    e_FM_EV_ERR_KG,                 /**< Keygen error event */
    e_FM_EV_PLCR,                   /**< Policer event */
    e_FM_EV_ERR_PLCR,               /**< Policer error event */
    e_FM_EV_ERR_10G_MAC0,           /**< 10G MAC 0 error event */
    e_FM_EV_ERR_10G_MAC1,           /**< 10G MAC 1 error event */
    e_FM_EV_ERR_1G_MAC0,            /**< 1G MAC 0 error event */
    e_FM_EV_ERR_1G_MAC1,            /**< 1G MAC 1 error event */
    e_FM_EV_ERR_1G_MAC2,            /**< 1G MAC 2 error event */
    e_FM_EV_ERR_1G_MAC3,            /**< 1G MAC 3 error event */
    e_FM_EV_ERR_1G_MAC4,            /**< 1G MAC 4 error event */
    e_FM_EV_ERR_1G_MAC5,            /**< 1G MAC 5 error event */
    e_FM_EV_ERR_1G_MAC6,            /**< 1G MAC 6 error event */
    e_FM_EV_ERR_1G_MAC7,            /**< 1G MAC 7 error event */
    e_FM_EV_ERR_MACSEC_MAC0,
    e_FM_EV_TMR,                    /**< Timer event */
    e_FM_EV_10G_MAC0,               /**< 10G MAC 0 event (Magic packet detection)*/
    e_FM_EV_10G_MAC1,               /**< 10G MAC 1 event (Magic packet detection)*/
    e_FM_EV_1G_MAC0,                /**< 1G MAC 0 event (Magic packet detection)*/
    e_FM_EV_1G_MAC1,                /**< 1G MAC 1 event (Magic packet detection)*/
    e_FM_EV_1G_MAC2,                /**< 1G MAC 2 (Magic packet detection)*/
    e_FM_EV_1G_MAC3,                /**< 1G MAC 3 (Magic packet detection)*/
    e_FM_EV_1G_MAC4,                /**< 1G MAC 4 (Magic packet detection)*/
    e_FM_EV_1G_MAC5,                /**< 1G MAC 5 (Magic packet detection)*/
    e_FM_EV_1G_MAC6,                /**< 1G MAC 6 (Magic packet detection)*/
    e_FM_EV_1G_MAC7,                /**< 1G MAC 7 (Magic packet detection)*/
    e_FM_EV_MACSEC_MAC0,            /**< MACSEC MAC 0 event */
    e_FM_EV_FMAN_CTRL_0,            /**< Fman controller event 0 */
    e_FM_EV_FMAN_CTRL_1,            /**< Fman controller event 1 */
    e_FM_EV_FMAN_CTRL_2,            /**< Fman controller event 2 */
    e_FM_EV_FMAN_CTRL_3,            /**< Fman controller event 3 */
    e_FM_EV_DUMMY_LAST
} e_FmInterModuleEvent;


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Description   PCD KG scheme registers
*//***************************************************************************/
typedef _Packed struct t_FmPcdPlcrProfileRegs {
    volatile uint32_t fmpl_pemode;      /* 0x090 FMPL_PEMODE - FM Policer Profile Entry Mode*/
    volatile uint32_t fmpl_pegnia;      /* 0x094 FMPL_PEGNIA - FM Policer Profile Entry GREEN Next Invoked Action*/
    volatile uint32_t fmpl_peynia;      /* 0x098 FMPL_PEYNIA - FM Policer Profile Entry YELLOW Next Invoked Action*/
    volatile uint32_t fmpl_pernia;      /* 0x09C FMPL_PERNIA - FM Policer Profile Entry RED Next Invoked Action*/
    volatile uint32_t fmpl_pecir;       /* 0x0A0 FMPL_PECIR  - FM Policer Profile Entry Committed Information Rate*/
    volatile uint32_t fmpl_pecbs;       /* 0x0A4 FMPL_PECBS  - FM Policer Profile Entry Committed Burst Size*/
    volatile uint32_t fmpl_pepepir_eir; /* 0x0A8 FMPL_PEPIR_EIR - FM Policer Profile Entry Peak/Excess Information Rate*/
    volatile uint32_t fmpl_pepbs_ebs;   /* 0x0AC FMPL_PEPBS_EBS - FM Policer Profile Entry Peak/Excess Information Rate*/
    volatile uint32_t fmpl_pelts;       /* 0x0B0 FMPL_PELTS  - FM Policer Profile Entry Last TimeStamp*/
    volatile uint32_t fmpl_pects;       /* 0x0B4 FMPL_PECTS  - FM Policer Profile Entry Committed Token Status*/
    volatile uint32_t fmpl_pepts_ets;   /* 0x0B8 FMPL_PEPTS_ETS - FM Policer Profile Entry Peak/Excess Token Status*/
    volatile uint32_t fmpl_pegpc;       /* 0x0BC FMPL_PEGPC  - FM Policer Profile Entry GREEN Packet Counter*/
    volatile uint32_t fmpl_peypc;       /* 0x0C0 FMPL_PEYPC  - FM Policer Profile Entry YELLOW Packet Counter*/
    volatile uint32_t fmpl_perpc;       /* 0x0C4 FMPL_PERPC  - FM Policer Profile Entry RED Packet Counter */
    volatile uint32_t fmpl_perypc;      /* 0x0C8 FMPL_PERYPC - FM Policer Profile Entry Recolored YELLOW Packet Counter*/
    volatile uint32_t fmpl_perrpc;      /* 0x0CC FMPL_PERRPC - FM Policer Profile Entry Recolored RED Packet Counter*/
    volatile uint32_t fmpl_res1[12];    /* 0x0D0-0x0FF Reserved */
} _PackedType t_FmPcdPlcrProfileRegs;


typedef _Packed struct t_FmPcdCcCapwapReassmTimeoutParams {
    volatile uint32_t                       portIdAndCapwapReassmTbl;
    volatile uint32_t                       fqidForTimeOutFrames;
    volatile uint32_t                       timeoutRequestTime;
}_PackedType t_FmPcdCcCapwapReassmTimeoutParams;

/**************************************************************************//**
 @Description   PCD CTRL Parameters Page
*//***************************************************************************/
typedef _Packed struct t_FmPcdCtrlParamsPage {
    volatile uint8_t  reserved0[16];
    volatile uint32_t iprIpv4Nia;
    volatile uint32_t iprIpv6Nia;
    volatile uint8_t  reserved1[24];
    volatile uint32_t ipfOptionsCounter;
    volatile uint8_t  reserved2[12];
    volatile uint32_t misc;
    volatile uint32_t errorsDiscardMask;
    volatile uint32_t discardMask;
    volatile uint8_t  reserved3[4];
    volatile uint32_t postBmiFetchNia;
    volatile uint8_t  reserved4[172];
} _PackedType t_FmPcdCtrlParamsPage;



#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/*for UNDER_CONSTRUCTION_FM_RMU_USE_SEC its defined in fm_ext.h*/
typedef uint32_t t_FmFmanCtrl;

#define FPM_PORT_FM_CTL1                0x00000001
#define FPM_PORT_FM_CTL2                0x00000002



typedef struct t_FmPcdCcFragScratchPoolCmdParams {
    uint32_t    numOfBuffers;
    uint8_t     bufferPoolId;
} t_FmPcdCcFragScratchPoolCmdParams;

typedef struct t_FmPcdCcReassmTimeoutParams {
    bool        activate;
    uint8_t     tsbs;
    uint32_t    iprcpt;
} t_FmPcdCcReassmTimeoutParams;

typedef struct {
    uint8_t             baseEntry;
    uint16_t            numOfClsPlanEntries;
    uint32_t            vectors[FM_PCD_MAX_NUM_OF_CLS_PLANS];
} t_FmPcdKgInterModuleClsPlanSet;

/**************************************************************************//**
 @Description   Structure for binding a port to keygen schemes.
*//***************************************************************************/
typedef struct t_FmPcdKgInterModuleBindPortToSchemes {
    uint8_t     hardwarePortId;
    uint8_t     netEnvId;
    bool        useClsPlan;                 /**< TRUE if this port uses the clsPlan mechanism */
    uint8_t     numOfSchemes;
    uint8_t     schemesIds[FM_PCD_KG_NUM_OF_SCHEMES];
} t_FmPcdKgInterModuleBindPortToSchemes;

typedef struct {
    uint32_t nextCcNodeInfo;
    t_List   node;
} t_CcNodeInfo;

typedef struct
{
    t_Handle    h_CcNode;
    uint16_t    index;
    t_List      node;
}t_CcNodeInformation;
#define CC_NODE_F_OBJECT(ptr)  NCSW_LIST_OBJECT(ptr, t_CcNodeInformation, node)

typedef enum e_ModifyState
{
    e_MODIFY_STATE_ADD = 0,
    e_MODIFY_STATE_REMOVE,
    e_MODIFY_STATE_CHANGE
} e_ModifyState;

typedef struct
{
    t_Handle h_Manip;
    t_List   node;
}t_ManipInfo;
#define CC_NEXT_NODE_F_OBJECT(ptr)  NCSW_LIST_OBJECT(ptr, t_CcNodeInfo, node)

typedef struct {
    uint32_t            type;
    uint8_t             prOffset;
    uint16_t            dataOffset;
    uint8_t             internalBufferOffset;
    uint8_t             numOfTasks;
    uint8_t             numOfExtraTasks;
    uint8_t             hardwarePortId;
    t_FmRevisionInfo    revInfo;
    uint32_t            nia;
    uint32_t            discardMask;
} t_GetCcParams;

typedef struct {
    uint32_t        type;
    int             psoSize;
    uint32_t        nia;
    t_FmFmanCtrl    orFmanCtrl;
    bool            overwrite;
    uint8_t         ofpDpde;
} t_SetCcParams;

typedef struct {
    t_GetCcParams getCcParams;
    t_SetCcParams setCcParams;
} t_FmPortGetSetCcParams;

typedef struct {
    uint32_t    type;
    bool        sleep;
} t_FmSetParams;

typedef struct {
    uint32_t    type;
    uint32_t    fmqm_gs;
    uint32_t    fm_npi;
    uint32_t    fm_cld;
    uint32_t    fmfp_extc;
} t_FmGetParams;

typedef struct {
    t_FmSetParams setParams;
    t_FmGetParams getParams;
} t_FmGetSetParams;

t_Error FmGetSetParams(t_Handle h_Fm, t_FmGetSetParams *p_Params);

static __inline__ bool TRY_LOCK(t_Handle h_Spinlock, volatile bool *p_Flag)
{
    uint32_t intFlags;
    if (h_Spinlock)
        intFlags = XX_LockIntrSpinlock(h_Spinlock);
    else
        intFlags = XX_DisableAllIntr();

    if (*p_Flag)
    {
        if (h_Spinlock)
            XX_UnlockIntrSpinlock(h_Spinlock, intFlags);
        else
            XX_RestoreAllIntr(intFlags);
        return FALSE;
    }
    *p_Flag = TRUE;

    if (h_Spinlock)
        XX_UnlockIntrSpinlock(h_Spinlock, intFlags);
    else
        XX_RestoreAllIntr(intFlags);

    return TRUE;
}

#define RELEASE_LOCK(_flag) _flag = FALSE;

/**************************************************************************//**
 @Collection   Defines used for manipulation CC and BMI
 @{
*//***************************************************************************/
#define INTERNAL_CONTEXT_OFFSET                 0x80000000
#define OFFSET_OF_PR                            0x40000000
#define MANIP_EXTRA_SPACE                       0x20000000
#define NUM_OF_TASKS                            0x10000000
#define OFFSET_OF_DATA                          0x08000000
#define HW_PORT_ID                              0x04000000
#define FM_REV                                  0x02000000
#define GET_NIA_FPNE                            0x01000000
#define GET_NIA_PNDN                            0x00800000
#define NUM_OF_EXTRA_TASKS                      0x00400000
#define DISCARD_MASK                            0x00200000

#define UPDATE_NIA_PNEN                         0x80000000
#define UPDATE_PSO                              0x40000000
#define UPDATE_NIA_PNDN                         0x20000000
#define UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY      0x10000000
#define UPDATE_OFP_DPTE                         0x08000000
#define UPDATE_NIA_FENE                         0x04000000
#define UPDATE_NIA_CMNE                         0x02000000
#define UPDATE_NIA_FPNE                         0x01000000
/* @} */

/**************************************************************************//**
 @Collection   Defines used for manipulation CC and CC
 @{
*//***************************************************************************/
#define UPDATE_NIA_ENQ_WITHOUT_DMA              0x80000000
#define UPDATE_CC_WITH_TREE                     0x40000000
#define UPDATE_CC_WITH_DELETE_TREE              0x20000000
#define UPDATE_KG_NIA_CC_WA                     0x10000000
#define UPDATE_KG_OPT_MODE                      0x08000000
#define UPDATE_KG_NIA                           0x04000000
#define UPDATE_CC_SHADOW_CLEAR                    0x02000000
/* @} */

#define UPDATE_FPM_BRKC_SLP                     0x80000000
#define UPDATE_FPM_EXTC		                0x40000000
#define UPDATE_FPM_EXTC_CLEAR	                0x20000000
#define GET_FMQM_GS		                0x10000000
#define GET_FM_NPI		                0x08000000
#define GET_FMFP_EXTC		                0x04000000
#define CLEAR_IRAM_READY	                0x02000000
#define UPDATE_FM_CLD		                0x01000000
#define GET_FM_CLD		                0x00800000
#define FM_MAX_NUM_OF_PORTS     (FM_MAX_NUM_OF_OH_PORTS +     \
                                 FM_MAX_NUM_OF_1G_RX_PORTS +  \
                                 FM_MAX_NUM_OF_10G_RX_PORTS + \
                                 FM_MAX_NUM_OF_1G_TX_PORTS +  \
                                 FM_MAX_NUM_OF_10G_TX_PORTS)

#define MODULE_NAME_SIZE        30
#define DUMMY_PORT_ID           0

#define FM_LIODN_OFFSET_MASK    0x3FF

/**************************************************************************//**
  @Description       NIA Description
*//***************************************************************************/
#define NIA_ENG_MASK                0x007C0000
#define NIA_AC_MASK                 0x0003ffff

#define NIA_ORDER_RESTOR            0x00800000
#define NIA_ENG_FM_CTL              0x00000000
#define NIA_ENG_PRS                 0x00440000
#define NIA_ENG_KG                  0x00480000
#define NIA_ENG_PLCR                0x004C0000
#define NIA_ENG_BMI                 0x00500000
#define NIA_ENG_QMI_ENQ             0x00540000
#define NIA_ENG_QMI_DEQ             0x00580000

#define NIA_FM_CTL_AC_CC                        0x00000006
#define NIA_FM_CTL_AC_HC                        0x0000000C
#define NIA_FM_CTL_AC_IND_MODE_TX               0x00000008
#define NIA_FM_CTL_AC_IND_MODE_RX               0x0000000A
#define NIA_FM_CTL_AC_POP_TO_N_STEP             0x0000000e
#define NIA_FM_CTL_AC_PRE_BMI_FETCH_HEADER      0x00000010
#define NIA_FM_CTL_AC_PRE_BMI_FETCH_FULL_FRAME  0x00000018
#define NIA_FM_CTL_AC_POST_BMI_FETCH            0x00000012
#define NIA_FM_CTL_AC_PRE_BMI_ENQ_FRAME         0x0000001A
#define NIA_FM_CTL_AC_PRE_BMI_DISCARD_FRAME     0x0000001E
#define NIA_FM_CTL_AC_POST_BMI_ENQ_ORR          0x00000014
#define NIA_FM_CTL_AC_POST_BMI_ENQ              0x00000022
#define NIA_FM_CTL_AC_PRE_CC                    0x00000020
#define NIA_FM_CTL_AC_POST_TX                   0x00000024
/* V3 only */
#define NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_ENQ_FRAME        0x00000028
#define NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_DISCARD_FRAME    0x0000002A
#define NIA_FM_CTL_AC_NO_IPACC_POP_TO_N_STEP            0x0000002C

#define NIA_BMI_AC_ENQ_FRAME        0x00000002
#define NIA_BMI_AC_TX_RELEASE       0x000002C0
#define NIA_BMI_AC_RELEASE          0x000000C0
#define NIA_BMI_AC_DISCARD          0x000000C1
#define NIA_BMI_AC_TX               0x00000274
#define NIA_BMI_AC_FETCH            0x00000208
#define NIA_BMI_AC_MASK             0x000003FF

#define NIA_KG_DIRECT               0x00000100
#define NIA_KG_CC_EN                0x00000200
#define NIA_PLCR_ABSOLUTE           0x00008000

#define NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA    0x00000202

#if defined(FM_OP_NO_VSP_NO_RELEASE_ERRATA_FMAN_A006675) || defined(FM_ERROR_VSP_NO_MATCH_SW006)
#define GET_NIA_BMI_AC_ENQ_FRAME(h_FmPcd)   \
    (uint32_t)((FmPcdIsAdvancedOffloadSupported(h_FmPcd)) ? \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_PRE_BMI_ENQ_FRAME) : \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_ENQ_FRAME))
#define GET_NIA_BMI_AC_DISCARD_FRAME(h_FmPcd)   \
    (uint32_t)((FmPcdIsAdvancedOffloadSupported(h_FmPcd)) ? \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_PRE_BMI_DISCARD_FRAME) : \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_DISCARD_FRAME))
#define GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME()   \
        (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_NO_IPACC_PRE_BMI_ENQ_FRAME)
#else
#define GET_NIA_BMI_AC_ENQ_FRAME(h_FmPcd)   \
    (uint32_t)((FmPcdIsAdvancedOffloadSupported(h_FmPcd)) ? \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_PRE_BMI_ENQ_FRAME) : \
                (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME))
#define GET_NIA_BMI_AC_DISCARD_FRAME(h_FmPcd)   \
    (uint32_t)((FmPcdIsAdvancedOffloadSupported(h_FmPcd)) ? \
                (NIA_ENG_FM_CTL | NIA_FM_CTL_AC_PRE_BMI_DISCARD_FRAME) : \
                (NIA_ENG_BMI | NIA_BMI_AC_DISCARD))
#define GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME()   \
            (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)
#endif /* defined(FM_OP_NO_VSP_NO_RELEASE_ERRATA_FMAN_A006675) || ... */

/**************************************************************************//**
  @Description        CTRL Parameters Page defines
*//***************************************************************************/
#define FM_CTL_PARAMS_PAGE_OP_FIX_EN            0x80000000
#define FM_CTL_PARAMS_PAGE_OFFLOAD_SUPPORT_EN   0x40000000
#define FM_CTL_PARAMS_PAGE_ALWAYS_ON            0x00000100

#define FM_CTL_PARAMS_PAGE_ERROR_VSP_MASK       0x0000003f

/**************************************************************************//**
 @Description       Port Id defines
*//***************************************************************************/
#if (DPAA_VERSION == 10)
#define BASE_OH_PORTID              1
#else
#define BASE_OH_PORTID              2
#endif /* (DPAA_VERSION == 10) */
#define BASE_1G_RX_PORTID           8
#define BASE_10G_RX_PORTID          0x10
#define BASE_1G_TX_PORTID           0x28
#define BASE_10G_TX_PORTID          0x30

#define FM_PCD_PORT_OH_BASE_INDX        0
#define FM_PCD_PORT_1G_RX_BASE_INDX     (FM_PCD_PORT_OH_BASE_INDX+FM_MAX_NUM_OF_OH_PORTS)
#define FM_PCD_PORT_10G_RX_BASE_INDX    (FM_PCD_PORT_1G_RX_BASE_INDX+FM_MAX_NUM_OF_1G_RX_PORTS)
#define FM_PCD_PORT_1G_TX_BASE_INDX     (FM_PCD_PORT_10G_RX_BASE_INDX+FM_MAX_NUM_OF_10G_RX_PORTS)
#define FM_PCD_PORT_10G_TX_BASE_INDX    (FM_PCD_PORT_1G_TX_BASE_INDX+FM_MAX_NUM_OF_1G_TX_PORTS)

#if (FM_MAX_NUM_OF_OH_PORTS > 0)
#define CHECK_PORT_ID_OH_PORTS(_relativePortId)                     \
    if ((_relativePortId) >= FM_MAX_NUM_OF_OH_PORTS)                \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal OH_PORT port id"))
#else
#define CHECK_PORT_ID_OH_PORTS(_relativePortId)                     \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal OH_PORT port id"))
#endif
#if (FM_MAX_NUM_OF_1G_RX_PORTS > 0)
#define CHECK_PORT_ID_1G_RX_PORTS(_relativePortId)                  \
    if ((_relativePortId) >= FM_MAX_NUM_OF_1G_RX_PORTS)             \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 1G_RX_PORT port id"))
#else
#define CHECK_PORT_ID_1G_RX_PORTS(_relativePortId)                  \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 1G_RX_PORT port id"))
#endif
#if (FM_MAX_NUM_OF_10G_RX_PORTS > 0)
#define CHECK_PORT_ID_10G_RX_PORTS(_relativePortId)                 \
    if ((_relativePortId) >= FM_MAX_NUM_OF_10G_RX_PORTS)            \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 10G_RX_PORT port id"))
#else
#define CHECK_PORT_ID_10G_RX_PORTS(_relativePortId)                 \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 10G_RX_PORT port id"))
#endif
#if (FM_MAX_NUM_OF_1G_TX_PORTS > 0)
#define CHECK_PORT_ID_1G_TX_PORTS(_relativePortId)                  \
    if ((_relativePortId) >= FM_MAX_NUM_OF_1G_TX_PORTS)             \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 1G_TX_PORT port id"))
#else
#define CHECK_PORT_ID_1G_TX_PORTS(_relativePortId)                  \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 1G_TX_PORT port id"))
#endif
#if (FM_MAX_NUM_OF_10G_TX_PORTS > 0)
#define CHECK_PORT_ID_10G_TX_PORTS(_relativePortId)                 \
    if ((_relativePortId) >= FM_MAX_NUM_OF_10G_TX_PORTS)            \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 10G_TX_PORT port id"))
#else
#define CHECK_PORT_ID_10G_TX_PORTS(_relativePortId)                 \
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal 10G_TX_PORT port id"))
#endif

uint8_t SwPortIdToHwPortId(e_FmPortType type, uint8_t relativePortId, uint8_t majorRev, uint8_t minorRev);

#define HW_PORT_ID_TO_SW_PORT_ID(_relativePortId, hardwarePortId)                   \
{   if (((hardwarePortId) >= BASE_OH_PORTID) &&                                     \
        ((hardwarePortId) < BASE_OH_PORTID+FM_MAX_NUM_OF_OH_PORTS))                 \
        _relativePortId = (uint8_t)((hardwarePortId)-BASE_OH_PORTID);               \
    else if (((hardwarePortId) >= BASE_10G_TX_PORTID) &&                            \
             ((hardwarePortId) < BASE_10G_TX_PORTID+FM_MAX_NUM_OF_10G_TX_PORTS))    \
        _relativePortId = (uint8_t)((hardwarePortId)-BASE_10G_TX_PORTID);           \
    else if (((hardwarePortId) >= BASE_1G_TX_PORTID) &&                             \
             ((hardwarePortId) < BASE_1G_TX_PORTID+FM_MAX_NUM_OF_1G_TX_PORTS))      \
        _relativePortId = (uint8_t)((hardwarePortId)-BASE_1G_TX_PORTID);            \
    else if (((hardwarePortId) >= BASE_10G_RX_PORTID) &&                            \
             ((hardwarePortId) < BASE_10G_RX_PORTID+FM_MAX_NUM_OF_10G_RX_PORTS))    \
        _relativePortId = (uint8_t)((hardwarePortId)-BASE_10G_RX_PORTID);           \
    else if (((hardwarePortId) >= BASE_1G_RX_PORTID) &&                             \
             ((hardwarePortId) < BASE_1G_RX_PORTID+FM_MAX_NUM_OF_1G_RX_PORTS))      \
        _relativePortId = (uint8_t)((hardwarePortId)-BASE_1G_RX_PORTID);            \
    else {                                                                          \
        _relativePortId = (uint8_t)DUMMY_PORT_ID;                                   \
        ASSERT_COND(TRUE);                                                          \
    }                                                                               \
}

#define HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId)                                             \
do {                                                                                                        \
    if (((hardwarePortId) >= BASE_OH_PORTID) && ((hardwarePortId) < BASE_OH_PORTID+FM_MAX_NUM_OF_OH_PORTS)) \
        swPortIndex = (uint8_t)((hardwarePortId)-BASE_OH_PORTID+FM_PCD_PORT_OH_BASE_INDX);                  \
    else if (((hardwarePortId) >= BASE_1G_RX_PORTID) &&                                                     \
             ((hardwarePortId) < BASE_1G_RX_PORTID+FM_MAX_NUM_OF_1G_RX_PORTS))                              \
        swPortIndex = (uint8_t)((hardwarePortId)-BASE_1G_RX_PORTID+FM_PCD_PORT_1G_RX_BASE_INDX);            \
    else if (((hardwarePortId) >= BASE_10G_RX_PORTID) &&                                                    \
             ((hardwarePortId) < BASE_10G_RX_PORTID+FM_MAX_NUM_OF_10G_RX_PORTS))                            \
        swPortIndex = (uint8_t)((hardwarePortId)-BASE_10G_RX_PORTID+FM_PCD_PORT_10G_RX_BASE_INDX);          \
    else if (((hardwarePortId) >= BASE_1G_TX_PORTID) &&                                                     \
             ((hardwarePortId) < BASE_1G_TX_PORTID+FM_MAX_NUM_OF_1G_TX_PORTS))                              \
        swPortIndex = (uint8_t)((hardwarePortId)-BASE_1G_TX_PORTID+FM_PCD_PORT_1G_TX_BASE_INDX);            \
    else if (((hardwarePortId) >= BASE_10G_TX_PORTID) &&                                                    \
             ((hardwarePortId) < BASE_10G_TX_PORTID+FM_MAX_NUM_OF_10G_TX_PORTS))                            \
        swPortIndex = (uint8_t)((hardwarePortId)-BASE_10G_TX_PORTID+FM_PCD_PORT_10G_TX_BASE_INDX);          \
    else ASSERT_COND(FALSE);                                                                                \
} while (0)

#define SW_PORT_INDX_TO_HW_PORT_ID(hardwarePortId, swPortIndex)                                                 \
do {                                                                                                            \
    if (((swPortIndex) >= FM_PCD_PORT_OH_BASE_INDX) && ((swPortIndex) < FM_PCD_PORT_1G_RX_BASE_INDX))           \
        hardwarePortId = (uint8_t)((swPortIndex)-FM_PCD_PORT_OH_BASE_INDX+BASE_OH_PORTID);                      \
    else if (((swPortIndex) >= FM_PCD_PORT_1G_RX_BASE_INDX) && ((swPortIndex) < FM_PCD_PORT_10G_RX_BASE_INDX))  \
        hardwarePortId = (uint8_t)((swPortIndex)-FM_PCD_PORT_1G_RX_BASE_INDX+BASE_1G_RX_PORTID);                \
    else if (((swPortIndex) >= FM_PCD_PORT_10G_RX_BASE_INDX) && ((swPortIndex) < FM_MAX_NUM_OF_PORTS))          \
        hardwarePortId = (uint8_t)((swPortIndex)-FM_PCD_PORT_10G_RX_BASE_INDX+BASE_10G_RX_PORTID);              \
    else if (((swPortIndex) >= FM_PCD_PORT_1G_TX_BASE_INDX) && ((swPortIndex) < FM_PCD_PORT_10G_TX_BASE_INDX))  \
        hardwarePortId = (uint8_t)((swPortIndex)-FM_PCD_PORT_1G_TX_BASE_INDX+BASE_1G_TX_PORTID);                \
    else if (((swPortIndex) >= FM_PCD_PORT_10G_TX_BASE_INDX) && ((swPortIndex) < FM_MAX_NUM_OF_PORTS))          \
        hardwarePortId = (uint8_t)((swPortIndex)-FM_PCD_PORT_10G_TX_BASE_INDX+BASE_10G_TX_PORTID);              \
    else ASSERT_COND(FALSE);                                                                                    \
} while (0)

#define BMI_MAX_FIFO_SIZE                   (FM_MURAM_SIZE)
#define BMI_FIFO_UNITS                      0x100

typedef struct {
    void        (*f_Isr) (t_Handle h_Arg);
    t_Handle    h_SrcHandle;
    uint8_t     guestId;
} t_FmIntrSrc;

#define ILLEGAL_HDR_NUM                     0xFF
#define NO_HDR_NUM                          FM_PCD_PRS_NUM_OF_HDRS

#define IS_PRIVATE_HEADER(hdr)              (((hdr) == HEADER_TYPE_USER_DEFINED_SHIM1) ||   \
                                             ((hdr) == HEADER_TYPE_USER_DEFINED_SHIM2))
#define IS_SPECIAL_HEADER(hdr)              ((hdr) == HEADER_TYPE_MACSEC)

static __inline__ uint8_t GetPrsHdrNum(e_NetHeaderType hdr)
{
	 switch (hdr)
	 {   case (HEADER_TYPE_ETH):              return 0;
	     case (HEADER_TYPE_LLC_SNAP):         return 1;
	     case (HEADER_TYPE_VLAN):             return 2;
	     case (HEADER_TYPE_PPPoE):            return 3;
	     case (HEADER_TYPE_PPP):              return 3;
	     case (HEADER_TYPE_MPLS):             return 4;
	     case (HEADER_TYPE_IPv4):             return 5;
	     case (HEADER_TYPE_IPv6):             return 6;
	     case (HEADER_TYPE_GRE):              return 7;
	     case (HEADER_TYPE_MINENCAP):         return 8;
	     case (HEADER_TYPE_USER_DEFINED_L3):  return 9;
	     case (HEADER_TYPE_TCP):              return 10;
	     case (HEADER_TYPE_UDP):              return 11;
	     case (HEADER_TYPE_IPSEC_AH):
	     case (HEADER_TYPE_IPSEC_ESP):        return 12;
	     case (HEADER_TYPE_SCTP):             return 13;
	     case (HEADER_TYPE_DCCP):             return 14;
	     case (HEADER_TYPE_USER_DEFINED_L4):  return 15;
	     case (HEADER_TYPE_USER_DEFINED_SHIM1):
	     case (HEADER_TYPE_USER_DEFINED_SHIM2):
	     case (HEADER_TYPE_MACSEC):           return NO_HDR_NUM;
	     default:
	         return ILLEGAL_HDR_NUM;
	 }
}

#define FM_PCD_MAX_NUM_OF_OPTIONS(clsPlanEntries)   ((clsPlanEntries==256)? 8:((clsPlanEntries==128)? 7: ((clsPlanEntries==64)? 6: ((clsPlanEntries==32)? 5:0))))


/**************************************************************************//**
 @Description   A structure for initializing a keygen classification plan group
*//***************************************************************************/
typedef struct t_FmPcdKgInterModuleClsPlanGrpParams {
    uint8_t         netEnvId;   /* IN */
    bool            grpExists;  /* OUT (unused in FmPcdKgBuildClsPlanGrp)*/
    uint8_t         clsPlanGrpId;  /* OUT */
    bool            emptyClsPlanGrp; /* OUT */
    uint8_t         numOfOptions;   /* OUT in FmPcdGetSetClsPlanGrpParams IN in FmPcdKgBuildClsPlanGrp*/
    protocolOpt_t   options[FM_PCD_MAX_NUM_OF_OPTIONS(FM_PCD_MAX_NUM_OF_CLS_PLANS)];
                                    /* OUT in FmPcdGetSetClsPlanGrpParams IN in FmPcdKgBuildClsPlanGrp*/
    uint32_t        optVectors[FM_PCD_MAX_NUM_OF_OPTIONS(FM_PCD_MAX_NUM_OF_CLS_PLANS)];
                               /* OUT in FmPcdGetSetClsPlanGrpParams IN in FmPcdKgBuildClsPlanGrp*/
} t_FmPcdKgInterModuleClsPlanGrpParams;

typedef struct t_FmPcdLock {
    t_Handle        h_Spinlock;
    volatile bool   flag;
    t_List          node;
} t_FmPcdLock;
#define FM_PCD_LOCK_OBJ(ptr)  NCSW_LIST_OBJECT(ptr, t_FmPcdLock, node)


typedef t_Error (t_FmPortGetSetCcParamsCallback) (t_Handle                  h_FmPort,
                                                  t_FmPortGetSetCcParams    *p_FmPortGetSetCcParams);


/***********************************************************************/
/*          Common API for FM-PCD module                               */
/***********************************************************************/
t_Handle    FmPcdGetHcHandle(t_Handle h_FmPcd);
uint32_t    FmPcdGetSwPrsOffset(t_Handle h_FmPcd, e_NetHeaderType hdr, uint8_t  indexPerHdr);
uint32_t    FmPcdGetLcv(t_Handle h_FmPcd, uint32_t netEnvId, uint8_t hdrNum);
uint32_t    FmPcdGetMacsecLcv(t_Handle h_FmPcd, uint32_t netEnvId);
void        FmPcdIncNetEnvOwners(t_Handle h_FmPcd, uint8_t netEnvId);
void        FmPcdDecNetEnvOwners(t_Handle h_FmPcd, uint8_t netEnvId);
uint8_t     FmPcdGetNetEnvId(t_Handle h_NetEnv);
void        FmPcdPortRegister(t_Handle h_FmPcd, t_Handle h_FmPort, uint8_t hardwarePortId);
uint32_t    FmPcdLock(t_Handle h_FmPcd);
void        FmPcdUnlock(t_Handle h_FmPcd, uint32_t  intFlags);
bool        FmPcdNetEnvIsHdrExist(t_Handle h_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr);
t_Error     FmPcdFragHcScratchPoolInit(t_Handle h_FmPcd, uint8_t scratchBpid);
t_Error     FmPcdRegisterReassmPort(t_Handle h_FmPcd, t_Handle h_IpReasmCommonPramTbl);
t_Error     FmPcdUnregisterReassmPort(t_Handle h_FmPcd, t_Handle h_IpReasmCommonPramTbl);
bool        FmPcdIsAdvancedOffloadSupported(t_Handle h_FmPcd);
bool        FmPcdLockTryLockAll(t_Handle h_FmPcd);
void        FmPcdLockUnlockAll(t_Handle h_FmPcd);
t_Error     FmPcdHcSync(t_Handle h_FmPcd);
t_Handle    FmGetPcd(t_Handle h_Fm);
/***********************************************************************/
/*          Common API for FM-PCD KG module                            */
/***********************************************************************/
uint8_t     FmPcdKgGetClsPlanGrpBase(t_Handle h_FmPcd, uint8_t clsPlanGrp);
uint16_t    FmPcdKgGetClsPlanGrpSize(t_Handle h_FmPcd, uint8_t clsPlanGrp);
t_Error     FmPcdKgBuildClsPlanGrp(t_Handle h_FmPcd, t_FmPcdKgInterModuleClsPlanGrpParams *p_Grp, t_FmPcdKgInterModuleClsPlanSet *p_ClsPlanSet);

uint8_t     FmPcdKgGetSchemeId(t_Handle h_Scheme);
#if (DPAA_VERSION >= 11)
bool        FmPcdKgGetVspe(t_Handle h_Scheme);
#endif /* (DPAA_VERSION >= 11) */
uint8_t     FmPcdKgGetRelativeSchemeId(t_Handle h_FmPcd, uint8_t schemeId);
void        FmPcdKgDestroyClsPlanGrp(t_Handle h_FmPcd, uint8_t grpId);
t_Error     FmPcdKgCheckInvalidateSchemeSw(t_Handle h_Scheme);
t_Error     FmPcdKgBuildBindPortToSchemes(t_Handle h_FmPcd , t_FmPcdKgInterModuleBindPortToSchemes *p_BindPortToSchemes, uint32_t *p_SpReg, bool add);
bool        FmPcdKgHwSchemeIsValid(uint32_t schemeModeReg);
uint32_t    FmPcdKgBuildWriteSchemeActionReg(uint8_t schemeId, bool updateCounter);
uint32_t    FmPcdKgBuildReadSchemeActionReg(uint8_t schemeId);
uint32_t    FmPcdKgBuildWriteClsPlanBlockActionReg(uint8_t grpId);
uint32_t    FmPcdKgBuildWritePortSchemeBindActionReg(uint8_t hardwarePortId);
uint32_t    FmPcdKgBuildReadPortSchemeBindActionReg(uint8_t hardwarePortId);
uint32_t    FmPcdKgBuildWritePortClsPlanBindActionReg(uint8_t hardwarePortId);
bool        FmPcdKgIsSchemeValidSw(t_Handle h_Scheme);

t_Error     FmPcdKgBindPortToSchemes(t_Handle h_FmPcd , t_FmPcdKgInterModuleBindPortToSchemes  *p_SchemeBind);
t_Error     FmPcdKgUnbindPortToSchemes(t_Handle h_FmPcd , t_FmPcdKgInterModuleBindPortToSchemes *p_SchemeBind);
uint32_t    FmPcdKgGetRequiredAction(t_Handle h_FmPcd, uint8_t schemeId);
uint32_t    FmPcdKgGetRequiredActionFlag(t_Handle h_FmPcd, uint8_t schemeId);
e_FmPcdDoneAction FmPcdKgGetDoneAction(t_Handle h_FmPcd, uint8_t schemeId);
e_FmPcdEngine FmPcdKgGetNextEngine(t_Handle h_FmPcd, uint8_t schemeId);
void        FmPcdKgUpdateRequiredAction(t_Handle h_Scheme, uint32_t requiredAction);
bool        FmPcdKgIsDirectPlcr(t_Handle h_FmPcd, uint8_t schemeId);
bool        FmPcdKgIsDistrOnPlcrProfile(t_Handle h_FmPcd, uint8_t schemeId);
uint16_t    FmPcdKgGetRelativeProfileId(t_Handle h_FmPcd, uint8_t schemeId);
t_Handle    FmPcdKgGetSchemeHandle(t_Handle h_FmPcd, uint8_t relativeSchemeId);
bool        FmPcdKgIsSchemeHasOwners(t_Handle h_Scheme);
t_Error     FmPcdKgCcGetSetParams(t_Handle h_FmPcd, t_Handle  h_Scheme, uint32_t requiredAction, uint32_t value);
t_Error     FmPcdKgSetOrBindToClsPlanGrp(t_Handle h_FmPcd, uint8_t hardwarePortId, uint8_t netEnvId, protocolOpt_t *p_OptArray, uint8_t *p_ClsPlanGrpId, bool *p_IsEmptyClsPlanGrp);
t_Error     FmPcdKgDeleteOrUnbindPortToClsPlanGrp(t_Handle h_FmPcd, uint8_t hardwarePortId, uint8_t clsPlanGrpId);

/***********************************************************************/
/*          Common API for FM-PCD parser module                        */
/***********************************************************************/
t_Error     FmPcdPrsIncludePortInStatistics(t_Handle p_FmPcd, uint8_t hardwarePortId,  bool include);

/***********************************************************************/
/*          Common API for FM-PCD policer module                       */
/***********************************************************************/
t_Error     FmPcdPlcrAllocProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId, uint16_t numOfProfiles);
t_Error     FmPcdPlcrFreeProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId);
bool        FmPcdPlcrIsProfileValid(t_Handle h_FmPcd, uint16_t absoluteProfileId);
uint16_t    FmPcdPlcrGetPortProfilesBase(t_Handle h_FmPcd, uint8_t hardwarePortId);
uint16_t    FmPcdPlcrGetPortNumOfProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId);
uint32_t    FmPcdPlcrBuildWritePlcrActionRegs(uint16_t absoluteProfileId);
uint32_t    FmPcdPlcrBuildCounterProfileReg(e_FmPcdPlcrProfileCounters counter);
uint32_t    FmPcdPlcrBuildWritePlcrActionReg(uint16_t absoluteProfileId);
uint32_t    FmPcdPlcrBuildReadPlcrActionReg(uint16_t absoluteProfileId);
uint16_t    FmPcdPlcrProfileGetAbsoluteId(t_Handle h_Profile);
t_Error     FmPcdPlcrGetAbsoluteIdByProfileParams(t_Handle                      h_FmPcd,
                                          e_FmPcdProfileTypeSelection   profileType,
                                          t_Handle                      h_FmPort,
                                          uint16_t                      relativeProfile,
                                          uint16_t                      *p_AbsoluteId);
void        FmPcdPlcrInvalidateProfileSw(t_Handle h_FmPcd, uint16_t absoluteProfileId);
void        FmPcdPlcrValidateProfileSw(t_Handle h_FmPcd, uint16_t absoluteProfileId);
bool        FmPcdPlcrHwProfileIsValid(uint32_t profileModeReg);
uint32_t    FmPcdPlcrGetRequiredAction(t_Handle h_FmPcd, uint16_t absoluteProfileId);
uint32_t    FmPcdPlcrGetRequiredActionFlag(t_Handle h_FmPcd, uint16_t absoluteProfileId);
uint32_t    FmPcdPlcrBuildNiaProfileReg(bool green, bool yellow, bool red);
void        FmPcdPlcrUpdateRequiredAction(t_Handle h_FmPcd, uint16_t absoluteProfileId, uint32_t requiredAction);
t_Error     FmPcdPlcrCcGetSetParams(t_Handle h_FmPcd, uint16_t profileIndx,uint32_t requiredAction);

/***********************************************************************/
/*          Common API for FM-PCD CC module                            */
/***********************************************************************/
uint8_t     FmPcdCcGetParseCode(t_Handle h_CcNode);
uint8_t     FmPcdCcGetOffset(t_Handle h_CcNode);
t_Error     FmPcdCcRemoveKey(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode, uint16_t keyIndex);
t_Error     FmPcdCcAddKey(t_Handle h_FmPcd, t_Handle h_CcNode, uint16_t keyIndex, uint8_t keySize, t_FmPcdCcKeyParams *p_FmPCdCcKeyParams);
t_Error     FmPcdCcModifyKey(t_Handle h_FmPcd, t_Handle h_CcNode, uint16_t keyIndex, uint8_t keySize, uint8_t *p_Key, uint8_t *p_Mask);
t_Error     FmPcdCcModifyKeyAndNextEngine(t_Handle h_FmPcd, t_Handle h_FmPcdCcNode, uint16_t keyIndex, uint8_t keySize, t_FmPcdCcKeyParams *p_FmPcdCcKeyParams);
t_Error     FmPcdCcModifyMissNextEngineParamNode(t_Handle h_FmPcd,t_Handle h_FmPcdCcNode, t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);
t_Error     FmPcdCcModifyNextEngineParamTree(t_Handle h_FmPcd, t_Handle h_FmPcdCcTree, uint8_t grpId, uint8_t index, t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);
uint32_t    FmPcdCcGetNodeAddrOffsetFromNodeInfo(t_Handle h_FmPcd, t_Handle h_Pointer);
t_Handle    FmPcdCcTreeGetSavedManipParams(t_Handle h_FmTree);
void        FmPcdCcTreeSetSavedManipParams(t_Handle h_FmTree, t_Handle h_SavedManipParams);
t_Error     FmPcdCcTreeAddIPR(t_Handle h_FmPcd, t_Handle h_FmTree, t_Handle h_NetEnv, t_Handle h_ReassemblyManip, bool schemes);
t_Error     FmPcdCcTreeAddCPR(t_Handle h_FmPcd, t_Handle h_FmTree, t_Handle h_NetEnv, t_Handle h_ReassemblyManip, bool schemes);
t_Error     FmPcdCcBindTree(t_Handle h_FmPcd, t_Handle h_PcdParams, t_Handle h_CcTree,  uint32_t  *p_Offset,t_Handle h_FmPort);
t_Error     FmPcdCcUnbindTree(t_Handle h_FmPcd, t_Handle h_CcTree);

/***********************************************************************/
/*          Common API for FM-PCD Manip module                            */
/***********************************************************************/
t_Error     FmPcdManipUpdate(t_Handle h_FmPcd, t_Handle h_PcdParams, t_Handle h_FmPort, t_Handle h_Manip, t_Handle h_Ad, bool validate, int level, t_Handle h_FmTree, bool modify);

/***********************************************************************/
/*          Common API for FM-Port module                            */
/***********************************************************************/
#if (DPAA_VERSION >= 11)
typedef enum e_FmPortGprFuncType
{
    e_FM_PORT_GPR_EMPTY = 0,
    e_FM_PORT_GPR_MURAM_PAGE
} e_FmPortGprFuncType;

t_Error     FmPortSetGprFunc(t_Handle h_FmPort, e_FmPortGprFuncType gprFunc, void **p_Value);
#endif /* DPAA_VERSION >= 11) */
t_Error     FmGetSetParams(t_Handle h_Fm, t_FmGetSetParams *p_FmGetSetParams);
t_Error     FmPortGetSetCcParams(t_Handle h_FmPort, t_FmPortGetSetCcParams *p_FmPortGetSetCcParams);
uint8_t     FmPortGetNetEnvId(t_Handle h_FmPort);
uint8_t     FmPortGetHardwarePortId(t_Handle h_FmPort);
uint32_t    FmPortGetPcdEngines(t_Handle h_FmPort);
void        FmPortPcdKgSwUnbindClsPlanGrp (t_Handle h_FmPort);


#if (DPAA_VERSION >= 11)
t_Error     FmPcdFrmReplicUpdate(t_Handle h_FmPcd, t_Handle h_FmPort, t_Handle h_FrmReplic);
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Function      FmRegisterIntr

 @Description   Used to register an inter-module event handler to be processed by FM

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     mod             The module that causes the event
 @Param[in]     modId           Module id - if more than 1 instansiation of this
                                mode exists,0 otherwise.
 @Param[in]     intrType        Interrupt type (error/normal) selection.
 @Param[in]     f_Isr           The interrupt service routine.
 @Param[in]     h_Arg           Argument to be passed to f_Isr.

 @Return        None.
*//***************************************************************************/
void FmRegisterIntr(t_Handle               h_Fm,
                    e_FmEventModules       mod,
                    uint8_t                modId,
                    e_FmIntrType           intrType,
                    void                   (*f_Isr) (t_Handle h_Arg),
                    t_Handle               h_Arg);

/**************************************************************************//**
 @Function      FmUnregisterIntr

 @Description   Used to un-register an inter-module event handler that was processed by FM

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     mod             The module that causes the event
 @Param[in]     modId           Module id - if more than 1 instansiation of this
                                mode exists,0 otherwise.
 @Param[in]     intrType        Interrupt type (error/normal) selection.

 @Return        None.
*//***************************************************************************/
void FmUnregisterIntr(t_Handle          h_Fm,
                      e_FmEventModules  mod,
                      uint8_t           modId,
                      e_FmIntrType      intrType);

/**************************************************************************//**
 @Function      FmRegisterFmCtlIntr

 @Description   Used to register to one of the fmCtl events in the FM module

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     eventRegId      FmCtl event id (0-7).
 @Param[in]     f_Isr           The interrupt service routine.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
void  FmRegisterFmCtlIntr(t_Handle h_Fm, uint8_t eventRegId, void (*f_Isr) (t_Handle h_Fm, uint32_t event));


/**************************************************************************//**
 @Description   enum for defining MAC types
*//***************************************************************************/
typedef enum e_FmMacType {
    e_FM_MAC_10G = 0,               /**< 10G MAC */
    e_FM_MAC_1G                     /**< 1G MAC */
} e_FmMacType;

/**************************************************************************//**
 @Description   Structure for port-FM communication during FM_PORT_Init.
                Fields commented 'IN' are passed by the port module to be used
                by the FM module.
                Fields commented 'OUT' will be filled by FM before returning to port.
                Some fields are optional (depending on configuration) and
                will be analized by the port and FM modules accordingly.
*//***************************************************************************/
typedef struct t_FmInterModulePortInitParams {
    uint8_t             hardwarePortId;     /**< IN. port Id */
    e_FmPortType        portType;           /**< IN. Port type */
    bool                independentMode;    /**< IN. TRUE if FM Port operates in independent mode */
    uint16_t            liodnOffset;        /**< IN. Port's requested resource */
    uint8_t             numOfTasks;         /**< IN. Port's requested resource */
    uint8_t             numOfExtraTasks;    /**< IN. Port's requested resource */
    uint8_t             numOfOpenDmas;      /**< IN. Port's requested resource */
    uint8_t             numOfExtraOpenDmas; /**< IN. Port's requested resource */
    uint32_t            sizeOfFifo;         /**< IN. Port's requested resource */
    uint32_t            extraSizeOfFifo;    /**< IN. Port's requested resource */
    uint8_t             deqPipelineDepth;   /**< IN. Port's requested resource */
    uint16_t            maxFrameLength;     /**< IN. Port's max frame length. */
    uint16_t            liodnBase;          /**< IN. Irrelevant for P4080 rev 1.
                                                 LIODN base for this port, to be
                                                 used together with LIODN offset. */
    t_FmPhysAddr        fmMuramPhysBaseAddr;/**< OUT. FM-MURAM physical address*/
} t_FmInterModulePortInitParams;

/**************************************************************************//**
 @Description   Structure for port-FM communication during FM_PORT_Free.
*//***************************************************************************/
typedef struct t_FmInterModulePortFreeParams {
    uint8_t             hardwarePortId;     /**< IN. port Id */
    e_FmPortType        portType;           /**< IN. Port type */
    uint8_t             deqPipelineDepth;   /**< IN. Port's requested resource */
} t_FmInterModulePortFreeParams;

/**************************************************************************//**
 @Function      FmGetPcdPrsBaseAddr

 @Description   Get the base address of the Parser from the FM module

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        Base address.
*//***************************************************************************/
uintptr_t FmGetPcdPrsBaseAddr(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetPcdKgBaseAddr

 @Description   Get the base address of the Keygen from the FM module

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        Base address.
*//***************************************************************************/
uintptr_t FmGetPcdKgBaseAddr(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetPcdPlcrBaseAddr

 @Description   Get the base address of the Policer from the FM module

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        Base address.
*//***************************************************************************/
uintptr_t FmGetPcdPlcrBaseAddr(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetMuramHandle

 @Description   Get the handle of the MURAM from the FM module

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        MURAM module handle.
*//***************************************************************************/
t_Handle FmGetMuramHandle(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetPhysicalMuramBase

 @Description   Get the physical base address of the MURAM from the FM module

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     fmPhysAddr      Physical MURAM base

 @Return        Physical base address.
*//***************************************************************************/
void FmGetPhysicalMuramBase(t_Handle h_Fm, t_FmPhysAddr *fmPhysAddr);

/**************************************************************************//**
 @Function      FmGetTimeStampScale

 @Description   Used internally by other modules in order to get the timeStamp
                period as requested by the application.

                This function returns bit number that is incremented every 1 usec.
                To calculate timestamp period in nsec, use
                1000 / (1 << FmGetTimeStampScale()).

 @Param[in]     h_Fm                    A handle to an FM Module.

 @Return        Bit that counts 1 usec.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
uint32_t FmGetTimeStampScale(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmResumeStalledPort

 @Description   Used internally by FM port to release a stalled port.

 @Param[in]     h_Fm                            A handle to an FM Module.
 @Param[in]     hardwarePortId                    HW port id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FmResumeStalledPort(t_Handle h_Fm, uint8_t hardwarePortId);

/**************************************************************************//**
 @Function      FmIsPortStalled

 @Description   Used internally by FM port to read the port's status.

 @Param[in]     h_Fm                            A handle to an FM Module.
 @Param[in]     hardwarePortId                  HW port id.
 @Param[in]     p_IsStalled                     A pointer to the boolean port stalled state

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FmIsPortStalled(t_Handle h_Fm, uint8_t hardwarePortId, bool *p_IsStalled);

/**************************************************************************//**
 @Function      FmResetMac

 @Description   Used by MAC driver to reset the MAC registers

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     type            MAC type.
 @Param[in]     macId           MAC id - according to type.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FmResetMac(t_Handle h_Fm, e_FmMacType type, uint8_t macId);

/**************************************************************************//**
 @Function      FmGetClockFreq

 @Description   Used by MAC driver to get the FM clock frequency

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        clock-freq on success; 0 otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
uint16_t FmGetClockFreq(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetMacClockFreq

 @Description   Used by MAC driver to get the MAC clock frequency

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        clock-freq on success; 0 otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
uint16_t FmGetMacClockFreq(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetId

 @Description   Used by PCD driver to read rhe FM id

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
uint8_t FmGetId(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmReset

 @Description   Used to reset the FM

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FmReset(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FmGetSetPortParams

 @Description   Used by FM-PORT driver to pass and receive parameters between
                PORT and FM modules.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in,out] p_PortParams    A structure of FM Port parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FmGetSetPortParams(t_Handle h_Fm,t_FmInterModulePortInitParams *p_PortParams);

/**************************************************************************//**
 @Function      FmFreePortParams

 @Description   Used by FM-PORT driver to free port's resources within the FM.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in,out] p_PortParams    A structure of FM Port parameters.

 @Return        None.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
void FmFreePortParams(t_Handle h_Fm,t_FmInterModulePortFreeParams *p_PortParams);

/**************************************************************************//**
 @Function      FmSetNumOfRiscsPerPort

 @Description   Used by FM-PORT driver to pass parameter between
                PORT and FM modules for working with number of RISC..

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     hardwarePortId    hardware port Id.
 @Param[in]     numOfFmanCtrls    number of Fman Controllers.
 @Param[in]     orFmanCtrl        Fman Controller for order restoration.

 @Return        None.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FmSetNumOfRiscsPerPort(t_Handle h_Fm, uint8_t hardwarePortId, uint8_t numOfFmanCtrls, t_FmFmanCtrl orFmanCtrl);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//*
 @Function      FmDumpPortRegs

 @Description   Dumps FM port registers which are part of FM common registers

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     hardwarePortId    HW port id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only FM_Init().
*//***************************************************************************/
t_Error FmDumpPortRegs(t_Handle h_Fm,uint8_t hardwarePortId);
#endif /* (defined(DEBUG_ERRORS) && ... */

void        FmRegisterPcd(t_Handle h_Fm, t_Handle h_FmPcd);
void        FmUnregisterPcd(t_Handle h_Fm);
t_Handle    FmGetPcdHandle(t_Handle h_Fm);
t_Error     FmEnableRamsEcc(t_Handle h_Fm);
t_Error     FmDisableRamsEcc(t_Handle h_Fm);
void        FmGetRevision(t_Handle h_Fm, t_FmRevisionInfo *p_FmRevisionInfo);
t_Error     FmAllocFmanCtrlEventReg(t_Handle h_Fm, uint8_t *p_EventId);
void        FmFreeFmanCtrlEventReg(t_Handle h_Fm, uint8_t eventId);
void        FmSetFmanCtrlIntr(t_Handle h_Fm, uint8_t   eventRegId, uint32_t enableEvents);
uint32_t    FmGetFmanCtrlIntr(t_Handle h_Fm, uint8_t   eventRegId);
void        FmRegisterFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId, void (*f_Isr) (t_Handle h_Fm, uint32_t event), t_Handle    h_Arg);
void        FmUnregisterFmanCtrlIntr(t_Handle h_Fm, uint8_t eventRegId);
t_Error     FmSetMacMaxFrame(t_Handle h_Fm, e_FmMacType type, uint8_t macId, uint16_t mtu);
bool        FmIsMaster(t_Handle h_Fm);
uint8_t     FmGetGuestId(t_Handle h_Fm);
uint16_t    FmGetTnumAgingPeriod(t_Handle h_Fm);
t_Error     FmSetPortPreFetchConfiguration(t_Handle h_Fm, uint8_t portNum, bool preFetchConfigured);
t_Error     FmGetPortPreFetchConfiguration(t_Handle h_Fm, uint8_t portNum, bool *p_PortConfigured, bool *p_PreFetchConfigured);


#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
t_Error     Fm10GTxEccWorkaround(t_Handle h_Fm, uint8_t macId);
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */

void        FmMuramClear(t_Handle h_FmMuram);
t_Error     FmSetNumOfOpenDmas(t_Handle h_Fm,
                               uint8_t  hardwarePortId,
                               uint8_t  *p_NumOfOpenDmas,
                               uint8_t  *p_NumOfExtraOpenDmas,
                               bool     initialConfig);
t_Error     FmSetNumOfTasks(t_Handle    h_Fm,
                            uint8_t     hardwarePortId,
                            uint8_t     *p_NumOfTasks,
                            uint8_t     *p_NumOfExtraTasks,
                            bool        initialConfig);
t_Error     FmSetSizeOfFifo(t_Handle    h_Fm,
                            uint8_t     hardwarePortId,
                            uint32_t    *p_SizeOfFifo,
                            uint32_t    *p_ExtraSizeOfFifo,
                            bool        initialConfig);

t_Error     FmSetCongestionGroupPFCpriority(t_Handle    h_Fm,
                                            uint32_t    congestionGroupId,
                                            uint8_t     priorityBitMap);

#if (DPAA_VERSION >= 11)
t_Error     FmVSPAllocForPort(t_Handle         h_Fm,
                              e_FmPortType     portType,
                              uint8_t          portId,
                              uint8_t          numOfStorageProfiles);

t_Error     FmVSPFreeForPort(t_Handle        h_Fm,
                             e_FmPortType    portType,
                             uint8_t         portId);

t_Error     FmVSPGetAbsoluteProfileId(t_Handle      h_Fm,
                                      e_FmPortType  portType,
                                      uint8_t       portId,
                                      uint16_t      relativeProfile,
                                      uint16_t      *p_AbsoluteId);
t_Error FmVSPCheckRelativeProfile(t_Handle        h_Fm,
                                  e_FmPortType    portType,
                                  uint8_t         portId,
                                  uint16_t        relativeProfile);

uintptr_t   FmGetVSPBaseAddr(t_Handle h_Fm);
#endif /* (DPAA_VERSION >= 11) */


#endif /* __FM_COMMON_H */
