// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include <bpf_arena_common.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	/* page 0 hosts the arena global, page 1 is for allocations */
	__uint(max_entries, 2);
} arena SEC(".maps");

/*
 * Occupies page 0 so no allocation lands at arena offset 0, which the
 * nullable tests below must be able to tell apart from NULL.
 */
u64 __arena arena_pad;

/* volatile to force the scalar reloads below */
volatile u64 stash;

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
int arena_arg_forms(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;
	u64 ret;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	/* PTR_TO_ARENA argument */
	*val = 41;
	ret = bpf_kfunc_arena_arg_test((u64 *)val);
	if (ret != 41 || *val != 42)
		return 2;

	/* the low 32 bits as a scalar */
	stash = (u32)(u64)val;
	ret = bpf_kfunc_arena_arg_test((u64 *)stash);
	if (ret != 42 || *val != 43)
		return 3;

	/* the full user address as a scalar */
	stash = (u64)val;
	bpf_addr_space_cast(stash, 1, 0);
	ret = bpf_kfunc_arena_arg_test((u64 *)stash);
	if (ret != 43 || *val != 44)
		return 4;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}

/*
 * Pin the rebase semantics using the capture kfuncs, which return the raw
 * argument value: __arena rebases unconditionally, so zero low 32 bits
 * arrive as the arena kernel base, while __arena__nullable turns them into
 * NULL.
 */
SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
int arena_arg_rebase(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;
	u64 base, off;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	base = bpf_kfunc_arena_cap_test(NULL);
	if (!base)
		return 2;

	/* only the low 32 bits contribute */
	stash = 0xbadc0ffe00000000;
	if (bpf_kfunc_arena_cap_test((u64 *)stash) != base)
		return 3;

	off = (u32)(u64)val;
	if (bpf_kfunc_arena_cap_test((u64 *)val) != base + off)
		return 4;

	if (bpf_kfunc_arena_cap_nullable_test(NULL) != 0)
		return 5;

	stash = 0xbadc0ffe00000000;
	if (bpf_kfunc_arena_cap_nullable_test((u64 *)stash) != 0)
		return 6;

	if (bpf_kfunc_arena_cap_nullable_test((u64 *)val) != base + off)
		return 7;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
int arena_args5(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	val[0] = 1;
	val[1] = 2;
	val[2] = 4;
	val[3] = 8;
	val[4] = 16;

	if (bpf_kfunc_arena_args5_test((u64 *)&val[0], (u64 *)&val[1],
				       (u64 *)&val[2], (u64 *)&val[3],
				       (u64 *)&val[4]) != 31)
		return 2;
	if (bpf_kfunc_arena_args5_test((u64 *)&val[0], (u64 *)&val[1],
				       (u64 *)&val[2], (u64 *)&val[3], NULL) != 15)
		return 3;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
int arena_arg_mixed(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	val[0] = 7;
	val[1] = 5;

	if (bpf_kfunc_arena_mixed_test((u64 *)&val[0], NULL) != 7)
		return 2;

	if (bpf_kfunc_arena_mixed_test((u64 *)&val[0], (u64 *)&val[1]) != 12)
		return 3;

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}

/* kernel-side faults on unpopulated pages recover via the scratch page */
SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
int arena_arg_unpopulated(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	u64 __arena *val;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	stash = (u64)val + PAGE_SIZE;
	bpf_kfunc_arena_arg_test((u64 *)stash);

	bpf_arena_free_pages(&arena, (void __arena *)val, 1);
#endif
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__failure __msg("arena pointer requires a program with an associated arena")
int arena_arg_no_arena(void *ctx)
{
	bpf_kfunc_arena_arg_test((u64 *)1);
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__failure __msg("is not a pointer to arena or scalar")
int arena_arg_bad_reg(void *ctx)
{
	u64 buf = 0;

	/* use the arena so the program passes the arena presence check */
	bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	bpf_kfunc_arena_arg_test(&buf);
	return 0;
}

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)
SEC("syscall")
__arch_x86_64
__arch_arm64
__failure __msg("arena pointer cannot be a stack argument")
int arena_arg_stack(void *ctx)
{
	bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	bpf_kfunc_arena_stack_arg_test(1, 2, 3, 4, 5, (u64 *)1);
	return 0;
}
#else
SEC("syscall")
__arch_x86_64
__arch_arm64
__description("arena_arg_stack: not supported, dummy test")
__skip("arena_arg_stack: not supported")
__success
int dummy_test(void *ctx)
{
	return 0;
}
#endif

char _license[] SEC("license") = "GPL";
