// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
} my_pid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, int);
} perf_buf_map SEC(".maps");

SEC("tp/raw_syscalls/sys_enter")
int handle_sys_enter(void *ctx)
{
	int zero = 0, *my_pid, cur_pid;
	int cpu = bpf_get_smp_processor_id();

	my_pid = bpf_map_lookup_elem(&my_pid_map, &zero);
	if (!my_pid)
		return 1;

	cur_pid = bpf_get_current_pid_tgid() >> 32;
	if (cur_pid != *my_pid)
		return 1;

	bpf_perf_event_output(ctx, &perf_buf_map, BPF_F_CURRENT_CPU,
			      &cpu, sizeof(cpu));
	return 1;
}

char _license[] SEC("license") = "GPL";
