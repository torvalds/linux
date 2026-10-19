// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
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
 * The same load reaches args->ptr on one path and args->cookie on the other,
 * the branch on the unknown cookie value makes the verifier walk both. The
 * rewrite is per insn, so the paths have to agree on what they load.
 */
SEC("struct_ops/test_arena_member")
__failure __msg("loads an arena pointer member on one path and something else on another")
__naked int mismatch_member(void)
{
	asm volatile (
	"r6 = *(u64 *)(r1 + 0);"
	"r2 = %[arena] ll;"
	"r0 = *(u64 *)(r6 + %[cookie_off]);"
	"if r0 == 0 goto 1f;"
	"r6 += %[cookie_off];"
"1:	r0 = *(u64 *)(r6 + 0);"
	"r0 = 0;"
	"exit;"
	:
	: __imm_addr(arena),
	  __imm_const(cookie_off, offsetof(struct bpf_testmod_arena_member_args, cookie))
	: __clobber_all);
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member_fail = {
	.test_arena_member = (void *)mismatch_member,
};
