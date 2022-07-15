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
#include <sound/intel-nhlt.h>
#include "skl.h"
#include "skl-i2s.h"

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

	dev_dbg(dev, "oem_id %.6s, oem_table_id %.8s oem_revision %d\n",
		nhlt->header.oem_id, nhlt->header.oem_table_id,
		nhlt->header.oem_revision);

	snprintf(skl->tplg_name, sizeof(skl->tplg_name), "%x-%.6s-%.8s-%d%s",
		skl->pci_id, nhlt->header.oem_id, nhlt->header.oem_table_id,
		nhlt->header.oem_revision, "-tplg.bin");

	skl_nhlt_trim_space(skl->tplg_name);

	return 0;
}

static ssize_t platform_id_show(struct device *dev,
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

static DEVICE_ATTR_RO(platform_id);

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

	if (skl->nhlt)
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
	unsigned long rate;
	int rate_index = 0;
	u16 channels, bps;
	u8 clk_src;
	int i, j;
	u32 fs;

	sclk = &ssp_clks[SKL_SCLK_OFS];
	sclkfs = &ssp_clks[SKL_SCLKFS_OFS];

	if (fmt->fmt_count == 0)
		return;

	fmt_cfg = (struct nhlt_fmt_cfg *)fmt->fmt_config;
	for (i = 0; i < fmt->fmt_count; i++) {
		struct nhlt_fmt_cfg *saved_fmt_cfg = fmt_cfg;
		bool present = false;

		wav_fmt = &saved_fmt_cfg->fmt_ext;

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
			struct nhlt_fmt_cfg *tmp_fmt_cfg = fmt_cfg;

			wav_fmt = &tmp_fmt_cfg->fmt_ext;
			if ((fs == wav_fmt->fmt.samples_per_sec) &&
			   (bps == wav_fmt->fmt.bits_per_sample)) {
				channels = max_t(u16, channels,
						wav_fmt->fmt.channels);
				saved_fmt_cfg = tmp_fmt_cfg;
			}
			/* Move to the next nhlt_fmt_cfg */
			tmp_fmt_cfg = (struct nhlt_fmt_cfg *)(tmp_fmt_cfg->config.caps +
							      tmp_fmt_cfg->config.size);
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
			struct nhlt_fmt_cfg *first_fmt_cfg;

			first_fmt_cfg = (struct nhlt_fmt_cfg *)fmt->fmt_config;
			i2s_config_ext = (struct skl_i2s_config_blob_ext *)
						first_fmt_cfg->config.caps;

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

			/* Move to the next nhlt_fmt_cfg */
			fmt_cfg = (struct nhlt_fmt_cfg *)(fmt_cfg->config.caps +
							  fmt_cfg->config.size);
			/*
			 * Do not copy the config data if there is no parent
			 * clock available for this clock source select
			 */
			if (!parent)
				continue;

			sclk[id].rate_cfg[rate_index].rate = rate;
			sclk[id].rate_cfg[rate_index].config = saved_fmt_cfg;
			sclkfs[id].rate_cfg[rate_index].rate = rate;
			sclkfs[id].rate_cfg[rate_index].config = saved_fmt_cfg;
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
	struct nhlt_fmt_cfg *fmt_cfg;
	struct skl_clk_parent_src *parent;
	u32 clkdiv, div_ratio;
	u8 clk_src;

	fmt_cfg = (struct nhlt_fmt_cfg *)fmt->fmt_config;
	i2s_config_ext = (struct skl_i2s_config_blob_ext *)fmt_cfg->config.caps;

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
	mclk[id].rate_cfg[0].config = fmt_cfg;
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
