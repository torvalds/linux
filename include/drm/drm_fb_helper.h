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

#include <drm/drm_crtc.h>
#include <linux/kgdb.h>

enum mode_set_atomic {
	LEAVE_ATOMIC_MODE_SET,
	ENTER_ATOMIC_MODE_SET,
};

struct drm_fb_offset {
	int x, y;
};

struct drm_fb_helper_crtc {
	struct drm_mode_set mode_set;
	struct drm_display_mode *desired_mode;
	int x, y;
};

/**
 * struct drm_fb_helper_surface_size - describes fbdev size and scanout surface size
 * @fb_width: fbdev width
 * @fb_height: fbdev height
 * @surface_width: scanout buffer width
 * @surface_height: scanout buffer height
 * @surface_bpp: scanout buffer bpp
 * @surface_depth: scanout buffer depth
 *
 * Note that the scanout surface width/height may be larger than the fbdev
 * width/height.  In case of multiple displays, the scanout surface is sized
 * according to the largest width/height (so it is large enough for all CRTCs
 * to scanout).  But the fbdev width/height is sized to the minimum width/
 * height of all the displays.  This ensures that fbcon fits on the smallest
 * of the attached displays.
 *
 * So what is passed to drm_fb_helper_fill_var() should be fb_width/fb_height,
 * rather than the surface size.
 */
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
 *
 * Driver callbacks used by the fbdev emulation helper library.
 */
struct drm_fb_helper_funcs {
	/**
	 * @gamma_set:
	 *
	 * Set the given gamma LUT register on the given CRTC.
	 *
	 * This callback is optional.
	 *
	 * FIXME:
	 *
	 * This callback is functionally redundant with the core gamma table
	 * support and simply exists because the fbdev hasn't yet been
	 * refactored to use the core gamma table interfaces.
	 */
	void (*gamma_set)(struct drm_crtc *crtc, u16 red, u16 green,
			  u16 blue, int regno);
	/**
	 * @gamma_get:
	 *
	 * Read the given gamma LUT register on the given CRTC, used to save the
	 * current LUT when force-restoring the fbdev for e.g. kdbg.
	 *
	 * This callback is optional.
	 *
	 * FIXME:
	 *
	 * This callback is functionally redundant with the core gamma table
	 * support and simply exists because the fbdev hasn't yet been
	 * refactored to use the core gamma table interfaces.
	 */
	void (*gamma_get)(struct drm_crtc *crtc, u16 *red, u16 *green,
			  u16 *blue, int regno);

	/**
	 * @fb_probe:
	 *
	 * Driver callback to allocate and initialize the fbdev info structure.
	 * Furthermore it also needs to allocate the DRM framebuffer used to
	 * back the fbdev.
	 *
	 * This callback is mandatory.
	 *
	 * RETURNS:
	 *
	 * The driver should return 0 on success and a negative error code on
	 * failure.
	 */
	int (*fb_probe)(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes);

	/**
	 * @initial_config:
	 *
	 * Driver callback to setup an initial fbdev display configuration.
	 * Drivers can use this callback to tell the fbdev emulation what the
	 * preferred initial configuration is. This is useful to implement
	 * smooth booting where the fbdev (and subsequently all userspace) never
	 * changes the mode, but always inherits the existing configuration.
	 *
	 * This callback is optional.
	 *
	 * RETURNS:
	 *
	 * The driver should return true if a suitable initial configuration has
	 * been filled out and false when the fbdev helper should fall back to
	 * the default probing logic.
	 */
	bool (*initial_config)(struct drm_fb_helper *fb_helper,
			       struct drm_fb_helper_crtc **crtcs,
			       struct drm_display_mode **modes,
			       struct drm_fb_offset *offsets,
			       bool *enabled, int width, int height);
};

struct drm_fb_helper_connector {
	struct drm_connector *connector;
};

/**
 * struct drm_fb_helper - main structure to emulate fbdev on top of KMS
 * @fb: Scanout framebuffer object
 * @dev: DRM device
 * @crtc_count: number of possible CRTCs
 * @crtc_info: per-CRTC helper state (mode, x/y offset, etc)
 * @connector_count: number of connected connectors
 * @connector_info_alloc_count: size of connector_info
 * @connector_info: array of per-connector information
 * @funcs: driver callbacks for fb helper
 * @fbdev: emulated fbdev device info struct
 * @pseudo_palette: fake palette of 16 colors
 * @dirty_clip: clip rectangle used with deferred_io to accumulate damage to
 *              the screen buffer
 * @dirty_lock: spinlock protecting @dirty_clip
 * @dirty_work: worker used to flush the framebuffer
 * @resume_work: worker used during resume if the console lock is already taken
 *
 * This is the main structure used by the fbdev helpers. Drivers supporting
 * fbdev emulation should embedded this into their overall driver structure.
 * Drivers must also fill out a struct &drm_fb_helper_funcs with a few
 * operations.
 */
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
	struct drm_clip_rect dirty_clip;
	spinlock_t dirty_lock;
	struct work_struct dirty_work;
	struct work_struct resume_work;

	/**
	 * @kernel_fb_list:
	 *
	 * Entry on the global kernel_fb_helper_list, used for kgdb entry/exit.
	 */
	struct list_head kernel_fb_list;

	/**
	 * @delayed_hotplug:
	 *
	 * A hotplug was received while fbdev wasn't in control of the DRM
	 * device, i.e. another KMS master was active. The output configuration
	 * needs to be reprobe when fbdev is in control again.
	 */
	bool delayed_hotplug;
};

#ifdef CONFIG_DRM_FBDEV_EMULATION
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

int drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper);

struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper);
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper);
void drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper);
void drm_fb_helper_fill_var(struct fb_info *info, struct drm_fb_helper *fb_helper,
			    uint32_t fb_width, uint32_t fb_height);
void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
			    uint32_t depth);

void drm_fb_helper_unlink_fbi(struct drm_fb_helper *fb_helper);

void drm_fb_helper_deferred_io(struct fb_info *info,
			       struct list_head *pagelist);

ssize_t drm_fb_helper_sys_read(struct fb_info *info, char __user *buf,
			       size_t count, loff_t *ppos);
ssize_t drm_fb_helper_sys_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos);

void drm_fb_helper_sys_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
void drm_fb_helper_sys_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
void drm_fb_helper_sys_imageblit(struct fb_info *info,
				 const struct fb_image *image);

void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image);

void drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper, bool suspend);
void drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper,
					bool suspend);

int drm_fb_helper_setcmap(struct fb_cmap *cmap, struct fb_info *info);

int drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper);
int drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel);
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
#else
static inline int drm_fb_helper_modinit(void)
{
	return 0;
}

static inline void drm_fb_helper_prepare(struct drm_device *dev,
					struct drm_fb_helper *helper,
					const struct drm_fb_helper_funcs *funcs)
{
}

static inline int drm_fb_helper_init(struct drm_device *dev,
		       struct drm_fb_helper *helper, int crtc_count,
		       int max_conn)
{
	return 0;
}

static inline void drm_fb_helper_fini(struct drm_fb_helper *helper)
{
}

static inline int drm_fb_helper_blank(int blank, struct fb_info *info)
{
	return 0;
}

static inline int drm_fb_helper_pan_display(struct fb_var_screeninfo *var,
					    struct fb_info *info)
{
	return 0;
}

static inline int drm_fb_helper_set_par(struct fb_info *info)
{
	return 0;
}

static inline int drm_fb_helper_check_var(struct fb_var_screeninfo *var,
					  struct fb_info *info)
{
	return 0;
}

static inline int
drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *fb_helper)
{
	return 0;
}

static inline struct fb_info *
drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
	return NULL;
}

static inline void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
}
static inline void drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper)
{
}

static inline void drm_fb_helper_fill_var(struct fb_info *info,
					  struct drm_fb_helper *fb_helper,
					  uint32_t fb_width, uint32_t fb_height)
{
}

static inline void drm_fb_helper_fill_fix(struct fb_info *info, uint32_t pitch,
					  uint32_t depth)
{
}

static inline int drm_fb_helper_setcmap(struct fb_cmap *cmap,
					struct fb_info *info)
{
	return 0;
}

static inline void drm_fb_helper_unlink_fbi(struct drm_fb_helper *fb_helper)
{
}

static inline void drm_fb_helper_deferred_io(struct fb_info *info,
					     struct list_head *pagelist)
{
}

static inline ssize_t drm_fb_helper_sys_read(struct fb_info *info,
					     char __user *buf, size_t count,
					     loff_t *ppos)
{
	return -ENODEV;
}

static inline ssize_t drm_fb_helper_sys_write(struct fb_info *info,
					      const char __user *buf,
					      size_t count, loff_t *ppos)
{
	return -ENODEV;
}

static inline void drm_fb_helper_sys_fillrect(struct fb_info *info,
					      const struct fb_fillrect *rect)
{
}

static inline void drm_fb_helper_sys_copyarea(struct fb_info *info,
					      const struct fb_copyarea *area)
{
}

static inline void drm_fb_helper_sys_imageblit(struct fb_info *info,
					       const struct fb_image *image)
{
}

static inline void drm_fb_helper_cfb_fillrect(struct fb_info *info,
					      const struct fb_fillrect *rect)
{
}

static inline void drm_fb_helper_cfb_copyarea(struct fb_info *info,
					      const struct fb_copyarea *area)
{
}

static inline void drm_fb_helper_cfb_imageblit(struct fb_info *info,
					       const struct fb_image *image)
{
}

static inline void drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper,
					     bool suspend)
{
}

static inline void
drm_fb_helper_set_suspend_unlocked(struct drm_fb_helper *fb_helper, bool suspend)
{
}

static inline int drm_fb_helper_hotplug_event(struct drm_fb_helper *fb_helper)
{
	return 0;
}

static inline int drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper,
					       int bpp_sel)
{
	return 0;
}

static inline int
drm_fb_helper_single_add_all_connectors(struct drm_fb_helper *fb_helper)
{
	return 0;
}

static inline int drm_fb_helper_debug_enter(struct fb_info *info)
{
	return 0;
}

static inline int drm_fb_helper_debug_leave(struct fb_info *info)
{
	return 0;
}

static inline struct drm_display_mode *
drm_has_preferred_mode(struct drm_fb_helper_connector *fb_connector,
		       int width, int height)
{
	return NULL;
}

static inline struct drm_display_mode *
drm_pick_cmdline_mode(struct drm_fb_helper_connector *fb_helper_conn,
		      int width, int height)
{
	return NULL;
}

static inline int
drm_fb_helper_add_one_connector(struct drm_fb_helper *fb_helper,
				struct drm_connector *connector)
{
	return 0;
}

static inline int
drm_fb_helper_remove_one_connector(struct drm_fb_helper *fb_helper,
				   struct drm_connector *connector)
{
	return 0;
}

#endif

static inline int
drm_fb_helper_remove_conflicting_framebuffers(struct apertures_struct *a,
					      const char *name, bool primary)
{
#if IS_REACHABLE(CONFIG_FB)
	return remove_conflicting_framebuffers(a, name, primary);
#else
	return 0;
#endif
}

#endif
