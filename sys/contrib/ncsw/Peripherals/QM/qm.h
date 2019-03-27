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
 *

 **************************************************************************/
/******************************************************************************
 @File          qm.h

 @Description   QM header
*//***************************************************************************/
#ifndef __QM_H
#define __QM_H

#include "std_ext.h"
#include "list_ext.h"
#include "qm_ext.h"
#include "qman_private.h"
#include "qm_ipc.h"


#define __ERR_MODULE__  MODULE_QM

#define QM_NUM_OF_SWP               10
#define QM_NUM_OF_DCP               5

#define CACHELINE_SIZE              64
#define QM_CONTEXTA_MAX_STASH_SIZE  (3 * CACHELINE_SIZE)

/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/
#define QM_EX_CORENET_INITIATOR_DATA                0x20000000
#define QM_EX_CORENET_TARGET_DATA                   0x10000000
#define QM_EX_CORENET_INVALID_TARGET_TRANSACTION    0x08000000
#define QM_EX_PFDR_THRESHOLD                        0x04000000
#define QM_EX_MULTI_ECC                             0x02000000
#define QM_EX_SINGLE_ECC                            0x01000000
#define QM_EX_PFDR_ENQUEUE_BLOCKED                  0x00800000
#define QM_EX_INVALID_COMMAND                       0x00010000
#define QM_EX_DEQUEUE_DCP                           0x00000800
#define QM_EX_DEQUEUE_FQ                            0x00000400
#define QM_EX_DEQUEUE_SOURCE                        0x00000200
#define QM_EX_DEQUEUE_QUEUE                         0x00000100
#define QM_EX_ENQUEUE_OVERFLOW                      0x00000008
#define QM_EX_ENQUEUE_STATE                         0x00000004
#define QM_EX_ENQUEUE_CHANNEL                       0x00000002
#define QM_EX_ENQUEUE_QUEUE                         0x00000001

#define GET_EXCEPTION_FLAG(bitMask, exception)       switch(exception){ \
    case e_QM_EX_CORENET_INITIATOR_DATA:                                \
        bitMask = QM_EX_CORENET_INITIATOR_DATA; break;                  \
    case e_QM_EX_CORENET_TARGET_DATA:                                   \
        bitMask = QM_EX_CORENET_TARGET_DATA; break;                     \
    case e_QM_EX_CORENET_INVALID_TARGET_TRANSACTION:                    \
        bitMask = QM_EX_CORENET_INVALID_TARGET_TRANSACTION; break;      \
    case e_QM_EX_PFDR_THRESHOLD:                                        \
        bitMask = QM_EX_PFDR_THRESHOLD; break;                          \
    case e_QM_EX_PFDR_ENQUEUE_BLOCKED:                                  \
        bitMask = QM_EX_PFDR_ENQUEUE_BLOCKED; break;                    \
    case e_QM_EX_SINGLE_ECC:                                            \
        bitMask = QM_EX_SINGLE_ECC; break;                              \
    case e_QM_EX_MULTI_ECC:                                             \
        bitMask = QM_EX_MULTI_ECC; break;                               \
    case e_QM_EX_INVALID_COMMAND:                                       \
        bitMask = QM_EX_INVALID_COMMAND; break;                         \
    case e_QM_EX_DEQUEUE_DCP:                                           \
        bitMask = QM_EX_DEQUEUE_DCP; break;                             \
    case e_QM_EX_DEQUEUE_FQ:                                            \
        bitMask = QM_EX_DEQUEUE_FQ; break;                              \
    case e_QM_EX_DEQUEUE_SOURCE:                                        \
        bitMask = QM_EX_DEQUEUE_SOURCE; break;                          \
    case e_QM_EX_DEQUEUE_QUEUE:                                         \
        bitMask = QM_EX_DEQUEUE_QUEUE; break;                           \
    case e_QM_EX_ENQUEUE_OVERFLOW:                                      \
        bitMask = QM_EX_ENQUEUE_OVERFLOW; break;                        \
    case e_QM_EX_ENQUEUE_STATE:                                         \
        bitMask = QM_EX_ENQUEUE_STATE; break;                           \
    case e_QM_EX_ENQUEUE_CHANNEL:                                       \
        bitMask = QM_EX_ENQUEUE_CHANNEL; break;                         \
    case e_QM_EX_ENQUEUE_QUEUE:                                         \
        bitMask = QM_EX_ENQUEUE_QUEUE; break;                           \
    default: bitMask = 0;break;}

/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/
/* QM defaults */
#define DEFAULT_exceptions                      ((uint32_t)(QM_EX_CORENET_INITIATOR_DATA                | \
                                                            QM_EX_CORENET_TARGET_DATA                   | \
                                                            QM_EX_CORENET_INVALID_TARGET_TRANSACTION    | \
                                                            QM_EX_PFDR_THRESHOLD                        | \
                                                            QM_EX_SINGLE_ECC                            | \
                                                            QM_EX_MULTI_ECC                             | \
                                                            QM_EX_PFDR_ENQUEUE_BLOCKED                  | \
                                                            QM_EX_INVALID_COMMAND                       | \
                                                            QM_EX_DEQUEUE_DCP                           | \
                                                            QM_EX_DEQUEUE_FQ                            | \
                                                            QM_EX_DEQUEUE_SOURCE                        | \
                                                            QM_EX_DEQUEUE_QUEUE                         | \
                                                            QM_EX_ENQUEUE_OVERFLOW                      | \
                                                            QM_EX_ENQUEUE_STATE                         | \
                                                            QM_EX_ENQUEUE_CHANNEL                       | \
                                                            QM_EX_ENQUEUE_QUEUE                         ))
#define DEFAULT_rtFramesDepth                   30000
#define DEFAULT_pfdrThreshold                   0
#define DEFAULT_sfdrThreshold                   0
#define DEFAULT_pfdrBaseConstant                64
/* Corenet initiator settings. Stash request queues are 4-deep to match cores'
    ability to snart. Stash priority is 3, other priorities are 2. */
#define DEFAULT_initiatorSrcciv     0
#define DEFAULT_initiatorSrqW       3
#define DEFAULT_initiatorRwW        2
#define DEFAULT_initiatorBmanW      2


/* QM-Portal defaults */
#define DEFAULT_dequeueDcaMode                  FALSE
#define DEFAULT_dequeueUpToThreeFrames          TRUE
#define DEFAULT_dequeueCommandType              e_QM_PORTAL_PRIORITY_PRECEDENCE_INTRA_CLASS_SCHEDULING
#define DEFAULT_dequeueUserToken                0xab
#define DEFAULT_dequeueSpecifiedWq              FALSE
#define DEFAULT_dequeueDedicatedChannel         TRUE
#define DEFAULT_dequeuePoolChannelId            0
#define DEFAULT_dequeueWqId                     0
#define DEFAULT_dequeueDedicatedChannelHasPrecedenceOverPoolChannels    TRUE
#define DEFAULT_dqrrSize                        DQRR_MAXFILL
#define DEFAULT_pullMode                        FALSE
#define DEFAULT_portalExceptions                ((uint32_t)(QM_PIRQ_EQCI | \
                                                            QM_PIRQ_EQRI | \
                                                            QM_PIRQ_DQRI | \
                                                            QM_PIRQ_MRI  | \
                                                            QM_PIRQ_CSCI))

/**************************************************************************//**
 @Description       Memory Mapped Registers
*//***************************************************************************/

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

typedef _Packed struct
{
     /* QMan Software Portal Configuration Registers */
    _Packed struct {
        volatile uint32_t   lio_cfg;                /**< QMan Software Portal LIO Configuration */
        volatile uint32_t   io_cfg;                 /**< QMan Software Portal 0 IO Configuration */
        volatile uint8_t    res1[4];                /**< reserved */
        volatile uint32_t   dd_cfg;                 /**< Software Portal Dynamic Debug Configuration */
    } _PackedType swpConfRegs[QM_NUM_OF_SWP];
    volatile uint8_t    res1[352];                  /**< reserved */

    /* Dynamic Debug (DD) Configuration Registers */
    volatile uint32_t   qman_dd_cfg;                /**< QMan Dynamic Debug (DD) Configuration */
    volatile uint8_t    res2[12];                   /**< reserved */
    volatile uint32_t   qcsp_dd_ihrsr;              /**< Software Portal DD Internal Halt Request Status */
    volatile uint32_t   qcsp_dd_ihrfr;              /**< Software Portal DD Internal Halt Request Force */
    volatile uint32_t   qcsp_dd_hasr;               /**< Software Portal DD Halt Acknowledge Status */
    volatile uint8_t    res3[4];                    /**< reserved */
    volatile uint32_t   dcp_dd_ihrsr;               /**< DCP DD Internal Halt Request Status */
    volatile uint32_t   dcp_dd_ihrfr;               /**< DCP DD Internal Halt Request Force */
    volatile uint32_t   dcp_dd_hasr;                /**< DCP DD Halt Acknowledge Status */
    volatile uint8_t    res4[212];                  /**< reserved */

    /* Direct Connect Portal (DCP) Configuration Registers */
    _Packed struct {
        volatile uint32_t   cfg;                    /**< DCP Configuration */
        volatile uint32_t   dd_cfg;                 /**< DCP Dynamic Debug Configuration */
        volatile uint32_t   dlm_cfg;                /**< DCP Dequeue Latency Monitor Configuration */
        volatile uint32_t   dlm_avg;                /**< DCP Dequeue Latency Monitor Average */
    } _PackedType dcpConfRegs[QM_NUM_OF_DCP];
    volatile uint8_t    res5[176];                  /**< reserved */

    /* Packed Frame Descriptor Record (PFDR) Manager Query Registers */
    volatile uint32_t   pfdr_fpc;                   /**< PFDR Free Pool Count */
    volatile uint32_t   pfdr_fp_head;               /**< PFDR Free Pool Head Pointer */
    volatile uint32_t   pfdr_fp_tail;               /**< PFDR Free Pool Tail Pointer */
    volatile uint8_t    res6[4];                    /**< reserved */
    volatile uint32_t   pfdr_fp_lwit;               /**< PFDR Free Pool Low Watermark Interrupt Threshold */
    volatile uint32_t   pfdr_cfg;                   /**< PFDR Configuration */
    volatile uint8_t    res7[232];                  /**< reserved */

    /* Single Frame Descriptor Record (SFDR) Manager Registers */
    volatile uint32_t   sfdr_cfg;                   /**< SFDR Configuration */
    volatile uint32_t   sfdr_in_use;                /**< SFDR In Use Register */
    volatile uint8_t    res8[248];                  /**< reserved */

    /* Work Queue Semaphore and Context Manager Registers */
    volatile uint32_t   wq_cs_cfg[6];               /**< Work Queue Class Scheduler Configuration */
    volatile uint8_t    res9[24];                   /**< reserved */
    volatile uint32_t   wq_def_enq_wqid;            /**< Work Queue Default Enqueue WQID */
    volatile uint8_t    res10[12];                   /**< reserved */
    volatile uint32_t   wq_sc_dd_cfg[5];            /**< WQ S/W Channel Dynamic Debug Config */
    volatile uint8_t    res11[44];                  /**< reserved */
    volatile uint32_t   wq_pc_dd_cs_cfg[8];         /**< WQ Pool Channel Dynamic Debug Config */
    volatile uint8_t    res12[32];                  /**< reserved */
    volatile uint32_t   wq_dc0_dd_cs_cfg[6];        /**< WQ DCP0 Chan. Dynamic Debug Config */
    volatile uint8_t    res13[40];                  /**< reserved */
    volatile uint32_t   wq_dc1_dd_cs_cfg[6];        /**< WQ DCP1 Chan. Dynamic Debug Config */
    volatile uint8_t    res14[40];                  /**< reserved */
    volatile uint32_t   wq_dc2_dd_cs_cfg;           /**< WQ DCP2 Chan. Dynamic Debug Config */
    volatile uint8_t    res15[60];                  /**< reserved */
    volatile uint32_t   wq_dc3_dd_cs_cfg;           /**< WQ DCP3 Chan. Dynamic Debug Config */
    volatile uint8_t    res16[124];                 /**< reserved */

    /* Congestion Manager (CM) Registers */
    volatile uint32_t   cm_cfg;                     /**< CM Configuration Register */
    volatile uint8_t    res17[508];                 /**< reserved */

    /* QMan Error Capture Registers */
    volatile uint32_t   ecsr;                       /**< QMan Error Capture Status Register */
    volatile uint32_t   ecir;                       /**< QMan Error Capture Information Register */
    volatile uint32_t   eadr;                       /**< QMan Error Capture Address Register */
    volatile uint8_t    res18[4];                   /**< reserved */
    volatile uint32_t   edata[16];                  /**< QMan ECC Error Data Register */
    volatile uint8_t    res19[32];                  /**< reserved */
    volatile uint32_t   sbet;                       /**< QMan Single Bit ECC Error Threshold Register */
    volatile uint8_t    res20[12];                  /**< reserved */
    volatile uint32_t   sbec[7];                    /**< QMan Single Bit ECC Error Count Register */
    volatile uint8_t    res21[100];                 /**< reserved */

    /* QMan Initialization and Debug Control Registers */
    volatile uint32_t   mcr;                        /**< QMan Management Command/Result Register */
    volatile uint32_t   mcp0;                       /**< QMan Management Command Parameter 0 Register */
    volatile uint32_t   mcp1;                       /**< QMan Management Command Parameter 1 Register */
    volatile uint8_t    res22[20];                  /**< reserved */
    volatile uint32_t   mr[16];                     /**< QMan Management Return Register */
    volatile uint8_t    res23[148];                 /**< reserved */
    volatile uint32_t   idle_stat;                  /**< QMan Idle Status Register */

    /* QMan ID/Revision Registers */
    volatile uint32_t   ip_rev_1;                   /**< QMan IP Block Revision 1 register */
    volatile uint32_t   ip_rev_2;                   /**< QMan IP Block Revision 2 register */

    /* QMan Initiator Interface Memory Window Configuration Registers */
    volatile uint32_t   fqd_bare;                   /**< FQD Extended Base Address Register */
    volatile uint32_t   fqd_bar;                    /**< Frame Queue Descriptor (FQD) Base Address Register */
    volatile uint8_t    res24[8];                   /**< reserved */
    volatile uint32_t   fqd_ar;                     /**< FQD Attributes Register */
    volatile uint8_t    res25[12];                  /**< reserved */
    volatile uint32_t   pfdr_bare;                  /**< PFDR Extended Base Address Register */
    volatile uint32_t   pfdr_bar;                   /**< Packed Frame Descriptor Record (PFDR) Base Addr */
    volatile uint8_t    res26[8];                   /**< reserved */
    volatile uint32_t   pfdr_ar;                    /**< PFDR Attributes Register */
    volatile uint8_t    res27[76];                  /**< reserved */
    volatile uint32_t   qcsp_bare;                  /**< QCSP Extended Base Address */
    volatile uint32_t   qcsp_bar;                   /**< QMan Software Portal Base Address */
    volatile uint8_t    res28[120];                 /**< reserved */
    volatile uint32_t   ci_sched_cfg;               /**< Initiator Scheduling Configuration */
    volatile uint32_t   srcidr;                     /**< QMan Source ID Register */
    volatile uint32_t   liodnr;                     /**< QMan Logical I/O Device Number Register */
    volatile uint8_t    res29[4];                   /**< reserved */
    volatile uint32_t   ci_rlm_cfg;                 /**< Initiator Read Latency Monitor Configuration */
    volatile uint32_t   ci_rlm_avg;                 /**< Initiator Read Latency Monitor Average */
    volatile uint8_t    res30[232];                 /**< reserved */

    /* QMan Interrupt and Error Registers */
    volatile uint32_t   err_isr;                    /**< QMan Error Interrupt Status Register */
    volatile uint32_t   err_ier;                    /**< QMan Error Interrupt Enable Register */
    volatile uint32_t   err_isdr;                   /**< QMan Error Interrupt Status Disable Register */
    volatile uint32_t   err_iir;                    /**< QMan Error Interrupt Inhibit Register */
    volatile uint8_t    res31[4];                   /**< reserved */
    volatile uint32_t   err_her;                    /**< QMan Error Halt Enable Register */

} _PackedType t_QmRegs;

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/

#define MODULE_NAME_SIZE            30

#define PORTALS_OFFSET_CE(portal)   (0x4000 * portal)
#define PORTALS_OFFSET_CI(portal)   (0x1000 * portal)

#define PFDR_ENTRY_SIZE             64 /* 64 bytes */
#define FQD_ENTRY_SIZE              64 /* 64 bytes */

/* Compilation constants */
#define DQRR_MAXFILL        15
#define EQCR_THRESH         1    /* reread h/w CI when running out of space */

/**************************************************************************//**
 @Description       Register defines
*//***************************************************************************/

/* Assists for QMAN_MCR */
#define MCR_INIT_PFDR               0x01000000
#define MCR_get_rslt(v)             (uint8_t)((v) >> 24)
#define MCR_rslt_idle(r)            (!rslt || (rslt >= 0xf0))
#define MCR_rslt_ok(r)              (rslt == 0xf0)
#define MCR_rslt_eaccess(r)         (rslt == 0xf8)
#define MCR_rslt_inval(r)           (rslt == 0xff)

/* masks */
#define REV1_MAJOR_MASK             0x0000FF00
#define REV1_MINOR_MASK             0x000000FF

#define REV2_INTEG_MASK             0x00FF0000
#define REV2_ERR_MASK               0x0000FF00
#define REV2_CFG_MASK               0x000000FF

#define AR_ENABLE                   0x80000000
#define AR_PRIORITY                 0x40000000
#define AR_STASH                    0x20000000
#define AR_SIZE_MASK                0x0000003f

#define ECIR_PORTAL_TYPE            0x20000000
#define ECIR_PORTAL_MASK            0x1f000000
#define ECIR_FQID_MASK              0x00ffffff

#define CI_SCHED_CFG_EN             0x80000000
/* shifts */
#define REV1_MAJOR_SHIFT            8
#define REV1_MINOR_SHIFT            0

#define REV2_INTEG_SHIFT            16
#define REV2_ERR_SHIFT              8
#define REV2_CFG_SHIFT              0

#define AR_SIZE_SHIFT               0

#define ECIR_PORTAL_SHIFT           24
#define ECIR_FQID_SHIFT             0

#define CI_SCHED_CFG_SRCCIV_SHIFT   24
#define CI_SCHED_CFG_SRQ_W_SHIFT    8
#define CI_SCHED_CFG_RW_W_SHIFT     4
#define CI_SCHED_CFG_BMAN_W_SHIFT   0


/********* CGR ******************************/
#define QM_CGR_TARG_FIRST_SWPORTAL     0x80000000
#define QM_CGR_TARG_FIRST_DCPORTAL     0x00200000
#define QM_CGR_TARGET_SWP(portlaId)    (QM_CGR_TARG_FIRST_SWPORTAL >> portlaId)
#define QM_CGR_TARGET_DCP(portlaId)    (QM_CGR_TARG_FIRST_DCPORTAL >> portlaId)


#define QM_DCP_CFG_ED               0x00000100
/*
#define CGR_VALID                       0x80
#define CGR_VERB_INIT                   0x50
#define CGR_VERB_MODIFY                 0x51
#define CGR_WRITE_ALL                   0x07FF
#define CGR_WRITE_ENABLE_CSCN           0x0010
#define CGR_WRITE_ENABLE_GREEN_MODIFY   0x0380
#define CGR_WRITE_ENABLE_YELLOW_MODIFY  0x0240
#define CGR_WRITE_ENABLE_RED_MODIFY     0x0120


#define CGR_MODE_BYTE               0x00
#define CGR_MODE_FRAME              0x01
#define GCR_ENABLE_WRED             0x01
#define GCR_ENABLE_TD               0x01
#define GCR_ENABLE_CSCN             0x01
*/


/* Lock/unlock frame queues, subject to the "UNLOCKED" flag. This is about
 * inter-processor locking only. */
#define FQLOCK(fq)                              \
    do {                                        \
        if (fq->flags & QMAN_FQ_FLAG_LOCKED)    \
            XX_LockSpinlock(&fq->fqlock);       \
    } while(0)
#define FQUNLOCK(fq)                            \
    do {                                        \
        if (fq->flags & QMAN_FQ_FLAG_LOCKED)    \
            XX_UnlockSpinlock(&fq->fqlock);     \
    } while(0)

/* Lock/unlock portals, subject to "UNLOCKED" flag. This is about disabling
 * interrupts/preemption and, if FLAG_UNLOCKED isn't defined, inter-processor
 * locking as well. */
#define NCSW_PLOCK(p) ((t_QmPortal*)(p))->irq_flags = XX_DisableAllIntr()
#define PUNLOCK(p) XX_RestoreAllIntr(((t_QmPortal*)(p))->irq_flags)


typedef void  (t_QmLoopDequeueRing)(t_Handle h_QmPortal);

/* Follows WQ_CS_CFG0-5 */
typedef enum {
    e_QM_WQ_SW_PORTALS = 0,
    e_QM_WQ_POOLS,
    e_QM_WQ_DCP0,
    e_QM_WQ_DCP1,
    e_QM_WQ_DCP2,
    e_QM_WQ_DCP3
} e_QmWqClass;

typedef enum {
    e_QM_PORTAL_NO_DEQUEUES = 0,
    e_QM_PORTAL_PRIORITY_PRECEDENCE_INTRA_CLASS_SCHEDULING,
    e_QM_PORTAL_ACTIVE_FQ_PRECEDENCE_INTRA_CLASS_SCHEDULING,
    e_QM_PORTAL_ACTIVE_FQ_PRECEDENCE_OVERRIDE_INTRA_CLASS_SCHEDULING
} e_QmPortalDequeueCommandType;

typedef enum e_QmInterModuleCounters {
    e_QM_IM_COUNTERS_SFDR_IN_USE = 0,
    e_QM_IM_COUNTERS_PFDR_IN_USE,
    e_QM_IM_COUNTERS_PFDR_FREE_POOL
} e_QmInterModuleCounters;

typedef struct t_QmInterModulePortalInitParams {
    uint8_t             portalId;
    uint8_t             stashDestQueue;
    uint16_t            liodn;
    uint16_t            dqrrLiodn;
    uint16_t            fdFqLiodn;
} t_QmInterModulePortalInitParams;

typedef struct t_QmCg {
    t_Handle                h_Qm;
    t_Handle                h_QmPortal;
    t_QmExceptionsCallback  *f_Exception;
    t_Handle                h_App;
    uint8_t                 id;
} t_QmCg;

typedef struct {
    uintptr_t                   swPortalsBaseAddress;   /**< QM Software Portals Base Address (virtual) */
    uint32_t                    partFqidBase;
    uint32_t                    partNumOfFqids;
    uint32_t                    totalNumOfFqids;
    uint32_t                    rtFramesDepth;
    uint32_t                    fqdMemPartitionId;
    uint32_t                    pfdrMemPartitionId;
    uint32_t                    pfdrThreshold;
    uint32_t                    sfdrThreshold;
    uint32_t                    pfdrBaseConstant;
    uint16_t                    liodn;
    t_QmDcPortalParams          dcPortalsParams[DPAA_MAX_NUM_OF_DC_PORTALS];
} t_QmDriverParams;

typedef struct {
    uint8_t                     guestId;
    t_Handle                    h_RsrvFqidMm;
    t_Handle                    h_FqidMm;
    t_Handle                    h_Session;
    char                        moduleName[MODULE_NAME_SIZE];
    t_Handle                    h_Portals[DPAA_MAX_NUM_OF_SW_PORTALS];
    t_QmRegs                    *p_QmRegs;
    uint32_t                    *p_FqdBase;
    uint32_t                    *p_PfdrBase;
    uint32_t                    exceptions;
    t_QmExceptionsCallback      *f_Exception;
    t_Handle                    h_App;
    uintptr_t                   errIrq;                 /**< error interrupt line; NO_IRQ if interrupts not used */
    uint32_t                    numOfPfdr;
    uint16_t                    partNumOfCgs;
    uint16_t                    partCgsBase;
    uint8_t                     cgsUsed[QM_MAX_NUM_OF_CGS];
t_Handle lock;
    t_QmDriverParams            *p_QmDriverParams;
} t_Qm;

typedef struct {
    uint32_t                        hwExtStructsMemAttr;
    uint8_t                         dqrrSize;
    bool                            pullMode;
    bool                            dequeueDcaMode;
    bool                            dequeueUpToThreeFrames;
    e_QmPortalDequeueCommandType    commandType;
    uint8_t                         userToken;
    bool                            specifiedWq;
    bool                            dedicatedChannel;
    bool                            dedicatedChannelHasPrecedenceOverPoolChannels;
    uint8_t                         poolChannels[QM_MAX_NUM_OF_POOL_CHANNELS];
    uint8_t                         poolChannelId;
    uint8_t                         wqId;
    uint16_t                        fdLiodnOffset;
    uint8_t                         stashDestQueue;
    uint8_t                         eqcr;
    bool                            eqcrHighPri;
    bool                            dqrr;
    uint16_t                        dqrrLiodn;
    bool                            dqrrHighPri;
    bool                            fdFq;
    uint16_t                        fdFqLiodn;
    bool                            fdFqHighPri;
    bool                            fdFqDrop;
} t_QmPortalDriverParams;

/*typedef struct t_QmPortalCgs{
    uint32_t    cgsMask[QM_MAX_NUM_OF_CGS/32];
}t_QmPortalCgs;
*/
typedef struct t_QmPortal {
    t_Handle                    h_Qm;
    struct qm_portal            *p_LowQmPortal;
    uint32_t                    bits;    /* PORTAL_BITS_*** - dynamic, strictly internal */
    t_Handle                    h_App;
    t_QmLoopDequeueRing         *f_LoopDequeueRingCB;
    bool                        pullMode;
    /* To avoid overloading the term "flags", we use these 2; */
    uint32_t                    options;    /* QMAN_PORTAL_FLAG_*** - static, caller-provided */
    uint32_t                    irq_flags;
    /* The wrap-around eq_[prod|cons] counters are used to support
     * QMAN_ENQUEUE_FLAG_WAIT_SYNC. */
    uint32_t                    eqProd;
    volatile int                disable_count;
    struct qman_cgrs            cgrs[2]; /* 2-element array. cgrs[0] is mask, cgrs[1] is previous snapshot. */
    /* If we receive a DQRR or MR ring entry for a "null" FQ, ie. for which
     * FQD::contextB is NULL rather than pointing to a FQ object, we use
     * these handlers. (This is not considered a fast-path mechanism.) */
    t_Handle                    cgsHandles[QM_MAX_NUM_OF_CGS];
    struct qman_fq_cb           *p_NullCB;
    t_QmReceivedFrameCallback   *f_DfltFrame;
    t_QmRejectedFrameCallback   *f_RejectedFrame;
    t_QmPortalDriverParams      *p_QmPortalDriverParams;
} t_QmPortal;

struct qman_fq {
    struct qman_fq_cb   cb;
    t_Handle            h_App;
    t_Handle            h_QmFqr;
    t_Handle            fqlock;
    uint32_t            fqid;
    uint32_t            fqidOffset;
    uint32_t            flags;
    /* s/w-visible states. Ie. tentatively scheduled + truly scheduled +
     * active + held-active + held-suspended are just "sched". Things like
     * 'retired' will not be assumed until it is complete (ie.
     * QMAN_FQ_STATE_CHANGING is set until then, to indicate it's completing
     * and to gate attempts to retry the retire command). Note, park
     * commands do not set QMAN_FQ_STATE_CHANGING because it's technically
     * impossible in the case of enqueue DCAs (which refer to DQRR ring
     * index rather than the FQ that ring entry corresponds to), so repeated
     * park commands are allowed (if you're silly enough to try) but won't
     * change FQ state, and the resulting park notifications move FQs from
     * 'sched' to 'parked'. */
    enum qman_fq_state  state;
    int                 cgr_groupid;
};

typedef struct {
    t_Handle                    h_Qm;
    t_Handle                    h_QmPortal;
    e_QmFQChannel               channel;
    uint8_t                     workQueue;
    bool                        shadowMode;
    uint32_t                    fqidBase;
    uint32_t                    numOfFqids;
    t_QmFqrDrainedCompletionCB  *f_CompletionCB;
    t_Handle                    h_App;
    uint32_t                    numOfDrainedFqids;
    bool                        *p_DrainedFqs;
    struct qman_fq              **p_Fqs;
} t_QmFqr;


/****************************************/
/*       Inter-Module functions         */
/****************************************/
uint32_t QmGetCounter(t_Handle h_Qm, e_QmInterModuleCounters counter);
t_Error  QmGetRevision(t_Handle h_Qm, t_QmRevisionInfo *p_QmRevisionInfo);
t_Error  QmGetSetPortalParams(t_Handle h_Qm, t_QmInterModulePortalInitParams *p_PortalParams);
t_Error  QmFreeDcPortal(t_Handle h_Qm, e_DpaaDcPortal dcPortalId);
uint32_t QmFqidGet(t_Qm *p_Qm, uint32_t size, uint32_t alignment, bool force, uint32_t base);
t_Error  QmFqidPut(t_Qm *p_Qm, uint32_t base);
t_Error  QmGetCgId(t_Handle h_Qm, uint8_t *p_CgId);
t_Error  QmFreeCgId(t_Handle h_Qm, uint8_t cgId);


static __inline__ void QmSetPortalHandle(t_Handle h_Qm, t_Handle h_Portal, e_DpaaSwPortal portalId)
{
    ASSERT_COND(!((t_Qm*)h_Qm)->h_Portals[portalId] || !h_Portal);
    ((t_Qm*)h_Qm)->h_Portals[portalId] = h_Portal;
}

static __inline__ t_Handle QmGetPortalHandle(t_Handle h_Qm)
{
    t_Qm        *p_Qm       = (t_Qm*)h_Qm;

    ASSERT_COND(p_Qm);
    return p_Qm->h_Portals[CORE_GetId()];
}

static __inline__ uint32_t GenerateCgrThresh(uint64_t val, int roundup)
{
    uint32_t e = 0;    /* co-efficient, exponent */
    uint32_t oddbit = 0;
    while(val > 0xff) {
        oddbit = (uint32_t)val & 1;
        val >>= 1;
        e++;
        if(roundup && oddbit)
            val++;
    }
    return (uint32_t)((val << 5) | e);
}

static __inline__ t_Error SetException(t_Qm *p_Qm, e_QmExceptions exception, bool enable)
{
    uint32_t            bitMask = 0;

    ASSERT_COND(p_Qm);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if(bitMask)
    {
        if (enable)
            p_Qm->exceptions |= bitMask;
        else
            p_Qm->exceptions &= ~bitMask;
   }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}


#endif /* __QM_H */
