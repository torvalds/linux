// SPDX-License-Identifier: GPL-2.0
#include <linux/tcp.h>
#include <net/tcp.h>

static void tcp_rack_mark_skb_lost(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	tcp_skb_mark_lost_uncond_verify(tp, skb);
	if (TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_RETRANS) {
		/* Account for retransmits that are lost again */
		TCP_SKB_CB(skb)->sacked &= ~TCPCB_SACKED_RETRANS;
		tp->retrans_out -= tcp_skb_pcount(skb);
		NET_ADD_STATS(sock_net(sk), LINUX_MIB_TCPLOSTRETRANSMIT,
			      tcp_skb_pcount(skb));
	}
}

static bool tcp_rack_sent_after(u64 t1, u64 t2, u32 seq1, u32 seq2)
{
	return t1 > t2 || (t1 == t2 && after(seq1, seq2));
}

/* RACK loss detection (IETF draft draft-ietf-tcpm-rack-01):
 *
 * Marks a packet lost, if some packet sent later has been (s)acked.
 * The underlying idea is similar to the traditional dupthresh and FACK
 * but they look at different metrics:
 *
 * dupthresh: 3 OOO packets delivered (packet count)
 * FACK: sequence delta to highest sacked sequence (sequence space)
 * RACK: sent time delta to the latest delivered packet (time domain)
 *
 * The advantage of RACK is it applies to both original and retransmitted
 * packet and therefore is robust against tail losses. Another advantage
 * is being more resilient to reordering by simply allowing some
 * "settling delay", instead of tweaking the dupthresh.
 *
 * When tcp_rack_detect_loss() detects some packets are lost and we
 * are not already in the CA_Recovery state, either tcp_rack_reo_timeout()
 * or tcp_time_to_recover()'s "Trick#1: the loss is proven" code path will
 * make us enter the CA_Recovery state.
 */
static void tcp_rack_detect_loss(struct sock *sk, u32 *reo_timeout)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 min_rtt = tcp_min_rtt(tp);
	struct sk_buff *skb, *n;
	u32 reo_wnd;

	*reo_timeout = 0;
	/* To be more reordering resilient, allow min_rtt/4 settling delay
	 * (lower-bounded to 1000uS). We use min_rtt instead of the smoothed
	 * RTT because reordering is often a path property and less related
	 * to queuing or delayed ACKs.
	 */
	reo_wnd = 1000;
	if ((tp->rack.reord || inet_csk(sk)->icsk_ca_state < TCP_CA_Recovery) &&
	    min_rtt != ~0U) {
		reo_wnd = max((min_rtt >> 2) * tp->rack.reo_wnd_steps, reo_wnd);
		reo_wnd = min(reo_wnd, tp->srtt_us >> 3);
	}

	list_for_each_entry_safe(skb, n, &tp->tsorted_sent_queue,
				 tcp_tsorted_anchor) {
		struct tcp_skb_cb *scb = TCP_SKB_CB(skb);
		s32 remaining;

		/* Skip ones marked lost but not yet retransmitted */
		if ((scb->sacked & TCPCB_LOST) &&
		    !(scb->sacked & TCPCB_SACKED_RETRANS))
			continue;

		if (!tcp_rack_sent_after(tp->rack.mstamp, skb->skb_mstamp,
					 tp->rack.end_seq, scb->end_seq))
			break;

		/* A packet is lost if it has not been s/acked beyond
		 * the recent RTT plus the reordering window.
		 */
		remaining = tp->rack.rtt_us + reo_wnd -
			    tcp_stamp_us_delta(tp->tcp_mstamp, skb->skb_mstamp);
		if (remaining < 0) {
			tcp_rack_mark_skb_lost(sk, skb);
			list_del_init(&skb->tcp_tsorted_anchor);
		} else {
			/* Record maximum wait time (+1 to avoid 0) */
			*reo_timeout = max_t(u32, *reo_timeout, 1 + remaining);
		}
	}
}

void tcp_rack_mark_lost(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 timeout;

	if (!tp->rack.advanced)
		return;

	/* Reset the advanced flag to avoid unnecessary queue scanning */
	tp->rack.advanced = 0;
	tcp_rack_detect_loss(sk, &timeout);
	if (timeout) {
		timeout = usecs_to_jiffies(timeout) + TCP_TIMEOUT_MIN;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_REO_TIMEOUT,
					  timeout, inet_csk(sk)->icsk_rto);
	}
}

/* Record the most recently (re)sent time among the (s)acked packets
 * This is "Step 3: Advance RACK.xmit_time and update RACK.RTT" from
 * draft-cheng-tcpm-rack-00.txt
 */
void tcp_rack_advance(struct tcp_sock *tp, u8 sacked, u32 end_seq,
		      u64 xmit_time)
{
	u32 rtt_us;

	if (tp->rack.mstamp &&
	    !tcp_rack_sent_after(xmit_time, tp->rack.mstamp,
				 end_seq, tp->rack.end_seq))
		return;

	rtt_us = tcp_stamp_us_delta(tp->tcp_mstamp, xmit_time);
	if (sacked & TCPCB_RETRANS) {
		/* If the sacked packet was retransmitted, it's ambiguous
		 * whether the retransmission or the original (or the prior
		 * retransmission) was sacked.
		 *
		 * If the original is lost, there is no ambiguity. Otherwise
		 * we assume the original can be delayed up to aRTT + min_rtt.
		 * the aRTT term is bounded by the fast recovery or timeout,
		 * so it's at least one RTT (i.e., retransmission is at least
		 * an RTT later).
		 */
		if (rtt_us < tcp_min_rtt(tp))
			return;
	}
	tp->rack.rtt_us = rtt_us;
	tp->rack.mstamp = xmit_time;
	tp->rack.end_seq = end_seq;
	tp->rack.advanced = 1;
}

/* We have waited long enough to accommodate reordering. Mark the expired
 * packets lost and retransmit them.
 */
void tcp_rack_reo_timeout(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 timeout, prior_inflight;

	prior_inflight = tcp_packets_in_flight(tp);
	tcp_rack_detect_loss(sk, &timeout);
	if (prior_inflight != tcp_packets_in_flight(tp)) {
		if (inet_csk(sk)->icsk_ca_state != TCP_CA_Recovery) {
			tcp_enter_recovery(sk, false);
			if (!inet_csk(sk)->icsk_ca_ops->cong_control)
				tcp_cwnd_reduction(sk, 1, 0);
		}
		tcp_xmit_retransmit_queue(sk);
	}
	if (inet_csk(sk)->icsk_pending != ICSK_TIME_RETRANS)
		tcp_rearm_rto(sk);
}

/* Updates the RACK's reo_wnd based on DSACK and no. of recoveries.
 *
 * If DSACK is received, increment reo_wnd by min_rtt/4 (upper bounded
 * by srtt), since there is possibility that spurious retransmission was
 * due to reordering delay longer than reo_wnd.
 *
 * Persist the current reo_wnd value for TCP_RACK_RECOVERY_THRESH (16)
 * no. of successful recoveries (accounts for full DSACK-based loss
 * recovery undo). After that, reset it to default (min_rtt/4).
 *
 * At max, reo_wnd is incremented only once per rtt. So that the new
 * DSACK on which we are reacting, is due to the spurious retx (approx)
 * after the reo_wnd has been updated last time.
 *
 * reo_wnd is tracked in terms of steps (of min_rtt/4), rather than
 * absolute value to account for change in rtt.
 */
void tcp_rack_update_reo_wnd(struct sock *sk, struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (sock_net(sk)->ipv4.sysctl_tcp_recovery & TCP_RACK_STATIC_REO_WND ||
	    !rs->prior_delivered)
		return;

	/* Disregard DSACK if a rtt has not passed since we adjusted reo_wnd */
	if (before(rs->prior_delivered, tp->rack.last_delivered))
		tp->rack.dsack_seen = 0;

	/* Adjust the reo_wnd if update is pending */
	if (tp->rack.dsack_seen) {
		tp->rack.reo_wnd_steps = min_t(u32, 0xFF,
					       tp->rack.reo_wnd_steps + 1);
		tp->rack.dsack_seen = 0;
		tp->rack.last_delivered = tp->delivered;
		tp->rack.reo_wnd_persist = TCP_RACK_RECOVERY_THRESH;
	} else if (!tp->rack.reo_wnd_persist) {
		tp->rack.reo_wnd_steps = 1;
	}
}
