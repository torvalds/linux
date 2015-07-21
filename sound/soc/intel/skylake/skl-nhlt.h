/*
 *  skl-nhlt.h - Intel HDA Platform NHLT header
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
#ifndef __SKL_NHLT_H__
#define __SKL_NHLT_H__

struct acpi_desc_header {
	u32  signature;
	u32  length;
	u8   revision;
	u8   checksum;
	u8   oem_id[6];
	u64  oem_table_id;
	u32  oem_revision;
	u32  creator_id;
	u32  creator_revision;
} __packed;

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
	struct acpi_desc_header header;
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

#endif
