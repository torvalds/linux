/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_mode.c
 * Copyright 2012 Red Hat Inc.
 * Parts based on xf86-video-ast
 * Copyright (c) 2005 ASPEED Technology Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#include "vbox_drv.h"
#include <linux/export.h>
#include <drm/drm_crtc_helper.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0) || defined(RHEL_73)
#include <drm/drm_plane_helper.h>
#endif

#include "vboxvideo.h"
#include "hgsmi_channels.h"

static int vbox_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			    u32 handle, u32 width, u32 height,
			    s32 hot_x, s32 hot_y);
static int vbox_cursor_move(struct drm_crtc *crtc, int x, int y);

/**
 * Set a graphics mode.  Poke any required values into registers, do an HGSMI
 * mode set and tell the host we support advanced graphics functions.
 */
static void vbox_do_modeset(struct drm_crtc *crtc,
			    const struct drm_display_mode *mode)
{
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct vbox_private *vbox;
	int width, height, bpp, pitch;
	unsigned int crtc_id;
	u16 flags;
	s32 x_offset, y_offset;

	vbox = crtc->dev->dev_private;
	width = mode->hdisplay ? mode->hdisplay : 640;
	height = mode->vdisplay ? mode->vdisplay : 480;
	crtc_id = vbox_crtc->crtc_id;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	bpp = crtc->enabled ? CRTC_FB(crtc)->format->cpp[0] * 8 : 32;
	pitch = crtc->enabled ? CRTC_FB(crtc)->pitches[0] : width * bpp / 8;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	bpp = crtc->enabled ? CRTC_FB(crtc)->bits_per_pixel : 32;
	pitch = crtc->enabled ? CRTC_FB(crtc)->pitches[0] : width * bpp / 8;
#else
	bpp = crtc->enabled ? CRTC_FB(crtc)->bits_per_pixel : 32;
	pitch = crtc->enabled ? CRTC_FB(crtc)->pitch : width * bpp / 8;
#endif
	x_offset = vbox->single_framebuffer ? crtc->x : vbox_crtc->x_hint;
	y_offset = vbox->single_framebuffer ? crtc->y : vbox_crtc->y_hint;

	/*
	 * This is the old way of setting graphics modes.  It assumed one screen
	 * and a frame-buffer at the start of video RAM.  On older versions of
	 * VirtualBox, certain parts of the code still assume that the first
	 * screen is programmed this way, so try to fake it.
	 */
	if (vbox_crtc->crtc_id == 0 && crtc->enabled &&
	    vbox_crtc->fb_offset / pitch < 0xffff - crtc->y &&
	    vbox_crtc->fb_offset % (bpp / 8) == 0)
		VBoxVideoSetModeRegisters(
			width, height, pitch * 8 / bpp,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
			CRTC_FB(crtc)->format->cpp[0] * 8,
#else
			CRTC_FB(crtc)->bits_per_pixel,
#endif
			0,
			vbox_crtc->fb_offset % pitch / bpp * 8 + crtc->x,
			vbox_crtc->fb_offset / pitch + crtc->y);

	flags = VBVA_SCREEN_F_ACTIVE;
	flags |= (crtc->enabled && !vbox_crtc->blanked) ?
		 0 : VBVA_SCREEN_F_BLANK;
	flags |= vbox_crtc->disconnected ? VBVA_SCREEN_F_DISABLED : 0;
	hgsmi_process_display_info(vbox->guest_pool, vbox_crtc->crtc_id,
				    x_offset, y_offset,
				    crtc->x * bpp / 8 + crtc->y * pitch,
				    pitch, width, height,
				    vbox_crtc->blanked ? 0 : bpp, flags);
}

static int vbox_set_view(struct drm_crtc *crtc)
{
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct vbox_private *vbox = crtc->dev->dev_private;
	void *p;

	/*
	 * Tell the host about the view.  This design originally targeted the
	 * Windows XP driver architecture and assumed that each screen would
	 * have a dedicated frame buffer with the command buffer following it,
	 * the whole being a "view".  The host works out which screen a command
	 * buffer belongs to by checking whether it is in the first view, then
	 * whether it is in the second and so on.  The first match wins.  We
	 * cheat around this by making the first view be the managed memory
	 * plus the first command buffer, the second the same plus the second
	 * buffer and so on.
	 */
	p = hgsmi_buffer_alloc(vbox->guest_pool, sizeof(VBVAINFOVIEW),
				 HGSMI_CH_VBVA, VBVA_INFO_VIEW);
	if (p) {
		VBVAINFOVIEW *pInfo = (VBVAINFOVIEW *) p;

		pInfo->view_index = vbox_crtc->crtc_id;
		pInfo->u32ViewOffset = vbox_crtc->fb_offset;
		pInfo->u32ViewSize =
		    vbox->available_vram_size - vbox_crtc->fb_offset +
		    vbox_crtc->crtc_id * VBVA_MIN_BUFFER_SIZE;
		pInfo->u32MaxScreenSize =
		    vbox->available_vram_size - vbox_crtc->fb_offset;
		hgsmi_buffer_submit(vbox->guest_pool, p);
		hgsmi_buffer_free(vbox->guest_pool, p);
	} else {
		return -ENOMEM;
	}

	return 0;
}

static void vbox_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct vbox_private *vbox = crtc->dev->dev_private;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		vbox_crtc->blanked = false;
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		vbox_crtc->blanked = true;
		break;
	}

	mutex_lock(&vbox->hw_mutex);
	vbox_do_modeset(crtc, &crtc->hwmode);
	mutex_unlock(&vbox->hw_mutex);
}

static bool vbox_crtc_mode_fixup(struct drm_crtc *crtc,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	return true;
}

/*
 * Try to map the layout of virtual screens to the range of the input device.
 * Return true if we need to re-set the crtc modes due to screen offset
 * changes.
 */
static bool vbox_set_up_input_mapping(struct vbox_private *vbox)
{
	struct drm_crtc *crtci;
	struct drm_connector *connectori;
	struct drm_framebuffer *fb1 = NULL;
	bool single_framebuffer = true;
	bool old_single_framebuffer = vbox->single_framebuffer;
	u16 width = 0, height = 0;

	/*
	 * Are we using an X.Org-style single large frame-buffer for all crtcs?
	 * If so then screen layout can be deduced from the crtc offsets.
	 * Same fall-back if this is the fbdev frame-buffer.
	 */
	list_for_each_entry(crtci, &vbox->dev->mode_config.crtc_list, head) {
		if (!fb1) {
			fb1 = CRTC_FB(crtci);
			if (to_vbox_framebuffer(fb1) == &vbox->fbdev->afb)
				break;
		} else if (CRTC_FB(crtci) && fb1 != CRTC_FB(crtci)) {
			single_framebuffer = false;
		}
	}
	if (single_framebuffer) {
		list_for_each_entry(crtci, &vbox->dev->mode_config.crtc_list,
				    head) {
			if (to_vbox_crtc(crtci)->crtc_id == 0) {
				vbox->single_framebuffer = true;
				vbox->input_mapping_width =
				    CRTC_FB(crtci)->width;
				vbox->input_mapping_height =
				    CRTC_FB(crtci)->height;
				return old_single_framebuffer !=
				    vbox->single_framebuffer;
			}
		}
	}
	/* Otherwise calculate the total span of all screens. */
	list_for_each_entry(connectori, &vbox->dev->mode_config.connector_list,
			    head) {
		struct vbox_connector *vbox_connector =
		    to_vbox_connector(connectori);
		struct vbox_crtc *vbox_crtc = vbox_connector->vbox_crtc;

		width = max_t(u16, width, vbox_crtc->x_hint +
					  vbox_connector->mode_hint.width);
		height = max_t(u16, height, vbox_crtc->y_hint +
					    vbox_connector->mode_hint.height);
	}

	vbox->single_framebuffer = false;
	vbox->input_mapping_width = width;
	vbox->input_mapping_height = height;

	return old_single_framebuffer != vbox->single_framebuffer;
}

static int vbox_crtc_do_set_base(struct drm_crtc *crtc,
				 struct drm_framebuffer *old_fb, int x, int y)
{
	struct vbox_private *vbox = crtc->dev->dev_private;
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct drm_gem_object *obj;
	struct vbox_framebuffer *vbox_fb;
	struct vbox_bo *bo;
	int ret;
	u64 gpu_addr;

	/* Unpin the previous fb. */
	if (old_fb) {
		vbox_fb = to_vbox_framebuffer(old_fb);
		obj = vbox_fb->obj;
		bo = gem_to_vbox_bo(obj);
		ret = vbox_bo_reserve(bo, false);
		if (ret)
			return ret;

		vbox_bo_unpin(bo);
		vbox_bo_unreserve(bo);
	}

	vbox_fb = to_vbox_framebuffer(CRTC_FB(crtc));
	obj = vbox_fb->obj;
	bo = gem_to_vbox_bo(obj);

	ret = vbox_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = vbox_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	vbox_bo_unreserve(bo);
	if (ret)
		return ret;

	vbox_crtc->fb_offset = gpu_addr;
	if (vbox_set_up_input_mapping(vbox)) {
		struct drm_crtc *crtci;

		list_for_each_entry(crtci, &vbox->dev->mode_config.crtc_list,
				    head) {
			vbox_set_view(crtc);
			vbox_do_modeset(crtci, &crtci->mode);
		}
	}

	return 0;
}

static int vbox_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				   struct drm_framebuffer *old_fb)
{
	return vbox_crtc_do_set_base(crtc, old_fb, x, y);
}

static int vbox_crtc_mode_set(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode,
			      int x, int y, struct drm_framebuffer *old_fb)
{
	struct vbox_private *vbox = crtc->dev->dev_private;
	int rc = 0;

	vbox_crtc_mode_set_base(crtc, x, y, old_fb);

	mutex_lock(&vbox->hw_mutex);
	rc = vbox_set_view(crtc);
	if (!rc)
		vbox_do_modeset(crtc, mode);
	hgsmi_update_input_mapping(vbox->guest_pool, 0, 0,
				    vbox->input_mapping_width,
				    vbox->input_mapping_height);
	mutex_unlock(&vbox->hw_mutex);

	return rc;
}

static void vbox_crtc_disable(struct drm_crtc *crtc)
{
}

static void vbox_crtc_prepare(struct drm_crtc *crtc)
{
}

static void vbox_crtc_commit(struct drm_crtc *crtc)
{
}

static const struct drm_crtc_helper_funcs vbox_crtc_helper_funcs = {
	.dpms = vbox_crtc_dpms,
	.mode_fixup = vbox_crtc_mode_fixup,
	.mode_set = vbox_crtc_mode_set,
	/* .mode_set_base = vbox_crtc_mode_set_base, */
	.disable = vbox_crtc_disable,
	.prepare = vbox_crtc_prepare,
	.commit = vbox_crtc_commit,
};

static void vbox_crtc_reset(struct drm_crtc *crtc)
{
}

static void vbox_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_funcs vbox_crtc_funcs = {
	.cursor_move = vbox_cursor_move,
	.cursor_set2 = vbox_cursor_set2,
	.reset = vbox_crtc_reset,
	.set_config = drm_crtc_helper_set_config,
	/* .gamma_set = vbox_crtc_gamma_set, */
	.destroy = vbox_crtc_destroy,
};

static struct vbox_crtc *vbox_crtc_init(struct drm_device *dev, unsigned int i)
{
	struct vbox_crtc *vbox_crtc;

	vbox_crtc = kzalloc(sizeof(*vbox_crtc), GFP_KERNEL);
	if (!vbox_crtc)
		return NULL;

	vbox_crtc->crtc_id = i;

	drm_crtc_init(dev, &vbox_crtc->base, &vbox_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&vbox_crtc->base, 256);
	drm_crtc_helper_add(&vbox_crtc->base, &vbox_crtc_helper_funcs);

	return vbox_crtc;
}

static void vbox_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) && !defined(RHEL_73)
static struct drm_encoder *drm_encoder_find(struct drm_device *dev, u32 id)
{
	struct drm_mode_object *mo;

	mo = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
	return mo ? obj_to_encoder(mo) : NULL;
}
#endif

static struct drm_encoder *vbox_best_single_encoder(struct drm_connector
						    *connector)
{
	int enc_id = connector->encoder_ids[0];

	/* pick the encoder ids */
	if (enc_id)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
		return drm_encoder_find(connector->dev, NULL, enc_id);
#else
		return drm_encoder_find(connector->dev, enc_id);
#endif

	return NULL;
}

static const struct drm_encoder_funcs vbox_enc_funcs = {
	.destroy = vbox_encoder_destroy,
};

static void vbox_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool vbox_mode_fixup(struct drm_encoder *encoder,
			    const struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void vbox_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
}

static void vbox_encoder_prepare(struct drm_encoder *encoder)
{
}

static void vbox_encoder_commit(struct drm_encoder *encoder)
{
}

static const struct drm_encoder_helper_funcs vbox_enc_helper_funcs = {
	.dpms = vbox_encoder_dpms,
	.mode_fixup = vbox_mode_fixup,
	.prepare = vbox_encoder_prepare,
	.commit = vbox_encoder_commit,
	.mode_set = vbox_encoder_mode_set,
};

static struct drm_encoder *vbox_encoder_init(struct drm_device *dev,
					     unsigned int i)
{
	struct vbox_encoder *vbox_encoder;

	vbox_encoder = kzalloc(sizeof(*vbox_encoder), GFP_KERNEL);
	if (!vbox_encoder)
		return NULL;

	drm_encoder_init(dev, &vbox_encoder->base, &vbox_enc_funcs,
			 DRM_MODE_ENCODER_DAC
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0) || defined(RHEL_73)
			 , NULL
#endif
	    );
	drm_encoder_helper_add(&vbox_encoder->base, &vbox_enc_helper_funcs);

	vbox_encoder->base.possible_crtcs = 1 << i;
	return &vbox_encoder->base;
}

/**
 * Generate EDID data with a mode-unique serial number for the virtual
 *  monitor to try to persuade Unity that different modes correspond to
 *  different monitors and it should not try to force the same resolution on
 *  them.
 */
static void vbox_set_edid(struct drm_connector *connector, int width,
			  int height)
{
	enum { EDID_SIZE = 128 };
	unsigned char edid[EDID_SIZE] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,	/* header */
		0x58, 0x58,	/* manufacturer (VBX) */
		0x00, 0x00,	/* product code */
		0x00, 0x00, 0x00, 0x00,	/* serial number goes here */
		0x01,		/* week of manufacture */
		0x00,		/* year of manufacture */
		0x01, 0x03,	/* EDID version */
		0x80,		/* capabilities - digital */
		0x00,		/* horiz. res in cm, zero for projectors */
		0x00,		/* vert. res in cm */
		0x78,		/* display gamma (120 == 2.2). */
		0xEE,		/* features (standby, suspend, off, RGB, std */
				/* colour space, preferred timing mode) */
		0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
		/* chromaticity for standard colour space. */
		0x00, 0x00, 0x00,	/* no default timings */
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		    0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,	/* no standard timings */
		0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x02, 0x02,
		    0x02, 0x02,
		/* descriptor block 1 goes below */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* descriptor block 2, monitor ranges */
		0x00, 0x00, 0x00, 0xFD, 0x00,
		0x00, 0xC8, 0x00, 0xC8, 0x64, 0x00, 0x0A, 0x20, 0x20, 0x20,
		    0x20, 0x20,
		/* 0-200Hz vertical, 0-200KHz horizontal, 1000MHz pixel clock */
		0x20,
		/* descriptor block 3, monitor name */
		0x00, 0x00, 0x00, 0xFC, 0x00,
		'V', 'B', 'O', 'X', ' ', 'm', 'o', 'n', 'i', 't', 'o', 'r',
		'\n',
		/* descriptor block 4: dummy data */
		0x00, 0x00, 0x00, 0x10, 0x00,
		0x0A, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20,
		0x00,		/* number of extensions */
		0x00		/* checksum goes here */
	};
	int clock = (width + 6) * (height + 6) * 60 / 10000;
	unsigned int i, sum = 0;

	edid[12] = width & 0xff;
	edid[13] = width >> 8;
	edid[14] = height & 0xff;
	edid[15] = height >> 8;
	edid[54] = clock & 0xff;
	edid[55] = clock >> 8;
	edid[56] = width & 0xff;
	edid[58] = (width >> 4) & 0xf0;
	edid[59] = height & 0xff;
	edid[61] = (height >> 4) & 0xf0;
	for (i = 0; i < EDID_SIZE - 1; ++i)
		sum += edid[i];
	edid[EDID_SIZE - 1] = (0x100 - (sum & 0xFF)) & 0xFF;
	drm_mode_connector_update_edid_property(connector, (struct edid *)edid);
}

static int vbox_get_modes(struct drm_connector *connector)
{
	struct vbox_connector *vbox_connector = NULL;
	struct drm_display_mode *mode = NULL;
	struct vbox_private *vbox = NULL;
	unsigned int num_modes = 0;
	int preferred_width, preferred_height;

	vbox_connector = to_vbox_connector(connector);
	vbox = connector->dev->dev_private;
	/*
	 * Heuristic: we do not want to tell the host that we support dynamic
	 * resizing unless we feel confident that the user space client using
	 * the video driver can handle hot-plug events.  So the first time modes
	 * are queried after a "master" switch we tell the host that we do not,
	 * and immediately after we send the client a hot-plug notification as
	 * a test to see if they will respond and query again.
	 * That is also the reason why capabilities are reported to the host at
	 * this place in the code rather than elsewhere.
	 * We need to report the flags location before reporting the IRQ
	 * capability.
	 */
	hgsmi_report_flags_location(vbox->guest_pool, GUEST_HEAP_OFFSET(vbox) +
				     HOST_FLAGS_OFFSET);
	if (vbox_connector->vbox_crtc->crtc_id == 0)
		vbox_report_caps(vbox);
	if (!vbox->initial_mode_queried) {
		if (vbox_connector->vbox_crtc->crtc_id == 0) {
			vbox->initial_mode_queried = true;
			vbox_report_hotplug(vbox);
		}
		return drm_add_modes_noedid(connector, 800, 600);
	}
	num_modes = drm_add_modes_noedid(connector, 2560, 1600);
	preferred_width = vbox_connector->mode_hint.width ?
			  vbox_connector->mode_hint.width : 1024;
	preferred_height = vbox_connector->mode_hint.height ?
			   vbox_connector->mode_hint.height : 768;
	mode = drm_cvt_mode(connector->dev, preferred_width, preferred_height,
			    60, false, false, false);
	if (mode) {
		mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		++num_modes;
	}
	vbox_set_edid(connector, preferred_width, preferred_height);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) || defined(RHEL_73)
	if (vbox_connector->vbox_crtc->x_hint != -1)
		drm_object_property_set_value(&connector->base,
			vbox->dev->mode_config.suggested_x_property,
			vbox_connector->vbox_crtc->x_hint);
	else
		drm_object_property_set_value(&connector->base,
			vbox->dev->mode_config.suggested_x_property, 0);

	if (vbox_connector->vbox_crtc->y_hint != -1)
		drm_object_property_set_value(&connector->base,
			vbox->dev->mode_config.suggested_y_property,
			vbox_connector->vbox_crtc->y_hint);
	else
		drm_object_property_set_value(&connector->base,
			vbox->dev->mode_config.suggested_y_property, 0);
#endif

	return num_modes;
}

static int vbox_mode_valid(struct drm_connector *connector,
			   struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void vbox_connector_destroy(struct drm_connector *connector)
{
	struct vbox_connector *vbox_connector = NULL;

	vbox_connector = to_vbox_connector(connector);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && !defined(RHEL_73)
	drm_sysfs_connector_remove(connector);
#else
	drm_connector_unregister(connector);
#endif
	drm_connector_cleanup(connector);
	kfree(connector);
}

static enum drm_connector_status
vbox_connector_detect(struct drm_connector *connector, bool force)
{
	struct vbox_connector *vbox_connector = NULL;

	(void)force;
	vbox_connector = to_vbox_connector(connector);

	return vbox_connector->mode_hint.disconnected ?
	    connector_status_disconnected : connector_status_connected;
}

static int vbox_fill_modes(struct drm_connector *connector, u32 max_x,
			   u32 max_y)
{
	struct vbox_connector *vbox_connector;
	struct drm_device *dev;
	struct drm_display_mode *mode, *iterator;

	vbox_connector = to_vbox_connector(connector);
	dev = vbox_connector->base.dev;
	list_for_each_entry_safe(mode, iterator, &connector->modes, head) {
		list_del(&mode->head);
		drm_mode_destroy(dev, mode);
	}

	return drm_helper_probe_single_connector_modes(connector, max_x, max_y);
}

static const struct drm_connector_helper_funcs vbox_connector_helper_funcs = {
	.mode_valid = vbox_mode_valid,
	.get_modes = vbox_get_modes,
	.best_encoder = vbox_best_single_encoder,
};

static const struct drm_connector_funcs vbox_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = vbox_connector_detect,
	.fill_modes = vbox_fill_modes,
	.destroy = vbox_connector_destroy,
};

static int vbox_connector_init(struct drm_device *dev,
			       struct vbox_crtc *vbox_crtc,
			       struct drm_encoder *encoder)
{
	struct vbox_connector *vbox_connector;
	struct drm_connector *connector;

	vbox_connector = kzalloc(sizeof(*vbox_connector), GFP_KERNEL);
	if (!vbox_connector)
		return -ENOMEM;

	connector = &vbox_connector->base;
	vbox_connector->vbox_crtc = vbox_crtc;

	drm_connector_init(dev, connector, &vbox_connector_funcs,
			   DRM_MODE_CONNECTOR_VGA);
	drm_connector_helper_add(connector, &vbox_connector_helper_funcs);

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) || defined(RHEL_73)
	drm_mode_create_suggested_offset_properties(dev);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && !defined(RHEL_73)
	drm_sysfs_connector_add(connector);
#else
	drm_connector_register(connector);
#endif

	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}

int vbox_mode_init(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct drm_encoder *encoder;
	struct vbox_crtc *vbox_crtc;
	unsigned int i;

	/* vbox_cursor_init(dev); */
	for (i = 0; i < vbox->num_crtcs; ++i) {
		vbox_crtc = vbox_crtc_init(dev, i);
		if (!vbox_crtc)
			return -ENOMEM;
		encoder = vbox_encoder_init(dev, i);
		if (!encoder)
			return -ENOMEM;
		vbox_connector_init(dev, vbox_crtc, encoder);
	}

	return 0;
}

void vbox_mode_fini(struct drm_device *dev)
{
	/* vbox_cursor_fini(dev); */
}

/**
 * Copy the ARGB image and generate the mask, which is needed in case the host
 * does not support ARGB cursors.  The mask is a 1BPP bitmap with the bit set
 * if the corresponding alpha value in the ARGB image is greater than 0xF0.
 */
static void copy_cursor_image(u8 *src, u8 *dst, u32 width, u32 height,
			      size_t mask_size)
{
	size_t line_size = (width + 7) / 8;
	u32 i, j;

	memcpy(dst + mask_size, src, width * height * 4);
	for (i = 0; i < height; ++i)
		for (j = 0; j < width; ++j)
			if (((u32 *)src)[i * width + j] > 0xf0000000)
				dst[i * line_size + j / 8] |= (0x80 >> (j % 8));
}

static int vbox_cursor_set2(struct drm_crtc *crtc, struct drm_file *file_priv,
			    u32 handle, u32 width, u32 height,
			    s32 hot_x, s32 hot_y)
{
	struct vbox_private *vbox = crtc->dev->dev_private;
	struct vbox_crtc *vbox_crtc = to_vbox_crtc(crtc);
	struct drm_gem_object *obj;
	struct vbox_bo *bo;
	int ret, rc;
	struct ttm_bo_kmap_obj uobj_map;
	u8 *src;
	u8 *dst = NULL;
	u32 caps = 0;
	size_t data_size, mask_size;
	bool src_isiomem;

	/*
	 * Re-set this regularly as in 5.0.20 and earlier the information was
	 * lost on save and restore.
	 */
	hgsmi_update_input_mapping(vbox->guest_pool, 0, 0,
				    vbox->input_mapping_width,
				    vbox->input_mapping_height);
	if (!handle) {
		bool cursor_enabled = false;
		struct drm_crtc *crtci;

		/* Hide cursor. */
		vbox_crtc->cursor_enabled = false;
		list_for_each_entry(crtci, &vbox->dev->mode_config.crtc_list,
				    head)
			if (to_vbox_crtc(crtci)->cursor_enabled)
				cursor_enabled = true;

		if (!cursor_enabled)
			hgsmi_update_pointer_shape(vbox->guest_pool, 0, 0, 0,
						    0, 0, NULL, 0);
		return 0;
	}
	vbox_crtc->cursor_enabled = true;
	if (width > VBOX_MAX_CURSOR_WIDTH || height > VBOX_MAX_CURSOR_HEIGHT ||
	    width == 0 || height == 0)
		return -EINVAL;
	rc = hgsmi_query_conf(vbox->guest_pool,
				VBOX_VBVA_CONF32_CURSOR_CAPABILITIES, &caps);
	ret = rc == VINF_SUCCESS ? 0 : rc == VERR_NO_MEMORY ? -ENOMEM : -EINVAL;
	if (ret)
		return ret;

	if (!(caps & VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE))
		/*
		 * -EINVAL means cursor_set2() not supported, -EAGAIN means
		 * retry at once.
		 */
		return -EBUSY;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0) || defined(RHEL_74)
	obj = drm_gem_object_lookup(file_priv, handle);
#else
	obj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
#endif
	if (obj) {
		bo = gem_to_vbox_bo(obj);
		ret = vbox_bo_reserve(bo, false);
		if (!ret) {
			/*
			 * The mask must be calculated based on the alpha
			 * channel, one bit per ARGB word, and must be 32-bit
			 * padded.
			 */
			mask_size = ((width + 7) / 8 * height + 3) & ~3;
			data_size = width * height * 4 + mask_size;
			vbox->cursor_hot_x = min_t(u32, max(hot_x, 0), width);
			vbox->cursor_hot_y = min_t(u32, max(hot_y, 0), height);
			vbox->cursor_width = width;
			vbox->cursor_height = height;
			vbox->cursor_data_size = data_size;
			dst = vbox->cursor_data;
			ret =
			    ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages,
					&uobj_map);
			if (!ret) {
				src =
				    ttm_kmap_obj_virtual(&uobj_map,
							 &src_isiomem);
				if (!src_isiomem) {
					u32 flags =
					    VBOX_MOUSE_POINTER_VISIBLE |
					    VBOX_MOUSE_POINTER_SHAPE |
					    VBOX_MOUSE_POINTER_ALPHA;
					copy_cursor_image(src, dst, width,
							  height, mask_size);
					rc = hgsmi_update_pointer_shape(
						vbox->guest_pool, flags,
						vbox->cursor_hot_x,
						vbox->cursor_hot_y,
						width, height, dst, data_size);
					ret =
					    rc == VINF_SUCCESS ? 0 : rc ==
					    VERR_NO_MEMORY ? -ENOMEM : rc ==
					    VERR_NOT_SUPPORTED ? -EBUSY :
					    -EINVAL;
				} else {
					DRM_ERROR("src cursor bo should be in main memory\n");
				}
				ttm_bo_kunmap(&uobj_map);
			} else {
				vbox->cursor_data_size = 0;
			}
			vbox_bo_unreserve(bo);
		}
		drm_gem_object_unreference_unlocked(obj);
	} else {
		DRM_ERROR("Cannot find cursor object %x for crtc\n", handle);
		ret = -ENOENT;
	}

	return ret;
}

static int vbox_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct vbox_private *vbox = crtc->dev->dev_private;
	u32 flags = VBOX_MOUSE_POINTER_VISIBLE |
	    VBOX_MOUSE_POINTER_SHAPE | VBOX_MOUSE_POINTER_ALPHA;
	s32 crtc_x =
	    vbox->single_framebuffer ? crtc->x : to_vbox_crtc(crtc)->x_hint;
	s32 crtc_y =
	    vbox->single_framebuffer ? crtc->y : to_vbox_crtc(crtc)->y_hint;
	u32 host_x, host_y;
	u32 hot_x = 0;
	u32 hot_y = 0;
	int rc;

	/*
	 * We compare these to unsigned later and don't
	 * need to handle negative.
	 */
	if (x + crtc_x < 0 || y + crtc_y < 0 || vbox->cursor_data_size == 0)
		return 0;

	rc = hgsmi_cursor_position(vbox->guest_pool, true, x + crtc_x,
				     y + crtc_y, &host_x, &host_y);
	/* Work around a bug after save and restore in 5.0.20 and earlier. */
	if (RT_FAILURE(rc) || (host_x == 0 && host_y == 0))
		return rc == VINF_SUCCESS ? 0
		    : rc == VERR_NO_MEMORY ? -ENOMEM : -EINVAL;
	if (x + crtc_x < host_x)
		hot_x = min(host_x - x - crtc_x, vbox->cursor_width);
	if (y + crtc_y < host_y)
		hot_y = min(host_y - y - crtc_y, vbox->cursor_height);
	if (hot_x == vbox->cursor_hot_x && hot_y == vbox->cursor_hot_y)
		return 0;
	vbox->cursor_hot_x = hot_x;
	vbox->cursor_hot_y = hot_y;
	rc = hgsmi_update_pointer_shape(vbox->guest_pool, flags, hot_x, hot_y,
					 vbox->cursor_width,
					 vbox->cursor_height, vbox->cursor_data,
					 vbox->cursor_data_size);
	return rc == VINF_SUCCESS ? 0 : rc == VERR_NO_MEMORY ? -ENOMEM : rc ==
	    VERR_NOT_SUPPORTED ? -EBUSY : -EINVAL;
}
