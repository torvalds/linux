/*
 * helper functions for vmalloc capture buffers
 *
 * The functions expect the hardware being able to scatter gatter
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */
#ifndef _VIDEOBUF_VMALLOC_H
#define _VIDEOBUF_VMALLOC_H

#include <media/videobuf-core.h>

/* --------------------------------------------------------------------- */

struct videobuf_vmalloc_memory
{
	u32                 magic;

	void                *vmalloc;

	/* remap_vmalloc_range seems to need to run after mmap() on some cases */
	struct vm_area_struct *vma;
};

void videobuf_queue_vmalloc_init(struct videobuf_queue* q,
			 struct videobuf_queue_ops *ops,
			 void *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv);

void *videobuf_to_vmalloc (struct videobuf_buffer *buf);

void videobuf_vmalloc_free (struct videobuf_buffer *buf);

#endif
