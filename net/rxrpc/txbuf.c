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
 * Allocate and partially initialise an I/O request structure.
 */
struct rxrpc_txbuf *rxrpc_alloc_txbuf(struct rxrpc_call *call, u8 packet_type,
				      gfp_t gfp)
{
	struct rxrpc_txbuf *txb;

	txb = kmalloc(sizeof(*txb), gfp);
	if (txb) {
		INIT_LIST_HEAD(&txb->call_link);
		INIT_LIST_HEAD(&txb->tx_link);
		refcount_set(&txb->ref, 1);
		txb->call_debug_id	= call->debug_id;
		txb->debug_id		= atomic_inc_return(&rxrpc_txbuf_debug_ids);
		txb->space		= sizeof(txb->data);
		txb->len		= 0;
		txb->offset		= 0;
		txb->flags		= 0;
		txb->ack_why		= 0;
		txb->seq		= call->tx_prepared + 1;
		txb->wire.epoch		= htonl(call->conn->proto.epoch);
		txb->wire.cid		= htonl(call->cid);
		txb->wire.callNumber	= htonl(call->call_id);
		txb->wire.seq		= htonl(txb->seq);
		txb->wire.type		= packet_type;
		txb->wire.flags		= call->conn->out_clientflag;
		txb->wire.userStatus	= 0;
		txb->wire.securityIndex	= call->security_ix;
		txb->wire._rsvd		= 0;
		txb->wire.serviceId	= htons(call->dest_srx.srx_service);

		trace_rxrpc_txbuf(txb->debug_id,
				  txb->call_debug_id, txb->seq, 1,
				  packet_type == RXRPC_PACKET_TYPE_DATA ?
				  rxrpc_txbuf_alloc_data :
				  rxrpc_txbuf_alloc_ack);
		atomic_inc(&rxrpc_nr_txbuf);
	}

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

static void rxrpc_free_txbuf(struct rcu_head *rcu)
{
	struct rxrpc_txbuf *txb = container_of(rcu, struct rxrpc_txbuf, rcu);

	trace_rxrpc_txbuf(txb->debug_id, txb->call_debug_id, txb->seq, 0,
			  rxrpc_txbuf_free);
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
			call_rcu(&txb->rcu, rxrpc_free_txbuf);
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
