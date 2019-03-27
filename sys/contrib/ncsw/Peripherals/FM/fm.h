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
 @File          fm.h

 @Description   FM internal structures and definitions.
*//***************************************************************************/
#ifndef __FM_H
#define __FM_H

#include "error_ext.h"
#include "std_ext.h"
#include "fm_ext.h"
#include "fm_ipc.h"

#include "fsl_fman.h"

#define __ERR_MODULE__  MODULE_FM

#define FM_MAX_NUM_OF_HW_PORT_IDS           64
#define FM_MAX_NUM_OF_GUESTS                100

/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/
#define FM_EX_DMA_BUS_ERROR                 0x80000000      /**< DMA bus error. */
#define FM_EX_DMA_READ_ECC                  0x40000000
#define FM_EX_DMA_SYSTEM_WRITE_ECC          0x20000000
#define FM_EX_DMA_FM_WRITE_ECC              0x10000000
#define FM_EX_FPM_STALL_ON_TASKS            0x08000000      /**< Stall of tasks on FPM */
#define FM_EX_FPM_SINGLE_ECC                0x04000000      /**< Single ECC on FPM */
#define FM_EX_FPM_DOUBLE_ECC                0x02000000
#define FM_EX_QMI_SINGLE_ECC                0x01000000      /**< Single ECC on FPM */
#define FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID   0x00800000      /**< Dequeu from default queue id */
#define FM_EX_QMI_DOUBLE_ECC                0x00400000
#define FM_EX_BMI_LIST_RAM_ECC              0x00200000
#define FM_EX_BMI_STORAGE_PROFILE_ECC       0x00100000
#define FM_EX_BMI_STATISTICS_RAM_ECC        0x00080000
#define FM_EX_IRAM_ECC                      0x00040000
#define FM_EX_MURAM_ECC                     0x00020000
#define FM_EX_BMI_DISPATCH_RAM_ECC          0x00010000
#define FM_EX_DMA_SINGLE_PORT_ECC           0x00008000

#define DMA_EMSR_EMSTR_MASK                 0x0000FFFF

#define DMA_THRESH_COMMQ_MASK               0xFF000000
#define DMA_THRESH_READ_INT_BUF_MASK        0x007F0000
#define DMA_THRESH_WRITE_INT_BUF_MASK       0x0000007F

#define GET_EXCEPTION_FLAG(bitMask, exception)              \
switch (exception){                                         \
    case e_FM_EX_DMA_BUS_ERROR:                             \
        bitMask = FM_EX_DMA_BUS_ERROR; break;               \
    case e_FM_EX_DMA_SINGLE_PORT_ECC:                       \
        bitMask = FM_EX_DMA_SINGLE_PORT_ECC; break;         \
    case e_FM_EX_DMA_READ_ECC:                              \
        bitMask = FM_EX_DMA_READ_ECC; break;                \
    case e_FM_EX_DMA_SYSTEM_WRITE_ECC:                      \
        bitMask = FM_EX_DMA_SYSTEM_WRITE_ECC; break;        \
    case e_FM_EX_DMA_FM_WRITE_ECC:                          \
        bitMask = FM_EX_DMA_FM_WRITE_ECC; break;            \
    case e_FM_EX_FPM_STALL_ON_TASKS:                        \
        bitMask = FM_EX_FPM_STALL_ON_TASKS; break;          \
    case e_FM_EX_FPM_SINGLE_ECC:                            \
        bitMask = FM_EX_FPM_SINGLE_ECC; break;              \
    case e_FM_EX_FPM_DOUBLE_ECC:                            \
        bitMask = FM_EX_FPM_DOUBLE_ECC; break;              \
    case e_FM_EX_QMI_SINGLE_ECC:                            \
        bitMask = FM_EX_QMI_SINGLE_ECC; break;              \
    case e_FM_EX_QMI_DOUBLE_ECC:                            \
        bitMask = FM_EX_QMI_DOUBLE_ECC; break;              \
    case e_FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID:               \
        bitMask = FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID; break; \
    case e_FM_EX_BMI_LIST_RAM_ECC:                          \
        bitMask = FM_EX_BMI_LIST_RAM_ECC; break;            \
    case e_FM_EX_BMI_STORAGE_PROFILE_ECC:                   \
        bitMask = FM_EX_BMI_STORAGE_PROFILE_ECC; break;     \
    case e_FM_EX_BMI_STATISTICS_RAM_ECC:                    \
        bitMask = FM_EX_BMI_STATISTICS_RAM_ECC; break;      \
    case e_FM_EX_BMI_DISPATCH_RAM_ECC:                      \
        bitMask = FM_EX_BMI_DISPATCH_RAM_ECC; break;        \
    case e_FM_EX_IRAM_ECC:                                  \
        bitMask = FM_EX_IRAM_ECC; break;                    \
    case e_FM_EX_MURAM_ECC:                                 \
        bitMask = FM_EX_MURAM_ECC; break;                   \
    default: bitMask = 0;break;                             \
}

#define GET_FM_MODULE_EVENT(_mod, _id, _intrType, _event)                                           \
    switch (_mod) {                                                                                 \
        case e_FM_MOD_PRS:                                                                          \
            if (_id) _event = e_FM_EV_DUMMY_LAST;                                                   \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? e_FM_EV_ERR_PRS : e_FM_EV_PRS;        \
            break;                                                                                  \
        case e_FM_MOD_KG:                                                                           \
            if (_id) _event = e_FM_EV_DUMMY_LAST;                                                   \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? e_FM_EV_ERR_KG : e_FM_EV_DUMMY_LAST;  \
            break;                                                                                  \
        case e_FM_MOD_PLCR:                                                                         \
            if (_id) _event = e_FM_EV_DUMMY_LAST;                                                   \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? e_FM_EV_ERR_PLCR : e_FM_EV_PLCR;      \
            break;                                                                                  \
        case e_FM_MOD_TMR:                                                                          \
            if (_id) _event = e_FM_EV_DUMMY_LAST;                                                   \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? e_FM_EV_DUMMY_LAST : e_FM_EV_TMR;     \
            break;                                                                                  \
        case e_FM_MOD_10G_MAC:                                                                      \
            if (_id >= FM_MAX_NUM_OF_10G_MACS) _event = e_FM_EV_DUMMY_LAST;                         \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? (e_FM_EV_ERR_10G_MAC0 + _id) : (e_FM_EV_10G_MAC0 + _id); \
            break;                                                                                  \
        case e_FM_MOD_1G_MAC:                                                                       \
            if (_id >= FM_MAX_NUM_OF_1G_MACS) _event = e_FM_EV_DUMMY_LAST;                          \
            else _event = (_intrType == e_FM_INTR_TYPE_ERR) ? (e_FM_EV_ERR_1G_MAC0 + _id) : (e_FM_EV_1G_MAC0 + _id); \
            break;                                                                                  \
        case e_FM_MOD_MACSEC:                                                                       \
            switch (_id){                                                                           \
                 case (0): _event = (_intrType == e_FM_INTR_TYPE_ERR) ? e_FM_EV_ERR_MACSEC_MAC0:e_FM_EV_MACSEC_MAC0; \
                 break;                                                                             \
                 }                                                                                  \
            break;                                                                                  \
        case e_FM_MOD_FMAN_CTRL:                                                                    \
            if (_intrType == e_FM_INTR_TYPE_ERR) _event = e_FM_EV_DUMMY_LAST;                       \
            else _event = (e_FM_EV_FMAN_CTRL_0 + _id);                                              \
            break;                                                                                  \
        default: _event = e_FM_EV_DUMMY_LAST;                                                       \
        break;                                                                                      \
    }

#define FMAN_CACHE_OVERRIDE_TRANS(fsl_cache_override, _cache_override) \
    switch (_cache_override){ \
        case  e_FM_DMA_NO_CACHE_OR:                    \
            fsl_cache_override =  E_FMAN_DMA_NO_CACHE_OR; break;    \
        case  e_FM_DMA_NO_STASH_DATA:                    \
            fsl_cache_override =  E_FMAN_DMA_NO_STASH_DATA; break;        \
        case  e_FM_DMA_MAY_STASH_DATA:                    \
            fsl_cache_override =  E_FMAN_DMA_MAY_STASH_DATA; break;    \
        case  e_FM_DMA_STASH_DATA:                        \
            fsl_cache_override =  E_FMAN_DMA_STASH_DATA; break;        \
        default: \
            fsl_cache_override =  E_FMAN_DMA_NO_CACHE_OR; break;    \
    }

#define FMAN_AID_MODE_TRANS(fsl_aid_mode, _aid_mode) \
    switch (_aid_mode){ \
        case  e_FM_DMA_AID_OUT_PORT_ID:                    \
            fsl_aid_mode =  E_FMAN_DMA_AID_OUT_PORT_ID; break;    \
        case  e_FM_DMA_AID_OUT_TNUM:                    \
            fsl_aid_mode =  E_FMAN_DMA_AID_OUT_TNUM; break;        \
        default: \
            fsl_aid_mode =  E_FMAN_DMA_AID_OUT_PORT_ID; break;    \
    }

#define FMAN_DMA_DBG_CNT_TRANS(fsl_dma_dbg_cnt, _dma_dbg_cnt) \
    switch (_dma_dbg_cnt){ \
        case  e_FM_DMA_DBG_NO_CNT:                    \
            fsl_dma_dbg_cnt =  E_FMAN_DMA_DBG_NO_CNT; break;    \
        case  e_FM_DMA_DBG_CNT_DONE:                    \
            fsl_dma_dbg_cnt =  E_FMAN_DMA_DBG_CNT_DONE; break;        \
        case  e_FM_DMA_DBG_CNT_COMM_Q_EM:                    \
            fsl_dma_dbg_cnt =  E_FMAN_DMA_DBG_CNT_COMM_Q_EM; break;    \
        case  e_FM_DMA_DBG_CNT_INT_READ_EM:                        \
            fsl_dma_dbg_cnt =  E_FMAN_DMA_DBG_CNT_INT_READ_EM; break;        \
        case  e_FM_DMA_DBG_CNT_INT_WRITE_EM:                        \
            fsl_dma_dbg_cnt = E_FMAN_DMA_DBG_CNT_INT_WRITE_EM ; break;        \
        case  e_FM_DMA_DBG_CNT_FPM_WAIT:                        \
            fsl_dma_dbg_cnt = E_FMAN_DMA_DBG_CNT_FPM_WAIT ; break;        \
        case  e_FM_DMA_DBG_CNT_SIGLE_BIT_ECC:                        \
            fsl_dma_dbg_cnt = E_FMAN_DMA_DBG_CNT_SIGLE_BIT_ECC ; break;        \
        case  e_FM_DMA_DBG_CNT_RAW_WAR_PROT:                        \
            fsl_dma_dbg_cnt = E_FMAN_DMA_DBG_CNT_RAW_WAR_PROT ; break;        \
        default: \
            fsl_dma_dbg_cnt =  E_FMAN_DMA_DBG_NO_CNT; break;    \
    }

#define FMAN_DMA_EMER_TRANS(fsl_dma_emer, _dma_emer) \
    switch (_dma_emer){ \
        case  e_FM_DMA_EM_EBS:                    \
            fsl_dma_emer =  E_FMAN_DMA_EM_EBS; break;    \
        case  e_FM_DMA_EM_SOS:                    \
            fsl_dma_emer =  E_FMAN_DMA_EM_SOS; break;        \
        default: \
            fsl_dma_emer =  E_FMAN_DMA_EM_EBS; break;    \
    }

#define FMAN_DMA_ERR_TRANS(fsl_dma_err, _dma_err) \
    switch (_dma_err){ \
        case  e_FM_DMA_ERR_CATASTROPHIC:                    \
            fsl_dma_err =  E_FMAN_DMA_ERR_CATASTROPHIC; break;    \
        case  e_FM_DMA_ERR_REPORT:                    \
            fsl_dma_err =  E_FMAN_DMA_ERR_REPORT; break;        \
        default: \
            fsl_dma_err =  E_FMAN_DMA_ERR_CATASTROPHIC; break;    \
    }

#define FMAN_CATASTROPHIC_ERR_TRANS(fsl_catastrophic_err, _catastrophic_err) \
    switch (_catastrophic_err){ \
        case  e_FM_CATASTROPHIC_ERR_STALL_PORT:                    \
            fsl_catastrophic_err =  E_FMAN_CATAST_ERR_STALL_PORT; break;    \
        case  e_FM_CATASTROPHIC_ERR_STALL_TASK:                    \
            fsl_catastrophic_err =  E_FMAN_CATAST_ERR_STALL_TASK; break;        \
        default: \
            fsl_catastrophic_err =  E_FMAN_CATAST_ERR_STALL_PORT; break;    \
    }

#define FMAN_COUNTERS_TRANS(fsl_counters, _counters) \
    switch (_counters){ \
        case  e_FM_COUNTERS_ENQ_TOTAL_FRAME:                    \
            fsl_counters =  E_FMAN_COUNTERS_ENQ_TOTAL_FRAME; break;    \
        case  e_FM_COUNTERS_DEQ_TOTAL_FRAME:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_TOTAL_FRAME; break;        \
        case  e_FM_COUNTERS_DEQ_0:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_0; break;    \
        case  e_FM_COUNTERS_DEQ_1:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_1; break;        \
        case  e_FM_COUNTERS_DEQ_2:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_2; break;    \
        case  e_FM_COUNTERS_DEQ_3:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_3; break;        \
        case  e_FM_COUNTERS_DEQ_FROM_DEFAULT:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_FROM_DEFAULT; break;    \
        case  e_FM_COUNTERS_DEQ_FROM_CONTEXT:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_FROM_CONTEXT; break;        \
        case  e_FM_COUNTERS_DEQ_FROM_FD:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_FROM_FD; break;    \
        case  e_FM_COUNTERS_DEQ_CONFIRM:                    \
            fsl_counters =  E_FMAN_COUNTERS_DEQ_CONFIRM; break;        \
        default: \
            fsl_counters =  E_FMAN_COUNTERS_ENQ_TOTAL_FRAME; break;    \
    }

/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/
#define DEFAULT_exceptions                 (FM_EX_DMA_BUS_ERROR            |\
                                            FM_EX_DMA_READ_ECC              |\
                                            FM_EX_DMA_SYSTEM_WRITE_ECC      |\
                                            FM_EX_DMA_FM_WRITE_ECC          |\
                                            FM_EX_FPM_STALL_ON_TASKS        |\
                                            FM_EX_FPM_SINGLE_ECC            |\
                                            FM_EX_FPM_DOUBLE_ECC            |\
                                            FM_EX_QMI_DEQ_FROM_UNKNOWN_PORTID|\
                                            FM_EX_BMI_LIST_RAM_ECC          |\
                                            FM_EX_BMI_STORAGE_PROFILE_ECC   |\
                                            FM_EX_BMI_STATISTICS_RAM_ECC    |\
                                            FM_EX_IRAM_ECC                  |\
                                            FM_EX_MURAM_ECC                 |\
                                            FM_EX_BMI_DISPATCH_RAM_ECC      |\
                                            FM_EX_QMI_DOUBLE_ECC            |\
                                            FM_EX_QMI_SINGLE_ECC)

#define DEFAULT_eccEnable                   FALSE
#ifdef FM_PEDANTIC_DMA
#define DEFAULT_aidOverride                 TRUE
#else
#define DEFAULT_aidOverride                 FALSE
#endif /* FM_PEDANTIC_DMA */
#define DEFAULT_aidMode                     e_FM_DMA_AID_OUT_TNUM
#define DEFAULT_dmaStopOnBusError           FALSE
#define DEFAULT_stopAtBusError              FALSE
#define DEFAULT_axiDbgNumOfBeats            1
#define DEFAULT_dmaReadIntBufLow            ((DMA_THRESH_MAX_BUF+1)/2)
#define DEFAULT_dmaReadIntBufHigh           ((DMA_THRESH_MAX_BUF+1)*3/4)
#define DEFAULT_dmaWriteIntBufLow           ((DMA_THRESH_MAX_BUF+1)/2)
#define DEFAULT_dmaWriteIntBufHigh          ((DMA_THRESH_MAX_BUF+1)*3/4)
#define DEFAULT_catastrophicErr             e_FM_CATASTROPHIC_ERR_STALL_PORT
#define DEFAULT_dmaErr                      e_FM_DMA_ERR_CATASTROPHIC
#define DEFAULT_resetOnInit                 FALSE
#define DEFAULT_resetOnInitOverrideCallback NULL
#define DEFAULT_haltOnExternalActivation    FALSE   /* do not change! if changed, must be disabled for rev1 ! */
#define DEFAULT_haltOnUnrecoverableEccError FALSE   /* do not change! if changed, must be disabled for rev1 ! */
#define DEFAULT_externalEccRamsEnable       FALSE
#define DEFAULT_VerifyUcode                 FALSE

#if (DPAA_VERSION < 11)
#define DEFAULT_totalFifoSize(major, minor)     \
    (((major == 2) || (major == 5)) ?           \
     (100*KILOBYTE) : ((major == 4) ?           \
     (49*KILOBYTE) : (122*KILOBYTE)))
#define DEFAULT_totalNumOfTasks(major, minor)   \
            BMI_MAX_NUM_OF_TASKS

#define DEFAULT_dmaCommQLow                 ((DMA_THRESH_MAX_COMMQ+1)/2)
#define DEFAULT_dmaCommQHigh                ((DMA_THRESH_MAX_COMMQ+1)*3/4)
#define DEFAULT_cacheOverride               e_FM_DMA_NO_CACHE_OR
#define DEFAULT_dmaCamNumOfEntries          32
#define DEFAULT_dmaDbgCntMode               e_FM_DMA_DBG_NO_CNT
#define DEFAULT_dmaEnEmergency              FALSE
#define DEFAULT_dmaSosEmergency             0
#define DEFAULT_dmaWatchdog                 0 /* disabled */
#define DEFAULT_dmaEnEmergencySmoother      FALSE
#define DEFAULT_dmaEmergencySwitchCounter   0

#define DEFAULT_dispLimit                   0
#define DEFAULT_prsDispTh                   16
#define DEFAULT_plcrDispTh                  16
#define DEFAULT_kgDispTh                    16
#define DEFAULT_bmiDispTh                   16
#define DEFAULT_qmiEnqDispTh                16
#define DEFAULT_qmiDeqDispTh                16
#define DEFAULT_fmCtl1DispTh                16
#define DEFAULT_fmCtl2DispTh                16

#else  /* (DPAA_VERSION < 11) */
/* Defaults are registers' reset values */
#define DEFAULT_totalFifoSize(major, minor)			\
	(((major == 6) && ((minor == 1) || (minor == 4))) ?	\
	(156*KILOBYTE) : (295*KILOBYTE))

/* According to the default value of FMBM_CFG2[TNTSKS] */
#define DEFAULT_totalNumOfTasks(major, minor)   \
      (((major == 6) && ((minor == 1) || (minor == 4))) ? 59 : 124)

#define DEFAULT_dmaCommQLow                 0x2A
#define DEFAULT_dmaCommQHigh                0x3F
#define DEFAULT_cacheOverride               e_FM_DMA_NO_CACHE_OR
#define DEFAULT_dmaCamNumOfEntries          64
#define DEFAULT_dmaDbgCntMode               e_FM_DMA_DBG_NO_CNT
#define DEFAULT_dmaEnEmergency              FALSE
#define DEFAULT_dmaSosEmergency             0
#define DEFAULT_dmaWatchdog                 0 /* disabled */
#define DEFAULT_dmaEnEmergencySmoother      FALSE
#define DEFAULT_dmaEmergencySwitchCounter   0

#define DEFAULT_dispLimit                   0
#define DEFAULT_prsDispTh                   16
#define DEFAULT_plcrDispTh                  16
#define DEFAULT_kgDispTh                    16
#define DEFAULT_bmiDispTh                   16
#define DEFAULT_qmiEnqDispTh                16
#define DEFAULT_qmiDeqDispTh                16
#define DEFAULT_fmCtl1DispTh                16
#define DEFAULT_fmCtl2DispTh                16
#endif /* (DPAA_VERSION < 11) */

#define FM_TIMESTAMP_1_USEC_BIT             8

/**************************************************************************//**
 @Collection   Defines used for enabling/disabling FM interrupts
 @{
*//***************************************************************************/
#define ERR_INTR_EN_DMA         0x00010000
#define ERR_INTR_EN_FPM         0x80000000
#define ERR_INTR_EN_BMI         0x00800000
#define ERR_INTR_EN_QMI         0x00400000
#define ERR_INTR_EN_PRS         0x00200000
#define ERR_INTR_EN_KG          0x00100000
#define ERR_INTR_EN_PLCR        0x00080000
#define ERR_INTR_EN_MURAM       0x00040000
#define ERR_INTR_EN_IRAM        0x00020000
#define ERR_INTR_EN_10G_MAC0    0x00008000
#define ERR_INTR_EN_10G_MAC1    0x00000040
#define ERR_INTR_EN_1G_MAC0     0x00004000
#define ERR_INTR_EN_1G_MAC1     0x00002000
#define ERR_INTR_EN_1G_MAC2     0x00001000
#define ERR_INTR_EN_1G_MAC3     0x00000800
#define ERR_INTR_EN_1G_MAC4     0x00000400
#define ERR_INTR_EN_1G_MAC5     0x00000200
#define ERR_INTR_EN_1G_MAC6     0x00000100
#define ERR_INTR_EN_1G_MAC7     0x00000080
#define ERR_INTR_EN_MACSEC_MAC0 0x00000001

#define INTR_EN_QMI             0x40000000
#define INTR_EN_PRS             0x20000000
#define INTR_EN_WAKEUP          0x10000000
#define INTR_EN_PLCR            0x08000000
#define INTR_EN_1G_MAC0         0x00080000
#define INTR_EN_1G_MAC1         0x00040000
#define INTR_EN_1G_MAC2         0x00020000
#define INTR_EN_1G_MAC3         0x00010000
#define INTR_EN_1G_MAC4         0x00000040
#define INTR_EN_1G_MAC5         0x00000020
#define INTR_EN_1G_MAC6         0x00000008
#define INTR_EN_1G_MAC7         0x00000002
#define INTR_EN_10G_MAC0        0x00200000
#define INTR_EN_10G_MAC1        0x00100000
#define INTR_EN_REV0            0x00008000
#define INTR_EN_REV1            0x00004000
#define INTR_EN_REV2            0x00002000
#define INTR_EN_REV3            0x00001000
#define INTR_EN_BRK             0x00000080
#define INTR_EN_TMR             0x01000000
#define INTR_EN_MACSEC_MAC0     0x00000001
/* @} */

/**************************************************************************//**
 @Description       Memory Mapped Registers
*//***************************************************************************/

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

typedef struct
{
    volatile uint32_t   iadd;           /**< FM IRAM instruction address register */
    volatile uint32_t   idata;          /**< FM IRAM instruction data register */
    volatile uint32_t   itcfg;          /**< FM IRAM timing config register */
    volatile uint32_t   iready;         /**< FM IRAM ready register */
    volatile uint32_t   res[0x1FFFC];
} t_FMIramRegs;

/* Trace buffer registers -
   each FM Controller has its own trace buffer residing at FM_MM_TRB(fmCtrlIndex) offset */
typedef struct t_FmTrbRegs
{
    volatile uint32_t   tcrh;
    volatile uint32_t   tcrl;
    volatile uint32_t   tesr;
    volatile uint32_t   tecr0h;
    volatile uint32_t   tecr0l;
    volatile uint32_t   terf0h;
    volatile uint32_t   terf0l;
    volatile uint32_t   tecr1h;
    volatile uint32_t   tecr1l;
    volatile uint32_t   terf1h;
    volatile uint32_t   terf1l;
    volatile uint32_t   tpcch;
    volatile uint32_t   tpccl;
    volatile uint32_t   tpc1h;
    volatile uint32_t   tpc1l;
    volatile uint32_t   tpc2h;
    volatile uint32_t   tpc2l;
    volatile uint32_t   twdimr;
    volatile uint32_t   twicvr;
    volatile uint32_t   tar;
    volatile uint32_t   tdr;
    volatile uint32_t   tsnum1;
    volatile uint32_t   tsnum2;
    volatile uint32_t   tsnum3;
    volatile uint32_t   tsnum4;
} t_FmTrbRegs;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/
#define FM_DEBUG_STATUS_REGISTER_OFFSET     0x000d1084UL
#define FM_FW_DEBUG_INSTRUCTION             0x6ffff805UL

/**************************************************************************//**
 @Description       FPM defines
*//***************************************************************************/
/* masks */
#define FPM_BRKC_RDBG                   0x00000200
#define FPM_BRKC_SLP                    0x00000800
/**************************************************************************//**
 @Description       BMI defines
*//***************************************************************************/
/* masks */
#define BMI_INIT_START                      0x80000000
#define BMI_ERR_INTR_EN_STORAGE_PROFILE_ECC 0x80000000
#define BMI_ERR_INTR_EN_LIST_RAM_ECC        0x40000000
#define BMI_ERR_INTR_EN_STATISTICS_RAM_ECC  0x20000000
#define BMI_ERR_INTR_EN_DISPATCH_RAM_ECC    0x10000000
/**************************************************************************//**
 @Description       QMI defines
*//***************************************************************************/
/* masks */
#define QMI_ERR_INTR_EN_DOUBLE_ECC      0x80000000
#define QMI_ERR_INTR_EN_DEQ_FROM_DEF    0x40000000
#define QMI_INTR_EN_SINGLE_ECC          0x80000000

/**************************************************************************//**
 @Description       IRAM defines
*//***************************************************************************/
/* masks */
#define IRAM_IADD_AIE                   0x80000000
#define IRAM_READY                      0x80000000

/**************************************************************************//**
 @Description       TRB defines
*//***************************************************************************/
/* masks */
#define TRB_TCRH_RESET              0x04000000
#define TRB_TCRH_ENABLE_COUNTERS    0x84008000
#define TRB_TCRH_DISABLE_COUNTERS   0x8400C000
#define TRB_TCRL_RESET              0x20000000
#define TRB_TCRL_UTIL               0x00000460
typedef struct {
    void        (*f_Isr) (t_Handle h_Arg, uint32_t event);
    t_Handle    h_SrcHandle;
} t_FmanCtrlIntrSrc;


typedef void (t_FmanCtrlIsr)( t_Handle h_Fm, uint32_t event);

typedef struct
{
/***************************/
/* Master/Guest parameters */
/***************************/
    uint8_t                     fmId;
    e_FmPortType                portsTypes[FM_MAX_NUM_OF_HW_PORT_IDS];
    uint16_t                    fmClkFreq;
    uint16_t                    fmMacClkFreq;
    t_FmRevisionInfo            revInfo;
/**************************/
/* Master Only parameters */
/**************************/
    bool                        enabledTimeStamp;
    uint8_t                     count1MicroBit;
    uint8_t                     totalNumOfTasks;
    uint32_t                    totalFifoSize;
    uint8_t                     maxNumOfOpenDmas;
    uint8_t                     accumulatedNumOfTasks;
    uint32_t                    accumulatedFifoSize;
    uint8_t                     accumulatedNumOfOpenDmas;
    uint8_t                     accumulatedNumOfDeqTnums;
#ifdef FM_LOW_END_RESTRICTION
    bool                        lowEndRestriction;
#endif /* FM_LOW_END_RESTRICTION */
    uint32_t                    exceptions;
    uintptr_t                   irq;
    uintptr_t                   errIrq;
    bool                        ramsEccEnable;
    bool                        explicitEnable;
    bool                        internalCall;
    uint8_t                     ramsEccOwners;
    uint32_t                    extraFifoPoolSize;
    uint8_t                     extraTasksPoolSize;
    uint8_t                     extraOpenDmasPoolSize;
#if defined(FM_MAX_NUM_OF_10G_MACS) && (FM_MAX_NUM_OF_10G_MACS)
    uint16_t                    portMaxFrameLengths10G[FM_MAX_NUM_OF_10G_MACS];
    uint16_t                    macMaxFrameLengths10G[FM_MAX_NUM_OF_10G_MACS];
#endif /* defined(FM_MAX_NUM_OF_10G_MACS) && ... */
    uint16_t                    portMaxFrameLengths1G[FM_MAX_NUM_OF_1G_MACS];
    uint16_t                    macMaxFrameLengths1G[FM_MAX_NUM_OF_1G_MACS];
} t_FmStateStruct;

#if (DPAA_VERSION >= 11)
typedef struct t_FmMapParam {
    uint16_t        profilesBase;
    uint16_t        numOfProfiles;
    t_Handle        h_FmPort;
} t_FmMapParam;

typedef struct t_FmAllocMng {
    bool            allocated;
    uint8_t         ownerId; /* guestId for KG in multi-partition only,
                                portId for PLCR in any environment */
} t_FmAllocMng;

typedef struct t_FmPcdSpEntry {
    bool            valid;
    t_FmAllocMng    profilesMng;
} t_FmPcdSpEntry;

typedef struct t_FmSp {
    void            *p_FmPcdStoragePrflRegs;
    t_FmPcdSpEntry  profiles[FM_VSP_MAX_NUM_OF_ENTRIES];
    t_FmMapParam    portsMapping[FM_MAX_NUM_OF_PORTS];
} t_FmSp;
#endif /* (DPAA_VERSION >= 11) */

typedef struct t_Fm
{
/***************************/
/* Master/Guest parameters */
/***************************/
/* locals for recovery */
    uintptr_t                   baseAddr;

/* un-needed for recovery */
    t_Handle                    h_Pcd;
    char                        fmModuleName[MODULE_NAME_SIZE];
    char                        fmIpcHandlerModuleName[FM_MAX_NUM_OF_GUESTS][MODULE_NAME_SIZE];
    t_Handle                    h_IpcSessions[FM_MAX_NUM_OF_GUESTS];
    t_FmIntrSrc                 intrMng[e_FM_EV_DUMMY_LAST];    /* FM exceptions user callback */
    uint8_t                     guestId;
/**************************/
/* Master Only parameters */
/**************************/
/* locals for recovery */
    struct fman_fpm_regs *p_FmFpmRegs;
    struct fman_bmi_regs *p_FmBmiRegs;
    struct fman_qmi_regs *p_FmQmiRegs;
    struct fman_dma_regs *p_FmDmaRegs;
    struct fman_regs            *p_FmRegs;
    t_FmExceptionsCallback      *f_Exception;
    t_FmBusErrorCallback        *f_BusError;
    t_Handle                    h_App;                          /* Application handle */
    t_Handle                    h_Spinlock;
    bool                        recoveryMode;
    t_FmStateStruct             *p_FmStateStruct;
    uint16_t                    tnumAgingPeriod;
#if (DPAA_VERSION >= 11)
    t_FmSp                      *p_FmSp;
    uint8_t                     partNumOfVSPs;
    uint8_t                     partVSPBase;
    uintptr_t                   vspBaseAddr;
#endif /* (DPAA_VERSION >= 11) */
    bool                        portsPreFetchConfigured[FM_MAX_NUM_OF_HW_PORT_IDS]; /* Prefetch configration per Tx-port */
    bool                        portsPreFetchValue[FM_MAX_NUM_OF_HW_PORT_IDS];      /* Prefetch configration per Tx-port */

/* un-needed for recovery */
    struct fman_cfg             *p_FmDriverParam;
    t_Handle                    h_FmMuram;
    uint64_t                    fmMuramPhysBaseAddr;
    bool                        independentMode;
    bool                        hcPortInitialized;
    uintptr_t                   camBaseAddr;                    /* save for freeing */
    uintptr_t                   resAddr;
    uintptr_t                   fifoBaseAddr;                   /* save for freeing */
    t_FmanCtrlIntrSrc           fmanCtrlIntr[FM_NUM_OF_FMAN_CTRL_EVENT_REGS];    /* FM exceptions user callback */
    bool                        usedEventRegs[FM_NUM_OF_FMAN_CTRL_EVENT_REGS];
    t_FmFirmwareParams          firmware;
    bool                        fwVerify;
    bool                        resetOnInit;
    t_FmResetOnInitOverrideCallback     *f_ResetOnInitOverride;
    uint32_t                    userSetExceptions;
} t_Fm;


#endif /* __FM_H */
