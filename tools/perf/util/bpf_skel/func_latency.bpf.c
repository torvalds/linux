// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// This should be in sync with "util/ftrace.h"
#define NUM_BUCKET  22

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 10000);
} functime SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} cpu_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} task_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, NUM_BUCKET);
} latency SEC(".maps");


int enabled = 0;

// stats
__s64 total;
__s64 count;
__s64 max;
__s64 min;

const volatile int has_cpu = 0;
const volatile int has_task = 0;
const volatile int use_nsec = 0;
const volatile unsigned int bucket_range;
const volatile unsigned int min_latency;
const volatile unsigned int max_latency;
const volatile unsigned int bucket_num = NUM_BUCKET;

SEC("kprobe/func")
int BPF_PROG(func_begin)
{
	__u64 key, now;

	if (!enabled)
		return 0;

	key = bpf_get_current_pid_tgid();

	if (has_cpu) {
		__u32 cpu = bpf_get_smp_processor_id();
		__u8 *ok;

		ok = bpf_map_lookup_elem(&cpu_filter, &cpu);
		if (!ok)
			return 0;
	}

	if (has_task) {
		__u32 pid = key & 0xffffffff;
		__u8 *ok;

		ok = bpf_map_lookup_elem(&task_filter, &pid);
		if (!ok)
			return 0;
	}

	now = bpf_ktime_get_ns();

	// overwrite timestamp for nested functions
	bpf_map_update_elem(&functime, &key, &now, BPF_ANY);
	return 0;
}

SEC("kretprobe/func")
int BPF_PROG(func_end)
{
	__u64 tid;
	__u64 *start;
	__u64 cmp_base = use_nsec ? 1 : 1000;

	if (!enabled)
		return 0;

	tid = bpf_get_current_pid_tgid();

	start = bpf_map_lookup_elem(&functime, &tid);
	if (start) {
		__s64 delta = bpf_ktime_get_ns() - *start;
		__u64 val = delta;
		__u32 key = 0;
		__u64 *hist;

		bpf_map_delete_elem(&functime, &tid);

		if (delta < 0)
			return 0;

		if (bucket_range != 0) {
			val = delta / cmp_base;

			if (min_latency > 0) {
				if (val > min_latency)
					val -= min_latency;
				else
					goto do_lookup;
			}

			// Less than 1 unit (ms or ns), or, in the future,
			// than the min latency desired.
			if (val > 0) { // 1st entry: [ 1 unit .. bucket_range units )
				key = val / bucket_range + 1;
				if (key >= bucket_num)
					key = bucket_num - 1;
			}

			goto do_lookup;
		}
		// calculate index using delta
		for (key = 0; key < (bucket_num - 1); key++) {
			if (delta < (cmp_base << key))
				break;
		}

do_lookup:
		hist = bpf_map_lookup_elem(&latency, &key);
		if (!hist)
			return 0;

		__sync_fetch_and_add(hist, 1);

		__sync_fetch_and_add(&total, delta); // always in nsec
		__sync_fetch_and_add(&count, 1);

		if (delta > max)
			max = delta;
		if (delta < min)
			min = delta;
	}

	return 0;
}
