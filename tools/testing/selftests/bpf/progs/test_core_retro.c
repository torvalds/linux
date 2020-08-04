// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct task_struct {
	int tgid;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} exp_tgid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} results SEC(".maps");

SEC("tp/raw_syscalls/sys_enter")
int handle_sys_enter(void *ctx)
{
	struct task_struct *task = (void *)bpf_get_current_task();
	int tgid = BPF_CORE_READ(task, tgid);
	int zero = 0;
	int real_tgid = bpf_get_current_pid_tgid() >> 32;
	int *exp_tgid = bpf_map_lookup_elem(&exp_tgid_map, &zero);

	/* only pass through sys_enters from test process */
	if (!exp_tgid || *exp_tgid != real_tgid)
		return 0;

	bpf_map_update_elem(&results, &zero, &tgid, 0);

	return 0;
}

char _license[] SEC("license") = "GPL";
