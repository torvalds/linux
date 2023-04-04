// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//

/*
 * Management of HDaudio multi-link (capabilities, power, coupling)
 */

#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda-mlink.h>

#include <linux/module.h>

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_MLINK)

int hda_bus_ml_get_capabilities(struct hdac_bus *bus)
{
	if (bus->mlcap)
		return snd_hdac_ext_bus_get_ml_capabilities(bus);
	return 0;
}
EXPORT_SYMBOL_NS(hda_bus_ml_get_capabilities, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_free(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink, *_h;

	if (!bus->mlcap)
		return;

	list_for_each_entry_safe(hlink, _h, &bus->hlink_list, list) {
		list_del(&hlink->list);
		kfree(hlink);
	}
}
EXPORT_SYMBOL_NS(hda_bus_ml_free, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_put_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	list_for_each_entry(hlink, &bus->hlink_list, list)
		snd_hdac_ext_bus_link_put(bus, hlink);
}
EXPORT_SYMBOL_NS(hda_bus_ml_put_all, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_reset_losidv(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	/* Reset stream-to-link mapping */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		writel(0, hlink->ml_addr + AZX_REG_ML_LOSIDV);
}
EXPORT_SYMBOL_NS(hda_bus_ml_reset_losidv, SND_SOC_SOF_HDA_MLINK);

int hda_bus_ml_resume(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;
	int ret;

	/* power up links that were active before suspend */
	list_for_each_entry(hlink, &bus->hlink_list, list) {
		if (hlink->ref_count) {
			ret = snd_hdac_ext_bus_link_power_up(hlink);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_NS(hda_bus_ml_resume, SND_SOC_SOF_HDA_MLINK);

int hda_bus_ml_suspend(struct hdac_bus *bus)
{
	return snd_hdac_ext_bus_link_power_down_all(bus);
}
EXPORT_SYMBOL_NS(hda_bus_ml_suspend, SND_SOC_SOF_HDA_MLINK);

#endif

MODULE_LICENSE("Dual BSD/GPL");
