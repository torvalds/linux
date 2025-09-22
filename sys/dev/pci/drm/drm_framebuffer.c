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

#include <linux/export.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_auth.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_print.h>
#include <drm/drm_util.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * Frame buffers are abstract memory objects that provide a source of pixels to
 * scanout to a CRTC. Applications explicitly request the creation of frame
 * buffers through the DRM_IOCTL_MODE_ADDFB(2) ioctls and receive an opaque
 * handle that can be passed to the KMS CRTC control, plane configuration and
 * page flip functions.
 *
 * Frame buffers rely on the underlying memory manager for allocating backing
 * storage. When creating a frame buffer applications pass a memory handle
 * (or a list of memory handles for multi-planar formats) through the
 * &struct drm_mode_fb_cmd2 argument. For drivers using GEM as their userspace
 * buffer management interface this would be a GEM handle.  Drivers are however
 * free to use their own backing storage object handles, e.g. vmwgfx directly
 * exposes special TTM handles to userspace and so expects TTM handles in the
 * create ioctl and not GEM handles.
 *
 * Framebuffers are tracked with &struct drm_framebuffer. They are published
 * using drm_framebuffer_init() - after calling that function userspace can use
 * and access the framebuffer object. The helper function
 * drm_helper_mode_fill_fb_struct() can be used to pre-fill the required
 * metadata fields.
 *
 * The lifetime of a drm framebuffer is controlled with a reference count,
 * drivers can grab additional references with drm_framebuffer_get() and drop
 * them again with drm_framebuffer_put(). For driver-private framebuffers for
 * which the last reference is never dropped (e.g. for the fbdev framebuffer
 * when the struct &struct drm_framebuffer is embedded into the fbdev helper
 * struct) drivers can manually clean up a framebuffer at module unload time
 * with drm_framebuffer_unregister_private(). But doing this is not
 * recommended, and it's better to have a normal free-standing &struct
 * drm_framebuffer.
 */

int drm_framebuffer_check_src_coords(uint32_t src_x, uint32_t src_y,
				     uint32_t src_w, uint32_t src_h,
				     const struct drm_framebuffer *fb)
{
	unsigned int fb_width, fb_height;

	fb_width = fb->width << 16;
	fb_height = fb->height << 16;

	/* Make sure source coordinates are inside the fb. */
	if (src_w > fb_width ||
	    src_x > fb_width - src_w ||
	    src_h > fb_height ||
	    src_y > fb_height - src_h) {
		drm_dbg_kms(fb->dev, "Invalid source coordinates "
			    "%u.%06ux%u.%06u+%u.%06u+%u.%06u (fb %ux%u)\n",
			    src_w >> 16, ((src_w & 0xffff) * 15625) >> 10,
			    src_h >> 16, ((src_h & 0xffff) * 15625) >> 10,
			    src_x >> 16, ((src_x & 0xffff) * 15625) >> 10,
			    src_y >> 16, ((src_y & 0xffff) * 15625) >> 10,
			    fb->width, fb->height);
		return -ENOSPC;
	}

	return 0;
}

/**
 * drm_mode_addfb - add an FB to the graphics configuration
 * @dev: drm device for the ioctl
 * @or: pointer to request structure
 * @file_priv: drm file
 *
 * Add a new FB to the specified CRTC, given a user request. This is the
 * original addfb ioctl which only supported RGB formats.
 *
 * Called by the user via ioctl, or by an in-kernel client.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_addfb(struct drm_device *dev, struct drm_mode_fb_cmd *or,
		   struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd2 r = {};
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	r.pixel_format = drm_driver_legacy_fb_format(dev, or->bpp, or->depth);
	if (r.pixel_format == DRM_FORMAT_INVALID) {
		drm_dbg_kms(dev, "bad {bpp:%d, depth:%d}\n", or->bpp, or->depth);
		return -EINVAL;
	}

	/* convert to new format and call new ioctl */
	r.fb_id = or->fb_id;
	r.width = or->width;
	r.height = or->height;
	r.pitches[0] = or->pitch;
	r.handles[0] = or->handle;

	ret = drm_mode_addfb2(dev, &r, file_priv);
	if (ret)
		return ret;

	or->fb_id = r.fb_id;

	return 0;
}

int drm_mode_addfb_ioctl(struct drm_device *dev,
			 void *data, struct drm_file *file_priv)
{
	return drm_mode_addfb(dev, data, file_priv);
}

static int framebuffer_check(struct drm_device *dev,
			     const struct drm_mode_fb_cmd2 *r)
{
	const struct drm_format_info *info;
	int i;

	/* check if the format is supported at all */
	if (!__drm_format_info(r->pixel_format)) {
		drm_dbg_kms(dev, "bad framebuffer format %p4cc\n",
			    &r->pixel_format);
		return -EINVAL;
	}

	if (r->width == 0) {
		drm_dbg_kms(dev, "bad framebuffer width %u\n", r->width);
		return -EINVAL;
	}

	if (r->height == 0) {
		drm_dbg_kms(dev, "bad framebuffer height %u\n", r->height);
		return -EINVAL;
	}

	/* now let the driver pick its own format info */
	info = drm_get_format_info(dev, r);

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = drm_format_info_plane_width(info, r->width, i);
		unsigned int height = drm_format_info_plane_height(info, r->height, i);
		unsigned int block_size = info->char_per_block[i];
		u64 min_pitch = drm_format_info_min_pitch(info, i, width);

		if (!block_size && (r->modifier[i] == DRM_FORMAT_MOD_LINEAR)) {
			drm_dbg_kms(dev, "Format requires non-linear modifier for plane %d\n", i);
			return -EINVAL;
		}

		if (!r->handles[i]) {
			drm_dbg_kms(dev, "no buffer object handle for plane %d\n", i);
			return -EINVAL;
		}

		if (min_pitch > UINT_MAX)
			return -ERANGE;

		if ((uint64_t) height * r->pitches[i] + r->offsets[i] > UINT_MAX)
			return -ERANGE;

		if (block_size && r->pitches[i] < min_pitch) {
			drm_dbg_kms(dev, "bad pitch %u for plane %d\n", r->pitches[i], i);
			return -EINVAL;
		}

		if (r->modifier[i] && !(r->flags & DRM_MODE_FB_MODIFIERS)) {
			drm_dbg_kms(dev, "bad fb modifier %llu for plane %d\n",
				    r->modifier[i], i);
			return -EINVAL;
		}

		if (r->flags & DRM_MODE_FB_MODIFIERS &&
		    r->modifier[i] != r->modifier[0]) {
			drm_dbg_kms(dev, "bad fb modifier %llu for plane %d\n",
				    r->modifier[i], i);
			return -EINVAL;
		}

		/* modifier specific checks: */
		switch (r->modifier[i]) {
		case DRM_FORMAT_MOD_SAMSUNG_64_32_TILE:
			/* NOTE: the pitch restriction may be lifted later if it turns
			 * out that no hw has this restriction:
			 */
			if (r->pixel_format != DRM_FORMAT_NV12 ||
					width % 128 || height % 32 ||
					r->pitches[i] % 128) {
				drm_dbg_kms(dev, "bad modifier data for plane %d\n", i);
				return -EINVAL;
			}
			break;

		default:
			break;
		}
	}

	for (i = info->num_planes; i < 4; i++) {
		if (r->modifier[i]) {
			drm_dbg_kms(dev, "non-zero modifier for unused plane %d\n", i);
			return -EINVAL;
		}

		/* Pre-FB_MODIFIERS userspace didn't clear the structs properly. */
		if (!(r->flags & DRM_MODE_FB_MODIFIERS))
			continue;

		if (r->handles[i]) {
			drm_dbg_kms(dev, "buffer object handle for unused plane %d\n", i);
			return -EINVAL;
		}

		if (r->pitches[i]) {
			drm_dbg_kms(dev, "non-zero pitch for unused plane %d\n", i);
			return -EINVAL;
		}

		if (r->offsets[i]) {
			drm_dbg_kms(dev, "non-zero offset for unused plane %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

struct drm_framebuffer *
drm_internal_framebuffer_create(struct drm_device *dev,
				const struct drm_mode_fb_cmd2 *r,
				struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_framebuffer *fb;
	int ret;

	if (r->flags & ~(DRM_MODE_FB_INTERLACED | DRM_MODE_FB_MODIFIERS)) {
		drm_dbg_kms(dev, "bad framebuffer flags 0x%08x\n", r->flags);
		return ERR_PTR(-EINVAL);
	}

	if ((config->min_width > r->width) || (r->width > config->max_width)) {
		drm_dbg_kms(dev, "bad framebuffer width %d, should be >= %d && <= %d\n",
			    r->width, config->min_width, config->max_width);
		return ERR_PTR(-EINVAL);
	}
	if ((config->min_height > r->height) || (r->height > config->max_height)) {
		drm_dbg_kms(dev, "bad framebuffer height %d, should be >= %d && <= %d\n",
			    r->height, config->min_height, config->max_height);
		return ERR_PTR(-EINVAL);
	}

	if (r->flags & DRM_MODE_FB_MODIFIERS &&
	    dev->mode_config.fb_modifiers_not_supported) {
		drm_dbg_kms(dev, "driver does not support fb modifiers\n");
		return ERR_PTR(-EINVAL);
	}

	ret = framebuffer_check(dev, r);
	if (ret)
		return ERR_PTR(ret);

	fb = dev->mode_config.funcs->fb_create(dev, file_priv, r);
	if (IS_ERR(fb)) {
		drm_dbg_kms(dev, "could not create framebuffer\n");
		return fb;
	}

	return fb;
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(drm_internal_framebuffer_create);

/**
 * drm_mode_addfb2 - add an FB to the graphics configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Add a new FB to the specified CRTC, given a user request with format. This is
 * the 2nd version of the addfb ioctl, which supports multi-planar framebuffers
 * and uses fourcc codes as pixel format specifiers.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_addfb2(struct drm_device *dev,
		    void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd2 *r = data;
	struct drm_framebuffer *fb;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	fb = drm_internal_framebuffer_create(dev, r, file_priv);
	if (IS_ERR(fb))
		return PTR_ERR(fb);

	drm_dbg_kms(dev, "[FB:%d]\n", fb->base.id);
	r->fb_id = fb->base.id;

	/* Transfer ownership to the filp for reaping on close */
	mutex_lock(&file_priv->fbs_lock);
	list_add(&fb->filp_head, &file_priv->fbs);
	mutex_unlock(&file_priv->fbs_lock);

	return 0;
}

int drm_mode_addfb2_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv)
{
#ifdef __BIG_ENDIAN
	if (!dev->mode_config.quirk_addfb_prefer_host_byte_order) {
		/*
		 * Drivers must set the
		 * quirk_addfb_prefer_host_byte_order quirk to make
		 * the drm_mode_addfb() compat code work correctly on
		 * bigendian machines.
		 *
		 * If they don't they interpret pixel_format values
		 * incorrectly for bug compatibility, which in turn
		 * implies the ADDFB2 ioctl does not work correctly
		 * then.  So block it to make userspace fallback to
		 * ADDFB.
		 */
		drm_dbg_kms(dev, "addfb2 broken on bigendian");
		return -EOPNOTSUPP;
	}
#endif
	return drm_mode_addfb2(dev, data, file_priv);
}

struct drm_mode_rmfb_work {
	struct work_struct work;
	struct list_head fbs;
};

static void drm_mode_rmfb_work_fn(struct work_struct *w)
{
	struct drm_mode_rmfb_work *arg = container_of(w, typeof(*arg), work);

	while (!list_empty(&arg->fbs)) {
		struct drm_framebuffer *fb =
			list_first_entry(&arg->fbs, typeof(*fb), filp_head);

		drm_dbg_kms(fb->dev,
			    "Removing [FB:%d] from all active usage due to RMFB ioctl\n",
			    fb->base.id);
		list_del_init(&fb->filp_head);
		drm_framebuffer_remove(fb);
	}
}

static int drm_mode_closefb(struct drm_framebuffer *fb,
			    struct drm_file *file_priv)
{
	struct drm_framebuffer *fbl;
	bool found = false;

	mutex_lock(&file_priv->fbs_lock);
	list_for_each_entry(fbl, &file_priv->fbs, filp_head)
		if (fb == fbl)
			found = true;

	if (!found) {
		mutex_unlock(&file_priv->fbs_lock);
		return -ENOENT;
	}

	list_del_init(&fb->filp_head);
	mutex_unlock(&file_priv->fbs_lock);

	/* Drop the reference that was stored in the fbs list */
	drm_framebuffer_put(fb);

	return 0;
}

/**
 * drm_mode_rmfb - remove an FB from the configuration
 * @dev: drm device
 * @fb_id: id of framebuffer to remove
 * @file_priv: drm file
 *
 * Remove the specified FB.
 *
 * Called by the user via ioctl, or by an in-kernel client.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_rmfb(struct drm_device *dev, u32 fb_id,
		  struct drm_file *file_priv)
{
	struct drm_framebuffer *fb;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	fb = drm_framebuffer_lookup(dev, file_priv, fb_id);
	if (!fb)
		return -ENOENT;

	ret = drm_mode_closefb(fb, file_priv);
	if (ret != 0) {
		drm_framebuffer_put(fb);
		return ret;
	}

	/*
	 * drm_framebuffer_remove may fail with -EINTR on pending signals,
	 * so run this in a separate stack as there's no way to correctly
	 * handle this after the fb is already removed from the lookup table.
	 */
	if (drm_framebuffer_read_refcount(fb) > 1) {
		struct drm_mode_rmfb_work arg;

		INIT_WORK_ONSTACK(&arg.work, drm_mode_rmfb_work_fn);
		INIT_LIST_HEAD(&arg.fbs);
		drm_WARN_ON(dev, !list_empty(&fb->filp_head));
		list_add_tail(&fb->filp_head, &arg.fbs);

		schedule_work(&arg.work);
		flush_work(&arg.work);
		destroy_work_on_stack(&arg.work);
	} else
		drm_framebuffer_put(fb);

	return 0;
}

int drm_mode_rmfb_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{
	uint32_t *fb_id = data;

	return drm_mode_rmfb(dev, *fb_id, file_priv);
}

int drm_mode_closefb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_closefb *r = data;
	struct drm_framebuffer *fb;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	if (r->pad)
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, file_priv, r->fb_id);
	if (!fb)
		return -ENOENT;

	ret = drm_mode_closefb(fb, file_priv);
	drm_framebuffer_put(fb);
	return ret;
}

/**
 * drm_mode_getfb - get FB info
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Lookup the FB given its ID and return info about it.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *r = data;
	struct drm_framebuffer *fb;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	fb = drm_framebuffer_lookup(dev, file_priv, r->fb_id);
	if (!fb)
		return -ENOENT;

	/* Multi-planar framebuffers need getfb2. */
	if (fb->format->num_planes > 1) {
		ret = -EINVAL;
		goto out;
	}

	if (!fb->funcs->create_handle) {
		ret = -ENODEV;
		goto out;
	}

	r->height = fb->height;
	r->width = fb->width;
	r->depth = fb->format->depth;
	r->bpp = drm_format_info_bpp(fb->format, 0);
	r->pitch = fb->pitches[0];

	/* GET_FB() is an unprivileged ioctl so we must not return a
	 * buffer-handle to non-master processes! For
	 * backwards-compatibility reasons, we cannot make GET_FB() privileged,
	 * so just return an invalid handle for non-masters.
	 */
	if (!drm_is_current_master(file_priv) && !capable(CAP_SYS_ADMIN)) {
		r->handle = 0;
		ret = 0;
		goto out;
	}

	ret = fb->funcs->create_handle(fb, file_priv, &r->handle);

out:
	drm_framebuffer_put(fb);
	return ret;
}

/**
 * drm_mode_getfb2_ioctl - get extended FB info
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Lookup the FB given its ID and return info about it.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getfb2_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd2 *r = data;
	struct drm_framebuffer *fb;
	unsigned int i;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, file_priv, r->fb_id);
	if (!fb)
		return -ENOENT;

	/* For multi-plane framebuffers, we require the driver to place the
	 * GEM objects directly in the drm_framebuffer. For single-plane
	 * framebuffers, we can fall back to create_handle.
	 */
	if (!fb->obj[0] &&
	    (fb->format->num_planes > 1 || !fb->funcs->create_handle)) {
		ret = -ENODEV;
		goto out;
	}

	r->height = fb->height;
	r->width = fb->width;
	r->pixel_format = fb->format->format;

	r->flags = 0;
	if (!dev->mode_config.fb_modifiers_not_supported)
		r->flags |= DRM_MODE_FB_MODIFIERS;

	for (i = 0; i < ARRAY_SIZE(r->handles); i++) {
		r->handles[i] = 0;
		r->pitches[i] = 0;
		r->offsets[i] = 0;
		r->modifier[i] = 0;
	}

	for (i = 0; i < fb->format->num_planes; i++) {
		r->pitches[i] = fb->pitches[i];
		r->offsets[i] = fb->offsets[i];
		if (!dev->mode_config.fb_modifiers_not_supported)
			r->modifier[i] = fb->modifier;
	}

	/* GET_FB2() is an unprivileged ioctl so we must not return a
	 * buffer-handle to non master/root processes! To match GET_FB()
	 * just return invalid handles (0) for non masters/root
	 * rather than making GET_FB2() privileged.
	 */
	if (!drm_is_current_master(file_priv) && !capable(CAP_SYS_ADMIN)) {
		ret = 0;
		goto out;
	}

	for (i = 0; i < fb->format->num_planes; i++) {
		int j;

		/* If we reuse the same object for multiple planes, also
		 * return the same handle.
		 */
		for (j = 0; j < i; j++) {
			if (fb->obj[i] == fb->obj[j]) {
				r->handles[i] = r->handles[j];
				break;
			}
		}

		if (r->handles[i])
			continue;

		if (fb->obj[i]) {
			ret = drm_gem_handle_create(file_priv, fb->obj[i],
						    &r->handles[i]);
		} else {
			WARN_ON(i > 0);
			ret = fb->funcs->create_handle(fb, file_priv,
						       &r->handles[i]);
		}

		if (ret != 0)
			goto out;
	}

out:
	if (ret != 0) {
		/* Delete any previously-created handles on failure. */
		for (i = 0; i < ARRAY_SIZE(r->handles); i++) {
			int j;

			if (r->handles[i])
				drm_gem_handle_delete(file_priv, r->handles[i]);

			/* Zero out any handles identical to the one we just
			 * deleted.
			 */
			for (j = i + 1; j < ARRAY_SIZE(r->handles); j++) {
				if (r->handles[j] == r->handles[i])
					r->handles[j] = 0;
			}
		}
	}

	drm_framebuffer_put(fb);
	return ret;
}

/**
 * drm_mode_dirtyfb_ioctl - flush frontbuffer rendering on an FB
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Lookup the FB and flush out the damaged area supplied by userspace as a clip
 * rectangle list. Generic userspace which does frontbuffer rendering must call
 * this ioctl to flush out the changes on manual-update display outputs, e.g.
 * usb display-link, mipi manual update panels or edp panel self refresh modes.
 *
 * Modesetting drivers which always update the frontbuffer do not need to
 * implement the corresponding &drm_framebuffer_funcs.dirty callback.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_clip_rect __user *clips_ptr;
	struct drm_clip_rect *clips = NULL;
	struct drm_mode_fb_dirty_cmd *r = data;
	struct drm_framebuffer *fb;
	unsigned flags;
	int num_clips;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	fb = drm_framebuffer_lookup(dev, file_priv, r->fb_id);
	if (!fb)
		return -ENOENT;

	num_clips = r->num_clips;
	clips_ptr = (struct drm_clip_rect __user *)(unsigned long)r->clips_ptr;

	if (!num_clips != !clips_ptr) {
		ret = -EINVAL;
		goto out_err1;
	}

	flags = DRM_MODE_FB_DIRTY_FLAGS & r->flags;

	/* If userspace annotates copy, clips must come in pairs */
	if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY && (num_clips % 2)) {
		ret = -EINVAL;
		goto out_err1;
	}

	if (num_clips && clips_ptr) {
		if (num_clips < 0 || num_clips > DRM_MODE_FB_DIRTY_MAX_CLIPS) {
			ret = -EINVAL;
			goto out_err1;
		}
		clips = kcalloc(num_clips, sizeof(*clips), GFP_KERNEL);
		if (!clips) {
			ret = -ENOMEM;
			goto out_err1;
		}

		ret = copy_from_user(clips, clips_ptr,
				     num_clips * sizeof(*clips));
		if (ret) {
			ret = -EFAULT;
			goto out_err2;
		}
	}

	if (fb->funcs->dirty) {
		ret = fb->funcs->dirty(fb, file_priv, flags, r->color,
				       clips, num_clips);
	} else {
		ret = -ENOSYS;
	}

out_err2:
	kfree(clips);
out_err1:
	drm_framebuffer_put(fb);

	return ret;
}

/**
 * drm_fb_release - remove and free the FBs on this file
 * @priv: drm file for the ioctl
 *
 * Destroy all the FBs associated with @filp.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
void drm_fb_release(struct drm_file *priv)
{
	struct drm_framebuffer *fb, *tfb;
	struct drm_mode_rmfb_work arg;

	INIT_LIST_HEAD(&arg.fbs);

	/*
	 * When the file gets released that means no one else can access the fb
	 * list any more, so no need to grab fpriv->fbs_lock. And we need to
	 * avoid upsetting lockdep since the universal cursor code adds a
	 * framebuffer while holding mutex locks.
	 *
	 * Note that a real deadlock between fpriv->fbs_lock and the modeset
	 * locks is impossible here since no one else but this function can get
	 * at it any more.
	 */
	list_for_each_entry_safe(fb, tfb, &priv->fbs, filp_head) {
		if (drm_framebuffer_read_refcount(fb) > 1) {
			list_move_tail(&fb->filp_head, &arg.fbs);
		} else {
			list_del_init(&fb->filp_head);

			/* This drops the fpriv->fbs reference. */
			drm_framebuffer_put(fb);
		}
	}

	if (!list_empty(&arg.fbs)) {
		INIT_WORK_ONSTACK(&arg.work, drm_mode_rmfb_work_fn);

		schedule_work(&arg.work);
		flush_work(&arg.work);
		destroy_work_on_stack(&arg.work);
	}
}

void drm_framebuffer_free(struct kref *kref)
{
	struct drm_framebuffer *fb =
			container_of(kref, struct drm_framebuffer, base.refcount);
	struct drm_device *dev = fb->dev;

	drm_WARN_ON(dev, !list_empty(&fb->filp_head));

	/*
	 * The lookup idr holds a weak reference, which has not necessarily been
	 * removed at this point. Check for that.
	 */
	drm_mode_object_unregister(dev, &fb->base);

	fb->funcs->destroy(fb);
}

/**
 * drm_framebuffer_init - initialize a framebuffer
 * @dev: DRM device
 * @fb: framebuffer to be initialized
 * @funcs: ... with these functions
 *
 * Allocates an ID for the framebuffer's parent mode object, sets its mode
 * functions & device file and adds it to the master fd list.
 *
 * IMPORTANT:
 * This functions publishes the fb and makes it available for concurrent access
 * by other users. Which means by this point the fb _must_ be fully set up -
 * since all the fb attributes are invariant over its lifetime, no further
 * locking but only correct reference counting is required.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_framebuffer_init(struct drm_device *dev, struct drm_framebuffer *fb,
			 const struct drm_framebuffer_funcs *funcs)
{
	unsigned int i;
	int ret;
	bool exists;

	if (WARN_ON_ONCE(fb->dev != dev || !fb->format))
		return -EINVAL;

	for (i = 0; i < fb->format->num_planes; i++) {
		if (drm_WARN_ON_ONCE(dev, fb->internal_flags & DRM_FRAMEBUFFER_HAS_HANDLE_REF(i)))
			fb->internal_flags &= ~DRM_FRAMEBUFFER_HAS_HANDLE_REF(i);
		if (fb->obj[i]) {
			exists = drm_gem_object_handle_get_if_exists_unlocked(fb->obj[i]);
			if (exists)
				fb->internal_flags |= DRM_FRAMEBUFFER_HAS_HANDLE_REF(i);
		}
	}

	INIT_LIST_HEAD(&fb->filp_head);

	fb->funcs = funcs;
#ifdef __linux__
	strcpy(fb->comm, current->comm);
#else
	strlcpy(fb->comm, curproc->p_p->ps_comm, sizeof(fb->comm));
#endif

	ret = __drm_mode_object_add(dev, &fb->base, DRM_MODE_OBJECT_FB,
				    false, drm_framebuffer_free);
	if (ret)
		goto err;

	mutex_lock(&dev->mode_config.fb_lock);
	dev->mode_config.num_fb++;
	list_add(&fb->head, &dev->mode_config.fb_list);
	mutex_unlock(&dev->mode_config.fb_lock);

	drm_mode_object_register(dev, &fb->base);

	return 0;

err:
	for (i = 0; i < fb->format->num_planes; i++) {
		if (fb->internal_flags & DRM_FRAMEBUFFER_HAS_HANDLE_REF(i)) {
			drm_gem_object_handle_put_unlocked(fb->obj[i]);
			fb->internal_flags &= ~DRM_FRAMEBUFFER_HAS_HANDLE_REF(i);
		}
	}
	return ret;
}
EXPORT_SYMBOL(drm_framebuffer_init);

/**
 * drm_framebuffer_lookup - look up a drm framebuffer and grab a reference
 * @dev: drm device
 * @file_priv: drm file to check for lease against.
 * @id: id of the fb object
 *
 * If successful, this grabs an additional reference to the framebuffer -
 * callers need to make sure to eventually unreference the returned framebuffer
 * again, using drm_framebuffer_put().
 */
struct drm_framebuffer *drm_framebuffer_lookup(struct drm_device *dev,
					       struct drm_file *file_priv,
					       uint32_t id)
{
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb = NULL;

	obj = __drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_FB);
	if (obj)
		fb = obj_to_fb(obj);
	return fb;
}
EXPORT_SYMBOL(drm_framebuffer_lookup);

/**
 * drm_framebuffer_unregister_private - unregister a private fb from the lookup idr
 * @fb: fb to unregister
 *
 * Drivers need to call this when cleaning up driver-private framebuffers, e.g.
 * those used for fbdev. Note that the caller must hold a reference of its own,
 * i.e. the object may not be destroyed through this call (since it'll lead to a
 * locking inversion).
 *
 * NOTE: This function is deprecated. For driver-private framebuffers it is not
 * recommended to embed a framebuffer struct info fbdev struct, instead, a
 * framebuffer pointer is preferred and drm_framebuffer_put() should be called
 * when the framebuffer is to be cleaned up.
 */
void drm_framebuffer_unregister_private(struct drm_framebuffer *fb)
{
	struct drm_device *dev;

	if (!fb)
		return;

	dev = fb->dev;

	/* Mark fb as reaped and drop idr ref. */
	drm_mode_object_unregister(dev, &fb->base);
}
EXPORT_SYMBOL(drm_framebuffer_unregister_private);

/**
 * drm_framebuffer_cleanup - remove a framebuffer object
 * @fb: framebuffer to remove
 *
 * Cleanup framebuffer. This function is intended to be used from the drivers
 * &drm_framebuffer_funcs.destroy callback. It can also be used to clean up
 * driver private framebuffers embedded into a larger structure.
 *
 * Note that this function does not remove the fb from active usage - if it is
 * still used anywhere, hilarity can ensue since userspace could call getfb on
 * the id and get back -EINVAL. Obviously no concern at driver unload time.
 *
 * Also, the framebuffer will not be removed from the lookup idr - for
 * user-created framebuffers this will happen in the rmfb ioctl. For
 * driver-private objects (e.g. for fbdev) drivers need to explicitly call
 * drm_framebuffer_unregister_private.
 */
void drm_framebuffer_cleanup(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	unsigned int i;

	for (i = 0; i < fb->format->num_planes; i++) {
		if (fb->internal_flags & DRM_FRAMEBUFFER_HAS_HANDLE_REF(i))
			drm_gem_object_handle_put_unlocked(fb->obj[i]);
	}

	mutex_lock(&dev->mode_config.fb_lock);
	list_del(&fb->head);
	dev->mode_config.num_fb--;
	mutex_unlock(&dev->mode_config.fb_lock);
}
EXPORT_SYMBOL(drm_framebuffer_cleanup);

static int atomic_remove_fb(struct drm_framebuffer *fb)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_device *dev = fb->dev;
	struct drm_atomic_state *state;
	struct drm_plane *plane;
	struct drm_connector *conn __maybe_unused;
	struct drm_connector_state *conn_state;
	int i, ret;
	unsigned plane_mask;
	bool disable_crtcs = false;

retry_disable:
	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}
	state->acquire_ctx = &ctx;

retry:
	plane_mask = 0;
	ret = drm_modeset_lock_all_ctx(dev, &ctx);
	if (ret)
		goto unlock;

	drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		if (plane->state->fb != fb)
			continue;

		drm_dbg_kms(dev,
			    "Disabling [PLANE:%d:%s] because [FB:%d] is removed\n",
			    plane->base.id, plane->name, fb->base.id);

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			ret = PTR_ERR(plane_state);
			goto unlock;
		}

		if (disable_crtcs && plane_state->crtc->primary == plane) {
			struct drm_crtc_state *crtc_state;

			drm_dbg_kms(dev,
				    "Disabling [CRTC:%d:%s] because [FB:%d] is removed\n",
				    plane_state->crtc->base.id,
				    plane_state->crtc->name, fb->base.id);

			crtc_state = drm_atomic_get_existing_crtc_state(state, plane_state->crtc);

			ret = drm_atomic_add_affected_connectors(state, plane_state->crtc);
			if (ret)
				goto unlock;

			crtc_state->active = false;
			ret = drm_atomic_set_mode_for_crtc(crtc_state, NULL);
			if (ret)
				goto unlock;
		}

		drm_atomic_set_fb_for_plane(plane_state, NULL);
		ret = drm_atomic_set_crtc_for_plane(plane_state, NULL);
		if (ret)
			goto unlock;

		plane_mask |= drm_plane_mask(plane);
	}

	/* This list is only filled when disable_crtcs is set. */
	for_each_new_connector_in_state(state, conn, conn_state, i) {
		ret = drm_atomic_set_crtc_for_connector(conn_state, NULL);

		if (ret)
			goto unlock;
	}

	if (plane_mask)
		ret = drm_atomic_commit(state);

unlock:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);

out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	if (ret == -EINVAL && !disable_crtcs) {
		disable_crtcs = true;
		goto retry_disable;
	}

	return ret;
}

static void legacy_remove_fb(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct drm_crtc *crtc;
	struct drm_plane *plane;

	drm_modeset_lock_all(dev);
	/* remove from any CRTC */
	drm_for_each_crtc(crtc, dev) {
		if (crtc->primary->fb == fb) {
			drm_dbg_kms(dev,
				    "Disabling [CRTC:%d:%s] because [FB:%d] is removed\n",
				    crtc->base.id, crtc->name, fb->base.id);

			/* should turn off the crtc */
			if (drm_crtc_force_disable(crtc))
				DRM_ERROR("failed to reset crtc %p when fb was deleted\n", crtc);
		}
	}

	drm_for_each_plane(plane, dev) {
		if (plane->fb == fb) {
			drm_dbg_kms(dev,
				    "Disabling [PLANE:%d:%s] because [FB:%d] is removed\n",
				    plane->base.id, plane->name, fb->base.id);
			drm_plane_force_disable(plane);
		}
	}
	drm_modeset_unlock_all(dev);
}

/**
 * drm_framebuffer_remove - remove and unreference a framebuffer object
 * @fb: framebuffer to remove
 *
 * Scans all the CRTCs and planes in @dev's mode_config.  If they're
 * using @fb, removes it, setting it to NULL. Then drops the reference to the
 * passed-in framebuffer. Might take the modeset locks.
 *
 * Note that this function optimizes the cleanup away if the caller holds the
 * last reference to the framebuffer. It is also guaranteed to not take the
 * modeset locks in this case.
 */
void drm_framebuffer_remove(struct drm_framebuffer *fb)
{
	struct drm_device *dev;

	if (!fb)
		return;

	dev = fb->dev;

	drm_WARN_ON(dev, !list_empty(&fb->filp_head));

	/*
	 * drm ABI mandates that we remove any deleted framebuffers from active
	 * usage. But since most sane clients only remove framebuffers they no
	 * longer need, try to optimize this away.
	 *
	 * Since we're holding a reference ourselves, observing a refcount of 1
	 * means that we're the last holder and can skip it. Also, the refcount
	 * can never increase from 1 again, so we don't need any barriers or
	 * locks.
	 *
	 * Note that userspace could try to race with use and instate a new
	 * usage _after_ we've cleared all current ones. End result will be an
	 * in-use fb with fb-id == 0. Userspace is allowed to shoot its own foot
	 * in this manner.
	 */
	if (drm_framebuffer_read_refcount(fb) > 1) {
		if (drm_drv_uses_atomic_modeset(dev)) {
			int ret = atomic_remove_fb(fb);

			WARN(ret, "atomic remove_fb failed with %i\n", ret);
		} else
			legacy_remove_fb(fb);
	}

	drm_framebuffer_put(fb);
}
EXPORT_SYMBOL(drm_framebuffer_remove);

void drm_framebuffer_print_info(struct drm_printer *p, unsigned int indent,
				const struct drm_framebuffer *fb)
{
	unsigned int i;

	drm_printf_indent(p, indent, "allocated by = %s\n", fb->comm);
	drm_printf_indent(p, indent, "refcount=%u\n",
			  drm_framebuffer_read_refcount(fb));
	drm_printf_indent(p, indent, "format=%p4cc\n", &fb->format->format);
	drm_printf_indent(p, indent, "modifier=0x%llx\n", fb->modifier);
	drm_printf_indent(p, indent, "size=%ux%u\n", fb->width, fb->height);
	drm_printf_indent(p, indent, "layers:\n");

	for (i = 0; i < fb->format->num_planes; i++) {
		drm_printf_indent(p, indent + 1, "size[%u]=%dx%d\n", i,
				  drm_format_info_plane_width(fb->format, fb->width, i),
				  drm_format_info_plane_height(fb->format, fb->height, i));
		drm_printf_indent(p, indent + 1, "pitch[%u]=%u\n", i, fb->pitches[i]);
		drm_printf_indent(p, indent + 1, "offset[%u]=%u\n", i, fb->offsets[i]);
		drm_printf_indent(p, indent + 1, "obj[%u]:%s\n", i,
				  fb->obj[i] ? "" : "(null)");
		if (fb->obj[i])
			drm_gem_print_info(p, indent + 2, fb->obj[i]);
	}
}

#ifdef CONFIG_DEBUG_FS
static int drm_framebuffer_info(struct seq_file *m, void *data)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_framebuffer *fb;

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(fb, dev) {
		drm_printf(&p, "framebuffer[%u]:\n", fb->base.id);
		drm_framebuffer_print_info(&p, 1, fb);
	}
	mutex_unlock(&dev->mode_config.fb_lock);

	return 0;
}

static const struct drm_debugfs_info drm_framebuffer_debugfs_list[] = {
	{ "framebuffer", drm_framebuffer_info, 0 },
};

void drm_framebuffer_debugfs_init(struct drm_device *dev)
{
	drm_debugfs_add_files(dev, drm_framebuffer_debugfs_list,
			      ARRAY_SIZE(drm_framebuffer_debugfs_list));
}
#endif
