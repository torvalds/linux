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
	size_t total, hoff;
	void *buf;

	txb = kzalloc(sizeof(*txb), gfp);
	if (!txb)
		return NULL;

	hoff = round_up(sizeof(*whdr), data_align) - sizeof(*whdr);
	total = hoff + sizeof(*whdr) + data_size;

	data_align = umax(data_align, L1_CACHE_BYTES);
	mutex_lock(&call->conn->tx_data_alloc_lock);
	buf = page_frag_alloc_align(&call->conn->tx_data_alloc, total, gfp,
				    data_align);
	mutex_unlock(&call->conn->tx_data_alloc_lock);
	if (!buf) {
		kfree(txb);
		return NULL;
	}

	whdr = buf + hoff;

	refcount_set(&txb->ref, 1);
	txb->call_debug_id	= call->debug_id;
	txb->debug_id		= atomic_inc_return(&rxrpc_txbuf_debug_ids);
	txb->alloc_size		= data_size;
	txb->space		= data_size;
	txb->offset		= sizeof(*whdr);
	txb->flags		= call->conn->out_clientflag;
	txb->seq		= call->send_top + 1;
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
		if (txb->kvec[i].iov_base &&
		    !is_zero_pfn(page_to_pfn(virt_to_page(txb->kvec[i].iov_base))))
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
