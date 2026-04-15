/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef __DRM_RAS_GENL_FAMILY_H__
#define __DRM_RAS_GENL_FAMILY_H__

#if IS_ENABLED(CONFIG_DRM_RAS)
int drm_ras_genl_family_register(void);
void drm_ras_genl_family_unregister(void);
#else
static inline int drm_ras_genl_family_register(void) { return 0; }
static inline void drm_ras_genl_family_unregister(void) { }
#endif

#endif
