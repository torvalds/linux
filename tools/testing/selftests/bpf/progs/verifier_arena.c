// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 2); /* arena of two pages close to 32-bit boundary*/
#ifdef __TARGET_ARCH_arm64
        __ulong(map_extra, (1ull << 32) | (~0u - __PAGE_SIZE * 2 + 1)); /* start of mmap() region */
#else
        __ulong(map_extra, (1ull << 44) | (~0u - __PAGE_SIZE * 2 + 1)); /* start of mmap() region */
#endif
} arena SEC(".maps");

SEC("syscall")
__success __retval(0)
int basic_alloc1(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	volatile int __arena *page1, *page2, *no_page, *page3;

	page1 = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page1)
		return 1;
	*page1 = 1;
	page2 = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page2)
		return 2;
	*page2 = 2;
	no_page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (no_page)
		return 3;
	if (*page1 != 1)
		return 4;
	if (*page2 != 2)
		return 5;
	bpf_arena_free_pages(&arena, (void __arena *)page2, 1);
	if (*page1 != 1)
		return 6;
	if (*page2 != 0) /* use-after-free should return 0 */
		return 7;
	page3 = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page3)
		return 8;
	*page3 = 3;
	if (page2 != page3)
		return 9;
	if (*page1 != 1)
		return 10;
#endif
	return 0;
}

SEC("syscall")
__success __retval(0)
int basic_alloc2(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	volatile char __arena *page1, *page2, *page3, *page4;

	page1 = bpf_arena_alloc_pages(&arena, NULL, 2, NUMA_NO_NODE, 0);
	if (!page1)
		return 1;
	page2 = page1 + __PAGE_SIZE;
	page3 = page1 + __PAGE_SIZE * 2;
	page4 = page1 - __PAGE_SIZE;
	*page1 = 1;
	*page2 = 2;
	*page3 = 3;
	*page4 = 4;
	if (*page1 != 1)
		return 1;
	if (*page2 != 2)
		return 2;
	if (*page3 != 0)
		return 3;
	if (*page4 != 0)
		return 4;
	bpf_arena_free_pages(&arena, (void __arena *)page1, 2);
	if (*page1 != 0)
		return 5;
	if (*page2 != 0)
		return 6;
	if (*page3 != 0)
		return 7;
	if (*page4 != 0)
		return 8;
#endif
	return 0;
}

struct bpf_arena___l {
        struct bpf_map map;
} __attribute__((preserve_access_index));

SEC("syscall")
__success __retval(0) __log_level(2)
int basic_alloc3(void *ctx)
{
	struct bpf_arena___l *ar = (struct bpf_arena___l *)&arena;
	volatile char __arena *pages;

	pages = bpf_arena_alloc_pages(&ar->map, NULL, ar->map.max_entries, NUMA_NO_NODE, 0);
	if (!pages)
		return 1;
	return 0;
}

SEC("syscall")
__success __retval(0)
int basic_reserve1(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	char __arena *page;
	int ret;

	page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!page)
		return 1;

	page += __PAGE_SIZE;

	/* Reserve the second page */
	ret = bpf_arena_reserve_pages(&arena, page, 1);
	if (ret)
		return 2;

	/* Try to explicitly allocate the reserved page. */
	page = bpf_arena_alloc_pages(&arena, page, 1, NUMA_NO_NODE, 0);
	if (page)
		return 3;

	/* Try to implicitly allocate the page (since there's only 2 of them). */
	page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (page)
		return 4;
#endif
	return 0;
}

SEC("syscall")
__success __retval(0)
int basic_reserve2(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	char __arena *page;
	int ret;

	page = arena_base(&arena);
	ret = bpf_arena_reserve_pages(&arena, page, 1);
	if (ret)
		return 1;

	page = bpf_arena_alloc_pages(&arena, page, 1, NUMA_NO_NODE, 0);
	if ((u64)page)
		return 2;
#endif
	return 0;
}

/* Reserve the same page twice, should return -EBUSY. */
SEC("syscall")
__success __retval(0)
int reserve_twice(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	char __arena *page;
	int ret;

	page = arena_base(&arena);

	ret = bpf_arena_reserve_pages(&arena, page, 1);
	if (ret)
		return 1;

	ret = bpf_arena_reserve_pages(&arena, page, 1);
	if (ret != -EBUSY)
		return 2;
#endif
	return 0;
}

/* Try to reserve past the end of the arena. */
SEC("syscall")
__success __retval(0)
int reserve_invalid_region(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	char __arena *page;
	int ret;

	/* Try a NULL pointer. */
	ret = bpf_arena_reserve_pages(&arena, NULL, 3);
	if (ret != -EINVAL)
		return 1;

	page = arena_base(&arena);

	ret = bpf_arena_reserve_pages(&arena, page, 3);
	if (ret != -EINVAL)
		return 2;

	ret = bpf_arena_reserve_pages(&arena, page, 4096);
	if (ret != -EINVAL)
		return 3;

	ret = bpf_arena_reserve_pages(&arena, page, (1ULL << 32) - 1);
	if (ret != -EINVAL)
		return 4;
#endif
	return 0;
}

SEC("iter.s/bpf_map")
__success __log_level(2)
int iter_maps1(struct bpf_iter__bpf_map *ctx)
{
	struct bpf_map *map = ctx->map;

	if (!map)
		return 0;
	bpf_arena_alloc_pages(map, NULL, map->max_entries, 0, 0);
	return 0;
}

SEC("iter.s/bpf_map")
__failure __msg("expected pointer to STRUCT bpf_map")
int iter_maps2(struct bpf_iter__bpf_map *ctx)
{
	struct seq_file *seq = ctx->meta->seq;

	bpf_arena_alloc_pages((void *)seq, NULL, 1, 0, 0);
	return 0;
}

SEC("iter.s/bpf_map")
__failure __msg("untrusted_ptr_bpf_map")
int iter_maps3(struct bpf_iter__bpf_map *ctx)
{
	struct bpf_map *map = ctx->map;

	if (!map)
		return 0;
	bpf_arena_alloc_pages(map->inner_map_meta, NULL, map->max_entries, 0, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
