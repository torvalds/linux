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

/* v6.12: 4c30f5ce4f7a ("sched_ext: Implement scx_bpf_dispatch[_vtime]_from_dsq()") */
#define __COMPAT_scx_bpf_dispatch_from_dsq_set_slice(it, slice)			\
	(bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_slice) ?			\
	 scx_bpf_dispatch_from_dsq_set_slice((it), (slice)) : (void)0)
#define __COMPAT_scx_bpf_dispatch_from_dsq_set_vtime(it, vtime)			\
	(bpf_ksym_exists(scx_bpf_dispatch_from_dsq_set_vtime) ?			\
	 scx_bpf_dispatch_from_dsq_set_vtime((it), (vtime)) : (void)0)
#define __COMPAT_scx_bpf_dispatch_from_dsq(it, p, dsq_id, enq_flags)		\
	(bpf_ksym_exists(scx_bpf_dispatch_from_dsq) ?				\
	 scx_bpf_dispatch_from_dsq((it), (p), (dsq_id), (enq_flags)) : false)
#define __COMPAT_scx_bpf_dispatch_vtime_from_dsq(it, p, dsq_id, enq_flags)	\
	(bpf_ksym_exists(scx_bpf_dispatch_vtime_from_dsq) ?			\
	 scx_bpf_dispatch_vtime_from_dsq((it), (p), (dsq_id), (enq_flags)) : false)

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
