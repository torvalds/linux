// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

__u32 perfbuf_val = 0;
__u32 ringbuf_val = 0;

int test_pid;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} percpu_array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} percpu_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
} perfbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 12);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} prog_array SEC(".maps");

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int sys_nanosleep_enter(void *ctx)
{
	int cur_pid;

	cur_pid = bpf_get_current_pid_tgid() >> 32;

	if (cur_pid != test_pid)
		return 0;

	bpf_perf_event_output(ctx, &perfbuf, BPF_F_CURRENT_CPU, &perfbuf_val, sizeof(perfbuf_val));
	bpf_ringbuf_output(&ringbuf, &ringbuf_val, sizeof(ringbuf_val), 0);

	return 0;
}

SEC("perf_event")
int handle_perf_event(void *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
