// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct inner_map_type {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, 4);
	__uint(value_size, 4);
	__uint(max_entries, 1);
} inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 64);
	__type(key, int);
	__type(value, int);
	__array(values, struct inner_map_type);
} outer_map SEC(".maps") = {
	.values = {
		[0] = &inner_map,
	},
};
