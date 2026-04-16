// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "stall"

#include <trace/events/sched.h>
#include <rv_trace.h>

#define RV_MON_TYPE RV_MON_PER_TASK
#define HA_TIMER_TYPE HA_TIMER_WHEEL
#include "stall.h"
#include <rv/ha_monitor.h>

static u64 threshold_jiffies = 1000;
module_param(threshold_jiffies, ullong, 0644);

static u64 ha_get_env(struct ha_monitor *ha_mon, enum envs_stall env, u64 time_ns)
{
	if (env == clk_stall)
		return ha_get_clk_jiffy(ha_mon, env);
	return ENV_INVALID_VALUE;
}

static void ha_reset_env(struct ha_monitor *ha_mon, enum envs_stall env, u64 time_ns)
{
	if (env == clk_stall)
		ha_reset_clk_jiffy(ha_mon, env);
}

static inline bool ha_verify_invariants(struct ha_monitor *ha_mon,
					enum states curr_state, enum events event,
					enum states next_state, u64 time_ns)
{
	if (curr_state == enqueued_stall)
		return ha_check_invariant_jiffy(ha_mon, clk_stall, time_ns);
	return true;
}

static inline bool ha_verify_guards(struct ha_monitor *ha_mon,
				    enum states curr_state, enum events event,
				    enum states next_state, u64 time_ns)
{
	bool res = true;

	if (curr_state == dequeued_stall && event == sched_wakeup_stall)
		ha_reset_env(ha_mon, clk_stall, time_ns);
	else if (curr_state == running_stall && event == sched_switch_preempt_stall)
		ha_reset_env(ha_mon, clk_stall, time_ns);
	return res;
}

static inline void ha_setup_invariants(struct ha_monitor *ha_mon,
				       enum states curr_state, enum events event,
				       enum states next_state, u64 time_ns)
{
	if (next_state == curr_state)
		return;
	if (next_state == enqueued_stall)
		ha_start_timer_jiffy(ha_mon, clk_stall, threshold_jiffies, time_ns);
	else if (curr_state == enqueued_stall)
		ha_cancel_timer(ha_mon);
}

static bool ha_verify_constraint(struct ha_monitor *ha_mon,
				 enum states curr_state, enum events event,
				 enum states next_state, u64 time_ns)
{
	if (!ha_verify_invariants(ha_mon, curr_state, event, next_state, time_ns))
		return false;

	if (!ha_verify_guards(ha_mon, curr_state, event, next_state, time_ns))
		return false;

	ha_setup_invariants(ha_mon, curr_state, event, next_state, time_ns);

	return true;
}

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	if (!preempt && prev_state != TASK_RUNNING)
		da_handle_start_event(prev, sched_switch_wait_stall);
	else
		da_handle_event(prev, sched_switch_preempt_stall);
	da_handle_event(next, sched_switch_in_stall);
}

static void handle_sched_wakeup(void *data, struct task_struct *p)
{
	da_handle_event(p, sched_wakeup_stall);
}

static int enable_stall(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("stall", sched_switch, handle_sched_switch);
	rv_attach_trace_probe("stall", sched_wakeup, handle_sched_wakeup);

	return 0;
}

static void disable_stall(void)
{
	rv_this.enabled = 0;

	rv_detach_trace_probe("stall", sched_switch, handle_sched_switch);
	rv_detach_trace_probe("stall", sched_wakeup, handle_sched_wakeup);

	da_monitor_destroy();
}

static struct rv_monitor rv_this = {
	.name = "stall",
	.description = "identify tasks stalled for longer than a threshold.",
	.enable = enable_stall,
	.disable = disable_stall,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_stall(void)
{
	return rv_register_monitor(&rv_this, NULL);
}

static void __exit unregister_stall(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_stall);
module_exit(unregister_stall);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("stall: identify tasks stalled for longer than a threshold.");
