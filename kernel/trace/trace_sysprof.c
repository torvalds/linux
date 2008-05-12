/*
 * trace stack traces
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2004, 2005, Soeren Sandmann
 */
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/fs.h>

#include "trace.h"

static struct trace_array	*sysprof_trace;
static int __read_mostly	tracer_enabled;

/*
 * 10 msecs for now:
 */
static const unsigned long sample_period = 1000000;
static const unsigned int sample_max_depth = 512;

/*
 * Per CPU hrtimers that do the profiling:
 */
static DEFINE_PER_CPU(struct hrtimer, stack_trace_hrtimer);

struct stack_frame {
	const void __user	*next_fp;
	unsigned long		return_address;
};

static int copy_stack_frame(const void __user *fp, struct stack_frame *frame)
{
	int ret;

	if (!access_ok(VERIFY_READ, fp, sizeof(*frame)))
		return 0;

	ret = 1;
	pagefault_disable();
	if (__copy_from_user_inatomic(frame, fp, sizeof(*frame)))
		ret = 0;
	pagefault_enable();

	return ret;
}

static void timer_notify(struct pt_regs *regs, int cpu)
{
	struct trace_array_cpu *data;
	struct stack_frame frame;
	struct trace_array *tr;
	const void __user *fp;
	int is_user;
	int i;

	if (!regs)
		return;

	tr = sysprof_trace;
	data = tr->data[cpu];
	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return;

	if (is_user && current->state != TASK_RUNNING)
		return;

	if (!is_user) {
		/* kernel */
		ftrace(tr, data, current->pid, 1, 0);
		return;

	}

	trace_special(tr, data, 0, current->pid, regs->ip);

	fp = (void __user *)regs->bp;

	for (i = 0; i < sample_max_depth; i++) {
		frame.next_fp = 0;
		frame.return_address = 0;
		if (!copy_stack_frame(fp, &frame))
			break;
		if ((unsigned long)fp < regs->sp)
			break;

		trace_special(tr, data, 1, frame.return_address,
			      (unsigned long)fp);
		fp = frame.next_fp;
	}

	trace_special(tr, data, 2, current->pid, i);

	/*
	 * Special trace entry if we overflow the max depth:
	 */
	if (i == sample_max_depth)
		trace_special(tr, data, -1, -1, -1);
}

static enum hrtimer_restart stack_trace_timer_fn(struct hrtimer *hrtimer)
{
	/* trace here */
	timer_notify(get_irq_regs(), smp_processor_id());

	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));

	return HRTIMER_RESTART;
}

static void start_stack_timer(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(stack_trace_hrtimer, cpu);

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = stack_trace_timer_fn;
	hrtimer->cb_mode = HRTIMER_CB_IRQSAFE_NO_SOFTIRQ;

	hrtimer_start(hrtimer, ns_to_ktime(sample_period), HRTIMER_MODE_REL);
}

static void start_stack_timers(void)
{
	cpumask_t saved_mask = current->cpus_allowed;
	int cpu;

	for_each_online_cpu(cpu) {
		set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));
		start_stack_timer(cpu);
	}
	set_cpus_allowed_ptr(current, &saved_mask);
}

static void stop_stack_timer(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(stack_trace_hrtimer, cpu);

	hrtimer_cancel(hrtimer);
}

static void stop_stack_timers(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		stop_stack_timer(cpu);
}

static notrace void stack_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static notrace void start_stack_trace(struct trace_array *tr)
{
	stack_reset(tr);
	start_stack_timers();
	tracer_enabled = 1;
}

static notrace void stop_stack_trace(struct trace_array *tr)
{
	stop_stack_timers();
	tracer_enabled = 0;
}

static notrace void stack_trace_init(struct trace_array *tr)
{
	sysprof_trace = tr;

	if (tr->ctrl)
		start_stack_trace(tr);
}

static notrace void stack_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_stack_trace(tr);
}

static void stack_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_stack_trace(tr);
	else
		stop_stack_trace(tr);
}

static struct tracer stack_trace __read_mostly =
{
	.name		= "sysprof",
	.init		= stack_trace_init,
	.reset		= stack_trace_reset,
	.ctrl_update	= stack_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sysprof,
#endif
};

__init static int init_stack_trace(void)
{
	return register_tracer(&stack_trace);
}
device_initcall(init_stack_trace);
