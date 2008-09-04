/*
 *  net/dccp/ccids/ccid3.c
 *
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-7 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *
 *  An implementation of the DCCP protocol
 *
 *  This code has been developed by the University of Waikato WAND
 *  research group. For further information please see http://www.wand.net.nz/
 *
 *  This code also uses code from Lulea University, rereleased as GPL by its
 *  authors:
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  Changes to meet Linux coding standards, to make it meet latest ccid3 draft
 *  and to make it work as a loadable module in the DCCP stack written by
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>.
 *
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
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
#include "../dccp.h"
#include "ccid3.h"

#include <asm/unaligned.h>

#ifdef CONFIG_IP_DCCP_CCID3_DEBUG
static int ccid3_debug;
#define ccid3_pr_debug(format, a...)	DCCP_PR_DEBUG(ccid3_debug, format, ##a)
#else
#define ccid3_pr_debug(format, a...)
#endif

/*
 *	Transmitter Half-Connection Routines
 */

/*
 * Compute the initial sending rate X_init in the manner of RFC 3390:
 *
 *	X_init  =  min(4 * s, max(2 * s, 4380 bytes)) / RTT
 *
 * Note that RFC 3390 uses MSS, RFC 4342 refers to RFC 3390, and rfc3448bis
 * (rev-02) clarifies the use of RFC 3390 with regard to the above formula.
 * For consistency with other parts of the code, X_init is scaled by 2^6.
 */
static inline u64 rfc3390_initial_rate(struct sock *sk)
{
	const struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	const __u32 w_init = clamp_t(__u32, 4380U, 2 * hctx->s, 4 * hctx->s);

	return scaled_div(w_init << 6, hctx->rtt);
}

/**
 * ccid3_update_send_interval  -  Calculate new t_ipi = s / X_inst
 * This respects the granularity of X_inst (64 * bytes/second).
 */
static void ccid3_update_send_interval(struct ccid3_hc_tx_sock *hctx)
{
	hctx->t_ipi = scaled_div32(((u64)hctx->s) << 6, hctx->x);

	ccid3_pr_debug("t_ipi=%u, s=%u, X=%u\n", hctx->t_ipi,
		       hctx->s, (unsigned)(hctx->x >> 6));
}

static u32 ccid3_hc_tx_idle_rtt(struct ccid3_hc_tx_sock *hctx, ktime_t now)
{
	u32 delta = ktime_us_delta(now, hctx->t_last_win_count);

	return delta / hctx->rtt;
}

/**
 * ccid3_hc_tx_update_x  -  Update allowed sending rate X
 * @stamp: most recent time if available - can be left NULL.
 * This function tracks draft rfc3448bis, check there for latest details.
 *
 * Note: X and X_recv are both stored in units of 64 * bytes/second, to support
 *       fine-grained resolution of sending rates. This requires scaling by 2^6
 *       throughout the code. Only X_calc is unscaled (in bytes/second).
 *
 */
static void ccid3_hc_tx_update_x(struct sock *sk, ktime_t *stamp)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	u64 min_rate = 2 * hctx->x_recv;
	const u64 old_x = hctx->x;
	ktime_t now = stamp ? *stamp : ktime_get_real();

	/*
	 * Handle IDLE periods: do not reduce below RFC3390 initial sending rate
	 * when idling [RFC 4342, 5.1]. Definition of idling is from rfc3448bis:
	 * a sender is idle if it has not sent anything over a 2-RTT-period.
	 * For consistency with X and X_recv, min_rate is also scaled by 2^6.
	 */
	if (ccid3_hc_tx_idle_rtt(hctx, now) >= 2) {
		min_rate = rfc3390_initial_rate(sk);
		min_rate = max(min_rate, 2 * hctx->x_recv);
	}

	if (hctx->p > 0) {

		hctx->x = min(((u64)hctx->x_calc) << 6, min_rate);
		hctx->x = max(hctx->x, (((u64)hctx->s) << 6) / TFRC_T_MBI);

	} else if (ktime_us_delta(now, hctx->t_ld) - (s64)hctx->rtt >= 0) {

		hctx->x = min(2 * hctx->x, min_rate);
		hctx->x = max(hctx->x,
			      scaled_div(((u64)hctx->s) << 6, hctx->rtt));
		hctx->t_ld = now;
	}

	if (hctx->x != old_x) {
		ccid3_pr_debug("X_prev=%u, X_now=%u, X_calc=%u, "
			       "X_recv=%u\n", (unsigned)(old_x >> 6),
			       (unsigned)(hctx->x >> 6), hctx->x_calc,
			       (unsigned)(hctx->x_recv >> 6));

		ccid3_update_send_interval(hctx);
	}
}

/*
 *	Track the mean packet size `s' (cf. RFC 4342, 5.3 and  RFC 3448, 4.1)
 *	@len: DCCP packet payload size in bytes
 */
static inline void ccid3_hc_tx_update_s(struct ccid3_hc_tx_sock *hctx, int len)
{
	const u16 old_s = hctx->s;

	hctx->s = tfrc_ewma(hctx->s, len, 9);

	if (hctx->s != old_s)
		ccid3_update_send_interval(hctx);
}

/*
 *	Update Window Counter using the algorithm from [RFC 4342, 8.1].
 *	As elsewhere, RTT > 0 is assumed by using dccp_sample_rtt().
 */
static inline void ccid3_hc_tx_update_win_count(struct ccid3_hc_tx_sock *hctx,
						ktime_t now)
{
	u32 delta = ktime_us_delta(now, hctx->t_last_win_count),
	    quarter_rtts = (4 * delta) / hctx->rtt;

	if (quarter_rtts > 0) {
		hctx->t_last_win_count = now;
		hctx->last_win_count  += min(quarter_rtts, 5U);
		hctx->last_win_count  &= 0xF;		/* mod 16 */
	}
}

static void ccid3_hc_tx_no_feedback_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	unsigned long t_nfb = USEC_PER_SEC / 5;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		/* XXX: set some sensible MIB */
		goto restart_timer;
	}

	ccid3_pr_debug("%s(%p) entry with%s feedback\n", dccp_role(sk), sk,
		       hctx->feedback ? "" : "out");

	/* Ignore and do not restart after leaving the established state */
	if ((1 << sk->sk_state) & ~(DCCPF_OPEN | DCCPF_PARTOPEN))
		goto out;

	/* Reset feedback state to "no feedback received" */
	hctx->feedback = false;

	/*
	 * Determine new allowed sending rate X as per draft rfc3448bis-00, 4.4
	 * RTO is 0 if and only if no feedback has been received yet.
	 */
	if (hctx->t_rto == 0 || hctx->p == 0) {

		/* halve send rate directly */
		hctx->x = max(hctx->x / 2, (((u64)hctx->s) << 6) / TFRC_T_MBI);
		ccid3_update_send_interval(hctx);
	} else {
		/*
		 *  Modify the cached value of X_recv
		 *
		 *  If (X_calc > 2 * X_recv)
		 *    X_recv = max(X_recv / 2, s / (2 * t_mbi));
		 *  Else
		 *    X_recv = X_calc / 4;
		 *
		 *  Note that X_recv is scaled by 2^6 while X_calc is not
		 */
		BUG_ON(hctx->p && !hctx->x_calc);

		if (hctx->x_calc > (hctx->x_recv >> 5))
			hctx->x_recv =
				max(hctx->x_recv / 2,
				    (((__u64)hctx->s) << 6) / (2 * TFRC_T_MBI));
		else {
			hctx->x_recv = hctx->x_calc;
			hctx->x_recv <<= 4;
		}
		ccid3_hc_tx_update_x(sk, NULL);
	}
	ccid3_pr_debug("Reduced X to %llu/64 bytes/sec\n",
			(unsigned long long)hctx->x);

	/*
	 * Set new timeout for the nofeedback timer.
	 * See comments in packet_recv() regarding the value of t_RTO.
	 */
	if (unlikely(hctx->t_rto == 0))		/* no feedback received yet */
		t_nfb = TFRC_INITIAL_TIMEOUT;
	else
		t_nfb = max(hctx->t_rto, 2 * hctx->t_ipi);

restart_timer:
	sk_reset_timer(sk, &hctx->no_feedback_timer,
			   jiffies + usecs_to_jiffies(t_nfb));
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/**
 * ccid3_hc_tx_send_packet  -  Delay-based dequeueing of TX packets
 * @skb: next packet candidate to send on @sk
 * This function uses the convention of ccid_packet_dequeue_eval() and
 * returns a millisecond-delay value between 0 and t_mbi = 64000 msec.
 */
static int ccid3_hc_tx_send_packet(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	ktime_t now = ktime_get_real();
	s64 delay;

	/*
	 * This function is called only for Data and DataAck packets. Sending
	 * zero-sized Data(Ack)s is theoretically possible, but for congestion
	 * control this case is pathological - ignore it.
	 */
	if (unlikely(skb->len == 0))
		return -EBADMSG;

	if (hctx->s == 0) {
		sk_reset_timer(sk, &hctx->no_feedback_timer, (jiffies +
				usecs_to_jiffies(TFRC_INITIAL_TIMEOUT)));
		hctx->last_win_count   = 0;
		hctx->t_last_win_count = now;

		/* Set t_0 for initial packet */
		hctx->t_nom = now;

		hctx->s = skb->len;

		/*
		 * Use initial RTT sample when available: recommended by erratum
		 * to RFC 4342. This implements the initialisation procedure of
		 * draft rfc3448bis, section 4.2. Remember, X is scaled by 2^6.
		 */
		if (dp->dccps_syn_rtt) {
			ccid3_pr_debug("SYN RTT = %uus\n", dp->dccps_syn_rtt);
			hctx->rtt  = dp->dccps_syn_rtt;
			hctx->x    = rfc3390_initial_rate(sk);
			hctx->t_ld = now;
		} else {
			/*
			 * Sender does not have RTT sample:
			 * - set fallback RTT (RFC 4340, 3.4) since a RTT value
			 *   is needed in several parts (e.g.  window counter);
			 * - set sending rate X_pps = 1pps as per RFC 3448, 4.2.
			 */
			hctx->rtt = DCCP_FALLBACK_RTT;
			hctx->x   = hctx->s;
			hctx->x <<= 6;
		}
		ccid3_update_send_interval(hctx);

	} else {
		delay = ktime_us_delta(hctx->t_nom, now);
		ccid3_pr_debug("delay=%ld\n", (long)delay);
		/*
		 *	Scheduling of packet transmissions [RFC 3448, 4.6]
		 *
		 * if (t_now > t_nom - delta)
		 *       // send the packet now
		 * else
		 *       // send the packet in (t_nom - t_now) milliseconds.
		 */
		if (delay >= TFRC_T_DELTA)
			return (u32)delay / USEC_PER_MSEC;

		ccid3_hc_tx_update_win_count(hctx, now);
	}

	/* prepare to send now (add options etc.) */
	dp->dccps_hc_tx_insert_options = 1;
	DCCP_SKB_CB(skb)->dccpd_ccval  = hctx->last_win_count;

	/* set the nominal send time for the next following packet */
	hctx->t_nom = ktime_add_us(hctx->t_nom, hctx->t_ipi);
	return CCID_PACKET_SEND_AT_ONCE;
}

static void ccid3_hc_tx_packet_sent(struct sock *sk, unsigned int len)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);

	ccid3_hc_tx_update_s(hctx, len);

	if (tfrc_tx_hist_add(&hctx->hist, dccp_sk(sk)->dccps_gss))
		DCCP_CRIT("packet history - out of memory!");
}

static void ccid3_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct tfrc_tx_hist_entry *acked;
	ktime_t now;
	unsigned long t_nfb;
	u32 r_sample;

	/* we are only interested in ACKs */
	if (!(DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK ||
	      DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_DATAACK))
		return;
	/*
	 * Locate the acknowledged packet in the TX history.
	 *
	 * Returning "entry not found" here can for instance happen when
	 *  - the host has not sent out anything (e.g. a passive server),
	 *  - the Ack is outdated (packet with higher Ack number was received),
	 *  - it is a bogus Ack (for a packet not sent on this connection).
	 */
	acked = tfrc_tx_hist_find_entry(hctx->hist, dccp_hdr_ack_seq(skb));
	if (acked == NULL)
		return;
	/* For the sake of RTT sampling, ignore/remove all older entries */
	tfrc_tx_hist_purge(&acked->next);

	/* Update the moving average for the RTT estimate (RFC 3448, 4.3) */
	now	  = ktime_get_real();
	r_sample  = dccp_sample_rtt(sk, ktime_us_delta(now, acked->stamp));
	hctx->rtt = tfrc_ewma(hctx->rtt, r_sample, 9);

	/*
	 * Update allowed sending rate X as per draft rfc3448bis-00, 4.2/3
	 */
	if (!hctx->feedback) {
		hctx->feedback = true;

		if (hctx->t_rto == 0) {
			/*
			 * Initial feedback packet: Larger Initial Windows (4.2)
			 */
			hctx->x    = rfc3390_initial_rate(sk);
			hctx->t_ld = now;

			ccid3_update_send_interval(hctx);

			goto done_computing_x;
		} else if (hctx->p == 0) {
			/*
			 * First feedback after nofeedback timer expiry (4.3)
			 */
			goto done_computing_x;
		}
	}

	/* Update sending rate (step 4 of [RFC 3448, 4.3]) */
	if (hctx->p > 0)
		hctx->x_calc = tfrc_calc_x(hctx->s, hctx->rtt, hctx->p);
	ccid3_hc_tx_update_x(sk, &now);

done_computing_x:
	ccid3_pr_debug("%s(%p), RTT=%uus (sample=%uus), s=%u, "
			       "p=%u, X_calc=%u, X_recv=%u, X=%u\n",
			       dccp_role(sk), sk, hctx->rtt, r_sample,
			       hctx->s, hctx->p, hctx->x_calc,
			       (unsigned)(hctx->x_recv >> 6),
			       (unsigned)(hctx->x >> 6));

	/* unschedule no feedback timer */
	sk_stop_timer(sk, &hctx->no_feedback_timer);

	/*
	 * As we have calculated new ipi, delta, t_nom it is possible
	 * that we now can send a packet, so wake up dccp_wait_for_ccid
	 */
	sk->sk_write_space(sk);

	/*
	 * Update timeout interval for the nofeedback timer.
	 * We use a configuration option to increase the lower bound.
	 * This can help avoid triggering the nofeedback timer too
	 * often ('spinning') on LANs with small RTTs.
	 */
	hctx->t_rto = max_t(u32, 4 * hctx->rtt, (CONFIG_IP_DCCP_CCID3_RTO *
						 (USEC_PER_SEC / 1000)));
	/*
	 * Schedule no feedback timer to expire in
	 * max(t_RTO, 2 * s/X)  =  max(t_RTO, 2 * t_ipi)
	 */
	t_nfb = max(hctx->t_rto, 2 * hctx->t_ipi);

	ccid3_pr_debug("%s(%p), Scheduled no feedback timer to "
		       "expire in %lu jiffies (%luus)\n",
		       dccp_role(sk), sk, usecs_to_jiffies(t_nfb), t_nfb);

	sk_reset_timer(sk, &hctx->no_feedback_timer,
			   jiffies + usecs_to_jiffies(t_nfb));
}

static int ccid3_hc_tx_parse_options(struct sock *sk, u8 packet_type,
				     u8 option, u8 *optval, u8 optlen)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	__be32 opt_val;

	switch (option) {
	case TFRC_OPT_RECEIVE_RATE:
	case TFRC_OPT_LOSS_EVENT_RATE:
		/* Must be ignored on Data packets, cf. RFC 4342 8.3 and 8.5 */
		if (packet_type == DCCP_PKT_DATA)
			break;
		if (unlikely(optlen != 4)) {
			DCCP_WARN("%s(%p), invalid len %d for %u\n",
				  dccp_role(sk), sk, optlen, option);
			return -EINVAL;
		}
		opt_val = ntohl(get_unaligned((__be32 *)optval));

		if (option == TFRC_OPT_RECEIVE_RATE) {
			/* Receive Rate is kept in units of 64 bytes/second */
			hctx->x_recv = opt_val;
			hctx->x_recv <<= 6;

			ccid3_pr_debug("%s(%p), RECEIVE_RATE=%u\n",
				       dccp_role(sk), sk, opt_val);
		} else {
			/* Update the fixpoint Loss Event Rate fraction */
			hctx->p = tfrc_invert_loss_event_rate(opt_val);

			ccid3_pr_debug("%s(%p), LOSS_EVENT_RATE=%u\n",
				       dccp_role(sk), sk, opt_val);
		}
	}
	return 0;
}

static int ccid3_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid3_hc_tx_sock *hctx = ccid_priv(ccid);

	hctx->hist  = NULL;
	setup_timer(&hctx->no_feedback_timer,
		    ccid3_hc_tx_no_feedback_timer, (unsigned long)sk);
	return 0;
}

static void ccid3_hc_tx_exit(struct sock *sk)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);

	sk_stop_timer(sk, &hctx->no_feedback_timer);
	tfrc_tx_hist_purge(&hctx->hist);
}

static void ccid3_hc_tx_get_info(struct sock *sk, struct tcp_info *info)
{
	info->tcpi_rto = ccid3_hc_tx_sk(sk)->t_rto;
	info->tcpi_rtt = ccid3_hc_tx_sk(sk)->rtt;
}

static int ccid3_hc_tx_getsockopt(struct sock *sk, const int optname, int len,
				  u32 __user *optval, int __user *optlen)
{
	const struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct tfrc_tx_info tfrc;
	const void *val;

	switch (optname) {
	case DCCP_SOCKOPT_CCID_TX_INFO:
		if (len < sizeof(tfrc))
			return -EINVAL;
		tfrc.tfrctx_x	   = hctx->x;
		tfrc.tfrctx_x_recv = hctx->x_recv;
		tfrc.tfrctx_x_calc = hctx->x_calc;
		tfrc.tfrctx_rtt	   = hctx->rtt;
		tfrc.tfrctx_p	   = hctx->p;
		tfrc.tfrctx_rto	   = hctx->t_rto;
		tfrc.tfrctx_ipi	   = hctx->t_ipi;
		len = sizeof(tfrc);
		val = &tfrc;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen) || copy_to_user(optval, val, len))
		return -EFAULT;

	return 0;
}

/*
 *	Receiver Half-Connection Routines
 */
static void ccid3_hc_rx_send_feedback(struct sock *sk,
				      const struct sk_buff *skb,
				      enum ccid3_fback_type fbtype)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);

	switch (fbtype) {
	case CCID3_FBACK_INITIAL:
		hcrx->x_recv = 0;
		hcrx->p_inverse = ~0U;   /* see RFC 4342, 8.5 */
		break;
	case CCID3_FBACK_PARAM_CHANGE:
		if (unlikely(hcrx->feedback == CCID3_FBACK_NONE)) {
			/*
			 * rfc3448bis-06, 6.3.1: First packet(s) lost or marked
			 * FIXME: in rfc3448bis the receiver returns X_recv=0
			 * here as it normally would in the first feedback packet.
			 * However this is not possible yet, since the code still
			 * uses RFC 3448, i.e.
			 *    If (p > 0)
			 *      Calculate X_calc using the TCP throughput equation.
			 *      X = max(min(X_calc, 2*X_recv), s/t_mbi);
			 * would bring X down to s/t_mbi. That is why we return
			 * X_recv according to rfc3448bis-06 for the moment.
			 */
			u32 s = tfrc_rx_hist_packet_size(&hcrx->hist),
			    rtt = tfrc_rx_hist_rtt(&hcrx->hist);

			hcrx->x_recv = scaled_div32(s, 2 * rtt);
			break;
		}
		/*
		 * When parameters change (new loss or p > p_prev), we do not
		 * have a reliable estimate for R_m of [RFC 3448, 6.2] and so
		 * always check whether at least RTT time units were covered.
		 */
		hcrx->x_recv = tfrc_rx_hist_x_recv(&hcrx->hist, hcrx->x_recv);
		break;
	case CCID3_FBACK_PERIODIC:
		/*
		 * Step (2) of rfc3448bis-06, 6.2:
		 * - if no data packets have been received, just restart timer
		 * - if data packets have been received, re-compute X_recv
		 */
		if (hcrx->hist.bytes_recvd == 0)
			goto prepare_for_next_time;
		hcrx->x_recv = tfrc_rx_hist_x_recv(&hcrx->hist, hcrx->x_recv);
		break;
	default:
		return;
	}

	ccid3_pr_debug("X_recv=%u, 1/p=%u\n", hcrx->x_recv, hcrx->p_inverse);

	dccp_sk(sk)->dccps_hc_rx_insert_options = 1;
	dccp_send_ack(sk);

prepare_for_next_time:
	tfrc_rx_hist_restart_byte_counter(&hcrx->hist);
	hcrx->last_counter = dccp_hdr(skb)->dccph_ccval;
	hcrx->feedback	   = fbtype;
}

static int ccid3_hc_rx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	__be32 x_recv, pinv;

	if (!(sk->sk_state == DCCP_OPEN || sk->sk_state == DCCP_PARTOPEN))
		return 0;

	if (dccp_packet_without_ack(skb))
		return 0;

	x_recv = htonl(hcrx->x_recv);
	pinv   = htonl(hcrx->p_inverse);

	if (dccp_insert_option(sk, skb, TFRC_OPT_LOSS_EVENT_RATE,
			       &pinv, sizeof(pinv)) ||
	    dccp_insert_option(sk, skb, TFRC_OPT_RECEIVE_RATE,
			       &x_recv, sizeof(x_recv)))
		return -1;

	return 0;
}

/** ccid3_first_li  -  Implements [RFC 3448, 6.3.1]
 *
 * Determine the length of the first loss interval via inverse lookup.
 * Assume that X_recv can be computed by the throughput equation
 *		    s
 *	X_recv = --------
 *		 R * fval
 * Find some p such that f(p) = fval; return 1/p (scaled).
 */
static u32 ccid3_first_li(struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	u32 s = tfrc_rx_hist_packet_size(&hcrx->hist),
	    rtt = tfrc_rx_hist_rtt(&hcrx->hist), x_recv, p;
	u64 fval;

	/*
	 * rfc3448bis-06, 6.3.1: First data packet(s) are marked or lost. Set p
	 * to give the equivalent of X_target = s/(2*R). Thus fval = 2 and so p
	 * is about 20.64%. This yields an interval length of 4.84 (rounded up).
	 */
	if (unlikely(hcrx->feedback == CCID3_FBACK_NONE))
		return 5;

	x_recv = tfrc_rx_hist_x_recv(&hcrx->hist, hcrx->x_recv);
	if (x_recv == 0)
		goto failed;

	fval = scaled_div32(scaled_div(s, rtt), x_recv);
	p = tfrc_calc_x_reverse_lookup(fval);

	ccid3_pr_debug("%s(%p), receive rate=%u bytes/s, implied "
		       "loss rate=%u\n", dccp_role(sk), sk, x_recv, p);

	if (p > 0)
		return scaled_div(1, p);
failed:
	return UINT_MAX;
}

static void ccid3_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	enum ccid3_fback_type do_feedback = CCID3_FBACK_NONE;
	const u64 ndp = dccp_sk(sk)->dccps_options_received.dccpor_ndp;
	const bool is_data_packet = dccp_data_packet(skb);

	/*
	 * Perform loss detection and handle pending losses
	 */
	if (tfrc_rx_handle_loss(&hcrx->hist, &hcrx->li_hist,
				skb, ndp, ccid3_first_li, sk)) {
		do_feedback = CCID3_FBACK_PARAM_CHANGE;
		goto done_receiving;
	}

	if (unlikely(hcrx->feedback == CCID3_FBACK_NONE)) {
		if (is_data_packet)
			do_feedback = CCID3_FBACK_INITIAL;
		goto update_records;
	}

	if (tfrc_rx_hist_loss_pending(&hcrx->hist))
		return; /* done receiving */

	/*
	 * Check if the periodic once-per-RTT feedback is due; RFC 4342, 10.3
	 */
	if (is_data_packet &&
	    SUB16(dccp_hdr(skb)->dccph_ccval, hcrx->last_counter) > 3)
		do_feedback = CCID3_FBACK_PERIODIC;

update_records:
	tfrc_rx_hist_add_packet(&hcrx->hist, skb, ndp);

done_receiving:
	if (do_feedback)
		ccid3_hc_rx_send_feedback(sk, skb, do_feedback);
}

static int ccid3_hc_rx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid_priv(ccid);

	tfrc_lh_init(&hcrx->li_hist);
	return tfrc_rx_hist_init(&hcrx->hist, sk);
}

static void ccid3_hc_rx_exit(struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);

	tfrc_rx_hist_purge(&hcrx->hist);
	tfrc_lh_cleanup(&hcrx->li_hist);
}

static void ccid3_hc_rx_get_info(struct sock *sk, struct tcp_info *info)
{
	info->tcpi_options  |= TCPI_OPT_TIMESTAMPS;
	info->tcpi_rcv_rtt  = tfrc_rx_hist_rtt(&ccid3_hc_rx_sk(sk)->hist);
}

static int ccid3_hc_rx_getsockopt(struct sock *sk, const int optname, int len,
				  u32 __user *optval, int __user *optlen)
{
	const struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	struct tfrc_rx_info rx_info;
	const void *val;

	switch (optname) {
	case DCCP_SOCKOPT_CCID_RX_INFO:
		if (len < sizeof(rx_info))
			return -EINVAL;
		rx_info.tfrcrx_x_recv = hcrx->x_recv;
		rx_info.tfrcrx_rtt    = tfrc_rx_hist_rtt(&hcrx->hist);
		rx_info.tfrcrx_p      = tfrc_invert_loss_event_rate(hcrx->p_inverse);
		len = sizeof(rx_info);
		val = &rx_info;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen) || copy_to_user(optval, val, len))
		return -EFAULT;

	return 0;
}

static struct ccid_operations ccid3 = {
	.ccid_id		   = DCCPC_CCID3,
	.ccid_name		   = "TCP-Friendly Rate Control",
	.ccid_owner		   = THIS_MODULE,
	.ccid_hc_tx_obj_size	   = sizeof(struct ccid3_hc_tx_sock),
	.ccid_hc_tx_init	   = ccid3_hc_tx_init,
	.ccid_hc_tx_exit	   = ccid3_hc_tx_exit,
	.ccid_hc_tx_send_packet	   = ccid3_hc_tx_send_packet,
	.ccid_hc_tx_packet_sent	   = ccid3_hc_tx_packet_sent,
	.ccid_hc_tx_packet_recv	   = ccid3_hc_tx_packet_recv,
	.ccid_hc_tx_parse_options  = ccid3_hc_tx_parse_options,
	.ccid_hc_rx_obj_size	   = sizeof(struct ccid3_hc_rx_sock),
	.ccid_hc_rx_init	   = ccid3_hc_rx_init,
	.ccid_hc_rx_exit	   = ccid3_hc_rx_exit,
	.ccid_hc_rx_insert_options = ccid3_hc_rx_insert_options,
	.ccid_hc_rx_packet_recv	   = ccid3_hc_rx_packet_recv,
	.ccid_hc_rx_get_info	   = ccid3_hc_rx_get_info,
	.ccid_hc_tx_get_info	   = ccid3_hc_tx_get_info,
	.ccid_hc_rx_getsockopt	   = ccid3_hc_rx_getsockopt,
	.ccid_hc_tx_getsockopt	   = ccid3_hc_tx_getsockopt,
};

#ifdef CONFIG_IP_DCCP_CCID3_DEBUG
module_param(ccid3_debug, bool, 0644);
MODULE_PARM_DESC(ccid3_debug, "Enable debug messages");
#endif

static __init int ccid3_module_init(void)
{
	struct timespec tp;

	/*
	 * Without a fine-grained clock resolution, RTTs/X_recv are not sampled
	 * correctly and feedback is sent either too early or too late.
	 */
	hrtimer_get_res(CLOCK_MONOTONIC, &tp);
	if (tp.tv_sec || tp.tv_nsec > DCCP_TIME_RESOLUTION * NSEC_PER_USEC) {
		printk(KERN_ERR "%s: Timer too coarse (%ld usec), need %u-usec"
		       " resolution - check your clocksource.\n", __func__,
		       tp.tv_nsec/NSEC_PER_USEC, DCCP_TIME_RESOLUTION);
		return -ESOCKTNOSUPPORT;
	}
	return ccid_register(&ccid3);
}
module_init(ccid3_module_init);

static __exit void ccid3_module_exit(void)
{
	ccid_unregister(&ccid3);
}
module_exit(ccid3_module_exit);

MODULE_AUTHOR("Ian McDonald <ian.mcdonald@jandi.co.nz>, "
	      "Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC CCID3 CCID");
MODULE_LICENSE("GPL");
MODULE_ALIAS("net-dccp-ccid-3");
