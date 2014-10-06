/*
 * Copyright (c) 2006-2009 Red Hat Inc.
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 *
 * DRM framebuffer helper functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#ifndef DRM_FB_HELPER_H
#define DRM_FB_HELPER_H

struct drm_fb_helper;

#include <linux/kgdb.h>

struct drm_fb_helper_crtc {
	struct drm_mode_set mode_set;
	struct drm_display_mode *desired_mode;
};

struct drm_fb_helper_surface_size {
	u32 fb_width;
	u32 fb_height;
	u32 surface_width;
	u32 surface_height;
	u32 surface_bpp;
	u32 surface_depth;
};

/**
 * struct drm_fb_helper_funcs - driver callbacks for the fbdev emulation library
 * @gamma_set: Set the given gamma lut register on the given crtc.
 * @gamma_get: Read the given gamma lut register on the given crtc, used to
 *             save the current lut when force-restoring the fbdev for e.g.
 *             kdbg.
 * @fb_probe: Driver callback to allocate and initialize the fbdev info
 *            structure. Furthermore it also needs to allocate the drm
 *            framebuffer used to back the fbdev.
 * @initial_config: Setup an initial fbdev display configuration
 *
 * Driver callbacks used by the fbdev emulation helper library.
 */
struct drm_fb_helper_funcs {
	void (*gamma_set)(struct drm_crtc *crtc, u16 red, u16 green,
			  u16 blue, int regno);
	void (*gamma_get)(struct drm_crtc *crtc, u16 *red, u16 *green,
			  u16 *blue, int regno);

	int (*fb_probe)(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes);
	bool (*initial_config)(struct drm_fb_helper *fb_helper,
			       struct drm_fb_helper_crtc **crtcs,
			       struct drm_display_mode **modes,
			       bool *enabled, int width, int height);
};

struct drm_fb_helper_connector {
	struct drm_connector *connector;
	struct drm_cmdline_mode cmdline_mode;
};

struct drm_fb_helper {
	struct drm_framebuffer *fb;
	struct drm_device *dev;
	int crtc_count;
	struct drm_fb_helper_crtc *crtc_info;
	int connector_count;
	int connector_info_alloc_count;
	struct drm_fb_helper_connector **connector_info;
	const struct drm_fb_helper_funcs *funcs;
	struct fb_info *fbdev;
	u32 pseudo_palette[17];
	struct list_head kernel_fb_list;

	/* we got a hotplug but fbdev wasn't running the console
	   delay until next set_par */
	bool delayed_hotplug;
};

void drm_fb_helper_prepare(struct drm_device *dev, struct drm_fb_helper *helper,
			   const struct drm_fb_helper_funcs *funcs);
int drm_fb_helper_init(struct drm_device *dev,
		       struct drm_fb_helper *helper, int crtc_count,
		       int max_conn);
void drm_fb_helper_fini(struct drm_fb_helper *helper);
int drm_fb_helper_blank(int blank, struct fb_info *info);
int drm_fb_helper_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info);
int drm_fb_helper_set_par(struct fb_info *info);
int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info);

bool drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper);
void drm_fb_helper_fill_var(struct fb_info *info, struct drm_fb_helper *fb_helper,
			    uint32_t fb_width, uint32_t fb_height);
void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
			    uint32_t depth);

int drm_fb_helper_setcmap(struct fb_cmap *cmap, struct fb_info *info);

int drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper);
bool drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel);
int drm_fb_helper_single_add_all_connectors(struct drm_fb_helper *fb_helper);
int drm_fb_helper_debug_enter(struct fb_info *info);
int drm_fb_helper_debug_leave(struct fb_info *info);
struct drm_display_mode *
drm_has_preferred_mode(struct drm_fb_helper_connector *fb_connector,
			int width, int height);
struct drm_display_mode *
drm_pick_cmdline_mode(struct drm_fb_helper_connector *fb_helper_conn,
		      int width, int height);

int drm_fb_helper_add_one_connector(struct drm_fb_helper *fb_helper, struct drm_connector *connector);
int drm_fb_helper_remove_one_connector(struct drm_fb_helper *fb_helper,
				       struct drm_connector *connector);
#endif
