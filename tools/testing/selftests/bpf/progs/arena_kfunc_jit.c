// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

/*
 * Verify the JIT-emitted rebase sequences for __arena and __arena__nullable
 * kfunc arguments. The capture kfuncs take the argument without
 * dereferencing it, so these tests pin only the emitted code.
 */
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
	__uint(max_entries, 1);
} arena SEC(".maps");

/* volatile to force the scalar reloads below */
volatile u64 stash;

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)

SEC("syscall")
__arch_x86_64
__jited("...")
__jited("	movl	%edi, %edi")
__jited("	addq	%r12, %rdi")
__jited("...")
__jited("	callq	{{.*}}")
__arch_arm64
__jited("...")
__jited("	add	x0, x28, w0, uxtw")
__jited("	{{(bl|mov)	.*}}")
__success
int arena_arg_jit_rebase(void *ctx)
{
	stash = (u64)bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	bpf_kfunc_arena_cap_test((u64 *)stash);
	return 0;
}

SEC("syscall")
__arch_x86_64
__jited("...")
__jited("	movl	%edi, %edi")
__jited("	testl	%edi, %edi")
__jited("	je	L0")
__jited("	addq	%r12, %rdi")
__jited("L0:	callq	{{.*}}")
__arch_arm64
__jited("...")
__jited("	mov	w0, w0")
__jited("	cbz	w0, L0")
__jited("	add	x0, x28, w0, uxtw")
__jited("L0:	{{.*}}")
__success
int arena_arg_jit_nullable(void *ctx)
{
	stash = (u64)bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	bpf_kfunc_arena_cap_nullable_test((u64 *)stash);
	return 0;
}

SEC("syscall")
__arch_x86_64
__jited("...")
__jited("	movl	%edi, %edi")
__jited("	addq	%r12, %rdi")
__jited("	movl	%esi, %esi")
__jited("	addq	%r12, %rsi")
__jited("	movl	%edx, %edx")
__jited("	addq	%r12, %rdx")
__jited("	movl	%ecx, %ecx")
__jited("	addq	%r12, %rcx")
__jited("	movl	%r8d, %r8d")
__jited("	testl	%r8d, %r8d")
__jited("	je	L0")
__jited("	addq	%r12, %r8")
__jited("L0:	callq	{{.*}}")
__arch_arm64
__jited("...")
__jited("	add	x0, x28, w0, uxtw")
__jited("	add	x1, x28, w1, uxtw")
__jited("	add	x2, x28, w2, uxtw")
__jited("	add	x3, x28, w3, uxtw")
__jited("	mov	w4, w4")
__jited("	cbz	w4, L0")
__jited("	add	x4, x28, w4, uxtw")
__jited("L0:	{{.*}}")
__success
int arena_arg_jit_args5(void *ctx)
{
	u64 __arena *val;

	val = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!val)
		return 1;

	val[0] = 1;
	val[1] = 2;
	val[2] = 4;
	val[3] = 8;
	val[4] = 16;

	bpf_kfunc_arena_args5_test((u64 *)&val[0], (u64 *)&val[1],
				   (u64 *)&val[2], (u64 *)&val[3],
				   (u64 *)&val[4]);
	return 0;
}

#endif /* __BPF_FEATURE_ADDR_SPACE_CAST */

char _license[] SEC("license") = "GPL";
