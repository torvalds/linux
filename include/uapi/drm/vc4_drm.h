/*
 * Copyright © 2014-2015 Broadcom
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
 */

#ifndef _UAPI_VC4_DRM_H_
#define _UAPI_VC4_DRM_H_

#include "drm.h"

#define DRM_VC4_CREATE_BO                         0x03
#define DRM_VC4_MMAP_BO                           0x04
#define DRM_VC4_CREATE_SHADER_BO                  0x05

#define DRM_IOCTL_VC4_CREATE_BO           DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_CREATE_BO, struct drm_vc4_create_bo)
#define DRM_IOCTL_VC4_MMAP_BO             DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_MMAP_BO, struct drm_vc4_mmap_bo)
#define DRM_IOCTL_VC4_CREATE_SHADER_BO    DRM_IOWR(DRM_COMMAND_BASE + DRM_VC4_CREATE_SHADER_BO, struct drm_vc4_create_shader_bo)

/**
 * struct drm_vc4_create_bo - ioctl argument for creating VC4 BOs.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_vc4_create_bo {
	__u32 size;
	__u32 flags;
	/** Returned GEM handle for the BO. */
	__u32 handle;
	__u32 pad;
};

/**
 * struct drm_vc4_mmap_bo - ioctl argument for mapping VC4 BOs.
 *
 * This doesn't actually perform an mmap.  Instead, it returns the
 * offset you need to use in an mmap on the DRM device node.  This
 * means that tools like valgrind end up knowing about the mapped
 * memory.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_vc4_mmap_bo {
	/** Handle for the object being mapped. */
	__u32 handle;
	__u32 flags;
	/** offset into the drm node to use for subsequent mmap call. */
	__u64 offset;
};

/**
 * struct drm_vc4_create_shader_bo - ioctl argument for creating VC4
 * shader BOs.
 *
 * Since allowing a shader to be overwritten while it's also being
 * executed from would allow privlege escalation, shaders must be
 * created using this ioctl, and they can't be mmapped later.
 */
struct drm_vc4_create_shader_bo {
	/* Size of the data argument. */
	__u32 size;
	/* Flags, currently must be 0. */
	__u32 flags;

	/* Pointer to the data. */
	__u64 data;

	/** Returned GEM handle for the BO. */
	__u32 handle;
	/* Pad, must be 0. */
	__u32 pad;
};

#endif /* _UAPI_VC4_DRM_H_ */
