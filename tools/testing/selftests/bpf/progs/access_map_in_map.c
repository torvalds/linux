// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <time.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"

struct inner_map_type {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, 4);
	__uint(value_size, 4);
	__uint(max_entries, 1);
} inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
	__array(values, struct inner_map_type);
} outer_array_map SEC(".maps") = {
	.values = {
		[0] = &inner_map,
	},
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
	__array(values, struct inner_map_type);
} outer_htab_map SEC(".maps") = {
	.values = {
		[0] = &inner_map,
	},
};

char _license[] SEC("license") = "GPL";

int tgid = 0;

static int acc_map_in_map(void *outer_map)
{
	int i, key, value = 0xdeadbeef;
	void *inner_map;

	if ((bpf_get_current_pid_tgid() >> 32) != tgid)
		return 0;

	/* Find nonexistent inner map */
	key = 1;
	inner_map = bpf_map_lookup_elem(outer_map, &key);
	if (inner_map)
		return 0;

	/* Find the old inner map */
	key = 0;
	inner_map = bpf_map_lookup_elem(outer_map, &key);
	if (!inner_map)
		return 0;

	/* Wait for the old inner map to be replaced */
	for (i = 0; i < 2048; i++)
		bpf_map_update_elem(inner_map, &key, &value, 0);

	return 0;
}

SEC("?kprobe/" SYS_PREFIX "sys_getpgid")
int access_map_in_array(void *ctx)
{
	return acc_map_in_map(&outer_array_map);
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int sleepable_access_map_in_array(void *ctx)
{
	return acc_map_in_map(&outer_array_map);
}

SEC("?kprobe/" SYS_PREFIX "sys_getpgid")
int access_map_in_htab(void *ctx)
{
	return acc_map_in_map(&outer_htab_map);
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int sleepable_access_map_in_htab(void *ctx)
{
	return acc_map_in_map(&outer_htab_map);
}
