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
 * The kernel owns the member, a program only reads it. A struct_ops program
 * cannot store through its arguments at all, so the store is refused before
 * the tag comes into play; the exchange test reaches the arena member check.
 */
SEC("struct_ops/test_arena_member")
__failure __msg("only read is supported")
int BPF_PROG(write_member, struct bpf_testmod_arena_member_args *args)
{
	asm volatile("" :: "r"(&arena));
	args->ptr = NULL;
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member_fail = {
	.test_arena_member = (void *)write_member,
};
