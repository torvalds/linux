/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIDEO_ADF_CLIENT_H_
#define _VIDEO_ADF_CLIENT_H_

#include <video/adf.h>

int adf_interface_blank(struct adf_interface *intf, u8 state);
u8 adf_interface_dpms_state(struct adf_interface *intf);

void adf_interface_current_mode(struct adf_interface *intf,
		struct drm_mode_modeinfo *mode);
size_t adf_interface_modelist(struct adf_interface *intf,
		struct drm_mode_modeinfo *modelist, size_t n_modes);
int adf_interface_set_mode(struct adf_interface *intf,
		struct drm_mode_modeinfo *mode);
int adf_interface_get_screen_size(struct adf_interface *intf, u16 *width,
		u16 *height);

bool adf_overlay_engine_supports_format(struct adf_overlay_engine *eng,
		u32 format);

size_t adf_device_attachments(struct adf_device *dev,
		struct adf_attachment *attachments, size_t n_attachments);
size_t adf_device_attachments_allowed(struct adf_device *dev,
		struct adf_attachment *attachments, size_t n_attachments);
bool adf_device_attached(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf);
bool adf_device_attach_allowed(struct adf_device *dev,
		struct adf_overlay_engine *eng, struct adf_interface *intf);
int adf_device_attach(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf);
int adf_device_detach(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf);

struct sync_fence *adf_device_post(struct adf_device *dev,
		struct adf_interface **intfs, size_t n_intfs,
		struct adf_buffer *bufs, size_t n_bufs, void *custom_data,
		size_t custom_data_size);
struct sync_fence *adf_device_post_nocopy(struct adf_device *dev,
		struct adf_interface **intfs, size_t n_intfs,
		struct adf_buffer *bufs, size_t n_bufs, void *custom_data,
		size_t custom_data_size);

#endif /* _VIDEO_ADF_CLIENT_H_ */
