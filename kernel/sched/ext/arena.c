/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * scx_arena_pool: kernel-side sub-allocator over BPF-arena pages.
 *
 * Each chunk added to @sch->arena_pool comes from one
 * bpf_arena_alloc_pages_sleepable() call and is registered at the
 * kernel-side mapping address. Callers translate to the BPF-arena form
 * themselves if needed.
 *
 * Allocations grow the pool on demand. Underlying arena pages are released
 * when the arena map itself is torn down.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#include <linux/genalloc.h>

#include "internal.h"
#include "arena.h"

enum scx_arena_consts {
	SCX_ARENA_MIN_ORDER		= 3,	/* 8-byte minimum sub-allocation */
	SCX_ARENA_GROW_PAGES		= 4,	/* per growth */
};

s32 scx_arena_pool_init(struct scx_sched *sch)
{
	if (!sch->arena_map)
		return 0;

	sch->arena_pool = gen_pool_create(SCX_ARENA_MIN_ORDER, NUMA_NO_NODE);
	if (!sch->arena_pool)
		return -ENOMEM;
	return 0;
}

static void scx_arena_clear_chunk(struct gen_pool *pool, struct gen_pool_chunk *chunk,
				  void *data)
{
	int order = pool->min_alloc_order;
	size_t chunk_sz = chunk->end_addr - chunk->start_addr + 1;
	unsigned long end_bit = chunk_sz >> order;
	unsigned long b, e;

	for_each_set_bitrange(b, e, chunk->bits, end_bit)
		gen_pool_free(pool, chunk->start_addr + (b << order),
			      (e - b) << order);
}

/*
 * Tear down the pool. Outstanding gen_pool allocations are freed via
 * scx_arena_clear_chunk() so gen_pool_destroy() doesn't BUG. The underlying
 * arena pages are released when the arena map itself is torn down.
 */
void scx_arena_pool_destroy(struct scx_sched *sch)
{
	if (!sch->arena_pool)
		return;
	gen_pool_for_each_chunk(sch->arena_pool, scx_arena_clear_chunk, NULL);
	gen_pool_destroy(sch->arena_pool);
	sch->arena_pool = NULL;
}

/*
 * Grow the pool by @page_cnt pages. bpf_arena_alloc_pages_sleepable() and
 * gen_pool_add() (which calls vzalloc(GFP_KERNEL)) require a sleepable
 * context.
 */
static int scx_arena_grow(struct scx_sched *sch, u32 page_cnt)
{
	u64 kern_vm_start;
	u32 uaddr32;
	void *p;
	int ret;

	if (!sch->arena_map || !sch->arena_pool)
		return -EINVAL;

	p = bpf_arena_alloc_pages_sleepable(sch->arena_map, NULL,
					    page_cnt, NUMA_NO_NODE, 0);
	if (!p)
		return -ENOMEM;

	uaddr32 = (u32)(unsigned long)p;
	/* arena.o, which defines these, is built only on MMU && 64BIT */
#if defined(CONFIG_MMU) && defined(CONFIG_64BIT)
	kern_vm_start = bpf_arena_map_kern_vm_start(sch->arena_map);
#else
	kern_vm_start = 0;
#endif

	ret = gen_pool_add(sch->arena_pool, kern_vm_start + uaddr32,
			   page_cnt * PAGE_SIZE, NUMA_NO_NODE);
	if (ret) {
		bpf_arena_free_pages_non_sleepable(sch->arena_map, p, page_cnt);
		return ret;
	}
	return 0;
}

/*
 * Allocate @size bytes from the arena pool. Returns kernel VA on success, NULL
 * on failure. May grow the pool via scx_arena_grow() which sleeps. Caller must
 * be in a GFP_KERNEL context.
 */
void *scx_arena_alloc(struct scx_sched *sch, size_t size)
{
	unsigned long kern_va;
	u32 page_cnt;

	might_sleep();

	if (!sch->arena_pool)
		return NULL;

	while (true) {
		kern_va = gen_pool_alloc(sch->arena_pool, size);
		if (kern_va)
			break;
		page_cnt = max_t(u32, SCX_ARENA_GROW_PAGES,
				 (size + PAGE_SIZE - 1) >> PAGE_SHIFT);
		if (scx_arena_grow(sch, page_cnt))
			return NULL;
	}

	return (void *)kern_va;
}

void scx_arena_free(struct scx_sched *sch, void *kern_va, size_t size)
{
	if (sch->arena_pool && kern_va)
		gen_pool_free(sch->arena_pool, (unsigned long)kern_va, size);
}
