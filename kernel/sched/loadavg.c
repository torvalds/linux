/*
 * kernel/sched/loadavg.c
 *
 * This file contains the magic bits required to compute the global loadavg
 * figure. Its a silly number but people think its important. We go through
 * great pains to make it work on big machines and tickless kernels.
 */

#include <linux/export.h>

#include "sched.h"

/*
 * Global load-average calculations
 *
 * We take a distributed and async approach to calculating the global load-avg
 * in order to minimize overhead.
 *
 * The global load average is an exponentially decaying average of nr_running +
 * nr_uninterruptible.
 *
 * Once every LOAD_FREQ:
 *
 *   nr_active = 0;
 *   for_each_possible_cpu(cpu)
 *	nr_active += cpu_of(cpu)->nr_running + cpu_of(cpu)->nr_uninterruptible;
 *
 *   avenrun[n] = avenrun[0] * exp_n + nr_active * (1 - exp_n)
 *
 * Due to a number of reasons the above turns in the mess below:
 *
 *  - for_each_possible_cpu() is prohibitively expensive on machines with
 *    serious number of cpus, therefore we need to take a distributed approach
 *    to calculating nr_active.
 *
 *        \Sum_i x_i(t) = \Sum_i x_i(t) - x_i(t_0) | x_i(t_0) := 0
 *                      = \Sum_i { \Sum_j=1 x_i(t_j) - x_i(t_j-1) }
 *
 *    So assuming nr_active := 0 when we start out -- true per definition, we
 *    can simply take per-cpu deltas and fold those into a global accumulate
 *    to obtain the same result. See calc_load_fold_active().
 *
 *    Furthermore, in order to avoid synchronizing all per-cpu delta folding
 *    across the machine, we assume 10 ticks is sufficient time for every
 *    cpu to have completed this task.
 *
 *    This places an upper-bound on the IRQ-off latency of the machine. Then
 *    again, being late doesn't loose the delta, just wrecks the sample.
 *
 *  - cpu_rq()->nr_uninterruptible isn't accurately tracked per-cpu because
 *    this would add another cross-cpu cacheline miss and atomic operation
 *    to the wakeup path. Instead we increment on whatever cpu the task ran
 *    when it went into uninterruptible state and decrement on whatever cpu
 *    did the wakeup. This means that only the sum of nr_uninterruptible over
 *    all cpus yields the correct result.
 *
 *  This covers the NO_HZ=n code, for extra head-aches, see the comment below.
 */

/* Variables and functions for calc_load */
atomic_long_t calc_load_tasks;
unsigned long calc_load_update;
unsigned long avenrun[3];
EXPORT_SYMBOL(avenrun); /* should be removed */

/**
 * get_avenrun - get the load average array
 * @loads:	pointer to dest load array
 * @offset:	offset to add
 * @shift:	shift count to shift the result left
 *
 * These values are estimates at best, so no need for locking.
 */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift)
{
	loads[0] = (avenrun[0] + offset) << shift;
	loads[1] = (avenrun[1] + offset) << shift;
	loads[2] = (avenrun[2] + offset) << shift;
}

long calc_load_fold_active(struct rq *this_rq)
{
	long nr_active, delta = 0;

	nr_active = this_rq->nr_running;
	nr_active += (long)this_rq->nr_uninterruptible;

	if (nr_active != this_rq->calc_load_active) {
		delta = nr_active - this_rq->calc_load_active;
		this_rq->calc_load_active = nr_active;
	}

	return delta;
}

/*
 * a1 = a0 * e + a * (1 - e)
 */
static unsigned long
calc_load(unsigned long load, unsigned long exp, unsigned long active)
{
	unsigned long newload;

	newload = load * exp + active * (FIXED_1 - exp);
	if (active >= load)
		newload += FIXED_1-1;

	return newload / FIXED_1;
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * Handle NO_HZ for the global load-average.
 *
 * Since the above described distributed algorithm to compute the global
 * load-average relies on per-cpu sampling from the tick, it is affected by
 * NO_HZ.
 *
 * The basic idea is to fold the nr_active delta into a global idle-delta upon
 * entering NO_HZ state such that we can include this as an 'extra' cpu delta
 * when we read the global state.
 *
 * Obviously reality has to ruin such a delightfully simple scheme:
 *
 *  - When we go NO_HZ idle during the window, we can negate our sample
 *    contribution, causing under-accounting.
 *
 *    We avoid this by keeping two idle-delta counters and flipping them
 *    when the window starts, thus separating old and new NO_HZ load.
 *
 *    The only trick is the slight shift in index flip for read vs write.
 *
 *        0s            5s            10s           15s
 *          +10           +10           +10           +10
 *        |-|-----------|-|-----------|-|-----------|-|
 *    r:0 0 1           1 0           0 1           1 0
 *    w:0 1 1           0 0           1 1           0 0
 *
 *    This ensures we'll fold the old idle contribution in this window while
 *    accumlating the new one.
 *
 *  - When we wake up from NO_HZ idle during the window, we push up our
 *    contribution, since we effectively move our sample point to a known
 *    busy state.
 *
 *    This is solved by pushing the window forward, and thus skipping the
 *    sample, for this cpu (effectively using the idle-delta for this cpu which
 *    was in effect at the time the window opened). This also solves the issue
 *    of having to deal with a cpu having been in NOHZ idle for multiple
 *    LOAD_FREQ intervals.
 *
 * When making the ILB scale, we should try to pull this in as well.
 */
static atomic_long_t calc_load_idle[2];
static int calc_load_idx;

static inline int calc_load_write_idx(void)
{
	int idx = calc_load_idx;

	/*
	 * See calc_global_nohz(), if we observe the new index, we also
	 * need to observe the new update time.
	 */
	smp_rmb();

	/*
	 * If the folding window started, make sure we start writing in the
	 * next idle-delta.
	 */
	if (!time_before(jiffies, calc_load_update))
		idx++;

	return idx & 1;
}

static inline int calc_load_read_idx(void)
{
	return calc_load_idx & 1;
}

void calc_load_enter_idle(void)
{
	struct rq *this_rq = this_rq();
	long delta;

	/*
	 * We're going into NOHZ mode, if there's any pending delta, fold it
	 * into the pending idle delta.
	 */
	delta = calc_load_fold_active(this_rq);
	if (delta) {
		int idx = calc_load_write_idx();

		atomic_long_add(delta, &calc_load_idle[idx]);
	}
}

void calc_load_exit_idle(void)
{
	struct rq *this_rq = this_rq();

	/*
	 * If we're still before the pending sample window, we're done.
	 */
	this_rq->calc_load_update = calc_load_update;
	if (time_before(jiffies, this_rq->calc_load_update))
		return;

	/*
	 * We woke inside or after the sample window, this means we're already
	 * accounted through the nohz accounting, so skip the entire deal and
	 * sync up for the next window.
	 */
	if (time_before(jiffies, this_rq->calc_load_update + 10))
		this_rq->calc_load_update += LOAD_FREQ;
}

static long calc_load_fold_idle(void)
{
	int idx = calc_load_read_idx();
	long delta = 0;

	if (atomic_long_read(&calc_load_idle[idx]))
		delta = atomic_long_xchg(&calc_load_idle[idx], 0);

	return delta;
}

/**
 * fixed_power_int - compute: x^n, in O(log n) time
 *
 * @x:         base of the power
 * @frac_bits: fractional bits of @x
 * @n:         power to raise @x to.
 *
 * By exploiting the relation between the definition of the natural power
 * function: x^n := x*x*...*x (x multiplied by itself for n times), and
 * the binary encoding of numbers used by computers: n := \Sum n_i * 2^i,
 * (where: n_i \elem {0, 1}, the binary vector representing n),
 * we find: x^n := x^(\Sum n_i * 2^i) := \Prod x^(n_i * 2^i), which is
 * of course trivially computable in O(log_2 n), the length of our binary
 * vector.
 */
static unsigned long
fixed_power_int(unsigned long x, unsigned int frac_bits, unsigned int n)
{
	unsigned long result = 1UL << frac_bits;

	if (n) {
		for (;;) {
			if (n & 1) {
				result *= x;
				result += 1UL << (frac_bits - 1);
				result >>= frac_bits;
			}
			n >>= 1;
			if (!n)
				break;
			x *= x;
			x += 1UL << (frac_bits - 1);
			x >>= frac_bits;
		}
	}

	return result;
}

/*
 * a1 = a0 * e + a * (1 - e)
 *
 * a2 = a1 * e + a * (1 - e)
 *    = (a0 * e + a * (1 - e)) * e + a * (1 - e)
 *    = a0 * e^2 + a * (1 - e) * (1 + e)
 *
 * a3 = a2 * e + a * (1 - e)
 *    = (a0 * e^2 + a * (1 - e) * (1 + e)) * e + a * (1 - e)
 *    = a0 * e^3 + a * (1 - e) * (1 + e + e^2)
 *
 *  ...
 *
 * an = a0 * e^n + a * (1 - e) * (1 + e + ... + e^n-1) [1]
 *    = a0 * e^n + a * (1 - e) * (1 - e^n)/(1 - e)
 *    = a0 * e^n + a * (1 - e^n)
 *
 * [1] application of the geometric series:
 *
 *              n         1 - x^(n+1)
 *     S_n := \Sum x^i = -------------
 *             i=0          1 - x
 */
static unsigned long
calc_load_n(unsigned long load, unsigned long exp,
	    unsigned long active, unsigned int n)
{
	return calc_load(load, fixed_power_int(exp, FSHIFT, n), active);
}

/*
 * NO_HZ can leave us missing all per-cpu ticks calling
 * calc_load_account_active(), but since an idle CPU folds its delta into
 * calc_load_tasks_idle per calc_load_account_idle(), all we need to do is fold
 * in the pending idle delta if our idle period crossed a load cycle boundary.
 *
 * Once we've updated the global active value, we need to apply the exponential
 * weights adjusted to the number of cycles missed.
 */
static void calc_global_nohz(void)
{
	long delta, active, n;

	if (!time_before(jiffies, calc_load_update + 10)) {
		/*
		 * Catch-up, fold however many we are behind still
		 */
		delta = jiffies - calc_load_update - 10;
		n = 1 + (delta / LOAD_FREQ);

		active = atomic_long_read(&calc_load_tasks);
		active = active > 0 ? active * FIXED_1 : 0;

		avenrun[0] = calc_load_n(avenrun[0], EXP_1, active, n);
		avenrun[1] = calc_load_n(avenrun[1], EXP_5, active, n);
		avenrun[2] = calc_load_n(avenrun[2], EXP_15, active, n);

		calc_load_update += n * LOAD_FREQ;
	}

	/*
	 * Flip the idle index...
	 *
	 * Make sure we first write the new time then flip the index, so that
	 * calc_load_write_idx() will see the new time when it reads the new
	 * index, this avoids a double flip messing things up.
	 */
	smp_wmb();
	calc_load_idx++;
}
#else /* !CONFIG_NO_HZ_COMMON */

static inline long calc_load_fold_idle(void) { return 0; }
static inline void calc_global_nohz(void) { }

#endif /* CONFIG_NO_HZ_COMMON */

/*
 * calc_load - update the avenrun load estimates 10 ticks after the
 * CPUs have updated calc_load_tasks.
 *
 * Called from the global timer code.
 */
void calc_global_load(unsigned long ticks)
{
	long active, delta;

	if (time_before(jiffies, calc_load_update + 10))
		return;

	/*
	 * Fold the 'old' idle-delta to include all NO_HZ cpus.
	 */
	delta = calc_load_fold_idle();
	if (delta)
		atomic_long_add(delta, &calc_load_tasks);

	active = atomic_long_read(&calc_load_tasks);
	active = active > 0 ? active * FIXED_1 : 0;

	avenrun[0] = calc_load(avenrun[0], EXP_1, active);
	avenrun[1] = calc_load(avenrun[1], EXP_5, active);
	avenrun[2] = calc_load(avenrun[2], EXP_15, active);

	calc_load_update += LOAD_FREQ;

	/*
	 * In case we idled for multiple LOAD_FREQ intervals, catch up in bulk.
	 */
	calc_global_nohz();
}

/*
 * Called from scheduler_tick() to periodically update this CPU's
 * active count.
 */
void calc_global_load_tick(struct rq *this_rq)
{
	long delta;

	if (time_before(jiffies, this_rq->calc_load_update))
		return;

	delta  = calc_load_fold_active(this_rq);
	if (delta)
		atomic_long_add(delta, &calc_load_tasks);

	this_rq->calc_load_update += LOAD_FREQ;
}
