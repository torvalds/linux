/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#ifndef __DRM_PRIVACY_SCREEN_MACHINE_H__
#define __DRM_PRIVACY_SCREEN_MACHINE_H__

#include <linux/list.h>

/**
 * struct drm_privacy_screen_lookup -  static privacy-screen lookup list entry
 *
 * Used for the static lookup-list for mapping privacy-screen consumer
 * dev-connector pairs to a privacy-screen provider.
 */
struct drm_privacy_screen_lookup {
	/** @list: Lookup list list-entry. */
	struct list_head list;
	/** @dev_id: Consumer device name or NULL to match all devices. */
	const char *dev_id;
	/** @con_id: Consumer connector name or NULL to match all connectors. */
	const char *con_id;
	/** @provider: dev_name() of the privacy_screen provider. */
	const char *provider;
};

void drm_privacy_screen_lookup_add(struct drm_privacy_screen_lookup *lookup);
void drm_privacy_screen_lookup_remove(struct drm_privacy_screen_lookup *lookup);

#if IS_ENABLED(CONFIG_DRM_PRIVACY_SCREEN) && IS_ENABLED(CONFIG_X86)
void drm_privacy_screen_lookup_init(void);
void drm_privacy_screen_lookup_exit(void);
#else
static inline void drm_privacy_screen_lookup_init(void)
{
}
static inline void drm_privacy_screen_lookup_exit(void)
{
}
#endif

#endif
