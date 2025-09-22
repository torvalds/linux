/*
 * Copyright 2009 Jerome Glisse.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 *    Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *    Dave Airlie
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>

#include <drm/drm_drv.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_cache.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_vram_mgr.h"

/**
 * DOC: amdgpu_object
 *
 * This defines the interfaces to operate on an &amdgpu_bo buffer object which
 * represents memory used by driver (VRAM, system memory, etc.). The driver
 * provides DRM/GEM APIs to userspace. DRM/GEM APIs then use these interfaces
 * to create/destroy/set buffer object which are then managed by the kernel TTM
 * memory manager.
 * The interfaces are also used internally by kernel clients, including gfx,
 * uvd, etc. for kernel managed allocations used by the GPU.
 *
 */

static void amdgpu_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct amdgpu_bo *bo = ttm_to_amdgpu_bo(tbo);

	amdgpu_bo_kunmap(bo);

	if (bo->tbo.base.import_attach)
		drm_prime_gem_destroy(&bo->tbo.base, bo->tbo.sg);
	drm_gem_object_release(&bo->tbo.base);
	amdgpu_bo_unref(&bo->parent);
	kvfree(bo);
}

static void amdgpu_bo_user_destroy(struct ttm_buffer_object *tbo)
{
	struct amdgpu_bo *bo = ttm_to_amdgpu_bo(tbo);
	struct amdgpu_bo_user *ubo;

	ubo = to_amdgpu_bo_user(bo);
	kfree(ubo->metadata);
	amdgpu_bo_destroy(tbo);
}

/**
 * amdgpu_bo_is_amdgpu_bo - check if the buffer object is an &amdgpu_bo
 * @bo: buffer object to be checked
 *
 * Uses destroy function associated with the object to determine if this is
 * an &amdgpu_bo.
 *
 * Returns:
 * true if the object belongs to &amdgpu_bo, false if not.
 */
bool amdgpu_bo_is_amdgpu_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &amdgpu_bo_destroy ||
	    bo->destroy == &amdgpu_bo_user_destroy)
		return true;

	return false;
}

/**
 * amdgpu_bo_placement_from_domain - set buffer's placement
 * @abo: &amdgpu_bo buffer object whose placement is to be set
 * @domain: requested domain
 *
 * Sets buffer's placement according to requested domain and the buffer's
 * flags.
 */
void amdgpu_bo_placement_from_domain(struct amdgpu_bo *abo, u32 domain)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(abo->tbo.bdev);
	struct ttm_placement *placement = &abo->placement;
	struct ttm_place *places = abo->placements;
	u64 flags = abo->flags;
	u32 c = 0;

	if (domain & AMDGPU_GEM_DOMAIN_VRAM) {
		unsigned int visible_pfn = adev->gmc.visible_vram_size >> PAGE_SHIFT;
		int8_t mem_id = KFD_XCP_MEM_ID(adev, abo->xcp_id);

		if (adev->gmc.mem_partitions && mem_id >= 0) {
			places[c].fpfn = adev->gmc.mem_partitions[mem_id].range.fpfn;
			/*
			 * memory partition range lpfn is inclusive start + size - 1
			 * TTM place lpfn is exclusive start + size
			 */
			places[c].lpfn = adev->gmc.mem_partitions[mem_id].range.lpfn + 1;
		} else {
			places[c].fpfn = 0;
			places[c].lpfn = 0;
		}
		places[c].mem_type = TTM_PL_VRAM;
		places[c].flags = 0;

		if (flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			places[c].lpfn = min_not_zero(places[c].lpfn, visible_pfn);
		else
			places[c].flags |= TTM_PL_FLAG_TOPDOWN;

		if (abo->tbo.type == ttm_bo_type_kernel &&
		    flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS)
			places[c].flags |= TTM_PL_FLAG_CONTIGUOUS;

		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_DOORBELL) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_DOORBELL;
		places[c].flags = 0;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GTT) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type =
			abo->flags & AMDGPU_GEM_CREATE_PREEMPTIBLE ?
			AMDGPU_PL_PREEMPT : TTM_PL_TT;
		places[c].flags = 0;
		/*
		 * When GTT is just an alternative to VRAM make sure that we
		 * only use it as fallback and still try to fill up VRAM first.
		 */
		if (abo->tbo.resource && !(adev->flags & AMD_IS_APU) &&
		    domain & abo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM)
			places[c].flags |= TTM_PL_FLAG_FALLBACK;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_CPU) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_SYSTEM;
		places[c].flags = 0;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GDS) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_GDS;
		places[c].flags = 0;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GWS) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_GWS;
		places[c].flags = 0;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_OA) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_OA;
		places[c].flags = 0;
		c++;
	}

	if (!c) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_SYSTEM;
		places[c].flags = 0;
		c++;
	}

	BUG_ON(c > AMDGPU_BO_MAX_PLACEMENTS);

	placement->num_placement = c;
	placement->placement = places;
}

/**
 * amdgpu_bo_create_reserved - create reserved BO for kernel use
 *
 * @adev: amdgpu device object
 * @size: size for the new BO
 * @align: alignment for the new BO
 * @domain: where to place it
 * @bo_ptr: used to initialize BOs in structures
 * @gpu_addr: GPU addr of the pinned BO
 * @cpu_addr: optional CPU address mapping
 *
 * Allocates and pins a BO for kernel internal use, and returns it still
 * reserved.
 *
 * Note: For bo_ptr new BO is only created if bo_ptr points to NULL.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_reserved(struct amdgpu_device *adev,
			      unsigned long size, int align,
			      u32 domain, struct amdgpu_bo **bo_ptr,
			      u64 *gpu_addr, void **cpu_addr)
{
	struct amdgpu_bo_param bp;
	bool free = false;
	int r;

	if (!size) {
		amdgpu_bo_unref(bo_ptr);
		return 0;
	}

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = align;
	bp.domain = domain;
	bp.flags = cpu_addr ? AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED
		: AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	bp.flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	if (!*bo_ptr) {
		r = amdgpu_bo_create(adev, &bp, bo_ptr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to allocate kernel bo\n",
				r);
			return r;
		}
		free = true;
	}

	r = amdgpu_bo_reserve(*bo_ptr, false);
	if (r) {
		dev_err(adev->dev, "(%d) failed to reserve kernel bo\n", r);
		goto error_free;
	}

	r = amdgpu_bo_pin(*bo_ptr, domain);
	if (r) {
		dev_err(adev->dev, "(%d) kernel bo pin failed\n", r);
		goto error_unreserve;
	}

	r = amdgpu_ttm_alloc_gart(&(*bo_ptr)->tbo);
	if (r) {
		dev_err(adev->dev, "%p bind failed\n", *bo_ptr);
		goto error_unpin;
	}

	if (gpu_addr)
		*gpu_addr = amdgpu_bo_gpu_offset(*bo_ptr);

	if (cpu_addr) {
		r = amdgpu_bo_kmap(*bo_ptr, cpu_addr);
		if (r) {
			dev_err(adev->dev, "(%d) kernel bo map failed\n", r);
			goto error_unpin;
		}
	}

	return 0;

error_unpin:
	amdgpu_bo_unpin(*bo_ptr);
error_unreserve:
	amdgpu_bo_unreserve(*bo_ptr);

error_free:
	if (free)
		amdgpu_bo_unref(bo_ptr);

	return r;
}

/**
 * amdgpu_bo_create_kernel - create BO for kernel use
 *
 * @adev: amdgpu device object
 * @size: size for the new BO
 * @align: alignment for the new BO
 * @domain: where to place it
 * @bo_ptr:  used to initialize BOs in structures
 * @gpu_addr: GPU addr of the pinned BO
 * @cpu_addr: optional CPU address mapping
 *
 * Allocates and pins a BO for kernel internal use.
 *
 * Note: For bo_ptr new BO is only created if bo_ptr points to NULL.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_kernel(struct amdgpu_device *adev,
			    unsigned long size, int align,
			    u32 domain, struct amdgpu_bo **bo_ptr,
			    u64 *gpu_addr, void **cpu_addr)
{
	int r;

	r = amdgpu_bo_create_reserved(adev, size, align, domain, bo_ptr,
				      gpu_addr, cpu_addr);

	if (r)
		return r;

	if (*bo_ptr)
		amdgpu_bo_unreserve(*bo_ptr);

	return 0;
}

/**
 * amdgpu_bo_create_kernel_at - create BO for kernel use at specific location
 *
 * @adev: amdgpu device object
 * @offset: offset of the BO
 * @size: size of the BO
 * @bo_ptr:  used to initialize BOs in structures
 * @cpu_addr: optional CPU address mapping
 *
 * Creates a kernel BO at a specific offset in VRAM.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_kernel_at(struct amdgpu_device *adev,
			       uint64_t offset, uint64_t size,
			       struct amdgpu_bo **bo_ptr, void **cpu_addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	unsigned int i;
	int r;

	offset &= LINUX_PAGE_MASK;
	size = ALIGN(size, PAGE_SIZE);

	r = amdgpu_bo_create_reserved(adev, size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM, bo_ptr, NULL,
				      cpu_addr);
	if (r)
		return r;

	if ((*bo_ptr) == NULL)
		return 0;

	/*
	 * Remove the original mem node and create a new one at the request
	 * position.
	 */
	if (cpu_addr)
		amdgpu_bo_kunmap(*bo_ptr);

	ttm_resource_free(&(*bo_ptr)->tbo, &(*bo_ptr)->tbo.resource);

	for (i = 0; i < (*bo_ptr)->placement.num_placement; ++i) {
		(*bo_ptr)->placements[i].fpfn = offset >> PAGE_SHIFT;
		(*bo_ptr)->placements[i].lpfn = (offset + size) >> PAGE_SHIFT;
	}
	r = ttm_bo_mem_space(&(*bo_ptr)->tbo, &(*bo_ptr)->placement,
			     &(*bo_ptr)->tbo.resource, &ctx);
	if (r)
		goto error;

	if (cpu_addr) {
		r = amdgpu_bo_kmap(*bo_ptr, cpu_addr);
		if (r)
			goto error;
	}

	amdgpu_bo_unreserve(*bo_ptr);
	return 0;

error:
	amdgpu_bo_unreserve(*bo_ptr);
	amdgpu_bo_unref(bo_ptr);
	return r;
}

/**
 * amdgpu_bo_free_kernel - free BO for kernel use
 *
 * @bo: amdgpu BO to free
 * @gpu_addr: pointer to where the BO's GPU memory space address was stored
 * @cpu_addr: pointer to where the BO's CPU memory space address was stored
 *
 * unmaps and unpin a BO for kernel internal use.
 */
void amdgpu_bo_free_kernel(struct amdgpu_bo **bo, u64 *gpu_addr,
			   void **cpu_addr)
{
	if (*bo == NULL)
		return;

	WARN_ON(amdgpu_ttm_adev((*bo)->tbo.bdev)->in_suspend);

	if (likely(amdgpu_bo_reserve(*bo, true) == 0)) {
		if (cpu_addr)
			amdgpu_bo_kunmap(*bo);

		amdgpu_bo_unpin(*bo);
		amdgpu_bo_unreserve(*bo);
	}
	amdgpu_bo_unref(bo);

	if (gpu_addr)
		*gpu_addr = 0;

	if (cpu_addr)
		*cpu_addr = NULL;
}

/* Validate bo size is bit bigger than the request domain */
static bool amdgpu_bo_validate_size(struct amdgpu_device *adev,
					  unsigned long size, u32 domain)
{
	struct ttm_resource_manager *man = NULL;

	/*
	 * If GTT is part of requested domains the check must succeed to
	 * allow fall back to GTT.
	 */
	if (domain & AMDGPU_GEM_DOMAIN_GTT)
		man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);
	else if (domain & AMDGPU_GEM_DOMAIN_VRAM)
		man = ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM);
	else
		return true;

	if (!man) {
		if (domain & AMDGPU_GEM_DOMAIN_GTT)
			WARN_ON_ONCE("GTT domain requested but GTT mem manager uninitialized");
		return false;
	}

	/* TODO add more domains checks, such as AMDGPU_GEM_DOMAIN_CPU, _DOMAIN_DOORBELL */
	if (size < man->size)
		return true;

	DRM_DEBUG("BO size %lu > total memory in domain: %llu\n", size, man->size);
	return false;
}

bool amdgpu_bo_support_uswc(u64 bo_flags)
{

#ifdef CONFIG_X86_32
	/* XXX: Write-combined CPU mappings of GTT seem broken on 32-bit
	 * See https://bugs.freedesktop.org/show_bug.cgi?id=84627
	 */
	return false;
#elif defined(CONFIG_X86) && !defined(CONFIG_X86_PAT)
	/* Don't try to enable write-combining when it can't work, or things
	 * may be slow
	 * See https://bugs.freedesktop.org/show_bug.cgi?id=88758
	 */

#ifndef CONFIG_COMPILE_TEST
#warning Please enable CONFIG_MTRR and CONFIG_X86_PAT for better performance \
	 thanks to write-combining
#endif

	if (bo_flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC)
		DRM_INFO_ONCE("Please enable CONFIG_MTRR and CONFIG_X86_PAT for "
			      "better performance thanks to write-combining\n");
	return false;
#else
	/* For architectures that don't support WC memory,
	 * mask out the WC flag from the BO
	 */
	if (!drm_arch_can_wc_memory())
		return false;

	return true;
#endif
}

/**
 * amdgpu_bo_create - create an &amdgpu_bo buffer object
 * @adev: amdgpu device object
 * @bp: parameters to be used for the buffer object
 * @bo_ptr: pointer to the buffer object pointer
 *
 * Creates an &amdgpu_bo buffer object.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_create(struct amdgpu_device *adev,
			       struct amdgpu_bo_param *bp,
			       struct amdgpu_bo **bo_ptr)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = (bp->type != ttm_bo_type_kernel),
		.no_wait_gpu = bp->no_wait_gpu,
		/* We opt to avoid OOM on system pages allocations */
		.gfp_retry_mayfail = true,
		.allow_res_evict = bp->type != ttm_bo_type_kernel,
		.resv = bp->resv
	};
	struct amdgpu_bo *bo;
	unsigned long page_align, size = bp->size;
	int r;

	/* Note that GDS/GWS/OA allocates 1 page per byte/resource. */
	if (bp->domain & (AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA)) {
		/* GWS and OA don't need any alignment. */
		page_align = bp->byte_align;
		size <<= PAGE_SHIFT;

	} else if (bp->domain & AMDGPU_GEM_DOMAIN_GDS) {
		/* Both size and alignment must be a multiple of 4. */
		page_align = ALIGN(bp->byte_align, 4);
		size = ALIGN(size, 4) << PAGE_SHIFT;
	} else {
		/* Memory should be aligned at least to a page size. */
		page_align = ALIGN(bp->byte_align, PAGE_SIZE) >> PAGE_SHIFT;
		size = ALIGN(size, PAGE_SIZE);
	}

	if (!amdgpu_bo_validate_size(adev, size, bp->domain))
		return -ENOMEM;

	BUG_ON(bp->bo_ptr_size < sizeof(struct amdgpu_bo));

	*bo_ptr = NULL;
	bo = kvzalloc(bp->bo_ptr_size, GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	drm_gem_private_object_init(adev_to_drm(adev), &bo->tbo.base, size);
	bo->tbo.base.funcs = &amdgpu_gem_object_funcs;
	bo->vm_bo = NULL;
	bo->preferred_domains = bp->preferred_domain ? bp->preferred_domain :
		bp->domain;
	bo->allowed_domains = bo->preferred_domains;
	if (bp->type != ttm_bo_type_kernel &&
	    !(bp->flags & AMDGPU_GEM_CREATE_DISCARDABLE) &&
	    bo->allowed_domains == AMDGPU_GEM_DOMAIN_VRAM)
		bo->allowed_domains |= AMDGPU_GEM_DOMAIN_GTT;

	bo->flags = bp->flags;

	if (adev->gmc.mem_partitions)
		/* For GPUs with spatial partitioning, bo->xcp_id=-1 means any partition */
		bo->xcp_id = bp->xcp_id_plus1 - 1;
	else
		/* For GPUs without spatial partitioning */
		bo->xcp_id = 0;

	if (!amdgpu_bo_support_uswc(bo->flags))
		bo->flags &= ~AMDGPU_GEM_CREATE_CPU_GTT_USWC;

	bo->tbo.bdev = &adev->mman.bdev;
	if (bp->domain & (AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA |
			  AMDGPU_GEM_DOMAIN_GDS))
		amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
	else
		amdgpu_bo_placement_from_domain(bo, bp->domain);
	if (bp->type == ttm_bo_type_kernel)
		bo->tbo.priority = 2;
	else if (!(bp->flags & AMDGPU_GEM_CREATE_DISCARDABLE))
		bo->tbo.priority = 1;

	if (!bp->destroy)
		bp->destroy = &amdgpu_bo_destroy;

	r = ttm_bo_init_reserved(&adev->mman.bdev, &bo->tbo, bp->type,
				 &bo->placement, page_align, &ctx,  NULL,
				 bp->resv, bp->destroy);
	if (unlikely(r != 0))
		return r;

	if (!amdgpu_gmc_vram_full_visible(&adev->gmc) &&
	    amdgpu_res_cpu_visible(adev, bo->tbo.resource))
		amdgpu_cs_report_moved_bytes(adev, ctx.bytes_moved,
					     ctx.bytes_moved);
	else
		amdgpu_cs_report_moved_bytes(adev, ctx.bytes_moved, 0);

	if (bp->flags & AMDGPU_GEM_CREATE_VRAM_CLEARED &&
	    bo->tbo.resource->mem_type == TTM_PL_VRAM) {
		struct dma_fence *fence;

		r = amdgpu_ttm_clear_buffer(bo, bo->tbo.base.resv, &fence);
		if (unlikely(r))
			goto fail_unreserve;

		dma_resv_add_fence(bo->tbo.base.resv, fence,
				   DMA_RESV_USAGE_KERNEL);
		dma_fence_put(fence);
	}
	if (!bp->resv)
		amdgpu_bo_unreserve(bo);
	*bo_ptr = bo;

	trace_amdgpu_bo_create(bo);

	/* Treat CPU_ACCESS_REQUIRED only as a hint if given by UMD */
	if (bp->type == ttm_bo_type_device)
		bo->flags &= ~AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	return 0;

fail_unreserve:
	if (!bp->resv)
		dma_resv_unlock(bo->tbo.base.resv);
	amdgpu_bo_unref(&bo);
	return r;
}

/**
 * amdgpu_bo_create_user - create an &amdgpu_bo_user buffer object
 * @adev: amdgpu device object
 * @bp: parameters to be used for the buffer object
 * @ubo_ptr: pointer to the buffer object pointer
 *
 * Create a BO to be used by user application;
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */

int amdgpu_bo_create_user(struct amdgpu_device *adev,
			  struct amdgpu_bo_param *bp,
			  struct amdgpu_bo_user **ubo_ptr)
{
	struct amdgpu_bo *bo_ptr;
	int r;

	bp->bo_ptr_size = sizeof(struct amdgpu_bo_user);
	bp->destroy = &amdgpu_bo_user_destroy;
	r = amdgpu_bo_create(adev, bp, &bo_ptr);
	if (r)
		return r;

	*ubo_ptr = to_amdgpu_bo_user(bo_ptr);
	return r;
}

/**
 * amdgpu_bo_create_vm - create an &amdgpu_bo_vm buffer object
 * @adev: amdgpu device object
 * @bp: parameters to be used for the buffer object
 * @vmbo_ptr: pointer to the buffer object pointer
 *
 * Create a BO to be for GPUVM.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */

int amdgpu_bo_create_vm(struct amdgpu_device *adev,
			struct amdgpu_bo_param *bp,
			struct amdgpu_bo_vm **vmbo_ptr)
{
	struct amdgpu_bo *bo_ptr;
	int r;

	/* bo_ptr_size will be determined by the caller and it depends on
	 * num of amdgpu_vm_pt entries.
	 */
	BUG_ON(bp->bo_ptr_size < sizeof(struct amdgpu_bo_vm));
	r = amdgpu_bo_create(adev, bp, &bo_ptr);
	if (r)
		return r;

	*vmbo_ptr = to_amdgpu_bo_vm(bo_ptr);
	return r;
}

/**
 * amdgpu_bo_kmap - map an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be mapped
 * @ptr: kernel virtual address to be returned
 *
 * Calls ttm_bo_kmap() to set up the kernel virtual mapping; calls
 * amdgpu_bo_kptr() to get the kernel virtual address.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_kmap(struct amdgpu_bo *bo, void **ptr)
{
	void *kptr;
	long r;

	if (bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)
		return -EPERM;

	r = dma_resv_wait_timeout(bo->tbo.base.resv, DMA_RESV_USAGE_KERNEL,
				  false, MAX_SCHEDULE_TIMEOUT);
	if (r < 0)
		return r;

	kptr = amdgpu_bo_kptr(bo);
	if (kptr) {
		if (ptr)
			*ptr = kptr;
		return 0;
	}

	r = ttm_bo_kmap(&bo->tbo, 0, PFN_UP(bo->tbo.base.size), &bo->kmap);
	if (r)
		return r;

	if (ptr)
		*ptr = amdgpu_bo_kptr(bo);

	return 0;
}

/**
 * amdgpu_bo_kptr - returns a kernel virtual address of the buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * Calls ttm_kmap_obj_virtual() to get the kernel virtual address
 *
 * Returns:
 * the virtual address of a buffer object area.
 */
void *amdgpu_bo_kptr(struct amdgpu_bo *bo)
{
	bool is_iomem;

	return ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
}

/**
 * amdgpu_bo_kunmap - unmap an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be unmapped
 *
 * Unmaps a kernel map set up by amdgpu_bo_kmap().
 */
void amdgpu_bo_kunmap(struct amdgpu_bo *bo)
{
	if (bo->kmap.bo)
		ttm_bo_kunmap(&bo->kmap);
}

/**
 * amdgpu_bo_ref - reference an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * References the contained &ttm_buffer_object.
 *
 * Returns:
 * a refcounted pointer to the &amdgpu_bo buffer object.
 */
struct amdgpu_bo *amdgpu_bo_ref(struct amdgpu_bo *bo)
{
	if (bo == NULL)
		return NULL;

	drm_gem_object_get(&bo->tbo.base);
	return bo;
}

/**
 * amdgpu_bo_unref - unreference an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * Unreferences the contained &ttm_buffer_object and clear the pointer
 */
void amdgpu_bo_unref(struct amdgpu_bo **bo)
{
	if ((*bo) == NULL)
		return;

	drm_gem_object_put(&(*bo)->tbo.base);
	*bo = NULL;
}

/**
 * amdgpu_bo_pin - pin an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be pinned
 * @domain: domain to be pinned to
 *
 * Pins the buffer object according to requested domain. If the memory is
 * unbound gart memory, binds the pages into gart table. Adjusts pin_count and
 * pin_size accordingly.
 *
 * Pinning means to lock pages in memory along with keeping them at a fixed
 * offset. It is required when a buffer can not be moved, for example, when
 * a display buffer is being scanned out.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_pin(struct amdgpu_bo *bo, u32 domain)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { false, false };
	int r, i;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm))
		return -EPERM;

	/* Check domain to be pinned to against preferred domains */
	if (bo->preferred_domains & domain)
		domain = bo->preferred_domains & domain;

	/* A shared bo cannot be migrated to VRAM */
	if (bo->tbo.base.import_attach) {
		if (domain & AMDGPU_GEM_DOMAIN_GTT)
			domain = AMDGPU_GEM_DOMAIN_GTT;
		else
			return -EINVAL;
	}

	if (bo->tbo.pin_count) {
		uint32_t mem_type = bo->tbo.resource->mem_type;
		uint32_t mem_flags = bo->tbo.resource->placement;

		if (!(domain & amdgpu_mem_type_to_domain(mem_type)))
			return -EINVAL;

		if ((mem_type == TTM_PL_VRAM) &&
		    (bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS) &&
		    !(mem_flags & TTM_PL_FLAG_CONTIGUOUS))
			return -EINVAL;

		ttm_bo_pin(&bo->tbo);
		return 0;
	}

	/* This assumes only APU display buffers are pinned with (VRAM|GTT).
	 * See function amdgpu_display_supported_domains()
	 */
	domain = amdgpu_bo_get_preferred_domain(adev, domain);

#ifdef notyet
	if (bo->tbo.base.import_attach)
		dma_buf_pin(bo->tbo.base.import_attach);
#endif

	/* force to pin into visible video ram */
	if (!(bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS))
		bo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	amdgpu_bo_placement_from_domain(bo, domain);
	for (i = 0; i < bo->placement.num_placement; i++) {
		if (bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS &&
		    bo->placements[i].mem_type == TTM_PL_VRAM)
			bo->placements[i].flags |= TTM_PL_FLAG_CONTIGUOUS;
	}

	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (unlikely(r)) {
		dev_err(adev->dev, "%p pin failed\n", bo);
		goto error;
	}

	ttm_bo_pin(&bo->tbo);

	if (bo->tbo.resource->mem_type == TTM_PL_VRAM) {
		atomic64_add(amdgpu_bo_size(bo), &adev->vram_pin_size);
		atomic64_add(amdgpu_vram_mgr_bo_visible_size(bo),
			     &adev->visible_pin_size);
	} else if (bo->tbo.resource->mem_type == TTM_PL_TT) {
		atomic64_add(amdgpu_bo_size(bo), &adev->gart_pin_size);
	}

error:
	return r;
}

/**
 * amdgpu_bo_unpin - unpin an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be unpinned
 *
 * Decreases the pin_count, and clears the flags if pin_count reaches 0.
 * Changes placement and pin size accordingly.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
void amdgpu_bo_unpin(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	ttm_bo_unpin(&bo->tbo);
	if (bo->tbo.pin_count)
		return;

#ifdef notyet
	if (bo->tbo.base.import_attach)
		dma_buf_unpin(bo->tbo.base.import_attach);
#endif

	if (bo->tbo.resource->mem_type == TTM_PL_VRAM) {
		atomic64_sub(amdgpu_bo_size(bo), &adev->vram_pin_size);
		atomic64_sub(amdgpu_vram_mgr_bo_visible_size(bo),
			     &adev->visible_pin_size);
	} else if (bo->tbo.resource->mem_type == TTM_PL_TT) {
		atomic64_sub(amdgpu_bo_size(bo), &adev->gart_pin_size);
	}

}

static const char * const amdgpu_vram_names[] = {
	"UNKNOWN",
	"GDDR1",
	"DDR2",
	"GDDR3",
	"GDDR4",
	"GDDR5",
	"HBM",
	"DDR3",
	"DDR4",
	"GDDR6",
	"DDR5",
	"LPDDR4",
	"LPDDR5"
};

/**
 * amdgpu_bo_init - initialize memory manager
 * @adev: amdgpu device object
 *
 * Calls amdgpu_ttm_init() to initialize amdgpu memory manager.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_init(struct amdgpu_device *adev)
{
	/* On A+A platform, VRAM can be mapped as WB */
	if (!adev->gmc.xgmi.connected_to_cpu && !adev->gmc.is_app_apu) {
#ifdef __linux__
		/* reserve PAT memory space to WC for VRAM */
		int r = arch_io_reserve_memtype_wc(adev->gmc.aper_base,
				adev->gmc.aper_size);

		if (r) {
			DRM_ERROR("Unable to set WC memtype for the aperture base\n");
			return r;
		}

		/* Add an MTRR for the VRAM */
		adev->gmc.vram_mtrr = arch_phys_wc_add(adev->gmc.aper_base,
				adev->gmc.aper_size);
#else
		paddr_t start, end;

		drm_mtrr_add(adev->gmc.aper_base, adev->gmc.aper_size, DRM_MTRR_WC);

		start = atop(bus_space_mmap(adev->memt, adev->gmc.aper_base, 0, 0, 0));
		end = start + atop(adev->gmc.aper_size);
		uvm_page_physload(start, end, start, end, PHYSLOAD_DEVICE);
#endif
	}

	DRM_INFO("Detected VRAM RAM=%lluM, BAR=%lluM\n",
		 adev->gmc.mc_vram_size >> 20,
		 (unsigned long long)adev->gmc.aper_size >> 20);
	DRM_INFO("RAM width %dbits %s\n",
		 adev->gmc.vram_width, amdgpu_vram_names[adev->gmc.vram_type]);
	return amdgpu_ttm_init(adev);
}

/**
 * amdgpu_bo_fini - tear down memory manager
 * @adev: amdgpu device object
 *
 * Reverses amdgpu_bo_init() to tear down memory manager.
 */
void amdgpu_bo_fini(struct amdgpu_device *adev)
{
	int idx;

	amdgpu_ttm_fini(adev);

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		if (!adev->gmc.xgmi.connected_to_cpu && !adev->gmc.is_app_apu) {
#ifdef __linux__
			arch_phys_wc_del(adev->gmc.vram_mtrr);
			arch_io_free_memtype_wc(adev->gmc.aper_base, adev->gmc.aper_size);
#else
			drm_mtrr_del(0, adev->gmc.aper_base, adev->gmc.aper_size, DRM_MTRR_WC);
#endif
		}
		drm_dev_exit(idx);
	}
}

/**
 * amdgpu_bo_set_tiling_flags - set tiling flags
 * @bo: &amdgpu_bo buffer object
 * @tiling_flags: new flags
 *
 * Sets buffer object's tiling flags with the new one. Used by GEM ioctl or
 * kernel driver to set the tiling flags on a buffer.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_set_tiling_flags(struct amdgpu_bo *bo, u64 tiling_flags)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct amdgpu_bo_user *ubo;

	BUG_ON(bo->tbo.type == ttm_bo_type_kernel);
	if (adev->family <= AMDGPU_FAMILY_CZ &&
	    AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT) > 6)
		return -EINVAL;

	ubo = to_amdgpu_bo_user(bo);
	ubo->tiling_flags = tiling_flags;
	return 0;
}

/**
 * amdgpu_bo_get_tiling_flags - get tiling flags
 * @bo: &amdgpu_bo buffer object
 * @tiling_flags: returned flags
 *
 * Gets buffer object's tiling flags. Used by GEM ioctl or kernel driver to
 * set the tiling flags on a buffer.
 */
void amdgpu_bo_get_tiling_flags(struct amdgpu_bo *bo, u64 *tiling_flags)
{
	struct amdgpu_bo_user *ubo;

	BUG_ON(bo->tbo.type == ttm_bo_type_kernel);
	dma_resv_assert_held(bo->tbo.base.resv);
	ubo = to_amdgpu_bo_user(bo);

	if (tiling_flags)
		*tiling_flags = ubo->tiling_flags;
}

/**
 * amdgpu_bo_set_metadata - set metadata
 * @bo: &amdgpu_bo buffer object
 * @metadata: new metadata
 * @metadata_size: size of the new metadata
 * @flags: flags of the new metadata
 *
 * Sets buffer object's metadata, its size and flags.
 * Used via GEM ioctl.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_set_metadata(struct amdgpu_bo *bo, void *metadata,
			   u32 metadata_size, uint64_t flags)
{
	struct amdgpu_bo_user *ubo;
	void *buffer;

	BUG_ON(bo->tbo.type == ttm_bo_type_kernel);
	ubo = to_amdgpu_bo_user(bo);
	if (!metadata_size) {
		if (ubo->metadata_size) {
			kfree(ubo->metadata);
			ubo->metadata = NULL;
			ubo->metadata_size = 0;
		}
		return 0;
	}

	if (metadata == NULL)
		return -EINVAL;

	buffer = kmemdup(metadata, metadata_size, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	kfree(ubo->metadata);
	ubo->metadata_flags = flags;
	ubo->metadata = buffer;
	ubo->metadata_size = metadata_size;

	return 0;
}

/**
 * amdgpu_bo_get_metadata - get metadata
 * @bo: &amdgpu_bo buffer object
 * @buffer: returned metadata
 * @buffer_size: size of the buffer
 * @metadata_size: size of the returned metadata
 * @flags: flags of the returned metadata
 *
 * Gets buffer object's metadata, its size and flags. buffer_size shall not be
 * less than metadata_size.
 * Used via GEM ioctl.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_get_metadata(struct amdgpu_bo *bo, void *buffer,
			   size_t buffer_size, uint32_t *metadata_size,
			   uint64_t *flags)
{
	struct amdgpu_bo_user *ubo;

	if (!buffer && !metadata_size)
		return -EINVAL;

	BUG_ON(bo->tbo.type == ttm_bo_type_kernel);
	ubo = to_amdgpu_bo_user(bo);
	if (metadata_size)
		*metadata_size = ubo->metadata_size;

	if (buffer) {
		if (buffer_size < ubo->metadata_size)
			return -EINVAL;

		if (ubo->metadata_size)
			memcpy(buffer, ubo->metadata, ubo->metadata_size);
	}

	if (flags)
		*flags = ubo->metadata_flags;

	return 0;
}

/**
 * amdgpu_bo_move_notify - notification about a memory move
 * @bo: pointer to a buffer object
 * @evict: if this move is evicting the buffer from the graphics address space
 * @new_mem: new resource for backing the BO
 *
 * Marks the corresponding &amdgpu_bo buffer object as invalid, also performs
 * bookkeeping.
 * TTM driver callback which is called when ttm moves a buffer.
 */
void amdgpu_bo_move_notify(struct ttm_buffer_object *bo,
			   bool evict,
			   struct ttm_resource *new_mem)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct ttm_resource *old_mem = bo->resource;
	struct amdgpu_bo *abo;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return;

	abo = ttm_to_amdgpu_bo(bo);
	amdgpu_vm_bo_invalidate(adev, abo, evict);

	amdgpu_bo_kunmap(abo);

#ifdef notyet
	if (abo->tbo.base.dma_buf && !abo->tbo.base.import_attach &&
	    old_mem && old_mem->mem_type != TTM_PL_SYSTEM)
		dma_buf_move_notify(abo->tbo.base.dma_buf);
#endif
	
	/* move_notify is called before move happens */
	trace_amdgpu_bo_move(abo, new_mem ? new_mem->mem_type : -1,
			     old_mem ? old_mem->mem_type : -1);
}

void amdgpu_bo_get_memory(struct amdgpu_bo *bo,
			  struct amdgpu_mem_stats *stats)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_resource *res = bo->tbo.resource;
	uint64_t size = amdgpu_bo_size(bo);
	struct drm_gem_object *obj;
	bool shared;

	/* Abort if the BO doesn't currently have a backing store */
	if (!res)
		return;

	obj = &bo->tbo.base;
	shared = drm_gem_object_is_shared_for_memory_stats(obj);

	switch (res->mem_type) {
	case TTM_PL_VRAM:
		stats->vram += size;
		if (amdgpu_res_cpu_visible(adev, res))
			stats->visible_vram += size;
		if (shared)
			stats->vram_shared += size;
		break;
	case TTM_PL_TT:
		stats->gtt += size;
		if (shared)
			stats->gtt_shared += size;
		break;
	case TTM_PL_SYSTEM:
	default:
		stats->cpu += size;
		if (shared)
			stats->cpu_shared += size;
		break;
	}

	if (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) {
		stats->requested_vram += size;
		if (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			stats->requested_visible_vram += size;

		if (res->mem_type != TTM_PL_VRAM) {
			stats->evicted_vram += size;
			if (bo->flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
				stats->evicted_visible_vram += size;
		}
	} else if (bo->preferred_domains & AMDGPU_GEM_DOMAIN_GTT) {
		stats->requested_gtt += size;
	}
}

/**
 * amdgpu_bo_release_notify - notification about a BO being released
 * @bo: pointer to a buffer object
 *
 * Wipes VRAM buffers whose contents should not be leaked before the
 * memory is released.
 */
void amdgpu_bo_release_notify(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct dma_fence *fence = NULL;
	struct amdgpu_bo *abo;
	int r;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return;

	abo = ttm_to_amdgpu_bo(bo);

	WARN_ON(abo->vm_bo);

	if (abo->kfd_bo)
		amdgpu_amdkfd_release_notify(abo);

	/*
	 * We lock the private dma_resv object here and since the BO is about to
	 * be released nobody else should have a pointer to it.
	 * So when this locking here fails something is wrong with the reference
	 * counting.
	 */
	if (WARN_ON_ONCE(!dma_resv_trylock(&bo->base._resv)))
		return;

	amdgpu_amdkfd_remove_all_eviction_fences(abo);

	if (!bo->resource || bo->resource->mem_type != TTM_PL_VRAM ||
	    !(abo->flags & AMDGPU_GEM_CREATE_VRAM_WIPE_ON_RELEASE) ||
	    adev->in_suspend || drm_dev_is_unplugged(adev_to_drm(adev)))
		goto out;

	r = dma_resv_reserve_fences(&bo->base._resv, 1);
	if (r)
		goto out;

	r = amdgpu_fill_buffer(abo, 0, &bo->base._resv, &fence, true);
	if (WARN_ON(r))
		goto out;

	amdgpu_vram_mgr_set_cleared(bo->resource);
	dma_resv_add_fence(&bo->base._resv, fence, DMA_RESV_USAGE_KERNEL);
	dma_fence_put(fence);

out:
	dma_resv_unlock(&bo->base._resv);
}

/**
 * amdgpu_bo_fault_reserve_notify - notification about a memory fault
 * @bo: pointer to a buffer object
 *
 * Notifies the driver we are taking a fault on this BO and have reserved it,
 * also performs bookkeeping.
 * TTM driver callback for dealing with vm faults.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
vm_fault_t amdgpu_bo_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_bo *abo = ttm_to_amdgpu_bo(bo);
	int r;

	/* Remember that this BO was accessed by the CPU */
	abo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	if (amdgpu_res_cpu_visible(adev, bo->resource))
		return 0;

	/* Can't move a pinned BO to visible VRAM */
	if (abo->tbo.pin_count > 0)
		return VM_FAULT_SIGBUS;

	/* hurrah the memory is not visible ! */
	atomic64_inc(&adev->num_vram_cpu_page_faults);
	amdgpu_bo_placement_from_domain(abo, AMDGPU_GEM_DOMAIN_VRAM |
					AMDGPU_GEM_DOMAIN_GTT);

	/* Avoid costly evictions; only set GTT as a busy placement */
	abo->placements[0].flags |= TTM_PL_FLAG_DESIRED;

	r = ttm_bo_validate(bo, &abo->placement, &ctx);
	if (unlikely(r == -EBUSY || r == -ERESTARTSYS))
		return VM_FAULT_NOPAGE;
	else if (unlikely(r))
		return VM_FAULT_SIGBUS;

	/* this should never happen */
	if (bo->resource->mem_type == TTM_PL_VRAM &&
	    !amdgpu_res_cpu_visible(adev, bo->resource))
		return VM_FAULT_SIGBUS;

	ttm_bo_move_to_lru_tail_unlocked(bo);
	return 0;
}

/**
 * amdgpu_bo_fence - add fence to buffer object
 *
 * @bo: buffer object in question
 * @fence: fence to add
 * @shared: true if fence should be added shared
 *
 */
void amdgpu_bo_fence(struct amdgpu_bo *bo, struct dma_fence *fence,
		     bool shared)
{
	struct dma_resv *resv = bo->tbo.base.resv;
	int r;

	r = dma_resv_reserve_fences(resv, 1);
	if (r) {
		/* As last resort on OOM we block for the fence */
		dma_fence_wait(fence, false);
		return;
	}

	dma_resv_add_fence(resv, fence, shared ? DMA_RESV_USAGE_READ :
			   DMA_RESV_USAGE_WRITE);
}

/**
 * amdgpu_bo_sync_wait_resv - Wait for BO reservation fences
 *
 * @adev: amdgpu device pointer
 * @resv: reservation object to sync to
 * @sync_mode: synchronization mode
 * @owner: fence owner
 * @intr: Whether the wait is interruptible
 *
 * Extract the fences from the reservation object and waits for them to finish.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_bo_sync_wait_resv(struct amdgpu_device *adev, struct dma_resv *resv,
			     enum amdgpu_sync_mode sync_mode, void *owner,
			     bool intr)
{
	struct amdgpu_sync sync;
	int r;

	amdgpu_sync_create(&sync);
	amdgpu_sync_resv(adev, &sync, resv, sync_mode, owner);
	r = amdgpu_sync_wait(&sync, intr);
	amdgpu_sync_free(&sync);
	return r;
}

/**
 * amdgpu_bo_sync_wait - Wrapper for amdgpu_bo_sync_wait_resv
 * @bo: buffer object to wait for
 * @owner: fence owner
 * @intr: Whether the wait is interruptible
 *
 * Wrapper to wait for fences in a BO.
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_bo_sync_wait(struct amdgpu_bo *bo, void *owner, bool intr)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	return amdgpu_bo_sync_wait_resv(adev, bo->tbo.base.resv,
					AMDGPU_SYNC_NE_OWNER, owner, intr);
}

/**
 * amdgpu_bo_gpu_offset - return GPU offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Note: object should either be pinned or reserved when calling this
 * function, it might be useful to add check for this for debugging.
 *
 * Returns:
 * current GPU offset of the object.
 */
u64 amdgpu_bo_gpu_offset(struct amdgpu_bo *bo)
{
	WARN_ON_ONCE(bo->tbo.resource->mem_type == TTM_PL_SYSTEM);
	WARN_ON_ONCE(!dma_resv_is_locked(bo->tbo.base.resv) &&
		     !bo->tbo.pin_count && bo->tbo.type != ttm_bo_type_kernel);
	WARN_ON_ONCE(bo->tbo.resource->start == AMDGPU_BO_INVALID_OFFSET);
	WARN_ON_ONCE(bo->tbo.resource->mem_type == TTM_PL_VRAM &&
		     !(bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS));

	return amdgpu_bo_gpu_offset_no_check(bo);
}

/**
 * amdgpu_bo_gpu_offset_no_check - return GPU offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Returns:
 * current GPU offset of the object without raising warnings.
 */
u64 amdgpu_bo_gpu_offset_no_check(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	uint64_t offset = AMDGPU_BO_INVALID_OFFSET;

	if (bo->tbo.resource->mem_type == TTM_PL_TT)
		offset = amdgpu_gmc_agp_addr(&bo->tbo);

	if (offset == AMDGPU_BO_INVALID_OFFSET)
		offset = (bo->tbo.resource->start << PAGE_SHIFT) +
			amdgpu_ttm_domain_start(adev, bo->tbo.resource->mem_type);

	return amdgpu_gmc_sign_extend(offset);
}

/**
 * amdgpu_bo_get_preferred_domain - get preferred domain
 * @adev: amdgpu device object
 * @domain: allowed :ref:`memory domains <amdgpu_memory_domains>`
 *
 * Returns:
 * Which of the allowed domains is preferred for allocating the BO.
 */
uint32_t amdgpu_bo_get_preferred_domain(struct amdgpu_device *adev,
					    uint32_t domain)
{
	if ((domain == (AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT)) &&
	    ((adev->asic_type == CHIP_CARRIZO) || (adev->asic_type == CHIP_STONEY))) {
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (adev->gmc.real_vram_size <= AMDGPU_SG_THRESHOLD)
			domain = AMDGPU_GEM_DOMAIN_GTT;
	}
	return domain;
}

#if defined(CONFIG_DEBUG_FS)
#define amdgpu_bo_print_flag(m, bo, flag)		        \
	do {							\
		if (bo->flags & (AMDGPU_GEM_CREATE_ ## flag)) {	\
			seq_printf((m), " " #flag);		\
		}						\
	} while (0)

/**
 * amdgpu_bo_print_info - print BO info in debugfs file
 *
 * @id: Index or Id of the BO
 * @bo: Requested BO for printing info
 * @m: debugfs file
 *
 * Print BO information in debugfs file
 *
 * Returns:
 * Size of the BO in bytes.
 */
u64 amdgpu_bo_print_info(int id, struct amdgpu_bo *bo, struct seq_file *m)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct dma_buf_attachment *attachment;
	struct dma_buf *dma_buf;
	const char *placement;
	unsigned int pin_count;
	u64 size;

	if (dma_resv_trylock(bo->tbo.base.resv)) {
		if (!bo->tbo.resource) {
			placement = "NONE";
		} else {
			switch (bo->tbo.resource->mem_type) {
			case TTM_PL_VRAM:
				if (amdgpu_res_cpu_visible(adev, bo->tbo.resource))
					placement = "VRAM VISIBLE";
				else
					placement = "VRAM";
				break;
			case TTM_PL_TT:
				placement = "GTT";
				break;
			case AMDGPU_PL_GDS:
				placement = "GDS";
				break;
			case AMDGPU_PL_GWS:
				placement = "GWS";
				break;
			case AMDGPU_PL_OA:
				placement = "OA";
				break;
			case AMDGPU_PL_PREEMPT:
				placement = "PREEMPTIBLE";
				break;
			case AMDGPU_PL_DOORBELL:
				placement = "DOORBELL";
				break;
			case TTM_PL_SYSTEM:
			default:
				placement = "CPU";
				break;
			}
		}
		dma_resv_unlock(bo->tbo.base.resv);
	} else {
		placement = "UNKNOWN";
	}

	size = amdgpu_bo_size(bo);
	seq_printf(m, "\t\t0x%08x: %12lld byte %s",
			id, size, placement);

	pin_count = READ_ONCE(bo->tbo.pin_count);
	if (pin_count)
		seq_printf(m, " pin count %d", pin_count);

	dma_buf = READ_ONCE(bo->tbo.base.dma_buf);
	attachment = READ_ONCE(bo->tbo.base.import_attach);

	if (attachment)
		seq_printf(m, " imported from ino:%lu", file_inode(dma_buf->file)->i_ino);
	else if (dma_buf)
		seq_printf(m, " exported as ino:%lu", file_inode(dma_buf->file)->i_ino);

	amdgpu_bo_print_flag(m, bo, CPU_ACCESS_REQUIRED);
	amdgpu_bo_print_flag(m, bo, NO_CPU_ACCESS);
	amdgpu_bo_print_flag(m, bo, CPU_GTT_USWC);
	amdgpu_bo_print_flag(m, bo, VRAM_CLEARED);
	amdgpu_bo_print_flag(m, bo, VRAM_CONTIGUOUS);
	amdgpu_bo_print_flag(m, bo, VM_ALWAYS_VALID);
	amdgpu_bo_print_flag(m, bo, EXPLICIT_SYNC);

	seq_puts(m, "\n");

	return size;
}
#endif
