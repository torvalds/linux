/*
 * videobuf2-memops.h - generic memory handling routines for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *	   Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef _MEDIA_VIDEOBUF2_MEMOPS_H
#define _MEDIA_VIDEOBUF2_MEMOPS_H

#include <media/videobuf2-v4l2.h>
#include <linux/mm.h>
#include <linux/refcount.h>

/**
 * struct vb2_vmarea_handler - common vma refcount tracking handler.
 *
 * @refcount:	pointer to &refcount_t entry in the buffer.
 * @put:	callback to function that decreases buffer refcount.
 * @arg:	argument for @put callback.
 */
struct vb2_vmarea_handler {
	refcount_t		*refcount;
	void			(*put)(void *arg);
	void			*arg;
};

extern const struct vm_operations_struct vb2_common_vm_ops;

struct frame_vector *vb2_create_framevec(unsigned long start,
					 unsigned long length,
					 bool write);
void vb2_destroy_framevec(struct frame_vector *vec);

#endif
