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

SDP_MODPARAM_INT(rcvbuf_initial_size, 32 * 1024,
		"Receive buffer initial size in bytes.");
SDP_MODPARAM_SINT(rcvbuf_scale, 0x8,
		"Receive buffer size scale factor.");

/* Like tcp_fin - called when SDP_MID_DISCONNECT is received */
static void
sdp_handle_disconn(struct sdp_sock *ssk)
{

	sdp_dbg(ssk->socket, "%s\n", __func__);

	SDP_WLOCK_ASSERT(ssk);
	if (TCPS_HAVERCVDFIN(ssk->state) == 0)
		socantrcvmore(ssk->socket);

	switch (ssk->state) {
	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		ssk->state = TCPS_CLOSE_WAIT;
		break;

	case TCPS_FIN_WAIT_1:
		/* Received a reply FIN - start Infiniband tear down */
		sdp_dbg(ssk->socket,
		    "%s: Starting Infiniband tear down sending DREQ\n",
		    __func__);

		sdp_cancel_dreq_wait_timeout(ssk);
		ssk->qp_active = 0;
		if (ssk->id) {
			struct rdma_cm_id *id;

			id = ssk->id;
			SDP_WUNLOCK(ssk);
			rdma_disconnect(id);
			SDP_WLOCK(ssk);
		} else {
			sdp_warn(ssk->socket,
			    "%s: ssk->id is NULL\n", __func__);
			return;
		}
		break;
	case TCPS_TIME_WAIT:
		/* This is a mutual close situation and we've got the DREQ from
		   the peer before the SDP_MID_DISCONNECT */
		break;
	case TCPS_CLOSED:
		/* FIN arrived after IB teardown started - do nothing */
		sdp_dbg(ssk->socket, "%s: fin in state %s\n",
		    __func__, sdp_state_str(ssk->state));
		return;
	default:
		sdp_warn(ssk->socket,
		    "%s: FIN in unexpected state. state=%d\n",
		    __func__, ssk->state);
		break;
	}
}

static int
sdp_post_recv(struct sdp_sock *ssk)
{
	struct sdp_buf *rx_req;
	int i, rc;
	u64 addr;
	struct ib_device *dev;
	struct ib_recv_wr rx_wr = { NULL };
	struct ib_sge ibsge[SDP_MAX_RECV_SGES];
	struct ib_sge *sge = ibsge;
	struct ib_recv_wr *bad_wr;
	struct mbuf *mb, *m;
	struct sdp_bsdh *h;
	int id = ring_head(ssk->rx_ring);

	/* Now, allocate and repost recv */
	sdp_prf(ssk->socket, mb, "Posting mb");
	mb = m_getm2(NULL, ssk->recv_bytes, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (mb == NULL) {
		/* Retry so we can't stall out with no memory. */
		if (!rx_ring_posted(ssk))
			queue_work(rx_comp_wq, &ssk->rx_comp_work);
		return -1;
	}
	for (m = mb; m != NULL; m = m->m_next) {
		m->m_len = M_SIZE(m);
		mb->m_pkthdr.len += m->m_len;
	}
	h = mtod(mb, struct sdp_bsdh *);
	rx_req = ssk->rx_ring.buffer + (id & (SDP_RX_SIZE - 1));
	rx_req->mb = mb;
	dev = ssk->ib_device;
        for (i = 0;  mb != NULL; i++, mb = mb->m_next, sge++) {
		addr = ib_dma_map_single(dev, mb->m_data, mb->m_len,
		    DMA_TO_DEVICE);
		/* TODO: proper error handling */
		BUG_ON(ib_dma_mapping_error(dev, addr));
		BUG_ON(i >= SDP_MAX_RECV_SGES);
		rx_req->mapping[i] = addr;
		sge->addr = addr;
		sge->length = mb->m_len;
		sge->lkey = ssk->sdp_dev->pd->local_dma_lkey;
        }

	rx_wr.next = NULL;
	rx_wr.wr_id = id | SDP_OP_RECV;
	rx_wr.sg_list = ibsge;
	rx_wr.num_sge = i;
	rc = ib_post_recv(ssk->qp, &rx_wr, &bad_wr);
	if (unlikely(rc)) {
		sdp_warn(ssk->socket, "ib_post_recv failed. status %d\n", rc);

		sdp_cleanup_sdp_buf(ssk, rx_req, DMA_FROM_DEVICE);
		m_freem(mb);

		sdp_notify(ssk, ECONNRESET);

		return -1;
	}

	atomic_inc(&ssk->rx_ring.head);
	SDPSTATS_COUNTER_INC(post_recv);

	return 0;
}

static inline int
sdp_post_recvs_needed(struct sdp_sock *ssk)
{
	unsigned long bytes_in_process;
	unsigned long max_bytes;
	int buffer_size;
	int posted;

	if (!ssk->qp_active || !ssk->socket)
		return 0;

	posted = rx_ring_posted(ssk);
	if (posted >= SDP_RX_SIZE)
		return 0;
	if (posted < SDP_MIN_TX_CREDITS)
		return 1;

	buffer_size = ssk->recv_bytes;
	max_bytes = max(ssk->socket->so_rcv.sb_hiwat,
	    (1 + SDP_MIN_TX_CREDITS) * buffer_size);
	max_bytes *= rcvbuf_scale;
	/*
	 * Compute bytes in the receive queue and socket buffer.
	 */
	bytes_in_process = (posted - SDP_MIN_TX_CREDITS) * buffer_size;
	bytes_in_process += sbused(&ssk->socket->so_rcv);

	return bytes_in_process < max_bytes;
}

static inline void
sdp_post_recvs(struct sdp_sock *ssk)
{

	while (sdp_post_recvs_needed(ssk))
		if (sdp_post_recv(ssk))
			return;
}

static inline struct mbuf *
sdp_sock_queue_rcv_mb(struct socket *sk, struct mbuf *mb)
{
	struct sdp_sock *ssk = sdp_sk(sk);
	struct sdp_bsdh *h;

	h = mtod(mb, struct sdp_bsdh *);

#ifdef SDP_ZCOPY
	SDP_SKB_CB(mb)->seq = rcv_nxt(ssk);
	if (h->mid == SDP_MID_SRCAVAIL) {
		struct sdp_srcah *srcah = (struct sdp_srcah *)(h+1);
		struct rx_srcavail_state *rx_sa;
		
		ssk->srcavail_cancel_mseq = 0;

		ssk->rx_sa = rx_sa = RX_SRCAVAIL_STATE(mb) = kzalloc(
				sizeof(struct rx_srcavail_state), M_NOWAIT);

		rx_sa->mseq = ntohl(h->mseq);
		rx_sa->used = 0;
		rx_sa->len = mb_len = ntohl(srcah->len);
		rx_sa->rkey = ntohl(srcah->rkey);
		rx_sa->vaddr = be64_to_cpu(srcah->vaddr);
		rx_sa->flags = 0;

		if (ssk->tx_sa) {
			sdp_dbg_data(ssk->socket, "got RX SrcAvail while waiting "
					"for TX SrcAvail. waking up TX SrcAvail"
					"to be aborted\n");
			wake_up(sk->sk_sleep);
		}

		atomic_add(mb->len, &ssk->rcv_nxt);
		sdp_dbg_data(sk, "queueing SrcAvail. mb_len = %d vaddr = %lld\n",
			mb_len, rx_sa->vaddr);
	} else
#endif
	{
		atomic_add(mb->m_pkthdr.len, &ssk->rcv_nxt);
	}

	m_adj(mb, SDP_HEAD_SIZE);
	SOCKBUF_LOCK(&sk->so_rcv);
	if (unlikely(h->flags & SDP_OOB_PRES))
		sdp_urg(ssk, mb);
	sbappend_locked(&sk->so_rcv, mb, 0);
	sorwakeup_locked(sk);
	return mb;
}

static int
sdp_get_recv_bytes(struct sdp_sock *ssk, u32 new_size)
{

	return MIN(new_size, SDP_MAX_PACKET);
}

int
sdp_init_buffers(struct sdp_sock *ssk, u32 new_size)
{

	ssk->recv_bytes = sdp_get_recv_bytes(ssk, new_size);
	sdp_post_recvs(ssk);

	return 0;
}

int
sdp_resize_buffers(struct sdp_sock *ssk, u32 new_size)
{
	u32 curr_size = ssk->recv_bytes;
	u32 max_size = SDP_MAX_PACKET;

	if (new_size > curr_size && new_size <= max_size) {
		ssk->recv_bytes = sdp_get_recv_bytes(ssk, new_size);
		return 0;
	}
	return -1;
}

static void
sdp_handle_resize_request(struct sdp_sock *ssk, struct sdp_chrecvbuf *buf)
{
	if (sdp_resize_buffers(ssk, ntohl(buf->size)) == 0)
		ssk->recv_request_head = ring_head(ssk->rx_ring) + 1;
	else
		ssk->recv_request_head = ring_tail(ssk->rx_ring);
	ssk->recv_request = 1;
}

static void
sdp_handle_resize_ack(struct sdp_sock *ssk, struct sdp_chrecvbuf *buf)
{
	u32 new_size = ntohl(buf->size);

	if (new_size > ssk->xmit_size_goal)
		ssk->xmit_size_goal = new_size;
}

static struct mbuf *
sdp_recv_completion(struct sdp_sock *ssk, int id)
{
	struct sdp_buf *rx_req;
	struct ib_device *dev;
	struct mbuf *mb;

	if (unlikely(id != ring_tail(ssk->rx_ring))) {
		printk(KERN_WARNING "Bogus recv completion id %d tail %d\n",
			id, ring_tail(ssk->rx_ring));
		return NULL;
	}

	dev = ssk->ib_device;
	rx_req = &ssk->rx_ring.buffer[id & (SDP_RX_SIZE - 1)];
	mb = rx_req->mb;
	sdp_cleanup_sdp_buf(ssk, rx_req, DMA_FROM_DEVICE);

	atomic_inc(&ssk->rx_ring.tail);
	atomic_dec(&ssk->remote_credits);
	return mb;
}

static void
sdp_process_rx_ctl_mb(struct sdp_sock *ssk, struct mbuf *mb)
{
	struct sdp_bsdh *h;
	struct socket *sk;

	SDP_WLOCK_ASSERT(ssk);

	sk = ssk->socket;
 	h = mtod(mb, struct sdp_bsdh *);
	switch (h->mid) {
	case SDP_MID_DATA:
	case SDP_MID_SRCAVAIL:
		sdp_dbg(sk, "DATA after socket rcv was shutdown\n");

		/* got data in RCV_SHUTDOWN */
		if (ssk->state == TCPS_FIN_WAIT_1) {
			sdp_dbg(sk, "RX data when state = FIN_WAIT1\n");
			sdp_notify(ssk, ECONNRESET);
		}

		break;
#ifdef SDP_ZCOPY
	case SDP_MID_RDMARDCOMPL:
		break;
	case SDP_MID_SENDSM:
		sdp_handle_sendsm(ssk, ntohl(h->mseq_ack));
		break;
	case SDP_MID_SRCAVAIL_CANCEL:
		sdp_dbg_data(sk, "Handling SrcAvailCancel\n");
		sdp_prf(sk, NULL, "Handling SrcAvailCancel");
		if (ssk->rx_sa) {
			ssk->srcavail_cancel_mseq = ntohl(h->mseq);
			ssk->rx_sa->flags |= RX_SA_ABORTED;
			ssk->rx_sa = NULL; /* TODO: change it into SDP_MID_DATA and get 
			                      the dirty logic from recvmsg */
		} else {
			sdp_dbg(sk, "Got SrcAvailCancel - "
					"but no SrcAvail in process\n");
		}
		break;
	case SDP_MID_SINKAVAIL:
		sdp_dbg_data(sk, "Got SinkAvail - not supported: ignored\n");
		sdp_prf(sk, NULL, "Got SinkAvail - not supported: ignored");
		/* FALLTHROUGH */
#endif
	case SDP_MID_ABORT:
		sdp_dbg_data(sk, "Handling ABORT\n");
		sdp_prf(sk, NULL, "Handling ABORT");
		sdp_notify(ssk, ECONNRESET);
		break;
	case SDP_MID_DISCONN:
		sdp_dbg_data(sk, "Handling DISCONN\n");
		sdp_prf(sk, NULL, "Handling DISCONN");
		sdp_handle_disconn(ssk);
		break;
	case SDP_MID_CHRCVBUF:
		sdp_dbg_data(sk, "Handling RX CHRCVBUF\n");
		sdp_handle_resize_request(ssk, (struct sdp_chrecvbuf *)(h+1));
		break;
	case SDP_MID_CHRCVBUF_ACK:
		sdp_dbg_data(sk, "Handling RX CHRCVBUF_ACK\n");
		sdp_handle_resize_ack(ssk, (struct sdp_chrecvbuf *)(h+1));
		break;
	default:
		/* TODO: Handle other messages */
		sdp_warn(sk, "SDP: FIXME MID %d\n", h->mid);
		break;
	}
	m_freem(mb);
}

static int
sdp_process_rx_mb(struct sdp_sock *ssk, struct mbuf *mb)
{
	struct socket *sk;
	struct sdp_bsdh *h;
	unsigned long mseq_ack;
	int credits_before;

	h = mtod(mb, struct sdp_bsdh *);
	sk = ssk->socket;
	/*
	 * If another thread is in so_pcbfree this may be partially torn
	 * down but no further synchronization is required as the destroying
	 * thread will wait for receive to shutdown before discarding the
	 * socket.
	 */
	if (sk == NULL) {
		m_freem(mb);
		return 0;
	}

	SDPSTATS_HIST_LINEAR(credits_before_update, tx_credits(ssk));

	mseq_ack = ntohl(h->mseq_ack);
	credits_before = tx_credits(ssk);
	atomic_set(&ssk->tx_ring.credits, mseq_ack - ring_head(ssk->tx_ring) +
			1 + ntohs(h->bufs));
	if (mseq_ack >= ssk->nagle_last_unacked)
		ssk->nagle_last_unacked = 0;

	sdp_prf1(ssk->socket, mb, "RX %s +%d c:%d->%d mseq:%d ack:%d\n",
		mid2str(h->mid), ntohs(h->bufs), credits_before,
		tx_credits(ssk), ntohl(h->mseq), ntohl(h->mseq_ack));

	if (unlikely(h->mid == SDP_MID_DATA &&
	    mb->m_pkthdr.len == SDP_HEAD_SIZE)) {
		/* Credit update is valid even after RCV_SHUTDOWN */
		m_freem(mb);
		return 0;
	}

	if ((h->mid != SDP_MID_DATA && h->mid != SDP_MID_SRCAVAIL) ||
	    TCPS_HAVERCVDFIN(ssk->state)) {
		sdp_prf(sk, NULL, "Control mb - queing to control queue");
#ifdef SDP_ZCOPY
		if (h->mid == SDP_MID_SRCAVAIL_CANCEL) {
			sdp_dbg_data(sk, "Got SrcAvailCancel. "
					"seq: 0x%d seq_ack: 0x%d\n",
					ntohl(h->mseq), ntohl(h->mseq_ack));
			ssk->srcavail_cancel_mseq = ntohl(h->mseq);
		}


		if (h->mid == SDP_MID_RDMARDCOMPL) {
			struct sdp_rrch *rrch = (struct sdp_rrch *)(h+1);
			sdp_dbg_data(sk, "RdmaRdCompl message arrived\n");
			sdp_handle_rdma_read_compl(ssk, ntohl(h->mseq_ack),
					ntohl(rrch->len));
		}
#endif
		if (mbufq_enqueue(&ssk->rxctlq, mb) != 0)
			m_freem(mb);
		return (0);
	}

	sdp_prf1(sk, NULL, "queueing %s mb\n", mid2str(h->mid));
	mb = sdp_sock_queue_rcv_mb(sk, mb);


	return 0;
}

/* called only from irq */
static struct mbuf *
sdp_process_rx_wc(struct sdp_sock *ssk, struct ib_wc *wc)
{
	struct mbuf *mb;
	struct sdp_bsdh *h;
	struct socket *sk = ssk->socket;
	int mseq;

	mb = sdp_recv_completion(ssk, wc->wr_id);
	if (unlikely(!mb))
		return NULL;

	if (unlikely(wc->status)) {
		if (ssk->qp_active && sk) {
			sdp_dbg(sk, "Recv completion with error. "
					"Status %d, vendor: %d\n",
				wc->status, wc->vendor_err);
			sdp_abort(sk);
			ssk->qp_active = 0;
		}
		m_freem(mb);
		return NULL;
	}

	sdp_dbg_data(sk, "Recv completion. ID %d Length %d\n",
			(int)wc->wr_id, wc->byte_len);
	if (unlikely(wc->byte_len < sizeof(struct sdp_bsdh))) {
		sdp_warn(sk, "SDP BUG! byte_len %d < %zd\n",
				wc->byte_len, sizeof(struct sdp_bsdh));
		m_freem(mb);
		return NULL;
	}
	/* Use m_adj to trim the tail of data we didn't use. */
	m_adj(mb, -(mb->m_pkthdr.len - wc->byte_len));
	h = mtod(mb, struct sdp_bsdh *);

	SDP_DUMP_PACKET(ssk->socket, "RX", mb, h);

	ssk->rx_packets++;
	ssk->rx_bytes += mb->m_pkthdr.len;

	mseq = ntohl(h->mseq);
	atomic_set(&ssk->mseq_ack, mseq);
	if (mseq != (int)wc->wr_id)
		sdp_warn(sk, "SDP BUG! mseq %d != wrid %d\n",
				mseq, (int)wc->wr_id);

	return mb;
}

/* Wakeup writers if we now have credits. */
static void
sdp_bzcopy_write_space(struct sdp_sock *ssk)
{
	struct socket *sk = ssk->socket;

	if (tx_credits(ssk) >= ssk->min_bufs && sk)
		sowwakeup(sk);
}

/* only from interrupt. */
static int
sdp_poll_rx_cq(struct sdp_sock *ssk)
{
	struct ib_cq *cq = ssk->rx_ring.cq;
	struct ib_wc ibwc[SDP_NUM_WC];
	int n, i;
	int wc_processed = 0;
	struct mbuf *mb;

	do {
		n = ib_poll_cq(cq, SDP_NUM_WC, ibwc);
		for (i = 0; i < n; ++i) {
			struct ib_wc *wc = &ibwc[i];

			BUG_ON(!(wc->wr_id & SDP_OP_RECV));
			mb = sdp_process_rx_wc(ssk, wc);
			if (!mb)
				continue;

			sdp_process_rx_mb(ssk, mb);
			wc_processed++;
		}
	} while (n == SDP_NUM_WC);

	if (wc_processed)
		sdp_bzcopy_write_space(ssk);

	return wc_processed;
}

static void
sdp_rx_comp_work(struct work_struct *work)
{
	struct sdp_sock *ssk = container_of(work, struct sdp_sock,
			rx_comp_work);

	sdp_prf(ssk->socket, NULL, "%s", __func__);

	SDP_WLOCK(ssk);
	if (unlikely(!ssk->qp)) {
		sdp_prf(ssk->socket, NULL, "qp was destroyed");
		goto out;
	}
	if (unlikely(!ssk->rx_ring.cq)) {
		sdp_prf(ssk->socket, NULL, "rx_ring.cq is NULL");
		goto out;
	}

	if (unlikely(!ssk->poll_cq)) {
		struct rdma_cm_id *id = ssk->id;
		if (id && id->qp)
			rdma_notify(id, IB_EVENT_COMM_EST);
		goto out;
	}

	sdp_do_posts(ssk);
out:
	SDP_WUNLOCK(ssk);
}

void
sdp_do_posts(struct sdp_sock *ssk)
{
	struct socket *sk = ssk->socket;
	int xmit_poll_force;
	struct mbuf *mb;

	SDP_WLOCK_ASSERT(ssk);
	if (!ssk->qp_active) {
		sdp_dbg(sk, "QP is deactivated\n");
		return;
	}

	while ((mb = mbufq_dequeue(&ssk->rxctlq)) != NULL)
		sdp_process_rx_ctl_mb(ssk, mb);

	if (ssk->state == TCPS_TIME_WAIT)
		return;

	if (!ssk->rx_ring.cq || !ssk->tx_ring.cq)
		return;

	sdp_post_recvs(ssk);

	if (tx_ring_posted(ssk))
		sdp_xmit_poll(ssk, 1);

	sdp_post_sends(ssk, M_NOWAIT);

	xmit_poll_force = tx_credits(ssk) < SDP_MIN_TX_CREDITS;

	if (credit_update_needed(ssk) || xmit_poll_force) {
		/* if has pending tx because run out of tx_credits - xmit it */
		sdp_prf(sk, NULL, "Processing to free pending sends");
		sdp_xmit_poll(ssk,  xmit_poll_force);
		sdp_prf(sk, NULL, "Sending credit update");
		sdp_post_sends(ssk, M_NOWAIT);
	}

}

int
sdp_process_rx(struct sdp_sock *ssk)
{
	int wc_processed = 0;
	int credits_before;

	if (!rx_ring_trylock(&ssk->rx_ring)) {
		sdp_dbg(ssk->socket, "ring destroyed. not polling it\n");
		return 0;
	}

	credits_before = tx_credits(ssk);

	wc_processed = sdp_poll_rx_cq(ssk);
	sdp_prf(ssk->socket, NULL, "processed %d", wc_processed);

	if (wc_processed) {
		sdp_prf(ssk->socket, NULL, "credits:  %d -> %d",
				credits_before, tx_credits(ssk));
		queue_work(rx_comp_wq, &ssk->rx_comp_work);
	}
	sdp_arm_rx_cq(ssk);

	rx_ring_unlock(&ssk->rx_ring);

	return (wc_processed);
}

static void
sdp_rx_irq(struct ib_cq *cq, void *cq_context)
{
	struct sdp_sock *ssk;

	ssk = cq_context;
	KASSERT(cq == ssk->rx_ring.cq,
	    ("%s: mismatched cq on %p", __func__, ssk));

	SDPSTATS_COUNTER_INC(rx_int_count);

	sdp_prf(sk, NULL, "rx irq");

	sdp_process_rx(ssk);
}

static
void sdp_rx_ring_purge(struct sdp_sock *ssk)
{
	while (rx_ring_posted(ssk) > 0) {
		struct mbuf *mb;
		mb = sdp_recv_completion(ssk, ring_tail(ssk->rx_ring));
		if (!mb)
			break;
		m_freem(mb);
	}
}

void
sdp_rx_ring_init(struct sdp_sock *ssk)
{
	ssk->rx_ring.buffer = NULL;
	ssk->rx_ring.destroyed = 0;
	rw_init(&ssk->rx_ring.destroyed_lock, "sdp rx lock");
}

static void
sdp_rx_cq_event_handler(struct ib_event *event, void *data)
{
}

int
sdp_rx_ring_create(struct sdp_sock *ssk, struct ib_device *device)
{
	struct ib_cq_init_attr rx_cq_attr = {
		.cqe = SDP_RX_SIZE,
		.comp_vector = 0,
		.flags = 0,
	};
	struct ib_cq *rx_cq;
	int rc = 0;

	sdp_dbg(ssk->socket, "rx ring created");
	INIT_WORK(&ssk->rx_comp_work, sdp_rx_comp_work);
	atomic_set(&ssk->rx_ring.head, 1);
	atomic_set(&ssk->rx_ring.tail, 1);

	ssk->rx_ring.buffer = malloc(sizeof(*ssk->rx_ring.buffer) * SDP_RX_SIZE,
	    M_SDP, M_WAITOK);

	rx_cq = ib_create_cq(device, sdp_rx_irq, sdp_rx_cq_event_handler,
	    ssk, &rx_cq_attr);
	if (IS_ERR(rx_cq)) {
		rc = PTR_ERR(rx_cq);
		sdp_warn(ssk->socket, "Unable to allocate RX CQ: %d.\n", rc);
		goto err_cq;
	}

	sdp_sk(ssk->socket)->rx_ring.cq = rx_cq;
	sdp_arm_rx_cq(ssk);

	return 0;

err_cq:
	free(ssk->rx_ring.buffer, M_SDP);
	ssk->rx_ring.buffer = NULL;
	return rc;
}

void
sdp_rx_ring_destroy(struct sdp_sock *ssk)
{

	cancel_work_sync(&ssk->rx_comp_work);
	rx_ring_destroy_lock(&ssk->rx_ring);

	if (ssk->rx_ring.buffer) {
		sdp_rx_ring_purge(ssk);
		free(ssk->rx_ring.buffer, M_SDP);
		ssk->rx_ring.buffer = NULL;
	}

	if (ssk->rx_ring.cq) {
		if (ib_destroy_cq(ssk->rx_ring.cq)) {
			sdp_warn(ssk->socket, "destroy cq(%p) failed\n",
				ssk->rx_ring.cq);
		} else {
			ssk->rx_ring.cq = NULL;
		}
	}

	WARN_ON(ring_head(ssk->rx_ring) != ring_tail(ssk->rx_ring));
}
