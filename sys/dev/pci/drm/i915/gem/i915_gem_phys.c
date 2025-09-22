/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/highmem.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>

#include <drm/drm_cache.h>
#include <drm/drm_drv.h>	/* for drm_dmamem_alloc() */

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_gem_object_frontbuffer.h"
#include "i915_gem_region.h"
#include "i915_gem_tiling.h"
#include "i915_scatterlist.h"

static int i915_gem_object_get_pages_phys(struct drm_i915_gem_object *obj)
{
#ifdef __linux__
	struct address_space *mapping = obj->base.filp->f_mapping;
#else
	struct drm_dmamem *dmah;
	int flags = 0;
#endif
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct scatterlist *sg;
	struct sg_table *st;
	dma_addr_t dma;
	void *vaddr;
	void *dst;
	int i;

	/* Contiguous chunk, with a single scatterlist element */
	if (overflows_type(obj->base.size, sg->length))
		return -E2BIG;

	if (GEM_WARN_ON(i915_gem_object_needs_bit17_swizzle(obj)))
		return -EINVAL;

	/*
	 * Always aligning to the object size, allows a single allocation
	 * to handle all possible callers, and given typical object sizes,
	 * the alignment of the buddy allocation will naturally match.
	 */
#ifdef __linux__
	vaddr = dma_alloc_coherent(obj->base.dev->dev,
				   roundup_pow_of_two(obj->base.size),
				   &dma, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;
#else
	dmah = drm_dmamem_alloc(i915->dmat,
	    roundup_pow_of_two(obj->base.size),
	    PAGE_SIZE, 1,
	    roundup_pow_of_two(obj->base.size), flags, 0);
	if (dmah == NULL)
		return -ENOMEM;
	dma = dmah->map->dm_segs[0].ds_addr;
	vaddr = dmah->kva;
#endif

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto err_pci;

	if (sg_alloc_table(st, 1, GFP_KERNEL))
		goto err_st;

	sg = st->sgl;
	sg->offset = 0;
	sg->length = obj->base.size;

#ifdef __linux__
	sg_assign_page(sg, (struct page *)vaddr);
#else
	sg_assign_page(sg, (struct vm_page *)dmah);
#endif
	sg_dma_address(sg) = dma;
	sg_dma_len(sg) = obj->base.size;

	dst = vaddr;
	for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
		struct vm_page *page;

#ifdef __linux__
		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page))
			goto err_st;
#else
		struct pglist plist;
		TAILQ_INIT(&plist);
		if (uvm_obj_wire(obj->base.uao, i * PAGE_SIZE,
		    (i + 1) * PAGE_SIZE, &plist))
			goto err_st;
		page = TAILQ_FIRST(&plist);
#endif

		memcpy_from_page(dst, page, 0, PAGE_SIZE);
		drm_clflush_virt_range(dst, PAGE_SIZE);

#ifdef __linux__
		put_page(page);
#else
		uvm_obj_unwire(obj->base.uao, i * PAGE_SIZE,
		    (i + 1) * PAGE_SIZE);
#endif
		dst += PAGE_SIZE;
	}

	intel_gt_chipset_flush(to_gt(i915));

	/* We're no longer struct page backed */
	obj->mem_flags &= ~I915_BO_FLAG_STRUCT_PAGE;
	__i915_gem_object_set_pages(obj, st);

	return 0;

err_st:
	kfree(st);
err_pci:
#ifdef __linux__
	dma_free_coherent(obj->base.dev->dev,
			  roundup_pow_of_two(obj->base.size),
			  vaddr, dma);
#else
	drm_dmamem_free(i915->dmat, dmah);
#endif
	return -ENOMEM;
}

void
i915_gem_object_put_pages_phys(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	dma_addr_t dma = sg_dma_address(pages->sgl);
#ifdef __linux__
	void *vaddr = sg_page(pages->sgl);
#else
	struct drm_dmamem *dmah = (void *)sg_page(pages->sgl);
	void *vaddr = dmah->kva;
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
#endif

	__i915_gem_object_release_shmem(obj, pages, false);

	if (obj->mm.dirty) {
#ifdef __linux__
		struct address_space *mapping = obj->base.filp->f_mapping;
#endif
		void *src = vaddr;
		int i;

		for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
			struct vm_page *page;

#ifdef __linux__
			page = shmem_read_mapping_page(mapping, i);
			if (IS_ERR(page))
				continue;
#else
			struct pglist plist;
			TAILQ_INIT(&plist);
			if (uvm_obj_wire(obj->base.uao, i * PAGE_SIZE,
			    (i + 1) * PAGE_SIZE, &plist))
				continue;
			page = TAILQ_FIRST(&plist);
#endif

			drm_clflush_virt_range(src, PAGE_SIZE);
			memcpy_to_page(page, 0, src, PAGE_SIZE);

			set_page_dirty(page);
#ifdef __linux__
			if (obj->mm.madv == I915_MADV_WILLNEED)
				mark_page_accessed(page);
			put_page(page);
#else
			uvm_obj_unwire(obj->base.uao, i * PAGE_SIZE,
				      (i + 1) * PAGE_SIZE);
#endif

			src += PAGE_SIZE;
		}
		obj->mm.dirty = false;
	}

	sg_free_table(pages);
	kfree(pages);

#ifdef __linux__
	dma_free_coherent(obj->base.dev->dev,
			  roundup_pow_of_two(obj->base.size),
			  vaddr, dma);
#else
	drm_dmamem_free(i915->dmat, dmah);
#endif
}

int i915_gem_object_pwrite_phys(struct drm_i915_gem_object *obj,
				const struct drm_i915_gem_pwrite *args)
{
#ifdef __linux__
	void *vaddr = sg_page(obj->mm.pages->sgl) + args->offset;
#else
	struct drm_dmamem *dmah = (void *)sg_page(obj->mm.pages->sgl);
	void *vaddr = dmah->kva + args->offset;
#endif
	char __user *user_data = u64_to_user_ptr(args->data_ptr);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	int err;

	err = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT);
	if (err)
		return err;

	/*
	 * We manually control the domain here and pretend that it
	 * remains coherent i.e. in the GTT domain, like shmem_pwrite.
	 */
	i915_gem_object_invalidate_frontbuffer(obj, ORIGIN_CPU);

	if (copy_from_user(vaddr, user_data, args->size))
		return -EFAULT;

	drm_clflush_virt_range(vaddr, args->size);
	intel_gt_chipset_flush(to_gt(i915));

	i915_gem_object_flush_frontbuffer(obj, ORIGIN_CPU);
	return 0;
}

int i915_gem_object_pread_phys(struct drm_i915_gem_object *obj,
			       const struct drm_i915_gem_pread *args)
{
#ifdef __linux__
	void *vaddr = sg_page(obj->mm.pages->sgl) + args->offset;
#else
	struct drm_dmamem *dmah = (void *)sg_page(obj->mm.pages->sgl);
	void *vaddr = dmah->kva + args->offset;
#endif
	char __user *user_data = u64_to_user_ptr(args->data_ptr);
	int err;

	err = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
	if (err)
		return err;

	drm_clflush_virt_range(vaddr, args->size);
	if (copy_to_user(user_data, vaddr, args->size))
		return -EFAULT;

	return 0;
}

static int i915_gem_object_shmem_to_phys(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;
	int err;

	pages = __i915_gem_object_unset_pages(obj);

	err = i915_gem_object_get_pages_phys(obj);
	if (err)
		goto err_xfer;

	/* Perma-pin (until release) the physical set of pages */
	__i915_gem_object_pin_pages(obj);

	if (!IS_ERR_OR_NULL(pages))
		i915_gem_object_put_pages_shmem(obj, pages);

	i915_gem_object_release_memory_region(obj);
	return 0;

err_xfer:
	if (!IS_ERR_OR_NULL(pages))
		__i915_gem_object_set_pages(obj, pages);
	return err;
}

int i915_gem_object_attach_phys(struct drm_i915_gem_object *obj, int align)
{
	int err;

	assert_object_held(obj);

	if (align > obj->base.size)
		return -EINVAL;

	if (!i915_gem_object_is_shmem(obj))
		return -EINVAL;

	if (!i915_gem_object_has_struct_page(obj))
		return 0;

	err = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE);
	if (err)
		return err;

	if (obj->mm.madv != I915_MADV_WILLNEED)
		return -EFAULT;

	if (i915_gem_object_has_tiling_quirk(obj))
		return -EFAULT;

	if (obj->mm.mapping || i915_gem_object_has_pinned_pages(obj))
		return -EBUSY;

	if (unlikely(obj->mm.madv != I915_MADV_WILLNEED)) {
		drm_dbg(obj->base.dev,
			"Attempting to obtain a purgeable object\n");
		return -EFAULT;
	}

	return i915_gem_object_shmem_to_phys(obj);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_phys.c"
#endif
