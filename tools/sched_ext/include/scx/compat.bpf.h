/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#ifndef __SCX_COMPAT_BPF_H
#define __SCX_COMPAT_BPF_H

#define __COMPAT_ENUM_OR_ZERO(__type, __ent)					\
({										\
	__type __ret = 0;							\
	if (bpf_core_enum_value_exists(__type, __ent))				\
		__ret = __ent;							\
	__ret;									\
})

/*
 * v6.15: 950ad93df2fc ("bpf: add kfunc for populating cpumask bits")
 *
 * Compat macro will be dropped on v6.19 release.
 */
int bpf_cpumask_populate(struct cpumask *dst, void *src, size_t src__sz) __ksym __weak;

#define __COMPAT_bpf_cpumask_populate(cpumask, src, size__sz)		\
	(bpf_ksym_exists(bpf_cpumask_populate) ?			\
	 (bpf_cpumask_populate(cpumask, src, size__sz)) : -EOPNOTSUPP)

/**
 * __COMPAT_is_enq_cpu_selected - Test if SCX_ENQ_CPU_SELECTED is on
 * in a compatible way. We will preserve this __COMPAT helper until v6.16.
 *
 * @enq_flags: enqueue flags from ops.enqueue()
 *
 * Return: True if SCX_ENQ_CPU_SELECTED is turned on in @enq_flags
 */
static inline bool __COMPAT_is_enq_cpu_selected(u64 enq_flags)
{
#ifdef HAVE_SCX_ENQ_CPU_SELECTED
	/*
	 * This is the case that a BPF code compiled against vmlinux.h
	 * where the enum SCX_ENQ_CPU_SELECTED exists.
	 */

	/*
	 * We should temporarily suspend the macro expansion of
	 * 'SCX_ENQ_CPU_SELECTED'. This avoids 'SCX_ENQ_CPU_SELECTED' being
	 * rewritten to '__SCX_ENQ_CPU_SELECTED' when 'SCX_ENQ_CPU_SELECTED'
	 * is defined in 'scripts/gen_enums.py'.
	 */
#pragma push_macro("SCX_ENQ_CPU_SELECTED")
#undef SCX_ENQ_CPU_SELECTED
	u64 flag;

	/*
	 * When the kernel did not have SCX_ENQ_CPU_SELECTED,
	 * select_task_rq_scx() has never been skipped. Thus, this case
	 * should be considered that the CPU has already been selected.
	 */
	if (!bpf_core_enum_value_exists(enum scx_enq_flags,
					SCX_ENQ_CPU_SELECTED))
		return true;

	flag = bpf_core_enum_value(enum scx_enq_flags, SCX_ENQ_CPU_SELECTED);
	return enq_flags & flag;

	/*
	 * Once done, resume the macro expansion of 'SCX_ENQ_CPU_SELECTED'.
	 */
#pragma pop_macro("SCX_ENQ_CPU_SELECTED")
#else
	/*
	 * This is the case that a BPF code compiled against vmlinux.h
	 * where the enum SCX_ENQ_CPU_SELECTED does NOT exist.
	 */
	return true;
#endif /* HAVE_SCX_ENQ_CPU_SELECTED */
}


#define scx_bpf_now()								\
	(bpf_ksym_exists(scx_bpf_now) ?						\
	 scx_bpf_now() :							\
	 bpf_ktime_get_ns())

/*
 * v6.15: Introduce event counters.
 *
 * Preserve the following macro until v6.17.
 */
#define __COMPAT_scx_bpf_events(events, size)					\
	(bpf_ksym_exists(scx_bpf_events) ?					\
	 scx_bpf_events(events, size) : ({}))

/*
 * v6.15: Introduce NUMA-aware kfuncs to operate with per-node idle
 * cpumasks.
 *
 * Preserve the following __COMPAT_scx_*_node macros until v6.17.
 */
#define __COMPAT_scx_bpf_nr_node_ids()						\
	(bpf_ksym_exists(scx_bpf_nr_node_ids) ?					\
	 scx_bpf_nr_node_ids() : 1U)

#define __COMPAT_scx_bpf_cpu_node(cpu)						\
	(bpf_ksym_exists(scx_bpf_cpu_node) ?					\
	 scx_bpf_cpu_node(cpu) : 0)

#define __COMPAT_scx_bpf_get_idle_cpumask_node(node)				\
	(bpf_ksym_exists(scx_bpf_get_idle_cpumask_node) ?			\
	 scx_bpf_get_idle_cpumask_node(node) :					\
	 scx_bpf_get_idle_cpumask())						\

#define __COMPAT_scx_bpf_get_idle_smtmask_node(node)				\
	(bpf_ksym_exists(scx_bpf_get_idle_smtmask_node) ?			\
	 scx_bpf_get_idle_smtmask_node(node) :					\
	 scx_bpf_get_idle_smtmask())

#define __COMPAT_scx_bpf_pick_idle_cpu_node(cpus_allowed, node, flags)		\
	(bpf_ksym_exists(scx_bpf_pick_idle_cpu_node) ?				\
	 scx_bpf_pick_idle_cpu_node(cpus_allowed, node, flags) :		\
	 scx_bpf_pick_idle_cpu(cpus_allowed, flags))

#define __COMPAT_scx_bpf_pick_any_cpu_node(cpus_allowed, node, flags)		\
	(bpf_ksym_exists(scx_bpf_pick_any_cpu_node) ?				\
	 scx_bpf_pick_any_cpu_node(cpus_allowed, node, flags) :			\
	 scx_bpf_pick_any_cpu(cpus_allowed, flags))

/*
 * v6.18: Add a helper to retrieve the current task running on a CPU.
 *
 * Keep this helper available until v6.20 for compatibility.
 */
static inline struct task_struct *__COMPAT_scx_bpf_cpu_curr(int cpu)
{
	struct rq *rq;

	if (bpf_ksym_exists(scx_bpf_cpu_curr))
		return scx_bpf_cpu_curr(cpu);

	rq = scx_bpf_cpu_rq(cpu);

	return rq ? rq->curr : NULL;
}

/*
 * Define sched_ext_ops. This may be expanded to define multiple variants for
 * backward compatibility. See compat.h::SCX_OPS_LOAD/ATTACH().
 */
#define SCX_OPS_DEFINE(__name, ...)						\
	SEC(".struct_ops.link")							\
	struct sched_ext_ops __name = {						\
		__VA_ARGS__,							\
	};

#endif	/* __SCX_COMPAT_BPF_H */
