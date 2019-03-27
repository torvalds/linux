/*-
 * Copyright (c) 2015 Michal Meloun
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_fb_helper.h>

#include <arm/nvidia/drm2/tegra_drm.h>

#include <sys/vmem.h>
#include <sys/vmem.h>
#include <vm/vm.h>
#include <vm/vm_pageout.h>

static void
tegra_bo_destruct(struct tegra_bo *bo)
{
	vm_page_t m;
	size_t size;
	int i;

	if (bo->cdev_pager == NULL)
		return;

	size = round_page(bo->gem_obj.size);
	if (bo->vbase != 0)
		pmap_qremove(bo->vbase, bo->npages);

	VM_OBJECT_WLOCK(bo->cdev_pager);
	for (i = 0; i < bo->npages; i++) {
		m = bo->m[i];
		cdev_pager_free_page(bo->cdev_pager, m);
		vm_page_lock(m);
		m->flags &= ~PG_FICTITIOUS;
		vm_page_unwire(m, PQ_NONE);
		vm_page_free(m);
		vm_page_unlock(m);
	}
	VM_OBJECT_WUNLOCK(bo->cdev_pager);

	vm_object_deallocate(bo->cdev_pager);
	if (bo->vbase != 0)
		vmem_free(kmem_arena, bo->vbase, size);
}

static void
tegra_bo_free_object(struct drm_gem_object *gem_obj)
{
	struct tegra_bo *bo;

	bo = container_of(gem_obj, struct tegra_bo, gem_obj);
	drm_gem_free_mmap_offset(gem_obj);
	drm_gem_object_release(gem_obj);

	tegra_bo_destruct(bo);

	free(bo->m, DRM_MEM_DRIVER);
	free(bo, DRM_MEM_DRIVER);
}

static int
tegra_bo_alloc_contig(size_t npages, u_long alignment, vm_memattr_t memattr,
    vm_page_t **ret_page)
{
	vm_page_t m;
	int pflags, tries, i;
	vm_paddr_t low, high, boundary;

	low = 0;
	high = -1UL;
	boundary = 0;
	pflags = VM_ALLOC_NORMAL  | VM_ALLOC_NOOBJ | VM_ALLOC_NOBUSY |
	    VM_ALLOC_WIRED | VM_ALLOC_ZERO;
	tries = 0;
retry:
	m = vm_page_alloc_contig(NULL, 0, pflags, npages, low, high, alignment,
	    boundary, memattr);
	if (m == NULL) {
		if (tries < 3) {
			if (!vm_page_reclaim_contig(pflags, npages, low, high,
			    alignment, boundary))
				vm_wait(NULL);
			tries++;
			goto retry;
		}
		return (ENOMEM);
	}

	for (i = 0; i < npages; i++, m++) {
		if ((m->flags & PG_ZERO) == 0)
			pmap_zero_page(m);
		m->valid = VM_PAGE_BITS_ALL;
		(*ret_page)[i] = m;
	}

	return (0);
}

/* Initialize pager and insert all object pages to it*/
static int
tegra_bo_init_pager(struct tegra_bo *bo)
{
	vm_page_t m;
	size_t size;
	int i;

	size = round_page(bo->gem_obj.size);

	bo->pbase = VM_PAGE_TO_PHYS(bo->m[0]);
	if (vmem_alloc(kmem_arena, size, M_WAITOK | M_BESTFIT, &bo->vbase))
		return (ENOMEM);

	VM_OBJECT_WLOCK(bo->cdev_pager);
	for (i = 0; i < bo->npages; i++) {
		m = bo->m[i];
		/*
		 * XXX This is a temporary hack.
		 * We need pager suitable for paging (mmap) managed
		 * real (non-fictitious) pages.
		 * - managed pages are needed for clean module unload.
		 * - aliasing fictitious page to real one is bad,
		 *   pmap cannot handle this situation without issues
		 *   It expects that
		 *    paddr = PHYS_TO_VM_PAGE(VM_PAGE_TO_PHYS(paddr))
		 *   for every single page passed to pmap.
		 */
		m->oflags &= ~VPO_UNMANAGED;
		m->flags |= PG_FICTITIOUS;
		if (vm_page_insert(m, bo->cdev_pager, i) != 0)
			return (EINVAL);
	}
	VM_OBJECT_WUNLOCK(bo->cdev_pager);

	pmap_qenter(bo->vbase, bo->m, bo->npages);
	return (0);
}

/* Allocate memory for frame buffer */
static int
tegra_bo_alloc(struct drm_device *drm, struct tegra_bo *bo)
{
	size_t size;
	int rv;

	size = bo->gem_obj.size;

	bo->npages = atop(size);
	bo->m = malloc(sizeof(vm_page_t *) * bo->npages, DRM_MEM_DRIVER,
	    M_WAITOK | M_ZERO);

	rv = tegra_bo_alloc_contig(bo->npages, PAGE_SIZE,
	    VM_MEMATTR_WRITE_COMBINING, &(bo->m));
	if (rv != 0) {
		DRM_WARNING("Cannot allocate memory for gem object.\n");
		return (rv);
	}
	rv = tegra_bo_init_pager(bo);
	if (rv != 0) {
		DRM_WARNING("Cannot initialize gem object pager.\n");
		return (rv);
	}
	return (0);
}

int
tegra_bo_create(struct drm_device *drm, size_t size, struct tegra_bo **res_bo)
{
	struct tegra_bo *bo;
	int rv;

	if (size <= 0)
		return (-EINVAL);

	bo = malloc(sizeof(*bo), DRM_MEM_DRIVER, M_WAITOK | M_ZERO);

	size = round_page(size);
	rv = drm_gem_object_init(drm, &bo->gem_obj, size);
	if (rv != 0) {
		free(bo, DRM_MEM_DRIVER);
		return (rv);
	}
	rv = drm_gem_create_mmap_offset(&bo->gem_obj);
	if (rv != 0) {
		drm_gem_object_release(&bo->gem_obj);
		free(bo, DRM_MEM_DRIVER);
		return (rv);
	}

	bo->cdev_pager = cdev_pager_allocate(&bo->gem_obj, OBJT_MGTDEVICE,
	    drm->driver->gem_pager_ops, size, 0, 0, NULL);
	rv = tegra_bo_alloc(drm, bo);
	if (rv != 0) {
		tegra_bo_free_object(&bo->gem_obj);
		return (rv);
	}

	*res_bo = bo;
	return (0);
}



static int
tegra_bo_create_with_handle(struct drm_file *file, struct drm_device *drm,
    size_t size, uint32_t *handle, struct tegra_bo **res_bo)
{
	int rv;
	struct tegra_bo *bo;

	rv = tegra_bo_create(drm, size, &bo);
	if (rv != 0)
		return (rv);

	rv = drm_gem_handle_create(file, &bo->gem_obj, handle);
	if (rv != 0) {
		tegra_bo_free_object(&bo->gem_obj);
		drm_gem_object_release(&bo->gem_obj);
		return (rv);
	}

	drm_gem_object_unreference_unlocked(&bo->gem_obj);

	*res_bo = bo;
	return (0);
}

static int
tegra_bo_dumb_create(struct drm_file *file, struct drm_device *drm_dev,
    struct drm_mode_create_dumb *args)
{
	struct tegra_drm *drm;
	struct tegra_bo *bo;
	int rv;

	drm = container_of(drm_dev, struct tegra_drm, drm_dev);

	args->pitch= (args->width * args->bpp + 7) / 8;
	args->pitch = roundup(args->pitch, drm->pitch_align);
	args->size = args->pitch * args->height;
	rv = tegra_bo_create_with_handle(file, drm_dev, args->size,
	    &args->handle, &bo);

	return (rv);
}

static int
tegra_bo_dumb_map_offset(struct drm_file *file_priv,
    struct drm_device *drm_dev, uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem_obj;
	int rv;

	DRM_LOCK(drm_dev);
	gem_obj = drm_gem_object_lookup(drm_dev, file_priv, handle);
	if (gem_obj == NULL) {
		device_printf(drm_dev->dev, "Object not found\n");
		DRM_UNLOCK(drm_dev);
		return (-EINVAL);
	}
	rv = drm_gem_create_mmap_offset(gem_obj);
	if (rv != 0)
		goto fail;

	*offset = DRM_GEM_MAPPING_OFF(gem_obj->map_list.key) |
	    DRM_GEM_MAPPING_KEY;

	drm_gem_object_unreference(gem_obj);
	DRM_UNLOCK(drm_dev);
	return (0);

fail:
	drm_gem_object_unreference(gem_obj);
	DRM_UNLOCK(drm_dev);
	return (rv);
}

static int
tegra_bo_dumb_destroy(struct drm_file *file_priv, struct drm_device *drm_dev,
    unsigned int handle)
{
	int rv;

	rv = drm_gem_handle_delete(file_priv, handle);
	return (rv);
}

/*
 * mmap support
 */
static int
tegra_gem_pager_fault(vm_object_t vm_obj, vm_ooffset_t offset, int prot,
    vm_page_t *mres)
{

#ifdef DRM_PAGER_DEBUG
	DRM_DEBUG("object %p offset %jd prot %d mres %p\n",
	    vm_obj, (intmax_t)offset, prot, mres);
#endif
	return (VM_PAGER_FAIL);

}

static int
tegra_gem_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	if (color != NULL)
		*color = 0;
	return (0);
}

static void
tegra_gem_pager_dtor(void *handle)
{

}

static struct cdev_pager_ops tegra_gem_pager_ops = {
	.cdev_pg_fault = tegra_gem_pager_fault,
	.cdev_pg_ctor  = tegra_gem_pager_ctor,
	.cdev_pg_dtor  = tegra_gem_pager_dtor
};

/* Fill up relevant fields in drm_driver ops */
void
tegra_bo_driver_register(struct drm_driver *drm_drv)
{
	drm_drv->gem_free_object = tegra_bo_free_object;
	drm_drv->gem_pager_ops = &tegra_gem_pager_ops;
	drm_drv->dumb_create = tegra_bo_dumb_create;
	drm_drv->dumb_map_offset = tegra_bo_dumb_map_offset;
	drm_drv->dumb_destroy = tegra_bo_dumb_destroy;
}
