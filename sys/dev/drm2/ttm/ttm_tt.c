/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
/*
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * <kib@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/ttm/ttm_module.h>
#include <dev/drm2/ttm/ttm_bo_driver.h>
#include <dev/drm2/ttm/ttm_placement.h>
#include <dev/drm2/ttm/ttm_page_alloc.h>

MALLOC_DEFINE(M_TTM_PD, "ttm_pd", "TTM Page Directories");

/**
 * Allocates storage for pointers to the pages that back the ttm.
 */
static void ttm_tt_alloc_page_directory(struct ttm_tt *ttm)
{
	ttm->pages = malloc(ttm->num_pages * sizeof(void *),
	    M_TTM_PD, M_WAITOK | M_ZERO);
}

static void ttm_dma_tt_alloc_page_directory(struct ttm_dma_tt *ttm)
{
	ttm->ttm.pages = malloc(ttm->ttm.num_pages * sizeof(void *),
	    M_TTM_PD, M_WAITOK | M_ZERO);
	ttm->dma_address = malloc(ttm->ttm.num_pages *
	    sizeof(*ttm->dma_address), M_TTM_PD, M_WAITOK);
}

#if defined(__i386__) || defined(__amd64__)
static inline int ttm_tt_set_page_caching(vm_page_t p,
					  enum ttm_caching_state c_old,
					  enum ttm_caching_state c_new)
{

	/* XXXKIB our VM does not need this. */
#if 0
	if (c_old != tt_cached) {
		/* p isn't in the default caching state, set it to
		 * writeback first to free its current memtype. */
		pmap_page_set_memattr(p, VM_MEMATTR_WRITE_BACK);
	}
#endif

	if (c_new == tt_wc)
		pmap_page_set_memattr(p, VM_MEMATTR_WRITE_COMBINING);
	else if (c_new == tt_uncached)
		pmap_page_set_memattr(p, VM_MEMATTR_UNCACHEABLE);

	return (0);
}
#else
static inline int ttm_tt_set_page_caching(vm_page_t p,
					  enum ttm_caching_state c_old,
					  enum ttm_caching_state c_new)
{
	return 0;
}
#endif

/*
 * Change caching policy for the linear kernel map
 * for range of pages in a ttm.
 */

static int ttm_tt_set_caching(struct ttm_tt *ttm,
			      enum ttm_caching_state c_state)
{
	int i, j;
	vm_page_t cur_page;
	int ret;

	if (ttm->caching_state == c_state)
		return 0;

	if (ttm->state == tt_unpopulated) {
		/* Change caching but don't populate */
		ttm->caching_state = c_state;
		return 0;
	}

	if (ttm->caching_state == tt_cached)
		drm_clflush_pages(ttm->pages, ttm->num_pages);

	for (i = 0; i < ttm->num_pages; ++i) {
		cur_page = ttm->pages[i];
		if (likely(cur_page != NULL)) {
			ret = ttm_tt_set_page_caching(cur_page,
						      ttm->caching_state,
						      c_state);
			if (unlikely(ret != 0))
				goto out_err;
		}
	}

	ttm->caching_state = c_state;

	return 0;

out_err:
	for (j = 0; j < i; ++j) {
		cur_page = ttm->pages[j];
		if (cur_page != NULL) {
			(void)ttm_tt_set_page_caching(cur_page, c_state,
						      ttm->caching_state);
		}
	}

	return ret;
}

int ttm_tt_set_placement_caching(struct ttm_tt *ttm, uint32_t placement)
{
	enum ttm_caching_state state;

	if (placement & TTM_PL_FLAG_WC)
		state = tt_wc;
	else if (placement & TTM_PL_FLAG_UNCACHED)
		state = tt_uncached;
	else
		state = tt_cached;

	return ttm_tt_set_caching(ttm, state);
}

void ttm_tt_destroy(struct ttm_tt *ttm)
{
	if (unlikely(ttm == NULL))
		return;

	if (ttm->state == tt_bound) {
		ttm_tt_unbind(ttm);
	}

	if (likely(ttm->pages != NULL)) {
		ttm->bdev->driver->ttm_tt_unpopulate(ttm);
	}

	if (!(ttm->page_flags & TTM_PAGE_FLAG_PERSISTENT_SWAP) &&
	    ttm->swap_storage)
		vm_object_deallocate(ttm->swap_storage);

	ttm->swap_storage = NULL;
	ttm->func->destroy(ttm);
}

int ttm_tt_init(struct ttm_tt *ttm, struct ttm_bo_device *bdev,
		unsigned long size, uint32_t page_flags,
		vm_page_t dummy_read_page)
{
	ttm->bdev = bdev;
	ttm->glob = bdev->glob;
	ttm->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	ttm->caching_state = tt_cached;
	ttm->page_flags = page_flags;
	ttm->dummy_read_page = dummy_read_page;
	ttm->state = tt_unpopulated;
	ttm->swap_storage = NULL;

	ttm_tt_alloc_page_directory(ttm);
	if (!ttm->pages) {
		ttm_tt_destroy(ttm);
		printf("Failed allocating page table\n");
		return -ENOMEM;
	}
	return 0;
}

void ttm_tt_fini(struct ttm_tt *ttm)
{
	free(ttm->pages, M_TTM_PD);
	ttm->pages = NULL;
}

int ttm_dma_tt_init(struct ttm_dma_tt *ttm_dma, struct ttm_bo_device *bdev,
		unsigned long size, uint32_t page_flags,
		vm_page_t dummy_read_page)
{
	struct ttm_tt *ttm = &ttm_dma->ttm;

	ttm->bdev = bdev;
	ttm->glob = bdev->glob;
	ttm->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	ttm->caching_state = tt_cached;
	ttm->page_flags = page_flags;
	ttm->dummy_read_page = dummy_read_page;
	ttm->state = tt_unpopulated;
	ttm->swap_storage = NULL;

	INIT_LIST_HEAD(&ttm_dma->pages_list);
	ttm_dma_tt_alloc_page_directory(ttm_dma);
	if (!ttm->pages || !ttm_dma->dma_address) {
		ttm_tt_destroy(ttm);
		printf("Failed allocating page table\n");
		return -ENOMEM;
	}
	return 0;
}

void ttm_dma_tt_fini(struct ttm_dma_tt *ttm_dma)
{
	struct ttm_tt *ttm = &ttm_dma->ttm;

	free(ttm->pages, M_TTM_PD);
	ttm->pages = NULL;
	free(ttm_dma->dma_address, M_TTM_PD);
	ttm_dma->dma_address = NULL;
}

void ttm_tt_unbind(struct ttm_tt *ttm)
{
	int ret;

	if (ttm->state == tt_bound) {
		ret = ttm->func->unbind(ttm);
		MPASS(ret == 0);
		ttm->state = tt_unbound;
	}
}

int ttm_tt_bind(struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem)
{
	int ret = 0;

	if (!ttm)
		return -EINVAL;

	if (ttm->state == tt_bound)
		return 0;

	ret = ttm->bdev->driver->ttm_tt_populate(ttm);
	if (ret)
		return ret;

	ret = ttm->func->bind(ttm, bo_mem);
	if (unlikely(ret != 0))
		return ret;

	ttm->state = tt_bound;

	return 0;
}

int ttm_tt_swapin(struct ttm_tt *ttm)
{
	vm_object_t obj;
	vm_page_t from_page, to_page;
	int i, ret, rv;

	obj = ttm->swap_storage;

	VM_OBJECT_WLOCK(obj);
	vm_object_pip_add(obj, 1);
	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = vm_page_grab(obj, i, VM_ALLOC_NORMAL);
		if (from_page->valid != VM_PAGE_BITS_ALL) {
			if (vm_pager_has_page(obj, i, NULL, NULL)) {
				rv = vm_pager_get_pages(obj, &from_page, 1,
				    NULL, NULL);
				if (rv != VM_PAGER_OK) {
					vm_page_lock(from_page);
					vm_page_free(from_page);
					vm_page_unlock(from_page);
					ret = -EIO;
					goto err_ret;
				}
			} else
				vm_page_zero_invalid(from_page, TRUE);
		}
		vm_page_xunbusy(from_page);
		to_page = ttm->pages[i];
		if (unlikely(to_page == NULL)) {
			ret = -ENOMEM;
			goto err_ret;
		}
		pmap_copy_page(from_page, to_page);
	}
	vm_object_pip_wakeup(obj);
	VM_OBJECT_WUNLOCK(obj);

	if (!(ttm->page_flags & TTM_PAGE_FLAG_PERSISTENT_SWAP))
		vm_object_deallocate(obj);
	ttm->swap_storage = NULL;
	ttm->page_flags &= ~TTM_PAGE_FLAG_SWAPPED;
	return (0);

err_ret:
	vm_object_pip_wakeup(obj);
	VM_OBJECT_WUNLOCK(obj);
	return (ret);
}

int ttm_tt_swapout(struct ttm_tt *ttm, vm_object_t persistent_swap_storage)
{
	vm_object_t obj;
	vm_page_t from_page, to_page;
	int i;

	MPASS(ttm->state == tt_unbound || ttm->state == tt_unpopulated);
	MPASS(ttm->caching_state == tt_cached);

	if (persistent_swap_storage == NULL) {
		obj = vm_pager_allocate(OBJT_SWAP, NULL,
		    IDX_TO_OFF(ttm->num_pages), VM_PROT_DEFAULT, 0,
		    curthread->td_ucred);
		if (obj == NULL) {
			printf("[TTM] Failed allocating swap storage\n");
			return (-ENOMEM);
		}
	} else
		obj = persistent_swap_storage;

	VM_OBJECT_WLOCK(obj);
	vm_object_pip_add(obj, 1);
	for (i = 0; i < ttm->num_pages; ++i) {
		from_page = ttm->pages[i];
		if (unlikely(from_page == NULL))
			continue;
		to_page = vm_page_grab(obj, i, VM_ALLOC_NORMAL);
		pmap_copy_page(from_page, to_page);
		to_page->valid = VM_PAGE_BITS_ALL;
		vm_page_dirty(to_page);
		vm_page_xunbusy(to_page);
	}
	vm_object_pip_wakeup(obj);
	VM_OBJECT_WUNLOCK(obj);

	ttm->bdev->driver->ttm_tt_unpopulate(ttm);
	ttm->swap_storage = obj;
	ttm->page_flags |= TTM_PAGE_FLAG_SWAPPED;
	if (persistent_swap_storage != NULL)
		ttm->page_flags |= TTM_PAGE_FLAG_PERSISTENT_SWAP;
	return (0);
}
