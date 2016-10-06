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
void rxrpc_set_timer(struct rxrpc_call *call, enum rxrpc_timer_trace why,
		     ktime_t now)
{
	unsigned long t_j, now_j = jiffies;
	ktime_t t;
	bool queue = false;

	read_lock_bh(&call->state_lock);

	if (call->state < RXRPC_CALL_COMPLETE) {
		t = call->expire_at;
		if (!ktime_after(t, now)) {
			trace_rxrpc_timer(call, why, now, now_j);
			queue = true;
			goto out;
		}

		if (!ktime_after(call->resend_at, now)) {
			call->resend_at = call->expire_at;
			if (!test_and_set_bit(RXRPC_CALL_EV_RESEND, &call->events))
				queue = true;
		} else if (ktime_before(call->resend_at, t)) {
			t = call->resend_at;
		}

		if (!ktime_after(call->ack_at, now)) {
			call->ack_at = call->expire_at;
			if (!test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events))
				queue = true;
		} else if (ktime_before(call->ack_at, t)) {
			t = call->ack_at;
		}

		if (!ktime_after(call->ping_at, now)) {
			call->ping_at = call->expire_at;
			if (!test_and_set_bit(RXRPC_CALL_EV_PING, &call->events))
				queue = true;
		} else if (ktime_before(call->ping_at, t)) {
			t = call->ping_at;
		}

		t_j = nsecs_to_jiffies(ktime_to_ns(ktime_sub(t, now)));
		t_j += jiffies;

		/* We have to make sure that the calculated jiffies value falls
		 * at or after the nsec value, or we may loop ceaselessly
		 * because the timer times out, but we haven't reached the nsec
		 * timeout yet.
		 */
		t_j++;

		if (call->timer.expires != t_j || !timer_pending(&call->timer)) {
			mod_timer(&call->timer, t_j);
			trace_rxrpc_timer(call, why, now, now_j);
		}
	}

out:
	if (queue)
		rxrpc_queue_call(call);
	read_unlock_bh(&call->state_lock);
}

/*
 * Propose a PING ACK be sent.
 */
static void rxrpc_propose_ping(struct rxrpc_call *call,
			       bool immediate, bool background)
{
	if (immediate) {
		if (background &&
		    !test_and_set_bit(RXRPC_CALL_EV_PING, &call->events))
			rxrpc_queue_call(call);
	} else {
		ktime_t now = ktime_get_real();
		ktime_t ping_at = ktime_add_ms(now, rxrpc_idle_ack_delay);

		if (ktime_before(ping_at, call->ping_at)) {
			call->ping_at = ping_at;
			rxrpc_set_timer(call, rxrpc_timer_set_for_ping, now);
		}
	}
}

/*
 * propose an ACK be sent
 */
static void __rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
				u16 skew, u32 serial, bool immediate,
				bool background,
				enum rxrpc_propose_ack_trace why)
{
	enum rxrpc_propose_ack_outcome outcome = rxrpc_propose_ack_use;
	unsigned int expiry = rxrpc_soft_ack_delay;
	ktime_t now, ack_at;
	s8 prior = rxrpc_ack_priority[ack_reason];

	/* Pings are handled specially because we don't want to accidentally
	 * lose a ping response by subsuming it into a ping.
	 */
	if (ack_reason == RXRPC_ACK_PING) {
		rxrpc_propose_ping(call, immediate, background);
		goto trace;
	}

	/* Update DELAY, IDLE, REQUESTED and PING_RESPONSE ACK serial
	 * numbers, but we don't alter the timeout.
	 */
	_debug("prior %u %u vs %u %u",
	       ack_reason, prior,
	       call->ackr_reason, rxrpc_ack_priority[call->ackr_reason]);
	if (ack_reason == call->ackr_reason) {
		if (RXRPC_ACK_UPDATEABLE & (1 << ack_reason)) {
			outcome = rxrpc_propose_ack_update;
			call->ackr_serial = serial;
			call->ackr_skew = skew;
		}
		if (!immediate)
			goto trace;
	} else if (prior > rxrpc_ack_priority[call->ackr_reason]) {
		call->ackr_reason = ack_reason;
		call->ackr_serial = serial;
		call->ackr_skew = skew;
	} else {
		outcome = rxrpc_propose_ack_subsume;
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

	if (test_bit(RXRPC_CALL_EV_ACK, &call->events)) {
		_debug("already scheduled");
	} else if (immediate || expiry == 0) {
		_debug("immediate ACK %lx", call->events);
		if (!test_and_set_bit(RXRPC_CALL_EV_ACK, &call->events) &&
		    background)
			rxrpc_queue_call(call);
	} else {
		now = ktime_get_real();
		ack_at = ktime_add_ms(now, expiry);
		if (ktime_before(ack_at, call->ack_at)) {
			call->ack_at = ack_at;
			rxrpc_set_timer(call, rxrpc_timer_set_for_ack, now);
		}
	}

trace:
	trace_rxrpc_propose_ack(call, why, ack_reason, serial, immediate,
				background, outcome);
}

/*
 * propose an ACK be sent, locking the call structure
 */
void rxrpc_propose_ACK(struct rxrpc_call *call, u8 ack_reason,
		       u16 skew, u32 serial, bool immediate, bool background,
		       enum rxrpc_propose_ack_trace why)
{
	spin_lock_bh(&call->lock);
	__rxrpc_propose_ACK(call, ack_reason, skew, serial,
			    immediate, background, why);
	spin_unlock_bh(&call->lock);
}

/*
 * Handle congestion being detected by the retransmit timeout.
 */
static void rxrpc_congestion_timeout(struct rxrpc_call *call)
{
	set_bit(RXRPC_CALL_RETRANS_TIMEOUT, &call->flags);
}

/*
 * Perform retransmission of NAK'd and unack'd packets.
 */
static void rxrpc_resend(struct rxrpc_call *call, ktime_t now)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_seq_t cursor, seq, top;
	ktime_t max_age, oldest, ack_ts;
	int ix;
	u8 annotation, anno_type, retrans = 0, unacked = 0;

	_enter("{%d,%d}", call->tx_hard_ack, call->tx_top);

	max_age = ktime_sub_ms(now, rxrpc_resend_timeout);

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
	oldest = now;
	for (seq = cursor + 1; before_eq(seq, top); seq++) {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		annotation = call->rxtx_annotations[ix];
		anno_type = annotation & RXRPC_TX_ANNO_MASK;
		annotation &= ~RXRPC_TX_ANNO_MASK;
		if (anno_type == RXRPC_TX_ANNO_ACK)
			continue;

		skb = call->rxtx_buffer[ix];
		rxrpc_see_skb(skb, rxrpc_skb_tx_seen);
		sp = rxrpc_skb(skb);

		if (anno_type == RXRPC_TX_ANNO_UNACK) {
			if (ktime_after(skb->tstamp, max_age)) {
				if (ktime_before(skb->tstamp, oldest))
					oldest = skb->tstamp;
				continue;
			}
			if (!(annotation & RXRPC_TX_ANNO_RESENT))
				unacked++;
		}

		/* Okay, we need to retransmit a packet. */
		call->rxtx_annotations[ix] = RXRPC_TX_ANNO_RETRANS | annotation;
		retrans++;
		trace_rxrpc_retransmit(call, seq, annotation | anno_type,
				       ktime_to_ns(ktime_sub(skb->tstamp, max_age)));
	}

	call->resend_at = ktime_add_ms(oldest, rxrpc_resend_timeout);

	if (unacked)
		rxrpc_congestion_timeout(call);

	/* If there was nothing that needed retransmission then it's likely
	 * that an ACK got lost somewhere.  Send a ping to find out instead of
	 * retransmitting data.
	 */
	if (!retrans) {
		rxrpc_set_timer(call, rxrpc_timer_set_for_resend, now);
		spin_unlock_bh(&call->lock);
		ack_ts = ktime_sub(now, call->acks_latest_ts);
		if (ktime_to_ns(ack_ts) < call->peer->rtt)
			goto out;
		rxrpc_propose_ACK(call, RXRPC_ACK_PING, 0, 0, true, false,
				  rxrpc_propose_ack_ping_for_lost_ack);
		rxrpc_send_ack_packet(call, true);
		goto out;
	}

	/* Now go through the Tx window and perform the retransmissions.  We
	 * have to drop the lock for each send.  If an ACK comes in whilst the
	 * lock is dropped, it may clear some of the retransmission markers for
	 * packets that it soft-ACKs.
	 */
	for (seq = cursor + 1; before_eq(seq, top); seq++) {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		annotation = call->rxtx_annotations[ix];
		anno_type = annotation & RXRPC_TX_ANNO_MASK;
		if (anno_type != RXRPC_TX_ANNO_RETRANS)
			continue;

		skb = call->rxtx_buffer[ix];
		rxrpc_get_skb(skb, rxrpc_skb_tx_got);
		spin_unlock_bh(&call->lock);

		if (rxrpc_send_data_packet(call, skb, true) < 0) {
			rxrpc_free_skb(skb, rxrpc_skb_tx_freed);
			return;
		}

		if (rxrpc_is_client_call(call))
			rxrpc_expose_client_call(call);

		rxrpc_free_skb(skb, rxrpc_skb_tx_freed);
		spin_lock_bh(&call->lock);

		/* We need to clear the retransmit state, but there are two
		 * things we need to be aware of: A new ACK/NAK might have been
		 * received and the packet might have been hard-ACK'd (in which
		 * case it will no longer be in the buffer).
		 */
		if (after(seq, call->tx_hard_ack)) {
			annotation = call->rxtx_annotations[ix];
			anno_type = annotation & RXRPC_TX_ANNO_MASK;
			if (anno_type == RXRPC_TX_ANNO_RETRANS ||
			    anno_type == RXRPC_TX_ANNO_NAK) {
				annotation &= ~RXRPC_TX_ANNO_MASK;
				annotation |= RXRPC_TX_ANNO_UNACK;
			}
			annotation |= RXRPC_TX_ANNO_RESENT;
			call->rxtx_annotations[ix] = annotation;
		}

		if (after(call->tx_hard_ack, seq))
			seq = call->tx_hard_ack;
	}

out_unlock:
	spin_unlock_bh(&call->lock);
out:
	_leave("");
}

/*
 * Handle retransmission and deferred ACK/abort generation.
 */
void rxrpc_process_call(struct work_struct *work)
{
	struct rxrpc_call *call =
		container_of(work, struct rxrpc_call, processor);
	ktime_t now;

	rxrpc_see_call(call);

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx}",
	       call->debug_id, rxrpc_call_states[call->state], call->events);

recheck_state:
	if (test_and_clear_bit(RXRPC_CALL_EV_ABORT, &call->events)) {
		rxrpc_send_abort_packet(call);
		goto recheck_state;
	}

	if (call->state == RXRPC_CALL_COMPLETE) {
		del_timer_sync(&call->timer);
		rxrpc_notify_socket(call);
		goto out_put;
	}

	now = ktime_get_real();
	if (ktime_before(call->expire_at, now)) {
		rxrpc_abort_call("EXP", call, 0, RX_CALL_TIMEOUT, ETIME);
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		goto recheck_state;
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_ACK, &call->events)) {
		if (call->ackr_reason) {
			rxrpc_send_ack_packet(call, false);
			goto recheck_state;
		}
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_PING, &call->events)) {
		rxrpc_send_ack_packet(call, true);
		goto recheck_state;
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_RESEND, &call->events)) {
		rxrpc_resend(call, now);
		goto recheck_state;
	}

	rxrpc_set_timer(call, rxrpc_timer_set_for_resend, now);

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
