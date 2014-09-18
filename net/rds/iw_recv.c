/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
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
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <rdma/rdma_cm.h>

#include "rds.h"
#include "iw.h"

static struct kmem_cache *rds_iw_incoming_slab;
static struct kmem_cache *rds_iw_frag_slab;
static atomic_t	rds_iw_allocation = ATOMIC_INIT(0);

static void rds_iw_frag_drop_page(struct rds_page_frag *frag)
{
	rdsdebug("frag %p page %p\n", frag, frag->f_page);
	__free_page(frag->f_page);
	frag->f_page = NULL;
}

static void rds_iw_frag_free(struct rds_page_frag *frag)
{
	rdsdebug("frag %p page %p\n", frag, frag->f_page);
	BUG_ON(frag->f_page);
	kmem_cache_free(rds_iw_frag_slab, frag);
}

/*
 * We map a page at a time.  Its fragments are posted in order.  This
 * is called in fragment order as the fragments get send completion events.
 * Only the last frag in the page performs the unmapping.
 *
 * It's OK for ring cleanup to call this in whatever order it likes because
 * DMA is not in flight and so we can unmap while other ring entries still
 * hold page references in their frags.
 */
static void rds_iw_recv_unmap_page(struct rds_iw_connection *ic,
				   struct rds_iw_recv_work *recv)
{
	struct rds_page_frag *frag = recv->r_frag;

	rdsdebug("recv %p frag %p page %p\n", recv, frag, frag->f_page);
	if (frag->f_mapped)
		ib_dma_unmap_page(ic->i_cm_id->device,
			       frag->f_mapped,
			       RDS_FRAG_SIZE, DMA_FROM_DEVICE);
	frag->f_mapped = 0;
}

void rds_iw_recv_init_ring(struct rds_iw_connection *ic)
{
	struct rds_iw_recv_work *recv;
	u32 i;

	for (i = 0, recv = ic->i_recvs; i < ic->i_recv_ring.w_nr; i++, recv++) {
		struct ib_sge *sge;

		recv->r_iwinc = NULL;
		recv->r_frag = NULL;

		recv->r_wr.next = NULL;
		recv->r_wr.wr_id = i;
		recv->r_wr.sg_list = recv->r_sge;
		recv->r_wr.num_sge = RDS_IW_RECV_SGE;

		sge = rds_iw_data_sge(ic, recv->r_sge);
		sge->addr = 0;
		sge->length = RDS_FRAG_SIZE;
		sge->lkey = 0;

		sge = rds_iw_header_sge(ic, recv->r_sge);
		sge->addr = ic->i_recv_hdrs_dma + (i * sizeof(struct rds_header));
		sge->length = sizeof(struct rds_header);
		sge->lkey = 0;
	}
}

static void rds_iw_recv_clear_one(struct rds_iw_connection *ic,
				  struct rds_iw_recv_work *recv)
{
	if (recv->r_iwinc) {
		rds_inc_put(&recv->r_iwinc->ii_inc);
		recv->r_iwinc = NULL;
	}
	if (recv->r_frag) {
		rds_iw_recv_unmap_page(ic, recv);
		if (recv->r_frag->f_page)
			rds_iw_frag_drop_page(recv->r_frag);
		rds_iw_frag_free(recv->r_frag);
		recv->r_frag = NULL;
	}
}

void rds_iw_recv_clear_ring(struct rds_iw_connection *ic)
{
	u32 i;

	for (i = 0; i < ic->i_recv_ring.w_nr; i++)
		rds_iw_recv_clear_one(ic, &ic->i_recvs[i]);

	if (ic->i_frag.f_page)
		rds_iw_frag_drop_page(&ic->i_frag);
}

static int rds_iw_recv_refill_one(struct rds_connection *conn,
				  struct rds_iw_recv_work *recv,
				  gfp_t kptr_gfp, gfp_t page_gfp)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	dma_addr_t dma_addr;
	struct ib_sge *sge;
	int ret = -ENOMEM;

	if (!recv->r_iwinc) {
		if (!atomic_add_unless(&rds_iw_allocation, 1, rds_iw_sysctl_max_recv_allocation)) {
			rds_iw_stats_inc(s_iw_rx_alloc_limit);
			goto out;
		}
		recv->r_iwinc = kmem_cache_alloc(rds_iw_incoming_slab,
						 kptr_gfp);
		if (!recv->r_iwinc) {
			atomic_dec(&rds_iw_allocation);
			goto out;
		}
		INIT_LIST_HEAD(&recv->r_iwinc->ii_frags);
		rds_inc_init(&recv->r_iwinc->ii_inc, conn, conn->c_faddr);
	}

	if (!recv->r_frag) {
		recv->r_frag = kmem_cache_alloc(rds_iw_frag_slab, kptr_gfp);
		if (!recv->r_frag)
			goto out;
		INIT_LIST_HEAD(&recv->r_frag->f_item);
		recv->r_frag->f_page = NULL;
	}

	if (!ic->i_frag.f_page) {
		ic->i_frag.f_page = alloc_page(page_gfp);
		if (!ic->i_frag.f_page)
			goto out;
		ic->i_frag.f_offset = 0;
	}

	dma_addr = ib_dma_map_page(ic->i_cm_id->device,
				  ic->i_frag.f_page,
				  ic->i_frag.f_offset,
				  RDS_FRAG_SIZE,
				  DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(ic->i_cm_id->device, dma_addr))
		goto out;

	/*
	 * Once we get the RDS_PAGE_LAST_OFF frag then rds_iw_frag_unmap()
	 * must be called on this recv.  This happens as completions hit
	 * in order or on connection shutdown.
	 */
	recv->r_frag->f_page = ic->i_frag.f_page;
	recv->r_frag->f_offset = ic->i_frag.f_offset;
	recv->r_frag->f_mapped = dma_addr;

	sge = rds_iw_data_sge(ic, recv->r_sge);
	sge->addr = dma_addr;
	sge->length = RDS_FRAG_SIZE;

	sge = rds_iw_header_sge(ic, recv->r_sge);
	sge->addr = ic->i_recv_hdrs_dma + (recv - ic->i_recvs) * sizeof(struct rds_header);
	sge->length = sizeof(struct rds_header);

	get_page(recv->r_frag->f_page);

	if (ic->i_frag.f_offset < RDS_PAGE_LAST_OFF) {
		ic->i_frag.f_offset += RDS_FRAG_SIZE;
	} else {
		put_page(ic->i_frag.f_page);
		ic->i_frag.f_page = NULL;
		ic->i_frag.f_offset = 0;
	}

	ret = 0;
out:
	return ret;
}

/*
 * This tries to allocate and post unused work requests after making sure that
 * they have all the allocations they need to queue received fragments into
 * sockets.  The i_recv_mutex is held here so that ring_alloc and _unalloc
 * pairs don't go unmatched.
 *
 * -1 is returned if posting fails due to temporary resource exhaustion.
 */
int rds_iw_recv_refill(struct rds_connection *conn, gfp_t kptr_gfp,
		       gfp_t page_gfp, int prefill)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct rds_iw_recv_work *recv;
	struct ib_recv_wr *failed_wr;
	unsigned int posted = 0;
	int ret = 0;
	u32 pos;

	while ((prefill || rds_conn_up(conn)) &&
	       rds_iw_ring_alloc(&ic->i_recv_ring, 1, &pos)) {
		if (pos >= ic->i_recv_ring.w_nr) {
			printk(KERN_NOTICE "Argh - ring alloc returned pos=%u\n",
					pos);
			ret = -EINVAL;
			break;
		}

		recv = &ic->i_recvs[pos];
		ret = rds_iw_recv_refill_one(conn, recv, kptr_gfp, page_gfp);
		if (ret) {
			ret = -1;
			break;
		}

		/* XXX when can this fail? */
		ret = ib_post_recv(ic->i_cm_id->qp, &recv->r_wr, &failed_wr);
		rdsdebug("recv %p iwinc %p page %p addr %lu ret %d\n", recv,
			 recv->r_iwinc, recv->r_frag->f_page,
			 (long) recv->r_frag->f_mapped, ret);
		if (ret) {
			rds_iw_conn_error(conn, "recv post on "
			       "%pI4 returned %d, disconnecting and "
			       "reconnecting\n", &conn->c_faddr,
			       ret);
			ret = -1;
			break;
		}

		posted++;
	}

	/* We're doing flow control - update the window. */
	if (ic->i_flowctl && posted)
		rds_iw_advertise_credits(conn, posted);

	if (ret)
		rds_iw_ring_unalloc(&ic->i_recv_ring, 1);
	return ret;
}

static void rds_iw_inc_purge(struct rds_incoming *inc)
{
	struct rds_iw_incoming *iwinc;
	struct rds_page_frag *frag;
	struct rds_page_frag *pos;

	iwinc = container_of(inc, struct rds_iw_incoming, ii_inc);
	rdsdebug("purging iwinc %p inc %p\n", iwinc, inc);

	list_for_each_entry_safe(frag, pos, &iwinc->ii_frags, f_item) {
		list_del_init(&frag->f_item);
		rds_iw_frag_drop_page(frag);
		rds_iw_frag_free(frag);
	}
}

void rds_iw_inc_free(struct rds_incoming *inc)
{
	struct rds_iw_incoming *iwinc;

	iwinc = container_of(inc, struct rds_iw_incoming, ii_inc);

	rds_iw_inc_purge(inc);
	rdsdebug("freeing iwinc %p inc %p\n", iwinc, inc);
	BUG_ON(!list_empty(&iwinc->ii_frags));
	kmem_cache_free(rds_iw_incoming_slab, iwinc);
	atomic_dec(&rds_iw_allocation);
	BUG_ON(atomic_read(&rds_iw_allocation) < 0);
}

int rds_iw_inc_copy_to_user(struct rds_incoming *inc, struct iovec *first_iov,
			    size_t size)
{
	struct rds_iw_incoming *iwinc;
	struct rds_page_frag *frag;
	struct iovec *iov = first_iov;
	unsigned long to_copy;
	unsigned long frag_off = 0;
	unsigned long iov_off = 0;
	int copied = 0;
	int ret;
	u32 len;

	iwinc = container_of(inc, struct rds_iw_incoming, ii_inc);
	frag = list_entry(iwinc->ii_frags.next, struct rds_page_frag, f_item);
	len = be32_to_cpu(inc->i_hdr.h_len);

	while (copied < size && copied < len) {
		if (frag_off == RDS_FRAG_SIZE) {
			frag = list_entry(frag->f_item.next,
					  struct rds_page_frag, f_item);
			frag_off = 0;
		}
		while (iov_off == iov->iov_len) {
			iov_off = 0;
			iov++;
		}

		to_copy = min(iov->iov_len - iov_off, RDS_FRAG_SIZE - frag_off);
		to_copy = min_t(size_t, to_copy, size - copied);
		to_copy = min_t(unsigned long, to_copy, len - copied);

		rdsdebug("%lu bytes to user [%p, %zu] + %lu from frag "
			 "[%p, %lu] + %lu\n",
			 to_copy, iov->iov_base, iov->iov_len, iov_off,
			 frag->f_page, frag->f_offset, frag_off);

		/* XXX needs + offset for multiple recvs per page */
		ret = rds_page_copy_to_user(frag->f_page,
					    frag->f_offset + frag_off,
					    iov->iov_base + iov_off,
					    to_copy);
		if (ret) {
			copied = ret;
			break;
		}

		iov_off += to_copy;
		frag_off += to_copy;
		copied += to_copy;
	}

	return copied;
}

/* ic starts out kzalloc()ed */
void rds_iw_recv_init_ack(struct rds_iw_connection *ic)
{
	struct ib_send_wr *wr = &ic->i_ack_wr;
	struct ib_sge *sge = &ic->i_ack_sge;

	sge->addr = ic->i_ack_dma;
	sge->length = sizeof(struct rds_header);
	sge->lkey = rds_iw_local_dma_lkey(ic);

	wr->sg_list = sge;
	wr->num_sge = 1;
	wr->opcode = IB_WR_SEND;
	wr->wr_id = RDS_IW_ACK_WR_ID;
	wr->send_flags = IB_SEND_SIGNALED | IB_SEND_SOLICITED;
}

/*
 * You'd think that with reliable IB connections you wouldn't need to ack
 * messages that have been received.  The problem is that IB hardware generates
 * an ack message before it has DMAed the message into memory.  This creates a
 * potential message loss if the HCA is disabled for any reason between when it
 * sends the ack and before the message is DMAed and processed.  This is only a
 * potential issue if another HCA is available for fail-over.
 *
 * When the remote host receives our ack they'll free the sent message from
 * their send queue.  To decrease the latency of this we always send an ack
 * immediately after we've received messages.
 *
 * For simplicity, we only have one ack in flight at a time.  This puts
 * pressure on senders to have deep enough send queues to absorb the latency of
 * a single ack frame being in flight.  This might not be good enough.
 *
 * This is implemented by have a long-lived send_wr and sge which point to a
 * statically allocated ack frame.  This ack wr does not fall under the ring
 * accounting that the tx and rx wrs do.  The QP attribute specifically makes
 * room for it beyond the ring size.  Send completion notices its special
 * wr_id and avoids working with the ring in that case.
 */
#ifndef KERNEL_HAS_ATOMIC64
static void rds_iw_set_ack(struct rds_iw_connection *ic, u64 seq,
				int ack_required)
{
	unsigned long flags;

	spin_lock_irqsave(&ic->i_ack_lock, flags);
	ic->i_ack_next = seq;
	if (ack_required)
		set_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);
	spin_unlock_irqrestore(&ic->i_ack_lock, flags);
}

static u64 rds_iw_get_ack(struct rds_iw_connection *ic)
{
	unsigned long flags;
	u64 seq;

	clear_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);

	spin_lock_irqsave(&ic->i_ack_lock, flags);
	seq = ic->i_ack_next;
	spin_unlock_irqrestore(&ic->i_ack_lock, flags);

	return seq;
}
#else
static void rds_iw_set_ack(struct rds_iw_connection *ic, u64 seq,
				int ack_required)
{
	atomic64_set(&ic->i_ack_next, seq);
	if (ack_required) {
		smp_mb__before_atomic();
		set_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);
	}
}

static u64 rds_iw_get_ack(struct rds_iw_connection *ic)
{
	clear_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);
	smp_mb__after_atomic();

	return atomic64_read(&ic->i_ack_next);
}
#endif


static void rds_iw_send_ack(struct rds_iw_connection *ic, unsigned int adv_credits)
{
	struct rds_header *hdr = ic->i_ack;
	struct ib_send_wr *failed_wr;
	u64 seq;
	int ret;

	seq = rds_iw_get_ack(ic);

	rdsdebug("send_ack: ic %p ack %llu\n", ic, (unsigned long long) seq);
	rds_message_populate_header(hdr, 0, 0, 0);
	hdr->h_ack = cpu_to_be64(seq);
	hdr->h_credit = adv_credits;
	rds_message_make_checksum(hdr);
	ic->i_ack_queued = jiffies;

	ret = ib_post_send(ic->i_cm_id->qp, &ic->i_ack_wr, &failed_wr);
	if (unlikely(ret)) {
		/* Failed to send. Release the WR, and
		 * force another ACK.
		 */
		clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
		set_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);

		rds_iw_stats_inc(s_iw_ack_send_failure);

		rds_iw_conn_error(ic->conn, "sending ack failed\n");
	} else
		rds_iw_stats_inc(s_iw_ack_sent);
}

/*
 * There are 3 ways of getting acknowledgements to the peer:
 *  1.	We call rds_iw_attempt_ack from the recv completion handler
 *	to send an ACK-only frame.
 *	However, there can be only one such frame in the send queue
 *	at any time, so we may have to postpone it.
 *  2.	When another (data) packet is transmitted while there's
 *	an ACK in the queue, we piggyback the ACK sequence number
 *	on the data packet.
 *  3.	If the ACK WR is done sending, we get called from the
 *	send queue completion handler, and check whether there's
 *	another ACK pending (postponed because the WR was on the
 *	queue). If so, we transmit it.
 *
 * We maintain 2 variables:
 *  -	i_ack_flags, which keeps track of whether the ACK WR
 *	is currently in the send queue or not (IB_ACK_IN_FLIGHT)
 *  -	i_ack_next, which is the last sequence number we received
 *
 * Potentially, send queue and receive queue handlers can run concurrently.
 * It would be nice to not have to use a spinlock to synchronize things,
 * but the one problem that rules this out is that 64bit updates are
 * not atomic on all platforms. Things would be a lot simpler if
 * we had atomic64 or maybe cmpxchg64 everywhere.
 *
 * Reconnecting complicates this picture just slightly. When we
 * reconnect, we may be seeing duplicate packets. The peer
 * is retransmitting them, because it hasn't seen an ACK for
 * them. It is important that we ACK these.
 *
 * ACK mitigation adds a header flag "ACK_REQUIRED"; any packet with
 * this flag set *MUST* be acknowledged immediately.
 */

/*
 * When we get here, we're called from the recv queue handler.
 * Check whether we ought to transmit an ACK.
 */
void rds_iw_attempt_ack(struct rds_iw_connection *ic)
{
	unsigned int adv_credits;

	if (!test_bit(IB_ACK_REQUESTED, &ic->i_ack_flags))
		return;

	if (test_and_set_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags)) {
		rds_iw_stats_inc(s_iw_ack_send_delayed);
		return;
	}

	/* Can we get a send credit? */
	if (!rds_iw_send_grab_credits(ic, 1, &adv_credits, 0, RDS_MAX_ADV_CREDIT)) {
		rds_iw_stats_inc(s_iw_tx_throttle);
		clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
		return;
	}

	clear_bit(IB_ACK_REQUESTED, &ic->i_ack_flags);
	rds_iw_send_ack(ic, adv_credits);
}

/*
 * We get here from the send completion handler, when the
 * adapter tells us the ACK frame was sent.
 */
void rds_iw_ack_send_complete(struct rds_iw_connection *ic)
{
	clear_bit(IB_ACK_IN_FLIGHT, &ic->i_ack_flags);
	rds_iw_attempt_ack(ic);
}

/*
 * This is called by the regular xmit code when it wants to piggyback
 * an ACK on an outgoing frame.
 */
u64 rds_iw_piggyb_ack(struct rds_iw_connection *ic)
{
	if (test_and_clear_bit(IB_ACK_REQUESTED, &ic->i_ack_flags))
		rds_iw_stats_inc(s_iw_ack_send_piggybacked);
	return rds_iw_get_ack(ic);
}

/*
 * It's kind of lame that we're copying from the posted receive pages into
 * long-lived bitmaps.  We could have posted the bitmaps and rdma written into
 * them.  But receiving new congestion bitmaps should be a *rare* event, so
 * hopefully we won't need to invest that complexity in making it more
 * efficient.  By copying we can share a simpler core with TCP which has to
 * copy.
 */
static void rds_iw_cong_recv(struct rds_connection *conn,
			      struct rds_iw_incoming *iwinc)
{
	struct rds_cong_map *map;
	unsigned int map_off;
	unsigned int map_page;
	struct rds_page_frag *frag;
	unsigned long frag_off;
	unsigned long to_copy;
	unsigned long copied;
	uint64_t uncongested = 0;
	void *addr;

	/* catch completely corrupt packets */
	if (be32_to_cpu(iwinc->ii_inc.i_hdr.h_len) != RDS_CONG_MAP_BYTES)
		return;

	map = conn->c_fcong;
	map_page = 0;
	map_off = 0;

	frag = list_entry(iwinc->ii_frags.next, struct rds_page_frag, f_item);
	frag_off = 0;

	copied = 0;

	while (copied < RDS_CONG_MAP_BYTES) {
		uint64_t *src, *dst;
		unsigned int k;

		to_copy = min(RDS_FRAG_SIZE - frag_off, PAGE_SIZE - map_off);
		BUG_ON(to_copy & 7); /* Must be 64bit aligned. */

		addr = kmap_atomic(frag->f_page);

		src = addr + frag_off;
		dst = (void *)map->m_page_addrs[map_page] + map_off;
		for (k = 0; k < to_copy; k += 8) {
			/* Record ports that became uncongested, ie
			 * bits that changed from 0 to 1. */
			uncongested |= ~(*src) & *dst;
			*dst++ = *src++;
		}
		kunmap_atomic(addr);

		copied += to_copy;

		map_off += to_copy;
		if (map_off == PAGE_SIZE) {
			map_off = 0;
			map_page++;
		}

		frag_off += to_copy;
		if (frag_off == RDS_FRAG_SIZE) {
			frag = list_entry(frag->f_item.next,
					  struct rds_page_frag, f_item);
			frag_off = 0;
		}
	}

	/* the congestion map is in little endian order */
	uncongested = le64_to_cpu(uncongested);

	rds_cong_map_updated(map, uncongested);
}

/*
 * Rings are posted with all the allocations they'll need to queue the
 * incoming message to the receiving socket so this can't fail.
 * All fragments start with a header, so we can make sure we're not receiving
 * garbage, and we can tell a small 8 byte fragment from an ACK frame.
 */
struct rds_iw_ack_state {
	u64		ack_next;
	u64		ack_recv;
	unsigned int	ack_required:1;
	unsigned int	ack_next_valid:1;
	unsigned int	ack_recv_valid:1;
};

static void rds_iw_process_recv(struct rds_connection *conn,
				struct rds_iw_recv_work *recv, u32 byte_len,
				struct rds_iw_ack_state *state)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	struct rds_iw_incoming *iwinc = ic->i_iwinc;
	struct rds_header *ihdr, *hdr;

	/* XXX shut down the connection if port 0,0 are seen? */

	rdsdebug("ic %p iwinc %p recv %p byte len %u\n", ic, iwinc, recv,
		 byte_len);

	if (byte_len < sizeof(struct rds_header)) {
		rds_iw_conn_error(conn, "incoming message "
		       "from %pI4 didn't include a "
		       "header, disconnecting and "
		       "reconnecting\n",
		       &conn->c_faddr);
		return;
	}
	byte_len -= sizeof(struct rds_header);

	ihdr = &ic->i_recv_hdrs[recv - ic->i_recvs];

	/* Validate the checksum. */
	if (!rds_message_verify_checksum(ihdr)) {
		rds_iw_conn_error(conn, "incoming message "
		       "from %pI4 has corrupted header - "
		       "forcing a reconnect\n",
		       &conn->c_faddr);
		rds_stats_inc(s_recv_drop_bad_checksum);
		return;
	}

	/* Process the ACK sequence which comes with every packet */
	state->ack_recv = be64_to_cpu(ihdr->h_ack);
	state->ack_recv_valid = 1;

	/* Process the credits update if there was one */
	if (ihdr->h_credit)
		rds_iw_send_add_credits(conn, ihdr->h_credit);

	if (ihdr->h_sport == 0 && ihdr->h_dport == 0 && byte_len == 0) {
		/* This is an ACK-only packet. The fact that it gets
		 * special treatment here is that historically, ACKs
		 * were rather special beasts.
		 */
		rds_iw_stats_inc(s_iw_ack_received);

		/*
		 * Usually the frags make their way on to incs and are then freed as
		 * the inc is freed.  We don't go that route, so we have to drop the
		 * page ref ourselves.  We can't just leave the page on the recv
		 * because that confuses the dma mapping of pages and each recv's use
		 * of a partial page.  We can leave the frag, though, it will be
		 * reused.
		 *
		 * FIXME: Fold this into the code path below.
		 */
		rds_iw_frag_drop_page(recv->r_frag);
		return;
	}

	/*
	 * If we don't already have an inc on the connection then this
	 * fragment has a header and starts a message.. copy its header
	 * into the inc and save the inc so we can hang upcoming fragments
	 * off its list.
	 */
	if (!iwinc) {
		iwinc = recv->r_iwinc;
		recv->r_iwinc = NULL;
		ic->i_iwinc = iwinc;

		hdr = &iwinc->ii_inc.i_hdr;
		memcpy(hdr, ihdr, sizeof(*hdr));
		ic->i_recv_data_rem = be32_to_cpu(hdr->h_len);

		rdsdebug("ic %p iwinc %p rem %u flag 0x%x\n", ic, iwinc,
			 ic->i_recv_data_rem, hdr->h_flags);
	} else {
		hdr = &iwinc->ii_inc.i_hdr;
		/* We can't just use memcmp here; fragments of a
		 * single message may carry different ACKs */
		if (hdr->h_sequence != ihdr->h_sequence ||
		    hdr->h_len != ihdr->h_len ||
		    hdr->h_sport != ihdr->h_sport ||
		    hdr->h_dport != ihdr->h_dport) {
			rds_iw_conn_error(conn,
				"fragment header mismatch; forcing reconnect\n");
			return;
		}
	}

	list_add_tail(&recv->r_frag->f_item, &iwinc->ii_frags);
	recv->r_frag = NULL;

	if (ic->i_recv_data_rem > RDS_FRAG_SIZE)
		ic->i_recv_data_rem -= RDS_FRAG_SIZE;
	else {
		ic->i_recv_data_rem = 0;
		ic->i_iwinc = NULL;

		if (iwinc->ii_inc.i_hdr.h_flags == RDS_FLAG_CONG_BITMAP)
			rds_iw_cong_recv(conn, iwinc);
		else {
			rds_recv_incoming(conn, conn->c_faddr, conn->c_laddr,
					  &iwinc->ii_inc, GFP_ATOMIC);
			state->ack_next = be64_to_cpu(hdr->h_sequence);
			state->ack_next_valid = 1;
		}

		/* Evaluate the ACK_REQUIRED flag *after* we received
		 * the complete frame, and after bumping the next_rx
		 * sequence. */
		if (hdr->h_flags & RDS_FLAG_ACK_REQUIRED) {
			rds_stats_inc(s_recv_ack_required);
			state->ack_required = 1;
		}

		rds_inc_put(&iwinc->ii_inc);
	}
}

/*
 * Plucking the oldest entry from the ring can be done concurrently with
 * the thread refilling the ring.  Each ring operation is protected by
 * spinlocks and the transient state of refilling doesn't change the
 * recording of which entry is oldest.
 *
 * This relies on IB only calling one cq comp_handler for each cq so that
 * there will only be one caller of rds_recv_incoming() per RDS connection.
 */
void rds_iw_recv_cq_comp_handler(struct ib_cq *cq, void *context)
{
	struct rds_connection *conn = context;
	struct rds_iw_connection *ic = conn->c_transport_data;

	rdsdebug("conn %p cq %p\n", conn, cq);

	rds_iw_stats_inc(s_iw_rx_cq_call);

	tasklet_schedule(&ic->i_recv_tasklet);
}

static inline void rds_poll_cq(struct rds_iw_connection *ic,
			       struct rds_iw_ack_state *state)
{
	struct rds_connection *conn = ic->conn;
	struct ib_wc wc;
	struct rds_iw_recv_work *recv;

	while (ib_poll_cq(ic->i_recv_cq, 1, &wc) > 0) {
		rdsdebug("wc wr_id 0x%llx status %u byte_len %u imm_data %u\n",
			 (unsigned long long)wc.wr_id, wc.status, wc.byte_len,
			 be32_to_cpu(wc.ex.imm_data));
		rds_iw_stats_inc(s_iw_rx_cq_event);

		recv = &ic->i_recvs[rds_iw_ring_oldest(&ic->i_recv_ring)];

		rds_iw_recv_unmap_page(ic, recv);

		/*
		 * Also process recvs in connecting state because it is possible
		 * to get a recv completion _before_ the rdmacm ESTABLISHED
		 * event is processed.
		 */
		if (rds_conn_up(conn) || rds_conn_connecting(conn)) {
			/* We expect errors as the qp is drained during shutdown */
			if (wc.status == IB_WC_SUCCESS) {
				rds_iw_process_recv(conn, recv, wc.byte_len, state);
			} else {
				rds_iw_conn_error(conn, "recv completion on "
				       "%pI4 had status %u, disconnecting and "
				       "reconnecting\n", &conn->c_faddr,
				       wc.status);
			}
		}

		rds_iw_ring_free(&ic->i_recv_ring, 1);
	}
}

void rds_iw_recv_tasklet_fn(unsigned long data)
{
	struct rds_iw_connection *ic = (struct rds_iw_connection *) data;
	struct rds_connection *conn = ic->conn;
	struct rds_iw_ack_state state = { 0, };

	rds_poll_cq(ic, &state);
	ib_req_notify_cq(ic->i_recv_cq, IB_CQ_SOLICITED);
	rds_poll_cq(ic, &state);

	if (state.ack_next_valid)
		rds_iw_set_ack(ic, state.ack_next, state.ack_required);
	if (state.ack_recv_valid && state.ack_recv > ic->i_ack_recv) {
		rds_send_drop_acked(conn, state.ack_recv, NULL);
		ic->i_ack_recv = state.ack_recv;
	}
	if (rds_conn_up(conn))
		rds_iw_attempt_ack(ic);

	/* If we ever end up with a really empty receive ring, we're
	 * in deep trouble, as the sender will definitely see RNR
	 * timeouts. */
	if (rds_iw_ring_empty(&ic->i_recv_ring))
		rds_iw_stats_inc(s_iw_rx_ring_empty);

	/*
	 * If the ring is running low, then schedule the thread to refill.
	 */
	if (rds_iw_ring_low(&ic->i_recv_ring))
		queue_delayed_work(rds_wq, &conn->c_recv_w, 0);
}

int rds_iw_recv(struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;
	int ret = 0;

	rdsdebug("conn %p\n", conn);

	/*
	 * If we get a temporary posting failure in this context then
	 * we're really low and we want the caller to back off for a bit.
	 */
	mutex_lock(&ic->i_recv_mutex);
	if (rds_iw_recv_refill(conn, GFP_KERNEL, GFP_HIGHUSER, 0))
		ret = -ENOMEM;
	else
		rds_iw_stats_inc(s_iw_rx_refill_from_thread);
	mutex_unlock(&ic->i_recv_mutex);

	if (rds_conn_up(conn))
		rds_iw_attempt_ack(ic);

	return ret;
}

int rds_iw_recv_init(void)
{
	struct sysinfo si;
	int ret = -ENOMEM;

	/* Default to 30% of all available RAM for recv memory */
	si_meminfo(&si);
	rds_iw_sysctl_max_recv_allocation = si.totalram / 3 * PAGE_SIZE / RDS_FRAG_SIZE;

	rds_iw_incoming_slab = kmem_cache_create("rds_iw_incoming",
					sizeof(struct rds_iw_incoming),
					0, 0, NULL);
	if (!rds_iw_incoming_slab)
		goto out;

	rds_iw_frag_slab = kmem_cache_create("rds_iw_frag",
					sizeof(struct rds_page_frag),
					0, 0, NULL);
	if (!rds_iw_frag_slab)
		kmem_cache_destroy(rds_iw_incoming_slab);
	else
		ret = 0;
out:
	return ret;
}

void rds_iw_recv_exit(void)
{
	kmem_cache_destroy(rds_iw_incoming_slab);
	kmem_cache_destroy(rds_iw_frag_slab);
}
