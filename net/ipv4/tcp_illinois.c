/*
 * TCP Illinois congestion control.
 * Home page:
 *	http://www.ews.uiuc.edu/~shaoliu/tcpillinois/index.html
 *
 * The algorithm is described in:
 * "TCP-Illinois: A Loss and Delay-Based Congestion Control Algorithm
 *  for High-Speed Networks"
 * http://www.ews.uiuc.edu/~shaoliu/papersandslides/liubassri06perf.pdf
 *
 * Implemented from description in paper and ns-2 simulation.
 * Copyright (C) 2007 Stephen Hemminger <shemminger@linux-foundation.org>
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>
#include <asm/div64.h>
#include <net/tcp.h>

#define ALPHA_SHIFT	7
#define ALPHA_SCALE	(1u<<ALPHA_SHIFT)
#define ALPHA_MIN	((3*ALPHA_SCALE)/10)	/* ~0.3 */
#define ALPHA_MAX	(10*ALPHA_SCALE)	/* 10.0 */
#define ALPHA_BASE	ALPHA_SCALE		/* 1.0 */

#define BETA_SHIFT	6
#define BETA_SCALE	(1u<<BETA_SHIFT)
#define BETA_MIN	(BETA_SCALE/8)		/* 0.8 */
#define BETA_MAX	(BETA_SCALE/2)
#define BETA_BASE	BETA_MAX		/* 0.5 */

#define THETA		5

static int win_thresh __read_mostly = 15;
module_param(win_thresh, int, 0644);
MODULE_PARM_DESC(win_thresh, "Window threshold for starting adaptive sizing");

#define MAX_RTT		0x7fffffff

/* TCP Illinois Parameters */
struct tcp_illinois {
	u32	last_alpha;
	u32	min_rtt;
	u32	max_rtt;
	u32	rtt_low;
	u32	rtt_cnt;
	u64	sum_rtt;
};

static void tcp_illinois_init(struct sock *sk)
{
	struct tcp_illinois *ca = inet_csk_ca(sk);

	ca->last_alpha = ALPHA_BASE;
	ca->min_rtt = 0x7fffffff;
}

/*
 * Keep track of min, max and average RTT
 */
static void tcp_illinois_rtt_calc(struct sock *sk, u32 rtt)
{
	struct tcp_illinois *ca = inet_csk_ca(sk);

	if (rtt < ca->min_rtt)
		ca->min_rtt = rtt;
	if (rtt > ca->max_rtt)
		ca->max_rtt = rtt;

	if (++ca->rtt_cnt == 1)
		ca->sum_rtt = rtt;
	else
		ca->sum_rtt += rtt;
}

/* max queuing delay */
static inline u32 max_delay(const struct tcp_illinois *ca)
{
	return ca->max_rtt - ca->min_rtt;
}

/* average queueing delay */
static u32 avg_delay(struct tcp_illinois *ca)
{
	u64 avg_rtt = ca->sum_rtt;

	do_div(avg_rtt, ca->rtt_cnt);

	ca->sum_rtt = 0;
	ca->rtt_cnt = 0;

	return avg_rtt - ca->min_rtt;
}

/*
 * Compute value of alpha used for additive increase.
 * If small window then use 1.0, equivalent to Reno.
 *
 * For larger windows, adjust based on average delay.
 * A. If average delay is at minimum (we are uncongested),
 *    then use large alpha (10.0) to increase faster.
 * B. If average delay is at maximum (getting congested)
 *    then use small alpha (1.0)
 *
 * The result is a convex window growth curve.
 */
static u32 alpha(const struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_illinois *ca = inet_csk_ca(sk);
	u32 dm = max_delay(ca);
	u32 da = avg_delay(ca);
	u32 d1, a;

	if (tp->snd_cwnd < win_thresh)
		return ALPHA_BASE;	/* same as Reno (1.0) */

	d1 = dm / 100;
	if (da <= d1) {
		/* Don't let noise force agressive response */
		if (ca->rtt_low < THETA) {
			++ca->rtt_low;
			return ca->last_alpha;
		} else
			return ALPHA_MAX;
	}

	ca->rtt_low = 0;

	/*
	 * Based on:
	 *
	 *      (dm - d1) amin amax
	 * k1 = -------------------
	 *         amax - amin
	 *
	 *       (dm - d1) amin
	 * k2 = ----------------  - d1
	 *        amax - amin
	 *
	 *             k1
	 * alpha = ----------
	 *          k2 + da
	 */

	dm -= d1;
	da -= d1;

	a = (dm * ALPHA_MAX) / (dm - (da  * (ALPHA_MAX - ALPHA_MIN)) / ALPHA_MIN);
	ca->last_alpha = a;
	return a;
}

/*
 * Increase window in response to successful acknowledgment.
 */
static void tcp_illinois_cong_avoid(struct sock *sk, u32 ack, u32 rtt,
				    u32 in_flight, int flag)
{
	struct tcp_sock *tp = tcp_sk(sk);

	/* RFC2861 only increase cwnd if fully utilized */
	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	/* In slow start */
	if (tp->snd_cwnd <= tp->snd_ssthresh)
		tcp_slow_start(tp);

	else {
		/* additive increase  cwnd += alpha / cwnd */
		if ((tp->snd_cwnd_cnt * alpha(sk)) >> ALPHA_SHIFT >= tp->snd_cwnd) {
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
			tp->snd_cwnd_cnt = 0;
		} else
			tp->snd_cwnd_cnt++;
	}
}

/*
 * Beta used for multiplicative decrease.
 * For small window sizes returns same value as Reno (0.5)
 *
 * If delay is small (10% of max) then beta = 1/8
 * If delay is up to 80% of max then beta = 1/2
 * In between is a linear function
 */
static inline u32 beta(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_illinois *ca = inet_csk_ca(sk);
	u32 dm = max_delay(ca);
	u32 da = avg_delay(ca);
	u32 d2, d3;

	if (tp->snd_cwnd < win_thresh)
		return BETA_BASE;

	d2 = dm / 10;
	if (da <= d2)
		return BETA_MIN;
	d3 = (8 * dm) / 10;
	if (da >= d3 || d3 <= d2)
		return BETA_MAX;

	/*
	 * Based on:
	 *
	 *       bmin d3 - bmax d2
	 * k3 = -------------------
	 *         d3 - d2
	 *
	 *       bmax - bmin
	 * k4 = -------------
	 *         d3 - d2
	 *
	 * b = k3 + k4 da
	 */
	return (BETA_MIN * d3 - BETA_MAX * d2 + (BETA_MAX - BETA_MIN) * da)
		/ (d3 - d2);
}

static u32 tcp_illinois_ssthresh(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);

	/* Multiplicative decrease */
	return max((tp->snd_cwnd * beta(sk)) >> BETA_SHIFT, 2U);
}

/* Extract info for TCP socket info provided via netlink.
 * We aren't really doing Vegas, but we can provide RTT info
 */
static void tcp_illinois_get_info(struct sock *sk, u32 ext,
			       struct sk_buff *skb)
{
	const struct tcp_illinois *ca = inet_csk_ca(sk);

	if (ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		struct tcpvegas_info info = {
			.tcpv_enabled = 1,
			.tcpv_rttcnt = ca->rtt_cnt,
			.tcpv_minrtt = ca->min_rtt,
		};
		u64 avg_rtt = ca->sum_rtt;
		do_div(avg_rtt, ca->rtt_cnt);
		info.tcpv_rtt = avg_rtt;

		nla_put(skb, INET_DIAG_VEGASINFO, sizeof(info), &info);
	}
}

static struct tcp_congestion_ops tcp_illinois = {
	.init		= tcp_illinois_init,
	.ssthresh	= tcp_illinois_ssthresh,
	.min_cwnd	= tcp_reno_min_cwnd,
	.cong_avoid	= tcp_illinois_cong_avoid,
	.rtt_sample	= tcp_illinois_rtt_calc,
	.get_info	= tcp_illinois_get_info,

	.owner		= THIS_MODULE,
	.name		= "illinois",
};

static int __init tcp_illinois_register(void)
{
	BUILD_BUG_ON(sizeof(struct tcp_illinois) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&tcp_illinois);
}

static void __exit tcp_illinois_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_illinois);
}

module_init(tcp_illinois_register);
module_exit(tcp_illinois_unregister);

MODULE_AUTHOR("Stephen Hemminger, Shao Liu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Illinois");
MODULE_VERSION("0.3");
