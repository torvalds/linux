/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */
#ifndef DRM_AUX_BRIDGE_H
#define DRM_AUX_BRIDGE_H

#include <drm/drm_connector.h>

#if IS_ENABLED(CONFIG_DRM_AUX_BRIDGE)
int drm_aux_bridge_register(struct device *parent);
#else
static inline int drm_aux_bridge_register(struct device *parent)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRM_AUX_HPD_BRIDGE)
struct device *drm_dp_hpd_bridge_register(struct device *parent,
					  struct device_node *np);
void drm_aux_hpd_bridge_notify(struct device *dev, enum drm_connector_status status);
#else
static inline struct device *drm_dp_hpd_bridge_register(struct device *parent,
							struct device_node *np)
{
	return NULL;
}

static inline void drm_aux_hpd_bridge_notify(struct device *dev, enum drm_connector_status status)
{
}
#endif

#endif
