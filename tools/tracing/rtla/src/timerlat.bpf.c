// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "timerlat_bpf.h"

#define nosubprog __always_inline
#define MAX_ENTRIES_DEFAULT 4096

char LICENSE[] SEC("license") = "GPL";

struct trace_event_raw_timerlat_sample {
	unsigned long long timer_latency;
	int context;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, MAX_ENTRIES_DEFAULT);
	__type(key, unsigned int);
	__type(value, unsigned long long);
} hist_irq SEC(".maps"), hist_thread SEC(".maps"), hist_user SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, SUMMARY_FIELD_N);
	__type(key, unsigned int);
	__type(value, unsigned long long);
} summary_irq SEC(".maps"), summary_thread SEC(".maps"), summary_user SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, unsigned int);
	__type(value, unsigned long long);
} stop_tracing SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1);
} signal_stop_tracing SEC(".maps");

/* Params to be set by rtla */
const volatile int bucket_size = 1;
const volatile int output_divisor = 1000;
const volatile int entries = 256;
const volatile int irq_threshold;
const volatile int thread_threshold;
const volatile bool aa_only;

nosubprog unsigned long long map_get(void *map,
				     unsigned int key)
{
	unsigned long long *value_ptr;

	value_ptr = bpf_map_lookup_elem(map, &key);

	return !value_ptr ? 0 : *value_ptr;
}

nosubprog void map_set(void *map,
		       unsigned int key,
		       unsigned long long value)
{
	bpf_map_update_elem(map, &key, &value, BPF_ANY);
}

nosubprog void map_increment(void *map,
			     unsigned int key)
{
	map_set(map, key, map_get(map, key) + 1);
}

nosubprog void update_main_hist(void *map,
				int bucket)
{
	if (entries == 0)
		/* No histogram */
		return;

	if (bucket >= entries)
		/* Overflow */
		return;

	map_increment(map, bucket);
}

nosubprog void update_summary(void *map,
			      unsigned long long latency,
			      int bucket)
{
	if (aa_only)
		/* Auto-analysis only, nothing to be done here */
		return;

	map_set(map, SUMMARY_CURRENT, latency);

	if (bucket >= entries)
		/* Overflow */
		map_increment(map, SUMMARY_OVERFLOW);

	if (latency > map_get(map, SUMMARY_MAX))
		map_set(map, SUMMARY_MAX, latency);

	if (latency < map_get(map, SUMMARY_MIN) || map_get(map, SUMMARY_COUNT) == 0)
		map_set(map, SUMMARY_MIN, latency);

	map_increment(map, SUMMARY_COUNT);
	map_set(map, SUMMARY_SUM, map_get(map, SUMMARY_SUM) + latency);
}

nosubprog void set_stop_tracing(void)
{
	int value = 0;

	/* Suppress further sample processing */
	map_set(&stop_tracing, 0, 1);

	/* Signal to userspace */
	bpf_ringbuf_output(&signal_stop_tracing, &value, sizeof(value), 0);
}

SEC("tp/osnoise/timerlat_sample")
int handle_timerlat_sample(struct trace_event_raw_timerlat_sample *tp_args)
{
	unsigned long long latency, latency_us;
	int bucket;

	if (map_get(&stop_tracing, 0))
		return 0;

	latency = tp_args->timer_latency / output_divisor;
	latency_us = tp_args->timer_latency / 1000;
	bucket = latency / bucket_size;

	if (tp_args->context == 0) {
		update_main_hist(&hist_irq, bucket);
		update_summary(&summary_irq, latency, bucket);

		if (irq_threshold != 0 && latency_us >= irq_threshold)
			set_stop_tracing();
	} else if (tp_args->context == 1) {
		update_main_hist(&hist_thread, bucket);
		update_summary(&summary_thread, latency, bucket);

		if (thread_threshold != 0 && latency_us >= thread_threshold)
			set_stop_tracing();
	} else {
		update_main_hist(&hist_user, bucket);
		update_summary(&summary_user, latency, bucket);
	}

	return 0;
}
