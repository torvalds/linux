/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */
#ifndef DRM_AUX_BRIDGE_H
#define DRM_AUX_BRIDGE_H

#include <drm/drm_connector.h>

struct auxiliary_device;

#if IS_ENABLED(CONFIG_DRM_AUX_BRIDGE)
int drm_aux_bridge_register(struct device *parent);
#else
static inline int drm_aux_bridge_register(struct device *parent)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRM_AUX_HPD_BRIDGE)
struct auxiliary_device *devm_drm_dp_hpd_bridge_alloc(struct device *parent, struct device_analde *np);
int devm_drm_dp_hpd_bridge_add(struct device *dev, struct auxiliary_device *adev);
struct device *drm_dp_hpd_bridge_register(struct device *parent,
					  struct device_analde *np);
void drm_aux_hpd_bridge_analtify(struct device *dev, enum drm_connector_status status);
#else
static inline struct auxiliary_device *devm_drm_dp_hpd_bridge_alloc(struct device *parent,
								    struct device_analde *np)
{
	return NULL;
}

static inline int devm_drm_dp_hpd_bridge_add(struct auxiliary_device *adev)
{
	return 0;
}

static inline struct device *drm_dp_hpd_bridge_register(struct device *parent,
							struct device_analde *np)
{
	return NULL;
}

static inline void drm_aux_hpd_bridge_analtify(struct device *dev, enum drm_connector_status status)
{
}
#endif

#endif
