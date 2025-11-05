// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "errno.h"

char _license[] SEC("license") = "GPL";

const void *user_ptr = NULL;

struct elem {
	char data[128];
	struct bpf_task_work tw;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} hmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} arrmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} lrumap SEC(".maps");

static int process_work(struct bpf_map *map, void *key, void *value)
{
	struct elem *work = value;

	bpf_copy_from_user_str(work->data, sizeof(work->data), (const void *)user_ptr, 0);
	return 0;
}

int key = 0;

SEC("perf_event")
int oncpu_hash_map(struct pt_regs *args)
{
	struct elem empty_work = { .data = { 0 } };
	struct elem *work;
	struct task_struct *task;
	int err;

	task = bpf_get_current_task_btf();
	err = bpf_map_update_elem(&hmap, &key, &empty_work, BPF_NOEXIST);
	if (err)
		return 0;
	work = bpf_map_lookup_elem(&hmap, &key);
	if (!work)
		return 0;

	bpf_task_work_schedule_resume_impl(task, &work->tw, &hmap, process_work, NULL);
	return 0;
}

SEC("perf_event")
int oncpu_array_map(struct pt_regs *args)
{
	struct elem *work;
	struct task_struct *task;

	task = bpf_get_current_task_btf();
	work = bpf_map_lookup_elem(&arrmap, &key);
	if (!work)
		return 0;
	bpf_task_work_schedule_signal_impl(task, &work->tw, &arrmap, process_work, NULL);
	return 0;
}

SEC("perf_event")
int oncpu_lru_map(struct pt_regs *args)
{
	struct elem empty_work = { .data = { 0 } };
	struct elem *work;
	struct task_struct *task;
	int err;

	task = bpf_get_current_task_btf();
	work = bpf_map_lookup_elem(&lrumap, &key);
	if (work)
		return 0;
	err = bpf_map_update_elem(&lrumap, &key, &empty_work, BPF_NOEXIST);
	if (err)
		return 0;
	work = bpf_map_lookup_elem(&lrumap, &key);
	if (!work || work->data[0])
		return 0;
	bpf_task_work_schedule_resume_impl(task, &work->tw, &lrumap, process_work, NULL);
	return 0;
}
