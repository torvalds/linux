// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include "bpf_arena_common.h"

#define ARENA_SIZE (1ull << 32)

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, ARENA_SIZE / PAGE_SIZE);
} arena SEC(".maps");

SEC("syscall")
__success __retval(0)
int big_alloc1(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	volatile char __arena *page1, *page2, *no_page, *page3;
	void __arena *base;

	page1 = base = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page1)
		return 1;
	*page1 = 1;
	page2 = bpf_arena_alloc_pages(&arena, base + ARENA_SIZE - PAGE_SIZE,
				      1, NUMA_NO_NODE, 0);
	if (!page2)
		return 2;
	*page2 = 2;
	no_page = bpf_arena_alloc_pages(&arena, base + ARENA_SIZE,
					1, NUMA_NO_NODE, 0);
	if (no_page)
		return 3;
	if (*page1 != 1)
		return 4;
	if (*page2 != 2)
		return 5;
	bpf_arena_free_pages(&arena, (void __arena *)page1, 1);
	if (*page2 != 2)
		return 6;
	if (*page1 != 0) /* use-after-free should return 0 */
		return 7;
	page3 = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page3)
		return 8;
	*page3 = 3;
	if (page1 != page3)
		return 9;
	if (*page2 != 2)
		return 10;
	if (*(page1 + PAGE_SIZE) != 0)
		return 11;
	if (*(page1 - PAGE_SIZE) != 0)
		return 12;
	if (*(page2 + PAGE_SIZE) != 0)
		return 13;
	if (*(page2 - PAGE_SIZE) != 0)
		return 14;
#endif
	return 0;
}
char _license[] SEC("license") = "GPL";
