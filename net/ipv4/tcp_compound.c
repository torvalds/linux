/*
 * TCP Vegas congestion control
 *
 * This is based on the congestion detection/avoidance scheme described in
 *    Lawrence S. Brakmo and Larry L. Peterson.
 *    "TCP Vegas: End to end congestion avoidance on a global internet."
 *    IEEE Journal on Selected Areas in Communication, 13(8):1465--1480,
 *    October 1995. Available from:
 *	ftp://ftp.cs.arizona.edu/xkernel/Papers/jsac.ps
 *
 * See http://www.cs.arizona.edu/xkernel/ for their implementation.
 * The main aspects that distinguish this implementation from the
 * Arizona Vegas implementation are:
 *   o We do not change the loss detection or recovery mechanisms of
 *     Linux in any way. Linux already recovers from losses quite well,
 *     using fine-grained timers, NewReno, and FACK.
 *   o To avoid the performance penalty imposed by increasing cwnd
 *     only every-other RTT during slow start, we increase during
 *     every RTT during slow start, just like Reno.
 *   o Largely to allow continuous cwnd growth during slow start,
 *     we use the rate at which ACKs come back as the "actual"
 *     rate, rather than the rate at which data is sent.
 *   o To speed convergence to the right rate, we set the cwnd
 *     to achieve the right ("actual") rate when we exit slow start.
 *   o To filter out the noise caused by delayed ACKs, we use the
 *     minimum RTT sample observed during the last RTT to calculate
 *     the actual rate.
 *   o When the sender re-starts from idle, it waits until it has
 *     received ACKs for an entire flight of new data before making
 *     a cwnd adjustment decision. The original Vegas implementation
 *     assumed senders never went idle.
 *
 *
 *   TCP Compound based on TCP Vegas
 *
 *   further details can be found here:
 *      ftp://ftp.research.microsoft.com/pub/tr/TR-2005-86.pdf
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

/* Default values of the Vegas variables, in fixed-point representation
 * with V_PARAM_SHIFT bits to the right of the binary point.
 */
#define V_PARAM_SHIFT 1

#define TCP_COMPOUND_ALPHA          3U
#define TCP_COMPOUND_BETA           1U
#define TCP_COMPOUND_GAMMA         30
#define TCP_COMPOUND_ZETA           1

/* TCP compound variables */
struct compound {
	u32 beg_snd_nxt;	/* right edge during last RTT */
	u32 beg_snd_una;	/* left edge  during last RTT */
	u32 beg_snd_cwnd;	/* saves the size of the cwnd */
	u8 doing_vegas_now;	/* if true, do vegas for this RTT */
	u16 cntRTT;		/* # of RTTs measured within last RTT */
	u32 minRTT;		/* min of RTTs measured within last RTT (in usec) */
	u32 baseRTT;		/* the min of all Vegas RTT measurements seen (in usec) */

	u32 cwnd;
	u32 dwnd;
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
	struct compound *vegas = inet_csk_ca(sk);

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
	struct compound *vegas = inet_csk_ca(sk);

	vegas->doing_vegas_now = 0;
}

static void tcp_compound_init(struct sock *sk)
{
	struct compound *vegas = inet_csk_ca(sk);
	const struct tcp_sock *tp = tcp_sk(sk);

	vegas->baseRTT = 0x7fffffff;
	vegas_enable(sk);

	vegas->dwnd = 0;
	vegas->cwnd = tp->snd_cwnd;
}

/* Do RTT sampling needed for Vegas.
 * Basically we:
 *   o min-filter RTT samples from within an RTT to get the current
 *     propagation delay + queuing delay (we are min-filtering to try to
 *     avoid the effects of delayed ACKs)
 *   o min-filter RTT samples from a much longer window (forever for now)
 *     to find the propagation delay (baseRTT)
 */
static void tcp_compound_rtt_calc(struct sock *sk, u32 usrtt)
{
	struct compound *vegas = inet_csk_ca(sk);
	u32 vrtt = usrtt + 1;	/* Never allow zero rtt or baseRTT */

	/* Filter to find propagation delay: */
	if (vrtt < vegas->baseRTT)
		vegas->baseRTT = vrtt;

	/* Find the min RTT during the last RTT to find
	 * the current prop. delay + queuing delay:
	 */

	vegas->minRTT = min(vegas->minRTT, vrtt);
	vegas->cntRTT++;
}

static void tcp_compound_state(struct sock *sk, u8 ca_state)
{

	if (ca_state == TCP_CA_Open)
		vegas_enable(sk);
	else
		vegas_disable(sk);
}


/* 64bit divisor, dividend and result. dynamic precision */
static inline u64 div64_64(u64 dividend, u64 divisor)
{
	u32 d = divisor;

	if (divisor > 0xffffffffULL) {
		unsigned int shift = fls(divisor >> 32);

		d = divisor >> shift;
		dividend >>= shift;
	}

	/* avoid 64 bit division if possible */
	if (dividend >> 32)
		do_div(dividend, d);
	else
		dividend = (u32) dividend / d;

	return dividend;
}

/* calculate the quartic root of "a" using Newton-Raphson */
static u32 qroot(u64 a)
{
	u32 x, x1;

	/* Initial estimate is based on:
	 * qrt(x) = exp(log(x) / 4)
	 */
	x = 1u << (fls64(a) >> 2);

	/*
	 * Iteration based on:
	 *                         3
	 * x    = ( 3 * x  +  a / x  ) / 4
	 *  k+1          k         k
	 */
	do {
		u64 x3 = x;

		x1 = x;
		x3 *= x;
		x3 *= x;

		x = (3 * x + (u32) div64_64(a, x3)) / 4;
	} while (abs(x1 - x) > 1);

	return x;
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
static void tcp_compound_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART || event == CA_EVENT_TX_START)
		tcp_compound_init(sk);
}

static void tcp_compound_cong_avoid(struct sock *sk, u32 ack,
				    u32 seq_rtt, u32 in_flight, int flag)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct compound *vegas = inet_csk_ca(sk);
	u8 inc = 0;

	if (vegas->cwnd + vegas->dwnd > tp->snd_cwnd) {
		if (vegas->cwnd > tp->snd_cwnd || vegas->dwnd > tp->snd_cwnd) {
			vegas->cwnd = tp->snd_cwnd;
			vegas->dwnd = 0;
		} else
			vegas->cwnd = tp->snd_cwnd - vegas->dwnd;

	}

	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	if (vegas->cwnd <= tp->snd_ssthresh)
		inc = 1;
	else if (tp->snd_cwnd_cnt < tp->snd_cwnd)
		tp->snd_cwnd_cnt++;

	if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
		inc = 1;
		tp->snd_cwnd_cnt = 0;
	}

	if (inc && tp->snd_cwnd < tp->snd_cwnd_clamp)
		vegas->cwnd++;

	/* The key players are v_beg_snd_una and v_beg_snd_nxt.
	 *
	 * These are so named because they represent the approximate values
	 * of snd_una and snd_nxt at the beginning of the current RTT. More
	 * precisely, they represent the amount of data sent during the RTT.
	 * At the end of the RTT, when we receive an ACK for v_beg_snd_nxt,
	 * we will calculate that (v_beg_snd_nxt - v_beg_snd_una) outstanding
	 * bytes of data have been ACKed during the course of the RTT, giving
	 * an "actual" rate of:
	 *
	 *     (v_beg_snd_nxt - v_beg_snd_una) / (rtt duration)
	 *
	 * Unfortunately, v_beg_snd_una is not exactly equal to snd_una,
	 * because delayed ACKs can cover more than one segment, so they
	 * don't line up nicely with the boundaries of RTTs.
	 *
	 * Another unfortunate fact of life is that delayed ACKs delay the
	 * advance of the left edge of our send window, so that the number
	 * of bytes we send in an RTT is often less than our cwnd will allow.
	 * So we keep track of our cwnd separately, in v_beg_snd_cwnd.
	 */

	if (after(ack, vegas->beg_snd_nxt)) {
		/* Do the Vegas once-per-RTT cwnd adjustment. */
		u32 old_wnd, old_snd_cwnd;

		/* Here old_wnd is essentially the window of data that was
		 * sent during the previous RTT, and has all
		 * been acknowledged in the course of the RTT that ended
		 * with the ACK we just received. Likewise, old_snd_cwnd
		 * is the cwnd during the previous RTT.
		 */
		if (!tp->mss_cache)
			return;

		old_wnd = (vegas->beg_snd_nxt - vegas->beg_snd_una) /
		    tp->mss_cache;
		old_snd_cwnd = vegas->beg_snd_cwnd;

		/* Save the extent of the current window so we can use this
		 * at the end of the next RTT.
		 */
		vegas->beg_snd_una = vegas->beg_snd_nxt;
		vegas->beg_snd_nxt = tp->snd_nxt;
		vegas->beg_snd_cwnd = tp->snd_cwnd;

		/* We do the Vegas calculations only if we got enough RTT
		 * samples that we can be reasonably sure that we got
		 * at least one RTT sample that wasn't from a delayed ACK.
		 * If we only had 2 samples total,
		 * then that means we're getting only 1 ACK per RTT, which
		 * means they're almost certainly delayed ACKs.
		 * If  we have 3 samples, we should be OK.
		 */

		if (vegas->cntRTT > 2) {
			u32 rtt, target_cwnd, diff;
			u32 brtt, dwnd;

			/* We have enough RTT samples, so, using the Vegas
			 * algorithm, we determine if we should increase or
			 * decrease cwnd, and by how much.
			 */

			/* Pluck out the RTT we are using for the Vegas
			 * calculations. This is the min RTT seen during the
			 * last RTT. Taking the min filters out the effects
			 * of delayed ACKs, at the cost of noticing congestion
			 * a bit later.
			 */
			rtt = vegas->minRTT;

			/* Calculate the cwnd we should have, if we weren't
			 * going too fast.
			 *
			 * This is:
			 *     (actual rate in segments) * baseRTT
			 * We keep it as a fixed point number with
			 * V_PARAM_SHIFT bits to the right of the binary point.
			 */
			if (!rtt)
				return;

			brtt = vegas->baseRTT;
			target_cwnd = ((old_wnd * brtt)
				       << V_PARAM_SHIFT) / rtt;

			/* Calculate the difference between the window we had,
			 * and the window we would like to have. This quantity
			 * is the "Diff" from the Arizona Vegas papers.
			 *
			 * Again, this is a fixed point number with
			 * V_PARAM_SHIFT bits to the right of the binary
			 * point.
			 */

			diff = (old_wnd << V_PARAM_SHIFT) - target_cwnd;

			dwnd = vegas->dwnd;

			if (diff < (TCP_COMPOUND_GAMMA << V_PARAM_SHIFT)) {
				u64 v;
				u32 x;

				/*
				 * The TCP Compound paper describes the choice
				 * of "k" determines the agressiveness,
				 * ie. slope of the response function.
				 *
				 * For same value as HSTCP would be 0.8
				 * but for computaional reasons, both the
				 * original authors and this implementation
				 * use 0.75.
				 */
				v = old_wnd;
				x = qroot(v * v * v) >> TCP_COMPOUND_ALPHA;
				if (x > 1)
					dwnd = x - 1;
				else
					dwnd = 0;

				dwnd += vegas->dwnd;

			} else if ((dwnd << V_PARAM_SHIFT) <
				   (diff * TCP_COMPOUND_BETA))
				dwnd = 0;
			else
				dwnd =
				    ((dwnd << V_PARAM_SHIFT) -
				     (diff *
				      TCP_COMPOUND_BETA)) >> V_PARAM_SHIFT;

			vegas->dwnd = dwnd;

		}

		/* Wipe the slate clean for the next RTT. */
		vegas->cntRTT = 0;
		vegas->minRTT = 0x7fffffff;
	}

	tp->snd_cwnd = vegas->cwnd + vegas->dwnd;
}

/* Extract info for Tcp socket info provided via netlink. */
static void tcp_compound_get_info(struct sock *sk, u32 ext, struct sk_buff *skb)
{
	const struct compound *ca = inet_csk_ca(sk);
	if (ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		struct tcpvegas_info *info;

		info = RTA_DATA(__RTA_PUT(skb, INET_DIAG_VEGASINFO,
					  sizeof(*info)));

		info->tcpv_enabled = ca->doing_vegas_now;
		info->tcpv_rttcnt = ca->cntRTT;
		info->tcpv_rtt = ca->baseRTT;
		info->tcpv_minrtt = ca->minRTT;
	rtattr_failure:;
	}
}

static struct tcp_congestion_ops tcp_compound = {
	.init		= tcp_compound_init,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_compound_cong_avoid,
	.min_cwnd	= tcp_reno_min_cwnd,
	.rtt_sample	= tcp_compound_rtt_calc,
	.set_state	= tcp_compound_state,
	.cwnd_event	= tcp_compound_cwnd_event,
	.get_info	= tcp_compound_get_info,

	.owner		= THIS_MODULE,
	.name		= "compound",
};

static int __init tcp_compound_register(void)
{
	BUG_ON(sizeof(struct compound) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_compound);
	return 0;
}

static void __exit tcp_compound_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_compound);
}

module_init(tcp_compound_register);
module_exit(tcp_compound_unregister);

MODULE_AUTHOR("Angelo P. Castellani, Stephen Hemminger");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Compound");
