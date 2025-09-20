// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct inner_map_type {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, 4);
	__uint(value_size, 4);
	__uint(max_entries, 1);
} inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 2);
	__array(values, struct inner_map_type);
} outer_htab_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 2);
	__array(values, struct inner_map_type);
} outer_alloc_htab_map SEC(".maps");

char _license[] SEC("license") = "GPL";
