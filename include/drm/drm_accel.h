/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef DRM_ACCEL_H_
#define DRM_ACCEL_H_

#define ACCEL_MAJOR		261

#if IS_ENABLED(CONFIG_DRM_ACCEL)

void accel_core_exit(void);
int accel_core_init(void);

#else

static inline void accel_core_exit(void)
{
}

static inline int __init accel_core_init(void)
{
	/* Return 0 to allow drm_core_init to complete successfully */
	return 0;
}

#endif /* IS_ENABLED(CONFIG_DRM_ACCEL) */

#endif /* DRM_ACCEL_H_ */
