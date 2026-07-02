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

static void do_simple_thread_func(int cnt, const char *fmt, ...)
{
	unsigned long bitmask[1] = {0xdeadbeefUL};
	va_list va;
	int array[6];
	int len = cnt % 5;
	int i;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ);

	for (i = 0; i < len; i++)
		array[i] = i + 1;
	array[i] = 0;

	va_start(va, fmt);

	/* Silly tracepoints */
	trace_foo_bar("hello", cnt, array, random_strings[len],
		      current->cpus_ptr, fmt, &va);

	va_end(va);

	trace_foo_with_template_simple("HELLO", cnt);

	trace_foo_bar_with_cond("Some times print", cnt);

	trace_foo_with_template_cond("prints other times", cnt);

	trace_foo_with_template_print("I have to be different", cnt);

	trace_foo_rel_loc("Hello __rel_loc", cnt, bitmask, current->cpus_ptr);
}

static void simple_thread_func(int cnt)
{
	do_simple_thread_func(cnt, "iter=%d", cnt);
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

static struct foo_timer_data *foo_timer_data;

static void sample_timer_cb(struct timer_list *t)
{
	struct foo_timer_data *data = container_of(t, struct foo_timer_data, timer);

	get_cpu();
	trace_foo_timer_fn(data);
	(*this_cpu_ptr(data->counter))++;
	put_cpu();

	mod_timer(t, jiffies + HZ);
}

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
	if (IS_ERR_OR_NULL(simple_tsk_fn)) {
		pr_err("Failed to create simple_thread_fn\n");
		simple_tsk_fn = NULL;
	}
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
	foo_timer_data = kzalloc_obj(*foo_timer_data, GFP_KERNEL);
	if (!foo_timer_data)
		return -ENOMEM;

	foo_timer_data->name = "sample_timer_counter";
	foo_timer_data->counter = alloc_percpu(int);
	if (!foo_timer_data->counter) {
		kfree(foo_timer_data);
		return -ENOMEM;
	}

	timer_setup(&foo_timer_data->timer, sample_timer_cb, 0);
	mod_timer(&foo_timer_data->timer, jiffies + HZ);

	simple_tsk = kthread_run(simple_thread, NULL, "event-sample");
	if (IS_ERR(simple_tsk)) {
		timer_shutdown_sync(&foo_timer_data->timer);
		free_percpu(foo_timer_data->counter);
		kfree(foo_timer_data);
		return PTR_ERR(simple_tsk);
	}

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

	timer_shutdown_sync(&foo_timer_data->timer);
	free_percpu(foo_timer_data->counter);
	kfree(foo_timer_data);
}

module_init(trace_event_init);
module_exit(trace_event_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("trace-events-sample");
MODULE_LICENSE("GPL");
