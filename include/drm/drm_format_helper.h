/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_DRM_FORMAT_HELPER_H
#define __LINUX_DRM_FORMAT_HELPER_H

struct drm_framebuffer;
struct drm_rect;

void drm_fb_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip);
void drm_fb_memcpy_dstclip(void *dst, void *vaddr, struct drm_framebuffer *fb,
			   struct drm_rect *clip);
void drm_fb_swab16(u16 *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip);
void drm_fb_xrgb8888_to_rgb565(u16 *dst, void *vaddr,
			       struct drm_framebuffer *fb,
			       struct drm_rect *clip, bool swap);
void drm_fb_xrgb8888_to_gray8(u8 *dst, void *vaddr, struct drm_framebuffer *fb,
			      struct drm_rect *clip);

#endif /* __LINUX_DRM_FORMAT_HELPER_H */
