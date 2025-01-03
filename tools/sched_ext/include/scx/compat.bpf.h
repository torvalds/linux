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
