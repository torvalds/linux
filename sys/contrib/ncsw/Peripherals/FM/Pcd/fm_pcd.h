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
 @File          fm_pcd.h

 @Description   FM PCD ...
*//***************************************************************************/
#ifndef __FM_PCD_H
#define __FM_PCD_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"
#include "fm_pcd_ext.h"
#include "fm_common.h"
#include "fsl_fman_prs.h"
#include "fsl_fman_kg.h"

#define __ERR_MODULE__  MODULE_FM_PCD


/****************************/
/* Defaults                 */
/****************************/
#define DEFAULT_plcrAutoRefresh                 FALSE
#define DEFAULT_fmPcdKgErrorExceptions          (FM_EX_KG_DOUBLE_ECC | FM_EX_KG_KEYSIZE_OVERFLOW)
#define DEFAULT_fmPcdPlcrErrorExceptions        (FM_PCD_EX_PLCR_DOUBLE_ECC | FM_PCD_EX_PLCR_INIT_ENTRY_ERROR)
#define DEFAULT_fmPcdPlcrExceptions             0
#define DEFAULT_fmPcdPrsErrorExceptions         (FM_PCD_EX_PRS_DOUBLE_ECC)

#define DEFAULT_fmPcdPrsExceptions              FM_PCD_EX_PRS_SINGLE_ECC
#define DEFAULT_numOfUsedProfilesPerWindow      16
#define DEFAULT_numOfSharedPlcrProfiles         4

/****************************/
/* Network defines          */
/****************************/
#define UDP_HEADER_SIZE     8

#define ESP_SPI_OFFSET      0
#define ESP_SPI_SIZE        4
#define ESP_SEQ_NUM_OFFSET  ESP_SPI_SIZE
#define ESP_SEQ_NUM_SIZE    4

/****************************/
/* General defines          */
/****************************/
#define ILLEGAL_CLS_PLAN    0xff
#define ILLEGAL_NETENV      0xff

#define FM_PCD_MAX_NUM_OF_ALIAS_HDRS    3

/****************************/
/* Error defines           */
/****************************/

#define FM_PCD_EX_PLCR_DOUBLE_ECC                   0x20000000
#define FM_PCD_EX_PLCR_INIT_ENTRY_ERROR             0x10000000
#define FM_PCD_EX_PLCR_PRAM_SELF_INIT_COMPLETE      0x08000000
#define FM_PCD_EX_PLCR_ATOMIC_ACTION_COMPLETE       0x04000000

#define GET_FM_PCD_EXCEPTION_FLAG(bitMask, exception)               \
switch (exception){                                                 \
    case e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC:                          \
        bitMask = FM_EX_KG_DOUBLE_ECC; break;                   \
    case e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC:                        \
        bitMask = FM_PCD_EX_PLCR_DOUBLE_ECC; break;                 \
    case e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW:                    \
        bitMask = FM_EX_KG_KEYSIZE_OVERFLOW; break;             \
    case e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR:                  \
        bitMask = FM_PCD_EX_PLCR_INIT_ENTRY_ERROR; break;           \
    case e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE:           \
        bitMask = FM_PCD_EX_PLCR_PRAM_SELF_INIT_COMPLETE; break;    \
    case e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE:            \
        bitMask = FM_PCD_EX_PLCR_ATOMIC_ACTION_COMPLETE; break;     \
    case e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC:                         \
        bitMask = FM_PCD_EX_PRS_DOUBLE_ECC; break;                  \
    case e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC:                         \
        bitMask = FM_PCD_EX_PRS_SINGLE_ECC; break;                  \
    default: bitMask = 0;break;}

/***********************************************************************/
/*          Policer defines                                            */
/***********************************************************************/
#define FM_PCD_PLCR_GCR_STEN                  0x40000000
#define FM_PCD_PLCR_DOUBLE_ECC                0x80000000
#define FM_PCD_PLCR_INIT_ENTRY_ERROR          0x40000000
#define FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE   0x80000000
#define FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE    0x40000000

/***********************************************************************/
/*          Memory map                                                 */
/***********************************************************************/
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */


typedef struct {
/* General Configuration and Status Registers */
    volatile uint32_t fmpl_gcr;         /* 0x000 FMPL_GCR  - FM Policer General Configuration */
    volatile uint32_t fmpl_gsr;         /* 0x004 FMPL_GSR  - FM Policer Global Status Register */
    volatile uint32_t fmpl_evr;         /* 0x008 FMPL_EVR  - FM Policer Event Register */
    volatile uint32_t fmpl_ier;         /* 0x00C FMPL_IER  - FM Policer Interrupt Enable Register */
    volatile uint32_t fmpl_ifr;         /* 0x010 FMPL_IFR  - FM Policer Interrupt Force Register */
    volatile uint32_t fmpl_eevr;        /* 0x014 FMPL_EEVR - FM Policer Error Event Register */
    volatile uint32_t fmpl_eier;        /* 0x018 FMPL_EIER - FM Policer Error Interrupt Enable Register */
    volatile uint32_t fmpl_eifr;        /* 0x01C FMPL_EIFR - FM Policer Error Interrupt Force Register */
/* Global Statistic Counters */
    volatile uint32_t fmpl_rpcnt;       /* 0x020 FMPL_RPC  - FM Policer RED Packets Counter */
    volatile uint32_t fmpl_ypcnt;       /* 0x024 FMPL_YPC  - FM Policer YELLOW Packets Counter */
    volatile uint32_t fmpl_rrpcnt;      /* 0x028 FMPL_RRPC - FM Policer Recolored RED Packet Counter */
    volatile uint32_t fmpl_rypcnt;      /* 0x02C FMPL_RYPC - FM Policer Recolored YELLOW Packet Counter */
    volatile uint32_t fmpl_tpcnt;       /* 0x030 FMPL_TPC  - FM Policer Total Packet Counter */
    volatile uint32_t fmpl_flmcnt;      /* 0x034 FMPL_FLMC - FM Policer Frame Length Mismatch Counter */
    volatile uint32_t fmpl_res0[21];    /* 0x038 - 0x08B Reserved */
/* Profile RAM Access Registers */
    volatile uint32_t fmpl_par;         /* 0x08C FMPL_PAR    - FM Policer Profile Action Register*/
    t_FmPcdPlcrProfileRegs profileRegs;
/* Error Capture Registers */
    volatile uint32_t fmpl_serc;        /* 0x100 FMPL_SERC - FM Policer Soft Error Capture */
    volatile uint32_t fmpl_upcr;        /* 0x104 FMPL_UPCR - FM Policer Uninitialized Profile Capture Register */
    volatile uint32_t fmpl_res2;        /* 0x108 Reserved */
/* Debug Registers */
    volatile uint32_t fmpl_res3[61];    /* 0x10C-0x200 Reserved Debug*/
/* Profile Selection Mapping Registers Per Port-ID (n=1-11, 16) */
    volatile uint32_t fmpl_dpmr;        /* 0x200 FMPL_DPMR - FM Policer Default Mapping Register */
    volatile uint32_t fmpl_pmr[63];     /*+default 0x204-0x2FF FMPL_PMR1 - FMPL_PMR63, - FM Policer Profile Mapping Registers.
                                           (for port-ID 1-11, only for supported Port-ID registers) */
} t_FmPcdPlcrRegs;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/***********************************************************************/
/*  Driver's internal structures                                       */
/***********************************************************************/

typedef struct {
    bool        known;
    uint8_t     id;
} t_FmPcdKgSchemesExtractsEntry;

typedef struct {
    t_FmPcdKgSchemesExtractsEntry extractsArray[FM_PCD_KG_MAX_NUM_OF_EXTRACTS_PER_KEY];
} t_FmPcdKgSchemesExtracts;

typedef struct {
    t_Handle        h_Manip;
    bool            keepRes;
    e_FmPcdEngine   nextEngine;
    uint8_t         parseCode;
} t_FmPcdInfoForManip;

/**************************************************************************//**
 @Description   A structure of parameters to communicate
                between the port and PCD regarding the KG scheme.
*//***************************************************************************/
typedef struct {
    uint8_t             netEnvId;    /* in */
    uint8_t             numOfDistinctionUnits; /* in */
    uint8_t             unitIds[FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS]; /* in */
    uint32_t            vector; /* out */
} t_NetEnvParams;

typedef struct {
    bool            allocated;
    uint8_t         ownerId; /* guestId for KG in multi-partition only.
                                portId for PLCR in any environment */
} t_FmPcdAllocMng;

typedef struct {
    volatile bool       lock;
    bool                used;
    uint8_t             owners;
    uint8_t             netEnvId;
    uint8_t             guestId;
    uint8_t             baseEntry;
    uint16_t            sizeOfGrp;
    protocolOpt_t       optArray[FM_PCD_MAX_NUM_OF_OPTIONS(FM_PCD_MAX_NUM_OF_CLS_PLANS)];
} t_FmPcdKgClsPlanGrp;

typedef struct {
    t_Handle            h_FmPcd;
    uint8_t             schemeId;
    t_FmPcdLock         *p_Lock;
    bool                valid;
    uint8_t             netEnvId;
    uint8_t             owners;
    uint32_t            matchVector;
    uint32_t            ccUnits;
    bool                nextRelativePlcrProfile;
    uint16_t            relativeProfileId;
    uint16_t            numOfProfiles;
    t_FmPcdKgKeyOrder   orderedArray;
    e_FmPcdEngine       nextEngine;
    e_FmPcdDoneAction   doneAction;
    bool                requiredActionFlag;
    uint32_t            requiredAction;
    bool                extractedOrs;
    uint8_t             bitOffsetInPlcrProfile;
    bool                directPlcr;
#if (DPAA_VERSION >= 11)
    bool                vspe;
#endif
} t_FmPcdKgScheme;

typedef union {
    struct fman_kg_scheme_regs schemeRegs;
    struct fman_kg_pe_regs portRegs;
    struct fman_kg_cp_regs clsPlanRegs;
} u_FmPcdKgIndirectAccessRegs;

typedef struct {
    struct fman_kg_regs *p_FmPcdKgRegs;
    uint32_t            schemeExceptionsBitMask;
    uint8_t             numOfSchemes;
    t_Handle            h_HwSpinlock;
    uint8_t             schemesIds[FM_PCD_KG_NUM_OF_SCHEMES];
    t_FmPcdKgScheme     schemes[FM_PCD_KG_NUM_OF_SCHEMES];
    t_FmPcdKgClsPlanGrp clsPlanGrps[FM_MAX_NUM_OF_PORTS];
    uint8_t             emptyClsPlanGrpId;
    t_FmPcdAllocMng     schemesMng[FM_PCD_KG_NUM_OF_SCHEMES]; /* only for MASTER ! */
    t_FmPcdAllocMng     clsPlanBlocksMng[FM_PCD_MAX_NUM_OF_CLS_PLANS/CLS_PLAN_NUM_PER_GRP];
    u_FmPcdKgIndirectAccessRegs *p_IndirectAccessRegs;
} t_FmPcdKg;

typedef struct {
    uint16_t            profilesBase;
    uint16_t            numOfProfiles;
    t_Handle            h_FmPort;
} t_FmPcdPlcrMapParam;

typedef struct {
    uint16_t                            absoluteProfileId;
    t_Handle                            h_FmPcd;
    bool                                valid;
    t_FmPcdLock                         *p_Lock;
    t_FmPcdAllocMng                     profilesMng;
    bool                                requiredActionFlag;
    uint32_t                            requiredAction;
    e_FmPcdEngine                       nextEngineOnGreen;          /**< Green next engine type */
    u_FmPcdPlcrNextEngineParams         paramsOnGreen;              /**< Green next engine params */

    e_FmPcdEngine                       nextEngineOnYellow;         /**< Yellow next engine type */
    u_FmPcdPlcrNextEngineParams         paramsOnYellow;             /**< Yellow next engine params */

    e_FmPcdEngine                       nextEngineOnRed;            /**< Red next engine type */
    u_FmPcdPlcrNextEngineParams         paramsOnRed;                /**< Red next engine params */
} t_FmPcdPlcrProfile;

typedef struct {
    t_FmPcdPlcrRegs                 *p_FmPcdPlcrRegs;
    uint16_t                        partPlcrProfilesBase;
    uint16_t                        partNumOfPlcrProfiles;
    t_FmPcdPlcrProfile              profiles[FM_PCD_PLCR_NUM_ENTRIES];
    uint16_t                        numOfSharedProfiles;
    uint16_t                        sharedProfilesIds[FM_PCD_PLCR_NUM_ENTRIES];
    t_FmPcdPlcrMapParam             portsMapping[FM_MAX_NUM_OF_PORTS];
    t_Handle                        h_HwSpinlock;
    t_Handle                        h_SwSpinlock;
} t_FmPcdPlcr;

typedef struct {
    uint32_t                        *p_SwPrsCode;
    uint32_t                        *p_CurrSwPrs;
    uint8_t                         currLabel;
    struct fman_prs_regs            *p_FmPcdPrsRegs;
    t_FmPcdPrsLabelParams           labelsTable[FM_PCD_PRS_NUM_OF_LABELS];
    uint32_t                        fmPcdPrsPortIdStatistics;
} t_FmPcdPrs;

typedef struct {
    struct {
        e_NetHeaderType             hdr;
        protocolOpt_t               opt; /* only one option !! */
    } hdrs[FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS];
} t_FmPcdIntDistinctionUnit;

typedef struct {
    e_NetHeaderType             hdr;
    protocolOpt_t               opt; /* only one option !! */
    e_NetHeaderType             aliasHdr;
} t_FmPcdNetEnvAliases;

typedef struct {
    uint8_t                     netEnvId;
    t_Handle                    h_FmPcd;
    t_Handle                    h_Spinlock;
    bool                        used;
    uint8_t                     owners;
    uint8_t                     clsPlanGrpId;
    t_FmPcdIntDistinctionUnit   units[FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS];
    uint32_t                    unitsVectors[FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS];
    uint32_t                    lcvs[FM_PCD_PRS_NUM_OF_HDRS];
    uint32_t                    macsecVector;
    t_FmPcdNetEnvAliases        aliasHdrs[FM_PCD_MAX_NUM_OF_ALIAS_HDRS];
} t_FmPcdNetEnv;

typedef struct {
    struct fman_prs_cfg          dfltCfg;
    bool                        plcrAutoRefresh;
    uint16_t                    prsMaxParseCycleLimit;
} t_FmPcdDriverParam;

typedef struct {
    t_Handle                    h_Fm;
    t_Handle                    h_FmMuram;
    t_FmRevisionInfo            fmRevInfo;

    uint64_t                    physicalMuramBase;

    t_Handle                    h_Spinlock;
    t_List                      freeLocksLst;
    t_List                      acquiredLocksLst;

    t_Handle                    h_IpcSession; /* relevant for guest only */
    bool                        enabled;
    uint8_t                     guestId;            /**< Guest Partition Id */
    uint8_t                     numOfEnabledGuestPartitionsPcds;
    char                        fmPcdModuleName[MODULE_NAME_SIZE];
    char                        fmPcdIpcHandlerModuleName[MODULE_NAME_SIZE]; /* relevant for guest only - this is the master's name */
    t_FmPcdNetEnv               netEnvs[FM_MAX_NUM_OF_PORTS];
    t_FmPcdKg                   *p_FmPcdKg;
    t_FmPcdPlcr                 *p_FmPcdPlcr;
    t_FmPcdPrs                  *p_FmPcdPrs;

    void                        *p_CcShadow;     /**< CC MURAM shadow */
    uint32_t                    ccShadowSize;
    uint32_t                    ccShadowAlign;
    volatile bool               shadowLock;
    t_Handle                    h_ShadowSpinlock;

    t_Handle                    h_Hc;

    uint32_t                    exceptions;
    t_FmPcdExceptionCallback    *f_Exception;
    t_FmPcdIdExceptionCallback  *f_FmPcdIndexedException;
    t_Handle                    h_App;
    uintptr_t                   ipv6FrameIdAddr;
    uintptr_t                   capwapFrameIdAddr;
    bool                        advancedOffloadSupport;

    t_FmPcdDriverParam          *p_FmPcdDriverParam;
} t_FmPcd;

#if (DPAA_VERSION >= 11)
typedef uint8_t t_FmPcdFrmReplicUpdateType;
#define FRM_REPLIC_UPDATE_COUNTER             0x01
#define FRM_REPLIC_UPDATE_INFO                0x02
#endif /* (DPAA_VERSION >= 11) */
/***********************************************************************/
/*  PCD internal routines                                              */
/***********************************************************************/

t_Error     PcdGetVectorForOpt(t_FmPcd *p_FmPcd, uint8_t netEnvId, protocolOpt_t opt, uint32_t *p_Vector);
t_Error     PcdGetUnitsVector(t_FmPcd *p_FmPcd, t_NetEnvParams *p_Params);
bool        PcdNetEnvIsUnitWithoutOpts(t_FmPcd *p_FmPcd, uint8_t netEnvId, uint32_t unitVector);
t_Error     PcdGetClsPlanGrpParams(t_FmPcd *p_FmPcd, t_FmPcdKgInterModuleClsPlanGrpParams *p_GrpParams);
void        FmPcdSetClsPlanGrpId(t_FmPcd *p_FmPcd, uint8_t netEnvId, uint8_t clsPlanGrpId);
e_NetHeaderType FmPcdGetAliasHdr(t_FmPcd *p_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr);
uint8_t     FmPcdNetEnvGetUnitIdForSingleHdr(t_FmPcd *p_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr);
uint8_t     FmPcdNetEnvGetUnitId(t_FmPcd *p_FmPcd, uint8_t netEnvId, e_NetHeaderType hdr, bool interchangeable, protocolOpt_t opt);

t_Error     FmPcdManipBuildIpReassmScheme(t_FmPcd *p_FmPcd, t_Handle h_NetEnv, t_Handle h_CcTree, t_Handle h_Manip, bool isIpv4, uint8_t groupId);
t_Error     FmPcdManipDeleteIpReassmSchemes(t_Handle h_Manip);
t_Error     FmPcdManipBuildCapwapReassmScheme(t_FmPcd *p_FmPcd, t_Handle h_NetEnv, t_Handle h_CcTree, t_Handle h_Manip, uint8_t groupId);
t_Error     FmPcdManipDeleteCapwapReassmSchemes(t_Handle h_Manip);
bool        FmPcdManipIpReassmIsIpv6Hdr(t_Handle h_Manip);

t_Handle    KgConfig( t_FmPcd *p_FmPcd, t_FmPcdParams *p_FmPcdParams);
t_Error     KgInit(t_FmPcd *p_FmPcd);
t_Error     KgFree(t_FmPcd *p_FmPcd);
void        KgSetClsPlan(t_Handle h_FmPcd, t_FmPcdKgInterModuleClsPlanSet *p_Set);
bool        KgIsSchemeAlwaysDirect(t_Handle h_FmPcd, uint8_t schemeId);
void        KgEnable(t_FmPcd *p_FmPcd);
void        KgDisable(t_FmPcd *p_FmPcd);
t_Error     KgAllocClsPlanEntries(t_Handle h_FmPcd, uint16_t numOfClsPlanEntries, uint8_t guestId, uint8_t *p_First);
void        KgFreeClsPlanEntries(t_Handle h_FmPcd, uint16_t numOfClsPlanEntries, uint8_t guestId, uint8_t base);

/* only for MULTI partittion */
t_Error     FmPcdKgAllocSchemes(t_Handle h_FmPcd, uint8_t numOfSchemes, uint8_t guestId, uint8_t *p_SchemesIds);
t_Error     FmPcdKgFreeSchemes(t_Handle h_FmPcd, uint8_t numOfSchemes, uint8_t guestId, uint8_t *p_SchemesIds);
/* only for SINGLE partittion */
t_Error     KgBindPortToSchemes(t_Handle h_FmPcd , uint8_t hardwarePortId, uint32_t spReg);

t_FmPcdLock *FmPcdAcquireLock(t_Handle h_FmPcd);
void        FmPcdReleaseLock(t_Handle h_FmPcd, t_FmPcdLock *p_Lock);

t_Handle    PlcrConfig(t_FmPcd *p_FmPcd, t_FmPcdParams *p_FmPcdParams);
t_Error     PlcrInit(t_FmPcd *p_FmPcd);
t_Error     PlcrFree(t_FmPcd *p_FmPcd);
void        PlcrEnable(t_FmPcd *p_FmPcd);
void        PlcrDisable(t_FmPcd *p_FmPcd);
uint16_t    PlcrAllocProfilesForPartition(t_FmPcd *p_FmPcd, uint16_t base, uint16_t numOfProfiles, uint8_t guestId);
void        PlcrFreeProfilesForPartition(t_FmPcd *p_FmPcd, uint16_t base, uint16_t numOfProfiles, uint8_t guestId);
t_Error     PlcrSetPortProfiles(t_FmPcd    *p_FmPcd,
                                uint8_t    hardwarePortId,
                                uint16_t   numOfProfiles,
                                uint16_t   base);
t_Error     PlcrClearPortProfiles(t_FmPcd *p_FmPcd, uint8_t hardwarePortId);

t_Handle    PrsConfig(t_FmPcd *p_FmPcd,t_FmPcdParams *p_FmPcdParams);
t_Error     PrsInit(t_FmPcd *p_FmPcd);
void        PrsEnable(t_FmPcd *p_FmPcd);
void        PrsDisable(t_FmPcd *p_FmPcd);
void        PrsFree(t_FmPcd *p_FmPcd );
t_Error     PrsIncludePortInStatistics(t_FmPcd *p_FmPcd, uint8_t hardwarePortId, bool include);

t_Error     FmPcdCcGetGrpParams(t_Handle treeId, uint8_t grpId, uint32_t *p_GrpBits, uint8_t *p_GrpBase);
uint8_t     FmPcdCcGetOffset(t_Handle h_CcNode);
uint8_t     FmPcdCcGetParseCode(t_Handle h_CcNode);
uint16_t    FmPcdCcGetNumOfKeys(t_Handle h_CcNode);
t_Error     ValidateNextEngineParams(t_Handle h_FmPcd, t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams, e_FmPcdCcStatsMode supportedStatsMode);

void        FmPcdManipUpdateOwner(t_Handle h_Manip, bool add);
t_Error     FmPcdManipCheckParamsForCcNextEngine(t_FmPcdCcNextEngineParams *p_InfoForManip, uint32_t *requiredAction);
void        FmPcdManipUpdateAdResultForCc(t_Handle                     h_Manip,
                                          t_FmPcdCcNextEngineParams    *p_CcNextEngineParams,
                                          t_Handle                     p_Ad,
                                          t_Handle                     *p_AdNewPtr);
void        FmPcdManipUpdateAdContLookupForCc(t_Handle h_Manip, t_Handle p_Ad, t_Handle *p_AdNew, uint32_t adTableOffset);
void        FmPcdManipUpdateOwner(t_Handle h_Manip, bool add);
t_Error     FmPcdManipCheckParamsWithCcNodeParams(t_Handle h_Manip, t_Handle h_FmPcdCcNode);
#ifdef FM_CAPWAP_SUPPORT
t_Handle    FmPcdManipApplSpecificBuild(void);
bool        FmPcdManipIsCapwapApplSpecific(t_Handle h_Manip);
#endif /* FM_CAPWAP_SUPPORT */
#if (DPAA_VERSION >= 11)
void *      FrmReplicGroupGetSourceTableDescriptor(t_Handle h_ReplicGroup);
void        FrmReplicGroupUpdateOwner(t_Handle h_ReplicGroup, bool add);
void        FrmReplicGroupUpdateAd(t_Handle h_ReplicGroup, void *p_Ad, t_Handle *h_AdNew);

void        FmPcdCcGetAdTablesThatPointOnReplicGroup(t_Handle   h_Node,
                                                     t_Handle   h_ReplicGroup,
                                                     t_List     *p_AdTables,
                                                     uint32_t   *p_NumOfAdTables);
#endif /* (DPAA_VERSION >= 11) */

void EnqueueNodeInfoToRelevantLst(t_List *p_List, t_CcNodeInformation *p_CcInfo, t_Handle h_Spinlock);
void DequeueNodeInfoFromRelevantLst(t_List *p_List, t_Handle h_Info, t_Handle h_Spinlock);
t_CcNodeInformation* FindNodeInfoInReleventLst(t_List *p_List, t_Handle h_Info, t_Handle h_Spinlock);
t_List *FmPcdManipGetSpinlock(t_Handle h_Manip);
t_List *FmPcdManipGetNodeLstPointedOnThisManip(t_Handle h_Manip);

typedef struct
{
    t_Handle    h_StatsAd;
    t_Handle    h_StatsCounters;
#if (DPAA_VERSION >= 11)
    t_Handle    h_StatsFLRs;
#endif /* (DPAA_VERSION >= 11) */
} t_FmPcdCcStatsParams;

void NextStepAd(t_Handle                     h_Ad,
                t_FmPcdCcStatsParams         *p_FmPcdCcStatsParams,
                t_FmPcdCcNextEngineParams    *p_FmPcdCcNextEngineParams,
                t_FmPcd                      *p_FmPcd);
void ReleaseLst(t_List *p_List);

static __inline__ t_Handle FmPcdGetMuramHandle(t_Handle h_FmPcd)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    ASSERT_COND(p_FmPcd);
    return p_FmPcd->h_FmMuram;
}

static __inline__ uint64_t FmPcdGetMuramPhysBase(t_Handle h_FmPcd)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    ASSERT_COND(p_FmPcd);
    return p_FmPcd->physicalMuramBase;
}

static __inline__ uint32_t FmPcdLockSpinlock(t_FmPcdLock *p_Lock)
{
    ASSERT_COND(p_Lock);
    return XX_LockIntrSpinlock(p_Lock->h_Spinlock);
}

static __inline__ void FmPcdUnlockSpinlock(t_FmPcdLock *p_Lock, uint32_t flags)
{
    ASSERT_COND(p_Lock);
    XX_UnlockIntrSpinlock(p_Lock->h_Spinlock, flags);
}

static __inline__ bool FmPcdLockTryLock(t_FmPcdLock *p_Lock)
{
    uint32_t intFlags;

    ASSERT_COND(p_Lock);
    intFlags = XX_LockIntrSpinlock(p_Lock->h_Spinlock);
    if (p_Lock->flag)
    {
        XX_UnlockIntrSpinlock(p_Lock->h_Spinlock, intFlags);
        return FALSE;
    }
    p_Lock->flag = TRUE;
    XX_UnlockIntrSpinlock(p_Lock->h_Spinlock, intFlags);
    return TRUE;
}

static __inline__ void FmPcdLockUnlock(t_FmPcdLock *p_Lock)
{
    ASSERT_COND(p_Lock);
    p_Lock->flag = FALSE;
}


#endif /* __FM_PCD_H */
