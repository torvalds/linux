// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "nomiss"

#include <uapi/linux/sched/types.h>
#include <trace/events/syscalls.h>
#include <trace/events/sched.h>
#include <trace/events/task.h>
#include <rv_trace.h>

#define RV_MON_TYPE RV_MON_PER_OBJ
#define HA_TIMER_TYPE HA_TIMER_WHEEL
/* The start condition is on sched_switch, it's dangerous to allocate there */
#define DA_SKIP_AUTO_ALLOC
typedef struct sched_dl_entity *monitor_target;
#include "nomiss.h"
#include <rv/ha_monitor.h>
#include <monitors/deadline/deadline.h>

/*
 * User configurable deadline threshold. If the total utilisation of deadline
 * tasks is larger than 1, they are only guaranteed bounded tardiness. See
 * Documentation/scheduler/sched-deadline.rst for more details.
 * The minimum tardiness without sched_feat(HRTICK_DL) is 1 tick to accommodate
 * for throttle enforced on the next tick.
 */
static u64 deadline_thresh = TICK_NSEC;
module_param(deadline_thresh, ullong, 0644);
#define DEADLINE_NS(ha_mon) (ha_get_target(ha_mon)->dl_deadline + deadline_thresh)

static u64 ha_get_env(struct ha_monitor *ha_mon, enum envs_nomiss env, u64 time_ns)
{
	if (env == clk_nomiss)
		return ha_get_clk_ns(ha_mon, env, time_ns);
	else if (env == is_constr_dl_nomiss)
		return !dl_is_implicit(ha_get_target(ha_mon));
	else if (env == is_defer_nomiss)
		return ha_get_target(ha_mon)->dl_defer;
	return ENV_INVALID_VALUE;
}

static void ha_reset_env(struct ha_monitor *ha_mon, enum envs_nomiss env, u64 time_ns)
{
	if (env == clk_nomiss)
		ha_reset_clk_ns(ha_mon, env, time_ns);
}

static inline bool ha_verify_invariants(struct ha_monitor *ha_mon,
					enum states curr_state, enum events event,
					enum states next_state, u64 time_ns)
{
	if (curr_state == ready_nomiss)
		return ha_check_invariant_ns(ha_mon, clk_nomiss, time_ns);
	else if (curr_state == running_nomiss)
		return ha_check_invariant_ns(ha_mon, clk_nomiss, time_ns);
	return true;
}

static inline void ha_convert_inv_guard(struct ha_monitor *ha_mon,
					enum states curr_state, enum events event,
					enum states next_state, u64 time_ns)
{
	if (curr_state == next_state)
		return;
	if (curr_state == ready_nomiss)
		ha_inv_to_guard(ha_mon, clk_nomiss, DEADLINE_NS(ha_mon), time_ns);
	else if (curr_state == running_nomiss)
		ha_inv_to_guard(ha_mon, clk_nomiss, DEADLINE_NS(ha_mon), time_ns);
}

static inline bool ha_verify_guards(struct ha_monitor *ha_mon,
				    enum states curr_state, enum events event,
				    enum states next_state, u64 time_ns)
{
	bool res = true;

	if (curr_state == ready_nomiss && event == dl_replenish_nomiss)
		ha_reset_env(ha_mon, clk_nomiss, time_ns);
	else if (curr_state == ready_nomiss && event == dl_throttle_nomiss)
		res = ha_get_env(ha_mon, is_defer_nomiss, time_ns) == 1ull;
	else if (curr_state == idle_nomiss && event == dl_replenish_nomiss)
		ha_reset_env(ha_mon, clk_nomiss, time_ns);
	else if (curr_state == running_nomiss && event == dl_replenish_nomiss)
		ha_reset_env(ha_mon, clk_nomiss, time_ns);
	else if (curr_state == sleeping_nomiss && event == dl_replenish_nomiss)
		ha_reset_env(ha_mon, clk_nomiss, time_ns);
	else if (curr_state == sleeping_nomiss && event == dl_throttle_nomiss)
		res = ha_get_env(ha_mon, is_constr_dl_nomiss, time_ns) == 1ull ||
		      ha_get_env(ha_mon, is_defer_nomiss, time_ns) == 1ull;
	else if (curr_state == throttled_nomiss && event == dl_replenish_nomiss)
		ha_reset_env(ha_mon, clk_nomiss, time_ns);
	return res;
}

static inline void ha_setup_invariants(struct ha_monitor *ha_mon,
				       enum states curr_state, enum events event,
				       enum states next_state, u64 time_ns)
{
	if (next_state == curr_state && event != dl_replenish_nomiss)
		return;
	if (next_state == ready_nomiss)
		ha_start_timer_ns(ha_mon, clk_nomiss, DEADLINE_NS(ha_mon), time_ns);
	else if (next_state == running_nomiss)
		ha_start_timer_ns(ha_mon, clk_nomiss, DEADLINE_NS(ha_mon), time_ns);
	else if (curr_state == ready_nomiss)
		ha_cancel_timer(ha_mon);
	else if (curr_state == running_nomiss)
		ha_cancel_timer(ha_mon);
}

static bool ha_verify_constraint(struct ha_monitor *ha_mon,
				 enum states curr_state, enum events event,
				 enum states next_state, u64 time_ns)
{
	if (!ha_verify_invariants(ha_mon, curr_state, event, next_state, time_ns))
		return false;

	ha_convert_inv_guard(ha_mon, curr_state, event, next_state, time_ns);

	if (!ha_verify_guards(ha_mon, curr_state, event, next_state, time_ns))
		return false;

	ha_setup_invariants(ha_mon, curr_state, event, next_state, time_ns);

	return true;
}

static void handle_dl_replenish(void *data, struct sched_dl_entity *dl_se,
				int cpu, u8 type)
{
	if (is_supported_type(type))
		da_handle_event(EXPAND_ID(dl_se, cpu, type), dl_replenish_nomiss);
}

static void handle_dl_throttle(void *data, struct sched_dl_entity *dl_se,
			       int cpu, u8 type)
{
	if (is_supported_type(type))
		da_handle_event(EXPAND_ID(dl_se, cpu, type), dl_throttle_nomiss);
}

static void handle_dl_server_stop(void *data, struct sched_dl_entity *dl_se,
				  int cpu, u8 type)
{
	/*
	 * This isn't the standard use of da_handle_start_run_event since this
	 * event cannot only occur from the initial state.
	 * It is fine to use here because it always brings to a known state and
	 * the fact we "pretend" the transition starts from the initial state
	 * has no side effect.
	 */
	if (is_supported_type(type))
		da_handle_start_run_event(EXPAND_ID(dl_se, cpu, type), dl_server_stop_nomiss);
}

static inline void handle_server_switch(struct task_struct *next, int cpu, u8 type)
{
	struct sched_dl_entity *dl_se = get_server(next, type);

	if (dl_se && is_idle_task(next))
		da_handle_event(EXPAND_ID(dl_se, cpu, type), dl_server_idle_nomiss);
}

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	int cpu = task_cpu(next);

	if (prev_state != TASK_RUNNING && !preempt && prev->policy == SCHED_DEADLINE)
		da_handle_event(EXPAND_ID_TASK(prev), sched_switch_suspend_nomiss);
	if (next->policy == SCHED_DEADLINE)
		da_handle_start_run_event(EXPAND_ID_TASK(next), sched_switch_in_nomiss);

	/*
	 * The server is available in next only if the next task is boosted,
	 * otherwise we need to retrieve it.
	 * Here the server continues in the state running/armed until actually
	 * stopped, this works since we continue expecting a throttle.
	 */
	if (next->dl_server)
		da_handle_start_event(EXPAND_ID(next->dl_server, cpu,
						get_server_type(next)),
				      sched_switch_in_nomiss);
	else {
		handle_server_switch(next, cpu, DL_SERVER_FAIR);
		if (IS_ENABLED(CONFIG_SCHED_CLASS_EXT))
			handle_server_switch(next, cpu, DL_SERVER_EXT);
	}
}

static void handle_sys_enter(void *data, struct pt_regs *regs, long id)
{
	struct task_struct *p;
	int new_policy = -1;
	pid_t pid = 0;

	new_policy = extract_params(regs, id, &pid);
	if (new_policy < 0)
		return;
	guard(rcu)();
	p = pid ? find_task_by_vpid(pid) : current;
	if (unlikely(!p) || new_policy == p->policy)
		return;

	if (p->policy == SCHED_DEADLINE)
		da_reset(EXPAND_ID_TASK(p));
	else if (new_policy == SCHED_DEADLINE)
		da_create_or_get(EXPAND_ID_TASK(p));
}

static void handle_sched_wakeup(void *data, struct task_struct *tsk)
{
	if (tsk->policy == SCHED_DEADLINE)
		da_handle_event(EXPAND_ID_TASK(tsk), sched_wakeup_nomiss);
}

static int enable_nomiss(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	retval = init_storage(false);
	if (retval)
		return retval;
	rv_attach_trace_probe("nomiss", sched_dl_replenish_tp, handle_dl_replenish);
	rv_attach_trace_probe("nomiss", sched_dl_throttle_tp, handle_dl_throttle);
	rv_attach_trace_probe("nomiss", sched_dl_server_stop_tp, handle_dl_server_stop);
	rv_attach_trace_probe("nomiss", sched_switch, handle_sched_switch);
	rv_attach_trace_probe("nomiss", sched_wakeup, handle_sched_wakeup);
	if (!should_skip_syscall_handle())
		rv_attach_trace_probe("nomiss", sys_enter, handle_sys_enter);
	rv_attach_trace_probe("nomiss", task_newtask, handle_newtask);
	rv_attach_trace_probe("nomiss", sched_process_exit, handle_exit);

	return 0;
}

static void disable_nomiss(void)
{
	rv_this.enabled = 0;

	/* Those are RCU writers, detach earlier hoping to close a bit faster */
	rv_detach_trace_probe("nomiss", task_newtask, handle_newtask);
	rv_detach_trace_probe("nomiss", sched_process_exit, handle_exit);
	if (!should_skip_syscall_handle())
		rv_detach_trace_probe("nomiss", sys_enter, handle_sys_enter);

	rv_detach_trace_probe("nomiss", sched_dl_replenish_tp, handle_dl_replenish);
	rv_detach_trace_probe("nomiss", sched_dl_throttle_tp, handle_dl_throttle);
	rv_detach_trace_probe("nomiss", sched_dl_server_stop_tp, handle_dl_server_stop);
	rv_detach_trace_probe("nomiss", sched_switch, handle_sched_switch);
	rv_detach_trace_probe("nomiss", sched_wakeup, handle_sched_wakeup);

	da_monitor_destroy();
}

static struct rv_monitor rv_this = {
	.name = "nomiss",
	.description = "dl entities run to completion before their deadline.",
	.enable = enable_nomiss,
	.disable = disable_nomiss,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_nomiss(void)
{
	return rv_register_monitor(&rv_this, &rv_deadline);
}

static void __exit unregister_nomiss(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_nomiss);
module_exit(unregister_nomiss);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("nomiss: dl entities run to completion before their deadline.");
