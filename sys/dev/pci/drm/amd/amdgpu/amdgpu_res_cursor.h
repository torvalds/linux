// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#ifndef __AMDGPU_RES_CURSOR_H__
#define __AMDGPU_RES_CURSOR_H__

#include <drm/drm_mm.h>
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_range_manager.h>

#include "amdgpu_vram_mgr.h"

/* state back for walking over vram_mgr and gtt_mgr allocations */
struct amdgpu_res_cursor {
	uint64_t		start;
	uint64_t		size;
	uint64_t		remaining;
	void			*node;
	uint32_t		mem_type;
};

/**
 * amdgpu_res_first - initialize a amdgpu_res_cursor
 *
 * @res: TTM resource object to walk
 * @start: Start of the range
 * @size: Size of the range
 * @cur: cursor object to initialize
 *
 * Start walking over the range of allocations between @start and @size.
 */
static inline void amdgpu_res_first(struct ttm_resource *res,
				    uint64_t start, uint64_t size,
				    struct amdgpu_res_cursor *cur)
{
	struct drm_buddy_block *block;
	struct list_head *head, *next;
	struct drm_mm_node *node;

	if (!res)
		goto fallback;

	BUG_ON(start + size > res->size);

	cur->mem_type = res->mem_type;

	switch (cur->mem_type) {
	case TTM_PL_VRAM:
		head = &to_amdgpu_vram_mgr_resource(res)->blocks;

		block = list_first_entry_or_null(head,
						 struct drm_buddy_block,
						 link);
		if (!block)
			goto fallback;

		while (start >= amdgpu_vram_mgr_block_size(block)) {
			start -= amdgpu_vram_mgr_block_size(block);

			next = block->link.next;
			if (next != head)
				block = list_entry(next, struct drm_buddy_block, link);
		}

		cur->start = amdgpu_vram_mgr_block_start(block) + start;
		cur->size = min(amdgpu_vram_mgr_block_size(block) - start, size);
		cur->remaining = size;
		cur->node = block;
		break;
	case TTM_PL_TT:
	case AMDGPU_PL_DOORBELL:
		node = to_ttm_range_mgr_node(res)->mm_nodes;
		while (start >= node->size << PAGE_SHIFT)
			start -= node++->size << PAGE_SHIFT;

		cur->start = (node->start << PAGE_SHIFT) + start;
		cur->size = min((node->size << PAGE_SHIFT) - start, size);
		cur->remaining = size;
		cur->node = node;
		break;
	default:
		goto fallback;
	}

	return;

fallback:
	cur->start = start;
	cur->size = size;
	cur->remaining = size;
	cur->node = NULL;
	WARN_ON(res && start + size > res->size);
}

/**
 * amdgpu_res_next - advance the cursor
 *
 * @cur: the cursor to advance
 * @size: number of bytes to move forward
 *
 * Move the cursor @size bytes forwrad, walking to the next node if necessary.
 */
static inline void amdgpu_res_next(struct amdgpu_res_cursor *cur, uint64_t size)
{
	struct drm_buddy_block *block;
	struct drm_mm_node *node;
	struct list_head *next;

	BUG_ON(size > cur->remaining);

	cur->remaining -= size;
	if (!cur->remaining)
		return;

	cur->size -= size;
	if (cur->size) {
		cur->start += size;
		return;
	}

	switch (cur->mem_type) {
	case TTM_PL_VRAM:
		block = cur->node;

		next = block->link.next;
		block = list_entry(next, struct drm_buddy_block, link);

		cur->node = block;
		cur->start = amdgpu_vram_mgr_block_start(block);
		cur->size = min(amdgpu_vram_mgr_block_size(block), cur->remaining);
		break;
	case TTM_PL_TT:
	case AMDGPU_PL_DOORBELL:
		node = cur->node;

		cur->node = ++node;
		cur->start = node->start << PAGE_SHIFT;
		cur->size = min(node->size << PAGE_SHIFT, cur->remaining);
		break;
	default:
		return;
	}
}

/**
 * amdgpu_res_cleared - check if blocks are cleared
 *
 * @cur: the cursor to extract the block
 *
 * Check if the @cur block is cleared
 */
static inline bool amdgpu_res_cleared(struct amdgpu_res_cursor *cur)
{
	struct drm_buddy_block *block;

	switch (cur->mem_type) {
	case TTM_PL_VRAM:
		block = cur->node;

		if (!amdgpu_vram_mgr_is_cleared(block))
			return false;
		break;
	default:
		return false;
	}

	return true;
}

#endif
