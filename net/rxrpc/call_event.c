/* Management of Tx window, Tx resend, ACKs and out-of-sequence reception
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
#include <linux/circ_buf.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/udp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Set the timer
 */
static void rxrpc_set_timer(struct rxrpc_call *call)
{
	unsigned long t, now = jiffies;

	_enter("{%ld,%ld,%ld:%ld}",
	       call->ack_at - now, call->resend_at - now, call->expire_at - now,
	       call->timer.expires - now);
	
	read_lock_bh(&call->state_lock);

	if (call->state < RXRPC_CALL_COMPLETE) {
		t = call->ack_at;
		if (time_before(call->resend_at, t))
			t = call->resend_at;
		if (time_before(call->expire_at, t))
			t = call->expire_at;
		if (!timer_pending(&call->timer) ||
		    time_before(t, call->timer.expires)) {
			_debug("set timer %ld", t - now);
			mod_timer(&call->timer, t);
		}
	}
	read_unlock_bh(&call->state_lock);
}

/*
 * propose an ACK be sent
 */
static void __rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
				u16 skew, u32 serial, bool immediate,
				bool background)
{
	unsigned long now, ack_at, expiry = rxrpc_soft_ack_delay;
	s8 prior = rxrpc_ack_priority[ack_reason];

	_enter("{%d},%s,%%%x,%u",
	       call->debug_id, rxrpc_acks(ack_reason), serial, immediate);

	/* Update DELAY, IDLE, REQUESTED and PING_RESPONSE ACK serial
	 * numbers, but we don't alter the timeout.
	 */
	_debug("prior %u %u vs %u %u",
	       ack_reason, prior,
	       call->ackr_reason, rxrpc_ack_priority[call->ackr_reason]);
	if (ack_reason == call->ackr_reason) {
		if (RXRPC_ACK_UPDATEABLE & (1 << ack_reason)) {
			call->ackr_serial = serial;
			call->ackr_skew = skew;
		}
		if (!immediate)
			return;
	} else if (prior > rxrpc_ack_priority[call->ackr_reason]) {
		call->ackr_reason = ack_reason;
		call->ackr_serial = serial;
		call->ackr_skew = skew;
	}

	switch (ack_reason) {
	case RXRPC_ACK_REQUESTED:
		if (rxrpc_requested_ack_delay < expiry)
			expiry = rxrpc_requested_ack_delay;
		if (serial == 1)
			immediate = false;
		break;

	case RXRPC_ACK_DELAY:
		if (rxrpc_soft_ack_delay < expiry)
			expiry = rxrpc_soft_ack_delay;
		break;

	case RXRPC_ACK_IDLE:
		if (rxrpc_idle_ack_delay < expiry)
			expiry = rxrpc_idle_ack_delay;
		break;

	default:
		immediate = true;
		break;
	}

	now = jiffies;
	if (test_bit(RXRPC_CALL_EV_ACK, &call->events)) {
		_debug("already scheduled");
	} else if (immediate || expiry == 0) {
		_debug("immediate ACK %lx", call->events);
		if (!test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events) &&
		    background)
			rxrpc_queue_call(call);
	} else {
		ack_at = now + expiry;
		_debug("deferred ACK %ld < %ld", expiry, call->ack_at - now);
		if (time_before(ack_at, call->ack_at)) {
			call->ack_at = ack_at;
			rxrpc_set_timer(call);
		}
	}
}

/*
 * propose an ACK be sent, locking the call structure
 */
void rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
		       u16 skew, u32 serial, bool immediate, bool background)
{
	spin_lock_bh(&call->lock);
	__rxrpc_propose_ACK(call, ack_reason, skew, serial,
			    immediate, background);
	spin_unlock_bh(&call->lock);
}

/*
 * Perform retransmission of NAK'd and unack'd packets.
 */
static void rxrpc_resend(struct rxrpc_call *call)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_seq_t cursor, seq, top;
	unsigned long resend_at, now;
	int ix;
	u8 annotation;

	_enter("{%d,%d}", call->tx_hard_ack, call->tx_top);

	spin_lock_bh(&call->lock);

	cursor = call->tx_hard_ack;
	top = call->tx_top;
	ASSERT(before_eq(cursor, top));
	if (cursor == top)
		goto out_unlock;

	/* Scan the packet list without dropping the lock and decide which of
	 * the packets in the Tx buffer we're going to resend and what the new
	 * resend timeout will be.
	 */
	now = jiffies;
	resend_at = now + rxrpc_resend_timeout;
	seq = cursor + 1;
	do {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		annotation = call->rxtx_annotations[ix];
		if (annotation == RXRPC_TX_ANNO_ACK)
			continue;

		skb = call->rxtx_buffer[ix];
		rxrpc_see_skb(skb);
		sp = rxrpc_skb(skb);

		if (annotation == RXRPC_TX_ANNO_UNACK) {
			if (time_after(sp->resend_at, now)) {
				if (time_before(sp->resend_at, resend_at))
					resend_at = sp->resend_at;
				continue;
			}
		}

		/* Okay, we need to retransmit a packet. */
		call->rxtx_annotations[ix] = RXRPC_TX_ANNO_RETRANS;
		seq++;
	} while (before_eq(seq, top));

	call->resend_at = resend_at;

	/* Now go through the Tx window and perform the retransmissions.  We
	 * have to drop the lock for each send.  If an ACK comes in whilst the
	 * lock is dropped, it may clear some of the retransmission markers for
	 * packets that it soft-ACKs.
	 */
	seq = cursor + 1;
	do {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		annotation = call->rxtx_annotations[ix];
		if (annotation != RXRPC_TX_ANNO_RETRANS)
			continue;

		skb = call->rxtx_buffer[ix];
		rxrpc_get_skb(skb);
		spin_unlock_bh(&call->lock);
		sp = rxrpc_skb(skb);

		/* Each Tx packet needs a new serial number */
		sp->hdr.serial = atomic_inc_return(&call->conn->serial);

		whdr = (struct rxrpc_wire_header *)skb->head;
		whdr->serial = htonl(sp->hdr.serial);

		if (rxrpc_send_data_packet(call->conn, skb) < 0) {
			call->resend_at = now + 2;
			rxrpc_free_skb(skb);
			return;
		}

		if (rxrpc_is_client_call(call))
			rxrpc_expose_client_call(call);
		sp->resend_at = now + rxrpc_resend_timeout;

		rxrpc_free_skb(skb);
		spin_lock_bh(&call->lock);

		/* We need to clear the retransmit state, but there are two
		 * things we need to be aware of: A new ACK/NAK might have been
		 * received and the packet might have been hard-ACK'd (in which
		 * case it will no longer be in the buffer).
		 */
		if (after(seq, call->tx_hard_ack) &&
		    (call->rxtx_annotations[ix] == RXRPC_TX_ANNO_RETRANS ||
		     call->rxtx_annotations[ix] == RXRPC_TX_ANNO_NAK))
			call->rxtx_annotations[ix] = RXRPC_TX_ANNO_UNACK;

		if (after(call->tx_hard_ack, seq))
			seq = call->tx_hard_ack;
		seq++;
	} while (before_eq(seq, top));

out_unlock:
	spin_unlock_bh(&call->lock);
	_leave("");
}

/*
 * Handle retransmission and deferred ACK/abort generation.
 */
void rxrpc_process_call(struct work_struct *work)
{
	struct rxrpc_call *call =
		container_of(work, struct rxrpc_call, processor);
	unsigned long now;

	rxrpc_see_call(call);

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx}",
	       call->debug_id, rxrpc_call_states[call->state], call->events);

recheck_state:
	if (test_and_clear_bit(RXRPC_CALL_EV_ABORT, &call->events)) {
		rxrpc_send_call_packet(call, RXRPC_PACKET_TYPE_ABORT);
		goto recheck_state;
	}

	if (call->state == RXRPC_CALL_COMPLETE) {
		del_timer_sync(&call->timer);
		goto out_put;
	}

	now = jiffies;
	if (time_after_eq(now, call->expire_at)) {
		rxrpc_abort_call("EXP", call, 0, RX_CALL_TIMEOUT, ETIME);
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_ACK, &call->events) ||
	    time_after_eq(now, call->ack_at)) {
		call->ack_at = call->expire_at;
		if (call->ackr_reason) {
			rxrpc_send_call_packet(call, RXRPC_PACKET_TYPE_ACK);
			goto recheck_state;
		}
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_RESEND, &call->events) ||
	    time_after_eq(now, call->resend_at)) {
		rxrpc_resend(call);
		goto recheck_state;
	}

	rxrpc_set_timer(call);

	/* other events may have been raised since we started checking */
	if (call->events && call->state < RXRPC_CALL_COMPLETE) {
		__rxrpc_queue_call(call);
		goto out;
	}

out_put:
	rxrpc_put_call(call, rxrpc_call_put);
out:
	_leave("");
}
