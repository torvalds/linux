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
	struct rxrpc_txbuf *txb;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;

	rxrpc_inc_stat(call->rxnet, stat_tx_acks[ack_reason]);

	txb = rxrpc_alloc_txbuf(call, RXRPC_PACKET_TYPE_ACK,
				rcu_read_lock_held() ? GFP_ATOMIC | __GFP_NOWARN : GFP_NOFS);
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

	trace_rxrpc_send_ack(call, why, ack_reason, serial);
	rxrpc_send_ack_packet(call, txb);
	rxrpc_put_txbuf(txb, rxrpc_txbuf_put_ack_tx);
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
void rxrpc_resend(struct rxrpc_call *call, struct sk_buff *ack_skb)
{
	struct rxrpc_ackpacket *ack = NULL;
	struct rxrpc_txbuf *txb;
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

	if (list_empty(&call->tx_buffer))
		goto no_resend;

	if (list_empty(&call->tx_buffer))
		goto no_further_resend;

	trace_rxrpc_resend(call, ack_skb);
	txb = list_first_entry(&call->tx_buffer, struct rxrpc_txbuf, call_link);

	/* Scan the soft ACK table without dropping the lock and resend any
	 * explicitly NAK'd packets.
	 */
	if (ack_skb) {
		ack = (void *)ack_skb->data + sizeof(struct rxrpc_wire_header);

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
			list_add_tail(&txb->tx_link, &retrans_queue);
			set_bit(RXRPC_TXBUF_RESENT, &txb->flags);
			rxrpc_inc_stat(call->rxnet, stat_tx_data_retrans);
		}
	}

no_further_resend:
no_resend:
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
		rxrpc_reduce_call_timer(call, resend_at, jiffies,
					rxrpc_timer_set_for_resend);
		ack_ts = ktime_sub(now, call->acks_latest_ts);
		if (ktime_to_us(ack_ts) < (call->peer->srtt_us >> 3))
			goto out;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);
		goto out;
	}

	/* Retransmit the queue */
	while ((txb = list_first_entry_or_null(&retrans_queue,
					       struct rxrpc_txbuf, tx_link))) {
		list_del_init(&txb->tx_link);
		rxrpc_transmit_one(call, txb);
	}

out:
	_leave("");
}

/*
 * Start transmitting the reply to a service.  This cancels the need to ACK the
 * request if we haven't yet done so.
 */
static void rxrpc_begin_service_reply(struct rxrpc_call *call)
{
	unsigned long now = jiffies;

	rxrpc_set_call_state(call, RXRPC_CALL_SERVER_SEND_REPLY);
	WRITE_ONCE(call->delay_ack_at, now + MAX_JIFFY_OFFSET);
	if (call->ackr_reason == RXRPC_ACK_DELAY)
		call->ackr_reason = 0;
	trace_rxrpc_timer(call, rxrpc_timer_init_for_send_reply, now);
}

/*
 * Close the transmission phase.  After this point there is no more data to be
 * transmitted in the call.
 */
static void rxrpc_close_tx_phase(struct rxrpc_call *call)
{
	_debug("________awaiting reply/ACK__________");

	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
		rxrpc_set_call_state(call, RXRPC_CALL_CLIENT_AWAIT_REPLY);
		break;
	case RXRPC_CALL_SERVER_SEND_REPLY:
		rxrpc_set_call_state(call, RXRPC_CALL_SERVER_AWAIT_ACK);
		break;
	default:
		break;
	}
}

static bool rxrpc_tx_window_has_space(struct rxrpc_call *call)
{
	unsigned int winsize = min_t(unsigned int, call->tx_winsize,
				     call->cong_cwnd + call->cong_extra);
	rxrpc_seq_t window = call->acks_hard_ack, wtop = window + winsize;
	rxrpc_seq_t tx_top = call->tx_top;
	int space;

	space = wtop - tx_top;
	return space > 0;
}

/*
 * Decant some if the sendmsg prepared queue into the transmission buffer.
 */
static void rxrpc_decant_prepared_tx(struct rxrpc_call *call)
{
	struct rxrpc_txbuf *txb;

	if (!test_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		if (list_empty(&call->tx_sendmsg))
			return;
		rxrpc_expose_client_call(call);
	}

	while ((txb = list_first_entry_or_null(&call->tx_sendmsg,
					       struct rxrpc_txbuf, call_link))) {
		spin_lock(&call->tx_lock);
		list_del(&txb->call_link);
		spin_unlock(&call->tx_lock);

		call->tx_top = txb->seq;
		list_add_tail(&txb->call_link, &call->tx_buffer);

		if (txb->wire.flags & RXRPC_LAST_PACKET)
			rxrpc_close_tx_phase(call);

		rxrpc_transmit_one(call, txb);

		if (!rxrpc_tx_window_has_space(call))
			break;
	}
}

static void rxrpc_transmit_some_data(struct rxrpc_call *call)
{
	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_SERVER_ACK_REQUEST:
		if (list_empty(&call->tx_sendmsg))
			return;
		rxrpc_begin_service_reply(call);
		fallthrough;

	case RXRPC_CALL_SERVER_SEND_REPLY:
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
		if (!rxrpc_tx_window_has_space(call))
			return;
		if (list_empty(&call->tx_sendmsg)) {
			rxrpc_inc_stat(call->rxnet, stat_tx_data_underflow);
			return;
		}
		rxrpc_decant_prepared_tx(call);
		break;
	default:
		return;
	}
}

/*
 * Ping the other end to fill our RTT cache and to retrieve the rwind
 * and MTU parameters.
 */
static void rxrpc_send_initial_ping(struct rxrpc_call *call)
{
	if (call->peer->rtt_count < 3 ||
	    ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000),
			 ktime_get_real()))
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_params);
}

/*
 * Handle retransmission and deferred ACK/abort generation.
 */
bool rxrpc_input_call_event(struct rxrpc_call *call, struct sk_buff *skb)
{
	unsigned long now, next, t;
	rxrpc_serial_t ackr_serial;
	bool resend = false, expired = false;
	s32 abort_code;

	rxrpc_see_call(call, rxrpc_call_see_input);

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx}",
	       call->debug_id, rxrpc_call_states[__rxrpc_call_state(call)],
	       call->events);

	if (__rxrpc_call_is_complete(call))
		goto out;

	/* Handle abort request locklessly, vs rxrpc_propose_abort(). */
	abort_code = smp_load_acquire(&call->send_abort);
	if (abort_code) {
		rxrpc_abort_call(call, 0, call->send_abort, call->send_abort_err,
				 call->send_abort_why);
		goto out;
	}

	if (skb && skb->mark == RXRPC_SKB_MARK_ERROR)
		goto out;

	/* If we see our async-event poke, check for timeout trippage. */
	now = jiffies;
	t = READ_ONCE(call->expect_rx_by);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_normal, now);
		expired = true;
	}

	t = READ_ONCE(call->expect_req_by);
	if (__rxrpc_call_state(call) == RXRPC_CALL_SERVER_RECV_REQUEST &&
	    time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_idle, now);
		expired = true;
	}

	t = READ_ONCE(call->expect_term_by);
	if (time_after_eq(now, t)) {
		trace_rxrpc_timer(call, rxrpc_timer_exp_hard, now);
		expired = true;
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
		resend = true;
	}

	if (skb)
		rxrpc_input_call_packet(call, skb);

	rxrpc_transmit_some_data(call);

	if (skb) {
		struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

		if (sp->hdr.type == RXRPC_PACKET_TYPE_ACK)
			rxrpc_congestion_degrade(call);
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_INITIAL_PING, &call->events))
		rxrpc_send_initial_ping(call);

	/* Process events */
	if (expired) {
		if (test_bit(RXRPC_CALL_RX_HEARD, &call->flags) &&
		    (int)call->conn->hi_serial - (int)call->rx_serial > 0) {
			trace_rxrpc_call_reset(call);
			rxrpc_abort_call(call, 0, RX_CALL_DEAD, -ECONNRESET,
					 rxrpc_abort_call_reset);
		} else {
			rxrpc_abort_call(call, 0, RX_CALL_TIMEOUT, -ETIME,
					 rxrpc_abort_call_timeout);
		}
		goto out;
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);

	if (resend && __rxrpc_call_state(call) != RXRPC_CALL_CLIENT_RECV_REPLY)
		rxrpc_resend(call, NULL);

	if (test_and_clear_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags))
		rxrpc_send_ACK(call, RXRPC_ACK_IDLE, 0,
			       rxrpc_propose_ack_rx_idle);

	if (call->ackr_nr_unacked > 2) {
		if (call->peer->rtt_count < 3)
			rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
				       rxrpc_propose_ack_ping_for_rtt);
		else if (ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000),
				      ktime_get_real()))
			rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
				       rxrpc_propose_ack_ping_for_old_rtt);
		else
			rxrpc_send_ACK(call, RXRPC_ACK_IDLE, 0,
				       rxrpc_propose_ack_input_data);
	}

	/* Make sure the timer is restarted */
	if (!__rxrpc_call_is_complete(call)) {
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
			rxrpc_poke_call(call, rxrpc_call_poke_timer_now);

		rxrpc_reduce_call_timer(call, next, now, rxrpc_timer_restart);
	}

out:
	if (__rxrpc_call_is_complete(call)) {
		del_timer_sync(&call->timer);
		if (!test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
			rxrpc_disconnect_call(call);
		if (call->security)
			call->security->free_call_crypto(call);
	}
	if (call->acks_hard_ack != call->tx_bottom)
		rxrpc_shrink_call_tx_buffer(call);
	_leave("");
	return true;
}
