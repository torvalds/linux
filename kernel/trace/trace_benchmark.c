// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/trace_clock.h>

#define CREATE_TRACE_POINTS
#include "trace_benchmark.h"

static struct task_struct *bm_event_thread;

static char bm_str[BENCHMARK_EVENT_STRLEN] = "START";

static u64 bm_total;
static u64 bm_totalsq;
static u64 bm_last;
static u64 bm_max;
static u64 bm_min;
static u64 bm_first;
static u64 bm_cnt;
static u64 bm_stddev;
static unsigned int bm_avg;
static unsigned int bm_std;

static bool ok_to_run;

/*
 * This gets called in a loop recording the time it took to write
 * the tracepoint. What it writes is the time statistics of the last
 * tracepoint write. As there is nothing to write the first time
 * it simply writes "START". As the first write is cold cache and
 * the rest is hot, we save off that time in bm_first and it is
 * reported as "first", which is shown in the second write to the
 * tracepoint. The "first" field is written within the statics from
 * then on but never changes.
 */
static void trace_do_benchmark(void)
{
	u64 start;
	u64 stop;
	u64 delta;
	u64 stddev;
	u64 seed;
	u64 last_seed;
	unsigned int avg;
	unsigned int std = 0;

	/* Only run if the tracepoint is actually active */
	if (!trace_benchmark_event_enabled() || !tracing_is_on())
		return;

	local_irq_disable();
	start = trace_clock_local();
	trace_benchmark_event(bm_str, bm_last);
	stop = trace_clock_local();
	local_irq_enable();

	bm_cnt++;

	delta = stop - start;

	/*
	 * The first read is cold cached, keep it separate from the
	 * other calculations.
	 */
	if (bm_cnt == 1) {
		bm_first = delta;
		scnprintf(bm_str, BENCHMARK_EVENT_STRLEN,
			  "first=%llu [COLD CACHED]", bm_first);
		return;
	}

	bm_last = delta;

	if (delta > bm_max)
		bm_max = delta;
	if (!bm_min || delta < bm_min)
		bm_min = delta;

	/*
	 * When bm_cnt is greater than UINT_MAX, it breaks the statistics
	 * accounting. Freeze the statistics when that happens.
	 * We should have enough data for the avg and stddev anyway.
	 */
	if (bm_cnt > UINT_MAX) {
		scnprintf(bm_str, BENCHMARK_EVENT_STRLEN,
		    "last=%llu first=%llu max=%llu min=%llu ** avg=%u std=%d std^2=%lld",
			  bm_last, bm_first, bm_max, bm_min, bm_avg, bm_std, bm_stddev);
		return;
	}

	bm_total += delta;
	bm_totalsq += delta * delta;

	if (bm_cnt > 1) {
		/*
		 * Apply Welford's method to calculate standard deviation:
		 * s^2 = 1 / (n * (n-1)) * (n * \Sum (x_i)^2 - (\Sum x_i)^2)
		 */
		stddev = (u64)bm_cnt * bm_totalsq - bm_total * bm_total;
		do_div(stddev, (u32)bm_cnt);
		do_div(stddev, (u32)bm_cnt - 1);
	} else
		stddev = 0;

	delta = bm_total;
	do_div(delta, (u32)bm_cnt);
	avg = delta;

	if (stddev > 0) {
		int i = 0;
		/*
		 * stddev is the square of standard deviation but
		 * we want the actually number. Use the average
		 * as our seed to find the std.
		 *
		 * The next try is:
		 *  x = (x + N/x) / 2
		 *
		 * Where N is the squared number to find the square
		 * root of.
		 */
		seed = avg;
		do {
			last_seed = seed;
			seed = stddev;
			if (!last_seed)
				break;
			seed = div64_u64(seed, last_seed);
			seed += last_seed;
			do_div(seed, 2);
		} while (i++ < 10 && last_seed != seed);

		std = seed;
	}

	scnprintf(bm_str, BENCHMARK_EVENT_STRLEN,
		  "last=%llu first=%llu max=%llu min=%llu avg=%u std=%d std^2=%lld",
		  bm_last, bm_first, bm_max, bm_min, avg, std, stddev);

	bm_std = std;
	bm_avg = avg;
	bm_stddev = stddev;
}

static int benchmark_event_kthread(void *arg)
{
	/* sleep a bit to make sure the tracepoint gets activated */
	msleep(100);

	while (!kthread_should_stop()) {

		trace_do_benchmark();

		/*
		 * We don't go to sleep, but let others run as well.
		 * This is basically a "yield()" to let any task that
		 * wants to run, schedule in, but if the CPU is idle,
		 * we'll keep burning cycles.
		 *
		 * Note the tasks_rcu_qs() version of cond_resched() will
		 * notify synchronize_rcu_tasks() that this thread has
		 * passed a quiescent state for rcu_tasks. Otherwise
		 * this thread will never voluntarily schedule which would
		 * block synchronize_rcu_tasks() indefinitely.
		 */
		cond_resched_tasks_rcu_qs();
	}

	return 0;
}

/*
 * When the benchmark tracepoint is enabled, it calls this
 * function and the thread that calls the tracepoint is created.
 */
int trace_benchmark_reg(void)
{
	if (!ok_to_run) {
		pr_warn("trace benchmark cannot be started via kernel command line\n");
		return -EBUSY;
	}

	bm_event_thread = kthread_run(benchmark_event_kthread,
				      NULL, "event_benchmark");
	if (IS_ERR(bm_event_thread)) {
		pr_warn("trace benchmark failed to create kernel thread\n");
		return PTR_ERR(bm_event_thread);
	}

	return 0;
}

/*
 * When the benchmark tracepoint is disabled, it calls this
 * function and the thread that calls the tracepoint is deleted
 * and all the numbers are reset.
 */
void trace_benchmark_unreg(void)
{
	if (!bm_event_thread)
		return;

	kthread_stop(bm_event_thread);
	bm_event_thread = NULL;

	strcpy(bm_str, "START");
	bm_total = 0;
	bm_totalsq = 0;
	bm_last = 0;
	bm_max = 0;
	bm_min = 0;
	bm_cnt = 0;
	/* These don't need to be reset but reset them anyway */
	bm_first = 0;
	bm_std = 0;
	bm_avg = 0;
	bm_stddev = 0;
}

static __init int ok_to_run_trace_benchmark(void)
{
	ok_to_run = true;

	return 0;
}

early_initcall(ok_to_run_trace_benchmark);
