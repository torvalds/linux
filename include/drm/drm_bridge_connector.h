/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __DRM_BRIDGE_CONNECTOR_H__
#define __DRM_BRIDGE_CONNECTOR_H__

struct drm_connector;
struct drm_device;
struct drm_encoder;

void drm_bridge_connector_enable_hpd(struct drm_connector *connector);
void drm_bridge_connector_disable_hpd(struct drm_connector *connector);
struct drm_connector *drm_bridge_connector_init(struct drm_device *drm,
						struct drm_encoder *encoder);

#endif /* __DRM_BRIDGE_CONNECTOR_H__ */
