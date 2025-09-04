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

/* v6.12: 819513666966 ("sched_ext: Add cgroup support") */
#define __COMPAT_scx_bpf_task_cgroup(p)						\
	(bpf_ksym_exists(scx_bpf_task_cgroup) ?					\
	 scx_bpf_task_cgroup((p)) : NULL)

/*
 * v6.13: The verb `dispatch` was too overloaded and confusing. kfuncs are
 * renamed to unload the verb.
 *
 * Build error is triggered if old names are used. New binaries work with both
 * new and old names. The compat macros will be removed on v6.15 release.
 *
 * scx_bpf_dispatch_from_dsq() and friends were added during v6.12 by
 * 4c30f5ce4f7a ("sched_ext: Implement scx_bpf_dispatch[_vtime]_from_dsq()").
 * Preserve __COMPAT macros until v6.15.
 */
void scx_bpf_dispatch___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym __weak;
void scx_bpf_dispatch_vtime___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym __weak;
bool scx_bpf_consume___compat(u64 dsq_id) __ksym __weak;
void scx_bpf_dispatch_from_dsq_set_slice___compat(struct bpf_iter_scx_dsq *it__iter, u64 slice) __ksym __weak;
void scx_bpf_dispatch_from_dsq_set_vtime___compat(struct bpf_iter_scx_dsq *it__iter, u64 vtime) __ksym __weak;
bool scx_bpf_dispatch_from_dsq___compat(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
bool scx_bpf_dispatch_vtime_from_dsq___compat(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
int bpf_cpumask_populate(struct cpumask *dst, void *src, size_t src__sz) __ksym __weak;

#define scx_bpf_dsq_insert(p, dsq_id, slice, enq_flags)				\
	(bpf_ksym_exists(scx_bpf_dsq_insert) ?					\
	 scx_bpf_dsq_insert((p), (dsq_id), (slice), (enq_flags)) :		\
	 scx_bpf_dispatch___compat((p), (dsq_id), (slice), (enq_flags)))

#define scx_bpf_dsq_insert_vtime(p, dsq_id, slice, vtime, enq_flags)		\
	(bpf_ksym_exists(scx_bpf_dsq_insert_vtime) ?				\
	 scx_bpf_dsq_insert_vtime((p), (dsq_id), (slice), (vtime), (enq_flags)) : \
	 scx_bpf_dispatch_vtime___compat((p), (dsq_id), (slice), (vtime), (enq_flags)))

#define scx_bpf_dsq_move_to_local(dsq_id)					\
	(bpf_ksym_exists(scx_bpf_dsq_move_to_local) ?				\
	 scx_bpf_dsq_move_to_local((dsq_id)) :					\
	 scx_bpf_consume___compat((dsq_id)))

#define __COMPAT_scx_bpf_dsq_move_set_slice(it__iter, slice)			\
	(bpf_ksym_exists(scx_bpf_dsq_move_set_slice) ?				\
	 scx_bpf_dsq_move_set_slice((it__iter), (slice)) :			\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_slice___compat) ?	\
	  scx_bpf_dispatch_from_dsq_set_slice___compat((it__iter), (slice)) :	\
	  (void)0))

#define __COMPAT_scx_bpf_dsq_move_set_vtime(it__iter, vtime)			\
	(bpf_ksym_exists(scx_bpf_dsq_move_set_vtime) ?				\
	 scx_bpf_dsq_move_set_vtime((it__iter), (vtime)) :			\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_vtime___compat) ?	\
	  scx_bpf_dispatch_from_dsq_set_vtime___compat((it__iter), (vtime)) :	\
	  (void) 0))

#define __COMPAT_scx_bpf_dsq_move(it__iter, p, dsq_id, enq_flags)		\
	(bpf_ksym_exists(scx_bpf_dsq_move) ?					\
	 scx_bpf_dsq_move((it__iter), (p), (dsq_id), (enq_flags)) :		\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq___compat) ?			\
	  scx_bpf_dispatch_from_dsq___compat((it__iter), (p), (dsq_id), (enq_flags)) : \
	  false))

#define __COMPAT_scx_bpf_dsq_move_vtime(it__iter, p, dsq_id, enq_flags)		\
	(bpf_ksym_exists(scx_bpf_dsq_move_vtime) ?				\
	 scx_bpf_dsq_move_vtime((it__iter), (p), (dsq_id), (enq_flags)) :	\
	 (bpf_ksym_exists(scx_bpf_dispatch_vtime_from_dsq___compat) ?		\
	  scx_bpf_dispatch_vtime_from_dsq___compat((it__iter), (p), (dsq_id), (enq_flags)) : \
	  false))

#define __COMPAT_bpf_cpumask_populate(cpumask, src, size__sz)		\
	(bpf_ksym_exists(bpf_cpumask_populate) ?			\
	 (bpf_cpumask_populate(cpumask, src, size__sz)) : -EOPNOTSUPP)

#define scx_bpf_dispatch(p, dsq_id, slice, enq_flags)				\
	_Static_assert(false, "scx_bpf_dispatch() renamed to scx_bpf_dsq_insert()")

#define scx_bpf_dispatch_vtime(p, dsq_id, slice, vtime, enq_flags)		\
	_Static_assert(false, "scx_bpf_dispatch_vtime() renamed to scx_bpf_dsq_insert_vtime()")

#define scx_bpf_consume(dsq_id) ({						\
	_Static_assert(false, "scx_bpf_consume() renamed to scx_bpf_dsq_move_to_local()"); \
	false;									\
})

#define scx_bpf_dispatch_from_dsq_set_slice(it__iter, slice)		\
	_Static_assert(false, "scx_bpf_dispatch_from_dsq_set_slice() renamed to scx_bpf_dsq_move_set_slice()")

#define scx_bpf_dispatch_from_dsq_set_vtime(it__iter, vtime)		\
	_Static_assert(false, "scx_bpf_dispatch_from_dsq_set_vtime() renamed to scx_bpf_dsq_move_set_vtime()")

#define scx_bpf_dispatch_from_dsq(it__iter, p, dsq_id, enq_flags) ({	\
	_Static_assert(false, "scx_bpf_dispatch_from_dsq() renamed to scx_bpf_dsq_move()"); \
	false;									\
})

#define scx_bpf_dispatch_vtime_from_dsq(it__iter, p, dsq_id, enq_flags) ({  \
	_Static_assert(false, "scx_bpf_dispatch_vtime_from_dsq() renamed to scx_bpf_dsq_move_vtime()"); \
	false;									\
})

#define __COMPAT_scx_bpf_dispatch_from_dsq_set_slice(it__iter, slice)		\
	_Static_assert(false, "__COMPAT_scx_bpf_dispatch_from_dsq_set_slice() renamed to __COMPAT_scx_bpf_dsq_move_set_slice()")

#define __COMPAT_scx_bpf_dispatch_from_dsq_set_vtime(it__iter, vtime)		\
	_Static_assert(false, "__COMPAT_scx_bpf_dispatch_from_dsq_set_vtime() renamed to __COMPAT_scx_bpf_dsq_move_set_vtime()")

#define __COMPAT_scx_bpf_dispatch_from_dsq(it__iter, p, dsq_id, enq_flags) ({	\
	_Static_assert(false, "__COMPAT_scx_bpf_dispatch_from_dsq() renamed to __COMPAT_scx_bpf_dsq_move()"); \
	false;									\
})

#define __COMPAT_scx_bpf_dispatch_vtime_from_dsq(it__iter, p, dsq_id, enq_flags) ({  \
	_Static_assert(false, "__COMPAT_scx_bpf_dispatch_vtime_from_dsq() renamed to __COMPAT_scx_bpf_dsq_move_vtime()"); \
	false;									\
})

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
