// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2015-2019 Intel Corporation

#include <linux/acpi.h>
#include <sound/intel-nhlt.h>

struct nhlt_acpi_table *intel_nhlt_init(struct device *dev)
{
	struct nhlt_acpi_table *nhlt;
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_NHLT, 0,
				(struct acpi_table_header **)&nhlt);
	if (ACPI_FAILURE(status)) {
		dev_warn(dev, "NHLT table not found\n");
		return NULL;
	}

	return nhlt;
}
EXPORT_SYMBOL_GPL(intel_nhlt_init);

void intel_nhlt_free(struct nhlt_acpi_table *nhlt)
{
	acpi_put_table((struct acpi_table_header *)nhlt);
}
EXPORT_SYMBOL_GPL(intel_nhlt_free);

int intel_nhlt_get_dmic_geo(struct device *dev, struct nhlt_acpi_table *nhlt)
{
	struct nhlt_endpoint *epnt;
	struct nhlt_dmic_array_config *cfg;
	struct nhlt_vendor_dmic_array_config *cfg_vendor;
	struct nhlt_fmt *fmt_configs;
	unsigned int dmic_geo = 0;
	u16 max_ch = 0;
	u8 i, j;

	if (!nhlt)
		return 0;

	if (nhlt->header.length <= sizeof(struct acpi_table_header)) {
		dev_warn(dev, "Invalid DMIC description table\n");
		return 0;
	}

	for (j = 0, epnt = nhlt->desc; j < nhlt->endpoint_count; j++,
	     epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length)) {

		if (epnt->linktype != NHLT_LINK_DMIC)
			continue;

		cfg = (struct nhlt_dmic_array_config  *)(epnt->config.caps);
		fmt_configs = (struct nhlt_fmt *)(epnt->config.caps + epnt->config.size);

		/* find max number of channels based on format_configuration */
		if (fmt_configs->fmt_count) {
			struct nhlt_fmt_cfg *fmt_cfg = fmt_configs->fmt_config;

			dev_dbg(dev, "found %d format definitions\n",
				fmt_configs->fmt_count);

			for (i = 0; i < fmt_configs->fmt_count; i++) {
				struct wav_fmt_ext *fmt_ext;

				fmt_ext = &fmt_cfg->fmt_ext;

				if (fmt_ext->fmt.channels > max_ch)
					max_ch = fmt_ext->fmt.channels;

				/* Move to the next nhlt_fmt_cfg */
				fmt_cfg = (struct nhlt_fmt_cfg *)(fmt_cfg->config.caps +
								  fmt_cfg->config.size);
			}
			dev_dbg(dev, "max channels found %d\n", max_ch);
		} else {
			dev_dbg(dev, "No format information found\n");
		}

		if (cfg->device_config.config_type != NHLT_CONFIG_TYPE_MIC_ARRAY) {
			dmic_geo = max_ch;
		} else {
			switch (cfg->array_type) {
			case NHLT_MIC_ARRAY_2CH_SMALL:
			case NHLT_MIC_ARRAY_2CH_BIG:
				dmic_geo = MIC_ARRAY_2CH;
				break;

			case NHLT_MIC_ARRAY_4CH_1ST_GEOM:
			case NHLT_MIC_ARRAY_4CH_L_SHAPED:
			case NHLT_MIC_ARRAY_4CH_2ND_GEOM:
				dmic_geo = MIC_ARRAY_4CH;
				break;
			case NHLT_MIC_ARRAY_VENDOR_DEFINED:
				cfg_vendor = (struct nhlt_vendor_dmic_array_config *)cfg;
				dmic_geo = cfg_vendor->nb_mics;
				break;
			default:
				dev_warn(dev, "%s: undefined DMIC array_type 0x%0x\n",
					 __func__, cfg->array_type);
			}

			if (dmic_geo > 0) {
				dev_dbg(dev, "Array with %d dmics\n", dmic_geo);
			}
			if (max_ch > dmic_geo) {
				dev_dbg(dev, "max channels %d exceed dmic number %d\n",
					max_ch, dmic_geo);
			}
		}
	}

	dev_dbg(dev, "dmic number %d max_ch %d\n", dmic_geo, max_ch);

	return dmic_geo;
}
EXPORT_SYMBOL_GPL(intel_nhlt_get_dmic_geo);

bool intel_nhlt_has_endpoint_type(struct nhlt_acpi_table *nhlt, u8 link_type)
{
	struct nhlt_endpoint *epnt;
	int i;

	if (!nhlt)
		return false;

	epnt = (struct nhlt_endpoint *)nhlt->desc;
	for (i = 0; i < nhlt->endpoint_count; i++) {
		if (epnt->linktype == link_type)
			return true;

		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}
	return false;
}
EXPORT_SYMBOL(intel_nhlt_has_endpoint_type);

int intel_nhlt_ssp_endpoint_mask(struct nhlt_acpi_table *nhlt, u8 device_type)
{
	struct nhlt_endpoint *epnt;
	int ssp_mask = 0;
	int i;

	if (!nhlt || (device_type != NHLT_DEVICE_BT && device_type != NHLT_DEVICE_I2S))
		return 0;

	epnt = (struct nhlt_endpoint *)nhlt->desc;
	for (i = 0; i < nhlt->endpoint_count; i++) {
		if (epnt->linktype == NHLT_LINK_SSP && epnt->device_type == device_type) {
			/* for SSP the virtual bus id is the SSP port */
			ssp_mask |= BIT(epnt->virtual_bus_id);
		}
		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}

	return ssp_mask;
}
EXPORT_SYMBOL(intel_nhlt_ssp_endpoint_mask);

static struct nhlt_specific_cfg *
nhlt_get_specific_cfg(struct device *dev, struct nhlt_fmt *fmt, u8 num_ch,
		      u32 rate, u8 vbps, u8 bps)
{
	struct nhlt_fmt_cfg *cfg = fmt->fmt_config;
	struct wav_fmt *wfmt;
	u16 _bps, _vbps;
	int i;

	dev_dbg(dev, "Endpoint format count=%d\n", fmt->fmt_count);

	for (i = 0; i < fmt->fmt_count; i++) {
		wfmt = &cfg->fmt_ext.fmt;
		_bps = wfmt->bits_per_sample;
		_vbps = cfg->fmt_ext.sample.valid_bits_per_sample;

		dev_dbg(dev, "Endpoint format: ch=%d fmt=%d/%d rate=%d\n",
			wfmt->channels, _vbps, _bps, wfmt->samples_per_sec);

		if (wfmt->channels == num_ch && wfmt->samples_per_sec == rate &&
		    vbps == _vbps && bps == _bps)
			return &cfg->config;

		cfg = (struct nhlt_fmt_cfg *)(cfg->config.caps + cfg->config.size);
	}

	return NULL;
}

static bool nhlt_check_ep_match(struct device *dev, struct nhlt_endpoint *epnt,
				u32 bus_id, u8 link_type, u8 dir, u8 dev_type)
{
	dev_dbg(dev, "Endpoint: vbus_id=%d link_type=%d dir=%d dev_type = %d\n",
		epnt->virtual_bus_id, epnt->linktype,
		epnt->direction, epnt->device_type);

	if ((epnt->virtual_bus_id != bus_id) ||
	    (epnt->linktype != link_type) ||
	    (epnt->direction != dir))
		return false;

	/* link of type DMIC bypasses device_type check */
	return epnt->linktype == NHLT_LINK_DMIC ||
	       epnt->device_type == dev_type;
}

struct nhlt_specific_cfg *
intel_nhlt_get_endpoint_blob(struct device *dev, struct nhlt_acpi_table *nhlt,
			     u32 bus_id, u8 link_type, u8 vbps, u8 bps,
			     u8 num_ch, u32 rate, u8 dir, u8 dev_type)
{
	struct nhlt_specific_cfg *cfg;
	struct nhlt_endpoint *epnt;
	struct nhlt_fmt *fmt;
	int i;

	if (!nhlt)
		return NULL;

	dev_dbg(dev, "Looking for configuration:\n");
	dev_dbg(dev, "  vbus_id=%d link_type=%d dir=%d, dev_type=%d\n",
		bus_id, link_type, dir, dev_type);
	dev_dbg(dev, "  ch=%d fmt=%d/%d rate=%d\n", num_ch, vbps, bps, rate);
	dev_dbg(dev, "Endpoint count=%d\n", nhlt->endpoint_count);

	epnt = (struct nhlt_endpoint *)nhlt->desc;

	for (i = 0; i < nhlt->endpoint_count; i++) {
		if (nhlt_check_ep_match(dev, epnt, bus_id, link_type, dir, dev_type)) {
			fmt = (struct nhlt_fmt *)(epnt->config.caps + epnt->config.size);

			cfg = nhlt_get_specific_cfg(dev, fmt, num_ch, rate, vbps, bps);
			if (cfg)
				return cfg;
		}

		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}

	return NULL;
}
EXPORT_SYMBOL(intel_nhlt_get_endpoint_blob);
