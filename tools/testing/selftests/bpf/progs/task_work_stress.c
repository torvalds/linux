// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

#define ENTRIES 128

char _license[] SEC("license") = "GPL";

__u64 callback_scheduled = 0;
__u64 callback_success = 0;
__u64 schedule_error = 0;
__u64 delete_success = 0;

struct elem {
	__u32 count;
	struct bpf_task_work tw;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, ENTRIES);
	__type(key, int);
	__type(value, struct elem);
} hmap SEC(".maps");

static int process_work(struct bpf_map *map, void *key, void *value)
{
	__sync_fetch_and_add(&callback_success, 1);
	return 0;
}

SEC("syscall")
int schedule_task_work(void *ctx)
{
	struct elem empty_work = {.count = 0};
	struct elem *work;
	int key = 0, err;

	key = bpf_ktime_get_ns() % ENTRIES;
	work = bpf_map_lookup_elem(&hmap, &key);
	if (!work) {
		bpf_map_update_elem(&hmap, &key, &empty_work, BPF_NOEXIST);
		work = bpf_map_lookup_elem(&hmap, &key);
		if (!work)
			return 0;
	}
	err = bpf_task_work_schedule_signal_impl(bpf_get_current_task_btf(), &work->tw, &hmap,
						 process_work, NULL);
	if (err)
		__sync_fetch_and_add(&schedule_error, 1);
	else
		__sync_fetch_and_add(&callback_scheduled, 1);
	return 0;
}

SEC("syscall")
int delete_task_work(void *ctx)
{
	int key = 0, err;

	key = bpf_get_prandom_u32() % ENTRIES;
	err = bpf_map_delete_elem(&hmap, &key);
	if (!err)
		__sync_fetch_and_add(&delete_success, 1);
	return 0;
}
