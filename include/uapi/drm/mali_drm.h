/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_DRM_H__
#define __MALI_DRM_H__

/* Mali specific ioctls */
#define NOT_USED_0_3
#define DRM_MALI_FB_ALLOC   0x04
#define DRM_MALI_FB_FREE            0x05
#define NOT_USED_6_12
#define DRM_MALI_MEM_INIT   0x13
#define DRM_MALI_MEM_ALLOC  0x14
#define DRM_MALI_MEM_FREE   0x15
#define DRM_MALI_FB_INIT            0x16

#define DRM_IOCTL_MALI_FB_ALLOC     DRM_IOWR(DRM_COMMAND_BASE + DRM_MALI_FB_ALLOC, drm_mali_mem_t)
#define DRM_IOCTL_MALI_FB_FREE      DRM_IOW( DRM_COMMAND_BASE + DRM_MALI_FB_FREE, drm_mali_mem_t)
#define DRM_IOCTL_MALI_MEM_INIT     DRM_IOWR(DRM_COMMAND_BASE + DRM_MALI_MEM_INIT, drm_mali_mem_t)
#define DRM_IOCTL_MALI_MEM_ALLOC        DRM_IOWR(DRM_COMMAND_BASE + DRM_MALI_MEM_ALLOC, drm_mali_mem_t)
#define DRM_IOCTL_MALI_MEM_FREE     DRM_IOW( DRM_COMMAND_BASE + DRM_MALI_MEM_FREE, drm_mali_mem_t)
#define DRM_IOCTL_MALI_FB_INIT      DRM_IOW( DRM_COMMAND_BASE + DRM_MALI_FB_INIT, drm_mali_fb_t)

typedef struct
{
	int context;
	unsigned int offset;
	unsigned int size;
	unsigned long free;
} drm_mali_mem_t;

typedef struct
{
	unsigned int offset, size;
} drm_mali_fb_t;

#endif /* __MALI_DRM_H__ */
