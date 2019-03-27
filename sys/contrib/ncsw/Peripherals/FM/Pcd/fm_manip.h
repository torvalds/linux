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
 @File          fm_manip.h

 @Description   FM PCD manip...
*//***************************************************************************/
#ifndef __FM_MANIP_H
#define __FM_MANIP_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"

#include "fm_cc.h"


/***********************************************************************/
/*          Header manipulations defines                              */
/***********************************************************************/

#define NUM_OF_SCRATCH_POOL_BUFFERS             1000 /*TODO - Change it!!*/

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
#define HMAN_OC_RMV_N_OR_INSRT_INT_FRM_HDR                      0x2e
#define HMAN_OC_INSRT_HDR_BY_TEMPL_N_OR_FRAG_AFTER              0x31
#define HMAN_OC_MV_INT_FRAME_HDR_FROM_FRM_TO_BUFFER_PREFFIX     0x2f
#define HMAN_OC_CAPWAP_RMV_DTLS_IF_EXIST                        0x30
#define HMAN_OC_CAPWAP_REASSEMBLY                               0x11 /* dummy */
#define HMAN_OC_CAPWAP_INDEXED_STATS                            0x32 /* dummy */
#define HMAN_OC_CAPWAP_FRAGMENTATION                            0x33
#else
#define HMAN_OC_CAPWAP_MANIP                                    0x2F
#define HMAN_OC_CAPWAP_FRAG_CHECK                               0x2E
#define HMAN_OC_CAPWAP_FRAGMENTATION                            0x33
#define HMAN_OC_CAPWAP_REASSEMBLY                               0x30
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
#define HMAN_OC_IP_MANIP                                        0x34
#define HMAN_OC_IP_FRAGMENTATION                                0x74
#define HMAN_OC_IP_REASSEMBLY                                   0xB4
#define HMAN_OC_IPSEC_MANIP                                     0xF4
#define HMAN_OC                                                 0x35

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
#define HMAN_RMV_HDR                               0x80000000
#define HMAN_INSRT_INT_FRM_HDR                     0x40000000

#define UDP_CHECKSUM_FIELD_OFFSET_FROM_UDP          6
#define UDP_CHECKSUM_FIELD_SIZE                     2
#define UDP_LENGTH_FIELD_OFFSET_FROM_UDP            4

#define IPv4_DSCECN_FIELD_OFFSET_FROM_IP            1
#define IPv4_TOTALLENGTH_FIELD_OFFSET_FROM_IP       2
#define IPv4_HDRCHECKSUM_FIELD_OFFSET_FROM_IP       10
#define VLAN_TAG_FIELD_OFFSET_FROM_ETH              12
#define IPv4_ID_FIELD_OFFSET_FROM_IP                4

#define IPv6_PAYLOAD_LENGTH_OFFSET_FROM_IP          4
#define IPv6_NEXT_HEADER_OFFSET_FROM_IP             6

#define FM_PCD_MANIP_CAPWAP_REASM_TABLE_SIZE               0x80
#define FM_PCD_MANIP_CAPWAP_REASM_TABLE_ALIGN              8
#define FM_PCD_MANIP_CAPWAP_REASM_RFD_SIZE                 32
#define FM_PCD_MANIP_CAPWAP_REASM_AUTO_LEARNING_HASH_ENTRY_SIZE 4
#define FM_PCD_MANIP_CAPWAP_REASM_TIME_OUT_ENTRY_SIZE      8


#define FM_PCD_MANIP_CAPWAP_REASM_TIME_OUT_BETWEEN_FRAMES          0x40000000
#define FM_PCD_MANIP_CAPWAP_REASM_HALT_ON_DUPLICATE_FRAG           0x10000000
#define FM_PCD_MANIP_CAPWAP_REASM_AUTOMATIC_LEARNIN_HASH_8_WAYS    0x08000000
#define FM_PCD_MANIP_CAPWAP_REASM_PR_COPY                          0x00800000

#define FM_PCD_MANIP_CAPWAP_FRAG_COMPR_OPTION_FIELD_EN             0x80000000

#define FM_PCD_MANIP_INDEXED_STATS_ENTRY_SIZE               4
#define FM_PCD_MANIP_INDEXED_STATS_CNIA                     0x20000000
#define FM_PCD_MANIP_INDEXED_STATS_DPD                      0x10000000
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

#if (DPAA_VERSION >= 11)
#define FM_PCD_MANIP_CAPWAP_DTLS                            0x00040000
#define FM_PCD_MANIP_CAPWAP_NADEN                           0x20000000

#define FM_PCD_MANIP_CAPWAP_FRAG_CHECK_MTU_SHIFT            16
#define FM_PCD_MANIP_CAPWAP_FRAG_CHECK_NO_FRAGMENTATION     0xFFFF0000
#define FM_PCD_MANIP_CAPWAP_FRAG_CHECK_CNIA                 0x20000000

#define FM_PCD_MANIP_CAPWAP_FRAG_COMPRESS_EN                0x04000000
#define FM_PCD_MANIP_CAPWAP_FRAG_SCRATCH_BPID               24
#define FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_EN                 0x08000000
#define FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_MASK               0xFF000000
#define FM_PCD_MANIP_CAPWAP_FRAG_SG_BDID_SHIFT              24
#endif /* (DPAA_VERSION >= 11) */

#define FM_PCD_MANIP_REASM_TABLE_SIZE                    0x40
#define FM_PCD_MANIP_REASM_TABLE_ALIGN                   8

#define FM_PCD_MANIP_REASM_COMMON_PARAM_TABLE_SIZE       64
#define FM_PCD_MANIP_REASM_COMMON_PARAM_TABLE_ALIGN      8
#define FM_PCD_MANIP_REASM_TIME_OUT_BETWEEN_FRAMES       0x80000000
#define FM_PCD_MANIP_REASM_COUPLING_ENABLE               0x40000000
#define FM_PCD_MANIP_REASM_COUPLING_MASK                 0xFF000000
#define FM_PCD_MANIP_REASM_COUPLING_SHIFT                24
#define FM_PCD_MANIP_REASM_LIODN_MASK                    0x0000003F
#define FM_PCD_MANIP_REASM_LIODN_SHIFT                   56
#define FM_PCD_MANIP_REASM_ELIODN_MASK                   0x000003c0
#define FM_PCD_MANIP_REASM_ELIODN_SHIFT                  38
#define FM_PCD_MANIP_REASM_COMMON_INT_BUFFER_IDX_MASK    0x000000FF
#define FM_PCD_MANIP_REASM_COMMON_INT_BUFFER_IDX_SHIFT   24
#define FM_PCD_MANIP_REASM_TIMEOUT_THREAD_THRESH        1024

#define FM_PCD_MANIP_IP_MTU_SHIFT                           16
#define FM_PCD_MANIP_IP_NO_FRAGMENTATION                    0xFFFF0000
#define FM_PCD_MANIP_IP_CNIA                                0x20000000

#define FM_PCD_MANIP_IP_FRAG_DF_SHIFT                       28
#define FM_PCD_MANIP_IP_FRAG_SCRATCH_BPID                   24
#define FM_PCD_MANIP_IP_FRAG_SG_BDID_EN                     0x08000000
#define FM_PCD_MANIP_IP_FRAG_SG_BDID_MASK                   0xFF000000
#define FM_PCD_MANIP_IP_FRAG_SG_BDID_SHIFT                  24

#define FM_PCD_MANIP_IPSEC_DEC                              0x10000000
#define FM_PCD_MANIP_IPSEC_VIPV_EN                          0x08000000
#define FM_PCD_MANIP_IPSEC_ECN_EN                           0x04000000
#define FM_PCD_MANIP_IPSEC_DSCP_EN                          0x02000000
#define FM_PCD_MANIP_IPSEC_VIPL_EN                          0x01000000
#define FM_PCD_MANIP_IPSEC_NADEN                            0x20000000

#define FM_PCD_MANIP_IPSEC_IP_HDR_LEN_MASK                  0x00FF0000
#define FM_PCD_MANIP_IPSEC_IP_HDR_LEN_SHIFT                 16

#define FM_PCD_MANIP_IPSEC_ARW_SIZE_MASK                    0xFFFF0000
#define FM_PCD_MANIP_IPSEC_ARW_SIZE_SHIFT                   16

#define e_FM_MANIP_IP_INDX                                  1

#define HMCD_OPCODE_GENERIC_RMV                 0x01
#define HMCD_OPCODE_GENERIC_INSRT               0x02
#define HMCD_OPCODE_GENERIC_REPLACE             0x05
#define HMCD_OPCODE_L2_RMV                      0x08
#define HMCD_OPCODE_L2_INSRT                    0x09
#define HMCD_OPCODE_VLAN_PRI_UPDATE             0x0B
#define HMCD_OPCODE_IPV4_UPDATE                 0x0C
#define HMCD_OPCODE_IPV6_UPDATE                 0x10
#define HMCD_OPCODE_TCP_UDP_UPDATE              0x0E
#define HMCD_OPCODE_TCP_UDP_CHECKSUM            0x14
#define HMCD_OPCODE_REPLACE_IP                  0x12
#define HMCD_OPCODE_RMV_TILL                    0x15
#define HMCD_OPCODE_UDP_INSRT                   0x16
#define HMCD_OPCODE_IP_INSRT                    0x17
#define HMCD_OPCODE_CAPWAP_RMV                  0x18
#define HMCD_OPCODE_CAPWAP_INSRT                0x18
#define HMCD_OPCODE_GEN_FIELD_REPLACE           0x19

#define HMCD_LAST                               0x00800000

#define HMCD_DSCP_VALUES                        64

#define HMCD_BASIC_SIZE                         4
#define HMCD_PTR_SIZE                           4
#define HMCD_PARAM_SIZE                         4
#define HMCD_IPV4_ADDR_SIZE                     4
#define HMCD_IPV6_ADDR_SIZE                     0x10
#define HMCD_L4_HDR_SIZE                        8

#define HMCD_CAPWAP_INSRT                       0x00010000
#define HMCD_INSRT_UDP_LITE                     0x00010000
#define HMCD_IP_ID_MASK                         0x0000FFFF
#define HMCD_IP_SIZE_MASK                       0x0000FF00
#define HMCD_IP_SIZE_SHIFT                      8
#define HMCD_IP_LAST_PID_MASK                   0x000000FF
#define HMCD_IP_OR_QOS                          0x00010000
#define HMCD_IP_L4_CS_CALC                      0x00040000
#define HMCD_IP_DF_MODE                         0x00400000


#define HMCD_OC_SHIFT                           24

#define HMCD_RMV_OFFSET_SHIFT                   0
#define HMCD_RMV_SIZE_SHIFT                     8

#define HMCD_INSRT_OFFSET_SHIFT                 0
#define HMCD_INSRT_SIZE_SHIFT                   8

#define HMTD_CFG_TYPE                           0x4000
#define HMTD_CFG_EXT_HMCT                       0x0080
#define HMTD_CFG_PRS_AFTER_HM                   0x0040
#define HMTD_CFG_NEXT_AD_EN                     0x0020

#define HMCD_RMV_L2_ETHERNET                    0
#define HMCD_RMV_L2_STACKED_QTAGS               1
#define HMCD_RMV_L2_ETHERNET_AND_MPLS           2
#define HMCD_RMV_L2_MPLS                        3
#define HMCD_RMV_L2_PPPOE                        4

#define HMCD_INSRT_L2_MPLS                      0
#define HMCD_INSRT_N_UPDATE_L2_MPLS             1
#define HMCD_INSRT_L2_PPPOE                     2
#define HMCD_INSRT_L2_SIZE_SHIFT                24

#define HMCD_L2_MODE_SHIFT                      16

#define HMCD_VLAN_PRI_REP_MODE_SHIFT            16
#define HMCD_VLAN_PRI_UPDATE                    0
#define HMCD_VLAN_PRI_UPDATE_DSCP_TO_VPRI       1

#define HMCD_IPV4_UPDATE_TTL                    0x00000001
#define HMCD_IPV4_UPDATE_TOS                    0x00000002
#define HMCD_IPV4_UPDATE_DST                    0x00000020
#define HMCD_IPV4_UPDATE_SRC                    0x00000040
#define HMCD_IPV4_UPDATE_ID                     0x00000080
#define HMCD_IPV4_UPDATE_TOS_SHIFT              8

#define HMCD_IPV6_UPDATE_HL                     0x00000001
#define HMCD_IPV6_UPDATE_TC                     0x00000002
#define HMCD_IPV6_UPDATE_DST                    0x00000040
#define HMCD_IPV6_UPDATE_SRC                    0x00000080
#define HMCD_IPV6_UPDATE_TC_SHIFT               8

#define HMCD_TCP_UDP_UPDATE_DST                 0x00004000
#define HMCD_TCP_UDP_UPDATE_SRC                 0x00008000
#define HMCD_TCP_UDP_UPDATE_SRC_SHIFT           16

#define HMCD_IP_REPLACE_REPLACE_IPV4            0x00000000
#define HMCD_IP_REPLACE_REPLACE_IPV6            0x00010000
#define HMCD_IP_REPLACE_TTL_HL                  0x00200000
#define HMCD_IP_REPLACE_ID                      0x00400000

#define HMCD_IP_REPLACE_L3HDRSIZE_SHIFT         24

#define HMCD_GEN_FIELD_SIZE_SHIFT               16
#define HMCD_GEN_FIELD_SRC_OFF_SHIFT            8
#define HMCD_GEN_FIELD_DST_OFF_SHIFT            0
#define HMCD_GEN_FIELD_MASK_EN                  0x00400000

#define HMCD_GEN_FIELD_MASK_OFF_SHIFT           16
#define HMCD_GEN_FIELD_MASK_SHIFT               24

#define DSCP_TO_VLAN_TABLE_SIZE                    32

#define MANIP_GET_HMCT_SIZE(h_Manip)                    (((t_FmPcdManip *)h_Manip)->tableSize)
#define MANIP_GET_DATA_SIZE(h_Manip)                    (((t_FmPcdManip *)h_Manip)->dataSize)

#define MANIP_GET_HMCT_PTR(h_Manip)                     (((t_FmPcdManip *)h_Manip)->p_Hmct)
#define MANIP_GET_DATA_PTR(h_Manip)                     (((t_FmPcdManip *)h_Manip)->p_Data)

#define MANIP_SET_HMCT_PTR(h_Manip, h_NewPtr)           (((t_FmPcdManip *)h_Manip)->p_Hmct = h_NewPtr)
#define MANIP_SET_DATA_PTR(h_Manip, h_NewPtr)           (((t_FmPcdManip *)h_Manip)->p_Data = h_NewPtr)

#define MANIP_GET_HMTD_PTR(h_Manip)                     (((t_FmPcdManip *)h_Manip)->h_Ad)
#define MANIP_DONT_REPARSE(h_Manip)                     (((t_FmPcdManip *)h_Manip)->dontParseAfterManip)
#define MANIP_SET_PREV(h_Manip, h_Prev)                 (((t_FmPcdManip *)h_Manip)->h_PrevManip = h_Prev)
#define MANIP_GET_OWNERS(h_Manip)                       (((t_FmPcdManip *)h_Manip)->owner)
#define MANIP_GET_TYPE(h_Manip)                         (((t_FmPcdManip *)h_Manip)->type)
#define MANIP_SET_UNIFIED_TBL_PTR_INDICATION(h_Manip)   (((t_FmPcdManip *)h_Manip)->unifiedTablePtr = TRUE)
#define MANIP_GET_MURAM(h_Manip)                        (((t_FmPcd *)((t_FmPcdManip *)h_Manip)->h_FmPcd)->h_FmMuram)
#define MANIP_FREE_HMTD(h_Manip)                        \
        {if (((t_FmPcdManip *)h_Manip)->muramAllocate)    \
            FM_MURAM_FreeMem(((t_FmPcd *)((t_FmPcdManip *)h_Manip)->h_FmPcd)->h_FmMuram, ((t_FmPcdManip *)h_Manip)->h_Ad);\
        else                                            \
            XX_Free(((t_FmPcdManip *)h_Manip)->h_Ad);    \
        ((t_FmPcdManip *)h_Manip)->h_Ad = NULL;            \
        }
/* position regarding Manip SW structure */
#define MANIP_IS_FIRST(h_Manip)                         (!(((t_FmPcdManip *)h_Manip)->h_PrevManip))
#define MANIP_IS_CASCADED(h_Manip)                       (((t_FmPcdManip *)h_Manip)->cascaded)
#define MANIP_IS_UNIFIED(h_Manip)                       (!(((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_NONE))
#define MANIP_IS_UNIFIED_NON_FIRST(h_Manip)             ((((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_MID) || \
                                                         (((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_LAST))
#define MANIP_IS_UNIFIED_NON_LAST(h_Manip)              ((((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_FIRST) ||\
                                                         (((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_MID))
#define MANIP_IS_UNIFIED_FIRST(h_Manip)                    (((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_FIRST)
#define MANIP_IS_UNIFIED_LAST(h_Manip)                   (((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_LAST)

#define MANIP_UPDATE_UNIFIED_POSITION(h_Manip)          (((t_FmPcdManip *)h_Manip)->unifiedPosition = \
                                                         (((t_FmPcdManip *)h_Manip)->unifiedPosition == e_MANIP_UNIFIED_NONE)? \
                                                            e_MANIP_UNIFIED_LAST : e_MANIP_UNIFIED_MID)

typedef enum e_ManipUnifiedPosition {
    e_MANIP_UNIFIED_NONE = 0,
    e_MANIP_UNIFIED_FIRST,
    e_MANIP_UNIFIED_MID,
    e_MANIP_UNIFIED_LAST
} e_ManipUnifiedPosition;

typedef enum e_ManipInfo {
    e_MANIP_HMTD,
    e_MANIP_HMCT,
    e_MANIP_HANDLER_TABLE_OWNER
}e_ManipInfo;
/***********************************************************************/
/*          Memory map                                                 */
/***********************************************************************/
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
typedef struct t_CapwapReasmPram {
    volatile uint32_t mode;
    volatile uint32_t autoLearnHashTblPtr;
    volatile uint32_t intStatsTblPtr;
    volatile uint32_t reasmFrmDescPoolTblPtr;
    volatile uint32_t reasmFrmDescIndexPoolTblPtr;
    volatile uint32_t timeOutTblPtr;
    volatile uint32_t bufferPoolIdAndRisc1SetIndexes;
    volatile uint32_t risc23SetIndexes;
    volatile uint32_t risc4SetIndexesAndExtendedStatsTblPtr;
    volatile uint32_t extendedStatsTblPtr;
    volatile uint32_t expirationDelay;
    volatile uint32_t totalProcessedFragCounter;
    volatile uint32_t totalUnsuccessfulReasmFramesCounter;
    volatile uint32_t totalDuplicatedFragCounter;
    volatile uint32_t totalMalformdFragCounter;
    volatile uint32_t totalTimeOutCounter;
    volatile uint32_t totalSetBusyCounter;
    volatile uint32_t totalRfdPoolBusyCounter;
    volatile uint32_t totalDiscardedFragsCounter;
    volatile uint32_t totalMoreThan16FramesCounter;
    volatile uint32_t internalBufferBusy;
    volatile uint32_t externalBufferBusy;
    volatile uint32_t reserved1[4];
} t_CapwapReasmPram;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

typedef _Packed struct t_ReassTbl {
    volatile uint16_t waysNumAndSetSize;
    volatile uint16_t autoLearnHashKeyMask;
    volatile uint32_t reassCommonPrmTblPtr;
    volatile uint32_t liodnAlAndAutoLearnHashTblPtrHi;
    volatile uint32_t autoLearnHashTblPtrLow;
    volatile uint32_t liodnSlAndAutoLearnSetLockTblPtrHi;
    volatile uint32_t autoLearnSetLockTblPtrLow;
    volatile uint16_t minFragSize; /* Not relevant for CAPWAP*/
    volatile uint16_t maxReassemblySize; /* Only relevant for CAPWAP*/
    volatile uint32_t totalSuccessfullyReasmFramesCounter;
    volatile uint32_t totalValidFragmentCounter;
    volatile uint32_t totalProcessedFragCounter;
    volatile uint32_t totalMalformdFragCounter;
    volatile uint32_t totalSetBusyCounter;
    volatile uint32_t totalDiscardedFragsCounter;
    volatile uint32_t totalMoreThan16FramesCounter;
    volatile uint32_t reserved2[2];
} _PackedType t_ReassTbl;

typedef struct t_ReassCommonTbl {
    volatile uint32_t timeoutModeAndFqid;
    volatile uint32_t reassFrmDescIndexPoolTblPtr;
    volatile uint32_t liodnAndReassFrmDescPoolPtrHi;
    volatile uint32_t reassFrmDescPoolPtrLow;
    volatile uint32_t timeOutTblPtr;
    volatile uint32_t expirationDelay;
    volatile uint32_t internalBufferManagement;
    volatile uint32_t reserved2;
    volatile uint32_t totalTimeOutCounter;
    volatile uint32_t totalRfdPoolBusyCounter;
    volatile uint32_t totalInternalBufferBusy;
    volatile uint32_t totalExternalBufferBusy;
    volatile uint32_t totalSgFragmentCounter;
    volatile uint32_t totalDmaSemaphoreDepletionCounter;
    volatile uint32_t totalNCSPCounter;
    volatile uint32_t discardMask;
} t_ReassCommonTbl;

typedef _Packed struct t_Hmtd {
    volatile uint16_t   cfg;
    volatile uint8_t    eliodnOffset;
    volatile uint8_t    extHmcdBasePtrHi;
    volatile uint32_t   hmcdBasePtr;
    volatile uint16_t   nextAdIdx;
    volatile uint8_t    res1;
    volatile uint8_t    opCode;
    volatile uint32_t   res2;
} _PackedType t_Hmtd;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/***********************************************************************/
/*  Driver's internal structures                                       */
/***********************************************************************/
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
typedef struct
{
    t_Handle p_AutoLearnHashTbl;
    t_Handle p_ReassmFrmDescrPoolTbl;
    t_Handle p_ReassmFrmDescrIndxPoolTbl;
    t_Handle p_TimeOutTbl;
    uint16_t maxNumFramesInProcess;
    uint8_t  numOfTasks;
    //uint8_t  poolId;
    uint8_t  prOffset;
    uint16_t dataOffset;
    uint8_t  sgBpid;
    uint8_t  hwPortId;
    uint32_t fqidForTimeOutFrames;
    uint32_t timeoutRoutineRequestTime;
    uint32_t bitFor1Micro;
} t_CapwapFragParams;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */

typedef struct
{
    t_AdOfTypeContLookup    *p_Frag;
#if (DPAA_VERSION == 10)
    uint8_t                 scratchBpid;
#endif /* (DPAA_VERSION == 10) */
} t_FragParams;

typedef struct t_ReassmParams
{
    e_NetHeaderType                 hdr; /* Header selection */
    t_ReassCommonTbl              	*p_ReassCommonTbl;
    uintptr_t                       reassFrmDescrIndxPoolTblAddr;
    uintptr_t                       reassFrmDescrPoolTblAddr;
    uintptr_t                       timeOutTblAddr;
    uintptr_t                       internalBufferPoolManagementIndexAddr;
    uintptr_t                       internalBufferPoolAddr;
    uint32_t                        maxNumFramesInProcess;
    uint8_t                         sgBpid;
    uint8_t                         dataMemId;
    uint16_t                        dataLiodnOffset;
    uint32_t                        fqidForTimeOutFrames;
    e_FmPcdManipReassemTimeOutMode  timeOutMode;
    uint32_t                        timeoutThresholdForReassmProcess;
    union {
	struct {
		t_Handle                h_Ipv4Ad;
	    t_Handle                h_Ipv6Ad;
	    bool                    ipv6Assigned;
	    t_ReassTbl				*p_Ipv4ReassTbl;
	    t_ReassTbl              *p_Ipv6ReassTbl;
	    uintptr_t               ipv4AutoLearnHashTblAddr;
	    uintptr_t               ipv6AutoLearnHashTblAddr;
	    uintptr_t               ipv4AutoLearnSetLockTblAddr;
	    uintptr_t               ipv6AutoLearnSetLockTblAddr;
	    uint16_t                        minFragSize[2];
	    e_FmPcdManipReassemWaysNumber   numOfFramesPerHashEntry[2];
	    uint8_t                         relativeSchemeId[2];
	    t_Handle                        h_Ipv4Scheme;
	    t_Handle                        h_Ipv6Scheme;
	    uint32_t                        nonConsistentSpFqid;
	} ip;
	struct {
		t_Handle                h_Ad;
	    t_ReassTbl				*p_ReassTbl;
	    uintptr_t               autoLearnHashTblAddr;
	    uintptr_t               autoLearnSetLockTblAddr;
	    uint16_t                maxRessembledsSize;
	    e_FmPcdManipReassemWaysNumber   numOfFramesPerHashEntry;
	    uint8_t                 relativeSchemeId;
	    t_Handle                h_Scheme;
	} capwap;
    };
} t_ReassmParams;

typedef struct{
    e_FmPcdManipType        type;
    t_FmPcdManipParams      manipParams;
    bool                    muramAllocate;
    t_Handle                h_Ad;
    uint32_t                opcode;
    bool                    rmv;
    bool                    insrt;
    t_Handle                h_NextManip;
    t_Handle                h_PrevManip;
    e_FmPcdManipType        nextManipType;
    /* HdrManip parameters*/
    uint8_t                 *p_Hmct;
    uint8_t                 *p_Data;
    bool                    dontParseAfterManip;
    bool                    fieldUpdate;
    bool                    custom;
    uint16_t                tableSize;
    uint8_t                 dataSize;
    bool                    cascaded;
    e_ManipUnifiedPosition  unifiedPosition;
    /* end HdrManip */
    uint8_t                 *p_Template;
    uint16_t                owner;
    uint32_t                updateParams;
    uint32_t                shadowUpdateParams;
    bool                    frag;
    bool                    reassm;
    uint16_t                sizeForFragmentation;
#if (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10))
    t_Handle                h_Frag;
    t_CapwapFragParams      capwapFragParams;
#endif /* (defined(FM_CAPWAP_SUPPORT) && (DPAA_VERSION == 10)) */
    union {
        t_ReassmParams    	reassmParams;
        t_FragParams      	fragParams;
    };
    uint8_t                 icOffset;
    uint16_t                ownerTmp;
    bool                    cnia;
    t_Handle                p_StatsTbl;
    t_Handle                h_FmPcd;
    t_List                  nodesLst;
    t_Handle                h_Spinlock;
} t_FmPcdManip;

typedef struct t_FmPcdCcSavedManipParams
{
    union
    {
        struct
        {
            uint16_t    dataOffset;
            //uint8_t     poolId;
        }capwapParams;
        struct
        {
            uint16_t    dataOffset;
            uint8_t     poolId;
        }ipParams;
    };

} t_FmPcdCcSavedManipParams;


#endif /* __FM_MANIP_H */
