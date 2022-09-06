// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "wwnr"

#include <trace/events/rv.h>
#include <trace/events/sched.h>

#include "wwnr.h"

struct rv_monitor rv_wwnr;
DECLARE_DA_MON_PER_TASK(wwnr, unsigned char);

static void handle_switch(void *data, bool preempt, struct task_struct *p,
			  struct task_struct *n, unsigned int prev_state)
{
	/* start monitoring only after the first suspension */
	if (prev_state == TASK_INTERRUPTIBLE)
		da_handle_start_event_wwnr(p, switch_out_wwnr);
	else
		da_handle_event_wwnr(p, switch_out_wwnr);

	da_handle_event_wwnr(n, switch_in_wwnr);
}

static void handle_wakeup(void *data, struct task_struct *p)
{
	da_handle_event_wwnr(p, wakeup_wwnr);
}

static int enable_wwnr(void)
{
	int retval;

	retval = da_monitor_init_wwnr();
	if (retval)
		return retval;

	rv_attach_trace_probe("wwnr", sched_switch, handle_switch);
	rv_attach_trace_probe("wwnr", sched_wakeup, handle_wakeup);

	return 0;
}

static void disable_wwnr(void)
{
	rv_wwnr.enabled = 0;

	rv_detach_trace_probe("wwnr", sched_switch, handle_switch);
	rv_detach_trace_probe("wwnr", sched_wakeup, handle_wakeup);

	da_monitor_destroy_wwnr();
}

struct rv_monitor rv_wwnr = {
	.name = "wwnr",
	.description = "wakeup while not running per-task testing model.",
	.enable = enable_wwnr,
	.disable = disable_wwnr,
	.reset = da_monitor_reset_all_wwnr,
	.enabled = 0,
};

static int register_wwnr(void)
{
	rv_register_monitor(&rv_wwnr);
	return 0;
}

static void unregister_wwnr(void)
{
	rv_unregister_monitor(&rv_wwnr);
}

module_init(register_wwnr);
module_exit(unregister_wwnr);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Bristot de Oliveira <bristot@kernel.org>");
MODULE_DESCRIPTION("wwnr: wakeup while not running monitor");
