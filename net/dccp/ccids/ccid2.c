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
static int ccid2_debug;
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
	struct dccp_sock *dp = dccp_sk(sk);
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

static void ccid2_hc_tx_packet_sent(struct sock *sk, unsigned int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	struct ccid2_seq *next;

	hc->tx_pipe++;

	hc->tx_seqh->ccid2s_seq   = dp->dccps_gss;
	hc->tx_seqh->ccid2s_acked = 0;
	hc->tx_seqh->ccid2s_sent  = ccid2_time_stamp;

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

/* XXX Lame code duplication!
 * returns -1 if none was found.
 * else returns the next offset to use in the function call.
 */
static int ccid2_ackvector(struct sock *sk, struct sk_buff *skb, int offset,
			   unsigned char **vec, unsigned char *veclen)
{
	const struct dccp_hdr *dh = dccp_hdr(skb);
	unsigned char *options = (unsigned char *)dh + dccp_hdr_len(skb);
	unsigned char *opt_ptr;
	const unsigned char *opt_end = (unsigned char *)dh +
					(dh->dccph_doff * 4);
	unsigned char opt, len;
	unsigned char *value;

	BUG_ON(offset < 0);
	options += offset;
	opt_ptr = options;
	if (opt_ptr >= opt_end)
		return -1;

	while (opt_ptr != opt_end) {
		opt   = *opt_ptr++;
		len   = 0;
		value = NULL;

		/* Check if this isn't a single byte option */
		if (opt > DCCPO_MAX_RESERVED) {
			if (opt_ptr == opt_end)
				goto out_invalid_option;

			len = *opt_ptr++;
			if (len < 3)
				goto out_invalid_option;
			/*
			 * Remove the type and len fields, leaving
			 * just the value size
			 */
			len     -= 2;
			value   = opt_ptr;
			opt_ptr += len;

			if (opt_ptr > opt_end)
				goto out_invalid_option;
		}

		switch (opt) {
		case DCCPO_ACK_VECTOR_0:
		case DCCPO_ACK_VECTOR_1:
			*vec	= value;
			*veclen = len;
			return offset + (opt_ptr - options);
		}
	}

	return -1;

out_invalid_option:
	DCCP_BUG("Invalid option - this should not happen (previous parsing)!");
	return -1;
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

	if (hc->tx_cwnd < hc->tx_ssthresh) {
		if (*maxincr > 0 && ++hc->tx_packets_acked == 2) {
			hc->tx_cwnd += 1;
			*maxincr    -= 1;
			hc->tx_packets_acked = 0;
		}
	} else if (++hc->tx_packets_acked >= hc->tx_cwnd) {
			hc->tx_cwnd += 1;
			hc->tx_packets_acked = 0;
	}
	/*
	 * FIXME: RTT is sampled several times per acknowledgment (for each
	 * entry in the Ack Vector), instead of once per Ack (as in TCP SACK).
	 * This causes the RTT to be over-estimated, since the older entries
	 * in the Ack Vector have earlier sending times.
	 * The cleanest solution is to not use the ccid2s_sent field at all
	 * and instead use DCCP timestamps: requires changes in other places.
	 */
	ccid2_rtt_estimator(sk, ccid2_time_stamp - seqp->ccid2s_sent);
}

static void ccid2_congestion_event(struct sock *sk, struct ccid2_seq *seqp)
{
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);

	if ((s32)(seqp->ccid2s_sent - hc->tx_last_cong) < 0) {
		ccid2_pr_debug("Multiple losses in an RTT---treating as one\n");
		return;
	}

	hc->tx_last_cong = ccid2_time_stamp;

	hc->tx_cwnd      = hc->tx_cwnd / 2 ? : 1U;
	hc->tx_ssthresh  = max(hc->tx_cwnd, 2U);

	/* Avoid spurious timeouts resulting from Ack Ratio > cwnd */
	if (dccp_sk(sk)->dccps_l_ack_ratio > hc->tx_cwnd)
		ccid2_change_l_ack_ratio(sk, hc->tx_cwnd);
}

static void ccid2_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hc = ccid2_hc_tx_sk(sk);
	const bool sender_was_blocked = ccid2_cwnd_network_limited(hc);
	u64 ackno, seqno;
	struct ccid2_seq *seqp;
	unsigned char *vector;
	unsigned char veclen;
	int offset = 0;
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

				ccid2_change_l_ack_ratio(sk, 2 * dp->dccps_l_ack_ratio);
			}
		}
	}

	/* check forward path congestion */
	/* still didn't send out new data packets */
	if (hc->tx_seqh == hc->tx_seqt)
		return;

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_ACK:
	case DCCP_PKT_DATAACK:
		break;
	default:
		return;
	}

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
	while ((offset = ccid2_ackvector(sk, skb, offset,
					 &vector, &veclen)) != -1) {
		/* go through this ack vector */
		while (veclen--) {
			const u8 rl = *vector & DCCP_ACKVEC_LEN_MASK;
			u64 ackno_end_rl = SUB48(ackno, rl);

			ccid2_pr_debug("ackvec start:%llu end:%llu\n",
				       (unsigned long long)ackno,
				       (unsigned long long)ackno_end_rl);
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
				const u8 state = *vector &
						 DCCP_ACKVEC_STATE_MASK;

				/* new packet received or marked */
				if (state != DCCP_ACKVEC_STATE_NOT_RECEIVED &&
				    !seqp->ccid2s_acked) {
					if (state ==
					    DCCP_ACKVEC_STATE_ECN_MARKED) {
						ccid2_congestion_event(sk,
								       seqp);
					} else
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
			vector++;
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

	/* check if incoming Acks allow pending packets to be sent */
	if (sender_was_blocked && !ccid2_cwnd_network_limited(hc))
		tasklet_schedule(&dccp_sk(sk)->dccps_xmitlet);
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

	/* Make sure that Ack Ratio is enabled and within bounds. */
	max_ratio = DIV_ROUND_UP(hc->tx_cwnd, 2);
	if (dp->dccps_l_ack_ratio == 0 || dp->dccps_l_ack_ratio > max_ratio)
		dp->dccps_l_ack_ratio = max_ratio;

	/* XXX init ~ to window size... */
	if (ccid2_hc_tx_alloc_seq(hc))
		return -ENOMEM;

	hc->tx_rto	 = DCCP_TIMEOUT_INIT;
	hc->tx_rpdupack  = -1;
	hc->tx_last_cong = ccid2_time_stamp;
	setup_timer(&hc->tx_rtotimer, ccid2_hc_tx_rto_expire,
			(unsigned long)sk);
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
}

static void ccid2_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_rx_sock *hc = ccid2_hc_rx_sk(sk);

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_DATA:
	case DCCP_PKT_DATAACK:
		hc->rx_data++;
		if (hc->rx_data >= dp->dccps_r_ack_ratio) {
			dccp_send_ack(sk);
			hc->rx_data = 0;
		}
		break;
	}
}

struct ccid_operations ccid2_ops = {
	.ccid_id		= DCCPC_CCID2,
	.ccid_name		= "TCP-like",
	.ccid_hc_tx_obj_size	= sizeof(struct ccid2_hc_tx_sock),
	.ccid_hc_tx_init	= ccid2_hc_tx_init,
	.ccid_hc_tx_exit	= ccid2_hc_tx_exit,
	.ccid_hc_tx_send_packet	= ccid2_hc_tx_send_packet,
	.ccid_hc_tx_packet_sent	= ccid2_hc_tx_packet_sent,
	.ccid_hc_tx_packet_recv	= ccid2_hc_tx_packet_recv,
	.ccid_hc_rx_obj_size	= sizeof(struct ccid2_hc_rx_sock),
	.ccid_hc_rx_packet_recv	= ccid2_hc_rx_packet_recv,
};

#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
module_param(ccid2_debug, bool, 0644);
MODULE_PARM_DESC(ccid2_debug, "Enable CCID-2 debug messages");
#endif
