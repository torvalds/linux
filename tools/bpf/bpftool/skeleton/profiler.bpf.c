// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2020 Facebook
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct bpf_perf_event_value___local {
	__u64 counter;
	__u64 enabled;
	__u64 running;
} __attribute__((preserve_access_index));

/* map of perf event fds, num_cpu * num_metric entries */
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(int));
} events SEC(".maps");

/* readings at fentry */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value___local));
} fentry_readings SEC(".maps");

/* accumulated readings */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value___local));
} accum_readings SEC(".maps");

/* sample counts, one per cpu */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u64));
} counts SEC(".maps");

const volatile __u32 num_cpu = 1;
const volatile __u32 num_metric = 1;
#define MAX_NUM_METRICS 4

SEC("fentry/XXX")
int BPF_PROG(fentry_XXX)
{
	struct bpf_perf_event_value___local *ptrs[MAX_NUM_METRICS];
	u32 key = bpf_get_smp_processor_id();
	u32 i;

	/* look up before reading, to reduce error */
	for (i = 0; i < num_metric && i < MAX_NUM_METRICS; i++) {
		u32 flag = i;

		ptrs[i] = bpf_map_lookup_elem(&fentry_readings, &flag);
		if (!ptrs[i])
			return 0;
	}

	for (i = 0; i < num_metric && i < MAX_NUM_METRICS; i++) {
		struct bpf_perf_event_value___local reading;
		int err;

		err = bpf_perf_event_read_value(&events, key, (void *)&reading,
						sizeof(reading));
		if (err)
			return 0;
		*(ptrs[i]) = reading;
		key += num_cpu;
	}

	return 0;
}

static inline void
fexit_update_maps(u32 id, struct bpf_perf_event_value___local *after)
{
	struct bpf_perf_event_value___local *before, diff;

	before = bpf_map_lookup_elem(&fentry_readings, &id);
	/* only account samples with a valid fentry_reading */
	if (before && before->counter) {
		struct bpf_perf_event_value___local *accum;

		diff.counter = after->counter - before->counter;
		diff.enabled = after->enabled - before->enabled;
		diff.running = after->running - before->running;

		accum = bpf_map_lookup_elem(&accum_readings, &id);
		if (accum) {
			accum->counter += diff.counter;
			accum->enabled += diff.enabled;
			accum->running += diff.running;
		}
	}
}

SEC("fexit/XXX")
int BPF_PROG(fexit_XXX)
{
	struct bpf_perf_event_value___local readings[MAX_NUM_METRICS];
	u32 cpu = bpf_get_smp_processor_id();
	u32 i, zero = 0;
	int err;
	u64 *count;

	/* read all events before updating the maps, to reduce error */
	for (i = 0; i < num_metric && i < MAX_NUM_METRICS; i++) {
		err = bpf_perf_event_read_value(&events, cpu + i * num_cpu,
						(void *)(readings + i),
						sizeof(*readings));
		if (err)
			return 0;
	}
	count = bpf_map_lookup_elem(&counts, &zero);
	if (count) {
		*count += 1;
		for (i = 0; i < num_metric && i < MAX_NUM_METRICS; i++)
			fexit_update_maps(i, &readings[i]);
	}
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
