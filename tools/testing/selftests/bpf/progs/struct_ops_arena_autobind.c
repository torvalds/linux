// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"
#include <bpf_arena_common.h>
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1);
} arena SEC(".maps");

/*
 * Neither the callback nor the unrelated program refers to the arena. libbpf
 * binds it to them all the same, so the callback can use the __arena argument
 * the kernel hands it.
 */
SEC("struct_ops/test_arena")
int test_arena_cb(unsigned long long *ctx)
{
	u64 __arena *ptr = (u64 __arena *)ctx[0];

	*ptr += 1;
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_autobind = {
	.test_arena = (void *)test_arena_cb,
};

SEC("syscall")
int unrelated(void *ctx)
{
	return 0;
}

SEC("syscall")
int trigger(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	*val = 41;
	if (bpf_testmod_ops3_call_test_arena((u64 *)val))
		return 2;
	if (*val != 42)
		return 3;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}
