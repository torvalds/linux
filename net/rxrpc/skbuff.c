/* ar-skbuff.c: socket buffer destruction handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

#define select_skb_count(op) (op >= rxrpc_skb_tx_cleaned ? &rxrpc_n_tx_skbs : &rxrpc_n_rx_skbs)

/*
 * Note the allocation or reception of a socket buffer.
 */
void rxrpc_new_skb(struct sk_buff *skb, enum rxrpc_skb_trace op)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(select_skb_count(op));
	trace_rxrpc_skb(skb, op, refcount_read(&skb->users), n, here);
}

/*
 * Note the re-emergence of a socket buffer from a queue or buffer.
 */
void rxrpc_see_skb(struct sk_buff *skb, enum rxrpc_skb_trace op)
{
	const void *here = __builtin_return_address(0);
	if (skb) {
		int n = atomic_read(select_skb_count(op));
		trace_rxrpc_skb(skb, op, refcount_read(&skb->users), n, here);
	}
}

/*
 * Note the addition of a ref on a socket buffer.
 */
void rxrpc_get_skb(struct sk_buff *skb, enum rxrpc_skb_trace op)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(select_skb_count(op));
	trace_rxrpc_skb(skb, op, refcount_read(&skb->users), n, here);
	skb_get(skb);
}

/*
 * Note the destruction of a socket buffer.
 */
void rxrpc_free_skb(struct sk_buff *skb, enum rxrpc_skb_trace op)
{
	const void *here = __builtin_return_address(0);
	if (skb) {
		int n;
		CHECK_SLAB_OKAY(&skb->users);
		n = atomic_dec_return(select_skb_count(op));
		trace_rxrpc_skb(skb, op, refcount_read(&skb->users), n, here);
		kfree_skb(skb);
	}
}

/*
 * Note the injected loss of a socket buffer.
 */
void rxrpc_lose_skb(struct sk_buff *skb, enum rxrpc_skb_trace op)
{
	const void *here = __builtin_return_address(0);
	if (skb) {
		int n;
		CHECK_SLAB_OKAY(&skb->users);
		n = atomic_dec_return(select_skb_count(op));
		trace_rxrpc_skb(skb, op, refcount_read(&skb->users), n, here);
		kfree_skb(skb);
	}
}

/*
 * Clear a queue of socket buffers.
 */
void rxrpc_purge_queue(struct sk_buff_head *list)
{
	const void *here = __builtin_return_address(0);
	struct sk_buff *skb;
	while ((skb = skb_dequeue((list))) != NULL) {
		int n = atomic_dec_return(select_skb_count(rxrpc_skb_rx_purged));
		trace_rxrpc_skb(skb, rxrpc_skb_rx_purged,
				refcount_read(&skb->users), n, here);
		kfree_skb(skb);
	}
}
