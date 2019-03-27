/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/aio.h>
#include <sys/file.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/protosw.h>
#include <sys/proc.h>
#include <sys/domain.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/tcp_var.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/toecore.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>

#ifdef TCP_OFFLOAD
#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"
#include "common/t4_tcb.h"
#include "tom/t4_tom.h"

/*
 * Use the 'backend3' field in AIO jobs to store the amount of data
 * received by the AIO job so far.
 */
#define	aio_received	backend3

static void aio_ddp_requeue_task(void *context, int pending);
static void ddp_complete_all(struct toepcb *toep, int error);
static void t4_aio_cancel_active(struct kaiocb *job);
static void t4_aio_cancel_queued(struct kaiocb *job);

static TAILQ_HEAD(, pageset) ddp_orphan_pagesets;
static struct mtx ddp_orphan_pagesets_lock;
static struct task ddp_orphan_task;

#define MAX_DDP_BUFFER_SIZE		(M_TCB_RX_DDP_BUF0_LEN)

/*
 * A page set holds information about a buffer used for DDP.  The page
 * set holds resources such as the VM pages backing the buffer (either
 * held or wired) and the page pods associated with the buffer.
 * Recently used page sets are cached to allow for efficient reuse of
 * buffers (avoiding the need to re-fault in pages, hold them, etc.).
 * Note that cached page sets keep the backing pages wired.  The
 * number of wired pages is capped by only allowing for two wired
 * pagesets per connection.  This is not a perfect cap, but is a
 * trade-off for performance.
 *
 * If an application ping-pongs two buffers for a connection via
 * aio_read(2) then those buffers should remain wired and expensive VM
 * fault lookups should be avoided after each buffer has been used
 * once.  If an application uses more than two buffers then this will
 * fall back to doing expensive VM fault lookups for each operation.
 */
static void
free_pageset(struct tom_data *td, struct pageset *ps)
{
	vm_page_t p;
	int i;

	if (ps->prsv.prsv_nppods > 0)
		t4_free_page_pods(&ps->prsv);

	if (ps->flags & PS_WIRED) {
		for (i = 0; i < ps->npages; i++) {
			p = ps->pages[i];
			vm_page_lock(p);
			vm_page_unwire(p, PQ_INACTIVE);
			vm_page_unlock(p);
		}
	} else
		vm_page_unhold_pages(ps->pages, ps->npages);
	mtx_lock(&ddp_orphan_pagesets_lock);
	TAILQ_INSERT_TAIL(&ddp_orphan_pagesets, ps, link);
	taskqueue_enqueue(taskqueue_thread, &ddp_orphan_task);
	mtx_unlock(&ddp_orphan_pagesets_lock);
}

static void
ddp_free_orphan_pagesets(void *context, int pending)
{
	struct pageset *ps;

	mtx_lock(&ddp_orphan_pagesets_lock);
	while (!TAILQ_EMPTY(&ddp_orphan_pagesets)) {
		ps = TAILQ_FIRST(&ddp_orphan_pagesets);
		TAILQ_REMOVE(&ddp_orphan_pagesets, ps, link);
		mtx_unlock(&ddp_orphan_pagesets_lock);
		if (ps->vm)
			vmspace_free(ps->vm);
		free(ps, M_CXGBE);
		mtx_lock(&ddp_orphan_pagesets_lock);
	}
	mtx_unlock(&ddp_orphan_pagesets_lock);
}

static void
recycle_pageset(struct toepcb *toep, struct pageset *ps)
{

	DDP_ASSERT_LOCKED(toep);
	if (!(toep->ddp.flags & DDP_DEAD) && ps->flags & PS_WIRED) {
		KASSERT(toep->ddp.cached_count + toep->ddp.active_count <
		    nitems(toep->ddp.db), ("too many wired pagesets"));
		TAILQ_INSERT_HEAD(&toep->ddp.cached_pagesets, ps, link);
		toep->ddp.cached_count++;
	} else
		free_pageset(toep->td, ps);
}

static void
ddp_complete_one(struct kaiocb *job, int error)
{
	long copied;

	/*
	 * If this job had copied data out of the socket buffer before
	 * it was cancelled, report it as a short read rather than an
	 * error.
	 */
	copied = job->aio_received;
	if (copied != 0 || error == 0)
		aio_complete(job, copied, 0);
	else
		aio_complete(job, -1, error);
}

static void
free_ddp_buffer(struct tom_data *td, struct ddp_buffer *db)
{

	if (db->job) {
		/*
		 * XXX: If we are un-offloading the socket then we
		 * should requeue these on the socket somehow.  If we
		 * got a FIN from the remote end, then this completes
		 * any remaining requests with an EOF read.
		 */
		if (!aio_clear_cancel_function(db->job))
			ddp_complete_one(db->job, 0);
	}

	if (db->ps)
		free_pageset(td, db->ps);
}

void
ddp_init_toep(struct toepcb *toep)
{

	TAILQ_INIT(&toep->ddp.aiojobq);
	TASK_INIT(&toep->ddp.requeue_task, 0, aio_ddp_requeue_task, toep);
	toep->ddp.flags = DDP_OK;
	toep->ddp.active_id = -1;
	mtx_init(&toep->ddp.lock, "t4 ddp", NULL, MTX_DEF);
}

void
ddp_uninit_toep(struct toepcb *toep)
{

	mtx_destroy(&toep->ddp.lock);
}

void
release_ddp_resources(struct toepcb *toep)
{
	struct pageset *ps;
	int i;

	DDP_LOCK(toep);
	toep->flags |= DDP_DEAD;
	for (i = 0; i < nitems(toep->ddp.db); i++) {
		free_ddp_buffer(toep->td, &toep->ddp.db[i]);
	}
	while ((ps = TAILQ_FIRST(&toep->ddp.cached_pagesets)) != NULL) {
		TAILQ_REMOVE(&toep->ddp.cached_pagesets, ps, link);
		free_pageset(toep->td, ps);
	}
	ddp_complete_all(toep, 0);
	DDP_UNLOCK(toep);
}

#ifdef INVARIANTS
void
ddp_assert_empty(struct toepcb *toep)
{
	int i;

	MPASS(!(toep->ddp.flags & DDP_TASK_ACTIVE));
	for (i = 0; i < nitems(toep->ddp.db); i++) {
		MPASS(toep->ddp.db[i].job == NULL);
		MPASS(toep->ddp.db[i].ps == NULL);
	}
	MPASS(TAILQ_EMPTY(&toep->ddp.cached_pagesets));
	MPASS(TAILQ_EMPTY(&toep->ddp.aiojobq));
}
#endif

static void
complete_ddp_buffer(struct toepcb *toep, struct ddp_buffer *db,
    unsigned int db_idx)
{
	unsigned int db_flag;

	toep->ddp.active_count--;
	if (toep->ddp.active_id == db_idx) {
		if (toep->ddp.active_count == 0) {
			KASSERT(toep->ddp.db[db_idx ^ 1].job == NULL,
			    ("%s: active_count mismatch", __func__));
			toep->ddp.active_id = -1;
		} else
			toep->ddp.active_id ^= 1;
#ifdef VERBOSE_TRACES
		CTR2(KTR_CXGBE, "%s: ddp_active_id = %d", __func__,
		    toep->ddp.active_id);
#endif
	} else {
		KASSERT(toep->ddp.active_count != 0 &&
		    toep->ddp.active_id != -1,
		    ("%s: active count mismatch", __func__));
	}

	db->cancel_pending = 0;
	db->job = NULL;
	recycle_pageset(toep, db->ps);
	db->ps = NULL;

	db_flag = db_idx == 1 ? DDP_BUF1_ACTIVE : DDP_BUF0_ACTIVE;
	KASSERT(toep->ddp.flags & db_flag,
	    ("%s: DDP buffer not active. toep %p, ddp_flags 0x%x",
	    __func__, toep, toep->ddp.flags));
	toep->ddp.flags &= ~db_flag;
}

/* XXX: handle_ddp_data code duplication */
void
insert_ddp_data(struct toepcb *toep, uint32_t n)
{
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = intotcpcb(inp);
	struct ddp_buffer *db;
	struct kaiocb *job;
	size_t placed;
	long copied;
	unsigned int db_flag, db_idx;

	INP_WLOCK_ASSERT(inp);
	DDP_ASSERT_LOCKED(toep);

	tp->rcv_nxt += n;
#ifndef USE_DDP_RX_FLOW_CONTROL
	KASSERT(tp->rcv_wnd >= n, ("%s: negative window size", __func__));
	tp->rcv_wnd -= n;
#endif
#ifndef USE_DDP_RX_FLOW_CONTROL
	toep->rx_credits += n;
#endif
	CTR2(KTR_CXGBE, "%s: placed %u bytes before falling out of DDP",
	    __func__, n);
	while (toep->ddp.active_count > 0) {
		MPASS(toep->ddp.active_id != -1);
		db_idx = toep->ddp.active_id;
		db_flag = db_idx == 1 ? DDP_BUF1_ACTIVE : DDP_BUF0_ACTIVE;
		MPASS((toep->ddp.flags & db_flag) != 0);
		db = &toep->ddp.db[db_idx];
		job = db->job;
		copied = job->aio_received;
		placed = n;
		if (placed > job->uaiocb.aio_nbytes - copied)
			placed = job->uaiocb.aio_nbytes - copied;
		if (placed > 0)
			job->msgrcv = 1;
		if (!aio_clear_cancel_function(job)) {
			/*
			 * Update the copied length for when
			 * t4_aio_cancel_active() completes this
			 * request.
			 */
			job->aio_received += placed;
		} else if (copied + placed != 0) {
			CTR4(KTR_CXGBE,
			    "%s: completing %p (copied %ld, placed %lu)",
			    __func__, job, copied, placed);
			/* XXX: This always completes if there is some data. */
			aio_complete(job, copied + placed, 0);
		} else if (aio_set_cancel_function(job, t4_aio_cancel_queued)) {
			TAILQ_INSERT_HEAD(&toep->ddp.aiojobq, job, list);
			toep->ddp.waiting_count++;
		} else
			aio_cancel(job);
		n -= placed;
		complete_ddp_buffer(toep, db, db_idx);
	}

	MPASS(n == 0);
}

/* SET_TCB_FIELD sent as a ULP command looks like this */
#define LEN__SET_TCB_FIELD_ULP (sizeof(struct ulp_txpkt) + \
    sizeof(struct ulptx_idata) + sizeof(struct cpl_set_tcb_field_core))

/* RX_DATA_ACK sent as a ULP command looks like this */
#define LEN__RX_DATA_ACK_ULP (sizeof(struct ulp_txpkt) + \
    sizeof(struct ulptx_idata) + sizeof(struct cpl_rx_data_ack_core))

static inline void *
mk_set_tcb_field_ulp(struct ulp_txpkt *ulpmc, struct toepcb *toep,
    uint64_t word, uint64_t mask, uint64_t val)
{
	struct ulptx_idata *ulpsc;
	struct cpl_set_tcb_field_core *req;

	ulpmc->cmd_dest = htonl(V_ULPTX_CMD(ULP_TX_PKT) | V_ULP_TXPKT_DEST(0));
	ulpmc->len = htobe32(howmany(LEN__SET_TCB_FIELD_ULP, 16));

	ulpsc = (struct ulptx_idata *)(ulpmc + 1);
	ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
	ulpsc->len = htobe32(sizeof(*req));

	req = (struct cpl_set_tcb_field_core *)(ulpsc + 1);
	OPCODE_TID(req) = htobe32(MK_OPCODE_TID(CPL_SET_TCB_FIELD, toep->tid));
	req->reply_ctrl = htobe16(V_NO_REPLY(1) |
	    V_QUEUENO(toep->ofld_rxq->iq.abs_id));
	req->word_cookie = htobe16(V_WORD(word) | V_COOKIE(0));
        req->mask = htobe64(mask);
        req->val = htobe64(val);

	ulpsc = (struct ulptx_idata *)(req + 1);
	if (LEN__SET_TCB_FIELD_ULP % 16) {
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_NOOP));
		ulpsc->len = htobe32(0);
		return (ulpsc + 1);
	}
	return (ulpsc);
}

static inline void *
mk_rx_data_ack_ulp(struct ulp_txpkt *ulpmc, struct toepcb *toep)
{
	struct ulptx_idata *ulpsc;
	struct cpl_rx_data_ack_core *req;

	ulpmc->cmd_dest = htonl(V_ULPTX_CMD(ULP_TX_PKT) | V_ULP_TXPKT_DEST(0));
	ulpmc->len = htobe32(howmany(LEN__RX_DATA_ACK_ULP, 16));

	ulpsc = (struct ulptx_idata *)(ulpmc + 1);
	ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
	ulpsc->len = htobe32(sizeof(*req));

	req = (struct cpl_rx_data_ack_core *)(ulpsc + 1);
	OPCODE_TID(req) = htobe32(MK_OPCODE_TID(CPL_RX_DATA_ACK, toep->tid));
	req->credit_dack = htobe32(F_RX_MODULATE_RX);

	ulpsc = (struct ulptx_idata *)(req + 1);
	if (LEN__RX_DATA_ACK_ULP % 16) {
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_NOOP));
		ulpsc->len = htobe32(0);
		return (ulpsc + 1);
	}
	return (ulpsc);
}

static struct wrqe *
mk_update_tcb_for_ddp(struct adapter *sc, struct toepcb *toep, int db_idx,
    struct pageset *ps, int offset, uint64_t ddp_flags, uint64_t ddp_flags_mask)
{
	struct wrqe *wr;
	struct work_request_hdr *wrh;
	struct ulp_txpkt *ulpmc;
	int len;

	KASSERT(db_idx == 0 || db_idx == 1,
	    ("%s: bad DDP buffer index %d", __func__, db_idx));

	/*
	 * We'll send a compound work request that has 3 SET_TCB_FIELDs and an
	 * RX_DATA_ACK (with RX_MODULATE to speed up delivery).
	 *
	 * The work request header is 16B and always ends at a 16B boundary.
	 * The ULPTX master commands that follow must all end at 16B boundaries
	 * too so we round up the size to 16.
	 */
	len = sizeof(*wrh) + 3 * roundup2(LEN__SET_TCB_FIELD_ULP, 16) +
	    roundup2(LEN__RX_DATA_ACK_ULP, 16);

	wr = alloc_wrqe(len, toep->ctrlq);
	if (wr == NULL)
		return (NULL);
	wrh = wrtod(wr);
	INIT_ULPTX_WRH(wrh, len, 1, 0);	/* atomic */
	ulpmc = (struct ulp_txpkt *)(wrh + 1);

	/* Write the buffer's tag */
	ulpmc = mk_set_tcb_field_ulp(ulpmc, toep,
	    W_TCB_RX_DDP_BUF0_TAG + db_idx,
	    V_TCB_RX_DDP_BUF0_TAG(M_TCB_RX_DDP_BUF0_TAG),
	    V_TCB_RX_DDP_BUF0_TAG(ps->prsv.prsv_tag));

	/* Update the current offset in the DDP buffer and its total length */
	if (db_idx == 0)
		ulpmc = mk_set_tcb_field_ulp(ulpmc, toep,
		    W_TCB_RX_DDP_BUF0_OFFSET,
		    V_TCB_RX_DDP_BUF0_OFFSET(M_TCB_RX_DDP_BUF0_OFFSET) |
		    V_TCB_RX_DDP_BUF0_LEN(M_TCB_RX_DDP_BUF0_LEN),
		    V_TCB_RX_DDP_BUF0_OFFSET(offset) |
		    V_TCB_RX_DDP_BUF0_LEN(ps->len));
	else
		ulpmc = mk_set_tcb_field_ulp(ulpmc, toep,
		    W_TCB_RX_DDP_BUF1_OFFSET,
		    V_TCB_RX_DDP_BUF1_OFFSET(M_TCB_RX_DDP_BUF1_OFFSET) |
		    V_TCB_RX_DDP_BUF1_LEN((u64)M_TCB_RX_DDP_BUF1_LEN << 32),
		    V_TCB_RX_DDP_BUF1_OFFSET(offset) |
		    V_TCB_RX_DDP_BUF1_LEN((u64)ps->len << 32));

	/* Update DDP flags */
	ulpmc = mk_set_tcb_field_ulp(ulpmc, toep, W_TCB_RX_DDP_FLAGS,
	    ddp_flags_mask, ddp_flags);

	/* Gratuitous RX_DATA_ACK with RX_MODULATE set to speed up delivery. */
	ulpmc = mk_rx_data_ack_ulp(ulpmc, toep);

	return (wr);
}

static int
handle_ddp_data(struct toepcb *toep, __be32 ddp_report, __be32 rcv_nxt, int len)
{
	uint32_t report = be32toh(ddp_report);
	unsigned int db_idx;
	struct inpcb *inp = toep->inp;
	struct ddp_buffer *db;
	struct tcpcb *tp;
	struct socket *so;
	struct sockbuf *sb;
	struct kaiocb *job;
	long copied;

	db_idx = report & F_DDP_BUF_IDX ? 1 : 0;

	if (__predict_false(!(report & F_DDP_INV)))
		CXGBE_UNIMPLEMENTED("DDP buffer still valid");

	INP_WLOCK(inp);
	so = inp_inpcbtosocket(inp);
	sb = &so->so_rcv;
	DDP_LOCK(toep);

	KASSERT(toep->ddp.active_id == db_idx,
	    ("completed DDP buffer (%d) != active_id (%d) for tid %d", db_idx,
	    toep->ddp.active_id, toep->tid));
	db = &toep->ddp.db[db_idx];
	job = db->job;

	if (__predict_false(inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT))) {
		/*
		 * This can happen due to an administrative tcpdrop(8).
		 * Just fail the request with ECONNRESET.
		 */
		CTR5(KTR_CXGBE, "%s: tid %u, seq 0x%x, len %d, inp_flags 0x%x",
		    __func__, toep->tid, be32toh(rcv_nxt), len, inp->inp_flags);
		if (aio_clear_cancel_function(job))
			ddp_complete_one(job, ECONNRESET);
		goto completed;
	}

	tp = intotcpcb(inp);

	/*
	 * For RX_DDP_COMPLETE, len will be zero and rcv_nxt is the
	 * sequence number of the next byte to receive.  The length of
	 * the data received for this message must be computed by
	 * comparing the new and old values of rcv_nxt.
	 *
	 * For RX_DATA_DDP, len might be non-zero, but it is only the
	 * length of the most recent DMA.  It does not include the
	 * total length of the data received since the previous update
	 * for this DDP buffer.  rcv_nxt is the sequence number of the
	 * first received byte from the most recent DMA.
	 */
	len += be32toh(rcv_nxt) - tp->rcv_nxt;
	tp->rcv_nxt += len;
	tp->t_rcvtime = ticks;
#ifndef USE_DDP_RX_FLOW_CONTROL
	KASSERT(tp->rcv_wnd >= len, ("%s: negative window size", __func__));
	tp->rcv_wnd -= len;
#endif
#ifdef VERBOSE_TRACES
	CTR4(KTR_CXGBE, "%s: DDP[%d] placed %d bytes (%#x)", __func__, db_idx,
	    len, report);
#endif

	/* receive buffer autosize */
	MPASS(toep->vnet == so->so_vnet);
	CURVNET_SET(toep->vnet);
	SOCKBUF_LOCK(sb);
	if (sb->sb_flags & SB_AUTOSIZE &&
	    V_tcp_do_autorcvbuf &&
	    sb->sb_hiwat < V_tcp_autorcvbuf_max &&
	    len > (sbspace(sb) / 8 * 7)) {
		unsigned int hiwat = sb->sb_hiwat;
		unsigned int newsize = min(hiwat + V_tcp_autorcvbuf_inc,
		    V_tcp_autorcvbuf_max);

		if (!sbreserve_locked(sb, newsize, so, NULL))
			sb->sb_flags &= ~SB_AUTOSIZE;
		else
			toep->rx_credits += newsize - hiwat;
	}
	SOCKBUF_UNLOCK(sb);
	CURVNET_RESTORE();

#ifndef USE_DDP_RX_FLOW_CONTROL
	toep->rx_credits += len;
#endif

	job->msgrcv = 1;
	if (db->cancel_pending) {
		/*
		 * Update the job's length but defer completion to the
		 * TCB_RPL callback.
		 */
		job->aio_received += len;
		goto out;
	} else if (!aio_clear_cancel_function(job)) {
		/*
		 * Update the copied length for when
		 * t4_aio_cancel_active() completes this request.
		 */
		job->aio_received += len;
	} else {
		copied = job->aio_received;
#ifdef VERBOSE_TRACES
		CTR4(KTR_CXGBE, "%s: completing %p (copied %ld, placed %d)",
		    __func__, job, copied, len);
#endif
		aio_complete(job, copied + len, 0);
		t4_rcvd(&toep->td->tod, tp);
	}

completed:
	complete_ddp_buffer(toep, db, db_idx);
	if (toep->ddp.waiting_count > 0)
		ddp_queue_toep(toep);
out:
	DDP_UNLOCK(toep);
	INP_WUNLOCK(inp);

	return (0);
}

void
handle_ddp_indicate(struct toepcb *toep)
{

	DDP_ASSERT_LOCKED(toep);
	MPASS(toep->ddp.active_count == 0);
	MPASS((toep->ddp.flags & (DDP_BUF0_ACTIVE | DDP_BUF1_ACTIVE)) == 0);
	if (toep->ddp.waiting_count == 0) {
		/*
		 * The pending requests that triggered the request for an
		 * an indicate were cancelled.  Those cancels should have
		 * already disabled DDP.  Just ignore this as the data is
		 * going into the socket buffer anyway.
		 */
		return;
	}
	CTR3(KTR_CXGBE, "%s: tid %d indicated (%d waiting)", __func__,
	    toep->tid, toep->ddp.waiting_count);
	ddp_queue_toep(toep);
}

enum {
	DDP_BUF0_INVALIDATED = 0x2,
	DDP_BUF1_INVALIDATED
};

CTASSERT(DDP_BUF0_INVALIDATED == CPL_COOKIE_DDP0);

static int
do_ddp_tcb_rpl(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_set_tcb_rpl *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	unsigned int db_idx;
	struct toepcb *toep;
	struct inpcb *inp;
	struct ddp_buffer *db;
	struct kaiocb *job;
	long copied;

	if (cpl->status != CPL_ERR_NONE)
		panic("XXX: tcp_rpl failed: %d", cpl->status);

	toep = lookup_tid(sc, tid);
	inp = toep->inp;
	switch (cpl->cookie) {
	case V_WORD(W_TCB_RX_DDP_FLAGS) | V_COOKIE(DDP_BUF0_INVALIDATED):
	case V_WORD(W_TCB_RX_DDP_FLAGS) | V_COOKIE(DDP_BUF1_INVALIDATED):
		/*
		 * XXX: This duplicates a lot of code with handle_ddp_data().
		 */
		db_idx = G_COOKIE(cpl->cookie) - DDP_BUF0_INVALIDATED;
		MPASS(db_idx < nitems(toep->ddp.db));
		INP_WLOCK(inp);
		DDP_LOCK(toep);
		db = &toep->ddp.db[db_idx];

		/*
		 * handle_ddp_data() should leave the job around until
		 * this callback runs once a cancel is pending.
		 */
		MPASS(db != NULL);
		MPASS(db->job != NULL);
		MPASS(db->cancel_pending);

		/*
		 * XXX: It's not clear what happens if there is data
		 * placed when the buffer is invalidated.  I suspect we
		 * need to read the TCB to see how much data was placed.
		 *
		 * For now this just pretends like nothing was placed.
		 *
		 * XXX: Note that if we did check the PCB we would need to
		 * also take care of updating the tp, etc.
		 */
		job = db->job;
		copied = job->aio_received;
		if (copied == 0) {
			CTR2(KTR_CXGBE, "%s: cancelling %p", __func__, job);
			aio_cancel(job);
		} else {
			CTR3(KTR_CXGBE, "%s: completing %p (copied %ld)",
			    __func__, job, copied);
			aio_complete(job, copied, 0);
			t4_rcvd(&toep->td->tod, intotcpcb(inp));
		}

		complete_ddp_buffer(toep, db, db_idx);
		if (toep->ddp.waiting_count > 0)
			ddp_queue_toep(toep);
		DDP_UNLOCK(toep);
		INP_WUNLOCK(inp);
		break;
	default:
		panic("XXX: unknown tcb_rpl offset %#x, cookie %#x",
		    G_WORD(cpl->cookie), G_COOKIE(cpl->cookie));
	}

	return (0);
}

void
handle_ddp_close(struct toepcb *toep, struct tcpcb *tp, __be32 rcv_nxt)
{
	struct ddp_buffer *db;
	struct kaiocb *job;
	long copied;
	unsigned int db_flag, db_idx;
	int len, placed;

	INP_WLOCK_ASSERT(toep->inp);
	DDP_ASSERT_LOCKED(toep);
	len = be32toh(rcv_nxt) - tp->rcv_nxt;

	tp->rcv_nxt += len;
#ifndef USE_DDP_RX_FLOW_CONTROL
	toep->rx_credits += len;
#endif

	while (toep->ddp.active_count > 0) {
		MPASS(toep->ddp.active_id != -1);
		db_idx = toep->ddp.active_id;
		db_flag = db_idx == 1 ? DDP_BUF1_ACTIVE : DDP_BUF0_ACTIVE;
		MPASS((toep->ddp.flags & db_flag) != 0);
		db = &toep->ddp.db[db_idx];
		job = db->job;
		copied = job->aio_received;
		placed = len;
		if (placed > job->uaiocb.aio_nbytes - copied)
			placed = job->uaiocb.aio_nbytes - copied;
		if (placed > 0)
			job->msgrcv = 1;
		if (!aio_clear_cancel_function(job)) {
			/*
			 * Update the copied length for when
			 * t4_aio_cancel_active() completes this
			 * request.
			 */
			job->aio_received += placed;
		} else {
			CTR4(KTR_CXGBE, "%s: tid %d completed buf %d len %d",
			    __func__, toep->tid, db_idx, placed);
			aio_complete(job, copied + placed, 0);
		}
		len -= placed;
		complete_ddp_buffer(toep, db, db_idx);
	}

	MPASS(len == 0);
	ddp_complete_all(toep, 0);
}

#define DDP_ERR (F_DDP_PPOD_MISMATCH | F_DDP_LLIMIT_ERR | F_DDP_ULIMIT_ERR |\
	 F_DDP_PPOD_PARITY_ERR | F_DDP_PADDING_ERR | F_DDP_OFFSET_ERR |\
	 F_DDP_INVALID_TAG | F_DDP_COLOR_ERR | F_DDP_TID_MISMATCH |\
	 F_DDP_INVALID_PPOD | F_DDP_HDRCRC_ERR | F_DDP_DATACRC_ERR)

extern cpl_handler_t t4_cpl_handler[];

static int
do_rx_data_ddp(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_rx_data_ddp *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	uint32_t vld;
	struct toepcb *toep = lookup_tid(sc, tid);

	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));
	KASSERT(toep->tid == tid, ("%s: toep tid/atid mismatch", __func__));
	KASSERT(!(toep->flags & TPF_SYNQE),
	    ("%s: toep %p claims to be a synq entry", __func__, toep));

	vld = be32toh(cpl->ddpvld);
	if (__predict_false(vld & DDP_ERR)) {
		panic("%s: DDP error 0x%x (tid %d, toep %p)",
		    __func__, vld, tid, toep);
	}

	if (toep->ulp_mode == ULP_MODE_ISCSI) {
		t4_cpl_handler[CPL_RX_ISCSI_DDP](iq, rss, m);
		return (0);
	}

	handle_ddp_data(toep, cpl->u.ddp_report, cpl->seq, be16toh(cpl->len));

	return (0);
}

static int
do_rx_ddp_complete(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_rx_ddp_complete *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);

	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));
	KASSERT(toep->tid == tid, ("%s: toep tid/atid mismatch", __func__));
	KASSERT(!(toep->flags & TPF_SYNQE),
	    ("%s: toep %p claims to be a synq entry", __func__, toep));

	handle_ddp_data(toep, cpl->ddp_report, cpl->rcv_nxt, 0);

	return (0);
}

static void
enable_ddp(struct adapter *sc, struct toepcb *toep)
{

	KASSERT((toep->ddp.flags & (DDP_ON | DDP_OK | DDP_SC_REQ)) == DDP_OK,
	    ("%s: toep %p has bad ddp_flags 0x%x",
	    __func__, toep, toep->ddp.flags));

	CTR3(KTR_CXGBE, "%s: tid %u (time %u)",
	    __func__, toep->tid, time_uptime);

	DDP_ASSERT_LOCKED(toep);
	toep->ddp.flags |= DDP_SC_REQ;
	t4_set_tcb_field(sc, toep->ctrlq, toep, W_TCB_RX_DDP_FLAGS,
	    V_TF_DDP_OFF(1) | V_TF_DDP_INDICATE_OUT(1) |
	    V_TF_DDP_BUF0_INDICATE(1) | V_TF_DDP_BUF1_INDICATE(1) |
	    V_TF_DDP_BUF0_VALID(1) | V_TF_DDP_BUF1_VALID(1),
	    V_TF_DDP_BUF0_INDICATE(1) | V_TF_DDP_BUF1_INDICATE(1), 0, 0);
	t4_set_tcb_field(sc, toep->ctrlq, toep, W_TCB_T_FLAGS,
	    V_TF_RCV_COALESCE_ENABLE(1), 0, 0, 0);
}

static int
calculate_hcf(int n1, int n2)
{
	int a, b, t;

	if (n1 <= n2) {
		a = n1;
		b = n2;
	} else {
		a = n2;
		b = n1;
	}

	while (a != 0) {
		t = a;
		a = b % a;
		b = t;
	}

	return (b);
}

static inline int
pages_to_nppods(int npages, int ddp_page_shift)
{

	MPASS(ddp_page_shift >= PAGE_SHIFT);

	return (howmany(npages >> (ddp_page_shift - PAGE_SHIFT), PPOD_PAGES));
}

static int
alloc_page_pods(struct ppod_region *pr, u_int nppods, u_int pgsz_idx,
    struct ppod_reservation *prsv)
{
	vmem_addr_t addr;       /* relative to start of region */

	if (vmem_alloc(pr->pr_arena, PPOD_SZ(nppods), M_NOWAIT | M_FIRSTFIT,
	    &addr) != 0)
		return (ENOMEM);

	CTR5(KTR_CXGBE, "%-17s arena %p, addr 0x%08x, nppods %d, pgsz %d",
	    __func__, pr->pr_arena, (uint32_t)addr & pr->pr_tag_mask,
	    nppods, 1 << pr->pr_page_shift[pgsz_idx]);

	/*
	 * The hardware tagmask includes an extra invalid bit but the arena was
	 * seeded with valid values only.  An allocation out of this arena will
	 * fit inside the tagmask but won't have the invalid bit set.
	 */
	MPASS((addr & pr->pr_tag_mask) == addr);
	MPASS((addr & pr->pr_invalid_bit) == 0);

	prsv->prsv_pr = pr;
	prsv->prsv_tag = V_PPOD_PGSZ(pgsz_idx) | addr;
	prsv->prsv_nppods = nppods;

	return (0);
}

int
t4_alloc_page_pods_for_ps(struct ppod_region *pr, struct pageset *ps)
{
	int i, hcf, seglen, idx, nppods;
	struct ppod_reservation *prsv = &ps->prsv;

	KASSERT(prsv->prsv_nppods == 0,
	    ("%s: page pods already allocated", __func__));

	/*
	 * The DDP page size is unrelated to the VM page size.  We combine
	 * contiguous physical pages into larger segments to get the best DDP
	 * page size possible.  This is the largest of the four sizes in
	 * A_ULP_RX_TDDP_PSZ that evenly divides the HCF of the segment sizes in
	 * the page list.
	 */
	hcf = 0;
	for (i = 0; i < ps->npages; i++) {
		seglen = PAGE_SIZE;
		while (i < ps->npages - 1 &&
		    ps->pages[i]->phys_addr + PAGE_SIZE ==
		    ps->pages[i + 1]->phys_addr) {
			seglen += PAGE_SIZE;
			i++;
		}

		hcf = calculate_hcf(hcf, seglen);
		if (hcf < (1 << pr->pr_page_shift[1])) {
			idx = 0;
			goto have_pgsz;	/* give up, short circuit */
		}
	}

#define PR_PAGE_MASK(x) ((1 << pr->pr_page_shift[(x)]) - 1)
	MPASS((hcf & PR_PAGE_MASK(0)) == 0); /* PAGE_SIZE is >= 4K everywhere */
	for (idx = nitems(pr->pr_page_shift) - 1; idx > 0; idx--) {
		if ((hcf & PR_PAGE_MASK(idx)) == 0)
			break;
	}
#undef PR_PAGE_MASK

have_pgsz:
	MPASS(idx <= M_PPOD_PGSZ);

	nppods = pages_to_nppods(ps->npages, pr->pr_page_shift[idx]);
	if (alloc_page_pods(pr, nppods, idx, prsv) != 0)
		return (0);
	MPASS(prsv->prsv_nppods > 0);

	return (1);
}

int
t4_alloc_page_pods_for_buf(struct ppod_region *pr, vm_offset_t buf, int len,
    struct ppod_reservation *prsv)
{
	int hcf, seglen, idx, npages, nppods;
	uintptr_t start_pva, end_pva, pva, p1;

	MPASS(buf > 0);
	MPASS(len > 0);

	/*
	 * The DDP page size is unrelated to the VM page size.  We combine
	 * contiguous physical pages into larger segments to get the best DDP
	 * page size possible.  This is the largest of the four sizes in
	 * A_ULP_RX_ISCSI_PSZ that evenly divides the HCF of the segment sizes
	 * in the page list.
	 */
	hcf = 0;
	start_pva = trunc_page(buf);
	end_pva = trunc_page(buf + len - 1);
	pva = start_pva;
	while (pva <= end_pva) {
		seglen = PAGE_SIZE;
		p1 = pmap_kextract(pva);
		pva += PAGE_SIZE;
		while (pva <= end_pva && p1 + seglen == pmap_kextract(pva)) {
			seglen += PAGE_SIZE;
			pva += PAGE_SIZE;
		}

		hcf = calculate_hcf(hcf, seglen);
		if (hcf < (1 << pr->pr_page_shift[1])) {
			idx = 0;
			goto have_pgsz;	/* give up, short circuit */
		}
	}

#define PR_PAGE_MASK(x) ((1 << pr->pr_page_shift[(x)]) - 1)
	MPASS((hcf & PR_PAGE_MASK(0)) == 0); /* PAGE_SIZE is >= 4K everywhere */
	for (idx = nitems(pr->pr_page_shift) - 1; idx > 0; idx--) {
		if ((hcf & PR_PAGE_MASK(idx)) == 0)
			break;
	}
#undef PR_PAGE_MASK

have_pgsz:
	MPASS(idx <= M_PPOD_PGSZ);

	npages = 1;
	npages += (end_pva - start_pva) >> pr->pr_page_shift[idx];
	nppods = howmany(npages, PPOD_PAGES);
	if (alloc_page_pods(pr, nppods, idx, prsv) != 0)
		return (ENOMEM);
	MPASS(prsv->prsv_nppods > 0);

	return (0);
}

void
t4_free_page_pods(struct ppod_reservation *prsv)
{
	struct ppod_region *pr = prsv->prsv_pr;
	vmem_addr_t addr;

	MPASS(prsv != NULL);
	MPASS(prsv->prsv_nppods != 0);

	addr = prsv->prsv_tag & pr->pr_tag_mask;
	MPASS((addr & pr->pr_invalid_bit) == 0);

	CTR4(KTR_CXGBE, "%-17s arena %p, addr 0x%08x, nppods %d", __func__,
	    pr->pr_arena, addr, prsv->prsv_nppods);

	vmem_free(pr->pr_arena, addr, PPOD_SZ(prsv->prsv_nppods));
	prsv->prsv_nppods = 0;
}

#define NUM_ULP_TX_SC_IMM_PPODS (256 / PPOD_SIZE)

int
t4_write_page_pods_for_ps(struct adapter *sc, struct sge_wrq *wrq, int tid,
    struct pageset *ps)
{
	struct wrqe *wr;
	struct ulp_mem_io *ulpmc;
	struct ulptx_idata *ulpsc;
	struct pagepod *ppod;
	int i, j, k, n, chunk, len, ddp_pgsz, idx;
	u_int ppod_addr;
	uint32_t cmd;
	struct ppod_reservation *prsv = &ps->prsv;
	struct ppod_region *pr = prsv->prsv_pr;

	KASSERT(!(ps->flags & PS_PPODS_WRITTEN),
	    ("%s: page pods already written", __func__));
	MPASS(prsv->prsv_nppods > 0);

	cmd = htobe32(V_ULPTX_CMD(ULP_TX_MEM_WRITE));
	if (is_t4(sc))
		cmd |= htobe32(F_ULP_MEMIO_ORDER);
	else
		cmd |= htobe32(F_T5_ULP_MEMIO_IMM);
	ddp_pgsz = 1 << pr->pr_page_shift[G_PPOD_PGSZ(prsv->prsv_tag)];
	ppod_addr = pr->pr_start + (prsv->prsv_tag & pr->pr_tag_mask);
	for (i = 0; i < prsv->prsv_nppods; ppod_addr += chunk) {

		/* How many page pods are we writing in this cycle */
		n = min(prsv->prsv_nppods - i, NUM_ULP_TX_SC_IMM_PPODS);
		chunk = PPOD_SZ(n);
		len = roundup2(sizeof(*ulpmc) + sizeof(*ulpsc) + chunk, 16);

		wr = alloc_wrqe(len, wrq);
		if (wr == NULL)
			return (ENOMEM);	/* ok to just bail out */
		ulpmc = wrtod(wr);

		INIT_ULPTX_WR(ulpmc, len, 0, 0);
		ulpmc->cmd = cmd;
		ulpmc->dlen = htobe32(V_ULP_MEMIO_DATA_LEN(chunk / 32));
		ulpmc->len16 = htobe32(howmany(len - sizeof(ulpmc->wr), 16));
		ulpmc->lock_addr = htobe32(V_ULP_MEMIO_ADDR(ppod_addr >> 5));

		ulpsc = (struct ulptx_idata *)(ulpmc + 1);
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
		ulpsc->len = htobe32(chunk);

		ppod = (struct pagepod *)(ulpsc + 1);
		for (j = 0; j < n; i++, j++, ppod++) {
			ppod->vld_tid_pgsz_tag_color = htobe64(F_PPOD_VALID |
			    V_PPOD_TID(tid) | prsv->prsv_tag);
			ppod->len_offset = htobe64(V_PPOD_LEN(ps->len) |
			    V_PPOD_OFST(ps->offset));
			ppod->rsvd = 0;
			idx = i * PPOD_PAGES * (ddp_pgsz / PAGE_SIZE);
			for (k = 0; k < nitems(ppod->addr); k++) {
				if (idx < ps->npages) {
					ppod->addr[k] =
					    htobe64(ps->pages[idx]->phys_addr);
					idx += ddp_pgsz / PAGE_SIZE;
				} else
					ppod->addr[k] = 0;
#if 0
				CTR5(KTR_CXGBE,
				    "%s: tid %d ppod[%d]->addr[%d] = %p",
				    __func__, toep->tid, i, k,
				    htobe64(ppod->addr[k]));
#endif
			}

		}

		t4_wrq_tx(sc, wr);
	}
	ps->flags |= PS_PPODS_WRITTEN;

	return (0);
}

int
t4_write_page_pods_for_buf(struct adapter *sc, struct sge_wrq *wrq, int tid,
    struct ppod_reservation *prsv, vm_offset_t buf, int buflen)
{
	struct wrqe *wr;
	struct ulp_mem_io *ulpmc;
	struct ulptx_idata *ulpsc;
	struct pagepod *ppod;
	int i, j, k, n, chunk, len, ddp_pgsz;
	u_int ppod_addr, offset;
	uint32_t cmd;
	struct ppod_region *pr = prsv->prsv_pr;
	uintptr_t end_pva, pva, pa;

	cmd = htobe32(V_ULPTX_CMD(ULP_TX_MEM_WRITE));
	if (is_t4(sc))
		cmd |= htobe32(F_ULP_MEMIO_ORDER);
	else
		cmd |= htobe32(F_T5_ULP_MEMIO_IMM);
	ddp_pgsz = 1 << pr->pr_page_shift[G_PPOD_PGSZ(prsv->prsv_tag)];
	offset = buf & PAGE_MASK;
	ppod_addr = pr->pr_start + (prsv->prsv_tag & pr->pr_tag_mask);
	pva = trunc_page(buf);
	end_pva = trunc_page(buf + buflen - 1);
	for (i = 0; i < prsv->prsv_nppods; ppod_addr += chunk) {

		/* How many page pods are we writing in this cycle */
		n = min(prsv->prsv_nppods - i, NUM_ULP_TX_SC_IMM_PPODS);
		MPASS(n > 0);
		chunk = PPOD_SZ(n);
		len = roundup2(sizeof(*ulpmc) + sizeof(*ulpsc) + chunk, 16);

		wr = alloc_wrqe(len, wrq);
		if (wr == NULL)
			return (ENOMEM);	/* ok to just bail out */
		ulpmc = wrtod(wr);

		INIT_ULPTX_WR(ulpmc, len, 0, 0);
		ulpmc->cmd = cmd;
		ulpmc->dlen = htobe32(V_ULP_MEMIO_DATA_LEN(chunk / 32));
		ulpmc->len16 = htobe32(howmany(len - sizeof(ulpmc->wr), 16));
		ulpmc->lock_addr = htobe32(V_ULP_MEMIO_ADDR(ppod_addr >> 5));

		ulpsc = (struct ulptx_idata *)(ulpmc + 1);
		ulpsc->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
		ulpsc->len = htobe32(chunk);

		ppod = (struct pagepod *)(ulpsc + 1);
		for (j = 0; j < n; i++, j++, ppod++) {
			ppod->vld_tid_pgsz_tag_color = htobe64(F_PPOD_VALID |
			    V_PPOD_TID(tid) |
			    (prsv->prsv_tag & ~V_PPOD_PGSZ(M_PPOD_PGSZ)));
			ppod->len_offset = htobe64(V_PPOD_LEN(buflen) |
			    V_PPOD_OFST(offset));
			ppod->rsvd = 0;

			for (k = 0; k < nitems(ppod->addr); k++) {
				if (pva > end_pva)
					ppod->addr[k] = 0;
				else {
					pa = pmap_kextract(pva);
					ppod->addr[k] = htobe64(pa);
					pva += ddp_pgsz;
				}
#if 0
				CTR5(KTR_CXGBE,
				    "%s: tid %d ppod[%d]->addr[%d] = %p",
				    __func__, tid, i, k,
				    htobe64(ppod->addr[k]));
#endif
			}

			/*
			 * Walk back 1 segment so that the first address in the
			 * next pod is the same as the last one in the current
			 * pod.
			 */
			pva -= ddp_pgsz;
		}

		t4_wrq_tx(sc, wr);
	}

	MPASS(pva <= end_pva);

	return (0);
}

static void
wire_pageset(struct pageset *ps)
{
	vm_page_t p;
	int i;

	KASSERT(!(ps->flags & PS_WIRED), ("pageset already wired"));

	for (i = 0; i < ps->npages; i++) {
		p = ps->pages[i];
		vm_page_lock(p);
		vm_page_wire(p);
		vm_page_unhold(p);
		vm_page_unlock(p);
	}
	ps->flags |= PS_WIRED;
}

/*
 * Prepare a pageset for DDP.  This wires the pageset and sets up page
 * pods.
 */
static int
prep_pageset(struct adapter *sc, struct toepcb *toep, struct pageset *ps)
{
	struct tom_data *td = sc->tom_softc;

	if (!(ps->flags & PS_WIRED))
		wire_pageset(ps);
	if (ps->prsv.prsv_nppods == 0 &&
	    !t4_alloc_page_pods_for_ps(&td->pr, ps)) {
		return (0);
	}
	if (!(ps->flags & PS_PPODS_WRITTEN) &&
	    t4_write_page_pods_for_ps(sc, toep->ctrlq, toep->tid, ps) != 0) {
		return (0);
	}

	return (1);
}

int
t4_init_ppod_region(struct ppod_region *pr, struct t4_range *r, u_int psz,
    const char *name)
{
	int i;

	MPASS(pr != NULL);
	MPASS(r->size > 0);

	pr->pr_start = r->start;
	pr->pr_len = r->size;
	pr->pr_page_shift[0] = 12 + G_HPZ0(psz);
	pr->pr_page_shift[1] = 12 + G_HPZ1(psz);
	pr->pr_page_shift[2] = 12 + G_HPZ2(psz);
	pr->pr_page_shift[3] = 12 + G_HPZ3(psz);

	/* The SGL -> page pod algorithm requires the sizes to be in order. */
	for (i = 1; i < nitems(pr->pr_page_shift); i++) {
		if (pr->pr_page_shift[i] <= pr->pr_page_shift[i - 1])
			return (ENXIO);
	}

	pr->pr_tag_mask = ((1 << fls(r->size)) - 1) & V_PPOD_TAG(M_PPOD_TAG);
	pr->pr_alias_mask = V_PPOD_TAG(M_PPOD_TAG) & ~pr->pr_tag_mask;
	if (pr->pr_tag_mask == 0 || pr->pr_alias_mask == 0)
		return (ENXIO);
	pr->pr_alias_shift = fls(pr->pr_tag_mask);
	pr->pr_invalid_bit = 1 << (pr->pr_alias_shift - 1);

	pr->pr_arena = vmem_create(name, 0, pr->pr_len, PPOD_SIZE, 0,
	    M_FIRSTFIT | M_NOWAIT);
	if (pr->pr_arena == NULL)
		return (ENOMEM);

	return (0);
}

void
t4_free_ppod_region(struct ppod_region *pr)
{

	MPASS(pr != NULL);

	if (pr->pr_arena)
		vmem_destroy(pr->pr_arena);
	bzero(pr, sizeof(*pr));
}

static int
pscmp(struct pageset *ps, struct vmspace *vm, vm_offset_t start, int npages,
    int pgoff, int len)
{

	if (ps->start != start || ps->npages != npages ||
	    ps->offset != pgoff || ps->len != len)
		return (1);

	return (ps->vm != vm || ps->vm_timestamp != vm->vm_map.timestamp);
}

static int
hold_aio(struct toepcb *toep, struct kaiocb *job, struct pageset **pps)
{
	struct vmspace *vm;
	vm_map_t map;
	vm_offset_t start, end, pgoff;
	struct pageset *ps;
	int n;

	DDP_ASSERT_LOCKED(toep);

	/*
	 * The AIO subsystem will cancel and drain all requests before
	 * permitting a process to exit or exec, so p_vmspace should
	 * be stable here.
	 */
	vm = job->userproc->p_vmspace;
	map = &vm->vm_map;
	start = (uintptr_t)job->uaiocb.aio_buf;
	pgoff = start & PAGE_MASK;
	end = round_page(start + job->uaiocb.aio_nbytes);
	start = trunc_page(start);

	if (end - start > MAX_DDP_BUFFER_SIZE) {
		/*
		 * Truncate the request to a short read.
		 * Alternatively, we could DDP in chunks to the larger
		 * buffer, but that would be quite a bit more work.
		 *
		 * When truncating, round the request down to avoid
		 * crossing a cache line on the final transaction.
		 */
		end = rounddown2(start + MAX_DDP_BUFFER_SIZE, CACHE_LINE_SIZE);
#ifdef VERBOSE_TRACES
		CTR4(KTR_CXGBE, "%s: tid %d, truncating size from %lu to %lu",
		    __func__, toep->tid, (unsigned long)job->uaiocb.aio_nbytes,
		    (unsigned long)(end - (start + pgoff)));
		job->uaiocb.aio_nbytes = end - (start + pgoff);
#endif
		end = round_page(end);
	}

	n = atop(end - start);

	/*
	 * Try to reuse a cached pageset.
	 */
	TAILQ_FOREACH(ps, &toep->ddp.cached_pagesets, link) {
		if (pscmp(ps, vm, start, n, pgoff,
		    job->uaiocb.aio_nbytes) == 0) {
			TAILQ_REMOVE(&toep->ddp.cached_pagesets, ps, link);
			toep->ddp.cached_count--;
			*pps = ps;
			return (0);
		}
	}

	/*
	 * If there are too many cached pagesets to create a new one,
	 * free a pageset before creating a new one.
	 */
	KASSERT(toep->ddp.active_count + toep->ddp.cached_count <=
	    nitems(toep->ddp.db), ("%s: too many wired pagesets", __func__));
	if (toep->ddp.active_count + toep->ddp.cached_count ==
	    nitems(toep->ddp.db)) {
		KASSERT(toep->ddp.cached_count > 0,
		    ("no cached pageset to free"));
		ps = TAILQ_LAST(&toep->ddp.cached_pagesets, pagesetq);
		TAILQ_REMOVE(&toep->ddp.cached_pagesets, ps, link);
		toep->ddp.cached_count--;
		free_pageset(toep->td, ps);
	}
	DDP_UNLOCK(toep);

	/* Create a new pageset. */
	ps = malloc(sizeof(*ps) + n * sizeof(vm_page_t), M_CXGBE, M_WAITOK |
	    M_ZERO);
	ps->pages = (vm_page_t *)(ps + 1);
	ps->vm_timestamp = map->timestamp;
	ps->npages = vm_fault_quick_hold_pages(map, start, end - start,
	    VM_PROT_WRITE, ps->pages, n);

	DDP_LOCK(toep);
	if (ps->npages < 0) {
		free(ps, M_CXGBE);
		return (EFAULT);
	}

	KASSERT(ps->npages == n, ("hold_aio: page count mismatch: %d vs %d",
	    ps->npages, n));

	ps->offset = pgoff;
	ps->len = job->uaiocb.aio_nbytes;
	atomic_add_int(&vm->vm_refcnt, 1);
	ps->vm = vm;
	ps->start = start;

	CTR5(KTR_CXGBE, "%s: tid %d, new pageset %p for job %p, npages %d",
	    __func__, toep->tid, ps, job, ps->npages);
	*pps = ps;
	return (0);
}

static void
ddp_complete_all(struct toepcb *toep, int error)
{
	struct kaiocb *job;

	DDP_ASSERT_LOCKED(toep);
	while (!TAILQ_EMPTY(&toep->ddp.aiojobq)) {
		job = TAILQ_FIRST(&toep->ddp.aiojobq);
		TAILQ_REMOVE(&toep->ddp.aiojobq, job, list);
		toep->ddp.waiting_count--;
		if (aio_clear_cancel_function(job))
			ddp_complete_one(job, error);
	}
}

static void
aio_ddp_cancel_one(struct kaiocb *job)
{
	long copied;

	/*
	 * If this job had copied data out of the socket buffer before
	 * it was cancelled, report it as a short read rather than an
	 * error.
	 */
	copied = job->aio_received;
	if (copied != 0)
		aio_complete(job, copied, 0);
	else
		aio_cancel(job);
}

/*
 * Called when the main loop wants to requeue a job to retry it later.
 * Deals with the race of the job being cancelled while it was being
 * examined.
 */
static void
aio_ddp_requeue_one(struct toepcb *toep, struct kaiocb *job)
{

	DDP_ASSERT_LOCKED(toep);
	if (!(toep->ddp.flags & DDP_DEAD) &&
	    aio_set_cancel_function(job, t4_aio_cancel_queued)) {
		TAILQ_INSERT_HEAD(&toep->ddp.aiojobq, job, list);
		toep->ddp.waiting_count++;
	} else
		aio_ddp_cancel_one(job);
}

static void
aio_ddp_requeue(struct toepcb *toep)
{
	struct adapter *sc = td_adapter(toep->td);
	struct socket *so;
	struct sockbuf *sb;
	struct inpcb *inp;
	struct kaiocb *job;
	struct ddp_buffer *db;
	size_t copied, offset, resid;
	struct pageset *ps;
	struct mbuf *m;
	uint64_t ddp_flags, ddp_flags_mask;
	struct wrqe *wr;
	int buf_flag, db_idx, error;

	DDP_ASSERT_LOCKED(toep);

restart:
	if (toep->ddp.flags & DDP_DEAD) {
		MPASS(toep->ddp.waiting_count == 0);
		MPASS(toep->ddp.active_count == 0);
		return;
	}

	if (toep->ddp.waiting_count == 0 ||
	    toep->ddp.active_count == nitems(toep->ddp.db)) {
		return;
	}

	job = TAILQ_FIRST(&toep->ddp.aiojobq);
	so = job->fd_file->f_data;
	sb = &so->so_rcv;
	SOCKBUF_LOCK(sb);

	/* We will never get anything unless we are or were connected. */
	if (!(so->so_state & (SS_ISCONNECTED|SS_ISDISCONNECTED))) {
		SOCKBUF_UNLOCK(sb);
		ddp_complete_all(toep, ENOTCONN);
		return;
	}

	KASSERT(toep->ddp.active_count == 0 || sbavail(sb) == 0,
	    ("%s: pending sockbuf data and DDP is active", __func__));

	/* Abort if socket has reported problems. */
	/* XXX: Wait for any queued DDP's to finish and/or flush them? */
	if (so->so_error && sbavail(sb) == 0) {
		toep->ddp.waiting_count--;
		TAILQ_REMOVE(&toep->ddp.aiojobq, job, list);
		if (!aio_clear_cancel_function(job)) {
			SOCKBUF_UNLOCK(sb);
			goto restart;
		}

		/*
		 * If this job has previously copied some data, report
		 * a short read and leave the error to be reported by
		 * a future request.
		 */
		copied = job->aio_received;
		if (copied != 0) {
			SOCKBUF_UNLOCK(sb);
			aio_complete(job, copied, 0);
			goto restart;
		}
		error = so->so_error;
		so->so_error = 0;
		SOCKBUF_UNLOCK(sb);
		aio_complete(job, -1, error);
		goto restart;
	}

	/*
	 * Door is closed.  If there is pending data in the socket buffer,
	 * deliver it.  If there are pending DDP requests, wait for those
	 * to complete.  Once they have completed, return EOF reads.
	 */
	if (sb->sb_state & SBS_CANTRCVMORE && sbavail(sb) == 0) {
		SOCKBUF_UNLOCK(sb);
		if (toep->ddp.active_count != 0)
			return;
		ddp_complete_all(toep, 0);
		return;
	}

	/*
	 * If DDP is not enabled and there is no pending socket buffer
	 * data, try to enable DDP.
	 */
	if (sbavail(sb) == 0 && (toep->ddp.flags & DDP_ON) == 0) {
		SOCKBUF_UNLOCK(sb);

		/*
		 * Wait for the card to ACK that DDP is enabled before
		 * queueing any buffers.  Currently this waits for an
		 * indicate to arrive.  This could use a TCB_SET_FIELD_RPL
		 * message to know that DDP was enabled instead of waiting
		 * for the indicate which would avoid copying the indicate
		 * if no data is pending.
		 *
		 * XXX: Might want to limit the indicate size to the size
		 * of the first queued request.
		 */
		if ((toep->ddp.flags & DDP_SC_REQ) == 0)
			enable_ddp(sc, toep);
		return;
	}
	SOCKBUF_UNLOCK(sb);

	/*
	 * If another thread is queueing a buffer for DDP, let it
	 * drain any work and return.
	 */
	if (toep->ddp.queueing != NULL)
		return;

	/* Take the next job to prep it for DDP. */
	toep->ddp.waiting_count--;
	TAILQ_REMOVE(&toep->ddp.aiojobq, job, list);
	if (!aio_clear_cancel_function(job))
		goto restart;
	toep->ddp.queueing = job;

	/* NB: This drops DDP_LOCK while it holds the backing VM pages. */
	error = hold_aio(toep, job, &ps);
	if (error != 0) {
		ddp_complete_one(job, error);
		toep->ddp.queueing = NULL;
		goto restart;
	}

	SOCKBUF_LOCK(sb);
	if (so->so_error && sbavail(sb) == 0) {
		copied = job->aio_received;
		if (copied != 0) {
			SOCKBUF_UNLOCK(sb);
			recycle_pageset(toep, ps);
			aio_complete(job, copied, 0);
			toep->ddp.queueing = NULL;
			goto restart;
		}

		error = so->so_error;
		so->so_error = 0;
		SOCKBUF_UNLOCK(sb);
		recycle_pageset(toep, ps);
		aio_complete(job, -1, error);
		toep->ddp.queueing = NULL;
		goto restart;
	}

	if (sb->sb_state & SBS_CANTRCVMORE && sbavail(sb) == 0) {
		SOCKBUF_UNLOCK(sb);
		recycle_pageset(toep, ps);
		if (toep->ddp.active_count != 0) {
			/*
			 * The door is closed, but there are still pending
			 * DDP buffers.  Requeue.  These jobs will all be
			 * completed once those buffers drain.
			 */
			aio_ddp_requeue_one(toep, job);
			toep->ddp.queueing = NULL;
			return;
		}
		ddp_complete_one(job, 0);
		ddp_complete_all(toep, 0);
		toep->ddp.queueing = NULL;
		return;
	}

sbcopy:
	/*
	 * If the toep is dead, there shouldn't be any data in the socket
	 * buffer, so the above case should have handled this.
	 */
	MPASS(!(toep->ddp.flags & DDP_DEAD));

	/*
	 * If there is pending data in the socket buffer (either
	 * from before the requests were queued or a DDP indicate),
	 * copy those mbufs out directly.
	 */
	copied = 0;
	offset = ps->offset + job->aio_received;
	MPASS(job->aio_received <= job->uaiocb.aio_nbytes);
	resid = job->uaiocb.aio_nbytes - job->aio_received;
	m = sb->sb_mb;
	KASSERT(m == NULL || toep->ddp.active_count == 0,
	    ("%s: sockbuf data with active DDP", __func__));
	while (m != NULL && resid > 0) {
		struct iovec iov[1];
		struct uio uio;
		int error;

		iov[0].iov_base = mtod(m, void *);
		iov[0].iov_len = m->m_len;
		if (iov[0].iov_len > resid)
			iov[0].iov_len = resid;
		uio.uio_iov = iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = 0;
		uio.uio_resid = iov[0].iov_len;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_rw = UIO_WRITE;
		error = uiomove_fromphys(ps->pages, offset + copied,
		    uio.uio_resid, &uio);
		MPASS(error == 0 && uio.uio_resid == 0);
		copied += uio.uio_offset;
		resid -= uio.uio_offset;
		m = m->m_next;
	}
	if (copied != 0) {
		sbdrop_locked(sb, copied);
		job->aio_received += copied;
		job->msgrcv = 1;
		copied = job->aio_received;
		inp = sotoinpcb(so);
		if (!INP_TRY_WLOCK(inp)) {
			/*
			 * The reference on the socket file descriptor in
			 * the AIO job should keep 'sb' and 'inp' stable.
			 * Our caller has a reference on the 'toep' that
			 * keeps it stable.
			 */
			SOCKBUF_UNLOCK(sb);
			DDP_UNLOCK(toep);
			INP_WLOCK(inp);
			DDP_LOCK(toep);
			SOCKBUF_LOCK(sb);

			/*
			 * If the socket has been closed, we should detect
			 * that and complete this request if needed on
			 * the next trip around the loop.
			 */
		}
		t4_rcvd_locked(&toep->td->tod, intotcpcb(inp));
		INP_WUNLOCK(inp);
		if (resid == 0 || toep->ddp.flags & DDP_DEAD) {
			/*
			 * We filled the entire buffer with socket
			 * data, DDP is not being used, or the socket
			 * is being shut down, so complete the
			 * request.
			 */
			SOCKBUF_UNLOCK(sb);
			recycle_pageset(toep, ps);
			aio_complete(job, copied, 0);
			toep->ddp.queueing = NULL;
			goto restart;
		}

		/*
		 * If DDP is not enabled, requeue this request and restart.
		 * This will either enable DDP or wait for more data to
		 * arrive on the socket buffer.
		 */
		if ((toep->ddp.flags & (DDP_ON | DDP_SC_REQ)) != DDP_ON) {
			SOCKBUF_UNLOCK(sb);
			recycle_pageset(toep, ps);
			aio_ddp_requeue_one(toep, job);
			toep->ddp.queueing = NULL;
			goto restart;
		}

		/*
		 * An indicate might have arrived and been added to
		 * the socket buffer while it was unlocked after the
		 * copy to lock the INP.  If so, restart the copy.
		 */
		if (sbavail(sb) != 0)
			goto sbcopy;
	}
	SOCKBUF_UNLOCK(sb);

	if (prep_pageset(sc, toep, ps) == 0) {
		recycle_pageset(toep, ps);
		aio_ddp_requeue_one(toep, job);
		toep->ddp.queueing = NULL;

		/*
		 * XXX: Need to retry this later.  Mostly need a trigger
		 * when page pods are freed up.
		 */
		printf("%s: prep_pageset failed\n", __func__);
		return;
	}

	/* Determine which DDP buffer to use. */
	if (toep->ddp.db[0].job == NULL) {
		db_idx = 0;
	} else {
		MPASS(toep->ddp.db[1].job == NULL);
		db_idx = 1;
	}

	ddp_flags = 0;
	ddp_flags_mask = 0;
	if (db_idx == 0) {
		ddp_flags |= V_TF_DDP_BUF0_VALID(1);
		if (so->so_state & SS_NBIO)
			ddp_flags |= V_TF_DDP_BUF0_FLUSH(1);
		ddp_flags_mask |= V_TF_DDP_PSH_NO_INVALIDATE0(1) |
		    V_TF_DDP_PUSH_DISABLE_0(1) | V_TF_DDP_PSHF_ENABLE_0(1) |
		    V_TF_DDP_BUF0_FLUSH(1) | V_TF_DDP_BUF0_VALID(1);
		buf_flag = DDP_BUF0_ACTIVE;
	} else {
		ddp_flags |= V_TF_DDP_BUF1_VALID(1);
		if (so->so_state & SS_NBIO)
			ddp_flags |= V_TF_DDP_BUF1_FLUSH(1);
		ddp_flags_mask |= V_TF_DDP_PSH_NO_INVALIDATE1(1) |
		    V_TF_DDP_PUSH_DISABLE_1(1) | V_TF_DDP_PSHF_ENABLE_1(1) |
		    V_TF_DDP_BUF1_FLUSH(1) | V_TF_DDP_BUF1_VALID(1);
		buf_flag = DDP_BUF1_ACTIVE;
	}
	MPASS((toep->ddp.flags & buf_flag) == 0);
	if ((toep->ddp.flags & (DDP_BUF0_ACTIVE | DDP_BUF1_ACTIVE)) == 0) {
		MPASS(db_idx == 0);
		MPASS(toep->ddp.active_id == -1);
		MPASS(toep->ddp.active_count == 0);
		ddp_flags_mask |= V_TF_DDP_ACTIVE_BUF(1);
	}

	/*
	 * The TID for this connection should still be valid.  If DDP_DEAD
	 * is set, SBS_CANTRCVMORE should be set, so we shouldn't be
	 * this far anyway.  Even if the socket is closing on the other
	 * end, the AIO job holds a reference on this end of the socket
	 * which will keep it open and keep the TCP PCB attached until
	 * after the job is completed.
	 */
	wr = mk_update_tcb_for_ddp(sc, toep, db_idx, ps, job->aio_received,
	    ddp_flags, ddp_flags_mask);
	if (wr == NULL) {
		recycle_pageset(toep, ps);
		aio_ddp_requeue_one(toep, job);
		toep->ddp.queueing = NULL;

		/*
		 * XXX: Need a way to kick a retry here.
		 *
		 * XXX: We know the fixed size needed and could
		 * preallocate this using a blocking request at the
		 * start of the task to avoid having to handle this
		 * edge case.
		 */
		printf("%s: mk_update_tcb_for_ddp failed\n", __func__);
		return;
	}

	if (!aio_set_cancel_function(job, t4_aio_cancel_active)) {
		free_wrqe(wr);
		recycle_pageset(toep, ps);
		aio_ddp_cancel_one(job);
		toep->ddp.queueing = NULL;
		goto restart;
	}

#ifdef VERBOSE_TRACES
	CTR5(KTR_CXGBE, "%s: scheduling %p for DDP[%d] (flags %#lx/%#lx)",
	    __func__, job, db_idx, ddp_flags, ddp_flags_mask);
#endif
	/* Give the chip the go-ahead. */
	t4_wrq_tx(sc, wr);
	db = &toep->ddp.db[db_idx];
	db->cancel_pending = 0;
	db->job = job;
	db->ps = ps;
	toep->ddp.queueing = NULL;
	toep->ddp.flags |= buf_flag;
	toep->ddp.active_count++;
	if (toep->ddp.active_count == 1) {
		MPASS(toep->ddp.active_id == -1);
		toep->ddp.active_id = db_idx;
		CTR2(KTR_CXGBE, "%s: ddp_active_id = %d", __func__,
		    toep->ddp.active_id);
	}
	goto restart;
}

void
ddp_queue_toep(struct toepcb *toep)
{

	DDP_ASSERT_LOCKED(toep);
	if (toep->ddp.flags & DDP_TASK_ACTIVE)
		return;
	toep->ddp.flags |= DDP_TASK_ACTIVE;
	hold_toepcb(toep);
	soaio_enqueue(&toep->ddp.requeue_task);
}

static void
aio_ddp_requeue_task(void *context, int pending)
{
	struct toepcb *toep = context;

	DDP_LOCK(toep);
	aio_ddp_requeue(toep);
	toep->ddp.flags &= ~DDP_TASK_ACTIVE;
	DDP_UNLOCK(toep);

	free_toepcb(toep);
}

static void
t4_aio_cancel_active(struct kaiocb *job)
{
	struct socket *so = job->fd_file->f_data;
	struct tcpcb *tp = so_sototcpcb(so);
	struct toepcb *toep = tp->t_toe;
	struct adapter *sc = td_adapter(toep->td);
	uint64_t valid_flag;
	int i;

	DDP_LOCK(toep);
	if (aio_cancel_cleared(job)) {
		DDP_UNLOCK(toep);
		aio_ddp_cancel_one(job);
		return;
	}

	for (i = 0; i < nitems(toep->ddp.db); i++) {
		if (toep->ddp.db[i].job == job) {
			/* Should only ever get one cancel request for a job. */
			MPASS(toep->ddp.db[i].cancel_pending == 0);

			/*
			 * Invalidate this buffer.  It will be
			 * cancelled or partially completed once the
			 * card ACKs the invalidate.
			 */
			valid_flag = i == 0 ? V_TF_DDP_BUF0_VALID(1) :
			    V_TF_DDP_BUF1_VALID(1);
			t4_set_tcb_field(sc, toep->ctrlq, toep,
			    W_TCB_RX_DDP_FLAGS, valid_flag, 0, 1,
			    i + DDP_BUF0_INVALIDATED);
			toep->ddp.db[i].cancel_pending = 1;
			CTR2(KTR_CXGBE, "%s: request %p marked pending",
			    __func__, job);
			break;
		}
	}
	DDP_UNLOCK(toep);
}

static void
t4_aio_cancel_queued(struct kaiocb *job)
{
	struct socket *so = job->fd_file->f_data;
	struct tcpcb *tp = so_sototcpcb(so);
	struct toepcb *toep = tp->t_toe;

	DDP_LOCK(toep);
	if (!aio_cancel_cleared(job)) {
		TAILQ_REMOVE(&toep->ddp.aiojobq, job, list);
		toep->ddp.waiting_count--;
		if (toep->ddp.waiting_count == 0)
			ddp_queue_toep(toep);
	}
	CTR2(KTR_CXGBE, "%s: request %p cancelled", __func__, job);
	DDP_UNLOCK(toep);

	aio_ddp_cancel_one(job);
}

int
t4_aio_queue_ddp(struct socket *so, struct kaiocb *job)
{
	struct tcpcb *tp = so_sototcpcb(so);
	struct toepcb *toep = tp->t_toe;


	/* Ignore writes. */
	if (job->uaiocb.aio_lio_opcode != LIO_READ)
		return (EOPNOTSUPP);

	DDP_LOCK(toep);

	/*
	 * XXX: Think about possibly returning errors for ENOTCONN,
	 * etc.  Perhaps the caller would only queue the request
	 * if it failed with EOPNOTSUPP?
	 */

#ifdef VERBOSE_TRACES
	CTR2(KTR_CXGBE, "%s: queueing %p", __func__, job);
#endif
	if (!aio_set_cancel_function(job, t4_aio_cancel_queued))
		panic("new job was cancelled");
	TAILQ_INSERT_TAIL(&toep->ddp.aiojobq, job, list);
	toep->ddp.waiting_count++;
	toep->ddp.flags |= DDP_OK;

	/*
	 * Try to handle this request synchronously.  If this has
	 * to block because the task is running, it will just bail
	 * and let the task handle it instead.
	 */
	aio_ddp_requeue(toep);
	DDP_UNLOCK(toep);
	return (0);
}

void
t4_ddp_mod_load(void)
{

	t4_register_shared_cpl_handler(CPL_SET_TCB_RPL, do_ddp_tcb_rpl,
	    CPL_COOKIE_DDP0);
	t4_register_shared_cpl_handler(CPL_SET_TCB_RPL, do_ddp_tcb_rpl,
	    CPL_COOKIE_DDP1);
	t4_register_cpl_handler(CPL_RX_DATA_DDP, do_rx_data_ddp);
	t4_register_cpl_handler(CPL_RX_DDP_COMPLETE, do_rx_ddp_complete);
	TAILQ_INIT(&ddp_orphan_pagesets);
	mtx_init(&ddp_orphan_pagesets_lock, "ddp orphans", NULL, MTX_DEF);
	TASK_INIT(&ddp_orphan_task, 0, ddp_free_orphan_pagesets, NULL);
}

void
t4_ddp_mod_unload(void)
{

	taskqueue_drain(taskqueue_thread, &ddp_orphan_task);
	MPASS(TAILQ_EMPTY(&ddp_orphan_pagesets));
	mtx_destroy(&ddp_orphan_pagesets_lock);
	t4_register_shared_cpl_handler(CPL_SET_TCB_RPL, NULL, CPL_COOKIE_DDP0);
	t4_register_shared_cpl_handler(CPL_SET_TCB_RPL, NULL, CPL_COOKIE_DDP1);
	t4_register_cpl_handler(CPL_RX_DATA_DDP, NULL);
	t4_register_cpl_handler(CPL_RX_DDP_COMPLETE, NULL);
}
#endif
