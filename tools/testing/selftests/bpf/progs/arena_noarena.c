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

char _license[] SEC("license") = "GPL";
