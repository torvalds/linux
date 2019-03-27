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
 @File          bm.c

 @Description   BM
*//***************************************************************************/
#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "mem_ext.h"
#include "core_ext.h"

#include "bm.h"


#define __ERR_MODULE__  MODULE_BM


/****************************************/
/*       static functions               */
/****************************************/

static uint32_t __poll_portal_slow(t_BmPortal *p);
static void __poll_portal_fast(t_BmPortal *p);

/* Portal interrupt handler */
static void portal_isr(void *ptr)
{
    t_BmPortal *portal = ptr;
        /* Only do fast-path handling if it's required */
    if (portal->flags & BMAN_PORTAL_FLAG_IRQ_FAST)
        __poll_portal_fast(portal);
    __poll_portal_slow(portal);

}

/**
 * bman_create_portal - Manage a Bman s/w portal
 * @portal: the s/w corenet portal to use
 * @flags: bit-mask of BMAN_PORTAL_FLAG_*** options
 * @pools: bit-array of buffer pools available to this portal
 * @portal_ctx: opaque user-supplied data to be associated with the portal
 *
 * Creates a managed portal object. @irq is only used if @flags specifies
 * BMAN_PORTAL_FLAG_IRQ. @pools is copied, so the caller can do as they please
 * with it after the function returns. It will only be possible to configure
 * buffer pool objects as "suppliers" if they are specified in @pools, and the
 * driver will only track depletion state changes to the same subset of buffer
 * pools. If @pools is NULL, buffer pool depletion state will not be tracked.
 * If the BMAN_PORTAL_FLAG_RECOVER flag is specified, then the function will
 * attempt to expire any existing RCR entries, otherwise the function will fail
 * if RCR is non-empty. If the BMAN_PORTAL_FLAG_WAIT flag is set, the function
 * is allowed to block waiting for expiration of RCR. BMAN_PORTAL_FLAG_WAIT_INT
 * makes any blocking interruptible.
 */

static t_Error bman_create_portal(t_BmPortal *p_BmPortal,
                                  uint32_t flags,
                                  const struct bman_depletion *pools)
{
    int                             ret = 0;
    uint8_t                         bpid = 0;
    e_BmPortalRcrConsumeMode        rcr_cmode;
    e_BmPortalProduceMode           pmode;

    pmode     = e_BmPortalPVB;
    rcr_cmode = (flags & BMAN_PORTAL_FLAG_CACHE) ? e_BmPortalRcrCCE : e_BmPortalRcrCCI;

    switch (pmode)
    {
        case e_BmPortalPCI:
            p_BmPortal->cbs[BM_RCR_RING].f_BmCommitCb = bm_rcr_pci_commit;
            break;
        case e_BmPortalPCE:
            p_BmPortal->cbs[BM_RCR_RING].f_BmCommitCb = bm_rcr_pce_commit;
            break;
        case e_BmPortalPVB:
            p_BmPortal->cbs[BM_RCR_RING].f_BmCommitCb = bm_rcr_pvb_commit;
            break;
    }
    switch (rcr_cmode)
    {
        case e_BmPortalRcrCCI:
            p_BmPortal->cbs[BM_RCR_RING].f_BmUpdateCb      = bm_rcr_cci_update;
            p_BmPortal->cbs[BM_RCR_RING].f_BmPrefetchCb    = NULL;
            break;
        case e_BmPortalRcrCCE:
            p_BmPortal->cbs[BM_RCR_RING].f_BmUpdateCb      = bm_rcr_cce_update;
            p_BmPortal->cbs[BM_RCR_RING].f_BmPrefetchCb    = bm_rcr_cce_prefetch;
            break;
    }

    if (bm_rcr_init(p_BmPortal->p_BmPortalLow, pmode, rcr_cmode)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("RCR initialization failed"));
        goto fail_rcr;
    }
    if (bm_mc_init(p_BmPortal->p_BmPortalLow)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("MC initialization failed"));
        goto fail_mc;
    }
    p_BmPortal->pools[0] = *pools;
    bman_depletion_init(&p_BmPortal->pools[1]);
    while (bpid < BM_MAX_NUM_OF_POOLS) {
        /* Default to all BPIDs disabled, we enable as required
         * at run-time. */
        bm_isr_bscn_mask(p_BmPortal->p_BmPortalLow, bpid, 0);
        bpid++;
    }
    p_BmPortal->flags = flags;
    p_BmPortal->slowpoll = 0;
    p_BmPortal->rcrProd = p_BmPortal->rcrCons = 0;
    memset(&p_BmPortal->depletionPoolsTable, 0, sizeof(p_BmPortal->depletionPoolsTable));
    /* Write-to-clear any stale interrupt status bits */
    bm_isr_disable_write(p_BmPortal->p_BmPortalLow, 0xffffffff);
    bm_isr_status_clear(p_BmPortal->p_BmPortalLow, 0xffffffff);
    bm_isr_enable_write(p_BmPortal->p_BmPortalLow, BM_PIRQ_RCRI | BM_PIRQ_BSCN);
    if (flags & BMAN_PORTAL_FLAG_IRQ)
    {
        XX_SetIntr(p_BmPortal->irq, portal_isr, p_BmPortal);
        XX_EnableIntr(p_BmPortal->irq);
        /* Enable the bits that make sense */
        bm_isr_uninhibit(p_BmPortal->p_BmPortalLow);
    } else
        /* without IRQ, we can't block */
        flags &= ~BMAN_PORTAL_FLAG_WAIT;
    /* Need RCR to be empty before continuing */
    bm_isr_disable_write(p_BmPortal->p_BmPortalLow, (uint32_t)~BM_PIRQ_RCRI);
    if (!(flags & BMAN_PORTAL_FLAG_RECOVER) ||
        !(flags & BMAN_PORTAL_FLAG_WAIT))
        ret = bm_rcr_get_fill(p_BmPortal->p_BmPortalLow);
    if (ret) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("RCR unclean, need recovery"));
        goto fail_rcr_empty;
    }
    bm_isr_disable_write(p_BmPortal->p_BmPortalLow, 0);
    return E_OK;
fail_rcr_empty:
    bm_mc_finish(p_BmPortal->p_BmPortalLow);
fail_mc:
    bm_rcr_finish(p_BmPortal->p_BmPortalLow);
fail_rcr:
    XX_Free(p_BmPortal);
    return ERROR_CODE(E_INVALID_STATE);
}

static void bman_destroy_portal(t_BmPortal* p_BmPortal)
{
    BmUpdate(p_BmPortal, BM_RCR_RING);
    if (p_BmPortal->flags & BMAN_PORTAL_FLAG_IRQ)
    {
        XX_DisableIntr(p_BmPortal->irq);
        XX_FreeIntr(p_BmPortal->irq);
    }
    bm_mc_finish(p_BmPortal->p_BmPortalLow);
    bm_rcr_finish(p_BmPortal->p_BmPortalLow);
    XX_Free(p_BmPortal->p_BmPortalLow);
}

/* When release logic waits on available RCR space, we need a global waitqueue
 * in the case of "affine" use (as the waits wake on different cpus which means
 * different portals - so we can't wait on any per-portal waitqueue). */

static uint32_t __poll_portal_slow(t_BmPortal* p_BmPortal)
{
    struct bman_depletion tmp;
    t_BmPool              *p_BmPool;
    uint32_t ret,is = bm_isr_status_read(p_BmPortal->p_BmPortalLow);
    ret = is;

    /* There is a gotcha to be aware of. If we do the query before clearing
     * the status register, we may miss state changes that occur between the
     * two. If we write to clear the status register before the query, the
     * cache-enabled query command may overtake the status register write
     * unless we use a heavyweight sync (which we don't want). Instead, we
     * write-to-clear the status register then *read it back* before doing
     * the query, hence the odd while loop with the 'is' accumulation. */
    if (is & BM_PIRQ_BSCN) {
        uint32_t i, j;
        uint32_t __is;
        bm_isr_status_clear(p_BmPortal->p_BmPortalLow, BM_PIRQ_BSCN);
        while ((__is = bm_isr_status_read(p_BmPortal->p_BmPortalLow)) & BM_PIRQ_BSCN) {
            is |= __is;
            bm_isr_status_clear(p_BmPortal->p_BmPortalLow, BM_PIRQ_BSCN);
        }
        is &= ~BM_PIRQ_BSCN;
        BmPortalQuery(p_BmPortal, &tmp, TRUE);
        for (i = 0; i < 2; i++) {
            uint32_t idx = i * 32;
            /* tmp is a mask of currently-depleted pools.
             * pools[0] is mask of those we care about.
             * pools[1] is our previous view (we only want to
             * be told about changes). */
            tmp.__state[i] &= p_BmPortal->pools[0].__state[i];
            if (tmp.__state[i] == p_BmPortal->pools[1].__state[i])
                /* fast-path, nothing to see, move along */
                continue;
            for (j = 0; j <= 31; j++, idx++) {
                int b4 = bman_depletion_get(&p_BmPortal->pools[1], (uint8_t)idx);
                int af = bman_depletion_get(&tmp, (uint8_t)idx);
                if (b4 == af)
                    continue;
                p_BmPool = p_BmPortal->depletionPoolsTable[idx];
                ASSERT_COND(p_BmPool->f_Depletion);
                p_BmPool->f_Depletion(p_BmPool->h_App, (bool)af);
            }
        }
        p_BmPortal->pools[1] = tmp;
    }

    if (is & BM_PIRQ_RCRI) {
        NCSW_PLOCK(p_BmPortal);
        p_BmPortal->rcrCons += BmUpdate(p_BmPortal, BM_RCR_RING);
        bm_rcr_set_ithresh(p_BmPortal->p_BmPortalLow, 0);
        PUNLOCK(p_BmPortal);
        bm_isr_status_clear(p_BmPortal->p_BmPortalLow, BM_PIRQ_RCRI);
        is &= ~BM_PIRQ_RCRI;
    }

    /* There should be no status register bits left undefined */
    ASSERT_COND(!is);
    return ret;
}

static void __poll_portal_fast(t_BmPortal* p_BmPortal)
{
    UNUSED(p_BmPortal);
    /* nothing yet, this is where we'll put optimised RCR consumption
     * tracking */
}


static __inline__ void rel_set_thresh(t_BmPortal *p_BmPortal, int check)
{
    if (!check || !bm_rcr_get_ithresh(p_BmPortal->p_BmPortalLow))
        bm_rcr_set_ithresh(p_BmPortal->p_BmPortalLow, RCR_ITHRESH);
}

/* Used as a wait_event() expression. If it returns non-NULL, any lock will
 * remain held. */
static struct bm_rcr_entry *try_rel_start(t_BmPortal *p_BmPortal)
{
    struct bm_rcr_entry *r;

    NCSW_PLOCK(p_BmPortal);
    if (bm_rcr_get_avail((p_BmPortal)->p_BmPortalLow) < RCR_THRESH)
        BmUpdate(p_BmPortal, BM_RCR_RING);
    r = bm_rcr_start((p_BmPortal)->p_BmPortalLow);
    if (!r) {
        rel_set_thresh(p_BmPortal, 1);
        PUNLOCK(p_BmPortal);
    }
    return r;
}

static __inline__ t_Error wait_rel_start(t_BmPortal             *p_BmPortal,
                                         struct bm_rcr_entry    **rel,
                                         uint32_t               flags)
{
    int tries = 100;

    UNUSED(flags);
    do {
        *rel = try_rel_start(p_BmPortal);
        XX_Sleep(1);
    } while (!*rel && --tries);

    if (!(*rel))
        return ERROR_CODE(E_BUSY);

    return E_OK;
}

/* This copies Qman's eqcr_completed() routine, see that for details */
static int rel_completed(t_BmPortal *p_BmPortal, uint32_t rcr_poll)
{
    uint32_t tr_cons = p_BmPortal->rcrCons;
    if (rcr_poll & 0xc0000000) {
        rcr_poll &= 0x7fffffff;
        tr_cons ^= 0x80000000;
    }
    if (tr_cons >= rcr_poll)
        return 1;
    if ((rcr_poll - tr_cons) > BM_RCR_SIZE)
        return 1;
    if (!bm_rcr_get_fill(p_BmPortal->p_BmPortalLow))
        /* If RCR is empty, we must have completed */
        return 1;
    rel_set_thresh(p_BmPortal, 0);
    return 0;
}

static __inline__ void rel_commit(t_BmPortal *p_BmPortal, uint32_t flags,uint8_t num)
{
    uint32_t rcr_poll;

    BmCommit(p_BmPortal, BM_RCR_RING, (uint8_t)(BM_RCR_VERB_CMD_BPID_SINGLE | (num & BM_RCR_VERB_BUFCOUNT_MASK)));
    /* increment the producer count and capture it for SYNC */
    rcr_poll = ++p_BmPortal->rcrProd;
    if ((flags & BMAN_RELEASE_FLAG_WAIT_SYNC) ==
        BMAN_RELEASE_FLAG_WAIT_SYNC)
        rel_set_thresh(p_BmPortal, 1);
    PUNLOCK(p_BmPortal);
    if ((flags & BMAN_RELEASE_FLAG_WAIT_SYNC) !=
        BMAN_RELEASE_FLAG_WAIT_SYNC)
        return;
    rel_completed(p_BmPortal, rcr_poll);
}


/****************************************/
/*       Inter-Module functions        */
/****************************************/

/**
 * bman_release - Release buffer(s) to the buffer pool
 * @p_BmPool: the buffer pool object to release to
 * @bufs: an array of buffers to release
 * @num: the number of buffers in @bufs (1-8)
 * @flags: bit-mask of BMAN_RELEASE_FLAG_*** options
 *
 * Adds the given buffers to RCR entries. If the portal @p_BmPortal was created with the
 * "COMPACT" flag, then it will be using a compaction algorithm to improve
 * utilization of RCR. As such, these buffers may join an existing ring entry
 * and/or it may not be issued right away so as to allow future releases to join
 * the same ring entry. Use the BMAN_RELEASE_FLAG_NOW flag to override this
 * behavior by committing the RCR entry (or entries) right away. If the RCR
 * ring is full, the function will return -EBUSY unless BMAN_RELEASE_FLAG_WAIT
 * is selected, in which case it will sleep waiting for space to become
 * available in RCR. If the function receives a signal before such time (and
 * BMAN_RELEASE_FLAG_WAIT_INT is set), the function returns -EINTR. Otherwise,
 * it returns zero.
 */

t_Error BmPortalRelease(t_Handle h_BmPortal,
                        uint8_t bpid,
                        struct bm_buffer *bufs,
                        uint8_t num,
                        uint32_t flags)
{
    t_BmPortal          *p_BmPortal = (t_BmPortal *)h_BmPortal;
    struct bm_rcr_entry *r;
    uint8_t i;

    SANITY_CHECK_RETURN_ERROR(p_BmPortal, E_INVALID_HANDLE);
    /* TODO: I'm ignoring BMAN_PORTAL_FLAG_COMPACT for now. */
    r = try_rel_start(p_BmPortal);
    if (!r) {
        if (flags & BMAN_RELEASE_FLAG_WAIT) {
            t_Error ret = wait_rel_start(p_BmPortal, &r, flags);
            if (ret)
                return ret;
        } else
            return ERROR_CODE(E_BUSY);
        ASSERT_COND(r != NULL);
    }
    r->bpid = bpid;
    for (i = 0; i < num; i++) {
        r->bufs[i].hi = bufs[i].hi;
        r->bufs[i].lo = bufs[i].lo;
    }
    /* Issue the release command and wait for sync if requested. NB: the
     * commit can't fail, only waiting can. Don't propagate any failure if a
     * signal arrives, otherwise the caller can't distinguish whether the
     * release was issued or not. Code for user-space can check
     * signal_pending() after we return. */
    rel_commit(p_BmPortal, flags, num);
    return E_OK;
}

uint8_t BmPortalAcquire(t_Handle h_BmPortal,
                        uint8_t  bpid,
                        struct bm_buffer *bufs,
                        uint8_t num)
{
    t_BmPortal          *p_BmPortal = (t_BmPortal *)h_BmPortal;
    struct bm_mc_command *mcc;
    struct bm_mc_result *mcr;
    uint8_t ret = 0;

    SANITY_CHECK_RETURN_VALUE(p_BmPortal, E_INVALID_HANDLE, 0);
    NCSW_PLOCK(p_BmPortal);
    mcc = bm_mc_start(p_BmPortal->p_BmPortalLow);
    mcc->acquire.bpid = bpid;
    bm_mc_commit(p_BmPortal->p_BmPortalLow,
                 (uint8_t)(BM_MCC_VERB_CMD_ACQUIRE |
                           (num & BM_MCC_VERB_ACQUIRE_BUFCOUNT)));
    while (!(mcr = bm_mc_result(p_BmPortal->p_BmPortalLow))) ;
    ret = num = (uint8_t)(mcr->verb & BM_MCR_VERB_ACQUIRE_BUFCOUNT);
    ASSERT_COND(num <= 8);
    while (num--) {
        bufs[num].bpid = bpid;
        bufs[num].hi = mcr->acquire.bufs[num].hi;
        bufs[num].lo = mcr->acquire.bufs[num].lo;
    }
    PUNLOCK(p_BmPortal);
    return ret;
}

t_Error BmPortalQuery(t_Handle h_BmPortal, struct bman_depletion *p_Pools, bool depletion)
{
    t_BmPortal          *p_BmPortal = (t_BmPortal *)h_BmPortal;
    struct bm_mc_result *mcr;

    SANITY_CHECK_RETURN_ERROR(p_BmPortal, E_INVALID_HANDLE);

    NCSW_PLOCK(p_BmPortal);
    bm_mc_start(p_BmPortal->p_BmPortalLow);
    bm_mc_commit(p_BmPortal->p_BmPortalLow, BM_MCC_VERB_CMD_QUERY);
    while (!(mcr = bm_mc_result(p_BmPortal->p_BmPortalLow))) ;
    if (depletion)
        *p_Pools = mcr->query.ds.state;
    else
        *p_Pools = mcr->query.as.state;
    PUNLOCK(p_BmPortal);
    return E_OK;
}

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle BM_PORTAL_Config(t_BmPortalParam *p_BmPortalParam)
{
    t_BmPortal          *p_BmPortal;

    SANITY_CHECK_RETURN_VALUE(p_BmPortalParam, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_BmPortalParam->h_Bm, E_INVALID_HANDLE, NULL);

    p_BmPortal = (t_BmPortal *)XX_Malloc(sizeof(t_BmPortal));
    if (!p_BmPortal)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Bm Portal obj!!!"));
        return NULL;
    }
    memset(p_BmPortal, 0, sizeof(t_BmPortal));

    p_BmPortal->p_BmPortalLow = (struct bm_portal *)XX_Malloc(sizeof(struct bm_portal));
    if (!p_BmPortal->p_BmPortalLow)
    {
        XX_Free(p_BmPortal);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Low bm portal obj!!!"));
        return NULL;
    }
    memset(p_BmPortal->p_BmPortalLow, 0, sizeof(struct bm_portal));

    p_BmPortal->p_BmPortalDriverParams = (t_BmPortalDriverParams *)XX_Malloc(sizeof(t_BmPortalDriverParams));
    if (!p_BmPortal->p_BmPortalDriverParams)
    {
        XX_Free(p_BmPortal);
        XX_Free(p_BmPortal->p_BmPortalLow);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Bm Portal driver parameters"));
        return NULL;
    }
    memset(p_BmPortal->p_BmPortalDriverParams, 0, sizeof(t_BmPortalDriverParams));

    p_BmPortal->p_BmPortalLow->addr.addr_ce = UINT_TO_PTR(p_BmPortalParam->ceBaseAddress);
    p_BmPortal->p_BmPortalLow->addr.addr_ci = UINT_TO_PTR(p_BmPortalParam->ciBaseAddress);
    p_BmPortal->cpu   = (int)p_BmPortalParam->swPortalId;
    p_BmPortal->irq   = p_BmPortalParam->irq;

    p_BmPortal->h_Bm    = p_BmPortalParam->h_Bm;

    p_BmPortal->p_BmPortalDriverParams->hwExtStructsMemAttr     = DEFAULT_memAttr;
    bman_depletion_fill(&p_BmPortal->p_BmPortalDriverParams->mask);

    return p_BmPortal;
}

t_Error BM_PORTAL_Init(t_Handle h_BmPortal)
{
    t_BmPortal          *p_BmPortal = (t_BmPortal *)h_BmPortal;
    uint32_t            flags;

    SANITY_CHECK_RETURN_ERROR(p_BmPortal, E_INVALID_HANDLE);

    flags = (uint32_t)((p_BmPortal->irq != NO_IRQ) ? BMAN_PORTAL_FLAG_IRQ : 0);
    flags |= ((p_BmPortal->p_BmPortalDriverParams->hwExtStructsMemAttr & MEMORY_ATTR_CACHEABLE) ?
           BMAN_PORTAL_FLAG_CACHE : 0);

    if (bman_create_portal(p_BmPortal,flags,&p_BmPortal->p_BmPortalDriverParams->mask)!=E_OK)
    {
        BM_PORTAL_Free(p_BmPortal);
        RETURN_ERROR(MAJOR, E_NULL_POINTER, ("create portal failed"));
    }
    BmSetPortalHandle(p_BmPortal->h_Bm, (t_Handle)p_BmPortal, (e_DpaaSwPortal)p_BmPortal->cpu);

    XX_Free(p_BmPortal->p_BmPortalDriverParams);
    p_BmPortal->p_BmPortalDriverParams = NULL;

    DBG(TRACE,("Bman-Portal (%d) @ %p:%p\n",
               p_BmPortal->cpu,
               p_BmPortal->p_BmPortalLow->addr.addr_ce,
               p_BmPortal->p_BmPortalLow->addr.addr_ci
               ));

    DBG(TRACE,("Bman-Portal (%d) @ 0x%016llx:0x%016llx",
               p_BmPortal->cpu,
               (uint64_t)XX_VirtToPhys(p_BmPortal->p_BmPortalLow->addr.addr_ce),
               (uint64_t)XX_VirtToPhys(p_BmPortal->p_BmPortalLow->addr.addr_ci)
               ));

    return E_OK;
}

t_Error BM_PORTAL_Free(t_Handle h_BmPortal)
{
    t_BmPortal  *p_BmPortal = (t_BmPortal *)h_BmPortal;

    if (!p_BmPortal)
       return ERROR_CODE(E_INVALID_HANDLE);
    BmSetPortalHandle(p_BmPortal->h_Bm, NULL, (e_DpaaSwPortal)p_BmPortal->cpu);
    bman_destroy_portal(p_BmPortal);
    XX_Free(p_BmPortal);
    return E_OK;
}

t_Error BM_PORTAL_ConfigMemAttr(t_Handle h_BmPortal, uint32_t hwExtStructsMemAttr)
{
    t_BmPortal  *p_BmPortal = (t_BmPortal *)h_BmPortal;

    SANITY_CHECK_RETURN_ERROR(p_BmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPortal->p_BmPortalDriverParams, E_INVALID_HANDLE);

    p_BmPortal->p_BmPortalDriverParams->hwExtStructsMemAttr = hwExtStructsMemAttr;

    return E_OK;
}
