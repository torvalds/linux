/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_HDMI_NOTIFIER_H
#define LINUX_HDMI_NOTIFIER_H

#include <linux/types.h>

enum {
	HDMI_CONNECTED,
	HDMI_DISCONNECTED,
	HDMI_NEW_EDID,
	HDMI_NEW_ELD,
};

struct hdmi_event_base {
	struct device *source;
};

struct hdmi_event_new_edid {
	struct hdmi_event_base base;
	const void *edid;
	size_t size;
};

struct hdmi_event_new_eld {
	struct hdmi_event_base base;
	unsigned char eld[128];
};

union hdmi_event {
	struct hdmi_event_base base;
	struct hdmi_event_new_edid edid;
	struct hdmi_event_new_eld eld;
};

struct notifier_block;

int hdmi_register_notifier(struct notifier_block *nb);
int hdmi_unregister_notifier(struct notifier_block *nb);

void hdmi_event_connect(struct device *dev);
void hdmi_event_disconnect(struct device *dev);
void hdmi_event_new_edid(struct device *dev, const void *edid, size_t size);
void hdmi_event_new_eld(struct device *dev, const void *eld);

#endif
