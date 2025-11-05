// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

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

static int process_work(struct bpf_map *map, void *key, void *value)
{
	struct elem *work = value;

	bpf_copy_from_user_str(work->data, sizeof(work->data), (const void *)user_ptr, 0);
	return 0;
}

int key = 0;

SEC("perf_event")
__failure __msg("doesn't match map pointer in R3")
int mismatch_map(struct pt_regs *args)
{
	struct elem *work;
	struct task_struct *task;

	task = bpf_get_current_task_btf();
	work = bpf_map_lookup_elem(&arrmap, &key);
	if (!work)
		return 0;
	bpf_task_work_schedule_resume_impl(task, &work->tw, &hmap, process_work, NULL);
	return 0;
}

SEC("perf_event")
__failure __msg("arg#1 doesn't point to a map value")
int no_map_task_work(struct pt_regs *args)
{
	struct task_struct *task;
	struct bpf_task_work tw;

	task = bpf_get_current_task_btf();
	bpf_task_work_schedule_resume_impl(task, &tw, &hmap, process_work, NULL);
	return 0;
}

SEC("perf_event")
__failure __msg("Possibly NULL pointer passed to trusted arg1")
int task_work_null(struct pt_regs *args)
{
	struct task_struct *task;

	task = bpf_get_current_task_btf();
	bpf_task_work_schedule_resume_impl(task, NULL, &hmap, process_work, NULL);
	return 0;
}

SEC("perf_event")
__failure __msg("Possibly NULL pointer passed to trusted arg2")
int map_null(struct pt_regs *args)
{
	struct elem *work;
	struct task_struct *task;

	task = bpf_get_current_task_btf();
	work = bpf_map_lookup_elem(&arrmap, &key);
	if (!work)
		return 0;
	bpf_task_work_schedule_resume_impl(task, &work->tw, NULL, process_work, NULL);
	return 0;
}
