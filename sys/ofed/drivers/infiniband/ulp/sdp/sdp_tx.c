/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2009 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "sdp.h"

#define sdp_cnt(var) do { (var)++; } while (0)

SDP_MODPARAM_SINT(sdp_keepalive_probes_sent, 0,
		"Total number of keepalive probes sent.");

static int sdp_process_tx_cq(struct sdp_sock *ssk);
static void sdp_poll_tx_timeout(void *data);

int
sdp_xmit_poll(struct sdp_sock *ssk, int force)
{
	int wc_processed = 0;

	SDP_WLOCK_ASSERT(ssk);
	sdp_prf(ssk->socket, NULL, "%s", __func__);

	/* If we don't have a pending timer, set one up to catch our recent
	   post in case the interface becomes idle */
	if (!callout_pending(&ssk->tx_ring.timer))
		callout_reset(&ssk->tx_ring.timer, SDP_TX_POLL_TIMEOUT,
		    sdp_poll_tx_timeout, ssk);

	/* Poll the CQ every SDP_TX_POLL_MODER packets */
	if (force || (++ssk->tx_ring.poll_cnt & (SDP_TX_POLL_MODER - 1)) == 0)
		wc_processed = sdp_process_tx_cq(ssk);

	return wc_processed;
}

void
sdp_post_send(struct sdp_sock *ssk, struct mbuf *mb)
{
	struct sdp_buf *tx_req;
	struct sdp_bsdh *h;
	unsigned long mseq;
	struct ib_device *dev;
	struct ib_send_wr *bad_wr;
	struct ib_sge ibsge[SDP_MAX_SEND_SGES];
	struct ib_sge *sge;
	struct ib_send_wr tx_wr = { NULL };
	int i, rc;
	u64 addr;

	SDPSTATS_COUNTER_MID_INC(post_send, h->mid);
	SDPSTATS_HIST(send_size, mb->len);

	if (!ssk->qp_active) {
		m_freem(mb);
		return;
	}

	mseq = ring_head(ssk->tx_ring);
	h = mtod(mb, struct sdp_bsdh *);
	ssk->tx_packets++;
	ssk->tx_bytes += mb->m_pkthdr.len;

#ifdef SDP_ZCOPY
	if (unlikely(h->mid == SDP_MID_SRCAVAIL)) {
		struct tx_srcavail_state *tx_sa = TX_SRCAVAIL_STATE(mb);
		if (ssk->tx_sa != tx_sa) {
			sdp_dbg_data(ssk->socket, "SrcAvail cancelled "
					"before being sent!\n");
			WARN_ON(1);
			m_freem(mb);
			return;
		}
		TX_SRCAVAIL_STATE(mb)->mseq = mseq;
	}
#endif

	if (unlikely(mb->m_flags & M_URG))
		h->flags = SDP_OOB_PRES | SDP_OOB_PEND;
	else
		h->flags = 0;

	mb->m_flags |= M_RDONLY; /* Don't allow compression once sent. */
	h->bufs = htons(rx_ring_posted(ssk));
	h->len = htonl(mb->m_pkthdr.len);
	h->mseq = htonl(mseq);
	h->mseq_ack = htonl(mseq_ack(ssk));

	sdp_prf1(ssk->socket, mb, "TX: %s bufs: %d mseq:%ld ack:%d",
			mid2str(h->mid), rx_ring_posted(ssk), mseq,
			ntohl(h->mseq_ack));

	SDP_DUMP_PACKET(ssk->socket, "TX", mb, h);

	tx_req = &ssk->tx_ring.buffer[mseq & (SDP_TX_SIZE - 1)];
	tx_req->mb = mb;
	dev = ssk->ib_device;
	sge = &ibsge[0];
	for (i = 0;  mb != NULL; i++, mb = mb->m_next, sge++) {
		addr = ib_dma_map_single(dev, mb->m_data, mb->m_len,
		    DMA_TO_DEVICE);
		/* TODO: proper error handling */
		BUG_ON(ib_dma_mapping_error(dev, addr));
		BUG_ON(i >= SDP_MAX_SEND_SGES);
		tx_req->mapping[i] = addr;
		sge->addr = addr;
		sge->length = mb->m_len;
		sge->lkey = ssk->sdp_dev->pd->local_dma_lkey;
	}
	tx_wr.next = NULL;
	tx_wr.wr_id = mseq | SDP_OP_SEND;
	tx_wr.sg_list = ibsge;
	tx_wr.num_sge = i;
	tx_wr.opcode = IB_WR_SEND;
	tx_wr.send_flags = IB_SEND_SIGNALED;
	if (unlikely(tx_req->mb->m_flags & M_URG))
		tx_wr.send_flags |= IB_SEND_SOLICITED;

	rc = ib_post_send(ssk->qp, &tx_wr, &bad_wr);
	if (unlikely(rc)) {
		sdp_dbg(ssk->socket,
				"ib_post_send failed with status %d.\n", rc);

		sdp_cleanup_sdp_buf(ssk, tx_req, DMA_TO_DEVICE);

		sdp_notify(ssk, ECONNRESET);
		m_freem(tx_req->mb);
		return;
	}

	atomic_inc(&ssk->tx_ring.head);
	atomic_dec(&ssk->tx_ring.credits);
	atomic_set(&ssk->remote_credits, rx_ring_posted(ssk));

	return;
}

static struct mbuf *
sdp_send_completion(struct sdp_sock *ssk, int mseq)
{
	struct ib_device *dev;
	struct sdp_buf *tx_req;
	struct mbuf *mb = NULL;
	struct sdp_tx_ring *tx_ring = &ssk->tx_ring;

	if (unlikely(mseq != ring_tail(*tx_ring))) {
		printk(KERN_WARNING "Bogus send completion id %d tail %d\n",
			mseq, ring_tail(*tx_ring));
		goto out;
	}

	dev = ssk->ib_device;
	tx_req = &tx_ring->buffer[mseq & (SDP_TX_SIZE - 1)];
	mb = tx_req->mb;
	sdp_cleanup_sdp_buf(ssk, tx_req, DMA_TO_DEVICE);

#ifdef SDP_ZCOPY
	/* TODO: AIO and real zcopy code; add their context support here */
	if (BZCOPY_STATE(mb))
		BZCOPY_STATE(mb)->busy--;
#endif

	atomic_inc(&tx_ring->tail);

out:
	return mb;
}

static int
sdp_handle_send_comp(struct sdp_sock *ssk, struct ib_wc *wc)
{
	struct mbuf *mb = NULL;
	struct sdp_bsdh *h;

	if (unlikely(wc->status)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			sdp_prf(ssk->socket, mb, "Send completion with error. "
				"Status %d", wc->status);
			sdp_dbg_data(ssk->socket, "Send completion with error. "
				"Status %d\n", wc->status);
			sdp_notify(ssk, ECONNRESET);
		}
	}

	mb = sdp_send_completion(ssk, wc->wr_id);
	if (unlikely(!mb))
		return -1;

	h = mtod(mb, struct sdp_bsdh *);
	sdp_prf1(ssk->socket, mb, "tx completion. mseq:%d", ntohl(h->mseq));
	sdp_dbg(ssk->socket, "tx completion. %p %d mseq:%d",
	    mb, mb->m_pkthdr.len, ntohl(h->mseq));
	m_freem(mb);

	return 0;
}

static inline void
sdp_process_tx_wc(struct sdp_sock *ssk, struct ib_wc *wc)
{

	if (likely(wc->wr_id & SDP_OP_SEND)) {
		sdp_handle_send_comp(ssk, wc);
		return;
	}

#ifdef SDP_ZCOPY
	if (wc->wr_id & SDP_OP_RDMA) {
		/* TODO: handle failed RDMA read cqe */

		sdp_dbg_data(ssk->socket,
	 	    "TX comp: RDMA read. status: %d\n", wc->status);
		sdp_prf1(sk, NULL, "TX comp: RDMA read");

		if (!ssk->tx_ring.rdma_inflight) {
			sdp_warn(ssk->socket, "ERROR: unexpected RDMA read\n");
			return;
		}

		if (!ssk->tx_ring.rdma_inflight->busy) {
			sdp_warn(ssk->socket,
			    "ERROR: too many RDMA read completions\n");
			return;
		}

		/* Only last RDMA read WR is signalled. Order is guaranteed -
		 * therefore if Last RDMA read WR is completed - all other
		 * have, too */
		ssk->tx_ring.rdma_inflight->busy = 0;
		sowwakeup(ssk->socket);
		sdp_dbg_data(ssk->socket, "woke up sleepers\n");
		return;
	}
#endif

	/* Keepalive probe sent cleanup */
	sdp_cnt(sdp_keepalive_probes_sent);

	if (likely(!wc->status))
		return;

	sdp_dbg(ssk->socket, " %s consumes KEEPALIVE status %d\n",
			__func__, wc->status);

	if (wc->status == IB_WC_WR_FLUSH_ERR)
		return;

	sdp_notify(ssk, ECONNRESET);
}

static int
sdp_process_tx_cq(struct sdp_sock *ssk)
{
	struct ib_wc ibwc[SDP_NUM_WC];
	int n, i;
	int wc_processed = 0;

	SDP_WLOCK_ASSERT(ssk);

	if (!ssk->tx_ring.cq) {
		sdp_dbg(ssk->socket, "tx irq on destroyed tx_cq\n");
		return 0;
	}

	do {
		n = ib_poll_cq(ssk->tx_ring.cq, SDP_NUM_WC, ibwc);
		for (i = 0; i < n; ++i) {
			sdp_process_tx_wc(ssk, ibwc + i);
			wc_processed++;
		}
	} while (n == SDP_NUM_WC);

	if (wc_processed) {
		sdp_post_sends(ssk, M_NOWAIT);
		sdp_prf1(sk, NULL, "Waking sendmsg. inflight=%d", 
				(u32) tx_ring_posted(ssk));
		sowwakeup(ssk->socket);
	}

	return wc_processed;
}

static void
sdp_poll_tx(struct sdp_sock *ssk)
{
	struct socket *sk = ssk->socket;
	u32 inflight, wc_processed;

	sdp_prf1(ssk->socket, NULL, "TX timeout: inflight=%d, head=%d tail=%d", 
		(u32) tx_ring_posted(ssk),
		ring_head(ssk->tx_ring), ring_tail(ssk->tx_ring));

	if (unlikely(ssk->state == TCPS_CLOSED)) {
		sdp_warn(sk, "Socket is closed\n");
		goto out;
	}

	wc_processed = sdp_process_tx_cq(ssk);
	if (!wc_processed)
		SDPSTATS_COUNTER_INC(tx_poll_miss);
	else
		SDPSTATS_COUNTER_INC(tx_poll_hit);

	inflight = (u32) tx_ring_posted(ssk);
	sdp_prf1(ssk->socket, NULL, "finished tx processing. inflight = %d",
	    inflight);

	/* If there are still packets in flight and the timer has not already
	 * been scheduled by the Tx routine then schedule it here to guarantee
	 * completion processing of these packets */
	if (inflight)
		callout_reset(&ssk->tx_ring.timer, SDP_TX_POLL_TIMEOUT,
		    sdp_poll_tx_timeout, ssk);
out:
#ifdef SDP_ZCOPY
	if (ssk->tx_ring.rdma_inflight && ssk->tx_ring.rdma_inflight->busy) {
		sdp_prf1(sk, NULL, "RDMA is inflight - arming irq");
		sdp_arm_tx_cq(ssk);
	}
#endif
	return;
}

static void
sdp_poll_tx_timeout(void *data)
{
	struct sdp_sock *ssk = (struct sdp_sock *)data;

	if (!callout_active(&ssk->tx_ring.timer))
		return;
	callout_deactivate(&ssk->tx_ring.timer);
	sdp_poll_tx(ssk);
}

static void
sdp_tx_irq(struct ib_cq *cq, void *cq_context)
{
	struct sdp_sock *ssk;

	ssk = cq_context;
	sdp_prf1(ssk->socket, NULL, "tx irq");
	sdp_dbg_data(ssk->socket, "Got tx comp interrupt\n");
	SDPSTATS_COUNTER_INC(tx_int_count);
	SDP_WLOCK(ssk);
	sdp_poll_tx(ssk);
	SDP_WUNLOCK(ssk);
}

static
void sdp_tx_ring_purge(struct sdp_sock *ssk)
{
	while (tx_ring_posted(ssk)) {
		struct mbuf *mb;
		mb = sdp_send_completion(ssk, ring_tail(ssk->tx_ring));
		if (!mb)
			break;
		m_freem(mb);
	}
}

void
sdp_post_keepalive(struct sdp_sock *ssk)
{
	int rc;
	struct ib_send_wr wr, *bad_wr;

	sdp_dbg(ssk->socket, "%s\n", __func__);

	memset(&wr, 0, sizeof(wr));

	wr.next    = NULL;
	wr.wr_id   = 0;
	wr.sg_list = NULL;
	wr.num_sge = 0;
	wr.opcode  = IB_WR_RDMA_WRITE;

	rc = ib_post_send(ssk->qp, &wr, &bad_wr);
	if (rc) {
		sdp_dbg(ssk->socket,
			"ib_post_keepalive failed with status %d.\n", rc);
		sdp_notify(ssk, ECONNRESET);
	}

	sdp_cnt(sdp_keepalive_probes_sent);
}

static void
sdp_tx_cq_event_handler(struct ib_event *event, void *data)
{
}

int
sdp_tx_ring_create(struct sdp_sock *ssk, struct ib_device *device)
{
	struct ib_cq_init_attr tx_cq_attr = {
		.cqe = SDP_TX_SIZE,
		.comp_vector = 0,
		.flags = 0,
	};
	struct ib_cq *tx_cq;
	int rc = 0;

	sdp_dbg(ssk->socket, "tx ring create\n");
	callout_init_rw(&ssk->tx_ring.timer, &ssk->lock, 0);
	callout_init_rw(&ssk->nagle_timer, &ssk->lock, 0);
	atomic_set(&ssk->tx_ring.head, 1);
	atomic_set(&ssk->tx_ring.tail, 1);

	ssk->tx_ring.buffer = malloc(sizeof(*ssk->tx_ring.buffer) * SDP_TX_SIZE,
	    M_SDP, M_WAITOK);

	tx_cq = ib_create_cq(device, sdp_tx_irq, sdp_tx_cq_event_handler,
			  ssk, &tx_cq_attr);
	if (IS_ERR(tx_cq)) {
		rc = PTR_ERR(tx_cq);
		sdp_warn(ssk->socket, "Unable to allocate TX CQ: %d.\n", rc);
		goto err_cq;
	}
	ssk->tx_ring.cq = tx_cq;
	ssk->tx_ring.poll_cnt = 0;
	sdp_arm_tx_cq(ssk);

	return 0;

err_cq:
	free(ssk->tx_ring.buffer, M_SDP);
	ssk->tx_ring.buffer = NULL;
	return rc;
}

void
sdp_tx_ring_destroy(struct sdp_sock *ssk)
{

	sdp_dbg(ssk->socket, "tx ring destroy\n");
	SDP_WLOCK(ssk);
	callout_stop(&ssk->tx_ring.timer);
	callout_stop(&ssk->nagle_timer);
	SDP_WUNLOCK(ssk);
	callout_drain(&ssk->tx_ring.timer);
	callout_drain(&ssk->nagle_timer);

	if (ssk->tx_ring.buffer) {
		sdp_tx_ring_purge(ssk);
		free(ssk->tx_ring.buffer, M_SDP);
		ssk->tx_ring.buffer = NULL;
	}

	if (ssk->tx_ring.cq) {
		if (ib_destroy_cq(ssk->tx_ring.cq)) {
			sdp_warn(ssk->socket, "destroy cq(%p) failed\n",
					ssk->tx_ring.cq);
		} else {
			ssk->tx_ring.cq = NULL;
		}
	}

	WARN_ON(ring_head(ssk->tx_ring) != ring_tail(ssk->tx_ring));
}
