// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") cg_ids = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u64),
	.max_entries = 1,
};

struct bpf_map_def SEC("maps") pidmap = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(__u32),
	.max_entries = 1,
};

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int trace(void *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid();
	__u32 key = 0, *expected_pid;
	__u64 *val;

	expected_pid = bpf_map_lookup_elem(&pidmap, &key);
	if (!expected_pid || *expected_pid != pid)
		return 0;

	val = bpf_map_lookup_elem(&cg_ids, &key);
	if (val)
		*val = bpf_get_current_cgroup_id();

	return 0;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1; /* ignored by tracepoints, required by libbpf.a */
