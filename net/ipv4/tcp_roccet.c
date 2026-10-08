// SPDX-License-Identifier: GPL-2.0
/*
 * TCP ROCCET: An RTT-Oriented CUBIC Congestion Control
 * Extension for 5G and Beyond Networks
 *
 * TCP ROCCET is a new TCP congestion control
 * algorithm suited for current cellular 5G NR beyond networks.
 * It extends the kernel default congestion control CUBIC
 * and improves its performance, and additionally solves an
 * unwanted side effects of CUBIC’s implementation.
 * ROCCET uses its own Slow Start, called LAUNCH, where loss
 * is not considered as a congestion event.
 * The congestion avoidance phase, called ORBITER, uses
 * CUBIC's window growth function and adds, based on RTT
 * and ACK rate, congestion events.
 *
 * A peer-reviewed paper on TCP ROCCET which was presented
 * at the WONS 2026 conference is available here:
 *	https://opendl.ifip-tc6.org/db/conf/wons/wons2026/1571217211.pdf
 *
 *
 * This module is based on the kernel CUBIC TCP implementation
 * (see net/ipv4/tcp_cubic.c). Branched module at kernel version 7.1.
 */

#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/time.h>
#include <net/tcp.h>
#include <vdso/time64.h>

/* Scale factor beta calculation (max_cwnd = snd_cwnd * beta) */
#define BICTCP_BETA_SCALE 1024

#define BICTCP_HZ 10 /* BIC HZ 2^10 = 1024 */

/* Alpha value for the srRTT  multiplied by 100.
 * Here 20 represents a value of 0.2
 */
#define ROCCET_ALPHA_TIMES_100 20

/* min RTT probe period in ms */
#define ROCCET_NEXT_MIN_RTT_PROBE_MS 5000

/* The maximum interval length of a min RTT probe
 * (and consequently also the max interval for the refill).
 */
#define ROCCET_MAX_PROBE_INTERVAL_MS 10000

#define ROCCET_MAX_SRRTT_CHECK_INTERVAL_MS (5 * 200)

/* State in which roccet currently operates */
enum roccet_state {
	LAUNCH,
	ORBITER,
	RTT_PROBE_ENTER,
	RTT_PROBE,
	RTT_PROBE_REFILL,
	DRAIN
};

/* TCP ROCCET struct based on the original BICTCP struct with
 * additions specific to the ROCCET-Algorithm.
 */
struct roccettcp {
	u32 cnt;	/* increase cwnd by 1 after ACKs */
	u32 last_max_cwnd;	/* last maximum snd_cwnd */
	u32 last_cwnd;	/* the last snd_cwnd */
	u32 last_time;	/* time when updated last_cwnd */
	u32 bic_origin_point;	/* origin point of bic function */
	u32 bic_K;	/* time to origin point from the
			 * beginning of the current epoch
			 */
	u32 delay_min;	/* min delay (usec) */
	u32 epoch_start;	/* beginning of an epoch */
	u32 ack_cnt;	/* number of acks */
	u32 tcp_cwnd;	/* estimated tcp cwnd */
	u32 curr_rtt;	/* last sample rtt of current round */

	u32 roccet_last_event_time_us;	/* The last time ROCCET was triggered */
	u32 curr_min_rtt;	/* The current observed minRTT */
	u32 next_min_rtt_probe;	/* Next time to probe the minRTT */
	u32 probe_min_rtt_until;	/* End of minRTT probing period.
					 * Set while in RTT_PROBE states
					 */
	u32 refill_until;	/* End of pipe refill after minRTT probe.
				 * Set while in RTT_PROBE states
				 */
	u32 cwnd_before_min_rtt_probe;	/* cwnd before min RTT probing. */
	u32 curr_srrtt;	/* srRTT calculated based on the latest ACK */
	u32 next_srrtt_check_ts;	/* Next check for srRTT */
	u32 last_rtt;	/* sample rtt of previous round.
			 * Used for jitter calculation
			 */

	u32 interval_snd_seq_start;
	u32 interval_una_seq_start;

	u32 ack_carry_over;	/* Used to carry over leftover acks from
				 * LAUNCH to ORBITER
				 */

	u32 last_ack_rate_time;	/* Timestamp of the last ACK-rate */
	u16 ack_rate_last_rate;	/* Last ACK-rate */
	u16 curr_ack_rate;	/* Current ACK-rate */
	u16 ack_rate_cnt;	/* Used for counting acks */

	enum roccet_state state : 3; /* Current operating state of roccet */
	bool initial_limit_reached: 1;	/* Set to true after the connection
					 * initially gets cwnd-limited
					 */
	bool is_in_initial_launch: 1;	/* true if the connection is in
					 * the initial launch phase.
					 */
	bool last_event_time_set: 1;	/* true if the last event time
					 * has been set/initialized.
					 */
	bool rtt_probe_timers_set: 1;	/* true if refill_until and
					 * probe_min_rtt_until are currently
					 * set.
					 */
};

/* Parameters that are specific to the ROCCET-Algorithm */
static uint sr_rtt_upper_bound __read_mostly = 100;
static int ack_rate_diff_ss __read_mostly = 10;

module_param(sr_rtt_upper_bound, uint, 0644);
MODULE_PARM_DESC(sr_rtt_upper_bound, "ROCCET's upper bound for srRTT.");
module_param(ack_rate_diff_ss, int, 0644);
MODULE_PARM_DESC(ack_rate_diff_ss,
		 "ROCCET's threshold to exit slow start if ACK-rate differs by given amount of segments.");

static int fast_convergence __read_mostly = 1;
static int beta __read_mostly = 717; /* = 717/1024 (BICTCP_BETA_SCALE) */
static int initial_ssthresh __read_mostly;
static int bic_scale __read_mostly = 41;
static int tcp_friendliness __read_mostly = 1;

static u32 cube_rtt_scale __read_mostly;
static u32 beta_scale __read_mostly;
static u64 cube_factor __read_mostly;

static int beta_param_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops beta_param_ops = {
	.set = beta_param_set,
	.get = param_get_int,
};
module_param_cb(beta, &beta_param_ops, &beta, 0644);
MODULE_PARM_DESC(beta, "beta factor for multiplicative decrease");

module_param(fast_convergence, int, 0644);
MODULE_PARM_DESC(fast_convergence, "turn on/off fast convergence");
module_param(initial_ssthresh, int, 0644);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
module_param(bic_scale, int, 0444);
MODULE_PARM_DESC(bic_scale,
		 "scale (scaled by 1024) value for bic function (bic_scale/1024)");
module_param(tcp_friendliness, int, 0644);
MODULE_PARM_DESC(tcp_friendliness, "turn on/off tcp friendliness");

/* Check & set the new beta parameter candidate value.
 */
static int beta_param_set(const char *val, const struct kernel_param *kp)
{
	int beta_candidate;
	int ret = kstrtoint(val, 10, &beta_candidate);

	if (ret)
		return ret;

	if (beta_candidate <= 0 || beta_candidate >= BICTCP_BETA_SCALE) {
		pr_err_once("TCP ROCCET: beta must be between 0 and %d\n",
			    BICTCP_BETA_SCALE);

		return -EINVAL;
	}

	return param_set_int(val, kp);
}

/* Used to check certain roccet parameters used in `param_precompute` in order
 * to avoid invalid scale-related calculations. This validates the specified
 * parameters or rejects them.
 */
static int param_check(void)
{
	if (beta <= 0 || beta >= BICTCP_BETA_SCALE) {
		pr_err_once("TCP ROCCET: beta must be between 0 and %d\n",
			    BICTCP_BETA_SCALE);

		return -EINVAL;
	}

	if (bic_scale <= 0) {
		pr_err_once("TCP ROCCET: bic_scale must be positive\n");

		return -EINVAL;
	}

	return 0;
}

/* Precompute some values based on the provided params.
 * These are only precomputed during module initialization and are not updated
 * during runtime. Parameter changes at runtime will only affect the next uses
 * of the parameters, but not the precomputed values.
 */
static void param_precompute(void)
{
	/* Precompute a bunch of the scaling factors that are used per-packet
	 * based on sRTT of 100ms.
	 */
	beta_scale =
		8 * (BICTCP_BETA_SCALE + beta) / 3 / (BICTCP_BETA_SCALE - beta);

	cube_rtt_scale = (bic_scale * 10); /* 1024*c/rtt */

	/* calculate the "K" for (wmax-cwnd) = c/rtt * K^3
	 *  so K = cubic_root( (wmax-cwnd)*rtt/c )
	 * the unit of K is bictcp_HZ=2^10, not HZ
	 *
	 *  c = bic_scale >> 10
	 *  rtt = 100ms
	 *
	 * the following code has been designed and tested for
	 * cwnd < 1 million packets
	 * RTT < 100 seconds
	 * HZ < 1,000,00  (corresponding to 10 nano-second)
	 */

	/* 1/c * 2^2*bictcp_HZ * srtt */
	cube_factor = 1ull << (10 + 3 * BICTCP_HZ); /* 2^40 */

	/* divide by bic_scale and by constant srtt (100ms) */
	do_div(cube_factor, bic_scale * 10);
}

static void roccet_reset(struct sock *sk, struct roccettcp *ca)
{
	/* Save previous state for parameters that should not be reset */
	bool prev_initial_limit_reached = ca->initial_limit_reached;
	bool prev_is_in_initial_launch = ca->is_in_initial_launch;

	/* Reset all the roccet parameters and later restore the ones that
	 * shouldn't be zero.
	 */
	memset(ca, 0, sizeof(struct roccettcp));

	/* Initialize all RTT values to U32_MAX, so that any lower samples
	 * will be accepted.
	 */
	ca->curr_min_rtt = U32_MAX;
	ca->curr_rtt = U32_MAX;
	ca->last_rtt = U32_MAX;

	/* Initialize the last event time to the current time, to use it for
	 * temporarily knowing when the connection started (only used for this
	 * after init).
	 */
	ca->roccet_last_event_time_us = jiffies_to_usecs(tcp_jiffies32);

	/* Initialize to current time to avoid an
	 * overflow in the ack rate calculation
	 */
	ca->last_ack_rate_time = jiffies_to_usecs(tcp_jiffies32);

	ca->interval_snd_seq_start = tcp_sk(sk)->snd_nxt;
	ca->interval_una_seq_start = tcp_sk(sk)->snd_una;

	/* Start state is LAUNCH */
	ca->state = LAUNCH;

	/* Set saved state for certain parameters */
	ca->initial_limit_reached = prev_initial_limit_reached;
	ca->is_in_initial_launch = prev_is_in_initial_launch;
}

static void roccet_init(struct sock *sk)
{
	struct roccettcp *ca = inet_csk_ca(sk);

	roccet_reset(sk, ca);

	/* Reset some parameters here, so they only get set during init */
	ca->initial_limit_reached = false;
	ca->is_in_initial_launch = true;

	if (initial_ssthresh)
		WRITE_ONCE(tcp_sk(sk)->snd_ssthresh, initial_ssthresh);

	cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}

static void roccet_set_next_srrtt_check_ts(struct roccettcp *ca, u32 now)
{
	/* Check the srrtt after 5 RTT's. Cap the assumed RTT's
	 * at 200 ms each => 1 sec max.
	 */
	ca->next_srrtt_check_ts = now + min(5 * ca->curr_rtt,
					    ROCCET_MAX_SRRTT_CHECK_INTERVAL_MS
					    * USEC_PER_MSEC);
}

static void roccet_set_next_min_rtt_probe_ts(struct roccettcp *ca)
{
	/* Probe for the min RTT in ROCCET_NEXT_MIN_RTT_PROBE_MS
	 * if no other update occurs.
	 * If we are in either RTT_PROBE or RTT_PROBE_REFILL, don't set
	 * the next probe time so we know when we approximately entered
	 * the probing phase.
	 */

	if (ca->state == RTT_PROBE || ca->state == RTT_PROBE_REFILL)
		return;

	ca->next_min_rtt_probe = jiffies_to_usecs(tcp_jiffies32) +
				 ROCCET_NEXT_MIN_RTT_PROBE_MS * USEC_PER_MSEC;
}

static void update_min_rtt(struct roccettcp *ca)
{
	/* Check if new lower min RTT was found. If so, set it directly.
	 * If no valid RTT sample has been received yet, the check will fail,
	 * since the rtt values are initialized to U32_MAX.
	 */
	if (ca->curr_rtt < ca->curr_min_rtt) {
		ca->curr_min_rtt = max(ca->curr_rtt, 1);
		roccet_set_next_min_rtt_probe_ts(ca);
	}
}

/* Return difference between last and current ack rate.
 */
static s32 get_ack_rate_diff(struct roccettcp *ca)
{
	if (ca->curr_ack_rate < ca->ack_rate_last_rate)
		return 0;
	return (s32)(ca->curr_ack_rate - ca->ack_rate_last_rate);
}

/* Update ack rate sampled by 100ms.
 */
static void update_ack_rate(struct roccettcp *ca, u32 acked, u32 now)
{
	const s32 idle_threshold = USEC_PER_SEC * 2;
	const s32 interval = USEC_PER_MSEC * 100;

	/* Check if the last interval has elapsed.
	 *
	 * Check via time_between32, so that idle connections
	 * (for more than U32_MAX/2 usecs), also trigger a new update
	 * and don't stall the ack-counting.
	 */
	bool interval_elapsed = !time_between32(now, ca->last_ack_rate_time
						  - interval,
						  ca->last_ack_rate_time
						  + interval);

	if (interval_elapsed) {
		/* Check if the connection was idle for *idle_threshold*
		 * seconds (e.g. no ACK for X seconds). If so, reset the ack
		 * counting as if a new connection was created.
		 */
		if (!time_between32(now, ca->last_ack_rate_time
					- idle_threshold,
					ca->last_ack_rate_time
					+ idle_threshold)) {
			ca->ack_rate_last_rate = 0;
			ca->curr_ack_rate = 0;
		} else {
			/* start counting for the new interval */
			ca->ack_rate_last_rate = ca->curr_ack_rate;
			ca->curr_ack_rate = ca->ack_rate_cnt;
		}
		/* Start ack-counting with the already present ACKs */
		ca->ack_rate_cnt = min_t(u32, acked, U16_MAX);
		ca->last_ack_rate_time = now;
	} else {
		/* Cap the ack count to avoid overflow.
		 * If we there are more than U16_MAX ACKs in 100ms,
		 * the resulting LAUNCH-exit won't cause any harm.
		 */
		ca->ack_rate_cnt = min_t(u32, ca->ack_rate_cnt + acked,
					 U16_MAX);
	}
}

/* Compute srRTT.
 */
static void update_srrtt(struct roccettcp *ca)
{
	u64 rrtt;

	/* Avoid integer overflow in the calculation below.
	 * This could occur in cases where we have not yet
	 * received an RTT sample after a min_rtt reset.
	 * In these cases, set the rtt to a safe value.
	 */
	if (ca->curr_rtt < ca->curr_min_rtt) {
		ca->curr_rtt = max(ca->curr_rtt, 1);
		ca->curr_min_rtt = ca->curr_rtt;
	}

	/* ca->curr_min_rtt can never be 0. For completeness, we check for this
	 * anyways in order to avoid division by zero errors.
	 */
	if (ca->curr_min_rtt == 0) {
		pr_err_once("TCP ROCCET: Recorded curr_min_rtt is 0\n");
		return; /* skip srRTT update */
	}

	/* Calculate the new rRTT (Scaled by 100).
	 * 100 * ((sRTT - sRTT_min) / sRTT_min).
	 *
	 * curr_min_rtt is always <= than curr_rtt,
	 * since this is the minimum of the rtt.
	 *
	 * 0 is a valid value for rrtt.
	 *
	 * If we have no valid RTT sample yet, curr_rtt and curr_min_rtt will
	 * be U32_MAX. This ultimately results in no srrtt increase, which
	 * is ok.
	 */
	rrtt = div_u64(100 * (u64)(ca->curr_rtt - ca->curr_min_rtt),
		       ca->curr_min_rtt);

	/* (1 - alpha) * srRTT + alpha * rRTT */
	ca->curr_srrtt = div_u64((100 - ROCCET_ALPHA_TIMES_100)
				 * (u64)ca->curr_srrtt + ROCCET_ALPHA_TIMES_100
				 * rrtt, 100);
}

/* Handle ROCCET loss/ECN during min RTT probing.
 */
static void roccet_min_rtt_probe_ce(struct roccettcp *ca, u32 cwnd)
{
	/* This should only be called in RTT_PROBE state */
	if (ca->state != RTT_PROBE)
		return;

	/* If ROCCET is in min RTT probing and a loss/ECN occurs,
	 * we use the cwnd before the probing interval to
	 * calculate the cwnd reduction and continue probing.
	 * After min RTT probing the cwnd is set to the reduced
	 * value. During min RTT probing it is very likely that
	 * congestion was caused by the cwnd value before min
	 * RTT probing.
	 */

	if (ca->cwnd_before_min_rtt_probe == 0)
		pr_warn_once("TCP ROCCET: cwnd_before_min_rtt_probe is 0 during RTT_PROBE. This should not happen.\n");
	else
		cwnd = ca->cwnd_before_min_rtt_probe;

	ca->cwnd_before_min_rtt_probe =
		max((cwnd * beta) / BICTCP_BETA_SCALE, 2U);
}

/* Do a ROCCET congestion event.
 */
static void roccet_congestion_event(struct sock *sk, u32 now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);

	u32 curr_cwnd = tcp_snd_cwnd(tp);

	ca->epoch_start = 0;
	ca->roccet_last_event_time_us = now;
	ca->last_event_time_set = true;

	if (ca->state == RTT_PROBE) {
		/* In case we are in RTT_PROBE, continue with this state, as
		 * the CE was likely caused by the cwnd before probing.
		 * However do react to the CE.
		 */
		roccet_min_rtt_probe_ce(ca, curr_cwnd);
		return;
	}

	ca->cnt = 100 * curr_cwnd;

	/* Set W_max only if the current cwnd is larger */
	if (ca->last_max_cwnd < curr_cwnd)
		ca->last_max_cwnd = curr_cwnd;

	/* Reduce cwnd by beta */
	tcp_snd_cwnd_set(tp, min(tp->snd_cwnd_clamp,
				 max((curr_cwnd * beta)
				     / BICTCP_BETA_SCALE, 2U)));

	/* Re-adjust ssthresh in case of ORBITER Congestion Event */
	WRITE_ONCE(tp->snd_ssthresh, tcp_snd_cwnd(tp));

	if (ca->state == LAUNCH) {
		ca->state = ORBITER;
	} else if (ca->state == ORBITER || ca->state == RTT_PROBE_REFILL) {
		/* If we are in orbiter or currently refilling the pipe,
		 * abort the refill.
		 */
		ca->state = DRAIN;
	}
	/* Other states (e.g. DRAIN or RTT_PROBE_ENTER) not handled as
	 * we don`t want any action there.
	 */
}

static void roccet_enter_min_rtt_probe(struct sock *sk, u32 now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);
	u32 interval, probe_cwnd;

	/* If probing is already set, there was a mix up.
	 * Continue anyway so we can recover.
	 */
	if (ca->rtt_probe_timers_set)
		pr_warn_once("TCP ROCCET: Probing time should not be set when entering RTT Probe\n");

	/* Safeguard for an "infinite" probe. This should logically never
	 * happen but still safeguard it.
	 */
	if (ca->curr_rtt == U32_MAX) {
		pr_warn_once("TCP ROCCET: curr_rtt is U32_MAX. Won't enter RTT_PROBE\n");
		ca->state = ORBITER;
		return;
	}

	/* Use field to store when we entered min RTT probing. Here we use
	 * jiffies to stay consistent with the epoch_start format.
	 */
	ca->next_min_rtt_probe = tcp_jiffies32;

	/* Start min RTT probing. Probe for 1 RTT or at least 200ms.
	 * Also bound the probe to a maximum of ROCCET_MAX_PROBE_INTERVAL_MS
	 * to avoid any "invalid"/very large RTT samples from breaking
	 * the probing and allowing ROCCET to recover afterwards.
	 */
	interval = max(200 * USEC_PER_MSEC, min(ca->curr_rtt,
						ROCCET_MAX_PROBE_INTERVAL_MS
						* USEC_PER_MSEC));

	/* This is to handle deep shared buffers with loss-based
	 * congestion control like CUBIC. If the cwnd is not limited
	 * by the application but falsely detected (see ROCCET paper),
	 * we have to empty the pipe more.
	 * If the limit detection is correct this will cause no harm
	 * to the tcp flow because the cwnd is not fully utilized and
	 * we set the cwnd to its previous value after probing.
	 */
	if (!tcp_is_cwnd_limited(sk))
		probe_cwnd = max(tcp_snd_cwnd(tp) / 3, TCP_INIT_CWND);
	else
		probe_cwnd = max(tcp_snd_cwnd(tp) / 2, TCP_INIT_CWND);

	ca->probe_min_rtt_until = now + interval;
	ca->cwnd_before_min_rtt_probe = tcp_snd_cwnd(tp);

	/* Reduce the cwnd to drain the buffer for probing. */
	tcp_snd_cwnd_set(tp, min(probe_cwnd, tp->snd_cwnd_clamp));

	/* Reset current min RTT to allow probing for
	 * a new lower and higher minimum RTT.
	 */
	ca->curr_min_rtt = U32_MAX;

	/* Refill the pipe after probing.
	 * For this we use the previous cwnd for another probing interval.
	 */
	ca->refill_until = ca->probe_min_rtt_until + interval;
	ca->rtt_probe_timers_set = true; /* Now both timers are set. */

	/* Now we know that ca->probe_min_rtt_until < ca->refill_until
	 * and we advance to the next state of the probing phase.
	 *
	 * Wrap-arounds of these values are handled by the relevant
	 * if-conditions.
	 */
	ca->state = RTT_PROBE;
}

/* Do minimum RTT probing.
 */
static void roccet_min_rtt_probe(struct sock *sk, u32 now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);

	/* Here we are in RTT_PROBE, so the probing time should be set.
	 * If not just continue and enter refill phase.
	 *
	 * probe_min_rtt_until wrap-arounds are handled by the time_between32
	 * check.
	 */
	if (!ca->rtt_probe_timers_set)
		pr_warn_once("TCP ROCCET: Probing time should be set\n");
	else if (time_between32(now, ca->probe_min_rtt_until
				    - ROCCET_MAX_PROBE_INTERVAL_MS
				    * USEC_PER_MSEC, ca->probe_min_rtt_until))
		/* No state change if we are in the probing interval. */
		return;
	else if (time_after32(now, ca->refill_until))
		/* If the probe_min_rtt_until interval has passed we likely
		 * just entered the refill interval, so go into
		 * RTT_PROBE_REFILL. If we are not in the refill_until
		 * interval, something went wrong and we should quickly exit
		 * the probing phase.
		 *
		 * Wraps-arounds of refill_until are caught by the time_after32
		 * check.
		 */
		pr_warn_once("TCP ROCCET: Skipped refill interval.\n");

	/* Reset cwnd to refill the pipe and consequently clear the stored
	 * window.
	 * Also perform sanity check. This should never happen.
	 */
	if (ca->cwnd_before_min_rtt_probe == 0)
		pr_warn_once("TCP ROCCET: cwnd_before_min_rtt_probe is 0 during RTT_PROBE.\n");
	else
		tcp_snd_cwnd_set(tp, min(ca->cwnd_before_min_rtt_probe,
					 tp->snd_cwnd_clamp));

	ca->cwnd_before_min_rtt_probe = 0;

	/* Exit the RTT_PROBE state */
	ca->state = RTT_PROBE_REFILL;
}

static void roccet_rtt_probe_refill(struct roccettcp *ca, u32 now)
{
	/* Once the refill interval is over, we can end the probing phase. */
	if (!time_between32(now, ca->refill_until
				 - ROCCET_MAX_PROBE_INTERVAL_MS
				 * USEC_PER_MSEC, ca->refill_until)) {
		/* End min RTT probing phase. */
		ca->state = ORBITER;
	}
}

/* calculate the cubic root of x using a table lookup followed by one
 * Newton-Raphson iteration.
 * Avg err ~= 0.195%
 */
static u32 cubic_root(u64 a)
{
	u32 x, b, shift;
	/* cbrt(x) MSB values for x MSB values in [0..63].
	 * Precomputed then refined by hand - Willy Tarreau
	 *
	 * For x in [0..63],
	 *   v = cbrt(x << 18) - 1
	 *   cbrt(x) = (v[x] + 10) >> 6
	 */
	static const u8 v[] = {
		/* 0x00 */ 0,	54,  54,  54,  118, 118, 118, 118,
		/* 0x08 */ 123, 129, 134, 138, 143, 147, 151, 156,
		/* 0x10 */ 157, 161, 164, 168, 170, 173, 176, 179,
		/* 0x18 */ 181, 185, 187, 190, 192, 194, 197, 199,
		/* 0x20 */ 200, 202, 204, 206, 209, 211, 213, 215,
		/* 0x28 */ 217, 219, 221, 222, 224, 225, 227, 229,
		/* 0x30 */ 231, 232, 234, 236, 237, 239, 240, 242,
		/* 0x38 */ 244, 245, 246, 248, 250, 251, 252, 254,
	};

	b = fls64(a);
	if (b < 7) {
		/* a in [0..63] */
		return ((u32)v[(u32)a] + 35) >> 6;
	}

	b = ((b * 84) >> 8) - 1;
	shift = (a >> (b * 3));

	x = ((u32)(((u32)v[shift] + 10) << b)) >> 6;

	/* Newton-Raphson iteration
	 *                         2
	 * x    = ( 2 * x  +  a / x  ) / 3
	 *  k+1          k         k
	 */
	x = (2 * x + (u32)div64_u64(a, (u64)x * (u64)(x - 1)));
	x = ((x * 341) >> 10);
	return x;
}

/* Compute congestion window to use.
 */
static void bictcp_update(struct roccettcp *ca, u32 cwnd,
			  u32 acked)
{
	u32 delta, bic_target, max_cnt;
	u64 offs, t;

	ca->ack_cnt += acked; /* count the number of ACKed packets */

	if (ca->last_cwnd == cwnd &&
	    (s32)(tcp_jiffies32 - ca->last_time) <= HZ / 32)
		return;

	/* The CUBIC function can update ca->cnt at most once per jiffy.
	 * On all cwnd reduction events, ca->epoch_start is set to 0,
	 * which will force a recalculation of ca->cnt.
	 */
	if (ca->epoch_start && tcp_jiffies32 == ca->last_time)
		goto tcp_friendliness;

	ca->last_cwnd = cwnd;
	ca->last_time = tcp_jiffies32;

	if (ca->epoch_start == 0) {
		ca->epoch_start = tcp_jiffies32; /* record beginning */
		ca->ack_cnt = acked; /* start counting */
		ca->tcp_cwnd = cwnd; /* syn with cubic */

		if (ca->last_max_cwnd <= cwnd) {
			ca->bic_K = 0;
			ca->bic_origin_point = cwnd;
		} else {
			/* Compute new K based on
			 * (wmax-cwnd) * (srtt>>3 / HZ) / c * 2^(3*bictcp_HZ)
			 */
			ca->bic_K = cubic_root(cube_factor *
					       (ca->last_max_cwnd - cwnd));
			ca->bic_origin_point = ca->last_max_cwnd;
		}
	}

	/* cubic function - calc */
	/* calculate c * time^3 / rtt,
	 *  while considering overflow in calculation of time^3
	 * (so time^3 is done by using 64 bit)
	 * and without the support of division of 64bit numbers
	 * (so all divisions are done by using 32 bit)
	 *  also NOTE the unit of those variables
	 *	  time  = (t - K) / 2^bictcp_HZ
	 *	  c = bic_scale >> 10
	 * rtt  = (srtt >> 3) / HZ
	 * !!! The following code does not have overflow problems,
	 * if the cwnd < 1 million packets !!!
	 */

	t = (s32)(tcp_jiffies32 - ca->epoch_start);
	t += usecs_to_jiffies(ca->delay_min);

	/* change the unit from HZ to bictcp_HZ */
	t <<= BICTCP_HZ;
	do_div(t, HZ);

	if (t < ca->bic_K) /* t - K */
		offs = ca->bic_K - t;
	else
		offs = t - ca->bic_K;

	/* c/rtt * (t-K)^3 */
	delta = (cube_rtt_scale * offs * offs * offs) >> (10 + 3 * BICTCP_HZ);
	if (t < ca->bic_K) /* below origin*/
		bic_target = ca->bic_origin_point - delta;
	else /* above origin*/
		bic_target = ca->bic_origin_point + delta;

	/* cubic function - calc bictcp_cnt*/
	if (bic_target > cwnd)
		ca->cnt = cwnd / (bic_target - cwnd);
	else
		ca->cnt = 100 * cwnd; /* very small increment*/

	/* The initial growth of cubic function may be too conservative
	 * when the available bandwidth is still unknown.
	 */
	if (ca->last_max_cwnd == 0 && ca->cnt > 20)
		ca->cnt = 20; /* increase cwnd 5% per RTT */

tcp_friendliness:
	/* TCP Friendly */
	if (tcp_friendliness) {
		u32 scale = beta_scale;

		delta = (cwnd * scale) >> 3;
		if (delta > 0) {
			while (ca->ack_cnt > delta) { /* update tcp cwnd */
				ca->ack_cnt -= delta;
				ca->tcp_cwnd++;
			}
		}

		if (ca->tcp_cwnd > cwnd) { /* if bic is slower than tcp */
			delta = ca->tcp_cwnd - cwnd;
			max_cnt = cwnd / delta;
			if (ca->cnt > max_cnt)
				ca->cnt = max_cnt;
		}
	}

	/* The maximum rate of cwnd increase CUBIC allows is 1 packet per
	 * 2 packets ACKed, meaning cwnd grows at 1.5x per RTT.
	 */
	ca->cnt = max(ca->cnt, 2U);
}

static void roccet_launch_update(struct sock *sk, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);

	u32 now = jiffies_to_usecs(tcp_jiffies32);

	/* LAUNCH: Detect an exit point for tcp slow start
	 * in networks with large buffers of multiple BDP
	 * Like in cellular networks (5G, ...).
	 *
	 * Or exit LAUNCH if cwnd is too large for application layer
	 * data rate (tcp cwnd validation).
	 */
	if ((ca->curr_srrtt > sr_rtt_upper_bound &&
	     get_ack_rate_diff(ca) <= ack_rate_diff_ss) ||
	     !tcp_in_slow_start(tp) ||
	    (!tcp_is_cwnd_limited(sk) && ca->initial_limit_reached)) {
		u32 new_cwnd;

		ca->epoch_start = 0;

		/* Handle initial LAUNCH. Most bufferbloat occurs here */
		if (ca->is_in_initial_launch) {
			/* Halving the cwnd will undo the previous step of slow
			 * start. Which is fine since the pipe is already full.
			 */
			new_cwnd = tcp_snd_cwnd(tp) / 2;
		} else {
			new_cwnd = tcp_snd_cwnd(tp) - (tcp_snd_cwnd(tp) / 3);
		}
		tcp_snd_cwnd_set(tp, min(max(new_cwnd, TCP_INIT_CWND),
					 tp->snd_cwnd_clamp));
		WRITE_ONCE(tp->snd_ssthresh, tcp_snd_cwnd(tp));
		ca->roccet_last_event_time_us = now;
		ca->last_event_time_set = true;
		ca->state = ORBITER;
		return;
	}

	/* If not already exiting LAUNCH, grow cwnd similar to slow-start */
	acked = tcp_slow_start(tp, acked);
	/* If cwnd hits ssthresh, go to ORBITER and if any ACKs are
	 * leftover save them for ORBITER.
	 * Check via tcp_in_slow_start() in case no ACKs are left.
	 */
	if (!tcp_in_slow_start(tp)) {
		ca->state = ORBITER;
		ca->epoch_start = 0;
		ca->ack_carry_over = acked;
	}
}

static void roccet_orbiter_update(struct sock *sk, u32 acked)
{
	/* ORBITER: Increase the cwnd by using the CUBIC cwnd growth function,
	 * if no roccet congestion event is detected.
	 */

	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);

	u32 now = jiffies_to_usecs(tcp_jiffies32);
	bool evaluate_srrtt = false;
	bool sent_more_than_acked = false;
	u32 roccet_xj, jitter, sent, received;

	/* Enter DRAIN when roccet was recently triggered */
	if (ca->last_event_time_set &&
	    time_between32(now, ca->roccet_last_event_time_us
				- 100 * USEC_PER_MSEC,
				ca->roccet_last_event_time_us
				+ 100 * USEC_PER_MSEC)) {
		ca->state = DRAIN;
		return;
	}

	/* Enter RTT_PROBE when the "timer" has expired.
	 *
	 * Since we are in ORBITER, we should have already received at least
	 * one RTT sample. However safeguard against it if not.
	 *
	 * Use double the probing period as a guard space to avoid
	 * idle-time locking us out of probing.
	 */
	if (!time_between32(now, ca->next_min_rtt_probe
				 - 2 * ROCCET_NEXT_MIN_RTT_PROBE_MS
				 * USEC_PER_MSEC, ca->next_min_rtt_probe) &&
	    ca->curr_rtt != U32_MAX) {
		ca->state = RTT_PROBE_ENTER;
		return;
	}

	/* Calculate jitter.
	 * Since we are in ORBITER, we should have already received at least
	 * one RTT sample. Even if not, ca->curr_rtt and ca->curr_min_rtt
	 * (the divisor later on) are U32_MAX, so they cancel each other out.
	 * And if ca->last_rtt is U32_MAX, roccet_xj will be very large, so
	 * the srRTT will not exceed it.
	 */
	if ((s32)(ca->curr_rtt - ca->last_rtt) < 0)
		jitter = ca->last_rtt - ca->curr_rtt;
	else
		jitter = ca->curr_rtt - ca->last_rtt;

	/* Calculate if more bytes were sent than received
	 * in the time interval.
	 *
	 * Handle wrap arounds by relying on unsigned subtraction.
	 * e.g. if snd_nxt wraps to 10 and seq_start is U32_MAX - 10,
	 * the subtraction will result in the value of 21.
	 */
	sent = tp->snd_nxt - ca->interval_snd_seq_start;
	received = tp->snd_una - ca->interval_una_seq_start;

	/* Check sent and received bytes from the previous interval.
	 * Here we use a guard space of 1% of the current cwnd.
	 * We do this to avoid a false positive evaluation due
	 * to delays caused by jitter or scheduling.
	 *
	 * (s64)(s32)(sent - received) => Preserves negative wrap-behavior
	 * with explicit s64 conversion afterwards.
	 */
	sent_more_than_acked = (s64)(s32)(sent - received) >
			       (s64)div_u64((u64)tcp_snd_cwnd(tp) *
					    (u64)tp->mss_cache, 100);

	/* Check if it's time to evaluate the srRTT */
	if (!time_between32(now, ca->next_srrtt_check_ts
				 - 2 * ROCCET_MAX_SRRTT_CHECK_INTERVAL_MS
				 * USEC_PER_MSEC, ca->next_srrtt_check_ts)) {
		evaluate_srrtt = true;

		/* reset struct and set next end of period */
		roccet_set_next_srrtt_check_ts(ca, now);

		/* Reset Rate calculation */
		ca->interval_snd_seq_start = tp->snd_nxt;
		ca->interval_una_seq_start = tp->snd_una;
	}

	/* If we are in recovery, skip growth and further ROCCET-CE's. */
	if (tcp_in_cwnd_reduction(sk))
		return;

	/* Respects the jitter of the connection and add it on top of
	 * the upper bound for the srRTT.
	 */
	roccet_xj = div_u64((u64)jitter * 100, ca->curr_min_rtt) +
			sr_rtt_upper_bound;

	/* The srRTT exceeds the upper bound if bufferbloat happens.
	 * Here, we want to reduce the cwnd and drain the buffer.
	 */
	if (ca->curr_srrtt > roccet_xj && evaluate_srrtt &&
	    sent_more_than_acked) {
		roccet_congestion_event(sk, now);
		return;
	}

	/* Terminates this function if cwnd is not fully utilized.
	 * In mobile networks like 5G, this termination causes the
	 * cwnd to be frozen at an excessively high value. This is
	 * because slow start or HyStart massively exceed the available
	 * bandwidth and leave the cwnd at an excessively high value.
	 * The cwnd cannot therefore be fully utilized because it is
	 * limited by the connection capacity.
	 */
	if (!tcp_is_cwnd_limited(sk) || sent_more_than_acked)
		return;

	/* In case there are any ACKs left over from LAUNCH,
	 * apply them now.
	 */
	if (ca->ack_carry_over) {
		acked += ca->ack_carry_over;
		ca->ack_carry_over = 0;
	}

	bictcp_update(ca, tcp_snd_cwnd(tp), acked);
	tcp_cong_avoid_ai(tp, max(1, ca->cnt), acked);
}

/* The Cubic ssthresh calculation is also used for ROCCET.
 * Called before TCP-CC state changes to TCP_CA_Recovery, TCP_CA_CWR or
 * TCP_CA_Loss. cwnd reduction is then handled in the roccet_state() callback.
 */
static u32 roccet_recalc_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);
	u32 cwnd = tcp_snd_cwnd(tp);

	/* In LAUNCH, we want no reduction on loss/ECN.
	 * On ECN this is set later on in roccet_state()
	 */
	if (ca->state == LAUNCH)
		return cwnd;

	/* In min RTT probe, use the cwnd before the probe to not undershoot */
	if (ca->state == RTT_PROBE)
		cwnd = ca->cwnd_before_min_rtt_probe;

	ca->epoch_start = 0;	/* end of epoch */

	/* Wmax and fast convergence */
	if (cwnd < ca->last_max_cwnd && fast_convergence)
		ca->last_max_cwnd = (cwnd * (BICTCP_BETA_SCALE + beta))
			/ (2 * BICTCP_BETA_SCALE);
	else
		ca->last_max_cwnd = cwnd;

	return max((cwnd * beta) / BICTCP_BETA_SCALE, 2U);
}

/* Handle a recovery event and return the new cwnd.
 */
static u32 roccet_handle_recovery(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);
	u32 cwnd = tcp_snd_cwnd(tp);

	/* If a loss occurs in the refill phase of min RTT probing
	 * we reduce the cwnd and abort the refill.
	 */
	if (ca->state == RTT_PROBE_REFILL)
		ca->state = ORBITER;

	/* In RTT_PROBE we don`t want to change state or immediately reduce
	 * the cwnd.
	 */
	if (ca->state == RTT_PROBE) {
		roccet_min_rtt_probe_ce(ca, cwnd);
		return tcp_snd_cwnd(tp);
	}

	/* On loss in LAUNCH, enter ORBITER without a cwnd reduction. */
	if (ca->state == LAUNCH) {
		ca->state = ORBITER;
		return cwnd;
	}

	return max((cwnd * beta) / BICTCP_BETA_SCALE, 2U);
}

/* Checks for state roccet-transitions and performs necessary state (re)sets.
 * This is done in order to avoid the possibility of forgetting to correctly
 * set a state when entering certain states.
 *
 * This is used whenever a state change is possible
 * (e.g. in roccet_state(), roccet_control(), ...).
 */
static void roccet_handle_state_transitions(struct sock *sk,
					    enum roccet_state prev_state,
					    u32 now)
{
	struct roccettcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 now_jiffies = tcp_jiffies32;

	if (ca->state == prev_state)
		return;

	/* When we exit LAUNCH we can be sure that we are no longer in the
	 * initial_launch. Also on any state transition from LAUNCH, we can
	 * say that the tcp_is_cwnd_limited() is now valid.
	 */
	if (prev_state == LAUNCH) {
		ca->is_in_initial_launch = false;
		ca->initial_limit_reached = true;
	}

	/* Whenever we enter ORBITER, we need to schedule the next SRTT check.
	 * This will be set before evaluating the next_srrtt_check_ts condition
	 * as this is only done in ORBITER.
	 */
	if (ca->state == ORBITER) {
		roccet_set_next_srrtt_check_ts(ca, now);
		ca->interval_snd_seq_start = tp->snd_nxt;
		ca->interval_una_seq_start = tp->snd_una;

		/* Add the time we spent in the min RTT probing phase to the
		 * epoch_start, so the cubic growth won't suddenly jump
		 * after probing. Here we use the next_min_rtt_probe field
		 * which we (ab)used to store the time when we entered min RTT
		 * probing (in jiffies).
		 */
		if ((prev_state == RTT_PROBE ||
		     prev_state == RTT_PROBE_REFILL)) {
			if (ca->epoch_start != 0) {
				ca->epoch_start += now_jiffies -
						   ca->next_min_rtt_probe;
				/* Clamp so that epoch_start doesn't go
				 * in the future.
				 */
				if (after(ca->epoch_start, now_jiffies))
					ca->epoch_start = now_jiffies;
			}

			roccet_set_next_min_rtt_probe_ts(ca);
		}
	}

	/* In refill we can transition to DRAIN. In this case, overwrite the
	 * jiffies ts
	 */
	if (ca->state == DRAIN && prev_state == RTT_PROBE_REFILL)
		roccet_set_next_min_rtt_probe_ts(ca);

	/* Whenever we leave the min RTT probing states (and not just
	 * transition between them), we want to reset the probing timers.
	 */
	if ((prev_state == RTT_PROBE_ENTER || prev_state == RTT_PROBE ||
	     prev_state == RTT_PROBE_REFILL) &&
	    (ca->state != RTT_PROBE && ca->state != RTT_PROBE_REFILL)) {
		ca->probe_min_rtt_until = 0;
		ca->refill_until = 0;
		ca->rtt_probe_timers_set = false;
	}
}

static void roccet_cwnd_event_tx_start(struct sock *sk)
{
	struct roccettcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 now = tcp_jiffies32;
	enum roccet_state prev_state = ca->state;
	s32 delta;

	delta = now - tcp_sk(sk)->lsndtime;

	/* We were application limited (idle) for a while.
	 * Shift epoch_start to keep cwnd growth to cubic curve.
	 */
	if (ca->epoch_start && delta > 0) {
		ca->epoch_start += delta;
		if (after(ca->epoch_start, now))
			ca->epoch_start = now;
	}

	/* After idle periods, go into LAUNCH with slow start */
	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_slow_start_after_idle) &&
	    delta >= inet_csk(sk)->icsk_rto) {
		tp->snd_ssthresh = tcp_snd_cwnd(tp);
		tcp_snd_cwnd_set(tp, min(TCP_INIT_CWND, tp->snd_cwnd_clamp));
		ca->state = LAUNCH;
	}

	roccet_handle_state_transitions(sk, prev_state, jiffies_to_usecs(now));
}

/* Handle different loss-states and perform adequate cwnd reductions.
 */
static void roccet_state(struct sock *sk, u8 new_state)
{
	struct roccettcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 cwnd;
	u32 now = jiffies_to_usecs(tcp_jiffies32);
	enum roccet_state prev_state = ca->state;

	if (new_state == TCP_CA_Loss) {
		roccet_reset(sk, ca);
		tcp_snd_cwnd_set(tp, min(TCP_INIT_CWND, tp->snd_cwnd_clamp));
		WRITE_ONCE(tp->snd_ssthresh, max(tp->snd_ssthresh,
						 tcp_snd_cwnd(tp)));
	} else if (tcp_in_cwnd_reduction(sk)) {
		/* In this case we don't want a further window reduction */
	} else if (new_state == TCP_CA_CWR) {
		/* Handle CWR as ROCCET congestion event,
		 * however afterwards always set Wmax to the current cwnd
		 * except during min RTT probing.
		 */
		cwnd = tcp_snd_cwnd(tp);
		roccet_congestion_event(sk, now);
		if (ca->state != RTT_PROBE_ENTER && ca->state != RTT_PROBE &&
		    ca->state != RTT_PROBE_REFILL)
			ca->last_max_cwnd = cwnd;
	} else if (new_state == TCP_CA_Recovery) {
		/* Directly reduce cwnd and rely on pacing */
		cwnd = roccet_handle_recovery(sk);
		tcp_snd_cwnd_set(tp, min(cwnd, tp->snd_cwnd_clamp));
	}

	roccet_handle_state_transitions(sk, prev_state, now);
}

/* Update RTT samples and min RTT.
 */
static void roccet_acked(struct sock *sk, const struct ack_sample *sample)
{
	struct roccettcp *ca = inet_csk_ca(sk);
	bool after_fast_recovery;
	u32 delay;

	/* Some calls are for duplicates without timestamps */
	if (sample->rtt_us < 0)
		return;

	/* Check if we are right after fast recovery. */
	after_fast_recovery = ca->epoch_start &&
			      (s32)(tcp_jiffies32 - ca->epoch_start) < HZ;

	/* If we are not after fast recovery or we are in RTT_PROBE however,
	 * we want to record every new min-sample.
	 */
	if (!after_fast_recovery || ca->state == RTT_PROBE) {
		/* Get valid sample for roccet */
		if (sample->rtt_us > 0) {
			ca->last_rtt = ca->curr_rtt;
			ca->curr_rtt = sample->rtt_us;
		}
	}

	/* Otherwise discard delay samples right after fast recovery. */
	if (after_fast_recovery)
		return;

	delay = sample->rtt_us;

	if (delay == 0)
		delay = 1;

	/* first call or link delay decreases */
	if (ca->delay_min == 0 || (s32)(delay - ca->delay_min) < 0)
		ca->delay_min = delay;
}

/* Custom Pacing Rate for ROCCET TCP.
 * The code here is similar to the pacing rate adjustments in tcp_input.c's
 * tcp_update_pacing_rate(). In (modified) slow start we want a pacing
 * following the sysctl_tcp_pacing_ss_ratio (usually 200%) parameter and
 * otherwise (congestion avoidance) we adjust the pacing to 100% and do not
 * use the sysctl_tcp_pacing_ca_ratio.
 */
static void roccet_update_pacing_rate(struct sock *sk)
{
	struct roccettcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u64 rate;

	/* set sk_pacing_rate to 200 % of current rate (mss * cwnd / srtt) */
	rate = (u64)tp->mss_cache * ((div64_u64(USEC_PER_SEC, 100) << 3));

	/* current rate is (cwnd * mss) / srtt
	 * In slow-start [1], set sk_pacing_rate to sysctl_tcp_pacing_ss_ratio
	 * (usually 200%) the current rate. Otherwise, set it to 100% the
	 * current rate.
	 *
	 * [1]: Normal Slow Start cond is (tp->snd_cwnd < tp->snd_ssthresh)
	 *	 If snd_cwnd >= (tp->snd_ssthresh / 2), we are approaching
	 *	 end of slow start and should slow down.
	 */
	if (ca->state == RTT_PROBE || tcp_snd_cwnd(tp) >= tp->snd_ssthresh / 2)
		/* Pacing rate of 100%
		 * (instead of ipv4.sysctl_tcp_pacing_ca_ratio)
		 */
		rate *= 100;
	else
		/* Here we are well inside of slow start
		 * (tcp_snd_cwnd(tp) < tp->snd_ssthresh / 2)
		 */
		rate *= READ_ONCE
			(sock_net(sk)->ipv4.sysctl_tcp_pacing_ss_ratio);

	rate *= max(tcp_snd_cwnd(tp), tp->packets_out);

	if (likely(tp->srtt_us))
		do_div(rate, tp->srtt_us);

	/* WRITE_ONCE() is needed because sch_fq fetches sk_pacing_rate
	 * without any lock. We want to make sure compiler won't store
	 * intermediate values in this location.
	 */
	WRITE_ONCE(sk->sk_pacing_rate,
		   min_t(u64, rate, READ_ONCE(sk->sk_max_pacing_rate)));
}

static void roccet_drain_update(struct sock *sk, u32 now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct roccettcp *ca = inet_csk_ca(sk);

	/* If we are in DRAIN, ca->roccet_last_event_time_us has to be set.
	 * If more than 100 ms have passed after a roccet event, exit DRAIN.
	 * The between check is here to handle idle-times and timer-wraps.
	 * It is ok if we "accidentally" exit DRAIN in such an occasion.
	 */
	if (!time_between32(now, ca->roccet_last_event_time_us
				- 100 * USEC_PER_MSEC,
				ca->roccet_last_event_time_us
				+ 100 * USEC_PER_MSEC)) {
		if (tcp_in_slow_start(tp))
			ca->state = LAUNCH;
		else
			ca->state = ORBITER;
	}
}

static void roccet_control(struct sock *sk, u32 ack, int flag,
			   const struct rate_sample *rs)
{
	struct roccettcp *ca = inet_csk_ca(sk);

	u32 now = jiffies_to_usecs(tcp_jiffies32);
	enum roccet_state prev_state = ca->state;

	/* Update ack rate. Even on no new acks/sacks. */
	update_ack_rate(ca, rs->acked_sacked, now);
	/* Only update RTT metrics if we have new acks/sacks in order to keep
	 * EWMA from running multiple times for no new acks.
	 */
	if (rs->acked_sacked > 0) {
		update_min_rtt(ca);
		update_srrtt(ca);
	}

	/* Evaluate roccet state */
	switch (ca->state) {
	case LAUNCH:
		roccet_launch_update(sk, rs->acked_sacked);
		break;
	case ORBITER:
		roccet_orbiter_update(sk, rs->acked_sacked);
		break;
	case DRAIN:
		/* In DRAIN the cwnd should not be increased */
		roccet_drain_update(sk, now);
		break;
	case RTT_PROBE_ENTER:
		roccet_enter_min_rtt_probe(sk, now);
		break;
	case RTT_PROBE:
		roccet_min_rtt_probe(sk, now);
		break;
	case RTT_PROBE_REFILL:
		roccet_rtt_probe_refill(ca, now);
		break;
	default:
		pr_err_once("TCP ROCCET: Invalid state %d\n", ca->state);
	}

	roccet_handle_state_transitions(sk, prev_state, now);

	roccet_update_pacing_rate(sk);

	/* Abuse ca->roccet_last_event_time_us as the start time for this
	 * connection (only after init, while initial_limit_reached is not
	 * set). When the connection starts sending enough data or after 500 ms
	 * we know that we can use tcp_is_cwnd_limited() to check if the
	 * connection is actually limited by the cwnd and is not just
	 * starting up.
	 */
	if (!ca->initial_limit_reached &&
	    (tcp_is_cwnd_limited(sk) ||
	     !time_between32(now, ca->roccet_last_event_time_us
				  - 500 * USEC_PER_MSEC,
				  ca->roccet_last_event_time_us
				  + 500 * USEC_PER_MSEC)))
		ca->initial_limit_reached = true;
}

static struct tcp_congestion_ops roccet_tcp __read_mostly = {
	.init = roccet_init,
	.ssthresh = roccet_recalc_ssthresh,
	.set_state = roccet_state,
	.undo_cwnd = tcp_reno_undo_cwnd,
	.cwnd_event_tx_start = roccet_cwnd_event_tx_start,
	.pkts_acked = roccet_acked,
	.cong_control = roccet_control,
	.owner = THIS_MODULE,
	.name = "roccet",
};

static int __init roccet_register(void)
{
	int param_err;

	BUILD_BUG_ON(sizeof(struct roccettcp) > ICSK_CA_PRIV_SIZE);

	/* Check for valid parameter ranges and then precompute factors */
	param_err = param_check();

	if (param_err)
		return param_err;

	param_precompute();

	return tcp_register_congestion_control(&roccet_tcp);
}

static void __exit roccet_unregister(void)
{
	tcp_unregister_congestion_control(&roccet_tcp);
}

module_init(roccet_register);
module_exit(roccet_unregister);

MODULE_AUTHOR("Lukas Prause, Tim Füchsel");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ROCCET TCP");
