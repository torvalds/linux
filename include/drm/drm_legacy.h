#ifndef __DRM_DRM_LEGACY_H__
#define __DRM_DRM_LEGACY_H__

/*
 * Legacy driver interfaces for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
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


/*
 * Legacy Support for palateontologic DRM drivers
 *
 * If you add a new driver and it uses any of these functions or structures,
 * you're doing it terribly wrong.
 */

int drm_legacy_addmap(struct drm_device *d, resource_size_t offset,
		      unsigned int size, enum drm_map_type type,
		      enum drm_map_flags flags, struct drm_local_map **map_p);
int drm_legacy_rmmap(struct drm_device *d, struct drm_local_map *map);
int drm_legacy_rmmap_locked(struct drm_device *d, struct drm_local_map *map);
struct drm_local_map *drm_legacy_getsarea(struct drm_device *dev);

int drm_legacy_addbufs_agp(struct drm_device *d, struct drm_buf_desc *req);
int drm_legacy_addbufs_pci(struct drm_device *d, struct drm_buf_desc *req);

void drm_legacy_idlelock_take(struct drm_lock_data *lock);
void drm_legacy_idlelock_release(struct drm_lock_data *lock);

/* drm_pci.c dma alloc wrappers */
void __drm_legacy_pci_free(struct drm_device *dev, drm_dma_handle_t * dmah);

/* drm_memory.c */
void drm_legacy_ioremap(struct drm_local_map *map, struct drm_device *dev);
void drm_legacy_ioremap_wc(struct drm_local_map *map, struct drm_device *dev);
void drm_legacy_ioremapfree(struct drm_local_map *map, struct drm_device *dev);

static __inline__ struct drm_local_map *drm_legacy_findmap(struct drm_device *dev,
							   unsigned int token)
{
	struct drm_map_list *_entry;
	list_for_each_entry(_entry, &dev->maplist, head)
	    if (_entry->user_token == token)
		return _entry->map;
	return NULL;
}

#endif /* __DRM_DRM_LEGACY_H__ */
