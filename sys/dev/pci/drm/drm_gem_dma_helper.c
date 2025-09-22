/* $OpenBSD: drm_gem_dma_helper.c,v 1.5 2024/12/15 11:02:59 mpi Exp $ */
/* $NetBSD: drm_gem_dma_helper.c,v 1.9 2019/11/05 23:29:28 jmcneill Exp $ */
/*-
 * Copyright (c) 2015-2017 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <linux/iosys-map.h>

#include <drm/drm_device.h>
#include <drm/drm_gem_dma_helper.h>

#include <uvm/uvm_extern.h>

static const struct drm_gem_object_funcs drm_gem_dma_default_funcs = {
	.free = drm_gem_dma_free_object,
	.get_sg_table = drm_gem_dma_get_sg_table,
	.vmap = drm_gem_dma_vmap,
//	.mmap = drm_gem_dma_mmap,
};

static struct drm_gem_dma_object *
drm_gem_dma_create_internal(struct drm_device *ddev, size_t size,
    struct sg_table *sgt)
{
	struct drm_gem_dma_object *obj;
	int error, nsegs;

	obj = malloc(sizeof(*obj), M_DRM, M_WAITOK | M_ZERO);
	obj->dmat = ddev->dmat;
	obj->dmasize = size;
	obj->base.funcs = &drm_gem_dma_default_funcs;

	if (sgt) {
		STUB();
#ifdef notyet
		error = -drm_prime_sg_to_bus_dmamem(obj->dmat, obj->dmasegs, 1,
		    &nsegs, sgt);
#endif
		error = -ENOMEM;
	} else {
		error = bus_dmamem_alloc(obj->dmat, obj->dmasize,
		    PAGE_SIZE, 0, obj->dmasegs, 1, &nsegs,
		    BUS_DMA_WAITOK);
	}
	if (error)
		goto failed;
	error = bus_dmamem_map(obj->dmat, obj->dmasegs, nsegs,
	    obj->dmasize, &obj->vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_NOCACHE);
	if (error)
		goto free;
	error = bus_dmamap_create(obj->dmat, obj->dmasize, 1,
	    obj->dmasize, 0, BUS_DMA_WAITOK, &obj->dmamap);
	if (error)
		goto unmap;
	error = bus_dmamap_load(obj->dmat, obj->dmamap, obj->vaddr,
	    obj->dmasize, NULL, BUS_DMA_WAITOK);
	if (error)
		goto destroy;

#ifdef notyet
	if (!sgt)
#endif
		memset(obj->vaddr, 0, obj->dmasize);

	error = drm_gem_object_init(ddev, &obj->base, size);
	if (error)
		goto unload;

	obj->dma_addr = obj->dmamap->dm_segs[0].ds_addr;
	return obj;

unload:
	bus_dmamap_unload(obj->dmat, obj->dmamap);
destroy:
	bus_dmamap_destroy(obj->dmat, obj->dmamap);
unmap:
	bus_dmamem_unmap(obj->dmat, obj->vaddr, obj->dmasize);
free:
#ifdef notyet
	if (obj->sgt)
		drm_prime_sg_free(obj->sgt);
	else
#endif
		bus_dmamem_free(obj->dmat, obj->dmasegs, nsegs);
failed:
	free(obj, M_DRM, sizeof(*obj));

	return NULL;
}

struct drm_gem_dma_object *
drm_gem_dma_create(struct drm_device *ddev, size_t size)
{

	return drm_gem_dma_create_internal(ddev, size, NULL);
}

static void
drm_gem_dma_obj_free(struct drm_gem_dma_object *obj)
{

	bus_dmamap_unload(obj->dmat, obj->dmamap);
	bus_dmamap_destroy(obj->dmat, obj->dmamap);
	bus_dmamem_unmap(obj->dmat, obj->vaddr, obj->dmasize);
#ifdef notyet
	if (obj->sgt)
		drm_prime_sg_free(obj->sgt);
	else
#endif
		bus_dmamem_free(obj->dmat, obj->dmasegs, 1);
	free(obj, M_DRM, sizeof(*obj));
}

void
drm_gem_dma_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gem_obj);

	drm_gem_free_mmap_offset(gem_obj);
	drm_gem_object_release(gem_obj);
	drm_gem_dma_obj_free(obj);
}

int
drm_gem_dma_dumb_create_internal(struct drm_file *file_priv,
    struct drm_device *ddev, struct drm_mode_create_dumb *args)
{
	struct drm_gem_dma_object *obj;
	uint32_t handle;
	int error;

	args->handle = 0;

	obj = drm_gem_dma_create(ddev, args->size);
	if (obj == NULL)
		return -ENOMEM;

	error = drm_gem_handle_create(file_priv, &obj->base, &handle);
	drm_gem_object_put(&obj->base);
	if (error) {
		drm_gem_dma_obj_free(obj);
		return error;
	}

	args->handle = handle;

	return 0;
}

int
drm_gem_dma_dumb_create(struct drm_file *file_priv, struct drm_device *ddev,
    struct drm_mode_create_dumb *args)
{
	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;
	args->size = roundup(args->size, PAGE_SIZE);

	return drm_gem_dma_dumb_create_internal(file_priv, ddev, args);
}

int
drm_gem_dma_fault(struct drm_gem_object *gem_obj, struct uvm_faultinfo *ufi,
    off_t offset, vaddr_t vaddr, vm_page_t *pps, int npages, int centeridx,
    vm_prot_t access_type, int flags)
{
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gem_obj);
	struct uvm_object *uobj = &obj->base.uobj;
	paddr_t paddr;
	int lcv, retval;
	vm_prot_t mapprot;

	offset -= drm_vma_node_offset_addr(&obj->base.vma_node);
	mapprot = ufi->entry->protection;

	retval = 0;
	for (lcv = 0; lcv < npages; lcv++, offset += PAGE_SIZE,
	    vaddr += PAGE_SIZE) {
		if ((flags & PGO_ALLPAGES) == 0 && lcv != centeridx)
			continue;

		if (pps[lcv] == PGO_DONTCARE)
			continue;

		paddr = bus_dmamem_mmap(obj->dmat, obj->dmasegs, 1,
		    offset, access_type, BUS_DMA_NOCACHE);
		if (paddr == -1) {
			retval = EACCES;
			break;
		}

		if (pmap_enter(ufi->orig_map->pmap, vaddr, paddr,
		    mapprot, PMAP_CANFAIL | mapprot) != 0) {
			pmap_update(ufi->orig_map->pmap);
			uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap,
			    uobj);
			uvm_wait("drm_gem_dma_fault");
			return ERESTART;
		}
	}

	pmap_update(ufi->orig_map->pmap);
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);

	return retval;
}

struct sg_table *
drm_gem_dma_get_sg_table(struct drm_gem_object *gem_obj)
{
	return NULL;
#ifdef notyet
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gem_obj);

	return drm_prime_bus_dmamem_to_sg(obj->dmat, obj->dmasegs, 1);
#endif
}

struct drm_gem_object *
drm_gem_dma_prime_import_sg_table(struct drm_device *ddev,
    struct dma_buf_attachment *attach, struct sg_table *sgt)
{
	return NULL;
#ifdef notyet
	size_t size = drm_prime_sg_size(sgt);
	struct drm_gem_dma_object *obj;

	obj = drm_gem_dma_create_internal(ddev, size, sgt);
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);

	return &obj->base;
#endif
}

int
drm_gem_dma_vmap(struct drm_gem_object *gem_obj, struct iosys_map *map)
{
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gem_obj);

	iosys_map_set_vaddr(map, obj->vaddr);

	return 0;
}
