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
				dev_warn(dev, "undefined DMIC array_type 0x%0x\n",
					 cfg->array_type);
			}
		}
		epnt = (struct nhlt_endpoint *)((u8 *)epnt + epnt->length);
	}

	return dmic_geo;
}
EXPORT_SYMBOL_GPL(intel_nhlt_get_dmic_geo);
