#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>
#include <asm/div64.h>

#include <net/tcp.h>

/* Vegas variables */
struct vegas {
	u32	beg_snd_nxt;	/* right edge during last RTT */
	u32	beg_snd_una;	/* left edge  during last RTT */
	u32	beg_snd_cwnd;	/* saves the size of the cwnd */
	u8	doing_vegas_now;/* if true, do vegas for this RTT */
	u16	cntRTT;		/* # of RTTs measured within last RTT */
	u32	minRTT;		/* min of RTTs measured within last RTT (in usec) */
	u32	baseRTT;	/* the min of all Vegas RTT measurements seen (in usec) */
};

/* There are several situations when we must "re-start" Vegas:
 *
 *  o when a connection is established
 *  o after an RTO
 *  o after fast recovery
 *  o when we send a packet and there is no outstanding
 *    unacknowledged data (restarting an idle connection)
 *
 * In these circumstances we cannot do a Vegas calculation at the
 * end of the first RTT, because any calculation we do is using
 * stale info -- both the saved cwnd and congestion feedback are
 * stale.
 *
 * Instead we must wait until the completion of an RTT during
 * which we actually receive ACKs.
 */
static inline void vegas_enable(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct vegas *vegas = inet_csk_ca(sk);

	/* Begin taking Vegas samples next time we send something. */
	vegas->doing_vegas_now = 1;

	/* Set the beginning of the next send window. */
	vegas->beg_snd_nxt = tp->snd_nxt;

	vegas->cntRTT = 0;
	vegas->minRTT = 0x7fffffff;
}

/* Stop taking Vegas samples for now. */
static inline void vegas_disable(struct sock *sk)
{
	struct vegas *vegas = inet_csk_ca(sk);

	vegas->doing_vegas_now = 0;
}

static void tcp_vegas_init(struct sock *sk)
{
	struct vegas *vegas = inet_csk_ca(sk);

	vegas->baseRTT = 0x7fffffff;
	vegas_enable(sk);
}

static void tcp_vegas_state(struct sock *sk, u8 ca_state)
{

	if (ca_state == TCP_CA_Open)
		vegas_enable(sk);
	else
		vegas_disable(sk);
}

/* Do RTT sampling needed for Vegas.
 * Basically we:
 *   o min-filter RTT samples from within an RTT to get the current
 *     propagation delay + queuing delay (we are min-filtering to try to
 *     avoid the effects of delayed ACKs)
 *   o min-filter RTT samples from a much longer window (forever for now)
 *     to find the propagation delay (baseRTT)
 */
static void tcp_vegas_pkts_acked(struct sock *sk, u32 cnt, ktime_t last)
{
	struct vegas *vegas = inet_csk_ca(sk);
	u32 vrtt;

	/* Never allow zero rtt or baseRTT */
	vrtt = (ktime_to_ns(net_timedelta(last)) / NSEC_PER_USEC) + 1;

	/* Filter to find propagation delay: */
	if (vrtt < vegas->baseRTT)
		vegas->baseRTT = vrtt;

	/* Find the min RTT during the last RTT to find
	 * the current prop. delay + queuing delay:
	 */
	vegas->minRTT = min(vegas->minRTT, vrtt);
	vegas->cntRTT++;
}

/*
 * If the connection is idle and we are restarting,
 * then we don't want to do any Vegas calculations
 * until we get fresh RTT samples.  So when we
 * restart, we reset our Vegas state to a clean
 * slate. After we get acks for this flight of
 * packets, _then_ we can make Vegas calculations
 * again.
 */
static void tcp_vegas_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START)
		tcp_vegas_init(sk);
}

/* Extract info for Tcp socket info provided via netlink. */
static void tcp_vegas_get_info(struct sock *sk, u32 ext,
			       struct sk_buff *skb)
{
	const struct vegas *ca = inet_csk_ca(sk);
	if (ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		struct tcpvegas_info *info;

		info = RTA_DATA(__RTA_PUT(skb, INET_DIAG_VEGASINFO,
					  sizeof(*info)));

		info->tcpv_enabled = ca->doing_vegas_now;
		info->tcpv_rttcnt = ca->cntRTT;
		info->tcpv_rtt = ca->baseRTT;
		info->tcpv_minrtt = ca->minRTT;
	rtattr_failure:	;
	}
}


