// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/trace.h>
#include <linux/trace_events.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/jiffies.h>

/*
 * Any file that uses trace points, must include the header.
 * But only one file, must include the header by defining
 * CREATE_TRACE_POINTS first.  This will make the C code that
 * creates the handles for the trace points.
 */
#define CREATE_TRACE_POINTS
#include "sample-trace-array.h"

struct trace_array *tr;
static void mytimer_handler(struct timer_list *unused);
static struct task_struct *simple_tsk;

/*
 * mytimer: Timer setup to disable tracing for event "sample_event". This
 * timer is only for the purposes of the sample module to demonstrate access of
 * Ftrace instances from within kernel.
 */
static DEFINE_TIMER(mytimer, mytimer_handler);

static void mytimer_handler(struct timer_list *unused)
{
	/*
	 * Disable tracing for event "sample_event".
	 */
	trace_array_set_clr_event(tr, "sample-subsystem", "sample_event",
			false);
}

static void simple_thread_func(int count)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ);

	/*
	 * Printing count value using trace_array_printk() - trace_printk()
	 * equivalent for the instance buffers.
	 */
	trace_array_printk(tr, _THIS_IP_, "trace_array_printk: count=%d\n",
			count);
	/*
	 * Tracepoint for event "sample_event". This will print the
	 * current value of count and current jiffies.
	 */
	trace_sample_event(count, jiffies);
}

static int simple_thread(void *arg)
{
	int count = 0;
	unsigned long delay = msecs_to_jiffies(5000);

	/*
	 * Enable tracing for "sample_event".
	 */
	trace_array_set_clr_event(tr, "sample-subsystem", "sample_event", true);

	/*
	 * Adding timer - mytimer. This timer will disable tracing after
	 * delay seconds.
	 *
	 */
	add_timer(&mytimer);
	mod_timer(&mytimer, jiffies+delay);

	while (!kthread_should_stop())
		simple_thread_func(count++);

	del_timer(&mytimer);

	/*
	 * trace_array_put() decrements the reference counter associated with
	 * the trace array - "tr". We are done using the trace array, hence
	 * decrement the reference counter so that it can be destroyed using
	 * trace_array_destroy().
	 */
	trace_array_put(tr);

	return 0;
}

static int __init sample_trace_array_init(void)
{
	/*
	 * Return a pointer to the trace array with name "sample-instance" if it
	 * exists, else create a new trace array.
	 *
	 * NOTE: This function increments the reference counter
	 * associated with the trace array - "tr".
	 */
	tr = trace_array_get_by_name("sample-instance");

	if (!tr)
		return -1;
	/*
	 * If context specific per-cpu buffers havent already been allocated.
	 */
	trace_printk_init_buffers();

	simple_tsk = kthread_run(simple_thread, NULL, "sample-instance");
	if (IS_ERR(simple_tsk))
		return -1;
	return 0;
}

static void __exit sample_trace_array_exit(void)
{
	kthread_stop(simple_tsk);

	/*
	 * We are unloading our module and no longer require the trace array.
	 * Remove/destroy "tr" using trace_array_destroy()
	 */
	trace_array_destroy(tr);
}

module_init(sample_trace_array_init);
module_exit(sample_trace_array_exit);

MODULE_AUTHOR("Divya Indi");
MODULE_DESCRIPTION("Sample module for kernel access to Ftrace instances");
MODULE_LICENSE("GPL");
