// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bperf.h"
#include "bperf_u.h"

reading_map diff_readings SEC(".maps");
reading_map accum_readings SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} filter SEC(".maps");

enum bperf_filter_type type = 0;
int enabled = 0;

SEC("fexit/XXX")
int BPF_PROG(fexit_XXX)
{
	struct bpf_perf_event_value *diff_val, *accum_val;
	__u32 filter_key, zero = 0;
	__u32 *accum_key;

	if (!enabled)
		return 0;

	switch (type) {
	case BPERF_FILTER_GLOBAL:
		accum_key = &zero;
		goto do_add;
	case BPERF_FILTER_CPU:
		filter_key = bpf_get_smp_processor_id();
		break;
	case BPERF_FILTER_PID:
		filter_key = bpf_get_current_pid_tgid() & 0xffffffff;
		break;
	case BPERF_FILTER_TGID:
		filter_key = bpf_get_current_pid_tgid() >> 32;
		break;
	default:
		return 0;
	}

	accum_key = bpf_map_lookup_elem(&filter, &filter_key);
	if (!accum_key)
		return 0;

do_add:
	diff_val = bpf_map_lookup_elem(&diff_readings, &zero);
	if (!diff_val)
		return 0;

	accum_val = bpf_map_lookup_elem(&accum_readings, accum_key);
	if (!accum_val)
		return 0;

	accum_val->counter += diff_val->counter;
	accum_val->enabled += diff_val->enabled;
	accum_val->running += diff_val->running;

	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
