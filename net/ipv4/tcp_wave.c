/*
 * TCP Wave
 *
 * Copyright 2017 Natale Patriciello <natale.patriciello@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define pr_fmt(fmt) "WAVE: " fmt

#include <net/tcp.h>
#include <linux/inet_diag.h>
#include <linux/module.h>

#define NOW ktime_to_us(ktime_get())
#define SPORT(sk) ntohs(inet_sk(sk)->inet_sport)
#define DPORT(sk) ntohs(inet_sk(sk)->inet_dport)

static uint init_burst __read_mostly = 10;
static uint min_burst __read_mostly = 3;
static uint init_timer_ms __read_mostly = 200;
static uint beta_ms __read_mostly = 150;

module_param(init_burst, uint, 0644);
MODULE_PARM_DESC(init_burst, "initial burst (segments)");
module_param(min_burst, uint, 0644);
MODULE_PARM_DESC(min_burst, "minimum burst (segments)");
module_param(init_timer_ms, uint, 0644);
MODULE_PARM_DESC(init_timer_ms, "initial timer (ms)");
module_param(beta_ms, uint, 0644);
MODULE_PARM_DESC(beta_ms, "beta parameter (ms)");

/* Shift factor for the exponentially weighted average. */
#define AVG_SCALE 20
#define AVG_UNIT BIT(AVG_SCALE)

/* Tell if the driver is initialized (init has been called) */
#define FLAG_INIT       0x1
/* Tell if, as sender, the driver is started (after TX_START) */
#define FLAG_START      0x2
/* If it's true, we save the sent size as a burst */
#define FLAG_SAVE       0x4

/* List for saving the size of sent burst over time */
struct wavetcp_burst_hist {
	u16 size;               /* The burst size */
	struct list_head list;  /* Kernel list declaration */
};

static bool test_flag(u8 flags, u8 value)
{
	return (flags & value) == value;
}

static void set_flag(u8 *flags, u8 value)
{
	*flags |= value;
}

static void clear_flag(u8 *flags, u8 value)
{
	*flags &= ~(value);
}

static bool ktime_is_null(ktime_t kt)
{
	return ktime_compare(kt, ns_to_ktime(0)) == 0;
}

/* TCP Wave private struct */
struct wavetcp {
	u8 flags; /* The module flags */
	u32 tx_timer; /* The current transmission timer (us) */
	u8 burst; /* The current burst size (segments) */
	s8 delta_segments; /* Difference between sent and burst size */
	u16 pkts_acked; /* The segments acked in the round */
	u8 backup_pkts_acked;
	u8 aligned_acks_rcv; /* The number of ACKs received in a round */
	u8 heuristic_scale; /* Heuristic scale, to divide the RTT */
	ktime_t previous_ack_t_disp; /* Previous ack_train_disp Value */
	ktime_t first_ack_time; /* First ACK time of the round */
	ktime_t last_ack_time; /* Last ACK time of the round */
	u32 backup_first_ack_time_us; /* Backup value of the first ack time */
	u32 previous_rtt; /* RTT of the previous acked segment */
	u32 first_rtt; /* First RTT of the round */
	u32 min_rtt; /* Minimum RTT of the round */
	u32 avg_rtt; /* Average RTT of the previous round */
	u32 max_rtt; /* Maximum RTT */
	u8 stab_factor; /* Stability factor */
	struct kmem_cache *cache; /* The memory for saving the burst sizes */
	struct wavetcp_burst_hist *history; /* The burst history */
};

/* Called to setup Wave for the current socket after it enters the CONNECTED
 * state (i.e., called after the SYN-ACK is received). The slow start should be
 * 0 (see wavetcp_get_ssthresh) and we set the initial cwnd to the initial
 * burst.
 *
 * After the ACK of the SYN-ACK is sent, the TCP will add a bit of delay to
 * permit the queueing of data from the application, otherwise we will end up
 * in a scattered situation (we have one segment -> send it -> no other segment,
 * don't set the timer -> slightly after, another segment come and we loop).
 *
 * At the first expiration, the cwnd will be large enough to push init_burst
 * segments out.
 */
static void wavetcp_init(struct sock *sk)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	sk->sk_pacing_status = SK_PACING_NEEDED;
	sk->sk_pacing_rate = sk->sk_max_pacing_rate;
	set_bit(TSQ_DISABLED, &sk->sk_tsq_flags);

	pr_debug("%llu sport: %hu [%s] max_pacing_rate %u, status %u (1==NEEDED)\n",
		 NOW, SPORT(sk), __func__, sk->sk_pacing_rate,
		 sk->sk_pacing_status);

	/* Setting the initial Cwnd to 0 will not call the TX_START event */
	tp->snd_ssthresh = 0;
	tp->snd_cwnd = init_burst;

	/* Used to avoid to take the SYN-ACK measurements */
	ca->flags = 0;
	ca->flags = FLAG_INIT | FLAG_SAVE;

	ca->burst = init_burst;
	ca->delta_segments = init_burst;
	ca->tx_timer = init_timer_ms * USEC_PER_MSEC;
	ca->pkts_acked = 0;
	ca->backup_pkts_acked = 0;
	ca->aligned_acks_rcv = 0;
	ca->first_ack_time = ns_to_ktime(0);
	ca->backup_first_ack_time_us = 0;
	ca->heuristic_scale = 0;
	ca->first_rtt = 0;
	ca->min_rtt = -1; /* a lot of time */
	ca->avg_rtt = 0;
	ca->max_rtt = 0;
	ca->stab_factor = 0;
	ca->previous_ack_t_disp = ns_to_ktime(0);

	ca->history = kmalloc(sizeof(*ca->history), GFP_KERNEL);

	/* Init the history of bwnd */
	INIT_LIST_HEAD(&ca->history->list);

	/* Init our cache pool for the bwnd history */
	ca->cache = KMEM_CACHE(wavetcp_burst_hist, 0);
}

static void wavetcp_release(struct sock *sk)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	struct wavetcp_burst_hist *tmp;
	struct list_head *pos, *q;

	if (!test_flag(ca->flags, FLAG_INIT))
		return;

	pr_debug("%llu sport: %hu [%s]\n", NOW, SPORT(sk), __func__);

	list_for_each_safe(pos, q, &ca->history->list) {
		tmp = list_entry(pos, struct wavetcp_burst_hist, list);
		list_del(pos);
		kmem_cache_free(ca->cache, tmp);
	}

	kfree(ca->history);
	kmem_cache_destroy(ca->cache);
}

/* Please explain that we will be forever in congestion avoidance. */
static u32 wavetcp_recalc_ssthresh(struct sock *sk)
{
	pr_debug("%llu [%s]\n", NOW, __func__);
	return 0;
}

static void wavetcp_state(struct sock *sk, u8 new_state)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	if (!test_flag(ca->flags, FLAG_INIT))
		return;

	switch (new_state) {
	case TCP_CA_Open:
		pr_debug("%llu sport: %hu [%s] set CA_Open\n", NOW,
			 SPORT(sk), __func__);
		/* We have fully recovered, so reset some variables */
		ca->delta_segments = 0;
		break;
	default:
		pr_debug("%llu sport: %hu [%s] set state %u, ignored\n",
			 NOW, SPORT(sk), __func__, new_state);
	}
}

static u32 wavetcp_undo_cwnd(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);

	/* Not implemented yet. We stick to the decision made earlier */
	pr_debug("%llu [%s]\n", NOW, __func__);
	return tp->snd_cwnd;
}

/* Add the size of the burst in the history of bursts */
static void wavetcp_insert_burst(struct wavetcp *ca, u32 burst)
{
	struct wavetcp_burst_hist *cur;

	pr_debug("%llu [%s] adding %u segment in the history of burst\n", NOW,
		 __func__, burst);
	/* Take the memory from the pre-allocated pool */
	cur = (struct wavetcp_burst_hist *)kmem_cache_alloc(ca->cache,
							    GFP_KERNEL);
	BUG_ON(!cur);

	cur->size = burst;
	list_add_tail(&cur->list, &ca->history->list);
}

static void wavetcp_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	if (!test_flag(ca->flags, FLAG_INIT))
		return;

	switch (event) {
	case CA_EVENT_TX_START:
		/* first transmit when no packets in flight */
		pr_debug("%llu sport: %hu [%s] TX_START\n", NOW,
			 SPORT(sk), __func__);

		set_flag(&ca->flags, FLAG_START);

		break;
	default:
		pr_debug("%llu sport: %hu [%s] got event %u, ignored\n",
			 NOW, SPORT(sk), __func__, event);
		break;
	}
}

static void wavetcp_adj_mode(struct sock *sk, unsigned long delta_rtt)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	ca->stab_factor = ca->avg_rtt / ca->tx_timer;

	ca->min_rtt = -1; /* a lot of time */
	ca->avg_rtt = ca->max_rtt;
	ca->tx_timer = init_timer_ms * USEC_PER_MSEC;

	pr_debug("%llu sport: %hu [%s] stab_factor %u, timer %u us, avg_rtt %u us\n",
		 NOW, SPORT(sk), __func__, ca->stab_factor,
		 ca->tx_timer, ca->avg_rtt);
}

static void wavetcp_tracking_mode(struct sock *sk, u64 delta_rtt,
				  ktime_t ack_train_disp)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	if (ktime_is_null(ack_train_disp)) {
		pr_debug("%llu sport: %hu [%s] ack_train_disp is 0. Impossible to do tracking.\n",
			 NOW, SPORT(sk), __func__);
		return;
	}

	ca->tx_timer = (ktime_to_us(ack_train_disp) + (delta_rtt / 2));

	if (ca->tx_timer == 0) {
		pr_debug("%llu sport: %hu [%s] WARNING: tx timer is 0"
			 ", forcefully set it to 1000 us\n",
			 NOW, SPORT(sk), __func__);
		ca->tx_timer = 1000;
	}

	pr_debug("%llu sport: %hu [%s] tx timer is %u us\n",
		 NOW, SPORT(sk), __func__, ca->tx_timer);
}

/* The weight a is:
 *
 * a = (first_rtt - min_rtt) / first_rtt
 *
 */
static u64 wavetcp_compute_weight(u32 first_rtt, u32 min_rtt)
{
	u64 diff = first_rtt - min_rtt;

	diff = diff * AVG_UNIT;

	return div64_u64(diff, first_rtt);
}

static ktime_t heuristic_ack_train_disp(struct sock *sk,
					const struct rate_sample *rs,
					u32 burst)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	ktime_t ack_train_disp = ns_to_ktime(0);
	ktime_t interval = ns_to_ktime(0);
	ktime_t backup_first_ack = ns_to_ktime(0);

	if (rs->interval_us <= 0) {
		pr_debug("%llu sport: %hu [%s] WARNING is not possible "
			 "to heuristically calculate ack_train_disp, returning 0."
			 "Delivered %u, interval_us %li\n",
			 NOW, SPORT(sk), __func__,
			 rs->delivered, rs->interval_us);
		return ack_train_disp;
	}

	interval = ns_to_ktime(rs->interval_us * NSEC_PER_USEC);
	backup_first_ack = ns_to_ktime(ca->backup_first_ack_time_us * NSEC_PER_USEC);

	/* The heuristic takes the RTT of the first ACK, the RTT of the
	 * latest ACK, and uses the difference as ack_train_disp.
	 *
	 * If the sample for the first and last ACK are the same (e.g.,
	 * one ACK per burst) we use as the latest option the value of
	 * interval_us (which is the RTT). However, this value is
	 * exponentially lowered each time we don't have any valid
	 * sample (i.e., we perform a division by 2, by 4, and so on).
	 * The increased transmitted rate, if it is out of the capacity
	 * of the bottleneck, will be compensated by an higher
	 * delta_rtt, and so limited by the adjustment algorithm. This
	 * is a blind search, but we do not have any valid sample...
	 */
	if (ktime_compare(interval, backup_first_ack) > 0) {
		/* first heuristic */
		ack_train_disp = ktime_sub(interval, backup_first_ack);
	} else {
		/* this branch avoids an overflow. However, reaching
		 * this point means that the ACK train is not aligned
		 * with the sent burst.
		 */
		ack_train_disp = ktime_sub(backup_first_ack, interval);
	}

	if (ktime_is_null(ack_train_disp)) {
		/* Blind search */
		u32 blind_interval_us = rs->interval_us >> ca->heuristic_scale;
		++ca->heuristic_scale;
		ack_train_disp = ns_to_ktime(blind_interval_us * NSEC_PER_USEC);
		pr_debug("%llu sport: %hu [%s] we received one BIG ack."
			 " Doing an heuristic with scale %u, interval_us"
			 " %li us, and setting ack_train_disp to %lli us\n",
			 NOW, SPORT(sk), __func__, ca->heuristic_scale,
			 rs->interval_us, ktime_to_us(ack_train_disp));
	} else {
		pr_debug("%llu sport: %hu [%s] we got the first ack with"
			 " interval %u us, the last (this) with interval %li us."
			 " Doing a substraction and setting ack_train_disp"
			 " to %lli us\n", NOW, SPORT(sk), __func__,
			 ca->backup_first_ack_time_us, rs->interval_us,
			 ktime_to_us(ack_train_disp));
	}

	return ack_train_disp;
}

/* In case that round_burst == current_burst:
 *
 * ack_train_disp = last - first * (rcv_ack/rcv_ack-1)
 *                  |__________|   |_________________|
 *                     left               right
 *
 * else (assuming left is last - first)
 *
 *                     left
 * ack_train_disp =  ------------   *  current_burst
 *                     round_burst
 */
static ktime_t get_ack_train_disp(const ktime_t *last_ack_time,
				  const ktime_t *first_ack_time,
				  u8 aligned_acks_rcv, u32 round_burst,
				  u32 current_burst)
{
	u64 left = ktime_to_ns(*last_ack_time) - ktime_to_ns(*first_ack_time);
	u64 right;

	if (round_burst == current_burst) {
		right = (aligned_acks_rcv * AVG_UNIT) / (aligned_acks_rcv - 1);
		pr_debug("%llu [%s] last %lli us, first %lli us, acks %u round_burst %u current_burst %u\n",
			 NOW, __func__, ktime_to_us(*last_ack_time),
			 ktime_to_us(*first_ack_time), aligned_acks_rcv,
			 round_burst, current_burst);
	} else {
		right = current_burst;
		left *= AVG_UNIT;
		do_div(left, round_burst);
		pr_debug("%llu [%s] last %lli us, first %lli us, small_round_burst %u\n",
			 NOW, __func__, ktime_to_us(*last_ack_time),
			 ktime_to_us(*first_ack_time), round_burst);
	}

	return ns_to_ktime((left * right) / AVG_UNIT);
}

static ktime_t calculate_ack_train_disp(struct sock *sk,
					const struct rate_sample *rs,
					u32 burst, u64 delta_rtt_us)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	ktime_t ack_train_disp = ns_to_ktime(0);

	if (ktime_is_null(ca->first_ack_time) || ca->aligned_acks_rcv <= 1) {
		/* We don't have the initial bound of the burst,
		 * or we don't have samples to do measurements
		 */
		if (ktime_is_null(ca->previous_ack_t_disp))
			/* do heuristic without saving anything */
			return heuristic_ack_train_disp(sk, rs, burst);

		/* Returning the previous value */
		return ca->previous_ack_t_disp;
	}

	/* If we have a complete burst, the value returned by get_ack_train_disp
	 * is safe to use. Otherwise, it can be a bad approximation, so it's better
	 * to use the previous value. Of course, if we don't have such value,
	 * a bad approximation is better than nothing.
	 */
	if (burst == ca->burst || ktime_is_null(ca->previous_ack_t_disp))
		ack_train_disp = get_ack_train_disp(&ca->last_ack_time,
						    &ca->first_ack_time,
						    ca->aligned_acks_rcv,
						    burst, ca->burst);
	else
		return ca->previous_ack_t_disp;

	if (ktime_is_null(ack_train_disp)) {
		/* Use the plain previous value */
		pr_debug("%llu sport: %hu [%s] use_plain previous_ack_train_disp %lli us, ack_train_disp %lli us\n",
			 NOW, SPORT(sk), __func__,
			 ktime_to_us(ca->previous_ack_t_disp),
			 ktime_to_us(ack_train_disp));
		return ca->previous_ack_t_disp;
	}

	/* We have a real sample! */
	ca->heuristic_scale = 0;
	ca->previous_ack_t_disp = ack_train_disp;

	pr_debug("%llu sport: %hu [%s] previous_ack_train_disp %lli us, final_ack_train_disp %lli us\n",
		 NOW, SPORT(sk), __func__, ktime_to_us(ca->previous_ack_t_disp),
		 ktime_to_us(ack_train_disp));

	return ack_train_disp;
}

static u32 calculate_avg_rtt(struct sock *sk)
{
	const struct wavetcp *ca = inet_csk_ca(sk);

	/* Why the if?
	 *
	 * a = (first_rtt - min_rtt) / first_rtt = 1 - (min_rtt/first_rtt)
	 *
	 * avg_rtt_0 = (1 - a) * first_rtt
	 *           = (1 - (1 - (min_rtt/first_rtt))) * first_rtt
	 *           = first_rtt - (first_rtt - min_rtt)
	 *           = min_rtt
	 *
	 *
	 * And.. what happen in the else branch? We calculate first a (scaled by
	 * 1024), then do the substraction (1-a) by keeping in the consideration
	 * the scale, and in the end coming back to the result removing the
	 * scaling.
	 *
	 * We divide the equation
	 *
	 * AvgRtt = a * AvgRtt + (1-a)*Rtt
	 *
	 * in two part properly scaled, left and right, and then having a sum of
	 * the two parts to avoid (possible) overflow.
	 */
	if (ca->avg_rtt == 0) {
		pr_debug("%llu sport: %hu [%s] returning min_rtt %u\n",
			 NOW, SPORT(sk), __func__, ca->min_rtt);
		return ca->min_rtt;
	} else if (ca->first_rtt > 0) {
		u32 old_value = ca->avg_rtt;
		u64 right;
		u64 left;
		u64 a;

		a = wavetcp_compute_weight(ca->first_rtt, ca->min_rtt);

		left = (a * ca->avg_rtt) / AVG_UNIT;
		right = ((AVG_UNIT - a) * ca->first_rtt) / AVG_UNIT;

		pr_debug("%llu sport: %hu [%s] previous avg %u us, first_rtt %u us, "
			 "min %u us, a (shifted) %llu, calculated avg %u us\n",
			 NOW, SPORT(sk), __func__, old_value, ca->first_rtt,
			 ca->min_rtt, a, (u32)left + (u32)right);
		return (u32)left + (u32)right;
	}

	pr_debug("%llu sport: %hu [%s] Can't calculate avg_rtt.\n",
		 NOW, SPORT(sk), __func__);
	return 0;
}

static u64 calculate_delta_rtt(const struct wavetcp *ca)
{
	return ca->avg_rtt - ca->min_rtt;
}

static void wavetcp_round_terminated(struct sock *sk,
				     const struct rate_sample *rs,
				     u32 burst)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	ktime_t ack_train_disp;
	u64 delta_rtt_us;
	u32 avg_rtt;

	avg_rtt = calculate_avg_rtt(sk);
	if (avg_rtt != 0)
		ca->avg_rtt = avg_rtt;

	/* If we have to wait, let's wait */
	if (ca->stab_factor > 0) {
		--ca->stab_factor;
		pr_debug("%llu sport: %hu [%s] reached burst %u, not applying (stab left: %u)\n",
			 NOW, SPORT(sk), __func__, burst, ca->stab_factor);
		return;
	}

	delta_rtt_us = calculate_delta_rtt(ca);
	ack_train_disp = calculate_ack_train_disp(sk, rs, burst, delta_rtt_us);

	pr_debug("%llu sport: %hu [%s] reached burst %u, drtt %llu, atd %lli\n",
		 NOW, SPORT(sk), __func__, burst, delta_rtt_us,
		 ktime_to_us(ack_train_disp));

	/* delta_rtt_us is in us, beta_ms in ms */
	if (delta_rtt_us > beta_ms * USEC_PER_MSEC)
		wavetcp_adj_mode(sk, delta_rtt_us);
	else
		wavetcp_tracking_mode(sk, delta_rtt_us, ack_train_disp);
}

static void wavetcp_reset_round(struct wavetcp *ca)
{
	ca->first_ack_time = ns_to_ktime(0);
	ca->last_ack_time = ca->first_ack_time;
	ca->backup_first_ack_time_us = 0;
	ca->aligned_acks_rcv = 0;
	ca->first_rtt = 0;
}

static void wavetcp_middle_round(struct sock *sk, ktime_t *last_ack_time,
				 const ktime_t *now)
{
	pr_debug("%llu sport: %hu [%s]", NOW, SPORT(sk), __func__);
	*last_ack_time = *now;
}

static void wavetcp_begin_round(struct sock *sk, ktime_t *first_ack_time,
				ktime_t *last_ack_time, const ktime_t *now)
{
	pr_debug("%llu sport: %hu [%s]", NOW, SPORT(sk), __func__);
	*first_ack_time = *now;
	*last_ack_time = *now;
	pr_debug("%llu sport: %hu [%s], first %lli\n", NOW, SPORT(sk),
		 __func__, ktime_to_us(*first_ack_time));
}

static void wavetcp_rtt_measurements(struct sock *sk, s32 rtt_us,
				     s32 interval_us)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	if (ca->backup_first_ack_time_us == 0 && interval_us > 0)
		ca->backup_first_ack_time_us = interval_us;

	if (rtt_us <= 0)
		return;

	ca->previous_rtt = rtt_us;

	/* Check the first RTT in the round */
	if (ca->first_rtt == 0) {
		ca->first_rtt = rtt_us;

		/* Check the minimum RTT we have seen */
		if (rtt_us < ca->min_rtt) {
			ca->min_rtt = rtt_us;
			pr_debug("%llu sport: %hu [%s] min rtt %u\n", NOW,
				 SPORT(sk), __func__, rtt_us);
		}

		/* Check the maximum RTT we have seen */
		if (rtt_us > ca->max_rtt) {
			ca->max_rtt = rtt_us;
			pr_debug("%llu sport: %hu [%s] max rtt %u\n", NOW,
				 SPORT(sk), __func__, rtt_us);
		}
	}
}

static u32 wavetcp_get_rate(struct sock *sk)
{
	const struct wavetcp *ca = inet_csk_ca(sk);
	u32 rate;

	rate = ca->burst * tcp_mss_to_mtu(sk, tcp_sk(sk)->mss_cache);
	rate *= USEC_PER_SEC / ca->tx_timer;

	pr_debug("%llu sport: %hu [%s] burst 10, mss %u, timer %u us, rate %u",
		 NOW, SPORT(sk), __func__, tcp_mss_to_mtu(sk, tcp_sk(sk)->mss_cache),
		 ca->tx_timer, rate);

	return rate;
}

static void wavetcp_end_round(struct sock *sk, const struct rate_sample *rs,
			      const ktime_t *now, u32 burst_size)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	pr_debug("%llu [%s]", NOW, __func__);

	/* The position we are is end_round, but if the following is false,
	 * in reality we are at the beginning of the next round,
	 * and the previous middle was an end. In the other case,
	 * update last_ack_time with the current time, and the number of
	 * received acks.
	 */
	if (rs->rtt_us >= ca->previous_rtt) {
		++ca->aligned_acks_rcv;
		ca->last_ack_time = *now;
	}

	/* If the round terminates without a sample of RTT, use the average */
	if (ca->first_rtt == 0) {
		ca->first_rtt = ca->avg_rtt;
		pr_debug("%llu sport: %hu [%s] Using the average value for first_rtt %u\n",
		    NOW, SPORT(sk), __func__, ca->first_rtt);
	}

	if (burst_size > min_burst) {
		wavetcp_round_terminated(sk, rs, burst_size);
		sk->sk_pacing_rate = wavetcp_get_rate(sk);
	} else {
		pr_debug("%llu sport: %hu [%s] skipping burst of %u segments\n",
			 NOW, SPORT(sk), __func__, burst_size);
	}

	wavetcp_reset_round(ca);

	/* We have to emulate a beginning of the round in case this RTT is less than
	 * the previous one
	 */
	if (rs->rtt_us > 0 && rs->rtt_us < ca->previous_rtt) {
		pr_debug("%llu sport: %hu [%s] Emulating the beginning, set the first_rtt to %u\n",
			 NOW, SPORT(sk), __func__, ca->first_rtt);

		/* Emulate the beginning of the round using as "now"
		 * the time of the previous ACK
		 */
		wavetcp_begin_round(sk, &ca->first_ack_time,
				    &ca->last_ack_time, now);
		/* Emulate a middle round with the current time */
		wavetcp_middle_round(sk, &ca->last_ack_time, now);

		/* Take the measurements for the RTT. If we are not emulating a
		 * beginning, then let the real begin to take it
		 */
		wavetcp_rtt_measurements(sk, rs->rtt_us, rs->interval_us);

		/* Emulate the reception of one aligned ack, this */
		ca->aligned_acks_rcv = 1;
	} else if (rs->rtt_us > 0) {
		ca->previous_rtt = rs->rtt_us;
	}
}

static void wavetcp_cong_control(struct sock *sk, const struct rate_sample *rs)
{
	ktime_t now = ktime_get();
	struct wavetcp *ca = inet_csk_ca(sk);
	struct wavetcp_burst_hist *tmp;
	struct list_head *pos;

	if (!test_flag(ca->flags, FLAG_INIT))
		return;

	pr_debug("%llu sport: %hu [%s] prior_delivered %u, delivered %i, interval_us %li, "
		 "rtt_us %li, losses %i, ack_sack %u, prior_in_flight %u, is_app %i,"
		 " is_retrans %i\n", NOW, SPORT(sk), __func__,
		 rs->prior_delivered, rs->delivered, rs->interval_us,
		 rs->rtt_us, rs->losses, rs->acked_sacked, rs->prior_in_flight,
		 rs->is_app_limited, rs->is_retrans);

	pos = ca->history->list.next;
	tmp = list_entry(pos, struct wavetcp_burst_hist, list);

	if (!tmp)
		return;

	/* Train management.*/
	ca->pkts_acked += rs->acked_sacked;

	if (ca->previous_rtt < rs->rtt_us)
		pr_debug("%llu sport: %hu [%s] previous < rtt: %u < %li",
			 NOW, SPORT(sk), __func__, ca->previous_rtt,
			 rs->rtt_us);
	else
		pr_debug("%llu sport: %hu [%s] previous >= rtt: %u >= %li",
			 NOW, SPORT(sk), __func__, ca->previous_rtt,
			 rs->rtt_us);

	/* We have three possibilities: beginning, middle, end.
	 *  - Beginning: is the moment in which we receive the first ACK for
	 *    the round
	 *  - Middle: we are receiving ACKs but still not as many to cover a
	 *    complete burst
	 *  - End: the other end ACKed sufficient bytes to declare a round
	 *    completed
	 */
	if (ca->pkts_acked < tmp->size) {
		/* The way to discriminate between beginning and end is thanks
		 * to ca->first_ack_time, which is zeroed at the end of a run
		 */
		if (ktime_is_null(ca->first_ack_time)) {
			wavetcp_begin_round(sk, &ca->first_ack_time,
					    &ca->last_ack_time, &now);
			++ca->aligned_acks_rcv;
			ca->backup_pkts_acked = ca->pkts_acked - rs->acked_sacked;

			pr_debug("%llu sport: %hu [%s] first ack of the train\n",
				 NOW, SPORT(sk), __func__);
		} else {
			if (rs->rtt_us >= ca->previous_rtt) {
				wavetcp_middle_round(sk, &ca->last_ack_time, &now);
				++ca->aligned_acks_rcv;
				pr_debug("%llu sport: %hu [%s] middle aligned ack (tot %u)\n",
					 NOW, SPORT(sk), __func__,
					 ca->aligned_acks_rcv);
			} else if (rs->rtt_us > 0) {
				/* This is the real round beginning! */
				ca->aligned_acks_rcv = 1;
				ca->pkts_acked = ca->backup_pkts_acked + rs->acked_sacked;

				wavetcp_begin_round(sk, &ca->first_ack_time,
						    &ca->last_ack_time, &now);

				pr_debug("%llu sport: %hu [%s] changed beginning to NOW\n",
					 NOW, SPORT(sk), __func__);
			}
		}

		/* Take RTT measurements for min and max measurments. For the
		 * end of the burst, do it manually depending on the case
		 */
		wavetcp_rtt_measurements(sk, rs->rtt_us, rs->interval_us);
	} else {
		wavetcp_end_round(sk, rs, &now, tmp->size);
		/* Consume the burst history if it's a cumulative ACK for many bursts */
		while (tmp && ca->pkts_acked >= tmp->size) {
			ca->pkts_acked -= tmp->size;

			/* Delete the burst from the history */
			pr_debug("%llu sport: %hu [%s] deleting burst of %u segments\n",
				 NOW, SPORT(sk), __func__, tmp->size);
			list_del(pos);
			kmem_cache_free(ca->cache, tmp);

			/* Take next burst */
			pos = ca->history->list.next;
			tmp = list_entry(pos, struct wavetcp_burst_hist, list);
		}
	}
}

/* Invoked each time we receive an ACK. Obviously, this function also gets
 * called when we receive the SYN-ACK, but we ignore it thanks to the
 * FLAG_INIT flag.
 *
 * We close the cwnd of the amount of segments acked, because we don't like
 * sending out segments if the timer is not expired. Without doing this, we
 * would end with cwnd - in_flight > 0.
 */
static void wavetcp_acked(struct sock *sk, const struct ack_sample *sample)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	if (!test_flag(ca->flags, FLAG_INIT))
		return;

	if (tp->snd_cwnd < sample->pkts_acked) {
		/* We sent some scattered segments, so the burst segments and
		 * the ACK we get is not aligned.
		 */
		pr_debug("%llu sport: %hu [%s] delta_seg %i\n",
			 NOW, SPORT(sk), __func__, ca->delta_segments);

		ca->delta_segments += sample->pkts_acked - tp->snd_cwnd;
	}

	pr_debug("%llu sport: %hu [%s] pkts_acked %u, rtt_us %i, in_flight %u "
		 ", cwnd %u, seq ack %u, delta %i\n", NOW, SPORT(sk),
		 __func__, sample->pkts_acked, sample->rtt_us,
		 sample->in_flight, tp->snd_cwnd, tp->snd_una,
		 ca->delta_segments);

	/* Brutally set the cwnd in order to not let segment out */
	tp->snd_cwnd = tcp_packets_in_flight(tp);
}

/* The TCP informs us that the timer is expired (or has never been set). We can
 * infer the latter by the FLAG_STARTED flag: if it's false, don't increase the
 * cwnd, because it is at its default value (init_burst) and we still have to
 * transmit the first burst.
 */
static void wavetcp_timer_expired(struct sock *sk)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 current_burst = ca->burst;

	if (!test_flag(ca->flags, FLAG_START) ||
	    !test_flag(ca->flags, FLAG_INIT)) {
		pr_debug("%llu sport: %hu [%s] returning because of flags, leaving cwnd %u\n",
			 NOW, SPORT(sk), __func__, tp->snd_cwnd);
		return;
	}

	pr_debug("%llu sport: %hu [%s] starting with delta %u current_burst %u\n",
		 NOW, SPORT(sk), __func__, ca->delta_segments, current_burst);

	if (ca->delta_segments < 0) {
		/* In the previous round, we sent more than the allowed burst,
		 * so reduce the current burst.
		 */
		BUG_ON(current_burst > ca->delta_segments);
		current_burst += ca->delta_segments; /* please *reduce* */

		/* Right now, we should send "current_burst" segments out */

		if (tcp_packets_in_flight(tp) > tp->snd_cwnd) {
			/* For some reasons (e.g., tcp loss probe)
			 * we sent something outside the allowed window.
			 * Add the amount of segments into the burst, in order
			 * to effectively send the previous "current_burst"
			 * segments, but without touching delta_segments.
			 */
			u32 diff = tcp_packets_in_flight(tp) - tp->snd_cwnd;

			current_burst += diff;
			pr_debug("%llu sport: %hu [%s] adding %u to balance "
				 "segments sent out of window", NOW,
				 SPORT(sk), __func__, diff);
		}
	}

	ca->delta_segments = current_burst;
	pr_debug("%llu sport: %hu [%s] setting delta_seg %u current burst %u\n",
		 NOW, SPORT(sk), __func__, ca->delta_segments, current_burst);

	if (current_burst < min_burst) {
		pr_debug("%llu sport: %hu [%s] WARNING !! not min_burst",
			 NOW, SPORT(sk), __func__);
		ca->delta_segments += min_burst - current_burst;
		current_burst = min_burst;
	}

	tp->snd_cwnd += current_burst;
	set_flag(&ca->flags, FLAG_SAVE);

	pr_debug("%llu sport: %hu [%s], increased window of %u segments, "
		 "total %u, delta %i, in_flight %u\n", NOW, SPORT(sk),
		 __func__, ca->burst, tp->snd_cwnd, ca->delta_segments,
		 tcp_packets_in_flight(tp));

	if (tp->snd_cwnd - tcp_packets_in_flight(tp) > current_burst) {
		pr_debug("%llu sport: %hu [%s] WARNING! "
			 " cwnd %u, in_flight %u, current burst %u\n",
			 NOW, SPORT(sk), __func__, tp->snd_cwnd,
			 tcp_packets_in_flight(tp), current_burst);
	}
}

static u64 wavetcp_get_timer(struct sock *sk)
{
	struct wavetcp *ca = inet_csk_ca(sk);
	u64 timer;

	BUG_ON(!test_flag(ca->flags, FLAG_INIT));

	timer = min_t(u64,
		      ca->tx_timer * NSEC_PER_USEC,
		      init_timer_ms * NSEC_PER_MSEC);

	pr_debug("%llu sport: %hu [%s] returning timer of %llu ns\n",
		 NOW, SPORT(sk), __func__, timer);

	return timer;
}

static void wavetcp_segment_sent(struct sock *sk, u32 sent)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct wavetcp *ca = inet_csk_ca(sk);

	if (!test_flag(ca->flags, FLAG_START)) {
		pr_debug("%llu sport: %hu [%s] !START\n",
			 NOW, SPORT(sk), __func__);
		return;
	}

	if (test_flag(ca->flags, FLAG_SAVE) && sent > 0) {
		wavetcp_insert_burst(ca, sent);
		clear_flag(&ca->flags, FLAG_SAVE);
	} else {
		pr_debug("%llu sport: %hu [%s] not saving burst, sent %u\n",
			 NOW, SPORT(sk), __func__, sent);
	}

	if (sent > ca->burst) {
		pr_debug("%llu sport: %hu [%s] WARNING! sent %u, burst %u"
		    " cwnd %u delta_seg %i\n, TSO very probable", NOW,
		    SPORT(sk), __func__, sent, ca->burst,
		    tp->snd_cwnd, ca->delta_segments);
	}

	ca->delta_segments -= sent;

	if (ca->delta_segments >= 0 &&
	    ca->burst > sent &&
	    tcp_packets_in_flight(tp) <= tp->snd_cwnd) {
		/* Reduce the cwnd accordingly, because we didn't sent enough
		 * to cover it (we are app limited probably)
		 */
		u32 diff = ca->burst - sent;

		if (tp->snd_cwnd >= diff)
			tp->snd_cwnd -= diff;
		else
			tp->snd_cwnd = 0;
		pr_debug("%llu sport: %hu [%s] reducing cwnd by %u, value %u\n",
			 NOW, SPORT(sk), __func__,
			 ca->burst - sent, tp->snd_cwnd);
	}
}

static size_t wavetcp_get_info(struct sock *sk, u32 ext, int *attr,
			       union tcp_cc_info *info)
{
	pr_debug("%llu [%s] ext=%u", NOW, __func__, ext);

	if (ext & (1 << (INET_DIAG_WAVEINFO - 1))) {
		struct wavetcp *ca = inet_csk_ca(sk);

		memset(&info->wave, 0, sizeof(info->wave));
		info->wave.tx_timer	= ca->tx_timer;
		info->wave.burst	= ca->burst;
		info->wave.previous_ack_t_disp = ca->previous_ack_t_disp;
		info->wave.min_rtt	= ca->min_rtt;
		info->wave.avg_rtt	= ca->avg_rtt;
		info->wave.max_rtt	= ca->max_rtt;
		*attr = INET_DIAG_WAVEINFO;
		return sizeof(info->wave);
	}
	return 0;
}

static u32 wavetcp_sndbuf_expand(struct sock *sk)
{
	return 10;
}

static u32 wavetcp_get_segs_per_round(struct sock *sk)
{
	struct wavetcp *ca = inet_csk_ca(sk);

	return ca->burst;
}

static struct tcp_congestion_ops wave_cong_tcp __read_mostly = {
	.init			= wavetcp_init,
	.get_info		= wavetcp_get_info,
	.release		= wavetcp_release,
	.ssthresh		= wavetcp_recalc_ssthresh,
/*	.cong_avoid		= wavetcp_cong_avoid, */
	.cong_control		= wavetcp_cong_control,
	.set_state		= wavetcp_state,
	.undo_cwnd		= wavetcp_undo_cwnd,
	.cwnd_event		= wavetcp_cwnd_event,
	.pkts_acked		= wavetcp_acked,
	.sndbuf_expand		= wavetcp_sndbuf_expand,
	.get_pacing_time	= wavetcp_get_timer,
	.pacing_timer_expired	= wavetcp_timer_expired,
	.get_segs_per_round	= wavetcp_get_segs_per_round,
	.segments_sent		= wavetcp_segment_sent,
	.owner			= THIS_MODULE,
	.name			= "wave",
};

static int __init wavetcp_register(void)
{
	BUILD_BUG_ON(sizeof(struct wavetcp) > ICSK_CA_PRIV_SIZE);

	return tcp_register_congestion_control(&wave_cong_tcp);
}

static void __exit wavetcp_unregister(void)
{
	tcp_unregister_congestion_control(&wave_cong_tcp);
}

module_init(wavetcp_register);
module_exit(wavetcp_unregister);

MODULE_AUTHOR("Natale Patriciello");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WAVE TCP");
MODULE_VERSION("0.2");
