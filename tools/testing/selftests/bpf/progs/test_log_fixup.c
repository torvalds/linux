// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct task_struct___bad {
	int pid;
	int fake_field;
	void *fake_field_subprog;
} __attribute__((preserve_access_index));

SEC("?raw_tp/sys_enter")
int bad_relo(const void *ctx)
{
	static struct task_struct___bad *t;

	return bpf_core_field_size(t->fake_field);
}

static __noinline int bad_subprog(void)
{
	static struct task_struct___bad *t;

	/* ugliness below is a field offset relocation */
	return (void *)&t->fake_field_subprog - (void *)t;
}

SEC("?raw_tp/sys_enter")
int bad_relo_subprog(const void *ctx)
{
	static struct task_struct___bad *t;

	return bad_subprog() + bpf_core_field_size(t->pid);
}

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} existing_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} missing_map SEC(".maps");

SEC("?raw_tp/sys_enter")
int use_missing_map(const void *ctx)
{
	int zero = 0, *value;

	value = bpf_map_lookup_elem(&existing_map, &zero);

	value = bpf_map_lookup_elem(&missing_map, &zero);

	return value != NULL;
}

char _license[] SEC("license") = "GPL";
