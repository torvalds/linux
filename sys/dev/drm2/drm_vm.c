/**
 * \file drm_vm.c
 * Memory mapping for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/** @file drm_vm.c
 * Support code for mmaping of DRM maps.
 */

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm.h>

#ifdef FREEBSD_NOTYET
int
drm_mmap(struct cdev *kdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	struct drm_file *file_priv = NULL;
	drm_local_map_t *map;
	enum drm_map_type type;
	vm_paddr_t phys;
	int error;

	/* d_mmap gets called twice, we can only reference file_priv during
	 * the first call.  We need to assume that if error is EBADF the
	 * call was successful and the client is authenticated.
	 */
	error = devfs_get_cdevpriv((void **)&file_priv);
	if (error == ENOENT) {
		DRM_ERROR("Could not find authenticator!\n");
		return EINVAL;
	}

	if (file_priv && !file_priv->authenticated)
		return EACCES;

	DRM_DEBUG("called with offset %016jx\n", offset);
	if (dev->dma && offset < ptoa(dev->dma->page_count)) {
		drm_device_dma_t *dma = dev->dma;

		DRM_SPINLOCK(&dev->dma_lock);

		if (dma->pagelist != NULL) {
			unsigned long page = offset >> PAGE_SHIFT;
			unsigned long phys = dma->pagelist[page];

			DRM_SPINUNLOCK(&dev->dma_lock);
			*paddr = phys;
			return 0;
		} else {
			DRM_SPINUNLOCK(&dev->dma_lock);
			return -1;
		}
	}

	/* A sequential search of a linked list is
	   fine here because: 1) there will only be
	   about 5-10 entries in the list and, 2) a
	   DRI client only has to do this mapping
	   once, so it doesn't have to be optimized
	   for performance, even if the list was a
	   bit longer.
	*/
	DRM_LOCK(dev);
	TAILQ_FOREACH(map, &dev->maplist, link) {
		if (offset >> DRM_MAP_HANDLE_SHIFT ==
		    (unsigned long)map->handle >> DRM_MAP_HANDLE_SHIFT)
			break;
	}

	if (map == NULL) {
		DRM_DEBUG("Can't find map, request offset = %016jx\n", offset);
		TAILQ_FOREACH(map, &dev->maplist, link) {
			DRM_DEBUG("map offset = %016lx, handle = %016lx\n",
			    map->offset, (unsigned long)map->handle);
		}
		DRM_UNLOCK(dev);
		return -1;
	}
	if (((map->flags & _DRM_RESTRICTED) && !DRM_SUSER(DRM_CURPROC))) {
		DRM_UNLOCK(dev);
		DRM_DEBUG("restricted map\n");
		return -1;
	}
	type = map->type;
	DRM_UNLOCK(dev);

	offset = offset & ((1ULL << DRM_MAP_HANDLE_SHIFT) - 1);

	switch (type) {
	case _DRM_FRAME_BUFFER:
	case _DRM_AGP:
		*memattr = VM_MEMATTR_WRITE_COMBINING;
		/* FALLTHROUGH */
	case _DRM_REGISTERS:
		phys = map->offset + offset;
		break;
	case _DRM_SCATTER_GATHER:
		*memattr = VM_MEMATTR_WRITE_COMBINING;
		/* FALLTHROUGH */
	case _DRM_CONSISTENT:
	case _DRM_SHM:
		phys = vtophys((char *)map->virtual + offset);
		break;
	default:
		DRM_ERROR("bad map type %d\n", type);
		return -1;	/* This should never happen. */
	}

	*paddr = phys;
	return 0;
}
#endif /* FREEBSD_NOTYET */
