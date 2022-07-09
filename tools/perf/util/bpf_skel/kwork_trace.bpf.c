// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022, Huawei

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define KWORK_COUNT 100
#define MAX_KWORKNAME 128

/*
 * This should be in sync with "util/kwork.h"
 */
enum kwork_class_type {
	KWORK_CLASS_IRQ,
	KWORK_CLASS_SOFTIRQ,
	KWORK_CLASS_WORKQUEUE,
	KWORK_CLASS_MAX,
};

struct work_key {
	__u32 type;
	__u32 cpu;
	__u64 id;
};

struct report_data {
	__u64 nr;
	__u64 total_time;
	__u64 max_time;
	__u64 max_time_start;
	__u64 max_time_end;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, MAX_KWORKNAME);
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_names SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_time SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(struct report_data));
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_report SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} perf_kwork_cpu_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, MAX_KWORKNAME);
	__uint(max_entries, 1);
} perf_kwork_name_filter SEC(".maps");

int enabled = 0;
int has_cpu_filter = 0;
int has_name_filter = 0;

char LICENSE[] SEC("license") = "Dual BSD/GPL";
