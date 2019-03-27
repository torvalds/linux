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
 @File          fsl_bman.h

 @Description   BM header
*//***************************************************************************/
#ifndef __FSL_BMAN_H
#define __FSL_BMAN_H

#include "std_ext.h"


/*************************************************/
/*   BMan s/w corenet portal, low-level i/face   */
/*************************************************/
typedef enum {
    e_BmPortalPCI = 0,          /* PI index, cache-inhibited */
    e_BmPortalPCE,              /* PI index, cache-enabled */
    e_BmPortalPVB               /* valid-bit */
} e_BmPortalProduceMode;

typedef enum {
    e_BmPortalRcrCCI = 0,      /* CI index, cache-inhibited */
    e_BmPortalRcrCCE           /* CI index, cache-enabled */
} e_BmPortalRcrConsumeMode;

/* Portal constants */
#define BM_RCR_SIZE        8

/* Hardware constants */
enum bm_isr_reg {
    bm_isr_status = 0,
    bm_isr_enable = 1,
    bm_isr_disable = 2,
    bm_isr_inhibit = 3
};

/* Represents s/w corenet portal mapped data structures */
struct bm_rcr_entry;    /* RCR (Release Command Ring) entries */
struct bm_mc_command;    /* MC (Management Command) command */
struct bm_mc_result;    /* MC result */

/* This type represents a s/w corenet portal space, and is used for creating the
 * portal objects within it (RCR, etc) */
struct bm_portal;

/* This wrapper represents a bit-array for the depletion state of the 64 Bman
 * buffer pools. */
struct bman_depletion {
    uint32_t __state[2];
};
#define __bmdep_word(x) ((x) >> 5)
#define __bmdep_shift(x) ((x) & 0x1f)
#define __bmdep_bit(x) (0x80000000 >> __bmdep_shift(x))
static __inline__ void bman_depletion_init(struct bman_depletion *c)
{
    c->__state[0] = c->__state[1] = 0;
}
static __inline__ void bman_depletion_fill(struct bman_depletion *c)
{
    c->__state[0] = c->__state[1] = (uint32_t)~0;
}
static __inline__ int bman_depletion_get(const struct bman_depletion *c, uint8_t bpid)
{
    return (int)(c->__state[__bmdep_word(bpid)] & __bmdep_bit(bpid));
}
static __inline__ void bman_depletion_set(struct bman_depletion *c, uint8_t bpid)
{
    c->__state[__bmdep_word(bpid)] |= __bmdep_bit(bpid);
}
static __inline__ void bman_depletion_unset(struct bman_depletion *c, uint8_t bpid)
{
    c->__state[__bmdep_word(bpid)] &= ~__bmdep_bit(bpid);
}

/* ------------------------------ */
/* --- Portal enumeration API --- */

/* ------------------------------ */
/* --- Buffer pool allocation --- */
#define BM_POOL_THRESH_SW_ENTER 0
#define BM_POOL_THRESH_SW_EXIT  1
#define BM_POOL_THRESH_HW_ENTER 2
#define BM_POOL_THRESH_HW_EXIT  3

/* --------------- */
/* --- RCR API --- */

/* Create/destroy */
t_Error bm_rcr_init(struct bm_portal *portal,
                    e_BmPortalProduceMode pmode,
                    e_BmPortalRcrConsumeMode cmode);
void bm_rcr_finish(struct bm_portal *portal);

/* Start/abort RCR entry */
struct bm_rcr_entry *bm_rcr_start(struct bm_portal *portal);
void bm_rcr_abort(struct bm_portal *portal);

/* For PI modes only. This presumes a started but uncommitted RCR entry. If
 * there's no more room in the RCR, this function returns NULL. Otherwise it
 * returns the next RCR entry and increments an internal PI counter without
 * flushing it to h/w. */
struct bm_rcr_entry *bm_rcr_pend_and_next(struct bm_portal *portal, uint8_t myverb);

/* Commit RCR entries, including pending ones (aka "write PI") */
void bm_rcr_pci_commit(struct bm_portal *portal, uint8_t myverb);
void bm_rcr_pce_prefetch(struct bm_portal *portal);
void bm_rcr_pce_commit(struct bm_portal *portal, uint8_t myverb);
void bm_rcr_pvb_commit(struct bm_portal *portal, uint8_t myverb);

/* Track h/w consumption. Returns non-zero if h/w had consumed previously
 * unconsumed RCR entries. */
uint8_t bm_rcr_cci_update(struct bm_portal *portal);
void bm_rcr_cce_prefetch(struct bm_portal *portal);
uint8_t bm_rcr_cce_update(struct bm_portal *portal);
/* Returns the number of available RCR entries */
uint8_t bm_rcr_get_avail(struct bm_portal *portal);
/* Returns the number of unconsumed RCR entries */
uint8_t bm_rcr_get_fill(struct bm_portal *portal);

/* Read/write the RCR interrupt threshold */
uint8_t bm_rcr_get_ithresh(struct bm_portal *portal);
void bm_rcr_set_ithresh(struct bm_portal *portal, uint8_t ithresh);


/* ------------------------------ */
/* --- Management command API --- */

/* Create/destroy */
t_Error bm_mc_init(struct bm_portal *portal);
void bm_mc_finish(struct bm_portal *portal);

/* Start/abort mgmt command */
struct bm_mc_command *bm_mc_start(struct bm_portal *portal);
void bm_mc_abort(struct bm_portal *portal);

/* Writes 'verb' with appropriate 'vbit'. Invalidates and pre-fetches the
 * response. */
void bm_mc_commit(struct bm_portal *portal, uint8_t myverb);

/* Poll for result. If NULL, invalidates and prefetches for the next call. */
struct bm_mc_result *bm_mc_result(struct bm_portal *portal);


/* ------------------------------------- */
/* --- Portal interrupt register API --- */

/* For a quick explanation of the Bman interrupt model, see the comments in the
 * equivalent section of the qman_portal.h header.
 */

/* Create/destroy */
t_Error bm_isr_init(struct bm_portal *portal);
void bm_isr_finish(struct bm_portal *portal);

/* BSCN masking is a per-portal configuration */
void bm_isr_bscn_mask(struct bm_portal *portal, uint8_t bpid, int enable);

/* Used by all portal interrupt registers except 'inhibit' */
#define BM_PIRQ_RCRI    0x00000002    /* RCR Ring (below threshold) */
#define BM_PIRQ_BSCN    0x00000001    /* Buffer depletion State Change */

/* These are bm_<reg>_<verb>(). So for example, bm_disable_write() means "write
 * the disable register" rather than "disable the ability to write". */
#define bm_isr_status_read(bm)      __bm_isr_read(bm, bm_isr_status)
#define bm_isr_status_clear(bm, m)  __bm_isr_write(bm, bm_isr_status, m)
#define bm_isr_enable_read(bm)      __bm_isr_read(bm, bm_isr_enable)
#define bm_isr_enable_write(bm, v)  __bm_isr_write(bm, bm_isr_enable, v)
#define bm_isr_disable_read(bm)     __bm_isr_read(bm, bm_isr_disable)
#define bm_isr_disable_write(bm, v) __bm_isr_write(bm, bm_isr_disable, v)
#define bm_isr_inhibit(bm)          __bm_isr_write(bm, bm_isr_inhibit, 1)
#define bm_isr_uninhibit(bm)        __bm_isr_write(bm, bm_isr_inhibit, 0)

/* Don't use these, use the wrappers above*/
uint32_t __bm_isr_read(struct bm_portal *portal, enum bm_isr_reg n);
void __bm_isr_write(struct bm_portal *portal, enum bm_isr_reg n, uint32_t val);

/* ------------------------------------------------------- */
/* --- Bman data structures (and associated constants) --- */
/* Code-reduction, define a wrapper for 48-bit buffers. In cases where a buffer
 * pool id specific to this buffer is needed (BM_RCR_VERB_CMD_BPID_MULTI,
 * BM_MCC_VERB_ACQUIRE), the 'bpid' field is used. */

#define BM_RCR_VERB_VBIT                0x80
#define BM_RCR_VERB_CMD_MASK            0x70    /* one of two values; */
#define BM_RCR_VERB_CMD_BPID_SINGLE     0x20
#define BM_RCR_VERB_CMD_BPID_MULTI      0x30
#define BM_RCR_VERB_BUFCOUNT_MASK       0x0f    /* values 1..8 */

#define BM_MCC_VERB_VBIT                0x80
#define BM_MCC_VERB_CMD_MASK            0x70    /* where the verb contains; */
#define BM_MCC_VERB_CMD_ACQUIRE         0x10
#define BM_MCC_VERB_CMD_QUERY           0x40
#define BM_MCC_VERB_ACQUIRE_BUFCOUNT    0x0f    /* values 1..8 go here */


#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

_Packed struct bm_buffer {
    volatile uint8_t reserved1;
    volatile uint8_t bpid;
    volatile uint16_t hi;    /* High 16-bits of 48-bit address */
    volatile uint32_t lo;    /* Low 32-bits of 48-bit address */
} _PackedType;

/* See 1.5.3.5.4: "Release Command" */
_Packed struct bm_rcr_entry {
    _Packed union {
        _Packed struct {
            volatile uint8_t __dont_write_directly__verb;
            volatile uint8_t bpid; /* used with BM_RCR_VERB_CMD_BPID_SINGLE */
            volatile uint8_t reserved1[62];
        } _PackedType;
        volatile struct bm_buffer bufs[8];
    } _PackedType;
} _PackedType;

/* See 1.5.3.1: "Acquire Command" */
/* See 1.5.3.2: "Query Command" */
_Packed struct bm_mc_command {
    volatile uint8_t __dont_write_directly__verb;
    _Packed union {
        _Packed struct bm_mcc_acquire {
            volatile uint8_t bpid;
            volatile uint8_t reserved1[62];
        } _PackedType acquire;
        _Packed struct bm_mcc_query {
            volatile uint8_t reserved1[63];
        } _PackedType query;
    } _PackedType;
} _PackedType;

/* See 1.5.3.3: "Acquire Reponse" */
/* See 1.5.3.4: "Query Reponse" */
_Packed struct bm_mc_result {
    _Packed union {
        _Packed struct {
            volatile uint8_t verb;
            volatile uint8_t reserved1[63];
        } _PackedType;
        _Packed union {
            _Packed struct {
                volatile uint8_t reserved1;
                volatile uint8_t bpid;
                volatile uint8_t reserved2[62];
            } _PackedType;
            volatile struct bm_buffer bufs[8];
        } _PackedType acquire;
        _Packed struct {
            volatile uint8_t reserved1[32];
            /* "availability state" and "depletion state" */
            _Packed struct {
                volatile uint8_t reserved1[8];
                /* Access using bman_depletion_***() */
                volatile struct bman_depletion state;
            } _PackedType as, ds;
        } _PackedType query;
    } _PackedType;
} _PackedType;

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


#define BM_MCR_VERB_VBIT                0x80
#define BM_MCR_VERB_CMD_MASK            BM_MCC_VERB_CMD_MASK
#define BM_MCR_VERB_CMD_ACQUIRE         BM_MCC_VERB_CMD_ACQUIRE
#define BM_MCR_VERB_CMD_QUERY           BM_MCC_VERB_CMD_QUERY
#define BM_MCR_VERB_CMD_ERR_INVALID     0x60
#define BM_MCR_VERB_CMD_ERR_ECC         0x70
#define BM_MCR_VERB_ACQUIRE_BUFCOUNT    BM_MCC_VERB_ACQUIRE_BUFCOUNT /* 0..8 */
/* Determine the "availability state" of pool 'p' from a query result 'r' */
#define BM_MCR_QUERY_AVAILABILITY(r,p) bman_depletion_get(&r->query.as.state,p)
/* Determine the "depletion state" of pool 'p' from a query result 'r' */
#define BM_MCR_QUERY_DEPLETION(r,p) bman_depletion_get(&r->query.ds.state,p)


/* Portal and Buffer Pools */
/* ----------------------- */

/* Flags to bman_create_portal() */
#define BMAN_PORTAL_FLAG_IRQ         0x00000001 /* use interrupt handler */
#define BMAN_PORTAL_FLAG_IRQ_FAST    0x00000002 /* ... for fast-path too! */
#define BMAN_PORTAL_FLAG_COMPACT     0x00000004 /* use compaction algorithm */
#define BMAN_PORTAL_FLAG_RECOVER     0x00000008 /* recovery mode */
#define BMAN_PORTAL_FLAG_WAIT        0x00000010 /* wait if RCR is full */
#define BMAN_PORTAL_FLAG_WAIT_INT    0x00000020 /* if wait, interruptible? */
#define BMAN_PORTAL_FLAG_CACHE       0x00000400 /* use cache-able area for rings */

/* Flags to bman_new_pool() */
#define BMAN_POOL_FLAG_NO_RELEASE    0x00000001 /* can't release to pool */
#define BMAN_POOL_FLAG_ONLY_RELEASE  0x00000002 /* can only release to pool */
#define BMAN_POOL_FLAG_DEPLETION     0x00000004 /* track depletion entry/exit */
#define BMAN_POOL_FLAG_DYNAMIC_BPID  0x00000008 /* (de)allocate bpid */
#define BMAN_POOL_FLAG_THRESH        0x00000010 /* set depletion thresholds */
#define BMAN_POOL_FLAG_STOCKPILE     0x00000020 /* stockpile to reduce hw ops */

/* Flags to bman_release() */
#define BMAN_RELEASE_FLAG_WAIT       0x00000001 /* wait if RCR is full */
#define BMAN_RELEASE_FLAG_WAIT_INT   0x00000002 /* if we wait, interruptible? */
#define BMAN_RELEASE_FLAG_WAIT_SYNC  0x00000004 /* if wait, until consumed? */
#define BMAN_RELEASE_FLAG_NOW        0x00000008 /* issue immediate release */

/* Flags to bman_acquire() */
#define BMAN_ACQUIRE_FLAG_STOCKPILE  0x00000001 /* no hw op, stockpile only */


#endif /* __FSL_BMAN_H */
