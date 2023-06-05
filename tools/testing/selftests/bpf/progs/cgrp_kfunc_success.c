// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "cgrp_kfunc_common.h"

char _license[] SEC("license") = "GPL";

int err, pid, invocations;

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(cgroup_mkdir,
 *         TP_PROTO(struct cgroup *cgrp, const char *path),
 *         TP_ARGS(cgrp, path)
 */

static bool is_test_kfunc_task(void)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;
	bool same = pid == cur_pid;

	if (same)
		__sync_fetch_and_add(&invocations, 1);

	return same;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_acquire_release_argument, struct cgroup *cgrp, const char *path)
{
	struct cgroup *acquired;

	if (!is_test_kfunc_task())
		return 0;

	acquired = bpf_cgroup_acquire(cgrp);
	if (!acquired)
		err = 1;
	else
		bpf_cgroup_release(acquired);

	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_acquire_leave_in_map, struct cgroup *cgrp, const char *path)
{
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = cgrps_kfunc_map_insert(cgrp);
	if (status)
		err = 1;

	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_xchg_release, struct cgroup *cgrp, const char *path)
{
	struct cgroup *kptr, *cg;
	struct __cgrps_kfunc_map_value *v;
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = cgrps_kfunc_map_insert(cgrp);
	if (status) {
		err = 1;
		return 0;
	}

	v = cgrps_kfunc_map_value_lookup(cgrp);
	if (!v) {
		err = 2;
		return 0;
	}

	kptr = v->cgrp;
	if (!kptr) {
		err = 4;
		return 0;
	}

	cg = bpf_cgroup_ancestor(kptr, 1);
	if (cg)	/* verifier only check */
		bpf_cgroup_release(cg);

	kptr = bpf_kptr_xchg(&v->cgrp, NULL);
	if (!kptr) {
		err = 3;
		return 0;
	}

	bpf_cgroup_release(kptr);

	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_get_release, struct cgroup *cgrp, const char *path)
{
	struct cgroup *kptr;
	struct __cgrps_kfunc_map_value *v;
	long status;

	if (!is_test_kfunc_task())
		return 0;

	status = cgrps_kfunc_map_insert(cgrp);
	if (status) {
		err = 1;
		return 0;
	}

	v = cgrps_kfunc_map_value_lookup(cgrp);
	if (!v) {
		err = 2;
		return 0;
	}

	bpf_rcu_read_lock();
	kptr = v->cgrp;
	if (!kptr)
		err = 3;
	bpf_rcu_read_unlock();

	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_get_ancestors, struct cgroup *cgrp, const char *path)
{
	struct cgroup *self, *ancestor1, *invalid;

	if (!is_test_kfunc_task())
		return 0;

	self = bpf_cgroup_ancestor(cgrp, cgrp->level);
	if (!self) {
		err = 1;
		return 0;
	}

	if (self->self.id != cgrp->self.id) {
		bpf_cgroup_release(self);
		err = 2;
		return 0;
	}
	bpf_cgroup_release(self);

	ancestor1 = bpf_cgroup_ancestor(cgrp, cgrp->level - 1);
	if (!ancestor1) {
		err = 3;
		return 0;
	}
	bpf_cgroup_release(ancestor1);

	invalid = bpf_cgroup_ancestor(cgrp, 10000);
	if (invalid) {
		bpf_cgroup_release(invalid);
		err = 4;
		return 0;
	}

	invalid = bpf_cgroup_ancestor(cgrp, -1);
	if (invalid) {
		bpf_cgroup_release(invalid);
		err = 5;
		return 0;
	}

	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_from_id, struct cgroup *cgrp, const char *path)
{
	struct cgroup *parent, *res;
	u64 parent_cgid;

	if (!is_test_kfunc_task())
		return 0;

	/* @cgrp's ID is not visible yet, let's test with the parent */
	parent = bpf_cgroup_ancestor(cgrp, cgrp->level - 1);
	if (!parent) {
		err = 1;
		return 0;
	}

	parent_cgid = parent->kn->id;
	bpf_cgroup_release(parent);

	res = bpf_cgroup_from_id(parent_cgid);
	if (!res) {
		err = 2;
		return 0;
	}

	bpf_cgroup_release(res);

	if (res != parent) {
		err = 3;
		return 0;
	}

	res = bpf_cgroup_from_id((u64)-1);
	if (res) {
		bpf_cgroup_release(res);
		err = 4;
		return 0;
	}

	return 0;
}
