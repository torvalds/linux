/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_TINYDRM_H
#define __LINUX_TINYDRM_H

#include <linux/mutex.h>
#include <drm/drm_simple_kms_helper.h>

struct drm_clip_rect;
struct drm_driver;
struct drm_file;
struct drm_framebuffer;
struct drm_framebuffer_funcs;

/**
 * struct tinydrm_device - tinydrm device
 */
struct tinydrm_device {
	/**
	 * @drm: DRM device
	 */
	struct drm_device *drm;

	/**
	 * @pipe: Display pipe structure
	 */
	struct drm_simple_display_pipe pipe;

	/**
	 * @dirty_lock: Serializes framebuffer flushing
	 */
	struct mutex dirty_lock;

	/**
	 * @fb_funcs: Framebuffer functions used when creating framebuffers
	 */
	const struct drm_framebuffer_funcs *fb_funcs;

	/**
	 * @fb_dirty: Framebuffer dirty callback
	 */
	int (*fb_dirty)(struct drm_framebuffer *framebuffer,
			struct drm_file *file_priv, unsigned flags,
			unsigned color, struct drm_clip_rect *clips,
			unsigned num_clips);
};

static inline struct tinydrm_device *
pipe_to_tinydrm(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct tinydrm_device, pipe);
}

/**
 * TINYDRM_MODE - tinydrm display mode
 * @hd: Horizontal resolution, width
 * @vd: Vertical resolution, height
 * @hd_mm: Display width in millimeters
 * @vd_mm: Display height in millimeters
 *
 * This macro creates a &drm_display_mode for use with tinydrm.
 */
#define TINYDRM_MODE(hd, vd, hd_mm, vd_mm) \
	.hdisplay = (hd), \
	.hsync_start = (hd), \
	.hsync_end = (hd), \
	.htotal = (hd), \
	.vdisplay = (vd), \
	.vsync_start = (vd), \
	.vsync_end = (vd), \
	.vtotal = (vd), \
	.width_mm = (hd_mm), \
	.height_mm = (vd_mm), \
	.type = DRM_MODE_TYPE_DRIVER, \
	.clock = 1 /* pass validation */

int devm_tinydrm_init(struct device *parent, struct tinydrm_device *tdev,
		      const struct drm_framebuffer_funcs *fb_funcs,
		      struct drm_driver *driver);
int devm_tinydrm_register(struct tinydrm_device *tdev);
void tinydrm_shutdown(struct tinydrm_device *tdev);

void tinydrm_display_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state);
int
tinydrm_display_pipe_init(struct tinydrm_device *tdev,
			  const struct drm_simple_display_pipe_funcs *funcs,
			  int connector_type,
			  const uint32_t *formats,
			  unsigned int format_count,
			  const struct drm_display_mode *mode,
			  unsigned int rotation);

#endif /* __LINUX_TINYDRM_H */
