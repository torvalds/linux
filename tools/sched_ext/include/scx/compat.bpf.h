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
struct cgroup *scx_bpf_task_cgroup___new(struct task_struct *p) __ksym __weak;

#define scx_bpf_task_cgroup(p)							\
	(bpf_ksym_exists(scx_bpf_task_cgroup___new) ?				\
	 scx_bpf_task_cgroup___new((p)) : NULL)

/*
 * v6.13: The verb `dispatch` was too overloaded and confusing. kfuncs are
 * renamed to unload the verb.
 *
 * scx_bpf_dispatch_from_dsq() and friends were added during v6.12 by
 * 4c30f5ce4f7a ("sched_ext: Implement scx_bpf_dispatch[_vtime]_from_dsq()").
 */
bool scx_bpf_dsq_move_to_local___new(u64 dsq_id) __ksym __weak;
void scx_bpf_dsq_move_set_slice___new(struct bpf_iter_scx_dsq *it__iter, u64 slice) __ksym __weak;
void scx_bpf_dsq_move_set_vtime___new(struct bpf_iter_scx_dsq *it__iter, u64 vtime) __ksym __weak;
bool scx_bpf_dsq_move___new(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
bool scx_bpf_dsq_move_vtime___new(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;

bool scx_bpf_consume___old(u64 dsq_id) __ksym __weak;
void scx_bpf_dispatch_from_dsq_set_slice___old(struct bpf_iter_scx_dsq *it__iter, u64 slice) __ksym __weak;
void scx_bpf_dispatch_from_dsq_set_vtime___old(struct bpf_iter_scx_dsq *it__iter, u64 vtime) __ksym __weak;
bool scx_bpf_dispatch_from_dsq___old(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
bool scx_bpf_dispatch_vtime_from_dsq___old(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;

#define scx_bpf_dsq_move_to_local(dsq_id)					\
	(bpf_ksym_exists(scx_bpf_dsq_move_to_local___new) ?			\
	 scx_bpf_dsq_move_to_local___new((dsq_id)) :				\
	 scx_bpf_consume___old((dsq_id)))

#define scx_bpf_dsq_move_set_slice(it__iter, slice)				\
	(bpf_ksym_exists(scx_bpf_dsq_move_set_slice___new) ?			\
	 scx_bpf_dsq_move_set_slice___new((it__iter), (slice)) :		\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_slice___old) ?		\
	  scx_bpf_dispatch_from_dsq_set_slice___old((it__iter), (slice)) :	\
	  (void)0))

#define scx_bpf_dsq_move_set_vtime(it__iter, vtime)				\
	(bpf_ksym_exists(scx_bpf_dsq_move_set_vtime___new) ?			\
	 scx_bpf_dsq_move_set_vtime___new((it__iter), (vtime)) :		\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_vtime___old) ?		\
	  scx_bpf_dispatch_from_dsq_set_vtime___old((it__iter), (vtime)) :	\
	  (void)0))

#define scx_bpf_dsq_move(it__iter, p, dsq_id, enq_flags)			\
	(bpf_ksym_exists(scx_bpf_dsq_move___new) ?				\
	 scx_bpf_dsq_move___new((it__iter), (p), (dsq_id), (enq_flags)) :	\
	 (bpf_ksym_exists(scx_bpf_dispatch_from_dsq___old) ?			\
	  scx_bpf_dispatch_from_dsq___old((it__iter), (p), (dsq_id), (enq_flags)) : \
	  false))

#define scx_bpf_dsq_move_vtime(it__iter, p, dsq_id, enq_flags)			\
	(bpf_ksym_exists(scx_bpf_dsq_move_vtime___new) ?			\
	 scx_bpf_dsq_move_vtime___new((it__iter), (p), (dsq_id), (enq_flags)) : \
	 (bpf_ksym_exists(scx_bpf_dispatch_vtime_from_dsq___old) ?		\
	  scx_bpf_dispatch_vtime_from_dsq___old((it__iter), (p), (dsq_id), (enq_flags)) : \
	  false))

/*
 * v6.15: 950ad93df2fc ("bpf: add kfunc for populating cpumask bits")
 *
 * Compat macro will be dropped on v6.19 release.
 */
int bpf_cpumask_populate(struct cpumask *dst, void *src, size_t src__sz) __ksym __weak;

#define __COMPAT_bpf_cpumask_populate(cpumask, src, size__sz)		\
	(bpf_ksym_exists(bpf_cpumask_populate) ?			\
	 (bpf_cpumask_populate(cpumask, src, size__sz)) : -EOPNOTSUPP)

/*
 * v6.19: Introduce lockless peek API for user DSQs.
 *
 * Preserve the following macro until v6.21.
 */
static inline struct task_struct *__COMPAT_scx_bpf_dsq_peek(u64 dsq_id)
{
	struct task_struct *p = NULL;
	struct bpf_iter_scx_dsq it;

	if (bpf_ksym_exists(scx_bpf_dsq_peek))
		return scx_bpf_dsq_peek(dsq_id);
	if (!bpf_iter_scx_dsq_new(&it, dsq_id, 0))
		p = bpf_iter_scx_dsq_next(&it);
	bpf_iter_scx_dsq_destroy(&it);
	return p;
}

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
 * v6.19: To work around BPF maximum parameter limit, the following kfuncs are
 * replaced with variants that pack scalar arguments in a struct. Wrappers are
 * provided to maintain source compatibility.
 *
 * v6.13: scx_bpf_dsq_insert_vtime() renaming is also handled here. See the
 * block on dispatch renaming above for more details.
 *
 * The kernel will carry the compat variants until v6.23 to maintain binary
 * compatibility. After v6.23 release, remove the compat handling and move the
 * wrappers to common.bpf.h.
 */
s32 scx_bpf_select_cpu_and___compat(struct task_struct *p, s32 prev_cpu, u64 wake_flags,
				    const struct cpumask *cpus_allowed, u64 flags) __ksym __weak;
void scx_bpf_dispatch_vtime___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym __weak;
void scx_bpf_dsq_insert_vtime___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym __weak;

/**
 * scx_bpf_select_cpu_and - Pick an idle CPU usable by task @p
 * @p: task_struct to select a CPU for
 * @prev_cpu: CPU @p was on previously
 * @wake_flags: %SCX_WAKE_* flags
 * @cpus_allowed: cpumask of allowed CPUs
 * @flags: %SCX_PICK_IDLE* flags
 *
 * Inline wrapper that packs scalar arguments into a struct and calls
 * __scx_bpf_select_cpu_and(). See __scx_bpf_select_cpu_and() for details.
 */
static inline s32
scx_bpf_select_cpu_and(struct task_struct *p, s32 prev_cpu, u64 wake_flags,
		       const struct cpumask *cpus_allowed, u64 flags)
{
	if (bpf_core_type_exists(struct scx_bpf_select_cpu_and_args)) {
		struct scx_bpf_select_cpu_and_args args = {
			.prev_cpu = prev_cpu,
			.wake_flags = wake_flags,
			.flags = flags,
		};

		return __scx_bpf_select_cpu_and(p, cpus_allowed, &args);
	} else {
		return scx_bpf_select_cpu_and___compat(p, prev_cpu, wake_flags,
						       cpus_allowed, flags);
	}
}

/**
 * scx_bpf_dsq_insert_vtime - Insert a task into the vtime priority queue of a DSQ
 * @p: task_struct to insert
 * @dsq_id: DSQ to insert into
 * @slice: duration @p can run for in nsecs, 0 to keep the current value
 * @vtime: @p's ordering inside the vtime-sorted queue of the target DSQ
 * @enq_flags: SCX_ENQ_*
 *
 * Inline wrapper that packs scalar arguments into a struct and calls
 * __scx_bpf_dsq_insert_vtime(). See __scx_bpf_dsq_insert_vtime() for details.
 */
static inline bool
scx_bpf_dsq_insert_vtime(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime,
			 u64 enq_flags)
{
	if (bpf_core_type_exists(struct scx_bpf_dsq_insert_vtime_args)) {
		struct scx_bpf_dsq_insert_vtime_args args = {
			.dsq_id = dsq_id,
			.slice = slice,
			.vtime = vtime,
			.enq_flags = enq_flags,
		};

		return __scx_bpf_dsq_insert_vtime(p, &args);
	} else if (bpf_ksym_exists(scx_bpf_dsq_insert_vtime___compat)) {
		scx_bpf_dsq_insert_vtime___compat(p, dsq_id, slice, vtime,
						  enq_flags);
		return true;
	} else {
		scx_bpf_dispatch_vtime___compat(p, dsq_id, slice, vtime,
						enq_flags);
		return true;
	}
}

/*
 * v6.19: scx_bpf_dsq_insert() now returns bool instead of void. Move
 * scx_bpf_dsq_insert() decl to common.bpf.h and drop compat helper after v6.22.
 * The extra ___compat suffix is to work around libbpf not ignoring __SUFFIX on
 * kernel side. The entire suffix can be dropped later.
 *
 * v6.13: scx_bpf_dsq_insert() renaming is also handled here. See the block on
 * dispatch renaming above for more details.
 */
bool scx_bpf_dsq_insert___v2___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym __weak;
void scx_bpf_dsq_insert___v1(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym __weak;
void scx_bpf_dispatch___compat(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym __weak;

static inline bool
scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags)
{
	if (bpf_ksym_exists(scx_bpf_dsq_insert___v2___compat)) {
		return scx_bpf_dsq_insert___v2___compat(p, dsq_id, slice, enq_flags);
	} else if (bpf_ksym_exists(scx_bpf_dsq_insert___v1)) {
		scx_bpf_dsq_insert___v1(p, dsq_id, slice, enq_flags);
		return true;
	} else {
		scx_bpf_dispatch___compat(p, dsq_id, slice, enq_flags);
		return true;
	}
}

/*
 * v6.19: scx_bpf_task_set_slice() and scx_bpf_task_set_dsq_vtime() added to for
 * sub-sched authority checks. Drop the wrappers and move the decls to
 * common.bpf.h after v6.22.
 */
bool scx_bpf_task_set_slice___new(struct task_struct *p, u64 slice) __ksym __weak;
bool scx_bpf_task_set_dsq_vtime___new(struct task_struct *p, u64 vtime) __ksym __weak;

static inline void scx_bpf_task_set_slice(struct task_struct *p, u64 slice)
{
	if (bpf_ksym_exists(scx_bpf_task_set_slice___new))
		scx_bpf_task_set_slice___new(p, slice);
	else
		p->scx.slice = slice;
}

static inline void scx_bpf_task_set_dsq_vtime(struct task_struct *p, u64 vtime)
{
	if (bpf_ksym_exists(scx_bpf_task_set_dsq_vtime___new))
		scx_bpf_task_set_dsq_vtime___new(p, vtime);
	else
		p->scx.dsq_vtime = vtime;
}

/*
 * v6.19: The new void variant can be called from anywhere while the older v1
 * variant can only be called from ops.cpu_release(). The double ___ prefixes on
 * the v2 variant need to be removed once libbpf is updated to ignore ___ prefix
 * on kernel side. Drop the wrapper and move the decl to common.bpf.h after
 * v6.22.
 */
u32 scx_bpf_reenqueue_local___v1(void) __ksym __weak;
void scx_bpf_reenqueue_local___v2___compat(void) __ksym __weak;

static inline bool __COMPAT_scx_bpf_reenqueue_local_from_anywhere(void)
{
	return bpf_ksym_exists(scx_bpf_reenqueue_local___v2___compat);
}

static inline void scx_bpf_reenqueue_local(void)
{
	if (__COMPAT_scx_bpf_reenqueue_local_from_anywhere())
		scx_bpf_reenqueue_local___v2___compat();
	else
		scx_bpf_reenqueue_local___v1();
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
