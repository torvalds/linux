/*
 * Copyright © 2008 Intel Corporation
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
 *     Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 */
#ifndef _DRM_MEM_UTIL_H_
#define _DRM_MEM_UTIL_H_

#include <linux/vmalloc.h>

static __inline__ void *drm_calloc_large(size_t nmemb, size_t size)
{
	if (size != 0 && nmemb > SIZE_MAX / size)
		return NULL;

	if (size * nmemb <= PAGE_SIZE)
	    return kcalloc(nmemb, size, GFP_KERNEL);

	return vzalloc(size * nmemb);
}

/* Modeled after cairo's malloc_ab, it's like calloc but without the zeroing. */
static __inline__ void *drm_malloc_ab(size_t nmemb, size_t size)
{
	if (size != 0 && nmemb > SIZE_MAX / size)
		return NULL;

	if (size * nmemb <= PAGE_SIZE)
	    return kmalloc(nmemb * size, GFP_KERNEL);

	return vmalloc(size * nmemb);
}

static __inline__ void *drm_malloc_gfp(size_t nmemb, size_t size, gfp_t gfp)
{
	if (size != 0 && nmemb > SIZE_MAX / size)
		return NULL;

	if (size * nmemb <= PAGE_SIZE)
		return kmalloc(nmemb * size, gfp);

	if (gfp & __GFP_RECLAIMABLE) {
		void *ptr = kmalloc(nmemb * size,
				    gfp | __GFP_NOWARN | __GFP_NORETRY);
		if (ptr)
			return ptr;
	}

	return __vmalloc(size * nmemb, gfp, PAGE_KERNEL);
}

static __inline void drm_free_large(void *ptr)
{
	kvfree(ptr);
}

#endif
