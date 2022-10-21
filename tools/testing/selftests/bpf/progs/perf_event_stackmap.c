// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#ifndef PERF_MAX_STACK_DEPTH
#define PERF_MAX_STACK_DEPTH         127
#endif

typedef __u64 stack_trace_t[PERF_MAX_STACK_DEPTH];
struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, 16384);
	__type(key, __u32);
	__type(value, stack_trace_t);
} stackmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, stack_trace_t);
} stackdata_map SEC(".maps");

long stackid_kernel = 1;
long stackid_user = 1;
long stack_kernel = 1;
long stack_user = 1;

SEC("perf_event")
int oncpu(void *ctx)
{
	stack_trace_t *trace;
	__u32 key = 0;
	long val;

	val = bpf_get_stackid(ctx, &stackmap, 0);
	if (val >= 0)
		stackid_kernel = 2;
	val = bpf_get_stackid(ctx, &stackmap, BPF_F_USER_STACK);
	if (val >= 0)
		stackid_user = 2;

	trace = bpf_map_lookup_elem(&stackdata_map, &key);
	if (!trace)
		return 0;

	val = bpf_get_stack(ctx, trace, sizeof(stack_trace_t), 0);
	if (val > 0)
		stack_kernel = 2;

	val = bpf_get_stack(ctx, trace, sizeof(stack_trace_t), BPF_F_USER_STACK);
	if (val > 0)
		stack_user = 2;

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
