/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include "i915_scatterlist.h"
#include "i915_ttm_buddy_manager.h"

#include <drm/drm_buddy.h>
#include <drm/drm_mm.h>

#include <linux/slab.h>

bool i915_sg_trim(struct sg_table *orig_st)
{
	struct sg_table new_st;
	struct scatterlist *sg, *new_sg;
	unsigned int i;

	if (orig_st->nents == orig_st->orig_nents)
		return false;

	if (sg_alloc_table(&new_st, orig_st->nents, GFP_KERNEL | __GFP_NOWARN))
		return false;

	new_sg = new_st.sgl;
	for_each_sg(orig_st->sgl, sg, orig_st->nents, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, 0);
		sg_dma_address(new_sg) = sg_dma_address(sg);
		sg_dma_len(new_sg) = sg_dma_len(sg);

		new_sg = sg_next(new_sg);
	}
	GEM_BUG_ON(new_sg); /* Should walk exactly nents and hit the end */

	sg_free_table(orig_st);

	*orig_st = new_st;
	return true;
}

static void i915_refct_sgt_release(struct kref *ref)
{
	struct i915_refct_sgt *rsgt =
		container_of(ref, typeof(*rsgt), kref);

	sg_free_table(&rsgt->table);
	kfree(rsgt);
}

static const struct i915_refct_sgt_ops rsgt_ops = {
	.release = i915_refct_sgt_release
};

/**
 * i915_refct_sgt_init - Initialize a struct i915_refct_sgt with default ops
 * @rsgt: The struct i915_refct_sgt to initialize.
 * @size: The size of the underlying memory buffer.
 */
void i915_refct_sgt_init(struct i915_refct_sgt *rsgt, size_t size)
{
	__i915_refct_sgt_init(rsgt, size, &rsgt_ops);
}

/**
 * i915_rsgt_from_mm_node - Create a refcounted sg_table from a struct
 * drm_mm_node
 * @node: The drm_mm_node.
 * @region_start: An offset to add to the dma addresses of the sg list.
 * @page_alignment: Required page alignment for each sg entry. Power of two.
 *
 * Create a struct sg_table, initializing it from a struct drm_mm_node,
 * taking a maximum segment length into account, splitting into segments
 * if necessary.
 *
 * Return: A pointer to a kmalloced struct i915_refct_sgt on success, negative
 * error code cast to an error pointer on failure.
 */
struct i915_refct_sgt *i915_rsgt_from_mm_node(const struct drm_mm_node *node,
					      u64 region_start,
					      u32 page_alignment)
{
	const u32 max_segment = round_down(UINT_MAX, page_alignment);
	const u32 segment_pages = max_segment >> PAGE_SHIFT;
	u64 block_size, offset, prev_end;
	struct i915_refct_sgt *rsgt;
	struct sg_table *st;
	struct scatterlist *sg;

	GEM_BUG_ON(!max_segment);

	rsgt = kmalloc(sizeof(*rsgt), GFP_KERNEL | __GFP_NOWARN);
	if (!rsgt)
		return ERR_PTR(-ENOMEM);

	i915_refct_sgt_init(rsgt, node->size << PAGE_SHIFT);
	st = &rsgt->table;
	/* restricted by sg_alloc_table */
	if (WARN_ON(overflows_type(DIV_ROUND_UP_ULL(node->size, segment_pages),
				   unsigned int))) {
		i915_refct_sgt_put(rsgt);
		return ERR_PTR(-E2BIG);
	}

	if (sg_alloc_table(st, DIV_ROUND_UP_ULL(node->size, segment_pages),
			   GFP_KERNEL | __GFP_NOWARN)) {
		i915_refct_sgt_put(rsgt);
		return ERR_PTR(-ENOMEM);
	}

	sg = st->sgl;
	st->nents = 0;
	prev_end = (resource_size_t)-1;
	block_size = node->size << PAGE_SHIFT;
	offset = node->start << PAGE_SHIFT;

	while (block_size) {
		u64 len;

		if (offset != prev_end || sg->length >= max_segment) {
			if (st->nents)
				sg = __sg_next(sg);

			sg_dma_address(sg) = region_start + offset;
			GEM_BUG_ON(!IS_ALIGNED(sg_dma_address(sg),
					       page_alignment));
			sg_dma_len(sg) = 0;
			sg->length = 0;
			st->nents++;
		}

		len = min_t(u64, block_size, max_segment - sg->length);
		sg->length += len;
		sg_dma_len(sg) += len;

		offset += len;
		block_size -= len;

		prev_end = offset;
	}

	sg_mark_end(sg);
	i915_sg_trim(st);

	return rsgt;
}

/**
 * i915_rsgt_from_buddy_resource - Create a refcounted sg_table from a struct
 * i915_buddy_block list
 * @res: The struct i915_ttm_buddy_resource.
 * @region_start: An offset to add to the dma addresses of the sg list.
 * @page_alignment: Required page alignment for each sg entry. Power of two.
 *
 * Create a struct sg_table, initializing it from struct i915_buddy_block list,
 * taking a maximum segment length into account, splitting into segments
 * if necessary.
 *
 * Return: A pointer to a kmalloced struct i915_refct_sgts on success, negative
 * error code cast to an error pointer on failure.
 */
struct i915_refct_sgt *i915_rsgt_from_buddy_resource(struct ttm_resource *res,
						     u64 region_start,
						     u32 page_alignment)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);
	const u64 size = res->size;
	const u32 max_segment = round_down(UINT_MAX, page_alignment);
	struct drm_buddy *mm = bman_res->mm;
	struct list_head *blocks = &bman_res->blocks;
	struct drm_buddy_block *block;
	struct i915_refct_sgt *rsgt;
	struct scatterlist *sg;
	struct sg_table *st;
	resource_size_t prev_end;

	GEM_BUG_ON(list_empty(blocks));
	GEM_BUG_ON(!max_segment);

	rsgt = kmalloc(sizeof(*rsgt), GFP_KERNEL | __GFP_NOWARN);
	if (!rsgt)
		return ERR_PTR(-ENOMEM);

	i915_refct_sgt_init(rsgt, size);
	st = &rsgt->table;
	/* restricted by sg_alloc_table */
	if (WARN_ON(overflows_type(PFN_UP(res->size), unsigned int))) {
		i915_refct_sgt_put(rsgt);
		return ERR_PTR(-E2BIG);
	}

	if (sg_alloc_table(st, PFN_UP(res->size), GFP_KERNEL | __GFP_NOWARN)) {
		i915_refct_sgt_put(rsgt);
		return ERR_PTR(-ENOMEM);
	}

	sg = st->sgl;
	st->nents = 0;
	prev_end = (resource_size_t)-1;

	list_for_each_entry(block, blocks, link) {
		u64 block_size, offset;

		block_size = min_t(u64, size, drm_buddy_block_size(mm, block));
		offset = drm_buddy_block_offset(block);

		while (block_size) {
			u64 len;

			if (offset != prev_end || sg->length >= max_segment) {
				if (st->nents)
					sg = __sg_next(sg);

				sg_dma_address(sg) = region_start + offset;
				GEM_BUG_ON(!IS_ALIGNED(sg_dma_address(sg),
						       page_alignment));
				sg_dma_len(sg) = 0;
				sg->length = 0;
				st->nents++;
			}

			len = min_t(u64, block_size, max_segment - sg->length);
			sg->length += len;
			sg_dma_len(sg) += len;

			offset += len;
			block_size -= len;

			prev_end = offset;
		}
	}

	sg_mark_end(sg);
	i915_sg_trim(st);

	return rsgt;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/scatterlist.c"
#endif
