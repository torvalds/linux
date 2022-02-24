/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_DRM_FORMAT_HELPER_H
#define __LINUX_DRM_FORMAT_HELPER_H

struct drm_format_info;
struct drm_framebuffer;
struct drm_rect;

unsigned int drm_fb_clip_offset(unsigned int pitch, const struct drm_format_info *format,
				const struct drm_rect *clip);

void drm_fb_memcpy(void *dst, unsigned int dst_pitch, const void *vaddr,
		   const struct drm_framebuffer *fb, const struct drm_rect *clip);
void drm_fb_memcpy_toio(void __iomem *dst, unsigned int dst_pitch, const void *vaddr,
			const struct drm_framebuffer *fb, const struct drm_rect *clip);
void drm_fb_swab(void *dst, unsigned int dst_pitch, const void *src,
		 const struct drm_framebuffer *fb, const struct drm_rect *clip,
		 bool cached);
void drm_fb_xrgb8888_to_rgb332(void *dst, unsigned int dst_pitch, const void *vaddr,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip);
void drm_fb_xrgb8888_to_rgb565(void *dst, unsigned int dst_pitch, const void *vaddr,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip,
			       bool swab);
void drm_fb_xrgb8888_to_rgb565_toio(void __iomem *dst, unsigned int dst_pitch,
				    const void *vaddr, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb888(void *dst, unsigned int dst_pitch, const void *src,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip);
void drm_fb_xrgb8888_to_rgb888_toio(void __iomem *dst, unsigned int dst_pitch,
				    const void *vaddr, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip);
void drm_fb_xrgb8888_to_xrgb2101010_toio(void __iomem *dst, unsigned int dst_pitch,
					 const void *vaddr, const struct drm_framebuffer *fb,
					 const struct drm_rect *clip);
void drm_fb_xrgb8888_to_gray8(void *dst, unsigned int dst_pitch, const void *vaddr,
			      const struct drm_framebuffer *fb, const struct drm_rect *clip);

int drm_fb_blit_toio(void __iomem *dst, unsigned int dst_pitch, uint32_t dst_format,
		     const void *vmap, const struct drm_framebuffer *fb,
		     const struct drm_rect *rect);

#endif /* __LINUX_DRM_FORMAT_HELPER_H */
