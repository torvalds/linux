/*
 *  net/dccp/ccids/ccid3.c
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005 Ian McDonald <iam4@cs.waikato.ac.nz>
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

#include <linux/config.h>
#include "../ccid.h"
#include "../dccp.h"
#include "lib/packet_history.h"
#include "lib/loss_interval.h"
#include "lib/tfrc.h"
#include "ccid3.h"

/*
 * Reason for maths with 10 here is to avoid 32 bit overflow when a is big.
 */
static inline u32 usecs_div(const u32 a, const u32 b)
{
	const u32 tmp = a * (USEC_PER_SEC / 10);
	return b > 20 ? tmp / (b / 10) : tmp;
}

static int ccid3_debug;

#ifdef CCID3_DEBUG
#define ccid3_pr_debug(format, a...) \
	do { if (ccid3_debug) \
		printk(KERN_DEBUG "%s: " format, __FUNCTION__, ##a); \
	} while (0)
#else
#define ccid3_pr_debug(format, a...)
#endif

static struct dccp_tx_hist *ccid3_tx_hist;
static struct dccp_rx_hist *ccid3_rx_hist;
static struct dccp_li_hist *ccid3_li_hist;

static int ccid3_init(struct sock *sk)
{
	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
	return 0;
}

static void ccid3_exit(struct sock *sk)
{
	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
}

/* TFRC sender states */
enum ccid3_hc_tx_states {
       	TFRC_SSTATE_NO_SENT = 1,
	TFRC_SSTATE_NO_FBACK,
	TFRC_SSTATE_FBACK,
	TFRC_SSTATE_TERM,
};

#ifdef CCID3_DEBUG
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

static inline void ccid3_hc_tx_set_state(struct sock *sk,
					 enum ccid3_hc_tx_states state)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	enum ccid3_hc_tx_states oldstate = hctx->ccid3hctx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_tx_state_name(oldstate),
		       ccid3_tx_state_name(state));
	WARN_ON(state == oldstate);
	hctx->ccid3hctx_state = state;
}

/* Calculate new t_ipi (inter packet interval) by t_ipi = s / X_inst */
static inline void ccid3_calc_new_t_ipi(struct ccid3_hc_tx_sock *hctx)
{
	/*
	 * If no feedback spec says t_ipi is 1 second (set elsewhere and then
	 * doubles after every no feedback timer (separate function)
	 */
	if (hctx->ccid3hctx_state != TFRC_SSTATE_NO_FBACK)
		hctx->ccid3hctx_t_ipi = usecs_div(hctx->ccid3hctx_s,
						  hctx->ccid3hctx_x);
}

/* Calculate new delta by delta = min(t_ipi / 2, t_gran / 2) */
static inline void ccid3_calc_new_delta(struct ccid3_hc_tx_sock *hctx)
{
	hctx->ccid3hctx_delta = min_t(u32, hctx->ccid3hctx_t_ipi / 2,
					   TFRC_OPSYS_HALF_TIME_GRAN);
}

/*
 * Update X by
 *    If (p > 0)
 *       x_calc = calcX(s, R, p);
 *       X = max(min(X_calc, 2 * X_recv), s / t_mbi);
 *    Else
 *       If (now - tld >= R)
 *          X = max(min(2 * X, 2 * X_recv), s / R);
 *          tld = now;
 */ 
static void ccid3_hc_tx_update_x(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	/* To avoid large error in calcX */
	if (hctx->ccid3hctx_p >= TFRC_SMALLEST_P) {
		hctx->ccid3hctx_x_calc = tfrc_calc_x(hctx->ccid3hctx_s,
						     hctx->ccid3hctx_rtt,
						     hctx->ccid3hctx_p);
		hctx->ccid3hctx_x = max_t(u32, min_t(u32, hctx->ccid3hctx_x_calc,
							  2 * hctx->ccid3hctx_x_recv),
					       (hctx->ccid3hctx_s /
					        TFRC_MAX_BACK_OFF_TIME));
	} else {
		struct timeval now;

		do_gettimeofday(&now);
	       	if (timeval_delta(&now, &hctx->ccid3hctx_t_ld) >=
		    hctx->ccid3hctx_rtt) {
			hctx->ccid3hctx_x = max_t(u32, min_t(u32, hctx->ccid3hctx_x_recv,
								  hctx->ccid3hctx_x) * 2,
						       usecs_div(hctx->ccid3hctx_s,
							       	 hctx->ccid3hctx_rtt));
			hctx->ccid3hctx_t_ld = now;
		}
	}
}

static void ccid3_hc_tx_no_feedback_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct dccp_sock *dp = dccp_sk(sk);
	unsigned long next_tmout = 0;
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		/* XXX: set some sensible MIB */
		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer,
			       jiffies + HZ / 5);
		goto out;
	}

	ccid3_pr_debug("%s, sk=%p, state=%s\n", dccp_role(sk), sk,
		       ccid3_tx_state_name(hctx->ccid3hctx_state));
	
	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_TERM:
		goto out;
	case TFRC_SSTATE_NO_FBACK:
		/* Halve send rate */
		hctx->ccid3hctx_x /= 2;
		if (hctx->ccid3hctx_x < (hctx->ccid3hctx_s /
					 TFRC_MAX_BACK_OFF_TIME))
			hctx->ccid3hctx_x = (hctx->ccid3hctx_s /
					     TFRC_MAX_BACK_OFF_TIME);

		ccid3_pr_debug("%s, sk=%p, state=%s, updated tx rate to %d "
			       "bytes/s\n",
			       dccp_role(sk), sk,
			       ccid3_tx_state_name(hctx->ccid3hctx_state),
			       hctx->ccid3hctx_x);
		next_tmout = max_t(u32, 2 * usecs_div(hctx->ccid3hctx_s,
						      hctx->ccid3hctx_x),
					TFRC_INITIAL_TIMEOUT);
		/*
		 * FIXME - not sure above calculation is correct. See section
		 * 5 of CCID3 11 should adjust tx_t_ipi and double that to
		 * achieve it really
		 */
		break;
	case TFRC_SSTATE_FBACK:
		/*
		 * Check if IDLE since last timeout and recv rate is less than
		 * 4 packets per RTT
		 */
		if (!hctx->ccid3hctx_idle ||
		    (hctx->ccid3hctx_x_recv >=
		     4 * usecs_div(hctx->ccid3hctx_s, hctx->ccid3hctx_rtt))) {
			ccid3_pr_debug("%s, sk=%p, state=%s, not idle\n",
				       dccp_role(sk), sk,
				       ccid3_tx_state_name(hctx->ccid3hctx_state));
			/* Halve sending rate */

			/*  If (X_calc > 2 * X_recv)
			 *    X_recv = max(X_recv / 2, s / (2 * t_mbi));
			 *  Else
			 *    X_recv = X_calc / 4;
			 */
			BUG_ON(hctx->ccid3hctx_p >= TFRC_SMALLEST_P &&
			       hctx->ccid3hctx_x_calc == 0);

			/* check also if p is zero -> x_calc is infinity? */
			if (hctx->ccid3hctx_p < TFRC_SMALLEST_P ||
			    hctx->ccid3hctx_x_calc > 2 * hctx->ccid3hctx_x_recv)
				hctx->ccid3hctx_x_recv = max_t(u32, hctx->ccid3hctx_x_recv / 2,
								    hctx->ccid3hctx_s / (2 * TFRC_MAX_BACK_OFF_TIME));
			else
				hctx->ccid3hctx_x_recv = hctx->ccid3hctx_x_calc / 4;

			/* Update sending rate */
			ccid3_hc_tx_update_x(sk);
		}
		/*
		 * Schedule no feedback timer to expire in
		 * max(4 * R, 2 * s / X)
		 */
		next_tmout = max_t(u32, hctx->ccid3hctx_t_rto, 
					2 * usecs_div(hctx->ccid3hctx_s,
						      hctx->ccid3hctx_x));
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		goto out;
	}

	sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, 
		      jiffies + max_t(u32, 1, usecs_to_jiffies(next_tmout)));
	hctx->ccid3hctx_idle = 1;
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static int ccid3_hc_tx_send_packet(struct sock *sk,
				   struct sk_buff *skb, int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct dccp_tx_hist_entry *new_packet;
	struct timeval now;
	long delay;
	int rc = -ENOTCONN;

	/* Check if pure ACK or Terminating*/

	/*
	 * XXX: We only call this function for DATA and DATAACK, on, these
	 * packets can have zero length, but why the comment about "pure ACK"?
	 */
	if (hctx == NULL || len == 0 ||
	    hctx->ccid3hctx_state == TFRC_SSTATE_TERM)
		goto out;

	/* See if last packet allocated was not sent */
	new_packet = dccp_tx_hist_head(&hctx->ccid3hctx_hist);
	if (new_packet == NULL || new_packet->dccphtx_sent) {
		new_packet = dccp_tx_hist_entry_new(ccid3_tx_hist,
						    SLAB_ATOMIC);

		rc = -ENOBUFS;
		if (new_packet == NULL) {
			ccid3_pr_debug("%s, sk=%p, not enough mem to add "
				       "to history, send refused\n",
				       dccp_role(sk), sk);
			goto out;
		}

		dccp_tx_hist_add_entry(&hctx->ccid3hctx_hist, new_packet);
	}

	do_gettimeofday(&now);

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		ccid3_pr_debug("%s, sk=%p, first packet(%llu)\n",
			       dccp_role(sk), sk, dp->dccps_gss);

		hctx->ccid3hctx_no_feedback_timer.function = ccid3_hc_tx_no_feedback_timer;
		hctx->ccid3hctx_no_feedback_timer.data     = (unsigned long)sk;
		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer,
			       jiffies + usecs_to_jiffies(TFRC_INITIAL_TIMEOUT));
		hctx->ccid3hctx_last_win_count	 = 0;
		hctx->ccid3hctx_t_last_win_count = now;
		ccid3_hc_tx_set_state(sk, TFRC_SSTATE_NO_FBACK);
		hctx->ccid3hctx_t_ipi = TFRC_INITIAL_TIMEOUT;

		/* Set nominal send time for initial packet */
		hctx->ccid3hctx_t_nom = now;
		timeval_add_usecs(&hctx->ccid3hctx_t_nom,
				  hctx->ccid3hctx_t_ipi);
		ccid3_calc_new_delta(hctx);
		rc = 0;
		break;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		delay = (timeval_delta(&now, &hctx->ccid3hctx_t_nom) -
		         hctx->ccid3hctx_delta);
		ccid3_pr_debug("send_packet delay=%ld\n", delay);
		delay /= -1000;
		/* divide by -1000 is to convert to ms and get sign right */
		rc = delay > 0 ? delay : 0;
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		rc = -EINVAL;
		break;
	}

	/* Can we send? if so add options and add to packet history */
	if (rc == 0)
		new_packet->dccphtx_ccval =
			DCCP_SKB_CB(skb)->dccpd_ccval =
				hctx->ccid3hctx_last_win_count;
out:
	return rc;
}

static void ccid3_hc_tx_packet_sent(struct sock *sk, int more, int len)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct timeval now;

	BUG_ON(hctx == NULL);

	if (hctx->ccid3hctx_state == TFRC_SSTATE_TERM) {
		ccid3_pr_debug("%s, sk=%p, while state is TFRC_SSTATE_TERM!\n",
			       dccp_role(sk), sk);
		return;
	}

	do_gettimeofday(&now);

	/* check if we have sent a data packet */
	if (len > 0) {
		unsigned long quarter_rtt;
		struct dccp_tx_hist_entry *packet;

		packet = dccp_tx_hist_head(&hctx->ccid3hctx_hist);
		if (packet == NULL) {
			printk(KERN_CRIT "%s: packet doesn't exists in "
					 "history!\n", __FUNCTION__);
			return;
		}
		if (packet->dccphtx_sent) {
			printk(KERN_CRIT "%s: no unsent packet in history!\n",
			       __FUNCTION__);
			return;
		}
		packet->dccphtx_tstamp = now;
		packet->dccphtx_seqno  = dp->dccps_gss;
		/*
		 * Check if win_count have changed
		 * Algorithm in "8.1. Window Counter Valuer" in
		 * draft-ietf-dccp-ccid3-11.txt
		 */
		quarter_rtt = timeval_delta(&now, &hctx->ccid3hctx_t_last_win_count);
		if (likely(hctx->ccid3hctx_rtt > 8))
			quarter_rtt /= hctx->ccid3hctx_rtt / 4;

		if (quarter_rtt > 0) {
			hctx->ccid3hctx_t_last_win_count = now;
			hctx->ccid3hctx_last_win_count	 = (hctx->ccid3hctx_last_win_count +
							    min_t(unsigned long, quarter_rtt, 5)) % 16;
			ccid3_pr_debug("%s, sk=%p, window changed from "
				       "%u to %u!\n",
				       dccp_role(sk), sk,
				       packet->dccphtx_ccval,
				       hctx->ccid3hctx_last_win_count);
		}

		hctx->ccid3hctx_idle = 0;
		packet->dccphtx_rtt  = hctx->ccid3hctx_rtt;
		packet->dccphtx_sent = 1;
	} else
		ccid3_pr_debug("%s, sk=%p, seqno=%llu NOT inserted!\n",
			       dccp_role(sk), sk, dp->dccps_gss);

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		/* if first wasn't pure ack */
		if (len != 0)
			printk(KERN_CRIT "%s: %s, First packet sent is noted "
					 "as a data packet\n",
			       __FUNCTION__, dccp_role(sk));
		return;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		if (len > 0) {
			hctx->ccid3hctx_t_nom = now;
			ccid3_calc_new_t_ipi(hctx);
			ccid3_calc_new_delta(hctx);
			timeval_add_usecs(&hctx->ccid3hctx_t_nom,
					  hctx->ccid3hctx_t_ipi);
		}
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		break;
	}
}

static void ccid3_hc_tx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_options_received *opt_recv;
	struct dccp_tx_hist_entry *packet;
	unsigned long next_tmout; 
	u32 t_elapsed;
	u32 pinv;
	u32 x_recv;
	u32 r_sample;

	if (hctx == NULL)
		return;

	if (hctx->ccid3hctx_state == TFRC_SSTATE_TERM) {
		ccid3_pr_debug("%s, sk=%p, received a packet when "
			       "terminating!\n", dccp_role(sk), sk);
		return;
	}

	/* we are only interested in ACKs */
	if (!(DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK ||
	      DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_DATAACK))
		return;

	opt_recv = &hctx->ccid3hctx_options_received;

	t_elapsed = dp->dccps_options_received.dccpor_elapsed_time;
	x_recv = opt_recv->ccid3or_receive_rate;
	pinv = opt_recv->ccid3or_loss_event_rate;

	switch (hctx->ccid3hctx_state) {
	case TFRC_SSTATE_NO_SENT:
		/* FIXME: what to do here? */
		return;
	case TFRC_SSTATE_NO_FBACK:
	case TFRC_SSTATE_FBACK:
		/* Calculate new round trip sample by
		 * R_sample = (now - t_recvdata) - t_delay */
		/* get t_recvdata from history */
		packet = dccp_tx_hist_find_entry(&hctx->ccid3hctx_hist,
						 DCCP_SKB_CB(skb)->dccpd_ack_seq);
		if (packet == NULL) {
			ccid3_pr_debug("%s, sk=%p, seqno %llu(%s) does't "
				       "exist in history!\n",
				       dccp_role(sk), sk,
				       DCCP_SKB_CB(skb)->dccpd_ack_seq,
				       dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
			return;
		}

		/* Update RTT */
		r_sample = timeval_now_delta(&packet->dccphtx_tstamp);
		/* FIXME: */
		// r_sample -= usecs_to_jiffies(t_elapsed * 10);

		/* Update RTT estimate by 
		 * If (No feedback recv)
		 *    R = R_sample;
		 * Else
		 *    R = q * R + (1 - q) * R_sample;
		 *
		 * q is a constant, RFC 3448 recomments 0.9
		 */
		if (hctx->ccid3hctx_state == TFRC_SSTATE_NO_FBACK) {
			ccid3_hc_tx_set_state(sk, TFRC_SSTATE_FBACK);
			hctx->ccid3hctx_rtt = r_sample;
		} else
			hctx->ccid3hctx_rtt = (hctx->ccid3hctx_rtt * 9) / 10 +
					      r_sample / 10;

		ccid3_pr_debug("%s, sk=%p, New RTT estimate=%uus, "
			       "r_sample=%us\n", dccp_role(sk), sk,
			       hctx->ccid3hctx_rtt, r_sample);

		/* Update timeout interval */
		hctx->ccid3hctx_t_rto = max_t(u32, 4 * hctx->ccid3hctx_rtt,
					      USEC_PER_SEC);

		/* Update receive rate */
		hctx->ccid3hctx_x_recv = x_recv;/* X_recv in bytes per sec */

		/* Update loss event rate */
		if (pinv == ~0 || pinv == 0)
			hctx->ccid3hctx_p = 0;
		else {
			hctx->ccid3hctx_p = 1000000 / pinv;

			if (hctx->ccid3hctx_p < TFRC_SMALLEST_P) {
				hctx->ccid3hctx_p = TFRC_SMALLEST_P;
				ccid3_pr_debug("%s, sk=%p, Smallest p used!\n",
					       dccp_role(sk), sk);
			}
		}

		/* unschedule no feedback timer */
		sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

		/* Update sending rate */
		ccid3_hc_tx_update_x(sk);

		/* Update next send time */
		timeval_sub_usecs(&hctx->ccid3hctx_t_nom,
				  hctx->ccid3hctx_t_ipi);
		ccid3_calc_new_t_ipi(hctx);
		timeval_add_usecs(&hctx->ccid3hctx_t_nom,
				  hctx->ccid3hctx_t_ipi);
		ccid3_calc_new_delta(hctx);

		/* remove all packets older than the one acked from history */
		dccp_tx_hist_purge_older(ccid3_tx_hist,
					 &hctx->ccid3hctx_hist, packet);

		/*
		 * Schedule no feedback timer to expire in
		 * max(4 * R, 2 * s / X)
		 */
		next_tmout = max(hctx->ccid3hctx_t_rto,
				 2 * usecs_div(hctx->ccid3hctx_s,
					       hctx->ccid3hctx_x));
			
		ccid3_pr_debug("%s, sk=%p, Scheduled no feedback timer to "
			       "expire in %lu jiffies (%luus)\n",
			       dccp_role(sk), sk,
			       usecs_to_jiffies(next_tmout), next_tmout); 

		sk_reset_timer(sk, &hctx->ccid3hctx_no_feedback_timer, 
			       jiffies + max_t(u32, 1, usecs_to_jiffies(next_tmout)));

		/* set idle flag */
		hctx->ccid3hctx_idle = 1;   
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hctx->ccid3hctx_state);
		dump_stack();
		break;
	}
}

static void ccid3_hc_tx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	if (hctx == NULL || !(sk->sk_state == DCCP_OPEN ||
			      sk->sk_state == DCCP_PARTOPEN))
		return;

	 DCCP_SKB_CB(skb)->dccpd_ccval = hctx->ccid3hctx_last_win_count;
}

static int ccid3_hc_tx_parse_options(struct sock *sk, unsigned char option,
				     unsigned char len, u16 idx,
				     unsigned char *value)
{
	int rc = 0;
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;
	struct ccid3_options_received *opt_recv;

	if (hctx == NULL)
		return 0;

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
		if (len != 4) {
			ccid3_pr_debug("%s, sk=%p, invalid len for "
				       "TFRC_OPT_LOSS_EVENT_RATE\n",
				       dccp_role(sk), sk);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_loss_event_rate = ntohl(*(u32 *)value);
			ccid3_pr_debug("%s, sk=%p, LOSS_EVENT_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_loss_event_rate);
		}
		break;
	case TFRC_OPT_LOSS_INTERVALS:
		opt_recv->ccid3or_loss_intervals_idx = idx;
		opt_recv->ccid3or_loss_intervals_len = len;
		ccid3_pr_debug("%s, sk=%p, LOSS_INTERVALS=(%u, %u)\n",
			       dccp_role(sk), sk,
			       opt_recv->ccid3or_loss_intervals_idx,
			       opt_recv->ccid3or_loss_intervals_len);
		break;
	case TFRC_OPT_RECEIVE_RATE:
		if (len != 4) {
			ccid3_pr_debug("%s, sk=%p, invalid len for "
				       "TFRC_OPT_RECEIVE_RATE\n",
				       dccp_role(sk), sk);
			rc = -EINVAL;
		} else {
			opt_recv->ccid3or_receive_rate = ntohl(*(u32 *)value);
			ccid3_pr_debug("%s, sk=%p, RECEIVE_RATE=%u\n",
				       dccp_role(sk), sk,
				       opt_recv->ccid3or_receive_rate);
		}
		break;
	}

	return rc;
}

static int ccid3_hc_tx_init(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	hctx = dp->dccps_hc_tx_ccid_private = kmalloc(sizeof(*hctx),
						      gfp_any());
	if (hctx == NULL)
		return -ENOMEM;

	memset(hctx, 0, sizeof(*hctx));

	if (dp->dccps_packet_size >= TFRC_MIN_PACKET_SIZE &&
	    dp->dccps_packet_size <= TFRC_MAX_PACKET_SIZE)
		hctx->ccid3hctx_s = dp->dccps_packet_size;
	else
		hctx->ccid3hctx_s = TFRC_STD_PACKET_SIZE;

	/* Set transmission rate to 1 packet per second */
	hctx->ccid3hctx_x     = hctx->ccid3hctx_s;
	hctx->ccid3hctx_t_rto = USEC_PER_SEC;
	hctx->ccid3hctx_state = TFRC_SSTATE_NO_SENT;
	INIT_LIST_HEAD(&hctx->ccid3hctx_hist);
	init_timer(&hctx->ccid3hctx_no_feedback_timer);

	return 0;
}

static void ccid3_hc_tx_exit(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);
	BUG_ON(hctx == NULL);

	ccid3_hc_tx_set_state(sk, TFRC_SSTATE_TERM);
	sk_stop_timer(sk, &hctx->ccid3hctx_no_feedback_timer);

	/* Empty packet history */
	dccp_tx_hist_purge(ccid3_tx_hist, &hctx->ccid3hctx_hist);

	kfree(dp->dccps_hc_tx_ccid_private);
	dp->dccps_hc_tx_ccid_private = NULL;
}

/*
 * RX Half Connection methods
 */

/* TFRC receiver states */
enum ccid3_hc_rx_states {
       	TFRC_RSTATE_NO_DATA = 1,
	TFRC_RSTATE_DATA,
	TFRC_RSTATE_TERM    = 127,
};

#ifdef CCID3_DEBUG
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

static inline void ccid3_hc_rx_set_state(struct sock *sk,
					 enum ccid3_hc_rx_states state)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	enum ccid3_hc_rx_states oldstate = hcrx->ccid3hcrx_state;

	ccid3_pr_debug("%s(%p) %-8.8s -> %s\n",
		       dccp_role(sk), sk, ccid3_rx_state_name(oldstate),
		       ccid3_rx_state_name(state));
	WARN_ON(state == oldstate);
	hcrx->ccid3hcrx_state = state;
}

static void ccid3_hc_rx_send_feedback(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct dccp_rx_hist_entry *packet;
	struct timeval now;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	do_gettimeofday(&now);

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		hcrx->ccid3hcrx_x_recv = 0;
		break;
	case TFRC_RSTATE_DATA: {
		const u32 delta = timeval_delta(&now,
					&hcrx->ccid3hcrx_tstamp_last_feedback);

		hcrx->ccid3hcrx_x_recv = (hcrx->ccid3hcrx_bytes_recv *
					  USEC_PER_SEC);
		if (likely(delta > 1))
			hcrx->ccid3hcrx_x_recv /= delta;
	}
		break;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hcrx->ccid3hcrx_state);
		dump_stack();
		return;
	}

	packet = dccp_rx_hist_find_data_packet(&hcrx->ccid3hcrx_hist);
	if (packet == NULL) {
		printk(KERN_CRIT "%s: %s, sk=%p, no data packet in history!\n",
		       __FUNCTION__, dccp_role(sk), sk);
		dump_stack();
		return;
	}

	hcrx->ccid3hcrx_tstamp_last_feedback = now;
	hcrx->ccid3hcrx_last_counter	     = packet->dccphrx_ccval;
	hcrx->ccid3hcrx_seqno_last_counter   = packet->dccphrx_seqno;
	hcrx->ccid3hcrx_bytes_recv	     = 0;

	/* Convert to multiples of 10us */
	hcrx->ccid3hcrx_elapsed_time =
			timeval_delta(&now, &packet->dccphrx_tstamp) / 10;
	if (hcrx->ccid3hcrx_p == 0)
		hcrx->ccid3hcrx_pinv = ~0;
	else
		hcrx->ccid3hcrx_pinv = 1000000 / hcrx->ccid3hcrx_p;
	dccp_send_ack(sk);
}

static void ccid3_hc_rx_insert_options(struct sock *sk, struct sk_buff *skb)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	u32 x_recv, pinv;
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	if (hcrx == NULL || !(sk->sk_state == DCCP_OPEN ||
			      sk->sk_state == DCCP_PARTOPEN))
		return;

	DCCP_SKB_CB(skb)->dccpd_ccval = hcrx->ccid3hcrx_last_counter;

	if (dccp_packet_without_ack(skb))
		return;
		
	if (hcrx->ccid3hcrx_elapsed_time != 0)
		dccp_insert_option_elapsed_time(sk, skb,
						hcrx->ccid3hcrx_elapsed_time);
	dccp_insert_option_timestamp(sk, skb);
	x_recv = htonl(hcrx->ccid3hcrx_x_recv);
	pinv   = htonl(hcrx->ccid3hcrx_pinv);
	dccp_insert_option(sk, skb, TFRC_OPT_LOSS_EVENT_RATE,
			   &pinv, sizeof(pinv));
	dccp_insert_option(sk, skb, TFRC_OPT_RECEIVE_RATE,
			   &x_recv, sizeof(x_recv));
}

/* calculate first loss interval
 *
 * returns estimated loss interval in usecs */

static u32 ccid3_hc_rx_calc_first_li(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	struct dccp_rx_hist_entry *entry, *next, *tail = NULL;
	u32 rtt, delta, x_recv, fval, p, tmp2;
	struct timeval tstamp = { 0, };
	int interval = 0;
	int win_count = 0;
	int step = 0;
	u64 tmp1;

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

	if (step == 0) {
		printk(KERN_CRIT "%s: %s, sk=%p, packet history contains no "
				 "data packets!\n",
		       __FUNCTION__, dccp_role(sk), sk);
		return ~0;
	}

	if (interval == 0) {
		ccid3_pr_debug("%s, sk=%p, Could not find a win_count "
			       "interval > 0. Defaulting to 1\n",
			       dccp_role(sk), sk);
		interval = 1;
	}
found:
	rtt = timeval_delta(&tstamp, &tail->dccphrx_tstamp) * 4 / interval;
	ccid3_pr_debug("%s, sk=%p, approximated RTT to %uus\n",
		       dccp_role(sk), sk, rtt);
	if (rtt == 0)
		rtt = 1;

	delta = timeval_now_delta(&hcrx->ccid3hcrx_tstamp_last_feedback);
	x_recv = hcrx->ccid3hcrx_bytes_recv * USEC_PER_SEC;
	if (likely(delta > 1))
		x_recv /= delta;

	tmp1 = (u64)x_recv * (u64)rtt;
	do_div(tmp1,10000000);
	tmp2 = (u32)tmp1;
	fval = (hcrx->ccid3hcrx_s * 100000) / tmp2;
	/* do not alter order above or you will get overflow on 32 bit */
	p = tfrc_calc_x_reverse_lookup(fval);
	ccid3_pr_debug("%s, sk=%p, receive rate=%u bytes/s, implied "
		       "loss rate=%u\n", dccp_role(sk), sk, x_recv, p);

	if (p == 0)
		return ~0;
	else
		return 1000000 / p; 
}

static void ccid3_hc_rx_update_li(struct sock *sk, u64 seq_loss, u8 win_loss)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	if (seq_loss != DCCP_MAX_SEQNO + 1 &&
	    list_empty(&hcrx->ccid3hcrx_li_hist)) {
		struct dccp_li_hist_entry *li_tail;

		li_tail = dccp_li_hist_interval_new(ccid3_li_hist,
						    &hcrx->ccid3hcrx_li_hist,
						    seq_loss, win_loss);
		if (li_tail == NULL)
			return;
		li_tail->dccplih_interval = ccid3_hc_rx_calc_first_li(sk);
	}
	/* FIXME: find end of interval */
}

static void ccid3_hc_rx_detect_loss(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	u8 win_loss;
	const u64 seq_loss = dccp_rx_hist_detect_loss(&hcrx->ccid3hcrx_hist,
						      &hcrx->ccid3hcrx_li_hist,
						      &win_loss);

	ccid3_hc_rx_update_li(sk, seq_loss, win_loss);
}

static void ccid3_hc_rx_packet_recv(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;
	const struct dccp_options_received *opt_recv;
	struct dccp_rx_hist_entry *packet;
	struct timeval now;
	u8 win_count;
	u32 p_prev;
	int ins;

	if (hcrx == NULL)
		return;

	BUG_ON(!(hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA ||
		 hcrx->ccid3hcrx_state == TFRC_RSTATE_DATA));

	opt_recv = &dp->dccps_options_received;

	switch (DCCP_SKB_CB(skb)->dccpd_type) {
	case DCCP_PKT_ACK:
		if (hcrx->ccid3hcrx_state == TFRC_RSTATE_NO_DATA)
			return;
	case DCCP_PKT_DATAACK:
		if (opt_recv->dccpor_timestamp_echo == 0)
			break;
		p_prev = hcrx->ccid3hcrx_rtt;
		do_gettimeofday(&now);
		hcrx->ccid3hcrx_rtt = timeval_usecs(&now) -
				     (opt_recv->dccpor_timestamp_echo -
				      opt_recv->dccpor_elapsed_time) * 10;
		if (p_prev != hcrx->ccid3hcrx_rtt)
			ccid3_pr_debug("%s, New RTT=%luus, elapsed time=%u\n",
				       dccp_role(sk), hcrx->ccid3hcrx_rtt,
				       opt_recv->dccpor_elapsed_time);
		break;
	case DCCP_PKT_DATA:
		break;
	default:
		ccid3_pr_debug("%s, sk=%p, not DATA/DATAACK/ACK packet(%s)\n",
			       dccp_role(sk), sk,
			       dccp_packet_name(DCCP_SKB_CB(skb)->dccpd_type));
		return;
	}

	packet = dccp_rx_hist_entry_new(ccid3_rx_hist, opt_recv->dccpor_ndp,
					skb, SLAB_ATOMIC);
	if (packet == NULL) {
		ccid3_pr_debug("%s, sk=%p, Not enough mem to add rx packet "
			       "to history (consider it lost)!",
			       dccp_role(sk), sk);
		return;
	}

	win_count = packet->dccphrx_ccval;

	ins = dccp_rx_hist_add_packet(ccid3_rx_hist, &hcrx->ccid3hcrx_hist,
				      &hcrx->ccid3hcrx_li_hist, packet);

	if (DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_ACK)
		return;

	switch (hcrx->ccid3hcrx_state) {
	case TFRC_RSTATE_NO_DATA:
		ccid3_pr_debug("%s, sk=%p(%s), skb=%p, sending initial "
			       "feedback\n",
			       dccp_role(sk), sk,
			       dccp_state_name(sk->sk_state), skb);
		ccid3_hc_rx_send_feedback(sk);
		ccid3_hc_rx_set_state(sk, TFRC_RSTATE_DATA);
		return;
	case TFRC_RSTATE_DATA:
		hcrx->ccid3hcrx_bytes_recv += skb->len -
					      dccp_hdr(skb)->dccph_doff * 4;
		if (ins != 0)
			break;

		do_gettimeofday(&now);
		if (timeval_delta(&now, &hcrx->ccid3hcrx_tstamp_last_ack) >=
		    hcrx->ccid3hcrx_rtt) {
			hcrx->ccid3hcrx_tstamp_last_ack = now;
			ccid3_hc_rx_send_feedback(sk);
		}
		return;
	default:
		printk(KERN_CRIT "%s: %s, sk=%p, Illegal state (%d)!\n",
		       __FUNCTION__, dccp_role(sk), sk, hcrx->ccid3hcrx_state);
		dump_stack();
		return;
	}

	/* Dealing with packet loss */
	ccid3_pr_debug("%s, sk=%p(%s), data loss! Reacting...\n",
		       dccp_role(sk), sk, dccp_state_name(sk->sk_state));

	ccid3_hc_rx_detect_loss(sk);
	p_prev = hcrx->ccid3hcrx_p;
	
	/* Calculate loss event rate */
	if (!list_empty(&hcrx->ccid3hcrx_li_hist))
		/* Scaling up by 1000000 as fixed decimal */
		hcrx->ccid3hcrx_p = 1000000 / dccp_li_hist_calc_i_mean(&hcrx->ccid3hcrx_li_hist);

	if (hcrx->ccid3hcrx_p > p_prev) {
		ccid3_hc_rx_send_feedback(sk);
		return;
	}
}

static int ccid3_hc_rx_init(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	hcrx = dp->dccps_hc_rx_ccid_private = kmalloc(sizeof(*hcrx),
						      gfp_any());
	if (hcrx == NULL)
		return -ENOMEM;

	memset(hcrx, 0, sizeof(*hcrx));

	if (dp->dccps_packet_size >= TFRC_MIN_PACKET_SIZE &&
	    dp->dccps_packet_size <= TFRC_MAX_PACKET_SIZE)
		hcrx->ccid3hcrx_s = dp->dccps_packet_size;
	else
		hcrx->ccid3hcrx_s = TFRC_STD_PACKET_SIZE;

	hcrx->ccid3hcrx_state = TFRC_RSTATE_NO_DATA;
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_hist);
	INIT_LIST_HEAD(&hcrx->ccid3hcrx_li_hist);
	/*
	 * XXX this seems to be paranoid, need to think more about this, for
	 * now start with something different than zero. -acme
	 */
	hcrx->ccid3hcrx_rtt = USEC_PER_SEC / 5;
	return 0;
}

static void ccid3_hc_rx_exit(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	ccid3_pr_debug("%s, sk=%p\n", dccp_role(sk), sk);

	if (hcrx == NULL)
		return;

	ccid3_hc_rx_set_state(sk, TFRC_RSTATE_TERM);

	/* Empty packet history */
	dccp_rx_hist_purge(ccid3_rx_hist, &hcrx->ccid3hcrx_hist);

	/* Empty loss interval history */
	dccp_li_hist_purge(ccid3_li_hist, &hcrx->ccid3hcrx_li_hist);

	kfree(dp->dccps_hc_rx_ccid_private);
	dp->dccps_hc_rx_ccid_private = NULL;
}

static void ccid3_hc_rx_get_info(struct sock *sk, struct tcp_info *info)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const struct ccid3_hc_rx_sock *hcrx = dp->dccps_hc_rx_ccid_private;

	if (hcrx == NULL)
		return;

	info->tcpi_ca_state	= hcrx->ccid3hcrx_state;
	info->tcpi_options	|= TCPI_OPT_TIMESTAMPS;
	info->tcpi_rcv_rtt	= hcrx->ccid3hcrx_rtt;
}

static void ccid3_hc_tx_get_info(struct sock *sk, struct tcp_info *info)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const struct ccid3_hc_tx_sock *hctx = dp->dccps_hc_tx_ccid_private;

	if (hctx == NULL)
		return;

	info->tcpi_rto = hctx->ccid3hctx_t_rto;
	info->tcpi_rtt = hctx->ccid3hctx_rtt;
}

static struct ccid ccid3 = {
	.ccid_id		   = 3,
	.ccid_name		   = "ccid3",
	.ccid_owner		   = THIS_MODULE,
	.ccid_init		   = ccid3_init,
	.ccid_exit		   = ccid3_exit,
	.ccid_hc_tx_init	   = ccid3_hc_tx_init,
	.ccid_hc_tx_exit	   = ccid3_hc_tx_exit,
	.ccid_hc_tx_send_packet	   = ccid3_hc_tx_send_packet,
	.ccid_hc_tx_packet_sent	   = ccid3_hc_tx_packet_sent,
	.ccid_hc_tx_packet_recv	   = ccid3_hc_tx_packet_recv,
	.ccid_hc_tx_insert_options = ccid3_hc_tx_insert_options,
	.ccid_hc_tx_parse_options  = ccid3_hc_tx_parse_options,
	.ccid_hc_rx_init	   = ccid3_hc_rx_init,
	.ccid_hc_rx_exit	   = ccid3_hc_rx_exit,
	.ccid_hc_rx_insert_options = ccid3_hc_rx_insert_options,
	.ccid_hc_rx_packet_recv	   = ccid3_hc_rx_packet_recv,
	.ccid_hc_rx_get_info	   = ccid3_hc_rx_get_info,
	.ccid_hc_tx_get_info	   = ccid3_hc_tx_get_info,
};
 
module_param(ccid3_debug, int, 0444);
MODULE_PARM_DESC(ccid3_debug, "Enable debug messages");

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
#ifdef CONFIG_IP_DCCP_UNLOAD_HACK
	/*
	 * Hack to use while developing, so that we get rid of the control
	 * sock, that is what keeps a refcount on dccp.ko -acme
	 */
	extern void dccp_ctl_sock_exit(void);

	dccp_ctl_sock_exit();
#endif
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

MODULE_AUTHOR("Ian McDonald <iam4@cs.waikato.ac.nz>, "
	      "Arnaldo Carvalho de Melo <acme@ghostprotocols.net>");
MODULE_DESCRIPTION("DCCP TFRC CCID3 CCID");
MODULE_LICENSE("GPL");
MODULE_ALIAS("net-dccp-ccid-3");
