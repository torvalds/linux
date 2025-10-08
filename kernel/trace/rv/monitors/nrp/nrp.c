// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "nrp"

#include <trace/events/irq.h>
#include <trace/events/sched.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "nrp.h"

static struct rv_monitor rv_nrp;
DECLARE_DA_MON_PER_TASK(nrp, unsigned char);

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/trace/irq_vectors.h>

static void handle_vector_irq_entry(void *data, int vector)
{
	da_handle_event_nrp(current, irq_entry_nrp);
}

static void attach_vector_irq(void)
{
	rv_attach_trace_probe("nrp", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_attach_trace_probe("nrp", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_attach_trace_probe("nrp", reschedule_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("nrp", call_function_entry, handle_vector_irq_entry);
		rv_attach_trace_probe("nrp", call_function_single_entry, handle_vector_irq_entry);
	}
}

static void detach_vector_irq(void)
{
	rv_detach_trace_probe("nrp", local_timer_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_IRQ_WORK))
		rv_detach_trace_probe("nrp", irq_work_entry, handle_vector_irq_entry);
	if (IS_ENABLED(CONFIG_SMP)) {
		rv_detach_trace_probe("nrp", reschedule_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("nrp", call_function_entry, handle_vector_irq_entry);
		rv_detach_trace_probe("nrp", call_function_single_entry, handle_vector_irq_entry);
	}
}

#else
/* We assume irq_entry tracepoints are sufficient on other architectures */
static void attach_vector_irq(void) { }
static void detach_vector_irq(void) { }
#endif

static void handle_irq_entry(void *data, int irq, struct irqaction *action)
{
	da_handle_event_nrp(current, irq_entry_nrp);
}

static void handle_sched_need_resched(void *data, struct task_struct *tsk,
				      int cpu, int tif)
{
	/*
	 * Although need_resched leads to both the rescheduling and preempt_irq
	 * states, it is safer to start the monitor always in preempt_irq,
	 * which may not mirror the system state but makes the monitor simpler,
	 */
	if (tif == TIF_NEED_RESCHED)
		da_handle_start_event_nrp(tsk, sched_need_resched_nrp);
}

static void handle_schedule_entry(void *data, bool preempt)
{
	if (preempt)
		da_handle_event_nrp(current, schedule_entry_preempt_nrp);
	else
		da_handle_event_nrp(current, schedule_entry_nrp);
}

static int enable_nrp(void)
{
	int retval;

	retval = da_monitor_init_nrp();
	if (retval)
		return retval;

	rv_attach_trace_probe("nrp", irq_handler_entry, handle_irq_entry);
	rv_attach_trace_probe("nrp", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_attach_trace_probe("nrp", sched_entry_tp, handle_schedule_entry);
	attach_vector_irq();

	return 0;
}

static void disable_nrp(void)
{
	rv_nrp.enabled = 0;

	rv_detach_trace_probe("nrp", irq_handler_entry, handle_irq_entry);
	rv_detach_trace_probe("nrp", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_detach_trace_probe("nrp", sched_entry_tp, handle_schedule_entry);
	detach_vector_irq();

	da_monitor_destroy_nrp();
}

static struct rv_monitor rv_nrp = {
	.name = "nrp",
	.description = "need resched preempts.",
	.enable = enable_nrp,
	.disable = disable_nrp,
	.reset = da_monitor_reset_all_nrp,
	.enabled = 0,
};

static int __init register_nrp(void)
{
	return rv_register_monitor(&rv_nrp, &rv_sched);
}

static void __exit unregister_nrp(void)
{
	rv_unregister_monitor(&rv_nrp);
}

module_init(register_nrp);
module_exit(unregister_nrp);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("nrp: need resched preempts.");
