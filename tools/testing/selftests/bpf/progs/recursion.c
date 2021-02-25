// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long);
} hash1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long);
} hash2 SEC(".maps");

int pass1 = 0;
int pass2 = 0;

SEC("fentry/__htab_map_lookup_elem")
int BPF_PROG(on_lookup, struct bpf_map *map)
{
	int key = 0;

	if (map == (void *)&hash1) {
		pass1++;
		return 0;
	}
	if (map == (void *)&hash2) {
		pass2++;
		/* htab_map_gen_lookup() will inline below call
		 * into direct call to __htab_map_lookup_elem()
		 */
		bpf_map_lookup_elem(&hash2, &key);
		return 0;
	}

	return 0;
}
