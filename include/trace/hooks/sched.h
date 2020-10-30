/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCHED_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
DECLARE_RESTRICTED_HOOK(android_rvh_select_task_rq_fair,
	TP_PROTO(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags, int *new_cpu),
	TP_ARGS(p, prev_cpu, sd_flag, wake_flags, new_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_select_task_rq_rt,
	TP_PROTO(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags, int *new_cpu),
	TP_ARGS(p, prev_cpu, sd_flag, wake_flags, new_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_select_fallback_rq,
	TP_PROTO(int cpu, struct task_struct *p, int *new_cpu),
	TP_ARGS(cpu, p, new_cpu), 1);

struct rq;
DECLARE_HOOK(android_vh_scheduler_tick,
	TP_PROTO(struct rq *rq),
	TP_ARGS(rq));

DECLARE_RESTRICTED_HOOK(android_rvh_enqueue_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_dequeue_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_can_migrate_task,
	TP_PROTO(struct task_struct *p, int dst_cpu, int *can_migrate),
	TP_ARGS(p, dst_cpu, can_migrate), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_find_lowest_rq,
	TP_PROTO(struct task_struct *p, struct cpumask *local_cpu_mask,
			int ret, int *lowest_cpu),
	TP_ARGS(p, local_cpu_mask, ret, lowest_cpu), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_prepare_prio_fork,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_finish_prio_fork,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_rtmutex_prepare_setprio,
	TP_PROTO(struct task_struct *p, struct task_struct *pi_task),
	TP_ARGS(p, pi_task), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_user_nice,
	TP_PROTO(struct task_struct *p, long *nice),
	TP_ARGS(p, nice), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_setscheduler,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p), 1);

struct sched_group;
DECLARE_RESTRICTED_HOOK(android_rvh_find_busiest_group,
	TP_PROTO(struct sched_group *busiest, struct rq *dst_rq, int *out_balance),
		TP_ARGS(busiest, dst_rq, out_balance), 1);

DECLARE_HOOK(android_vh_dump_throttled_rt_tasks,
	TP_PROTO(int cpu, u64 clock, ktime_t rt_period, u64 rt_runtime,
			s64 rt_period_timer_expires),
	TP_ARGS(cpu, clock, rt_period, rt_runtime, rt_period_timer_expires));

DECLARE_HOOK(android_vh_jiffies_update,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

struct rq_flags;
DECLARE_RESTRICTED_HOOK(android_rvh_sched_newidle_balance,
	TP_PROTO(struct rq *this_rq, struct rq_flags *rf,
		 int *pulled_task, int *done),
	TP_ARGS(this_rq, rf, pulled_task, done), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_sched_nohz_balancer_kick,
	TP_PROTO(struct rq *rq, unsigned int *flags, int *done),
	TP_ARGS(rq, flags, done), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_find_busiest_queue,
	TP_PROTO(int dst_cpu, struct sched_group *group,
		 struct cpumask *env_cpus, struct rq **busiest,
		 int *done),
	TP_ARGS(dst_cpu, group, env_cpus, busiest, done), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_migrate_queued_task,
	TP_PROTO(struct rq *rq, struct rq_flags *rf,
		 struct task_struct *p, int new_cpu,
		 int *detached),
	TP_ARGS(rq, rf, p, new_cpu, detached), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_find_energy_efficient_cpu,
	TP_PROTO(struct task_struct *p, int prev_cpu, int sync, int *new_cpu),
	TP_ARGS(p, prev_cpu, sync, new_cpu), 1);
struct sched_attr;
DECLARE_HOOK(android_vh_set_sugov_sched_attr,
	TP_PROTO(struct sched_attr *attr),
	TP_ARGS(attr));
DECLARE_RESTRICTED_HOOK(android_rvh_set_iowait,
	TP_PROTO(struct task_struct *p, int *should_iowait_boost),
	TP_ARGS(p, should_iowait_boost), 1);
struct sugov_policy;
DECLARE_RESTRICTED_HOOK(android_rvh_set_sugov_update,
	TP_PROTO(struct sugov_policy *sg_policy, unsigned int next_freq, bool *should_update),
	TP_ARGS(sg_policy, next_freq, should_update), 1);
#else
#define trace_android_rvh_select_task_rq_fair(p, prev_cpu, sd_flag, wake_flags, new_cpu)
#define trace_android_rvh_select_task_rq_rt(p, prev_cpu, sd_flag, wake_flags, new_cpu)
#define trace_android_rvh_select_fallback_rq(cpu, p, dest_cpu)
#define trace_android_vh_scheduler_tick(rq)
#define trace_android_rvh_enqueue_task(rq, p)
#define trace_android_rvh_dequeue_task(rq, p)
#define trace_android_rvh_can_migrate_task(p, dst_cpu, can_migrate)
#define trace_android_rvh_find_lowest_rq(p, local_cpu_mask, ret, lowest_cpu)
#define trace_android_rvh_prepare_prio_fork(p)
#define trace_android_rvh_finish_prio_fork(p)
#define trace_android_rvh_rtmutex_prepare_setprio(p, pi_task)
#define trace_android_rvh_set_user_nice(p, nice)
#define trace_android_rvh_setscheduler(p)
#define trace_android_rvh_find_busiest_group(busiest, dst_rq, out_balance)
#define trace_android_vh_dump_throttled_rt_tasks(cpu, clock, rt_period, rt_runtime, rt_period_timer_expires)
#define trace_android_vh_jiffies_update(unused)
#define trace_android_rvh_sched_newidle_balance(this_rq, rf, pulled_task, done)
#define trace_android_rvh_sched_nohz_balancer_kick(rq, flags, done)
#define trace_android_rvh_find_busiest_queue(dst_cpu, group, env_cpus, busiest, done)
#define trace_android_rvh_migrate_queued_task(rq, rf, p, new_cpu, detached)
#define trace_android_rvh_find_energy_efficient_cpu(p, prev_cpu, sync, new_cpu)
#define trace_android_vh_set_sugov_sched_attr(attr)
#define trace_android_rvh_set_iowait(p, should_iowait_boost)
#define trace_android_rvh_set_sugov_update(sg_policy, next_freq, should_update)
#endif
#endif /* _TRACE_HOOK_SCHED_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
