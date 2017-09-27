/*
 *  skl-nhlt.c - Intel SKL Platform NHLT parsing
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Sanjiv Kumar <sanjiv.kumar@intel.com>
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
 *
 */
#include <linux/pci.h>
#include "skl.h"

/* Unique identification for getting NHLT blobs */
static guid_t osc_guid =
	GUID_INIT(0xA69F886E, 0x6CEB, 0x4594,
		  0xA4, 0x1F, 0x7B, 0x5D, 0xCE, 0x24, 0xC5, 0x53);

struct nhlt_acpi_table *skl_nhlt_init(struct device *dev)
{
	acpi_handle handle;
	union acpi_object *obj;
	struct nhlt_resource_desc  *nhlt_ptr = NULL;
	struct nhlt_acpi_table *nhlt_table = NULL;

	handle = ACPI_HANDLE(dev);
	if (!handle) {
		dev_err(dev, "Didn't find ACPI_HANDLE\n");
		return NULL;
	}

	obj = acpi_evaluate_dsm(handle, &osc_guid, 1, 1, NULL);
	if (obj && obj->type == ACPI_TYPE_BUFFER) {
		nhlt_ptr = (struct nhlt_resource_desc  *)obj->buffer.pointer;
		nhlt_table = (struct nhlt_acpi_table *)
				memremap(nhlt_ptr->min_addr, nhlt_ptr->length,
				MEMREMAP_WB);
		ACPI_FREE(obj);
		return nhlt_table;
	}

	dev_err(dev, "device specific method to extract NHLT blob failed\n");
	return NULL;
}

void skl_nhlt_free(struct nhlt_acpi_table *nhlt)
{
	memunmap((void *) nhlt);
}

static struct nhlt_specific_cfg *skl_get_specific_cfg(
		struct device *dev, struct nhlt_fmt *fmt,
		u8 no_ch, u32 rate, u16 bps, u8 linktype)
{
	struct nhlt_specific_cfg *sp_config;
	struct wav_fmt *wfmt;
	struct nhlt_fmt_cfg *fmt_config = fmt->fmt_config;
	int i;

	dev_dbg(dev, "Format count =%d\n", fmt->fmt_count);

	for (i = 0; i < fmt->fmt_count; i++) {
		wfmt = &fmt_config->fmt_ext.fmt;
		dev_dbg(dev, "ch=%d fmt=%d s_rate=%d\n", wfmt->channels,
			 wfmt->bits_per_sample, wfmt->samples_per_sec);
		if (wfmt->channels == no_ch && wfmt->bits_per_sample == bps) {
			/*
			 * if link type is dmic ignore rate check as the blob is
			 * generic for all rates
			 */
			sp_config = &fmt_config->config;
			if (linktype == NHLT_LINK_DMIC)
				return sp_config;

			if (wfmt->samples_per_sec == rate)
				return sp_config;
		}

		fmt_config = (struct nhlt_fmt_cfg *)(fmt_config->config.caps +
						fmt_config->config.size);
	}

	return NULL;
}

static void dump_config(struct device *dev, u32 instance_id, u8 linktype,
		u8 s_fmt, u8 num_channels, u32 s_rate, u8 dirn, u16 bps)
{
	dev_dbg(dev, "Input configuration\n");
	dev_dbg(dev, "ch=%d fmt=%d s_rate=%d\n", num_channels, s_fmt, s_rate);
	dev_dbg(dev, "vbus_id=%d link_type=%d\n", instance_id, linktype);
	dev_dbg(dev, "bits_per_sample=%d\n", bps);
}

static bool skl_check_ep_match(struct device *dev, struct nhlt_endpoint *epnt,
		u32 instance_id, u8 link_type, u8 dirn, u8 dev_type)
{
	dev_dbg(dev, "vbus_id=%d link_type=%d dir=%d dev_type = %d\n",
			epnt->virtual_bus_id, epnt->linktype,
			epnt->direction, epnt->device_type);

	if ((epnt->virtual_bus_id == instance_id) &&
			(epnt->linktype == link_type) &&
			(epnt->direction == dirn) &&
			(epnt->device_type == dev_type))
		return true;
	else
		return false;
}

struct nhlt_specific_cfg
*skl_get_ep_blob(struct skl *skl, u32 instance, u8 link_type,
			u8 s_fmt, u8 num_ch, u32 s_rate,
			u8 dirn, u8 dev_type)
{
	struct nhlt_fmt *fmt;
	struct nhlt_endpoint *epnt;
	struct hdac_bus *bus = ebus_to_hbus(&skl->ebus);
	struct device *dev = bus->dev;
	struct nhlt_specific_cfg *sp_config;
	struct nhlt_acpi_table *nhlt = skl->nhlt;
	u16 bps = (s_fmt == 16) ? 16 : 32;
	u8 j;

	dump_config(dev, instance, link_type, s_fmt, num_ch, s_rate, dirn, bps);

	epnt = (struct nhlt_endpoint *)nhlt->desc;

	dev_dbg(dev, "endpoint count =%d\n", nhlt->endpoint_count);

	for (j = 0; j < nhlt->endpoint_count; j++) {
		if (skl_check_ep_match(dev, epnt, instance, link_type,
						dirn, dev_type)) {
			fmt = (struct nhlt_fmt *)(epnt->config.caps +
						 epnt->config.size);
			sp_config = skl_get_specific_cfg(dev, fmt, num_ch,
							s_rate, bps, link_type);
			if (sp_config)
				return sp_config;
		}

		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}

	return NULL;
}

int skl_get_dmic_geo(struct skl *skl)
{
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	struct nhlt_endpoint *epnt;
	struct nhlt_dmic_array_config *cfg;
	struct device *dev = &skl->pci->dev;
	unsigned int dmic_geo = 0;
	u8 j;

	epnt = (struct nhlt_endpoint *)nhlt->desc;

	for (j = 0; j < nhlt->endpoint_count; j++) {
		if (epnt->linktype == NHLT_LINK_DMIC) {
			cfg = (struct nhlt_dmic_array_config  *)
					(epnt->config.caps);
			switch (cfg->array_type) {
			case NHLT_MIC_ARRAY_2CH_SMALL:
			case NHLT_MIC_ARRAY_2CH_BIG:
				dmic_geo |= MIC_ARRAY_2CH;
				break;

			case NHLT_MIC_ARRAY_4CH_1ST_GEOM:
			case NHLT_MIC_ARRAY_4CH_L_SHAPED:
			case NHLT_MIC_ARRAY_4CH_2ND_GEOM:
				dmic_geo |= MIC_ARRAY_4CH;
				break;

			default:
				dev_warn(dev, "undefined DMIC array_type 0x%0x\n",
						cfg->array_type);

			}
		}
		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}

	return dmic_geo;
}

static void skl_nhlt_trim_space(char *trim)
{
	char *s = trim;
	int cnt;
	int i;

	cnt = 0;
	for (i = 0; s[i]; i++) {
		if (!isspace(s[i]))
			s[cnt++] = s[i];
	}

	s[cnt] = '\0';
}

int skl_nhlt_update_topology_bin(struct skl *skl)
{
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	struct hdac_bus *bus = ebus_to_hbus(&skl->ebus);
	struct device *dev = bus->dev;

	dev_dbg(dev, "oem_id %.6s, oem_table_id %8s oem_revision %d\n",
		nhlt->header.oem_id, nhlt->header.oem_table_id,
		nhlt->header.oem_revision);

	snprintf(skl->tplg_name, sizeof(skl->tplg_name), "%x-%.6s-%.8s-%d%s",
		skl->pci_id, nhlt->header.oem_id, nhlt->header.oem_table_id,
		nhlt->header.oem_revision, "-tplg.bin");

	skl_nhlt_trim_space(skl->tplg_name);

	return 0;
}

static ssize_t skl_nhlt_platform_id_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct skl *skl = ebus_to_skl(ebus);
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	char platform_id[32];

	sprintf(platform_id, "%x-%.6s-%.8s-%d", skl->pci_id,
			nhlt->header.oem_id, nhlt->header.oem_table_id,
			nhlt->header.oem_revision);

	skl_nhlt_trim_space(platform_id);
	return sprintf(buf, "%s\n", platform_id);
}

static DEVICE_ATTR(platform_id, 0444, skl_nhlt_platform_id_show, NULL);

int skl_nhlt_create_sysfs(struct skl *skl)
{
	struct device *dev = &skl->pci->dev;

	if (sysfs_create_file(&dev->kobj, &dev_attr_platform_id.attr))
		dev_warn(dev, "Error creating sysfs entry\n");

	return 0;
}

void skl_nhlt_remove_sysfs(struct skl *skl)
{
	struct device *dev = &skl->pci->dev;

	sysfs_remove_file(&dev->kobj, &dev_attr_platform_id.attr);
}
