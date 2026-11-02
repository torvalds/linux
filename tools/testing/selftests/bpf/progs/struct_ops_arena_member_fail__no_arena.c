// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/* No arena in the object: the member cannot be translated into anything. */
SEC("struct_ops/test_arena_member")
__failure __msg("arena pointer member ptr of struct bpf_testmod_arena_member_args "
		"requires a program with an associated arena")
int BPF_PROG(no_arena_member, struct bpf_testmod_arena_member_args *args)
{
	return args->ptr != NULL;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_member_fail = {
	.test_arena_member = (void *)no_arena_member,
};
