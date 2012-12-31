/* include/media/videobuf2-ion.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Definition of Android ION memory allocator for videobuf2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MEDIA_VIDEOBUF2_ION_H
#define _MEDIA_VIDEOBUF2_ION_H

#include <linux/ion.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>

struct vb2_ion {
	struct device	*dev;
	const char	*name;
	unsigned long	align;
	bool		contig;
	bool		cacheable;
};

struct vb2_drv {
	struct device	*dev;
	bool		use_mmu;
};

static inline unsigned long vb2_ion_plane_dvaddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	return (unsigned long)vb2_plane_cookie(vb, plane_no);
}

static inline unsigned long vb2_ion_plane_kvaddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	return (unsigned long)vb2_plane_vaddr(vb, plane_no);
}

void *vb2_ion_init(struct vb2_ion *ion, struct vb2_drv *drv);
void vb2_ion_cleanup(void *alloc_ctx);
void **vb2_ion_init_multi(unsigned int num_planes, struct vb2_ion *ion, struct vb2_drv *drv);
void vb2_ion_cleanup_multi(void **alloc_ctxes);

void vb2_ion_set_sharable(void *alloc_ctx, bool sharable);
void vb2_ion_set_cacheable(void *alloc_ctx, bool cacheable);
bool vb2_ion_get_cacheable(void *alloc_ctx);
int vb2_ion_cache_flush(struct vb2_buffer *vb, u32 num_planes);
int vb2_ion_cache_inv(struct vb2_buffer *vb, u32 num_planes);

void vb2_ion_suspend(void *alloc_ctx);
void vb2_ion_resume(void *alloc_ctx);

extern const struct vb2_mem_ops vb2_ion_memops;
extern struct ion_device *ion_exynos;

#endif
