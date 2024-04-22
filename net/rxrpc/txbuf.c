// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC Tx data buffering.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include "ar-internal.h"

static atomic_t rxrpc_txbuf_debug_ids;
atomic_t rxrpc_nr_txbuf;

/*
 * Allocate and partially initialise a data transmission buffer.
 */
struct rxrpc_txbuf *rxrpc_alloc_data_txbuf(struct rxrpc_call *call, size_t data_size,
					   size_t data_align, gfp_t gfp)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_txbuf *txb;
	size_t total, hoff = 0;
	void *buf;

	txb = kmalloc(sizeof(*txb), gfp);
	if (!txb)
		return NULL;

	if (data_align)
		hoff = round_up(sizeof(*whdr), data_align) - sizeof(*whdr);
	total = hoff + sizeof(*whdr) + data_size;

	mutex_lock(&call->conn->tx_data_alloc_lock);
	buf = __page_frag_alloc_align(&call->conn->tx_data_alloc, total, gfp,
				      ~(data_align - 1) & ~(L1_CACHE_BYTES - 1));
	mutex_unlock(&call->conn->tx_data_alloc_lock);
	if (!buf) {
		kfree(txb);
		return NULL;
	}

	whdr = buf + hoff;

	INIT_LIST_HEAD(&txb->call_link);
	INIT_LIST_HEAD(&txb->tx_link);
	refcount_set(&txb->ref, 1);
	txb->last_sent		= KTIME_MIN;
	txb->call_debug_id	= call->debug_id;
	txb->debug_id		= atomic_inc_return(&rxrpc_txbuf_debug_ids);
	txb->space		= data_size;
	txb->len		= 0;
	txb->offset		= sizeof(*whdr);
	txb->flags		= call->conn->out_clientflag;
	txb->ack_why		= 0;
	txb->seq		= call->tx_prepared + 1;
	txb->serial		= 0;
	txb->cksum		= 0;
	txb->nr_kvec		= 1;
	txb->kvec[0].iov_base	= whdr;
	txb->kvec[0].iov_len	= sizeof(*whdr);

	whdr->epoch		= htonl(call->conn->proto.epoch);
	whdr->cid		= htonl(call->cid);
	whdr->callNumber	= htonl(call->call_id);
	whdr->seq		= htonl(txb->seq);
	whdr->type		= RXRPC_PACKET_TYPE_DATA;
	whdr->flags		= 0;
	whdr->userStatus	= 0;
	whdr->securityIndex	= call->security_ix;
	whdr->_rsvd		= 0;
	whdr->serviceId		= htons(call->dest_srx.srx_service);

	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, 1,
			  rxrpc_txbuf_alloc_data);

	atomic_inc(&rxrpc_nr_txbuf);
	return txb;
}

/*
 * Allocate and partially initialise an ACK packet.
 */
struct rxrpc_txbuf *rxrpc_alloc_ack_txbuf(struct rxrpc_call *call, size_t sack_size)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_acktrailer *trailer;
	struct rxrpc_ackpacket *ack;
	struct rxrpc_txbuf *txb;
	gfp_t gfp = rcu_read_lock_held() ? GFP_ATOMIC | __GFP_NOWARN : GFP_NOFS;
	void *buf, *buf2 = NULL;
	u8 *filler;

	txb = kmalloc(sizeof(*txb), gfp);
	if (!txb)
		return NULL;

	buf = page_frag_alloc(&call->local->tx_alloc,
			      sizeof(*whdr) + sizeof(*ack) + 1 + 3 + sizeof(*trailer), gfp);
	if (!buf) {
		kfree(txb);
		return NULL;
	}

	if (sack_size) {
		buf2 = page_frag_alloc(&call->local->tx_alloc, sack_size, gfp);
		if (!buf2) {
			page_frag_free(buf);
			kfree(txb);
			return NULL;
		}
	}

	whdr	= buf;
	ack	= buf + sizeof(*whdr);
	filler	= buf + sizeof(*whdr) + sizeof(*ack) + 1;
	trailer	= buf + sizeof(*whdr) + sizeof(*ack) + 1 + 3;

	INIT_LIST_HEAD(&txb->call_link);
	INIT_LIST_HEAD(&txb->tx_link);
	refcount_set(&txb->ref, 1);
	txb->call_debug_id	= call->debug_id;
	txb->debug_id		= atomic_inc_return(&rxrpc_txbuf_debug_ids);
	txb->space		= 0;
	txb->len		= sizeof(*whdr) + sizeof(*ack) + 3 + sizeof(*trailer);
	txb->offset		= 0;
	txb->flags		= call->conn->out_clientflag;
	txb->ack_rwind		= 0;
	txb->seq		= 0;
	txb->serial		= 0;
	txb->cksum		= 0;
	txb->nr_kvec		= 3;
	txb->kvec[0].iov_base	= whdr;
	txb->kvec[0].iov_len	= sizeof(*whdr) + sizeof(*ack);
	txb->kvec[1].iov_base	= buf2;
	txb->kvec[1].iov_len	= sack_size;
	txb->kvec[2].iov_base	= filler;
	txb->kvec[2].iov_len	= 3 + sizeof(*trailer);

	whdr->epoch		= htonl(call->conn->proto.epoch);
	whdr->cid		= htonl(call->cid);
	whdr->callNumber	= htonl(call->call_id);
	whdr->seq		= 0;
	whdr->type		= RXRPC_PACKET_TYPE_ACK;
	whdr->flags		= 0;
	whdr->userStatus	= 0;
	whdr->securityIndex	= call->security_ix;
	whdr->_rsvd		= 0;
	whdr->serviceId		= htons(call->dest_srx.srx_service);

	get_page(virt_to_head_page(trailer));

	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, 1,
			  rxrpc_txbuf_alloc_ack);
	atomic_inc(&rxrpc_nr_txbuf);
	return txb;
}

void rxrpc_get_txbuf(struct rxrpc_txbuf *txb, enum rxrpc_txbuf_trace what)
{
	int r;

	__refcount_inc(&txb->ref, &r);
	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, r + 1, what);
}

void rxrpc_see_txbuf(struct rxrpc_txbuf *txb, enum rxrpc_txbuf_trace what)
{
	int r = refcount_read(&txb->ref);

	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, r, what);
}

static void rxrpc_free_txbuf(struct rxrpc_txbuf *txb)
{
	int i;

	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, 0,
			  rxrpc_txbuf_free);
	for (i = 0; i < txb->nr_kvec; i++)
		if (txb->kvec[i].iov_base)
			page_frag_free(txb->kvec[i].iov_base);
	kfree(txb);
	atomic_dec(&rxrpc_nr_txbuf);
}

void rxrpc_put_txbuf(struct rxrpc_txbuf *txb, enum rxrpc_txbuf_trace what)
{
	unsigned int debug_id, call_debug_id;
	rxrpc_seq_t seq;
	bool dead;
	int r;

	if (txb) {
		debug_id = txb->debug_id;
		call_debug_id = txb->call_debug_id;
		seq = txb->seq;
		dead = __refcount_dec_and_test(&txb->ref, &r);
		trace_rxrpc_txbuf(debug_id, call_debug_id, seq, r - 1, what);
		if (dead)
			rxrpc_free_txbuf(txb);
	}
}

/*
 * Shrink the transmit buffer.
 */
void rxrpc_shrink_call_tx_buffer(struct rxrpc_call *call)
{
	struct rxrpc_txbuf *txb;
	rxrpc_seq_t hard_ack = smp_load_acquire(&call->acks_hard_ack);
	bool wake = false;

	_enter("%x/%x/%x", call->tx_bottom, call->acks_hard_ack, call->tx_top);

	while ((txb = list_first_entry_or_null(&call->tx_buffer,
					       struct rxrpc_txbuf, call_link))) {
		hard_ack = smp_load_acquire(&call->acks_hard_ack);
		if (before(hard_ack, txb->seq))
			break;

		if (txb->seq != call->tx_bottom + 1)
			rxrpc_see_txbuf(txb, rxrpc_txbuf_see_out_of_step);
		ASSERTCMP(txb->seq, ==, call->tx_bottom + 1);
		smp_store_release(&call->tx_bottom, call->tx_bottom + 1);
		list_del_rcu(&txb->call_link);

		trace_rxrpc_txqueue(call, rxrpc_txqueue_dequeue);

		rxrpc_put_txbuf(txb, rxrpc_txbuf_put_rotated);
		if (after(call->acks_hard_ack, call->tx_bottom + 128))
			wake = true;
	}

	if (wake)
		wake_up(&call->waitq);
}
