// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include "bpf_helpers.h"

struct {
	int type;
	int max_entries;
	int *key;
	int *value;
} results_map SEC(".maps") = {
	.type = BPF_MAP_TYPE_ARRAY,
	.max_entries = 4,
};

SEC("kprobe/sys_nanosleep")
int handle_sys_nanosleep_entry(struct pt_regs *ctx)
{
	const int key = 0, value = 1;

	bpf_map_update_elem(&results_map, &key, &value, 0);
	return 0;
}

SEC("kretprobe/sys_nanosleep")
int handle_sys_getpid_return(struct pt_regs *ctx)
{
	const int key = 1, value = 2;

	bpf_map_update_elem(&results_map, &key, &value, 0);
	return 0;
}

SEC("uprobe/trigger_func")
int handle_uprobe_entry(struct pt_regs *ctx)
{
	const int key = 2, value = 3;

	bpf_map_update_elem(&results_map, &key, &value, 0);
	return 0;
}

SEC("uretprobe/trigger_func")
int handle_uprobe_return(struct pt_regs *ctx)
{
	const int key = 3, value = 4;

	bpf_map_update_elem(&results_map, &key, &value, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1;
