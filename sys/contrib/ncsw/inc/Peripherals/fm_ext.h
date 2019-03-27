/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
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


/**************************************************************************//**
 @File          fm_ext.h

 @Description   FM Application Programming Interface.
*//***************************************************************************/
#ifndef __FM_EXT
#define __FM_EXT

#include "error_ext.h"
#include "std_ext.h"
#include "dpaa_ext.h"
#include "fsl_fman_sp.h"

/**************************************************************************//**
 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_lib_grp FM library

 @Description   FM API functions, definitions and enums.

                The FM module is the main driver module and is a mandatory module
                for FM driver users. This module must be initialized first prior
                to any other drivers modules.
                The FM is a "singleton" module. It is responsible of the common
                HW modules: FPM, DMA, common QMI and common BMI initializations and
                run-time control routines. This module must be initialized always
                when working with any of the FM modules.
                NOTE - We assume that the FM library will be initialized only by core No. 0!

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Enum for defining port types
*//***************************************************************************/
typedef enum e_FmPortType {
    e_FM_PORT_TYPE_OH_OFFLINE_PARSING = 0,  /**< Offline parsing port */
    e_FM_PORT_TYPE_RX,                      /**< 1G Rx port */
    e_FM_PORT_TYPE_RX_10G,                  /**< 10G Rx port */
    e_FM_PORT_TYPE_TX,                      /**< 1G Tx port */
    e_FM_PORT_TYPE_TX_10G,                  /**< 10G Tx port */
    e_FM_PORT_TYPE_DUMMY
} e_FmPortType;

/**************************************************************************//**
 @Collection    General FM defines
*//***************************************************************************/
#define FM_MAX_NUM_OF_PARTITIONS    64      /**< Maximum number of partitions */
#define FM_PHYS_ADDRESS_SIZE        6       /**< FM Physical address size */
/* @} */


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Description   FM physical Address
*//***************************************************************************/
typedef _Packed struct t_FmPhysAddr {
    volatile uint8_t    high;         /**< High part of the physical address */
    volatile uint32_t   low;          /**< Low part of the physical address */
} _PackedType t_FmPhysAddr;

/**************************************************************************//**
 @Description   Parse results memory layout
*//***************************************************************************/
typedef _Packed struct t_FmPrsResult {
    volatile uint8_t     lpid;               /**< Logical port id */
    volatile uint8_t     shimr;              /**< Shim header result  */
    volatile uint16_t    l2r;                /**< Layer 2 result */
    volatile uint16_t    l3r;                /**< Layer 3 result */
    volatile uint8_t     l4r;                /**< Layer 4 result */
    volatile uint8_t     cplan;              /**< Classification plan id */
    volatile uint16_t    nxthdr;             /**< Next Header  */
    volatile uint16_t    cksum;              /**< Running-sum */
    volatile uint16_t    flags_frag_off;     /**< Flags & fragment-offset field of the last IP-header */
    volatile uint8_t     route_type;         /**< Routing type field of a IPv6 routing extension header */
    volatile uint8_t     rhp_ip_valid;       /**< Routing Extension Header Present; last bit is IP valid */
    volatile uint8_t     shim_off[2];        /**< Shim offset */
    volatile uint8_t     ip_pid_off;         /**< IP PID (last IP-proto) offset */
    volatile uint8_t     eth_off;            /**< ETH offset */
    volatile uint8_t     llc_snap_off;       /**< LLC_SNAP offset */
    volatile uint8_t     vlan_off[2];        /**< VLAN offset */
    volatile uint8_t     etype_off;          /**< ETYPE offset */
    volatile uint8_t     pppoe_off;          /**< PPP offset */
    volatile uint8_t     mpls_off[2];        /**< MPLS offset */
    volatile uint8_t     ip_off[2];          /**< IP offset */
    volatile uint8_t     gre_off;            /**< GRE offset */
    volatile uint8_t     l4_off;             /**< Layer 4 offset */
    volatile uint8_t     nxthdr_off;         /**< Parser end point */
} _PackedType t_FmPrsResult;

/**************************************************************************//**
 @Collection   FM Parser results
*//***************************************************************************/
#define FM_PR_L2_VLAN_STACK         0x00000100  /**< Parse Result: VLAN stack */
#define FM_PR_L2_ETHERNET           0x00008000  /**< Parse Result: Ethernet*/
#define FM_PR_L2_VLAN               0x00004000  /**< Parse Result: VLAN */
#define FM_PR_L2_LLC_SNAP           0x00002000  /**< Parse Result: LLC_SNAP */
#define FM_PR_L2_MPLS               0x00001000  /**< Parse Result: MPLS */
#define FM_PR_L2_PPPoE              0x00000800  /**< Parse Result: PPPoE */
/* @} */

/**************************************************************************//**
 @Collection   FM Frame descriptor macros
*//***************************************************************************/
#define FM_FD_CMD_FCO                   0x80000000  /**< Frame queue Context Override */
#define FM_FD_CMD_RPD                   0x40000000  /**< Read Prepended Data */
#define FM_FD_CMD_UPD                   0x20000000  /**< Update Prepended Data */
#define FM_FD_CMD_DTC                   0x10000000  /**< Do L4 Checksum */
#define FM_FD_CMD_DCL4C                 0x10000000  /**< Didn't calculate L4 Checksum */
#define FM_FD_CMD_CFQ                   0x00ffffff  /**< Confirmation Frame Queue */

#define FM_FD_ERR_UNSUPPORTED_FORMAT    0x04000000  /**< Not for Rx-Port! Unsupported Format */
#define FM_FD_ERR_LENGTH                0x02000000  /**< Not for Rx-Port! Length Error */
#define FM_FD_ERR_DMA                   0x01000000  /**< DMA Data error */

#define FM_FD_IPR                       0x00000001  /**< IPR frame (not error) */

#define FM_FD_ERR_IPR_NCSP              (0x00100000 | FM_FD_IPR)    /**< IPR non-consistent-sp */
#define FM_FD_ERR_IPR                   (0x00200000 | FM_FD_IPR)    /**< IPR error */
#define FM_FD_ERR_IPR_TO                (0x00300000 | FM_FD_IPR)    /**< IPR timeout */

#ifdef FM_CAPWAP_SUPPORT
#define FM_FD_ERR_CRE                   0x00200000
#define FM_FD_ERR_CHE                   0x00100000
#endif /* FM_CAPWAP_SUPPORT */

#define FM_FD_ERR_PHYSICAL              0x00080000  /**< Rx FIFO overflow, FCS error, code error, running disparity
                                                         error (SGMII and TBI modes), FIFO parity error. PHY
                                                         Sequence error, PHY error control character detected. */
#define FM_FD_ERR_SIZE                  0x00040000  /**< Frame too long OR Frame size exceeds max_length_frame  */
#define FM_FD_ERR_CLS_DISCARD           0x00020000  /**< classification discard */
#define FM_FD_ERR_EXTRACTION            0x00008000  /**< Extract Out of Frame */
#define FM_FD_ERR_NO_SCHEME             0x00004000  /**< No Scheme Selected */
#define FM_FD_ERR_KEYSIZE_OVERFLOW      0x00002000  /**< Keysize Overflow */
#define FM_FD_ERR_COLOR_RED             0x00000800  /**< Frame color is red */
#define FM_FD_ERR_COLOR_YELLOW          0x00000400  /**< Frame color is yellow */
#define FM_FD_ERR_ILL_PLCR              0x00000200  /**< Illegal Policer Profile selected */
#define FM_FD_ERR_PLCR_FRAME_LEN        0x00000100  /**< Policer frame length error */
#define FM_FD_ERR_PRS_TIMEOUT           0x00000080  /**< Parser Time out Exceed */
#define FM_FD_ERR_PRS_ILL_INSTRUCT      0x00000040  /**< Invalid Soft Parser instruction */
#define FM_FD_ERR_PRS_HDR_ERR           0x00000020  /**< Header error was identified during parsing */
#define FM_FD_ERR_BLOCK_LIMIT_EXCEEDED  0x00000008  /**< Frame parsed beyind 256 first bytes */

#define FM_FD_TX_STATUS_ERR_MASK        (FM_FD_ERR_UNSUPPORTED_FORMAT   | \
                                         FM_FD_ERR_LENGTH               | \
                                         FM_FD_ERR_DMA) /**< TX Error FD bits */

#define FM_FD_RX_STATUS_ERR_MASK        (FM_FD_ERR_UNSUPPORTED_FORMAT   | \
                                         FM_FD_ERR_LENGTH               | \
                                         FM_FD_ERR_DMA                  | \
                                         FM_FD_ERR_IPR                  | \
                                         FM_FD_ERR_IPR_TO               | \
                                         FM_FD_ERR_IPR_NCSP             | \
                                         FM_FD_ERR_PHYSICAL             | \
                                         FM_FD_ERR_SIZE                 | \
                                         FM_FD_ERR_CLS_DISCARD          | \
                                         FM_FD_ERR_COLOR_RED            | \
                                         FM_FD_ERR_COLOR_YELLOW         | \
                                         FM_FD_ERR_ILL_PLCR             | \
                                         FM_FD_ERR_PLCR_FRAME_LEN       | \
                                         FM_FD_ERR_EXTRACTION           | \
                                         FM_FD_ERR_NO_SCHEME            | \
                                         FM_FD_ERR_KEYSIZE_OVERFLOW     | \
                                         FM_FD_ERR_PRS_TIMEOUT          | \
                                         FM_FD_ERR_PRS_ILL_INSTRUCT     | \
                                         FM_FD_ERR_PRS_HDR_ERR          | \
                                         FM_FD_ERR_BLOCK_LIMIT_EXCEEDED) /**< RX Error FD bits */

#define FM_FD_RX_STATUS_ERR_NON_FM      0x00400000  /**< non Frame-Manager error */
/* @} */

/**************************************************************************//**
 @Description   Context A
*//***************************************************************************/
typedef _Packed struct t_FmContextA {
    volatile uint32_t    command;   /**< ContextA Command */
    volatile uint8_t     res0[4];   /**< ContextA Reserved bits */
} _PackedType t_FmContextA;

/**************************************************************************//**
 @Description   Context B
*//***************************************************************************/
typedef uint32_t t_FmContextB;

/**************************************************************************//**
 @Collection   Special Operation options
*//***************************************************************************/
typedef uint32_t fmSpecialOperations_t;                 /**< typedef for defining Special Operation options */

#define  FM_SP_OP_IPSEC                     0x80000000  /**< activate features that related to IPSec (e.g fix Eth-type) */
#define  FM_SP_OP_IPSEC_UPDATE_UDP_LEN      0x40000000  /**< update the UDP-Len after Encryption */
#define  FM_SP_OP_IPSEC_MANIP               0x20000000  /**< handle the IPSec-manip options */
#define  FM_SP_OP_RPD                       0x10000000  /**< Set the RPD bit */
#define  FM_SP_OP_DCL4C                     0x08000000  /**< Set the DCL4C bit */
#define  FM_SP_OP_CHECK_SEC_ERRORS          0x04000000  /**< Check SEC errors */
#define  FM_SP_OP_CLEAR_RPD                 0x02000000  /**< Clear the RPD bit */
#define  FM_SP_OP_CAPWAP_DTLS_ENC           0x01000000  /**< activate features that related to CAPWAP-DTLS post Encryption */
#define  FM_SP_OP_CAPWAP_DTLS_DEC           0x00800000  /**< activate features that related to CAPWAP-DTLS post Decryption */
#define  FM_SP_OP_IPSEC_NO_ETH_HDR          0x00400000  /**< activate features that related to IPSec without Eth hdr */
/* @} */

/**************************************************************************//**
 @Collection   Context A macros
*//***************************************************************************/
#define FM_CONTEXTA_OVERRIDE_MASK       0x80000000
#define FM_CONTEXTA_ICMD_MASK           0x40000000
#define FM_CONTEXTA_A1_VALID_MASK       0x20000000
#define FM_CONTEXTA_MACCMD_MASK         0x00ff0000
#define FM_CONTEXTA_MACCMD_VALID_MASK   0x00800000
#define FM_CONTEXTA_MACCMD_SECURED_MASK 0x00100000
#define FM_CONTEXTA_MACCMD_SC_MASK      0x000f0000
#define FM_CONTEXTA_A1_MASK             0x0000ffff

#define FM_CONTEXTA_GET_OVERRIDE(contextA)                 ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_OVERRIDE_MASK) >> (31-0))
#define FM_CONTEXTA_GET_ICMD(contextA)                     ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_ICMD_MASK) >> (31-1))
#define FM_CONTEXTA_GET_A1_VALID(contextA)                 ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_A1_VALID_MASK) >> (31-2))
#define FM_CONTEXTA_GET_A1(contextA)                       ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_A1_MASK) >> (31-31))
#define FM_CONTEXTA_GET_MACCMD(contextA)                   ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_MACCMD_MASK) >> (31-15))
#define FM_CONTEXTA_GET_MACCMD_VALID(contextA)             ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_MACCMD_VALID_MASK) >> (31-8))
#define FM_CONTEXTA_GET_MACCMD_SECURED(contextA)           ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_MACCMD_SECURED_MASK) >> (31-11))
#define FM_CONTEXTA_GET_MACCMD_SECURE_CHANNEL(contextA)    ((((t_FmContextA *)contextA)->command & FM_CONTEXTA_MACCMD_SC_MASK) >> (31-15))

#define FM_CONTEXTA_SET_OVERRIDE(contextA,val)              (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_OVERRIDE_MASK) | (((uint32_t)(val) << (31-0)) & FM_CONTEXTA_OVERRIDE_MASK) ))
#define FM_CONTEXTA_SET_ICMD(contextA,val)                  (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_ICMD_MASK) | (((val) << (31-1)) & FM_CONTEXTA_ICMD_MASK) ))
#define FM_CONTEXTA_SET_A1_VALID(contextA,val)              (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_A1_VALID_MASK) | (((val) << (31-2)) & FM_CONTEXTA_A1_VALID_MASK) ))
#define FM_CONTEXTA_SET_A1(contextA,val)                    (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_A1_MASK) | (((val) << (31-31)) & FM_CONTEXTA_A1_MASK) ))
#define FM_CONTEXTA_SET_MACCMD(contextA,val)                (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_MACCMD_MASK) | (((val) << (31-15)) & FM_CONTEXTA_MACCMD_MASK) ))
#define FM_CONTEXTA_SET_MACCMD_VALID(contextA,val)          (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_MACCMD_VALID_MASK) | (((val) << (31-8)) & FM_CONTEXTA_MACCMD_VALID_MASK) ))
#define FM_CONTEXTA_SET_MACCMD_SECURED(contextA,val)        (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_MACCMD_SECURED_MASK) | (((val) << (31-11)) & FM_CONTEXTA_MACCMD_SECURED_MASK) ))
#define FM_CONTEXTA_SET_MACCMD_SECURE_CHANNEL(contextA,val) (((t_FmContextA *)contextA)->command = (uint32_t)((((t_FmContextA *)contextA)->command & ~FM_CONTEXTA_MACCMD_SC_MASK) | (((val) << (31-15)) & FM_CONTEXTA_MACCMD_SC_MASK) ))
/* @} */

/**************************************************************************//**
 @Collection   Context B macros
*//***************************************************************************/
#define FM_CONTEXTB_FQID_MASK               0x00ffffff

#define FM_CONTEXTB_GET_FQID(contextB)      (*((t_FmContextB *)contextB) & FM_CONTEXTB_FQID_MASK)
#define FM_CONTEXTB_SET_FQID(contextB,val)  (*((t_FmContextB *)contextB) = ((*((t_FmContextB *)contextB) & ~FM_CONTEXTB_FQID_MASK) | ((val) & FM_CONTEXTB_FQID_MASK)))
/* @} */

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/**************************************************************************//**
 @Description   FM Exceptions
*//***************************************************************************/
typedef enum e_FmExceptions {
    e_FM_EX_DMA_BUS_ERROR = 0,          /**< DMA bus error. */
    e_FM_EX_DMA_READ_ECC,               /**< Read Buffer ECC error (Valid for FM rev < 6)*/
    e_FM_EX_DMA_SYSTEM_WRITE_ECC,       /**< Write Buffer ECC error on system side (Valid for FM rev < 6)*/
    e_FM_EX_DMA_FM_WRITE_ECC,           /**< Write Buffer ECC error on FM side (Valid for FM rev < 6)*/
    e_FM_EX_DMA_SINGLE_PORT_ECC,        /**< Single Port ECC error on FM side (Valid for FM rev > 6)*/
    e_FM_EX_FPM_STALL_ON_TASKS,         /**< Stall of tasks on FPM */
    e_FM_EX_FPM_SINGLE_ECC,             /**< Single ECC on FPM. */
    e_FM_EX_FPM_DOUBLE_ECC,             /**< Double ECC error on FPM ram access */
    e_FM_EX_QMI_SINGLE_ECC,             /**< Single ECC on QMI. */
    e_FM_EX_QMI_DOUBLE_ECC,             /**< Double bit ECC occurred on QMI */
    e_FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID,/**< Dequeue from unknown port id */
    e_FM_EX_BMI_LIST_RAM_ECC,           /**< Linked List RAM ECC error */
    e_FM_EX_BMI_STORAGE_PROFILE_ECC,    /**< Storage Profile ECC Error */
    e_FM_EX_BMI_STATISTICS_RAM_ECC,     /**< Statistics Count RAM ECC Error Enable */
    e_FM_EX_BMI_DISPATCH_RAM_ECC,       /**< Dispatch RAM ECC Error Enable */
    e_FM_EX_IRAM_ECC,                   /**< Double bit ECC occurred on IRAM*/
    e_FM_EX_MURAM_ECC                   /**< Double bit ECC occurred on MURAM*/
} e_FmExceptions;

/**************************************************************************//**
 @Description   Enum for defining port DMA swap mode
*//***************************************************************************/
typedef enum e_FmDmaSwapOption {
    e_FM_DMA_NO_SWP = FMAN_DMA_NO_SWP,          /**< No swap, transfer data as is.*/
    e_FM_DMA_SWP_PPC_LE = FMAN_DMA_SWP_PPC_LE,  /**< The transferred data should be swapped
                                                in PowerPc Little Endian mode. */
    e_FM_DMA_SWP_BE = FMAN_DMA_SWP_BE           /**< The transferred data should be swapped
                                                in Big Endian mode */
} e_FmDmaSwapOption;

/**************************************************************************//**
 @Description   Enum for defining port DMA cache attributes
*//***************************************************************************/
typedef enum e_FmDmaCacheOption {
    e_FM_DMA_NO_STASH = FMAN_DMA_NO_STASH,      /**< Cacheable, no Allocate (No Stashing) */
    e_FM_DMA_STASH = FMAN_DMA_STASH             /**< Cacheable and Allocate (Stashing on) */
} e_FmDmaCacheOption;


/**************************************************************************//**
 @Group         FM_init_grp FM Initialization Unit

 @Description   FM Initialization Unit

                Initialization Flow
                Initialization of the FM Module will be carried out by the application
                according to the following sequence:
                -  Calling the configuration routine with basic parameters.
                -  Calling the advance initialization routines to change driver's defaults.
                -  Calling the initialization routine.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      t_FmExceptionsCallback

 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App      - User's application descriptor.
 @Param[in]     exception  - The exception.
*//***************************************************************************/
typedef void (t_FmExceptionsCallback)(t_Handle          h_App,
                                      e_FmExceptions    exception);


/**************************************************************************//**
 @Function      t_FmBusErrorCallback

 @Description   Bus error user callback routine, will be called upon a
                bus error, passing parameters describing the errors and the owner.

 @Param[in]     h_App       - User's application descriptor.
 @Param[in]     portType    - Port type (e_FmPortType)
 @Param[in]     portId      - Port id - relative to type.
 @Param[in]     addr        - Address that caused the error
 @Param[in]     tnum        - Owner of error
 @Param[in]     liodn       - Logical IO device number
*//***************************************************************************/
typedef void (t_FmBusErrorCallback) (t_Handle        h_App,
                                     e_FmPortType    portType,
                                     uint8_t         portId,
                                     uint64_t        addr,
                                     uint8_t         tnum,
                                     uint16_t        liodn);

/**************************************************************************//**
 @Description   A structure for defining buffer prefix area content.
*//***************************************************************************/
typedef struct t_FmBufferPrefixContent {
    uint16_t    privDataSize;       /**< Number of bytes to be left at the beginning
                                         of the external buffer; Note that the private-area will
                                         start from the base of the buffer address. */
    bool        passPrsResult;      /**< TRUE to pass the parse result to/from the FM;
                                         User may use FM_PORT_GetBufferPrsResult() in order to
                                         get the parser-result from a buffer. */
    bool        passTimeStamp;      /**< TRUE to pass the timeStamp to/from the FM
                                         User may use FM_PORT_GetBufferTimeStamp() in order to
                                         get the parser-result from a buffer. */
    bool        passHashResult;     /**< TRUE to pass the KG hash result to/from the FM
                                         User may use FM_PORT_GetBufferHashResult() in order to
                                         get the parser-result from a buffer. */
    bool        passAllOtherPCDInfo;/**< Add all other Internal-Context information:
                                         AD, hash-result, key, etc. */
    uint16_t    dataAlign;          /**< 0 to use driver's default alignment [DEFAULT_FM_SP_bufferPrefixContent_dataAlign],
                                         other value for selecting a data alignment (must be a power of 2);
                                         if write optimization is used, must be >= 16. */
    uint8_t     manipExtraSpace;    /**< Maximum extra size needed (insertion-size minus removal-size);
                                         Note that this field impacts the size of the buffer-prefix
                                         (i.e. it pushes the data offset);
                                         This field is irrelevant if DPAA_VERSION==10 */
} t_FmBufferPrefixContent;

/**************************************************************************//**
 @Description   A structure of information about each of the external
                buffer pools used by a port or storage-profile.
*//***************************************************************************/
typedef struct t_FmExtPoolParams {
    uint8_t                 id;     /**< External buffer pool id */
    uint16_t                size;   /**< External buffer pool buffer size */
} t_FmExtPoolParams;

/**************************************************************************//**
 @Description   A structure for informing the driver about the external
                buffer pools allocated in the BM and used by a port or a
                storage-profile.
*//***************************************************************************/
typedef struct t_FmExtPools {
    uint8_t                 numOfPoolsUsed;     /**< Number of pools use by this port */
    t_FmExtPoolParams       extBufPool[FM_PORT_MAX_NUM_OF_EXT_POOLS];
                                                /**< Parameters for each port */
} t_FmExtPools;

/**************************************************************************//**
 @Description   A structure for defining backup BM Pools.
*//***************************************************************************/
typedef struct t_FmBackupBmPools {
    uint8_t     numOfBackupPools;       /**< Number of BM backup pools -
                                             must be smaller than the total number of
                                             pools defined for the specified port.*/
    uint8_t     poolIds[FM_PORT_MAX_NUM_OF_EXT_POOLS];
                                        /**< numOfBackupPools pool id's, specifying which
                                             pools should be used only as backup. Pool
                                             id's specified here must be a subset of the
                                             pools used by the specified port.*/
} t_FmBackupBmPools;

/**************************************************************************//**
 @Description   A structure for defining BM pool depletion criteria
*//***************************************************************************/
typedef struct t_FmBufPoolDepletion {
    bool        poolsGrpModeEnable;                 /**< select mode in which pause frames will be sent after
                                                         a number of pools (all together!) are depleted */
    uint8_t     numOfPools;                         /**< the number of depleted pools that will invoke
                                                         pause frames transmission. */
    bool        poolsToConsider[BM_MAX_NUM_OF_POOLS];
                                                    /**< For each pool, TRUE if it should be considered for
                                                         depletion (Note - this pool must be used by this port!). */
    bool        singlePoolModeEnable;               /**< select mode in which pause frames will be sent after
                                                         a single-pool is depleted; */
    bool        poolsToConsiderForSingleMode[BM_MAX_NUM_OF_POOLS];
                                                    /**< For each pool, TRUE if it should be considered for
                                                         depletion (Note - this pool must be used by this port!) */
#if (DPAA_VERSION >= 11)
    bool        pfcPrioritiesEn[FM_MAX_NUM_OF_PFC_PRIORITIES];
                                                    /**< This field is used by the MAC as the Priority Enable Vector in the PFC frame which is transmitted */
#endif /* (DPAA_VERSION >= 11) */
} t_FmBufPoolDepletion;

/**************************************************************************//**
 @Description   A Structure for defining Ucode patch for loading.
*//***************************************************************************/
typedef struct t_FmFirmwareParams {
    uint32_t                size;                   /**< Size of uCode */
    uint32_t                *p_Code;                /**< A pointer to the uCode */
} t_FmFirmwareParams;

/**************************************************************************//**
 @Description   A Structure for defining FM initialization parameters
*//***************************************************************************/
typedef struct t_FmParams {
    uint8_t                 fmId;                   /**< Index of the FM */
    uint8_t                 guestId;                /**< FM Partition Id */
    uintptr_t               baseAddr;               /**< A pointer to base of memory mapped FM registers (virtual);
                                                         this field is optional when the FM runs in "guest-mode"
                                                         (i.e. guestId != NCSW_MASTER_ID); in that case, the driver will
                                                         use the memory-map instead of calling the IPC where possible;
                                                         NOTE that this should include ALL common registers of the FM including
                                                         the PCD registers area (i.e. until the VSP pages - 880KB). */
    t_Handle                h_FmMuram;              /**< A handle of an initialized MURAM object,
                                                         to be used by the FM. */
    uint16_t                fmClkFreq;              /**< In Mhz;
                                                         Relevant when FM not runs in "guest-mode". */
    uint16_t                fmMacClkRatio;          /**< FM MAC Clock ratio, for backward comparability:
                                                                     when fmMacClkRatio = 0, ratio is 2:1
                                                                     when fmMacClkRatio = 1, ratio is 1:1  */
    t_FmExceptionsCallback  *f_Exception;           /**< An application callback routine to handle exceptions;
                                                         Relevant when FM not runs in "guest-mode". */
    t_FmBusErrorCallback    *f_BusError;            /**< An application callback routine to handle exceptions;
                                                         Relevant when FM not runs in "guest-mode". */
    t_Handle                h_App;                  /**< A handle to an application layer object; This handle will
                                                         be passed by the driver upon calling the above callbacks;
                                                         Relevant when FM not runs in "guest-mode". */
    uintptr_t               irq;                    /**< FM interrupt source for normal events;
                                                         Relevant when FM not runs in "guest-mode". */
    uintptr_t               errIrq;                 /**< FM interrupt source for errors;
                                                         Relevant when FM not runs in "guest-mode". */
    t_FmFirmwareParams      firmware;               /**< The firmware parameters structure;
                                                         Relevant when FM not runs in "guest-mode". */

#if (DPAA_VERSION >= 11)
    uintptr_t               vspBaseAddr;            /**< A pointer to base of memory mapped FM VSP registers (virtual);
                                                         i.e. up to 24KB, depending on the specific chip. */
    uint8_t                 partVSPBase;            /**< The first Virtual-Storage-Profile-id dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
    uint8_t                 partNumOfVSPs;          /**< Number of VSPs dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
#endif /* (DPAA_VERSION >= 11) */
} t_FmParams;


/**************************************************************************//**
 @Function      FM_Config

 @Description   Creates the FM module and returns its handle (descriptor).
                This descriptor must be passed as first parameter to all other
                FM function calls.

                No actual initialization or configuration of FM hardware is
                done by this routine. All FM parameters get default values that
                may be changed by calling one or more of the advance config routines.

 @Param[in]     p_FmParams  - A pointer to a data structure of mandatory FM parameters

 @Return        A handle to the FM object, or NULL for Failure.
*//***************************************************************************/
t_Handle FM_Config(t_FmParams *p_FmParams);

/**************************************************************************//**
 @Function      FM_Init

 @Description   Initializes the FM module by defining the software structure
                and configuring the hardware registers.

 @Param[in]     h_Fm - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_Init(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_Free

 @Description   Frees all resources that were assigned to FM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_Fm - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_Free(t_Handle h_Fm);


/**************************************************************************//**
 @Group         FM_advanced_init_grp    FM Advanced Configuration Unit

 @Description   Advanced configuration routines are optional routines that may
                be called in order to change the default driver settings.

                Note: Advanced configuration routines are not available for guest partition.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Enum for selecting DMA debug mode
*//***************************************************************************/
typedef enum e_FmDmaDbgCntMode {
    e_FM_DMA_DBG_NO_CNT             = 0,    /**< No counting */
    e_FM_DMA_DBG_CNT_DONE,                  /**< Count DONE commands */
    e_FM_DMA_DBG_CNT_COMM_Q_EM,             /**< count command queue emergency signals */
    e_FM_DMA_DBG_CNT_INT_READ_EM,           /**< Count Internal Read buffer emergency signal */
    e_FM_DMA_DBG_CNT_INT_WRITE_EM,          /**< Count Internal Write buffer emergency signal */
    e_FM_DMA_DBG_CNT_FPM_WAIT,              /**< Count FPM WAIT signal */
    e_FM_DMA_DBG_CNT_SIGLE_BIT_ECC,         /**< Single bit ECC errors. */
    e_FM_DMA_DBG_CNT_RAW_WAR_PROT           /**< Number of times there was a need for RAW & WAR protection. */
} e_FmDmaDbgCntMode;

/**************************************************************************//**
 @Description   Enum for selecting DMA Cache Override
*//***************************************************************************/
typedef enum e_FmDmaCacheOverride {
    e_FM_DMA_NO_CACHE_OR = 0,               /**< No override of the Cache field */
    e_FM_DMA_NO_STASH_DATA,                 /**< Data should not be stashed in system level cache */
    e_FM_DMA_MAY_STASH_DATA,                /**< Data may be stashed in system level cache */
    e_FM_DMA_STASH_DATA                     /**< Data should be stashed in system level cache */
} e_FmDmaCacheOverride;

/**************************************************************************//**
 @Description   Enum for selecting DMA External Bus Priority
*//***************************************************************************/
typedef enum e_FmDmaExtBusPri {
    e_FM_DMA_EXT_BUS_NORMAL = 0,            /**< Normal priority */
    e_FM_DMA_EXT_BUS_EBS,                   /**< AXI extended bus service priority */
    e_FM_DMA_EXT_BUS_SOS,                   /**< AXI sos priority */
    e_FM_DMA_EXT_BUS_EBS_AND_SOS            /**< AXI ebs + sos priority */
} e_FmDmaExtBusPri;

/**************************************************************************//**
 @Description   Enum for choosing the field that will be output on AID
*//***************************************************************************/
typedef enum e_FmDmaAidMode {
    e_FM_DMA_AID_OUT_PORT_ID = 0,           /**< 4 LSB of PORT_ID */
    e_FM_DMA_AID_OUT_TNUM                   /**< 4 LSB of TNUM */
} e_FmDmaAidMode;

/**************************************************************************//**
 @Description   Enum for selecting FPM Catastrophic error behavior
*//***************************************************************************/
typedef enum e_FmCatastrophicErr {
    e_FM_CATASTROPHIC_ERR_STALL_PORT = 0,   /**< Port_ID is stalled (only reset can release it) */
    e_FM_CATASTROPHIC_ERR_STALL_TASK        /**< Only erroneous task is stalled */
} e_FmCatastrophicErr;

/**************************************************************************//**
 @Description   Enum for selecting FPM DMA Error behavior
*//***************************************************************************/
typedef enum e_FmDmaErr {
    e_FM_DMA_ERR_CATASTROPHIC = 0,          /**< Dma error is treated as a catastrophic
                                                 error (e_FmCatastrophicErr)*/
    e_FM_DMA_ERR_REPORT                     /**< Dma error is just reported */
} e_FmDmaErr;

/**************************************************************************//**
 @Description   Enum for selecting DMA Emergency level by BMI emergency signal
*//***************************************************************************/
typedef enum e_FmDmaEmergencyLevel {
    e_FM_DMA_EM_EBS = 0,                    /**< EBS emergency */
    e_FM_DMA_EM_SOS                         /**< SOS emergency */
} e_FmDmaEmergencyLevel;

/**************************************************************************//**
 @Collection   Enum for selecting DMA Emergency options
*//***************************************************************************/
typedef uint32_t fmEmergencyBus_t;          /**< DMA emergency options */

#define  FM_DMA_MURAM_READ_EMERGENCY        0x00800000    /**< Enable emergency for MURAM1 */
#define  FM_DMA_MURAM_WRITE_EMERGENCY       0x00400000    /**< Enable emergency for MURAM2 */
#define  FM_DMA_EXT_BUS_EMERGENCY           0x00100000    /**< Enable emergency for external bus */
/* @} */

/**************************************************************************//**
 @Description   A structure for defining DMA emergency level
*//***************************************************************************/
typedef struct t_FmDmaEmergency {
    fmEmergencyBus_t        emergencyBusSelect;             /**< An OR of the busses where emergency
                                                                 should be enabled */
    e_FmDmaEmergencyLevel   emergencyLevel;                 /**< EBS/SOS */
} t_FmDmaEmergency;

/**************************************************************************//*
 @Description   structure for defining FM threshold
*//***************************************************************************/
typedef struct t_FmThresholds {
    uint8_t                 dispLimit;                      /**< The number of times a frames may
                                                                 be passed in the FM before assumed to
                                                                 be looping. */
    uint8_t                 prsDispTh;                      /**< This is the number pf packets that may be
                                                                 queued in the parser dispatch queue*/
    uint8_t                 plcrDispTh;                     /**< This is the number pf packets that may be
                                                                 queued in the policer dispatch queue*/
    uint8_t                 kgDispTh;                       /**< This is the number pf packets that may be
                                                                 queued in the keygen dispatch queue*/
    uint8_t                 bmiDispTh;                      /**< This is the number pf packets that may be
                                                                 queued in the BMI dispatch queue*/
    uint8_t                 qmiEnqDispTh;                   /**< This is the number pf packets that may be
                                                                 queued in the QMI enqueue dispatch queue*/
    uint8_t                 qmiDeqDispTh;                   /**< This is the number pf packets that may be
                                                                 queued in the QMI dequeue dispatch queue*/
    uint8_t                 fmCtl1DispTh;                   /**< This is the number pf packets that may be
                                                                 queued in fmCtl1 dispatch queue*/
    uint8_t                 fmCtl2DispTh;                   /**< This is the number pf packets that may be
                                                                 queued in fmCtl2 dispatch queue*/
} t_FmThresholds;

/**************************************************************************//*
 @Description   structure for defining DMA thresholds
*//***************************************************************************/
typedef struct t_FmDmaThresholds {
    uint8_t                     assertEmergency;            /**< When this value is reached,
                                                                 assert emergency (Threshold)*/
    uint8_t                     clearEmergency;             /**< After emergency is asserted, it is held
                                                                 until this value is reached (Hystheresis) */
} t_FmDmaThresholds;

/**************************************************************************//**
 @Function      t_FmResetOnInitOverrideCallback

 @Description   FMan specific reset on init user callback routine,
                will be used to override the standard FMan reset on init procedure

 @Param[in]     h_Fm  - FMan handler
*//***************************************************************************/
typedef void (t_FmResetOnInitOverrideCallback)(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_ConfigResetOnInit

 @Description   Define whether to reset the FM before initialization.
                Change the default configuration [DEFAULT_resetOnInit].

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     enable              When TRUE, FM will be reset before any initialization.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigResetOnInit(t_Handle h_Fm, bool enable);

/**************************************************************************//**
 @Function      FM_ConfigResetOnInitOverrideCallback

 @Description   Define a special reset of FM before initialization.
                Change the default configuration [DEFAULT_resetOnInitOverrideCallback].

 @Param[in]     h_Fm                	A handle to an FM Module.
 @Param[in]     f_ResetOnInitOverride   FM specific reset on init user callback routine.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigResetOnInitOverrideCallback(t_Handle h_Fm, t_FmResetOnInitOverrideCallback *f_ResetOnInitOverride);

/**************************************************************************//**
 @Function      FM_ConfigTotalFifoSize

 @Description   Define Total FIFO size for the whole FM.
                Calling this routine changes the total Fifo size in the internal driver
                data base from its default configuration [DEFAULT_totalFifoSize]

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     totalFifoSize       The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigTotalFifoSize(t_Handle h_Fm, uint32_t totalFifoSize);

 /**************************************************************************//**
 @Function      FM_ConfigDmaCacheOverride

 @Description   Define cache override mode.
                Calling this routine changes the cache override mode
                in the internal driver data base from its default configuration [DEFAULT_cacheOverride]

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     cacheOverride   The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaCacheOverride(t_Handle h_Fm, e_FmDmaCacheOverride cacheOverride);

/**************************************************************************//**
 @Function      FM_ConfigDmaAidOverride

 @Description   Define DMA AID override mode.
                Calling this routine changes the AID override mode
                in the internal driver data base from its default configuration  [DEFAULT_aidOverride]

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     aidOverride     The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaAidOverride(t_Handle h_Fm, bool aidOverride);

/**************************************************************************//**
 @Function      FM_ConfigDmaAidMode

 @Description   Define DMA AID  mode.
                Calling this routine changes the AID  mode in the internal
                driver data base from its default configuration [DEFAULT_aidMode]

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     aidMode         The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaAidMode(t_Handle h_Fm, e_FmDmaAidMode aidMode);

/**************************************************************************//**
 @Function      FM_ConfigDmaAxiDbgNumOfBeats

 @Description   Define DMA AXI number of beats.
                Calling this routine changes the AXI number of beats in the internal
                driver data base from its default configuration [DEFAULT_axiDbgNumOfBeats]

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     axiDbgNumOfBeats    The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaAxiDbgNumOfBeats(t_Handle h_Fm, uint8_t axiDbgNumOfBeats);

/**************************************************************************//**
 @Function      FM_ConfigDmaCamNumOfEntries

 @Description   Define number of CAM entries.
                Calling this routine changes the number of CAM entries in the internal
                driver data base from its default configuration [DEFAULT_dmaCamNumOfEntries].

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     numOfEntries    The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaCamNumOfEntries(t_Handle h_Fm, uint8_t numOfEntries);

/**************************************************************************//**
 @Function      FM_ConfigEnableCounters

 @Description   Obsolete, always return E_OK.

 @Param[in]     h_Fm    A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_ConfigEnableCounters(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_ConfigDmaDbgCounter

 @Description   Define DMA debug counter.
                Calling this routine changes the number of the DMA debug counter in the internal
                driver data base from its default configuration [DEFAULT_dmaDbgCntMode].

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     fmDmaDbgCntMode     An enum selecting the debug counter mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaDbgCounter(t_Handle h_Fm, e_FmDmaDbgCntMode fmDmaDbgCntMode);

/**************************************************************************//**
 @Function      FM_ConfigDmaStopOnBusErr

 @Description   Define bus error behavior.
                Calling this routine changes the bus error behavior definition
                in the internal driver data base from its default
                configuration [DEFAULT_dmaStopOnBusError].

 @Param[in]     h_Fm    A handle to an FM Module.
 @Param[in]     stop    TRUE to stop on bus error, FALSE to continue.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                Only if bus error is enabled.
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaStopOnBusErr(t_Handle h_Fm, bool stop);

/**************************************************************************//**
 @Function      FM_ConfigDmaEmergency

 @Description   Define DMA emergency.
                Calling this routine changes the DMA emergency definition
                in the internal driver data base from its default
                configuration where's it's disabled.

 @Param[in]     h_Fm        A handle to an FM Module.
 @Param[in]     p_Emergency An OR mask of all required options.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaEmergency(t_Handle h_Fm, t_FmDmaEmergency *p_Emergency);

/**************************************************************************//**
 @Function      FM_ConfigDmaErr

 @Description   DMA error treatment.
                Calling this routine changes the DMA error treatment
                in the internal driver data base from its default
                configuration [DEFAULT_dmaErr].

 @Param[in]     h_Fm    A handle to an FM Module.
 @Param[in]     dmaErr  The selected new choice.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaErr(t_Handle h_Fm, e_FmDmaErr dmaErr);

/**************************************************************************//**
 @Function      FM_ConfigCatastrophicErr

 @Description   Define FM behavior on catastrophic error.
                Calling this routine changes the FM behavior on catastrophic
                error in the internal driver data base from its default
                [DEFAULT_catastrophicErr].

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     catastrophicErr     The selected new choice.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigCatastrophicErr(t_Handle h_Fm, e_FmCatastrophicErr catastrophicErr);

/**************************************************************************//**
 @Function      FM_ConfigEnableMuramTestMode

 @Description   Enable MURAM test mode.
                Calling this routine changes the internal driver data base
                from its default selection of test mode where it's disabled.
                This routine is only avaiable on old FM revisions (FMan v2).

 @Param[in]     h_Fm    A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigEnableMuramTestMode(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_ConfigEnableIramTestMode

 @Description   Enable IRAM test mode.
                Calling this routine changes the internal driver data base
                from its default selection of test mode where it's disabled.
                This routine is only avaiable on old FM revisions (FMan v2).

 @Param[in]     h_Fm    A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigEnableIramTestMode(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_ConfigHaltOnExternalActivation

 @Description   Define FM behavior on external halt activation.
                Calling this routine changes the FM behavior on external halt
                activation in the internal driver data base from its default
                [DEFAULT_haltOnExternalActivation].

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     enable          TRUE to enable halt on external halt
                                activation.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigHaltOnExternalActivation(t_Handle h_Fm, bool enable);

/**************************************************************************//**
 @Function      FM_ConfigHaltOnUnrecoverableEccError

 @Description   Define FM behavior on external halt activation.
                Calling this routine changes the FM behavior on unrecoverable
                ECC error in the internal driver data base from its default
                [DEFAULT_haltOnUnrecoverableEccError].
                This routine is only avaiable on old FM revisions (FMan v2).

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     enable          TRUE to enable halt on unrecoverable Ecc error

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigHaltOnUnrecoverableEccError(t_Handle h_Fm, bool enable);

/**************************************************************************//**
 @Function      FM_ConfigException

 @Description   Define FM exceptions.
                Calling this routine changes the exceptions defaults in the
                internal driver data base where all exceptions are enabled.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigException(t_Handle h_Fm, e_FmExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_ConfigExternalEccRamsEnable

 @Description   Select external ECC enabling.
                Calling this routine changes the ECC enabling control in the internal
                driver data base from its default [DEFAULT_externalEccRamsEnable].
                When this option is enabled Rams ECC enabling is not effected
                by FM_EnableRamsEcc/FM_DisableRamsEcc, but by a JTAG.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     enable          TRUE to enable this option.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigExternalEccRamsEnable(t_Handle h_Fm, bool enable);

/**************************************************************************//**
 @Function      FM_ConfigTnumAgingPeriod

 @Description   Define Tnum aging period.
                Calling this routine changes the Tnum aging of dequeue TNUMs
                in the QMI in the internal driver data base from its default
                [DEFAULT_tnumAgingPeriod].

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     tnumAgingPeriod     Tnum Aging Period in microseconds.
                                    Note that period is recalculated in units of
                                    64 FM clocks. Driver will pick the closest
                                    possible period.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
                NOTE that if some MAC is configured for PFC, '0' value is NOT
                allowed.
*//***************************************************************************/
t_Error FM_ConfigTnumAgingPeriod(t_Handle h_Fm, uint16_t tnumAgingPeriod);

/**************************************************************************//*
 @Function      FM_ConfigDmaEmergencySmoother

 @Description   Define DMA emergency smoother.
                Calling this routine changes the definition of the minimum
                amount of DATA beats transferred on the AXI READ and WRITE
                ports before lowering the emergency level.
                By default smoother is disabled.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     emergencyCnt    emergency switching counter.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaEmergencySmoother(t_Handle h_Fm, uint32_t emergencyCnt);

/**************************************************************************//*
 @Function      FM_ConfigThresholds

 @Description   Calling this routine changes the internal driver data base
                from its default FM threshold configuration:
                    dispLimit:    [DEFAULT_dispLimit]
                    prsDispTh:    [DEFAULT_prsDispTh]
                    plcrDispTh:   [DEFAULT_plcrDispTh]
                    kgDispTh:     [DEFAULT_kgDispTh]
                    bmiDispTh:    [DEFAULT_bmiDispTh]
                    qmiEnqDispTh: [DEFAULT_qmiEnqDispTh]
                    qmiDeqDispTh: [DEFAULT_qmiDeqDispTh]
                    fmCtl1DispTh: [DEFAULT_fmCtl1DispTh]
                    fmCtl2DispTh: [DEFAULT_fmCtl2DispTh]


 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     p_FmThresholds  A structure of threshold parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigThresholds(t_Handle h_Fm, t_FmThresholds *p_FmThresholds);

/**************************************************************************//*
 @Function      FM_ConfigDmaSosEmergencyThreshold

 @Description   Calling this routine changes the internal driver data base
                from its default dma SOS emergency configuration [DEFAULT_dmaSosEmergency]

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     dmaSosEmergency     The selected new value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaSosEmergencyThreshold(t_Handle h_Fm, uint32_t dmaSosEmergency);

/**************************************************************************//*
 @Function      FM_ConfigDmaWriteBufThresholds

 @Description   Calling this routine changes the internal driver data base
                from its default configuration of DMA write buffer threshold
                assertEmergency: [DEFAULT_dmaWriteIntBufLow]
                clearEmergency:  [DEFAULT_dmaWriteIntBufHigh]
                This routine is only avaiable on old FM revisions (FMan v2).

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     p_FmDmaThresholds   A structure of thresholds to define emergency behavior -
                                    When 'assertEmergency' value is reached, emergency is asserted,
                                    then it is held until 'clearEmergency' value is reached.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaWriteBufThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds);

 /**************************************************************************//*
 @Function      FM_ConfigDmaCommQThresholds

 @Description   Calling this routine changes the internal driver data base
                from its default configuration of DMA command queue threshold
                assertEmergency: [DEFAULT_dmaCommQLow]
                clearEmergency:  [DEFAULT_dmaCommQHigh]

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     p_FmDmaThresholds   A structure of thresholds to define emergency behavior -
                                    When 'assertEmergency' value is reached, emergency is asserted,
                                    then it is held until 'clearEmergency' value is reached..

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaCommQThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds);

/**************************************************************************//*
 @Function      FM_ConfigDmaReadBufThresholds

 @Description   Calling this routine changes the internal driver data base
                from its default configuration of DMA read buffer threshold
                assertEmergency: [DEFAULT_dmaReadIntBufLow]
                clearEmergency:  [DEFAULT_dmaReadIntBufHigh]
                This routine is only avaiable on old FM revisions (FMan v2).

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     p_FmDmaThresholds   A structure of thresholds to define emergency behavior -
                                    When 'assertEmergency' value is reached, emergency is asserted,
                                    then it is held until 'clearEmergency' value is reached..

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaReadBufThresholds(t_Handle h_Fm, t_FmDmaThresholds *p_FmDmaThresholds);

/**************************************************************************//*
 @Function      FM_ConfigDmaWatchdog

 @Description   Calling this routine changes the internal driver data base
                from its default watchdog configuration, which is disabled
                [DEFAULT_dmaWatchdog].

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     watchDogValue   The selected new value - in microseconds.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ConfigDmaWatchdog(t_Handle h_Fm, uint32_t watchDogValue);

/** @} */ /* end of FM_advanced_init_grp group */
/** @} */ /* end of FM_init_grp group */


/**************************************************************************//**
 @Group         FM_runtime_control_grp FM Runtime Control Unit

 @Description   FM Runtime control unit API functions, definitions and enums.
                The FM driver provides a set of control routines.
                These routines may only be called after the module was fully
                initialized (both configuration and initialization routines were
                called). They are typically used to get information from hardware
                (status, counters/statistics, revision etc.), to modify a current
                state or to force/enable a required action. Run-time control may
                be called whenever necessary and as many times as needed.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Collection   General FM defines.
*//***************************************************************************/
#define FM_MAX_NUM_OF_VALID_PORTS   (FM_MAX_NUM_OF_OH_PORTS +       \
                                     FM_MAX_NUM_OF_1G_RX_PORTS +    \
                                     FM_MAX_NUM_OF_10G_RX_PORTS +   \
                                     FM_MAX_NUM_OF_1G_TX_PORTS +    \
                                     FM_MAX_NUM_OF_10G_TX_PORTS)      /**< Number of available FM ports */
/* @} */

/**************************************************************************//*
 @Description   A Structure for Port bandwidth requirement. Port is identified
                by type and relative id.
*//***************************************************************************/
typedef struct t_FmPortBandwidth {
    e_FmPortType        type;           /**< FM port type */
    uint8_t             relativePortId; /**< Type relative port id */
    uint8_t             bandwidth;      /**< bandwidth - (in term of percents) */
} t_FmPortBandwidth;

/**************************************************************************//*
 @Description   A Structure containing an array of Port bandwidth requirements.
                The user should state the ports requiring bandwidth in terms of
                percentage - i.e. all port's bandwidths in the array must add
                up to 100.
*//***************************************************************************/
typedef struct t_FmPortsBandwidthParams {
    uint8_t             numOfPorts;         /**< The number of relevant ports, which is the
                                                 number of valid entries in the array below */
    t_FmPortBandwidth   portsBandwidths[FM_MAX_NUM_OF_VALID_PORTS];
                                            /**< for each port, it's bandwidth (all port's
                                                 bandwidths must add up to 100.*/
} t_FmPortsBandwidthParams;

/**************************************************************************//**
 @Description   DMA Emergency control on MURAM
*//***************************************************************************/
typedef enum e_FmDmaMuramPort {
    e_FM_DMA_MURAM_PORT_WRITE,              /**< MURAM write port */
    e_FM_DMA_MURAM_PORT_READ                /**< MURAM read port */
} e_FmDmaMuramPort;

/**************************************************************************//**
 @Description   Enum for defining FM counters
*//***************************************************************************/
typedef enum e_FmCounters {
    e_FM_COUNTERS_ENQ_TOTAL_FRAME = 0,              /**< QMI total enqueued frames counter */
    e_FM_COUNTERS_DEQ_TOTAL_FRAME,                  /**< QMI total dequeued frames counter */
    e_FM_COUNTERS_DEQ_0,                            /**< QMI 0 frames from QMan counter */
    e_FM_COUNTERS_DEQ_1,                            /**< QMI 1 frames from QMan counter */
    e_FM_COUNTERS_DEQ_2,                            /**< QMI 2 frames from QMan counter */
    e_FM_COUNTERS_DEQ_3,                            /**< QMI 3 frames from QMan counter */
    e_FM_COUNTERS_DEQ_FROM_DEFAULT,                 /**< QMI dequeue from default queue counter */
    e_FM_COUNTERS_DEQ_FROM_CONTEXT,                 /**< QMI dequeue from FQ context counter */
    e_FM_COUNTERS_DEQ_FROM_FD,                      /**< QMI dequeue from FD command field counter */
    e_FM_COUNTERS_DEQ_CONFIRM                       /**< QMI dequeue confirm counter */
} e_FmCounters;

/**************************************************************************//**
 @Description   A Structure for returning FM revision information
*//***************************************************************************/
typedef struct t_FmRevisionInfo {
    uint8_t         majorRev;               /**< Major revision */
    uint8_t         minorRev;               /**< Minor revision */
} t_FmRevisionInfo;

/**************************************************************************//**
 @Description   A Structure for returning FM ctrl code revision information
*//***************************************************************************/
typedef struct t_FmCtrlCodeRevisionInfo {
    uint16_t        packageRev;             /**< Package revision */
    uint8_t         majorRev;               /**< Major revision */
    uint8_t         minorRev;               /**< Minor revision */
} t_FmCtrlCodeRevisionInfo;

/**************************************************************************//**
 @Description   A Structure for defining DMA status
*//***************************************************************************/
typedef struct t_FmDmaStatus {
    bool    cmqNotEmpty;            /**< Command queue is not empty */
    bool    busError;               /**< Bus error occurred */
    bool    readBufEccError;        /**< Double ECC error on buffer Read (Valid for FM rev < 6)*/
    bool    writeBufEccSysError;    /**< Double ECC error on buffer write from system side (Valid for FM rev < 6)*/
    bool    writeBufEccFmError;     /**< Double ECC error on buffer write from FM side (Valid for FM rev < 6) */
    bool    singlePortEccError;     /**< Single Port ECC error from FM side (Valid for FM rev >= 6)*/
} t_FmDmaStatus;

/**************************************************************************//**
 @Description   A Structure for obtaining FM controller monitor values
*//***************************************************************************/
typedef struct t_FmCtrlMon {
    uint8_t percentCnt[2];          /**< Percentage value */
} t_FmCtrlMon;


#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      FM_DumpRegs

 @Description   Dumps all FM registers

 @Param[in]     h_Fm      A handle to an FM Module.

 @Return        E_OK on success;

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FM_DumpRegs(t_Handle h_Fm);
#endif /* (defined(DEBUG_ERRORS) && ... */

/**************************************************************************//**
 @Function      FM_SetException

 @Description   Calling this routine enables/disables the specified exception.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_SetException(t_Handle h_Fm, e_FmExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_EnableRamsEcc

 @Description   Enables ECC mechanism for all the different FM RAM's; E.g. IRAM,
                MURAM, Parser, Keygen, Policer, etc.
                Note:
                If FM_ConfigExternalEccRamsEnable was called to enable external
                setting of ECC, this routine effects IRAM ECC only.
                This routine is also called by the driver if an ECC exception is
                enabled.

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_EnableRamsEcc(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_DisableRamsEcc

 @Description   Disables ECC mechanism for all the different FM RAM's; E.g. IRAM,
                MURAM, Parser, Keygen, Policer, etc.
                Note:
                If FM_ConfigExternalEccRamsEnable was called to enable external
                setting of ECC, this routine effects IRAM ECC only.
                In opposed to FM_EnableRamsEcc, this routine must be called
                explicitly to disable all Rams ECC.

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Config() and before FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_DisableRamsEcc(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_GetRevision

 @Description   Returns the FM revision

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[out]    p_FmRevisionInfo    A structure of revision information parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error  FM_GetRevision(t_Handle h_Fm, t_FmRevisionInfo *p_FmRevisionInfo);

/**************************************************************************//**
 @Function      FM_GetFmanCtrlCodeRevision

 @Description   Returns the Fman controller code revision

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[out]    p_RevisionInfo      A structure of revision information parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FM_GetFmanCtrlCodeRevision(t_Handle h_Fm, t_FmCtrlCodeRevisionInfo *p_RevisionInfo);

/**************************************************************************//**
 @Function      FM_GetCounter

 @Description   Reads one of the FM counters.

 @Param[in]     h_Fm        A handle to an FM Module.
 @Param[in]     counter     The requested counter.

 @Return        Counter's current value.

 @Cautions      Allowed only following FM_Init().
                Note that it is user's responsibility to call this routine only
                for enabled counters, and there will be no indication if a
                disabled counter is accessed.
*//***************************************************************************/
uint32_t  FM_GetCounter(t_Handle h_Fm, e_FmCounters counter);

/**************************************************************************//**
 @Function      FM_ModifyCounter

 @Description   Sets a value to an enabled counter. Use "0" to reset the counter.

 @Param[in]     h_Fm        A handle to an FM Module.
 @Param[in]     counter     The requested counter.
 @Param[in]     val         The requested value to be written into the counter.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error  FM_ModifyCounter(t_Handle h_Fm, e_FmCounters counter, uint32_t val);

/**************************************************************************//**
 @Function      FM_Resume

 @Description   Release FM after halt FM command or after unrecoverable ECC error.

 @Param[in]     h_Fm        A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void FM_Resume(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_SetDmaEmergency

 @Description   Manual emergency set

 @Param[in]     h_Fm        A handle to an FM Module.
 @Param[in]     muramPort   MURAM direction select.
 @Param[in]     enable      TRUE to manually enable emergency, FALSE to disable.

 @Return        None.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void FM_SetDmaEmergency(t_Handle h_Fm, e_FmDmaMuramPort muramPort, bool enable);

/**************************************************************************//**
 @Function      FM_SetDmaExtBusPri

 @Description   Set the DMA external bus priority

 @Param[in]     h_Fm    A handle to an FM Module.
 @Param[in]     pri     External bus priority select

 @Return        None.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void FM_SetDmaExtBusPri(t_Handle h_Fm, e_FmDmaExtBusPri pri);

/**************************************************************************//**
 @Function      FM_GetDmaStatus

 @Description   Reads the DMA current status

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[out]    p_FmDmaStatus   A structure of DMA status parameters.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
void FM_GetDmaStatus(t_Handle h_Fm, t_FmDmaStatus *p_FmDmaStatus);

/**************************************************************************//**
 @Function      FM_ErrorIsr

 @Description   FM interrupt-service-routine for errors.

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; E_EMPTY if no errors found in register, other
                error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ErrorIsr(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_EventIsr

 @Description   FM interrupt-service-routine for normal events.

 @Param[in]     h_Fm            A handle to an FM Module.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void FM_EventIsr(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_GetSpecialOperationCoding

 @Description   Return a specific coding according to the input mask.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     spOper          special operation mask.
 @Param[out]    p_SpOperCoding  special operation code.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
*//***************************************************************************/
t_Error FM_GetSpecialOperationCoding(t_Handle               h_Fm,
                                     fmSpecialOperations_t  spOper,
                                     uint8_t                *p_SpOperCoding);

/**************************************************************************//**
 @Function      FM_CtrlMonStart

 @Description   Start monitoring utilization of all available FM controllers.

                In order to obtain FM controllers utilization the following sequence
                should be used:
                -# FM_CtrlMonStart()
                -# FM_CtrlMonStop()
                -# FM_CtrlMonGetCounters() - issued for each FM controller

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID).
*//***************************************************************************/
t_Error FM_CtrlMonStart(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_CtrlMonStop

 @Description   Stop monitoring utilization of all available FM controllers.

                In order to obtain FM controllers utilization the following sequence
                should be used:
                -# FM_CtrlMonStart()
                -# FM_CtrlMonStop()
                -# FM_CtrlMonGetCounters() - issued for each FM controller

 @Param[in]     h_Fm            A handle to an FM Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID).
*//***************************************************************************/
t_Error FM_CtrlMonStop(t_Handle h_Fm);

/**************************************************************************//**
 @Function      FM_CtrlMonGetCounters

 @Description   Obtain FM controller utilization parameters.

                In order to obtain FM controllers utilization the following sequence
                should be used:
                -# FM_CtrlMonStart()
                -# FM_CtrlMonStop()
                -# FM_CtrlMonGetCounters() - issued for each FM controller

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     fmCtrlIndex     FM Controller index for that utilization results
                                are requested.
 @Param[in]     p_Mon           Pointer to utilization results structure.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID).
*//***************************************************************************/
t_Error FM_CtrlMonGetCounters(t_Handle h_Fm, uint8_t fmCtrlIndex, t_FmCtrlMon *p_Mon);


/**************************************************************************//*
 @Function      FM_ForceIntr

 @Description   Causes an interrupt event on the requested source.

 @Param[in]     h_Fm            A handle to an FM Module.
 @Param[in]     exception       An exception to be forced.

 @Return        E_OK on success; Error code if the exception is not enabled,
                or is not able to create interrupt.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_ForceIntr (t_Handle h_Fm, e_FmExceptions exception);

/**************************************************************************//*
 @Function      FM_SetPortsBandwidth

 @Description   Sets relative weights between ports when accessing common resources.

 @Param[in]     h_Fm                A handle to an FM Module.
 @Param[in]     p_PortsBandwidth    A structure of ports bandwidths in percentage, i.e.
                                    total must equal 100.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_SetPortsBandwidth(t_Handle h_Fm, t_FmPortsBandwidthParams *p_PortsBandwidth);

/**************************************************************************//*
 @Function      FM_GetMuramHandle

 @Description   Gets the corresponding MURAM handle

 @Param[in]     h_Fm                A handle to an FM Module.

 @Return        MURAM handle; NULL otherwise.

 @Cautions      Allowed only following FM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Handle FM_GetMuramHandle(t_Handle h_Fm);

/** @} */ /* end of FM_runtime_control_grp group */
/** @} */ /* end of FM_lib_grp group */
/** @} */ /* end of FM_grp group */


#ifdef NCSW_BACKWARD_COMPATIBLE_API
typedef t_FmFirmwareParams          t_FmPcdFirmwareParams;
typedef t_FmBufferPrefixContent     t_FmPortBufferPrefixContent;
typedef t_FmExtPoolParams           t_FmPortExtPoolParams;
typedef t_FmExtPools                t_FmPortExtPools;
typedef t_FmBackupBmPools           t_FmPortBackupBmPools;
typedef t_FmBufPoolDepletion        t_FmPortBufPoolDepletion;
typedef e_FmDmaSwapOption           e_FmPortDmaSwapOption;
typedef e_FmDmaCacheOption          e_FmPortDmaCacheOption;

#define FM_CONTEXTA_GET_OVVERIDE    FM_CONTEXTA_GET_OVERRIDE
#define FM_CONTEXTA_SET_OVVERIDE    FM_CONTEXTA_SET_OVERRIDE

#define e_FM_EX_BMI_PIPELINE_ECC    e_FM_EX_BMI_STORAGE_PROFILE_ECC
#define e_FM_PORT_DMA_NO_SWP        e_FM_DMA_NO_SWP
#define e_FM_PORT_DMA_SWP_PPC_LE    e_FM_DMA_SWP_PPC_LE
#define e_FM_PORT_DMA_SWP_BE        e_FM_DMA_SWP_BE
#define e_FM_PORT_DMA_NO_STASH      e_FM_DMA_NO_STASH
#define e_FM_PORT_DMA_STASH         e_FM_DMA_STASH
#endif /* NCSW_BACKWARD_COMPATIBLE_API */


#endif /* __FM_EXT */
