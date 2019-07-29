/*
 * CAIA Delay-Gradient (CDG) congestion control
 *
 * This implementation is based on the paper:
 *   D.A. Hayes and G. Armitage. "Revisiting TCP congestion control using
 *   delay gradients." In IFIP Networking, pages 328-341. Springer, 2011.
 *
 * Scavenger traffic (Less-than-Best-Effort) should disable coexistence
 * heuristics using parameters use_shadow=0 and use_ineff=0.
 *
 * Parameters window, backoff_beta, and backoff_factor are crucial for
 * throughput and delay. Future work is needed to determine better defaults,
 * and to provide guidelines for use in different environments/contexts.
 *
 * Except for window, knobs are configured via /sys/module/tcp_cdg/parameters/.
 * Parameter window is only configurable when loading tcp_cdg as a module.
 *
 * Notable differences from paper/FreeBSD:
 *   o Using Hybrid Slow start and Proportional Rate Reduction.
 *   o Add toggle for shadow window mechanism. Suggested by David Hayes.
 *   o Add toggle for non-congestion loss tolerance.
 *   o Scaling parameter G is changed to a backoff factor;
 *     conversion is given by: backoff_factor = 1000/(G * window).
 *   o Limit shadow window to 2 * cwnd, or to cwnd when application limited.
 *   o More accurate e^-x.
 */
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/sched/clock.h>

#include <net/tcp.h>

#define HYSTART_ACK_TRAIN	1
#define HYSTART_DELAY		2

static int window __read_mostly = 8;
static unsigned int backoff_beta __read_mostly = 0.7071 * 1024; /* sqrt 0.5 */
static unsigned int backoff_factor __read_mostly = 42;
static unsigned int hystart_detect __read_mostly = 3;
static unsigned int use_ineff __read_mostly = 5;
static bool use_shadow __read_mostly = true;
static bool use_tolerance __read_mostly;

module_param(window, int, 0444);
MODULE_PARM_DESC(window, "gradient window size (power of two <= 256)");
module_param(backoff_beta, uint, 0644);
MODULE_PARM_DESC(backoff_beta, "backoff beta (0-1024)");
module_param(backoff_factor, uint, 0644);
MODULE_PARM_DESC(backoff_factor, "backoff probability scale factor");
module_param(hystart_detect, uint, 0644);
MODULE_PARM_DESC(hystart_detect, "use Hybrid Slow start "
		 "(0: disabled, 1: ACK train, 2: delay threshold, 3: both)");
module_param(use_ineff, uint, 0644);
MODULE_PARM_DESC(use_ineff, "use ineffectual backoff detection (threshold)");
module_param(use_shadow, bool, 0644);
MODULE_PARM_DESC(use_shadow, "use shadow window heuristic");
module_param(use_tolerance, bool, 0644);
MODULE_PARM_DESC(use_tolerance, "use loss tolerance heuristic");

struct cdg_minmax {
	union {
		struct {
			s32 min;
			s32 max;
		};
		u64 v64;
	};
};

enum cdg_state {
	CDG_UNKNOWN = 0,
	CDG_NONFULL = 1,
	CDG_FULL    = 2,
	CDG_BACKOFF = 3,
};

struct cdg {
	struct cdg_minmax rtt;
	struct cdg_minmax rtt_prev;
	struct cdg_minmax *gradients;
	struct cdg_minmax gsum;
	bool gfilled;
	u8  tail;
	u8  state;
	u8  delack;
	u32 rtt_seq;
	u32 shadow_wnd;
	u16 backoff_cnt;
	u16 sample_cnt;
	s32 delay_min;
	u32 last_ack;
	u32 round_start;
};

/**
 * nexp_u32 - negative base-e exponential
 * @ux: x in units of micro
 *
 * Returns exp(ux * -1e-6) * U32_MAX.
 */
static u32 __pure nexp_u32(u32 ux)
{
	static const u16 v[] = {
		/* exp(-x)*65536-1 for x = 0, 0.000256, 0.000512, ... */
		65535,
		65518, 65501, 65468, 65401, 65267, 65001, 64470, 63422,
		61378, 57484, 50423, 38795, 22965, 8047,  987,   14,
	};
	u32 msb = ux >> 8;
	u32 res;
	int i;

	/* Cut off when ux >= 2^24 (actual result is <= 222/U32_MAX). */
	if (msb > U16_MAX)
		return 0;

	/* Scale first eight bits linearly: */
	res = U32_MAX - (ux & 0xff) * (U32_MAX / 1000000);

	/* Obtain e^(x + y + ...) by computing e^x * e^y * ...: */
	for (i = 1; msb; i++, msb >>= 1) {
		u32 y = v[i & -(msb & 1)] + U32_C(1);

		res = ((u64)res * y) >> 16;
	}

	return res;
}

/* Based on the HyStart algorithm (by Ha et al.) that is implemented in
 * tcp_cubic. Differences/experimental changes:
 *   o Using Hayes' delayed ACK filter.
 *   o Using a usec clock for the ACK train.
 *   o Reset ACK train when application limited.
 *   o Invoked at any cwnd (i.e. also when cwnd < 16).
 *   o Invoked only when cwnd < ssthresh (i.e. not when cwnd == ssthresh).
 */
static void tcp_cdg_hystart_update(struct sock *sk)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	ca->delay_min = min_not_zero(ca->delay_min, ca->rtt.min);
	if (ca->delay_min == 0)
		return;

	if (hystart_detect & HYSTART_ACK_TRAIN) {
		u32 now_us = tp->tcp_mstamp;

		if (ca->last_ack == 0 || !tcp_is_cwnd_limited(sk)) {
			ca->last_ack = now_us;
			ca->round_start = now_us;
		} else if (before(now_us, ca->last_ack + 3000)) {
			u32 base_owd = max(ca->delay_min / 2U, 125U);

			ca->last_ack = now_us;
			if (after(now_us, ca->round_start + base_owd)) {
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINCWND,
					      tp->snd_cwnd);
				tp->snd_ssthresh = tp->snd_cwnd;
				return;
			}
		}
	}

	if (hystart_detect & HYSTART_DELAY) {
		if (ca->sample_cnt < 8) {
			ca->sample_cnt++;
		} else {
			s32 thresh = max(ca->delay_min + ca->delay_min / 8U,
					 125U);

			if (ca->rtt.min > thresh) {
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYCWND,
					      tp->snd_cwnd);
				tp->snd_ssthresh = tp->snd_cwnd;
			}
		}
	}
}

static s32 tcp_cdg_grad(struct cdg *ca)
{
	s32 gmin = ca->rtt.min - ca->rtt_prev.min;
	s32 gmax = ca->rtt.max - ca->rtt_prev.max;
	s32 grad;

	if (ca->gradients) {
		ca->gsum.min += gmin - ca->gradients[ca->tail].min;
		ca->gsum.max += gmax - ca->gradients[ca->tail].max;
		ca->gradients[ca->tail].min = gmin;
		ca->gradients[ca->tail].max = gmax;
		ca->tail = (ca->tail + 1) & (window - 1);
		gmin = ca->gsum.min;
		gmax = ca->gsum.max;
	}

	/* We keep sums to ignore gradients during cwnd reductions;
	 * the paper's smoothed gradients otherwise simplify to:
	 * (rtt_latest - rtt_oldest) / window.
	 *
	 * We also drop division by window here.
	 */
	grad = gmin > 0 ? gmin : gmax;

	/* Extrapolate missing values in gradient window: */
	if (!ca->gfilled) {
		if (!ca->gradients && window > 1)
			grad *= window; /* Memory allocation failed. */
		else if (ca->tail == 0)
			ca->gfilled = true;
		else
			grad = (grad * window) / (int)ca->tail;
	}

	/* Backoff was effectual: */
	if (gmin <= -32 || gmax <= -32)
		ca->backoff_cnt = 0;

	if (use_tolerance) {
		/* Reduce small variations to zero: */
		gmin = DIV_ROUND_CLOSEST(gmin, 64);
		gmax = DIV_ROUND_CLOSEST(gmax, 64);

		if (gmin > 0 && gmax <= 0)
			ca->state = CDG_FULL;
		else if ((gmin > 0 && gmax > 0) || gmax < 0)
			ca->state = CDG_NONFULL;
	}
	return grad;
}

static bool tcp_cdg_backoff(struct sock *sk, u32 grad)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (prandom_u32() <= nexp_u32(grad * backoff_factor))
		return false;

	if (use_ineff) {
		ca->backoff_cnt++;
		if (ca->backoff_cnt > use_ineff)
			return false;
	}

	ca->shadow_wnd = max(ca->shadow_wnd, tp->snd_cwnd);
	ca->state = CDG_BACKOFF;
	tcp_enter_cwr(sk);
	return true;
}

/* Not called in CWR or Recovery state. */
static void tcp_cdg_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 prior_snd_cwnd;
	u32 incr;

	if (tcp_in_slow_start(tp) && hystart_detect)
		tcp_cdg_hystart_update(sk);

	if (after(ack, ca->rtt_seq) && ca->rtt.v64) {
		s32 grad = 0;

		if (ca->rtt_prev.v64)
			grad = tcp_cdg_grad(ca);
		ca->rtt_seq = tp->snd_nxt;
		ca->rtt_prev = ca->rtt;
		ca->rtt.v64 = 0;
		ca->last_ack = 0;
		ca->sample_cnt = 0;

		if (grad > 0 && tcp_cdg_backoff(sk, grad))
			return;
	}

	if (!tcp_is_cwnd_limited(sk)) {
		ca->shadow_wnd = min(ca->shadow_wnd, tp->snd_cwnd);
		return;
	}

	prior_snd_cwnd = tp->snd_cwnd;
	tcp_reno_cong_avoid(sk, ack, acked);

	incr = tp->snd_cwnd - prior_snd_cwnd;
	ca->shadow_wnd = max(ca->shadow_wnd, ca->shadow_wnd + incr);
}

static void tcp_cdg_acked(struct sock *sk, const struct ack_sample *sample)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (sample->rtt_us <= 0)
		return;

	/* A heuristic for filtering delayed ACKs, adapted from:
	 * D.A. Hayes. "Timing enhancements to the FreeBSD kernel to support
	 * delay and rate based TCP mechanisms." TR 100219A. CAIA, 2010.
	 */
	if (tp->sacked_out == 0) {
		if (sample->pkts_acked == 1 && ca->delack) {
			/* A delayed ACK is only used for the minimum if it is
			 * provenly lower than an existing non-zero minimum.
			 */
			ca->rtt.min = min(ca->rtt.min, sample->rtt_us);
			ca->delack--;
			return;
		} else if (sample->pkts_acked > 1 && ca->delack < 5) {
			ca->delack++;
		}
	}

	ca->rtt.min = min_not_zero(ca->rtt.min, sample->rtt_us);
	ca->rtt.max = max(ca->rtt.max, sample->rtt_us);
}

static u32 tcp_cdg_ssthresh(struct sock *sk)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (ca->state == CDG_BACKOFF)
		return max(2U, (tp->snd_cwnd * min(1024U, backoff_beta)) >> 10);

	if (ca->state == CDG_NONFULL && use_tolerance)
		return tp->snd_cwnd;

	ca->shadow_wnd = min(ca->shadow_wnd >> 1, tp->snd_cwnd);
	if (use_shadow)
		return max3(2U, ca->shadow_wnd, tp->snd_cwnd >> 1);
	return max(2U, tp->snd_cwnd >> 1);
}

static void tcp_cdg_cwnd_event(struct sock *sk, const enum tcp_ca_event ev)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct cdg_minmax *gradients;

	switch (ev) {
	case CA_EVENT_CWND_RESTART:
		gradients = ca->gradients;
		if (gradients)
			memset(gradients, 0, window * sizeof(gradients[0]));
		memset(ca, 0, sizeof(*ca));

		ca->gradients = gradients;
		ca->rtt_seq = tp->snd_nxt;
		ca->shadow_wnd = tp->snd_cwnd;
		break;
	case CA_EVENT_COMPLETE_CWR:
		ca->state = CDG_UNKNOWN;
		ca->rtt_seq = tp->snd_nxt;
		ca->rtt_prev = ca->rtt;
		ca->rtt.v64 = 0;
		break;
	default:
		break;
	}
}

static void tcp_cdg_init(struct sock *sk)
{
	struct cdg *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	/* We silently fall back to window = 1 if allocation fails. */
	if (window > 1)
		ca->gradients = kcalloc(window, sizeof(ca->gradients[0]),
					GFP_NOWAIT | __GFP_NOWARN);
	ca->rtt_seq = tp->snd_nxt;
	ca->shadow_wnd = tp->snd_cwnd;
}

static void tcp_cdg_release(struct sock *sk)
{
	struct cdg *ca = inet_csk_ca(sk);

	kfree(ca->gradients);
}

static struct tcp_congestion_ops tcp_cdg __read_mostly = {
	.cong_avoid = tcp_cdg_cong_avoid,
	.cwnd_event = tcp_cdg_cwnd_event,
	.pkts_acked = tcp_cdg_acked,
	.undo_cwnd = tcp_reno_undo_cwnd,
	.ssthresh = tcp_cdg_ssthresh,
	.release = tcp_cdg_release,
	.init = tcp_cdg_init,
	.owner = THIS_MODULE,
	.name = "cdg",
};

static int __init tcp_cdg_register(void)
{
	if (backoff_beta > 1024 || window < 1 || window > 256)
		return -ERANGE;
	if (!is_power_of_2(window))
		return -EINVAL;

	BUILD_BUG_ON(sizeof(struct cdg) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_cdg);
	return 0;
}

static void __exit tcp_cdg_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_cdg);
}

module_init(tcp_cdg_register);
module_exit(tcp_cdg_unregister);
MODULE_AUTHOR("Kenneth Klette Jonassen");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP CDG");
