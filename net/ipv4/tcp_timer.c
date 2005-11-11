/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_timer.c,v 1.88 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 */

#include <linux/module.h>
#include <net/tcp.h>

int sysctl_tcp_syn_retries = TCP_SYN_RETRIES; 
int sysctl_tcp_synack_retries = TCP_SYNACK_RETRIES; 
int sysctl_tcp_keepalive_time = TCP_KEEPALIVE_TIME;
int sysctl_tcp_keepalive_probes = TCP_KEEPALIVE_PROBES;
int sysctl_tcp_keepalive_intvl = TCP_KEEPALIVE_INTVL;
int sysctl_tcp_retries1 = TCP_RETR1;
int sysctl_tcp_retries2 = TCP_RETR2;
int sysctl_tcp_orphan_retries;

static void tcp_write_timer(unsigned long);
static void tcp_delack_timer(unsigned long);
static void tcp_keepalive_timer (unsigned long data);

void tcp_init_xmit_timers(struct sock *sk)
{
	inet_csk_init_xmit_timers(sk, &tcp_write_timer, &tcp_delack_timer,
				  &tcp_keepalive_timer);
}

EXPORT_SYMBOL(tcp_init_xmit_timers);

static void tcp_write_err(struct sock *sk)
{
	sk->sk_err = sk->sk_err_soft ? : ETIMEDOUT;
	sk->sk_error_report(sk);

	tcp_done(sk);
	NET_INC_STATS_BH(LINUX_MIB_TCPABORTONTIMEOUT);
}

/* Do not allow orphaned sockets to eat all our resources.
 * This is direct violation of TCP specs, but it is required
 * to prevent DoS attacks. It is called when a retransmission timeout
 * or zero probe timeout occurs on orphaned socket.
 *
 * Criteria is still not confirmed experimentally and may change.
 * We kill the socket, if:
 * 1. If number of orphaned sockets exceeds an administratively configured
 *    limit.
 * 2. If we have strong memory pressure.
 */
static int tcp_out_of_resources(struct sock *sk, int do_reset)
{
	struct tcp_sock *tp = tcp_sk(sk);
	int orphans = atomic_read(&tcp_orphan_count);

	/* If peer does not open window for long time, or did not transmit 
	 * anything for long time, penalize it. */
	if ((s32)(tcp_time_stamp - tp->lsndtime) > 2*TCP_RTO_MAX || !do_reset)
		orphans <<= 1;

	/* If some dubious ICMP arrived, penalize even more. */
	if (sk->sk_err_soft)
		orphans <<= 1;

	if (orphans >= sysctl_tcp_max_orphans ||
	    (sk->sk_wmem_queued > SOCK_MIN_SNDBUF &&
	     atomic_read(&tcp_memory_allocated) > sysctl_tcp_mem[2])) {
		if (net_ratelimit())
			printk(KERN_INFO "Out of socket memory\n");

		/* Catch exceptional cases, when connection requires reset.
		 *      1. Last segment was sent recently. */
		if ((s32)(tcp_time_stamp - tp->lsndtime) <= TCP_TIMEWAIT_LEN ||
		    /*  2. Window is closed. */
		    (!tp->snd_wnd && !tp->packets_out))
			do_reset = 1;
		if (do_reset)
			tcp_send_active_reset(sk, GFP_ATOMIC);
		tcp_done(sk);
		NET_INC_STATS_BH(LINUX_MIB_TCPABORTONMEMORY);
		return 1;
	}
	return 0;
}

/* Calculate maximal number or retries on an orphaned socket. */
static int tcp_orphan_retries(struct sock *sk, int alive)
{
	int retries = sysctl_tcp_orphan_retries; /* May be zero. */

	/* We know from an ICMP that something is wrong. */
	if (sk->sk_err_soft && !alive)
		retries = 0;

	/* However, if socket sent something recently, select some safe
	 * number of retries. 8 corresponds to >100 seconds with minimal
	 * RTO of 200msec. */
	if (retries == 0 && alive)
		retries = 8;
	return retries;
}

/* A write timeout has occurred. Process the after effects. */
static int tcp_write_timeout(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	int retry_until;

	if ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		if (icsk->icsk_retransmits)
			dst_negative_advice(&sk->sk_dst_cache);
		retry_until = icsk->icsk_syn_retries ? : sysctl_tcp_syn_retries;
	} else {
		if (icsk->icsk_retransmits >= sysctl_tcp_retries1) {
			/* NOTE. draft-ietf-tcpimpl-pmtud-01.txt requires pmtu black
			   hole detection. :-(

			   It is place to make it. It is not made. I do not want
			   to make it. It is disgusting. It does not work in any
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

		retry_until = sysctl_tcp_retries2;
		if (sock_flag(sk, SOCK_DEAD)) {
			const int alive = (icsk->icsk_rto < TCP_RTO_MAX);
 
			retry_until = tcp_orphan_retries(sk, alive);

			if (tcp_out_of_resources(sk, alive || icsk->icsk_retransmits < retry_until))
				return 1;
		}
	}

	if (icsk->icsk_retransmits >= retry_until) {
		/* Has it gone just too far? */
		tcp_write_err(sk);
		return 1;
	}
	return 0;
}

static void tcp_delack_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_sock *tp = tcp_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */
		icsk->icsk_ack.blocked = 1;
		NET_INC_STATS_BH(LINUX_MIB_DELAYEDACKLOCKED);
		sk_reset_timer(sk, &icsk->icsk_delack_timer, jiffies + TCP_DELACK_MIN);
		goto out_unlock;
	}

	sk_stream_mem_reclaim(sk);

	if (sk->sk_state == TCP_CLOSE || !(icsk->icsk_ack.pending & ICSK_ACK_TIMER))
		goto out;

	if (time_after(icsk->icsk_ack.timeout, jiffies)) {
		sk_reset_timer(sk, &icsk->icsk_delack_timer, icsk->icsk_ack.timeout);
		goto out;
	}
	icsk->icsk_ack.pending &= ~ICSK_ACK_TIMER;

	if (!skb_queue_empty(&tp->ucopy.prequeue)) {
		struct sk_buff *skb;

		NET_INC_STATS_BH(LINUX_MIB_TCPSCHEDULERFAILED);

		while ((skb = __skb_dequeue(&tp->ucopy.prequeue)) != NULL)
			sk->sk_backlog_rcv(sk, skb);

		tp->ucopy.memory = 0;
	}

	if (inet_csk_ack_scheduled(sk)) {
		if (!icsk->icsk_ack.pingpong) {
			/* Delayed ACK missed: inflate ATO. */
			icsk->icsk_ack.ato = min(icsk->icsk_ack.ato << 1, icsk->icsk_rto);
		} else {
			/* Delayed ACK missed: leave pingpong mode and
			 * deflate ATO.
			 */
			icsk->icsk_ack.pingpong = 0;
			icsk->icsk_ack.ato      = TCP_ATO_MIN;
		}
		tcp_send_ack(sk);
		NET_INC_STATS_BH(LINUX_MIB_DELAYEDACKS);
	}
	TCP_CHECK_TIMER(sk);

out:
	if (tcp_memory_pressure)
		sk_stream_mem_reclaim(sk);
out_unlock:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void tcp_probe_timer(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	int max_probes;

	if (tp->packets_out || !sk->sk_send_head) {
		icsk->icsk_probes_out = 0;
		return;
	}

	/* *WARNING* RFC 1122 forbids this
	 *
	 * It doesn't AFAIK, because we kill the retransmit timer -AK
	 *
	 * FIXME: We ought not to do it, Solaris 2.5 actually has fixing
	 * this behaviour in Solaris down as a bug fix. [AC]
	 *
	 * Let me to explain. icsk_probes_out is zeroed by incoming ACKs
	 * even if they advertise zero window. Hence, connection is killed only
	 * if we received no ACKs for normal connection timeout. It is not killed
	 * only because window stays zero for some time, window may be zero
	 * until armageddon and even later. We are in full accordance
	 * with RFCs, only probe timer combines both retransmission timeout
	 * and probe timeout in one bottle.				--ANK
	 */
	max_probes = sysctl_tcp_retries2;

	if (sock_flag(sk, SOCK_DEAD)) {
		const int alive = ((icsk->icsk_rto << icsk->icsk_backoff) < TCP_RTO_MAX);
 
		max_probes = tcp_orphan_retries(sk, alive);

		if (tcp_out_of_resources(sk, alive || icsk->icsk_probes_out <= max_probes))
			return;
	}

	if (icsk->icsk_probes_out > max_probes) {
		tcp_write_err(sk);
	} else {
		/* Only send another probe if we didn't close things up. */
		tcp_send_probe0(sk);
	}
}

/*
 *	The TCP retransmit timer.
 */

static void tcp_retransmit_timer(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (!tp->packets_out)
		goto out;

	BUG_TRAP(!skb_queue_empty(&sk->sk_write_queue));

	if (!tp->snd_wnd && !sock_flag(sk, SOCK_DEAD) &&
	    !((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV))) {
		/* Receiver dastardly shrinks window. Our retransmits
		 * become zero probes, but we should not timeout this
		 * connection. If the socket is an orphan, time it out,
		 * we cannot allow such beasts to hang infinitely.
		 */
#ifdef TCP_DEBUG
		if (net_ratelimit()) {
			struct inet_sock *inet = inet_sk(sk);
			printk(KERN_DEBUG "TCP: Treason uncloaked! Peer %u.%u.%u.%u:%u/%u shrinks window %u:%u. Repaired.\n",
			       NIPQUAD(inet->daddr), htons(inet->dport),
			       inet->num, tp->snd_una, tp->snd_nxt);
		}
#endif
		if (tcp_time_stamp - tp->rcv_tstamp > TCP_RTO_MAX) {
			tcp_write_err(sk);
			goto out;
		}
		tcp_enter_loss(sk, 0);
		tcp_retransmit_skb(sk, skb_peek(&sk->sk_write_queue));
		__sk_dst_reset(sk);
		goto out_reset_timer;
	}

	if (tcp_write_timeout(sk))
		goto out;

	if (icsk->icsk_retransmits == 0) {
		if (icsk->icsk_ca_state == TCP_CA_Disorder ||
		    icsk->icsk_ca_state == TCP_CA_Recovery) {
			if (tp->rx_opt.sack_ok) {
				if (icsk->icsk_ca_state == TCP_CA_Recovery)
					NET_INC_STATS_BH(LINUX_MIB_TCPSACKRECOVERYFAIL);
				else
					NET_INC_STATS_BH(LINUX_MIB_TCPSACKFAILURES);
			} else {
				if (icsk->icsk_ca_state == TCP_CA_Recovery)
					NET_INC_STATS_BH(LINUX_MIB_TCPRENORECOVERYFAIL);
				else
					NET_INC_STATS_BH(LINUX_MIB_TCPRENOFAILURES);
			}
		} else if (icsk->icsk_ca_state == TCP_CA_Loss) {
			NET_INC_STATS_BH(LINUX_MIB_TCPLOSSFAILURES);
		} else {
			NET_INC_STATS_BH(LINUX_MIB_TCPTIMEOUTS);
		}
	}

	if (tcp_use_frto(sk)) {
		tcp_enter_frto(sk);
	} else {
		tcp_enter_loss(sk, 0);
	}

	if (tcp_retransmit_skb(sk, skb_peek(&sk->sk_write_queue)) > 0) {
		/* Retransmission failed because of local congestion,
		 * do not backoff.
		 */
		if (!icsk->icsk_retransmits)
			icsk->icsk_retransmits = 1;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  min(icsk->icsk_rto, TCP_RESOURCE_PROBE_INTERVAL),
					  TCP_RTO_MAX);
		goto out;
	}

	/* Increase the timeout each time we retransmit.  Note that
	 * we do not increase the rtt estimate.  rto is initialized
	 * from rtt, but increases here.  Jacobson (SIGCOMM 88) suggests
	 * that doubling rto each time is the least we can get away with.
	 * In KA9Q, Karn uses this for the first few times, and then
	 * goes to quadratic.  netBSD doubles, but only goes up to *64,
	 * and clamps at 1 to 64 sec afterwards.  Note that 120 sec is
	 * defined in the protocol as the maximum possible RTT.  I guess
	 * we'll have to use something other than TCP to talk to the
	 * University of Mars.
	 *
	 * PAWS allows us longer timeouts and large windows, so once
	 * implemented ftp to mars will work nicely. We will have to fix
	 * the 120 second clamps though!
	 */
	icsk->icsk_backoff++;
	icsk->icsk_retransmits++;

out_reset_timer:
	icsk->icsk_rto = min(icsk->icsk_rto << 1, TCP_RTO_MAX);
	inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS, icsk->icsk_rto, TCP_RTO_MAX);
	if (icsk->icsk_retransmits > sysctl_tcp_retries1)
		__sk_dst_reset(sk);

out:;
}

static void tcp_write_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct inet_connection_sock *icsk = inet_csk(sk);
	int event;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later */
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer, jiffies + (HZ / 20));
		goto out_unlock;
	}

	if (sk->sk_state == TCP_CLOSE || !icsk->icsk_pending)
		goto out;

	if (time_after(icsk->icsk_timeout, jiffies)) {
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer, icsk->icsk_timeout);
		goto out;
	}

	event = icsk->icsk_pending;
	icsk->icsk_pending = 0;

	switch (event) {
	case ICSK_TIME_RETRANS:
		tcp_retransmit_timer(sk);
		break;
	case ICSK_TIME_PROBE0:
		tcp_probe_timer(sk);
		break;
	}
	TCP_CHECK_TIMER(sk);

out:
	sk_stream_mem_reclaim(sk);
out_unlock:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	Timer for listening sockets
 */

static void tcp_synack_timer(struct sock *sk)
{
	inet_csk_reqsk_queue_prune(sk, TCP_SYNQ_INTERVAL,
				   TCP_TIMEOUT_INIT, TCP_RTO_MAX);
}

void tcp_set_keepalive(struct sock *sk, int val)
{
	if ((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_LISTEN))
		return;

	if (val && !sock_flag(sk, SOCK_KEEPOPEN))
		inet_csk_reset_keepalive_timer(sk, keepalive_time_when(tcp_sk(sk)));
	else if (!val)
		inet_csk_delete_keepalive_timer(sk);
}


static void tcp_keepalive_timer (unsigned long data)
{
	struct sock *sk = (struct sock *) data;
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	__u32 elapsed;

	/* Only process if socket is not in use. */
	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		/* Try again later. */ 
		inet_csk_reset_keepalive_timer (sk, HZ/20);
		goto out;
	}

	if (sk->sk_state == TCP_LISTEN) {
		tcp_synack_timer(sk);
		goto out;
	}

	if (sk->sk_state == TCP_FIN_WAIT2 && sock_flag(sk, SOCK_DEAD)) {
		if (tp->linger2 >= 0) {
			const int tmo = tcp_fin_time(sk) - TCP_TIMEWAIT_LEN;

			if (tmo > 0) {
				tcp_time_wait(sk, TCP_FIN_WAIT2, tmo);
				goto out;
			}
		}
		tcp_send_active_reset(sk, GFP_ATOMIC);
		goto death;
	}

	if (!sock_flag(sk, SOCK_KEEPOPEN) || sk->sk_state == TCP_CLOSE)
		goto out;

	elapsed = keepalive_time_when(tp);

	/* It is alive without keepalive 8) */
	if (tp->packets_out || sk->sk_send_head)
		goto resched;

	elapsed = tcp_time_stamp - tp->rcv_tstamp;

	if (elapsed >= keepalive_time_when(tp)) {
		if ((!tp->keepalive_probes && icsk->icsk_probes_out >= sysctl_tcp_keepalive_probes) ||
		     (tp->keepalive_probes && icsk->icsk_probes_out >= tp->keepalive_probes)) {
			tcp_send_active_reset(sk, GFP_ATOMIC);
			tcp_write_err(sk);
			goto out;
		}
		if (tcp_write_wakeup(sk) <= 0) {
			icsk->icsk_probes_out++;
			elapsed = keepalive_intvl_when(tp);
		} else {
			/* If keepalive was lost due to local congestion,
			 * try harder.
			 */
			elapsed = TCP_RESOURCE_PROBE_INTERVAL;
		}
	} else {
		/* It is tp->rcv_tstamp + keepalive_time_when(tp) */
		elapsed = keepalive_time_when(tp) - elapsed;
	}

	TCP_CHECK_TIMER(sk);
	sk_stream_mem_reclaim(sk);

resched:
	inet_csk_reset_keepalive_timer (sk, elapsed);
	goto out;

death:	
	tcp_done(sk);

out:
	bh_unlock_sock(sk);
	sock_put(sk);
}
