// SPDX-License-Identifier: GPL-2.0-or-later
/* Processing of received RxRPC packets
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ar-internal.h"

/* Override priority when generating ACKs for received DATA */
static const u8 rxrpc_ack_priority[RXRPC_ACK__INVALID] = {
	[RXRPC_ACK_IDLE]		= 1,
	[RXRPC_ACK_DELAY]		= 2,
	[RXRPC_ACK_REQUESTED]		= 3,
	[RXRPC_ACK_DUPLICATE]		= 4,
	[RXRPC_ACK_EXCEEDS_WINDOW]	= 5,
	[RXRPC_ACK_NOSPACE]		= 6,
	[RXRPC_ACK_OUT_OF_SEQUENCE]	= 7,
};

static void rxrpc_proto_abort(struct rxrpc_call *call, rxrpc_seq_t seq,
			      enum rxrpc_abort_reason why)
{
	rxrpc_abort_call(call, seq, RX_PROTOCOL_ERROR, -EBADMSG, why);
}

/*
 * Do TCP-style congestion management [RFC5681].
 */
static void rxrpc_congestion_management(struct rxrpc_call *call,
					struct rxrpc_ack_summary *summary)
{
	summary->change = rxrpc_cong_no_change;
	summary->in_flight = rxrpc_tx_in_flight(call);

	if (test_and_clear_bit(RXRPC_CALL_RETRANS_TIMEOUT, &call->flags)) {
		summary->retrans_timeo = true;
		call->cong_ssthresh = umax(summary->in_flight / 2, 2);
		call->cong_cwnd = 1;
		if (call->cong_cwnd >= call->cong_ssthresh &&
		    call->cong_ca_state == RXRPC_CA_SLOW_START) {
			call->cong_ca_state = RXRPC_CA_CONGEST_AVOIDANCE;
			call->cong_tstamp = call->acks_latest_ts;
			call->cong_cumul_acks = 0;
		}
	}

	call->cong_cumul_acks += summary->nr_new_sacks;
	call->cong_cumul_acks += summary->nr_new_hacks;
	if (call->cong_cumul_acks > 255)
		call->cong_cumul_acks = 255;

	switch (call->cong_ca_state) {
	case RXRPC_CA_SLOW_START:
		if (call->acks_nr_snacks > 0)
			goto packet_loss_detected;
		if (call->cong_cumul_acks > 0)
			call->cong_cwnd += 1;
		if (call->cong_cwnd >= call->cong_ssthresh) {
			call->cong_ca_state = RXRPC_CA_CONGEST_AVOIDANCE;
			call->cong_tstamp = call->acks_latest_ts;
		}
		goto out;

	case RXRPC_CA_CONGEST_AVOIDANCE:
		if (call->acks_nr_snacks > 0)
			goto packet_loss_detected;

		/* We analyse the number of packets that get ACK'd per RTT
		 * period and increase the window if we managed to fill it.
		 */
		if (call->rtt_count == 0)
			goto out;
		if (ktime_before(call->acks_latest_ts,
				 ktime_add_us(call->cong_tstamp,
					      call->srtt_us >> 3)))
			goto out_no_clear_ca;
		summary->change = rxrpc_cong_rtt_window_end;
		call->cong_tstamp = call->acks_latest_ts;
		if (call->cong_cumul_acks >= call->cong_cwnd)
			call->cong_cwnd++;
		goto out;

	case RXRPC_CA_PACKET_LOSS:
		if (call->acks_nr_snacks == 0)
			goto resume_normality;

		if (summary->new_low_snack) {
			summary->change = rxrpc_cong_new_low_nack;
			call->cong_dup_acks = 1;
			if (call->cong_extra > 1)
				call->cong_extra = 1;
			goto send_extra_data;
		}

		call->cong_dup_acks++;
		if (call->cong_dup_acks < 3)
			goto send_extra_data;

		summary->change = rxrpc_cong_begin_retransmission;
		call->cong_ca_state = RXRPC_CA_FAST_RETRANSMIT;
		call->cong_ssthresh = umax(summary->in_flight / 2, 2);
		call->cong_cwnd = call->cong_ssthresh + 3;
		call->cong_extra = 0;
		call->cong_dup_acks = 0;
		summary->need_retransmit = true;
		summary->in_fast_or_rto_recovery = true;
		goto out;

	case RXRPC_CA_FAST_RETRANSMIT:
		rxrpc_tlp_init(call);
		summary->in_fast_or_rto_recovery = true;
		if (!summary->new_low_snack) {
			if (summary->nr_new_sacks == 0)
				call->cong_cwnd += 1;
			call->cong_dup_acks++;
			if (call->cong_dup_acks == 2) {
				summary->change = rxrpc_cong_retransmit_again;
				call->cong_dup_acks = 0;
				summary->need_retransmit = true;
			}
		} else {
			summary->change = rxrpc_cong_progress;
			call->cong_cwnd = call->cong_ssthresh;
			if (call->acks_nr_snacks == 0) {
				summary->exiting_fast_or_rto_recovery = true;
				goto resume_normality;
			}
		}
		goto out;

	default:
		BUG();
		goto out;
	}

resume_normality:
	summary->change = rxrpc_cong_cleared_nacks;
	call->cong_dup_acks = 0;
	call->cong_extra = 0;
	call->cong_tstamp = call->acks_latest_ts;
	if (call->cong_cwnd < call->cong_ssthresh)
		call->cong_ca_state = RXRPC_CA_SLOW_START;
	else
		call->cong_ca_state = RXRPC_CA_CONGEST_AVOIDANCE;
out:
	call->cong_cumul_acks = 0;
out_no_clear_ca:
	if (call->cong_cwnd >= RXRPC_TX_MAX_WINDOW)
		call->cong_cwnd = RXRPC_TX_MAX_WINDOW;
	trace_rxrpc_congest(call, summary);
	return;

packet_loss_detected:
	summary->change = rxrpc_cong_saw_nack;
	call->cong_ca_state = RXRPC_CA_PACKET_LOSS;
	call->cong_dup_acks = 0;
	goto send_extra_data;

send_extra_data:
	/* Send some previously unsent DATA if we have some to advance the ACK
	 * state.
	 */
	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags) ||
	    call->acks_nr_sacks != call->tx_top - call->tx_bottom) {
		call->cong_extra++;
		wake_up(&call->waitq);
	}
	goto out_no_clear_ca;
}

/*
 * Degrade the congestion window if we haven't transmitted a packet for >1RTT.
 */
void rxrpc_congestion_degrade(struct rxrpc_call *call)
{
	ktime_t rtt, now, time_since;

	if (call->cong_ca_state != RXRPC_CA_SLOW_START &&
	    call->cong_ca_state != RXRPC_CA_CONGEST_AVOIDANCE)
		return;
	if (__rxrpc_call_state(call) == RXRPC_CALL_CLIENT_AWAIT_REPLY)
		return;

	rtt = ns_to_ktime(call->srtt_us * (NSEC_PER_USEC / 8));
	now = ktime_get_real();
	time_since = ktime_sub(now, call->tx_last_sent);
	if (ktime_before(time_since, rtt))
		return;

	trace_rxrpc_reset_cwnd(call, time_since, rtt);
	rxrpc_inc_stat(call->rxnet, stat_tx_data_cwnd_reset);
	call->tx_last_sent = now;
	call->cong_ca_state = RXRPC_CA_SLOW_START;
	call->cong_ssthresh = umax(call->cong_ssthresh, call->cong_cwnd * 3 / 4);
	call->cong_cwnd = umax(call->cong_cwnd / 2, RXRPC_MIN_CWND);
}

/*
 * Add an RTT sample derived from an ACK'd DATA packet.
 */
static void rxrpc_add_data_rtt_sample(struct rxrpc_call *call,
				      struct rxrpc_ack_summary *summary,
				      struct rxrpc_txqueue *tq,
				      int ix)
{
	ktime_t xmit_ts = ktime_add_us(tq->xmit_ts_base, tq->segment_xmit_ts[ix]);

	rxrpc_call_add_rtt(call, rxrpc_rtt_rx_data_ack, -1,
			   summary->acked_serial, summary->ack_serial,
			   xmit_ts, call->acks_latest_ts);
	__clear_bit(ix, &tq->rtt_samples); /* Prevent repeat RTT sample */
}

/*
 * Apply a hard ACK by advancing the Tx window.
 */
static bool rxrpc_rotate_tx_window(struct rxrpc_call *call, rxrpc_seq_t to,
				   struct rxrpc_ack_summary *summary)
{
	struct rxrpc_txqueue *tq = call->tx_queue;
	rxrpc_seq_t seq = call->tx_bottom + 1;
	bool rot_last = false, trace = false;

	_enter("%x,%x", call->tx_bottom, to);

	trace_rxrpc_tx_rotate(call, seq, to);
	trace_rxrpc_tq(call, tq, seq, rxrpc_tq_rotate);

	if (call->acks_lowest_nak == call->tx_bottom) {
		call->acks_lowest_nak = to;
	} else if (after(to, call->acks_lowest_nak)) {
		summary->new_low_snack = true;
		call->acks_lowest_nak = to;
	}

	/* We may have a left over fully-consumed buffer at the front that we
	 * couldn't drop before (rotate_and_keep below).
	 */
	if (seq == call->tx_qbase + RXRPC_NR_TXQUEUE) {
		call->tx_qbase += RXRPC_NR_TXQUEUE;
		call->tx_queue = tq->next;
		trace_rxrpc_tq(call, tq, seq, rxrpc_tq_rotate_and_free);
		kfree(tq);
		tq = call->tx_queue;
	}

	do {
		unsigned int ix = seq - call->tx_qbase;

		_debug("tq=%x seq=%x i=%d f=%x", tq->qbase, seq, ix, tq->bufs[ix]->flags);
		if (tq->bufs[ix]->flags & RXRPC_LAST_PACKET) {
			set_bit(RXRPC_CALL_TX_LAST, &call->flags);
			rot_last = true;
		}

		if (summary->acked_serial == tq->segment_serial[ix] &&
		    test_bit(ix, &tq->rtt_samples))
			rxrpc_add_data_rtt_sample(call, summary, tq, ix);

		if (ix == tq->nr_reported_acks) {
			/* Packet directly hard ACK'd. */
			tq->nr_reported_acks++;
			rxrpc_input_rack_one(call, summary, tq, ix);
			if (seq == call->tlp_seq)
				summary->tlp_probe_acked = true;
			summary->nr_new_hacks++;
			__set_bit(ix, &tq->segment_acked);
			trace_rxrpc_rotate(call, tq, summary, seq, rxrpc_rotate_trace_hack);
		} else if (test_bit(ix, &tq->segment_acked)) {
			/* Soft ACK -> hard ACK. */
			call->acks_nr_sacks--;
			trace_rxrpc_rotate(call, tq, summary, seq, rxrpc_rotate_trace_sack);
		} else {
			/* Soft NAK -> hard ACK. */
			call->acks_nr_snacks--;
			rxrpc_input_rack_one(call, summary, tq, ix);
			if (seq == call->tlp_seq)
				summary->tlp_probe_acked = true;
			summary->nr_new_hacks++;
			__set_bit(ix, &tq->segment_acked);
			trace_rxrpc_rotate(call, tq, summary, seq, rxrpc_rotate_trace_snak);
		}

		call->tx_nr_sent--;
		if (__test_and_clear_bit(ix, &tq->segment_lost))
			call->tx_nr_lost--;
		if (__test_and_clear_bit(ix, &tq->segment_retransmitted))
			call->tx_nr_resent--;
		__clear_bit(ix, &tq->ever_retransmitted);

		rxrpc_put_txbuf(tq->bufs[ix], rxrpc_txbuf_put_rotated);
		tq->bufs[ix] = NULL;

		WRITE_ONCE(call->tx_bottom, seq);
		trace_rxrpc_txqueue(call, (rot_last ?
					   rxrpc_txqueue_rotate_last :
					   rxrpc_txqueue_rotate));

		seq++;
		trace = true;
		if (!(seq & RXRPC_TXQ_MASK)) {
			trace_rxrpc_rack_update(call, summary);
			trace = false;
			prefetch(tq->next);
			if (tq != call->tx_qtail) {
				call->tx_qbase += RXRPC_NR_TXQUEUE;
				call->tx_queue = tq->next;
				trace_rxrpc_tq(call, tq, seq, rxrpc_tq_rotate_and_free);
				kfree(tq);
				tq = call->tx_queue;
			} else {
				trace_rxrpc_tq(call, tq, seq, rxrpc_tq_rotate_and_keep);
				tq = NULL;
				break;
			}
		}

	} while (before_eq(seq, to));

	if (trace)
		trace_rxrpc_rack_update(call, summary);

	if (rot_last) {
		set_bit(RXRPC_CALL_TX_ALL_ACKED, &call->flags);
		if (tq) {
			trace_rxrpc_tq(call, tq, seq, rxrpc_tq_rotate_and_free);
			kfree(tq);
			call->tx_queue = NULL;
		}
	}

	_debug("%x,%x,%x,%d", to, call->tx_bottom, call->tx_top, rot_last);

	wake_up(&call->waitq);
	return rot_last;
}

/*
 * End the transmission phase of a call.
 *
 * This occurs when we get an ACKALL packet, the first DATA packet of a reply,
 * or a final ACK packet.
 */
static void rxrpc_end_tx_phase(struct rxrpc_call *call, bool reply_begun,
			       enum rxrpc_abort_reason abort_why)
{
	ASSERT(test_bit(RXRPC_CALL_TX_LAST, &call->flags));

	call->rack_timer_mode = RXRPC_CALL_RACKTIMER_OFF;
	call->rack_timo_at = KTIME_MAX;
	trace_rxrpc_rack_timer(call, 0, false);
	trace_rxrpc_timer_can(call, rxrpc_timer_trace_rack_off + call->rack_timer_mode);

	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		if (reply_begun) {
			rxrpc_set_call_state(call, RXRPC_CALL_CLIENT_RECV_REPLY);
			trace_rxrpc_txqueue(call, rxrpc_txqueue_end);
			break;
		}

		rxrpc_set_call_state(call, RXRPC_CALL_CLIENT_AWAIT_REPLY);
		trace_rxrpc_txqueue(call, rxrpc_txqueue_await_reply);
		break;

	case RXRPC_CALL_SERVER_AWAIT_ACK:
		rxrpc_call_completed(call);
		trace_rxrpc_txqueue(call, rxrpc_txqueue_end);
		break;

	default:
		kdebug("end_tx %s", rxrpc_call_states[__rxrpc_call_state(call)]);
		rxrpc_proto_abort(call, call->tx_top, abort_why);
		break;
	}
}

/*
 * Begin the reply reception phase of a call.
 */
static bool rxrpc_receiving_reply(struct rxrpc_call *call)
{
	struct rxrpc_ack_summary summary = { 0 };
	rxrpc_seq_t top = READ_ONCE(call->tx_top);

	if (call->ackr_reason) {
		call->delay_ack_at = KTIME_MAX;
		trace_rxrpc_timer_can(call, rxrpc_timer_trace_delayed_ack);
	}

	if (!test_bit(RXRPC_CALL_TX_LAST, &call->flags)) {
		if (!rxrpc_rotate_tx_window(call, top, &summary)) {
			rxrpc_proto_abort(call, top, rxrpc_eproto_early_reply);
			return false;
		}
	}

	rxrpc_end_tx_phase(call, true, rxrpc_eproto_unexpected_reply);
	return true;
}

/*
 * End the packet reception phase.
 */
static void rxrpc_end_rx_phase(struct rxrpc_call *call, rxrpc_serial_t serial)
{
	rxrpc_seq_t whigh = READ_ONCE(call->rx_highest_seq);

	_enter("%d,%s", call->debug_id, rxrpc_call_states[__rxrpc_call_state(call)]);

	trace_rxrpc_receive(call, rxrpc_receive_end, 0, whigh);

	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_RECV_REPLY:
		rxrpc_propose_delay_ACK(call, serial, rxrpc_propose_ack_terminal_ack);
		rxrpc_call_completed(call);
		break;

	case RXRPC_CALL_SERVER_RECV_REQUEST:
		rxrpc_set_call_state(call, RXRPC_CALL_SERVER_ACK_REQUEST);
		call->expect_req_by = KTIME_MAX;
		rxrpc_propose_delay_ACK(call, serial, rxrpc_propose_ack_processing_op);
		break;

	default:
		break;
	}
}

static void rxrpc_input_update_ack_window(struct rxrpc_call *call,
					  rxrpc_seq_t window, rxrpc_seq_t wtop)
{
	call->ackr_window = window;
	call->ackr_wtop = wtop;
}

/*
 * Push a DATA packet onto the Rx queue.
 */
static void rxrpc_input_queue_data(struct rxrpc_call *call, struct sk_buff *skb,
				   rxrpc_seq_t window, rxrpc_seq_t wtop,
				   enum rxrpc_receive_trace why)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	bool last = sp->hdr.flags & RXRPC_LAST_PACKET;

	spin_lock_irq(&call->recvmsg_queue.lock);

	__skb_queue_tail(&call->recvmsg_queue, skb);
	rxrpc_input_update_ack_window(call, window, wtop);
	trace_rxrpc_receive(call, last ? why + 1 : why, sp->hdr.serial, sp->hdr.seq);
	if (last)
		/* Change the state inside the lock so that recvmsg syncs
		 * correctly with it and using sendmsg() to send a reply
		 * doesn't race.
		 */
		rxrpc_end_rx_phase(call, sp->hdr.serial);

	spin_unlock_irq(&call->recvmsg_queue.lock);
}

/*
 * Process a DATA packet.
 */
static void rxrpc_input_data_one(struct rxrpc_call *call, struct sk_buff *skb,
				 bool *_notify, rxrpc_serial_t *_ack_serial, int *_ack_reason)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct sk_buff *oos;
	rxrpc_serial_t serial = sp->hdr.serial;
	unsigned int sack = call->ackr_sack_base;
	rxrpc_seq_t window = call->ackr_window;
	rxrpc_seq_t wtop = call->ackr_wtop;
	rxrpc_seq_t wlimit = window + call->rx_winsize - 1;
	rxrpc_seq_t seq = sp->hdr.seq;
	bool last = sp->hdr.flags & RXRPC_LAST_PACKET;
	int ack_reason = -1;

	rxrpc_inc_stat(call->rxnet, stat_rx_data);
	if (sp->hdr.flags & RXRPC_REQUEST_ACK)
		rxrpc_inc_stat(call->rxnet, stat_rx_data_reqack);
	if (sp->hdr.flags & RXRPC_JUMBO_PACKET)
		rxrpc_inc_stat(call->rxnet, stat_rx_data_jumbo);

	if (last) {
		if (test_and_set_bit(RXRPC_CALL_RX_LAST, &call->flags) &&
		    seq + 1 != wtop)
			return rxrpc_proto_abort(call, seq, rxrpc_eproto_different_last);
	} else {
		if (test_bit(RXRPC_CALL_RX_LAST, &call->flags) &&
		    after_eq(seq, wtop)) {
			pr_warn("Packet beyond last: c=%x q=%x window=%x-%x wlimit=%x\n",
				call->debug_id, seq, window, wtop, wlimit);
			return rxrpc_proto_abort(call, seq, rxrpc_eproto_data_after_last);
		}
	}

	if (after(seq, call->rx_highest_seq))
		call->rx_highest_seq = seq;

	trace_rxrpc_rx_data(call->debug_id, seq, serial, sp->hdr.flags);

	if (before(seq, window)) {
		ack_reason = RXRPC_ACK_DUPLICATE;
		goto send_ack;
	}
	if (after(seq, wlimit)) {
		ack_reason = RXRPC_ACK_EXCEEDS_WINDOW;
		goto send_ack;
	}

	/* Queue the packet. */
	if (seq == window) {
		if (sp->hdr.flags & RXRPC_REQUEST_ACK)
			ack_reason = RXRPC_ACK_REQUESTED;
		/* Send an immediate ACK if we fill in a hole */
		else if (!skb_queue_empty(&call->rx_oos_queue))
			ack_reason = RXRPC_ACK_DELAY;

		window++;
		if (after(window, wtop)) {
			trace_rxrpc_sack(call, seq, sack, rxrpc_sack_none);
			wtop = window;
		} else {
			trace_rxrpc_sack(call, seq, sack, rxrpc_sack_advance);
			sack = (sack + 1) % RXRPC_SACK_SIZE;
		}


		rxrpc_get_skb(skb, rxrpc_skb_get_to_recvmsg);

		rxrpc_input_queue_data(call, skb, window, wtop, rxrpc_receive_queue);
		*_notify = true;

		while ((oos = skb_peek(&call->rx_oos_queue))) {
			struct rxrpc_skb_priv *osp = rxrpc_skb(oos);

			if (after(osp->hdr.seq, window))
				break;

			__skb_unlink(oos, &call->rx_oos_queue);
			last = osp->hdr.flags & RXRPC_LAST_PACKET;
			seq = osp->hdr.seq;
			call->ackr_sack_table[sack] = 0;
			trace_rxrpc_sack(call, seq, sack, rxrpc_sack_fill);
			sack = (sack + 1) % RXRPC_SACK_SIZE;

			window++;
			rxrpc_input_queue_data(call, oos, window, wtop,
					       rxrpc_receive_queue_oos);
		}

		call->ackr_sack_base = sack;
	} else {
		unsigned int slot;

		ack_reason = RXRPC_ACK_OUT_OF_SEQUENCE;

		slot = seq - window;
		sack = (sack + slot) % RXRPC_SACK_SIZE;

		if (call->ackr_sack_table[sack % RXRPC_SACK_SIZE]) {
			ack_reason = RXRPC_ACK_DUPLICATE;
			goto send_ack;
		}

		call->ackr_sack_table[sack % RXRPC_SACK_SIZE] |= 1;
		trace_rxrpc_sack(call, seq, sack, rxrpc_sack_oos);

		if (after(seq + 1, wtop)) {
			wtop = seq + 1;
			rxrpc_input_update_ack_window(call, window, wtop);
		}

		skb_queue_walk(&call->rx_oos_queue, oos) {
			struct rxrpc_skb_priv *osp = rxrpc_skb(oos);

			if (after(osp->hdr.seq, seq)) {
				rxrpc_get_skb(skb, rxrpc_skb_get_to_recvmsg_oos);
				__skb_queue_before(&call->rx_oos_queue, oos, skb);
				goto oos_queued;
			}
		}

		rxrpc_get_skb(skb, rxrpc_skb_get_to_recvmsg_oos);
		__skb_queue_tail(&call->rx_oos_queue, skb);
	oos_queued:
		trace_rxrpc_receive(call, last ? rxrpc_receive_oos_last : rxrpc_receive_oos,
				    sp->hdr.serial, sp->hdr.seq);
	}

send_ack:
	if (ack_reason >= 0) {
		if (rxrpc_ack_priority[ack_reason] > rxrpc_ack_priority[*_ack_reason]) {
			*_ack_serial = serial;
			*_ack_reason = ack_reason;
		} else if (rxrpc_ack_priority[ack_reason] == rxrpc_ack_priority[*_ack_reason] &&
			   ack_reason == RXRPC_ACK_REQUESTED) {
			*_ack_serial = serial;
			*_ack_reason = ack_reason;
		}
	}
}

/*
 * Split a jumbo packet and file the bits separately.
 */
static bool rxrpc_input_split_jumbo(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_jumbo_header jhdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb), *jsp;
	struct sk_buff *jskb;
	rxrpc_serial_t ack_serial = 0;
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int len = skb->len - offset;
	bool notify = false;
	int ack_reason = 0, count = 1, stat_ix;

	while (sp->hdr.flags & RXRPC_JUMBO_PACKET) {
		if (len < RXRPC_JUMBO_SUBPKTLEN)
			goto protocol_error;
		if (sp->hdr.flags & RXRPC_LAST_PACKET)
			goto protocol_error;
		if (skb_copy_bits(skb, offset + RXRPC_JUMBO_DATALEN,
				  &jhdr, sizeof(jhdr)) < 0)
			goto protocol_error;

		jskb = skb_clone(skb, GFP_NOFS);
		if (!jskb) {
			kdebug("couldn't clone");
			return false;
		}
		rxrpc_new_skb(jskb, rxrpc_skb_new_jumbo_subpacket);
		jsp = rxrpc_skb(jskb);
		jsp->offset = offset;
		jsp->len = RXRPC_JUMBO_DATALEN;
		rxrpc_input_data_one(call, jskb, &notify, &ack_serial, &ack_reason);
		rxrpc_free_skb(jskb, rxrpc_skb_put_jumbo_subpacket);

		sp->hdr.flags = jhdr.flags;
		sp->hdr._rsvd = ntohs(jhdr._rsvd);
		sp->hdr.seq++;
		sp->hdr.serial++;
		offset += RXRPC_JUMBO_SUBPKTLEN;
		len -= RXRPC_JUMBO_SUBPKTLEN;
		count++;
	}

	sp->offset = offset;
	sp->len    = len;
	rxrpc_input_data_one(call, skb, &notify, &ack_serial, &ack_reason);

	stat_ix = umin(count, ARRAY_SIZE(call->rxnet->stat_rx_jumbo)) - 1;
	atomic_inc(&call->rxnet->stat_rx_jumbo[stat_ix]);

	if (ack_reason > 0) {
		rxrpc_send_ACK(call, ack_reason, ack_serial,
			       rxrpc_propose_ack_input_data);
	} else {
		call->ackr_nr_unacked++;
		rxrpc_propose_delay_ACK(call, sp->hdr.serial,
					rxrpc_propose_ack_input_data);
	}
	if (notify && !test_bit(RXRPC_CALL_CONN_CHALLENGING, &call->flags)) {
		trace_rxrpc_notify_socket(call->debug_id, sp->hdr.serial);
		rxrpc_notify_socket(call);
	}
	return true;

protocol_error:
	return false;
}

/*
 * Process a DATA packet, adding the packet to the Rx ring.  The caller's
 * packet ref must be passed on or discarded.
 */
static void rxrpc_input_data(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	rxrpc_serial_t serial = sp->hdr.serial;
	rxrpc_seq_t seq0 = sp->hdr.seq;

	_enter("{%x,%x,%x},{%u,%x}",
	       call->ackr_window, call->ackr_wtop, call->rx_highest_seq,
	       skb->len, seq0);

	if (__rxrpc_call_is_complete(call))
		return;

	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		/* Received data implicitly ACKs all of the request
		 * packets we sent when we're acting as a client.
		 */
		if (!rxrpc_receiving_reply(call))
			goto out_notify;
		break;

	case RXRPC_CALL_SERVER_RECV_REQUEST: {
		unsigned long timo = READ_ONCE(call->next_req_timo);

		if (timo) {
			ktime_t delay = ms_to_ktime(timo);

			call->expect_req_by = ktime_add(ktime_get_real(), delay);
			trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_idle);
		}
		break;
	}

	default:
		break;
	}

	if (!rxrpc_input_split_jumbo(call, skb)) {
		rxrpc_proto_abort(call, sp->hdr.seq, rxrpc_badmsg_bad_jumbo);
		goto out_notify;
	}
	return;

out_notify:
	trace_rxrpc_notify_socket(call->debug_id, serial);
	rxrpc_notify_socket(call);
	_leave(" [queued]");
}

/*
 * See if there's a cached RTT probe to complete.
 */
static void rxrpc_complete_rtt_probe(struct rxrpc_call *call,
				     ktime_t resp_time,
				     rxrpc_serial_t acked_serial,
				     rxrpc_serial_t ack_serial,
				     enum rxrpc_rtt_rx_trace type)
{
	rxrpc_serial_t orig_serial;
	unsigned long avail;
	ktime_t sent_at;
	bool matched = false;
	int i;

	avail = READ_ONCE(call->rtt_avail);
	smp_rmb(); /* Read avail bits before accessing data. */

	for (i = 0; i < ARRAY_SIZE(call->rtt_serial); i++) {
		if (!test_bit(i + RXRPC_CALL_RTT_PEND_SHIFT, &avail))
			continue;

		sent_at = call->rtt_sent_at[i];
		orig_serial = call->rtt_serial[i];

		if (orig_serial == acked_serial) {
			clear_bit(i + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);
			smp_mb(); /* Read data before setting avail bit */
			set_bit(i, &call->rtt_avail);
			rxrpc_call_add_rtt(call, type, i, acked_serial, ack_serial,
					   sent_at, resp_time);
			matched = true;
		}

		/* If a later serial is being acked, then mark this slot as
		 * being available.
		 */
		if (after(acked_serial, orig_serial)) {
			trace_rxrpc_rtt_rx(call, rxrpc_rtt_rx_obsolete, i,
					   orig_serial, acked_serial, 0, 0, 0);
			clear_bit(i + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);
			smp_wmb();
			set_bit(i, &call->rtt_avail);
		}
	}

	if (!matched)
		trace_rxrpc_rtt_rx(call, rxrpc_rtt_rx_lost, 9, 0, acked_serial, 0, 0, 0);
}

/*
 * Process the extra information that may be appended to an ACK packet
 */
static void rxrpc_input_ack_trailer(struct rxrpc_call *call, struct sk_buff *skb,
				    struct rxrpc_acktrailer *trailer)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_peer *peer = call->peer;
	unsigned int max_data, capacity;
	bool wake = false;
	u32 max_mtu	= ntohl(trailer->maxMTU);
	//u32 if_mtu	= ntohl(trailer->ifMTU);
	u32 rwind	= ntohl(trailer->rwind);
	u32 jumbo_max	= ntohl(trailer->jumbo_max);

	if (rwind > RXRPC_TX_MAX_WINDOW)
		rwind = RXRPC_TX_MAX_WINDOW;
	if (call->tx_winsize != rwind) {
		if (rwind > call->tx_winsize)
			wake = true;
		trace_rxrpc_rx_rwind_change(call, sp->hdr.serial, rwind, wake);
		call->tx_winsize = rwind;
	}

	max_mtu = clamp(max_mtu, 500, 65535);
	peer->ackr_max_data = max_mtu;

	if (max_mtu < peer->max_data) {
		trace_rxrpc_pmtud_reduce(peer, sp->hdr.serial, max_mtu,
					 rxrpc_pmtud_reduce_ack);
		peer->max_data = max_mtu;
	}

	max_data = umin(max_mtu, peer->max_data);
	capacity = max_data;
	capacity += sizeof(struct rxrpc_jumbo_header); /* First subpacket has main hdr, not jumbo */
	capacity /= sizeof(struct rxrpc_jumbo_header) + RXRPC_JUMBO_DATALEN;

	if (jumbo_max == 0) {
		/* The peer says it supports pmtu discovery */
		peer->ackr_adv_pmtud = true;
	} else {
		peer->ackr_adv_pmtud = false;
		capacity = clamp(capacity, 1, jumbo_max);
	}

	call->tx_jumbo_max = capacity;

	if (wake)
		wake_up(&call->waitq);
}

#if defined(CONFIG_X86) && __GNUC__ && !defined(__clang__)
/* Clang doesn't support the %z constraint modifier */
#define shiftr_adv_rotr(shift_from, rotate_into) ({			\
			asm(" shr%z1 %1\n"				\
			    " inc %0\n"					\
			    " rcr%z2 %2\n"				\
			    : "+d"(shift_from), "+m"(*(shift_from)), "+rm"(rotate_into) \
			    );						\
		})
#else
#define shiftr_adv_rotr(shift_from, rotate_into) ({	\
			typeof(rotate_into) __bit0 = *(shift_from) & 1;	\
			*(shift_from) >>= 1;				\
			shift_from++;					\
			rotate_into >>= 1;				\
			rotate_into |= __bit0 << (sizeof(rotate_into) * 8 - 1); \
		})
#endif

/*
 * Deal with RTT samples from soft ACKs.
 */
static void rxrpc_input_soft_rtt(struct rxrpc_call *call,
				 struct rxrpc_ack_summary *summary,
				 struct rxrpc_txqueue *tq)
{
	for (int ix = 0; ix < RXRPC_NR_TXQUEUE; ix++)
		if (summary->acked_serial == tq->segment_serial[ix])
			return rxrpc_add_data_rtt_sample(call, summary, tq, ix);
}

/*
 * Process a batch of soft ACKs specific to a transmission queue segment.
 */
static void rxrpc_input_soft_ack_tq(struct rxrpc_call *call,
				    struct rxrpc_ack_summary *summary,
				    struct rxrpc_txqueue *tq,
				    unsigned long extracted_acks,
				    int nr_reported,
				    rxrpc_seq_t seq,
				    rxrpc_seq_t *lowest_nak)
{
	unsigned long old_reported = 0, flipped, new_acks = 0;
	unsigned long a_to_n, n_to_a = 0;
	int new, a, n;

	if (tq->nr_reported_acks > 0)
		old_reported = ~0UL >> (RXRPC_NR_TXQUEUE - tq->nr_reported_acks);

	_enter("{%x,%lx,%d},%lx,%d,%x",
	       tq->qbase, tq->segment_acked, tq->nr_reported_acks,
	       extracted_acks, nr_reported, seq);

	_debug("[%x]", tq->qbase);
	_debug("tq    %16lx %u", tq->segment_acked, tq->nr_reported_acks);
	_debug("sack  %16lx %u", extracted_acks, nr_reported);

	/* See how many previously logged ACKs/NAKs have flipped. */
	flipped = (tq->segment_acked ^ extracted_acks) & old_reported;
	if (flipped) {
		n_to_a = ~tq->segment_acked & flipped; /* Old NAK -> ACK */
		a_to_n =  tq->segment_acked & flipped; /* Old ACK -> NAK */
		a = hweight_long(n_to_a);
		n = hweight_long(a_to_n);
		_debug("flip  %16lx", flipped);
		_debug("ntoa  %16lx %d", n_to_a, a);
		_debug("aton  %16lx %d", a_to_n, n);
		call->acks_nr_sacks	+= a - n;
		call->acks_nr_snacks	+= n - a;
		summary->nr_new_sacks	+= a;
		summary->nr_new_snacks	+= n;
	}

	/* See how many new ACKs/NAKs have been acquired. */
	new = nr_reported - tq->nr_reported_acks;
	if (new > 0) {
		new_acks = extracted_acks & ~old_reported;
		if (new_acks) {
			a = hweight_long(new_acks);
			n = new - a;
			_debug("new_a %16lx new=%d a=%d n=%d", new_acks, new, a, n);
			call->acks_nr_sacks	+= a;
			call->acks_nr_snacks	+= n;
			summary->nr_new_sacks	+= a;
			summary->nr_new_snacks	+= n;
		} else {
			call->acks_nr_snacks	+= new;
			summary->nr_new_snacks	+= new;
		}
	}

	tq->nr_reported_acks = nr_reported;
	tq->segment_acked = extracted_acks;
	trace_rxrpc_apply_acks(call, tq);

	if (extracted_acks != ~0UL) {
		rxrpc_seq_t lowest = seq + ffz(extracted_acks);

		if (before(lowest, *lowest_nak))
			*lowest_nak = lowest;
	}

	if (summary->acked_serial)
		rxrpc_input_soft_rtt(call, summary, tq);

	new_acks |= n_to_a;
	if (new_acks)
		rxrpc_input_rack(call, summary, tq, new_acks);

	if (call->tlp_serial &&
	    rxrpc_seq_in_txq(tq, call->tlp_seq) &&
	    test_bit(call->tlp_seq - tq->qbase, &new_acks))
		summary->tlp_probe_acked = true;
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
static void rxrpc_input_soft_acks(struct rxrpc_call *call,
				  struct rxrpc_ack_summary *summary,
				  struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_txqueue *tq = call->tx_queue;
	unsigned long extracted = ~0UL;
	unsigned int nr = 0;
	rxrpc_seq_t seq = call->acks_hard_ack + 1;
	rxrpc_seq_t lowest_nak = seq + sp->ack.nr_acks;
	u8 *acks = skb->data + sizeof(struct rxrpc_wire_header) + sizeof(struct rxrpc_ackpacket);

	_enter("%x,%x,%u", tq->qbase, seq, sp->ack.nr_acks);

	while (after(seq, tq->qbase + RXRPC_NR_TXQUEUE - 1))
		tq = tq->next;

	for (unsigned int i = 0; i < sp->ack.nr_acks; i++) {
		/* Decant ACKs until we hit a txqueue boundary. */
		shiftr_adv_rotr(acks, extracted);
		if (i == 256) {
			acks -= i;
			i = 0;
		}
		seq++;
		nr++;
		if ((seq & RXRPC_TXQ_MASK) != 0)
			continue;

		_debug("bound %16lx %u", extracted, nr);

		rxrpc_input_soft_ack_tq(call, summary, tq, extracted, RXRPC_NR_TXQUEUE,
					seq - RXRPC_NR_TXQUEUE, &lowest_nak);
		extracted = ~0UL;
		nr = 0;
		tq = tq->next;
		prefetch(tq);
	}

	if (nr) {
		unsigned int nr_reported = seq & RXRPC_TXQ_MASK;

		extracted >>= RXRPC_NR_TXQUEUE - nr_reported;
		_debug("tail  %16lx %u", extracted, nr_reported);
		rxrpc_input_soft_ack_tq(call, summary, tq, extracted, nr_reported,
					seq & ~RXRPC_TXQ_MASK, &lowest_nak);
	}

	/* We *can* have more nacks than we did - the peer is permitted to drop
	 * packets it has soft-acked and re-request them.  Further, it is
	 * possible for the nack distribution to change whilst the number of
	 * nacks stays the same or goes down.
	 */
	if (lowest_nak != call->acks_lowest_nak) {
		call->acks_lowest_nak = lowest_nak;
		summary->new_low_snack = true;
	}

	_debug("summary A=%d+%d N=%d+%d",
	       call->acks_nr_sacks,  summary->nr_new_sacks,
	       call->acks_nr_snacks, summary->nr_new_snacks);
}

/*
 * Return true if the ACK is valid - ie. it doesn't appear to have regressed
 * with respect to the ack state conveyed by preceding ACKs.
 */
static bool rxrpc_is_ack_valid(struct rxrpc_call *call,
			       rxrpc_seq_t hard_ack, rxrpc_seq_t prev_pkt)
{
	rxrpc_seq_t base = READ_ONCE(call->acks_hard_ack);

	if (after(hard_ack, base))
		return true; /* The window advanced */

	if (before(hard_ack, base))
		return false; /* firstPacket regressed */

	if (after_eq(prev_pkt, call->acks_prev_seq))
		return true; /* previousPacket hasn't regressed. */

	/* Some rx implementations put a serial number in previousPacket. */
	if (after(prev_pkt, base + call->tx_winsize))
		return false;
	return true;
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
static void rxrpc_input_ack(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_ack_summary summary = { 0 };
	struct rxrpc_acktrailer trailer;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	rxrpc_seq_t first_soft_ack, hard_ack, prev_pkt;
	int nr_acks, offset, ioffset;

	_enter("");

	offset = sizeof(struct rxrpc_wire_header) + sizeof(struct rxrpc_ackpacket);

	summary.ack_serial	= sp->hdr.serial;
	first_soft_ack		= sp->ack.first_ack;
	prev_pkt		= sp->ack.prev_ack;
	nr_acks			= sp->ack.nr_acks;
	hard_ack		= first_soft_ack - 1;
	summary.acked_serial	= sp->ack.acked_serial;
	summary.ack_reason	= (sp->ack.reason < RXRPC_ACK__INVALID ?
				   sp->ack.reason : RXRPC_ACK__INVALID);

	trace_rxrpc_rx_ack(call, sp);
	rxrpc_inc_stat(call->rxnet, stat_rx_acks[summary.ack_reason]);
	prefetch(call->tx_queue);

	/* If we get an EXCEEDS_WINDOW ACK from the server, it probably
	 * indicates that the client address changed due to NAT.  The server
	 * lost the call because it switched to a different peer.
	 */
	if (unlikely(summary.ack_reason == RXRPC_ACK_EXCEEDS_WINDOW) &&
	    hard_ack == 0 &&
	    prev_pkt == 0 &&
	    rxrpc_is_client_call(call)) {
		rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
					  0, -ENETRESET);
		goto send_response;
	}

	/* If we get an OUT_OF_SEQUENCE ACK from the server, that can also
	 * indicate a change of address.  However, we can retransmit the call
	 * if we still have it buffered to the beginning.
	 */
	if (unlikely(summary.ack_reason == RXRPC_ACK_OUT_OF_SEQUENCE) &&
	    hard_ack == 0 &&
	    prev_pkt == 0 &&
	    call->tx_bottom == 0 &&
	    rxrpc_is_client_call(call)) {
		rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
					  0, -ENETRESET);
		goto send_response;
	}

	/* Discard any out-of-order or duplicate ACKs (outside lock). */
	if (!rxrpc_is_ack_valid(call, hard_ack, prev_pkt)) {
		trace_rxrpc_rx_discard_ack(call, summary.ack_serial, hard_ack, prev_pkt);
		goto send_response; /* Still respond if requested. */
	}

	trailer.maxMTU = 0;
	ioffset = offset + nr_acks + 3;
	if (skb->len >= ioffset + sizeof(trailer) &&
	    skb_copy_bits(skb, ioffset, &trailer, sizeof(trailer)) < 0)
		return rxrpc_proto_abort(call, 0, rxrpc_badmsg_short_ack_trailer);

	if (nr_acks > 0)
		skb_condense(skb);

	call->acks_latest_ts = ktime_get_real();
	call->acks_hard_ack = hard_ack;
	call->acks_prev_seq = prev_pkt;

	if (summary.acked_serial) {
		switch (summary.ack_reason) {
		case RXRPC_ACK_PING_RESPONSE:
			rxrpc_complete_rtt_probe(call, call->acks_latest_ts,
						 summary.acked_serial, summary.ack_serial,
						 rxrpc_rtt_rx_ping_response);
			break;
		default:
			if (after(summary.acked_serial, call->acks_highest_serial))
				call->acks_highest_serial = summary.acked_serial;
			summary.rtt_sample_avail = true;
			break;
		}
	}

	/* Parse rwind and mtu sizes if provided. */
	if (trailer.maxMTU)
		rxrpc_input_ack_trailer(call, skb, &trailer);

	if (hard_ack + 1 == 0)
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_zero);

	/* Ignore ACKs unless we are or have just been transmitting. */
	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
	case RXRPC_CALL_SERVER_SEND_REPLY:
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		break;
	default:
		goto send_response;
	}

	if (before(hard_ack, call->tx_bottom) ||
	    after(hard_ack, call->tx_top))
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_outside_window);
	if (nr_acks > call->tx_top - hard_ack)
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_sack_overflow);

	if (after(hard_ack, call->tx_bottom)) {
		if (rxrpc_rotate_tx_window(call, hard_ack, &summary)) {
			rxrpc_end_tx_phase(call, false, rxrpc_eproto_unexpected_ack);
			goto send_response;
		}
	}

	if (nr_acks > 0) {
		if (offset > (int)skb->len - nr_acks)
			return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_short_sack);
		rxrpc_input_soft_acks(call, &summary, skb);
	}

	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags) &&
	    call->acks_nr_sacks == call->tx_top - hard_ack &&
	    rxrpc_is_client_call(call))
		rxrpc_propose_ping(call, summary.ack_serial,
				   rxrpc_propose_ack_ping_for_lost_reply);

	/* Drive the congestion management algorithm first and then RACK-TLP as
	 * the latter depends on the state/change in state in the former.
	 */
	rxrpc_congestion_management(call, &summary);
	rxrpc_rack_detect_loss_and_arm_timer(call, &summary);
	rxrpc_tlp_process_ack(call, &summary);
	if (call->tlp_serial && after_eq(summary.acked_serial, call->tlp_serial))
		call->tlp_serial = 0;

send_response:
	if (summary.ack_reason == RXRPC_ACK_PING)
		rxrpc_send_ACK(call, RXRPC_ACK_PING_RESPONSE, summary.ack_serial,
			       rxrpc_propose_ack_respond_to_ping);
	else if (sp->hdr.flags & RXRPC_REQUEST_ACK)
		rxrpc_send_ACK(call, RXRPC_ACK_REQUESTED, summary.ack_serial,
			       rxrpc_propose_ack_respond_to_ack);
}

/*
 * Process an ACKALL packet.
 */
static void rxrpc_input_ackall(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_ack_summary summary = { 0 };

	if (rxrpc_rotate_tx_window(call, call->tx_top, &summary))
		rxrpc_end_tx_phase(call, false, rxrpc_eproto_unexpected_ackall);
}

/*
 * Process an ABORT packet directed at a call.
 */
static void rxrpc_input_abort(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_rx_abort(call, sp->hdr.serial, skb->priority);

	rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
				  skb->priority, -ECONNABORTED);
}

/*
 * Process an incoming call packet.
 */
void rxrpc_input_call_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	unsigned long timo;

	_enter("%p,%p", call, skb);

	if (sp->hdr.serviceId != call->dest_srx.srx_service)
		call->dest_srx.srx_service = sp->hdr.serviceId;
	if ((int)sp->hdr.serial - (int)call->rx_serial > 0)
		call->rx_serial = sp->hdr.serial;
	if (!test_bit(RXRPC_CALL_RX_HEARD, &call->flags))
		set_bit(RXRPC_CALL_RX_HEARD, &call->flags);

	timo = READ_ONCE(call->next_rx_timo);
	if (timo) {
		ktime_t delay = ms_to_ktime(timo);

		call->expect_rx_by = ktime_add(ktime_get_real(), delay);
		trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_expect_rx);
	}

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_DATA:
		return rxrpc_input_data(call, skb);

	case RXRPC_PACKET_TYPE_ACK:
		return rxrpc_input_ack(call, skb);

	case RXRPC_PACKET_TYPE_BUSY:
		/* Just ignore BUSY packets from the server; the retry and
		 * lifespan timers will take care of business.  BUSY packets
		 * from the client don't make sense.
		 */
		return;

	case RXRPC_PACKET_TYPE_ABORT:
		return rxrpc_input_abort(call, skb);

	case RXRPC_PACKET_TYPE_ACKALL:
		return rxrpc_input_ackall(call, skb);

	default:
		break;
	}
}

/*
 * Handle a new service call on a channel implicitly completing the preceding
 * call on that channel.  This does not apply to client conns.
 *
 * TODO: If callNumber > call_id + 1, renegotiate security.
 */
void rxrpc_implicit_end_call(struct rxrpc_call *call, struct sk_buff *skb)
{
	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		rxrpc_call_completed(call);
		fallthrough;
	case RXRPC_CALL_COMPLETE:
		break;
	default:
		rxrpc_abort_call(call, 0, RX_CALL_DEAD, -ESHUTDOWN,
				 rxrpc_eproto_improper_term);
		trace_rxrpc_improper_term(call);
		break;
	}

	rxrpc_input_call_event(call);
}
