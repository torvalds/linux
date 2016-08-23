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

/*
 * set up for the ACK at the end of the receive phase when we discard the final
 * receive phase data packet
 * - called with softirqs disabled
 */
static void rxrpc_request_final_ACK(struct rxrpc_call *call)
{
	/* the call may be aborted before we have a chance to ACK it */
	write_lock(&call->state_lock);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_RECV_REPLY:
		call->state = RXRPC_CALL_CLIENT_FINAL_ACK;
		_debug("request final ACK");

		/* get an extra ref on the call for the final-ACK generator to
		 * release */
		rxrpc_get_call(call);
		set_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events);
		if (try_to_del_timer_sync(&call->ack_timer) >= 0)
			rxrpc_queue_call(call);
		break;

	case RXRPC_CALL_SERVER_RECV_REQUEST:
		call->state = RXRPC_CALL_SERVER_ACK_REQUEST;
	default:
		break;
	}

	write_unlock(&call->state_lock);
}

/*
 * drop the bottom ACK off of the call ACK window and advance the window
 */
static void rxrpc_hard_ACK_data(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	int loop;
	u32 seq;

	spin_lock_bh(&call->lock);

	_debug("hard ACK #%u", sp->hdr.seq);

	for (loop = 0; loop < RXRPC_ACKR_WINDOW_ASZ; loop++) {
		call->ackr_window[loop] >>= 1;
		call->ackr_window[loop] |=
			call->ackr_window[loop + 1] << (BITS_PER_LONG - 1);
	}

	seq = sp->hdr.seq;
	ASSERTCMP(seq, ==, call->rx_data_eaten + 1);
	call->rx_data_eaten = seq;

	if (call->ackr_win_top < UINT_MAX)
		call->ackr_win_top++;

	ASSERTIFCMP(call->state <= RXRPC_CALL_COMPLETE,
		    call->rx_data_post, >=, call->rx_data_recv);
	ASSERTIFCMP(call->state <= RXRPC_CALL_COMPLETE,
		    call->rx_data_recv, >=, call->rx_data_eaten);

	if (sp->hdr.flags & RXRPC_LAST_PACKET) {
		rxrpc_request_final_ACK(call);
	} else if (atomic_dec_and_test(&call->ackr_not_idle) &&
		   test_and_clear_bit(RXRPC_CALL_TX_SOFT_ACK, &call->flags)) {
		/* We previously soft-ACK'd some received packets that have now
		 * been consumed, so send a hard-ACK if no more packets are
		 * immediately forthcoming to allow the transmitter to free up
		 * its Tx bufferage.
		 */
		_debug("send Rx idle ACK");
		__rxrpc_propose_ACK(call, RXRPC_ACK_IDLE,
				    skb->priority, sp->hdr.serial, false);
	}

	spin_unlock_bh(&call->lock);
}

/**
 * rxrpc_kernel_data_consumed - Record consumption of data message
 * @call: The call to which the message pertains.
 * @skb: Message holding data
 *
 * Record the consumption of a data message and generate an ACK if appropriate.
 * The call state is shifted if this was the final packet.  The caller must be
 * in process context with no spinlocks held.
 *
 * TODO: Actually generate the ACK here rather than punting this to the
 * workqueue.
 */
void rxrpc_kernel_data_consumed(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	_enter("%d,%p{%u}", call->debug_id, skb, sp->hdr.seq);

	ASSERTCMP(sp->call, ==, call);
	ASSERTCMP(sp->hdr.type, ==, RXRPC_PACKET_TYPE_DATA);

	/* TODO: Fix the sequence number tracking */
	ASSERTCMP(sp->hdr.seq, >=, call->rx_data_recv);
	ASSERTCMP(sp->hdr.seq, <=, call->rx_data_recv + 1);
	ASSERTCMP(sp->hdr.seq, >, call->rx_data_eaten);

	call->rx_data_recv = sp->hdr.seq;
	rxrpc_hard_ACK_data(call, skb);
}
EXPORT_SYMBOL(rxrpc_kernel_data_consumed);

/*
 * Destroy a packet that has an RxRPC control buffer
 */
void rxrpc_packet_destructor(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_call *call = sp->call;

	_enter("%p{%p}", skb, call);

	if (call) {
		if (atomic_dec_return(&call->skb_count) < 0)
			BUG();
		rxrpc_put_call(call);
		sp->call = NULL;
	}

	if (skb->sk)
		sock_rfree(skb);
	_leave("");
}

/**
 * rxrpc_kernel_free_skb - Free an RxRPC socket buffer
 * @skb: The socket buffer to be freed
 *
 * Let RxRPC free its own socket buffer, permitting it to maintain debug
 * accounting.
 */
void rxrpc_kernel_free_skb(struct sk_buff *skb)
{
	rxrpc_free_skb(skb);
}
EXPORT_SYMBOL(rxrpc_kernel_free_skb);

/*
 * Note the existence of a new-to-us socket buffer (allocated or dequeued).
 */
void rxrpc_new_skb(struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&rxrpc_n_skbs);
	trace_rxrpc_skb(skb, 0, atomic_read(&skb->users), n, here);
}

/*
 * Note the re-emergence of a socket buffer from a queue or buffer.
 */
void rxrpc_see_skb(struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	if (skb) {
		int n = atomic_read(&rxrpc_n_skbs);
		trace_rxrpc_skb(skb, 1, atomic_read(&skb->users), n, here);
	}
}

/*
 * Note the addition of a ref on a socket buffer.
 */
void rxrpc_get_skb(struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	int n = atomic_inc_return(&rxrpc_n_skbs);
	trace_rxrpc_skb(skb, 2, atomic_read(&skb->users), n, here);
	skb_get(skb);
}

/*
 * Note the destruction of a socket buffer.
 */
void rxrpc_free_skb(struct sk_buff *skb)
{
	const void *here = __builtin_return_address(0);
	if (skb) {
		int n;
		CHECK_SLAB_OKAY(&skb->users);
		n = atomic_dec_return(&rxrpc_n_skbs);
		trace_rxrpc_skb(skb, 3, atomic_read(&skb->users), n, here);
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
		int n = atomic_dec_return(&rxrpc_n_skbs);
		trace_rxrpc_skb(skb, 4, atomic_read(&skb->users), n, here);
		kfree_skb(skb);
	}
}
