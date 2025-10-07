// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

/* Timer tests */

struct timer_elem {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct timer_elem);
} timer_map SEC(".maps");

static int timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	u32 data;
	/* Timer callbacks are never sleepable, even from non-sleepable programs */
	bpf_copy_from_user(&data, sizeof(data), NULL);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
__failure __msg("helper call might sleep in a non-sleepable prog")
int timer_non_sleepable_prog(void *ctx)
{
	struct timer_elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&timer_map, &key);
	if (!val)
		return 0;

	bpf_timer_init(&val->t, &timer_map, 0);
	bpf_timer_set_callback(&val->t, timer_cb);
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("helper call might sleep in a non-sleepable prog")
int timer_sleepable_prog(void *ctx)
{
	struct timer_elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&timer_map, &key);
	if (!val)
		return 0;

	bpf_timer_init(&val->t, &timer_map, 0);
	bpf_timer_set_callback(&val->t, timer_cb);
	return 0;
}

/* Workqueue tests */

struct wq_elem {
	struct bpf_wq w;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct wq_elem);
} wq_map SEC(".maps");

static int wq_cb(void *map, int *key, void *value)
{
	u32 data;
	/* Workqueue callbacks are always sleepable, even from non-sleepable programs */
	bpf_copy_from_user(&data, sizeof(data), NULL);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
__success
int wq_non_sleepable_prog(void *ctx)
{
	struct wq_elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&wq_map, &key);
	if (!val)
		return 0;

	if (bpf_wq_init(&val->w, &wq_map, 0) != 0)
		return 0;
	if (bpf_wq_set_callback_impl(&val->w, wq_cb, 0, NULL) != 0)
		return 0;
	return 0;
}

SEC("lsm.s/file_open")
__success
int wq_sleepable_prog(void *ctx)
{
	struct wq_elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&wq_map, &key);
	if (!val)
		return 0;

	if (bpf_wq_init(&val->w, &wq_map, 0) != 0)
		return 0;
	if (bpf_wq_set_callback_impl(&val->w, wq_cb, 0, NULL) != 0)
		return 0;
	return 0;
}

/* Task work tests */

struct task_work_elem {
	struct bpf_task_work tw;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct task_work_elem);
} task_work_map SEC(".maps");

static int task_work_cb(struct bpf_map *map, void *key, void *value)
{
	u32 data;
	/* Task work callbacks are always sleepable, even from non-sleepable programs */
	bpf_copy_from_user(&data, sizeof(data), NULL);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
__success
int task_work_non_sleepable_prog(void *ctx)
{
	struct task_work_elem *val;
	struct task_struct *task;
	int key = 0;

	val = bpf_map_lookup_elem(&task_work_map, &key);
	if (!val)
		return 0;

	task = bpf_get_current_task_btf();
	if (!task)
		return 0;

	bpf_task_work_schedule_resume(task, &val->tw, &task_work_map, task_work_cb, NULL);
	return 0;
}

SEC("lsm.s/file_open")
__success
int task_work_sleepable_prog(void *ctx)
{
	struct task_work_elem *val;
	struct task_struct *task;
	int key = 0;

	val = bpf_map_lookup_elem(&task_work_map, &key);
	if (!val)
		return 0;

	task = bpf_get_current_task_btf();
	if (!task)
		return 0;

	bpf_task_work_schedule_resume(task, &val->tw, &task_work_map, task_work_cb, NULL);
	return 0;
}
