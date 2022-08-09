// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/kthread.h>

/*
 * Any file that uses trace points, must include the header.
 * But only one file, must include the header by defining
 * CREATE_TRACE_POINTS first.  This will make the C code that
 * creates the handles for the trace points.
 */
#define CREATE_TRACE_POINTS
#include "trace-events-sample.h"

static const char *random_strings[] = {
	"Mother Goose",
	"Snoopy",
	"Gandalf",
	"Frodo",
	"One ring to rule them all"
};

static void simple_thread_func(int cnt)
{
	unsigned long bitmask[1] = {0xdeadbeefUL};
	int array[6];
	int len = cnt % 5;
	int i;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ);

	for (i = 0; i < len; i++)
		array[i] = i + 1;
	array[i] = 0;

	/* Silly tracepoints */
	trace_foo_bar("hello", cnt, array, random_strings[len],
		      current->cpus_ptr);

	trace_foo_with_template_simple("HELLO", cnt);

	trace_foo_bar_with_cond("Some times print", cnt);

	trace_foo_with_template_cond("prints other times", cnt);

	trace_foo_with_template_print("I have to be different", cnt);

	trace_foo_rel_loc("Hello __rel_loc", cnt, bitmask);
}

static int simple_thread(void *arg)
{
	int cnt = 0;

	while (!kthread_should_stop())
		simple_thread_func(cnt++);

	return 0;
}

static struct task_struct *simple_tsk;
static struct task_struct *simple_tsk_fn;

static void simple_thread_func_fn(int cnt)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ);

	/* More silly tracepoints */
	trace_foo_bar_with_fn("Look at me", cnt);
	trace_foo_with_template_fn("Look at me too", cnt);
}

static int simple_thread_fn(void *arg)
{
	int cnt = 0;

	while (!kthread_should_stop())
		simple_thread_func_fn(cnt++);

	return 0;
}

static DEFINE_MUTEX(thread_mutex);
static int simple_thread_cnt;

int foo_bar_reg(void)
{
	mutex_lock(&thread_mutex);
	if (simple_thread_cnt++)
		goto out;

	pr_info("Starting thread for foo_bar_fn\n");
	/*
	 * We shouldn't be able to start a trace when the module is
	 * unloading (there's other locks to prevent that). But
	 * for consistency sake, we still take the thread_mutex.
	 */
	simple_tsk_fn = kthread_run(simple_thread_fn, NULL, "event-sample-fn");
 out:
	mutex_unlock(&thread_mutex);
	return 0;
}

void foo_bar_unreg(void)
{
	mutex_lock(&thread_mutex);
	if (--simple_thread_cnt)
		goto out;

	pr_info("Killing thread for foo_bar_fn\n");
	if (simple_tsk_fn)
		kthread_stop(simple_tsk_fn);
	simple_tsk_fn = NULL;
 out:
	mutex_unlock(&thread_mutex);
}

static int __init trace_event_init(void)
{
	simple_tsk = kthread_run(simple_thread, NULL, "event-sample");
	if (IS_ERR(simple_tsk))
		return -1;

	return 0;
}

static void __exit trace_event_exit(void)
{
	kthread_stop(simple_tsk);
	mutex_lock(&thread_mutex);
	if (simple_tsk_fn)
		kthread_stop(simple_tsk_fn);
	simple_tsk_fn = NULL;
	mutex_unlock(&thread_mutex);
}

module_init(trace_event_init);
module_exit(trace_event_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("trace-events-sample");
MODULE_LICENSE("GPL");
