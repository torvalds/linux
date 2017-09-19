/*
 * Copyright (C) 2017 Oracle Corporation
 * This file is based on ????.c?
 * Copyright 2017 Canonical
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Andreas Pokorny
 */

#include "vbox_drv.h"

/*
 * Based on qxl_prime.c:
 * Empty Implementations as there should not be any other driver for a virtual
 * device that might share buffers with vboxvideo
 */

int vbox_gem_prime_pin(struct drm_gem_object *obj)
{
	WARN_ONCE(1, "not implemented");
	return -ENOSYS;
}

void vbox_gem_prime_unpin(struct drm_gem_object *obj)
{
	WARN_ONCE(1, "not implemented");
}

struct sg_table *vbox_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	WARN_ONCE(1, "not implemented");
	return ERR_PTR(-ENOSYS);
}

struct drm_gem_object *vbox_gem_prime_import_sg_table(struct drm_device *dev,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0) && !defined(RHEL_72)
						      size_t size,
#else
						      struct dma_buf_attachment
						      *attach,
#endif
						      struct sg_table *table)
{
	WARN_ONCE(1, "not implemented");
	return ERR_PTR(-ENOSYS);
}

void *vbox_gem_prime_vmap(struct drm_gem_object *obj)
{
	WARN_ONCE(1, "not implemented");
	return ERR_PTR(-ENOSYS);
}

void vbox_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	WARN_ONCE(1, "not implemented");
}

int vbox_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *area)
{
	WARN_ONCE(1, "not implemented");
	return -ENOSYS;
}
