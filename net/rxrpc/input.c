/* RxRPC packet reception
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
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
#include <linux/errqueue.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <linux/gfp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/net_namespace.h>
#include "ar-internal.h"

static void rxrpc_proto_abort(const char *why,
			      struct rxrpc_call *call, rxrpc_seq_t seq)
{
	if (rxrpc_abort_call(why, call, seq, RX_PROTOCOL_ERROR, -EBADMSG)) {
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		rxrpc_queue_call(call);
	}
}

/*
 * Do TCP-style congestion management [RFC 5681].
 */
static void rxrpc_congestion_management(struct rxrpc_call *call,
					struct sk_buff *skb,
					struct rxrpc_ack_summary *summary,
					rxrpc_serial_t acked_serial)
{
	enum rxrpc_congest_change change = rxrpc_cong_no_change;
	unsigned int cumulative_acks = call->cong_cumul_acks;
	unsigned int cwnd = call->cong_cwnd;
	bool resend = false;

	summary->flight_size =
		(call->tx_top - call->tx_hard_ack) - summary->nr_acks;

	if (test_and_clear_bit(RXRPC_CALL_RETRANS_TIMEOUT, &call->flags)) {
		summary->retrans_timeo = true;
		call->cong_ssthresh = max_t(unsigned int,
					    summary->flight_size / 2, 2);
		cwnd = 1;
		if (cwnd >= call->cong_ssthresh &&
		    call->cong_mode == RXRPC_CALL_SLOW_START) {
			call->cong_mode = RXRPC_CALL_CONGEST_AVOIDANCE;
			call->cong_tstamp = skb->tstamp;
			cumulative_acks = 0;
		}
	}

	cumulative_acks += summary->nr_new_acks;
	cumulative_acks += summary->nr_rot_new_acks;
	if (cumulative_acks > 255)
		cumulative_acks = 255;

	summary->mode = call->cong_mode;
	summary->cwnd = call->cong_cwnd;
	summary->ssthresh = call->cong_ssthresh;
	summary->cumulative_acks = cumulative_acks;
	summary->dup_acks = call->cong_dup_acks;

	switch (call->cong_mode) {
	case RXRPC_CALL_SLOW_START:
		if (summary->nr_nacks > 0)
			goto packet_loss_detected;
		if (summary->cumulative_acks > 0)
			cwnd += 1;
		if (cwnd >= call->cong_ssthresh) {
			call->cong_mode = RXRPC_CALL_CONGEST_AVOIDANCE;
			call->cong_tstamp = skb->tstamp;
		}
		goto out;

	case RXRPC_CALL_CONGEST_AVOIDANCE:
		if (summary->nr_nacks > 0)
			goto packet_loss_detected;

		/* We analyse the number of packets that get ACK'd per RTT
		 * period and increase the window if we managed to fill it.
		 */
		if (call->peer->rtt_usage == 0)
			goto out;
		if (ktime_before(skb->tstamp,
				 ktime_add_ns(call->cong_tstamp,
					      call->peer->rtt)))
			goto out_no_clear_ca;
		change = rxrpc_cong_rtt_window_end;
		call->cong_tstamp = skb->tstamp;
		if (cumulative_acks >= cwnd)
			cwnd++;
		goto out;

	case RXRPC_CALL_PACKET_LOSS:
		if (summary->nr_nacks == 0)
			goto resume_normality;

		if (summary->new_low_nack) {
			change = rxrpc_cong_new_low_nack;
			call->cong_dup_acks = 1;
			if (call->cong_extra > 1)
				call->cong_extra = 1;
			goto send_extra_data;
		}

		call->cong_dup_acks++;
		if (call->cong_dup_acks < 3)
			goto send_extra_data;

		change = rxrpc_cong_begin_retransmission;
		call->cong_mode = RXRPC_CALL_FAST_RETRANSMIT;
		call->cong_ssthresh = max_t(unsigned int,
					    summary->flight_size / 2, 2);
		cwnd = call->cong_ssthresh + 3;
		call->cong_extra = 0;
		call->cong_dup_acks = 0;
		resend = true;
		goto out;

	case RXRPC_CALL_FAST_RETRANSMIT:
		if (!summary->new_low_nack) {
			if (summary->nr_new_acks == 0)
				cwnd += 1;
			call->cong_dup_acks++;
			if (call->cong_dup_acks == 2) {
				change = rxrpc_cong_retransmit_again;
				call->cong_dup_acks = 0;
				resend = true;
			}
		} else {
			change = rxrpc_cong_progress;
			cwnd = call->cong_ssthresh;
			if (summary->nr_nacks == 0)
				goto resume_normality;
		}
		goto out;

	default:
		BUG();
		goto out;
	}

resume_normality:
	change = rxrpc_cong_cleared_nacks;
	call->cong_dup_acks = 0;
	call->cong_extra = 0;
	call->cong_tstamp = skb->tstamp;
	if (cwnd < call->cong_ssthresh)
		call->cong_mode = RXRPC_CALL_SLOW_START;
	else
		call->cong_mode = RXRPC_CALL_CONGEST_AVOIDANCE;
out:
	cumulative_acks = 0;
out_no_clear_ca:
	if (cwnd >= RXRPC_RXTX_BUFF_SIZE - 1)
		cwnd = RXRPC_RXTX_BUFF_SIZE - 1;
	call->cong_cwnd = cwnd;
	call->cong_cumul_acks = cumulative_acks;
	trace_rxrpc_congest(call, summary, acked_serial, change);
	if (resend && !test_and_set_bit(RXRPC_CALL_EV_RESEND, &call->events))
		rxrpc_queue_call(call);
	return;

packet_loss_detected:
	change = rxrpc_cong_saw_nack;
	call->cong_mode = RXRPC_CALL_PACKET_LOSS;
	call->cong_dup_acks = 0;
	goto send_extra_data;

send_extra_data:
	/* Send some previously unsent DATA if we have some to advance the ACK
	 * state.
	 */
	if (call->rxtx_annotations[call->tx_top & RXRPC_RXTX_BUFF_MASK] &
	    RXRPC_TX_ANNO_LAST ||
	    summary->nr_acks != call->tx_top - call->tx_hard_ack) {
		call->cong_extra++;
		wake_up(&call->waitq);
	}
	goto out_no_clear_ca;
}

/*
 * Ping the other end to fill our RTT cache and to retrieve the rwind
 * and MTU parameters.
 */
static void rxrpc_send_ping(struct rxrpc_call *call, struct sk_buff *skb,
			    int skew)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	ktime_t now = skb->tstamp;

	if (call->peer->rtt_usage < 3 ||
	    ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000), now))
		rxrpc_propose_ACK(call, RXRPC_ACK_PING, skew, sp->hdr.serial,
				  true, true,
				  rxrpc_propose_ack_ping_for_params);
}

/*
 * Apply a hard ACK by advancing the Tx window.
 */
static void rxrpc_rotate_tx_window(struct rxrpc_call *call, rxrpc_seq_t to,
				   struct rxrpc_ack_summary *summary)
{
	struct sk_buff *skb, *list = NULL;
	int ix;
	u8 annotation;

	if (call->acks_lowest_nak == call->tx_hard_ack) {
		call->acks_lowest_nak = to;
	} else if (before_eq(call->acks_lowest_nak, to)) {
		summary->new_low_nack = true;
		call->acks_lowest_nak = to;
	}

	spin_lock(&call->lock);

	while (before(call->tx_hard_ack, to)) {
		call->tx_hard_ack++;
		ix = call->tx_hard_ack & RXRPC_RXTX_BUFF_MASK;
		skb = call->rxtx_buffer[ix];
		annotation = call->rxtx_annotations[ix];
		rxrpc_see_skb(skb, rxrpc_skb_tx_rotated);
		call->rxtx_buffer[ix] = NULL;
		call->rxtx_annotations[ix] = 0;
		skb->next = list;
		list = skb;

		if (annotation & RXRPC_TX_ANNO_LAST)
			set_bit(RXRPC_CALL_TX_LAST, &call->flags);
		if ((annotation & RXRPC_TX_ANNO_MASK) != RXRPC_TX_ANNO_ACK)
			summary->nr_rot_new_acks++;
	}

	spin_unlock(&call->lock);

	trace_rxrpc_transmit(call, (test_bit(RXRPC_CALL_TX_LAST, &call->flags) ?
				    rxrpc_transmit_rotate_last :
				    rxrpc_transmit_rotate));
	wake_up(&call->waitq);

	while (list) {
		skb = list;
		list = skb->next;
		skb->next = NULL;
		rxrpc_free_skb(skb, rxrpc_skb_tx_freed);
	}
}

/*
 * End the transmission phase of a call.
 *
 * This occurs when we get an ACKALL packet, the first DATA packet of a reply,
 * or a final ACK packet.
 */
static bool rxrpc_end_tx_phase(struct rxrpc_call *call, bool reply_begun,
			       const char *abort_why)
{

	ASSERT(test_bit(RXRPC_CALL_TX_LAST, &call->flags));

	write_lock(&call->state_lock);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		if (reply_begun)
			call->state = RXRPC_CALL_CLIENT_RECV_REPLY;
		else
			call->state = RXRPC_CALL_CLIENT_AWAIT_REPLY;
		break;

	case RXRPC_CALL_SERVER_AWAIT_ACK:
		__rxrpc_call_completed(call);
		rxrpc_notify_socket(call);
		break;

	default:
		goto bad_state;
	}

	write_unlock(&call->state_lock);
	if (call->state == RXRPC_CALL_CLIENT_AWAIT_REPLY) {
		trace_rxrpc_transmit(call, rxrpc_transmit_await_reply);
	} else {
		trace_rxrpc_transmit(call, rxrpc_transmit_end);
	}
	_leave(" = ok");
	return true;

bad_state:
	write_unlock(&call->state_lock);
	kdebug("end_tx %s", rxrpc_call_states[call->state]);
	rxrpc_proto_abort(abort_why, call, call->tx_top);
	return false;
}

/*
 * Begin the reply reception phase of a call.
 */
static bool rxrpc_receiving_reply(struct rxrpc_call *call)
{
	struct rxrpc_ack_summary summary = { 0 };
	unsigned long now, timo;
	rxrpc_seq_t top = READ_ONCE(call->tx_top);

	if (call->ackr_reason) {
		spin_lock_bh(&call->lock);
		call->ackr_reason = 0;
		spin_unlock_bh(&call->lock);
		now = jiffies;
		timo = now + MAX_JIFFY_OFFSET;
		WRITE_ONCE(call->resend_at, timo);
		WRITE_ONCE(call->ack_at, timo);
		trace_rxrpc_timer(call, rxrpc_timer_init_for_reply, now);
	}

	if (!test_bit(RXRPC_CALL_TX_LAST, &call->flags))
		rxrpc_rotate_tx_window(call, top, &summary);
	if (!test_bit(RXRPC_CALL_TX_LAST, &call->flags)) {
		rxrpc_proto_abort("TXL", call, top);
		return false;
	}
	if (!rxrpc_end_tx_phase(call, true, "ETD"))
		return false;
	call->tx_phase = false;
	return true;
}

/*
 * Scan a jumbo packet to validate its structure and to work out how many
 * subpackets it contains.
 *
 * A jumbo packet is a collection of consecutive packets glued together with
 * little headers between that indicate how to change the initial header for
 * each subpacket.
 *
 * RXRPC_JUMBO_PACKET must be set on all but the last subpacket - and all but
 * the last are RXRPC_JUMBO_DATALEN in size.  The last subpacket may be of any
 * size.
 */
static bool rxrpc_validate_jumbo(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int len = skb->len;
	int nr_jumbo = 1;
	u8 flags = sp->hdr.flags;

	do {
		nr_jumbo++;
		if (len - offset < RXRPC_JUMBO_SUBPKTLEN)
			goto protocol_error;
		if (flags & RXRPC_LAST_PACKET)
			goto protocol_error;
		offset += RXRPC_JUMBO_DATALEN;
		if (skb_copy_bits(skb, offset, &flags, 1) < 0)
			goto protocol_error;
		offset += sizeof(struct rxrpc_jumbo_header);
	} while (flags & RXRPC_JUMBO_PACKET);

	sp->nr_jumbo = nr_jumbo;
	return true;

protocol_error:
	return false;
}

/*
 * Handle reception of a duplicate packet.
 *
 * We have to take care to avoid an attack here whereby we're given a series of
 * jumbograms, each with a sequence number one before the preceding one and
 * filled up to maximum UDP size.  If they never send us the first packet in
 * the sequence, they can cause us to have to hold on to around 2MiB of kernel
 * space until the call times out.
 *
 * We limit the space usage by only accepting three duplicate jumbo packets per
 * call.  After that, we tell the other side we're no longer accepting jumbos
 * (that information is encoded in the ACK packet).
 */
static void rxrpc_input_dup_data(struct rxrpc_call *call, rxrpc_seq_t seq,
				 u8 annotation, bool *_jumbo_bad)
{
	/* Discard normal packets that are duplicates. */
	if (annotation == 0)
		return;

	/* Skip jumbo subpackets that are duplicates.  When we've had three or
	 * more partially duplicate jumbo packets, we refuse to take any more
	 * jumbos for this call.
	 */
	if (!*_jumbo_bad) {
		call->nr_jumbo_bad++;
		*_jumbo_bad = true;
	}
}

/*
 * Process a DATA packet, adding the packet to the Rx ring.
 */
static void rxrpc_input_data(struct rxrpc_call *call, struct sk_buff *skb,
			     u16 skew)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	enum rxrpc_call_state state;
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int ix;
	rxrpc_serial_t serial = sp->hdr.serial, ack_serial = 0;
	rxrpc_seq_t seq = sp->hdr.seq, hard_ack;
	bool immediate_ack = false, jumbo_bad = false, queued;
	u16 len;
	u8 ack = 0, flags, annotation = 0;

	_enter("{%u,%u},{%u,%u}",
	       call->rx_hard_ack, call->rx_top, skb->len, seq);

	_proto("Rx DATA %%%u { #%u f=%02x }",
	       sp->hdr.serial, seq, sp->hdr.flags);

	state = READ_ONCE(call->state);
	if (state >= RXRPC_CALL_COMPLETE)
		return;

	if (call->state == RXRPC_CALL_SERVER_RECV_REQUEST) {
		unsigned long timo = READ_ONCE(call->next_req_timo);
		unsigned long now, expect_req_by;

		if (timo) {
			now = jiffies;
			expect_req_by = now + timo;
			WRITE_ONCE(call->expect_req_by, expect_req_by);
			rxrpc_reduce_call_timer(call, expect_req_by, now,
						rxrpc_timer_set_for_idle);
		}
	}

	/* Received data implicitly ACKs all of the request packets we sent
	 * when we're acting as a client.
	 */
	if ((state == RXRPC_CALL_CLIENT_SEND_REQUEST ||
	     state == RXRPC_CALL_CLIENT_AWAIT_REPLY) &&
	    !rxrpc_receiving_reply(call))
		return;

	call->ackr_prev_seq = seq;

	hard_ack = READ_ONCE(call->rx_hard_ack);
	if (after(seq, hard_ack + call->rx_winsize)) {
		ack = RXRPC_ACK_EXCEEDS_WINDOW;
		ack_serial = serial;
		goto ack;
	}

	flags = sp->hdr.flags;
	if (flags & RXRPC_JUMBO_PACKET) {
		if (call->nr_jumbo_bad > 3) {
			ack = RXRPC_ACK_NOSPACE;
			ack_serial = serial;
			goto ack;
		}
		annotation = 1;
	}

next_subpacket:
	queued = false;
	ix = seq & RXRPC_RXTX_BUFF_MASK;
	len = skb->len;
	if (flags & RXRPC_JUMBO_PACKET)
		len = RXRPC_JUMBO_DATALEN;

	if (flags & RXRPC_LAST_PACKET) {
		if (test_bit(RXRPC_CALL_RX_LAST, &call->flags) &&
		    seq != call->rx_top)
			return rxrpc_proto_abort("LSN", call, seq);
	} else {
		if (test_bit(RXRPC_CALL_RX_LAST, &call->flags) &&
		    after_eq(seq, call->rx_top))
			return rxrpc_proto_abort("LSA", call, seq);
	}

	trace_rxrpc_rx_data(call, seq, serial, flags, annotation);
	if (before_eq(seq, hard_ack)) {
		ack = RXRPC_ACK_DUPLICATE;
		ack_serial = serial;
		goto skip;
	}

	if (flags & RXRPC_REQUEST_ACK && !ack) {
		ack = RXRPC_ACK_REQUESTED;
		ack_serial = serial;
	}

	if (call->rxtx_buffer[ix]) {
		rxrpc_input_dup_data(call, seq, annotation, &jumbo_bad);
		if (ack != RXRPC_ACK_DUPLICATE) {
			ack = RXRPC_ACK_DUPLICATE;
			ack_serial = serial;
		}
		immediate_ack = true;
		goto skip;
	}

	/* Queue the packet.  We use a couple of memory barriers here as need
	 * to make sure that rx_top is perceived to be set after the buffer
	 * pointer and that the buffer pointer is set after the annotation and
	 * the skb data.
	 *
	 * Barriers against rxrpc_recvmsg_data() and rxrpc_rotate_rx_window()
	 * and also rxrpc_fill_out_ack().
	 */
	rxrpc_get_skb(skb, rxrpc_skb_rx_got);
	call->rxtx_annotations[ix] = annotation;
	smp_wmb();
	call->rxtx_buffer[ix] = skb;
	if (after(seq, call->rx_top)) {
		smp_store_release(&call->rx_top, seq);
	} else if (before(seq, call->rx_top)) {
		/* Send an immediate ACK if we fill in a hole */
		if (!ack) {
			ack = RXRPC_ACK_DELAY;
			ack_serial = serial;
		}
		immediate_ack = true;
	}
	if (flags & RXRPC_LAST_PACKET) {
		set_bit(RXRPC_CALL_RX_LAST, &call->flags);
		trace_rxrpc_receive(call, rxrpc_receive_queue_last, serial, seq);
	} else {
		trace_rxrpc_receive(call, rxrpc_receive_queue, serial, seq);
	}
	queued = true;

	if (after_eq(seq, call->rx_expect_next)) {
		if (after(seq, call->rx_expect_next)) {
			_net("OOS %u > %u", seq, call->rx_expect_next);
			ack = RXRPC_ACK_OUT_OF_SEQUENCE;
			ack_serial = serial;
		}
		call->rx_expect_next = seq + 1;
	}

skip:
	offset += len;
	if (flags & RXRPC_JUMBO_PACKET) {
		if (skb_copy_bits(skb, offset, &flags, 1) < 0)
			return rxrpc_proto_abort("XJF", call, seq);
		offset += sizeof(struct rxrpc_jumbo_header);
		seq++;
		serial++;
		annotation++;
		if (flags & RXRPC_JUMBO_PACKET)
			annotation |= RXRPC_RX_ANNO_JLAST;
		if (after(seq, hard_ack + call->rx_winsize)) {
			ack = RXRPC_ACK_EXCEEDS_WINDOW;
			ack_serial = serial;
			if (!jumbo_bad) {
				call->nr_jumbo_bad++;
				jumbo_bad = true;
			}
			goto ack;
		}

		_proto("Rx DATA Jumbo %%%u", serial);
		goto next_subpacket;
	}

	if (queued && flags & RXRPC_LAST_PACKET && !ack) {
		ack = RXRPC_ACK_DELAY;
		ack_serial = serial;
	}

ack:
	if (ack)
		rxrpc_propose_ACK(call, ack, skew, ack_serial,
				  immediate_ack, true,
				  rxrpc_propose_ack_input_data);

	if (sp->hdr.seq == READ_ONCE(call->rx_hard_ack) + 1)
		rxrpc_notify_socket(call);
	_leave(" [queued]");
}

/*
 * Process a requested ACK.
 */
static void rxrpc_input_requested_ack(struct rxrpc_call *call,
				      ktime_t resp_time,
				      rxrpc_serial_t orig_serial,
				      rxrpc_serial_t ack_serial)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	ktime_t sent_at;
	int ix;

	for (ix = 0; ix < RXRPC_RXTX_BUFF_SIZE; ix++) {
		skb = call->rxtx_buffer[ix];
		if (!skb)
			continue;

		sp = rxrpc_skb(skb);
		if (sp->hdr.serial != orig_serial)
			continue;
		smp_rmb();
		sent_at = skb->tstamp;
		goto found;
	}
	return;

found:
	rxrpc_peer_add_rtt(call, rxrpc_rtt_rx_requested_ack,
			   orig_serial, ack_serial, sent_at, resp_time);
}

/*
 * Process the response to a ping that we sent to find out if we lost an ACK.
 *
 * If we got back a ping response that indicates a lower tx_top than what we
 * had at the time of the ping transmission, we adjudge all the DATA packets
 * sent between the response tx_top and the ping-time tx_top to have been lost.
 */
static void rxrpc_input_check_for_lost_ack(struct rxrpc_call *call)
{
	rxrpc_seq_t top, bottom, seq;
	bool resend = false;

	spin_lock_bh(&call->lock);

	bottom = call->tx_hard_ack + 1;
	top = call->acks_lost_top;
	if (before(bottom, top)) {
		for (seq = bottom; before_eq(seq, top); seq++) {
			int ix = seq & RXRPC_RXTX_BUFF_MASK;
			u8 annotation = call->rxtx_annotations[ix];
			u8 anno_type = annotation & RXRPC_TX_ANNO_MASK;

			if (anno_type != RXRPC_TX_ANNO_UNACK)
				continue;
			annotation &= ~RXRPC_TX_ANNO_MASK;
			annotation |= RXRPC_TX_ANNO_RETRANS;
			call->rxtx_annotations[ix] = annotation;
			resend = true;
		}
	}

	spin_unlock_bh(&call->lock);

	if (resend && !test_and_set_bit(RXRPC_CALL_EV_RESEND, &call->events))
		rxrpc_queue_call(call);
}

/*
 * Process a ping response.
 */
static void rxrpc_input_ping_response(struct rxrpc_call *call,
				      ktime_t resp_time,
				      rxrpc_serial_t orig_serial,
				      rxrpc_serial_t ack_serial)
{
	rxrpc_serial_t ping_serial;
	ktime_t ping_time;

	ping_time = call->ping_time;
	smp_rmb();
	ping_serial = call->ping_serial;

	if (orig_serial == call->acks_lost_ping)
		rxrpc_input_check_for_lost_ack(call);

	if (!test_bit(RXRPC_CALL_PINGING, &call->flags) ||
	    before(orig_serial, ping_serial))
		return;
	clear_bit(RXRPC_CALL_PINGING, &call->flags);
	if (after(orig_serial, ping_serial))
		return;

	rxrpc_peer_add_rtt(call, rxrpc_rtt_rx_ping_response,
			   orig_serial, ack_serial, ping_time, resp_time);
}

/*
 * Process the extra information that may be appended to an ACK packet
 */
static void rxrpc_input_ackinfo(struct rxrpc_call *call, struct sk_buff *skb,
				struct rxrpc_ackinfo *ackinfo)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_peer *peer;
	unsigned int mtu;
	bool wake = false;
	u32 rwind = ntohl(ackinfo->rwind);

	_proto("Rx ACK %%%u Info { rx=%u max=%u rwin=%u jm=%u }",
	       sp->hdr.serial,
	       ntohl(ackinfo->rxMTU), ntohl(ackinfo->maxMTU),
	       rwind, ntohl(ackinfo->jumbo_max));

	if (call->tx_winsize != rwind) {
		if (rwind > RXRPC_RXTX_BUFF_SIZE - 1)
			rwind = RXRPC_RXTX_BUFF_SIZE - 1;
		if (rwind > call->tx_winsize)
			wake = true;
		trace_rxrpc_rx_rwind_change(call, sp->hdr.serial,
					    ntohl(ackinfo->rwind), wake);
		call->tx_winsize = rwind;
	}

	if (call->cong_ssthresh > rwind)
		call->cong_ssthresh = rwind;

	mtu = min(ntohl(ackinfo->rxMTU), ntohl(ackinfo->maxMTU));

	peer = call->peer;
	if (mtu < peer->maxdata) {
		spin_lock_bh(&peer->lock);
		peer->maxdata = mtu;
		peer->mtu = mtu + peer->hdrsize;
		spin_unlock_bh(&peer->lock);
		_net("Net MTU %u (maxdata %u)", peer->mtu, peer->maxdata);
	}

	if (wake)
		wake_up(&call->waitq);
}

/*
 * Process individual soft ACKs.
 *
 * Each ACK in the array corresponds to one packet and can be either an ACK or
 * a NAK.  If we get find an explicitly NAK'd packet we resend immediately;
 * packets that lie beyond the end of the ACK list are scheduled for resend by
 * the timer on the basis that the peer might just not have processed them at
 * the time the ACK was sent.
 */
static void rxrpc_input_soft_acks(struct rxrpc_call *call, u8 *acks,
				  rxrpc_seq_t seq, int nr_acks,
				  struct rxrpc_ack_summary *summary)
{
	int ix;
	u8 annotation, anno_type;

	for (; nr_acks > 0; nr_acks--, seq++) {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		annotation = call->rxtx_annotations[ix];
		anno_type = annotation & RXRPC_TX_ANNO_MASK;
		annotation &= ~RXRPC_TX_ANNO_MASK;
		switch (*acks++) {
		case RXRPC_ACK_TYPE_ACK:
			summary->nr_acks++;
			if (anno_type == RXRPC_TX_ANNO_ACK)
				continue;
			summary->nr_new_acks++;
			call->rxtx_annotations[ix] =
				RXRPC_TX_ANNO_ACK | annotation;
			break;
		case RXRPC_ACK_TYPE_NACK:
			if (!summary->nr_nacks &&
			    call->acks_lowest_nak != seq) {
				call->acks_lowest_nak = seq;
				summary->new_low_nack = true;
			}
			summary->nr_nacks++;
			if (anno_type == RXRPC_TX_ANNO_NAK)
				continue;
			summary->nr_new_nacks++;
			if (anno_type == RXRPC_TX_ANNO_RETRANS)
				continue;
			call->rxtx_annotations[ix] =
				RXRPC_TX_ANNO_NAK | annotation;
			break;
		default:
			return rxrpc_proto_abort("SFT", call, 0);
		}
	}
}

/*
 * Process an ACK packet.
 *
 * ack.firstPacket is the sequence number of the first soft-ACK'd/NAK'd packet
 * in the ACK array.  Anything before that is hard-ACK'd and may be discarded.
 *
 * A hard-ACK means that a packet has been processed and may be discarded; a
 * soft-ACK means that the packet may be discarded and retransmission
 * requested.  A phase is complete when all packets are hard-ACK'd.
 */
static void rxrpc_input_ack(struct rxrpc_call *call, struct sk_buff *skb,
			    u16 skew)
{
	struct rxrpc_ack_summary summary = { 0 };
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	union {
		struct rxrpc_ackpacket ack;
		struct rxrpc_ackinfo info;
		u8 acks[RXRPC_MAXACKS];
	} buf;
	rxrpc_serial_t acked_serial;
	rxrpc_seq_t first_soft_ack, hard_ack;
	int nr_acks, offset, ioffset;

	_enter("");

	offset = sizeof(struct rxrpc_wire_header);
	if (skb_copy_bits(skb, offset, &buf.ack, sizeof(buf.ack)) < 0) {
		_debug("extraction failure");
		return rxrpc_proto_abort("XAK", call, 0);
	}
	offset += sizeof(buf.ack);

	acked_serial = ntohl(buf.ack.serial);
	first_soft_ack = ntohl(buf.ack.firstPacket);
	hard_ack = first_soft_ack - 1;
	nr_acks = buf.ack.nAcks;
	summary.ack_reason = (buf.ack.reason < RXRPC_ACK__INVALID ?
			      buf.ack.reason : RXRPC_ACK__INVALID);

	trace_rxrpc_rx_ack(call, sp->hdr.serial, acked_serial,
			   first_soft_ack, ntohl(buf.ack.previousPacket),
			   summary.ack_reason, nr_acks);

	if (buf.ack.reason == RXRPC_ACK_PING_RESPONSE)
		rxrpc_input_ping_response(call, skb->tstamp, acked_serial,
					  sp->hdr.serial);
	if (buf.ack.reason == RXRPC_ACK_REQUESTED)
		rxrpc_input_requested_ack(call, skb->tstamp, acked_serial,
					  sp->hdr.serial);

	if (buf.ack.reason == RXRPC_ACK_PING) {
		_proto("Rx ACK %%%u PING Request", sp->hdr.serial);
		rxrpc_propose_ACK(call, RXRPC_ACK_PING_RESPONSE,
				  skew, sp->hdr.serial, true, true,
				  rxrpc_propose_ack_respond_to_ping);
	} else if (sp->hdr.flags & RXRPC_REQUEST_ACK) {
		rxrpc_propose_ACK(call, RXRPC_ACK_REQUESTED,
				  skew, sp->hdr.serial, true, true,
				  rxrpc_propose_ack_respond_to_ack);
	}

	ioffset = offset + nr_acks + 3;
	if (skb->len >= ioffset + sizeof(buf.info)) {
		if (skb_copy_bits(skb, ioffset, &buf.info, sizeof(buf.info)) < 0)
			return rxrpc_proto_abort("XAI", call, 0);
		rxrpc_input_ackinfo(call, skb, &buf.info);
	}

	if (first_soft_ack == 0)
		return rxrpc_proto_abort("AK0", call, 0);

	/* Ignore ACKs unless we are or have just been transmitting. */
	switch (READ_ONCE(call->state)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
	case RXRPC_CALL_SERVER_SEND_REPLY:
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		break;
	default:
		return;
	}

	/* Discard any out-of-order or duplicate ACKs. */
	if (before_eq(sp->hdr.serial, call->acks_latest)) {
		_debug("discard ACK %d <= %d",
		       sp->hdr.serial, call->acks_latest);
		return;
	}
	call->acks_latest_ts = skb->tstamp;
	call->acks_latest = sp->hdr.serial;

	if (before(hard_ack, call->tx_hard_ack) ||
	    after(hard_ack, call->tx_top))
		return rxrpc_proto_abort("AKW", call, 0);
	if (nr_acks > call->tx_top - hard_ack)
		return rxrpc_proto_abort("AKN", call, 0);

	if (after(hard_ack, call->tx_hard_ack))
		rxrpc_rotate_tx_window(call, hard_ack, &summary);

	if (nr_acks > 0) {
		if (skb_copy_bits(skb, offset, buf.acks, nr_acks) < 0)
			return rxrpc_proto_abort("XSA", call, 0);
		rxrpc_input_soft_acks(call, buf.acks, first_soft_ack, nr_acks,
				      &summary);
	}

	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags)) {
		rxrpc_end_tx_phase(call, false, "ETA");
		return;
	}

	if (call->rxtx_annotations[call->tx_top & RXRPC_RXTX_BUFF_MASK] &
	    RXRPC_TX_ANNO_LAST &&
	    summary.nr_acks == call->tx_top - hard_ack &&
	    rxrpc_is_client_call(call))
		rxrpc_propose_ACK(call, RXRPC_ACK_PING, skew, sp->hdr.serial,
				  false, true,
				  rxrpc_propose_ack_ping_for_lost_reply);

	return rxrpc_congestion_management(call, skb, &summary, acked_serial);
}

/*
 * Process an ACKALL packet.
 */
static void rxrpc_input_ackall(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_ack_summary summary = { 0 };
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	_proto("Rx ACKALL %%%u", sp->hdr.serial);

	rxrpc_rotate_tx_window(call, call->tx_top, &summary);
	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags))
		rxrpc_end_tx_phase(call, false, "ETL");
}

/*
 * Process an ABORT packet directed at a call.
 */
static void rxrpc_input_abort(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	__be32 wtmp;
	u32 abort_code = RX_CALL_DEAD;

	_enter("");

	if (skb->len >= 4 &&
	    skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  &wtmp, sizeof(wtmp)) >= 0)
		abort_code = ntohl(wtmp);

	trace_rxrpc_rx_abort(call, sp->hdr.serial, abort_code);

	_proto("Rx ABORT %%%u { %x }", sp->hdr.serial, abort_code);

	if (rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
				      abort_code, -ECONNABORTED))
		rxrpc_notify_socket(call);
}

/*
 * Process an incoming call packet.
 */
static void rxrpc_input_call_packet(struct rxrpc_call *call,
				    struct sk_buff *skb, u16 skew)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	unsigned long timo;

	_enter("%p,%p", call, skb);

	timo = READ_ONCE(call->next_rx_timo);
	if (timo) {
		unsigned long now = jiffies, expect_rx_by;

		expect_rx_by = now + timo;
		WRITE_ONCE(call->expect_rx_by, expect_rx_by);
		rxrpc_reduce_call_timer(call, expect_rx_by, now,
					rxrpc_timer_set_for_normal);
	}

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_DATA:
		rxrpc_input_data(call, skb, skew);
		break;

	case RXRPC_PACKET_TYPE_ACK:
		rxrpc_input_ack(call, skb, skew);
		break;

	case RXRPC_PACKET_TYPE_BUSY:
		_proto("Rx BUSY %%%u", sp->hdr.serial);

		/* Just ignore BUSY packets from the server; the retry and
		 * lifespan timers will take care of business.  BUSY packets
		 * from the client don't make sense.
		 */
		break;

	case RXRPC_PACKET_TYPE_ABORT:
		rxrpc_input_abort(call, skb);
		break;

	case RXRPC_PACKET_TYPE_ACKALL:
		rxrpc_input_ackall(call, skb);
		break;

	default:
		break;
	}

	_leave("");
}

/*
 * Handle a new call on a channel implicitly completing the preceding call on
 * that channel.
 *
 * TODO: If callNumber > call_id + 1, renegotiate security.
 */
static void rxrpc_input_implicit_end_call(struct rxrpc_connection *conn,
					  struct rxrpc_call *call)
{
	switch (READ_ONCE(call->state)) {
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		rxrpc_call_completed(call);
		break;
	case RXRPC_CALL_COMPLETE:
		break;
	default:
		if (rxrpc_abort_call("IMP", call, 0, RX_CALL_DEAD, -ESHUTDOWN)) {
			set_bit(RXRPC_CALL_EV_ABORT, &call->events);
			rxrpc_queue_call(call);
		}
		break;
	}

	trace_rxrpc_improper_term(call);
	__rxrpc_disconnect_call(conn, call);
	rxrpc_notify_socket(call);
}

/*
 * post connection-level events to the connection
 * - this includes challenges, responses, some aborts and call terminal packet
 *   retransmission.
 */
static void rxrpc_post_packet_to_conn(struct rxrpc_connection *conn,
				      struct sk_buff *skb)
{
	_enter("%p,%p", conn, skb);

	skb_queue_tail(&conn->rx_queue, skb);
	rxrpc_queue_conn(conn);
}

/*
 * post endpoint-level events to the local endpoint
 * - this includes debug and version messages
 */
static void rxrpc_post_packet_to_local(struct rxrpc_local *local,
				       struct sk_buff *skb)
{
	_enter("%p,%p", local, skb);

	skb_queue_tail(&local->event_queue, skb);
	rxrpc_queue_local(local);
}

/*
 * put a packet up for transport-level abort
 */
static void rxrpc_reject_packet(struct rxrpc_local *local, struct sk_buff *skb)
{
	CHECK_SLAB_OKAY(&local->usage);

	skb_queue_tail(&local->reject_queue, skb);
	rxrpc_queue_local(local);
}

/*
 * Extract the wire header from a packet and translate the byte order.
 */
static noinline
int rxrpc_extract_header(struct rxrpc_skb_priv *sp, struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;

	/* dig out the RxRPC connection details */
	if (skb_copy_bits(skb, 0, &whdr, sizeof(whdr)) < 0) {
		trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
				      tracepoint_string("bad_hdr"));
		return -EBADMSG;
	}

	memset(sp, 0, sizeof(*sp));
	sp->hdr.epoch		= ntohl(whdr.epoch);
	sp->hdr.cid		= ntohl(whdr.cid);
	sp->hdr.callNumber	= ntohl(whdr.callNumber);
	sp->hdr.seq		= ntohl(whdr.seq);
	sp->hdr.serial		= ntohl(whdr.serial);
	sp->hdr.flags		= whdr.flags;
	sp->hdr.type		= whdr.type;
	sp->hdr.userStatus	= whdr.userStatus;
	sp->hdr.securityIndex	= whdr.securityIndex;
	sp->hdr._rsvd		= ntohs(whdr._rsvd);
	sp->hdr.serviceId	= ntohs(whdr.serviceId);
	return 0;
}

/*
 * handle data received on the local endpoint
 * - may be called in interrupt context
 *
 * The socket is locked by the caller and this prevents the socket from being
 * shut down and the local endpoint from going away, thus sk_user_data will not
 * be cleared until this function returns.
 */
void rxrpc_data_ready(struct sock *udp_sk)
{
	struct rxrpc_connection *conn;
	struct rxrpc_channel *chan;
	struct rxrpc_call *call;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_local *local = udp_sk->sk_user_data;
	struct sk_buff *skb;
	unsigned int channel;
	int ret, skew;

	_enter("%p", udp_sk);

	ASSERT(!irqs_disabled());

	skb = skb_recv_udp(udp_sk, 0, 1, &ret);
	if (!skb) {
		if (ret == -EAGAIN)
			return;
		_debug("UDP socket error %d", ret);
		return;
	}

	rxrpc_new_skb(skb, rxrpc_skb_rx_received);

	_net("recv skb %p", skb);

	/* we'll probably need to checksum it (didn't call sock_recvmsg) */
	if (skb_checksum_complete(skb)) {
		rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
		__UDP_INC_STATS(&init_net, UDP_MIB_INERRORS, 0);
		_leave(" [CSUM failed]");
		return;
	}

	__UDP_INC_STATS(&init_net, UDP_MIB_INDATAGRAMS, 0);

	/* The UDP protocol already released all skb resources;
	 * we are free to add our own data there.
	 */
	sp = rxrpc_skb(skb);

	/* dig out the RxRPC connection details */
	if (rxrpc_extract_header(sp, skb) < 0)
		goto bad_message;

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			trace_rxrpc_rx_lose(sp);
			rxrpc_lose_skb(skb, rxrpc_skb_rx_lost);
			return;
		}
	}

	trace_rxrpc_rx_packet(sp);

	_net("Rx RxRPC %s ep=%x call=%x:%x",
	     sp->hdr.flags & RXRPC_CLIENT_INITIATED ? "ToServer" : "ToClient",
	     sp->hdr.epoch, sp->hdr.cid, sp->hdr.callNumber);

	if (sp->hdr.type >= RXRPC_N_PACKET_TYPES ||
	    !((RXRPC_SUPPORTED_PACKET_TYPES >> sp->hdr.type) & 1)) {
		_proto("Rx Bad Packet Type %u", sp->hdr.type);
		goto bad_message;
	}

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_VERSION:
		if (!(sp->hdr.flags & RXRPC_CLIENT_INITIATED))
			goto discard;
		rxrpc_post_packet_to_local(local, skb);
		goto out;

	case RXRPC_PACKET_TYPE_BUSY:
		if (sp->hdr.flags & RXRPC_CLIENT_INITIATED)
			goto discard;
		/* Fall through */

	case RXRPC_PACKET_TYPE_DATA:
		if (sp->hdr.callNumber == 0)
			goto bad_message;
		if (sp->hdr.flags & RXRPC_JUMBO_PACKET &&
		    !rxrpc_validate_jumbo(skb))
			goto bad_message;
		break;

		/* Packet types 9-11 should just be ignored. */
	case RXRPC_PACKET_TYPE_PARAMS:
	case RXRPC_PACKET_TYPE_10:
	case RXRPC_PACKET_TYPE_11:
		goto discard;
	}

	rcu_read_lock();

	conn = rxrpc_find_connection_rcu(local, skb);
	if (conn) {
		if (sp->hdr.securityIndex != conn->security_ix)
			goto wrong_security;

		if (sp->hdr.serviceId != conn->service_id) {
			if (!test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags) ||
			    conn->service_id != conn->params.service_id)
				goto reupgrade;
			conn->service_id = sp->hdr.serviceId;
		}

		if (sp->hdr.callNumber == 0) {
			/* Connection-level packet */
			_debug("CONN %p {%d}", conn, conn->debug_id);
			rxrpc_post_packet_to_conn(conn, skb);
			goto out_unlock;
		}

		/* Note the serial number skew here */
		skew = (int)sp->hdr.serial - (int)conn->hi_serial;
		if (skew >= 0) {
			if (skew > 0)
				conn->hi_serial = sp->hdr.serial;
		} else {
			skew = -skew;
			skew = min(skew, 65535);
		}

		/* Call-bound packets are routed by connection channel. */
		channel = sp->hdr.cid & RXRPC_CHANNELMASK;
		chan = &conn->channels[channel];

		/* Ignore really old calls */
		if (sp->hdr.callNumber < chan->last_call)
			goto discard_unlock;

		if (sp->hdr.callNumber == chan->last_call) {
			if (chan->call ||
			    sp->hdr.type == RXRPC_PACKET_TYPE_ABORT)
				goto discard_unlock;

			/* For the previous service call, if completed
			 * successfully, we discard all further packets.
			 */
			if (rxrpc_conn_is_service(conn) &&
			    chan->last_type == RXRPC_PACKET_TYPE_ACK)
				goto discard_unlock;

			/* But otherwise we need to retransmit the final packet
			 * from data cached in the connection record.
			 */
			rxrpc_post_packet_to_conn(conn, skb);
			goto out_unlock;
		}

		call = rcu_dereference(chan->call);

		if (sp->hdr.callNumber > chan->call_id) {
			if (!(sp->hdr.flags & RXRPC_CLIENT_INITIATED)) {
				rcu_read_unlock();
				goto reject_packet;
			}
			if (call)
				rxrpc_input_implicit_end_call(conn, call);
			call = NULL;
		}

		if (call) {
			if (sp->hdr.serviceId != call->service_id)
				call->service_id = sp->hdr.serviceId;
			if ((int)sp->hdr.serial - (int)call->rx_serial > 0)
				call->rx_serial = sp->hdr.serial;
			if (!test_bit(RXRPC_CALL_RX_HEARD, &call->flags))
				set_bit(RXRPC_CALL_RX_HEARD, &call->flags);
		}
	} else {
		skew = 0;
		call = NULL;
	}

	if (!call || atomic_read(&call->usage) == 0) {
		if (!(sp->hdr.type & RXRPC_CLIENT_INITIATED) ||
		    sp->hdr.callNumber == 0 ||
		    sp->hdr.type != RXRPC_PACKET_TYPE_DATA)
			goto bad_message_unlock;
		if (sp->hdr.seq != 1)
			goto discard_unlock;
		call = rxrpc_new_incoming_call(local, conn, skb);
		if (!call) {
			rcu_read_unlock();
			goto reject_packet;
		}
		rxrpc_send_ping(call, skb, skew);
		mutex_unlock(&call->user_mutex);
	}

	rxrpc_input_call_packet(call, skb, skew);
	goto discard_unlock;

discard_unlock:
	rcu_read_unlock();
discard:
	rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
out:
	trace_rxrpc_rx_done(0, 0);
	return;

out_unlock:
	rcu_read_unlock();
	goto out;

wrong_security:
	rcu_read_unlock();
	trace_rxrpc_abort(0, "SEC", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RXKADINCONSISTENCY, EBADMSG);
	skb->priority = RXKADINCONSISTENCY;
	goto post_abort;

reupgrade:
	rcu_read_unlock();
	trace_rxrpc_abort(0, "UPG", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_PROTOCOL_ERROR, EBADMSG);
	goto protocol_error;

bad_message_unlock:
	rcu_read_unlock();
bad_message:
	trace_rxrpc_abort(0, "BAD", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_PROTOCOL_ERROR, EBADMSG);
protocol_error:
	skb->priority = RX_PROTOCOL_ERROR;
post_abort:
	skb->mark = RXRPC_SKB_MARK_LOCAL_ABORT;
reject_packet:
	trace_rxrpc_rx_done(skb->mark, skb->priority);
	rxrpc_reject_packet(local, skb);
	_leave(" [badmsg]");
}
