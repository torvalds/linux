// SPDX-License-Identifier: GPL-2.0-or-later
/* RACK-TLP [RFC8958] Implementation
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ar-internal.h"

static bool rxrpc_rack_sent_after(ktime_t t1, rxrpc_seq_t seq1,
				  ktime_t t2, rxrpc_seq_t seq2)
{
	if (ktime_after(t1, t2))
		return true;
	return t1 == t2 && after(seq1, seq2);
}

/*
 * Mark a packet lost.
 */
static void rxrpc_rack_mark_lost(struct rxrpc_call *call,
				 struct rxrpc_txqueue *tq, unsigned int ix)
{
	if (__test_and_set_bit(ix, &tq->segment_lost)) {
		if (__test_and_clear_bit(ix, &tq->segment_retransmitted))
			call->tx_nr_resent--;
	} else {
		call->tx_nr_lost++;
	}
	tq->segment_xmit_ts[ix] = UINT_MAX;
}

/*
 * Get the transmission time of a packet in the Tx queue.
 */
static ktime_t rxrpc_get_xmit_ts(const struct rxrpc_txqueue *tq, unsigned int ix)
{
	if (tq->segment_xmit_ts[ix] == UINT_MAX)
		return KTIME_MAX;
	return ktime_add_us(tq->xmit_ts_base, tq->segment_xmit_ts[ix]);
}

/*
 * Get a bitmask of nack bits for a queue segment and mask off any that aren't
 * yet reported.
 */
static unsigned long rxrpc_tq_nacks(const struct rxrpc_txqueue *tq)
{
	unsigned long nacks = ~tq->segment_acked;

	if (tq->nr_reported_acks < RXRPC_NR_TXQUEUE)
		nacks &= (1UL << tq->nr_reported_acks) - 1;
	return nacks;
}

/*
 * Update the RACK state for the most recently sent packet that has been
 * delivered [RFC8958 6.2 Step 2].
 */
static void rxrpc_rack_update(struct rxrpc_call *call,
			      struct rxrpc_ack_summary *summary,
			      struct rxrpc_txqueue *tq,
			      unsigned int ix)
{
	rxrpc_seq_t seq = tq->qbase + ix;
	ktime_t xmit_ts = rxrpc_get_xmit_ts(tq, ix);
	ktime_t rtt = ktime_sub(call->acks_latest_ts, xmit_ts);

	if (__test_and_clear_bit(ix, &tq->segment_lost))
		call->tx_nr_lost--;

	if (test_bit(ix, &tq->segment_retransmitted)) {
		/* Use Rx.serial instead of TCP.ACK.ts_option.echo_reply. */
		if (before(call->acks_highest_serial, tq->segment_serial[ix]))
			return;
		if (rtt < minmax_get(&call->min_rtt))
			return;
	}

	/* The RACK algorithm requires the segment ACKs to be traversed in
	 * order of segment transmission - but the only thing this seems to
	 * matter for is that RACK.rtt is set to the rtt of the most recently
	 * transmitted segment.  We should be able to achieve the same by only
	 * setting RACK.rtt if the xmit time is greater.
	 */
	if (ktime_after(xmit_ts, call->rack_rtt_ts)) {
		call->rack_rtt	  = rtt;
		call->rack_rtt_ts = xmit_ts;
	}

	if (rxrpc_rack_sent_after(xmit_ts, seq, call->rack_xmit_ts, call->rack_end_seq)) {
		call->rack_rtt = rtt;
		call->rack_xmit_ts = xmit_ts;
		call->rack_end_seq = seq;
	}
}

/*
 * Detect data segment reordering [RFC8958 6.2 Step 3].
 */
static void rxrpc_rack_detect_reordering(struct rxrpc_call *call,
					 struct rxrpc_ack_summary *summary,
					 struct rxrpc_txqueue *tq,
					 unsigned int ix)
{
	rxrpc_seq_t seq = tq->qbase + ix;

	/* Track the highest sequence number so far ACK'd.  This is not
	 * necessarily the same as ack.firstPacket + ack.nAcks - 1 as the peer
	 * could put a NACK in the last SACK slot.
	 */
	if (after(seq, call->rack_fack))
		call->rack_fack = seq;
	else if (before(seq, call->rack_fack) &&
		 test_bit(ix, &tq->segment_retransmitted))
		call->rack_reordering_seen = true;
}

void rxrpc_input_rack_one(struct rxrpc_call *call,
			  struct rxrpc_ack_summary *summary,
			  struct rxrpc_txqueue *tq,
			  unsigned int ix)
{
	rxrpc_rack_update(call, summary, tq, ix);
	rxrpc_rack_detect_reordering(call, summary, tq, ix);
}

void rxrpc_input_rack(struct rxrpc_call *call,
		      struct rxrpc_ack_summary *summary,
		      struct rxrpc_txqueue *tq,
		      unsigned long new_acks)
{
	while (new_acks) {
		unsigned int ix = __ffs(new_acks);

		__clear_bit(ix, &new_acks);
		rxrpc_input_rack_one(call, summary, tq, ix);
	}

	trace_rxrpc_rack_update(call, summary);
}

/*
 * Update the reordering window [RFC8958 6.2 Step 4].  Returns the updated
 * duration of the reordering window.
 *
 * Note that the Rx protocol doesn't have a 'DSACK option' per se, but ACKs can
 * be given a 'DUPLICATE' reason with the serial number referring to the
 * duplicated DATA packet.  Rx does not inform as to whether this was a
 * reception of the same packet twice or of a retransmission of a packet we
 * already received (though this could be determined by the transmitter based
 * on the serial number).
 */
static ktime_t rxrpc_rack_update_reo_wnd(struct rxrpc_call *call,
					 struct rxrpc_ack_summary *summary)
{
	rxrpc_seq_t snd_una = call->acks_lowest_nak; /* Lowest unack'd seq */
	rxrpc_seq_t snd_nxt = call->tx_transmitted + 1; /* Next seq to be sent */
	bool have_dsack_option = summary->ack_reason == RXRPC_ACK_DUPLICATE;
	int dup_thresh = 3;

	/* DSACK-based reordering window adaptation */
	if (!call->rack_dsack_round_none &&
	    after_eq(snd_una, call->rack_dsack_round))
		call->rack_dsack_round_none = true;

	/* Grow the reordering window per round that sees DSACK.  Reset the
	 * window after 16 DSACK-free recoveries.
	 */
	if (call->rack_dsack_round_none && have_dsack_option) {
		call->rack_dsack_round_none = false;
		call->rack_dsack_round = snd_nxt;
		call->rack_reo_wnd_mult++;
		call->rack_reo_wnd_persist = 16;
	} else if (summary->exiting_fast_or_rto_recovery) {
		call->rack_reo_wnd_persist--;
		if (call->rack_reo_wnd_persist <= 0)
			call->rack_reo_wnd_mult = 1;
	}

	if (!call->rack_reordering_seen) {
		if (summary->in_fast_or_rto_recovery)
			return 0;
		if (call->acks_nr_sacks >= dup_thresh)
			return 0;
	}

	return us_to_ktime(umin(call->rack_reo_wnd_mult * minmax_get(&call->min_rtt) / 4,
				call->srtt_us >> 3));
}

/*
 * Detect losses [RFC8958 6.2 Step 5].
 */
static ktime_t rxrpc_rack_detect_loss(struct rxrpc_call *call,
				      struct rxrpc_ack_summary *summary)
{
	struct rxrpc_txqueue *tq;
	ktime_t timeout = 0, lost_after, now = ktime_get_real();

	call->rack_reo_wnd = rxrpc_rack_update_reo_wnd(call, summary);
	lost_after = ktime_add(call->rack_rtt, call->rack_reo_wnd);
	trace_rxrpc_rack_scan_loss(call);

	for (tq = call->tx_queue; tq; tq = tq->next) {
		unsigned long nacks = rxrpc_tq_nacks(tq);

		if (after(tq->qbase, call->tx_transmitted))
			break;
		trace_rxrpc_rack_scan_loss_tq(call, tq, nacks);

		/* Skip ones marked lost but not yet retransmitted */
		nacks &= ~tq->segment_lost | tq->segment_retransmitted;

		while (nacks) {
			unsigned int ix = __ffs(nacks);
			rxrpc_seq_t seq = tq->qbase + ix;
			ktime_t remaining;
			ktime_t xmit_ts = rxrpc_get_xmit_ts(tq, ix);

			__clear_bit(ix, &nacks);

			if (rxrpc_rack_sent_after(call->rack_xmit_ts, call->rack_end_seq,
						  xmit_ts, seq)) {
				remaining = ktime_sub(ktime_add(xmit_ts, lost_after), now);
				if (remaining <= 0) {
					rxrpc_rack_mark_lost(call, tq, ix);
					trace_rxrpc_rack_detect_loss(call, summary, seq);
				} else {
					timeout = max(remaining, timeout);
				}
			}
		}
	}

	return timeout;
}

/*
 * Detect losses and set a timer to retry the detection [RFC8958 6.2 Step 5].
 */
void rxrpc_rack_detect_loss_and_arm_timer(struct rxrpc_call *call,
					  struct rxrpc_ack_summary *summary)
{
	ktime_t timeout = rxrpc_rack_detect_loss(call, summary);

	if (timeout) {
		call->rack_timer_mode = RXRPC_CALL_RACKTIMER_RACK_REORDER;
		call->rack_timo_at = ktime_add(ktime_get_real(), timeout);
		trace_rxrpc_rack_timer(call, timeout, false);
		trace_rxrpc_timer_set(call, timeout, rxrpc_timer_trace_rack_reo);
	}
}

/*
 * Handle RACK-TLP RTO expiration [RFC8958 6.3].
 */
static void rxrpc_rack_mark_losses_on_rto(struct rxrpc_call *call)
{
	struct rxrpc_txqueue *tq;
	rxrpc_seq_t snd_una = call->acks_lowest_nak; /* Lowest unack'd seq */
	ktime_t lost_after = ktime_add(call->rack_rtt, call->rack_reo_wnd);
	ktime_t deadline = ktime_sub(ktime_get_real(), lost_after);

	for (tq = call->tx_queue; tq; tq = tq->next) {
		unsigned long unacked = ~tq->segment_acked;

		trace_rxrpc_rack_mark_loss_tq(call, tq);
		while (unacked) {
			unsigned int ix = __ffs(unacked);
			rxrpc_seq_t seq = tq->qbase + ix;
			ktime_t xmit_ts = rxrpc_get_xmit_ts(tq, ix);

			if (after(seq, call->tx_transmitted))
				return;
			__clear_bit(ix, &unacked);

			if (seq == snd_una ||
			    ktime_before(xmit_ts, deadline))
				rxrpc_rack_mark_lost(call, tq, ix);
		}
	}
}

/*
 * Calculate the TLP loss probe timeout (PTO) [RFC8958 7.2].
 */
ktime_t rxrpc_tlp_calc_pto(struct rxrpc_call *call, ktime_t now)
{
	unsigned int flight_size = rxrpc_tx_in_flight(call);
	ktime_t rto_at = ktime_add(call->tx_last_sent,
				   rxrpc_get_rto_backoff(call, false));
	ktime_t pto;

	if (call->rtt_count > 0) {
		/* Use 2*SRTT as the timeout. */
		pto = ns_to_ktime(call->srtt_us * NSEC_PER_USEC / 4);
		if (flight_size)
			pto = ktime_add(pto, call->tlp_max_ack_delay);
	} else {
		pto = NSEC_PER_SEC;
	}

	if (ktime_after(ktime_add(now, pto), rto_at))
		pto = ktime_sub(rto_at, now);
	return pto;
}

/*
 * Send a TLP loss probe on PTO expiration [RFC8958 7.3].
 */
void rxrpc_tlp_send_probe(struct rxrpc_call *call)
{
	unsigned int in_flight = rxrpc_tx_in_flight(call);

	if (after_eq(call->acks_hard_ack, call->tx_transmitted))
		return; /* Everything we transmitted has been acked. */

	/* There must be no other loss probe still in flight and we need to
	 * have taken a new RTT sample since last probe or the start of
	 * connection.
	 */
	if (!call->tlp_serial &&
	    call->tlp_rtt_taken != call->rtt_taken) {
		call->tlp_is_retrans = false;
		if (after(call->send_top, call->tx_transmitted) &&
		    rxrpc_tx_window_space(call) > 0) {
			/* Transmit the lowest-sequence unsent DATA */
			call->tx_last_serial = 0;
			rxrpc_transmit_some_data(call, 1, rxrpc_txdata_tlp_new_data);
			call->tlp_serial = call->tx_last_serial;
			call->tlp_seq = call->tx_transmitted;
			trace_rxrpc_tlp_probe(call, rxrpc_tlp_probe_trace_transmit_new);
			in_flight = rxrpc_tx_in_flight(call);
		} else {
			/* Retransmit the highest-sequence DATA sent */
			call->tx_last_serial = 0;
			rxrpc_resend_tlp(call);
			call->tlp_is_retrans = true;
			trace_rxrpc_tlp_probe(call, rxrpc_tlp_probe_trace_retransmit);
		}
	} else {
		trace_rxrpc_tlp_probe(call, rxrpc_tlp_probe_trace_busy);
	}

	if (in_flight != 0) {
		ktime_t rto = rxrpc_get_rto_backoff(call, false);

		call->rack_timer_mode = RXRPC_CALL_RACKTIMER_RTO;
		call->rack_timo_at = ktime_add(ktime_get_real(), rto);
		trace_rxrpc_rack_timer(call, rto, false);
		trace_rxrpc_timer_set(call, rto, rxrpc_timer_trace_rack_rto);
	}
}

/*
 * Detect losses using the ACK of a TLP loss probe [RFC8958 7.4].
 */
void rxrpc_tlp_process_ack(struct rxrpc_call *call, struct rxrpc_ack_summary *summary)
{
	if (!call->tlp_serial || after(call->tlp_seq, call->acks_hard_ack))
		return;

	if (!call->tlp_is_retrans) {
		/* TLP of new data delivered */
		trace_rxrpc_tlp_ack(call, summary, rxrpc_tlp_ack_trace_new_data);
		call->tlp_serial = 0;
	} else if (summary->ack_reason == RXRPC_ACK_DUPLICATE &&
		   summary->acked_serial == call->tlp_serial) {
		/* General Case: Detected packet losses using RACK [7.4.1] */
		trace_rxrpc_tlp_ack(call, summary, rxrpc_tlp_ack_trace_dup_acked);
		call->tlp_serial = 0;
	} else if (after(call->acks_hard_ack, call->tlp_seq)) {
		/* Repaired the single loss */
		trace_rxrpc_tlp_ack(call, summary, rxrpc_tlp_ack_trace_hard_beyond);
		call->tlp_serial = 0;
		// TODO: Invoke congestion control to react to the loss
		// event the probe has repaired
	} else if (summary->tlp_probe_acked) {
		trace_rxrpc_tlp_ack(call, summary, rxrpc_tlp_ack_trace_acked);
		/* Special Case: Detected a single loss repaired by the loss
		 * probe [7.4.2]
		 */
		call->tlp_serial = 0;
	} else {
		trace_rxrpc_tlp_ack(call, summary, rxrpc_tlp_ack_trace_incomplete);
	}
}

/*
 * Handle RACK timer expiration; returns true to request a resend.
 */
void rxrpc_rack_timer_expired(struct rxrpc_call *call, ktime_t overran_by)
{
	struct rxrpc_ack_summary summary = {};
	enum rxrpc_rack_timer_mode mode = call->rack_timer_mode;

	trace_rxrpc_rack_timer(call, overran_by, true);
	call->rack_timer_mode = RXRPC_CALL_RACKTIMER_OFF;

	switch (mode) {
	case RXRPC_CALL_RACKTIMER_RACK_REORDER:
		rxrpc_rack_detect_loss_and_arm_timer(call, &summary);
		break;
	case RXRPC_CALL_RACKTIMER_TLP_PTO:
		rxrpc_tlp_send_probe(call);
		break;
	case RXRPC_CALL_RACKTIMER_RTO:
		// Might need to poke the congestion algo in some way
		rxrpc_rack_mark_losses_on_rto(call);
		break;
	//case RXRPC_CALL_RACKTIMER_ZEROWIN:
	default:
		pr_warn("Unexpected rack timer %u", call->rack_timer_mode);
	}
}
