// SPDX-License-Identifier: GPL-2.0-or-later
/* Management of Tx window, Tx resend, ACKs and out-of-sequence reception
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
 * Propose a PING ACK be sent.
 */
void rxrpc_propose_ping(struct rxrpc_call *call, u32 serial,
			enum rxrpc_propose_ack_trace why)
{
	unsigned long now = jiffies;
	unsigned long ping_at = now + rxrpc_idle_ack_delay;

	if (time_before(ping_at, call->ping_at)) {
		WRITE_ONCE(call->ping_at, ping_at);
		rxrpc_reduce_call_timer(call, ping_at, now,
					rxrpc_timer_set_for_ping);
		trace_rxrpc_propose_ack(call, why, RXRPC_ACK_PING, serial);
	}
}

/*
 * Propose a DELAY ACK be sent in the future.
 */
void rxrpc_propose_delay_ACK(struct rxrpc_call *call, rxrpc_serial_t serial,
			     enum rxrpc_propose_ack_trace why)
{
	unsigned long expiry = rxrpc_soft_ack_delay;
	unsigned long now = jiffies, ack_at;

	call->ackr_serial = serial;

	if (rxrpc_soft_ack_delay < expiry)
		expiry = rxrpc_soft_ack_delay;
	if (call->peer->srtt_us != 0)
		ack_at = usecs_to_jiffies(call->peer->srtt_us >> 3);
	else
		ack_at = expiry;

	ack_at += READ_ONCE(call->tx_backoff);
	ack_at += now;
	if (time_before(ack_at, call->delay_ack_at)) {
		WRITE_ONCE(call->delay_ack_at, ack_at);
		rxrpc_reduce_call_timer(call, ack_at, now,
					rxrpc_timer_set_for_ack);
	}

	trace_rxrpc_propose_ack(call, why, RXRPC_ACK_DELAY, serial);
}

/*
 * Queue an ACK for immediate transmission.
 */
void rxrpc_send_ACK(struct rxrpc_call *call, u8 ack_reason,
		    rxrpc_serial_t serial, enum rxrpc_propose_ack_trace why)
{
	struct rxrpc_local *local = call->conn->params.local;
	struct rxrpc_txbuf *txb;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;
	if (ack_reason == RXRPC_ACK_DELAY &&
	    test_and_set_bit(RXRPC_CALL_DELAY_ACK_PENDING, &call->flags)) {
		trace_rxrpc_drop_ack(call, why, ack_reason, serial, false);
		return;
	}

	rxrpc_inc_stat(call->rxnet, stat_tx_acks[ack_reason]);

	txb = rxrpc_alloc_txbuf(call, RXRPC_PACKET_TYPE_ACK,
				in_softirq() ? GFP_ATOMIC | __GFP_NOWARN : GFP_NOFS);
	if (!txb) {
		kleave(" = -ENOMEM");
		return;
	}

	txb->ack_why		= why;
	txb->wire.seq		= 0;
	txb->wire.type		= RXRPC_PACKET_TYPE_ACK;
	txb->wire.flags		|= RXRPC_SLOW_START_OK;
	txb->ack.bufferSpace	= 0;
	txb->ack.maxSkew	= 0;
	txb->ack.firstPacket	= 0;
	txb->ack.previousPacket	= 0;
	txb->ack.serial		= htonl(serial);
	txb->ack.reason		= ack_reason;
	txb->ack.nAcks		= 0;

	if (!rxrpc_try_get_call(call, rxrpc_call_got)) {
		rxrpc_put_txbuf(txb, rxrpc_txbuf_put_nomem);
		return;
	}

	spin_lock_bh(&local->ack_tx_lock);
	list_add_tail(&txb->tx_link, &local->ack_tx_queue);
	spin_unlock_bh(&local->ack_tx_lock);
	trace_rxrpc_send_ack(call, why, ack_reason, serial);

	if (in_task()) {
		rxrpc_transmit_ack_packets(call->peer->local);
	} else {
		rxrpc_get_local(local);
		rxrpc_queue_local(local);
	}
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
static void rxrpc_resend(struct rxrpc_call *call, unsigned long now_j)
{
	struct rxrpc_ackpacket *ack = NULL;
	struct rxrpc_txbuf *txb;
	struct sk_buff *ack_skb = NULL;
	unsigned long resend_at;
	rxrpc_seq_t transmitted = READ_ONCE(call->tx_transmitted);
	ktime_t now, max_age, oldest, ack_ts;
	bool unacked = false;
	unsigned int i;
	LIST_HEAD(retrans_queue);

	_enter("{%d,%d}", call->acks_hard_ack, call->tx_top);

	now = ktime_get_real();
	max_age = ktime_sub_us(now, jiffies_to_usecs(call->peer->rto_j));
	oldest = now;

	/* See if there's an ACK saved with a soft-ACK table in it. */
	if (call->acks_soft_tbl) {
		spin_lock_bh(&call->acks_ack_lock);
		ack_skb = call->acks_soft_tbl;
		if (ack_skb) {
			rxrpc_get_skb(ack_skb, rxrpc_skb_ack);
			ack = (void *)ack_skb->data + sizeof(struct rxrpc_wire_header);
		}
		spin_unlock_bh(&call->acks_ack_lock);
	}

	if (list_empty(&call->tx_buffer))
		goto no_resend;

	spin_lock(&call->tx_lock);

	if (list_empty(&call->tx_buffer))
		goto no_further_resend;

	trace_rxrpc_resend(call);
	txb = list_first_entry(&call->tx_buffer, struct rxrpc_txbuf, call_link);

	/* Scan the soft ACK table without dropping the lock and resend any
	 * explicitly NAK'd packets.
	 */
	if (ack) {
		for (i = 0; i < ack->nAcks; i++) {
			rxrpc_seq_t seq;

			if (ack->acks[i] & 1)
				continue;
			seq = ntohl(ack->firstPacket) + i;
			if (after(txb->seq, transmitted))
				break;
			if (after(txb->seq, seq))
				continue; /* A new hard ACK probably came in */
			list_for_each_entry_from(txb, &call->tx_buffer, call_link) {
				if (txb->seq == seq)
					goto found_txb;
			}
			goto no_further_resend;

		found_txb:
			if (after(ntohl(txb->wire.serial), call->acks_highest_serial))
				continue; /* Ack point not yet reached */

			rxrpc_see_txbuf(txb, rxrpc_txbuf_see_unacked);

			if (list_empty(&txb->tx_link)) {
				rxrpc_get_txbuf(txb, rxrpc_txbuf_get_retrans);
				rxrpc_get_call(call, rxrpc_call_got_tx);
				list_add_tail(&txb->tx_link, &retrans_queue);
				set_bit(RXRPC_TXBUF_RESENT, &txb->flags);
			}

			trace_rxrpc_retransmit(call, txb->seq,
					       ktime_to_ns(ktime_sub(txb->last_sent,
								     max_age)));

			if (list_is_last(&txb->call_link, &call->tx_buffer))
				goto no_further_resend;
			txb = list_next_entry(txb, call_link);
		}
	}

	/* Fast-forward through the Tx queue to the point the peer says it has
	 * seen.  Anything between the soft-ACK table and that point will get
	 * ACK'd or NACK'd in due course, so don't worry about it here; here we
	 * need to consider retransmitting anything beyond that point.
	 *
	 * Note that ACK for a packet can beat the update of tx_transmitted.
	 */
	if (after_eq(READ_ONCE(call->acks_prev_seq), READ_ONCE(call->tx_transmitted)))
		goto no_further_resend;

	list_for_each_entry_from(txb, &call->tx_buffer, call_link) {
		if (before_eq(txb->seq, READ_ONCE(call->acks_prev_seq)))
			continue;
		if (after(txb->seq, READ_ONCE(call->tx_transmitted)))
			break; /* Not transmitted yet */

		if (ack && ack->reason == RXRPC_ACK_PING_RESPONSE &&
		    before(ntohl(txb->wire.serial), ntohl(ack->serial)))
			goto do_resend; /* Wasn't accounted for by a more recent ping. */

		if (ktime_after(txb->last_sent, max_age)) {
			if (ktime_before(txb->last_sent, oldest))
				oldest = txb->last_sent;
			continue;
		}

	do_resend:
		unacked = true;
		if (list_empty(&txb->tx_link)) {
			rxrpc_get_txbuf(txb, rxrpc_txbuf_get_retrans);
			list_add_tail(&txb->tx_link, &retrans_queue);
			set_bit(RXRPC_TXBUF_RESENT, &txb->flags);
			rxrpc_inc_stat(call->rxnet, stat_tx_data_retrans);
		}
	}

no_further_resend:
	spin_unlock(&call->tx_lock);
no_resend:
	rxrpc_free_skb(ack_skb, rxrpc_skb_freed);

	resend_at = nsecs_to_jiffies(ktime_to_ns(ktime_sub(now, oldest)));
	resend_at += jiffies + rxrpc_get_rto_backoff(call->peer,
						     !list_empty(&retrans_queue));
	WRITE_ONCE(call->resend_at, resend_at);

	if (unacked)
		rxrpc_congestion_timeout(call);

	/* If there was nothing that needed retransmission then it's likely
	 * that an ACK got lost somewhere.  Send a ping to find out instead of
	 * retransmitting data.
	 */
	if (list_empty(&retrans_queue)) {
		rxrpc_reduce_call_timer(call, resend_at, now_j,
					rxrpc_timer_set_for_resend);
		ack_ts = ktime_sub(now, call->acks_latest_ts);
		if (ktime_to_us(ack_ts) < (call->peer->srtt_us >> 3))
			goto out;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);
		goto out;
	}

	while ((txb = list_first_entry_or_null(&retrans_queue,
					       struct rxrpc_txbuf, tx_link))) {
		list_del_init(&txb->tx_link);
		rxrpc_send_data_packet(call, txb);
		rxrpc_put_txbuf(txb, rxrpc_txbuf_put_trans);

		trace_rxrpc_retransmit(call, txb->seq,
				       ktime_to_ns(ktime_sub(txb->last_sent,
							     max_age)));
	}

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
	unsigned long now, next, t;
	unsigned int iterations = 0;
	rxrpc_serial_t ackr_serial;

	rxrpc_see_call(call);

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx}",
	       call->debug_id, rxrpc_call_states[call->state], call->events);

recheck_state:
	/* Limit the number of times we do this before returning to the manager */
	iterations++;
	if (iterations > 5)
		goto requeue;

	if (test_and_clear_bit(RXRPC_CALL_EV_ABORT, &call->events)) {
		rxrpc_send_abort_packet(call);
		goto recheck_state;
	}

	if (READ_ONCE(call->acks_hard_ack) != call->tx_bottom)
		rxrpc_shrink_call_tx_buffer(call);

	if (call->state == RXRPC_CALL_COMPLETE) {
		rxrpc_delete_call_timer(call);
		goto out_put;
	}

	/* Work out if any timeouts tripped */
	now = jiffies;
	t = READ_ONCE(call->expect_rx_by);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_normal, now);
		set_bit(RXRPC_CALL_EV_EXPIRED, &call->events);
	}

	t = READ_ONCE(call->expect_req_by);
	if (call->state == RXRPC_CALL_SERVER_RECV_REQUEST &&
	    time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_idle, now);
		set_bit(RXRPC_CALL_EV_EXPIRED, &call->events);
	}

	t = READ_ONCE(call->expect_term_by);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_hard, now);
		set_bit(RXRPC_CALL_EV_EXPIRED, &call->events);
	}

	t = READ_ONCE(call->delay_ack_at);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_ack, now);
		cmpxchg(&call->delay_ack_at, t, now + MAX_JIFFY_OFFSET);
		ackr_serial = xchg(&call->ackr_serial, 0);
		rxrpc_send_ACK(call, RXRPC_ACK_DELAY, ackr_serial,
			       rxrpc_propose_ack_ping_for_lost_ack);
	}

	t = READ_ONCE(call->ack_lost_at);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_lost_ack, now);
		cmpxchg(&call->ack_lost_at, t, now + MAX_JIFFY_OFFSET);
		set_bit(RXRPC_CALL_EV_ACK_LOST, &call->events);
	}

	t = READ_ONCE(call->keepalive_at);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_keepalive, now);
		cmpxchg(&call->keepalive_at, t, now + MAX_JIFFY_OFFSET);
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	t = READ_ONCE(call->ping_at);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_ping, now);
		cmpxchg(&call->ping_at, t, now + MAX_JIFFY_OFFSET);
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	t = READ_ONCE(call->resend_at);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_resend, now);
		cmpxchg(&call->resend_at, t, now + MAX_JIFFY_OFFSET);
		set_bit(RXRPC_CALL_EV_RESEND, &call->events);
	}

	/* Process events */
	if (test_and_clear_bit(RXRPC_CALL_EV_EXPIRED, &call->events)) {
		if (test_bit(RXRPC_CALL_RX_HEARD, &call->flags) &&
		    (int)call->conn->hi_serial - (int)call->rx_serial > 0) {
			trace_rxrpc_call_reset(call);
			rxrpc_abort_call("EXP", call, 0, RX_CALL_DEAD, -ECONNRESET);
		} else {
			rxrpc_abort_call("EXP", call, 0, RX_CALL_TIMEOUT, -ETIME);
		}
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		goto recheck_state;
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events)) {
		call->acks_lost_top = call->tx_top;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_RESEND, &call->events) &&
	    call->state != RXRPC_CALL_CLIENT_RECV_REPLY) {
		rxrpc_resend(call, now);
		goto recheck_state;
	}

	/* Make sure the timer is restarted */
	next = call->expect_rx_by;

#define set(T) { t = READ_ONCE(T); if (time_before(t, next)) next = t; }

	set(call->expect_req_by);
	set(call->expect_term_by);
	set(call->delay_ack_at);
	set(call->ack_lost_at);
	set(call->resend_at);
	set(call->keepalive_at);
	set(call->ping_at);

	now = jiffies;
	if (time_after_eq(now, next))
		goto recheck_state;

	rxrpc_reduce_call_timer(call, next, now, rxrpc_timer_restart);

	/* other events may have been raised since we started checking */
	if (call->events && call->state < RXRPC_CALL_COMPLETE)
		goto requeue;

out_put:
	rxrpc_put_call(call, rxrpc_call_put);
out:
	_leave("");
	return;

requeue:
	__rxrpc_queue_call(call);
	goto out;
}
