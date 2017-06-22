/*
 * Copyright © 2012 Red Hat
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *      Rob Clark <rob.clark@linaro.org>
 *
 */

#ifndef __DRM_PRIME_H__
#define __DRM_PRIME_H__

#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>

/**
 * struct drm_prime_file_private - per-file tracking for PRIME
 *
 * This just contains the internal &struct dma_buf and handle caches for each
 * &struct drm_file used by the PRIME core code.
 */

struct drm_prime_file_private {
/* private: */
	struct mutex lock;
	struct rb_root dmabufs;
	struct rb_root handles;
};

struct dma_buf_export_info;
struct dma_buf;

struct drm_device;
struct drm_gem_object;
struct drm_file;

struct dma_buf *drm_gem_prime_export(struct drm_device *dev,
				     struct drm_gem_object *obj,
				     int flags);
int drm_gem_prime_handle_to_fd(struct drm_device *dev,
			       struct drm_file *file_priv, uint32_t handle, uint32_t flags,
			       int *prime_fd);
struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf);
int drm_gem_prime_fd_to_handle(struct drm_device *dev,
			       struct drm_file *file_priv, int prime_fd, uint32_t *handle);
struct dma_buf *drm_gem_dmabuf_export(struct drm_device *dev,
				      struct dma_buf_export_info *exp_info);
void drm_gem_dmabuf_release(struct dma_buf *dma_buf);

int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_pages);
struct sg_table *drm_prime_pages_to_sg(struct page **pages, unsigned int nr_pages);
void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg);


#endif /* __DRM_PRIME_H__ */
