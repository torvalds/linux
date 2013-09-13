/*
 * Copyright 2013 Red Hat
 * All Rights Reserved.
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
 * THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef QXL_DRM_H
#define QXL_DRM_H

#include <stddef.h>
#include "drm/drm.h"

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use uint64_t instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define QXL_GEM_DOMAIN_CPU 0
#define QXL_GEM_DOMAIN_VRAM 1
#define QXL_GEM_DOMAIN_SURFACE 2

#define DRM_QXL_ALLOC       0x00
#define DRM_QXL_MAP         0x01
#define DRM_QXL_EXECBUFFER  0x02
#define DRM_QXL_UPDATE_AREA 0x03
#define DRM_QXL_GETPARAM    0x04
#define DRM_QXL_CLIENTCAP   0x05

#define DRM_QXL_ALLOC_SURF  0x06

struct drm_qxl_alloc {
	uint32_t size;
	uint32_t handle; /* 0 is an invalid handle */
};

struct drm_qxl_map {
	uint64_t offset; /* use for mmap system call */
	uint32_t handle;
	uint32_t pad;
};

/*
 * dest is the bo we are writing the relocation into
 * src is bo we are relocating.
 * *(dest_handle.base_addr + dest_offset) = physical_address(src_handle.addr +
 * src_offset)
 */
#define QXL_RELOC_TYPE_BO 1
#define QXL_RELOC_TYPE_SURF 2

struct drm_qxl_reloc {
	uint64_t src_offset; /* offset into src_handle or src buffer */
	uint64_t dst_offset; /* offset in dest handle */
	uint32_t src_handle; /* dest handle to compute address from */
	uint32_t dst_handle; /* 0 if to command buffer */
	uint32_t reloc_type;
	uint32_t pad;
};

struct drm_qxl_command {
	uint64_t	 __user command; /* void* */
	uint64_t	 __user relocs; /* struct drm_qxl_reloc* */
	uint32_t		type;
	uint32_t		command_size;
	uint32_t		relocs_num;
	uint32_t                pad;
};

/* XXX: call it drm_qxl_commands? */
struct drm_qxl_execbuffer {
	uint32_t		flags;		/* for future use */
	uint32_t		commands_num;
	uint64_t	 __user commands;	/* struct drm_qxl_command* */
};

struct drm_qxl_update_area {
	uint32_t handle;
	uint32_t top;
	uint32_t left;
	uint32_t bottom;
	uint32_t right;
	uint32_t pad;
};

#define QXL_PARAM_NUM_SURFACES 1 /* rom->n_surfaces */
#define QXL_PARAM_MAX_RELOCS 2
struct drm_qxl_getparam {
	uint64_t param;
	uint64_t value;
};

/* these are one bit values */
struct drm_qxl_clientcap {
	uint32_t index;
	uint32_t pad;
};

struct drm_qxl_alloc_surf {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	int32_t stride;
	uint32_t handle;
	uint32_t pad;
};

#define DRM_IOCTL_QXL_ALLOC \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_ALLOC, struct drm_qxl_alloc)

#define DRM_IOCTL_QXL_MAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_MAP, struct drm_qxl_map)

#define DRM_IOCTL_QXL_EXECBUFFER \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_EXECBUFFER,\
		struct drm_qxl_execbuffer)

#define DRM_IOCTL_QXL_UPDATE_AREA \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_UPDATE_AREA,\
		struct drm_qxl_update_area)

#define DRM_IOCTL_QXL_GETPARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_GETPARAM,\
		struct drm_qxl_getparam)

#define DRM_IOCTL_QXL_CLIENTCAP \
	DRM_IOW(DRM_COMMAND_BASE + DRM_QXL_CLIENTCAP,\
		struct drm_qxl_clientcap)

#define DRM_IOCTL_QXL_ALLOC_SURF \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_QXL_ALLOC_SURF,\
		struct drm_qxl_alloc_surf)

#endif
