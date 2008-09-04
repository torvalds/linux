/*
 *  net/dccp/ccids/ccid2.c
 *
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
#include "../feat.h"
#include "../ccid.h"
#include "../dccp.h"
#include "ccid2.h"


#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
static int ccid2_debug;
#define ccid2_pr_debug(format, a...)	DCCP_PR_DEBUG(ccid2_debug, format, ##a)
#else
#define ccid2_pr_debug(format, a...)
#endif

static int ccid2_hc_tx_alloc_seq(struct ccid2_hc_tx_sock *hctx)
{
	struct ccid2_seq *seqp;
	int i;

	/* check if we have space to preserve the pointer to the buffer */
	if (hctx->seqbufc >= sizeof(hctx->seqbuf) / sizeof(struct ccid2_seq *))
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
	if (hctx->seqbufc == 0)
		hctx->seqh = hctx->seqt = seqp;
	else {
		/* link the existing list with the one we just created */
		hctx->seqh->ccid2s_next = seqp;
		seqp->ccid2s_prev = hctx->seqh;

		hctx->seqt->ccid2s_prev = &seqp[CCID2_SEQBUF_LEN - 1];
		seqp[CCID2_SEQBUF_LEN - 1].ccid2s_next = hctx->seqt;
	}

	/* store the original pointer to the buffer so we can free it */
	hctx->seqbuf[hctx->seqbufc] = seqp;
	hctx->seqbufc++;

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
	struct dccp_sock *dp = dccp_sk(sk);
	u32 max_ratio = DIV_ROUND_UP(ccid2_hc_tx_sk(sk)->cwnd, 2);

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
	if (val > DCCPF_ACK_RATIO_MAX)
		val = DCCPF_ACK_RATIO_MAX;

	if (val == dp->dccps_l_ack_ratio)
		return;

	ccid2_pr_debug("changing local ack ratio to %u\n", val);
	dp->dccps_l_ack_ratio = val;
}

static void ccid2_hc_tx_rto_expire(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	const bool sender_was_blocked = ccid2_cwnd_network_limited(hctx);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &hctx->rtotimer, jiffies + HZ / 5);
		goto out;
	}

	ccid2_pr_debug("RTO_EXPIRE\n");

	/* back-off timer */
	hctx->rto <<= 1;
	if (hctx->rto > DCCP_RTO_MAX)
		hctx->rto = DCCP_RTO_MAX;

	/* adjust pipe, cwnd etc */
	hctx->ssthresh = hctx->cwnd / 2;
	if (hctx->ssthresh < 2)
		hctx->ssthresh = 2;
	hctx->cwnd = 1;
	hctx->pipe = 0;

	/* clear state about stuff we sent */
	hctx->seqt = hctx->seqh;
	hctx->packets_acked = 0;

	/* clear ack ratio state. */
	hctx->rpseq    = 0;
	hctx->rpdupack = -1;
	ccid2_change_l_ack_ratio(sk, 1);

	/* if we were blocked before, we may now send cwnd=1 packet */
	if (sender_was_blocked)
		tasklet_schedule(&dccp_sk(sk)->dccps_xmitlet);
	/* restart backed-off timer */
	sk_reset_timer(sk, &hctx->rtotimer, jiffies + hctx->rto);
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void ccid2_hc_tx_packet_sent(struct sock *sk, unsigned int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	struct ccid2_seq *next;

	hctx->pipe++;

	hctx->seqh->ccid2s_seq   = dp->dccps_gss;
	hctx->seqh->ccid2s_acked = 0;
	hctx->seqh->ccid2s_sent  = jiffies;

	next = hctx->seqh->ccid2s_next;
	/* check if we need to alloc more space */
	if (next == hctx->seqt) {
		if (ccid2_hc_tx_alloc_seq(hctx)) {
			DCCP_CRIT("packet history - out of memory!");
			/* FIXME: find a more graceful way to bail out */
			return;
		}
		next = hctx->seqh->ccid2s_next;
		BUG_ON(next == hctx->seqt);
	}
	hctx->seqh = next;

	ccid2_pr_debug("cwnd=%d pipe=%d\n", hctx->cwnd, hctx->pipe);

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
	hctx->arsent++;
	/* We had an ack loss in this window... */
	if (hctx->ackloss) {
		if (hctx->arsent >= hctx->cwnd) {
			hctx->arsent  = 0;
			hctx->ackloss = 0;
		}
	} else {
		/* No acks lost up to now... */
		/* decrease ack ratio if enough packets were sent */
		if (dp->dccps_l_ack_ratio > 1) {
			/* XXX don't calculate denominator each time */
			int denom = dp->dccps_l_ack_ratio * dp->dccps_l_ack_ratio -
				    dp->dccps_l_ack_ratio;

			denom = hctx->cwnd * hctx->cwnd / denom;

			if (hctx->arsent >= denom) {
				ccid2_change_l_ack_ratio(sk, dp->dccps_l_ack_ratio - 1);
				hctx->arsent = 0;
			}
		} else {
			/* we can't increase ack ratio further [1] */
			hctx->arsent = 0; /* or maybe set it to cwnd*/
		}
	}
#endif

	/* setup RTO timer */
	if (!timer_pending(&hctx->rtotimer))
		sk_reset_timer(sk, &hctx->rtotimer, jiffies + hctx->rto);

#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
	do {
		struct ccid2_seq *seqp = hctx->seqt;

		while (seqp != hctx->seqh) {
			ccid2_pr_debug("out seq=%llu acked=%d time=%lu\n",
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
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	long m = mrtt ? : 1;

	if (hctx->srtt == 0) {
		/* First measurement m */
		hctx->srtt = m << 3;
		hctx->mdev = m << 1;

		hctx->mdev_max = max(TCP_RTO_MIN, hctx->mdev);
		hctx->rttvar   = hctx->mdev_max;
		hctx->rtt_seq  = dccp_sk(sk)->dccps_gss;
	} else {
		/* Update scaled SRTT as SRTT += 1/8 * (m - SRTT) */
		m -= (hctx->srtt >> 3);
		hctx->srtt += m;

		/* Similarly, update scaled mdev with regard to |m| */
		if (m < 0) {
			m = -m;
			m -= (hctx->mdev >> 2);
			/*
			 * This neutralises RTO increase when RTT < SRTT - mdev
			 * (see P. Sarolahti, A. Kuznetsov,"Congestion Control
			 * in Linux TCP", USENIX 2002, pp. 49-62).
			 */
			if (m > 0)
				m >>= 3;
		} else {
			m -= (hctx->mdev >> 2);
		}
		hctx->mdev += m;

		if (hctx->mdev > hctx->mdev_max) {
			hctx->mdev_max = hctx->mdev;
			if (hctx->mdev_max > hctx->rttvar)
				hctx->rttvar = hctx->mdev_max;
		}

		/*
		 * Decay RTTVAR at most once per flight, exploiting that
		 *  1) pipe <= cwnd <= Sequence_Window = W  (RFC 4340, 7.5.2)
		 *  2) AWL = GSS-W+1 <= GAR <= GSS          (RFC 4340, 7.5.1)
		 * GAR is a useful bound for FlightSize = pipe, AWL is probably
		 * too low as it over-estimates pipe.
		 */
		if (after48(dccp_sk(sk)->dccps_gar, hctx->rtt_seq)) {
			if (hctx->mdev_max < hctx->rttvar)
				hctx->rttvar -= (hctx->rttvar -
						 hctx->mdev_max) >> 2;
			hctx->rtt_seq  = dccp_sk(sk)->dccps_gss;
			hctx->mdev_max = TCP_RTO_MIN;
		}
	}

	/*
	 * Set RTO from SRTT and RTTVAR
	 * Clock granularity is ignored since the minimum error for RTTVAR is
	 * clamped to 50msec (corresponding to HZ=20). This leads to a minimum
	 * RTO of 200msec. This agrees with TCP and RFC 4341, 5.: "Because DCCP
	 * does not retransmit data, DCCP does not require TCP's recommended
	 * minimum timeout of one second".
	 */
	hctx->rto = (hctx->srtt >> 3) + hctx->rttvar;

	if (hctx->rto > DCCP_RTO_MAX)
		hctx->rto = DCCP_RTO_MAX;
}

static void ccid2_new_ack(struct sock *sk, struct ccid2_seq *seqp,
			  unsigned int *maxincr)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (hctx->cwnd < hctx->ssthresh) {
		if (*maxincr > 0 && ++hctx->packets_acked == 2) {
			hctx->cwnd += 1;
			*maxincr   -= 1;
			hctx->packets_acked = 0;
		}
	} else if (++hctx->packets_acked >= hctx->cwnd) {
			hctx->cwnd += 1;
			hctx->packets_acked = 0;
	}
	/*
	 * FIXME: RTT is sampled several times per acknowledgment (for each
	 * entry in the Ack Vector), instead of once per Ack (as in TCP SACK).
	 * This causes the RTT to be over-estimated, since the older entries
	 * in the Ack Vector have earlier sending times.
	 * The cleanest solution is to not use the ccid2s_sent field at all
	 * and instead use DCCP timestamps - need to be resolved at some time.
	 */
	ccid2_rtt_estimator(sk, jiffies - seqp->ccid2s_sent);
}

static void ccid2_congestion_event(struct sock *sk, struct ccid2_seq *seqp)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (time_before(seqp->ccid2s_sent, hctx->last_cong)) {
		ccid2_pr_debug("Multiple losses in an RTT---treating as one\n");
		return;
	}

	hctx->last_cong = jiffies;

	hctx->cwnd     = hctx->cwnd / 2 ? : 1U;
	hctx->ssthresh = max(hctx->cwnd, 2U);

	/* Avoid spurious timeouts resulting from Ack Ratio > cwnd */
	if (dccp_sk(sk)->dccps_l_ack_ratio > hctx->cwnd)
		ccid2_change_l_ack_ratio(sk, hctx->cwnd);
}

static int ccid2_hc_tx_parse_options(struct sock *sk, u8 packet_type,
				     u8 option, u8 *optval, u8 optlen)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	switch (option) {
	case DCCPO_ACK_VECTOR_0:
	case DCCPO_ACK_VECTOR_1:
		return dccp_ackvec_parsed_add(&hctx->av_chunks, optval, optlen,
					      option - DCCPO_ACK_VECTOR_0);
	}
	return 0;
}

static void ccid2_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	const bool sender_was_blocked = ccid2_cwnd_network_limited(hctx);
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
	if (hctx->rpdupack == -1) {
		hctx->rpdupack = 0;
		hctx->rpseq = seqno;
	} else {
		/* check if packet is consecutive */
		if (dccp_delta_seqno(hctx->rpseq, seqno) == 1)
			hctx->rpseq = seqno;
		/* it's a later packet */
		else if (after48(seqno, hctx->rpseq)) {
			hctx->rpdupack++;

			/* check if we got enough dupacks */
			if (hctx->rpdupack >= NUMDUPACK) {
				hctx->rpdupack = -1; /* XXX lame */
				hctx->rpseq = 0;

				ccid2_change_l_ack_ratio(sk, 2 * dp->dccps_l_ack_ratio);
			}
		}
	}

	/* check forward path congestion */
	if (dccp_packet_without_ack(skb))
		return;

	/* still didn't send out new data packets */
	if (hctx->seqh == hctx->seqt)
		goto done;

	ackno = DCCP_SKB_CB(skb)->dccpd_ack_seq;
	if (after48(ackno, hctx->high_ack))
		hctx->high_ack = ackno;

	seqp = hctx->seqt;
	while (before48(seqp->ccid2s_seq, ackno)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hctx->seqh) {
			seqp = hctx->seqh->ccid2s_prev;
			break;
		}
	}

	/*
	 * In slow-start, cwnd can increase up to a maximum of Ack Ratio/2
	 * packets per acknowledgement. Rounding up avoids that cwnd is not
	 * advanced when Ack Ratio is 1 and gives a slight edge otherwise.
	 */
	if (hctx->cwnd < hctx->ssthresh)
		maxincr = DIV_ROUND_UP(dp->dccps_l_ack_ratio, 2);

	/* go through all ack vectors */
	list_for_each_entry(avp, &hctx->av_chunks, node) {
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
				if (seqp == hctx->seqt) {
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
					hctx->pipe--;
				}
				if (seqp == hctx->seqt) {
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
	seqp = hctx->seqt;
	while (before48(seqp->ccid2s_seq, hctx->high_ack)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hctx->seqh) {
			seqp = hctx->seqh->ccid2s_prev;
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
		if (seqp == hctx->seqt)
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
				hctx->pipe--;
			}
			if (seqp == hctx->seqt)
				break;
			seqp = seqp->ccid2s_prev;
		}

		hctx->seqt = last_acked;
	}

	/* trim acked packets in tail */
	while (hctx->seqt != hctx->seqh) {
		if (!hctx->seqt->ccid2s_acked)
			break;

		hctx->seqt = hctx->seqt->ccid2s_next;
	}

	/* restart RTO timer if not all outstanding data has been acked */
	if (hctx->pipe == 0)
		sk_stop_timer(sk, &hctx->rtotimer);
	else
		sk_reset_timer(sk, &hctx->rtotimer, jiffies + hctx->rto);
done:
	/* check if incoming Acks allow pending packets to be sent */
	if (sender_was_blocked && !ccid2_cwnd_network_limited(hctx))
		tasklet_schedule(&dccp_sk(sk)->dccps_xmitlet);
	dccp_ackvec_parsed_cleanup(&hctx->av_chunks);
}

static int ccid2_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid_priv(ccid);
	struct dccp_sock *dp = dccp_sk(sk);
	u32 max_ratio;

	/* RFC 4341, 5: initialise ssthresh to arbitrarily high (max) value */
	hctx->ssthresh = ~0U;

	/* Use larger initial windows (RFC 3390, rfc2581bis) */
	hctx->cwnd = rfc3390_bytes_to_packets(dp->dccps_mss_cache);

	/* Make sure that Ack Ratio is enabled and within bounds. */
	max_ratio = DIV_ROUND_UP(hctx->cwnd, 2);
	if (dp->dccps_l_ack_ratio == 0 || dp->dccps_l_ack_ratio > max_ratio)
		dp->dccps_l_ack_ratio = max_ratio;

	/* XXX init ~ to window size... */
	if (ccid2_hc_tx_alloc_seq(hctx))
		return -ENOMEM;

	hctx->rto	= DCCP_TIMEOUT_INIT;
	hctx->rpdupack  = -1;
	hctx->last_cong = jiffies;
	setup_timer(&hctx->rtotimer, ccid2_hc_tx_rto_expire, (unsigned long)sk);
	INIT_LIST_HEAD(&hctx->av_chunks);
	return 0;
}

static void ccid2_hc_tx_exit(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	int i;

	sk_stop_timer(sk, &hctx->rtotimer);

	for (i = 0; i < hctx->seqbufc; i++)
		kfree(hctx->seqbuf[i]);
	hctx->seqbufc = 0;
}

static void ccid2_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_rx_sock *hcrx = ccid2_hc_rx_sk(sk);

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_DATA:
	case DCCP_PKT_DATAACK:
		hcrx->data++;
		if (hcrx->data >= dp->dccps_r_ack_ratio) {
			dccp_send_ack(sk);
			hcrx->data = 0;
		}
		break;
	}
}

static struct ccid_operations ccid2 = {
	.ccid_id		  = DCCPC_CCID2,
	.ccid_name		  = "TCP-like",
	.ccid_owner		  = THIS_MODULE,
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
MODULE_PARM_DESC(ccid2_debug, "Enable debug messages");
#endif

static __init int ccid2_module_init(void)
{
	return ccid_register(&ccid2);
}
module_init(ccid2_module_init);

static __exit void ccid2_module_exit(void)
{
	ccid_unregister(&ccid2);
}
module_exit(ccid2_module_exit);

MODULE_AUTHOR("Andrea Bittau <a.bittau@cs.ucl.ac.uk>");
MODULE_DESCRIPTION("DCCP TCP-Like (CCID2) CCID");
MODULE_LICENSE("GPL");
MODULE_ALIAS("net-dccp-ccid-2");
