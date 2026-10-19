// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

/*
 * Programs without an arena. They cannot share an object with an arena map,
 * libbpf binds the arena of an object to every program in it.
 */

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
__flag(BPF_F_ARENA_SCALAR)
__arch_x86_64
__arch_arm64
__description("arena_scalar: no arena, no access through a number")
__failure __msg("R6 invalid mem access 'scalar'")
__naked void no_arena(void)
{
	asm volatile ("					\
	r6 = 0x100000000000 ll;				\
	r0 = *(u64 *)(r6 + 0);				\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
