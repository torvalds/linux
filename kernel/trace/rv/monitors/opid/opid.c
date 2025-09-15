// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "opid"

#include <trace/events/sched.h>
#include <trace/events/irq.h>
#include <trace/events/preemptirq.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "opid.h"

static struct rv_monitor rv_opid;
DECLARE_DA_MON_PER_CPU(opid, unsigned char);

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/trace/irq_vectors.h>

static void handle_vector_irq_entry(void *data, int vector)
{
	da_handle_event_opid(irq_entry_opid);
}

static void attach_vector_irq(void)
{
	rv_attach_trace_probe("opid", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_attach_trace_probe("opid", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_attach_trace_probe("opid", reschedule_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("opid", call_function_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("opid", call_function_single_entry, handle_vector_irq_entry);
	}
}

static void detach_vector_irq(void)
{
	rv_detach_trace_probe("opid", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_detach_trace_probe("opid", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_detach_trace_probe("opid", reschedule_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("opid", call_function_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("opid", call_function_single_entry, handle_vector_irq_entry);
	}
}

#else
/* We assume irq_entry tracepoints are sufficient on other architectures */
static void attach_vector_irq(void) { }
static void detach_vector_irq(void) { }
#endif

static void handle_irq_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_opid(irq_disable_opid);
}

static void handle_irq_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_opid(irq_enable_opid);
}

static void handle_irq_entry(void *data, int irq, struct irqaction *action)
{
	da_handle_event_opid(irq_entry_opid);
}

static void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_opid(preempt_disable_opid);
}

static void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_opid(preempt_enable_opid);
}

static void handle_sched_need_resched(void *data, struct task_struct *tsk, int cpu, int tif)
{
	/* The monitor's intitial state is not in_irq */
	if (this_cpu_read(hardirq_context))
		da_handle_event_opid(sched_need_resched_opid);
	else
		da_handle_start_event_opid(sched_need_resched_opid);
}

static void handle_sched_waking(void *data, struct task_struct *p)
{
	/* The monitor's intitial state is not in_irq */
	if (this_cpu_read(hardirq_context))
		da_handle_event_opid(sched_waking_opid);
	else
		da_handle_start_event_opid(sched_waking_opid);
}

static int enable_opid(void)
{
	int retval;

	retval = da_monitor_init_opid();
	if (retval)
		return retval;

	rv_attach_trace_probe("opid", irq_disable, handle_irq_disable);
	rv_attach_trace_probe("opid", irq_enable, handle_irq_enable);
	rv_attach_trace_probe("opid", irq_handler_entry, handle_irq_entry);
	rv_attach_trace_probe("opid", preempt_disable, handle_preempt_disable);
	rv_attach_trace_probe("opid", preempt_enable, handle_preempt_enable);
	rv_attach_trace_probe("opid", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_attach_trace_probe("opid", sched_waking, handle_sched_waking);
	attach_vector_irq();

	return 0;
}

static void disable_opid(void)
{
	rv_opid.enabled = 0;

	rv_detach_trace_probe("opid", irq_disable, handle_irq_disable);
	rv_detach_trace_probe("opid", irq_enable, handle_irq_enable);
	rv_detach_trace_probe("opid", irq_handler_entry, handle_irq_entry);
	rv_detach_trace_probe("opid", preempt_disable, handle_preempt_disable);
	rv_detach_trace_probe("opid", preempt_enable, handle_preempt_enable);
	rv_detach_trace_probe("opid", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_detach_trace_probe("opid", sched_waking, handle_sched_waking);
	detach_vector_irq();

	da_monitor_destroy_opid();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_opid = {
	.name = "opid",
	.description = "operations with preemption and irq disabled.",
	.enable = enable_opid,
	.disable = disable_opid,
	.reset = da_monitor_reset_all_opid,
	.enabled = 0,
};

static int __init register_opid(void)
{
	return rv_register_monitor(&rv_opid, &rv_sched);
}

static void __exit unregister_opid(void)
{
	rv_unregister_monitor(&rv_opid);
}

module_init(register_opid);
module_exit(unregister_opid);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("opid: operations with preemption and irq disabled.");
