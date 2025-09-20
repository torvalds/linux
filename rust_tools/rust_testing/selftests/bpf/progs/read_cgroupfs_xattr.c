// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

pid_t target_pid = 0;

char xattr_value[64];
static const char expected_value_a[] = "bpf_selftest_value_a";
static const char expected_value_b[] = "bpf_selftest_value_b";
bool found_value_a;
bool found_value_b;

SEC("lsm.s/file_open")
int BPF_PROG(test_file_open)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup_subsys_state *css, *tmp;
	struct bpf_dynptr value_ptr;
	struct cgroup *cgrp;

	if ((bpf_get_current_pid_tgid() >> 32) != target_pid)
		return 0;

	bpf_rcu_read_lock();
	cgrp = bpf_cgroup_from_id(cgrp_id);
	if (!cgrp) {
		bpf_rcu_read_unlock();
		return 0;
	}

	css = &cgrp->self;
	bpf_dynptr_from_mem(xattr_value, sizeof(xattr_value), 0, &value_ptr);
	bpf_for_each(css, tmp, css, BPF_CGROUP_ITER_ANCESTORS_UP) {
		int ret;

		ret = bpf_cgroup_read_xattr(tmp->cgroup, "user.bpf_test",
					    &value_ptr);
		if (ret < 0)
			continue;

		if (ret == sizeof(expected_value_a) &&
		    !bpf_strncmp(xattr_value, sizeof(expected_value_a), expected_value_a))
			found_value_a = true;
		if (ret == sizeof(expected_value_b) &&
		    !bpf_strncmp(xattr_value, sizeof(expected_value_b), expected_value_b))
			found_value_b = true;
	}

	bpf_rcu_read_unlock();
	bpf_cgroup_release(cgrp);

	return 0;
}
