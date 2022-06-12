/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __PANEL_EVENT_NOTIFIER_H
#define __PANEL_EVENT_NOTIFIER_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <drm/drm_panel.h>

enum panel_event_notifier_tag {
	PANEL_EVENT_NOTIFICATION_NONE,
	PANEL_EVENT_NOTIFICATION_PRIMARY,
	PANEL_EVENT_NOTIFICATION_SECONDARY,
	PANEL_EVENT_NOTIFICATION_MAX
};

enum panel_event_notifier_client {
	PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH,
	PANEL_EVENT_NOTIFIER_CLIENT_SECONDARY_TOUCH,
	PANEL_EVENT_NOTIFIER_CLIENT_ECM,
	PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER,
	PANEL_EVENT_NOTIFIER_CLIENT_MAX
};

enum panel_event_notification_type {
	DRM_PANEL_EVENT_NONE,
	DRM_PANEL_EVENT_BLANK,
	DRM_PANEL_EVENT_UNBLANK,
	DRM_PANEL_EVENT_BLANK_LP,
	DRM_PANEL_EVENT_FPS_CHANGE,
	DRM_PANEL_EVENT_MAX
};

struct panel_event_notification_data {
	u32 old_fps;
	u32 new_fps;
	bool early_trigger;
};

struct panel_event_notification {
	enum panel_event_notification_type notif_type;
	struct panel_event_notification_data notif_data;
	struct drm_panel *panel;
};

typedef void (*panel_event_notifier_handler)(enum panel_event_notifier_tag tag,
				struct panel_event_notification *notification,
					void *pvt_data);

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
		enum panel_event_notifier_client client_handle,
		struct drm_panel *panel,
		panel_event_notifier_handler notif_handler, void *pvt_data);
void panel_event_notifier_unregister(void *cookie);
void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification);

#else
static inline void *panel_event_notifier_register(enum panel_event_notifier_tag tag,
		enum panel_event_notifier_client client_handle,
		struct drm_panel *panel,
		panel_event_notifier_handler notif_handler, void *pvt_data)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void panel_event_notifier_unregister(void *cookie)
{
}
static inline void panel_event_notification_trigger(
		enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification)
{
}
#endif
#endif
