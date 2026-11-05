// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf_arena_common.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1);
} arena SEC(".maps");

/*
 * The same load reads args->ptr on one path and the memory argument on the
 * other. The verifier lets a load insn serve a kernel struct pointer and a
 * memory pointer, see save_aux_ptr_type(), but the rewrite of the arena
 * member would corrupt the value the other path loads. A global function
 * is the only place a struct_ops program of this type gets a memory pointer.
 * The loaded value goes nowhere: an ALU insn on it would be an arena
 * pointer on one path and a scalar on the other, which the verifier rejects
 * on its own before the fixup pass gets to the load.
 */
__noinline int load_either(unsigned long long *ctx __arg_ctx, u64 *mem __arg_nonnull)
{
	struct bpf_testmod_arena_member_args *args = (void *)ctx[0];
	void *p = args;

	if (args->cookie == 0)
		p = mem;
	/* one load insn, reached with both pointers */
	asm volatile("r0 = *(u64 *)(%[p] + 0)" :: [p]"r"(p) : "r0");
	return 0;
}

SEC("struct_ops/test_arena_member")
__failure __msg("loads an arena pointer member on one path and something else on another")
int BPF_PROG(mismatch_mem, struct bpf_testmod_arena_member_args *args)
{
	u64 local = 0;

	asm volatile("" :: "r"(&arena));
	return load_either(ctx, &local);
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member_fail = {
	.test_arena_member = (void *)mismatch_mem,
};
