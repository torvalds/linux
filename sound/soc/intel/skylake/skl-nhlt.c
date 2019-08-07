// SPDX-License-Identifier: GPL-2.0-only
/*
 *  skl-nhlt.c - Intel SKL Platform NHLT parsing
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Sanjiv Kumar <sanjiv.kumar@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/pci.h>
#include "skl.h"
#include "skl-i2s.h"

#define NHLT_ACPI_HEADER_SIG	"NHLT"

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
		if (nhlt_ptr->length)
			nhlt_table = (struct nhlt_acpi_table *)
				memremap(nhlt_ptr->min_addr, nhlt_ptr->length,
				MEMREMAP_WB);
		ACPI_FREE(obj);
		if (nhlt_table && (strncmp(nhlt_table->header.signature,
					NHLT_ACPI_HEADER_SIG,
					strlen(NHLT_ACPI_HEADER_SIG)) != 0)) {
			memunmap(nhlt_table);
			dev_err(dev, "NHLT ACPI header signature incorrect\n");
			return NULL;
		}
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
			(epnt->direction == dirn)) {
		/* do not check dev_type for DMIC link type */
		if (epnt->linktype == NHLT_LINK_DMIC)
			return true;

		if (epnt->device_type == dev_type)
			return true;
	}

	return false;
}

struct nhlt_specific_cfg
*skl_get_ep_blob(struct skl_dev *skl, u32 instance, u8 link_type,
			u8 s_fmt, u8 num_ch, u32 s_rate,
			u8 dirn, u8 dev_type)
{
	struct nhlt_fmt *fmt;
	struct nhlt_endpoint *epnt;
	struct hdac_bus *bus = skl_to_bus(skl);
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

int skl_get_dmic_geo(struct skl_dev *skl)
{
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	struct nhlt_endpoint *epnt;
	struct nhlt_dmic_array_config *cfg;
	struct device *dev = &skl->pci->dev;
	unsigned int dmic_geo = 0;
	u8 j;

	if (!nhlt)
		return 0;

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

int skl_nhlt_update_topology_bin(struct skl_dev *skl)
{
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	struct hdac_bus *bus = skl_to_bus(skl);
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
	struct hdac_bus *bus = pci_get_drvdata(pci);
	struct skl_dev *skl = bus_to_skl(bus);
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	char platform_id[32];

	sprintf(platform_id, "%x-%.6s-%.8s-%d", skl->pci_id,
			nhlt->header.oem_id, nhlt->header.oem_table_id,
			nhlt->header.oem_revision);

	skl_nhlt_trim_space(platform_id);
	return sprintf(buf, "%s\n", platform_id);
}

static DEVICE_ATTR(platform_id, 0444, skl_nhlt_platform_id_show, NULL);

int skl_nhlt_create_sysfs(struct skl_dev *skl)
{
	struct device *dev = &skl->pci->dev;

	if (sysfs_create_file(&dev->kobj, &dev_attr_platform_id.attr))
		dev_warn(dev, "Error creating sysfs entry\n");

	return 0;
}

void skl_nhlt_remove_sysfs(struct skl_dev *skl)
{
	struct device *dev = &skl->pci->dev;

	sysfs_remove_file(&dev->kobj, &dev_attr_platform_id.attr);
}

/*
 * Queries NHLT for all the fmt configuration for a particular endpoint and
 * stores all possible rates supported in a rate table for the corresponding
 * sclk/sclkfs.
 */
static void skl_get_ssp_clks(struct skl_dev *skl, struct skl_ssp_clk *ssp_clks,
				struct nhlt_fmt *fmt, u8 id)
{
	struct skl_i2s_config_blob_ext *i2s_config_ext;
	struct skl_i2s_config_blob_legacy *i2s_config;
	struct skl_clk_parent_src *parent;
	struct skl_ssp_clk *sclk, *sclkfs;
	struct nhlt_fmt_cfg *fmt_cfg;
	struct wav_fmt_ext *wav_fmt;
	unsigned long rate = 0;
	bool present = false;
	int rate_index = 0;
	u16 channels, bps;
	u8 clk_src;
	int i, j;
	u32 fs;

	sclk = &ssp_clks[SKL_SCLK_OFS];
	sclkfs = &ssp_clks[SKL_SCLKFS_OFS];

	if (fmt->fmt_count == 0)
		return;

	for (i = 0; i < fmt->fmt_count; i++) {
		fmt_cfg = &fmt->fmt_config[i];
		wav_fmt = &fmt_cfg->fmt_ext;

		channels = wav_fmt->fmt.channels;
		bps = wav_fmt->fmt.bits_per_sample;
		fs = wav_fmt->fmt.samples_per_sec;

		/*
		 * In case of TDM configuration on a ssp, there can
		 * be more than one blob in which channel masks are
		 * different for each usecase for a specific rate and bps.
		 * But the sclk rate will be generated for the total
		 * number of channels used for that endpoint.
		 *
		 * So for the given fs and bps, choose blob which has
		 * the superset of all channels for that endpoint and
		 * derive the rate.
		 */
		for (j = i; j < fmt->fmt_count; j++) {
			fmt_cfg = &fmt->fmt_config[j];
			wav_fmt = &fmt_cfg->fmt_ext;
			if ((fs == wav_fmt->fmt.samples_per_sec) &&
			   (bps == wav_fmt->fmt.bits_per_sample))
				channels = max_t(u16, channels,
						wav_fmt->fmt.channels);
		}

		rate = channels * bps * fs;

		/* check if the rate is added already to the given SSP's sclk */
		for (j = 0; (j < SKL_MAX_CLK_RATES) &&
			    (sclk[id].rate_cfg[j].rate != 0); j++) {
			if (sclk[id].rate_cfg[j].rate == rate) {
				present = true;
				break;
			}
		}

		/* Fill rate and parent for sclk/sclkfs */
		if (!present) {
			i2s_config_ext = (struct skl_i2s_config_blob_ext *)
						fmt->fmt_config[0].config.caps;

			/* MCLK Divider Source Select */
			if (is_legacy_blob(i2s_config_ext->hdr.sig)) {
				i2s_config = ext_to_legacy_blob(i2s_config_ext);
				clk_src = get_clk_src(i2s_config->mclk,
						SKL_MNDSS_DIV_CLK_SRC_MASK);
			} else {
				clk_src = get_clk_src(i2s_config_ext->mclk,
						SKL_MNDSS_DIV_CLK_SRC_MASK);
			}

			parent = skl_get_parent_clk(clk_src);

			/*
			 * Do not copy the config data if there is no parent
			 * clock available for this clock source select
			 */
			if (!parent)
				continue;

			sclk[id].rate_cfg[rate_index].rate = rate;
			sclk[id].rate_cfg[rate_index].config = fmt_cfg;
			sclkfs[id].rate_cfg[rate_index].rate = rate;
			sclkfs[id].rate_cfg[rate_index].config = fmt_cfg;
			sclk[id].parent_name = parent->name;
			sclkfs[id].parent_name = parent->name;

			rate_index++;
		}
	}
}

static void skl_get_mclk(struct skl_dev *skl, struct skl_ssp_clk *mclk,
				struct nhlt_fmt *fmt, u8 id)
{
	struct skl_i2s_config_blob_ext *i2s_config_ext;
	struct skl_i2s_config_blob_legacy *i2s_config;
	struct nhlt_specific_cfg *fmt_cfg;
	struct skl_clk_parent_src *parent;
	u32 clkdiv, div_ratio;
	u8 clk_src;

	fmt_cfg = &fmt->fmt_config[0].config;
	i2s_config_ext = (struct skl_i2s_config_blob_ext *)fmt_cfg->caps;

	/* MCLK Divider Source Select and divider */
	if (is_legacy_blob(i2s_config_ext->hdr.sig)) {
		i2s_config = ext_to_legacy_blob(i2s_config_ext);
		clk_src = get_clk_src(i2s_config->mclk,
				SKL_MCLK_DIV_CLK_SRC_MASK);
		clkdiv = i2s_config->mclk.mdivr &
				SKL_MCLK_DIV_RATIO_MASK;
	} else {
		clk_src = get_clk_src(i2s_config_ext->mclk,
				SKL_MCLK_DIV_CLK_SRC_MASK);
		clkdiv = i2s_config_ext->mclk.mdivr[0] &
				SKL_MCLK_DIV_RATIO_MASK;
	}

	/* bypass divider */
	div_ratio = 1;

	if (clkdiv != SKL_MCLK_DIV_RATIO_MASK)
		/* Divider is 2 + clkdiv */
		div_ratio = clkdiv + 2;

	/* Calculate MCLK rate from source using div value */
	parent = skl_get_parent_clk(clk_src);
	if (!parent)
		return;

	mclk[id].rate_cfg[0].rate = parent->rate/div_ratio;
	mclk[id].rate_cfg[0].config = &fmt->fmt_config[0];
	mclk[id].parent_name = parent->name;
}

void skl_get_clks(struct skl_dev *skl, struct skl_ssp_clk *ssp_clks)
{
	struct nhlt_acpi_table *nhlt = (struct nhlt_acpi_table *)skl->nhlt;
	struct nhlt_endpoint *epnt;
	struct nhlt_fmt *fmt;
	int i;
	u8 id;

	epnt = (struct nhlt_endpoint *)nhlt->desc;
	for (i = 0; i < nhlt->endpoint_count; i++) {
		if (epnt->linktype == NHLT_LINK_SSP) {
			id = epnt->virtual_bus_id;

			fmt = (struct nhlt_fmt *)(epnt->config.caps
					+ epnt->config.size);

			skl_get_ssp_clks(skl, ssp_clks, fmt, id);
			skl_get_mclk(skl, ssp_clks, fmt, id);
		}
		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}
}
