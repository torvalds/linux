/*
 *  hdac-ext-controller.c - HD-audio extended controller functions.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>

/*
 * maximum HDAC capablities we should parse to avoid endless looping:
 * currently we have 4 extended caps, so this is future proof for now.
 * extend when this limit is seen meeting in real HW
 */
#define HDAC_MAX_CAPS 10

/**
 * snd_hdac_ext_bus_parse_capabilities - parse capablity structure
 * @ebus: the pointer to extended bus object
 *
 * Returns 0 if successful, or a negative error code.
 */
int snd_hdac_ext_bus_parse_capabilities(struct hdac_ext_bus *ebus)
{
	unsigned int cur_cap;
	unsigned int offset;
	struct hdac_bus *bus = &ebus->bus;
	unsigned int counter = 0;

	offset = snd_hdac_chip_readl(bus, LLCH);

	/* Lets walk the linked capabilities list */
	do {
		cur_cap = _snd_hdac_chip_read(l, bus, offset);

		dev_dbg(bus->dev, "Capability version: 0x%x\n",
				((cur_cap & AZX_CAP_HDR_VER_MASK) >> AZX_CAP_HDR_VER_OFF));

		dev_dbg(bus->dev, "HDA capability ID: 0x%x\n",
				(cur_cap & AZX_CAP_HDR_ID_MASK) >> AZX_CAP_HDR_ID_OFF);

		switch ((cur_cap & AZX_CAP_HDR_ID_MASK) >> AZX_CAP_HDR_ID_OFF) {
		case AZX_ML_CAP_ID:
			dev_dbg(bus->dev, "Found ML capability\n");
			ebus->mlcap = bus->remap_addr + offset;
			break;

		case AZX_GTS_CAP_ID:
			dev_dbg(bus->dev, "Found GTS capability offset=%x\n", offset);
			ebus->gtscap = bus->remap_addr + offset;
			break;

		case AZX_PP_CAP_ID:
			/* PP capability found, the Audio DSP is present */
			dev_dbg(bus->dev, "Found PP capability offset=%x\n", offset);
			ebus->ppcap = bus->remap_addr + offset;
			break;

		case AZX_SPB_CAP_ID:
			/* SPIB capability found, handler function */
			dev_dbg(bus->dev, "Found SPB capability\n");
			ebus->spbcap = bus->remap_addr + offset;
			break;

		default:
			dev_dbg(bus->dev, "Unknown capability %d\n", cur_cap);
			break;
		}

		counter++;

		if (counter > HDAC_MAX_CAPS) {
			dev_err(bus->dev, "We exceeded HDAC Ext capablities!!!\n");
			break;
		}

		/* read the offset of next capabiity */
		offset = cur_cap & AZX_CAP_HDR_NXT_PTR_MASK;

	} while (offset);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_parse_capabilities);

/*
 * processing pipe helpers - these helpers are useful for dealing with HDA
 * new capability of processing pipelines
 */

/**
 * snd_hdac_ext_bus_ppcap_enable - enable/disable processing pipe capability
 * @ebus: HD-audio extended core bus
 * @enable: flag to turn on/off the capability
 */
void snd_hdac_ext_bus_ppcap_enable(struct hdac_ext_bus *ebus, bool enable)
{
	struct hdac_bus *bus = &ebus->bus;

	if (!ebus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL");
		return;
	}

	if (enable)
		snd_hdac_updatel(ebus->ppcap, AZX_REG_PP_PPCTL, 0, AZX_PPCTL_GPROCEN);
	else
		snd_hdac_updatel(ebus->ppcap, AZX_REG_PP_PPCTL, AZX_PPCTL_GPROCEN, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_enable);

/**
 * snd_hdac_ext_bus_ppcap_int_enable - ppcap interrupt enable/disable
 * @ebus: HD-audio extended core bus
 * @enable: flag to enable/disable interrupt
 */
void snd_hdac_ext_bus_ppcap_int_enable(struct hdac_ext_bus *ebus, bool enable)
{
	struct hdac_bus *bus = &ebus->bus;

	if (!ebus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL\n");
		return;
	}

	if (enable)
		snd_hdac_updatel(ebus->ppcap, AZX_REG_PP_PPCTL, 0, AZX_PPCTL_PIE);
	else
		snd_hdac_updatel(ebus->ppcap, AZX_REG_PP_PPCTL, AZX_PPCTL_PIE, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_int_enable);

/*
 * Multilink helpers - these helpers are useful for dealing with HDA
 * new multilink capability
 */

/**
 * snd_hdac_ext_bus_get_ml_capabilities - get multilink capability
 * @ebus: HD-audio extended core bus
 *
 * This will parse all links and read the mlink capabilities and add them
 * in hlink_list of extended hdac bus
 * Note: this will be freed on bus exit by driver
 */
int snd_hdac_ext_bus_get_ml_capabilities(struct hdac_ext_bus *ebus)
{
	int idx;
	u32 link_count;
	struct hdac_ext_link *hlink;
	struct hdac_bus *bus = &ebus->bus;

	link_count = readl(ebus->mlcap + AZX_REG_ML_MLCD) + 1;

	dev_dbg(bus->dev, "In %s Link count: %d\n", __func__, link_count);

	for (idx = 0; idx < link_count; idx++) {
		hlink  = kzalloc(sizeof(*hlink), GFP_KERNEL);
		if (!hlink)
			return -ENOMEM;
		hlink->index = idx;
		hlink->bus = bus;
		hlink->ml_addr = ebus->mlcap + AZX_ML_BASE +
					(AZX_ML_INTERVAL * idx);
		hlink->lcaps  = snd_hdac_chip_readl(bus, ML_LCAP);
		hlink->lsdiid = snd_hdac_chip_readw(bus, ML_LSDIID);

		list_add_tail(&hlink->list, &ebus->hlink_list);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_ml_capabilities);

/**
 * snd_hdac_link_free_all- free hdac extended link objects
 *
 * @ebus: HD-audio ext core bus
 */

void snd_hdac_link_free_all(struct hdac_ext_bus *ebus)
{
	struct hdac_ext_link *l;

	while (!list_empty(&ebus->hlink_list)) {
		l = list_first_entry(&ebus->hlink_list, struct hdac_ext_link, list);
		list_del(&l->list);
		kfree(l);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_link_free_all);

/**
 * snd_hdac_ext_bus_get_link_index - get link based on codec name
 * @ebus: HD-audio extended core bus
 * @codec_name: codec name
 */
struct hdac_ext_link *snd_hdac_ext_bus_get_link(struct hdac_ext_bus *ebus,
						 const char *codec_name)
{
	int i;
	struct hdac_ext_link *hlink = NULL;
	int bus_idx, addr;

	if (sscanf(codec_name, "ehdaudio%dD%d", &bus_idx, &addr) != 2)
		return NULL;
	if (ebus->idx != bus_idx)
		return NULL;

	list_for_each_entry(hlink, &ebus->hlink_list, list) {
		for (i = 0; i < HDA_MAX_CODECS; i++) {
			if (hlink->lsdiid & (0x1 << addr))
				return hlink;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_link);

static int check_hdac_link_power_active(struct hdac_ext_link *link, bool enable)
{
	int timeout;
	u32 val;
	int mask = (1 << AZX_MLCTL_CPA);

	udelay(3);
	timeout = 50;

	do {
		val = snd_hdac_chip_readl(link->bus, ML_LCTL);
		if (enable) {
			if (((val & mask) >> AZX_MLCTL_CPA))
				return 0;
		} else {
			if (!((val & mask) >> AZX_MLCTL_CPA))
				return 0;
		}
		udelay(3);
	} while (--timeout);

	return -EIO;
}

/**
 * snd_hdac_ext_bus_link_power_up -power up hda link
 * @link: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *link)
{
	snd_hdac_chip_updatel(link->bus, ML_LCTL, 0, AZX_MLCTL_SPA);

	return check_hdac_link_power_active(link, true);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_up);

/**
 * snd_hdac_ext_bus_link_power_down -power down hda link
 * @link: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *link)
{
	snd_hdac_chip_updatel(link->bus, ML_LCTL, AZX_MLCTL_SPA, 0);

	return check_hdac_link_power_active(link, false);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_down);
