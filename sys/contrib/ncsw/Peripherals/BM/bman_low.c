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
 @File          bman_low.c

 @Description   BM low-level implementation
*//***************************************************************************/
#include <sys/cdefs.h>
#include <sys/types.h>
#include <machine/atomic.h>

#include "std_ext.h"
#include "core_ext.h"
#include "xx_ext.h"
#include "error_ext.h"

#include "bman_private.h"


/***************************/
/* Portal register assists */
/***************************/

/* Cache-inhibited register offsets */
#define REG_RCR_PI_CINH     0x0000
#define REG_RCR_CI_CINH     0x0004
#define REG_RCR_ITR         0x0008
#define REG_CFG             0x0100
#define REG_SCN(n)          (0x0200 + ((n) << 2))
#define REG_ISR             0x0e00
#define REG_IER             0x0e04
#define REG_ISDR            0x0e08
#define REG_IIR             0x0e0c

/* Cache-enabled register offsets */
#define CL_CR               0x0000
#define CL_RR0              0x0100
#define CL_RR1              0x0140
#define CL_RCR              0x1000
#define CL_RCR_PI_CENA      0x3000
#define CL_RCR_CI_CENA      0x3100

/* The h/w design requires mappings to be size-aligned so that "add"s can be
 * reduced to "or"s. The primitives below do the same for s/w. */

static __inline__ void *ptr_ADD(void *a, uintptr_t b)
{
    return (void *)((uintptr_t)a + b);
}

/* Bitwise-OR two pointers */
static __inline__ void *ptr_OR(void *a, uintptr_t b)
{
    return (void *)((uintptr_t)a | b);
}

/* Cache-inhibited register access */
static __inline__ uint32_t __bm_in(struct bm_addr *bm, uintptr_t offset)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(bm->addr_ci, offset);
    return GET_UINT32(*tmp);
}
static __inline__ void __bm_out(struct bm_addr *bm, uintptr_t offset, uint32_t val)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(bm->addr_ci, offset);
    WRITE_UINT32(*tmp, val);
}
#define bm_in(reg)        __bm_in(&portal->addr, REG_##reg)
#define bm_out(reg, val)    __bm_out(&portal->addr, REG_##reg, val)

/* Convert 'n' cachelines to a pointer value for bitwise OR */
#define bm_cl(n)        (void *)((n) << 6)

/* Cache-enabled (index) register access */
static __inline__ void __bm_cl_touch_ro(struct bm_addr *bm, uintptr_t offset)
{
    dcbt_ro(ptr_ADD(bm->addr_ce, offset));
}
static __inline__ void __bm_cl_touch_rw(struct bm_addr *bm, uintptr_t offset)
{
    dcbt_rw(ptr_ADD(bm->addr_ce, offset));
}
static __inline__ uint32_t __bm_cl_in(struct bm_addr *bm, uintptr_t offset)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(bm->addr_ce, offset);
    return GET_UINT32(*tmp);
}
static __inline__ void __bm_cl_out(struct bm_addr *bm, uintptr_t offset, uint32_t val)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(bm->addr_ce, offset);
    WRITE_UINT32(*tmp, val);
    dcbf(tmp);
}
static __inline__ void __bm_cl_invalidate(struct bm_addr *bm, uintptr_t offset)
{
    dcbi(ptr_ADD(bm->addr_ce, offset));
}
#define bm_cl_touch_ro(reg)    __bm_cl_touch_ro(&portal->addr, CL_##reg##_CENA)
#define bm_cl_touch_rw(reg)    __bm_cl_touch_rw(&portal->addr, CL_##reg##_CENA)
#define bm_cl_in(reg)        __bm_cl_in(&portal->addr, CL_##reg##_CENA)
#define bm_cl_out(reg, val)    __bm_cl_out(&portal->addr, CL_##reg##_CENA, val)
#define bm_cl_invalidate(reg) __bm_cl_invalidate(&portal->addr, CL_##reg##_CENA)

/* Cyclic helper for rings. TODO: once we are able to do fine-grain perf
 * analysis, look at using the "extra" bit in the ring index registers to avoid
 * cyclic issues. */
static __inline__ uint8_t cyc_diff(uint8_t ringsize, uint8_t first, uint8_t last)
{
    /* 'first' is included, 'last' is excluded */
    if (first <= last)
        return (uint8_t)(last - first);
    return (uint8_t)(ringsize + last - first);
}

/* --------------- */
/* --- RCR API --- */

/* It's safer to code in terms of the 'rcr' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define RCR_API_START()      register struct bm_rcr *rcr = &portal->rcr */

/* Bit-wise logic to wrap a ring pointer by clearing the "carry bit" */
#define RCR_CARRYCLEAR(p) \
    (void *)((uintptr_t)(p) & (~(uintptr_t)(BM_RCR_SIZE << 6)))

/* Bit-wise logic to convert a ring pointer to a ring index */
static __inline__ uint8_t RCR_PTR2IDX(struct bm_rcr_entry *e)
{
    return (uint8_t)(((uintptr_t)e >> 6) & (BM_RCR_SIZE - 1));
}

/* Increment the 'cursor' ring pointer, taking 'vbit' into account */
static __inline__ void RCR_INC(struct bm_rcr *rcr)
{
    /* NB: this is odd-looking, but experiments show that it generates
     * fast code with essentially no branching overheads. We increment to
     * the next RCR pointer and handle overflow and 'vbit'. */
    struct bm_rcr_entry *partial = rcr->cursor + 1;
    rcr->cursor = RCR_CARRYCLEAR(partial);
    if (partial != rcr->cursor)
        rcr->vbit ^= BM_RCR_VERB_VBIT;
}

t_Error bm_rcr_init(struct bm_portal *portal,
                    e_BmPortalProduceMode pmode,
                    e_BmPortalRcrConsumeMode cmode)
{
    register struct bm_rcr *rcr = &portal->rcr;
    uint32_t cfg;
    uint8_t pi;

    rcr->ring = ptr_ADD(portal->addr.addr_ce, CL_RCR);
    rcr->ci = (uint8_t)(bm_in(RCR_CI_CINH) & (BM_RCR_SIZE - 1));
    pi = (uint8_t)(bm_in(RCR_PI_CINH) & (BM_RCR_SIZE - 1));
    rcr->cursor = rcr->ring + pi;
    rcr->vbit = (uint8_t)((bm_in(RCR_PI_CINH) & BM_RCR_SIZE) ?  BM_RCR_VERB_VBIT : 0);
    rcr->available = (uint8_t)(BM_RCR_SIZE - 1 - cyc_diff(BM_RCR_SIZE, rcr->ci, pi));
    rcr->ithresh = (uint8_t)bm_in(RCR_ITR);
#ifdef BM_CHECKING
    rcr->busy = 0;
    rcr->pmode = pmode;
    rcr->cmode = cmode;
#else
    UNUSED(cmode);
#endif /* BM_CHECKING */
    cfg = (bm_in(CFG) & 0xffffffe0) | (pmode & 0x3); /* BCSP_CFG::RPM */
    bm_out(CFG, cfg);
    return 0;
}

void bm_rcr_finish(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    uint8_t pi = (uint8_t)(bm_in(RCR_PI_CINH) & (BM_RCR_SIZE - 1));
    uint8_t ci = (uint8_t)(bm_in(RCR_CI_CINH) & (BM_RCR_SIZE - 1));
    ASSERT_COND(!rcr->busy);
    if (pi != RCR_PTR2IDX(rcr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("losing uncommitted RCR entries"));
    if (ci != rcr->ci)
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("missing existing RCR completions"));
    if (rcr->ci != RCR_PTR2IDX(rcr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("RCR destroyed unquiesced"));
}

struct bm_rcr_entry *bm_rcr_start(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    ASSERT_COND(!rcr->busy);
    if (!rcr->available)
        return NULL;
#ifdef BM_CHECKING
    rcr->busy = 1;
#endif /* BM_CHECKING */
    dcbz_64(rcr->cursor);
    return rcr->cursor;
}

void bm_rcr_abort(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    ASSERT_COND(rcr->busy);
#ifdef BM_CHECKING
    rcr->busy = 0;
#else
    UNUSED(rcr);
#endif /* BM_CHECKING */
}

struct bm_rcr_entry *bm_rcr_pend_and_next(struct bm_portal *portal, uint8_t myverb)
{
    register struct bm_rcr *rcr = &portal->rcr;
    ASSERT_COND(rcr->busy);
    ASSERT_COND(rcr->pmode != e_BmPortalPVB);
    if (rcr->available == 1)
        return NULL;
    rcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | rcr->vbit);
    dcbf_64(rcr->cursor);
    RCR_INC(rcr);
    rcr->available--;
    dcbz_64(rcr->cursor);
    return rcr->cursor;
}

void bm_rcr_pci_commit(struct bm_portal *portal, uint8_t myverb)
{
    register struct bm_rcr *rcr = &portal->rcr;
    ASSERT_COND(rcr->busy);
    ASSERT_COND(rcr->pmode == e_BmPortalPCI);
    rcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | rcr->vbit);
    RCR_INC(rcr);
    rcr->available--;
    mb();
    bm_out(RCR_PI_CINH, RCR_PTR2IDX(rcr->cursor));
#ifdef BM_CHECKING
    rcr->busy = 0;
#endif /* BM_CHECKING */
}

void bm_rcr_pce_prefetch(struct bm_portal *portal)
{
    ASSERT_COND(((struct bm_rcr *)&portal->rcr)->pmode == e_BmPortalPCE);
    bm_cl_invalidate(RCR_PI);
    bm_cl_touch_rw(RCR_PI);
}

void bm_rcr_pce_commit(struct bm_portal *portal, uint8_t myverb)
{
    register struct bm_rcr *rcr = &portal->rcr;
    ASSERT_COND(rcr->busy);
    ASSERT_COND(rcr->pmode == e_BmPortalPCE);
    rcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | rcr->vbit);
    RCR_INC(rcr);
    rcr->available--;
    wmb();
    bm_cl_out(RCR_PI, RCR_PTR2IDX(rcr->cursor));
#ifdef BM_CHECKING
    rcr->busy = 0;
#endif /* BM_CHECKING */
}

void bm_rcr_pvb_commit(struct bm_portal *portal, uint8_t myverb)
{
    register struct bm_rcr *rcr = &portal->rcr;
    struct bm_rcr_entry *rcursor;
    ASSERT_COND(rcr->busy);
    ASSERT_COND(rcr->pmode == e_BmPortalPVB);
    rmb();
    rcursor = rcr->cursor;
    rcursor->__dont_write_directly__verb = (uint8_t)(myverb | rcr->vbit);
    dcbf_64(rcursor);
    RCR_INC(rcr);
    rcr->available--;
#ifdef BM_CHECKING
    rcr->busy = 0;
#endif /* BM_CHECKING */
}


uint8_t bm_rcr_cci_update(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    uint8_t diff, old_ci = rcr->ci;
    ASSERT_COND(rcr->cmode == e_BmPortalRcrCCI);
    rcr->ci = (uint8_t)(bm_in(RCR_CI_CINH) & (BM_RCR_SIZE - 1));
    diff = cyc_diff(BM_RCR_SIZE, old_ci, rcr->ci);
    rcr->available += diff;
    return diff;
}


void bm_rcr_cce_prefetch(struct bm_portal *portal)
{
    ASSERT_COND(((struct bm_rcr *)&portal->rcr)->cmode == e_BmPortalRcrCCE);
    bm_cl_touch_ro(RCR_CI);
}


uint8_t bm_rcr_cce_update(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    uint8_t diff, old_ci = rcr->ci;
    ASSERT_COND(rcr->cmode == e_BmPortalRcrCCE);
    rcr->ci = (uint8_t)(bm_cl_in(RCR_CI) & (BM_RCR_SIZE - 1));
    bm_cl_invalidate(RCR_CI);
    diff = cyc_diff(BM_RCR_SIZE, old_ci, rcr->ci);
    rcr->available += diff;
    return diff;
}


uint8_t bm_rcr_get_ithresh(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    return rcr->ithresh;
}


void bm_rcr_set_ithresh(struct bm_portal *portal, uint8_t ithresh)
{
    register struct bm_rcr *rcr = &portal->rcr;
    rcr->ithresh = ithresh;
    bm_out(RCR_ITR, ithresh);
}


uint8_t bm_rcr_get_avail(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    return rcr->available;
}


uint8_t bm_rcr_get_fill(struct bm_portal *portal)
{
    register struct bm_rcr *rcr = &portal->rcr;
    return (uint8_t)(BM_RCR_SIZE - 1 - rcr->available);
}


/* ------------------------------ */
/* --- Management command API --- */

/* It's safer to code in terms of the 'mc' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define MC_API_START()      register struct bm_mc *mc = &portal->mc */


t_Error bm_mc_init(struct bm_portal *portal)
{
    register struct bm_mc *mc = &portal->mc;
    mc->cr = ptr_ADD(portal->addr.addr_ce, CL_CR);
    mc->rr = ptr_ADD(portal->addr.addr_ce, CL_RR0);
    mc->rridx = (uint8_t)((mc->cr->__dont_write_directly__verb & BM_MCC_VERB_VBIT) ?
            0 : 1);
    mc->vbit = (uint8_t)(mc->rridx ? BM_MCC_VERB_VBIT : 0);
#ifdef BM_CHECKING
    mc->state = mc_idle;
#endif /* BM_CHECKING */
    return 0;
}


void bm_mc_finish(struct bm_portal *portal)
{
    register struct bm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_idle);
#ifdef BM_CHECKING
    if (mc->state != mc_idle)
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("Losing incomplete MC command"));
#else
    UNUSED(mc);
#endif /* BM_CHECKING */
}


struct bm_mc_command *bm_mc_start(struct bm_portal *portal)
{
    register struct bm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_idle);
#ifdef BM_CHECKING
    mc->state = mc_user;
#endif /* BM_CHECKING */
    dcbz_64(mc->cr);
    return mc->cr;
}


void bm_mc_abort(struct bm_portal *portal)
{
    register struct bm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_user);
#ifdef BM_CHECKING
    mc->state = mc_idle;
#else
    UNUSED(mc);
#endif /* BM_CHECKING */
}


void bm_mc_commit(struct bm_portal *portal, uint8_t myverb)
{
    register struct bm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_user);
    rmb();
    mc->cr->__dont_write_directly__verb = (uint8_t)(myverb | mc->vbit);
    dcbf_64(mc->cr);
    dcbit_ro(mc->rr + mc->rridx);
#ifdef BM_CHECKING
    mc->state = mc_hw;
#endif /* BM_CHECKING */
}


struct bm_mc_result *bm_mc_result(struct bm_portal *portal)
{
    register struct bm_mc *mc = &portal->mc;
    struct bm_mc_result *rr = mc->rr + mc->rridx;
    ASSERT_COND(mc->state == mc_hw);
    /* The inactive response register's verb byte always returns zero until
     * its command is submitted and completed. This includes the valid-bit,
     * in case you were wondering... */
    if (!rr->verb) {
        dcbit_ro(rr);
        return NULL;
    }
    mc->rridx ^= 1;
    mc->vbit ^= BM_MCC_VERB_VBIT;
#ifdef BM_CHECKING
    mc->state = mc_idle;
#endif /* BM_CHECKING */
    return rr;
}

/* ------------------------------------- */
/* --- Portal interrupt register API --- */

#define SCN_REG(bpid) REG_SCN((bpid) / 32)
#define SCN_BIT(bpid) (0x80000000 >> (bpid & 31))
void bm_isr_bscn_mask(struct bm_portal *portal, uint8_t bpid, int enable)
{
    uint32_t val;
    ASSERT_COND(bpid < BM_MAX_NUM_OF_POOLS);
    /* REG_SCN for bpid=0..31, REG_SCN+4 for bpid=32..63 */
    val = __bm_in(&portal->addr, SCN_REG(bpid));
    if (enable)
        val |= SCN_BIT(bpid);
    else
        val &= ~SCN_BIT(bpid);
    __bm_out(&portal->addr, SCN_REG(bpid), val);
}


uint32_t __bm_isr_read(struct bm_portal *portal, enum bm_isr_reg n)
{
    return __bm_in(&portal->addr, REG_ISR + (n << 2));
}


void __bm_isr_write(struct bm_portal *portal, enum bm_isr_reg n, uint32_t val)
{
    __bm_out(&portal->addr, REG_ISR + (n << 2), val);
}

