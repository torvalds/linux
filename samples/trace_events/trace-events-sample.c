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
		      tsk_cpus_allowed(current));

	trace_foo_bar_with_cond("Some times print", cnt);
}

static int simple_thread(void *arg)
{
	int cnt = 0;

	while (!kthread_should_stop())
		simple_thread_func(cnt++);

	return 0;
}

static struct task_struct *simple_tsk;

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
}

module_init(trace_event_init);
module_exit(trace_event_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("trace-events-sample");
MODULE_LICENSE("GPL");
