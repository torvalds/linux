/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  skl-nhlt.h - Intel HDA Platform NHLT header
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Sanjiv Kumar <sanjiv.kumar@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#ifndef __SKL_NHLT_H__
#define __SKL_NHLT_H__

#include <linux/acpi.h>

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

enum nhlt_link_type {
	NHLT_LINK_HDA = 0,
	NHLT_LINK_DSP = 1,
	NHLT_LINK_DMIC = 2,
	NHLT_LINK_SSP = 3,
	NHLT_LINK_INVALID
};

enum nhlt_device_type {
	NHLT_DEVICE_BT = 0,
	NHLT_DEVICE_DMIC = 1,
	NHLT_DEVICE_I2S = 4,
	NHLT_DEVICE_INVALID
};

struct nhlt_specific_cfg {
	u32 size;
	u8 caps[0];
} __packed;

struct nhlt_fmt_cfg {
	struct wav_fmt_ext fmt_ext;
	struct nhlt_specific_cfg config;
} __packed;

struct nhlt_fmt {
	u8 fmt_count;
	struct nhlt_fmt_cfg fmt_config[0];
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
	struct nhlt_endpoint desc[0];
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

struct nhlt_tdm_config {
	u8 virtual_slot;
	u8 config_type;
} __packed;

struct nhlt_dmic_array_config {
	struct nhlt_tdm_config tdm_config;
	u8 array_type;
} __packed;

enum {
	NHLT_MIC_ARRAY_2CH_SMALL = 0xa,
	NHLT_MIC_ARRAY_2CH_BIG = 0xb,
	NHLT_MIC_ARRAY_4CH_1ST_GEOM = 0xc,
	NHLT_MIC_ARRAY_4CH_L_SHAPED = 0xd,
	NHLT_MIC_ARRAY_4CH_2ND_GEOM = 0xe,
	NHLT_MIC_ARRAY_VENDOR_DEFINED = 0xf,
};

#endif
