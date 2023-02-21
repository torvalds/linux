// SPDX-License-Identifier: GPL-2.0-only
/*
 *  hdac-ext-controller.c - HD-audio extended controller functions.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>

/*
 * processing pipe helpers - these helpers are useful for dealing with HDA
 * new capability of processing pipelines
 */

/**
 * snd_hdac_ext_bus_ppcap_enable - enable/disable processing pipe capability
 * @bus: the pointer to HDAC bus object
 * @enable: flag to turn on/off the capability
 */
void snd_hdac_ext_bus_ppcap_enable(struct hdac_bus *bus, bool enable)
{

	if (!bus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL");
		return;
	}

	if (enable)
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL,
				 AZX_PPCTL_GPROCEN, AZX_PPCTL_GPROCEN);
	else
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL,
				 AZX_PPCTL_GPROCEN, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_enable);

/**
 * snd_hdac_ext_bus_ppcap_int_enable - ppcap interrupt enable/disable
 * @bus: the pointer to HDAC bus object
 * @enable: flag to enable/disable interrupt
 */
void snd_hdac_ext_bus_ppcap_int_enable(struct hdac_bus *bus, bool enable)
{

	if (!bus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL\n");
		return;
	}

	if (enable)
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL,
				 AZX_PPCTL_PIE, AZX_PPCTL_PIE);
	else
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL,
				 AZX_PPCTL_PIE, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_int_enable);

/*
 * Multilink helpers - these helpers are useful for dealing with HDA
 * new multilink capability
 */

/**
 * snd_hdac_ext_bus_get_ml_capabilities - get multilink capability
 * @bus: the pointer to HDAC bus object
 *
 * This will parse all links and read the mlink capabilities and add them
 * in hlink_list of extended hdac bus
 * Note: this will be freed on bus exit by driver
 */
int snd_hdac_ext_bus_get_ml_capabilities(struct hdac_bus *bus)
{
	int idx;
	u32 link_count;
	struct hdac_ext_link *hlink;

	link_count = readl(bus->mlcap + AZX_REG_ML_MLCD) + 1;

	dev_dbg(bus->dev, "In %s Link count: %d\n", __func__, link_count);

	for (idx = 0; idx < link_count; idx++) {
		hlink  = kzalloc(sizeof(*hlink), GFP_KERNEL);
		if (!hlink)
			return -ENOMEM;
		hlink->index = idx;
		hlink->bus = bus;
		hlink->ml_addr = bus->mlcap + AZX_ML_BASE +
					(AZX_ML_INTERVAL * idx);
		hlink->lcaps  = readl(hlink->ml_addr + AZX_REG_ML_LCAP);
		hlink->lsdiid = readw(hlink->ml_addr + AZX_REG_ML_LSDIID);

		/* since link in On, update the ref */
		hlink->ref_count = 1;

		list_add_tail(&hlink->list, &bus->hlink_list);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_ml_capabilities);

/**
 * snd_hdac_ext_link_free_all- free hdac extended link objects
 *
 * @bus: the pointer to HDAC bus object
 */

void snd_hdac_ext_link_free_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	while (!list_empty(&bus->hlink_list)) {
		hlink = list_first_entry(&bus->hlink_list, struct hdac_ext_link, list);
		list_del(&hlink->list);
		kfree(hlink);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_link_free_all);

/**
 * snd_hdac_ext_bus_get_hlink_by_addr - get hlink at specified address
 * @bus: hlink's parent bus device
 * @addr: codec device address
 *
 * Returns hlink object or NULL if matching hlink is not found.
 */
struct hdac_ext_link *snd_hdac_ext_bus_get_hlink_by_addr(struct hdac_bus *bus, int addr)
{
	struct hdac_ext_link *hlink;

	list_for_each_entry(hlink, &bus->hlink_list, list)
		if (hlink->lsdiid & (0x1 << addr))
			return hlink;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_hlink_by_addr);

/**
 * snd_hdac_ext_bus_get_hlink_by_name - get hlink based on codec name
 * @bus: the pointer to HDAC bus object
 * @codec_name: codec name
 */
struct hdac_ext_link *snd_hdac_ext_bus_get_hlink_by_name(struct hdac_bus *bus,
							 const char *codec_name)
{
	int bus_idx, addr;

	if (sscanf(codec_name, "ehdaudio%dD%d", &bus_idx, &addr) != 2)
		return NULL;
	if (bus->idx != bus_idx)
		return NULL;
	if (addr < 0 || addr > 31)
		return NULL;

	return snd_hdac_ext_bus_get_hlink_by_addr(bus, addr);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_hlink_by_name);

static int check_hdac_link_power_active(struct hdac_ext_link *hlink, bool enable)
{
	int timeout;
	u32 val;
	int mask = (1 << AZX_ML_LCTL_CPA_SHIFT);

	udelay(3);
	timeout = 150;

	do {
		val = readl(hlink->ml_addr + AZX_REG_ML_LCTL);
		if (enable) {
			if (((val & mask) >> AZX_ML_LCTL_CPA_SHIFT))
				return 0;
		} else {
			if (!((val & mask) >> AZX_ML_LCTL_CPA_SHIFT))
				return 0;
		}
		udelay(3);
	} while (--timeout);

	return -EIO;
}

/**
 * snd_hdac_ext_bus_link_power_up -power up hda link
 * @hlink: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *hlink)
{
	snd_hdac_updatel(hlink->ml_addr, AZX_REG_ML_LCTL,
			 AZX_ML_LCTL_SPA, AZX_ML_LCTL_SPA);

	return check_hdac_link_power_active(hlink, true);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_up);

/**
 * snd_hdac_ext_bus_link_power_down -power down hda link
 * @hlink: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *hlink)
{
	snd_hdac_updatel(hlink->ml_addr, AZX_REG_ML_LCTL, AZX_ML_LCTL_SPA, 0);

	return check_hdac_link_power_active(hlink, false);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_down);

/**
 * snd_hdac_ext_bus_link_power_up_all -power up all hda link
 * @bus: the pointer to HDAC bus object
 */
int snd_hdac_ext_bus_link_power_up_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink = NULL;
	int ret;

	list_for_each_entry(hlink, &bus->hlink_list, list) {
		ret = snd_hdac_ext_bus_link_power_up(hlink);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_up_all);

/**
 * snd_hdac_ext_bus_link_power_down_all -power down all hda link
 * @bus: the pointer to HDAC bus object
 */
int snd_hdac_ext_bus_link_power_down_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink = NULL;
	int ret;

	list_for_each_entry(hlink, &bus->hlink_list, list) {
		ret = snd_hdac_ext_bus_link_power_down(hlink);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_down_all);

/**
 * snd_hdac_ext_bus_link_set_stream_id - maps stream id to link output
 * @link: HD-audio ext link to set up
 * @stream: stream id
 */
void snd_hdac_ext_bus_link_set_stream_id(struct hdac_ext_link *link,
					 int stream)
{
	snd_hdac_updatew(link->ml_addr, AZX_REG_ML_LOSIDV, (1 << stream), 1 << stream);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_set_stream_id);

/**
 * snd_hdac_ext_bus_link_clear_stream_id - maps stream id to link output
 * @link: HD-audio ext link to set up
 * @stream: stream id
 */
void snd_hdac_ext_bus_link_clear_stream_id(struct hdac_ext_link *link,
					   int stream)
{
	snd_hdac_updatew(link->ml_addr, AZX_REG_ML_LOSIDV, (1 << stream), 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_clear_stream_id);

int snd_hdac_ext_bus_link_get(struct hdac_bus *bus,
				struct hdac_ext_link *hlink)
{
	unsigned long codec_mask;
	int ret = 0;

	mutex_lock(&bus->lock);

	/*
	 * if we move from 0 to 1, count will be 1 so power up this link
	 * as well, also check the dma status and trigger that
	 */
	if (++hlink->ref_count == 1) {
		if (!bus->cmd_dma_state) {
			snd_hdac_bus_init_cmd_io(bus);
			bus->cmd_dma_state = true;
		}

		ret = snd_hdac_ext_bus_link_power_up(hlink);

		/*
		 * clear the register to invalidate all the output streams
		 */
		snd_hdac_updatew(hlink->ml_addr, AZX_REG_ML_LOSIDV,
				 AZX_ML_LOSIDV_STREAM_MASK, 0);
		/*
		 *  wait for 521usec for codec to report status
		 *  HDA spec section 4.3 - Codec Discovery
		 */
		udelay(521);
		codec_mask = snd_hdac_chip_readw(bus, STATESTS);
		dev_dbg(bus->dev, "codec_mask = 0x%lx\n", codec_mask);
		snd_hdac_chip_writew(bus, STATESTS, codec_mask);
		if (!bus->codec_mask)
			bus->codec_mask = codec_mask;
	}

	mutex_unlock(&bus->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_get);

int snd_hdac_ext_bus_link_put(struct hdac_bus *bus,
			      struct hdac_ext_link *hlink)
{
	int ret = 0;
	struct hdac_ext_link *hlink_tmp;
	bool link_up = false;

	mutex_lock(&bus->lock);

	/*
	 * if we move from 1 to 0, count will be 0
	 * so power down this link as well
	 */
	if (--hlink->ref_count == 0) {
		ret = snd_hdac_ext_bus_link_power_down(hlink);

		/*
		 * now check if all links are off, if so turn off
		 * cmd dma as well
		 */
		list_for_each_entry(hlink_tmp, &bus->hlink_list, list) {
			if (hlink_tmp->ref_count) {
				link_up = true;
				break;
			}
		}

		if (!link_up) {
			snd_hdac_bus_stop_cmd_io(bus);
			bus->cmd_dma_state = false;
		}
	}

	mutex_unlock(&bus->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_put);

static void hdac_ext_codec_link_up(struct hdac_device *codec)
{
	const char *devname = dev_name(&codec->dev);
	struct hdac_ext_link *hlink =
		snd_hdac_ext_bus_get_hlink_by_name(codec->bus, devname);

	if (hlink)
		snd_hdac_ext_bus_link_get(codec->bus, hlink);
}

static void hdac_ext_codec_link_down(struct hdac_device *codec)
{
	const char *devname = dev_name(&codec->dev);
	struct hdac_ext_link *hlink =
		snd_hdac_ext_bus_get_hlink_by_name(codec->bus, devname);

	if (hlink)
		snd_hdac_ext_bus_link_put(codec->bus, hlink);
}

void snd_hdac_ext_bus_link_power(struct hdac_device *codec, bool enable)
{
	struct hdac_bus *bus = codec->bus;
	bool oldstate = test_bit(codec->addr, &bus->codec_powered);

	if (enable == oldstate)
		return;

	snd_hdac_bus_link_power(codec, enable);

	if (enable)
		hdac_ext_codec_link_up(codec);
	else
		hdac_ext_codec_link_down(codec);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power);
