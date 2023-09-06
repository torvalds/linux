// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook
// Copyright (c) 2021 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define MAX_LEVELS  10  // max cgroup hierarchy level: arbitrary
#define MAX_EVENTS  32  // max events per cgroup: arbitrary

// NOTE: many of map and global data will be modified before loading
//       from the userspace (perf tool) using the skeleton helpers.

// single set of global perf events to measure
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 1);
} events SEC(".maps");

// from cgroup id to event index
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, 1);
} cgrp_idx SEC(".maps");

// per-cpu event snapshots to calculate delta
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
} prev_readings SEC(".maps");

// aggregated event values for each cgroup (per-cpu)
// will be read from the user-space
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
} cgrp_readings SEC(".maps");

const volatile __u32 num_events = 1;
const volatile __u32 num_cpus = 1;

int enabled = 0;
int use_cgroup_v2 = 0;

static inline int get_cgroup_v1_idx(__u32 *cgrps, int size)
{
	struct task_struct *p = (void *)bpf_get_current_task();
	struct cgroup *cgrp;
	register int i = 0;
	__u32 *elem;
	int level;
	int cnt;

	cgrp = BPF_CORE_READ(p, cgroups, subsys[perf_event_cgrp_id], cgroup);
	level = BPF_CORE_READ(cgrp, level);

	for (cnt = 0; i < MAX_LEVELS; i++) {
		__u64 cgrp_id;

		if (i > level)
			break;

		// convert cgroup-id to a map index
		cgrp_id = BPF_CORE_READ(cgrp, ancestor_ids[i]);
		elem = bpf_map_lookup_elem(&cgrp_idx, &cgrp_id);
		if (!elem)
			continue;

		cgrps[cnt++] = *elem;
		if (cnt == size)
			break;
	}

	return cnt;
}

static inline int get_cgroup_v2_idx(__u32 *cgrps, int size)
{
	register int i = 0;
	__u32 *elem;
	int cnt;

	for (cnt = 0; i < MAX_LEVELS; i++) {
		__u64 cgrp_id = bpf_get_current_ancestor_cgroup_id(i);

		if (cgrp_id == 0)
			break;

		// convert cgroup-id to a map index
		elem = bpf_map_lookup_elem(&cgrp_idx, &cgrp_id);
		if (!elem)
			continue;

		cgrps[cnt++] = *elem;
		if (cnt == size)
			break;
	}

	return cnt;
}

static int bperf_cgroup_count(void)
{
	register __u32 idx = 0;  // to have it in a register to pass BPF verifier
	register int c = 0;
	struct bpf_perf_event_value val, delta, *prev_val, *cgrp_val;
	__u32 cpu = bpf_get_smp_processor_id();
	__u32 cgrp_idx[MAX_LEVELS];
	int cgrp_cnt;
	__u32 key, cgrp;
	long err;

	if (use_cgroup_v2)
		cgrp_cnt = get_cgroup_v2_idx(cgrp_idx, MAX_LEVELS);
	else
		cgrp_cnt = get_cgroup_v1_idx(cgrp_idx, MAX_LEVELS);

	for ( ; idx < MAX_EVENTS; idx++) {
		if (idx == num_events)
			break;

		// XXX: do not pass idx directly (for verifier)
		key = idx;
		// this is per-cpu array for diff
		prev_val = bpf_map_lookup_elem(&prev_readings, &key);
		if (!prev_val) {
			val.counter = val.enabled = val.running = 0;
			bpf_map_update_elem(&prev_readings, &key, &val, BPF_ANY);

			prev_val = bpf_map_lookup_elem(&prev_readings, &key);
			if (!prev_val)
				continue;
		}

		// read from global perf_event array
		key = idx * num_cpus + cpu;
		err = bpf_perf_event_read_value(&events, key, &val, sizeof(val));
		if (err)
			continue;

		if (enabled) {
			delta.counter = val.counter - prev_val->counter;
			delta.enabled = val.enabled - prev_val->enabled;
			delta.running = val.running - prev_val->running;

			for (c = 0; c < MAX_LEVELS; c++) {
				if (c == cgrp_cnt)
					break;

				cgrp = cgrp_idx[c];

				// aggregate the result by cgroup
				key = cgrp * num_events + idx;
				cgrp_val = bpf_map_lookup_elem(&cgrp_readings, &key);
				if (cgrp_val) {
					cgrp_val->counter += delta.counter;
					cgrp_val->enabled += delta.enabled;
					cgrp_val->running += delta.running;
				} else {
					bpf_map_update_elem(&cgrp_readings, &key,
							    &delta, BPF_ANY);
				}
			}
		}

		*prev_val = val;
	}
	return 0;
}

// This will be attached to cgroup-switches event for each cpu
SEC("perf_event")
int BPF_PROG(on_cgrp_switch)
{
	return bperf_cgroup_count();
}

SEC("raw_tp/sched_switch")
int BPF_PROG(trigger_read)
{
	return bperf_cgroup_count();
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
