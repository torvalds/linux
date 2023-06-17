/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CGROUP_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_cgroup_set_task,
	TP_PROTO(int ret, struct task_struct *task),
	TP_ARGS(ret, task));

DECLARE_RESTRICTED_HOOK(android_rvh_refrigerator,
	TP_PROTO(bool f),
	TP_ARGS(f), 1);

struct cgroup_subsys;
struct cgroup_taskset;
DECLARE_HOOK(android_vh_cgroup_attach,
	TP_PROTO(struct cgroup_subsys *ss, struct cgroup_taskset *tset),
	TP_ARGS(ss, tset))
DECLARE_RESTRICTED_HOOK(android_rvh_cgroup_force_kthread_migration,
	TP_PROTO(struct task_struct *tsk, struct cgroup *dst_cgrp, bool *force_migration),
	TP_ARGS(tsk, dst_cgrp, force_migration), 1);

struct cgroup_taskset;
struct cgroup_subsys;

DECLARE_RESTRICTED_HOOK(android_rvh_cpuset_fork,
	TP_PROTO(struct task_struct *p, bool *inherit_cpus),
	TP_ARGS(p, inherit_cpus), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_cpu_cgroup_attach,
	TP_PROTO(struct cgroup_taskset *tset),
	TP_ARGS(tset), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_cpu_cgroup_online,
	TP_PROTO(struct cgroup_subsys_state *css),
	TP_ARGS(css), 1);
#endif

#include <trace/define_trace.h>
