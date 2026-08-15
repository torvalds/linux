// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/*
 * One explicit array map and no global variables, so the generated loader
 * has exactly one map to create (no .rodata/.bss). prog_tests/signed_loader.c
 * uses this to check that a signed loader ignores ctx-supplied max_entries:
 * the map must keep its attested size (4), not whatever the host puts in
 * the loader ctx.
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, __u64);
} amap SEC(".maps");

SEC("socket")
int probe(void *ctx)
{
	__u32 key = 0;
	__u64 *val = bpf_map_lookup_elem(&amap, &key);

	return val ? (int)*val : 0;
}

char _license[] SEC("license") = "GPL";
