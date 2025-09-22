/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include <linux/highmem.h>
#include <linux/prime_numbers.h>

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_move.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_migrate.h"
#include "i915_reg.h"
#include "i915_ttm_buddy_manager.h"

#include "huge_gem_object.h"
#include "i915_selftest.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_mmap.h"

struct tile {
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int size;
	unsigned int tiling;
	unsigned int swizzle;
};

static u64 swizzle_bit(unsigned int bit, u64 offset)
{
	return (offset & BIT_ULL(bit)) >> (bit - 6);
}

static u64 tiled_offset(const struct tile *tile, u64 v)
{
	u64 x, y;

	if (tile->tiling == I915_TILING_NONE)
		return v;

	y = div64_u64_rem(v, tile->stride, &x);
	v = div64_u64_rem(y, tile->height, &y) * tile->stride * tile->height;

	if (tile->tiling == I915_TILING_X) {
		v += y * tile->width;
		v += div64_u64_rem(x, tile->width, &x) << tile->size;
		v += x;
	} else if (tile->width == 128) {
		const unsigned int ytile_span = 16;
		const unsigned int ytile_height = 512;

		v += y * ytile_span;
		v += div64_u64_rem(x, ytile_span, &x) * ytile_height;
		v += x;
	} else {
		const unsigned int ytile_span = 32;
		const unsigned int ytile_height = 256;

		v += y * ytile_span;
		v += div64_u64_rem(x, ytile_span, &x) * ytile_height;
		v += x;
	}

	switch (tile->swizzle) {
	case I915_BIT_6_SWIZZLE_9:
		v ^= swizzle_bit(9, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v);
		break;
	case I915_BIT_6_SWIZZLE_9_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(11, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v) ^ swizzle_bit(11, v);
		break;
	}

	return v;
}

static int check_partial_mapping(struct drm_i915_gem_object *obj,
				 const struct tile *tile,
				 struct rnd_state *prng)
{
	const unsigned long npages = obj->base.size / PAGE_SIZE;
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_gtt_view view;
	struct i915_vma *vma;
	unsigned long offset;
	unsigned long page;
	u32 __iomem *io;
	struct vm_page *p;
	unsigned int n;
	u32 *cpu;
	int err;

	err = i915_gem_object_set_tiling(obj, tile->tiling, tile->stride);
	if (err) {
		pr_err("Failed to set tiling mode=%u, stride=%u, err=%d\n",
		       tile->tiling, tile->stride, err);
		return err;
	}

	GEM_BUG_ON(i915_gem_object_get_tiling(obj) != tile->tiling);
	GEM_BUG_ON(i915_gem_object_get_stride(obj) != tile->stride);

	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err) {
		pr_err("Failed to flush to GTT write domain; err=%d\n", err);
		return err;
	}

	page = i915_prandom_u32_max_state(npages, prng);
	view = compute_partial_view(obj, page, MIN_CHUNK_PAGES);

	vma = i915_gem_object_ggtt_pin(obj, &view, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma)) {
		pr_err("Failed to pin partial view: offset=%lu; err=%d\n",
		       page, (int)PTR_ERR(vma));
		return PTR_ERR(vma);
	}

	n = page - view.partial.offset;
	GEM_BUG_ON(n >= view.partial.size);

	io = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(io)) {
		pr_err("Failed to iomap partial view: offset=%lu; err=%d\n",
		       page, (int)PTR_ERR(io));
		err = PTR_ERR(io);
		goto out;
	}

	iowrite32(page, io + n * PAGE_SIZE / sizeof(*io));
	i915_vma_unpin_iomap(vma);

	offset = tiled_offset(tile, page << PAGE_SHIFT);
	if (offset >= obj->base.size)
		goto out;

	intel_gt_flush_ggtt_writes(to_gt(i915));

	p = i915_gem_object_get_page(obj, offset >> PAGE_SHIFT);
	cpu = kmap(p) + offset_in_page(offset);
	drm_clflush_virt_range(cpu, sizeof(*cpu));
	if (*cpu != (u32)page) {
		pr_err("Partial view for %lu [%u] (offset=%llu, size=%u [%llu, row size %u], fence=%d, tiling=%d, stride=%d) misalignment, expected write to page (%lu + %u [0x%lx]) of 0x%x, found 0x%x\n",
		       page, n,
		       view.partial.offset,
		       view.partial.size,
		       vma->size >> PAGE_SHIFT,
		       tile->tiling ? tile_row_pages(obj) : 0,
		       vma->fence ? vma->fence->id : -1, tile->tiling, tile->stride,
		       offset >> PAGE_SHIFT,
		       (unsigned int)offset_in_page(offset),
		       offset,
		       (u32)page, *cpu);
		err = -EINVAL;
	}
	*cpu = 0;
	drm_clflush_virt_range(cpu, sizeof(*cpu));
	kunmap(p);

out:
	i915_gem_object_lock(obj, NULL);
	i915_vma_destroy(vma);
	i915_gem_object_unlock(obj);
	return err;
}

static int check_partial_mappings(struct drm_i915_gem_object *obj,
				  const struct tile *tile,
				  unsigned long end_time)
{
	const unsigned int nreal = obj->scratch / PAGE_SIZE;
	const unsigned long npages = obj->base.size / PAGE_SIZE;
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_vma *vma;
	unsigned long page;
	int err;

	err = i915_gem_object_set_tiling(obj, tile->tiling, tile->stride);
	if (err) {
		pr_err("Failed to set tiling mode=%u, stride=%u, err=%d\n",
		       tile->tiling, tile->stride, err);
		return err;
	}

	GEM_BUG_ON(i915_gem_object_get_tiling(obj) != tile->tiling);
	GEM_BUG_ON(i915_gem_object_get_stride(obj) != tile->stride);

	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err) {
		pr_err("Failed to flush to GTT write domain; err=%d\n", err);
		return err;
	}

	for_each_prime_number_from(page, 1, npages) {
		struct i915_gtt_view view =
			compute_partial_view(obj, page, MIN_CHUNK_PAGES);
		unsigned long offset;
		u32 __iomem *io;
		struct vm_page *p;
		unsigned int n;
		u32 *cpu;

		GEM_BUG_ON(view.partial.size > nreal);
		cond_resched();

		vma = i915_gem_object_ggtt_pin(obj, &view, 0, 0, PIN_MAPPABLE);
		if (IS_ERR(vma)) {
			pr_err("Failed to pin partial view: offset=%lu; err=%d\n",
			       page, (int)PTR_ERR(vma));
			return PTR_ERR(vma);
		}

		n = page - view.partial.offset;
		GEM_BUG_ON(n >= view.partial.size);

		io = i915_vma_pin_iomap(vma);
		i915_vma_unpin(vma);
		if (IS_ERR(io)) {
			pr_err("Failed to iomap partial view: offset=%lu; err=%d\n",
			       page, (int)PTR_ERR(io));
			return PTR_ERR(io);
		}

		iowrite32(page, io + n * PAGE_SIZE / sizeof(*io));
		i915_vma_unpin_iomap(vma);

		offset = tiled_offset(tile, page << PAGE_SHIFT);
		if (offset >= obj->base.size)
			continue;

		intel_gt_flush_ggtt_writes(to_gt(i915));

		p = i915_gem_object_get_page(obj, offset >> PAGE_SHIFT);
		cpu = kmap(p) + offset_in_page(offset);
		drm_clflush_virt_range(cpu, sizeof(*cpu));
		if (*cpu != (u32)page) {
			pr_err("Partial view for %lu [%u] (offset=%llu, size=%u [%llu, row size %u], fence=%d, tiling=%d, stride=%d) misalignment, expected write to page (%lu + %u [0x%lx]) of 0x%x, found 0x%x\n",
			       page, n,
			       view.partial.offset,
			       view.partial.size,
			       vma->size >> PAGE_SHIFT,
			       tile->tiling ? tile_row_pages(obj) : 0,
			       vma->fence ? vma->fence->id : -1, tile->tiling, tile->stride,
			       offset >> PAGE_SHIFT,
			       (unsigned int)offset_in_page(offset),
			       offset,
			       (u32)page, *cpu);
			err = -EINVAL;
		}
		*cpu = 0;
		drm_clflush_virt_range(cpu, sizeof(*cpu));
		kunmap(p);
		if (err)
			return err;

		i915_gem_object_lock(obj, NULL);
		i915_vma_destroy(vma);
		i915_gem_object_unlock(obj);

		if (igt_timeout(end_time,
				"%s: timed out after tiling=%d stride=%d\n",
				__func__, tile->tiling, tile->stride))
			return -EINTR;
	}

	return 0;
}

static unsigned int
setup_tile_size(struct tile *tile, struct drm_i915_private *i915)
{
	if (GRAPHICS_VER(i915) <= 2) {
		tile->height = 16;
		tile->width = 128;
		tile->size = 11;
	} else if (tile->tiling == I915_TILING_Y &&
		   HAS_128_BYTE_Y_TILING(i915)) {
		tile->height = 32;
		tile->width = 128;
		tile->size = 12;
	} else {
		tile->height = 8;
		tile->width = 512;
		tile->size = 12;
	}

	if (GRAPHICS_VER(i915) < 4)
		return 8192 / tile->width;
	else if (GRAPHICS_VER(i915) < 7)
		return 128 * I965_FENCE_MAX_PITCH_VAL / tile->width;
	else
		return 128 * GEN7_FENCE_MAX_PITCH_VAL / tile->width;
}

static int igt_partial_tiling(void *arg)
{
	const unsigned int nreal = 1 << 12; /* largest tile row x2 */
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	intel_wakeref_t wakeref;
	int tiling;
	int err;

	if (!i915_ggtt_has_aperture(to_gt(i915)->ggtt))
		return 0;

	/* We want to check the page mapping and fencing of a large object
	 * mmapped through the GTT. The object we create is larger than can
	 * possibly be mmaped as a whole, and so we must use partial GGTT vma.
	 * We then check that a write through each partial GGTT vma ends up
	 * in the right set of pages within the object, and with the expected
	 * tiling, which we verify by manual swizzling.
	 */

	obj = huge_gem_object(i915,
			      nreal << PAGE_SHIFT,
			      (1 + next_prime_number(to_gt(i915)->ggtt->vm.total >> PAGE_SHIFT)) << PAGE_SHIFT);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err) {
		pr_err("Failed to allocate %u pages (%lu total), err=%d\n",
		       nreal, obj->base.size / PAGE_SIZE, err);
		goto out;
	}

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	if (1) {
		IGT_TIMEOUT(end);
		struct tile tile;

		tile.height = 1;
		tile.width = 1;
		tile.size = 0;
		tile.stride = 0;
		tile.swizzle = I915_BIT_6_SWIZZLE_NONE;
		tile.tiling = I915_TILING_NONE;

		err = check_partial_mappings(obj, &tile, end);
		if (err && err != -EINTR)
			goto out_unlock;
	}

	for (tiling = I915_TILING_X; tiling <= I915_TILING_Y; tiling++) {
		IGT_TIMEOUT(end);
		unsigned int max_pitch;
		unsigned int pitch;
		struct tile tile;

		if (i915->gem_quirks & GEM_QUIRK_PIN_SWIZZLED_PAGES)
			/*
			 * The swizzling pattern is actually unknown as it
			 * varies based on physical address of each page.
			 * See i915_gem_detect_bit_6_swizzle().
			 */
			break;

		tile.tiling = tiling;
		switch (tiling) {
		case I915_TILING_X:
			tile.swizzle = to_gt(i915)->ggtt->bit_6_swizzle_x;
			break;
		case I915_TILING_Y:
			tile.swizzle = to_gt(i915)->ggtt->bit_6_swizzle_y;
			break;
		}

		GEM_BUG_ON(tile.swizzle == I915_BIT_6_SWIZZLE_UNKNOWN);
		if (tile.swizzle == I915_BIT_6_SWIZZLE_9_17 ||
		    tile.swizzle == I915_BIT_6_SWIZZLE_9_10_17)
			continue;

		max_pitch = setup_tile_size(&tile, i915);

		for (pitch = max_pitch; pitch; pitch >>= 1) {
			tile.stride = tile.width * pitch;
			err = check_partial_mappings(obj, &tile, end);
			if (err == -EINTR)
				goto next_tiling;
			if (err)
				goto out_unlock;

			if (pitch > 2 && GRAPHICS_VER(i915) >= 4) {
				tile.stride = tile.width * (pitch - 1);
				err = check_partial_mappings(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}

			if (pitch < max_pitch && GRAPHICS_VER(i915) >= 4) {
				tile.stride = tile.width * (pitch + 1);
				err = check_partial_mappings(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}
		}

		if (GRAPHICS_VER(i915) >= 4) {
			for_each_prime_number(pitch, max_pitch) {
				tile.stride = tile.width * pitch;
				err = check_partial_mappings(obj, &tile, end);
				if (err == -EINTR)
					goto next_tiling;
				if (err)
					goto out_unlock;
			}
		}

next_tiling: ;
	}

out_unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
}

static int igt_smoke_tiling(void *arg)
{
	const unsigned int nreal = 1 << 12; /* largest tile row x2 */
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	intel_wakeref_t wakeref;
	I915_RND_STATE(prng);
	unsigned long count;
	IGT_TIMEOUT(end);
	int err;

	if (!i915_ggtt_has_aperture(to_gt(i915)->ggtt))
		return 0;

	/*
	 * igt_partial_tiling() does an exhastive check of partial tiling
	 * chunking, but will undoubtably run out of time. Here, we do a
	 * randomised search and hope over many runs of 1s with different
	 * seeds we will do a thorough check.
	 *
	 * Remember to look at the st_seed if we see a flip-flop in BAT!
	 */

	if (i915->gem_quirks & GEM_QUIRK_PIN_SWIZZLED_PAGES)
		return 0;

	obj = huge_gem_object(i915,
			      nreal << PAGE_SHIFT,
			      (1 + next_prime_number(to_gt(i915)->ggtt->vm.total >> PAGE_SHIFT)) << PAGE_SHIFT);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err) {
		pr_err("Failed to allocate %u pages (%lu total), err=%d\n",
		       nreal, obj->base.size / PAGE_SIZE, err);
		goto out;
	}

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	count = 0;
	do {
		struct tile tile;

		tile.tiling =
			i915_prandom_u32_max_state(I915_TILING_Y + 1, &prng);
		switch (tile.tiling) {
		case I915_TILING_NONE:
			tile.height = 1;
			tile.width = 1;
			tile.size = 0;
			tile.stride = 0;
			tile.swizzle = I915_BIT_6_SWIZZLE_NONE;
			break;

		case I915_TILING_X:
			tile.swizzle = to_gt(i915)->ggtt->bit_6_swizzle_x;
			break;
		case I915_TILING_Y:
			tile.swizzle = to_gt(i915)->ggtt->bit_6_swizzle_y;
			break;
		}

		if (tile.swizzle == I915_BIT_6_SWIZZLE_9_17 ||
		    tile.swizzle == I915_BIT_6_SWIZZLE_9_10_17)
			continue;

		if (tile.tiling != I915_TILING_NONE) {
			unsigned int max_pitch = setup_tile_size(&tile, i915);

			tile.stride =
				i915_prandom_u32_max_state(max_pitch, &prng);
			tile.stride = (1 + tile.stride) * tile.width;
			if (GRAPHICS_VER(i915) < 4)
				tile.stride = rounddown_pow_of_two(tile.stride);
		}

		err = check_partial_mapping(obj, &tile, &prng);
		if (err)
			break;

		count++;
	} while (!__igt_timeout(end, NULL));

	pr_info("%s: Completed %lu trials\n", __func__, count);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
}

static int make_obj_busy(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_engine_cs *engine;

	for_each_uabi_engine(engine, i915) {
		struct i915_request *rq;
		struct i915_vma *vma;
		struct i915_gem_ww_ctx ww;
		int err;

		vma = i915_vma_instance(obj, &engine->gt->ggtt->vm, NULL);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		i915_gem_ww_ctx_init(&ww, false);
retry:
		err = i915_gem_object_lock(obj, &ww);
		if (!err)
			err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_USER);
		if (err)
			goto err;

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_unpin;
		}

		err = i915_vma_move_to_active(vma, rq,
					      EXEC_OBJECT_WRITE);

		i915_request_add(rq);
err_unpin:
		i915_vma_unpin(vma);
err:
		if (err == -EDEADLK) {
			err = i915_gem_ww_ctx_backoff(&ww);
			if (!err)
				goto retry;
		}
		i915_gem_ww_ctx_fini(&ww);
		if (err)
			return err;
	}

	i915_gem_object_put(obj); /* leave it only alive via its active ref */
	return 0;
}

static enum i915_mmap_type default_mapping(struct drm_i915_private *i915)
{
	if (HAS_LMEM(i915))
		return I915_MMAP_TYPE_FIXED;

	return I915_MMAP_TYPE_GTT;
}

static struct drm_i915_gem_object *
create_sys_or_internal(struct drm_i915_private *i915,
		       unsigned long size)
{
	if (HAS_LMEM(i915)) {
		struct intel_memory_region *sys_region =
			i915->mm.regions[INTEL_REGION_SMEM];

		return __i915_gem_object_create_user(i915, size, &sys_region, 1);
	}

	return i915_gem_object_create_internal(i915, size);
}

static bool assert_mmap_offset(struct drm_i915_private *i915,
			       unsigned long size,
			       int expected)
{
	struct drm_i915_gem_object *obj;
	u64 offset;
	int ret;

	obj = create_sys_or_internal(i915, size);
	if (IS_ERR(obj))
		return expected && expected == PTR_ERR(obj);

	ret = __assign_mmap_offset(obj, default_mapping(i915), &offset, NULL);
	i915_gem_object_put(obj);

	return ret == expected;
}

static void disable_retire_worker(struct drm_i915_private *i915)
{
	i915_gem_driver_unregister__shrinker(i915);
	intel_gt_pm_get_untracked(to_gt(i915));
	cancel_delayed_work_sync(&to_gt(i915)->requests.retire_work);
}

static void restore_retire_worker(struct drm_i915_private *i915)
{
	igt_flush_test(i915);
	intel_gt_pm_put_untracked(to_gt(i915));
	i915_gem_driver_register__shrinker(i915);
}

static void mmap_offset_lock(struct drm_i915_private *i915)
	__acquires(&i915->drm.vma_offset_manager->vm_lock)
{
	write_lock(&i915->drm.vma_offset_manager->vm_lock);
}

static void mmap_offset_unlock(struct drm_i915_private *i915)
	__releases(&i915->drm.vma_offset_manager->vm_lock)
{
	write_unlock(&i915->drm.vma_offset_manager->vm_lock);
}

static int igt_mmap_offset_exhaustion(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_mm *mm = &i915->drm.vma_offset_manager->vm_addr_space_mm;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node *hole, *next;
	int loop, err = 0;
	u64 offset;
	int enospc = HAS_LMEM(i915) ? -ENXIO : -ENOSPC;

	/* Disable background reaper */
	disable_retire_worker(i915);
	GEM_BUG_ON(!to_gt(i915)->awake);
	intel_gt_retire_requests(to_gt(i915));
	i915_gem_drain_freed_objects(i915);

	/* Trim the device mmap space to only a page */
	mmap_offset_lock(i915);
	loop = 1; /* PAGE_SIZE units */
	list_for_each_entry_safe(hole, next, &mm->hole_stack, hole_stack) {
		struct drm_mm_node *resv;

		resv = kzalloc(sizeof(*resv), GFP_NOWAIT);
		if (!resv) {
			err = -ENOMEM;
			goto out_park;
		}

		resv->start = drm_mm_hole_node_start(hole) + loop;
		resv->size = hole->hole_size - loop;
		resv->color = -1ul;
		loop = 0;

		if (!resv->size) {
			kfree(resv);
			continue;
		}

		pr_debug("Reserving hole [%llx + %llx]\n",
			 resv->start, resv->size);

		err = drm_mm_reserve_node(mm, resv);
		if (err) {
			pr_err("Failed to trim VMA manager, err=%d\n", err);
			kfree(resv);
			goto out_park;
		}
	}
	GEM_BUG_ON(!list_is_singular(&mm->hole_stack));
	mmap_offset_unlock(i915);

	/* Just fits! */
	if (!assert_mmap_offset(i915, PAGE_SIZE, 0)) {
		pr_err("Unable to insert object into single page hole\n");
		err = -EINVAL;
		goto out;
	}

	/* Too large */
	if (!assert_mmap_offset(i915, 2 * PAGE_SIZE, enospc)) {
		pr_err("Unexpectedly succeeded in inserting too large object into single page hole\n");
		err = -EINVAL;
		goto out;
	}

	/* Fill the hole, further allocation attempts should then fail */
	obj = create_sys_or_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("Unable to create object for reclaimed hole\n");
		goto out;
	}

	err = __assign_mmap_offset(obj, default_mapping(i915), &offset, NULL);
	if (err) {
		pr_err("Unable to insert object into reclaimed hole\n");
		goto err_obj;
	}

	if (!assert_mmap_offset(i915, PAGE_SIZE, enospc)) {
		pr_err("Unexpectedly succeeded in inserting object into no holes!\n");
		err = -EINVAL;
		goto err_obj;
	}

	i915_gem_object_put(obj);

	/* Now fill with busy dead objects that we expect to reap */
	for (loop = 0; loop < 3; loop++) {
		if (intel_gt_is_wedged(to_gt(i915)))
			break;

		obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto out;
		}

		err = make_obj_busy(obj);
		if (err) {
			pr_err("[loop %d] Failed to busy the object\n", loop);
			goto err_obj;
		}
	}

out:
	mmap_offset_lock(i915);
out_park:
	drm_mm_for_each_node_safe(hole, next, mm) {
		if (hole->color != -1ul)
			continue;

		drm_mm_remove_node(hole);
		kfree(hole);
	}
	mmap_offset_unlock(i915);
	restore_retire_worker(i915);
	return err;
err_obj:
	i915_gem_object_put(obj);
	goto out;
}

static int gtt_set(struct drm_i915_gem_object *obj)
{
	intel_wakeref_t wakeref;
	struct i915_vma *vma;
	void __iomem *map;
	int err = 0;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	wakeref = intel_gt_pm_get(vma->vm->gt);
	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto out;
	}

	memset_io(map, POISON_INUSE, obj->base.size);
	i915_vma_unpin_iomap(vma);

out:
	intel_gt_pm_put(vma->vm->gt, wakeref);
	return err;
}

static int gtt_check(struct drm_i915_gem_object *obj)
{
	intel_wakeref_t wakeref;
	struct i915_vma *vma;
	void __iomem *map;
	int err = 0;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	wakeref = intel_gt_pm_get(vma->vm->gt);
	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto out;
	}

	if (memchr_inv((void __force *)map, POISON_FREE, obj->base.size)) {
		pr_err("%s: Write via mmap did not land in backing store (GTT)\n",
		       obj->mm.region->name);
		err = -EINVAL;
	}
	i915_vma_unpin_iomap(vma);

out:
	intel_gt_pm_put(vma->vm->gt, wakeref);
	return err;
}

static int wc_set(struct drm_i915_gem_object *obj)
{
	void *vaddr;

	vaddr = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	memset(vaddr, POISON_INUSE, obj->base.size);
	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	return 0;
}

static int wc_check(struct drm_i915_gem_object *obj)
{
	void *vaddr;
	int err = 0;

	vaddr = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	if (memchr_inv(vaddr, POISON_FREE, obj->base.size)) {
		pr_err("%s: Write via mmap did not land in backing store (WC)\n",
		       obj->mm.region->name);
		err = -EINVAL;
	}
	i915_gem_object_unpin_map(obj);

	return err;
}

static bool can_mmap(struct drm_i915_gem_object *obj, enum i915_mmap_type type)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	bool no_map;

	if (obj->ops->mmap_offset)
		return type == I915_MMAP_TYPE_FIXED;
	else if (type == I915_MMAP_TYPE_FIXED)
		return false;

	if (type == I915_MMAP_TYPE_GTT &&
	    !i915_ggtt_has_aperture(to_gt(i915)->ggtt))
		return false;

	i915_gem_object_lock(obj, NULL);
	no_map = (type != I915_MMAP_TYPE_GTT &&
		  !i915_gem_object_has_struct_page(obj) &&
		  !i915_gem_object_has_iomem(obj));
	i915_gem_object_unlock(obj);

	return !no_map;
}

#define expand32(x) (((x) << 0) | ((x) << 8) | ((x) << 16) | ((x) << 24))
static int __igt_mmap(struct drm_i915_private *i915,
		      struct drm_i915_gem_object *obj,
		      enum i915_mmap_type type)
{
	struct vm_area_struct *area;
	unsigned long addr;
	int err, i;
	u64 offset;

	if (!can_mmap(obj, type))
		return 0;

	err = wc_set(obj);
	if (err == -ENXIO)
		err = gtt_set(obj);
	if (err)
		return err;

	err = __assign_mmap_offset(obj, type, &offset, NULL);
	if (err)
		return err;

	addr = igt_mmap_offset(i915, offset, obj->base.size, PROT_WRITE, MAP_SHARED);
	if (IS_ERR_VALUE(addr))
		return addr;

	pr_debug("igt_mmap(%s, %d) @ %lx\n", obj->mm.region->name, type, addr);

	mmap_read_lock(current->mm);
	area = vma_lookup(current->mm, addr);
	mmap_read_unlock(current->mm);
	if (!area) {
		pr_err("%s: Did not create a vm_area_struct for the mmap\n",
		       obj->mm.region->name);
		err = -EINVAL;
		goto out_unmap;
	}

	for (i = 0; i < obj->base.size / sizeof(u32); i++) {
		u32 __user *ux = u64_to_user_ptr((u64)(addr + i * sizeof(*ux)));
		u32 x;

		if (get_user(x, ux)) {
			pr_err("%s: Unable to read from mmap, offset:%zd\n",
			       obj->mm.region->name, i * sizeof(x));
			err = -EFAULT;
			goto out_unmap;
		}

		if (x != expand32(POISON_INUSE)) {
			pr_err("%s: Read incorrect value from mmap, offset:%zd, found:%x, expected:%x\n",
			       obj->mm.region->name,
			       i * sizeof(x), x, expand32(POISON_INUSE));
			err = -EINVAL;
			goto out_unmap;
		}

		x = expand32(POISON_FREE);
		if (put_user(x, ux)) {
			pr_err("%s: Unable to write to mmap, offset:%zd\n",
			       obj->mm.region->name, i * sizeof(x));
			err = -EFAULT;
			goto out_unmap;
		}
	}

	if (type == I915_MMAP_TYPE_GTT)
		intel_gt_flush_ggtt_writes(to_gt(i915));

	err = wc_check(obj);
	if (err == -ENXIO)
		err = gtt_check(obj);
out_unmap:
	vm_munmap(addr, obj->base.size);
	return err;
}

static int igt_mmap(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id) {
		unsigned long sizes[] = {
			PAGE_SIZE,
			mr->min_page_size,
			SZ_4M,
		};
		int i;

		if (mr->private)
			continue;

		for (i = 0; i < ARRAY_SIZE(sizes); i++) {
			struct drm_i915_gem_object *obj;
			int err;

			obj = __i915_gem_object_create_user(i915, sizes[i], &mr, 1);
			if (obj == ERR_PTR(-ENODEV))
				continue;

			if (IS_ERR(obj))
				return PTR_ERR(obj);

			err = __igt_mmap(i915, obj, I915_MMAP_TYPE_GTT);
			if (err == 0)
				err = __igt_mmap(i915, obj, I915_MMAP_TYPE_WC);
			if (err == 0)
				err = __igt_mmap(i915, obj, I915_MMAP_TYPE_FIXED);

			i915_gem_object_put(obj);
			if (err)
				return err;
		}
	}

	return 0;
}

static void igt_close_objects(struct drm_i915_private *i915,
			      struct list_head *objects)
{
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, objects, st_link) {
		i915_gem_object_lock(obj, NULL);
		if (i915_gem_object_has_pinned_pages(obj))
			i915_gem_object_unpin_pages(obj);
		/* No polluting the memory region between tests */
		__i915_gem_object_put_pages(obj);
		i915_gem_object_unlock(obj);
		list_del(&obj->st_link);
		i915_gem_object_put(obj);
	}

	cond_resched();

	i915_gem_drain_freed_objects(i915);
}

static void igt_make_evictable(struct list_head *objects)
{
	struct drm_i915_gem_object *obj;

	list_for_each_entry(obj, objects, st_link) {
		i915_gem_object_lock(obj, NULL);
		if (i915_gem_object_has_pinned_pages(obj))
			i915_gem_object_unpin_pages(obj);
		i915_gem_object_unlock(obj);
	}

	cond_resched();
}

static int igt_fill_mappable(struct intel_memory_region *mr,
			     struct list_head *objects)
{
	u64 size, total;
	int err;

	total = 0;
	size = resource_size(&mr->io);
	do {
		struct drm_i915_gem_object *obj;

		obj = i915_gem_object_create_region(mr, size, 0, 0);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err_close;
		}

		list_add(&obj->st_link, objects);

		err = i915_gem_object_pin_pages_unlocked(obj);
		if (err) {
			if (err != -ENXIO && err != -ENOMEM)
				goto err_close;

			if (size == mr->min_page_size) {
				err = 0;
				break;
			}

			size >>= 1;
			continue;
		}

		total += obj->base.size;
	} while (1);

	pr_info("%s filled=%lluMiB\n", __func__, total >> 20);
	return 0;

err_close:
	igt_close_objects(mr->i915, objects);
	return err;
}

static int ___igt_mmap_migrate(struct drm_i915_private *i915,
			       struct drm_i915_gem_object *obj,
			       unsigned long addr,
			       bool unfaultable)
{
	struct vm_area_struct *area;
	int err = 0, i;

	pr_info("igt_mmap(%s, %d) @ %lx\n",
		obj->mm.region->name, I915_MMAP_TYPE_FIXED, addr);

	mmap_read_lock(current->mm);
	area = vma_lookup(current->mm, addr);
	mmap_read_unlock(current->mm);
	if (!area) {
		pr_err("%s: Did not create a vm_area_struct for the mmap\n",
		       obj->mm.region->name);
		err = -EINVAL;
		goto out_unmap;
	}

	for (i = 0; i < obj->base.size / sizeof(u32); i++) {
		u32 __user *ux = u64_to_user_ptr((u64)(addr + i * sizeof(*ux)));
		u32 x;

		if (get_user(x, ux)) {
			err = -EFAULT;
			if (!unfaultable) {
				pr_err("%s: Unable to read from mmap, offset:%zd\n",
				       obj->mm.region->name, i * sizeof(x));
				goto out_unmap;
			}

			continue;
		}

		if (unfaultable) {
			pr_err("%s: Faulted unmappable memory\n",
			       obj->mm.region->name);
			err = -EINVAL;
			goto out_unmap;
		}

		if (x != expand32(POISON_INUSE)) {
			pr_err("%s: Read incorrect value from mmap, offset:%zd, found:%x, expected:%x\n",
			       obj->mm.region->name,
			       i * sizeof(x), x, expand32(POISON_INUSE));
			err = -EINVAL;
			goto out_unmap;
		}

		x = expand32(POISON_FREE);
		if (put_user(x, ux)) {
			pr_err("%s: Unable to write to mmap, offset:%zd\n",
			       obj->mm.region->name, i * sizeof(x));
			err = -EFAULT;
			goto out_unmap;
		}
	}

	if (unfaultable) {
		if (err == -EFAULT)
			err = 0;
	} else {
		obj->flags &= ~I915_BO_ALLOC_GPU_ONLY;
		err = wc_check(obj);
	}
out_unmap:
	vm_munmap(addr, obj->base.size);
	return err;
}

#define IGT_MMAP_MIGRATE_TOPDOWN     (1 << 0)
#define IGT_MMAP_MIGRATE_FILL        (1 << 1)
#define IGT_MMAP_MIGRATE_EVICTABLE   (1 << 2)
#define IGT_MMAP_MIGRATE_UNFAULTABLE (1 << 3)
#define IGT_MMAP_MIGRATE_FAIL_GPU    (1 << 4)
static int __igt_mmap_migrate(struct intel_memory_region **placements,
			      int n_placements,
			      struct intel_memory_region *expected_mr,
			      unsigned int flags)
{
	struct drm_i915_private *i915 = placements[0]->i915;
	struct drm_i915_gem_object *obj;
	struct i915_request *rq = NULL;
	unsigned long addr;
	LIST_HEAD(objects);
	u64 offset;
	int err;

	obj = __i915_gem_object_create_user(i915, PAGE_SIZE,
					    placements,
					    n_placements);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (flags & IGT_MMAP_MIGRATE_TOPDOWN)
		obj->flags |= I915_BO_ALLOC_GPU_ONLY;

	err = __assign_mmap_offset(obj, I915_MMAP_TYPE_FIXED, &offset, NULL);
	if (err)
		goto out_put;

	/*
	 * This will eventually create a GEM context, due to opening dummy drm
	 * file, which needs a tiny amount of mappable device memory for the top
	 * level paging structures(and perhaps scratch), so make sure we
	 * allocate early, to avoid tears.
	 */
	addr = igt_mmap_offset(i915, offset, obj->base.size,
			       PROT_WRITE, MAP_SHARED);
	if (IS_ERR_VALUE(addr)) {
		err = addr;
		goto out_put;
	}

	if (flags & IGT_MMAP_MIGRATE_FILL) {
		err = igt_fill_mappable(placements[0], &objects);
		if (err)
			goto out_put;
	}

	err = i915_gem_object_lock(obj, NULL);
	if (err)
		goto out_put;

	err = i915_gem_object_pin_pages(obj);
	if (err) {
		i915_gem_object_unlock(obj);
		goto out_put;
	}

	err = intel_context_migrate_clear(to_gt(i915)->migrate.context, NULL,
					  obj->mm.pages->sgl, obj->pat_index,
					  i915_gem_object_is_lmem(obj),
					  expand32(POISON_INUSE), &rq);
	i915_gem_object_unpin_pages(obj);
	if (rq) {
		err = dma_resv_reserve_fences(obj->base.resv, 1);
		if (!err)
			dma_resv_add_fence(obj->base.resv, &rq->fence,
					   DMA_RESV_USAGE_KERNEL);
		i915_request_put(rq);
	}
	i915_gem_object_unlock(obj);
	if (err)
		goto out_put;

	if (flags & IGT_MMAP_MIGRATE_EVICTABLE)
		igt_make_evictable(&objects);

	if (flags & IGT_MMAP_MIGRATE_FAIL_GPU) {
		err = i915_gem_object_lock(obj, NULL);
		if (err)
			goto out_put;

		/*
		 * Ensure we only simulate the gpu failuire when faulting the
		 * pages.
		 */
		err = i915_gem_object_wait_moving_fence(obj, true);
		i915_gem_object_unlock(obj);
		if (err)
			goto out_put;
		i915_ttm_migrate_set_failure_modes(true, false);
	}

	err = ___igt_mmap_migrate(i915, obj, addr,
				  flags & IGT_MMAP_MIGRATE_UNFAULTABLE);

	if (!err && obj->mm.region != expected_mr) {
		pr_err("%s region mismatch %s\n", __func__, expected_mr->name);
		err = -EINVAL;
	}

	if (flags & IGT_MMAP_MIGRATE_FAIL_GPU) {
		struct intel_gt *gt;
		unsigned int id;

		i915_ttm_migrate_set_failure_modes(false, false);

		for_each_gt(gt, i915, id) {
			intel_wakeref_t wakeref;
			bool wedged;

			mutex_lock(&gt->reset.mutex);
			wedged = test_bit(I915_WEDGED, &gt->reset.flags);
			mutex_unlock(&gt->reset.mutex);
			if (!wedged) {
				pr_err("gt(%u) not wedged\n", id);
				err = -EINVAL;
				continue;
			}

			wakeref = intel_runtime_pm_get(gt->uncore->rpm);
			igt_global_reset_lock(gt);
			intel_gt_reset(gt, ALL_ENGINES, NULL);
			igt_global_reset_unlock(gt);
			intel_runtime_pm_put(gt->uncore->rpm, wakeref);
		}

		if (!i915_gem_object_has_unknown_state(obj)) {
			pr_err("object missing unknown_state\n");
			err = -EINVAL;
		}
	}

out_put:
	i915_gem_object_put(obj);
	igt_close_objects(i915, &objects);
	return err;
}

static int igt_mmap_migrate(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *system = i915->mm.regions[INTEL_REGION_SMEM];
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id) {
		struct intel_memory_region *mixed[] = { mr, system };
		struct intel_memory_region *single[] = { mr };
		struct ttm_resource_manager *man = mr->region_private;
		struct resource saved_io;
		int err;

		if (mr->private)
			continue;

		if (!resource_size(&mr->io))
			continue;

		/*
		 * For testing purposes let's force small BAR, if not already
		 * present.
		 */
		saved_io = mr->io;
		if (resource_size(&mr->io) == mr->total) {
			resource_size_t io_size = resource_size(&mr->io);

			io_size = rounddown_pow_of_two(io_size >> 1);
			if (io_size < PAGE_SIZE)
				continue;

			mr->io = DEFINE_RES_MEM(mr->io.start, io_size);
			i915_ttm_buddy_man_force_visible_size(man,
							      io_size >> PAGE_SHIFT);
		}

		/*
		 * Allocate in the mappable portion, should be no suprises here.
		 */
		err = __igt_mmap_migrate(mixed, ARRAY_SIZE(mixed), mr, 0);
		if (err)
			goto out_io_size;

		/*
		 * Allocate in the non-mappable portion, but force migrating to
		 * the mappable portion on fault (LMEM -> LMEM)
		 */
		err = __igt_mmap_migrate(single, ARRAY_SIZE(single), mr,
					 IGT_MMAP_MIGRATE_TOPDOWN |
					 IGT_MMAP_MIGRATE_FILL |
					 IGT_MMAP_MIGRATE_EVICTABLE);
		if (err)
			goto out_io_size;

		/*
		 * Allocate in the non-mappable portion, but force spilling into
		 * system memory on fault (LMEM -> SMEM)
		 */
		err = __igt_mmap_migrate(mixed, ARRAY_SIZE(mixed), system,
					 IGT_MMAP_MIGRATE_TOPDOWN |
					 IGT_MMAP_MIGRATE_FILL);
		if (err)
			goto out_io_size;

		/*
		 * Allocate in the non-mappable portion, but since the mappable
		 * portion is already full, and we can't spill to system memory,
		 * then we should expect the fault to fail.
		 */
		err = __igt_mmap_migrate(single, ARRAY_SIZE(single), mr,
					 IGT_MMAP_MIGRATE_TOPDOWN |
					 IGT_MMAP_MIGRATE_FILL |
					 IGT_MMAP_MIGRATE_UNFAULTABLE);
		if (err)
			goto out_io_size;

		/*
		 * Allocate in the non-mappable portion, but force migrating to
		 * the mappable portion on fault (LMEM -> LMEM). We then also
		 * simulate a gpu error when moving the pages when faulting the
		 * pages, which should result in wedging the gpu and returning
		 * SIGBUS in the fault handler, since we can't fallback to
		 * memcpy.
		 */
		err = __igt_mmap_migrate(single, ARRAY_SIZE(single), mr,
					 IGT_MMAP_MIGRATE_TOPDOWN |
					 IGT_MMAP_MIGRATE_FILL |
					 IGT_MMAP_MIGRATE_EVICTABLE |
					 IGT_MMAP_MIGRATE_FAIL_GPU |
					 IGT_MMAP_MIGRATE_UNFAULTABLE);
out_io_size:
		mr->io = saved_io;
		i915_ttm_buddy_man_force_visible_size(man,
						      resource_size(&mr->io) >> PAGE_SHIFT);
		if (err)
			return err;
	}

	return 0;
}

static const char *repr_mmap_type(enum i915_mmap_type type)
{
	switch (type) {
	case I915_MMAP_TYPE_GTT: return "gtt";
	case I915_MMAP_TYPE_WB: return "wb";
	case I915_MMAP_TYPE_WC: return "wc";
	case I915_MMAP_TYPE_UC: return "uc";
	case I915_MMAP_TYPE_FIXED: return "fixed";
	default: return "unknown";
	}
}

static bool can_access(struct drm_i915_gem_object *obj)
{
	bool access;

	i915_gem_object_lock(obj, NULL);
	access = i915_gem_object_has_struct_page(obj) ||
		i915_gem_object_has_iomem(obj);
	i915_gem_object_unlock(obj);

	return access;
}

static int __igt_mmap_access(struct drm_i915_private *i915,
			     struct drm_i915_gem_object *obj,
			     enum i915_mmap_type type)
{
	unsigned long __user *ptr;
	unsigned long A, B;
	unsigned long x, y;
	unsigned long addr;
	int err;
	u64 offset;

	memset(&A, 0xAA, sizeof(A));
	memset(&B, 0xBB, sizeof(B));

	if (!can_mmap(obj, type) || !can_access(obj))
		return 0;

	err = __assign_mmap_offset(obj, type, &offset, NULL);
	if (err)
		return err;

	addr = igt_mmap_offset(i915, offset, obj->base.size, PROT_WRITE, MAP_SHARED);
	if (IS_ERR_VALUE(addr))
		return addr;
	ptr = (unsigned long __user *)addr;

	err = __put_user(A, ptr);
	if (err) {
		pr_err("%s(%s): failed to write into user mmap\n",
		       obj->mm.region->name, repr_mmap_type(type));
		goto out_unmap;
	}

	intel_gt_flush_ggtt_writes(to_gt(i915));

	err = access_process_vm(current, addr, &x, sizeof(x), 0);
	if (err != sizeof(x)) {
		pr_err("%s(%s): access_process_vm() read failed\n",
		       obj->mm.region->name, repr_mmap_type(type));
		goto out_unmap;
	}

	err = access_process_vm(current, addr, &B, sizeof(B), FOLL_WRITE);
	if (err != sizeof(B)) {
		pr_err("%s(%s): access_process_vm() write failed\n",
		       obj->mm.region->name, repr_mmap_type(type));
		goto out_unmap;
	}

	intel_gt_flush_ggtt_writes(to_gt(i915));

	err = __get_user(y, ptr);
	if (err) {
		pr_err("%s(%s): failed to read from user mmap\n",
		       obj->mm.region->name, repr_mmap_type(type));
		goto out_unmap;
	}

	if (x != A || y != B) {
		pr_err("%s(%s): failed to read/write values, found (%lx, %lx)\n",
		       obj->mm.region->name, repr_mmap_type(type),
		       x, y);
		err = -EINVAL;
		goto out_unmap;
	}

out_unmap:
	vm_munmap(addr, obj->base.size);
	return err;
}

static int igt_mmap_access(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id) {
		struct drm_i915_gem_object *obj;
		int err;

		if (mr->private)
			continue;

		obj = __i915_gem_object_create_user(i915, PAGE_SIZE, &mr, 1);
		if (obj == ERR_PTR(-ENODEV))
			continue;

		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = __igt_mmap_access(i915, obj, I915_MMAP_TYPE_GTT);
		if (err == 0)
			err = __igt_mmap_access(i915, obj, I915_MMAP_TYPE_WB);
		if (err == 0)
			err = __igt_mmap_access(i915, obj, I915_MMAP_TYPE_WC);
		if (err == 0)
			err = __igt_mmap_access(i915, obj, I915_MMAP_TYPE_UC);
		if (err == 0)
			err = __igt_mmap_access(i915, obj, I915_MMAP_TYPE_FIXED);

		i915_gem_object_put(obj);
		if (err)
			return err;
	}

	return 0;
}

static int __igt_mmap_gpu(struct drm_i915_private *i915,
			  struct drm_i915_gem_object *obj,
			  enum i915_mmap_type type)
{
	struct intel_engine_cs *engine;
	unsigned long addr;
	u32 __user *ux;
	u32 bbe;
	int err;
	u64 offset;

	/*
	 * Verify that the mmap access into the backing store aligns with
	 * that of the GPU, i.e. that mmap is indeed writing into the same
	 * page as being read by the GPU.
	 */

	if (!can_mmap(obj, type))
		return 0;

	err = wc_set(obj);
	if (err == -ENXIO)
		err = gtt_set(obj);
	if (err)
		return err;

	err = __assign_mmap_offset(obj, type, &offset, NULL);
	if (err)
		return err;

	addr = igt_mmap_offset(i915, offset, obj->base.size, PROT_WRITE, MAP_SHARED);
	if (IS_ERR_VALUE(addr))
		return addr;

	ux = u64_to_user_ptr((u64)addr);
	bbe = MI_BATCH_BUFFER_END;
	if (put_user(bbe, ux)) {
		pr_err("%s: Unable to write to mmap\n", obj->mm.region->name);
		err = -EFAULT;
		goto out_unmap;
	}

	if (type == I915_MMAP_TYPE_GTT)
		intel_gt_flush_ggtt_writes(to_gt(i915));

	for_each_uabi_engine(engine, i915) {
		struct i915_request *rq;
		struct i915_vma *vma;
		struct i915_gem_ww_ctx ww;

		vma = i915_vma_instance(obj, engine->kernel_context->vm, NULL);
		if (IS_ERR(vma)) {
			err = PTR_ERR(vma);
			goto out_unmap;
		}

		i915_gem_ww_ctx_init(&ww, false);
retry:
		err = i915_gem_object_lock(obj, &ww);
		if (!err)
			err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_USER);
		if (err)
			goto out_ww;

		rq = i915_request_create(engine->kernel_context);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_unpin;
		}

		err = i915_vma_move_to_active(vma, rq, 0);

		err = engine->emit_bb_start(rq, i915_vma_offset(vma), 0, 0);
		i915_request_get(rq);
		i915_request_add(rq);

		if (i915_request_wait(rq, 0, HZ / 5) < 0) {
			struct drm_printer p =
				drm_info_printer(engine->i915->drm.dev);

			pr_err("%s(%s, %s): Failed to execute batch\n",
			       __func__, engine->name, obj->mm.region->name);
			intel_engine_dump(engine, &p,
					  "%s\n", engine->name);

			intel_gt_set_wedged(engine->gt);
			err = -EIO;
		}
		i915_request_put(rq);

out_unpin:
		i915_vma_unpin(vma);
out_ww:
		if (err == -EDEADLK) {
			err = i915_gem_ww_ctx_backoff(&ww);
			if (!err)
				goto retry;
		}
		i915_gem_ww_ctx_fini(&ww);
		if (err)
			goto out_unmap;
	}

out_unmap:
	vm_munmap(addr, obj->base.size);
	return err;
}

static int igt_mmap_gpu(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id) {
		struct drm_i915_gem_object *obj;
		int err;

		if (mr->private)
			continue;

		obj = __i915_gem_object_create_user(i915, PAGE_SIZE, &mr, 1);
		if (obj == ERR_PTR(-ENODEV))
			continue;

		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = __igt_mmap_gpu(i915, obj, I915_MMAP_TYPE_GTT);
		if (err == 0)
			err = __igt_mmap_gpu(i915, obj, I915_MMAP_TYPE_WC);
		if (err == 0)
			err = __igt_mmap_gpu(i915, obj, I915_MMAP_TYPE_FIXED);

		i915_gem_object_put(obj);
		if (err)
			return err;
	}

	return 0;
}

static int check_present_pte(pte_t *pte, unsigned long addr, void *data)
{
	pte_t ptent = ptep_get(pte);

	if (!pte_present(ptent) || pte_none(ptent)) {
		pr_err("missing PTE:%lx\n",
		       (addr - (unsigned long)data) >> PAGE_SHIFT);
		return -EINVAL;
	}

	return 0;
}

static int check_absent_pte(pte_t *pte, unsigned long addr, void *data)
{
	pte_t ptent = ptep_get(pte);

	if (pte_present(ptent) && !pte_none(ptent)) {
		pr_err("present PTE:%lx; expected to be revoked\n",
		       (addr - (unsigned long)data) >> PAGE_SHIFT);
		return -EINVAL;
	}

	return 0;
}

static int check_present(unsigned long addr, unsigned long len)
{
	return apply_to_page_range(current->mm, addr, len,
				   check_present_pte, (void *)addr);
}

static int check_absent(unsigned long addr, unsigned long len)
{
	return apply_to_page_range(current->mm, addr, len,
				   check_absent_pte, (void *)addr);
}

static int prefault_range(u64 start, u64 len)
{
	const char __user *addr, *end;
	char __maybe_unused c;
	int err;

	addr = u64_to_user_ptr(start);
	end = addr + len;

	for (; addr < end; addr += PAGE_SIZE) {
		err = __get_user(c, addr);
		if (err)
			return err;
	}

	return __get_user(c, end - 1);
}

static int __igt_mmap_revoke(struct drm_i915_private *i915,
			     struct drm_i915_gem_object *obj,
			     enum i915_mmap_type type)
{
	unsigned long addr;
	int err;
	u64 offset;

	if (!can_mmap(obj, type))
		return 0;

	err = __assign_mmap_offset(obj, type, &offset, NULL);
	if (err)
		return err;

	addr = igt_mmap_offset(i915, offset, obj->base.size, PROT_WRITE, MAP_SHARED);
	if (IS_ERR_VALUE(addr))
		return addr;

	err = prefault_range(addr, obj->base.size);
	if (err)
		goto out_unmap;

	err = check_present(addr, obj->base.size);
	if (err) {
		pr_err("%s: was not present\n", obj->mm.region->name);
		goto out_unmap;
	}

	/*
	 * After unbinding the object from the GGTT, its address may be reused
	 * for other objects. Ergo we have to revoke the previous mmap PTE
	 * access as it no longer points to the same object.
	 */
	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE);
	i915_gem_object_unlock(obj);
	if (err) {
		pr_err("Failed to unbind object!\n");
		goto out_unmap;
	}

	if (type != I915_MMAP_TYPE_GTT) {
		i915_gem_object_lock(obj, NULL);
		__i915_gem_object_put_pages(obj);
		i915_gem_object_unlock(obj);
		if (i915_gem_object_has_pages(obj)) {
			pr_err("Failed to put-pages object!\n");
			err = -EINVAL;
			goto out_unmap;
		}
	}

	err = check_absent(addr, obj->base.size);
	if (err) {
		pr_err("%s: was not absent\n", obj->mm.region->name);
		goto out_unmap;
	}

out_unmap:
	vm_munmap(addr, obj->base.size);
	return err;
}

static int igt_mmap_revoke(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_memory_region *mr;
	enum intel_region_id id;

	for_each_memory_region(mr, i915, id) {
		struct drm_i915_gem_object *obj;
		int err;

		if (mr->private)
			continue;

		obj = __i915_gem_object_create_user(i915, PAGE_SIZE, &mr, 1);
		if (obj == ERR_PTR(-ENODEV))
			continue;

		if (IS_ERR(obj))
			return PTR_ERR(obj);

		err = __igt_mmap_revoke(i915, obj, I915_MMAP_TYPE_GTT);
		if (err == 0)
			err = __igt_mmap_revoke(i915, obj, I915_MMAP_TYPE_WC);
		if (err == 0)
			err = __igt_mmap_revoke(i915, obj, I915_MMAP_TYPE_FIXED);

		i915_gem_object_put(obj);
		if (err)
			return err;
	}

	return 0;
}

int i915_gem_mman_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_partial_tiling),
		SUBTEST(igt_smoke_tiling),
		SUBTEST(igt_mmap_offset_exhaustion),
		SUBTEST(igt_mmap),
		SUBTEST(igt_mmap_migrate),
		SUBTEST(igt_mmap_access),
		SUBTEST(igt_mmap_revoke),
		SUBTEST(igt_mmap_gpu),
	};

	return i915_live_subtests(tests, i915);
}
