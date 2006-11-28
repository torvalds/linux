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
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

/* Default values of the Vegas variables, in fixed-point representation
 * with V_PARAM_SHIFT bits to the right of the binary point.
 */
#define V_PARAM_SHIFT 1
static int alpha = 2<<V_PARAM_SHIFT;
static int beta  = 4<<V_PARAM_SHIFT;
static int gamma = 1<<V_PARAM_SHIFT;

module_param(alpha, int, 0644);
MODULE_PARM_DESC(alpha, "lower bound of packets in network (scale by 2)");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "upper bound of packets in network (scale by 2)");
module_param(gamma, int, 0644);
MODULE_PARM_DESC(gamma, "limit on increase (scale by 2)");


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

/* Do RTT sampling needed for Vegas.
 * Basically we:
 *   o min-filter RTT samples from within an RTT to get the current
 *     propagation delay + queuing delay (we are min-filtering to try to
 *     avoid the effects of delayed ACKs)
 *   o min-filter RTT samples from a much longer window (forever for now)
 *     to find the propagation delay (baseRTT)
 */
static void tcp_vegas_rtt_calc(struct sock *sk, u32 usrtt)
{
	struct vegas *vegas = inet_csk_ca(sk);
	u32 vrtt = usrtt + 1; /* Never allow zero rtt or baseRTT */

	/* Filter to find propagation delay: */
	if (vrtt < vegas->baseRTT)
		vegas->baseRTT = vrtt;

	/* Find the min RTT during the last RTT to find
	 * the current prop. delay + queuing delay:
	 */
	vegas->minRTT = min(vegas->minRTT, vrtt);
	vegas->cntRTT++;
}

static void tcp_vegas_state(struct sock *sk, u8 ca_state)
{

	if (ca_state == TCP_CA_Open)
		vegas_enable(sk);
	else
		vegas_disable(sk);
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

static void tcp_vegas_cong_avoid(struct sock *sk, u32 ack,
				 u32 seq_rtt, u32 in_flight, int flag)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct vegas *vegas = inet_csk_ca(sk);

	if (!vegas->doing_vegas_now)
		return tcp_reno_cong_avoid(sk, ack, seq_rtt, in_flight, flag);

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
		old_wnd = (vegas->beg_snd_nxt - vegas->beg_snd_una) /
			tp->mss_cache;
		old_snd_cwnd = vegas->beg_snd_cwnd;

		/* Save the extent of the current window so we can use this
		 * at the end of the next RTT.
		 */
		vegas->beg_snd_una  = vegas->beg_snd_nxt;
		vegas->beg_snd_nxt  = tp->snd_nxt;
		vegas->beg_snd_cwnd = tp->snd_cwnd;

		/* We do the Vegas calculations only if we got enough RTT
		 * samples that we can be reasonably sure that we got
		 * at least one RTT sample that wasn't from a delayed ACK.
		 * If we only had 2 samples total,
		 * then that means we're getting only 1 ACK per RTT, which
		 * means they're almost certainly delayed ACKs.
		 * If  we have 3 samples, we should be OK.
		 */

		if (vegas->cntRTT <= 2) {
			/* We don't have enough RTT samples to do the Vegas
			 * calculation, so we'll behave like Reno.
			 */
			tcp_reno_cong_avoid(sk, ack, seq_rtt, in_flight, flag);
		} else {
			u32 rtt, target_cwnd, diff;

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
			target_cwnd = ((old_wnd * vegas->baseRTT)
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

			if (tp->snd_cwnd <= tp->snd_ssthresh) {
				/* Slow start.  */
				if (diff > gamma) {
					/* Going too fast. Time to slow down
					 * and switch to congestion avoidance.
					 */
					tp->snd_ssthresh = 2;

					/* Set cwnd to match the actual rate
					 * exactly:
					 *   cwnd = (actual rate) * baseRTT
					 * Then we add 1 because the integer
					 * truncation robs us of full link
					 * utilization.
					 */
					tp->snd_cwnd = min(tp->snd_cwnd,
							   (target_cwnd >>
							    V_PARAM_SHIFT)+1);

				}
				tcp_slow_start(tp);
			} else {
				/* Congestion avoidance. */
				u32 next_snd_cwnd;

				/* Figure out where we would like cwnd
				 * to be.
				 */
				if (diff > beta) {
					/* The old window was too fast, so
					 * we slow down.
					 */
					next_snd_cwnd = old_snd_cwnd - 1;
				} else if (diff < alpha) {
					/* We don't have enough extra packets
					 * in the network, so speed up.
					 */
					next_snd_cwnd = old_snd_cwnd + 1;
				} else {
					/* Sending just as fast as we
					 * should be.
					 */
					next_snd_cwnd = old_snd_cwnd;
				}

				/* Adjust cwnd upward or downward, toward the
				 * desired value.
				 */
				if (next_snd_cwnd > tp->snd_cwnd)
					tp->snd_cwnd++;
				else if (next_snd_cwnd < tp->snd_cwnd)
					tp->snd_cwnd--;
			}

			if (tp->snd_cwnd < 2)
				tp->snd_cwnd = 2;
			else if (tp->snd_cwnd > tp->snd_cwnd_clamp)
				tp->snd_cwnd = tp->snd_cwnd_clamp;
		}

		/* Wipe the slate clean for the next RTT. */
		vegas->cntRTT = 0;
		vegas->minRTT = 0x7fffffff;
	}
	/* Use normal slow start */
	else if (tp->snd_cwnd <= tp->snd_ssthresh) 
		tcp_slow_start(tp);
	
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

static struct tcp_congestion_ops tcp_vegas = {
	.init		= tcp_vegas_init,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_vegas_cong_avoid,
	.min_cwnd	= tcp_reno_min_cwnd,
	.rtt_sample	= tcp_vegas_rtt_calc,
	.set_state	= tcp_vegas_state,
	.cwnd_event	= tcp_vegas_cwnd_event,
	.get_info	= tcp_vegas_get_info,

	.owner		= THIS_MODULE,
	.name		= "vegas",
};

static int __init tcp_vegas_register(void)
{
	BUILD_BUG_ON(sizeof(struct vegas) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_vegas);
	return 0;
}

static void __exit tcp_vegas_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_vegas);
}

module_init(tcp_vegas_register);
module_exit(tcp_vegas_unregister);

MODULE_AUTHOR("Stephen Hemminger");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Vegas");
