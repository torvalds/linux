/*
 * trace context switch
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <trace/sched.h>

#include "trace.h"

static struct trace_array	*ctx_trace;
static int __read_mostly	tracer_enabled;
static atomic_t			sched_ref;

static void
probe_sched_switch(struct rq *__rq, struct task_struct *prev,
			struct task_struct *next)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (!atomic_read(&sched_ref))
		return;

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);

	if (!tracer_enabled)
		return;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		tracing_sched_switch_trace(ctx_trace, data, prev, next, flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static void
probe_sched_wakeup(struct rq *__rq, struct task_struct *wakee)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (!likely(tracer_enabled))
		return;

	tracing_record_cmdline(current);

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		tracing_sched_wakeup_trace(ctx_trace, data, wakee, current,
			flags);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

static void sched_switch_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr->data[cpu]);
}

static int tracing_sched_register(void)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_sched_wakeup);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return ret;
	}

	ret = register_trace_sched_wakeup_new(probe_sched_wakeup);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_sched_switch);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_schedule\n");
		goto fail_deprobe_wake_new;
	}

	return ret;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_sched_wakeup);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_sched_wakeup);
	return ret;
}

static void tracing_sched_unregister(void)
{
	unregister_trace_sched_switch(probe_sched_switch);
	unregister_trace_sched_wakeup_new(probe_sched_wakeup);
	unregister_trace_sched_wakeup(probe_sched_wakeup);
}

static void tracing_start_sched_switch(void)
{
	long ref;

	ref = atomic_inc_return(&sched_ref);
	if (ref == 1)
		tracing_sched_register();
}

static void tracing_stop_sched_switch(void)
{
	long ref;

	ref = atomic_dec_and_test(&sched_ref);
	if (ref)
		tracing_sched_unregister();
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch();
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch();
}

static void start_sched_trace(struct trace_array *tr)
{
	sched_switch_reset(tr);
	tracing_start_cmdline_record();
	tracer_enabled = 1;
}

static void stop_sched_trace(struct trace_array *tr)
{
	tracer_enabled = 0;
	tracing_stop_cmdline_record();
}

static void sched_switch_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;

	if (tr->ctrl)
		start_sched_trace(tr);
}

static void sched_switch_trace_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_sched_trace(tr);
}

static void sched_switch_trace_ctrl_update(struct trace_array *tr)
{
	/* When starting a new trace, reset the buffers */
	if (tr->ctrl)
		start_sched_trace(tr);
	else
		stop_sched_trace(tr);
}

static struct tracer sched_switch_trace __read_mostly =
{
	.name		= "sched_switch",
	.init		= sched_switch_trace_init,
	.reset		= sched_switch_trace_reset,
	.ctrl_update	= sched_switch_trace_ctrl_update,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sched_switch,
#endif
};

__init static int init_sched_switch_trace(void)
{
	int ret = 0;

	if (atomic_read(&sched_ref))
		ret = tracing_sched_register();
	if (ret) {
		pr_info("error registering scheduler trace\n");
		return ret;
	}
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);
