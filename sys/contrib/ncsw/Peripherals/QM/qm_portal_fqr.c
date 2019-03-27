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
 @File          qm.c

 @Description   QM & Portal implementation
*//***************************************************************************/
#include <sys/cdefs.h>
#include <sys/types.h>
#include <machine/atomic.h>

#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "mm_ext.h"
#include "qm.h"
#include "qman_low.h"

#include <machine/vmparam.h>

/****************************************/
/*       static functions               */
/****************************************/

#define SLOW_POLL_IDLE   1000
#define SLOW_POLL_BUSY   10

/*
 * Context entries are 32-bit.  The qman driver uses the pointer to the queue as
 * its context, and the pointer is 64-byte aligned, per the XX_MallocSmart()
 * call.  Take advantage of this fact to shove a 64-bit kernel pointer into a
 * 32-bit context integer, and back.
 *
 * XXX: This depends on the fact that VM_MAX_KERNEL_ADDRESS is less than 38-bit
 * count from VM_MIN_KERNEL_ADDRESS.  If this ever changes, this needs to be
 * updated.
 */
CTASSERT((VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS) < (1ULL << 35));
static inline uint32_t
aligned_int_from_ptr(const void *p)
{
	uintptr_t ctx;

	ctx = (uintptr_t)p;
	KASSERT(ctx >= VM_MIN_KERNEL_ADDRESS, ("%p is too low!\n", p));
	ctx -= VM_MIN_KERNEL_ADDRESS;
	KASSERT((ctx & 0x07) == 0, ("Pointer %p is not 8-byte aligned!\n", p));

	return (ctx >> 3);
}

static inline void *
ptr_from_aligned_int(uint32_t ctx)
{
	uintptr_t p;

	p = ctx;
	p = VM_MIN_KERNEL_ADDRESS + (p << 3);

	return ((void *)p);
}

static t_Error qman_volatile_dequeue(t_QmPortal     *p_QmPortal,
                                     struct qman_fq *p_Fq,
                                     uint32_t       vdqcr)
{
    ASSERT_COND((p_Fq->state == qman_fq_state_parked) ||
                (p_Fq->state == qman_fq_state_retired));
    ASSERT_COND(!(vdqcr & QM_VDQCR_FQID_MASK));
    ASSERT_COND(!(p_Fq->flags & QMAN_FQ_STATE_VDQCR));

    vdqcr = (vdqcr & ~QM_VDQCR_FQID_MASK) | p_Fq->fqid;
    NCSW_PLOCK(p_QmPortal);
    FQLOCK(p_Fq);
    p_Fq->flags |= QMAN_FQ_STATE_VDQCR;
    qm_dqrr_vdqcr_set(p_QmPortal->p_LowQmPortal, vdqcr);
    FQUNLOCK(p_Fq);
    PUNLOCK(p_QmPortal);

    return E_OK;
}

static const char *mcr_result_str(uint8_t result)
{
    switch (result) {
    case QM_MCR_RESULT_NULL:
        return "QM_MCR_RESULT_NULL";
    case QM_MCR_RESULT_OK:
        return "QM_MCR_RESULT_OK";
    case QM_MCR_RESULT_ERR_FQID:
        return "QM_MCR_RESULT_ERR_FQID";
    case QM_MCR_RESULT_ERR_FQSTATE:
        return "QM_MCR_RESULT_ERR_FQSTATE";
    case QM_MCR_RESULT_ERR_NOTEMPTY:
        return "QM_MCR_RESULT_ERR_NOTEMPTY";
    case QM_MCR_RESULT_PENDING:
        return "QM_MCR_RESULT_PENDING";
    }
    return "<unknown MCR result>";
}

static t_Error qman_create_fq(t_QmPortal        *p_QmPortal,
                              uint32_t          fqid,
                              uint32_t          flags,
                              struct qman_fq    *p_Fq)
{
    struct qm_fqd fqd;
    struct qm_mcr_queryfq_np np;
    struct qm_mc_command *p_Mcc;
    struct qm_mc_result *p_Mcr;

    p_Fq->fqid = fqid;
    p_Fq->flags = flags;
    p_Fq->state = qman_fq_state_oos;
    p_Fq->cgr_groupid = 0;
    if (!(flags & QMAN_FQ_FLAG_RECOVER) ||
            (flags & QMAN_FQ_FLAG_NO_MODIFY))
        return E_OK;
    /* Everything else is RECOVER support */
    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->queryfq.fqid = fqid;
    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYFQ);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_QUERYFQ);
    if (p_Mcr->result != QM_MCR_RESULT_OK) {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("QUERYFQ failed: %s", mcr_result_str(p_Mcr->result)));
    }
    fqd = p_Mcr->queryfq.fqd;
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->queryfq_np.fqid = fqid;
    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYFQ_NP);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_QUERYFQ_NP);
    if (p_Mcr->result != QM_MCR_RESULT_OK) {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("UERYFQ_NP failed: %s", mcr_result_str(p_Mcr->result)));
    }
    np = p_Mcr->queryfq_np;
    /* Phew, have queryfq and queryfq_np results, stitch together
     * the FQ object from those. */
    p_Fq->cgr_groupid = fqd.cgid;
    switch (np.state & QM_MCR_NP_STATE_MASK) {
    case QM_MCR_NP_STATE_OOS:
        break;
    case QM_MCR_NP_STATE_RETIRED:
        p_Fq->state = qman_fq_state_retired;
        if (np.frm_cnt)
            p_Fq->flags |= QMAN_FQ_STATE_NE;
        break;
    case QM_MCR_NP_STATE_TEN_SCHED:
    case QM_MCR_NP_STATE_TRU_SCHED:
    case QM_MCR_NP_STATE_ACTIVE:
        p_Fq->state = qman_fq_state_sched;
        if (np.state & QM_MCR_NP_STATE_R)
            p_Fq->flags |= QMAN_FQ_STATE_CHANGING;
        break;
    case QM_MCR_NP_STATE_PARKED:
        p_Fq->state = qman_fq_state_parked;
        break;
    default:
        ASSERT_COND(FALSE);
    }
    if (fqd.fq_ctrl & QM_FQCTRL_CGE)
        p_Fq->state |= QMAN_FQ_STATE_CGR_EN;
    PUNLOCK(p_QmPortal);

    return E_OK;
}

static void qman_destroy_fq(struct qman_fq *p_Fq, uint32_t flags)
{
    /* We don't need to lock the FQ as it is a pre-condition that the FQ be
     * quiesced. Instead, run some checks. */
    UNUSED(flags);
    switch (p_Fq->state) {
    case qman_fq_state_parked:
        ASSERT_COND(flags & QMAN_FQ_DESTROY_PARKED);
    case qman_fq_state_oos:
        return;
    default:
        break;
    }
    ASSERT_COND(FALSE);
}

static t_Error qman_init_fq(t_QmPortal          *p_QmPortal,
                            struct qman_fq      *p_Fq,
                            uint32_t            flags,
                            struct qm_mcc_initfq *p_Opts)
{
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    uint8_t res, myverb = (uint8_t)((flags & QMAN_INITFQ_FLAG_SCHED) ?
        QM_MCC_VERB_INITFQ_SCHED : QM_MCC_VERB_INITFQ_PARKED);

    SANITY_CHECK_RETURN_ERROR((p_Fq->state == qman_fq_state_oos) ||
                              (p_Fq->state == qman_fq_state_parked),
                              E_INVALID_STATE);

    if (p_Fq->flags & QMAN_FQ_FLAG_NO_MODIFY)
        return ERROR_CODE(E_INVALID_VALUE);
    /* Issue an INITFQ_[PARKED|SCHED] management command */
    NCSW_PLOCK(p_QmPortal);
    FQLOCK(p_Fq);
    if ((p_Fq->flags & QMAN_FQ_STATE_CHANGING) ||
            ((p_Fq->state != qman_fq_state_oos) &&
                (p_Fq->state != qman_fq_state_parked))) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_BUSY);
    }
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    Mem2IOCpy32((void*)&p_Mcc->initfq, p_Opts, sizeof(struct qm_mcc_initfq));
    qm_mc_commit(p_QmPortal->p_LowQmPortal, myverb);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == myverb);
    res = p_Mcr->result;
    if (res != QM_MCR_RESULT_OK) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE,("INITFQ failed: %s", mcr_result_str(res)));
    }

    if (p_Mcc->initfq.we_mask & QM_INITFQ_WE_FQCTRL) {
        if (p_Mcc->initfq.fqd.fq_ctrl & QM_FQCTRL_CGE)
            p_Fq->flags |= QMAN_FQ_STATE_CGR_EN;
        else
            p_Fq->flags &= ~QMAN_FQ_STATE_CGR_EN;
    }
    if (p_Mcc->initfq.we_mask & QM_INITFQ_WE_CGID)
        p_Fq->cgr_groupid = p_Mcc->initfq.fqd.cgid;
    p_Fq->state = (flags & QMAN_INITFQ_FLAG_SCHED) ?
            qman_fq_state_sched : qman_fq_state_parked;
    FQUNLOCK(p_Fq);
    PUNLOCK(p_QmPortal);
    return E_OK;
}

static t_Error qman_retire_fq(t_QmPortal        *p_QmPortal,
                              struct qman_fq    *p_Fq,
                              uint32_t          *p_Flags,
                              bool              drain)
{
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    t_Error                 err = E_OK;
    uint8_t                 res;

    SANITY_CHECK_RETURN_ERROR((p_Fq->state == qman_fq_state_parked) ||
                              (p_Fq->state == qman_fq_state_sched),
                              E_INVALID_STATE);

    if (p_Fq->flags & QMAN_FQ_FLAG_NO_MODIFY)
        return E_INVALID_VALUE;
    NCSW_PLOCK(p_QmPortal);
    FQLOCK(p_Fq);
    if ((p_Fq->flags & QMAN_FQ_STATE_CHANGING) ||
            (p_Fq->state == qman_fq_state_retired) ||
                (p_Fq->state == qman_fq_state_oos)) {
        err = E_BUSY;
        goto out;
    }
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->alterfq.fqid = p_Fq->fqid;
    if (drain)
        p_Mcc->alterfq.context_b = aligned_int_from_ptr(p_Fq);
    qm_mc_commit(p_QmPortal->p_LowQmPortal,
                 (uint8_t)((drain)?QM_MCC_VERB_ALTER_RETIRE_CTXB:QM_MCC_VERB_ALTER_RETIRE));
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) ==
                (drain)?QM_MCR_VERB_ALTER_RETIRE_CTXB:QM_MCR_VERB_ALTER_RETIRE);
    res = p_Mcr->result;
    if (res == QM_MCR_RESULT_OK)
    {
        /* Process 'fq' right away, we'll ignore FQRNI */
        if (p_Mcr->alterfq.fqs & QM_MCR_FQS_NOTEMPTY)
            p_Fq->flags |= QMAN_FQ_STATE_NE;
        if (p_Mcr->alterfq.fqs & QM_MCR_FQS_ORLPRESENT)
            p_Fq->flags |= QMAN_FQ_STATE_ORL;
        p_Fq->state = qman_fq_state_retired;
    }
    else if (res == QM_MCR_RESULT_PENDING)
        p_Fq->flags |= QMAN_FQ_STATE_CHANGING;
    else {
        XX_Print("ALTER_RETIRE failed: %s\n",
                mcr_result_str(res));
        err = E_INVALID_STATE;
    }
    if (p_Flags)
        *p_Flags = p_Fq->flags;
out:
    FQUNLOCK(p_Fq);
    PUNLOCK(p_QmPortal);
    return err;
}

static t_Error qman_oos_fq(t_QmPortal *p_QmPortal, struct qman_fq *p_Fq)
{
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    uint8_t                 res;

    ASSERT_COND(p_Fq->state == qman_fq_state_retired);
    if (p_Fq->flags & QMAN_FQ_FLAG_NO_MODIFY)
        return ERROR_CODE(E_INVALID_VALUE);
    NCSW_PLOCK(p_QmPortal);
    FQLOCK(p_Fq);
    if ((p_Fq->flags & QMAN_FQ_STATE_BLOCKOOS) ||
            (p_Fq->state != qman_fq_state_retired)) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_BUSY);
    }
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->alterfq.fqid = p_Fq->fqid;
    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_ALTER_OOS);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_ALTER_OOS);
    res = p_Mcr->result;
    if (res != QM_MCR_RESULT_OK) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("ALTER_OOS failed: %s\n", mcr_result_str(res)));
    }
    p_Fq->state = qman_fq_state_oos;

    FQUNLOCK(p_Fq);
    PUNLOCK(p_QmPortal);
    return E_OK;
}

static t_Error qman_schedule_fq(t_QmPortal *p_QmPortal, struct qman_fq *p_Fq)
{
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    uint8_t                 res;

    ASSERT_COND(p_Fq->state == qman_fq_state_parked);
    if (p_Fq->flags & QMAN_FQ_FLAG_NO_MODIFY)
        return ERROR_CODE(E_INVALID_VALUE);
    /* Issue a ALTERFQ_SCHED management command */
    NCSW_PLOCK(p_QmPortal);
    FQLOCK(p_Fq);
    if ((p_Fq->flags & QMAN_FQ_STATE_CHANGING) ||
            (p_Fq->state != qman_fq_state_parked)) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_BUSY);
    }
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->alterfq.fqid = p_Fq->fqid;
    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_ALTER_SCHED);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_ALTER_SCHED);
    res = p_Mcr->result;
    if (res != QM_MCR_RESULT_OK) {
        FQUNLOCK(p_Fq);
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("ALTER_SCHED failed: %s\n", mcr_result_str(res)));
    }
    p_Fq->state = qman_fq_state_sched;

    FQUNLOCK(p_Fq);
    PUNLOCK(p_QmPortal);
    return E_OK;
}

/* Inline helper to reduce nesting in LoopMessageRing() */
static __inline__ void fq_state_change(struct qman_fq *p_Fq,
                                       struct qm_mr_entry *p_Msg,
                                       uint8_t verb)
{
    FQLOCK(p_Fq);
    switch(verb) {
        case QM_MR_VERB_FQRL:
            ASSERT_COND(p_Fq->flags & QMAN_FQ_STATE_ORL);
            p_Fq->flags &= ~QMAN_FQ_STATE_ORL;
            break;
        case QM_MR_VERB_FQRN:
            ASSERT_COND((p_Fq->state == qman_fq_state_parked) ||
                (p_Fq->state == qman_fq_state_sched));
            ASSERT_COND(p_Fq->flags & QMAN_FQ_STATE_CHANGING);
            p_Fq->flags &= ~QMAN_FQ_STATE_CHANGING;
            if (p_Msg->fq.fqs & QM_MR_FQS_NOTEMPTY)
                p_Fq->flags |= QMAN_FQ_STATE_NE;
            if (p_Msg->fq.fqs & QM_MR_FQS_ORLPRESENT)
                p_Fq->flags |= QMAN_FQ_STATE_ORL;
            p_Fq->state = qman_fq_state_retired;
            break;
        case QM_MR_VERB_FQPN:
            ASSERT_COND(p_Fq->state == qman_fq_state_sched);
            ASSERT_COND(p_Fq->flags & QMAN_FQ_STATE_CHANGING);
            p_Fq->state = qman_fq_state_parked;
    }
    FQUNLOCK(p_Fq);
}

static t_Error freeDrainedFq(struct qman_fq *p_Fq)
{
    t_QmFqr     *p_QmFqr;
    uint32_t    i;

    ASSERT_COND(p_Fq);
    p_QmFqr = (t_QmFqr *)p_Fq->h_QmFqr;
    ASSERT_COND(p_QmFqr);

    ASSERT_COND(!p_QmFqr->p_DrainedFqs[p_Fq->fqidOffset]);
    p_QmFqr->p_DrainedFqs[p_Fq->fqidOffset] = TRUE;
    p_QmFqr->numOfDrainedFqids++;
    if (p_QmFqr->numOfDrainedFqids == p_QmFqr->numOfFqids)
    {
        for (i=0;i<p_QmFqr->numOfFqids;i++)
        {
            if ((p_QmFqr->p_Fqs[i]->state == qman_fq_state_retired) &&
                    (qman_oos_fq(p_QmFqr->h_QmPortal, p_QmFqr->p_Fqs[i]) != E_OK))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("qman_oos_fq() failed!"));
            qman_destroy_fq(p_QmFqr->p_Fqs[i], 0);
            XX_FreeSmart(p_QmFqr->p_Fqs[i]);
        }
        XX_Free(p_QmFqr->p_DrainedFqs);
        p_QmFqr->p_DrainedFqs = NULL;

        if (p_QmFqr->f_CompletionCB)
        {
            p_QmFqr->f_CompletionCB(p_QmFqr->h_App, p_QmFqr);
            XX_Free(p_QmFqr->p_Fqs);
            if (p_QmFqr->fqidBase)
                QmFqidPut(p_QmFqr->h_Qm, p_QmFqr->fqidBase);
            XX_Free(p_QmFqr);
        }
    }

    return E_OK;
}

static t_Error drainRetiredFq(struct qman_fq *p_Fq)
{
    t_QmFqr     *p_QmFqr;

    ASSERT_COND(p_Fq);
    p_QmFqr = (t_QmFqr *)p_Fq->h_QmFqr;
    ASSERT_COND(p_QmFqr);

    if (p_Fq->flags & QMAN_FQ_STATE_NE)
    {
        if (qman_volatile_dequeue(p_QmFqr->h_QmPortal, p_Fq,
                                (QM_VDQCR_PRECEDENCE_VDQCR | QM_VDQCR_NUMFRAMES_TILLEMPTY)) != E_OK)

            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("drain with volatile failed"));
        return E_OK;
    }
    else
        return freeDrainedFq(p_Fq);
}

static e_RxStoreResponse drainCB(t_Handle h_App,
                                 t_Handle h_QmFqr,
                                 t_Handle h_QmPortal,
                                 uint32_t fqidOffset,
                                 t_DpaaFD *p_Frame)
{
    UNUSED(h_App);
    UNUSED(h_QmFqr);
    UNUSED(h_QmPortal);
    UNUSED(fqidOffset);
    UNUSED(p_Frame);

    DBG(TRACE,("got fd for fqid %d", ((t_QmFqr *)h_QmFqr)->fqidBase + fqidOffset));
    return e_RX_STORE_RESPONSE_CONTINUE;
}

static void cb_ern_dcErn(t_Handle                   h_App,
                         t_Handle                   h_QmPortal,
                         struct qman_fq             *p_Fq,
                         const struct qm_mr_entry   *p_Msg)
{
    static int cnt = 0;
    UNUSED(p_Fq);
    UNUSED(p_Msg);
    UNUSED(h_App);
    UNUSED(h_QmPortal);

    XX_Print("cb_ern_dcErn_fqs() unimplemented %d\n", ++cnt);
}

static void cb_fqs(t_Handle                   h_App,
                   t_Handle                   h_QmPortal,
                   struct qman_fq             *p_Fq,
                   const struct qm_mr_entry   *p_Msg)
{
    UNUSED(p_Msg);
    UNUSED(h_App);
    UNUSED(h_QmPortal);

    if (p_Fq->state == qman_fq_state_retired &&
        !(p_Fq->flags & QMAN_FQ_STATE_ORL))
        drainRetiredFq(p_Fq);
}

static void null_cb_mr(t_Handle                   h_App,
                       t_Handle                   h_QmPortal,
                       struct qman_fq             *p_Fq,
                       const struct qm_mr_entry   *p_Msg)
{
    t_QmPortal      *p_QmPortal = (t_QmPortal *)h_QmPortal;

    UNUSED(p_Fq);UNUSED(h_App);

    if ((p_Msg->verb & QM_MR_VERB_DC_ERN) == QM_MR_VERB_DC_ERN)
        XX_Print("Ignoring unowned MR frame on cpu %d, dc-portal 0x%02x.\n",
                 p_QmPortal->p_LowQmPortal->config.cpu,p_Msg->dcern.portal);
    else
        XX_Print("Ignoring unowned MR frame on cpu %d, verb 0x%02x.\n",
                 p_QmPortal->p_LowQmPortal->config.cpu,p_Msg->verb);
}

static uint32_t LoopMessageRing(t_QmPortal *p_QmPortal, uint32_t is)
{
    struct qm_mr_entry          *p_Msg;

    if (is & QM_PIRQ_CSCI) {
        struct qm_mc_result *p_Mcr;
        struct qman_cgrs    tmp;
        uint32_t            mask;
        unsigned int        i, j;

        NCSW_PLOCK(p_QmPortal);
        qm_mc_start(p_QmPortal->p_LowQmPortal);
        qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYCONGESTION);
        while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;

        /* cgrs[0] is the portal mask for its cg's, cgrs[1] is the
           previous state of cg's */
        for (i = 0; i < QM_MAX_NUM_OF_CGS/32; i++)
        {
            /* get curent state */
            tmp.q.__state[i] = p_Mcr->querycongestion.state.__state[i];
            /* keep only cg's that are registered for this portal */
            tmp.q.__state[i] &= p_QmPortal->cgrs[0].q.__state[i];
            /* handle only cg's that changed their state from previous exception */
            tmp.q.__state[i] ^= p_QmPortal->cgrs[1].q.__state[i];
            /* update previous */
            p_QmPortal->cgrs[1].q.__state[i] = p_Mcr->querycongestion.state.__state[i];
        }
        PUNLOCK(p_QmPortal);

        /* if in interrupt */
        /* call the callback routines for any CG with a changed state */
        for (i = 0; i < QM_MAX_NUM_OF_CGS/32; i++)
            for(j=0, mask = 0x80000000; j<32 ; j++, mask>>=1)
            {
                if(tmp.q.__state[i] & mask)
                {
                    t_QmCg *p_QmCg = (t_QmCg *)(p_QmPortal->cgsHandles[i*32 + j]);
                    if(p_QmCg->f_Exception)
                        p_QmCg->f_Exception(p_QmCg->h_App, e_QM_EX_CG_STATE_CHANGE);
                }
            }

    }


    if (is & QM_PIRQ_EQRI) {
        NCSW_PLOCK(p_QmPortal);
        qmPortalEqcrCceUpdate(p_QmPortal->p_LowQmPortal);
        qm_eqcr_set_ithresh(p_QmPortal->p_LowQmPortal, 0);
        PUNLOCK(p_QmPortal);
    }

    if (is & QM_PIRQ_MRI) {
mr_loop:
        qmPortalMrPvbUpdate(p_QmPortal->p_LowQmPortal);
        p_Msg = qm_mr_current(p_QmPortal->p_LowQmPortal);
        if (p_Msg) {
            struct qman_fq  *p_FqFqs  = ptr_from_aligned_int(p_Msg->fq.contextB);
            struct qman_fq  *p_FqErn  = ptr_from_aligned_int(p_Msg->ern.tag);
            uint8_t         verb    =(uint8_t)(p_Msg->verb & QM_MR_VERB_TYPE_MASK);
            t_QmRejectedFrameInfo   rejectedFrameInfo;

            memset(&rejectedFrameInfo, 0, sizeof(t_QmRejectedFrameInfo));
            if (!(verb & QM_MR_VERB_DC_ERN))
            {
                switch(p_Msg->ern.rc)
                {
                    case(QM_MR_RC_CGR_TAILDROP):
                        rejectedFrameInfo.rejectionCode = e_QM_RC_CG_TAILDROP;
                        rejectedFrameInfo.cg.cgId = (uint8_t)p_FqErn->cgr_groupid;
                        break;
                    case(QM_MR_RC_WRED):
                        rejectedFrameInfo.rejectionCode = e_QM_RC_CG_WRED;
                        rejectedFrameInfo.cg.cgId = (uint8_t)p_FqErn->cgr_groupid;
                        break;
                    case(QM_MR_RC_FQ_TAILDROP):
                        rejectedFrameInfo.rejectionCode = e_QM_RC_FQ_TAILDROP;
                        rejectedFrameInfo.cg.cgId = (uint8_t)p_FqErn->cgr_groupid;
                        break;
                    case(QM_MR_RC_ERROR):
                        break;
                    default:
                        REPORT_ERROR(MINOR, E_NOT_SUPPORTED, ("Unknown rejection code"));
                }
                if (!p_FqErn)
                    p_QmPortal->p_NullCB->ern(p_QmPortal->h_App, NULL, p_QmPortal, 0, (t_DpaaFD*)&p_Msg->ern.fd, &rejectedFrameInfo);
                else
                    p_FqErn->cb.ern(p_FqErn->h_App, p_FqErn->h_QmFqr, p_QmPortal, p_FqErn->fqidOffset, (t_DpaaFD*)&p_Msg->ern.fd, &rejectedFrameInfo);
            } else if (verb == QM_MR_VERB_DC_ERN)
            {
                if (!p_FqErn)
                    p_QmPortal->p_NullCB->dc_ern(NULL, p_QmPortal, NULL, p_Msg);
                else
                    p_FqErn->cb.dc_ern(p_FqErn->h_App, p_QmPortal, p_FqErn, p_Msg);
            } else
            {
                if (verb == QM_MR_VERB_FQRNI)
                    ; /* we drop FQRNIs on the floor */
                else if (!p_FqFqs)
                            p_QmPortal->p_NullCB->fqs(NULL, p_QmPortal, NULL, p_Msg);
                else if ((verb == QM_MR_VERB_FQRN) ||
                         (verb == QM_MR_VERB_FQRL) ||
                         (verb == QM_MR_VERB_FQPN))
                {
                    fq_state_change(p_FqFqs, p_Msg, verb);
                    p_FqFqs->cb.fqs(p_FqFqs->h_App, p_QmPortal, p_FqFqs, p_Msg);
                }
            }
            qm_mr_next(p_QmPortal->p_LowQmPortal);
            qmPortalMrCciConsume(p_QmPortal->p_LowQmPortal, 1);

            goto mr_loop;
        }
    }

    return is & (QM_PIRQ_CSCI | QM_PIRQ_EQCI | QM_PIRQ_EQRI | QM_PIRQ_MRI);
}

static void LoopDequeueRing(t_Handle h_QmPortal)
{
    struct qm_dqrr_entry        *p_Dq;
    struct qman_fq              *p_Fq;
    enum qman_cb_dqrr_result    res = qman_cb_dqrr_consume;
    e_RxStoreResponse           tmpRes;
    t_QmPortal                  *p_QmPortal = (t_QmPortal *)h_QmPortal;
    int                         prefetch = !(p_QmPortal->options & QMAN_PORTAL_FLAG_RSTASH);

    while (res != qman_cb_dqrr_pause)
    {
        if (prefetch)
            qmPortalDqrrPvbPrefetch(p_QmPortal->p_LowQmPortal);
        qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
        p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
        if (!p_Dq)
            break;
	p_Fq = ptr_from_aligned_int(p_Dq->contextB);
        if (p_Dq->stat & QM_DQRR_STAT_UNSCHEDULED) {
            /* We only set QMAN_FQ_STATE_NE when retiring, so we only need
             * to check for clearing it when doing volatile dequeues. It's
             * one less thing to check in the critical path (SDQCR). */
            tmpRes = p_Fq->cb.dqrr(p_Fq->h_App, p_Fq->h_QmFqr, p_QmPortal, p_Fq->fqidOffset, (t_DpaaFD*)&p_Dq->fd);
            if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                res = qman_cb_dqrr_pause;
            /* Check for VDQCR completion */
            if (p_Dq->stat & QM_DQRR_STAT_DQCR_EXPIRED)
                p_Fq->flags &= ~QMAN_FQ_STATE_VDQCR;
            if (p_Dq->stat & QM_DQRR_STAT_FQ_EMPTY)
            {
                p_Fq->flags &= ~QMAN_FQ_STATE_NE;
                freeDrainedFq(p_Fq);
            }
        }
        else
        {
            /* Interpret 'dq' from the owner's perspective. */
            /* use portal default handlers */
            ASSERT_COND(p_Dq->fqid);
            if (p_Fq)
            {
                tmpRes = p_Fq->cb.dqrr(p_Fq->h_App,
                                       p_Fq->h_QmFqr,
                                       p_QmPortal,
                                       p_Fq->fqidOffset,
                                       (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
                else if (p_Fq->state == qman_fq_state_waiting_parked)
                    res = qman_cb_dqrr_park;
            }
            else
            {
                tmpRes = p_QmPortal->p_NullCB->dqrr(p_QmPortal->h_App,
                                                    NULL,
                                                    p_QmPortal,
                                                    p_Dq->fqid,
                                                    (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
            }
        }

        /* Parking isn't possible unless HELDACTIVE was set. NB,
         * FORCEELIGIBLE implies HELDACTIVE, so we only need to
         * check for HELDACTIVE to cover both. */
        ASSERT_COND((p_Dq->stat & QM_DQRR_STAT_FQ_HELDACTIVE) ||
                    (res != qman_cb_dqrr_park));
        if (p_QmPortal->options & QMAN_PORTAL_FLAG_DCA) {
            /* Defer just means "skip it, I'll consume it myself later on" */
            if (res != qman_cb_dqrr_defer)
                qmPortalDqrrDcaConsume1ptr(p_QmPortal->p_LowQmPortal,
                                           p_Dq,
                                           (res == qman_cb_dqrr_park));
            qm_dqrr_next(p_QmPortal->p_LowQmPortal);
        } else {
            if (res == qman_cb_dqrr_park)
                /* The only thing to do for non-DCA is the park-request */
                qm_dqrr_park_ci(p_QmPortal->p_LowQmPortal);
            qm_dqrr_next(p_QmPortal->p_LowQmPortal);
            qmPortalDqrrCciConsume(p_QmPortal->p_LowQmPortal, 1);
        }
    }
}

static void LoopDequeueRingDcaOptimized(t_Handle h_QmPortal)
{
    struct qm_dqrr_entry        *p_Dq;
    struct qman_fq              *p_Fq;
    enum qman_cb_dqrr_result    res = qman_cb_dqrr_consume;
    e_RxStoreResponse           tmpRes;
    t_QmPortal                  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    while (res != qman_cb_dqrr_pause)
    {
        qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
        p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
        if (!p_Dq)
            break;
	p_Fq = ptr_from_aligned_int(p_Dq->contextB);
        if (p_Dq->stat & QM_DQRR_STAT_UNSCHEDULED) {
            /* We only set QMAN_FQ_STATE_NE when retiring, so we only need
             * to check for clearing it when doing volatile dequeues. It's
             * one less thing to check in the critical path (SDQCR). */
            tmpRes = p_Fq->cb.dqrr(p_Fq->h_App, p_Fq->h_QmFqr, p_QmPortal, p_Fq->fqidOffset, (t_DpaaFD*)&p_Dq->fd);
            if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                res = qman_cb_dqrr_pause;
            /* Check for VDQCR completion */
            if (p_Dq->stat & QM_DQRR_STAT_DQCR_EXPIRED)
                p_Fq->flags &= ~QMAN_FQ_STATE_VDQCR;
            if (p_Dq->stat & QM_DQRR_STAT_FQ_EMPTY)
            {
                p_Fq->flags &= ~QMAN_FQ_STATE_NE;
                freeDrainedFq(p_Fq);
            }
        }
        else
        {
            /* Interpret 'dq' from the owner's perspective. */
            /* use portal default handlers */
            ASSERT_COND(p_Dq->fqid);
            if (p_Fq)
            {
                tmpRes = p_Fq->cb.dqrr(p_Fq->h_App,
                                       p_Fq->h_QmFqr,
                                       p_QmPortal,
                                       p_Fq->fqidOffset,
                                       (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
                else if (p_Fq->state == qman_fq_state_waiting_parked)
                    res = qman_cb_dqrr_park;
            }
            else
            {
                tmpRes = p_QmPortal->p_NullCB->dqrr(p_QmPortal->h_App,
                                                    NULL,
                                                    p_QmPortal,
                                                    p_Dq->fqid,
                                                    (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
            }
        }

        /* Parking isn't possible unless HELDACTIVE was set. NB,
         * FORCEELIGIBLE implies HELDACTIVE, so we only need to
         * check for HELDACTIVE to cover both. */
        ASSERT_COND((p_Dq->stat & QM_DQRR_STAT_FQ_HELDACTIVE) ||
                (res != qman_cb_dqrr_park));
        /* Defer just means "skip it, I'll consume it myself later on" */
        if (res != qman_cb_dqrr_defer)
            qmPortalDqrrDcaConsume1ptr(p_QmPortal->p_LowQmPortal,
                                       p_Dq,
                                       (res == qman_cb_dqrr_park));
        qm_dqrr_next(p_QmPortal->p_LowQmPortal);
    }
}

static void LoopDequeueRingOptimized(t_Handle h_QmPortal)
{
    struct qm_dqrr_entry        *p_Dq;
    struct qman_fq              *p_Fq;
    enum qman_cb_dqrr_result    res = qman_cb_dqrr_consume;
    e_RxStoreResponse           tmpRes;
    t_QmPortal                  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    while (res != qman_cb_dqrr_pause)
    {
        qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
        p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
        if (!p_Dq)
            break;
	p_Fq = ptr_from_aligned_int(p_Dq->contextB);
        if (p_Dq->stat & QM_DQRR_STAT_UNSCHEDULED) {
            /* We only set QMAN_FQ_STATE_NE when retiring, so we only need
             * to check for clearing it when doing volatile dequeues. It's
             * one less thing to check in the critical path (SDQCR). */
            tmpRes = p_Fq->cb.dqrr(p_Fq->h_App, p_Fq->h_QmFqr, p_QmPortal, p_Fq->fqidOffset, (t_DpaaFD*)&p_Dq->fd);
            if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                res = qman_cb_dqrr_pause;
            /* Check for VDQCR completion */
            if (p_Dq->stat & QM_DQRR_STAT_DQCR_EXPIRED)
                p_Fq->flags &= ~QMAN_FQ_STATE_VDQCR;
            if (p_Dq->stat & QM_DQRR_STAT_FQ_EMPTY)
            {
                p_Fq->flags &= ~QMAN_FQ_STATE_NE;
                freeDrainedFq(p_Fq);
            }
        }
        else
        {
            /* Interpret 'dq' from the owner's perspective. */
            /* use portal default handlers */
            ASSERT_COND(p_Dq->fqid);
            if (p_Fq)
            {
                tmpRes = p_Fq->cb.dqrr(p_Fq->h_App,
                                       p_Fq->h_QmFqr,
                                       p_QmPortal,
                                       p_Fq->fqidOffset,
                                       (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
                else if (p_Fq->state == qman_fq_state_waiting_parked)
                    res = qman_cb_dqrr_park;
            }
            else
            {
                tmpRes = p_QmPortal->p_NullCB->dqrr(p_QmPortal->h_App,
                                                    NULL,
                                                    p_QmPortal,
                                                    p_Dq->fqid,
                                                    (t_DpaaFD*)&p_Dq->fd);
                if (tmpRes == e_RX_STORE_RESPONSE_PAUSE)
                    res = qman_cb_dqrr_pause;
            }
        }

        /* Parking isn't possible unless HELDACTIVE was set. NB,
         * FORCEELIGIBLE implies HELDACTIVE, so we only need to
         * check for HELDACTIVE to cover both. */
        ASSERT_COND((p_Dq->stat & QM_DQRR_STAT_FQ_HELDACTIVE) ||
                (res != qman_cb_dqrr_park));
        if (res == qman_cb_dqrr_park)
            /* The only thing to do for non-DCA is the park-request */
            qm_dqrr_park_ci(p_QmPortal->p_LowQmPortal);
        qm_dqrr_next(p_QmPortal->p_LowQmPortal);
        qmPortalDqrrCciConsume(p_QmPortal->p_LowQmPortal, 1);
    }
}

/* Portal interrupt handler */
static void portal_isr(void *ptr)
{
    t_QmPortal  *p_QmPortal = ptr;
    uint32_t    event = 0;
    uint32_t    enableEvents = qm_isr_enable_read(p_QmPortal->p_LowQmPortal);

    DBG(TRACE, ("software-portal %d got interrupt", p_QmPortal->p_LowQmPortal->config.cpu));

    event |= (qm_isr_status_read(p_QmPortal->p_LowQmPortal) &
            enableEvents);

    qm_isr_status_clear(p_QmPortal->p_LowQmPortal, event);
    /* Only do fast-path handling if it's required */
    if (/*(event & QM_PIRQ_DQRI) &&*/
        (p_QmPortal->options & QMAN_PORTAL_FLAG_IRQ_FAST))
        p_QmPortal->f_LoopDequeueRingCB(p_QmPortal);
    if (p_QmPortal->options & QMAN_PORTAL_FLAG_IRQ_SLOW)
        LoopMessageRing(p_QmPortal, event);
}


static t_Error qman_query_fq_np(t_QmPortal *p_QmPortal, struct qman_fq *p_Fq, struct qm_mcr_queryfq_np *p_Np)
{
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    uint8_t                 res;

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->queryfq_np.fqid = p_Fq->fqid;
    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYFQ_NP);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCR_VERB_QUERYFQ_NP);
    res = p_Mcr->result;
    if (res == QM_MCR_RESULT_OK)
        *p_Np = p_Mcr->queryfq_np;
    PUNLOCK(p_QmPortal);
    if (res != QM_MCR_RESULT_OK)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("QUERYFQ_NP failed: %s\n", mcr_result_str(res)));
    return E_OK;
}

static uint8_t QmCgGetCgId(t_Handle h_QmCg)
{
   t_QmCg *p_QmCg = (t_QmCg *)h_QmCg;

   return p_QmCg->id;

}

static t_Error qm_new_fq(t_QmPortal                         *p_QmPortal,
                         uint32_t                           fqid,
                         uint32_t                           fqidOffset,
                         uint32_t                           channel,
                         uint32_t                           wqid,
                         uint16_t                           count,
                         uint32_t                           flags,
                         t_QmFqrCongestionAvoidanceParams   *p_CgParams,
                         t_QmContextA                       *p_ContextA,
                         t_QmContextB                       *p_ContextB,
                         bool                               initParked,
                         t_Handle                           h_QmFqr,
                         struct qman_fq                     **p_Fqs)
{
    struct qman_fq          *p_Fq = NULL;
    struct qm_mcc_initfq    fq_opts;
    uint32_t                i;
    t_Error                 err = E_OK;
    int         gap, tmp;
    uint32_t    tmpA, tmpN, ta=0, tn=0, initFqFlag;

    ASSERT_COND(p_QmPortal);
    ASSERT_COND(count);

    for(i=0;i<count;i++)
    {
        p_Fq = (struct qman_fq *)XX_MallocSmart(sizeof(struct qman_fq), 0, 64);
        if (!p_Fq)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FQ obj!!!"));
        memset(p_Fq, 0, sizeof(struct qman_fq));
        p_Fq->cb.dqrr     = p_QmPortal->f_DfltFrame;
        p_Fq->cb.ern      = p_QmPortal->f_RejectedFrame;
        p_Fq->cb.dc_ern   = cb_ern_dcErn;
        p_Fq->cb.fqs      = cb_fqs;
        p_Fq->h_App       = p_QmPortal->h_App;
        p_Fq->h_QmFqr     = h_QmFqr;
        p_Fq->fqidOffset  = fqidOffset;
        p_Fqs[i] = p_Fq;
        if ((err = qman_create_fq(p_QmPortal,(uint32_t)(fqid + i), 0, p_Fqs[i])) != E_OK)
            break;
    }

    if (err != E_OK)
    {
        for(i=0;i<count;i++)
            if (p_Fqs[i])
            {
                XX_FreeSmart(p_Fqs[i]);
                p_Fqs[i] = NULL;
            }
        RETURN_ERROR(MINOR, err, ("Failed to create Fqs"));
    }

    memset(&fq_opts,0,sizeof(fq_opts));
    fq_opts.fqid = fqid;
    fq_opts.count = (uint16_t)(count-1);
    fq_opts.we_mask |= QM_INITFQ_WE_DESTWQ;
    fq_opts.fqd.dest.channel = channel;
    fq_opts.fqd.dest.wq = wqid;
    fq_opts.we_mask |= QM_INITFQ_WE_FQCTRL;
    fq_opts.fqd.fq_ctrl = (uint16_t)flags;

    if ((flags & QM_FQCTRL_CGE) || (flags & QM_FQCTRL_TDE))
        ASSERT_COND(p_CgParams);

    if(flags & QM_FQCTRL_CGE)
    {
        ASSERT_COND(p_CgParams->h_QmCg);

        /* CG OAC and FQ TD may not be configured at the same time. if both are required,
           than we configure CG first, and the FQ TD later - see below. */
        fq_opts.fqd.cgid = QmCgGetCgId(p_CgParams->h_QmCg);
        fq_opts.we_mask |= QM_INITFQ_WE_CGID;
        if(p_CgParams->overheadAccountingLength)
        {
            fq_opts.we_mask |= QM_INITFQ_WE_OAC;
            fq_opts.we_mask &= ~QM_INITFQ_WE_TDTHRESH;
            fq_opts.fqd.td_thresh = (uint16_t)(QM_FQD_TD_THRESH_OAC_EN | p_CgParams->overheadAccountingLength);
        }
    }
    if((flags & QM_FQCTRL_TDE) && (!p_CgParams->overheadAccountingLength))
    {
        ASSERT_COND(p_CgParams->fqTailDropThreshold);

        fq_opts.we_mask |= QM_INITFQ_WE_TDTHRESH;

            /* express thresh as ta*2^tn */
            gap = (int)p_CgParams->fqTailDropThreshold;
            for (tmpA=0 ; tmpA<256; tmpA++ )
                for (tmpN=0 ; tmpN<32; tmpN++ )
                {
                    tmp = ABS((int)(p_CgParams->fqTailDropThreshold - tmpA*(1<<tmpN)));
                    if (tmp < gap)
                    {
                       ta = tmpA;
                       tn = tmpN;
                       gap = tmp;
                    }
                }
            fq_opts.fqd.td.exp = tn;
            fq_opts.fqd.td.mant = ta;
    }

    if (p_ContextA)
    {
        fq_opts.we_mask |= QM_INITFQ_WE_CONTEXTA;
        memcpy((void*)&fq_opts.fqd.context_a, p_ContextA, sizeof(t_QmContextA));
    }
    /* If this FQ will not be used for tx, we can use contextB field */
    if (fq_opts.fqd.dest.channel < e_QM_FQ_CHANNEL_FMAN0_SP0)
    {
            fq_opts.we_mask |= QM_INITFQ_WE_CONTEXTB;
            fq_opts.fqd.context_b = aligned_int_from_ptr(p_Fqs[0]);
    }
    else if (p_ContextB) /* Tx-Queue */
    {
        fq_opts.we_mask |= QM_INITFQ_WE_CONTEXTB;
        memcpy((void*)&fq_opts.fqd.context_b, p_ContextB, sizeof(t_QmContextB));
    }

    if((flags & QM_FQCTRL_TDE) && (p_CgParams->overheadAccountingLength))
        initFqFlag = 0;
    else
        initFqFlag = (uint32_t)(initParked?0:QMAN_INITFQ_FLAG_SCHED);

    if ((err = qman_init_fq(p_QmPortal, p_Fqs[0], initFqFlag, &fq_opts)) != E_OK)
    {
        for(i=0;i<count;i++)
            if (p_Fqs[i])
            {
                XX_FreeSmart(p_Fqs[i]);
                p_Fqs[i] = NULL;
            }
        RETURN_ERROR(MINOR, err, ("Failed to init Fqs [%d-%d]", fqid, fqid+count-1));
    }

    /* if both CG OAC and FQ TD are needed, we call qman_init_fq again, this time for the FQ TD only */
    if((flags & QM_FQCTRL_TDE) && (p_CgParams->overheadAccountingLength))
    {
        ASSERT_COND(p_CgParams->fqTailDropThreshold);

        fq_opts.we_mask = QM_INITFQ_WE_TDTHRESH;

        /* express thresh as ta*2^tn */
        gap = (int)p_CgParams->fqTailDropThreshold;
        for (tmpA=0 ; tmpA<256; tmpA++ )
            for (tmpN=0 ; tmpN<32; tmpN++ )
            {
                tmp = ABS((int)(p_CgParams->fqTailDropThreshold - tmpA*(1<<tmpN)));
                if (tmp < gap)
                {
                   ta = tmpA;
                   tn = tmpN;
                   gap = tmp;
                }
            }
        fq_opts.fqd.td.exp = tn;
        fq_opts.fqd.td.mant = ta;
        if ((err = qman_init_fq(p_QmPortal, p_Fqs[0], (uint32_t)(initParked?0:QMAN_INITFQ_FLAG_SCHED), &fq_opts)) != E_OK)
        {
            for(i=0;i<count;i++)
                if (p_Fqs[i])
                {
                    XX_FreeSmart(p_Fqs[i]);
                    p_Fqs[i] = NULL;
                }
            RETURN_ERROR(MINOR, err, ("Failed to init Fqs"));
        }
    }


    for(i=1;i<count;i++)
    {
        memcpy(p_Fqs[i], p_Fqs[0], sizeof(struct qman_fq));
        p_Fqs[i]->fqid += i;
    }

    return err;
}


static t_Error qm_free_fq(t_QmPortal *p_QmPortal, struct qman_fq *p_Fq)
{
    uint32_t flags=0;

    if (qman_retire_fq(p_QmPortal, p_Fq, &flags, false) != E_OK)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("qman_retire_fq() failed!"));

    if (flags & QMAN_FQ_STATE_CHANGING)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("fq %d currently in use, will be retired", p_Fq->fqid));

    if (flags & QMAN_FQ_STATE_NE)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("qman_retire_fq() failed;" \
                                          "Frame Queue Not Empty, Need to dequeue"));

    if (qman_oos_fq(p_QmPortal, p_Fq) != E_OK)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("qman_oos_fq() failed!"));

    qman_destroy_fq(p_Fq,0);

    return E_OK;
}

static void qman_disable_portal(t_QmPortal *p_QmPortal)
{
    NCSW_PLOCK(p_QmPortal);
    if (!(p_QmPortal->disable_count++))
        qm_dqrr_set_maxfill(p_QmPortal->p_LowQmPortal, 0);
    PUNLOCK(p_QmPortal);
}


/* quiesce SDQCR/VDQCR, then drain till h/w wraps up anything it
 * was doing (5ms is more than enough to ensure it's done). */
static void clean_dqrr_mr(t_QmPortal *p_QmPortal)
{
    struct qm_dqrr_entry    *p_Dq;
    struct qm_mr_entry      *p_Msg;
    int                     idle = 0;

    qm_dqrr_sdqcr_set(p_QmPortal->p_LowQmPortal, 0);
    qm_dqrr_vdqcr_set(p_QmPortal->p_LowQmPortal, 0);
drain_loop:
    qmPortalDqrrPvbPrefetch(p_QmPortal->p_LowQmPortal);
    qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
    qmPortalMrPvbUpdate(p_QmPortal->p_LowQmPortal);
    p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
    p_Msg = qm_mr_current(p_QmPortal->p_LowQmPortal);
    if (p_Dq) {
        qm_dqrr_next(p_QmPortal->p_LowQmPortal);
        qmPortalDqrrCciConsume(p_QmPortal->p_LowQmPortal, 1);
    }
    if (p_Msg) {
    qm_mr_next(p_QmPortal->p_LowQmPortal);
        qmPortalMrCciConsume(p_QmPortal->p_LowQmPortal, 1);
    }
    if (!p_Dq && !p_Msg) {
    if (++idle < 5) {
    XX_UDelay(1000);
    goto drain_loop;
    }
    } else {
    idle = 0;
    goto drain_loop;
    }
}

static t_Error qman_create_portal(t_QmPortal *p_QmPortal,
                                   uint32_t flags,
                                   uint32_t sdqcrFlags,
                                   uint8_t  dqrrSize)
{
    const struct qm_portal_config   *p_Config = &(p_QmPortal->p_LowQmPortal->config);
    int                             ret = 0;
    t_Error                         err;
    uint32_t                        isdr;

    if ((err = qm_eqcr_init(p_QmPortal->p_LowQmPortal, e_QmPortalPVB, e_QmPortalEqcrCCE)) != E_OK)
        RETURN_ERROR(MINOR, err, ("Qman EQCR initialization failed\n"));

    if (qm_dqrr_init(p_QmPortal->p_LowQmPortal,
                     sdqcrFlags ? e_QmPortalDequeuePushMode : e_QmPortalDequeuePullMode,
                     e_QmPortalPVB,
                     (flags & QMAN_PORTAL_FLAG_DCA) ? e_QmPortalDqrrDCA : e_QmPortalDqrrCCI,
                     dqrrSize,
                     (flags & QMAN_PORTAL_FLAG_RSTASH) ? 1 : 0,
                     (flags & QMAN_PORTAL_FLAG_DSTASH) ? 1 : 0)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("DQRR initialization failed"));
        goto fail_dqrr;
    }

    if (qm_mr_init(p_QmPortal->p_LowQmPortal, e_QmPortalPVB, e_QmPortalMrCCI)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("MR initialization failed"));
        goto fail_mr;
    }
    if (qm_mc_init(p_QmPortal->p_LowQmPortal)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("MC initialization failed"));
        goto fail_mc;
    }
    if (qm_isr_init(p_QmPortal->p_LowQmPortal)) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("ISR initialization failed"));
        goto fail_isr;
    }
    /* static interrupt-gating controls */
    qm_dqrr_set_ithresh(p_QmPortal->p_LowQmPortal, 12);
    qm_mr_set_ithresh(p_QmPortal->p_LowQmPortal, 4);
    qm_isr_set_iperiod(p_QmPortal->p_LowQmPortal, 100);
    p_QmPortal->options = flags;
    isdr = 0xffffffff;
    qm_isr_status_clear(p_QmPortal->p_LowQmPortal, 0xffffffff);
    qm_isr_enable_write(p_QmPortal->p_LowQmPortal, DEFAULT_portalExceptions);
    qm_isr_disable_write(p_QmPortal->p_LowQmPortal, isdr);
    if (flags & QMAN_PORTAL_FLAG_IRQ)
    {
        XX_SetIntr(p_Config->irq, portal_isr, p_QmPortal);
        XX_EnableIntr(p_Config->irq);
        qm_isr_uninhibit(p_QmPortal->p_LowQmPortal);
    } else
        /* without IRQ, we can't block */
        flags &= ~QMAN_PORTAL_FLAG_WAIT;
    /* Need EQCR to be empty before continuing */
    isdr ^= QM_PIRQ_EQCI;
    qm_isr_disable_write(p_QmPortal->p_LowQmPortal, isdr);
    ret = qm_eqcr_get_fill(p_QmPortal->p_LowQmPortal);
    if (ret) {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("EQCR unclean"));
        goto fail_eqcr_empty;
    }
    isdr ^= (QM_PIRQ_DQRI | QM_PIRQ_MRI);
    qm_isr_disable_write(p_QmPortal->p_LowQmPortal, isdr);
    if (qm_dqrr_current(p_QmPortal->p_LowQmPortal) != NULL)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("DQRR unclean"));
goto fail_dqrr_mr_empty;
    }
    if (qm_mr_current(p_QmPortal->p_LowQmPortal) != NULL)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("MR unclean"));
goto fail_dqrr_mr_empty;
    }
    qm_isr_disable_write(p_QmPortal->p_LowQmPortal, 0);
    qm_dqrr_sdqcr_set(p_QmPortal->p_LowQmPortal, sdqcrFlags);
    return E_OK;
fail_dqrr_mr_empty:
fail_eqcr_empty:
    qm_isr_finish(p_QmPortal->p_LowQmPortal);
fail_isr:
    qm_mc_finish(p_QmPortal->p_LowQmPortal);
fail_mc:
    qm_mr_finish(p_QmPortal->p_LowQmPortal);
fail_mr:
    qm_dqrr_finish(p_QmPortal->p_LowQmPortal);
fail_dqrr:
    qm_eqcr_finish(p_QmPortal->p_LowQmPortal);
    return ERROR_CODE(E_INVALID_STATE);
}

static void qman_destroy_portal(t_QmPortal *p_QmPortal)
{
    /* NB we do this to "quiesce" EQCR. If we add enqueue-completions or
     * something related to QM_PIRQ_EQCI, this may need fixing. */
    qmPortalEqcrCceUpdate(p_QmPortal->p_LowQmPortal);
    if (p_QmPortal->options & QMAN_PORTAL_FLAG_IRQ)
    {
        XX_DisableIntr(p_QmPortal->p_LowQmPortal->config.irq);
        XX_FreeIntr(p_QmPortal->p_LowQmPortal->config.irq);
    }
    qm_isr_finish(p_QmPortal->p_LowQmPortal);
    qm_mc_finish(p_QmPortal->p_LowQmPortal);
    qm_mr_finish(p_QmPortal->p_LowQmPortal);
    qm_dqrr_finish(p_QmPortal->p_LowQmPortal);
    qm_eqcr_finish(p_QmPortal->p_LowQmPortal);
}

static inline struct qm_eqcr_entry *try_eq_start(t_QmPortal *p_QmPortal)
{
    struct qm_eqcr_entry    *p_Eq;
    uint8_t                 avail;

    avail = qm_eqcr_get_avail(p_QmPortal->p_LowQmPortal);
    if (avail == EQCR_THRESH)
        qmPortalEqcrCcePrefetch(p_QmPortal->p_LowQmPortal);
    else if (avail < EQCR_THRESH)
            qmPortalEqcrCceUpdate(p_QmPortal->p_LowQmPortal);
    p_Eq = qm_eqcr_start(p_QmPortal->p_LowQmPortal);

    return p_Eq;
}


static t_Error qman_orp_update(t_QmPortal   *p_QmPortal,
                               uint32_t     orpId,
                               uint16_t     orpSeqnum,
                               uint32_t     flags)
{
    struct qm_eqcr_entry *p_Eq;

    NCSW_PLOCK(p_QmPortal);
    p_Eq = try_eq_start(p_QmPortal);
    if (!p_Eq)
    {
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_BUSY);
    }

    if (flags & QMAN_ENQUEUE_FLAG_NESN)
        orpSeqnum |= QM_EQCR_SEQNUM_NESN;
    else
        /* No need to check 4 QMAN_ENQUEUE_FLAG_HOLE */
        orpSeqnum &= ~QM_EQCR_SEQNUM_NESN;
    p_Eq->seqnum  = orpSeqnum;
    p_Eq->orp     = orpId;
qmPortalEqcrPvbCommit(p_QmPortal->p_LowQmPortal, (uint8_t)QM_EQCR_VERB_ORP);

    PUNLOCK(p_QmPortal);
    return E_OK;
}

static __inline__ t_Error CheckStashParams(t_QmFqrParams *p_QmFqrParams)
{
    ASSERT_COND(p_QmFqrParams);

    if (p_QmFqrParams->stashingParams.frameAnnotationSize > QM_CONTEXTA_MAX_STASH_SIZE)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Frame Annotation Size Exceeded Max Stash Size(%d)", QM_CONTEXTA_MAX_STASH_SIZE));
    if (p_QmFqrParams->stashingParams.frameDataSize > QM_CONTEXTA_MAX_STASH_SIZE)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Frame Data Size Exceeded Max Stash Size(%d)", QM_CONTEXTA_MAX_STASH_SIZE));
    if (p_QmFqrParams->stashingParams.fqContextSize > QM_CONTEXTA_MAX_STASH_SIZE)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Frame Context Size Exceeded Max Stash Size(%d)", QM_CONTEXTA_MAX_STASH_SIZE));
    if (p_QmFqrParams->stashingParams.fqContextSize)
    {
        if (!p_QmFqrParams->stashingParams.fqContextAddr)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("FQ Context Address Must be givven"));
        if (!IS_ALIGNED(p_QmFqrParams->stashingParams.fqContextAddr, CACHELINE_SIZE))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("FQ Context Address Must be aligned to %d", CACHELINE_SIZE));
        if (p_QmFqrParams->stashingParams.fqContextAddr & 0xffffff0000000000LL)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("FQ Context Address May be up to 40 bit"));
    }

    return E_OK;
}

static t_Error QmPortalRegisterCg(t_Handle h_QmPortal, t_Handle h_QmCg, uint8_t  cgId)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    /* cgrs[0] is the mask of registered CG's*/
    if(p_QmPortal->cgrs[0].q.__state[cgId/32] & (0x80000000 >> (cgId % 32)))
        RETURN_ERROR(MINOR, E_BUSY, ("CG already used"));

    p_QmPortal->cgrs[0].q.__state[cgId/32] |=  0x80000000 >> (cgId % 32);
    p_QmPortal->cgsHandles[cgId] = h_QmCg;

    return E_OK;
}

static t_Error QmPortalUnregisterCg(t_Handle h_QmPortal, uint8_t  cgId)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    /* cgrs[0] is the mask of registered CG's*/
    if(!(p_QmPortal->cgrs[0].q.__state[cgId/32] & (0x80000000 >> (cgId % 32))))
        RETURN_ERROR(MINOR, E_BUSY, ("CG is not in use"));

    p_QmPortal->cgrs[0].q.__state[cgId/32] &=  ~0x80000000 >> (cgId % 32);
    p_QmPortal->cgsHandles[cgId] = NULL;

    return E_OK;
}

static e_DpaaSwPortal QmPortalGetSwPortalId(t_Handle h_QmPortal)
{
    t_QmPortal *p_QmPortal = (t_QmPortal *)h_QmPortal;

    return (e_DpaaSwPortal)p_QmPortal->p_LowQmPortal->config.cpu;
}

static t_Error CalcWredCurve(t_QmCgWredCurve *p_WredCurve, uint32_t  *p_CurveWord)
{
    uint32_t    maxP, roundDown, roundUp, tmpA, tmpN;
    uint32_t    ma=0, mn=0, slope, sa=0, sn=0, pn;
    int         pres = 1000;
    int         gap, tmp;

/*  TODO - change maxTh to uint64_t?
   if(p_WredCurve->maxTh > (1<<39))
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("maxTh is not in range"));*/

    /* express maxTh as ma*2^mn */
     gap = (int)p_WredCurve->maxTh;
     for (tmpA=0 ; tmpA<256; tmpA++ )
         for (tmpN=0 ; tmpN<32; tmpN++ )
         {
             tmp = ABS((int)(p_WredCurve->maxTh - tmpA*(1<<tmpN)));
             if (tmp < gap)
             {
                ma = tmpA;
                mn = tmpN;
                gap = tmp;
             }
         }
     ASSERT_COND(ma <256);
     ASSERT_COND(mn <32);
     p_WredCurve->maxTh = ma*(1<<mn);

     if(p_WredCurve->maxTh <= p_WredCurve->minTh)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("maxTh must be larger than minTh"));
     if(p_WredCurve->probabilityDenominator > 64)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("probabilityDenominator mustn't be 1-64"));

    /* first we translate from Cisco probabilityDenominator
       to 256 fixed denominator, result must be divisible by 4. */
    /* we multiply by a fixed value to get better accuracy (without
       using floating point) */
    maxP = (uint32_t)(256*1000/p_WredCurve->probabilityDenominator);
    if (maxP % 4*pres)
    {
        roundDown  = maxP + (maxP % (4*pres));
        roundUp = roundDown + 4*pres;
        if((roundUp - maxP) > (maxP - roundDown))
            maxP = roundDown;
        else
            maxP = roundUp;
    }
    maxP = maxP/pres;
    ASSERT_COND(maxP <= 256);
    pn = (uint8_t)(maxP/4 - 1);

    if(maxP >= (p_WredCurve->maxTh - p_WredCurve->minTh))
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("Due to probabilityDenominator selected, maxTh-minTh must be larger than %d", maxP));

    pres = 1000000;
    slope = maxP*pres/(p_WredCurve->maxTh - p_WredCurve->minTh);
    /* express slope as sa/2^sn */
    gap = (int)slope;
    for (tmpA=(uint32_t)(64*pres) ; tmpA<128*pres; tmpA += pres )
        for (tmpN=7 ; tmpN<64; tmpN++ )
        {
            tmp = ABS((int)(slope - tmpA/(1<<tmpN)));
            if (tmp < gap)
            {
               sa = tmpA;
               sn = tmpN;
               gap = tmp;
            }
        }
    sa = sa/pres;
    ASSERT_COND(sa<128 && sa>=64);
    sn = sn;
    ASSERT_COND(sn<64 && sn>=7);

    *p_CurveWord = ((ma << 24) |
                    (mn << 19) |
                    (sa << 12) |
                    (sn << 6) |
                    pn);

    return E_OK;
}

static t_Error QmPortalPullFrame(t_Handle h_QmPortal, uint32_t pdqcr, t_DpaaFD *p_Frame)
{
    t_QmPortal              *p_QmPortal = (t_QmPortal *)h_QmPortal;
    struct qm_dqrr_entry    *p_Dq;
    struct qman_fq          *p_Fq;
    int                     prefetch;
    uint32_t                *p_Dst, *p_Src;

    ASSERT_COND(p_QmPortal);
    ASSERT_COND(p_Frame);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal->pullMode, E_INVALID_STATE);

    NCSW_PLOCK(p_QmPortal);

    qm_dqrr_pdqcr_set(p_QmPortal->p_LowQmPortal, pdqcr);
    mb();
    while (qm_dqrr_pdqcr_get(p_QmPortal->p_LowQmPortal)) ;

    prefetch = !(p_QmPortal->options & QMAN_PORTAL_FLAG_RSTASH);
    while(TRUE)
    {
        if (prefetch)
            qmPortalDqrrPvbPrefetch(p_QmPortal->p_LowQmPortal);
        qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
        p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
        if (!p_Dq)
            continue;
        p_Fq = ptr_from_aligned_int(p_Dq->contextB);
        ASSERT_COND(p_Dq->fqid);
        p_Dst = (uint32_t *)p_Frame;
        p_Src = (uint32_t *)&p_Dq->fd;
        p_Dst[0] = p_Src[0];
        p_Dst[1] = p_Src[1];
        p_Dst[2] = p_Src[2];
        p_Dst[3] = p_Src[3];
        if (p_QmPortal->options & QMAN_PORTAL_FLAG_DCA)
        {
            qmPortalDqrrDcaConsume1ptr(p_QmPortal->p_LowQmPortal,
                                       p_Dq,
                                       false);
            qm_dqrr_next(p_QmPortal->p_LowQmPortal);
        }
        else
        {
            qm_dqrr_next(p_QmPortal->p_LowQmPortal);
            qmPortalDqrrCciConsume(p_QmPortal->p_LowQmPortal, 1);
        }
        break;
    }

    PUNLOCK(p_QmPortal);

    if (!(p_Dq->stat & QM_DQRR_STAT_FD_VALID))
        return ERROR_CODE(E_EMPTY);

    return E_OK;
}


/****************************************/
/*       API Init unit functions        */
/****************************************/
t_Handle QM_PORTAL_Config(t_QmPortalParam *p_QmPortalParam)
{
    t_QmPortal          *p_QmPortal;
    uint32_t            i;

    SANITY_CHECK_RETURN_VALUE(p_QmPortalParam, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_QmPortalParam->swPortalId < DPAA_MAX_NUM_OF_SW_PORTALS, E_INVALID_VALUE, 0);

    p_QmPortal = (t_QmPortal *)XX_Malloc(sizeof(t_QmPortal));
    if (!p_QmPortal)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Qm Portal obj!!!"));
        return NULL;
    }
    memset(p_QmPortal, 0, sizeof(t_QmPortal));

    p_QmPortal->p_LowQmPortal = (struct qm_portal *)XX_Malloc(sizeof(struct qm_portal));
    if (!p_QmPortal->p_LowQmPortal)
    {
        XX_Free(p_QmPortal);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Low qm p_QmPortal obj!!!"));
        return NULL;
    }
    memset(p_QmPortal->p_LowQmPortal, 0, sizeof(struct qm_portal));

    p_QmPortal->p_QmPortalDriverParams = (t_QmPortalDriverParams *)XX_Malloc(sizeof(t_QmPortalDriverParams));
    if (!p_QmPortal->p_QmPortalDriverParams)
    {
        XX_Free(p_QmPortal->p_LowQmPortal);
        XX_Free(p_QmPortal);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Qm Portal driver parameters"));
        return NULL;
    }
    memset(p_QmPortal->p_QmPortalDriverParams, 0, sizeof(t_QmPortalDriverParams));

    p_QmPortal->p_LowQmPortal->addr.addr_ce = UINT_TO_PTR(p_QmPortalParam->ceBaseAddress);
    p_QmPortal->p_LowQmPortal->addr.addr_ci = UINT_TO_PTR(p_QmPortalParam->ciBaseAddress);
    p_QmPortal->p_LowQmPortal->config.irq = p_QmPortalParam->irq;
    p_QmPortal->p_LowQmPortal->config.bound = 0;
    p_QmPortal->p_LowQmPortal->config.cpu = (int)p_QmPortalParam->swPortalId;
    p_QmPortal->p_LowQmPortal->config.channel = (e_QmFQChannel)(e_QM_FQ_CHANNEL_SWPORTAL0 + p_QmPortalParam->swPortalId);
    p_QmPortal->p_LowQmPortal->bind_lock = XX_InitSpinlock();

    p_QmPortal->h_Qm                = p_QmPortalParam->h_Qm;
    p_QmPortal->f_DfltFrame         = p_QmPortalParam->f_DfltFrame;
    p_QmPortal->f_RejectedFrame     = p_QmPortalParam->f_RejectedFrame;
    p_QmPortal->h_App               = p_QmPortalParam->h_App;

    p_QmPortal->p_QmPortalDriverParams->fdLiodnOffset           = p_QmPortalParam->fdLiodnOffset;
    p_QmPortal->p_QmPortalDriverParams->dequeueDcaMode          = DEFAULT_dequeueDcaMode;
    p_QmPortal->p_QmPortalDriverParams->dequeueUpToThreeFrames  = DEFAULT_dequeueUpToThreeFrames;
    p_QmPortal->p_QmPortalDriverParams->commandType             = DEFAULT_dequeueCommandType;
    p_QmPortal->p_QmPortalDriverParams->userToken               = DEFAULT_dequeueUserToken;
    p_QmPortal->p_QmPortalDriverParams->specifiedWq             = DEFAULT_dequeueSpecifiedWq;
    p_QmPortal->p_QmPortalDriverParams->dedicatedChannel        = DEFAULT_dequeueDedicatedChannel;
    p_QmPortal->p_QmPortalDriverParams->dedicatedChannelHasPrecedenceOverPoolChannels =
        DEFAULT_dequeueDedicatedChannelHasPrecedenceOverPoolChannels;
    p_QmPortal->p_QmPortalDriverParams->poolChannelId           = DEFAULT_dequeuePoolChannelId;
    p_QmPortal->p_QmPortalDriverParams->wqId                    = DEFAULT_dequeueWqId;
    for (i=0;i<QM_MAX_NUM_OF_POOL_CHANNELS;i++)
        p_QmPortal->p_QmPortalDriverParams->poolChannels[i] = FALSE;
    p_QmPortal->p_QmPortalDriverParams->dqrrSize                = DEFAULT_dqrrSize;
    p_QmPortal->p_QmPortalDriverParams->pullMode                = DEFAULT_pullMode;

    return p_QmPortal;
}

t_Error QM_PORTAL_Init(t_Handle h_QmPortal)
{
    t_QmPortal                          *p_QmPortal = (t_QmPortal *)h_QmPortal;
    uint32_t                            i, flags=0, sdqcrFlags=0;
    t_Error                             err;
    t_QmInterModulePortalInitParams     qmParams;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal->p_QmPortalDriverParams, E_INVALID_HANDLE);

    memset(&qmParams, 0, sizeof(qmParams));
    qmParams.portalId       = (uint8_t)p_QmPortal->p_LowQmPortal->config.cpu;
    qmParams.liodn          = p_QmPortal->p_QmPortalDriverParams->fdLiodnOffset;
    qmParams.dqrrLiodn      = p_QmPortal->p_QmPortalDriverParams->dqrrLiodn;
    qmParams.fdFqLiodn      = p_QmPortal->p_QmPortalDriverParams->fdFqLiodn;
    qmParams.stashDestQueue = p_QmPortal->p_QmPortalDriverParams->stashDestQueue;
    if ((err = QmGetSetPortalParams(p_QmPortal->h_Qm, &qmParams)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    flags = (uint32_t)(((p_QmPortal->p_LowQmPortal->config.irq == NO_IRQ) ?
            0 :
            (QMAN_PORTAL_FLAG_IRQ |
             QMAN_PORTAL_FLAG_IRQ_FAST |
             QMAN_PORTAL_FLAG_IRQ_SLOW)));
    flags |= ((p_QmPortal->p_QmPortalDriverParams->dequeueDcaMode) ? QMAN_PORTAL_FLAG_DCA : 0);
    flags |= (p_QmPortal->p_QmPortalDriverParams->dqrr)?QMAN_PORTAL_FLAG_RSTASH:0;
    flags |= (p_QmPortal->p_QmPortalDriverParams->fdFq)?QMAN_PORTAL_FLAG_DSTASH:0;

    p_QmPortal->pullMode = p_QmPortal->p_QmPortalDriverParams->pullMode;
    if (!p_QmPortal->pullMode)
    {
        sdqcrFlags |= (p_QmPortal->p_QmPortalDriverParams->dequeueUpToThreeFrames) ? QM_SDQCR_COUNT_UPTO3 : QM_SDQCR_COUNT_EXACT1;
        sdqcrFlags |= QM_SDQCR_TOKEN_SET(p_QmPortal->p_QmPortalDriverParams->userToken);
        sdqcrFlags |= QM_SDQCR_TYPE_SET(p_QmPortal->p_QmPortalDriverParams->commandType);
        if (!p_QmPortal->p_QmPortalDriverParams->specifiedWq)
        {
            /* sdqcrFlags |= QM_SDQCR_SOURCE_CHANNELS;*/ /* removed as the macro is '0' */
            sdqcrFlags |= (p_QmPortal->p_QmPortalDriverParams->dedicatedChannelHasPrecedenceOverPoolChannels) ? QM_SDQCR_DEDICATED_PRECEDENCE : 0;
            sdqcrFlags |= (p_QmPortal->p_QmPortalDriverParams->dedicatedChannel) ? QM_SDQCR_CHANNELS_DEDICATED : 0;
            for (i=0;i<QM_MAX_NUM_OF_POOL_CHANNELS;i++)
                sdqcrFlags |= ((p_QmPortal->p_QmPortalDriverParams->poolChannels[i]) ?
                     QM_SDQCR_CHANNELS_POOL(i+1) : 0);
        }
        else
        {
            sdqcrFlags |= QM_SDQCR_SOURCE_SPECIFICWQ;
            sdqcrFlags |= (p_QmPortal->p_QmPortalDriverParams->dedicatedChannel) ?
                            QM_SDQCR_SPECIFICWQ_DEDICATED : QM_SDQCR_SPECIFICWQ_POOL(p_QmPortal->p_QmPortalDriverParams->poolChannelId);
            sdqcrFlags |= QM_SDQCR_SPECIFICWQ_WQ(p_QmPortal->p_QmPortalDriverParams->wqId);
        }
    }
    if ((flags & QMAN_PORTAL_FLAG_RSTASH) && (flags & QMAN_PORTAL_FLAG_DCA))
        p_QmPortal->f_LoopDequeueRingCB = LoopDequeueRingDcaOptimized;
    else if ((flags & QMAN_PORTAL_FLAG_RSTASH) && !(flags & QMAN_PORTAL_FLAG_DCA))
        p_QmPortal->f_LoopDequeueRingCB = LoopDequeueRingOptimized;
    else
        p_QmPortal->f_LoopDequeueRingCB = LoopDequeueRing;

    if ((!p_QmPortal->f_RejectedFrame) || (!p_QmPortal->f_DfltFrame))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("f_RejectedFrame or f_DfltFrame callback not provided"));

    p_QmPortal->p_NullCB = (struct qman_fq_cb *)XX_Malloc(sizeof(struct qman_fq_cb));
    if (!p_QmPortal->p_NullCB)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FQ Null CB obj!!!"));
    memset(p_QmPortal->p_NullCB, 0, sizeof(struct qman_fq_cb));

    p_QmPortal->p_NullCB->dqrr      = p_QmPortal->f_DfltFrame;
    p_QmPortal->p_NullCB->ern       = p_QmPortal->f_RejectedFrame;
    p_QmPortal->p_NullCB->dc_ern    = p_QmPortal->p_NullCB->fqs = null_cb_mr;

    if (qman_create_portal(p_QmPortal, flags, sdqcrFlags, p_QmPortal->p_QmPortalDriverParams->dqrrSize) != E_OK)
    {
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("create portal failed"));
    }

    QmSetPortalHandle(p_QmPortal->h_Qm, (t_Handle)p_QmPortal, (e_DpaaSwPortal)p_QmPortal->p_LowQmPortal->config.cpu);
    XX_Free(p_QmPortal->p_QmPortalDriverParams);
    p_QmPortal->p_QmPortalDriverParams = NULL;

    DBG(TRACE, ("Qman-Portal %d @ %p:%p",
                p_QmPortal->p_LowQmPortal->config.cpu,
                p_QmPortal->p_LowQmPortal->addr.addr_ce,
                p_QmPortal->p_LowQmPortal->addr.addr_ci
                ));

    DBG(TRACE, ("Qman-Portal %d phys @ 0x%016llx:0x%016llx",
                p_QmPortal->p_LowQmPortal->config.cpu,
                (uint64_t)XX_VirtToPhys(p_QmPortal->p_LowQmPortal->addr.addr_ce),
                (uint64_t)XX_VirtToPhys(p_QmPortal->p_LowQmPortal->addr.addr_ci)
                ));

    return E_OK;
}

t_Error QM_PORTAL_Free(t_Handle h_QmPortal)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    if (!p_QmPortal)
       return ERROR_CODE(E_INVALID_HANDLE);

    ASSERT_COND(p_QmPortal->p_LowQmPortal);
    QmSetPortalHandle(p_QmPortal->h_Qm, NULL, (e_DpaaSwPortal)p_QmPortal->p_LowQmPortal->config.cpu);
    qman_destroy_portal(p_QmPortal);
    if (p_QmPortal->p_NullCB)
        XX_Free(p_QmPortal->p_NullCB);

    if (p_QmPortal->p_LowQmPortal->bind_lock)
        XX_FreeSpinlock(p_QmPortal->p_LowQmPortal->bind_lock);
    if(p_QmPortal->p_QmPortalDriverParams)
        XX_Free(p_QmPortal->p_QmPortalDriverParams);
    XX_Free(p_QmPortal->p_LowQmPortal);
    XX_Free(p_QmPortal);

    return E_OK;
}

t_Error QM_PORTAL_ConfigDcaMode(t_Handle h_QmPortal, bool enable)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal->p_QmPortalDriverParams, E_INVALID_HANDLE);

    p_QmPortal->p_QmPortalDriverParams->dequeueDcaMode = enable;

    return E_OK;
}

t_Error QM_PORTAL_ConfigStash(t_Handle h_QmPortal, t_QmPortalStashParam *p_StashParams)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal->p_QmPortalDriverParams, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(p_StashParams, E_NULL_POINTER);

    p_QmPortal->p_QmPortalDriverParams->stashDestQueue  = p_StashParams->stashDestQueue;
    p_QmPortal->p_QmPortalDriverParams->dqrrLiodn       = p_StashParams->dqrrLiodn;
    p_QmPortal->p_QmPortalDriverParams->fdFqLiodn       = p_StashParams->fdFqLiodn;
    p_QmPortal->p_QmPortalDriverParams->eqcr            = p_StashParams->eqcr;
    p_QmPortal->p_QmPortalDriverParams->eqcrHighPri     = p_StashParams->eqcrHighPri;
    p_QmPortal->p_QmPortalDriverParams->dqrr            = p_StashParams->dqrr;
    p_QmPortal->p_QmPortalDriverParams->dqrrHighPri     = p_StashParams->dqrrHighPri;
    p_QmPortal->p_QmPortalDriverParams->fdFq            = p_StashParams->fdFq;
    p_QmPortal->p_QmPortalDriverParams->fdFqHighPri     = p_StashParams->fdFqHighPri;
    p_QmPortal->p_QmPortalDriverParams->fdFqDrop        = p_StashParams->fdFqDrop;

    return E_OK;
}


t_Error QM_PORTAL_ConfigPullMode(t_Handle h_QmPortal, bool pullMode)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_QmPortal->p_QmPortalDriverParams, E_NULL_POINTER);

    p_QmPortal->p_QmPortalDriverParams->pullMode  = pullMode;

    return E_OK;
}

t_Error QM_PORTAL_AddPoolChannel(t_Handle h_QmPortal, uint8_t poolChannelId)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;
    uint32_t    sdqcrFlags;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((poolChannelId < QM_MAX_NUM_OF_POOL_CHANNELS), E_INVALID_VALUE);

    sdqcrFlags = qm_dqrr_sdqcr_get(p_QmPortal->p_LowQmPortal);
    sdqcrFlags |= QM_SDQCR_CHANNELS_POOL(poolChannelId+1);
    qm_dqrr_sdqcr_set(p_QmPortal->p_LowQmPortal, sdqcrFlags);

    return E_OK;
}

t_Error QM_PORTAL_Poll(t_Handle h_QmPortal, e_QmPortalPollSource source)
{
    t_QmPortal  *p_QmPortal = (t_QmPortal *)h_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);

    NCSW_PLOCK(p_QmPortal);

    if ((source == e_QM_PORTAL_POLL_SOURCE_CONTROL_FRAMES) ||
        (source == e_QM_PORTAL_POLL_SOURCE_BOTH))
    {
        uint32_t is = qm_isr_status_read(p_QmPortal->p_LowQmPortal);
        uint32_t active = LoopMessageRing(p_QmPortal, is);
        if (active)
            qm_isr_status_clear(p_QmPortal->p_LowQmPortal, active);
    }
    if ((source == e_QM_PORTAL_POLL_SOURCE_DATA_FRAMES) ||
        (source == e_QM_PORTAL_POLL_SOURCE_BOTH))
        p_QmPortal->f_LoopDequeueRingCB((t_Handle)p_QmPortal);

    PUNLOCK(p_QmPortal);

    return E_OK;
}

t_Error QM_PORTAL_PollFrame(t_Handle h_QmPortal, t_QmPortalFrameInfo *p_frameInfo)
{
    t_QmPortal              *p_QmPortal     = (t_QmPortal *)h_QmPortal;
    struct qm_dqrr_entry    *p_Dq;
    struct qman_fq          *p_Fq;
    int                     prefetch;

    SANITY_CHECK_RETURN_ERROR(p_QmPortal, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_frameInfo, E_NULL_POINTER);

    NCSW_PLOCK(p_QmPortal);

    prefetch = !(p_QmPortal->options & QMAN_PORTAL_FLAG_RSTASH);
    if (prefetch)
        qmPortalDqrrPvbPrefetch(p_QmPortal->p_LowQmPortal);
    qmPortalDqrrPvbUpdate(p_QmPortal->p_LowQmPortal);
    p_Dq = qm_dqrr_current(p_QmPortal->p_LowQmPortal);
    if (!p_Dq)
    {
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_EMPTY);
    }
    p_Fq = ptr_from_aligned_int(p_Dq->contextB);
    ASSERT_COND(p_Dq->fqid);
    if (p_Fq)
    {
        p_frameInfo->h_App = p_Fq->h_App;
        p_frameInfo->h_QmFqr = p_Fq->h_QmFqr;
        p_frameInfo->fqidOffset = p_Fq->fqidOffset;
        memcpy((void*)&p_frameInfo->frame, (void*)&p_Dq->fd, sizeof(t_DpaaFD));
    }
    else
    {
        p_frameInfo->h_App = p_QmPortal->h_App;
        p_frameInfo->h_QmFqr = NULL;
        p_frameInfo->fqidOffset = p_Dq->fqid;
        memcpy((void*)&p_frameInfo->frame, (void*)&p_Dq->fd, sizeof(t_DpaaFD));
    }
    if (p_QmPortal->options & QMAN_PORTAL_FLAG_DCA) {
        qmPortalDqrrDcaConsume1ptr(p_QmPortal->p_LowQmPortal,
                                   p_Dq,
                                   false);
        qm_dqrr_next(p_QmPortal->p_LowQmPortal);
    } else {
        qm_dqrr_next(p_QmPortal->p_LowQmPortal);
        qmPortalDqrrCciConsume(p_QmPortal->p_LowQmPortal, 1);
    }

    PUNLOCK(p_QmPortal);

    return E_OK;
}


t_Handle QM_FQR_Create(t_QmFqrParams *p_QmFqrParams)
{
    t_QmFqr             *p_QmFqr;
    uint32_t            i, flags = 0;
    u_QmFqdContextA     cnxtA;

    SANITY_CHECK_RETURN_VALUE(p_QmFqrParams, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_QmFqrParams->h_Qm, E_INVALID_HANDLE, NULL);

    if (p_QmFqrParams->shadowMode &&
        (!p_QmFqrParams->useForce || p_QmFqrParams->numOfFqids != 1))
    {
        REPORT_ERROR(MAJOR, E_CONFLICT, ("shadowMode must be use with useForce and numOfFqids==1!!!"));
        return NULL;
    }

    p_QmFqr = (t_QmFqr *)XX_MallocSmart(sizeof(t_QmFqr), 0, 64);
    if (!p_QmFqr)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("QM FQR obj!!!"));
        return NULL;
    }
    memset(p_QmFqr, 0, sizeof(t_QmFqr));

    p_QmFqr->h_Qm       = p_QmFqrParams->h_Qm;
    p_QmFqr->h_QmPortal = p_QmFqrParams->h_QmPortal;
    p_QmFqr->shadowMode = p_QmFqrParams->shadowMode;
    p_QmFqr->numOfFqids = (p_QmFqrParams->useForce && !p_QmFqrParams->numOfFqids) ?
                              1 : p_QmFqrParams->numOfFqids;

    if (!p_QmFqr->h_QmPortal)
    {
        p_QmFqr->h_QmPortal = QmGetPortalHandle(p_QmFqr->h_Qm);
        SANITY_CHECK_RETURN_VALUE(p_QmFqr->h_QmPortal, E_INVALID_HANDLE, NULL);
    }

    p_QmFqr->p_Fqs = (struct qman_fq **)XX_Malloc(sizeof(struct qman_fq *) * p_QmFqr->numOfFqids);
    if (!p_QmFqr->p_Fqs)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("QM FQs obj!!!"));
        QM_FQR_Free(p_QmFqr);
        return NULL;
    }
    memset(p_QmFqr->p_Fqs, 0, sizeof(struct qman_fq *) * p_QmFqr->numOfFqids);

    if (p_QmFqr->shadowMode)
    {
        struct qman_fq          *p_Fq = NULL;

        p_QmFqr->fqidBase = p_QmFqrParams->qs.frcQ.fqid;
        p_Fq = (struct qman_fq *)XX_MallocSmart(sizeof(struct qman_fq), 0, 64);
        if (!p_Fq)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FQ obj!!!"));
            QM_FQR_Free(p_QmFqr);
            return NULL;
        }
        memset(p_Fq, 0, sizeof(struct qman_fq));
        p_Fq->cb.dqrr     = ((t_QmPortal*)p_QmFqr->h_QmPortal)->f_DfltFrame;
        p_Fq->cb.ern      = ((t_QmPortal*)p_QmFqr->h_QmPortal)->f_RejectedFrame;
        p_Fq->cb.dc_ern   = cb_ern_dcErn;
        p_Fq->cb.fqs      = cb_fqs;
        p_Fq->h_App       = ((t_QmPortal*)p_QmFqr->h_QmPortal)->h_App;
        p_Fq->h_QmFqr     = p_QmFqr;
        p_Fq->state       = qman_fq_state_sched;
        p_Fq->fqid        = p_QmFqr->fqidBase;
        p_QmFqr->p_Fqs[0] = p_Fq;
    }
    else
    {
        p_QmFqr->channel    = p_QmFqrParams->channel;
        p_QmFqr->workQueue  = p_QmFqrParams->wq;

        p_QmFqr->fqidBase = QmFqidGet(p_QmFqr->h_Qm,
                                      p_QmFqr->numOfFqids,
                                      p_QmFqrParams->qs.nonFrcQs.align,
                                      p_QmFqrParams->useForce,
                                      p_QmFqrParams->qs.frcQ.fqid);
        if (p_QmFqr->fqidBase == (uint32_t)ILLEGAL_BASE)
        {
            REPORT_ERROR(CRITICAL,E_INVALID_STATE,("can't allocate a fqid"));
            QM_FQR_Free(p_QmFqr);
            return NULL;
        }

        if(p_QmFqrParams->congestionAvoidanceEnable &&
            (p_QmFqrParams->congestionAvoidanceParams.h_QmCg == NULL) &&
            (p_QmFqrParams->congestionAvoidanceParams.fqTailDropThreshold == 0))
        {
            REPORT_ERROR(CRITICAL,E_INVALID_STATE,("NULL congestion group handle and no FQ Threshold"));
            QM_FQR_Free(p_QmFqr);
            return NULL;
        }
        if(p_QmFqrParams->congestionAvoidanceEnable)
        {
            if(p_QmFqrParams->congestionAvoidanceParams.h_QmCg)
                flags |= QM_FQCTRL_CGE;
            if(p_QmFqrParams->congestionAvoidanceParams.fqTailDropThreshold)
                flags |= QM_FQCTRL_TDE;
        }

    /*
        flags |= (p_QmFqrParams->holdActive)    ? QM_FQCTRL_ORP : 0;
        flags |= (p_QmFqrParams->holdActive)    ? QM_FQCTRL_CPCSTASH : 0;
        flags |= (p_QmFqrParams->holdActive)    ? QM_FQCTRL_FORCESFDR : 0;
        flags |= (p_QmFqrParams->holdActive)    ? QM_FQCTRL_AVOIDBLOCK : 0;
    */
        flags |= (p_QmFqrParams->holdActive)    ? QM_FQCTRL_HOLDACTIVE : 0;
        flags |= (p_QmFqrParams->preferInCache) ? QM_FQCTRL_LOCKINCACHE : 0;

        if (p_QmFqrParams->useContextAForStash)
        {
            if (CheckStashParams(p_QmFqrParams) != E_OK)
            {
                REPORT_ERROR(CRITICAL,E_INVALID_STATE,NO_MSG);
                QM_FQR_Free(p_QmFqr);
                return NULL;
            }

            memset(&cnxtA, 0, sizeof(cnxtA));
            cnxtA.stashing.annotation_cl = DIV_CEIL(p_QmFqrParams->stashingParams.frameAnnotationSize, CACHELINE_SIZE);
            cnxtA.stashing.data_cl = DIV_CEIL(p_QmFqrParams->stashingParams.frameDataSize, CACHELINE_SIZE);
            cnxtA.stashing.context_cl = DIV_CEIL(p_QmFqrParams->stashingParams.fqContextSize, CACHELINE_SIZE);
            cnxtA.context_hi = (uint8_t)((p_QmFqrParams->stashingParams.fqContextAddr >> 32) & 0xff);
            cnxtA.context_lo = (uint32_t)(p_QmFqrParams->stashingParams.fqContextAddr);
            flags |= QM_FQCTRL_CTXASTASHING;
        }

        for(i=0;i<p_QmFqr->numOfFqids;i++)
            if (qm_new_fq(p_QmFqr->h_QmPortal,
                          p_QmFqr->fqidBase+i,
                          i,
                          p_QmFqr->channel,
                          p_QmFqr->workQueue,
                          1/*p_QmFqr->numOfFqids*/,
                          flags,
                          (p_QmFqrParams->congestionAvoidanceEnable ?
                              &p_QmFqrParams->congestionAvoidanceParams : NULL),
                          p_QmFqrParams->useContextAForStash ?
                              (t_QmContextA *)&cnxtA : p_QmFqrParams->p_ContextA,
                          p_QmFqrParams->p_ContextB,
                          p_QmFqrParams->initParked,
                          p_QmFqr,
                          &p_QmFqr->p_Fqs[i]) != E_OK)
            {
                QM_FQR_Free(p_QmFqr);
                return NULL;
            }
    }
    return p_QmFqr;
}

t_Error  QM_FQR_Free(t_Handle h_QmFqr)
{
    t_QmFqr     *p_QmFqr    = (t_QmFqr *)h_QmFqr;
    uint32_t    i;

    if (!p_QmFqr)
        return ERROR_CODE(E_INVALID_HANDLE);

    if (p_QmFqr->p_Fqs)
    {
        for (i=0;i<p_QmFqr->numOfFqids;i++)
            if (p_QmFqr->p_Fqs[i])
            {
                if (!p_QmFqr->shadowMode)
                    qm_free_fq(p_QmFqr->h_QmPortal, p_QmFqr->p_Fqs[i]);
                XX_FreeSmart(p_QmFqr->p_Fqs[i]);
            }
        XX_Free(p_QmFqr->p_Fqs);
    }

    if (!p_QmFqr->shadowMode && p_QmFqr->fqidBase)
        QmFqidPut(p_QmFqr->h_Qm, p_QmFqr->fqidBase);

    XX_FreeSmart(p_QmFqr);

    return E_OK;
}

t_Error  QM_FQR_FreeWDrain(t_Handle                     h_QmFqr,
                           t_QmFqrDrainedCompletionCB   *f_CompletionCB,
                           bool                         deliverFrame,
                           t_QmReceivedFrameCallback    *f_CallBack,
                           t_Handle                     h_App)
{
    t_QmFqr     *p_QmFqr    = (t_QmFqr *)h_QmFqr;
    uint32_t    i;

    if (!p_QmFqr)
        return ERROR_CODE(E_INVALID_HANDLE);

    if (p_QmFqr->shadowMode)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("QM_FQR_FreeWDrain can't be called to shadow FQR!!!. call QM_FQR_Free"));

    p_QmFqr->p_DrainedFqs = (bool *)XX_Malloc(sizeof(bool) * p_QmFqr->numOfFqids);
    if (!p_QmFqr->p_DrainedFqs)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("QM Drained-FQs obj!!!. Try to Free without draining"));
    memset(p_QmFqr->p_DrainedFqs, 0, sizeof(bool) * p_QmFqr->numOfFqids);

    if (f_CompletionCB)
    {
        p_QmFqr->f_CompletionCB = f_CompletionCB;
        p_QmFqr->h_App          = h_App;
    }

    if (deliverFrame)
    {
        if (!f_CallBack)
        {
            REPORT_ERROR(MAJOR, E_NULL_POINTER, ("f_CallBack must be given."));
            XX_Free(p_QmFqr->p_DrainedFqs);
            return ERROR_CODE(E_NULL_POINTER);
        }
        QM_FQR_RegisterCB(p_QmFqr, f_CallBack, h_App);
    }
    else
        QM_FQR_RegisterCB(p_QmFqr, drainCB, h_App);

    for (i=0;i<p_QmFqr->numOfFqids;i++)
    {
        if (qman_retire_fq(p_QmFqr->h_QmPortal, p_QmFqr->p_Fqs[i], 0, true) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("qman_retire_fq() failed!"));

        if (p_QmFqr->p_Fqs[i]->flags & QMAN_FQ_STATE_CHANGING)
            DBG(INFO, ("fq %d currently in use, will be retired", p_QmFqr->p_Fqs[i]->fqid));
        else
            drainRetiredFq(p_QmFqr->p_Fqs[i]);
    }

    if (!p_QmFqr->f_CompletionCB)
    {
        while(p_QmFqr->p_DrainedFqs) ;
        DBG(TRACE, ("QM-FQR with base %d completed", p_QmFqr->fqidBase));
        XX_FreeSmart(p_QmFqr->p_Fqs);
        if (p_QmFqr->fqidBase)
            QmFqidPut(p_QmFqr->h_Qm, p_QmFqr->fqidBase);
        XX_FreeSmart(p_QmFqr);
    }

    return E_OK;
}

t_Error QM_FQR_RegisterCB(t_Handle h_QmFqr, t_QmReceivedFrameCallback *f_CallBack, t_Handle h_App)
{
    t_QmFqr     *p_QmFqr = (t_QmFqr *)h_QmFqr;
    int         i;

    SANITY_CHECK_RETURN_ERROR(p_QmFqr, E_INVALID_HANDLE);

    for (i=0;i<p_QmFqr->numOfFqids;i++)
    {
        p_QmFqr->p_Fqs[i]->cb.dqrr = f_CallBack;
        p_QmFqr->p_Fqs[i]->h_App   = h_App;
    }

    return E_OK;
}

t_Error QM_FQR_Enqueue(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, t_DpaaFD *p_Frame)
{
    t_QmFqr                 *p_QmFqr = (t_QmFqr *)h_QmFqr;
    t_QmPortal              *p_QmPortal;
    struct qm_eqcr_entry    *p_Eq;
    uint32_t                *p_Dst, *p_Src;
    const struct qman_fq    *p_Fq;

    SANITY_CHECK_RETURN_ERROR(p_QmFqr, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((fqidOffset < p_QmFqr->numOfFqids), E_INVALID_VALUE);

    if (!h_QmPortal)
    {
        SANITY_CHECK_RETURN_ERROR(p_QmFqr->h_Qm, E_INVALID_HANDLE);
        h_QmPortal = QmGetPortalHandle(p_QmFqr->h_Qm);
        SANITY_CHECK_RETURN_ERROR(h_QmPortal, E_INVALID_HANDLE);
    }
    p_QmPortal = (t_QmPortal *)h_QmPortal;
 
    p_Fq = p_QmFqr->p_Fqs[fqidOffset];

#ifdef QM_CHECKING
    if (p_Fq->flags & QMAN_FQ_FLAG_NO_ENQUEUE)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, NO_MSG);
    if ((!(p_Fq->flags & QMAN_FQ_FLAG_NO_MODIFY)) &&
        ((p_Fq->state == qman_fq_state_retired) ||
         (p_Fq->state == qman_fq_state_oos)))
        return ERROR_CODE(E_BUSY);
#endif /* QM_CHECKING */

    NCSW_PLOCK(p_QmPortal);
    p_Eq = try_eq_start(p_QmPortal);
    if (!p_Eq)
    {
        PUNLOCK(p_QmPortal);
        return ERROR_CODE(E_BUSY);
    }

    p_Eq->fqid = p_Fq->fqid;
    p_Eq->tag = aligned_int_from_ptr(p_Fq);
    /* gcc does a dreadful job of the following;
     *  eq->fd = *fd;
     * It causes the entire function to save/restore a wider range of
     * registers, and comes up with instruction-waste galore. This will do
     * until we can rework the function for better code-generation. */
    p_Dst = (uint32_t *)&p_Eq->fd;
    p_Src = (uint32_t *)p_Frame;
    p_Dst[0] = p_Src[0];
    p_Dst[1] = p_Src[1];
    p_Dst[2] = p_Src[2];
    p_Dst[3] = p_Src[3];

    qmPortalEqcrPvbCommit(p_QmPortal->p_LowQmPortal,
                          (uint8_t)(QM_EQCR_VERB_CMD_ENQUEUE/* |
                          (flags & (QM_EQCR_VERB_COLOUR_MASK | QM_EQCR_VERB_INTERRUPT))*/));
    PUNLOCK(p_QmPortal);

    return E_OK;
}


t_Error QM_FQR_PullFrame(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, t_DpaaFD *p_Frame)
{
    t_QmFqr                 *p_QmFqr = (t_QmFqr *)h_QmFqr;
    uint32_t                pdqcr = 0;

    SANITY_CHECK_RETURN_ERROR(p_QmFqr, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((fqidOffset < p_QmFqr->numOfFqids), E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(p_Frame, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR((p_QmFqr->p_Fqs[fqidOffset]->state == qman_fq_state_oos) ||
                              (p_QmFqr->p_Fqs[fqidOffset]->state == qman_fq_state_parked),
                              E_INVALID_STATE);
    if (!h_QmPortal)
    {
        SANITY_CHECK_RETURN_ERROR(p_QmFqr->h_Qm, E_INVALID_HANDLE);
        h_QmPortal = QmGetPortalHandle(p_QmFqr->h_Qm);
        SANITY_CHECK_RETURN_ERROR(h_QmPortal, E_INVALID_HANDLE);
    }

    pdqcr |= QM_PDQCR_MODE_UNSCHEDULED;
    pdqcr |= QM_PDQCR_FQID(p_QmFqr->p_Fqs[fqidOffset]->fqid);
    return QmPortalPullFrame(h_QmPortal, pdqcr, p_Frame);
}

t_Error QM_FQR_Resume(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset)
{
    t_QmFqr     *p_QmFqr = (t_QmFqr *)h_QmFqr;

    SANITY_CHECK_RETURN_ERROR(p_QmFqr, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((fqidOffset < p_QmFqr->numOfFqids), E_INVALID_VALUE);

    if (!h_QmPortal)
    {
        SANITY_CHECK_RETURN_ERROR(p_QmFqr->h_Qm, E_INVALID_HANDLE);
        h_QmPortal = QmGetPortalHandle(p_QmFqr->h_Qm);
        SANITY_CHECK_RETURN_ERROR(h_QmPortal, E_INVALID_HANDLE);
    }
    return qman_schedule_fq(h_QmPortal, p_QmFqr->p_Fqs[fqidOffset]);
}

t_Error  QM_FQR_Suspend(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset)
{
    t_QmFqr     *p_QmFqr = (t_QmFqr *)h_QmFqr;

    SANITY_CHECK_RETURN_ERROR(p_QmFqr, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR((fqidOffset < p_QmFqr->numOfFqids), E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR((p_QmFqr->p_Fqs[fqidOffset]->flags & QM_FQCTRL_HOLDACTIVE), E_INVALID_STATE);

    UNUSED(h_QmPortal);
    p_QmFqr->p_Fqs[fqidOffset]->state = qman_fq_state_waiting_parked;

    return E_OK;
}

uint32_t QM_FQR_GetFqid(t_Handle h_QmFqr)
{
    t_QmFqr *p_QmFqr = (t_QmFqr *)h_QmFqr;

    SANITY_CHECK_RETURN_VALUE(p_QmFqr, E_INVALID_HANDLE, 0);

    return p_QmFqr->fqidBase;
}

uint32_t QM_FQR_GetCounter(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, e_QmFqrCounters counter)
{
    t_QmFqr *p_QmFqr = (t_QmFqr *)h_QmFqr;
    struct qm_mcr_queryfq_np    queryfq_np;

    SANITY_CHECK_RETURN_VALUE(p_QmFqr, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE((fqidOffset < p_QmFqr->numOfFqids), E_INVALID_VALUE, 0);

    if (!h_QmPortal)
    {
        SANITY_CHECK_RETURN_VALUE(p_QmFqr->h_Qm, E_INVALID_HANDLE, 0);
        h_QmPortal = QmGetPortalHandle(p_QmFqr->h_Qm);
        SANITY_CHECK_RETURN_VALUE(h_QmPortal, E_INVALID_HANDLE, 0);
    }
    if (qman_query_fq_np(h_QmPortal, p_QmFqr->p_Fqs[fqidOffset], &queryfq_np) != E_OK)
        return 0;
    switch (counter)
    {
        case e_QM_FQR_COUNTERS_FRAME :
            return queryfq_np.frm_cnt;
        case e_QM_FQR_COUNTERS_BYTE :
            return queryfq_np.byte_cnt;
        default :
            break;
    }
    /* should never get here */
    ASSERT_COND(FALSE);

    return 0;
}


t_Handle QM_CG_Create(t_QmCgParams *p_CgParams)
{
    t_QmCg                          *p_QmCg;
    t_QmPortal                      *p_QmPortal;
    t_Error                         err;
    uint32_t                        wredParams;
    uint32_t                        tmpA, tmpN, ta=0, tn=0;
    int                             gap, tmp;
    struct qm_mc_command            *p_Mcc;
    struct qm_mc_result             *p_Mcr;

    SANITY_CHECK_RETURN_VALUE(p_CgParams, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_CgParams->h_Qm, E_INVALID_HANDLE, NULL);

    if(p_CgParams->notifyDcPortal &&
       ((p_CgParams->dcPortalId == e_DPAA_DCPORTAL2) || (p_CgParams->dcPortalId == e_DPAA_DCPORTAL3)))
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("notifyDcPortal is invalid for this DC Portal"));
        return NULL;
    }

    if (!p_CgParams->h_QmPortal)
    {
        p_QmPortal = QmGetPortalHandle(p_CgParams->h_Qm);
        SANITY_CHECK_RETURN_VALUE(p_QmPortal, E_INVALID_STATE, NULL);
    }
    else
        p_QmPortal = p_CgParams->h_QmPortal;

    p_QmCg = (t_QmCg *)XX_Malloc(sizeof(t_QmCg));
    if (!p_QmCg)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("QM CG obj!!!"));
        return NULL;
    }
    memset(p_QmCg, 0, sizeof(t_QmCg));

    /* build CG struct */
    p_QmCg->h_Qm        = p_CgParams->h_Qm;
    p_QmCg->h_QmPortal  = p_QmPortal;
    p_QmCg->h_App       = p_CgParams->h_App;
    err = QmGetCgId(p_CgParams->h_Qm, &p_QmCg->id);
    if (err)
    {
        XX_Free(p_QmCg);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("QmGetCgId failed"));
        return NULL;
    }

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;

    err = QmPortalRegisterCg(p_QmPortal, p_QmCg, p_QmCg->id);
    if (err)
    {
        XX_Free(p_QmCg);
        PUNLOCK(p_QmPortal);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("QmPortalRegisterCg failed"));
        return NULL;
    }

    /*  Build CGR command */
    {
#ifdef QM_CGS_NO_FRAME_MODE
    t_QmRevisionInfo    revInfo;

    QmGetRevision(p_QmCg->h_Qm, &revInfo);

    if (!((revInfo.majorRev == 1) && (revInfo.minorRev == 0)))
#endif /* QM_CGS_NO_FRAME_MODE */
        if (p_CgParams->frameCount)
        {
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_MODE;
            p_Mcc->initcgr.cgr.frame_mode = QM_CGR_EN;
        }
    }

    if (p_CgParams->wredEnable)
    {
        if (p_CgParams->wredParams.enableGreen)
        {
            err = CalcWredCurve(&p_CgParams->wredParams.greenCurve, &wredParams);
            if(err)
            {
                XX_Free(p_QmCg);
                PUNLOCK(p_QmPortal);
                REPORT_ERROR(MAJOR, err, NO_MSG);
                return NULL;
            }
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_G | QM_CGR_WE_WR_PARM_G;
            p_Mcc->initcgr.cgr.wr_en_g = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_g.word = wredParams;
        }
        if (p_CgParams->wredParams.enableYellow)
        {
            err = CalcWredCurve(&p_CgParams->wredParams.yellowCurve, &wredParams);
            if(err)
            {
                XX_Free(p_QmCg);
                PUNLOCK(p_QmPortal);
                REPORT_ERROR(MAJOR, err, NO_MSG);
                return NULL;
            }
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_Y | QM_CGR_WE_WR_PARM_Y;
            p_Mcc->initcgr.cgr.wr_en_y = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_y.word = wredParams;
        }
        if (p_CgParams->wredParams.enableRed)
        {
            err = CalcWredCurve(&p_CgParams->wredParams.redCurve, &wredParams);
            if(err)
            {
                XX_Free(p_QmCg);
                PUNLOCK(p_QmPortal);
                REPORT_ERROR(MAJOR, err, NO_MSG);
                return NULL;
            }
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_R | QM_CGR_WE_WR_PARM_R;
            p_Mcc->initcgr.cgr.wr_en_r = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_r.word = wredParams;
        }
    }

    if (p_CgParams->tailDropEnable)
    {
        if (!p_CgParams->threshold)
        {
            XX_Free(p_QmCg);
            PUNLOCK(p_QmPortal);
            REPORT_ERROR(MINOR, E_INVALID_STATE, ("tailDropThreshold must be configured if tailDropEnable "));
            return NULL;
        }
        p_Mcc->initcgr.cgr.cstd_en = QM_CGR_EN;
        p_Mcc->initcgr.we_mask |= QM_CGR_WE_CSTD_EN;
    }

    if (p_CgParams->threshold)
    {
        p_Mcc->initcgr.we_mask |= QM_CGR_WE_CS_THRES;
        p_QmCg->f_Exception = p_CgParams->f_Exception;
        if (p_QmCg->f_Exception || p_CgParams->notifyDcPortal)
        {
            p_Mcc->initcgr.cgr.cscn_en = QM_CGR_EN;
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_CSCN_EN | QM_CGR_WE_CSCN_TARG;
            /* if SW - set target, if HW - if FM, set HW target, otherwize, set SW target */
            p_Mcc->initcgr.cgr.cscn_targ = 0;
            if (p_QmCg->f_Exception)
                p_Mcc->initcgr.cgr.cscn_targ = (uint32_t)QM_CGR_TARGET_SWP(QmPortalGetSwPortalId(p_QmCg->h_QmPortal));
            if (p_CgParams->notifyDcPortal)
                p_Mcc->initcgr.cgr.cscn_targ |= (uint32_t)QM_CGR_TARGET_DCP(p_CgParams->dcPortalId);
        }

        /* express thresh as ta*2^tn */
        gap = (int)p_CgParams->threshold;
        for (tmpA=0 ; tmpA<256; tmpA++ )
            for (tmpN=0 ; tmpN<32; tmpN++ )
            {
                tmp = ABS((int)(p_CgParams->threshold - tmpA*(1<<tmpN)));
                if (tmp < gap)
                {
                   ta = tmpA;
                   tn = tmpN;
                   gap = tmp;
                }
            }
        p_Mcc->initcgr.cgr.cs_thres.TA = ta;
        p_Mcc->initcgr.cgr.cs_thres.Tn = tn;
    }
    else if(p_CgParams->f_Exception)
    {
        XX_Free(p_QmCg);
        PUNLOCK(p_QmPortal);
        REPORT_ERROR(MINOR, E_INVALID_STATE, ("No threshold configured, but f_Exception defined"));
        return NULL;
    }

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_INITCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_INITCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        XX_Free(p_QmCg);
        PUNLOCK(p_QmPortal);
        REPORT_ERROR(MINOR, E_INVALID_STATE, ("INITCGR failed: %s", mcr_result_str(p_Mcr->result)));
        return NULL;
    }
    PUNLOCK(p_QmPortal);

    return p_QmCg;
}

t_Error QM_CG_Free(t_Handle h_QmCg)
{

    t_QmCg                  *p_QmCg = (t_QmCg *)h_QmCg;
    t_Error                 err;
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    t_QmPortal              *p_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmCg, E_INVALID_HANDLE);

    p_QmPortal = (t_QmPortal *)p_QmCg->h_QmPortal;

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;
    p_Mcc->initcgr.we_mask = QM_CGR_WE_MASK;

    err = QmFreeCgId(p_QmCg->h_Qm, p_QmCg->id);
    if(err)
    {
        XX_Free(p_QmCg);
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("QmFreeCgId failed"));
    }

    err = QmPortalUnregisterCg(p_QmCg->h_QmPortal, p_QmCg->id);
    if(err)
    {
        XX_Free(p_QmCg);
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("QmPortalUnregisterCg failed"));
    }

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_MODIFYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_MODIFYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("INITCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }
    PUNLOCK(p_QmPortal);

    XX_Free(p_QmCg);

    return E_OK;
}

t_Error QM_CG_SetException(t_Handle h_QmCg, e_QmExceptions exception, bool enable)
{
    t_QmCg                  *p_QmCg = (t_QmCg *)h_QmCg;
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    t_QmPortal              *p_QmPortal;

    SANITY_CHECK_RETURN_ERROR(p_QmCg, E_INVALID_HANDLE);

    p_QmPortal = (t_QmPortal *)p_QmCg->h_QmPortal;
    if (!p_QmCg->f_Exception)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("Either threshold or exception callback was not configured."));

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;
    p_Mcc->initcgr.we_mask = QM_CGR_WE_CSCN_EN;

    if(exception == e_QM_EX_CG_STATE_CHANGE)
    {
        if(enable)
            p_Mcc->initcgr.cgr.cscn_en = QM_CGR_EN;
    }
    else
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Illegal exception"));
    }

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_MODIFYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_MODIFYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("INITCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }
    PUNLOCK(p_QmPortal);

    return E_OK;
}

t_Error QM_CG_ModifyWredCurve(t_Handle h_QmCg, t_QmCgModifyWredParams *p_QmCgModifyParams)
{
    t_QmCg                  *p_QmCg = (t_QmCg *)h_QmCg;
    uint32_t                wredParams;
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    t_QmPortal              *p_QmPortal;
    t_Error                 err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_QmCg, E_INVALID_HANDLE);

    p_QmPortal = (t_QmPortal *)p_QmCg->h_QmPortal;

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_QUERYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("QM_MCC_VERB_QUERYCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }

    switch(p_QmCgModifyParams->color)
    {
        case(e_QM_CG_COLOR_GREEN):
            if(!p_Mcr->querycgr.cgr.wr_en_g)
            {
                PUNLOCK(p_QmPortal);
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("WRED is not enabled for green"));
            }
            break;
        case(e_QM_CG_COLOR_YELLOW):
            if(!p_Mcr->querycgr.cgr.wr_en_y)
            {
                PUNLOCK(p_QmPortal);
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("WRED is not enabled for yellow"));
            }
            break;
        case(e_QM_CG_COLOR_RED):
            if(!p_Mcr->querycgr.cgr.wr_en_r)
            {
                PUNLOCK(p_QmPortal);
                RETURN_ERROR(MINOR, E_INVALID_STATE, ("WRED is not enabled for red"));
            }
            break;
    }

    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;

    switch(p_QmCgModifyParams->color)
    {
        case(e_QM_CG_COLOR_GREEN):
            err = CalcWredCurve(&p_QmCgModifyParams->wredParams, &wredParams);
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_G | QM_CGR_WE_WR_PARM_G;
            p_Mcc->initcgr.cgr.wr_en_g = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_g.word = wredParams;
            break;
        case(e_QM_CG_COLOR_YELLOW):
            err = CalcWredCurve(&p_QmCgModifyParams->wredParams, &wredParams);
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_Y | QM_CGR_WE_WR_PARM_Y;
            p_Mcc->initcgr.cgr.wr_en_y = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_y.word = wredParams;
            break;
        case(e_QM_CG_COLOR_RED):
            err = CalcWredCurve(&p_QmCgModifyParams->wredParams, &wredParams);
            p_Mcc->initcgr.we_mask |= QM_CGR_WE_WR_EN_R | QM_CGR_WE_WR_PARM_R;
            p_Mcc->initcgr.cgr.wr_en_r = QM_CGR_EN;
            p_Mcc->initcgr.cgr.wr_parm_r.word = wredParams;
            break;
    }
    if (err)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, err, NO_MSG);
    }

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_MODIFYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_MODIFYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("INITCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }
    PUNLOCK(p_QmPortal);

    return E_OK;
}

t_Error QM_CG_ModifyTailDropThreshold(t_Handle h_QmCg, uint32_t threshold)
{
    t_QmCg                  *p_QmCg = (t_QmCg *)h_QmCg;
    struct qm_mc_command    *p_Mcc;
    struct qm_mc_result     *p_Mcr;
    t_QmPortal              *p_QmPortal;
    uint32_t                tmpA, tmpN, ta=0, tn=0;
    int                     gap, tmp;

    SANITY_CHECK_RETURN_ERROR(p_QmCg, E_INVALID_HANDLE);

    p_QmPortal = (t_QmPortal *)p_QmCg->h_QmPortal;

    NCSW_PLOCK(p_QmPortal);
    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_QUERYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_QUERYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("QM_MCC_VERB_QUERYCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }

    if(!p_Mcr->querycgr.cgr.cstd_en)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("Tail Drop is not enabled!"));
    }

    p_Mcc = qm_mc_start(p_QmPortal->p_LowQmPortal);
    p_Mcc->initcgr.cgid = p_QmCg->id;
    p_Mcc->initcgr.we_mask |= QM_CGR_WE_CS_THRES;

    /* express thresh as ta*2^tn */
    gap = (int)threshold;
    for (tmpA=0 ; tmpA<256; tmpA++ )
        for (tmpN=0 ; tmpN<32; tmpN++ )
        {
            tmp = ABS((int)(threshold - tmpA*(1<<tmpN)));
            if (tmp < gap)
            {
               ta = tmpA;
               tn = tmpN;
               gap = tmp;
            }
        }
    p_Mcc->initcgr.cgr.cs_thres.TA = ta;
    p_Mcc->initcgr.cgr.cs_thres.Tn = tn;

    qm_mc_commit(p_QmPortal->p_LowQmPortal, QM_MCC_VERB_MODIFYCGR);
    while (!(p_Mcr = qm_mc_result(p_QmPortal->p_LowQmPortal))) ;
    ASSERT_COND((p_Mcr->verb & QM_MCR_VERB_MASK) == QM_MCC_VERB_MODIFYCGR);
    if (p_Mcr->result != QM_MCR_RESULT_OK)
    {
        PUNLOCK(p_QmPortal);
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("INITCGR failed: %s", mcr_result_str(p_Mcr->result)));
    }
    PUNLOCK(p_QmPortal);

    return E_OK;
}

