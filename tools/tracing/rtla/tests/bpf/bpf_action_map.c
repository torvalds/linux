// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, unsigned int);
	__type(value, unsigned long long);
} rtla_test_map SEC(".maps");

struct trace_event_raw_timerlat_sample;

SEC("tp/timerlat_action")
int action_handler(struct trace_event_raw_timerlat_sample *tp_args)
{
	unsigned int key = 0;
	unsigned long long value = 42;

	bpf_map_update_elem(&rtla_test_map, &key, &value, BPF_ANY);

	return 0;
}
