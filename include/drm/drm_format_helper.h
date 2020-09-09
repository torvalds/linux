/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_DRM_FORMAT_HELPER_H
#define __LINUX_DRM_FORMAT_HELPER_H

struct drm_framebuffer;
struct drm_rect;

void drm_fb_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip);
void drm_fb_memcpy_dstclip(void __iomem *dst, void *vaddr,
			   struct drm_framebuffer *fb,
			   struct drm_rect *clip);
void drm_fb_swab(void *dst, void *src, struct drm_framebuffer *fb,
		 struct drm_rect *clip, bool cached);
void drm_fb_xrgb8888_to_rgb565(void *dst, void *vaddr,
			       struct drm_framebuffer *fb,
			       struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb565_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb888_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip);
void drm_fb_xrgb8888_to_gray8(u8 *dst, void *vaddr, struct drm_framebuffer *fb,
			      struct drm_rect *clip);

#endif /* __LINUX_DRM_FORMAT_HELPER_H */
