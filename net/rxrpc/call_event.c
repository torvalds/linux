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
	ktime_t delay = ms_to_ktime(READ_ONCE(rxrpc_idle_ack_delay));
	ktime_t now = ktime_get_real();
	ktime_t ping_at = ktime_add(now, delay);

	trace_rxrpc_propose_ack(call, why, RXRPC_ACK_PING, serial);
	if (ktime_before(ping_at, call->ping_at)) {
		call->ping_at = ping_at;
		trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_ping);
	}
}

/*
 * Propose a DELAY ACK be sent in the future.
 */
void rxrpc_propose_delay_ACK(struct rxrpc_call *call, rxrpc_serial_t serial,
			     enum rxrpc_propose_ack_trace why)
{
	ktime_t now = ktime_get_real(), delay;

	trace_rxrpc_propose_ack(call, why, RXRPC_ACK_DELAY, serial);

	if (call->peer->srtt_us)
		delay = (call->peer->srtt_us >> 3) * NSEC_PER_USEC;
	else
		delay = ms_to_ktime(READ_ONCE(rxrpc_soft_ack_delay));
	ktime_add_ms(delay, call->tx_backoff);

	call->delay_ack_at = ktime_add(now, delay);
	trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_delayed_ack);
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
	struct rxrpc_skb_priv *sp;
	struct rxrpc_txbuf *txb;
	rxrpc_seq_t transmitted = call->tx_transmitted;
	ktime_t next_resend = KTIME_MAX, rto = ns_to_ktime(call->peer->rto_us * NSEC_PER_USEC);
	ktime_t resend_at = KTIME_MAX, now, delay;
	bool unacked = false, did_send = false;
	unsigned int i;

	_enter("{%d,%d}", call->acks_hard_ack, call->tx_top);

	now = ktime_get_real();

	if (list_empty(&call->tx_buffer))
		goto no_resend;

	trace_rxrpc_resend(call, ack_skb);
	txb = list_first_entry(&call->tx_buffer, struct rxrpc_txbuf, call_link);

	/* Scan the soft ACK table without dropping the lock and resend any
	 * explicitly NAK'd packets.
	 */
	if (ack_skb) {
		sp = rxrpc_skb(ack_skb);
		ack = (void *)ack_skb->data + sizeof(struct rxrpc_wire_header);

		for (i = 0; i < sp->ack.nr_acks; i++) {
			rxrpc_seq_t seq;

			if (ack->acks[i] & 1)
				continue;
			seq = sp->ack.first_ack + i;
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
			resend_at = ktime_add(txb->last_sent, rto);
			if (after(txb->serial, call->acks_highest_serial)) {
				if (ktime_after(resend_at, now) &&
				    ktime_before(resend_at, next_resend))
					next_resend = resend_at;
				continue; /* Ack point not yet reached */
			}

			rxrpc_see_txbuf(txb, rxrpc_txbuf_see_unacked);

			trace_rxrpc_retransmit(call, txb->seq, txb->serial,
					       ktime_sub(resend_at, now));

			txb->flags |= RXRPC_TXBUF_RESENT;
			rxrpc_transmit_one(call, txb);
			did_send = true;
			now = ktime_get_real();

			if (list_is_last(&txb->call_link, &call->tx_buffer))
				goto no_further_resend;
			txb = list_next_entry(txb, call_link);
		}
	}

	/* Fast-forward through the Tx queue to the point the peer says it has
	 * seen.  Anything between the soft-ACK table and that point will get
	 * ACK'd or NACK'd in due course, so don't worry about it here; here we
	 * need to consider retransmitting anything beyond that point.
	 */
	if (after_eq(call->acks_prev_seq, call->tx_transmitted))
		goto no_further_resend;

	list_for_each_entry_from(txb, &call->tx_buffer, call_link) {
		resend_at = ktime_add(txb->last_sent, rto);

		if (before_eq(txb->seq, call->acks_prev_seq))
			continue;
		if (after(txb->seq, call->tx_transmitted))
			break; /* Not transmitted yet */

		if (ack && ack->reason == RXRPC_ACK_PING_RESPONSE &&
		    before(txb->serial, ntohl(ack->serial)))
			goto do_resend; /* Wasn't accounted for by a more recent ping. */

		if (ktime_after(resend_at, now)) {
			if (ktime_before(resend_at, next_resend))
				next_resend = resend_at;
			continue;
		}

	do_resend:
		unacked = true;

		txb->flags |= RXRPC_TXBUF_RESENT;
		rxrpc_transmit_one(call, txb);
		did_send = true;
		rxrpc_inc_stat(call->rxnet, stat_tx_data_retrans);
		now = ktime_get_real();
	}

no_further_resend:
no_resend:
	if (resend_at < KTIME_MAX) {
		delay = rxrpc_get_rto_backoff(call->peer, did_send);
		resend_at = ktime_add(resend_at, delay);
		trace_rxrpc_timer_set(call, resend_at - now, rxrpc_timer_trace_resend_reset);
	}
	call->resend_at = resend_at;

	if (unacked)
		rxrpc_congestion_timeout(call);

	/* If there was nothing that needed retransmission then it's likely
	 * that an ACK got lost somewhere.  Send a ping to find out instead of
	 * retransmitting data.
	 */
	if (!did_send) {
		ktime_t next_ping = ktime_add_us(call->acks_latest_ts,
						 call->peer->srtt_us >> 3);

		if (ktime_sub(next_ping, now) <= 0)
			rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
				       rxrpc_propose_ack_ping_for_0_retrans);
	}

	_leave("");
}

/*
 * Start transmitting the reply to a service.  This cancels the need to ACK the
 * request if we haven't yet done so.
 */
static void rxrpc_begin_service_reply(struct rxrpc_call *call)
{
	rxrpc_set_call_state(call, RXRPC_CALL_SERVER_SEND_REPLY);
	if (call->ackr_reason == RXRPC_ACK_DELAY)
		call->ackr_reason = 0;
	call->delay_ack_at = KTIME_MAX;
	trace_rxrpc_timer_can(call, rxrpc_timer_trace_delayed_ack);
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

		if (txb->flags & RXRPC_LAST_PACKET)
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
	ktime_t now, t;
	bool resend = false;
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

	if (skb)
		rxrpc_input_call_packet(call, skb);

	/* If we see our async-event poke, check for timeout trippage. */
	now = ktime_get_real();
	t = ktime_sub(call->expect_rx_by, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_expect_rx);
		goto expired;
	}

	t = ktime_sub(call->expect_req_by, now);
	if (t <= 0) {
		call->expect_req_by = KTIME_MAX;
		if (__rxrpc_call_state(call) == RXRPC_CALL_SERVER_RECV_REQUEST) {
			trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_idle);
			goto expired;
		}
	}

	t = ktime_sub(READ_ONCE(call->expect_term_by), now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_hard);
		goto expired;
	}

	t = ktime_sub(call->delay_ack_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_delayed_ack);
		call->delay_ack_at = KTIME_MAX;
		rxrpc_send_ACK(call, RXRPC_ACK_DELAY, 0,
			       rxrpc_propose_ack_delayed_ack);
	}

	t = ktime_sub(call->ack_lost_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_lost_ack);
		call->ack_lost_at = KTIME_MAX;
		set_bit(RXRPC_CALL_EV_ACK_LOST, &call->events);
	}

	t = ktime_sub(call->ping_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_ping);
		call->ping_at = KTIME_MAX;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	t = ktime_sub(call->resend_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_resend);
		call->resend_at = KTIME_MAX;
		resend = true;
	}

	rxrpc_transmit_some_data(call);

	now = ktime_get_real();
	t = ktime_sub(call->keepalive_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_keepalive);
		call->keepalive_at = KTIME_MAX;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	if (skb) {
		struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

		if (sp->hdr.type == RXRPC_PACKET_TYPE_ACK)
			rxrpc_congestion_degrade(call);
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_INITIAL_PING, &call->events))
		rxrpc_send_initial_ping(call);

	/* Process events */
	if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);

	if (resend &&
	    __rxrpc_call_state(call) != RXRPC_CALL_CLIENT_RECV_REPLY &&
	    !test_bit(RXRPC_CALL_TX_ALL_ACKED, &call->flags))
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
		ktime_t next = READ_ONCE(call->expect_term_by), delay;

#define set(T) { ktime_t _t = (T); if (ktime_before(_t, next)) next = _t; }

		set(call->expect_req_by);
		set(call->expect_rx_by);
		set(call->delay_ack_at);
		set(call->ack_lost_at);
		set(call->resend_at);
		set(call->keepalive_at);
		set(call->ping_at);

		now = ktime_get_real();
		delay = ktime_sub(next, now);
		if (delay <= 0) {
			rxrpc_poke_call(call, rxrpc_call_poke_timer_now);
		} else {
			unsigned long nowj = jiffies, delayj, nextj;

			delayj = max(nsecs_to_jiffies(delay), 1);
			nextj = nowj + delayj;
			if (time_before(nextj, call->timer.expires) ||
			    !timer_pending(&call->timer)) {
				trace_rxrpc_timer_restart(call, delay, delayj);
				timer_reduce(&call->timer, nextj);
			}
		}
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

expired:
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
