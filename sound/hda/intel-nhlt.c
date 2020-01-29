// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2015-2019 Intel Corporation

#include <linux/acpi.h>
#include <sound/intel-nhlt.h>

#define NHLT_ACPI_HEADER_SIG	"NHLT"

/* Unique identification for getting NHLT blobs */
static const guid_t osc_guid =
	GUID_INIT(0xA69F886E, 0x6CEB, 0x4594,
		  0xA4, 0x1F, 0x7B, 0x5D, 0xCE, 0x24, 0xC5, 0x53);

struct nhlt_acpi_table *intel_nhlt_init(struct device *dev)
{
	acpi_handle handle;
	union acpi_object *obj;
	struct nhlt_resource_desc *nhlt_ptr;
	struct nhlt_acpi_table *nhlt_table = NULL;

	handle = ACPI_HANDLE(dev);
	if (!handle) {
		dev_err(dev, "Didn't find ACPI_HANDLE\n");
		return NULL;
	}

	obj = acpi_evaluate_dsm(handle, &osc_guid, 1, 1, NULL);

	if (!obj)
		return NULL;

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_dbg(dev, "No NHLT table found\n");
		ACPI_FREE(obj);
		return NULL;
	}

	nhlt_ptr = (struct nhlt_resource_desc  *)obj->buffer.pointer;
	if (nhlt_ptr->length)
		nhlt_table = (struct nhlt_acpi_table *)
			memremap(nhlt_ptr->min_addr, nhlt_ptr->length,
				 MEMREMAP_WB);
	ACPI_FREE(obj);
	if (nhlt_table &&
	    (strncmp(nhlt_table->header.signature,
		     NHLT_ACPI_HEADER_SIG,
		     strlen(NHLT_ACPI_HEADER_SIG)) != 0)) {
		memunmap(nhlt_table);
		dev_err(dev, "NHLT ACPI header signature incorrect\n");
		return NULL;
	}
	return nhlt_table;
}
EXPORT_SYMBOL_GPL(intel_nhlt_init);

void intel_nhlt_free(struct nhlt_acpi_table *nhlt)
{
	memunmap((void *)nhlt);
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
