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
static int			sched_ref;
static DEFINE_MUTEX(sched_register_mutex);

static void
probe_sched_switch(struct rq *__rq, struct task_struct *prev,
			struct task_struct *next)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu;
	int pc;

	if (!sched_ref)
		return;

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);

	if (!tracer_enabled)
		return;

	pc = preempt_count();
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];

	if (likely(!atomic_read(&data->disabled)))
		tracing_sched_switch_trace(ctx_trace, data, prev, next, flags, pc);

	local_irq_restore(flags);
}

static void
probe_sched_wakeup(struct rq *__rq, struct task_struct *wakee, int success)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu, pc;

	if (!likely(tracer_enabled))
		return;

	pc = preempt_count();
	tracing_record_cmdline(current);

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];

	if (likely(!atomic_read(&data->disabled)))
		tracing_sched_wakeup_trace(ctx_trace, data, wakee, current,
					   flags, pc);

	local_irq_restore(flags);
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
	mutex_lock(&sched_register_mutex);
	if (!(sched_ref++))
		tracing_sched_register();
	mutex_unlock(&sched_register_mutex);
}

static void tracing_stop_sched_switch(void)
{
	mutex_lock(&sched_register_mutex);
	if (!(--sched_ref))
		tracing_sched_unregister();
	mutex_unlock(&sched_register_mutex);
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch();
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch();
}

/**
 * tracing_start_sched_switch_record - start tracing context switches
 *
 * Turns on context switch tracing for a tracer.
 */
void tracing_start_sched_switch_record(void)
{
	if (unlikely(!ctx_trace)) {
		WARN_ON(1);
		return;
	}

	tracing_start_sched_switch();

	mutex_lock(&sched_register_mutex);
	tracer_enabled++;
	mutex_unlock(&sched_register_mutex);
}

/**
 * tracing_stop_sched_switch_record - start tracing context switches
 *
 * Turns off context switch tracing for a tracer.
 */
void tracing_stop_sched_switch_record(void)
{
	mutex_lock(&sched_register_mutex);
	tracer_enabled--;
	WARN_ON(tracer_enabled < 0);
	mutex_unlock(&sched_register_mutex);

	tracing_stop_sched_switch();
}

/**
 * tracing_sched_switch_assign_trace - assign a trace array for ctx switch
 * @tr: trace array pointer to assign
 *
 * Some tracers might want to record the context switches in their
 * trace. This function lets those tracers assign the trace array
 * to use.
 */
void tracing_sched_switch_assign_trace(struct trace_array *tr)
{
	ctx_trace = tr;
}

static void start_sched_trace(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	tracing_start_sched_switch_record();
}

static void stop_sched_trace(struct trace_array *tr)
{
	tracing_stop_sched_switch_record();
}

static int sched_switch_trace_init(struct trace_array *tr)
{
	ctx_trace = tr;
	start_sched_trace(tr);
	return 0;
}

static void sched_switch_trace_reset(struct trace_array *tr)
{
	if (sched_ref)
		stop_sched_trace(tr);
}

static void sched_switch_trace_start(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	tracing_start_sched_switch();
}

static void sched_switch_trace_stop(struct trace_array *tr)
{
	tracing_stop_sched_switch();
}

static struct tracer sched_switch_trace __read_mostly =
{
	.name		= "sched_switch",
	.init		= sched_switch_trace_init,
	.reset		= sched_switch_trace_reset,
	.start		= sched_switch_trace_start,
	.stop		= sched_switch_trace_stop,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_sched_switch,
#endif
};

__init static int init_sched_switch_trace(void)
{
	return register_tracer(&sched_switch_trace);
}
device_initcall(init_sched_switch_trace);

