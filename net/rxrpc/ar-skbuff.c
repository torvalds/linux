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
		set_bit(RXRPC_CALL_ACK_FINAL, &call->events);
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
static void rxrpc_hard_ACK_data(struct rxrpc_call *call,
				struct rxrpc_skb_priv *sp)
{
	int loop;
	u32 seq;

	spin_lock_bh(&call->lock);

	_debug("hard ACK #%u", ntohl(sp->hdr.seq));

	for (loop = 0; loop < RXRPC_ACKR_WINDOW_ASZ; loop++) {
		call->ackr_window[loop] >>= 1;
		call->ackr_window[loop] |=
			call->ackr_window[loop + 1] << (BITS_PER_LONG - 1);
	}

	seq = ntohl(sp->hdr.seq);
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
		_debug("send Rx idle ACK");
		__rxrpc_propose_ACK(call, RXRPC_ACK_IDLE, sp->hdr.serial,
				    true);
	}

	spin_unlock_bh(&call->lock);
}

/*
 * destroy a packet that has an RxRPC control buffer
 * - advance the hard-ACK state of the parent call (done here in case something
 *   in the kernel bypasses recvmsg() and steals the packet directly off of the
 *   socket receive queue)
 */
void rxrpc_packet_destructor(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_call *call = sp->call;

	_enter("%p{%p}", skb, call);

	if (call) {
		/* send the final ACK on a client call */
		if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA)
			rxrpc_hard_ACK_data(call, sp);
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
