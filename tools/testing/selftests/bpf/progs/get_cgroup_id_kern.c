// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__u64 cg_id;
__u64 expected_pid;

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int trace(void *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid();

	if (expected_pid == pid)
		cg_id = bpf_get_current_cgroup_id();

	return 0;
}

char _license[] SEC("license") = "GPL";
