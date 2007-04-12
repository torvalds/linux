/*
 *  net/dccp/timer.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/dccp.h>
#include <linux/skbuff.h>

#include "dccp.h"

/* sysctl variables governing numbers of retransmission attempts */
int  sysctl_dccp_request_retries	__read_mostly = TCP_SYN_RETRIES;
int  sysctl_dccp_retries1		__read_mostly = TCP_RETR1;
int  sysctl_dccp_retries2		__read_mostly = TCP_RETR2;

static void dccp_write_err(struct sock *sk)
{
	sk->sk_err = sk->sk_err_soft ? : ETIMEDOUT;
	sk->sk_error_report(sk);

	dccp_send_reset(sk, DCCP_RESET_CODE_ABORTED);
	dccp_done(sk);
	DCCP_INC_STATS_BH(DCCP_MIB_ABORTONTIMEOUT);
}

/* A write timeout has occurred. Process the after effects. */
static int dccp_write_timeout(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	int retry_until;

	if (sk->sk_state == DCCP_REQUESTING || sk->sk_state == DCCP_PARTOPEN) {
		if (icsk->icsk_retransmits != 0)
			dst_negative_advice(&sk->sk_dst_cache);
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

			dst_negative_advice(&sk->sk_dst_cache);
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

	/* retransmit timer is used for feature negotiation throughout
	 * connection.  In this case, no packet is re-transmitted, but rather an
	 * ack is generated and pending changes are placed into its options.
	 */
	if (sk->sk_send_head == NULL) {
		dccp_pr_debug("feat negotiation retransmit timeout %p\n", sk);
		if (sk->sk_state == DCCP_OPEN)
			dccp_send_ack(sk);
		goto backoff;
	}

	/*
	 * sk->sk_send_head has to have one skb with
	 * DCCP_SKB_CB(skb)->dccpd_type set to one of the retransmittable DCCP
	 * packet types. The only packets eligible for retransmission are:
	 *	-- Requests in client-REQUEST  state (sec. 8.1.1)
	 *	-- Acks     in client-PARTOPEN state (sec. 8.1.5)
	 *	-- CloseReq in server-CLOSEREQ state (sec. 8.3)
	 *	-- Close    in   node-CLOSING  state (sec. 8.3)                */
	BUG_TRAP(sk->sk_send_head != NULL);

	/*
	 * More than than 4MSL (8 minutes) has passed, a RESET(aborted) was
	 * sent, no need to retransmit, this sock is dead.
	 */
	if (dccp_write_timeout(sk))
		goto out;

	/*
	 * We want to know the number of packets retransmitted, not the
	 * total number of retransmissions of clones of original packets.
	 */
	if (icsk->icsk_retransmits == 0)
		DCCP_INC_STATS_BH(DCCP_MIB_TIMEOUTS);

	if (dccp_retransmit_skb(sk, sk->sk_send_head) < 0) {
		/*
		 * Retransmission failed because of local congestion,
		 * do not backoff.
		 */
		if (icsk->icsk_retransmits == 0)
			icsk->icsk_retransmits = 1;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  min(icsk->icsk_rto,
					      TCP_RESOURCE_PROBE_INTERVAL),
					  DCCP_RTO_MAX);
		goto out;
	}

backoff:
	icsk->icsk_backoff++;
	icsk->icsk_retransmits++;

	icsk->icsk_rto = min(icsk->icsk_rto << 1, DCCP_RTO_MAX);
	inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS, icsk->icsk_rto,
				  DCCP_RTO_MAX);
	if (icsk->icsk_retransmits > sysctl_dccp_retries1)
		__sk_dst_reset(sk);
out:;
}

static void dccp_write_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct inet_connection_sock *icsk = inet_csk(sk);
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

/*
 *	Timer for listening sockets
 */
static void dccp_response_timer(struct sock *sk)
{
	inet_csk_reqsk_queue_prune(sk, TCP_SYNQ_INTERVAL, DCCP_TIMEOUT_INIT,
				   DCCP_RTO_MAX);
}

static void dccp_keepalive_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;

	/* Only process if socket is not in use. */
	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		inet_csk_reset_keepalive_timer(sk, HZ / 20);
		goto out;
	}

	if (sk->sk_state == DCCP_LISTEN) {
		dccp_response_timer(sk);
		goto out;
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/* This is the same as tcp_delack_timer, sans prequeue & mem_reclaim stuff */
static void dccp_delack_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct inet_connection_sock *icsk = inet_csk(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		icsk->icsk_ack.blocked = 1;
		NET_INC_STATS_BH(LINUX_MIB_DELAYEDACKLOCKED);
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
		if (!icsk->icsk_ack.pingpong) {
			/* Delayed ACK missed: inflate ATO. */
			icsk->icsk_ack.ato = min(icsk->icsk_ack.ato << 1,
						 icsk->icsk_rto);
		} else {
			/* Delayed ACK missed: leave pingpong mode and
			 * deflate ATO.
			 */
			icsk->icsk_ack.pingpong = 0;
			icsk->icsk_ack.ato = TCP_ATO_MIN;
		}
		dccp_send_ack(sk);
		NET_INC_STATS_BH(LINUX_MIB_DELAYEDACKS);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/* Transmit-delay timer: used by the CCIDs to delay actual send time */
static void dccp_write_xmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct dccp_sock *dp = dccp_sk(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		sk_reset_timer(sk, &dp->dccps_xmit_timer, jiffies+1);
	else
		dccp_write_xmit(sk, 0);
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void dccp_init_write_xmit_timer(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	init_timer(&dp->dccps_xmit_timer);
	dp->dccps_xmit_timer.data = (unsigned long)sk;
	dp->dccps_xmit_timer.function = dccp_write_xmit_timer;
}

void dccp_init_xmit_timers(struct sock *sk)
{
	dccp_init_write_xmit_timer(sk);
	inet_csk_init_xmit_timers(sk, &dccp_write_timer, &dccp_delack_timer,
				  &dccp_keepalive_timer);
}
