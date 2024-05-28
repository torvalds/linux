// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio Component Binding Interface
 *
 * Copyright (C) 2021, 2023 Cirrus Logic, Inc. and
 *			Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/acpi.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <sound/hda_codec.h>
#include "hda_component.h"
#include "hda_local.h"

#ifdef CONFIG_ACPI
void hda_component_acpi_device_notify(struct hda_component *comps, int num_comps,
				      acpi_handle handle, u32 event, void *data)
{
	int i;

	for (i = 0; i < num_comps; i++) {
		if (comps[i].dev && comps[i].acpi_notify)
			comps[i].acpi_notify(acpi_device_handle(comps[i].adev), event,
					     comps[i].dev);
	}
}
EXPORT_SYMBOL_NS_GPL(hda_component_acpi_device_notify, SND_HDA_SCODEC_COMPONENT);

int hda_component_manager_bind_acpi_notifications(struct hda_codec *cdc,
						  struct hda_component *comps, int num_comps,
						  acpi_notify_handler handler, void *data)
{
	bool support_notifications = false;
	struct acpi_device *adev;
	int ret;
	int i;

	adev = comps[0].adev;
	if (!acpi_device_handle(adev))
		return 0;

	for (i = 0; i < num_comps; i++)
		support_notifications = support_notifications ||
			comps[i].acpi_notifications_supported;

	if (support_notifications) {
		ret = acpi_install_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
						  handler, data);
		if (ret < 0) {
			codec_warn(cdc, "Failed to install notify handler: %d\n", ret);
			return 0;
		}

		codec_dbg(cdc, "Notify handler installed\n");
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(hda_component_manager_bind_acpi_notifications, SND_HDA_SCODEC_COMPONENT);

void hda_component_manager_unbind_acpi_notifications(struct hda_codec *cdc,
						     struct hda_component *comps,
						     acpi_notify_handler handler)
{
	struct acpi_device *adev;
	int ret;

	adev = comps[0].adev;
	if (!acpi_device_handle(adev))
		return;

	ret = acpi_remove_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY, handler);
	if (ret < 0)
		codec_warn(cdc, "Failed to uninstall notify handler: %d\n", ret);
}
EXPORT_SYMBOL_NS_GPL(hda_component_manager_unbind_acpi_notifications, SND_HDA_SCODEC_COMPONENT);
#endif /* ifdef CONFIG_ACPI */

void hda_component_manager_playback_hook(struct hda_component *comps, int num_comps, int action)
{
	int i;

	for (i = 0; i < num_comps; i++) {
		if (comps[i].dev && comps[i].pre_playback_hook)
			comps[i].pre_playback_hook(comps[i].dev, action);
	}
	for (i = 0; i < num_comps; i++) {
		if (comps[i].dev && comps[i].playback_hook)
			comps[i].playback_hook(comps[i].dev, action);
	}
	for (i = 0; i < num_comps; i++) {
		if (comps[i].dev && comps[i].post_playback_hook)
			comps[i].post_playback_hook(comps[i].dev, action);
	}
}
EXPORT_SYMBOL_NS_GPL(hda_component_manager_playback_hook, SND_HDA_SCODEC_COMPONENT);

struct hda_scodec_match {
	const char *bus;
	const char *hid;
	const char *match_str;
	int index;
};

/* match the device name in a slightly relaxed manner */
static int hda_comp_match_dev_name(struct device *dev, void *data)
{
	struct hda_scodec_match *p = data;
	const char *d = dev_name(dev);
	int n = strlen(p->bus);
	char tmp[32];

	/* check the bus name */
	if (strncmp(d, p->bus, n))
		return 0;
	/* skip the bus number */
	if (isdigit(d[n]))
		n++;
	/* the rest must be exact matching */
	snprintf(tmp, sizeof(tmp), p->match_str, p->hid, p->index);
	return !strcmp(d + n, tmp);
}

int hda_component_manager_init(struct hda_codec *cdc,
			       struct hda_component *comps, int count,
			       const char *bus, const char *hid,
			       const char *match_str,
			       const struct component_master_ops *ops)
{
	struct device *dev = hda_codec_dev(cdc);
	struct component_match *match = NULL;
	struct hda_scodec_match *sm;
	int ret, i;

	for (i = 0; i < count; i++) {
		sm = devm_kmalloc(dev, sizeof(*sm), GFP_KERNEL);
		if (!sm)
			return -ENOMEM;

		sm->bus = bus;
		sm->hid = hid;
		sm->match_str = match_str;
		sm->index = i;
		comps[i].codec = cdc;
		component_match_add(dev, &match, hda_comp_match_dev_name, sm);
	}

	ret = component_master_add_with_match(dev, ops, match);
	if (ret)
		codec_err(cdc, "Fail to register component aggregator %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(hda_component_manager_init, SND_HDA_SCODEC_COMPONENT);

void hda_component_manager_free(struct hda_codec *cdc,
				const struct component_master_ops *ops)
{
	struct device *dev = hda_codec_dev(cdc);

	component_master_del(dev, ops);
}
EXPORT_SYMBOL_NS_GPL(hda_component_manager_free, SND_HDA_SCODEC_COMPONENT);

MODULE_DESCRIPTION("HD Audio component binding library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
