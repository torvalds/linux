/* linux/inclue/media/videobuf2-cma-phys.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * CMA-phys memory allocator for videobuf2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _MEDIA_VIDEOBUF2_CMA_PHYS_H
#define _MEDIA_VIDEOBUF2_CMA_PHYS_H

#include <media/videobuf2-core.h>

static inline unsigned long vb2_cma_phys_plane_paddr(struct vb2_buffer *vb,
						unsigned int plane_no)
{
	return (unsigned long)vb2_plane_cookie(vb, plane_no);
}

struct vb2_alloc_ctx *vb2_cma_phys_init(struct device *dev, const char *type,
					unsigned long alignment,
					bool cacheable);
void vb2_cma_phys_cleanup(void *alloc_ctx);

struct vb2_alloc_ctx **vb2_cma_phys_init_multi(struct device *dev,
				  unsigned int num_planes, const char *types[],
				  unsigned long alignments[],
				  bool cacheable);
void vb2_cma_phys_cleanup_multi(void **alloc_ctxes);

void vb2_cma_phys_set_cacheable(void *alloc_ctx, bool cacheable);
bool vb2_cma_phys_get_cacheable(void *alloc_ctx);
int vb2_cma_phys_cache_flush(struct vb2_buffer *vb, u32 num_planes);
int vb2_cma_phys_cache_inv(struct vb2_buffer *vb, u32 num_planes);
int vb2_cma_phys_cache_clean(struct vb2_buffer *vb, u32 num_planes);
int vb2_cma_phys_cache_clean2(struct vb2_buffer *vb, u32 num_planes);

extern const struct vb2_mem_ops vb2_cma_phys_memops;

#endif

