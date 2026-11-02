// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1); /* number of pages */
} arena SEC(".maps");

/*
 * Dereferencing an arena global needs the compiler to emit an
 * addr_space_cast, which only clang does. Keep the variables so the arena
 * is still populated and the skeleton still has its arena member, but skip
 * the test elsewhere.
 */
#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
bool skip_tests __attribute((__section__(".data"))) = false;
#else
bool skip_tests = true;
#endif

long __arena_global a_val = 1;
extern long __arena_global b_val; /* defined in linked_arena2.c */

SEC("syscall")
int sum1(void *ctx)
{
#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
	return a_val + b_val;
#else
	/*
	 * Reference the extern without dereferencing it, so that the relink
	 * test still has an extern to resolve.
	 */
	return (long)&b_val;
#endif
}

char _license[] SEC("license") = "GPL";
