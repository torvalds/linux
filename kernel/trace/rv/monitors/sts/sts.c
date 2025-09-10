// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "sts"

#include <trace/events/sched.h>
#include <trace/events/irq.h>
#include <trace/events/preemptirq.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "sts.h"

static struct rv_monitor rv_sts;
DECLARE_DA_MON_PER_CPU(sts, unsigned char);

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/trace/irq_vectors.h>

static void handle_vector_irq_entry(void *data, int vector)
{
	da_handle_event_sts(irq_entry_sts);
}

static void attach_vector_irq(void)
{
	rv_attach_trace_probe("sts", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_attach_trace_probe("sts", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_attach_trace_probe("sts", reschedule_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("sts", call_function_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("sts", call_function_single_entry, handle_vector_irq_entry);
	}
}

static void detach_vector_irq(void)
{
	rv_detach_trace_probe("sts", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_detach_trace_probe("sts", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_detach_trace_probe("sts", reschedule_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("sts", call_function_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("sts", call_function_single_entry, handle_vector_irq_entry);
	}
}

#else
/* We assume irq_entry tracepoints are sufficient on other architectures */
static void attach_vector_irq(void) { }
static void detach_vector_irq(void) { }
#endif

static void handle_irq_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_sts(irq_disable_sts);
}

static void handle_irq_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_sts(irq_enable_sts);
}

static void handle_irq_entry(void *data, int irq, struct irqaction *action)
{
	da_handle_event_sts(irq_entry_sts);
}

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	da_handle_event_sts(sched_switch_sts);
}

static void handle_schedule_entry(void *data, bool preempt)
{
	da_handle_event_sts(schedule_entry_sts);
}

static void handle_schedule_exit(void *data, bool is_switch)
{
	da_handle_start_event_sts(schedule_exit_sts);
}

static int enable_sts(void)
{
	int retval;

	retval = da_monitor_init_sts();
	if (retval)
		return retval;

	rv_attach_trace_probe("sts", irq_disable, handle_irq_disable);
	rv_attach_trace_probe("sts", irq_enable, handle_irq_enable);
	rv_attach_trace_probe("sts", irq_handler_entry, handle_irq_entry);
	rv_attach_trace_probe("sts", sched_switch, handle_sched_switch);
	rv_attach_trace_probe("sts", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("sts", sched_exit_tp, handle_schedule_exit);
	attach_vector_irq();

	return 0;
}

static void disable_sts(void)
{
	rv_sts.enabled = 0;

	rv_detach_trace_probe("sts", irq_disable, handle_irq_disable);
	rv_detach_trace_probe("sts", irq_enable, handle_irq_enable);
	rv_detach_trace_probe("sts", irq_handler_entry, handle_irq_entry);
	rv_detach_trace_probe("sts", sched_switch, handle_sched_switch);
	rv_detach_trace_probe("sts", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("sts", sched_exit_tp, handle_schedule_exit);
	detach_vector_irq();

	da_monitor_destroy_sts();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_sts = {
	.name = "sts",
	.description = "schedule implies task switch.",
	.enable = enable_sts,
	.disable = disable_sts,
	.reset = da_monitor_reset_all_sts,
	.enabled = 0,
};

static int __init register_sts(void)
{
	return rv_register_monitor(&rv_sts, &rv_sched);
}

static void __exit unregister_sts(void)
{
	rv_unregister_monitor(&rv_sts);
}

module_init(register_sts);
module_exit(unregister_sts);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("sts: schedule implies task switch.");
