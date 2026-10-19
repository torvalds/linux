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
 * An offset into a union resolves to its first member, so a program reading
 * raw there would get an arena pointer it did not ask for.
 */
SEC("struct_ops/test_arena_member")
__failure __msg("arena pointer member ptr of bpf_testmod_arena_union is inside a union")
int BPF_PROG(union_member, struct bpf_testmod_arena_member_args *args)
{
	asm volatile("" :: "r"(&arena));
	return *args->u.ptr != 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member_fail = {
	.test_arena_member = (void *)union_member,
};
