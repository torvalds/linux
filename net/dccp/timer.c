// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  net/dccp/timer.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/dccp.h>
#include <linux/skbuff.h>
#include <linux/export.h>

#include "dccp.h"

/* sysctl variables governing numbers of retransmission attempts */
int  sysctl_dccp_request_retries	__read_mostly = TCP_SYN_RETRIES;
int  sysctl_dccp_retries1		__read_mostly = TCP_RETR1;
int  sysctl_dccp_retries2		__read_mostly = TCP_RETR2;

static void dccp_write_err(struct sock *sk)
{
	sk->sk_err = READ_ONCE(sk->sk_err_soft) ? : ETIMEDOUT;
	sk_error_report(sk);

	dccp_send_reset(sk, DCCP_RESET_CODE_ABORTED);
	dccp_done(sk);
	__DCCP_INC_STATS(DCCP_MIB_ABORTONTIMEOUT);
}

/* A write timeout has occurred. Process the after effects. */
static int dccp_write_timeout(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	int retry_until;

	if (sk->sk_state == DCCP_REQUESTING || sk->sk_state == DCCP_PARTOPEN) {
		if (icsk->icsk_retransmits != 0)
			dst_negative_advice(sk);
		retry_until = icsk->icsk_syn_retries ?
			    : sysctl_dccp_request_retries;
	} else {
		if (icsk->icsk_retransmits >= sysctl_dccp_retries1) {
			/* NOTE. draft-ietf-tcpimpl-pmtud-01.txt requires pmtu
			   black hole detection. :-(

			   It is place to make it. It is not made. I do not want
			   to make it. It is disguisting. It does not work in any
			   case. Let me to cite the same draft, which requires for
			   us to implement this:

   "The one security concern raised by this memo is that ICMP black holes
   are often caused by over-zealous security administrators who block
   all ICMP messages.  It is vitally important that those who design and
   deploy security systems understand the impact of strict filtering on
   upper-layer protocols.  The safest web site in the world is worthless
   if most TCP implementations cannot transfer data from it.  It would
   be far nicer to have all of the black holes fixed rather than fixing
   all of the TCP implementations."

			   Golden words :-).
		   */

			dst_negative_advice(sk);
		}

		retry_until = sysctl_dccp_retries2;
		/*
		 * FIXME: see tcp_write_timout and tcp_out_of_resources
		 */
	}

	if (icsk->icsk_retransmits >= retry_until) {
		/* Has it gone just too far? */
		dccp_write_err(sk);
		return 1;
	}
	return 0;
}

/*
 *	The DCCP retransmit timer.
 */
static void dccp_retransmit_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	/*
	 * More than 4MSL (8 minutes) has passed, a RESET(aborted) was
	 * sent, no need to retransmit, this sock is dead.
	 */
	if (dccp_write_timeout(sk))
		return;

	/*
	 * We want to know the number of packets retransmitted, not the
	 * total number of retransmissions of clones of original packets.
	 */
	if (icsk->icsk_retransmits == 0)
		__DCCP_INC_STATS(DCCP_MIB_TIMEOUTS);

	if (dccp_retransmit_skb(sk) != 0) {
		/*
		 * Retransmission failed because of local congestion,
		 * do not backoff.
		 */
		if (--icsk->icsk_retransmits == 0)
			icsk->icsk_retransmits = 1;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  min(icsk->icsk_rto,
					      TCP_RESOURCE_PROBE_INTERVAL),
					  DCCP_RTO_MAX);
		return;
	}

	icsk->icsk_backoff++;

	icsk->icsk_rto = min(icsk->icsk_rto << 1, DCCP_RTO_MAX);
	inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS, icsk->icsk_rto,
				  DCCP_RTO_MAX);
	if (icsk->icsk_retransmits > sysctl_dccp_retries1)
		__sk_dst_reset(sk);
}

static void dccp_write_timer(struct timer_list *t)
{
	struct inet_connection_sock *icsk =
			from_timer(icsk, t, icsk_retransmit_timer);
	struct sock *sk = &icsk->icsk_inet.sk;
	int event = 0;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later */
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer,
			       jiffies + (HZ / 20));
		goto out;
	}

	if (sk->sk_state == DCCP_CLOSED || !icsk->icsk_pending)
		goto out;

	if (time_after(icsk->icsk_timeout, jiffies)) {
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer,
			       icsk->icsk_timeout);
		goto out;
	}

	event = icsk->icsk_pending;
	icsk->icsk_pending = 0;

	switch (event) {
	case ICSK_TIME_RETRANS:
		dccp_retransmit_timer(sk);
		break;
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void dccp_keepalive_timer(struct timer_list *t)
{
	struct sock *sk = from_timer(sk, t, sk_timer);

	pr_err("dccp should not use a keepalive timer !\n");
	sock_put(sk);
}

/* This is the same as tcp_delack_timer, sans prequeue & mem_reclaim stuff */
static void dccp_delack_timer(struct timer_list *t)
{
	struct inet_connection_sock *icsk =
			from_timer(icsk, t, icsk_delack_timer);
	struct sock *sk = &icsk->icsk_inet.sk;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		__NET_INC_STATS(sock_net(sk), LINUX_MIB_DELAYEDACKLOCKED);
		sk_reset_timer(sk, &icsk->icsk_delack_timer,
			       jiffies + TCP_DELACK_MIN);
		goto out;
	}

	if (sk->sk_state == DCCP_CLOSED ||
	    !(icsk->icsk_ack.pending & ICSK_ACK_TIMER))
		goto out;
	if (time_after(icsk->icsk_ack.timeout, jiffies)) {
		sk_reset_timer(sk, &icsk->icsk_delack_timer,
			       icsk->icsk_ack.timeout);
		goto out;
	}

	icsk->icsk_ack.pending &= ~ICSK_ACK_TIMER;

	if (inet_csk_ack_scheduled(sk)) {
		if (!inet_csk_in_pingpong_mode(sk)) {
			/* Delayed ACK missed: inflate ATO. */
			icsk->icsk_ack.ato = min_t(u32, icsk->icsk_ack.ato << 1,
						   icsk->icsk_rto);
		} else {
			/* Delayed ACK missed: leave pingpong mode and
			 * deflate ATO.
			 */
			inet_csk_exit_pingpong_mode(sk);
			icsk->icsk_ack.ato = TCP_ATO_MIN;
		}
		dccp_send_ack(sk);
		__NET_INC_STATS(sock_net(sk), LINUX_MIB_DELAYEDACKS);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/**
 * dccp_write_xmitlet  -  Workhorse for CCID packet dequeueing interface
 * @t: pointer to the tasklet associated with this handler
 *
 * See the comments above %ccid_dequeueing_decision for supported modes.
 */
static void dccp_write_xmitlet(struct tasklet_struct *t)
{
	struct dccp_sock *dp = from_tasklet(dp, t, dccps_xmitlet);
	struct sock *sk = &dp->dccps_inet_connection.icsk_inet.sk;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		sk_reset_timer(sk, &dccp_sk(sk)->dccps_xmit_timer, jiffies + 1);
	else
		dccp_write_xmit(sk);
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void dccp_write_xmit_timer(struct timer_list *t)
{
	struct dccp_sock *dp = from_timer(dp, t, dccps_xmit_timer);

	dccp_write_xmitlet(&dp->dccps_xmitlet);
}

void dccp_init_xmit_timers(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	tasklet_setup(&dp->dccps_xmitlet, dccp_write_xmitlet);
	timer_setup(&dp->dccps_xmit_timer, dccp_write_xmit_timer, 0);
	inet_csk_init_xmit_timers(sk, &dccp_write_timer, &dccp_delack_timer,
				  &dccp_keepalive_timer);
}

static ktime_t dccp_timestamp_seed;
/**
 * dccp_timestamp  -  10s of microseconds time source
 * Returns the number of 10s of microseconds since loading DCCP. This is native
 * DCCP time difference format (RFC 4340, sec. 13).
 * Please note: This will wrap around about circa every 11.9 hours.
 */
u32 dccp_timestamp(void)
{
	u64 delta = (u64)ktime_us_delta(ktime_get_real(), dccp_timestamp_seed);

	do_div(delta, 10);
	return delta;
}
EXPORT_SYMBOL_GPL(dccp_timestamp);

void __init dccp_timestamping_init(void)
{
	dccp_timestamp_seed = ktime_get_real();
}
