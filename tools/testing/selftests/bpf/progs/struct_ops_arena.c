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
	/* page 0 hosts the arena globals, page 1 is for allocations */
	__uint(max_entries, 2);
} arena SEC(".maps");

/* also associates the callbacks with the arena */
u64 __arena arena_touch;
/* raw value of the last __arena ctx argument, captured by test_arena_cb */
u64 __arena cb_ptr_val;

SEC("struct_ops/test_arena")
int test_arena_cb(unsigned long long *ctx)
{
	u64 __arena *ptr = (u64 __arena *)ctx[0];

	arena_touch++;
	cb_ptr_val = ctx[0];
	*ptr += 1;
	return 0;
}

SEC("struct_ops/test_arena_nullable")
int test_arena_nullable_cb(unsigned long long *ctx)
{
	u64 __arena *ptr = (u64 __arena *)ctx[0];

	arena_touch++;
	if (!ptr)
		return 0xbee;
	*ptr += 1;
	return 0;
}

SEC("struct_ops/test_arena_stack")
int test_arena_stack_cb(unsigned long long *ctx)
{
	u64 __arena *ptr = (u64 __arena *)ctx[8];

	arena_touch++;
	/* pin the slot layout: the leading args fill ctx[0]..ctx[7] */
	if (ctx[0] != 1 || ctx[7] != 8)
		return 0xbad;
	*ptr += 1;
	return 0;
}

SEC("struct_ops/test_arena_multislot")
int test_arena_multislot_cb(unsigned long long *ctx)
{
	u64 __arena *ptr = (u64 __arena *)ctx[2];

	arena_touch++;
	/*
	 * The 16-byte struct occupies ctx[0] and ctx[1], so @ptr is argument
	 * one but slot two. Getting that wrong hands the callback a scalar.
	 */
	if (ctx[0] != 11 || ctx[1] != 22)
		return 0xbad;
	*ptr += 1;
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena = {
	.test_arena = (void *)test_arena_cb,
	.test_arena_nullable = (void *)test_arena_nullable_cb,
	.test_arena_stack = (void *)test_arena_stack_cb,
	.test_arena_multislot = (void *)test_arena_multislot_cb,
};

SEC("syscall")
int trigger(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;
	int ret;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	*val = 41;
	ret = bpf_testmod_ops3_call_test_arena((u64 *)val);
	if (ret)
		return 2;
	if (*val != 42)
		return 3;

	/*
	 * The callback must have seen exactly (u32)(kaddr - kern_vm_start),
	 * which is the arena offset of val with the upper 32 bits clear.
	 */
	if (cb_ptr_val != (u32)(u64)val)
		return 4;

	ret = bpf_testmod_ops3_call_test_arena_nullable((u64 *)val);
	if (ret)
		return 5;
	if (*val != 43)
		return 6;

	/* NULL survives the nullable kfunc and the trampoline as NULL */
	ret = bpf_testmod_ops3_call_test_arena_nullable(NULL);
	if (ret != 0xbee)
		return 7;

	/* the arena pointer is stack-passed into the trampoline here */
	ret = bpf_testmod_ops3_call_test_arena_stack((u64 *)val);
	if (ret)
		return 8;
	if (*val != 44)
		return 9;

	/* a multi-slot arg precedes the arena pointer here */
	ret = bpf_testmod_ops3_call_test_arena_multislot((u64 *)val);
	if (ret)
		return 10;
	if (*val != 45)
		return 11;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}
