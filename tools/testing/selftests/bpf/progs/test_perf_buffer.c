// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include "bpf_helpers.h"

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} perf_buf_map SEC(".maps");

SEC("kprobe/sys_nanosleep")
int handle_sys_nanosleep_entry(struct pt_regs *ctx)
{
	int cpu = bpf_get_smp_processor_id();

	bpf_perf_event_output(ctx, &perf_buf_map, BPF_F_CURRENT_CPU,
			      &cpu, sizeof(cpu));
	return 0;
}

char _license[] SEC("license") = "GPL";
