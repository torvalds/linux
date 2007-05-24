/*
 *  net/dccp/ccids/ccid3.c
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-6 Ian McDonald <ian.mcdonald@jandi.co.nz>
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
#include "../ccid.h"
#include "../dccp.h"
#include "lib/packet_history.h"
#include "lib/loss_interval.h"
#include "lib/tfrc.h"
#include "ccid3.h"

#ifdef CONFIG_IP_DCCP_CCID3_DEBUG
static int ccid3_debug;
#define ccid3_pr_debug(format, a...)	DCCP_PR_DEBUG(ccid3_debug, format, ##a)
#else
#define ccid3_pr_debug(format, a...)
#endif

static struct dccp_tx_hist *ccid3_tx_hist;
static struct dccp_rx_hist *ccid3_rx_hist;
static struct dccp_li_hist *ccid3_li_hist;

/*
 *	Transmitter Half-Connection Routines
 */
#ifdef CONFIG_IP_DCCP_CCID3_DEBUG
static const char *ccid3_tx_state_name(enum ccid3_hc_tx_states state)
{
	static char *ccid3_state_names[] = {
	[TFRC_SSTATE_NO_SENT]  = "NO_SENT",
	[TFRC_SSTATE_NO_FBACK] = "NO_FBACK",
	[TFRC_SSTATE_FBACK]    = "FBACK",
	[TFRC_SSTATE_TERM]     = "TERM",
	};

	return ccid3_state_names[state];
}
#endif

static void ccid3_hc_tx_set_state(struct sock *sk,
				  enum ccid3_hc_tx_states state)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	enum ccid3_hc_tx_states oldstate = hctx->ccid3hctx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_tx_state_name(oldstate),
		       ccid3_tx_state_name(state));
	WARN_ON(state == oldstate);
	hctx->ccid3hctx_state = state;
}

/*
 * Compute the initial sending rate X_init according to RFC 3390:
 *	w_init   =    min(4 * MSS, max(2 * MSS, 4380 bytes))
 *	X_init   =    w_init / RTT
 * For consistency with other parts of the code, X_init is scaled by 2^6.
 */
static inline u64 rfc3390_initial_rate(struct sock *sk)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const __u32 w_init = min(4 * dp->dccps_mss_cache,
				 max(2 * dp->dccps_mss_cache, 4380U));

	return scaled_div(w_init << 6, ccid3_hc_tx_sk(sk)->ccid3hctx_rtt);
}

/*
 * Recalculate t_ipi and delta (should be called whenever X changes)
 */
static inline void ccid3_update_send_interval(struct ccid3_hc_tx_sock *hctx)
{
	/* Calculate new t_ipi = s / X_inst (X_inst is in 64 * bytes/second) */
	hctx->ccid3hctx_t_ipi = scaled_div32(((u64)hctx->ccid3hctx_s) << 6,
					     hctx->ccid3hctx_x);

	/* Calculate new delta by delta = min(t_ipi / 2, t_gran / 2) */
	hctx->ccid3hctx_delta = min_t(u32, hctx->ccid3hctx_t_ipi / 2,
					   TFRC_OPSYS_HALF_TIME_GRAN);

	ccid3_pr_debug("t_ipi=%u, delta=%u, s=%u, X=%u\n",
		       hctx->ccid3hctx_t_ipi, hctx->ccid3hctx_delta,
		       hctx->ccid3hctx_s, (unsigned)(hctx->ccid3hctx_x >> 6));

}
/*
 * Update X by
 *    If (p > 0)
 *       X_calc = calcX(s, R, p);
 *       X = max(min(X_calc, 2 * X_recv), s / t_mbi);
 *    Else
 *       If (now - tld >= R)
 *          X = max(min(2 * X, 2 * X_recv), s / R);
 *          tld = now;
 *
 * Note: X and X_recv are both stored in units of 64 * bytes/second, to support
 *       fine-grained resolution of sending rates. This requires scaling by 2^6
 *       throughout the code. Only X_calc is unscaled (in bytes/second).
 *
 */
static void ccid3_hc_tx_update_x(struct sock *sk, struct timeval *now)

{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	__u64 min_rate = 2 * hctx->ccid3hctx_x_recv;
	const  __u64 old_x = hctx->ccid3hctx_x;

	/*
	 * Handle IDLE periods: do not reduce below RFC3390 initial sending rate
	 * when idling [RFC 4342, 5.1]. See also draft-ietf-dccp-rfc3448bis.
	 * For consistency with X and X_recv, min_rate is also scaled by 2^6.
	 */
	if (unlikely(hctx->ccid3hctx_idle)) {
		min_rate = rfc3390_initial_rate(sk);
		min_rate = max(min_rate, 2 * hctx->ccid3hctx_x_recv);
	}

	if (hctx->ccid3hctx_p > 0) {

		hctx->ccid3hctx_x = min(((__u64)hctx->ccid3hctx_x_calc) << 6,
					min_rate);
		hctx->ccid3hctx_x = max(hctx->ccid3hctx_x,
					(((__u64)hctx->ccid3hctx_s) << 6) /
								TFRC_T_MBI);

	} else if (timeval_delta(now, &hctx->ccid3hctx_t_ld) -
			(suseconds_t)hctx->ccid3hctx_rtt >= 0) {

		hctx->ccid3hctx_x =
			max(min(2 * hctx->ccid3hctx_x, min_rate),
			    scaled_div(((__u64)hctx->ccid3hctx_s) << 6,
				       hctx->ccid3hctx_rtt));
		hctx->ccid3hctx_t_ld = *now;
	}

	if (hctx->ccid3hctx_x != old_x) {
		ccid3_pr_debug("X_prev=%u, X_now=%u, X_calc=%u, "
			       "X_recv=%u\n", (unsigned)(old_x >> 6),
			       (unsigned)(hctx->ccid3hctx_x >> 6),
			       hctx->ccid3hctx_x_calc,
			       (unsigned)(hctx->ccid3hctx_x_recv >> 6));

		ccid3_update_send_interval(hctx);
	}
}

/*
 *	Track the mean packet size `s' (cf. RFC 4342, 5.3 and  RFC 3448, 4.1)
 *	@len: DCCP packet payload size in bytes
 */
static inline void ccid3_hc_tx_update_s(struct ccid3_hc_tx_sock *hctx, int len)
{
	const u16 old_s = hctx->ccid3hctx_s;

	hctx->ccid3hctx_s = old_s == 0 ? len : (9 * old_s + len) / 10;

	if (hctx->ccid3hctx_s != old_s)
		ccid3_update_send_interval(hctx);
}

/*
 *	Update Window Counter using the algorithm from [RFC 4342, 8.1].
 *	The algorithm is not applicable if RTT < 4 microseconds.
 */
static inline void ccid3_hc_tx_update_win_count(struct ccid3_hc_tx_sock *hctx,
						struct timeval *now)
{
	suseconds_t delta;
	u32 quarter_rtts;

	if (unlikely(hctx->ccid3hctx_rtt < 4))	/* avoid divide-by-zero */
		return;

	delta = timeval_delta(now, &hctx->ccid3hctx_t_last_win_count);
	DCCP_BUG_ON(delta < 0);

	quarter_rtts = (u32)delta / (hctx->ccid3hctx_rtt / 4);

	if (quarter_rtts > 0) {
		hctx->ccid3hctx_t_last_win_count = *now;
		hctx->ccid3hctx_last_win_count	+= min_t(u32, quarter_rtts, 5);
		hctx->ccid3hctx_last_win_count	&= 0xF;		/* mod 16 */

		ccid3_pr_debug("now at %#X\n", hctx->ccid3hctx_last_win_count);
	}
}

static void ccid3_hc_tx_no_feedback_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct timeval now;
	unsigned long t_nfb = USEC_PER_SEC / 5;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		/* XXX: set some sensible MIB */
		goto restart_timer;
	}

	ccid3_pr_debug("%s(%p, state=%s) - entry \n", dccp_role(sk), sk,
		       ccid3_tx_state_name(hctx->ccid3hctx_state));

	hctx->ccid3hctx_idle = 1;

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_FBACK:
		/* RFC 3448, 4.4: Halve send rate directly */
		hctx->ccid3hctx_x = max(hctx->ccid3hctx_x / 2,
					(((__u64)hctx->ccid3hctx_s) << 6) /
								    TFRC_T_MBI);

		ccid3_pr_debug("%s(%p, state=%s), updated tx rate to %u "
			       "bytes/s\n", dccp_role(sk), sk,
			       ccid3_tx_state_name(hctx->ccid3hctx_state),
			       (unsigned)(hctx->ccid3hctx_x >> 6));
		/* The value of R is still undefined and so we can not recompute
		 * the timout value. Keep initial value as per [RFC 4342, 5]. */
		t_nfb = TFRC_INITIAL_TIMEOUT;
		ccid3_update_send_interval(hctx);
		break;
	case TFRC_SSTATE_FBACK:
		/*
		 *  Modify the cached value of X_recv [RFC 3448, 4.4]
		 *
		 *  If (p == 0 || X_calc > 2 * X_recv)
		 *    X_recv = max(X_recv / 2, s / (2 * t_mbi));
		 *  Else
		 *    X_recv = X_calc / 4;
		 *
		 *  Note that X_recv is scaled by 2^6 while X_calc is not
		 */
		BUG_ON(hctx->ccid3hctx_p && !hctx->ccid3hctx_x_calc);

		if (hctx->ccid3hctx_p == 0 ||
		    (hctx->ccid3hctx_x_calc > (hctx->ccid3hctx_x_recv >> 5))) {

			hctx->ccid3hctx_x_recv =
				max(hctx->ccid3hctx_x_recv / 2,
				    (((__u64)hctx->ccid3hctx_s) << 6) /
							      (2 * TFRC_T_MBI));

			if (hctx->ccid3hctx_p == 0)
				dccp_timestamp(sk, &now);
		} else {
			hctx->ccid3hctx_x_recv = hctx->ccid3hctx_x_calc;
			hctx->ccid3hctx_x_recv <<= 4;
		}
		/* Now recalculate X [RFC 3448, 4.3, step (4)] */
		ccid3_hc_tx_update_x(sk, &now);
		/*
		 * Schedule no feedback timer to expire in
		 * max(t_RTO, 2 * s/X)  =  max(t_RTO, 2 * t_ipi)
		 * See comments in packet_recv() regarding the value of t_RTO.
		 */
		t_nfb = max(hctx->ccid3hctx_t_rto, 2 * hctx->ccid3hctx_t_ipi);
		break;
	case TFRC_SSTATE_NO_SENT:
		DCCP_BUG("%s(%p) - Illegal state NO_SENT", dccp_role(sk), sk);
		/* fall through */
	case TFRC_SSTATE_TERM:
		goto out;
	}

restart_timer:
	sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer,
			   jiffies + usecs_to_jiffies(t_nfb));
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 * returns
 *   > 0: delay (in msecs) that should pass before actually sending
 *   = 0: can send immediately
 *   < 0: error condition; do not send packet
 */
static int ccid3_hc_tx_send_packet(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct timeval now;
	suseconds_t delay;

	BUG_ON(hctx == NULL);

	/*
	 * This function is called only for Data and DataAck packets. Sending
	 * zero-sized Data(Ack)s is theoretically possible, but for congestion
	 * control this case is pathological - ignore it.
	 */
	if (unlikely(skb->len == 0))
		return -EBADMSG;

	dccp_timestamp(sk, &now);

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer,
			       (jiffies +
				usecs_to_jiffies(TFRC_INITIAL_TIMEOUT)));
		hctx->ccid3hctx_last_win_count	 = 0;
		hctx->ccid3hctx_t_last_win_count = now;

		/* Set t_0 for initial packet */
		hctx->ccid3hctx_t_nom = now;

		hctx->ccid3hctx_s = skb->len;

		/*
		 * Use initial RTT sample when available: recommended by erratum
		 * to RFC 4342. This implements the initialisation procedure of
		 * draft rfc3448bis, section 4.2. Remember, X is scaled by 2^6.
		 */
		if (dp->dccps_syn_rtt) {
			ccid3_pr_debug("SYN RTT = %uus\n", dp->dccps_syn_rtt);
			hctx->ccid3hctx_rtt  = dp->dccps_syn_rtt;
			hctx->ccid3hctx_x    = rfc3390_initial_rate(sk);
			hctx->ccid3hctx_t_ld = now;
		} else {
			/* Sender does not have RTT sample: X = MSS/second */
			hctx->ccid3hctx_x = dp->dccps_mss_cache;
			hctx->ccid3hctx_x <<= 6;
		}
		ccid3_update_send_interval(hctx);

		ccid3_hc_tx_set_state(sk, TFRC_SSTATE_NO_FBACK);
		break;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		delay = timeval_delta(&hctx->ccid3hctx_t_nom, &now);
		ccid3_pr_debug("delay=%ld\n", (long)delay);
		/*
		 *	Scheduling of packet transmissions [RFC 3448, 4.6]
		 *
		 * if (t_now > t_nom - delta)
		 *       // send the packet now
		 * else
		 *       // send the packet in (t_nom - t_now) milliseconds.
		 */
		if (delay - (suseconds_t)hctx->ccid3hctx_delta >= 0)
			return delay / 1000L;

		ccid3_hc_tx_update_win_count(hctx, &now);
		break;
	case TFRC_SSTATE_TERM:
		DCCP_BUG("%s(%p) - Illegal state TERM", dccp_role(sk), sk);
		return -EINVAL;
	}

	/* prepare to send now (add options etc.) */
	dp->dccps_hc_tx_insert_options = 1;
	DCCP_SKB_CB(skb)->dccpd_ccval = hctx->ccid3hctx_last_win_count;
	hctx->ccid3hctx_idle = 0;

	/* set the nominal send time for the next following packet */
	timeval_add_usecs(&hctx->ccid3hctx_t_nom, hctx->ccid3hctx_t_ipi);

	return 0;
}

static void ccid3_hc_tx_packet_sent(struct sock *sk, int more,
				    unsigned int len)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct timeval now;
	struct dccp_tx_hist_entry *packet;

	BUG_ON(hctx == NULL);

	ccid3_hc_tx_update_s(hctx, len);

	packet = dccp_tx_hist_entry_new(ccid3_tx_hist, GFP_ATOMIC);
	if (unlikely(packet == NULL)) {
		DCCP_CRIT("packet history - out of memory!");
		return;
	}
	dccp_tx_hist_add_entry(&hctx->ccid3hctx_hist, packet);

	dccp_timestamp(sk, &now);
	packet->dccphtx_tstamp = now;
	packet->dccphtx_seqno  = dccp_sk(sk)->dccps_gss;
	packet->dccphtx_rtt    = hctx->ccid3hctx_rtt;
	packet->dccphtx_sent   = 1;
}

static void ccid3_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct ccid3_options_received *opt_recv;
	struct dccp_tx_hist_entry *packet;
	struct timeval now;
	unsigned long t_nfb;
	u32 pinv, r_sample;

	BUG_ON(hctx == NULL);

	/* we are only interested in ACKs */
	if (!(DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK ||
	      DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_DATAACK))
		return;

	opt_recv = &hctx->ccid3hctx_options_received;

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		/* get packet from history to look up t_recvdata */
		packet = dccp_tx_hist_find_entry(&hctx->ccid3hctx_hist,
					      DCCP_SKB_CB(skb)->dccpd_ack_seq);
		if (unlikely(packet == NULL)) {
			DCCP_WARN("%s(%p), seqno %llu(%s) doesn't exist "
				  "in history!\n",  dccp_role(sk), sk,
			    (unsigned long long)DCCP_SKB_CB(skb)->dccpd_ack_seq,
				dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
			return;
		}

		/* Update receive rate in units of 64 * bytes/second */
		hctx->ccid3hctx_x_recv = opt_recv->ccid3or_receive_rate;
		hctx->ccid3hctx_x_recv <<= 6;

		/* Update loss event rate */
		pinv = opt_recv->ccid3or_loss_event_rate;
		if (pinv == ~0U || pinv == 0)	       /* see RFC 4342, 8.5   */
			hctx->ccid3hctx_p = 0;
		else				       /* can not exceed 100% */
			hctx->ccid3hctx_p = 1000000 / pinv;

		dccp_timestamp(sk, &now);

		/*
		 * Calculate new round trip sample as per [RFC 3448, 4.3] by
		 *	R_sample  =  (now - t_recvdata) - t_elapsed
		 */
		r_sample = dccp_sample_rtt(sk, &now, &packet->dccphtx_tstamp);

		/*
		 * Update RTT estimate by
		 * If (No feedback recv)
		 *    R = R_sample;
		 * Else
		 *    R = q * R + (1 - q) * R_sample;
		 *
		 * q is a constant, RFC 3448 recomments 0.9
		 */
		if (hctx->ccid3hctx_state == TFRC_SSTATE_NO_FBACK) {
			/*
			 * Larger Initial Windows [RFC 4342, sec. 5]
			 */
			hctx->ccid3hctx_rtt  = r_sample;
			hctx->ccid3hctx_x    = rfc3390_initial_rate(sk);
			hctx->ccid3hctx_t_ld = now;

			ccid3_update_send_interval(hctx);

			ccid3_pr_debug("%s(%p), s=%u, MSS=%u, "
				       "R_sample=%uus, X=%u\n", dccp_role(sk),
				       sk, hctx->ccid3hctx_s,
				       dccp_sk(sk)->dccps_mss_cache, r_sample,
				       (unsigned)(hctx->ccid3hctx_x >> 6));

			ccid3_hc_tx_set_state(sk, TFRC_SSTATE_FBACK);
		} else {
			hctx->ccid3hctx_rtt = (9 * hctx->ccid3hctx_rtt +
						   r_sample) / 10;

			/* Update sending rate (step 4 of [RFC 3448, 4.3]) */
			if (hctx->ccid3hctx_p > 0)
				hctx->ccid3hctx_x_calc =
					tfrc_calc_x(hctx->ccid3hctx_s,
						    hctx->ccid3hctx_rtt,
						    hctx->ccid3hctx_p);
			ccid3_hc_tx_update_x(sk, &now);

			ccid3_pr_debug("%s(%p), RTT=%uus (sample=%uus), s=%u, "
				       "p=%u, X_calc=%u, X_recv=%u, X=%u\n",
				       dccp_role(sk),
				       sk, hctx->ccid3hctx_rtt, r_sample,
				       hctx->ccid3hctx_s, hctx->ccid3hctx_p,
				       hctx->ccid3hctx_x_calc,
				       (unsigned)(hctx->ccid3hctx_x_recv >> 6),
				       (unsigned)(hctx->ccid3hctx_x >> 6));
		}

		/* unschedule no feedback timer */
		sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

		/* remove all packets older than the one acked from history */
		dccp_tx_hist_purge_older(ccid3_tx_hist,
					 &hctx->ccid3hctx_hist, packet);
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
		hctx->ccid3hctx_t_rto = max_t(u32, 4 * hctx->ccid3hctx_rtt,
						   CONFIG_IP_DCCP_CCID3_RTO *
						   (USEC_PER_SEC/1000));
		/*
		 * Schedule no feedback timer to expire in
		 * max(t_RTO, 2 * s/X)  =  max(t_RTO, 2 * t_ipi)
		 */
		t_nfb = max(hctx->ccid3hctx_t_rto, 2 * hctx->ccid3hctx_t_ipi);

		ccid3_pr_debug("%s(%p), Scheduled no feedback timer to "
			       "expire in %lu jiffies (%luus)\n",
			       dccp_role(sk),
			       sk, usecs_to_jiffies(t_nfb), t_nfb);

		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer,
				   jiffies + usecs_to_jiffies(t_nfb));

		/* set idle flag */
		hctx->ccid3hctx_idle = 1;
		break;
	case TFRC_SSTATE_NO_SENT:	/* fall through */
	case TFRC_SSTATE_TERM:		/* ignore feedback when closing */
		break;
	}
}

static int ccid3_hc_tx_parse_options(struct sock *sk, unsigned char option,
				     unsigned char len, u16 idx,
				     unsigned char *value)
{
	int rc = 0;
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	struct ccid3_options_received *opt_recv;

	BUG_ON(hctx == NULL);

	opt_recv = &hctx->ccid3hctx_options_received;

	if (opt_recv->ccid3or_seqno != dp->dccps_gsr) {
		opt_recv->ccid3or_seqno		     = dp->dccps_gsr;
		opt_recv->ccid3or_loss_event_rate    = ~0;
		opt_recv->ccid3or_loss_intervals_idx = 0;
		opt_recv->ccid3or_loss_intervals_len = 0;
		opt_recv->ccid3or_receive_rate	     = 0;
	}

	switch (option) {
	case TFRC_OPT_LOSS_EVENT_RATE:
		if (unlikely(len != 4)) {
			DCCP_WARN("%s(%p), invalid len %d "
				  "for TFRC_OPT_LOSS_EVENT_RATE\n",
				  dccp_role(sk), sk, len);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_loss_event_rate =
						ntohl(*(__be32 *)value);
			ccid3_pr_debug("%s(%p), LOSS_EVENT_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_loss_event_rate);
		}
		break;
	case TFRC_OPT_LOSS_INTERVALS:
		opt_recv->ccid3or_loss_intervals_idx = idx;
		opt_recv->ccid3or_loss_intervals_len = len;
		ccid3_pr_debug("%s(%p), LOSS_INTERVALS=(%u, %u)\n",
			       dccp_role(sk), sk,
			       opt_recv->ccid3or_loss_intervals_idx,
			       opt_recv->ccid3or_loss_intervals_len);
		break;
	case TFRC_OPT_RECEIVE_RATE:
		if (unlikely(len != 4)) {
			DCCP_WARN("%s(%p), invalid len %d "
				  "for TFRC_OPT_RECEIVE_RATE\n",
				  dccp_role(sk), sk, len);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_receive_rate =
						ntohl(*(__be32 *)value);
			ccid3_pr_debug("%s(%p), RECEIVE_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_receive_rate);
		}
		break;
	}

	return rc;
}

static int ccid3_hc_tx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid3_hc_tx_sock *hctx = ccid_priv(ccid);

	hctx->ccid3hctx_s     = 0;
	hctx->ccid3hctx_rtt   = 0;
	hctx->ccid3hctx_state = TFRC_SSTATE_NO_SENT;
	INIT_LIST_HEAD(&hctx->ccid3hctx_hist);

	hctx->ccid3hctx_no_feedback_timer.function =
				ccid3_hc_tx_no_feedback_timer;
	hctx->ccid3hctx_no_feedback_timer.data     = (unsigned long)sk;
	init_timer(&hctx->ccid3hctx_no_feedback_timer);

	return 0;
}

static void ccid3_hc_tx_exit(struct sock *sk)
{
	struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);

	BUG_ON(hctx == NULL);

	ccid3_hc_tx_set_state(sk, TFRC_SSTATE_TERM);
	sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

	/* Empty packet history */
	dccp_tx_hist_purge(ccid3_tx_hist, &hctx->ccid3hctx_hist);
}

static void ccid3_hc_tx_get_info(struct sock *sk, struct tcp_info *info)
{
	const struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);

	/* Listen socks doesn't have a private CCID block */
	if (sk->sk_state == DCCP_LISTEN)
		return;

	BUG_ON(hctx == NULL);

	info->tcpi_rto = hctx->ccid3hctx_t_rto;
	info->tcpi_rtt = hctx->ccid3hctx_rtt;
}

static int ccid3_hc_tx_getsockopt(struct sock *sk, const int optname, int len,
				  u32 __user *optval, int __user *optlen)
{
	const struct ccid3_hc_tx_sock *hctx = ccid3_hc_tx_sk(sk);
	const void *val;

	/* Listen socks doesn't have a private CCID block */
	if (sk->sk_state == DCCP_LISTEN)
		return -EINVAL;

	switch (optname) {
	case DCCP_SOCKOPT_CCID_TX_INFO:
		if (len < sizeof(hctx->ccid3hctx_tfrc))
			return -EINVAL;
		len = sizeof(hctx->ccid3hctx_tfrc);
		val = &hctx->ccid3hctx_tfrc;
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
#ifdef CONFIG_IP_DCCP_CCID3_DEBUG
static const char *ccid3_rx_state_name(enum ccid3_hc_rx_states state)
{
	static char *ccid3_rx_state_names[] = {
	[TFRC_RSTATE_NO_DATA] = "NO_DATA",
	[TFRC_RSTATE_DATA]    = "DATA",
	[TFRC_RSTATE_TERM]    = "TERM",
	};

	return ccid3_rx_state_names[state];
}
#endif

static void ccid3_hc_rx_set_state(struct sock *sk,
				  enum ccid3_hc_rx_states state)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	enum ccid3_hc_rx_states oldstate = hcrx->ccid3hcrx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_rx_state_name(oldstate),
		       ccid3_rx_state_name(state));
	WARN_ON(state == oldstate);
	hcrx->ccid3hcrx_state = state;
}

static inline void ccid3_hc_rx_update_s(struct ccid3_hc_rx_sock *hcrx, int len)
{
	if (unlikely(len == 0))	/* don't update on empty packets (e.g. ACKs) */
		ccid3_pr_debug("Packet payload length is 0 - not updating\n");
	else
		hcrx->ccid3hcrx_s = hcrx->ccid3hcrx_s == 0 ? len :
				    (9 * hcrx->ccid3hcrx_s + len) / 10;
}

static void ccid3_hc_rx_send_feedback(struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_rx_hist_entry *packet;
	struct timeval now;
	suseconds_t delta;

	ccid3_pr_debug("%s(%p) - entry \n", dccp_role(sk), sk);

	dccp_timestamp(sk, &now);

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		hcrx->ccid3hcrx_x_recv = 0;
		break;
	case TFRC_RSTATE_DATA:
		delta = timeval_delta(&now,
				      &hcrx->ccid3hcrx_tstamp_last_feedback);
		DCCP_BUG_ON(delta < 0);
		hcrx->ccid3hcrx_x_recv =
			scaled_div32(hcrx->ccid3hcrx_bytes_recv, delta);
		break;
	case TFRC_RSTATE_TERM:
		DCCP_BUG("%s(%p) - Illegal state TERM", dccp_role(sk), sk);
		return;
	}

	packet = dccp_rx_hist_find_data_packet(&hcrx->ccid3hcrx_hist);
	if (unlikely(packet == NULL)) {
		DCCP_WARN("%s(%p), no data packet in history!\n",
			  dccp_role(sk), sk);
		return;
	}

	hcrx->ccid3hcrx_tstamp_last_feedback = now;
	hcrx->ccid3hcrx_ccval_last_counter   = packet->dccphrx_ccval;
	hcrx->ccid3hcrx_bytes_recv	     = 0;

	/* Elapsed time information [RFC 4340, 13.2] in units of 10 * usecs */
	delta = timeval_delta(&now, &packet->dccphrx_tstamp);
	DCCP_BUG_ON(delta < 0);
	hcrx->ccid3hcrx_elapsed_time = delta / 10;

	if (hcrx->ccid3hcrx_p == 0)
		hcrx->ccid3hcrx_pinv = ~0U;	/* see RFC 4342, 8.5 */
	else if (hcrx->ccid3hcrx_p > 1000000) {
		DCCP_WARN("p (%u) > 100%%\n", hcrx->ccid3hcrx_p);
		hcrx->ccid3hcrx_pinv = 1;	/* use 100% in this case */
	} else
		hcrx->ccid3hcrx_pinv = 1000000 / hcrx->ccid3hcrx_p;

	dp->dccps_hc_rx_insert_options = 1;
	dccp_send_ack(sk);
}

static int ccid3_hc_rx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	__be32 x_recv, pinv;

	BUG_ON(hcrx == NULL);

	if (!(sk->sk_state == DCCP_OPEN || sk->sk_state == DCCP_PARTOPEN))
		return 0;

	DCCP_SKB_CB(skb)->dccpd_ccval = hcrx->ccid3hcrx_ccval_last_counter;

	if (dccp_packet_without_ack(skb))
		return 0;

	x_recv = htonl(hcrx->ccid3hcrx_x_recv);
	pinv   = htonl(hcrx->ccid3hcrx_pinv);

	if ((hcrx->ccid3hcrx_elapsed_time != 0 &&
	     dccp_insert_option_elapsed_time(sk, skb,
					     hcrx->ccid3hcrx_elapsed_time)) ||
	    dccp_insert_option_timestamp(sk, skb) ||
	    dccp_insert_option(sk, skb, TFRC_OPT_LOSS_EVENT_RATE,
			       &pinv, sizeof(pinv)) ||
	    dccp_insert_option(sk, skb, TFRC_OPT_RECEIVE_RATE,
			       &x_recv, sizeof(x_recv)))
		return -1;

	return 0;
}

/* calculate first loss interval
 *
 * returns estimated loss interval in usecs */

static u32 ccid3_hc_rx_calc_first_li(struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	struct dccp_rx_hist_entry *entry, *next, *tail = NULL;
	u32 x_recv, p;
	suseconds_t rtt, delta;
	struct timeval tstamp = { 0, };
	int interval = 0;
	int win_count = 0;
	int step = 0;
	u64 fval;

	list_for_each_entry_safe(entry, next, &hcrx->ccid3hcrx_hist,
				 dccphrx_node) {
		if (dccp_rx_hist_entry_data_packet(entry)) {
			tail = entry;

			switch (step) {
			case 0:
				tstamp	  = entry->dccphrx_tstamp;
				win_count = entry->dccphrx_ccval;
				step = 1;
				break;
			case 1:
				interval = win_count - entry->dccphrx_ccval;
				if (interval < 0)
					interval += TFRC_WIN_COUNT_LIMIT;
				if (interval > 4)
					goto found;
				break;
			}
		}
	}

	if (unlikely(step == 0)) {
		DCCP_WARN("%s(%p), packet history has no data packets!\n",
			  dccp_role(sk), sk);
		return ~0;
	}

	if (unlikely(interval == 0)) {
		DCCP_WARN("%s(%p), Could not find a win_count interval > 0."
			  "Defaulting to 1\n", dccp_role(sk), sk);
		interval = 1;
	}
found:
	if (!tail) {
		DCCP_CRIT("tail is null\n");
		return ~0;
	}

	delta = timeval_delta(&tstamp, &tail->dccphrx_tstamp);
	DCCP_BUG_ON(delta < 0);

	rtt = delta * 4 / interval;
	ccid3_pr_debug("%s(%p), approximated RTT to %dus\n",
		       dccp_role(sk), sk, (int)rtt);

	/*
	 * Determine the length of the first loss interval via inverse lookup.
	 * Assume that X_recv can be computed by the throughput equation
	 *		    s
	 *	X_recv = --------
	 *		 R * fval
	 * Find some p such that f(p) = fval; return 1/p [RFC 3448, 6.3.1].
	 */
	if (rtt == 0) {			/* would result in divide-by-zero */
		DCCP_WARN("RTT==0\n");
		return ~0;
	}

	dccp_timestamp(sk, &tstamp);
	delta = timeval_delta(&tstamp, &hcrx->ccid3hcrx_tstamp_last_feedback);
	DCCP_BUG_ON(delta <= 0);

	x_recv = scaled_div32(hcrx->ccid3hcrx_bytes_recv, delta);
	if (x_recv == 0) {		/* would also trigger divide-by-zero */
		DCCP_WARN("X_recv==0\n");
		if ((x_recv = hcrx->ccid3hcrx_x_recv) == 0) {
			DCCP_BUG("stored value of X_recv is zero");
			return ~0;
		}
	}

	fval = scaled_div(hcrx->ccid3hcrx_s, rtt);
	fval = scaled_div32(fval, x_recv);
	p = tfrc_calc_x_reverse_lookup(fval);

	ccid3_pr_debug("%s(%p), receive rate=%u bytes/s, implied "
		       "loss rate=%u\n", dccp_role(sk), sk, x_recv, p);

	if (p == 0)
		return ~0;
	else
		return 1000000 / p;
}

static void ccid3_hc_rx_update_li(struct sock *sk, u64 seq_loss, u8 win_loss)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	struct dccp_li_hist_entry *head;
	u64 seq_temp;

	if (list_empty(&hcrx->ccid3hcrx_li_hist)) {
		if (!dccp_li_hist_interval_new(ccid3_li_hist,
		   &hcrx->ccid3hcrx_li_hist, seq_loss, win_loss))
			return;

		head = list_entry(hcrx->ccid3hcrx_li_hist.next,
		   struct dccp_li_hist_entry, dccplih_node);
		head->dccplih_interval = ccid3_hc_rx_calc_first_li(sk);
	} else {
		struct dccp_li_hist_entry *entry;
		struct list_head *tail;

		head = list_entry(hcrx->ccid3hcrx_li_hist.next,
		   struct dccp_li_hist_entry, dccplih_node);
		/* FIXME win count check removed as was wrong */
		/* should make this check with receive history */
		/* and compare there as per section 10.2 of RFC4342 */

		/* new loss event detected */
		/* calculate last interval length */
		seq_temp = dccp_delta_seqno(head->dccplih_seqno, seq_loss);
		entry = dccp_li_hist_entry_new(ccid3_li_hist, GFP_ATOMIC);

		if (entry == NULL) {
			DCCP_BUG("out of memory - can not allocate entry");
			return;
		}

		list_add(&entry->dccplih_node, &hcrx->ccid3hcrx_li_hist);

		tail = hcrx->ccid3hcrx_li_hist.prev;
		list_del(tail);
		kmem_cache_free(ccid3_li_hist->dccplih_slab, tail);

		/* Create the newest interval */
		entry->dccplih_seqno = seq_loss;
		entry->dccplih_interval = seq_temp;
		entry->dccplih_win_count = win_loss;
	}
}

static int ccid3_hc_rx_detect_loss(struct sock *sk,
				    struct dccp_rx_hist_entry *packet)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	struct dccp_rx_hist_entry *rx_hist =
				dccp_rx_hist_head(&hcrx->ccid3hcrx_hist);
	u64 seqno = packet->dccphrx_seqno;
	u64 tmp_seqno;
	int loss = 0;
	u8 ccval;


	tmp_seqno = hcrx->ccid3hcrx_seqno_nonloss;

	if (!rx_hist ||
	   follows48(packet->dccphrx_seqno, hcrx->ccid3hcrx_seqno_nonloss)) {
		hcrx->ccid3hcrx_seqno_nonloss = seqno;
		hcrx->ccid3hcrx_ccval_nonloss = packet->dccphrx_ccval;
		goto detect_out;
	}


	while (dccp_delta_seqno(hcrx->ccid3hcrx_seqno_nonloss, seqno)
	   > TFRC_RECV_NUM_LATE_LOSS) {
		loss = 1;
		ccid3_hc_rx_update_li(sk, hcrx->ccid3hcrx_seqno_nonloss,
		   hcrx->ccid3hcrx_ccval_nonloss);
		tmp_seqno = hcrx->ccid3hcrx_seqno_nonloss;
		dccp_inc_seqno(&tmp_seqno);
		hcrx->ccid3hcrx_seqno_nonloss = tmp_seqno;
		dccp_inc_seqno(&tmp_seqno);
		while (dccp_rx_hist_find_entry(&hcrx->ccid3hcrx_hist,
		   tmp_seqno, &ccval)) {
			hcrx->ccid3hcrx_seqno_nonloss = tmp_seqno;
			hcrx->ccid3hcrx_ccval_nonloss = ccval;
			dccp_inc_seqno(&tmp_seqno);
		}
	}

	/* FIXME - this code could be simplified with above while */
	/* but works at moment */
	if (follows48(packet->dccphrx_seqno, hcrx->ccid3hcrx_seqno_nonloss)) {
		hcrx->ccid3hcrx_seqno_nonloss = seqno;
		hcrx->ccid3hcrx_ccval_nonloss = packet->dccphrx_ccval;
	}

detect_out:
	dccp_rx_hist_add_packet(ccid3_rx_hist, &hcrx->ccid3hcrx_hist,
		   &hcrx->ccid3hcrx_li_hist, packet,
		   hcrx->ccid3hcrx_seqno_nonloss);
	return loss;
}

static void ccid3_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	const struct dccp_options_received *opt_recv;
	struct dccp_rx_hist_entry *packet;
	struct timeval now;
	u32 p_prev, r_sample, rtt_prev;
	int loss, payload_size;

	BUG_ON(hcrx == NULL);

	opt_recv = &dccp_sk(sk)->dccps_options_received;

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_ACK:
		if (hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA)
			return;
	case DCCP_PKT_DATAACK:
		if (opt_recv->dccpor_timestamp_echo == 0)
			break;
		rtt_prev = hcrx->ccid3hcrx_rtt;
		dccp_timestamp(sk, &now);
		r_sample = dccp_sample_rtt(sk, &now, NULL);

		if (hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA)
			hcrx->ccid3hcrx_rtt = r_sample;
		else
			hcrx->ccid3hcrx_rtt = (hcrx->ccid3hcrx_rtt * 9) / 10 +
					      r_sample / 10;

		if (rtt_prev != hcrx->ccid3hcrx_rtt)
			ccid3_pr_debug("%s(%p), New RTT=%uus, elapsed time=%u\n",
				       dccp_role(sk), sk, hcrx->ccid3hcrx_rtt,
				       opt_recv->dccpor_elapsed_time);
		break;
	case DCCP_PKT_DATA:
		break;
	default: /* We're not interested in other packet types, move along */
		return;
	}

	packet = dccp_rx_hist_entry_new(ccid3_rx_hist, sk, opt_recv->dccpor_ndp,
					skb, GFP_ATOMIC);
	if (unlikely(packet == NULL)) {
		DCCP_WARN("%s(%p), Not enough mem to add rx packet "
			  "to history, consider it lost!\n", dccp_role(sk), sk);
		return;
	}

	loss = ccid3_hc_rx_detect_loss(sk, packet);

	if (DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK)
		return;

	payload_size = skb->len - dccp_hdr(skb)->dccph_doff * 4;
	ccid3_hc_rx_update_s(hcrx, payload_size);

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		ccid3_pr_debug("%s(%p, state=%s), skb=%p, sending initial "
			       "feedback\n", dccp_role(sk), sk,
			       dccp_state_name(sk->sk_state), skb);
		ccid3_hc_rx_send_feedback(sk);
		ccid3_hc_rx_set_state(sk, TFRC_RSTATE_DATA);
		return;
	case TFRC_RSTATE_DATA:
		hcrx->ccid3hcrx_bytes_recv += payload_size;
		if (loss)
			break;

		dccp_timestamp(sk, &now);
		if ((timeval_delta(&now, &hcrx->ccid3hcrx_tstamp_last_ack) -
		     (suseconds_t)hcrx->ccid3hcrx_rtt) >= 0) {
			hcrx->ccid3hcrx_tstamp_last_ack = now;
			ccid3_hc_rx_send_feedback(sk);
		}
		return;
	case TFRC_RSTATE_TERM:
		DCCP_BUG("%s(%p) - Illegal state TERM", dccp_role(sk), sk);
		return;
	}

	/* Dealing with packet loss */
	ccid3_pr_debug("%s(%p, state=%s), data loss! Reacting...\n",
		       dccp_role(sk), sk, dccp_state_name(sk->sk_state));

	p_prev = hcrx->ccid3hcrx_p;

	/* Calculate loss event rate */
	if (!list_empty(&hcrx->ccid3hcrx_li_hist)) {
		u32 i_mean = dccp_li_hist_calc_i_mean(&hcrx->ccid3hcrx_li_hist);

		/* Scaling up by 1000000 as fixed decimal */
		if (i_mean != 0)
			hcrx->ccid3hcrx_p = 1000000 / i_mean;
	} else
		DCCP_BUG("empty loss history");

	if (hcrx->ccid3hcrx_p > p_prev) {
		ccid3_hc_rx_send_feedback(sk);
		return;
	}
}

static int ccid3_hc_rx_init(struct ccid *ccid, struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid_priv(ccid);

	ccid3_pr_debug("entry\n");

	hcrx->ccid3hcrx_state = TFRC_RSTATE_NO_DATA;
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_hist);
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_li_hist);
	dccp_timestamp(sk, &hcrx->ccid3hcrx_tstamp_last_ack);
	hcrx->ccid3hcrx_tstamp_last_feedback = hcrx->ccid3hcrx_tstamp_last_ack;
	hcrx->ccid3hcrx_s   = 0;
	hcrx->ccid3hcrx_rtt = 0;
	return 0;
}

static void ccid3_hc_rx_exit(struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);

	BUG_ON(hcrx == NULL);

	ccid3_hc_rx_set_state(sk, TFRC_RSTATE_TERM);

	/* Empty packet history */
	dccp_rx_hist_purge(ccid3_rx_hist, &hcrx->ccid3hcrx_hist);

	/* Empty loss interval history */
	dccp_li_hist_purge(ccid3_li_hist, &hcrx->ccid3hcrx_li_hist);
}

static void ccid3_hc_rx_get_info(struct sock *sk, struct tcp_info *info)
{
	const struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);

	/* Listen socks doesn't have a private CCID block */
	if (sk->sk_state == DCCP_LISTEN)
		return;

	BUG_ON(hcrx == NULL);

	info->tcpi_ca_state = hcrx->ccid3hcrx_state;
	info->tcpi_options  |= TCPI_OPT_TIMESTAMPS;
	info->tcpi_rcv_rtt  = hcrx->ccid3hcrx_rtt;
}

static int ccid3_hc_rx_getsockopt(struct sock *sk, const int optname, int len,
				  u32 __user *optval, int __user *optlen)
{
	const struct ccid3_hc_rx_sock *hcrx = ccid3_hc_rx_sk(sk);
	const void *val;

	/* Listen socks doesn't have a private CCID block */
	if (sk->sk_state == DCCP_LISTEN)
		return -EINVAL;

	switch (optname) {
	case DCCP_SOCKOPT_CCID_RX_INFO:
		if (len < sizeof(hcrx->ccid3hcrx_tfrc))
			return -EINVAL;
		len = sizeof(hcrx->ccid3hcrx_tfrc);
		val = &hcrx->ccid3hcrx_tfrc;
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
	.ccid_name		   = "ccid3",
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
module_param(ccid3_debug, int, 0444);
MODULE_PARM_DESC(ccid3_debug, "Enable debug messages");
#endif

static __init int ccid3_module_init(void)
{
	int rc = -ENOBUFS;

	ccid3_rx_hist = dccp_rx_hist_new("ccid3");
	if (ccid3_rx_hist == NULL)
		goto out;

	ccid3_tx_hist = dccp_tx_hist_new("ccid3");
	if (ccid3_tx_hist == NULL)
		goto out_free_rx;

	ccid3_li_hist = dccp_li_hist_new("ccid3");
	if (ccid3_li_hist == NULL)
		goto out_free_tx;

	rc = ccid_register(&ccid3);
	if (rc != 0)
		goto out_free_loss_interval_history;
out:
	return rc;

out_free_loss_interval_history:
	dccp_li_hist_delete(ccid3_li_hist);
	ccid3_li_hist = NULL;
out_free_tx:
	dccp_tx_hist_delete(ccid3_tx_hist);
	ccid3_tx_hist = NULL;
out_free_rx:
	dccp_rx_hist_delete(ccid3_rx_hist);
	ccid3_rx_hist = NULL;
	goto out;
}
module_init(ccid3_module_init);

static __exit void ccid3_module_exit(void)
{
	ccid_unregister(&ccid3);

	if (ccid3_tx_hist != NULL) {
		dccp_tx_hist_delete(ccid3_tx_hist);
		ccid3_tx_hist = NULL;
	}
	if (ccid3_rx_hist != NULL) {
		dccp_rx_hist_delete(ccid3_rx_hist);
		ccid3_rx_hist = NULL;
	}
	if (ccid3_li_hist != NULL) {
		dccp_li_hist_delete(ccid3_li_hist);
		ccid3_li_hist = NULL;
	}
}
module_exit(ccid3_module_exit);

MODULE_AUTHOR("Ian McDonald <ian.mcdonald@jandi.co.nz>, "
	      "Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC CCID3 CCID");
MODULE_LICENSE("GPL");
MODULE_ALIAS("net-dccp-ccid-3");
