// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

char value[16];

static __always_inline void read_xattr(struct cgroup *cgroup)
{
	struct bpf_dynptr value_ptr;

	bpf_dynptr_from_mem(value, sizeof(value), 0, &value_ptr);
	bpf_cgroup_read_xattr(cgroup, "user.bpf_test",
			      &value_ptr);
}

SEC("lsm.s/socket_connect")
__success
int BPF_PROG(trusted_cgroup_ptr_sleepable)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	read_xattr(cgrp);
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("lsm/socket_connect")
__success
int BPF_PROG(trusted_cgroup_ptr_non_sleepable)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	read_xattr(cgrp);
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("lsm/socket_connect")
__success
int BPF_PROG(use_css_iter_non_sleepable)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	bpf_for_each(css, css, &cgrp->self, BPF_CGROUP_ITER_ANCESTORS_UP)
		read_xattr(css->cgroup);

	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("lsm.s/socket_connect")
__failure __msg("kernel func bpf_iter_css_new requires RCU critical section protection")
int BPF_PROG(use_css_iter_sleepable_missing_rcu_lock)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	bpf_for_each(css, css, &cgrp->self, BPF_CGROUP_ITER_ANCESTORS_UP)
		read_xattr(css->cgroup);

	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("lsm.s/socket_connect")
__success
int BPF_PROG(use_css_iter_sleepable_with_rcu_lock)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;

	bpf_rcu_read_lock();
	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		goto out;

	bpf_for_each(css, css, &cgrp->self, BPF_CGROUP_ITER_ANCESTORS_UP)
		read_xattr(css->cgroup);

	bpf_cgroup_release(cgrp);
out:
	bpf_rcu_read_unlock();
	return 0;
}

SEC("lsm/socket_connect")
__success
int BPF_PROG(use_bpf_cgroup_ancestor)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp, *ancestor;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	ancestor = bpf_cgroup_ancestor(cgrp, 1);
	if (!ancestor)
		goto out;

	read_xattr(cgrp);
	bpf_cgroup_release(ancestor);
out:
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("cgroup/sendmsg4")
__success
int BPF_PROG(cgroup_skb)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp, *ancestor;

	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp)
		return 0;

	ancestor = bpf_cgroup_ancestor(cgrp, 1);
	if (!ancestor)
		goto out;

	read_xattr(cgrp);
	bpf_cgroup_release(ancestor);
out:
	bpf_cgroup_release(cgrp);
	return 0;
}
