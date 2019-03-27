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
 @File          bm.h

 @Description   BM header
*//***************************************************************************/
#ifndef __BM_H
#define __BM_H

#include "xx_common.h"
#include "bm_ext.h"
#include "mm_ext.h"

#include "bman_private.h"
#include "bm_ipc.h"


#define __ERR_MODULE__  MODULE_BM

#define BM_NUM_OF_POOLS             64
#define BM_NUM_OF_PM                8

/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/
#define BM_EX_INVALID_COMMAND       0x00000010
#define BM_EX_FBPR_THRESHOLD        0x00000008
#define BM_EX_MULTI_ECC             0x00000004
#define BM_EX_SINGLE_ECC            0x00000002
#define BM_EX_POOLS_AVAIL_STATE     0x00000001

#define GET_EXCEPTION_FLAG(bitMask, exception)                          \
switch(exception){                                                      \
    case e_BM_EX_INVALID_COMMAND:                                       \
        bitMask = BM_EX_INVALID_COMMAND; break;                         \
    case e_BM_EX_FBPR_THRESHOLD:                                        \
        bitMask = BM_EX_FBPR_THRESHOLD; break;                          \
    case e_BM_EX_SINGLE_ECC:                                            \
        bitMask = BM_EX_SINGLE_ECC; break;                              \
    case e_BM_EX_MULTI_ECC:                                             \
        bitMask = BM_EX_MULTI_ECC; break;                               \
    default: bitMask = 0;break;                                         \
}

/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/
/* BM defaults */
#define DEFAULT_exceptions                  (BM_EX_INVALID_COMMAND      |\
                                            BM_EX_FBPR_THRESHOLD        |\
                                            BM_EX_MULTI_ECC             |\
                                            BM_EX_SINGLE_ECC            )

#define DEFAULT_fbprThreshold               0
/* BM-Portal defaults */
#define DEFAULT_memAttr                     MEMORY_ATTR_CACHEABLE

/* BM-Pool defaults */
#define DEFAULT_dynamicBpid                 TRUE
#define DEFAULT_useDepletion                FALSE
#define DEFAULT_useStockpile                FALSE
#define DEFAULT_numOfBufsPerCmd             8

/**************************************************************************//**
 @Description       Memory Mapped Registers
*//***************************************************************************/

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

typedef _Packed struct
{
    /* BMan Buffer Pool Configuration & Status Registers */
    volatile uint32_t   swdet[BM_NUM_OF_POOLS];     /**< S/W Portal depletion entry threshold */
    volatile uint32_t   hwdet[BM_NUM_OF_POOLS];     /**< H/W Portal depletion entry threshold */
    volatile uint32_t   swdxt[BM_NUM_OF_POOLS];     /**< S/W Portal depletion exit threshold */
    volatile uint32_t   hwdxt[BM_NUM_OF_POOLS];     /**< H/W Portal depletion exit threshold */
    volatile uint32_t   sdcnt[BM_NUM_OF_POOLS];     /**< S/W Portal depletion count */
    volatile uint32_t   hdcnt[BM_NUM_OF_POOLS];     /**< H/W Portal depletion count */
    volatile uint32_t   content[BM_NUM_OF_POOLS];   /**< Snapshot of buffer count in Pool */
    volatile uint32_t   hdptr[BM_NUM_OF_POOLS];     /**< Head Pointer for Pool's FBPR list. */

    /* Free Buffer Proxy Record (FBPR) Manager Query Registers */
    volatile uint32_t   fbpr_fpc;                   /**< FBPR Free Pool Count */
    volatile uint32_t   fbpr_fp_lwit;               /**< FBPR Free Pool Low Watermark Interrupt Threshold  */
    volatile uint8_t    res1[248];                  /**< reserved */

    /* Performance Monitor (PM) Configuration Register */
    volatile uint32_t   cmd_pm_cfg[BM_NUM_OF_PM];   /**< BMan Command Performance Monitor configuration registers. */
    volatile uint32_t   fl_pm_cfg[BM_NUM_OF_PM];    /**< BMan Free List Performance Monitor configuration registers */
    volatile uint8_t    res2[192];                  /**< reserved */

    /* BMan Error Capture Registers */
    volatile uint32_t   ecsr;                       /**< BMan Error Capture Status Register */
    volatile uint32_t   ecir;                       /**< BMan Error Capture Information Register */
    volatile uint32_t   eadr;                       /**< BMan Error Capture Address Register */
    volatile uint8_t    res3[4];                    /**< reserved */
    volatile uint32_t   edata[8];                   /**< BMan ECC Error Data Register */
    volatile uint32_t   sbet;                       /**< BMan Single Bit ECC Error Threshold Register */
    volatile uint32_t   efcr;                       /**< BMan Error Fetch Capture Register */
    volatile uint32_t   efar;                       /**< BMan Error Fetch Address Register */
    volatile uint8_t    res4[68];                   /**< reserved */
    volatile uint32_t   sbec0;                      /**< BMan Single Bit ECC Error Count 0 Register */
    volatile uint32_t   sbec1;                      /**< BMan Single Bit ECC Error Count 1 Register */
    volatile uint8_t    res5[368];                  /**< reserved */

    /* BMan ID/Revision Registers */
    volatile uint32_t   ip_rev_1;                   /**< BMan IP Block Revision 1 register */
    volatile uint32_t   ip_rev_2;                   /**< BMan IP Block Revision 2 register */

    /* CoreNet Initiator Interface Memory Window Configuration Registers */
    volatile uint32_t   fbpr_bare;                  /**< Data Structure Extended Base Address Register */
    volatile uint32_t   fbpr_bar;                   /**< Data Structure Base Address Register */
    volatile uint8_t    res6[8];                    /**< reserved */
    volatile uint32_t   fbpr_ar;                    /**< Data Structure Attributes Register */
    volatile uint8_t    res7[240];                  /**< reserved */
    volatile uint32_t   srcidr;                     /**< BMan Source ID Register */
    volatile uint32_t   liodnr;                     /**< BMan Logical I/O Device Number Register */
    volatile uint8_t    res8[244];                  /**< reserved */

    /* BMan Interrupt and Error Registers */
    volatile uint32_t   err_isr;                    /**< BMan Error Interrupt Status Register */
    volatile uint32_t   err_ier;                    /**< BMan Error Interrupt Enable Register */
    volatile uint32_t   err_isdr;                   /**< BMan Error Interrupt Status Disable Register */
    volatile uint32_t   err_iir;                    /**< BMan Error Interrupt Inhibit Register */
    volatile uint32_t   err_ifr;                    /**< BMan Error Interrupt Force Register */
} _PackedType t_BmRegs;

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */

/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/
#define MODULE_NAME_SIZE            30

#define FBPR_ENTRY_SIZE             64 /* 64 bytes */

/* Compilation constants */
#define RCR_THRESH      2    /* reread h/w CI when running out of space */
#define RCR_ITHRESH     4    /* if RCR congests, interrupt threshold */

/* Lock/unlock portals, subject to "UNLOCKED" flag */
#define NCSW_PLOCK(p) ((t_BmPortal*)(p))->irq_flags = XX_DisableAllIntr()
#define PUNLOCK(p) XX_RestoreAllIntr(((t_BmPortal*)(p))->irq_flags)

#define BM_RCR_RING         0
#define BM_NUM_OF_RINGS     1

/**************************************************************************//**
 @Description       Register defines
*//***************************************************************************/

/* masks */
#define REV1_MAJOR_MASK             0x0000FF00
#define REV1_MINOR_MASK             0x000000FF

#define REV2_INTEG_MASK             0x00FF0000
#define REV2_ERR_MASK               0x0000FF00
#define REV2_CFG_MASK               0x000000FF

#define AR_PRIORITY                 0x40000000
#define AR_SIZE_MASK                0x0000003f

/* shifts */
#define REV1_MAJOR_SHIFT            8
#define REV1_MINOR_SHIFT            0

#define REV2_INTEG_SHIFT            16
#define REV2_ERR_SHIFT              8
#define REV2_CFG_SHIFT              0

#define AR_SIZE_SHIFT               0

typedef uint8_t bmRingType_t;
typedef uint8_t (t_BmUpdateCb)(struct bm_portal *p_BmPortalLow);
typedef void    (t_BmPrefetchCb)(struct bm_portal *p_BmPortalLow);
typedef void    (t_BmCommitCb)(struct bm_portal *p_BmPortalLow, uint8_t myverb);

typedef struct {
    bool                        useStockpile;       /**<  */
    bool                        dynamicBpid;        /**< boolean indicates use of dynamic Bpid */
    bool                        useDepletion;       /**< boolean indicates use of depletion */
    uint32_t                    depletionThresholds[MAX_DEPLETION_THRESHOLDS];      /**< depletion-entry/exit thresholds, if useThresholds is set. NB:
                                                         this is only allowed if useThresholds is used and
                                                         when run in the control plane (which controls Bman CCSR) */
} t_BmPoolDriverParams;

typedef struct BmPool {
    uint8_t                     bpid;           /**< index of the buffer pool to encapsulate (0-63) */
    t_Handle                    h_Bm;
    t_Handle                    h_BmPortal;
    bool                        shadowMode;
    uint32_t                    numOfBuffers;   /**< Number of buffers use by this pool */
    t_BufferPoolInfo            bufferPoolInfo; /**< Data buffers pool information */
    uint32_t                    flags;          /**< bit-mask of BMAN_POOL_FLAG_*** options */
    t_Handle                    h_App;          /**< opaque user value passed as a parameter to 'cb' */
    t_BmDepletionCallback       *f_Depletion;   /**< depletion-entry/exit callback, if BMAN_POOL_FLAG_DEPLETION is set */
    uint32_t                    swDepletionCount;
    uint32_t                    hwDepletionCount;
    /* stockpile state - NULL unless BMAN_POOL_FLAG_STOCKPILE is set */
    struct bm_buffer            *sp;
    uint16_t                    spFill;
    uint8_t                     spBufsCmd;
    uint16_t                    spMaxBufs;
    uint16_t                    spMinBufs;
    bool                        noBuffCtxt;

    t_BmPoolDriverParams        *p_BmPoolDriverParams;
} t_BmPool;

typedef struct {
    t_BmUpdateCb            *f_BmUpdateCb;
    t_BmPrefetchCb          *f_BmPrefetchCb;
    t_BmCommitCb            *f_BmCommitCb;
} t_BmPortalCallbacks;

typedef struct {
    uint32_t                hwExtStructsMemAttr;
    struct bman_depletion   mask;
} t_BmPortalDriverParams;

typedef struct {
    t_Handle                h_Bm;
    struct bm_portal        *p_BmPortalLow;
    t_BmPortalCallbacks     cbs[BM_NUM_OF_RINGS];
    uintptr_t               irq;
    int                     cpu; /* This is used for any "core-affine" portals, ie. default portals
                                  * associated to the corresponding cpu. -1 implies that there is no core
                                  * affinity configured. */
    struct bman_depletion   pools[2];   /**< 2-element array. pools[0] is mask, pools[1] is snapshot. */
    uint32_t                flags;        /**< BMAN_PORTAL_FLAG_*** - static, caller-provided */
    uint32_t                irq_flags;
    int                     thresh_set;
    uint32_t                slowpoll;
    uint32_t                rcrProd;   /**< The wrap-around rcr_[prod|cons] counters are used to support BMAN_RELEASE_FLAG_WAIT_SYNC. */
    uint32_t                rcrCons;
    /**< 64-entry hash-table of pool objects that are tracking depletion
     * entry/exit (ie. BMAN_POOL_FLAG_DEPLETION). This isn't fast-path, so
     * we're not fussy about cache-misses and so forth - whereas the above
     * members should all fit in one cacheline.
     * BTW, with BM_MAX_NUM_OF_POOLS entries in the hash table and BM_MAX_NUM_OF_POOLS buffer pools to track,
     * you'll never guess the hash-function ... */
    t_BmPool                *depletionPoolsTable[BM_MAX_NUM_OF_POOLS];
    t_BmPortalDriverParams  *p_BmPortalDriverParams;
} t_BmPortal;

typedef struct {
    uint8_t                     partBpidBase;
    uint8_t                     partNumOfPools;
    uint32_t                    totalNumOfBuffers;      /**< total number of buffers */
    uint32_t                    fbprMemPartitionId;
    uint32_t                    fbprThreshold;
    uint16_t                    liodn;
} t_BmDriverParams;

typedef struct {
    uint8_t                     guestId;
    t_Handle                    h_BpidMm;
    t_Handle                    h_SpinLock;
    t_Handle                    h_Portals[DPAA_MAX_NUM_OF_SW_PORTALS];
    t_Handle                    h_Session;
    char                        moduleName[MODULE_NAME_SIZE];
    t_BmRegs                    *p_BmRegs;
    void                        *p_FbprBase;
    uint32_t                    exceptions;
    t_BmExceptionsCallback      *f_Exception;
    t_Handle                    h_App;
    uintptr_t                   errIrq;                 /**< error interrupt line; NO_IRQ if interrupts not used */
    t_BmDriverParams            *p_BmDriverParams;
} t_Bm;

static __inline__ void BmSetPortalHandle(t_Handle h_Bm, t_Handle h_Portal, e_DpaaSwPortal portalId)
{
    ASSERT_COND(!((t_Bm*)h_Bm)->h_Portals[portalId] || !h_Portal);
    ((t_Bm*)h_Bm)->h_Portals[portalId] = h_Portal;
}

static __inline__ t_Handle BmGetPortalHandle(t_Handle h_Bm)
{
    t_Bm *p_Bm = (t_Bm*)h_Bm;
    ASSERT_COND(p_Bm);
    return p_Bm->h_Portals[CORE_GetId()];
}

static __inline__ uint8_t BmUpdate(t_BmPortal *p_BmPortal, bmRingType_t type)
{
    return p_BmPortal->cbs[type].f_BmUpdateCb(p_BmPortal->p_BmPortalLow);
}

static __inline__ void BmPrefetch(t_BmPortal *p_BmPortal, bmRingType_t type)
{
    if (p_BmPortal->cbs[type].f_BmPrefetchCb)
        p_BmPortal->cbs[type].f_BmPrefetchCb(p_BmPortal->p_BmPortalLow);
}

static __inline__ void BmCommit(t_BmPortal *p_BmPortal, bmRingType_t type, uint8_t myverb)
{
    p_BmPortal->cbs[type].f_BmCommitCb(p_BmPortal->p_BmPortalLow, myverb);
}

static __inline__ uint32_t BmBpidGet(t_Bm *p_Bm, bool force, uint32_t base)
{
    uint64_t ans, size = 1;
    uint64_t alignment = 1;

    if (force)
    {
        if (MM_InRange(p_Bm->h_BpidMm, (uint64_t)base))
        {
            ans = MM_GetForce(p_Bm->h_BpidMm,
                              base,
                              size,
                              "BM BPID MEM");
            ans = base;
        }
        else if (p_Bm->h_Session)
        {
            t_BmIpcMsg              msg;
            t_BmIpcReply            reply;
            uint32_t                replyLength;
            t_BmIpcBpidParams       ipcBpid;
            t_Error                 errCode = E_OK;

            memset(&msg, 0, sizeof(t_BmIpcMsg));
            memset(&reply, 0, sizeof(t_BmIpcReply));
            ipcBpid.bpid        = (uint8_t)base;
            msg.msgId           = BM_FORCE_BPID;
            memcpy(msg.msgBody, &ipcBpid, sizeof(t_BmIpcBpidParams));
            replyLength = sizeof(uint32_t) + sizeof(uint32_t);
            if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                             (uint8_t*)&msg,
                                             sizeof(msg.msgId) + sizeof(t_BmIpcBpidParams),
                                             (uint8_t*)&reply,
                                             &replyLength,
                                             NULL,
                                             NULL)) != E_OK)
            {
                REPORT_ERROR(MAJOR, errCode, NO_MSG);
                return (uint32_t)ILLEGAL_BASE;
            }
            if (replyLength != (sizeof(uint32_t) + sizeof(uint32_t)))
            {
                REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("IPC reply length mismatch"));
                return (uint32_t)ILLEGAL_BASE;
            }
            memcpy((uint8_t*)&ans, reply.replyBody, sizeof(uint32_t));
        }
        else
        {
            DBG(WARNING, ("No Ipc - can't validate bpid."));
            ans = base;
        }
    }
    else
        ans = MM_Get(p_Bm->h_BpidMm,
                     size,
                     alignment,
                     "BM BPID MEM");
    KASSERT(ans < UINT32_MAX, ("Oops, %lx > UINT32_MAX!\n", ans));
    return (uint32_t)ans;
}

static __inline__ t_Error BmBpidPut(t_Bm *p_Bm, uint32_t base)
{
    if (MM_InRange(p_Bm->h_BpidMm, (uint64_t)base))
    {
        if (MM_Put(p_Bm->h_BpidMm, (uint64_t)base) != base)
            return E_OK;
        else
            return ERROR_CODE(E_NOT_FOUND);
    }
    else if (p_Bm->h_Session)
    {
        t_BmIpcMsg              msg;
        t_BmIpcBpidParams       ipcBpid;
        t_Error                 errCode = E_OK;

        memset(&msg, 0, sizeof(t_BmIpcMsg));
        ipcBpid.bpid        = (uint8_t)base;
        msg.msgId           = BM_PUT_BPID;
        memcpy(msg.msgBody, &ipcBpid, sizeof(t_BmIpcBpidParams));
        if ((errCode = XX_IpcSendMessage(p_Bm->h_Session,
                                         (uint8_t*)&msg,
                                         sizeof(msg.msgId) + sizeof(t_BmIpcBpidParams),
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL)) != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
    }
    else
        DBG(WARNING, ("No Ipc - can't validate bpid."));
    return E_OK;
}

/****************************************/
/*       Inter-Module functions        */
/****************************************/
typedef enum e_BmInterModuleCounters {
    e_BM_IM_COUNTERS_FBPR = 0,
    e_BM_IM_COUNTERS_POOL_CONTENT,
    e_BM_IM_COUNTERS_POOL_SW_DEPLETION,
    e_BM_IM_COUNTERS_POOL_HW_DEPLETION
} e_BmInterModuleCounters;


t_Error BmSetPoolThresholds(t_Handle h_Bm, uint8_t bpid, const uint32_t *thresholds);
t_Error BmUnSetPoolThresholds(t_Handle h_Bm, uint8_t bpid);
uint8_t BmPortalAcquire(t_Handle h_BmPortal, uint8_t bpid, struct bm_buffer *bufs, uint8_t num);
t_Error BmPortalRelease(t_Handle h_BmPortal, uint8_t bpid, struct bm_buffer *bufs, uint8_t num, uint32_t flags);
t_Error BmPortalQuery(t_Handle h_BmPortal, struct bman_depletion *p_Pools, bool depletion);
uint32_t BmGetCounter(t_Handle h_Bm, e_BmInterModuleCounters counter, uint8_t bpid);
t_Error BmGetRevision(t_Handle h_Bm, t_BmRevisionInfo *p_BmRevisionInfo);


#endif /* __BM_H */
