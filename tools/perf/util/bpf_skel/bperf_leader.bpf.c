// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(int));
	__uint(map_flags, BPF_F_PRESERVE_ELEMS);
} events SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
	__uint(max_entries, 1);
} prev_readings SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
	__uint(max_entries, 1);
} diff_readings SEC(".maps");

SEC("raw_tp/sched_switch")
int BPF_PROG(on_switch)
{
	struct bpf_perf_event_value val, *prev_val, *diff_val;
	__u32 key = bpf_get_smp_processor_id();
	__u32 zero = 0;
	long err;

	prev_val = bpf_map_lookup_elem(&prev_readings, &zero);
	if (!prev_val)
		return 0;

	diff_val = bpf_map_lookup_elem(&diff_readings, &zero);
	if (!diff_val)
		return 0;

	err = bpf_perf_event_read_value(&events, key, &val, sizeof(val));
	if (err)
		return 0;

	diff_val->counter = val.counter - prev_val->counter;
	diff_val->enabled = val.enabled - prev_val->enabled;
	diff_val->running = val.running - prev_val->running;
	*prev_val = val;
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
