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
			dev_dbg(dev, "%s: found %d format definitions\n",
				__func__, fmt_configs->fmt_count);

			for (i = 0; i < fmt_configs->fmt_count; i++) {
				struct wav_fmt_ext *fmt_ext;

				fmt_ext = &fmt_configs->fmt_config[i].fmt_ext;

				if (fmt_ext->fmt.channels > max_ch)
					max_ch = fmt_ext->fmt.channels;
			}
			dev_dbg(dev, "%s: max channels found %d\n", __func__, max_ch);
		} else {
			dev_dbg(dev, "%s: No format information found\n", __func__);
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
				dev_dbg(dev, "%s: Array with %d dmics\n", __func__, dmic_geo);
			}
			if (max_ch > dmic_geo) {
				dev_dbg(dev, "%s: max channels %d exceed dmic number %d\n",
					__func__, max_ch, dmic_geo);
			}
		}
	}

	dev_dbg(dev, "%s: dmic number %d max_ch %d\n",
		__func__, dmic_geo, max_ch);

	return dmic_geo;
}
EXPORT_SYMBOL_GPL(intel_nhlt_get_dmic_geo);
