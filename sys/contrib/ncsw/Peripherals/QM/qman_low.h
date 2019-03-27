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
 @File          qman_low.c

 @Description   QM Low-level implementation
*//***************************************************************************/
#include "std_ext.h"
#include "core_ext.h"
#include "xx_ext.h"
#include "error_ext.h"

#include "qman_private.h"


/***************************/
/* Portal register assists */
/***************************/

/* Cache-inhibited register offsets */
#define REG_EQCR_PI_CINH    0x0000
#define REG_EQCR_CI_CINH    0x0004
#define REG_EQCR_ITR        0x0008
#define REG_DQRR_PI_CINH    0x0040
#define REG_DQRR_CI_CINH    0x0044
#define REG_DQRR_ITR        0x0048
#define REG_DQRR_DCAP       0x0050
#define REG_DQRR_SDQCR      0x0054
#define REG_DQRR_VDQCR      0x0058
#define REG_DQRR_PDQCR      0x005c
#define REG_MR_PI_CINH      0x0080
#define REG_MR_CI_CINH      0x0084
#define REG_MR_ITR          0x0088
#define REG_CFG             0x0100
#define REG_ISR             0x0e00
#define REG_IER             0x0e04
#define REG_ISDR            0x0e08
#define REG_IIR             0x0e0c
#define REG_ITPR            0x0e14

/* Cache-enabled register offsets */
#define CL_EQCR             0x0000
#define CL_DQRR             0x1000
#define CL_MR               0x2000
#define CL_EQCR_PI_CENA     0x3000
#define CL_EQCR_CI_CENA     0x3100
#define CL_DQRR_PI_CENA     0x3200
#define CL_DQRR_CI_CENA     0x3300
#define CL_MR_PI_CENA       0x3400
#define CL_MR_CI_CENA       0x3500
#define CL_RORI_CENA        0x3600
#define CL_CR               0x3800
#define CL_RR0              0x3900
#define CL_RR1              0x3940

static __inline__ void *ptr_ADD(void *a, uintptr_t b)
{
    return (void *)((uintptr_t)a + b);
}

/* The h/w design requires mappings to be size-aligned so that "add"s can be
 * reduced to "or"s. The primitives below do the same for s/w. */
/* Bitwise-OR two pointers */
static __inline__ void *ptr_OR(void *a, uintptr_t b)
{
    return (void *)((uintptr_t)a | b);
}

/* Cache-inhibited register access */
static __inline__ uint32_t __qm_in(struct qm_addr *qm, uintptr_t offset)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(qm->addr_ci, offset);
    return GET_UINT32(*tmp);
}
static __inline__ void __qm_out(struct qm_addr *qm, uintptr_t offset, uint32_t val)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(qm->addr_ci, offset);
    WRITE_UINT32(*tmp, val);
}
#define qm_in(reg)        __qm_in(&portal->addr, REG_##reg)
#define qm_out(reg, val)    __qm_out(&portal->addr, REG_##reg, (uint32_t)val)

/* Convert 'n' cachelines to a pointer value for bitwise OR */
#define qm_cl(n)        ((n) << 6)

/* Cache-enabled (index) register access */
static __inline__ void __qm_cl_touch_ro(struct qm_addr *qm, uintptr_t offset)
{
    dcbt_ro(ptr_ADD(qm->addr_ce, offset));
}
static __inline__ void __qm_cl_touch_rw(struct qm_addr *qm, uintptr_t offset)
{
    dcbt_rw(ptr_ADD(qm->addr_ce, offset));
}
static __inline__ uint32_t __qm_cl_in(struct qm_addr *qm, uintptr_t offset)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(qm->addr_ce, offset);
    return GET_UINT32(*tmp);
}
static __inline__ void __qm_cl_out(struct qm_addr *qm, uintptr_t offset, uint32_t val)
{
    uint32_t    *tmp = (uint32_t *)ptr_ADD(qm->addr_ce, offset);
    WRITE_UINT32(*tmp, val);
    dcbf(tmp);
}
static __inline__ void __qm_cl_invalidate(struct qm_addr *qm, uintptr_t offset)
{
    dcbi(ptr_ADD(qm->addr_ce, offset));
}
#define qm_cl_touch_ro(reg)    __qm_cl_touch_ro(&portal->addr, CL_##reg##_CENA)
#define qm_cl_touch_rw(reg)    __qm_cl_touch_rw(&portal->addr, CL_##reg##_CENA)
#define qm_cl_in(reg)          __qm_cl_in(&portal->addr, CL_##reg##_CENA)
#define qm_cl_out(reg, val)    __qm_cl_out(&portal->addr, CL_##reg##_CENA, val)
#define qm_cl_invalidate(reg)  __qm_cl_invalidate(&portal->addr, CL_##reg##_CENA)

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

static __inline__ t_Error __qm_portal_bind(struct qm_portal *portal, uint8_t iface)
{
    t_Error ret = E_BUSY;
    if (!(portal->config.bound & iface)) {
        portal->config.bound |= iface;
        ret = E_OK;
    }
    return ret;
}

static __inline__ void __qm_portal_unbind(struct qm_portal *portal, uint8_t iface)
{
#ifdef QM_CHECKING
    ASSERT_COND(portal->config.bound & iface);
#endif /* QM_CHECKING */
    portal->config.bound &= ~iface;
}

/* ---------------- */
/* --- EQCR API --- */

/* It's safer to code in terms of the 'eqcr' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define EQCR_API_START()    register struct qm_eqcr *eqcr = &portal->eqcr */

/* Bit-wise logic to wrap a ring pointer by clearing the "carry bit" */
#define EQCR_CARRYCLEAR(p) \
    (void *)((uintptr_t)(p) & (~(uintptr_t)(QM_EQCR_SIZE << 6)))

/* Bit-wise logic to convert a ring pointer to a ring index */
static __inline__ uint8_t EQCR_PTR2IDX(struct qm_eqcr_entry *e)
{
    return (uint8_t)(((uintptr_t)e >> 6) & (QM_EQCR_SIZE - 1));
}

/* Increment the 'cursor' ring pointer, taking 'vbit' into account */
static __inline__ void EQCR_INC(struct qm_eqcr *eqcr)
{
    /* NB: this is odd-looking, but experiments show that it generates fast
     * code with essentially no branching overheads. We increment to the
     * next EQCR pointer and handle overflow and 'vbit'. */
    struct qm_eqcr_entry *partial = eqcr->cursor + 1;
    eqcr->cursor = EQCR_CARRYCLEAR(partial);
    if (partial != eqcr->cursor)
        eqcr->vbit ^= QM_EQCR_VERB_VBIT;
}

static __inline__ t_Error qm_eqcr_init(struct qm_portal *portal, e_QmPortalProduceMode pmode,
        e_QmPortalEqcrConsumeMode cmode)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    uint32_t cfg;
    uint8_t pi;

    if (__qm_portal_bind(portal, QM_BIND_EQCR))
        return ERROR_CODE(E_BUSY);
    eqcr->ring = ptr_ADD(portal->addr.addr_ce, CL_EQCR);
    eqcr->ci = (uint8_t)(qm_in(EQCR_CI_CINH) & (QM_EQCR_SIZE - 1));
    qm_cl_invalidate(EQCR_CI);
    pi = (uint8_t)(qm_in(EQCR_PI_CINH) & (QM_EQCR_SIZE - 1));
    eqcr->cursor = eqcr->ring + pi;
    eqcr->vbit = (uint8_t)((qm_in(EQCR_PI_CINH) & QM_EQCR_SIZE) ?
            QM_EQCR_VERB_VBIT : 0);
    eqcr->available = (uint8_t)(QM_EQCR_SIZE - 1 -
            cyc_diff(QM_EQCR_SIZE, eqcr->ci, pi));
    eqcr->ithresh = (uint8_t)qm_in(EQCR_ITR);

#ifdef QM_CHECKING
    eqcr->busy = 0;
    eqcr->pmode = pmode;
    eqcr->cmode = cmode;
#else
    UNUSED(cmode);
#endif /* QM_CHECKING */
    cfg = (qm_in(CFG) & 0x00ffffff) |
        ((pmode & 0x3) << 24);    /* QCSP_CFG::EPM */
    qm_out(CFG, cfg);
    return 0;
}

static __inline__ void qm_eqcr_finish(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    uint8_t pi = (uint8_t)(qm_in(EQCR_PI_CINH) & (QM_EQCR_SIZE - 1));
    uint8_t ci = (uint8_t)(qm_in(EQCR_CI_CINH) & (QM_EQCR_SIZE - 1));

#ifdef QM_CHECKING
    ASSERT_COND(!eqcr->busy);
#endif /* QM_CHECKING */
    if (pi != EQCR_PTR2IDX(eqcr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("losing uncommitted EQCR entries"));
    if (ci != eqcr->ci)
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("missing existing EQCR completions"));
    if (eqcr->ci != EQCR_PTR2IDX(eqcr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("EQCR destroyed unquiesced"));
    __qm_portal_unbind(portal, QM_BIND_EQCR);
}

static __inline__ struct qm_eqcr_entry *qm_eqcr_start(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
#ifdef QM_CHECKING
    ASSERT_COND(!eqcr->busy);
#endif /* QM_CHECKING */
    if (!eqcr->available)
        return NULL;
#ifdef QM_CHECKING
    eqcr->busy = 1;
#endif /* QM_CHECKING */
    dcbz_64(eqcr->cursor);
    return eqcr->cursor;
}

static __inline__ void qm_eqcr_abort(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_eqcr *eqcr = &portal->eqcr;
    ASSERT_COND(eqcr->busy);
    eqcr->busy = 0;
#else
    UNUSED(portal);
#endif /* QM_CHECKING */
}

static __inline__ struct qm_eqcr_entry *qm_eqcr_pend_and_next(struct qm_portal *portal, uint8_t myverb)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
#ifdef QM_CHECKING
    ASSERT_COND(eqcr->busy);
    ASSERT_COND(eqcr->pmode != e_QmPortalPVB);
#endif /* QM_CHECKING */
    if (eqcr->available == 1)
        return NULL;
    eqcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | eqcr->vbit);
    dcbf_64(eqcr->cursor);
    EQCR_INC(eqcr);
    eqcr->available--;
    dcbz_64(eqcr->cursor);
    return eqcr->cursor;
}

#ifdef QM_CHECKING
#define EQCR_COMMIT_CHECKS(eqcr) \
do { \
    ASSERT_COND(eqcr->busy); \
    ASSERT_COND(eqcr->cursor->orp == (eqcr->cursor->orp & 0x00ffffff)); \
    ASSERT_COND(eqcr->cursor->fqid == (eqcr->cursor->fqid & 0x00ffffff)); \
} while(0)

#else
#define EQCR_COMMIT_CHECKS(eqcr)
#endif /* QM_CHECKING */


static __inline__ void qmPortalEqcrPciCommit(struct qm_portal *portal, uint8_t myverb)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
#ifdef QM_CHECKING
    EQCR_COMMIT_CHECKS(eqcr);
    ASSERT_COND(eqcr->pmode == e_QmPortalPCI);
#endif /* QM_CHECKING */
    eqcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | eqcr->vbit);
    EQCR_INC(eqcr);
    eqcr->available--;
    dcbf_64(eqcr->cursor);
    mb();
    qm_out(EQCR_PI_CINH, EQCR_PTR2IDX(eqcr->cursor));
#ifdef QM_CHECKING
    eqcr->busy = 0;
#endif /* QM_CHECKING */
}

static __inline__ void qmPortalEqcrPcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_eqcr *eqcr = &portal->eqcr;
    ASSERT_COND(eqcr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    qm_cl_invalidate(EQCR_PI);
    qm_cl_touch_rw(EQCR_PI);
}

static __inline__ void qmPortalEqcrPceCommit(struct qm_portal *portal, uint8_t myverb)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
#ifdef QM_CHECKING
    EQCR_COMMIT_CHECKS(eqcr);
    ASSERT_COND(eqcr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    eqcr->cursor->__dont_write_directly__verb = (uint8_t)(myverb | eqcr->vbit);
    EQCR_INC(eqcr);
    eqcr->available--;
    dcbf_64(eqcr->cursor);
    wmb();
    qm_cl_out(EQCR_PI, EQCR_PTR2IDX(eqcr->cursor));
#ifdef QM_CHECKING
    eqcr->busy = 0;
#endif /* QM_CHECKING */
}

static __inline__ void qmPortalEqcrPvbCommit(struct qm_portal *portal, uint8_t myverb)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    struct qm_eqcr_entry *eqcursor;
#ifdef QM_CHECKING
    EQCR_COMMIT_CHECKS(eqcr);
    ASSERT_COND(eqcr->pmode == e_QmPortalPVB);
#endif /* QM_CHECKING */
    rmb();
    eqcursor = eqcr->cursor;
    eqcursor->__dont_write_directly__verb = (uint8_t)(myverb | eqcr->vbit);
    dcbf_64(eqcursor);
    EQCR_INC(eqcr);
    eqcr->available--;
#ifdef QM_CHECKING
    eqcr->busy = 0;
#endif /* QM_CHECKING */
}

static __inline__ uint8_t qmPortalEqcrCciUpdate(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    uint8_t diff, old_ci = eqcr->ci;
#ifdef QM_CHECKING
    ASSERT_COND(eqcr->cmode == e_QmPortalEqcrCCI);
#endif /* QM_CHECKING */
    eqcr->ci = (uint8_t)(qm_in(EQCR_CI_CINH) & (QM_EQCR_SIZE - 1));
    diff = cyc_diff(QM_EQCR_SIZE, old_ci, eqcr->ci);
    eqcr->available += diff;
    return diff;
}

static __inline__ void qmPortalEqcrCcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_eqcr *eqcr = &portal->eqcr;
    ASSERT_COND(eqcr->cmode == e_QmPortalEqcrCCE);
#endif /* QM_CHECKING */
    qm_cl_touch_ro(EQCR_CI);
}

static __inline__ uint8_t qmPortalEqcrCceUpdate(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    uint8_t diff, old_ci = eqcr->ci;
#ifdef QM_CHECKING
    ASSERT_COND(eqcr->cmode == e_QmPortalEqcrCCE);
#endif /* QM_CHECKING */
    eqcr->ci = (uint8_t)(qm_cl_in(EQCR_CI) & (QM_EQCR_SIZE - 1));
    qm_cl_invalidate(EQCR_CI);
    diff = cyc_diff(QM_EQCR_SIZE, old_ci, eqcr->ci);
    eqcr->available += diff;
    return diff;
}

static __inline__ uint8_t qm_eqcr_get_ithresh(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    return eqcr->ithresh;
}

static __inline__ void qm_eqcr_set_ithresh(struct qm_portal *portal, uint8_t ithresh)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    eqcr->ithresh = ithresh;
    qm_out(EQCR_ITR, ithresh);
}

static __inline__ uint8_t qm_eqcr_get_avail(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    return eqcr->available;
}

static __inline__ uint8_t qm_eqcr_get_fill(struct qm_portal *portal)
{
    register struct qm_eqcr *eqcr = &portal->eqcr;
    return (uint8_t)(QM_EQCR_SIZE - 1 - eqcr->available);
}



/* ---------------- */
/* --- DQRR API --- */

/* TODO: many possible improvements;
 * - look at changing the API to use pointer rather than index parameters now
 *   that 'cursor' is a pointer,
 * - consider moving other parameters to pointer if it could help (ci)
 */

/* It's safer to code in terms of the 'dqrr' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define DQRR_API_START()    register struct qm_dqrr *dqrr = &portal->dqrr */

#define DQRR_CARRYCLEAR(p) \
    (void *)((uintptr_t)(p) & (~(uintptr_t)(QM_DQRR_SIZE << 6)))

static __inline__ uint8_t DQRR_PTR2IDX(struct qm_dqrr_entry *e)
{
    return (uint8_t)(((uintptr_t)e >> 6) & (QM_DQRR_SIZE - 1));
}

static __inline__ struct qm_dqrr_entry *DQRR_INC(struct qm_dqrr_entry *e)
{
    return DQRR_CARRYCLEAR(e + 1);
}

static __inline__ void qm_dqrr_set_maxfill(struct qm_portal *portal, uint8_t mf)
{
    qm_out(CFG, (qm_in(CFG) & 0xff0fffff) |
        ((mf & (QM_DQRR_SIZE - 1)) << 20));
}

static __inline__ t_Error qm_dqrr_init(struct qm_portal *portal, e_QmPortalDequeueMode dmode,
        e_QmPortalProduceMode pmode, e_QmPortalDqrrConsumeMode cmode,
        uint8_t max_fill, int stash_ring, int stash_data)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    const struct qm_portal_config *config = &portal->config;
    uint32_t cfg;

    if (__qm_portal_bind(portal, QM_BIND_DQRR))
        return ERROR_CODE(E_BUSY);
    if ((stash_ring || stash_data) && (config->cpu == -1))
        return ERROR_CODE(E_INVALID_STATE);
    /* Make sure the DQRR will be idle when we enable */
    qm_out(DQRR_SDQCR, 0);
    qm_out(DQRR_VDQCR, 0);
    qm_out(DQRR_PDQCR, 0);
    dqrr->ring = ptr_ADD(portal->addr.addr_ce, CL_DQRR);
    dqrr->pi = (uint8_t)(qm_in(DQRR_PI_CINH) & (QM_DQRR_SIZE - 1));
    dqrr->ci = (uint8_t)(qm_in(DQRR_CI_CINH) & (QM_DQRR_SIZE - 1));
    dqrr->cursor = dqrr->ring + dqrr->ci;
    dqrr->fill = cyc_diff(QM_DQRR_SIZE, dqrr->ci, dqrr->pi);
    dqrr->vbit = (uint8_t)((qm_in(DQRR_PI_CINH) & QM_DQRR_SIZE) ?
            QM_DQRR_VERB_VBIT : 0);
    dqrr->ithresh = (uint8_t)qm_in(DQRR_ITR);

#ifdef QM_CHECKING
    dqrr->dmode = dmode;
    dqrr->pmode = pmode;
    dqrr->cmode = cmode;
    dqrr->flags = 0;
    if (stash_ring)
        dqrr->flags |= QM_DQRR_FLAG_RE;
    if (stash_data)
        dqrr->flags |= QM_DQRR_FLAG_SE;
#else
    UNUSED(pmode);
#endif /* QM_CHECKING */

    cfg = (qm_in(CFG) & 0xff000f00) |
        ((max_fill & (QM_DQRR_SIZE - 1)) << 20) | /* DQRR_MF */
        ((dmode & 1) << 18) |            /* DP */
        ((cmode & 3) << 16) |            /* DCM */
        (stash_ring ? 0x80 : 0) |        /* RE */
        (0 ? 0x40 : 0) |            /* Ignore RP */
        (stash_data ? 0x20 : 0) |        /* SE */
        (0 ? 0x10 : 0);                /* Ignore SP */
    qm_out(CFG, cfg);
    return E_OK;
}


static __inline__ void qm_dqrr_finish(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    if (dqrr->ci != DQRR_PTR2IDX(dqrr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("Ignoring completed DQRR entries"));
    __qm_portal_unbind(portal, QM_BIND_DQRR);
}

static __inline__ struct qm_dqrr_entry *qm_dqrr_current(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    if (!dqrr->fill)
        return NULL;
    return dqrr->cursor;
}

static __inline__ uint8_t qm_dqrr_cursor(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    return DQRR_PTR2IDX(dqrr->cursor);
}

static __inline__ uint8_t qm_dqrr_next(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->fill);
#endif
    dqrr->cursor = DQRR_INC(dqrr->cursor);
    return --dqrr->fill;
}

static __inline__ uint8_t qmPortalDqrrPciUpdate(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    uint8_t diff, old_pi = dqrr->pi;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->pmode == e_QmPortalPCI);
#endif /* QM_CHECKING */
    dqrr->pi = (uint8_t)(qm_in(DQRR_PI_CINH) & (QM_DQRR_SIZE - 1));
    diff = cyc_diff(QM_DQRR_SIZE, old_pi, dqrr->pi);
    dqrr->fill += diff;
    return diff;
}

static __inline__ void qmPortalDqrrPcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    qm_cl_invalidate(DQRR_PI);
    qm_cl_touch_ro(DQRR_PI);
}

static __inline__ uint8_t qmPortalDqrrPceUpdate(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    uint8_t diff, old_pi = dqrr->pi;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    dqrr->pi = (uint8_t)(qm_cl_in(DQRR_PI) & (QM_DQRR_SIZE - 1));
    diff = cyc_diff(QM_DQRR_SIZE, old_pi, dqrr->pi);
    dqrr->fill += diff;
    return diff;
}

static __inline__ void qmPortalDqrrPvbPrefetch(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->pmode == e_QmPortalPVB);
    /* If ring entries get stashed, don't invalidate/prefetch */
    if (!(dqrr->flags & QM_DQRR_FLAG_RE))
#endif /*QM_CHECKING */
        dcbit_ro(ptr_ADD(dqrr->ring, qm_cl(dqrr->pi)));
}

static __inline__ uint8_t qmPortalDqrrPvbUpdate(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    struct qm_dqrr_entry *res = ptr_ADD(dqrr->ring, qm_cl(dqrr->pi));
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->pmode == e_QmPortalPVB);
#endif /* QM_CHECKING */
    if ((res->verb & QM_DQRR_VERB_VBIT) == dqrr->vbit) {
        dqrr->pi = (uint8_t)((dqrr->pi + 1) & (QM_DQRR_SIZE - 1));
        if (!dqrr->pi)
            dqrr->vbit ^= QM_DQRR_VERB_VBIT;
        dqrr->fill++;
        return 1;
    }
    return 0;
}

static __inline__ void qmPortalDqrrCciConsume(struct qm_portal *portal, uint8_t num)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrCCI);
#endif /* QM_CHECKING */
    dqrr->ci = (uint8_t)((dqrr->ci + num) & (QM_DQRR_SIZE - 1));
    qm_out(DQRR_CI_CINH, dqrr->ci);
}

static __inline__ void qmPortalDqrrCciConsumeToCurrent(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrCCI);
#endif /* QM_CHECKING */
    dqrr->ci = DQRR_PTR2IDX(dqrr->cursor);
    qm_out(DQRR_CI_CINH, dqrr->ci);
}

static __inline__ void qmPortalDqrrCcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrCCE);
#endif /* QM_CHECKING */
    qm_cl_invalidate(DQRR_CI);
    qm_cl_touch_rw(DQRR_CI);
}

static __inline__ void qmPortalDqrrCceConsume(struct qm_portal *portal, uint8_t num)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrCCE);
#endif /* QM_CHECKING */
    dqrr->ci = (uint8_t)((dqrr->ci + num) & (QM_DQRR_SIZE - 1));
    qm_cl_out(DQRR_CI, dqrr->ci);
}

static __inline__ void qmPortalDqrrCceConsume_to_current(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrCCE);
#endif /* QM_CHECKING */
    dqrr->ci = DQRR_PTR2IDX(dqrr->cursor);
    qm_cl_out(DQRR_CI, dqrr->ci);
}

static __inline__ void qmPortalDqrrDcaConsume1(struct qm_portal *portal, uint8_t idx, bool park)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    ASSERT_COND(idx < QM_DQRR_SIZE);
    qm_out(DQRR_DCAP, (0 << 8) |    /* S */
        ((uint32_t)(park ? 1 : 0) << 6) |    /* PK */
        idx);            /* DCAP_CI */
}

static __inline__ void qmPortalDqrrDcaConsume1ptr(struct qm_portal      *portal,
                                                  struct qm_dqrr_entry  *dq,
                                                  bool                  park)
{
    uint8_t idx = DQRR_PTR2IDX(dq);
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;

    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
    ASSERT_COND((dqrr->ring + idx) == dq);
    ASSERT_COND(idx < QM_DQRR_SIZE);
#endif /* QM_CHECKING */
    qm_out(DQRR_DCAP, (0 << 8) |        /* DQRR_DCAP::S */
        ((uint32_t)(park ? 1 : 0) << 6) |        /* DQRR_DCAP::PK */
        idx);                /* DQRR_DCAP::DCAP_CI */
}

static __inline__ void qmPortalDqrrDcaConsumeN(struct qm_portal *portal, uint16_t bitmask)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    qm_out(DQRR_DCAP, (1 << 8) |        /* DQRR_DCAP::S */
        ((uint32_t)bitmask << 16));        /* DQRR_DCAP::DCAP_CI */
}

static __inline__ uint8_t qmPortalDqrrDcaCci(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    return (uint8_t)(qm_in(DQRR_CI_CINH) & (QM_DQRR_SIZE - 1));
}

static __inline__ void qmPortalDqrrDcaCcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    qm_cl_invalidate(DQRR_CI);
    qm_cl_touch_ro(DQRR_CI);
}

static __inline__ uint8_t qmPortalDqrrDcaCce(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode == e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    return (uint8_t)(qm_cl_in(DQRR_CI) & (QM_DQRR_SIZE - 1));
}

static __inline__ uint8_t qm_dqrr_get_ci(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode != e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */

    return dqrr->ci;
}

static __inline__ void qm_dqrr_park(struct qm_portal *portal, uint8_t idx)
{
#ifdef QM_CHECKING
    register struct qm_dqrr *dqrr = &portal->dqrr;
    ASSERT_COND(dqrr->cmode != e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */

    qm_out(DQRR_DCAP, (0 << 8) |        /* S */
        (uint32_t)(1 << 6) |            /* PK */
        (idx & (QM_DQRR_SIZE - 1)));    /* DCAP_CI */
}

static __inline__ void qm_dqrr_park_ci(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
#ifdef QM_CHECKING
    ASSERT_COND(dqrr->cmode != e_QmPortalDqrrDCA);
#endif /* QM_CHECKING */
    qm_out(DQRR_DCAP, (0 << 8) |        /* S */
        (uint32_t)(1 << 6) |            /* PK */
        (dqrr->ci & (QM_DQRR_SIZE - 1)));/* DCAP_CI */
}

static __inline__ void qm_dqrr_sdqcr_set(struct qm_portal *portal, uint32_t sdqcr)
{
    qm_out(DQRR_SDQCR, sdqcr);
}

static __inline__ uint32_t qm_dqrr_sdqcr_get(struct qm_portal *portal)
{
    return qm_in(DQRR_SDQCR);
}

static __inline__ void qm_dqrr_vdqcr_set(struct qm_portal *portal, uint32_t vdqcr)
{
    qm_out(DQRR_VDQCR, vdqcr);
}

static __inline__ uint32_t qm_dqrr_vdqcr_get(struct qm_portal *portal)
{
    return qm_in(DQRR_VDQCR);
}

static __inline__ void qm_dqrr_pdqcr_set(struct qm_portal *portal, uint32_t pdqcr)
{
    qm_out(DQRR_PDQCR, pdqcr);
}

static __inline__ uint32_t qm_dqrr_pdqcr_get(struct qm_portal *portal)
{
    return qm_in(DQRR_PDQCR);
}

static __inline__ uint8_t qm_dqrr_get_ithresh(struct qm_portal *portal)
{
    register struct qm_dqrr *dqrr = &portal->dqrr;
    return dqrr->ithresh;
}

static __inline__ void qm_dqrr_set_ithresh(struct qm_portal *portal, uint8_t ithresh)
{
    qm_out(DQRR_ITR, ithresh);
}

static __inline__ uint8_t qm_dqrr_get_maxfill(struct qm_portal *portal)
{
    return (uint8_t)((qm_in(CFG) & 0x00f00000) >> 20);
}

/* -------------- */
/* --- MR API --- */

/* It's safer to code in terms of the 'mr' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define MR_API_START()    register struct qm_mr *mr = &portal->mr */

#define MR_CARRYCLEAR(p) \
    (void *)((uintptr_t)(p) & (~(uintptr_t)(QM_MR_SIZE << 6)))

static __inline__ uint8_t MR_PTR2IDX(struct qm_mr_entry *e)
{
    return (uint8_t)(((uintptr_t)e >> 6) & (QM_MR_SIZE - 1));
}

static __inline__ struct qm_mr_entry *MR_INC(struct qm_mr_entry *e)
{
    return MR_CARRYCLEAR(e + 1);
}

static __inline__ t_Error qm_mr_init(struct qm_portal *portal, e_QmPortalProduceMode pmode,
        e_QmPortalMrConsumeMode cmode)
{
    register struct qm_mr *mr = &portal->mr;
    uint32_t cfg;

    if (__qm_portal_bind(portal, QM_BIND_MR))
        return ERROR_CODE(E_BUSY);
    mr->ring = ptr_ADD(portal->addr.addr_ce, CL_MR);
    mr->pi = (uint8_t)(qm_in(MR_PI_CINH) & (QM_MR_SIZE - 1));
    mr->ci = (uint8_t)(qm_in(MR_CI_CINH) & (QM_MR_SIZE - 1));
    mr->cursor = mr->ring + mr->ci;
    mr->fill = cyc_diff(QM_MR_SIZE, mr->ci, mr->pi);
    mr->vbit = (uint8_t)((qm_in(MR_PI_CINH) & QM_MR_SIZE) ?QM_MR_VERB_VBIT : 0);
    mr->ithresh = (uint8_t)qm_in(MR_ITR);

#ifdef QM_CHECKING
    mr->pmode = pmode;
    mr->cmode = cmode;
#else
    UNUSED(pmode);
#endif /* QM_CHECKING */
    cfg = (qm_in(CFG) & 0xfffff0ff) |
        ((cmode & 1) << 8);        /* QCSP_CFG:MM */
    qm_out(CFG, cfg);
    return E_OK;
}


static __inline__ void qm_mr_finish(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    if (mr->ci != MR_PTR2IDX(mr->cursor))
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("Ignoring completed MR entries"));
    __qm_portal_unbind(portal, QM_BIND_MR);
}

static __inline__ void qm_mr_current_prefetch(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    dcbt_ro(mr->cursor);
}

static __inline__ struct qm_mr_entry *qm_mr_current(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    if (!mr->fill)
        return NULL;
    return mr->cursor;
}

static __inline__ uint8_t qm_mr_cursor(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    return MR_PTR2IDX(mr->cursor);
}

static __inline__ uint8_t qm_mr_next(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
#ifdef QM_CHECKING
    ASSERT_COND(mr->fill);
#endif /* QM_CHECKING */
    mr->cursor = MR_INC(mr->cursor);
    return --mr->fill;
}

static __inline__ uint8_t qmPortalMrPciUpdate(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    uint8_t diff, old_pi = mr->pi;
#ifdef QM_CHECKING
    ASSERT_COND(mr->pmode == e_QmPortalPCI);
#endif /* QM_CHECKING */
    mr->pi = (uint8_t)qm_in(MR_PI_CINH);
    diff = cyc_diff(QM_MR_SIZE, old_pi, mr->pi);
    mr->fill += diff;
    return diff;
}

static __inline__ void qmPortalMrPcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_mr *mr = &portal->mr;
    ASSERT_COND(mr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    qm_cl_invalidate(MR_PI);
    qm_cl_touch_ro(MR_PI);
}

static __inline__ uint8_t qmPortalMrPceUpdate(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    uint8_t diff, old_pi = mr->pi;
#ifdef QM_CHECKING
    ASSERT_COND(mr->pmode == e_QmPortalPCE);
#endif /* QM_CHECKING */
    mr->pi = (uint8_t)(qm_cl_in(MR_PI) & (QM_MR_SIZE - 1));
    diff = cyc_diff(QM_MR_SIZE, old_pi, mr->pi);
    mr->fill += diff;
    return diff;
}

static __inline__ void qmPortalMrPvbUpdate(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    struct qm_mr_entry *res = ptr_ADD(mr->ring, qm_cl(mr->pi));
#ifdef QM_CHECKING
    ASSERT_COND(mr->pmode == e_QmPortalPVB);
#endif /* QM_CHECKING */
    dcbit_ro(ptr_ADD(mr->ring, qm_cl(mr->pi)));
    if ((res->verb & QM_MR_VERB_VBIT) == mr->vbit) {
        mr->pi = (uint8_t)((mr->pi + 1) & (QM_MR_SIZE - 1));
        if (!mr->pi)
            mr->vbit ^= QM_MR_VERB_VBIT;
        mr->fill++;
    }
}

static __inline__ void qmPortalMrCciConsume(struct qm_portal *portal, uint8_t num)
{
    register struct qm_mr *mr = &portal->mr;
#ifdef QM_CHECKING
    ASSERT_COND(mr->cmode == e_QmPortalMrCCI);
#endif /* QM_CHECKING */
    mr->ci = (uint8_t)((mr->ci + num) & (QM_MR_SIZE - 1));
    qm_out(MR_CI_CINH, mr->ci);
}

static __inline__ void qmPortalMrCciConsumeToCurrent(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
#ifdef QM_CHECKING
    ASSERT_COND(mr->cmode == e_QmPortalMrCCI);
#endif /* QM_CHECKING */
    mr->ci = MR_PTR2IDX(mr->cursor);
    qm_out(MR_CI_CINH, mr->ci);
}

static __inline__ void qmPortalMrCcePrefetch(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_mr *mr = &portal->mr;
    ASSERT_COND(mr->cmode == e_QmPortalMrCCE);
#endif /* QM_CHECKING */
    qm_cl_invalidate(MR_CI);
    qm_cl_touch_rw(MR_CI);
}

static __inline__ void qmPortalMrCceConsume(struct qm_portal *portal, uint8_t num)
{
    register struct qm_mr *mr = &portal->mr;
#ifdef QM_CHECKING
    ASSERT_COND(mr->cmode == e_QmPortalMrCCE);
#endif /* QM_CHECKING */
    mr->ci = (uint8_t)((mr->ci + num) & (QM_MR_SIZE - 1));
    qm_cl_out(MR_CI, mr->ci);
}

static __inline__ void qmPortalMrCceConsumeToCurrent(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
#ifdef QM_CHECKING
    ASSERT_COND(mr->cmode == e_QmPortalMrCCE);
#endif /* QM_CHECKING */
    mr->ci = MR_PTR2IDX(mr->cursor);
    qm_cl_out(MR_CI, mr->ci);
}

static __inline__ uint8_t qm_mr_get_ci(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    return mr->ci;
}

static __inline__ uint8_t qm_mr_get_ithresh(struct qm_portal *portal)
{
    register struct qm_mr *mr = &portal->mr;
    return mr->ithresh;
}

static __inline__ void qm_mr_set_ithresh(struct qm_portal *portal, uint8_t ithresh)
{
    qm_out(MR_ITR, ithresh);
}

/* ------------------------------ */
/* --- Management command API --- */

/* It's safer to code in terms of the 'mc' object than the 'portal' object,
 * because the latter runs the risk of copy-n-paste errors from other code where
 * we could manipulate some other structure within 'portal'. */
/* #define MC_API_START()      register struct qm_mc *mc = &portal->mc */

static __inline__ t_Error qm_mc_init(struct qm_portal *portal)
{
    register struct qm_mc *mc = &portal->mc;
    if (__qm_portal_bind(portal, QM_BIND_MC))
        return ERROR_CODE(E_BUSY);
    mc->cr = ptr_ADD(portal->addr.addr_ce, CL_CR);
    mc->rr = ptr_ADD(portal->addr.addr_ce, CL_RR0);
    mc->rridx = (uint8_t)((mc->cr->__dont_write_directly__verb & QM_MCC_VERB_VBIT) ?
            0 : 1);
    mc->vbit = (uint8_t)(mc->rridx ? QM_MCC_VERB_VBIT : 0);
#ifdef QM_CHECKING
    mc->state = mc_idle;
#endif /* QM_CHECKING */
    return E_OK;
}

static __inline__ void qm_mc_finish(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_idle);
    if (mc->state != mc_idle)
        REPORT_ERROR(WARNING, E_INVALID_STATE, ("Losing incomplete MC command"));
#endif /* QM_CHECKING */
    __qm_portal_unbind(portal, QM_BIND_MC);
}

static __inline__ struct qm_mc_command *qm_mc_start(struct qm_portal *portal)
{
    register struct qm_mc *mc = &portal->mc;
#ifdef QM_CHECKING
    ASSERT_COND(mc->state == mc_idle);
    mc->state = mc_user;
#endif /* QM_CHECKING */
    dcbz_64(mc->cr);
    return mc->cr;
}

static __inline__ void qm_mc_abort(struct qm_portal *portal)
{
#ifdef QM_CHECKING
    register struct qm_mc *mc = &portal->mc;
    ASSERT_COND(mc->state == mc_user);
    mc->state = mc_idle;
#else
    UNUSED(portal);
#endif /* QM_CHECKING */
}

static __inline__ void qm_mc_commit(struct qm_portal *portal, uint8_t myverb)
{
    register struct qm_mc *mc = &portal->mc;
#ifdef QM_CHECKING
    ASSERT_COND(mc->state == mc_user);
#endif /* QM_CHECKING */
    rmb();
    mc->cr->__dont_write_directly__verb = (uint8_t)(myverb | mc->vbit);
    dcbf_64(mc->cr);
    dcbit_ro(mc->rr + mc->rridx);
#ifdef QM_CHECKING
    mc->state = mc_hw;
#endif /* QM_CHECKING */
}

static __inline__ struct qm_mc_result *qm_mc_result(struct qm_portal *portal)
{
    register struct qm_mc *mc = &portal->mc;
    struct qm_mc_result *rr = mc->rr + mc->rridx;
#ifdef QM_CHECKING
    ASSERT_COND(mc->state == mc_hw);
#endif /* QM_CHECKING */
    /* The inactive response register's verb byte always returns zero until
     * its command is submitted and completed. This includes the valid-bit,
     * in case you were wondering... */
    if (!rr->verb) {
        dcbit_ro(rr);
        return NULL;
    }
    mc->rridx ^= 1;
    mc->vbit ^= QM_MCC_VERB_VBIT;
#ifdef QM_CHECKING
    mc->state = mc_idle;
#endif /* QM_CHECKING */
    return rr;
}

/* ------------------------------------- */
/* --- Portal interrupt register API --- */

static __inline__ t_Error qm_isr_init(struct qm_portal *portal)
{
    if (__qm_portal_bind(portal, QM_BIND_ISR))
        return ERROR_CODE(E_BUSY);
    return E_OK;
}

static __inline__ void qm_isr_finish(struct qm_portal *portal)
{
    __qm_portal_unbind(portal, QM_BIND_ISR);
}

static __inline__ void qm_isr_set_iperiod(struct qm_portal *portal, uint16_t iperiod)
{
    qm_out(ITPR, iperiod);
}

static __inline__ uint32_t __qm_isr_read(struct qm_portal *portal, enum qm_isr_reg n)
{
    return __qm_in(&portal->addr, REG_ISR + (n << 2));
}

static __inline__ void __qm_isr_write(struct qm_portal *portal, enum qm_isr_reg n, uint32_t val)
{
    __qm_out(&portal->addr, REG_ISR + (n << 2), val);
}
