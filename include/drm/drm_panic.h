/* SPDX-License-Identifier: GPL-2.0 or MIT */

/*
 * Copyright (c) 2024 Intel
 * Copyright (c) 2024 Red Hat
 */

#ifndef __DRM_PANIC_H__
#define __DRM_PANIC_H__

#include <linux/types.h>
#include <linux/iosys-map.h>

#include <drm/drm_fourcc.h>

struct page;

enum drm_panic_type {
	DRM_PANIC_TYPE_KMSG,
	DRM_PANIC_TYPE_USER,
	DRM_PANIC_TYPE_QR,
};

/**
 * struct drm_scanout_buffer - DRM scanout buffer
 *
 * This structure holds the information necessary for drm_panic to draw the
 * panic screen, and display it.
 */
struct drm_scanout_buffer {
	/**
	 * @format:
	 *
	 * drm format of the scanout buffer.
	 */
	const struct drm_format_info *format;

	/**
	 * @map:
	 *
	 * Virtual address of the scanout buffer, either in memory or iomem.
	 * The scanout buffer should be in linear format, and can be directly
	 * sent to the display hardware. Tearing is not an issue for the panic
	 * screen.
	 */
	struct iosys_map map[DRM_FORMAT_MAX_PLANES];

	/**
	 * @pages: Optional, if the scanout buffer is not mapped, set this field
	 * to the array of pages of the scanout buffer. The panic code will use
	 * kmap_local_page_try_from_panic() to map one page at a time to write
	 * all the pixels. This array shouldn't be allocated from the
	 * get_scanoutbuffer() callback.
	 * The scanout buffer should be in linear format.
	 */
	struct page **pages;

	/**
	 * @width: Width of the scanout buffer, in pixels.
	 */
	unsigned int width;

	/**
	 * @height: Height of the scanout buffer, in pixels.
	 */
	unsigned int height;

	/**
	 * @pitch: Length in bytes between the start of two consecutive lines.
	 */
	unsigned int pitch[DRM_FORMAT_MAX_PLANES];

	/**
	 * @set_pixel: Optional function, to set a pixel color on the
	 * framebuffer. It allows to handle special tiling format inside the
	 * driver. It takes precedence over the @map and @pages fields.
	 */
	void (*set_pixel)(struct drm_scanout_buffer *sb, unsigned int x,
			  unsigned int y, u32 color);

	/**
	 * @private: private pointer that you can use in the callbacks
	 * set_pixel()
	 */
	void *private;
};

#endif /* __DRM_PANIC_H__ */
