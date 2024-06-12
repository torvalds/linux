/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#ifndef _CGRP_KFUNC_COMMON_H
#define _CGRP_KFUNC_COMMON_H

#include <errno.h>
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct __cgrps_kfunc_map_value {
	struct cgroup __kptr * cgrp;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct __cgrps_kfunc_map_value);
	__uint(max_entries, 1);
} __cgrps_kfunc_map SEC(".maps");

struct cgroup *bpf_cgroup_acquire(struct cgroup *p) __ksym;
void bpf_cgroup_release(struct cgroup *p) __ksym;
struct cgroup *bpf_cgroup_ancestor(struct cgroup *cgrp, int level) __ksym;
struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

static inline struct __cgrps_kfunc_map_value *cgrps_kfunc_map_value_lookup(struct cgroup *cgrp)
{
	s32 id;
	long status;

	status = bpf_probe_read_kernel(&id, sizeof(id), &cgrp->self.id);
	if (status)
		return NULL;

	return bpf_map_lookup_elem(&__cgrps_kfunc_map, &id);
}

static inline int cgrps_kfunc_map_insert(struct cgroup *cgrp)
{
	struct __cgrps_kfunc_map_value local, *v;
	long status;
	struct cgroup *acquired, *old;
	s32 id;

	status = bpf_probe_read_kernel(&id, sizeof(id), &cgrp->self.id);
	if (status)
		return status;

	local.cgrp = NULL;
	status = bpf_map_update_elem(&__cgrps_kfunc_map, &id, &local, BPF_NOEXIST);
	if (status)
		return status;

	v = bpf_map_lookup_elem(&__cgrps_kfunc_map, &id);
	if (!v) {
		bpf_map_delete_elem(&__cgrps_kfunc_map, &id);
		return -ENOENT;
	}

	acquired = bpf_cgroup_acquire(cgrp);
	if (!acquired) {
		bpf_map_delete_elem(&__cgrps_kfunc_map, &id);
		return -ENOENT;
	}

	old = bpf_kptr_xchg(&v->cgrp, acquired);
	if (old) {
		bpf_cgroup_release(old);
		return -EEXIST;
	}

	return 0;
}

#endif /* _CGRP_KFUNC_COMMON_H */
