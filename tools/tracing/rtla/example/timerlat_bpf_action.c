// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct trace_event_raw_timerlat_sample {
	unsigned long long timer_latency;
} __attribute__((preserve_access_index));

SEC("tp/timerlat_action")
int action_handler(struct trace_event_raw_timerlat_sample *tp_args)
{
	bpf_printk("Latency: %lld\n", tp_args->timer_latency);
	return 0;
}
