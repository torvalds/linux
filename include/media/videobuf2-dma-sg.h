/*
 * videobuf2-dma-sg.h - DMA scatter/gather memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef _MEDIA_VIDEOBUF2_DMA_SG_H
#define _MEDIA_VIDEOBUF2_DMA_SG_H

#include <media/videobuf2-core.h>

static inline struct sg_table *vb2_dma_sg_plane_desc(
		struct vb2_buffer *vb, unsigned int plane_no)
{
	return (struct sg_table *)vb2_plane_cookie(vb, plane_no);
}

void *vb2_dma_sg_init_ctx(struct device *dev);
void vb2_dma_sg_cleanup_ctx(void *alloc_ctx);

extern const struct vb2_mem_ops vb2_dma_sg_memops;

#endif
