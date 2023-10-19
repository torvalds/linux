// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} cg_ids SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} pidmap SEC(".maps");

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
