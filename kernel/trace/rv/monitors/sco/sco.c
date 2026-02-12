// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "sco"

#include <trace/events/sched.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#define RV_MON_TYPE RV_MON_PER_CPU
#include "sco.h"
#include <rv/da_monitor.h>

static void handle_sched_set_state(void *data, struct task_struct *tsk, int state)
{
	da_handle_start_event(sched_set_state_sco);
}

static void handle_schedule_entry(void *data, bool preempt)
{
	da_handle_event(schedule_entry_sco);
}

static void handle_schedule_exit(void *data, bool is_switch)
{
	da_handle_start_event(schedule_exit_sco);
}

static int enable_sco(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("sco", sched_set_state_tp, handle_sched_set_state);
	rv_attach_trace_probe("sco", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("sco", sched_exit_tp, handle_schedule_exit);

	return 0;
}

static void disable_sco(void)
{
	rv_this.enabled = 0;

	rv_detach_trace_probe("sco", sched_set_state_tp, handle_sched_set_state);
	rv_detach_trace_probe("sco", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("sco", sched_exit_tp, handle_schedule_exit);

	da_monitor_destroy();
}

static struct rv_monitor rv_this = {
	.name = "sco",
	.description = "scheduling context operations.",
	.enable = enable_sco,
	.disable = disable_sco,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_sco(void)
{
	return rv_register_monitor(&rv_this, &rv_sched);
}

static void __exit unregister_sco(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_sco);
module_exit(unregister_sco);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("sco: scheduling context operations.");
