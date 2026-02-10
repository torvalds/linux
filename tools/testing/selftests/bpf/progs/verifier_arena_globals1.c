// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"
#include "bpf_arena_common.h"
#include "bpf_misc.h"

#define ARENA_PAGES (1UL<< (32 - __builtin_ffs(__PAGE_SIZE) + 1))
#define GLOBAL_PAGES (16)

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
 * Global data, to be placed at the end of the arena.
 */
volatile char __arena global_data[GLOBAL_PAGES][PAGE_SIZE];

SEC("syscall")
__success __retval(0)
int check_reserve1(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	const u8 magic = 0x5a;
	__u8 __arena *guard, *globals;
	volatile char __arena *ptr;
	int i;
	int ret;

	guard = (void __arena *)arena_base(&arena);
	globals = (void __arena *)(arena_base(&arena) + (ARENA_PAGES - GLOBAL_PAGES) * PAGE_SIZE);

	/* Reserve the region we've offset the globals by. */
	ret = bpf_arena_reserve_pages(&arena, guard, ARENA_PAGES - GLOBAL_PAGES);
	if (ret)
		return 1;

	/* Make sure the globals are in the expected offset. */
	ret = bpf_arena_reserve_pages(&arena, globals, 1);
	if (!ret)
		return 2;

	/* Verify globals are properly mapped in by libbpf. */
	for (i = 0; i < GLOBAL_PAGES; i++) {
		ptr = &global_data[i][PAGE_SIZE / 2];

		*ptr = magic;
		if (*ptr != magic)
			return i + 3;
	}
#endif
	return 0;
}

/*
 * Relocation check by reading directly into the global data w/o using symbols.
 */
SEC("syscall")
__success __retval(0)
int check_relocation(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	const u8 magic = 0xfa;
	u8 __arena *ptr;

	global_data[GLOBAL_PAGES - 1][PAGE_SIZE / 2] = magic;
	ptr = (u8 __arena *)((u64)(ARENA_PAGES * PAGE_SIZE - PAGE_SIZE / 2));
	if (*ptr != magic)
		return 1;

#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
