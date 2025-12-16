// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include "bpf_arena_common.h"

#define ARENA_PAGES (32)

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, ARENA_PAGES);
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, (1ull << 32) | (~0u - __PAGE_SIZE * ARENA_PAGES + 1));
#else
	__ulong(map_extra, (1ull << 44) | (~0u - __PAGE_SIZE * ARENA_PAGES + 1));
#endif
} arena SEC(".maps");

/*
 * Fill the entire arena with global data.
 * The offset into the arena should be 0.
 */
char __arena global_data[ARENA_PAGES][PAGE_SIZE];

SEC("syscall")
__success __retval(0)
int check_reserve2(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	void __arena *guard;
	int ret;

	guard = (void __arena *)arena_base(&arena);

	/* Make sure the data at offset 0 case is properly handled. */
	ret = bpf_arena_reserve_pages(&arena, guard, 1);
	if (!ret)
		return 1;
#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
