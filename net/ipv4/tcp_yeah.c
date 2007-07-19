/*
 *
 *   YeAH TCP
 *
 * For further details look at:
 *    http://wil.cs.caltech.edu/pfldnet2007/paper/YeAH_TCP.pdf
 *
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

#include "tcp_vegas.h"

#define TCP_YEAH_ALPHA       80 //lin number of packets queued at the bottleneck
#define TCP_YEAH_GAMMA        1 //lin fraction of queue to be removed per rtt
#define TCP_YEAH_DELTA        3 //log minimum fraction of cwnd to be removed on loss
#define TCP_YEAH_EPSILON      1 //log maximum fraction to be removed on early decongestion
#define TCP_YEAH_PHY          8 //lin maximum delta from base
#define TCP_YEAH_RHO         16 //lin minumum number of consecutive rtt to consider competition on loss
#define TCP_YEAH_ZETA        50 //lin minimum number of state switchs to reset reno_count

#define TCP_SCALABLE_AI_CNT	 100U

/* YeAH variables */
struct yeah {
	struct vegas vegas;	/* must be first */

	/* YeAH */
	u32 lastQ;
	u32 doing_reno_now;

	u32 reno_count;
	u32 fast_count;

	u32 pkts_acked;
};

static void tcp_yeah_init(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct yeah *yeah = inet_csk_ca(sk);

	tcp_vegas_init(sk);

	yeah->doing_reno_now = 0;
	yeah->lastQ = 0;

	yeah->reno_count = 2;

	/* Ensure the MD arithmetic works.  This is somewhat pedantic,
	 * since I don't think we will see a cwnd this large. :) */
	tp->snd_cwnd_clamp = min_t(u32, tp->snd_cwnd_clamp, 0xffffffff/128);

}


static void tcp_yeah_pkts_acked(struct sock *sk, u32 pkts_acked, ktime_t last)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct yeah *yeah = inet_csk_ca(sk);

	if (icsk->icsk_ca_state == TCP_CA_Open)
		yeah->pkts_acked = pkts_acked;

	tcp_vegas_pkts_acked(sk, pkts_acked, last);
}

static void tcp_yeah_cong_avoid(struct sock *sk, u32 ack,
				u32 in_flight, int flag)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct yeah *yeah = inet_csk_ca(sk);

	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	if (tp->snd_cwnd <= tp->snd_ssthresh)
		tcp_slow_start(tp);

	else if (!yeah->doing_reno_now) {
		/* Scalable */

		tp->snd_cwnd_cnt+=yeah->pkts_acked;
		if (tp->snd_cwnd_cnt > min(tp->snd_cwnd, TCP_SCALABLE_AI_CNT)){
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
			tp->snd_cwnd_cnt = 0;
		}

		yeah->pkts_acked = 1;

	} else {
		/* Reno */

		if (tp->snd_cwnd_cnt < tp->snd_cwnd)
			tp->snd_cwnd_cnt++;

		if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
			tp->snd_cwnd++;
			tp->snd_cwnd_cnt = 0;
		}
	}

	/* The key players are v_vegas.beg_snd_una and v_beg_snd_nxt.
	 *
	 * These are so named because they represent the approximate values
	 * of snd_una and snd_nxt at the beginning of the current RTT. More
	 * precisely, they represent the amount of data sent during the RTT.
	 * At the end of the RTT, when we receive an ACK for v_beg_snd_nxt,
	 * we will calculate that (v_beg_snd_nxt - v_vegas.beg_snd_una) outstanding
	 * bytes of data have been ACKed during the course of the RTT, giving
	 * an "actual" rate of:
	 *
	 *     (v_beg_snd_nxt - v_vegas.beg_snd_una) / (rtt duration)
	 *
	 * Unfortunately, v_vegas.beg_snd_una is not exactly equal to snd_una,
	 * because delayed ACKs can cover more than one segment, so they
	 * don't line up yeahly with the boundaries of RTTs.
	 *
	 * Another unfortunate fact of life is that delayed ACKs delay the
	 * advance of the left edge of our send window, so that the number
	 * of bytes we send in an RTT is often less than our cwnd will allow.
	 * So we keep track of our cwnd separately, in v_beg_snd_cwnd.
	 */

	if (after(ack, yeah->vegas.beg_snd_nxt)) {

		/* We do the Vegas calculations only if we got enough RTT
		 * samples that we can be reasonably sure that we got
		 * at least one RTT sample that wasn't from a delayed ACK.
		 * If we only had 2 samples total,
		 * then that means we're getting only 1 ACK per RTT, which
		 * means they're almost certainly delayed ACKs.
		 * If  we have 3 samples, we should be OK.
		 */

		if (yeah->vegas.cntRTT > 2) {
			u32 rtt, queue;
			u64 bw;

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
			rtt = yeah->vegas.minRTT;

			/* Compute excess number of packets above bandwidth
			 * Avoid doing full 64 bit divide.
			 */
			bw = tp->snd_cwnd;
			bw *= rtt - yeah->vegas.baseRTT;
			do_div(bw, rtt);
			queue = bw;

			if (queue > TCP_YEAH_ALPHA ||
			    rtt - yeah->vegas.baseRTT > (yeah->vegas.baseRTT / TCP_YEAH_PHY)) {
				if (queue > TCP_YEAH_ALPHA
				    && tp->snd_cwnd > yeah->reno_count) {
					u32 reduction = min(queue / TCP_YEAH_GAMMA ,
							    tp->snd_cwnd >> TCP_YEAH_EPSILON);

					tp->snd_cwnd -= reduction;

					tp->snd_cwnd = max(tp->snd_cwnd,
							   yeah->reno_count);

					tp->snd_ssthresh = tp->snd_cwnd;
				}

				if (yeah->reno_count <= 2)
					yeah->reno_count = max(tp->snd_cwnd>>1, 2U);
				else
					yeah->reno_count++;

				yeah->doing_reno_now = min(yeah->doing_reno_now + 1,
							   0xffffffU);
			} else {
				yeah->fast_count++;

				if (yeah->fast_count > TCP_YEAH_ZETA) {
					yeah->reno_count = 2;
					yeah->fast_count = 0;
				}

				yeah->doing_reno_now = 0;
			}

			yeah->lastQ = queue;

		}

		/* Save the extent of the current window so we can use this
		 * at the end of the next RTT.
		 */
		yeah->vegas.beg_snd_una  = yeah->vegas.beg_snd_nxt;
		yeah->vegas.beg_snd_nxt  = tp->snd_nxt;
		yeah->vegas.beg_snd_cwnd = tp->snd_cwnd;

		/* Wipe the slate clean for the next RTT. */
		yeah->vegas.cntRTT = 0;
		yeah->vegas.minRTT = 0x7fffffff;
	}
}

static u32 tcp_yeah_ssthresh(struct sock *sk) {
	const struct tcp_sock *tp = tcp_sk(sk);
	struct yeah *yeah = inet_csk_ca(sk);
	u32 reduction;

	if (yeah->doing_reno_now < TCP_YEAH_RHO) {
		reduction = yeah->lastQ;

		reduction = min( reduction, max(tp->snd_cwnd>>1, 2U) );

		reduction = max( reduction, tp->snd_cwnd >> TCP_YEAH_DELTA);
	} else
		reduction = max(tp->snd_cwnd>>1,2U);

	yeah->fast_count = 0;
	yeah->reno_count = max(yeah->reno_count>>1, 2U);

	return tp->snd_cwnd - reduction;
}

static struct tcp_congestion_ops tcp_yeah = {
	.flags		= TCP_CONG_RTT_STAMP,
	.init		= tcp_yeah_init,
	.ssthresh	= tcp_yeah_ssthresh,
	.cong_avoid	= tcp_yeah_cong_avoid,
	.min_cwnd	= tcp_reno_min_cwnd,
	.set_state	= tcp_vegas_state,
	.cwnd_event	= tcp_vegas_cwnd_event,
	.get_info	= tcp_vegas_get_info,
	.pkts_acked	= tcp_yeah_pkts_acked,

	.owner		= THIS_MODULE,
	.name		= "yeah",
};

static int __init tcp_yeah_register(void)
{
	BUG_ON(sizeof(struct yeah) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_yeah);
	return 0;
}

static void __exit tcp_yeah_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_yeah);
}

module_init(tcp_yeah_register);
module_exit(tcp_yeah_unregister);

MODULE_AUTHOR("Angelo P. Castellani");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("YeAH TCP");
