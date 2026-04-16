// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "opid"

#include <trace/events/sched.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#define RV_MON_TYPE RV_MON_PER_CPU
#include "opid.h"
#include <rv/ha_monitor.h>

static u64 ha_get_env(struct ha_monitor *ha_mon, enum envs_opid env, u64 time_ns)
{
	if (env == irq_off_opid)
		return irqs_disabled();
	else if (env == preempt_off_opid) {
		/*
		 * If CONFIG_PREEMPTION is enabled, then the tracepoint itself disables
		 * preemption (adding one to the preempt_count). Since we are
		 * interested in the preempt_count at the time the tracepoint was
		 * hit, we consider 1 as still enabled.
		 */
		if (IS_ENABLED(CONFIG_PREEMPTION))
			return (preempt_count() & PREEMPT_MASK) > 1;
		return true;
	}
	return ENV_INVALID_VALUE;
}

static inline bool ha_verify_guards(struct ha_monitor *ha_mon,
				    enum states curr_state, enum events event,
				    enum states next_state, u64 time_ns)
{
	bool res = true;

	if (curr_state == any_opid && event == sched_need_resched_opid)
		res = ha_get_env(ha_mon, irq_off_opid, time_ns) == 1ull;
	else if (curr_state == any_opid && event == sched_waking_opid)
		res = ha_get_env(ha_mon, irq_off_opid, time_ns) == 1ull &&
		      ha_get_env(ha_mon, preempt_off_opid, time_ns) == 1ull;
	return res;
}

static bool ha_verify_constraint(struct ha_monitor *ha_mon,
				 enum states curr_state, enum events event,
				 enum states next_state, u64 time_ns)
{
	if (!ha_verify_guards(ha_mon, curr_state, event, next_state, time_ns))
		return false;

	return true;
}

static void handle_sched_need_resched(void *data, struct task_struct *tsk, int cpu, int tif)
{
	da_handle_start_run_event(sched_need_resched_opid);
}

static void handle_sched_waking(void *data, struct task_struct *p)
{
	da_handle_start_run_event(sched_waking_opid);
}

static int enable_opid(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("opid", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_attach_trace_probe("opid", sched_waking, handle_sched_waking);

	return 0;
}

static void disable_opid(void)
{
	rv_this.enabled = 0;

	rv_detach_trace_probe("opid", sched_set_need_resched_tp, handle_sched_need_resched);
	rv_detach_trace_probe("opid", sched_waking, handle_sched_waking);

	da_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_this = {
	.name = "opid",
	.description = "operations with preemption and irq disabled.",
	.enable = enable_opid,
	.disable = disable_opid,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_opid(void)
{
	return rv_register_monitor(&rv_this, &rv_sched);
}

static void __exit unregister_opid(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_opid);
module_exit(unregister_opid);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("opid: operations with preemption and irq disabled.");
