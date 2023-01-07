// SPDX-License-Identifier: GPL-2.0-or-later
/* Processing of received RxRPC packets
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ar-internal.h"

static void rxrpc_proto_abort(struct rxrpc_call *call, rxrpc_seq_t seq,
			      enum rxrpc_abort_reason why)
{
	rxrpc_abort_call(call, seq, RX_PROTOCOL_ERROR, -EBADMSG, why);
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
		(call->tx_top - call->acks_hard_ack) - summary->nr_acks;

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
		if (summary->saw_nacks)
			goto packet_loss_detected;
		if (summary->cumulative_acks > 0)
			cwnd += 1;
		if (cwnd >= call->cong_ssthresh) {
			call->cong_mode = RXRPC_CALL_CONGEST_AVOIDANCE;
			call->cong_tstamp = skb->tstamp;
		}
		goto out;

	case RXRPC_CALL_CONGEST_AVOIDANCE:
		if (summary->saw_nacks)
			goto packet_loss_detected;

		/* We analyse the number of packets that get ACK'd per RTT
		 * period and increase the window if we managed to fill it.
		 */
		if (call->peer->rtt_count == 0)
			goto out;
		if (ktime_before(skb->tstamp,
				 ktime_add_us(call->cong_tstamp,
					      call->peer->srtt_us >> 3)))
			goto out_no_clear_ca;
		change = rxrpc_cong_rtt_window_end;
		call->cong_tstamp = skb->tstamp;
		if (cumulative_acks >= cwnd)
			cwnd++;
		goto out;

	case RXRPC_CALL_PACKET_LOSS:
		if (!summary->saw_nacks)
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
			if (!summary->saw_nacks)
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
	if (cwnd >= RXRPC_TX_MAX_WINDOW)
		cwnd = RXRPC_TX_MAX_WINDOW;
	call->cong_cwnd = cwnd;
	call->cong_cumul_acks = cumulative_acks;
	trace_rxrpc_congest(call, summary, acked_serial, change);
	if (resend)
		rxrpc_resend(call, skb);
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
	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags) ||
	    summary->nr_acks != call->tx_top - call->acks_hard_ack) {
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
	ktime_t rtt, now;

	if (call->cong_mode != RXRPC_CALL_SLOW_START &&
	    call->cong_mode != RXRPC_CALL_CONGEST_AVOIDANCE)
		return;
	if (__rxrpc_call_state(call) == RXRPC_CALL_CLIENT_AWAIT_REPLY)
		return;

	rtt = ns_to_ktime(call->peer->srtt_us * (1000 / 8));
	now = ktime_get_real();
	if (!ktime_before(ktime_add(call->tx_last_sent, rtt), now))
		return;

	trace_rxrpc_reset_cwnd(call, now);
	rxrpc_inc_stat(call->rxnet, stat_tx_data_cwnd_reset);
	call->tx_last_sent = now;
	call->cong_mode = RXRPC_CALL_SLOW_START;
	call->cong_ssthresh = max_t(unsigned int, call->cong_ssthresh,
				    call->cong_cwnd * 3 / 4);
	call->cong_cwnd = max_t(unsigned int, call->cong_cwnd / 2, RXRPC_MIN_CWND);
}

/*
 * Apply a hard ACK by advancing the Tx window.
 */
static bool rxrpc_rotate_tx_window(struct rxrpc_call *call, rxrpc_seq_t to,
				   struct rxrpc_ack_summary *summary)
{
	struct rxrpc_txbuf *txb;
	bool rot_last = false;

	list_for_each_entry_rcu(txb, &call->tx_buffer, call_link, false) {
		if (before_eq(txb->seq, call->acks_hard_ack))
			continue;
		summary->nr_rot_new_acks++;
		if (test_bit(RXRPC_TXBUF_LAST, &txb->flags)) {
			set_bit(RXRPC_CALL_TX_LAST, &call->flags);
			rot_last = true;
		}
		if (txb->seq == to)
			break;
	}

	if (rot_last)
		set_bit(RXRPC_CALL_TX_ALL_ACKED, &call->flags);

	_enter("%x,%x,%x,%d", to, call->acks_hard_ack, call->tx_top, rot_last);

	if (call->acks_lowest_nak == call->acks_hard_ack) {
		call->acks_lowest_nak = to;
	} else if (after(to, call->acks_lowest_nak)) {
		summary->new_low_nack = true;
		call->acks_lowest_nak = to;
	}

	smp_store_release(&call->acks_hard_ack, to);

	trace_rxrpc_txqueue(call, (rot_last ?
				   rxrpc_txqueue_rotate_last :
				   rxrpc_txqueue_rotate));
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
	unsigned long now, timo;
	rxrpc_seq_t top = READ_ONCE(call->tx_top);

	if (call->ackr_reason) {
		now = jiffies;
		timo = now + MAX_JIFFY_OFFSET;

		WRITE_ONCE(call->delay_ack_at, timo);
		trace_rxrpc_timer(call, rxrpc_timer_init_for_reply, now);
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
		call->expect_req_by = jiffies + MAX_JIFFY_OFFSET;
		rxrpc_propose_delay_ACK(call, serial, rxrpc_propose_ack_processing_op);
		break;

	default:
		break;
	}
}

static void rxrpc_input_update_ack_window(struct rxrpc_call *call,
					  rxrpc_seq_t window, rxrpc_seq_t wtop)
{
	atomic64_set_release(&call->ackr_window, ((u64)wtop) << 32 | window);
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

	__skb_queue_tail(&call->recvmsg_queue, skb);
	rxrpc_input_update_ack_window(call, window, wtop);
	trace_rxrpc_receive(call, last ? why + 1 : why, sp->hdr.serial, sp->hdr.seq);
	if (last)
		rxrpc_end_rx_phase(call, sp->hdr.serial);
}

/*
 * Process a DATA packet.
 */
static void rxrpc_input_data_one(struct rxrpc_call *call, struct sk_buff *skb,
				 bool *_notify)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct sk_buff *oos;
	rxrpc_serial_t serial = sp->hdr.serial;
	u64 win = atomic64_read(&call->ackr_window);
	rxrpc_seq_t window = lower_32_bits(win);
	rxrpc_seq_t wtop = upper_32_bits(win);
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
		rxrpc_seq_t reset_from;
		bool reset_sack = false;

		if (sp->hdr.flags & RXRPC_REQUEST_ACK)
			ack_reason = RXRPC_ACK_REQUESTED;
		/* Send an immediate ACK if we fill in a hole */
		else if (!skb_queue_empty(&call->rx_oos_queue))
			ack_reason = RXRPC_ACK_DELAY;
		else
			atomic_inc_return(&call->ackr_nr_unacked);

		window++;
		if (after(window, wtop))
			wtop = window;

		rxrpc_get_skb(skb, rxrpc_skb_get_to_recvmsg);

		spin_lock(&call->recvmsg_queue.lock);
		rxrpc_input_queue_data(call, skb, window, wtop, rxrpc_receive_queue);
		*_notify = true;

		while ((oos = skb_peek(&call->rx_oos_queue))) {
			struct rxrpc_skb_priv *osp = rxrpc_skb(oos);

			if (after(osp->hdr.seq, window))
				break;

			__skb_unlink(oos, &call->rx_oos_queue);
			last = osp->hdr.flags & RXRPC_LAST_PACKET;
			seq = osp->hdr.seq;
			if (!reset_sack) {
				reset_from = seq;
				reset_sack = true;
			}

			window++;
			rxrpc_input_queue_data(call, oos, window, wtop,
						 rxrpc_receive_queue_oos);
		}

		spin_unlock(&call->recvmsg_queue.lock);

		if (reset_sack) {
			do {
				call->ackr_sack_table[reset_from % RXRPC_SACK_SIZE] = 0;
			} while (reset_from++, before(reset_from, window));
		}
	} else {
		bool keep = false;

		ack_reason = RXRPC_ACK_OUT_OF_SEQUENCE;

		if (!call->ackr_sack_table[seq % RXRPC_SACK_SIZE]) {
			call->ackr_sack_table[seq % RXRPC_SACK_SIZE] = 1;
			keep = 1;
		}

		if (after(seq + 1, wtop)) {
			wtop = seq + 1;
			rxrpc_input_update_ack_window(call, window, wtop);
		}

		if (!keep) {
			ack_reason = RXRPC_ACK_DUPLICATE;
			goto send_ack;
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
	if (ack_reason >= 0)
		rxrpc_send_ACK(call, ack_reason, serial,
			       rxrpc_propose_ack_input_data);
	else
		rxrpc_propose_delay_ACK(call, serial,
					rxrpc_propose_ack_input_data);
}

/*
 * Split a jumbo packet and file the bits separately.
 */
static bool rxrpc_input_split_jumbo(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_jumbo_header jhdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb), *jsp;
	struct sk_buff *jskb;
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int len = skb->len - offset;
	bool notify = false;

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
		rxrpc_input_data_one(call, jskb, &notify);
		rxrpc_free_skb(jskb, rxrpc_skb_put_jumbo_subpacket);

		sp->hdr.flags = jhdr.flags;
		sp->hdr._rsvd = ntohs(jhdr._rsvd);
		sp->hdr.seq++;
		sp->hdr.serial++;
		offset += RXRPC_JUMBO_SUBPKTLEN;
		len -= RXRPC_JUMBO_SUBPKTLEN;
	}

	sp->offset = offset;
	sp->len    = len;
	rxrpc_input_data_one(call, skb, &notify);
	if (notify) {
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

	_enter("{%llx,%x},{%u,%x}",
	       atomic64_read(&call->ackr_window), call->rx_highest_seq,
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
		unsigned long now, expect_req_by;

		if (timo) {
			now = jiffies;
			expect_req_by = now + timo;
			WRITE_ONCE(call->expect_req_by, expect_req_by);
			rxrpc_reduce_call_timer(call, expect_req_by, now,
						rxrpc_timer_set_for_idle);
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
	skb = NULL;

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
			if (type != rxrpc_rtt_rx_cancel)
				rxrpc_peer_add_rtt(call, type, i, acked_serial, ack_serial,
						   sent_at, resp_time);
			else
				trace_rxrpc_rtt_rx(call, rxrpc_rtt_rx_cancel, i,
						   orig_serial, acked_serial, 0, 0);
			matched = true;
		}

		/* If a later serial is being acked, then mark this slot as
		 * being available.
		 */
		if (after(acked_serial, orig_serial)) {
			trace_rxrpc_rtt_rx(call, rxrpc_rtt_rx_obsolete, i,
					   orig_serial, acked_serial, 0, 0);
			clear_bit(i + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);
			smp_wmb();
			set_bit(i, &call->rtt_avail);
		}
	}

	if (!matched)
		trace_rxrpc_rtt_rx(call, rxrpc_rtt_rx_lost, 9, 0, acked_serial, 0, 0);
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

	if (rwind > RXRPC_TX_MAX_WINDOW)
		rwind = RXRPC_TX_MAX_WINDOW;
	if (call->tx_winsize != rwind) {
		if (rwind > call->tx_winsize)
			wake = true;
		trace_rxrpc_rx_rwind_change(call, sp->hdr.serial, rwind, wake);
		call->tx_winsize = rwind;
	}

	if (call->cong_ssthresh > rwind)
		call->cong_ssthresh = rwind;

	mtu = min(ntohl(ackinfo->rxMTU), ntohl(ackinfo->maxMTU));

	peer = call->peer;
	if (mtu < peer->maxdata) {
		spin_lock(&peer->lock);
		peer->maxdata = mtu;
		peer->mtu = mtu + peer->hdrsize;
		spin_unlock(&peer->lock);
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
	unsigned int i;

	for (i = 0; i < nr_acks; i++) {
		if (acks[i] == RXRPC_ACK_TYPE_ACK) {
			summary->nr_acks++;
			summary->nr_new_acks++;
		} else {
			if (!summary->saw_nacks &&
			    call->acks_lowest_nak != seq + i) {
				call->acks_lowest_nak = seq + i;
				summary->new_low_nack = true;
			}
			summary->saw_nacks = true;
		}
	}
}

/*
 * Return true if the ACK is valid - ie. it doesn't appear to have regressed
 * with respect to the ack state conveyed by preceding ACKs.
 */
static bool rxrpc_is_ack_valid(struct rxrpc_call *call,
			       rxrpc_seq_t first_pkt, rxrpc_seq_t prev_pkt)
{
	rxrpc_seq_t base = READ_ONCE(call->acks_first_seq);

	if (after(first_pkt, base))
		return true; /* The window advanced */

	if (before(first_pkt, base))
		return false; /* firstPacket regressed */

	if (after_eq(prev_pkt, call->acks_prev_seq))
		return true; /* previousPacket hasn't regressed. */

	/* Some rx implementations put a serial number in previousPacket. */
	if (after_eq(prev_pkt, base + call->tx_winsize))
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
	struct rxrpc_ackpacket ack;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_ackinfo info;
	rxrpc_serial_t ack_serial, acked_serial;
	rxrpc_seq_t first_soft_ack, hard_ack, prev_pkt;
	int nr_acks, offset, ioffset;

	_enter("");

	offset = sizeof(struct rxrpc_wire_header);
	if (skb_copy_bits(skb, offset, &ack, sizeof(ack)) < 0)
		return rxrpc_proto_abort(call, 0, rxrpc_badmsg_short_ack);
	offset += sizeof(ack);

	ack_serial = sp->hdr.serial;
	acked_serial = ntohl(ack.serial);
	first_soft_ack = ntohl(ack.firstPacket);
	prev_pkt = ntohl(ack.previousPacket);
	hard_ack = first_soft_ack - 1;
	nr_acks = ack.nAcks;
	summary.ack_reason = (ack.reason < RXRPC_ACK__INVALID ?
			      ack.reason : RXRPC_ACK__INVALID);

	trace_rxrpc_rx_ack(call, ack_serial, acked_serial,
			   first_soft_ack, prev_pkt,
			   summary.ack_reason, nr_acks);
	rxrpc_inc_stat(call->rxnet, stat_rx_acks[ack.reason]);

	switch (ack.reason) {
	case RXRPC_ACK_PING_RESPONSE:
		rxrpc_complete_rtt_probe(call, skb->tstamp, acked_serial, ack_serial,
					 rxrpc_rtt_rx_ping_response);
		break;
	case RXRPC_ACK_REQUESTED:
		rxrpc_complete_rtt_probe(call, skb->tstamp, acked_serial, ack_serial,
					 rxrpc_rtt_rx_requested_ack);
		break;
	default:
		if (acked_serial != 0)
			rxrpc_complete_rtt_probe(call, skb->tstamp, acked_serial, ack_serial,
						 rxrpc_rtt_rx_cancel);
		break;
	}

	if (ack.reason == RXRPC_ACK_PING) {
		rxrpc_send_ACK(call, RXRPC_ACK_PING_RESPONSE, ack_serial,
			       rxrpc_propose_ack_respond_to_ping);
	} else if (sp->hdr.flags & RXRPC_REQUEST_ACK) {
		rxrpc_send_ACK(call, RXRPC_ACK_REQUESTED, ack_serial,
			       rxrpc_propose_ack_respond_to_ack);
	}

	/* If we get an EXCEEDS_WINDOW ACK from the server, it probably
	 * indicates that the client address changed due to NAT.  The server
	 * lost the call because it switched to a different peer.
	 */
	if (unlikely(ack.reason == RXRPC_ACK_EXCEEDS_WINDOW) &&
	    first_soft_ack == 1 &&
	    prev_pkt == 0 &&
	    rxrpc_is_client_call(call)) {
		rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
					  0, -ENETRESET);
		return;
	}

	/* If we get an OUT_OF_SEQUENCE ACK from the server, that can also
	 * indicate a change of address.  However, we can retransmit the call
	 * if we still have it buffered to the beginning.
	 */
	if (unlikely(ack.reason == RXRPC_ACK_OUT_OF_SEQUENCE) &&
	    first_soft_ack == 1 &&
	    prev_pkt == 0 &&
	    call->acks_hard_ack == 0 &&
	    rxrpc_is_client_call(call)) {
		rxrpc_set_call_completion(call, RXRPC_CALL_REMOTELY_ABORTED,
					  0, -ENETRESET);
		return;
	}

	/* Discard any out-of-order or duplicate ACKs (outside lock). */
	if (!rxrpc_is_ack_valid(call, first_soft_ack, prev_pkt)) {
		trace_rxrpc_rx_discard_ack(call->debug_id, ack_serial,
					   first_soft_ack, call->acks_first_seq,
					   prev_pkt, call->acks_prev_seq);
		return;
	}

	info.rxMTU = 0;
	ioffset = offset + nr_acks + 3;
	if (skb->len >= ioffset + sizeof(info) &&
	    skb_copy_bits(skb, ioffset, &info, sizeof(info)) < 0)
		return rxrpc_proto_abort(call, 0, rxrpc_badmsg_short_ack_info);

	if (nr_acks > 0)
		skb_condense(skb);

	call->acks_latest_ts = skb->tstamp;
	call->acks_first_seq = first_soft_ack;
	call->acks_prev_seq = prev_pkt;

	switch (ack.reason) {
	case RXRPC_ACK_PING:
		break;
	default:
		if (after(acked_serial, call->acks_highest_serial))
			call->acks_highest_serial = acked_serial;
		break;
	}

	/* Parse rwind and mtu sizes if provided. */
	if (info.rxMTU)
		rxrpc_input_ackinfo(call, skb, &info);

	if (first_soft_ack == 0)
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_zero);

	/* Ignore ACKs unless we are or have just been transmitting. */
	switch (__rxrpc_call_state(call)) {
	case RXRPC_CALL_CLIENT_SEND_REQUEST:
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
	case RXRPC_CALL_SERVER_SEND_REPLY:
	case RXRPC_CALL_SERVER_AWAIT_ACK:
		break;
	default:
		return;
	}

	if (before(hard_ack, call->acks_hard_ack) ||
	    after(hard_ack, call->tx_top))
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_outside_window);
	if (nr_acks > call->tx_top - hard_ack)
		return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_sack_overflow);

	if (after(hard_ack, call->acks_hard_ack)) {
		if (rxrpc_rotate_tx_window(call, hard_ack, &summary)) {
			rxrpc_end_tx_phase(call, false, rxrpc_eproto_unexpected_ack);
			return;
		}
	}

	if (nr_acks > 0) {
		if (offset > (int)skb->len - nr_acks)
			return rxrpc_proto_abort(call, 0, rxrpc_eproto_ackr_short_sack);
		rxrpc_input_soft_acks(call, skb->data + offset, first_soft_ack,
				      nr_acks, &summary);
	}

	if (test_bit(RXRPC_CALL_TX_LAST, &call->flags) &&
	    summary.nr_acks == call->tx_top - hard_ack &&
	    rxrpc_is_client_call(call))
		rxrpc_propose_ping(call, ack_serial,
				   rxrpc_propose_ack_ping_for_lost_reply);

	rxrpc_congestion_management(call, skb, &summary, acked_serial);
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
		unsigned long now = jiffies, expect_rx_by;

		expect_rx_by = now + timo;
		WRITE_ONCE(call->expect_rx_by, expect_rx_by);
		rxrpc_reduce_call_timer(call, expect_rx_by, now,
					rxrpc_timer_set_for_normal);
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

	rxrpc_input_call_event(call, skb);
}
