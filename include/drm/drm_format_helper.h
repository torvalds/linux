/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_DRM_FORMAT_HELPER_H
#define __LINUX_DRM_FORMAT_HELPER_H

#include <linux/types.h>

struct drm_device;
struct drm_format_info;
struct drm_framebuffer;
struct drm_rect;

struct iosys_map;

/**
 * struct drm_format_conv_state - Stores format-conversion state
 *
 * DRM helpers for format conversion store temporary state in
 * struct drm_xfrm_buf. The buffer's resources can be reused
 * among multiple conversion operations.
 *
 * All fields are considered private.
 */
struct drm_format_conv_state {
	/* private: */
	struct {
		void *mem;
		size_t size;
		bool preallocated;
	} tmp;
};

#define __DRM_FORMAT_CONV_STATE_INIT(_mem, _size, _preallocated) { \
		.tmp = { \
			.mem = (_mem), \
			.size = (_size), \
			.preallocated = (_preallocated), \
		} \
	}

/**
 * DRM_FORMAT_CONV_STATE_INIT - Initializer for struct drm_format_conv_state
 *
 * Initializes an instance of struct drm_format_conv_state to default values.
 */
#define DRM_FORMAT_CONV_STATE_INIT \
	__DRM_FORMAT_CONV_STATE_INIT(NULL, 0, false)

/**
 * DRM_FORMAT_CONV_STATE_INIT_PREALLOCATED - Initializer for struct drm_format_conv_state
 * @_mem: The preallocated memory area
 * @_size: The number of bytes in _mem
 *
 * Initializes an instance of struct drm_format_conv_state to preallocated
 * storage. The caller is responsible for releasing the provided memory range.
 */
#define DRM_FORMAT_CONV_STATE_INIT_PREALLOCATED(_mem, _size) \
	__DRM_FORMAT_CONV_STATE_INIT(_mem, _size, true)

void drm_format_conv_state_init(struct drm_format_conv_state *state);
void drm_format_conv_state_copy(struct drm_format_conv_state *state,
				const struct drm_format_conv_state *old_state);
void *drm_format_conv_state_reserve(struct drm_format_conv_state *state,
				    size_t new_size, gfp_t flags);
void drm_format_conv_state_release(struct drm_format_conv_state *state);

unsigned int drm_fb_clip_offset(unsigned int pitch, const struct drm_format_info *format,
				const struct drm_rect *clip);

void drm_fb_memcpy(struct iosys_map *dst, const unsigned int *dst_pitch,
		   const struct iosys_map *src, const struct drm_framebuffer *fb,
		   const struct drm_rect *clip);
void drm_fb_swab(struct iosys_map *dst, const unsigned int *dst_pitch,
		 const struct iosys_map *src, const struct drm_framebuffer *fb,
		 const struct drm_rect *clip, bool cached,
		 struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_rgb332(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_rgb565(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state,
			       bool swab);
void drm_fb_xrgb8888_to_xrgb1555(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_argb1555(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_rgba5551(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_rgb888(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_bgr888(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_argb8888(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_xrgb2101010(struct iosys_map *dst, const unsigned int *dst_pitch,
				    const struct iosys_map *src, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip,
				    struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_argb2101010(struct iosys_map *dst, const unsigned int *dst_pitch,
				    const struct iosys_map *src, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip,
				    struct drm_format_conv_state *state);
void drm_fb_xrgb8888_to_gray8(struct iosys_map *dst, const unsigned int *dst_pitch,
			      const struct iosys_map *src, const struct drm_framebuffer *fb,
			      const struct drm_rect *clip, struct drm_format_conv_state *state);
void drm_fb_argb8888_to_argb4444(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state);

int drm_fb_blit(struct iosys_map *dst, const unsigned int *dst_pitch, uint32_t dst_format,
		const struct iosys_map *src, const struct drm_framebuffer *fb,
		const struct drm_rect *clip, struct drm_format_conv_state *state);

void drm_fb_xrgb8888_to_mono(struct iosys_map *dst, const unsigned int *dst_pitch,
			     const struct iosys_map *src, const struct drm_framebuffer *fb,
			     const struct drm_rect *clip, struct drm_format_conv_state *state);

size_t drm_fb_build_fourcc_list(struct drm_device *dev,
				const u32 *native_fourccs, size_t native_nfourccs,
				u32 *fourccs_out, size_t nfourccs_out);

#endif /* __LINUX_DRM_FORMAT_HELPER_H */
