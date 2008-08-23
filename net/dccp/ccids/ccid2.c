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

#include "../ccid.h"
#include "../dccp.h"
#include "ccid2.h"


#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
static int ccid2_debug;
#define ccid2_pr_debug(format, a...)	DCCP_PR_DEBUG(ccid2_debug, format, ##a)

static void ccid2_hc_tx_check_sanity(const struct ccid2_hc_tx_sock *hctx)
{
	int len = 0;
	int pipe = 0;
	struct ccid2_seq *seqp = hctx->ccid2hctx_seqh;

	/* there is data in the chain */
	if (seqp != hctx->ccid2hctx_seqt) {
		seqp = seqp->ccid2s_prev;
		len++;
		if (!seqp->ccid2s_acked)
			pipe++;

		while (seqp != hctx->ccid2hctx_seqt) {
			struct ccid2_seq *prev = seqp->ccid2s_prev;

			len++;
			if (!prev->ccid2s_acked)
				pipe++;

			/* packets are sent sequentially */
			BUG_ON(dccp_delta_seqno(seqp->ccid2s_seq,
						prev->ccid2s_seq ) >= 0);
			BUG_ON(time_before(seqp->ccid2s_sent,
					   prev->ccid2s_sent));

			seqp = prev;
		}
	}

	BUG_ON(pipe != hctx->ccid2hctx_pipe);
	ccid2_pr_debug("len of chain=%d\n", len);

	do {
		seqp = seqp->ccid2s_prev;
		len++;
	} while (seqp != hctx->ccid2hctx_seqh);

	ccid2_pr_debug("total len=%d\n", len);
	BUG_ON(len != hctx->ccid2hctx_seqbufc * CCID2_SEQBUF_LEN);
}
#else
#define ccid2_pr_debug(format, a...)
#define ccid2_hc_tx_check_sanity(hctx)
#endif

static int ccid2_hc_tx_alloc_seq(struct ccid2_hc_tx_sock *hctx)
{
	struct ccid2_seq *seqp;
	int i;

	/* check if we have space to preserve the pointer to the buffer */
	if (hctx->ccid2hctx_seqbufc >= (sizeof(hctx->ccid2hctx_seqbuf) /
					sizeof(struct ccid2_seq*)))
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
	if (hctx->ccid2hctx_seqbufc == 0)
		hctx->ccid2hctx_seqh = hctx->ccid2hctx_seqt = seqp;
	else {
		/* link the existing list with the one we just created */
		hctx->ccid2hctx_seqh->ccid2s_next = seqp;
		seqp->ccid2s_prev = hctx->ccid2hctx_seqh;

		hctx->ccid2hctx_seqt->ccid2s_prev = &seqp[CCID2_SEQBUF_LEN - 1];
		seqp[CCID2_SEQBUF_LEN - 1].ccid2s_next = hctx->ccid2hctx_seqt;
	}

	/* store the original pointer to the buffer so we can free it */
	hctx->ccid2hctx_seqbuf[hctx->ccid2hctx_seqbufc] = seqp;
	hctx->ccid2hctx_seqbufc++;

	return 0;
}

static int ccid2_hc_tx_send_packet(struct sock *sk, struct sk_buff *skb)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (hctx->ccid2hctx_pipe < hctx->ccid2hctx_cwnd)
		return 0;

	return 1; /* XXX CCID should dequeue when ready instead of polling */
}

static void ccid2_change_l_ack_ratio(struct sock *sk, u32 val)
{
	struct dccp_sock *dp = dccp_sk(sk);
	u32 max_ratio = DIV_ROUND_UP(ccid2_hc_tx_sk(sk)->ccid2hctx_cwnd, 2);

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
	if (val > 0xFFFF)		/* RFC 4340, 11.3 */
		val = 0xFFFF;

	if (val == dp->dccps_l_ack_ratio)
		return;

	ccid2_pr_debug("changing local ack ratio to %u\n", val);
	dp->dccps_l_ack_ratio = val;
}

static void ccid2_change_srtt(struct ccid2_hc_tx_sock *hctx, long val)
{
	ccid2_pr_debug("change SRTT to %ld\n", val);
	hctx->ccid2hctx_srtt = val;
}

static void ccid2_start_rto_timer(struct sock *sk);

static void ccid2_hc_tx_rto_expire(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	long s;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &hctx->ccid2hctx_rtotimer,
			       jiffies + HZ / 5);
		goto out;
	}

	ccid2_pr_debug("RTO_EXPIRE\n");

	ccid2_hc_tx_check_sanity(hctx);

	/* back-off timer */
	hctx->ccid2hctx_rto <<= 1;

	s = hctx->ccid2hctx_rto / HZ;
	if (s > 60)
		hctx->ccid2hctx_rto = 60 * HZ;

	ccid2_start_rto_timer(sk);

	/* adjust pipe, cwnd etc */
	hctx->ccid2hctx_ssthresh = hctx->ccid2hctx_cwnd / 2;
	if (hctx->ccid2hctx_ssthresh < 2)
		hctx->ccid2hctx_ssthresh = 2;
	hctx->ccid2hctx_cwnd	 = 1;
	hctx->ccid2hctx_pipe	 = 0;

	/* clear state about stuff we sent */
	hctx->ccid2hctx_seqt = hctx->ccid2hctx_seqh;
	hctx->ccid2hctx_packets_acked = 0;

	/* clear ack ratio state. */
	hctx->ccid2hctx_rpseq	 = 0;
	hctx->ccid2hctx_rpdupack = -1;
	ccid2_change_l_ack_ratio(sk, 1);
	ccid2_hc_tx_check_sanity(hctx);
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void ccid2_start_rto_timer(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	ccid2_pr_debug("setting RTO timeout=%ld\n", hctx->ccid2hctx_rto);

	BUG_ON(timer_pending(&hctx->ccid2hctx_rtotimer));
	sk_reset_timer(sk, &hctx->ccid2hctx_rtotimer,
		       jiffies + hctx->ccid2hctx_rto);
}

static void ccid2_hc_tx_packet_sent(struct sock *sk, int more, unsigned int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	struct ccid2_seq *next;

	hctx->ccid2hctx_pipe++;

	hctx->ccid2hctx_seqh->ccid2s_seq   = dp->dccps_gss;
	hctx->ccid2hctx_seqh->ccid2s_acked = 0;
	hctx->ccid2hctx_seqh->ccid2s_sent  = jiffies;

	next = hctx->ccid2hctx_seqh->ccid2s_next;
	/* check if we need to alloc more space */
	if (next == hctx->ccid2hctx_seqt) {
		if (ccid2_hc_tx_alloc_seq(hctx)) {
			DCCP_CRIT("packet history - out of memory!");
			/* FIXME: find a more graceful way to bail out */
			return;
		}
		next = hctx->ccid2hctx_seqh->ccid2s_next;
		BUG_ON(next == hctx->ccid2hctx_seqt);
	}
	hctx->ccid2hctx_seqh = next;

	ccid2_pr_debug("cwnd=%d pipe=%d\n", hctx->ccid2hctx_cwnd,
		       hctx->ccid2hctx_pipe);

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
	hctx->ccid2hctx_arsent++;
	/* We had an ack loss in this window... */
	if (hctx->ccid2hctx_ackloss) {
		if (hctx->ccid2hctx_arsent >= hctx->ccid2hctx_cwnd) {
			hctx->ccid2hctx_arsent	= 0;
			hctx->ccid2hctx_ackloss	= 0;
		}
	} else {
		/* No acks lost up to now... */
		/* decrease ack ratio if enough packets were sent */
		if (dp->dccps_l_ack_ratio > 1) {
			/* XXX don't calculate denominator each time */
			int denom = dp->dccps_l_ack_ratio * dp->dccps_l_ack_ratio -
				    dp->dccps_l_ack_ratio;

			denom = hctx->ccid2hctx_cwnd * hctx->ccid2hctx_cwnd / denom;

			if (hctx->ccid2hctx_arsent >= denom) {
				ccid2_change_l_ack_ratio(sk, dp->dccps_l_ack_ratio - 1);
				hctx->ccid2hctx_arsent = 0;
			}
		} else {
			/* we can't increase ack ratio further [1] */
			hctx->ccid2hctx_arsent = 0; /* or maybe set it to cwnd*/
		}
	}
#endif

	/* setup RTO timer */
	if (!timer_pending(&hctx->ccid2hctx_rtotimer))
		ccid2_start_rto_timer(sk);

#ifdef CONFIG_IP_DCCP_CCID2_DEBUG
	do {
		struct ccid2_seq *seqp = hctx->ccid2hctx_seqt;

		while (seqp != hctx->ccid2hctx_seqh) {
			ccid2_pr_debug("out seq=%llu acked=%d time=%lu\n",
				       (unsigned long long)seqp->ccid2s_seq,
				       seqp->ccid2s_acked, seqp->ccid2s_sent);
			seqp = seqp->ccid2s_next;
		}
	} while (0);
	ccid2_pr_debug("=========\n");
	ccid2_hc_tx_check_sanity(hctx);
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

static void ccid2_hc_tx_kill_rto_timer(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	sk_stop_timer(sk, &hctx->ccid2hctx_rtotimer);
	ccid2_pr_debug("deleted RTO timer\n");
}

static inline void ccid2_new_ack(struct sock *sk,
				 struct ccid2_seq *seqp,
				 unsigned int *maxincr)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (hctx->ccid2hctx_cwnd < hctx->ccid2hctx_ssthresh) {
		if (*maxincr > 0 && ++hctx->ccid2hctx_packets_acked == 2) {
			hctx->ccid2hctx_cwnd += 1;
			*maxincr	     -= 1;
			hctx->ccid2hctx_packets_acked = 0;
		}
	} else if (++hctx->ccid2hctx_packets_acked >= hctx->ccid2hctx_cwnd) {
			hctx->ccid2hctx_cwnd += 1;
			hctx->ccid2hctx_packets_acked = 0;
	}

	/* update RTO */
	if (hctx->ccid2hctx_srtt == -1 ||
	    time_after(jiffies, hctx->ccid2hctx_lastrtt + hctx->ccid2hctx_srtt)) {
		unsigned long r = (long)jiffies - (long)seqp->ccid2s_sent;
		int s;

		/* first measurement */
		if (hctx->ccid2hctx_srtt == -1) {
			ccid2_pr_debug("R: %lu Time=%lu seq=%llu\n",
				       r, jiffies,
				       (unsigned long long)seqp->ccid2s_seq);
			ccid2_change_srtt(hctx, r);
			hctx->ccid2hctx_rttvar = r >> 1;
		} else {
			/* RTTVAR */
			long tmp = hctx->ccid2hctx_srtt - r;
			long srtt;

			if (tmp < 0)
				tmp *= -1;

			tmp >>= 2;
			hctx->ccid2hctx_rttvar *= 3;
			hctx->ccid2hctx_rttvar >>= 2;
			hctx->ccid2hctx_rttvar += tmp;

			/* SRTT */
			srtt = hctx->ccid2hctx_srtt;
			srtt *= 7;
			srtt >>= 3;
			tmp = r >> 3;
			srtt += tmp;
			ccid2_change_srtt(hctx, srtt);
		}
		s = hctx->ccid2hctx_rttvar << 2;
		/* clock granularity is 1 when based on jiffies */
		if (!s)
			s = 1;
		hctx->ccid2hctx_rto = hctx->ccid2hctx_srtt + s;

		/* must be at least a second */
		s = hctx->ccid2hctx_rto / HZ;
		/* DCCP doesn't require this [but I like it cuz my code sux] */
#if 1
		if (s < 1)
			hctx->ccid2hctx_rto = HZ;
#endif
		/* max 60 seconds */
		if (s > 60)
			hctx->ccid2hctx_rto = HZ * 60;

		hctx->ccid2hctx_lastrtt = jiffies;

		ccid2_pr_debug("srtt: %ld rttvar: %ld rto: %ld (HZ=%d) R=%lu\n",
			       hctx->ccid2hctx_srtt, hctx->ccid2hctx_rttvar,
			       hctx->ccid2hctx_rto, HZ, r);
	}

	/* we got a new ack, so re-start RTO timer */
	ccid2_hc_tx_kill_rto_timer(sk);
	ccid2_start_rto_timer(sk);
}

static void ccid2_hc_tx_dec_pipe(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (hctx->ccid2hctx_pipe == 0)
		DCCP_BUG("pipe == 0");
	else
		hctx->ccid2hctx_pipe--;

	if (hctx->ccid2hctx_pipe == 0)
		ccid2_hc_tx_kill_rto_timer(sk);
}

static void ccid2_congestion_event(struct sock *sk, struct ccid2_seq *seqp)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);

	if (time_before(seqp->ccid2s_sent, hctx->ccid2hctx_last_cong)) {
		ccid2_pr_debug("Multiple losses in an RTT---treating as one\n");
		return;
	}

	hctx->ccid2hctx_last_cong = jiffies;

	hctx->ccid2hctx_cwnd     = hctx->ccid2hctx_cwnd / 2 ? : 1U;
	hctx->ccid2hctx_ssthresh = max(hctx->ccid2hctx_cwnd, 2U);

	/* Avoid spurious timeouts resulting from Ack Ratio > cwnd */
	if (dccp_sk(sk)->dccps_l_ack_ratio > hctx->ccid2hctx_cwnd)
		ccid2_change_l_ack_ratio(sk, hctx->ccid2hctx_cwnd);
}

static void ccid2_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	u64 ackno, seqno;
	struct ccid2_seq *seqp;
	unsigned char *vector;
	unsigned char veclen;
	int offset = 0;
	int done = 0;
	unsigned int maxincr = 0;

	ccid2_hc_tx_check_sanity(hctx);
	/* check reverse path congestion */
	seqno = DCCP_SKB_CB(skb)->dccpd_seq;

	/* XXX this whole "algorithm" is broken.  Need to fix it to keep track
	 * of the seqnos of the dupacks so that rpseq and rpdupack are correct
	 * -sorbo.
	 */
	/* need to bootstrap */
	if (hctx->ccid2hctx_rpdupack == -1) {
		hctx->ccid2hctx_rpdupack = 0;
		hctx->ccid2hctx_rpseq = seqno;
	} else {
		/* check if packet is consecutive */
		if (dccp_delta_seqno(hctx->ccid2hctx_rpseq, seqno) == 1)
			hctx->ccid2hctx_rpseq = seqno;
		/* it's a later packet */
		else if (after48(seqno, hctx->ccid2hctx_rpseq)) {
			hctx->ccid2hctx_rpdupack++;

			/* check if we got enough dupacks */
			if (hctx->ccid2hctx_rpdupack >= NUMDUPACK) {
				hctx->ccid2hctx_rpdupack = -1; /* XXX lame */
				hctx->ccid2hctx_rpseq = 0;

				ccid2_change_l_ack_ratio(sk, 2 * dp->dccps_l_ack_ratio);
			}
		}
	}

	/* check forward path congestion */
	/* still didn't send out new data packets */
	if (hctx->ccid2hctx_seqh == hctx->ccid2hctx_seqt)
		return;

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_ACK:
	case DCCP_PKT_DATAACK:
		break;
	default:
		return;
	}

	ackno = DCCP_SKB_CB(skb)->dccpd_ack_seq;
	if (after48(ackno, hctx->ccid2hctx_high_ack))
		hctx->ccid2hctx_high_ack = ackno;

	seqp = hctx->ccid2hctx_seqt;
	while (before48(seqp->ccid2s_seq, ackno)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hctx->ccid2hctx_seqh) {
			seqp = hctx->ccid2hctx_seqh->ccid2s_prev;
			break;
		}
	}

	/*
	 * In slow-start, cwnd can increase up to a maximum of Ack Ratio/2
	 * packets per acknowledgement. Rounding up avoids that cwnd is not
	 * advanced when Ack Ratio is 1 and gives a slight edge otherwise.
	 */
	if (hctx->ccid2hctx_cwnd < hctx->ccid2hctx_ssthresh)
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
				if (seqp == hctx->ccid2hctx_seqt) {
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
					ccid2_hc_tx_dec_pipe(sk);
				}
				if (seqp == hctx->ccid2hctx_seqt) {
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
	seqp = hctx->ccid2hctx_seqt;
	while (before48(seqp->ccid2s_seq, hctx->ccid2hctx_high_ack)) {
		seqp = seqp->ccid2s_next;
		if (seqp == hctx->ccid2hctx_seqh) {
			seqp = hctx->ccid2hctx_seqh->ccid2s_prev;
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
		if (seqp == hctx->ccid2hctx_seqt)
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
				ccid2_hc_tx_dec_pipe(sk);
			}
			if (seqp == hctx->ccid2hctx_seqt)
				break;
			seqp = seqp->ccid2s_prev;
		}

		hctx->ccid2hctx_seqt = last_acked;
	}

	/* trim acked packets in tail */
	while (hctx->ccid2hctx_seqt != hctx->ccid2hctx_seqh) {
		if (!hctx->ccid2hctx_seqt->ccid2s_acked)
			break;

		hctx->ccid2hctx_seqt = hctx->ccid2hctx_seqt->ccid2s_next;
	}

	ccid2_hc_tx_check_sanity(hctx);
}

static int ccid2_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid_priv(ccid);
	struct dccp_sock *dp = dccp_sk(sk);
	u32 max_ratio;

	/* RFC 4341, 5: initialise ssthresh to arbitrarily high (max) value */
	hctx->ccid2hctx_ssthresh  = ~0U;

	/*
	 * RFC 4341, 5: "The cwnd parameter is initialized to at most four
	 * packets for new connections, following the rules from [RFC3390]".
	 * We need to convert the bytes of RFC3390 into the packets of RFC 4341.
	 */
	hctx->ccid2hctx_cwnd = clamp(4380U / dp->dccps_mss_cache, 2U, 4U);

	/* Make sure that Ack Ratio is enabled and within bounds. */
	max_ratio = DIV_ROUND_UP(hctx->ccid2hctx_cwnd, 2);
	if (dp->dccps_l_ack_ratio == 0 || dp->dccps_l_ack_ratio > max_ratio)
		dp->dccps_l_ack_ratio = max_ratio;

	/* XXX init ~ to window size... */
	if (ccid2_hc_tx_alloc_seq(hctx))
		return -ENOMEM;

	hctx->ccid2hctx_rto	 = 3 * HZ;
	ccid2_change_srtt(hctx, -1);
	hctx->ccid2hctx_rttvar	 = -1;
	hctx->ccid2hctx_rpdupack = -1;
	hctx->ccid2hctx_last_cong = jiffies;
	setup_timer(&hctx->ccid2hctx_rtotimer, ccid2_hc_tx_rto_expire,
			(unsigned long)sk);

	ccid2_hc_tx_check_sanity(hctx);
	return 0;
}

static void ccid2_hc_tx_exit(struct sock *sk)
{
	struct ccid2_hc_tx_sock *hctx = ccid2_hc_tx_sk(sk);
	int i;

	ccid2_hc_tx_kill_rto_timer(sk);

	for (i = 0; i < hctx->ccid2hctx_seqbufc; i++)
		kfree(hctx->ccid2hctx_seqbuf[i]);
	hctx->ccid2hctx_seqbufc = 0;
}

static void ccid2_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid2_hc_rx_sock *hcrx = ccid2_hc_rx_sk(sk);

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_DATA:
	case DCCP_PKT_DATAACK:
		hcrx->ccid2hcrx_data++;
		if (hcrx->ccid2hcrx_data >= dp->dccps_r_ack_ratio) {
			dccp_send_ack(sk);
			hcrx->ccid2hcrx_data = 0;
		}
		break;
	}
}

static struct ccid_operations ccid2 = {
	.ccid_id		= DCCPC_CCID2,
	.ccid_name		= "TCP-like",
	.ccid_owner		= THIS_MODULE,
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
