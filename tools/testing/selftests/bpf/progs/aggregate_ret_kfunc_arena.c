// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf_arena_common.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

#if defined(__clang_major__) && __clang_major__ >= 23

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 2);
} arena SEC(".maps");

SEC("syscall")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_ret_kfunc_arena(void *ctx)
{
	u32 volatile __arena *page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	u32 volatile __arena *a, *b;
	struct prog_test_ret_arena r;

	if (!page)
		return 1;

	/* Both halves come back in R0:R2, pointing at page and page + 4. */
	r = bpf_kfunc_call_test_ret_arena((u64)page);
	if (!r.a || !r.b)
		return 2;

	a = (u32 __arena *)r.a;
	b = (u32 __arena *)r.b;
	*a = 1;
	*b = 2;
	if (*a != 1)
		return 3;
	if (*b != 2)
		return 4;

	page[0] = 7;
	if (*a != 7)
		return 5;
	page[1] = 9;
	if (*b != 9)
		return 6;

	return 0;
}

SEC("syscall")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_ret_kfunc_arena_mixed(void *ctx)
{
	u32 __arena *page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	struct prog_test_ret_arena_mixed r;
	u32 volatile __arena *p;

	if (!page)
		return 1;

	/* An arena pointer in R0 beside a scalar in R2. */
	r = bpf_kfunc_call_test_ret_arena_mixed((u64)page);
	if (!r.p)
		return 2;
	if (r.tag != 0xbeef)
		return 3;

	p = (u32 __arena *)r.p;
	*p = 3;
	if (*p != 3)
		return 4;

	return 0;
}

SEC("syscall")
__arch_x86_64 __arch_arm64
__failure __msg("is not composed of scalars or arena pointers")
__msg("member 'b' has type PTR")
int aggregate_ret_kfunc_arena_untagged_fail(void *ctx)
{
	struct prog_test_ret_arena_untagged r;

	r = bpf_kfunc_call_test_ret_arena_untagged(0);

	return r.a == r.b;
}

SEC("syscall")
__arch_x86_64 __arch_arm64
__failure __msg("is not composed of scalars or arena pointers")
__msg("member 'b' has type PTR")
int aggregate_ret_kfunc_arena_union_fail(void *ctx)
{
	union prog_test_ret_arena_union r;

	r = bpf_kfunc_call_test_ret_arena_union(0);

	return r.a == r.b;
}

#else

SEC("socket")
__description("aggregate_ret_kfunc_arena: needs LLVM 23, dummy test")
__skip("needs LLVM 23")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
