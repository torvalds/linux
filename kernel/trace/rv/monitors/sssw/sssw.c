// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "sssw"

#include <trace/events/sched.h>
#include <trace/events/signal.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "sssw.h"

static struct rv_monitor rv_sssw;
DECLARE_DA_MON_PER_TASK(sssw, unsigned char);

static void handle_sched_set_state(void *data, struct task_struct *tsk, int state)
{
	if (state == TASK_RUNNING)
		da_handle_start_event_sssw(tsk, sched_set_state_runnable_sssw);
	else
		da_handle_event_sssw(tsk, sched_set_state_sleepable_sssw);
}

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	if (preempt)
		da_handle_event_sssw(prev, sched_switch_preempt_sssw);
	else if (prev_state == TASK_RUNNING)
		da_handle_event_sssw(prev, sched_switch_yield_sssw);
	else if (prev_state == TASK_RTLOCK_WAIT)
		/* special case of sleeping task with racy conditions */
		da_handle_event_sssw(prev, sched_switch_blocking_sssw);
	else
		da_handle_event_sssw(prev, sched_switch_suspend_sssw);
	da_handle_event_sssw(next, sched_switch_in_sssw);
}

static void handle_sched_wakeup(void *data, struct task_struct *p)
{
	/*
	 * Wakeup can also lead to signal_wakeup although the system is
	 * actually runnable. The monitor can safely start with this event.
	 */
	da_handle_start_event_sssw(p, sched_wakeup_sssw);
}

static void handle_signal_deliver(void *data, int sig,
				   struct kernel_siginfo *info,
				   struct k_sigaction *ka)
{
	da_handle_event_sssw(current, signal_deliver_sssw);
}

static int enable_sssw(void)
{
	int retval;

	retval = da_monitor_init_sssw();
	if (retval)
		return retval;

	rv_attach_trace_probe("sssw", sched_set_state_tp, handle_sched_set_state);
	rv_attach_trace_probe("sssw", sched_switch, handle_sched_switch);
	rv_attach_trace_probe("sssw", sched_wakeup, handle_sched_wakeup);
	rv_attach_trace_probe("sssw", signal_deliver, handle_signal_deliver);

	return 0;
}

static void disable_sssw(void)
{
	rv_sssw.enabled = 0;

	rv_detach_trace_probe("sssw", sched_set_state_tp, handle_sched_set_state);
	rv_detach_trace_probe("sssw", sched_switch, handle_sched_switch);
	rv_detach_trace_probe("sssw", sched_wakeup, handle_sched_wakeup);
	rv_detach_trace_probe("sssw", signal_deliver, handle_signal_deliver);

	da_monitor_destroy_sssw();
}

static struct rv_monitor rv_sssw = {
	.name = "sssw",
	.description = "set state sleep and wakeup.",
	.enable = enable_sssw,
	.disable = disable_sssw,
	.reset = da_monitor_reset_all_sssw,
	.enabled = 0,
};

static int __init register_sssw(void)
{
	return rv_register_monitor(&rv_sssw, &rv_sched);
}

static void __exit unregister_sssw(void)
{
	rv_unregister_monitor(&rv_sssw);
}

module_init(register_sssw);
module_exit(unregister_sssw);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("sssw: set state sleep and wakeup.");
