// SPDX-License-Identifier: GPL-2.0
#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"

struct bar {
	struct bpf_list_node node;
	int data;
};

struct foo {
	struct bpf_list_node node;
	struct bpf_list_head head __contains(bar, node);
	struct bpf_spin_lock lock;
	int data;
	struct bpf_list_node node2;
};

struct map_value {
	struct bpf_spin_lock lock;
	int data;
	struct bpf_list_head head __contains(foo, node);
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
};

struct array_map array_map SEC(".maps");
struct array_map inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
	__array(values, struct array_map);
} map_of_maps SEC(".maps") = {
	.values = {
		[0] = &inner_map,
	},
};

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))

private(A) struct bpf_spin_lock glock;
private(A) struct bpf_list_head ghead __contains(foo, node);
private(B) struct bpf_spin_lock glock2;

#endif
