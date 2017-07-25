/*
 *  Copyright (c) 2005, 2006 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 *
 *  Changes to meet Linux coding standards, and DCCP infrastructure fixes.
 *
 *  Copyright (c) 2006 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This implementation should follow RFC 4341
 */
#include <linux/slab.h>
#include "../feat.h"
#include "ccid2.h"


#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
static bool ccid2_debug;
#define ccid2_pr_debug(format, a...)	DCCP_PR_DEBUG(ccid2_debug, format, ##a)
#else
#define ccid2_pr_debug(format, a...)
#endif

static int ccid2_hc_tx_alloc_seq(struct ccid2_hc_tx_sock *hc)
{
	struct ccid2_seq *seqp;
	int i;

	/* check if we have space to preserve the pointer to the buffer */
	if (hc->tx_seqbufc >= (sizeof(hc->tx_seqbuf) /
			       sizeof(struct ccid2_seq *)))
		return -ENOMEM;

	/* allocate buffer and initialize linked list */
	seqp = kmalloc(CCID2_SEQBUF_LEN * sizeof(struct ccid2_seq), gfp_any());
	if (seqp == NULL)
		return -ENOMEM;

	for (i = 0; i < (CCID2_SEQBUF_LEN - 1); i++) {
		seqp[i].ccid2s_next = &seqp[i + 1];
		seqp[i + 1].ccid2s_prev = &seqp[i];
	}
	seqp[CCID2_SEQBUF_LEN - 1].ccid2s_next = seqp;
	seqp->ccid2s_prev = &seqp[CCID2_SEQBUF_LEN - 1];

	/* This is the first allocation.  Initiate the head and tail.  */
	if (hc->tx_seqbufc == 0)
		hc->tx_seqh = hc->tx_seqt = seqp;
	else {
		/* link the existing list with the one we just created */
		hc->tx_seqh->ccid2s_next = seqp;
		seqp->ccid2s_prev = hc->tx_seqh;

		hc->tx_seqt->ccid2s_prev = &seqp[CCID2_SEQBUF_LEN - 1];
		seqp[CCID2_SEQBUF_LEN - 1].ccid2s_next = hc->tx_seqt;
	}

	/* store the original pointer to the buffer so we can free it */
	hc->tx_seqbuf[hc->tx_seqbufc] = seqp;
	hc->tx_seqbufc++;

	return 0;
}

static int ccid2_hc_tx_send_packet(struct sock *sk, struct sk_buff *skb)
{
	if (ccid2_cwnd_network_limited(ccid2_hc_tx_sk(sk)))
		return CCID_PACKET_WILL_DEQUEUE_LATER;
	return CCID_PACKET_SEND_AT_ONCE;
}

static void ccid2_change_l_ack_ratio(struct sock *sk, u32 val)
{
	u32 max_ratio = DIV_ROUND_UP(ccid2_hc_tx_sk(sk)->tx_cwnd, 2);

	/*
	 * Ensure that Ack Ratio does not exceed ceil(cwnd/2), which is (2) from
	 * RFC 4341, 6.1.2. We ignore the statement that Ack Ratio 2 is always
	 * acceptable since this causes starvation/deadlock whenever cwnd < 2.
	 * The same problem arises when Ack Ratio is 0 (ie. Ack Ratio disabled).
	 */
	if (val == 0 || val > max_ratio) {
		DCCP_WARN("Limiting Ack Ratio (%u) to %u\n", val, max_ratio);
		val = max_ratio;
	}
	dccp_feat_signal_nn_change(sk, DCCPF_ACK_RATIO,
				   min_t(u32, val, DCCPF_ACK_RATIO_MAX));
}

static void ccid2_check_l_ack_ratio(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);

	/*
	 * After a loss, idle period, application limited period, or RTO we
	 * need to check that the ack ratio is still less than the congestion
	 * window. Otherwise, we will send an entire congestion window of
	 * packets and got no response because we haven't sent ack ratio
	 * packets yet.
	 * If the ack ratio does need to be reduced, we reduce it to half of
	 * the congestion window (or 1 if that's zero) instead of to the
	 * congestion window. This prevents problems if one ack is lost.
	 */
	if (dccp_feat_nn_get(sk, DCCPF_ACK_RATIO) > hc->tx_cwnd)
		ccid2_change_l_ack_ratio(sk, hc->tx_cwnd/2 ? : 1U);
}

static void ccid2_change_l_seq_window(struct sock *sk, u64 val)
{
	dccp_feat_signal_nn_change(sk, DCCPF_SEQUENCE_WINDOW,
				   clamp_val(val, DCCPF_SEQ_WMIN,
						  DCCPF_SEQ_WMAX));
}

static void ccid2_hc_tx_rto_expire(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	const bool sender_was_blocked = ccid2_cwnd_network_limited(hc);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &hc->tx_rtotimer, jiffies + HZ / 5);
		goto out;
	}

	ccid2_pr_debug("RTO_EXPIRE\n");

	/* back-off timer */
	hc->tx_rto <<= 1;
	if (hc->tx_rto > DCCP_RTO_MAX)
		hc->tx_rto = DCCP_RTO_MAX;

	/* adjust pipe, cwnd etc */
	hc->tx_ssthresh = hc->tx_cwnd / 2;
	if (hc->tx_ssthresh < 2)
		hc->tx_ssthresh = 2;
	hc->tx_cwnd	= 1;
	hc->tx_pipe	= 0;

	/* clear state about stuff we sent */
	hc->tx_seqt = hc->tx_seqh;
	hc->tx_packets_acked = 0;

	/* clear ack ratio state. */
	hc->tx_rpseq    = 0;
	hc->tx_rpdupack = -1;
	ccid2_change_l_ack_ratio(sk, 1);

	/* if we were blocked before, we may now send cwnd=1 packet */
	if (sender_was_blocked)
		tasklet_schedule(&dccp_sk(sk)->dccps_xmitlet);
	/* restart backed-off timer */
	sk_reset_timer(sk, &hc->tx_rtotimer, jiffies + hc->tx_rto);
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	Congestion window validation (RFC 2861).
 */
static bool ccid2_do_cwv = true;
module_param(ccid2_do_cwv, bool, 0644);
MODULE_PARM_DESC(ccid2_do_cwv, "Perform RFC2861 Congestion Window Validation");

/**
 * ccid2_update_used_window  -  Track how much of cwnd is actually used
 * This is done in addition to CWV. The sender needs to have an idea of how many
 * packets may be in flight, to set the local Sequence Window value accordingly
 * (RFC 4340, 7.5.2). The CWV mechanism is exploited to keep track of the
 * maximum-used window. We use an EWMA low-pass filter to filter out noise.
 */
static void ccid2_update_used_window(struct ccid2_hc_tx_sock *hc, u32 new_wnd)
{
	hc->tx_expected_wnd = (3 * hc->tx_expected_wnd + new_wnd) / 4;
}

/* This borrows the code of tcp_cwnd_application_limited() */
static void ccid2_cwnd_application_limited(struct sock *sk, const u32 now)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	/* don't reduce cwnd below the initial window (IW) */
	u32 init_win = rfc3390_bytes_to_packets(dccp_sk(sk)->dccps_mss_cache),
	    win_used = max(hc->tx_cwnd_used, init_win);

	if (win_used < hc->tx_cwnd) {
		hc->tx_ssthresh = max(hc->tx_ssthresh,
				     (hc->tx_cwnd >> 1) + (hc->tx_cwnd >> 2));
		hc->tx_cwnd = (hc->tx_cwnd + win_used) >> 1;
	}
	hc->tx_cwnd_used  = 0;
	hc->tx_cwnd_stamp = now;

	ccid2_check_l_ack_ratio(sk);
}

/* This borrows the code of tcp_cwnd_restart() */
static void ccid2_cwnd_restart(struct sock *sk, const u32 now)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	u32 cwnd = hc->tx_cwnd, restart_cwnd,
	    iwnd = rfc3390_bytes_to_packets(dccp_sk(sk)->dccps_mss_cache);

	hc->tx_ssthresh = max(hc->tx_ssthresh, (cwnd >> 1) + (cwnd >> 2));

	/* don't reduce cwnd below the initial window (IW) */
	restart_cwnd = min(cwnd, iwnd);
	cwnd >>= (now - hc->tx_lsndtime) / hc->tx_rto;
	hc->tx_cwnd = max(cwnd, restart_cwnd);

	hc->tx_cwnd_stamp = now;
	hc->tx_cwnd_used  = 0;

	ccid2_check_l_ack_ratio(sk);
}

static void ccid2_hc_tx_packet_sent(struct sock *sk, unsigned int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	const u32 now = ccid2_jiffies32;
	struct ccid2_seq *next;

	/* slow-start after idle periods (RFC 2581, RFC 2861) */
	if (ccid2_do_cwv && !hc->tx_pipe &&
	    (s32)(now - hc->tx_lsndtime) >= hc->tx_rto)
		ccid2_cwnd_restart(sk, now);

	hc->tx_lsndtime = now;
	hc->tx_pipe    += 1;

	/* see whether cwnd was fully used (RFC 2861), update expected window */
	if (ccid2_cwnd_network_limited(hc)) {
		ccid2_update_used_window(hc, hc->tx_cwnd);
		hc->tx_cwnd_used  = 0;
		hc->tx_cwnd_stamp = now;
	} else {
		if (hc->tx_pipe > hc->tx_cwnd_used)
			hc->tx_cwnd_used = hc->tx_pipe;

		ccid2_update_used_window(hc, hc->tx_cwnd_used);

		if (ccid2_do_cwv && (s32)(now - hc->tx_cwnd_stamp) >= hc->tx_rto)
			ccid2_cwnd_application_limited(sk, now);
	}

	hc->tx_seqh->ccid2s_seq   = dp->dccps_gss;
	hc->tx_seqh->ccid2s_acked = 0;
	hc->tx_seqh->ccid2s_sent  = now;

	next = hc->tx_seqh->ccid2s_next;
	/* check if we need to alloc more space */
	if (next == hc->tx_seqt) {
		if (ccid2_hc_tx_alloc_seq(hc)) {
			DCCP_CRIT("packet history - out of memory!");
			/* FIXME: find a more graceful way to bail out */
			return;
		}
		next = hc->tx_seqh->ccid2s_next;
		BUG_ON(next == hc->tx_seqt);
	}
	hc->tx_seqh = next;

	ccid2_pr_debug("cwnd=%d pipe=%d\n", hc->tx_cwnd, hc->tx_pipe);

	/*
	 * FIXME: The code below is broken and the variables have been removed
	 * from the socket struct. The `ackloss' variable was always set to 0,
	 * and with arsent there are several problems:
	 *  (i) it doesn't just count the number of Acks, but all sent packets;
	 *  (ii) it is expressed in # of packets, not # of windows, so the
	 *  comparison below uses the wrong formula: Appendix A of RFC 4341
	 *  comes up with the number K = cwnd / (R^2 - R) of consecutive windows
	 *  of data with no lost or marked Ack packets. If arsent were the # of
	 *  consecutive Acks received without loss, then Ack Ratio needs to be
	 *  decreased by 1 when
	 *	      arsent >=  K * cwnd / R  =  cwnd^2 / (R^3 - R^2)
	 *  where cwnd / R is the number of Acks received per window of data
	 *  (cf. RFC 4341, App. A). The problems are that
	 *  - arsent counts other packets as well;
	 *  - the comparison uses a formula different from RFC 4341;
	 *  - computing a cubic/quadratic equation each time is too complicated.
	 *  Hence a different algorithm is needed.
	 */
#if 0
	/* Ack Ratio.  Need to maintain a concept of how many windows we sent */
	hc->tx_arsent++;
	/* We had an ack loss in this window... */
	if (hc->tx_ackloss) {
		if (hc->tx_arsent >= hc->tx_cwnd) {
			hc->tx_arsent  = 0;
			hc->tx_ackloss = 0;
		}
	} else {
		/* No acks lost up to now... */
		/* decrease ack ratio if enough packets were sent */
		if (dp->dccps_l_ack_ratio > 1) {
			/* XXX don't calculate denominator each time */
			int denom = dp->dccps_l_ack_ratio * dp->dccps_l_ack_ratio -
				    dp->dccps_l_ack_ratio;

			denom = hc->tx_cwnd * hc->tx_cwnd / denom;

			if (hc->tx_arsent >= denom) {
				ccid2_change_l_ack_ratio(sk, dp->dccps_l_ack_ratio - 1);
				hc->tx_arsent = 0;
			}
		} else {
			/* we can't increase ack ratio further [1] */
			hc->tx_arsent = 0; /* or maybe set it to cwnd*/
		}
	}
#endif

	sk_reset_timer(sk, &hc->tx_rtotimer, jiffies + hc->tx_rto);

#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
	do {
		struct ccid2_seq *seqp = hc->tx_seqt;

		while (seqp != hc->tx_seqh) {
			ccid2_pr_debug("out seq=%llu acked=%d time=%u\n",
				       (unsigned long long)seqp->ccid2s_seq,
				       seqp->ccid2s_acked, seqp->ccid2s_sent);
			seqp = seqp->ccid2s_next;
		}
	} while (0);
	ccid2_pr_debug("=========\n");
#endif
}

/**
 * ccid2_rtt_estimator - Sample RTT and compute RTO using RFC2988 algorithm
 * This code is almost identical with TCP's tcp_rtt_estimator(), since
 * - it has a higher sampling frequency (recommended by RFC 1323),
 * - the RTO does not collapse into RTT due to RTTVAR going towards zero,
 * - it is simple (cf. more complex proposals such as Eifel timer or research
 *   which suggests that the gain should be set according to window size),
 * - in tests it was found to work well with CCID2 [gerrit].
 */
static void ccid2_rtt_estimator(struct sock *sk, const long mrtt)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	long m = mrtt ? : 1;

	if (hc->tx_srtt == 0) {
		/* First measurement m */
		hc->tx_srtt = m << 3;
		hc->tx_mdev = m << 1;

		hc->tx_mdev_max = max(hc->tx_mdev, tcp_rto_min(sk));
		hc->tx_rttvar   = hc->tx_mdev_max;

		hc->tx_rtt_seq  = dccp_sk(sk)->dccps_gss;
	} else {
		/* Update scaled SRTT as SRTT += 1/8 * (m - SRTT) */
		m -= (hc->tx_srtt >> 3);
		hc->tx_srtt += m;

		/* Similarly, update scaled mdev with regard to |m| */
		if (m < 0) {
			m = -m;
			m -= (hc->tx_mdev >> 2);
			/*
			 * This neutralises RTO increase when RTT < SRTT - mdev
			 * (see P. Sarolahti, A. Kuznetsov,"Congestion Control
			 * in Linux TCP", USENIX 2002, pp. 49-62).
			 */
			if (m > 0)
				m >>= 3;
		} else {
			m -= (hc->tx_mdev >> 2);
		}
		hc->tx_mdev += m;

		if (hc->tx_mdev > hc->tx_mdev_max) {
			hc->tx_mdev_max = hc->tx_mdev;
			if (hc->tx_mdev_max > hc->tx_rttvar)
				hc->tx_rttvar = hc->tx_mdev_max;
		}

		/*
		 * Decay RTTVAR at most once per flight, exploiting that
		 *  1) pipe <= cwnd <= Sequence_Window = W  (RFC 4340, 7.5.2)
		 *  2) AWL = GSS-W+1 <= GAR <= GSS          (RFC 4340, 7.5.1)
		 * GAR is a useful bound for FlightSize = pipe.
		 * AWL is probably too low here, as it over-estimates pipe.
		 */
		if (after48(dccp_sk(sk)->dccps_gar, hc->tx_rtt_seq)) {
			if (hc->tx_mdev_max < hc->tx_rttvar)
				hc->tx_rttvar -= (hc->tx_rttvar -
						  hc->tx_mdev_max) >> 2;
			hc->tx_rtt_seq  = dccp_sk(sk)->dccps_gss;
			hc->tx_mdev_max = tcp_rto_min(sk);
		}
	}

	/*
	 * Set RTO from SRTT and RTTVAR
	 * As in TCP, 4 * RTTVAR >= TCP_RTO_MIN, giving a minimum RTO of 200 ms.
	 * This agrees with RFC 4341, 5:
	 *	"Because DCCP does not retransmit data, DCCP does not require
	 *	 TCP's recommended minimum timeout of one second".
	 */
	hc->tx_rto = (hc->tx_srtt >> 3) + hc->tx_rttvar;

	if (hc->tx_rto > DCCP_RTO_MAX)
		hc->tx_rto = DCCP_RTO_MAX;
}

static void ccid2_new_ack(struct sock *sk, struct ccid2_seq *seqp,
			  unsigned int *maxincr)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	int r_seq_used = hc->tx_cwnd / dp->dccps_l_ack_ratio;

	if (hc->tx_cwnd < dp->dccps_l_seq_win &&
	    r_seq_used < dp->dccps_r_seq_win) {
		if (hc->tx_cwnd < hc->tx_ssthresh) {
			if (*maxincr > 0 && ++hc->tx_packets_acked >= 2) {
				hc->tx_cwnd += 1;
				*maxincr    -= 1;
				hc->tx_packets_acked = 0;
			}
		} else if (++hc->tx_packets_acked >= hc->tx_cwnd) {
			hc->tx_cwnd += 1;
			hc->tx_packets_acked = 0;
		}
	}

	/*
	 * Adjust the local sequence window and the ack ratio to allow about
	 * 5 times the number of packets in the network (RFC 4340 7.5.2)
	 */
	if (r_seq_used * CCID2_WIN_CHANGE_FACTOR >= dp->dccps_r_seq_win)
		ccid2_change_l_ack_ratio(sk, dp->dccps_l_ack_ratio * 2);
	else if (r_seq_used * CCID2_WIN_CHANGE_FACTOR < dp->dccps_r_seq_win/2)
		ccid2_change_l_ack_ratio(sk, dp->dccps_l_ack_ratio / 2 ? : 1U);

	if (hc->tx_cwnd * CCID2_WIN_CHANGE_FACTOR >= dp->dccps_l_seq_win)
		ccid2_change_l_seq_window(sk, dp->dccps_l_seq_win * 2);
	else if (hc->tx_cwnd * CCID2_WIN_CHANGE_FACTOR < dp->dccps_l_seq_win/2)
		ccid2_change_l_seq_window(sk, dp->dccps_l_seq_win / 2);

	/*
	 * FIXME: RTT is sampled several times per acknowledgment (for each
	 * entry in the Ack Vector), instead of once per Ack (as in TCP SACK).
	 * This causes the RTT to be over-estimated, since the older entries
	 * in the Ack Vector have earlier sending times.
	 * The cleanest solution is to not use the ccid2s_sent field at all
	 * and instead use DCCP timestamps: requires changes in other places.
	 */
	ccid2_rtt_estimator(sk, ccid2_jiffies32 - seqp->ccid2s_sent);
}

static void ccid2_congestion_event(struct sock *sk, struct ccid2_seq *seqp)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);

	if ((s32)(seqp->ccid2s_sent - hc->tx_last_cong) < 0) {
		ccid2_pr_debug("Multiple losses in an RTT---treating as one\n");
		return;
	}

	hc->tx_last_cong = ccid2_jiffies32;

	hc->tx_cwnd      = hc->tx_cwnd / 2 ? : 1U;
	hc->tx_ssthresh  = max(hc->tx_cwnd, 2U);

	ccid2_check_l_ack_ratio(sk);
}

static int ccid2_hc_tx_parse_options(struct sock *sk, u8 packet_type,
				     u8 option, u8 *optval, u8 optlen)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);

	switch (option) {
	case DCCPO_ACK_VECTOR_0:
	case DCCPO_ACK_VECTOR_1:
		return dccp_ackvec_parsed_add(&hc->tx_av_chunks, optval, optlen,
					      option - DCCPO_ACK_VECTOR_0);
	}
	return 0;
}

static void ccid2_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	const bool sender_was_blocked = ccid2_cwnd_network_limited(hc);
	struct dccp_ackvec_parsed *avp;
	u64 ackno, seqno;
	struct ccid2_seq *seqp;
	int done = 0;
	unsigned int maxincr = 0;

	/* check reverse path congestion */
	seqno = DCCP_SKB_CB(skb)->dccpd_seq;

	/* XXX this whole "algorithm" is broken.  Need to fix it to keep track
	 * of the seqnos of the dupacks so that rpseq and rpdupack are correct
	 * -sorbo.
	 */
	/* need to bootstrap */
	if (hc->tx_rpdupack == -1) {
		hc->tx_rpdupack = 0;
		hc->tx_rpseq    = seqno;
	} else {
		/* check if packet is consecutive */
		if (dccp_delta_seqno(hc->tx_rpseq, seqno) == 1)
			hc->tx_rpseq = seqno;
		/* it's a later packet */
		else if (after48(seqno, hc->tx_rpseq)) {
			hc->tx_rpdupack++;

			/* check if we got enough dupacks */
			if (hc->tx_rpdupack >= NUMDUPACK) {
				hc->tx_rpdupack = -1; /* XXX lame */
				hc->tx_rpseq    = 0;
#ifdef __CCID2_COPES_GRACEFULLY_WITH_ACK_CONGESTION_CONTROL__
				/*
				 * FIXME: Ack Congestion Control is broken; in
				 * the current state instabilities occurred with
				 * Ack Ratios greater than 1; causing hang-ups
				 * and long RTO timeouts. This needs to be fixed
				 * before opening up dynamic changes. -- gerrit
				 */
				ccid2_change_l_ack_ratio(sk, 2 * dp->dccps_l_ack_ratio);
#endif
			}
		}
	}

	/* check forward path congestion */
	if (dccp_packet_without_ack(skb))
		return;

	/* still didn't send out new data packets */
	if (hc->tx_seqh == hc->tx_seqt)
		goto done;

	ackno = DCCP_SKB_CB(skb)->dccpd_ack_seq;
	if (after48(ackno, hc->tx_high_ack))
		hc->tx_high_ack = ackno;

	seqp = hc->tx_seqt;
	while (before48(seqp->ccid2s_seq, ackno)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hc->tx_seqh) {
			seqp = hc->tx_seqh->ccid2s_prev;
			break;
		}
	}

	/*
	 * In slow-start, cwnd can increase up to a maximum of Ack Ratio/2
	 * packets per acknowledgement. Rounding up avoids that cwnd is not
	 * advanced when Ack Ratio is 1 and gives a slight edge otherwise.
	 */
	if (hc->tx_cwnd < hc->tx_ssthresh)
		maxincr = DIV_ROUND_UP(dp->dccps_l_ack_ratio, 2);

	/* go through all ack vectors */
	list_for_each_entry(avp, &hc->tx_av_chunks, node) {
		/* go through this ack vector */
		for (; avp->len--; avp->vec++) {
			u64 ackno_end_rl = SUB48(ackno,
						 dccp_ackvec_runlen(avp->vec));

			ccid2_pr_debug("ackvec %llu |%u,%u|\n",
				       (unsigned long long)ackno,
				       dccp_ackvec_state(avp->vec) >> 6,
				       dccp_ackvec_runlen(avp->vec));
			/* if the seqno we are analyzing is larger than the
			 * current ackno, then move towards the tail of our
			 * seqnos.
			 */
			while (after48(seqp->ccid2s_seq, ackno)) {
				if (seqp == hc->tx_seqt) {
					done = 1;
					break;
				}
				seqp = seqp->ccid2s_prev;
			}
			if (done)
				break;

			/* check all seqnos in the range of the vector
			 * run length
			 */
			while (between48(seqp->ccid2s_seq,ackno_end_rl,ackno)) {
				const u8 state = dccp_ackvec_state(avp->vec);

				/* new packet received or marked */
				if (state != DCCPAV_NOT_RECEIVED &&
				    !seqp->ccid2s_acked) {
					if (state == DCCPAV_ECN_MARKED)
						ccid2_congestion_event(sk,
								       seqp);
					else
						ccid2_new_ack(sk, seqp,
							      &maxincr);

					seqp->ccid2s_acked = 1;
					ccid2_pr_debug("Got ack for %llu\n",
						       (unsigned long long)seqp->ccid2s_seq);
					hc->tx_pipe--;
				}
				if (seqp == hc->tx_seqt) {
					done = 1;
					break;
				}
				seqp = seqp->ccid2s_prev;
			}
			if (done)
				break;

			ackno = SUB48(ackno_end_rl, 1);
		}
		if (done)
			break;
	}

	/* The state about what is acked should be correct now
	 * Check for NUMDUPACK
	 */
	seqp = hc->tx_seqt;
	while (before48(seqp->ccid2s_seq, hc->tx_high_ack)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hc->tx_seqh) {
			seqp = hc->tx_seqh->ccid2s_prev;
			break;
		}
	}
	done = 0;
	while (1) {
		if (seqp->ccid2s_acked) {
			done++;
			if (done == NUMDUPACK)
				break;
		}
		if (seqp == hc->tx_seqt)
			break;
		seqp = seqp->ccid2s_prev;
	}

	/* If there are at least 3 acknowledgements, anything unacknowledged
	 * below the last sequence number is considered lost
	 */
	if (done == NUMDUPACK) {
		struct ccid2_seq *last_acked = seqp;

		/* check for lost packets */
		while (1) {
			if (!seqp->ccid2s_acked) {
				ccid2_pr_debug("Packet lost: %llu\n",
					       (unsigned long long)seqp->ccid2s_seq);
				/* XXX need to traverse from tail -> head in
				 * order to detect multiple congestion events in
				 * one ack vector.
				 */
				ccid2_congestion_event(sk, seqp);
				hc->tx_pipe--;
			}
			if (seqp == hc->tx_seqt)
				break;
			seqp = seqp->ccid2s_prev;
		}

		hc->tx_seqt = last_acked;
	}

	/* trim acked packets in tail */
	while (hc->tx_seqt != hc->tx_seqh) {
		if (!hc->tx_seqt->ccid2s_acked)
			break;

		hc->tx_seqt = hc->tx_seqt->ccid2s_next;
	}

	/* restart RTO timer if not all outstanding data has been acked */
	if (hc->tx_pipe == 0)
		sk_stop_timer(sk, &hc->tx_rtotimer);
	else
		sk_reset_timer(sk, &hc->tx_rtotimer, jiffies + hc->tx_rto);
done:
	/* check if incoming Acks allow pending packets to be sent */
	if (sender_was_blocked && !ccid2_cwnd_network_limited(hc))
		tasklet_schedule(&dccp_sk(sk)->dccps_xmitlet);
	dccp_ackvec_parsed_cleanup(&hc->tx_av_chunks);
}

static int ccid2_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid2_hc_tx_sock *hc = ccid_priv(ccid);
	struct dccp_sock *dp = dccp_sk(sk);
	u32 max_ratio;

	/* RFC 4341, 5: initialise ssthresh to arbitrarily high (max) value */
	hc->tx_ssthresh = ~0U;

	/* Use larger initial windows (RFC 4341, section 5). */
	hc->tx_cwnd = rfc3390_bytes_to_packets(dp->dccps_mss_cache);
	hc->tx_expected_wnd = hc->tx_cwnd;

	/* Make sure that Ack Ratio is enabled and within bounds. */
	max_ratio = DIV_ROUND_UP(hc->tx_cwnd, 2);
	if (dp->dccps_l_ack_ratio == 0 || dp->dccps_l_ack_ratio > max_ratio)
		dp->dccps_l_ack_ratio = max_ratio;

	/* XXX init ~ to window size... */
	if (ccid2_hc_tx_alloc_seq(hc))
		return -ENOMEM;

	hc->tx_rto	 = DCCP_TIMEOUT_INIT;
	hc->tx_rpdupack  = -1;
	hc->tx_last_cong = hc->tx_lsndtime = hc->tx_cwnd_stamp = ccid2_jiffies32;
	hc->tx_cwnd_used = 0;
	setup_timer(&hc->tx_rtotimer, ccid2_hc_tx_rto_expire,
			(unsigned long)sk);
	INIT_LIST_HEAD(&hc->tx_av_chunks);
	return 0;
}

static void ccid2_hc_tx_exit(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	int i;

	sk_stop_timer(sk, &hc->tx_rtotimer);

	for (i = 0; i < hc->tx_seqbufc; i++)
		kfree(hc->tx_seqbuf[i]);
	hc->tx_seqbufc = 0;
	dccp_ackvec_parsed_cleanup(&hc->tx_av_chunks);
}

static void ccid2_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ccid2_hc_rx_sock *hc = ccid2_hc_rx_sk(sk);

	if (!dccp_data_packet(skb))
		return;

	if (++hc->rx_num_data_pkts >= dccp_sk(sk)->dccps_r_ack_ratio) {
		dccp_send_ack(sk);
		hc->rx_num_data_pkts = 0;
	}
}

struct ccid_operations ccid2_ops = {
	.ccid_id		  = DCCPC_CCID2,
	.ccid_name		  = "TCP-like",
	.ccid_hc_tx_obj_size	  = sizeof(struct ccid2_hc_tx_sock),
	.ccid_hc_tx_init	  = ccid2_hc_tx_init,
	.ccid_hc_tx_exit	  = ccid2_hc_tx_exit,
	.ccid_hc_tx_send_packet	  = ccid2_hc_tx_send_packet,
	.ccid_hc_tx_packet_sent	  = ccid2_hc_tx_packet_sent,
	.ccid_hc_tx_parse_options = ccid2_hc_tx_parse_options,
	.ccid_hc_tx_packet_recv	  = ccid2_hc_tx_packet_recv,
	.ccid_hc_rx_obj_size	  = sizeof(struct ccid2_hc_rx_sock),
	.ccid_hc_rx_packet_recv	  = ccid2_hc_rx_packet_recv,
};

#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
module_param(ccid2_debug, bool, 0644);
MODULE_PARM_DESC(ccid2_debug, "Enable CCID-2 debug messages");
#endif
