/*
 * Copyright (c) 2016 Intel Corporation
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
 */

#ifndef __DRM_FRAMEBUFFER_H__
#define __DRM_FRAMEBUFFER_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_modeset.h>

struct drm_framebuffer;
struct drm_file;
struct drm_device;

/**
 * struct drm_framebuffer_funcs - framebuffer hooks
 */
struct drm_framebuffer_funcs {
	/**
	 * @destroy:
	 *
	 * Clean up framebuffer resources, specifically also unreference the
	 * backing storage. The core guarantees to call this function for every
	 * framebuffer successfully created by ->fb_create() in
	 * &drm_mode_config_funcs. Drivers must also call
	 * drm_framebuffer_cleanup() to release DRM core resources for this
	 * framebuffer.
	 */
	void (*destroy)(struct drm_framebuffer *framebuffer);

	/**
	 * @create_handle:
	 *
	 * Create a buffer handle in the driver-specific buffer manager (either
	 * GEM or TTM) valid for the passed-in struct &drm_file. This is used by
	 * the core to implement the GETFB IOCTL, which returns (for
	 * sufficiently priviledged user) also a native buffer handle. This can
	 * be used for seamless transitions between modesetting clients by
	 * copying the current screen contents to a private buffer and blending
	 * between that and the new contents.
	 *
	 * GEM based drivers should call drm_gem_handle_create() to create the
	 * handle.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*create_handle)(struct drm_framebuffer *fb,
			     struct drm_file *file_priv,
			     unsigned int *handle);
	/**
	 * @dirty:
	 *
	 * Optional callback for the dirty fb IOCTL.
	 *
	 * Userspace can notify the driver via this callback that an area of the
	 * framebuffer has changed and should be flushed to the display
	 * hardware. This can also be used internally, e.g. by the fbdev
	 * emulation, though that's not the case currently.
	 *
	 * See documentation in drm_mode.h for the struct drm_mode_fb_dirty_cmd
	 * for more information as all the semantics and arguments have a one to
	 * one mapping on this function.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*dirty)(struct drm_framebuffer *framebuffer,
		     struct drm_file *file_priv, unsigned flags,
		     unsigned color, struct drm_clip_rect *clips,
		     unsigned num_clips);
};

struct drm_framebuffer {
	struct drm_device *dev;
	/*
	 * Note that the fb is refcounted for the benefit of driver internals,
	 * for example some hw, disabling a CRTC/plane is asynchronous, and
	 * scanout does not actually complete until the next vblank.  So some
	 * cleanup (like releasing the reference(s) on the backing GEM bo(s))
	 * should be deferred.  In cases like this, the driver would like to
	 * hold a ref to the fb even though it has already been removed from
	 * userspace perspective.
	 * The refcount is stored inside the mode object.
	 */
	/*
	 * Place on the dev->mode_config.fb_list, access protected by
	 * dev->mode_config.fb_lock.
	 */
	struct list_head head;
	struct drm_mode_object base;
	const struct drm_framebuffer_funcs *funcs;
	unsigned int pitches[4];
	unsigned int offsets[4];
	uint64_t modifier[4];
	unsigned int width;
	unsigned int height;
	/* depth can be 15 or 16 */
	unsigned int depth;
	int bits_per_pixel;
	int flags;
	uint32_t pixel_format; /* fourcc format */
	int hot_x;
	int hot_y;
	struct list_head filp_head;
};

int drm_framebuffer_init(struct drm_device *dev,
			 struct drm_framebuffer *fb,
			 const struct drm_framebuffer_funcs *funcs);
struct drm_framebuffer *drm_framebuffer_lookup(struct drm_device *dev,
					       uint32_t id);
void drm_framebuffer_remove(struct drm_framebuffer *fb);
void drm_framebuffer_cleanup(struct drm_framebuffer *fb);
void drm_framebuffer_unregister_private(struct drm_framebuffer *fb);

/**
 * drm_framebuffer_reference - incr the fb refcnt
 * @fb: framebuffer
 *
 * This functions increments the fb's refcount.
 */
static inline void drm_framebuffer_reference(struct drm_framebuffer *fb)
{
	drm_mode_object_reference(&fb->base);
}

/**
 * drm_framebuffer_unreference - unref a framebuffer
 * @fb: framebuffer to unref
 *
 * This functions decrements the fb's refcount and frees it if it drops to zero.
 */
static inline void drm_framebuffer_unreference(struct drm_framebuffer *fb)
{
	drm_mode_object_unreference(&fb->base);
}

/**
 * drm_framebuffer_read_refcount - read the framebuffer reference count.
 * @fb: framebuffer
 *
 * This functions returns the framebuffer's reference count.
 */
static inline uint32_t drm_framebuffer_read_refcount(struct drm_framebuffer *fb)
{
	return atomic_read(&fb->base.refcount.refcount);
}
#endif
