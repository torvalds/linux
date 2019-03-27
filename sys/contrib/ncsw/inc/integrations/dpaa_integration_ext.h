/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 **************************************************************************/
/**

 @File          dpaa_integration_ext.h

 @Description   P5020 FM external definitions and structures.
*//***************************************************************************/
#ifndef __DPAA_INTEGRATION_EXT_H
#define __DPAA_INTEGRATION_EXT_H

#include "std_ext.h"


/**************************************************************************//**
 @Description   DPAA SW Portals Enumeration.
*//***************************************************************************/
typedef enum
{
    e_DPAA_SWPORTAL0 = 0,
    e_DPAA_SWPORTAL1,
    e_DPAA_SWPORTAL2,
    e_DPAA_SWPORTAL3,
    e_DPAA_SWPORTAL4,
    e_DPAA_SWPORTAL5,
    e_DPAA_SWPORTAL6,
    e_DPAA_SWPORTAL7,
    e_DPAA_SWPORTAL8,
    e_DPAA_SWPORTAL9,
    e_DPAA_SWPORTAL_DUMMY_LAST
} e_DpaaSwPortal;

/**************************************************************************//**
 @Description   DPAA Direct Connect Portals Enumeration.
*//***************************************************************************/
typedef enum
{
    e_DPAA_DCPORTAL0 = 0,
    e_DPAA_DCPORTAL1,
    e_DPAA_DCPORTAL2,
    e_DPAA_DCPORTAL3,
    e_DPAA_DCPORTAL4,
    e_DPAA_DCPORTAL_DUMMY_LAST
} e_DpaaDcPortal;

#define DPAA_MAX_NUM_OF_SW_PORTALS      e_DPAA_SWPORTAL_DUMMY_LAST
#define DPAA_MAX_NUM_OF_DC_PORTALS      e_DPAA_DCPORTAL_DUMMY_LAST

/*****************************************************************************
 QMan INTEGRATION-SPECIFIC DEFINITIONS
******************************************************************************/
#define QM_MAX_NUM_OF_POOL_CHANNELS     15      /**< Total number of channels, dedicated and pool */
#define QM_MAX_NUM_OF_WQ                8       /**< Number of work queues per channel */
#define QM_MAX_NUM_OF_CGS               256     /**< Congestion groups number */
#define QM_MAX_NUM_OF_FQIDS             (16 * MEGABYTE)
                                                /**< FQIDs range - 24 bits */

/**************************************************************************//**
 @Description   Work Queue Channel assignments in QMan.
*//***************************************************************************/
typedef enum
{
    e_QM_FQ_CHANNEL_SWPORTAL0 = 0,              /**< Dedicated channels serviced by software portals 0 to 9 */
    e_QM_FQ_CHANNEL_SWPORTAL1,
    e_QM_FQ_CHANNEL_SWPORTAL2,
    e_QM_FQ_CHANNEL_SWPORTAL3,
    e_QM_FQ_CHANNEL_SWPORTAL4,
    e_QM_FQ_CHANNEL_SWPORTAL5,
    e_QM_FQ_CHANNEL_SWPORTAL6,
    e_QM_FQ_CHANNEL_SWPORTAL7,
    e_QM_FQ_CHANNEL_SWPORTAL8,
    e_QM_FQ_CHANNEL_SWPORTAL9,

    e_QM_FQ_CHANNEL_POOL1 = 0x21,               /**< Pool channels that can be serviced by any of the software portals */
    e_QM_FQ_CHANNEL_POOL2,
    e_QM_FQ_CHANNEL_POOL3,
    e_QM_FQ_CHANNEL_POOL4,
    e_QM_FQ_CHANNEL_POOL5,
    e_QM_FQ_CHANNEL_POOL6,
    e_QM_FQ_CHANNEL_POOL7,
    e_QM_FQ_CHANNEL_POOL8,
    e_QM_FQ_CHANNEL_POOL9,
    e_QM_FQ_CHANNEL_POOL10,
    e_QM_FQ_CHANNEL_POOL11,
    e_QM_FQ_CHANNEL_POOL12,
    e_QM_FQ_CHANNEL_POOL13,
    e_QM_FQ_CHANNEL_POOL14,
    e_QM_FQ_CHANNEL_POOL15,

    e_QM_FQ_CHANNEL_FMAN0_SP0 = 0x40,           /**< Dedicated channels serviced by Direct Connect Portal 0:
                                                     connected to FMan 0; assigned in incrementing order to
                                                     each sub-portal (SP) in the portal */
    e_QM_FQ_CHANNEL_FMAN0_SP1,
    e_QM_FQ_CHANNEL_FMAN0_SP2,
    e_QM_FQ_CHANNEL_FMAN0_SP3,
    e_QM_FQ_CHANNEL_FMAN0_SP4,
    e_QM_FQ_CHANNEL_FMAN0_SP5,
    e_QM_FQ_CHANNEL_FMAN0_SP6,
    e_QM_FQ_CHANNEL_FMAN0_SP7,
    e_QM_FQ_CHANNEL_FMAN0_SP8,
    e_QM_FQ_CHANNEL_FMAN0_SP9,
    e_QM_FQ_CHANNEL_FMAN0_SP10,
    e_QM_FQ_CHANNEL_FMAN0_SP11,

    e_QM_FQ_CHANNEL_RMAN_SP2 = 0x62,            /**< Dedicated channels serviced by Direct Connect Portal 1: connected to RMan */
    e_QM_FQ_CHANNEL_RMAN_SP3,

    e_QM_FQ_CHANNEL_CAAM = 0x80,                /**< Dedicated channel serviced by Direct Connect Portal 2:
                                                     connected to SEC 4.x */

    e_QM_FQ_CHANNEL_PME = 0xA0,                 /**< Dedicated channel serviced by Direct Connect Portal 3:
                                                     connected to PME */
    e_QM_FQ_CHANNEL_RAID = 0xC0                 /**< Dedicated channel serviced by Direct Connect Portal 4:
                                                     connected to RAID */
} e_QmFQChannel;

/*****************************************************************************
 BMan INTEGRATION-SPECIFIC DEFINITIONS
******************************************************************************/
#define BM_MAX_NUM_OF_POOLS         64          /**< Number of buffers pools */

/*****************************************************************************
 FM INTEGRATION-SPECIFIC DEFINITIONS
******************************************************************************/
#define INTG_MAX_NUM_OF_FM          1

/* Ports defines */
#define FM_MAX_NUM_OF_1G_MACS       5
#define FM_MAX_NUM_OF_10G_MACS      1
#define FM_MAX_NUM_OF_MACS          (FM_MAX_NUM_OF_1G_MACS + FM_MAX_NUM_OF_10G_MACS)
#define FM_MAX_NUM_OF_OH_PORTS      7

#define FM_MAX_NUM_OF_1G_RX_PORTS   FM_MAX_NUM_OF_1G_MACS
#define FM_MAX_NUM_OF_10G_RX_PORTS  FM_MAX_NUM_OF_10G_MACS
#define FM_MAX_NUM_OF_RX_PORTS      (FM_MAX_NUM_OF_10G_RX_PORTS + FM_MAX_NUM_OF_1G_RX_PORTS)

#define FM_MAX_NUM_OF_1G_TX_PORTS   FM_MAX_NUM_OF_1G_MACS
#define FM_MAX_NUM_OF_10G_TX_PORTS  FM_MAX_NUM_OF_10G_MACS
#define FM_MAX_NUM_OF_TX_PORTS      (FM_MAX_NUM_OF_10G_TX_PORTS + FM_MAX_NUM_OF_1G_TX_PORTS)

#define FM_PORT_MAX_NUM_OF_EXT_POOLS            8           /**< Number of external BM pools per Rx port */
#define FM_PORT_NUM_OF_CONGESTION_GRPS          256         /**< Total number of congestion groups in QM */
#define FM_MAX_NUM_OF_SUB_PORTALS               12
#define FM_PORT_MAX_NUM_OF_OBSERVED_EXT_POOLS   0

/* RAMs defines */
#define FM_MURAM_SIZE                   (160 * KILOBYTE)
#define FM_IRAM_SIZE(a,b)               ( 64 * KILOBYTE)

/* PCD defines */
#define FM_PCD_PLCR_NUM_ENTRIES         256                 /**< Total number of policer profiles */
#define FM_PCD_KG_NUM_OF_SCHEMES        32                  /**< Total number of KG schemes */
#define FM_PCD_MAX_NUM_OF_CLS_PLANS     256                 /**< Number of classification plan entries. */

/* RTC defines */
#define FM_RTC_NUM_OF_ALARMS            2                   /**< RTC number of alarms */
#define FM_RTC_NUM_OF_PERIODIC_PULSES   2                   /**< RTC number of periodic pulses */
#define FM_RTC_NUM_OF_EXT_TRIGGERS      2                   /**< RTC number of external triggers */

/* QMI defines */
#define QMI_MAX_NUM_OF_TNUMS            64
#define MAX_QMI_DEQ_SUBPORTAL           12
#define QMI_DEF_TNUMS_THRESH            48

/* FPM defines */
#define FM_NUM_OF_FMAN_CTRL_EVENT_REGS  4

/* DMA defines */
#define DMA_THRESH_MAX_COMMQ            31
#define DMA_THRESH_MAX_BUF              127

/* BMI defines */
#define BMI_MAX_NUM_OF_TASKS            128
#define BMI_MAX_NUM_OF_DMAS             32
#define BMI_MAX_FIFO_SIZE               (FM_MURAM_SIZE)
#define PORT_MAX_WEIGHT                 16


#define FM_CHECK_PORT_RESTRICTIONS(__validPorts, __newPortIndx)   TRUE

/* P5020 unique features */
#define FM_QMI_DEQ_OPTIONS_SUPPORT
#define FM_NO_DISPATCH_RAM_ECC
#define FM_FIFO_ALLOCATION_OLD_ALG
#define FM_NO_WATCHDOG
#define FM_NO_TNUM_AGING
#define FM_NO_TGEC_LOOPBACK
#define FM_KG_NO_BYPASS_FQID_GEN
#define FM_KG_NO_BYPASS_PLCR_PROFILE_GEN
#define FM_NO_BACKUP_POOLS
#define FM_NO_OP_OBSERVED_POOLS
#define FM_NO_ADVANCED_RATE_LIMITER
#define FM_NO_OP_OBSERVED_CGS

/* FM erratas (P5020, P3041) */
#define FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
#define FM_TX_SHORT_FRAME_BAD_TS_ERRATA_10GMAC_A006     /* No implementation, Out of LLD scope */
#define FM_TX_FIFO_CORRUPTION_ERRATA_10GMAC_A007
#define FM_ECC_HALT_NO_SYNC_ERRATA_10GMAC_A008

#define FM_NO_RX_PREAM_ERRATA_DTSECx1
#define FM_GRS_ERRATA_DTSEC_A002
#define FM_BAD_TX_TS_IN_B_2_B_ERRATA_DTSEC_A003
#define FM_GTS_ERRATA_DTSEC_A004
#define FM_PAUSE_BLOCK_ERRATA_DTSEC_A006                        /* do nothing */
#define FM_RESERVED_ACCESS_TO_DISABLED_DEV_ERRATA_DTSEC_A0011   /* do nothing */
#define FM_GTS_AFTER_MAC_ABORTED_FRAME_ERRATA_DTSEC_A0012       FM_GTS_ERRATA_DTSEC_A004
#define FM_10_100_SGMII_NO_TS_ERRATA_DTSEC3
#define FM_TX_LOCKUP_ERRATA_DTSEC6

#define FM_IM_TX_SYNC_SKIP_TNUM_ERRATA_FMAN_A001                /* Implemented by ucode */
#define FM_HC_DEF_FQID_ONLY_ERRATA_FMAN_A003                    /* Implemented by ucode */
#define FM_IM_TX_SHARED_TNUM_ERRATA_FMAN4                       /* Implemented by ucode */
#define FM_IM_GS_DEADLOCK_ERRATA_FMAN5                          /* Implemented by ucode */
#define FM_IM_DEQ_PIPELINE_DEPTH_ERRATA_FMAN10                  /* Implemented by ucode */
#define FM_CC_GEN6_MISSMATCH_ERRATA_FMAN12                      /* Implemented by ucode */
#define FM_CC_CHANGE_SHARED_TNUM_ERRATA_FMAN13                  /* Implemented by ucode */
#define FM_IM_LARGE_MRBLR_ERRATA_FMAN15                         /* Implemented by ucode */
#define FM_BMI_TO_RISC_ENQ_ERRATA_FMANc                         /* No implementation, Out of LLD scope */
#define FM_INVALID_SWPRS_DATA_ERRATA_FMANd
//#define FM_PRS_MPLS_SSA_ERRATA_FMANj                            /* No implementation, No patch yet */
//#define FM_PRS_INITIAL_PLANID_ERRATA_FMANk                      /* No implementation, No patch yet */

#define FM_NO_COPY_CTXA_CTXB_ERRATA_FMAN_SW001

#define FM_10G_REM_N_LCL_FLT_EX_ERRATA_10GMAC001

/* P2041 */
#define FM_BAD_VLAN_DETECT_ERRATA_10GMAC_A010

/* Common to all */
#define FM_RX_PREAM_4_ERRATA_DTSEC_A001                 FM_NO_RX_PREAM_ERRATA_DTSECx1
#define FM_UCODE_NOT_RESET_ERRATA_BUGZILLA6173
#define FM_MAGIC_PACKET_UNRECOGNIZED_ERRATA_DTSEC2              /* No implementation, Out of LLD scope */
#define FM_PRS_MEM_ERRATA_FMAN_SW003
#define FM_LEN_CHECK_ERRATA_FMAN_SW002

#define	DPAA_VERSION	10
#define	FM_PCD_SW_PRS_SIZE	0x00000800
#define	FM_PCD_PRS_SW_PATCHES_SIZE	0x00000200
#define	FM_NUM_OF_CTRL	2

#endif /* __DPAA_INTEGRATION_EXT_H */
