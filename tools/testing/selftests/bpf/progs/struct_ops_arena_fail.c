// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/* No arena in the program: attaching to test_arena must be rejected. */
SEC("struct_ops/test_arena")
int test_arena_no_arena(unsigned long long *ctx)
{
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_arena_fail = {
	.test_arena = (void *)test_arena_no_arena,
};
