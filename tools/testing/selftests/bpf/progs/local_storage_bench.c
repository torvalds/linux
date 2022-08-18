// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define HASHMAP_SZ 4194304

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, int);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
		__uint(map_flags, BPF_F_NO_PREALLOC);
		__type(key, int);
		__type(value, int);
	});
} array_of_local_storage_maps SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, int);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_HASH);
		__uint(max_entries, HASHMAP_SZ);
		__type(key, int);
		__type(value, int);
	});
} array_of_hash_maps SEC(".maps");

long important_hits;
long hits;

/* set from user-space */
const volatile unsigned int use_hashmap;
const volatile unsigned int hashmap_num_keys;
const volatile unsigned int num_maps;
const volatile unsigned int interleave;

struct loop_ctx {
	struct task_struct *task;
	long loop_hits;
	long loop_important_hits;
};

static int do_lookup(unsigned int elem, struct loop_ctx *lctx)
{
	void *map, *inner_map;
	int idx = 0;

	if (use_hashmap)
		map = &array_of_hash_maps;
	else
		map = &array_of_local_storage_maps;

	inner_map = bpf_map_lookup_elem(map, &elem);
	if (!inner_map)
		return -1;

	if (use_hashmap) {
		idx = bpf_get_prandom_u32() % hashmap_num_keys;
		bpf_map_lookup_elem(inner_map, &idx);
	} else {
		bpf_task_storage_get(inner_map, lctx->task, &idx,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	}

	lctx->loop_hits++;
	if (!elem)
		lctx->loop_important_hits++;
	return 0;
}

static long loop(u32 index, void *ctx)
{
	struct loop_ctx *lctx = (struct loop_ctx *)ctx;
	unsigned int map_idx = index % num_maps;

	do_lookup(map_idx, lctx);
	if (interleave && map_idx % 3 == 0)
		do_lookup(0, lctx);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int get_local(void *ctx)
{
	struct loop_ctx lctx;

	lctx.task = bpf_get_current_task_btf();
	lctx.loop_hits = 0;
	lctx.loop_important_hits = 0;
	bpf_loop(10000, &loop, &lctx, 0);
	__sync_add_and_fetch(&hits, lctx.loop_hits);
	__sync_add_and_fetch(&important_hits, lctx.loop_important_hits);
	return 0;
}

char _license[] SEC("license") = "GPL";
