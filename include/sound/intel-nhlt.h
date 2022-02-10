/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  intel-nhlt.h - Intel HDA Platform NHLT header
 *
 *  Copyright (c) 2015-2019 Intel Corporation
 */

#ifndef __INTEL_NHLT_H__
#define __INTEL_NHLT_H__

#include <linux/acpi.h>

enum nhlt_link_type {
	NHLT_LINK_HDA = 0,
	NHLT_LINK_DSP = 1,
	NHLT_LINK_DMIC = 2,
	NHLT_LINK_SSP = 3,
	NHLT_LINK_INVALID
};

#if IS_ENABLED(CONFIG_ACPI) && IS_ENABLED(CONFIG_SND_INTEL_NHLT)

struct wav_fmt {
	u16 fmt_tag;
	u16 channels;
	u32 samples_per_sec;
	u32 avg_bytes_per_sec;
	u16 block_align;
	u16 bits_per_sample;
	u16 cb_size;
} __packed;

struct wav_fmt_ext {
	struct wav_fmt fmt;
	union samples {
		u16 valid_bits_per_sample;
		u16 samples_per_block;
		u16 reserved;
	} sample;
	u32 channel_mask;
	u8 sub_fmt[16];
} __packed;

enum nhlt_device_type {
	NHLT_DEVICE_BT = 0,
	NHLT_DEVICE_DMIC = 1,
	NHLT_DEVICE_I2S = 4,
	NHLT_DEVICE_INVALID
};

struct nhlt_specific_cfg {
	u32 size;
	u8 caps[];
} __packed;

struct nhlt_fmt_cfg {
	struct wav_fmt_ext fmt_ext;
	struct nhlt_specific_cfg config;
} __packed;

struct nhlt_fmt {
	u8 fmt_count;
	struct nhlt_fmt_cfg fmt_config[];
} __packed;

struct nhlt_endpoint {
	u32  length;
	u8   linktype;
	u8   instance_id;
	u16  vendor_id;
	u16  device_id;
	u16  revision_id;
	u32  subsystem_id;
	u8   device_type;
	u8   direction;
	u8   virtual_bus_id;
	struct nhlt_specific_cfg config;
} __packed;

struct nhlt_acpi_table {
	struct acpi_table_header header;
	u8 endpoint_count;
	struct nhlt_endpoint desc[];
} __packed;

struct nhlt_resource_desc  {
	u32 extra;
	u16 flags;
	u64 addr_spc_gra;
	u64 min_addr;
	u64 max_addr;
	u64 addr_trans_offset;
	u64 length;
} __packed;

#define MIC_ARRAY_2CH 2
#define MIC_ARRAY_4CH 4

struct nhlt_device_specific_config {
	u8 virtual_slot;
	u8 config_type;
} __packed;

struct nhlt_dmic_array_config {
	struct nhlt_device_specific_config device_config;
	u8 array_type;
} __packed;

struct nhlt_vendor_dmic_array_config {
	struct nhlt_dmic_array_config dmic_config;
	u8 nb_mics;
	/* TODO add vendor mic config */
} __packed;

enum {
	NHLT_CONFIG_TYPE_GENERIC = 0,
	NHLT_CONFIG_TYPE_MIC_ARRAY = 1
};

enum {
	NHLT_MIC_ARRAY_2CH_SMALL = 0xa,
	NHLT_MIC_ARRAY_2CH_BIG = 0xb,
	NHLT_MIC_ARRAY_4CH_1ST_GEOM = 0xc,
	NHLT_MIC_ARRAY_4CH_L_SHAPED = 0xd,
	NHLT_MIC_ARRAY_4CH_2ND_GEOM = 0xe,
	NHLT_MIC_ARRAY_VENDOR_DEFINED = 0xf,
};

struct nhlt_acpi_table *intel_nhlt_init(struct device *dev);

void intel_nhlt_free(struct nhlt_acpi_table *addr);

int intel_nhlt_get_dmic_geo(struct device *dev, struct nhlt_acpi_table *nhlt);

bool intel_nhlt_has_endpoint_type(struct nhlt_acpi_table *nhlt, u8 link_type);
struct nhlt_specific_cfg *
intel_nhlt_get_endpoint_blob(struct device *dev, struct nhlt_acpi_table *nhlt,
			     u32 bus_id, u8 link_type, u8 vbps, u8 bps,
			     u8 num_ch, u32 rate, u8 dir, u8 dev_type);

#else

struct nhlt_acpi_table;

static inline struct nhlt_acpi_table *intel_nhlt_init(struct device *dev)
{
	return NULL;
}

static inline void intel_nhlt_free(struct nhlt_acpi_table *addr)
{
}

static inline int intel_nhlt_get_dmic_geo(struct device *dev,
					  struct nhlt_acpi_table *nhlt)
{
	return 0;
}

static inline bool intel_nhlt_has_endpoint_type(struct nhlt_acpi_table *nhlt,
						u8 link_type)
{
	return false;
}

static inline struct nhlt_specific_cfg *
intel_nhlt_get_endpoint_blob(struct device *dev, struct nhlt_acpi_table *nhlt,
			     u32 bus_id, u8 link_type, u8 vbps, u8 bps,
			     u8 num_ch, u32 rate, u8 dir, u8 dev_type)
{
	return NULL;
}

#endif

#endif
