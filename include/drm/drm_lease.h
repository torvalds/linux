/*
 * Copyright © 2017 Keith Packard <keithp@keithp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _DRM_LEASE_H_
#define _DRM_LEASE_H_

struct drm_file;
struct drm_device;
struct drm_master;

struct drm_master *drm_lease_owner(struct drm_master *master);

void drm_lease_destroy(struct drm_master *lessee);

bool drm_lease_held(struct drm_file *file_priv, int id);

bool _drm_lease_held(struct drm_file *file_priv, int id);

void drm_lease_revoke(struct drm_master *master);

uint32_t drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs);

int drm_mode_create_lease_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);

int drm_mode_list_lessees_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);

int drm_mode_get_lease_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv);

int drm_mode_revoke_lease_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv);

#endif /* _DRM_LEASE_H_ */
