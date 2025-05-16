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

	if (call->srtt_us)
		delay = (call->srtt_us >> 3) * NSEC_PER_USEC;
	else
		delay = ms_to_ktime(READ_ONCE(rxrpc_soft_ack_delay));
	ktime_add_ms(delay, call->tx_backoff);

	call->delay_ack_at = ktime_add(now, delay);
	trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_delayed_ack);
}

/*
 * Retransmit one or more packets.
 */
static bool rxrpc_retransmit_data(struct rxrpc_call *call,
				  struct rxrpc_send_data_req *req)
{
	struct rxrpc_txqueue *tq = req->tq;
	unsigned int ix = req->seq & RXRPC_TXQ_MASK;
	struct rxrpc_txbuf *txb = tq->bufs[ix];

	_enter("%x,%x,%x,%x", tq->qbase, req->seq, ix, txb->debug_id);

	req->retrans = true;
	trace_rxrpc_retransmit(call, req, txb);

	txb->flags |= RXRPC_TXBUF_RESENT;
	rxrpc_send_data_packet(call, req);
	rxrpc_inc_stat(call->rxnet, stat_tx_data_retrans);

	req->tq		= NULL;
	req->n		= 0;
	req->did_send	= true;
	req->now	= ktime_get_real();
	return true;
}

/*
 * Perform retransmission of NAK'd and unack'd packets.
 */
static void rxrpc_resend(struct rxrpc_call *call)
{
	struct rxrpc_send_data_req req = {
		.now	= ktime_get_real(),
		.trace	= rxrpc_txdata_retransmit,
	};
	struct rxrpc_txqueue *tq;

	_enter("{%d,%d}", call->tx_bottom, call->tx_top);

	trace_rxrpc_resend(call, call->acks_highest_serial);

	/* Scan the transmission queue, looking for lost packets. */
	for (tq = call->tx_queue; tq; tq = tq->next) {
		unsigned long lost = tq->segment_lost;

		if (after(tq->qbase, call->tx_transmitted))
			break;

		_debug("retr %16lx %u c=%08x [%x]",
		       tq->segment_acked, tq->nr_reported_acks, call->debug_id, tq->qbase);
		_debug("lost %16lx", lost);

		trace_rxrpc_resend_lost(call, tq, lost);
		while (lost) {
			unsigned int ix = __ffs(lost);
			struct rxrpc_txbuf *txb = tq->bufs[ix];

			__clear_bit(ix, &lost);
			rxrpc_see_txbuf(txb, rxrpc_txbuf_see_lost);

			req.tq  = tq;
			req.seq = tq->qbase + ix;
			req.n   = 1;
			rxrpc_retransmit_data(call, &req);
		}
	}

	rxrpc_get_rto_backoff(call, req.did_send);
	_leave("");
}

/*
 * Resend the highest-seq DATA packet so far transmitted for RACK-TLP [RFC8985 7.3].
 */
void rxrpc_resend_tlp(struct rxrpc_call *call)
{
	struct rxrpc_send_data_req req = {
		.now		= ktime_get_real(),
		.seq		= call->tx_transmitted,
		.n		= 1,
		.tlp_probe	= true,
		.trace		= rxrpc_txdata_tlp_retransmit,
	};

	/* There's a chance it'll be on the tail segment of the queue. */
	req.tq = READ_ONCE(call->tx_qtail);
	if (req.tq &&
	    before(call->tx_transmitted, req.tq->qbase + RXRPC_NR_TXQUEUE)) {
		rxrpc_retransmit_data(call, &req);
		return;
	}

	for (req.tq = call->tx_queue; req.tq; req.tq = req.tq->next) {
		if (after_eq(call->tx_transmitted, req.tq->qbase) &&
		    before(call->tx_transmitted, req.tq->qbase + RXRPC_NR_TXQUEUE)) {
			rxrpc_retransmit_data(call, &req);
			return;
		}
	}
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

/*
 * Transmit some as-yet untransmitted data, to a maximum of the supplied limit.
 */
static void rxrpc_transmit_fresh_data(struct rxrpc_call *call, unsigned int limit,
				      enum rxrpc_txdata_trace trace)
{
	int space = rxrpc_tx_window_space(call);

	if (!test_bit(RXRPC_CALL_EXPOSED, &call->flags)) {
		if (call->send_top == call->tx_top)
			return;
		rxrpc_expose_client_call(call);
	}

	while (space > 0) {
		struct rxrpc_send_data_req req = {
			.now	= ktime_get_real(),
			.seq	= call->tx_transmitted + 1,
			.n	= 0,
			.trace	= trace,
		};
		struct rxrpc_txqueue *tq;
		struct rxrpc_txbuf *txb;
		rxrpc_seq_t send_top, seq;
		int limit = min(space, max(call->peer->pmtud_jumbo, 1));

		/* Order send_top before the contents of the new txbufs and
		 * txqueue pointers
		 */
		send_top = smp_load_acquire(&call->send_top);
		if (call->tx_top == send_top)
			break;

		trace_rxrpc_transmit(call, send_top, space);

		tq = call->tx_qtail;
		seq = call->tx_top;
		trace_rxrpc_tq(call, tq, seq, rxrpc_tq_decant);

		do {
			int ix;

			seq++;
			ix = seq & RXRPC_TXQ_MASK;
			if (!ix) {
				tq = tq->next;
				trace_rxrpc_tq(call, tq, seq, rxrpc_tq_decant_advance);
			}
			if (!req.tq)
				req.tq = tq;
			txb = tq->bufs[ix];
			req.n++;
			if (!txb->jumboable)
				break;
		} while (req.n < limit && before(seq, send_top));

		if (txb->flags & RXRPC_LAST_PACKET) {
			rxrpc_close_tx_phase(call);
			tq = NULL;
		}
		call->tx_qtail = tq;
		call->tx_top = seq;

		space -= req.n;
		rxrpc_send_data_packet(call, &req);
	}
}

void rxrpc_transmit_some_data(struct rxrpc_call *call, unsigned int limit,
			      enum rxrpc_txdata_trace trace)
{
	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_SERVER_ACK_REQUEST:
		if (call->tx_bottom == READ_ONCE(call->send_top))
			return;
		rxrpc_begin_service_reply(call);
		fallthrough;

	case RXRPC_CALL_SERVER_SEND_REPLY:
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
		if (!rxrpc_tx_window_space(call))
			return;
		if (call->tx_bottom == READ_ONCE(call->send_top)) {
			rxrpc_inc_stat(call->rxnet, stat_tx_data_underflow);
			return;
		}
		rxrpc_transmit_fresh_data(call, limit, trace);
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
	if (call->rtt_count < 3 ||
	    ktime_before(ktime_add_ms(call->rtt_last_req, 1000),
			 ktime_get_real()))
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_params);
}

/*
 * Handle retransmission and deferred ACK/abort generation.
 */
bool rxrpc_input_call_event(struct rxrpc_call *call)
{
	struct sk_buff *skb;
	ktime_t now, t;
	bool did_receive = false, saw_ack = false;
	s32 abort_code;

	rxrpc_see_call(call, rxrpc_call_see_input);

	//printk("\n--------------------\n");
	_enter("{%d,%s,%lx}",
	       call->debug_id, rxrpc_call_states[__rxrpc_call_state(call)],
	       call->events);

	/* Handle abort request locklessly, vs rxrpc_propose_abort(). */
	abort_code = smp_load_acquire(&call->send_abort);
	if (abort_code) {
		rxrpc_abort_call(call, 0, call->send_abort, call->send_abort_err,
				 call->send_abort_why);
		goto out;
	}

	do {
		skb = __skb_dequeue(&call->rx_queue);
		if (skb) {
			struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

			if (__rxrpc_call_is_complete(call) ||
			    skb->mark == RXRPC_SKB_MARK_ERROR) {
				rxrpc_free_skb(skb, rxrpc_skb_put_call_rx);
				goto out;
			}

			saw_ack |= sp->hdr.type == RXRPC_PACKET_TYPE_ACK;

			rxrpc_input_call_packet(call, skb);
			rxrpc_free_skb(skb, rxrpc_skb_put_call_rx);
			did_receive = true;
		}

		t = ktime_sub(call->rack_timo_at, ktime_get_real());
		if (t <= 0) {
			trace_rxrpc_timer_exp(call, t,
					      rxrpc_timer_trace_rack_off + call->rack_timer_mode);
			call->rack_timo_at = KTIME_MAX;
			rxrpc_rack_timer_expired(call, t);
		}

	} while (!skb_queue_empty(&call->rx_queue));

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

	t = ktime_sub(call->ping_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_ping);
		call->ping_at = KTIME_MAX;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	now = ktime_get_real();
	t = ktime_sub(call->keepalive_at, now);
	if (t <= 0) {
		trace_rxrpc_timer_exp(call, t, rxrpc_timer_trace_keepalive);
		call->keepalive_at = KTIME_MAX;
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_keepalive);
	}

	if (test_and_clear_bit(RXRPC_CALL_EV_INITIAL_PING, &call->events))
		rxrpc_send_initial_ping(call);

	rxrpc_transmit_some_data(call, UINT_MAX, rxrpc_txdata_new_data);

	if (saw_ack)
		rxrpc_congestion_degrade(call);

	if (did_receive &&
	    (__rxrpc_call_state(call) == RXRPC_CALL_CLIENT_SEND_REQUEST ||
	     __rxrpc_call_state(call) == RXRPC_CALL_SERVER_SEND_REPLY)) {
		t = ktime_sub(call->rack_timo_at, ktime_get_real());
		trace_rxrpc_rack(call, t);
	}

	/* Process events */
	if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
			       rxrpc_propose_ack_ping_for_lost_ack);

	if (call->tx_nr_lost > 0 &&
	    __rxrpc_call_state(call) != RXRPC_CALL_CLIENT_RECV_REPLY &&
	    !test_bit(RXRPC_CALL_TX_ALL_ACKED, &call->flags))
		rxrpc_resend(call);

	if (test_and_clear_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags))
		rxrpc_send_ACK(call, RXRPC_ACK_IDLE, 0,
			       rxrpc_propose_ack_rx_idle);

	if (call->ackr_nr_unacked > 2) {
		if (call->rtt_count < 3)
			rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
				       rxrpc_propose_ack_ping_for_rtt);
		else if (ktime_before(ktime_add_ms(call->rtt_last_req, 1000),
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
		set(call->rack_timo_at);
		set(call->keepalive_at);
		set(call->ping_at);

		now = ktime_get_real();
		delay = ktime_sub(next, now);
		if (delay <= 0) {
			rxrpc_poke_call(call, rxrpc_call_poke_timer_now);
		} else {
			unsigned long nowj = jiffies, delayj, nextj;

			delayj = umax(nsecs_to_jiffies(delay), 1);
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
		timer_delete_sync(&call->timer);
		if (!test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
			rxrpc_disconnect_call(call);
		if (call->security)
			call->security->free_call_crypto(call);
	} else {
		if (did_receive &&
		    call->peer->ackr_adv_pmtud &&
		    call->peer->pmtud_pending)
			rxrpc_send_probe_for_pmtud(call);
	}
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
