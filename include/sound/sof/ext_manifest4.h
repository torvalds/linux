/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

/*
 * Extended manifest is a place to store metadata about firmware, known during
 * compilation time - for example firmware version or used compiler.
 * Given information are read on host side before firmware startup.
 * This part of output binary is not signed.
 */

#ifndef __SOF_FIRMWARE_EXT_MANIFEST4_H__
#define __SOF_FIRMWARE_EXT_MANIFEST4_H__

#include <linux/uuid.h>

/* In ASCII  $AE1 */
#define SOF_EXT_MAN4_MAGIC_NUMBER	0x31454124

#define MAX_MODULE_NAME_LEN		8
#define MAX_FW_BINARY_NAME		8
#define DEFAULT_HASH_SHA256_LEN		32
#define SOF_MAN4_FW_HDR_OFFSET		0x2000
#define SOF_MAN4_FW_HDR_OFFSET_CAVS_1_5	0x284

/*********************************************************************
 *	extended manifest		(struct sof_ext_manifest4_hdr)
 *-------------------
 *	css_manifest hdr
 *-------------------
 *	offset reserved for future
 *-------------------
 *	fw_hdr				(struct sof_man4_fw_binary_header)
 *-------------------
 *	module_entry[0]			(struct sof_man4_module)
 *-------------------
 *	module_entry[1]
 *-------------------
 *	...
 *-------------------
 *	module_entry[n]
 *-------------------
 *	module_config[0]		(struct sof_man4_module_config)
 *-------------------
 *	module_config[1]
 *-------------------
 *	...
 *-------------------
 *	module_config[m]
 *-------------------
 *	FW content
 *-------------------
 *********************************************************************/

struct sof_ext_manifest4_hdr {
	uint32_t id;
	uint32_t len; /* length of extension manifest */
	uint16_t version_major; /* header version */
	uint16_t version_minor;
	uint32_t num_module_entries;
} __packed;

struct sof_man4_fw_binary_header {
	/* This part must be unchanged to be backward compatible with SPT-LP ROM */
	uint32_t id;
	uint32_t len; /* sizeof(sof_man4_fw_binary_header) in bytes */
	uint8_t name[MAX_FW_BINARY_NAME];
	uint32_t preload_page_count; /* number of pages of preloaded image */
	uint32_t fw_image_flags;
	uint32_t feature_mask;
	uint16_t major_version; /* Firmware version */
	uint16_t minor_version;
	uint16_t hotfix_version;
	uint16_t build_version;
	uint32_t num_module_entries;

	/* This part may change to contain any additional data for BaseFw that is skipped by ROM */
	uint32_t hw_buf_base_addr;
	uint32_t hw_buf_length;
	uint32_t load_offset; /* This value is used by ROM */
} __packed;

struct sof_man4_segment_desc {
	uint32_t flags;
	uint32_t v_base_addr;
	uint32_t file_offset;
} __packed;

struct sof_man4_module {
	uint32_t id;
	uint8_t name[MAX_MODULE_NAME_LEN];
	guid_t uuid;
	uint32_t type;
	uint8_t hash[DEFAULT_HASH_SHA256_LEN];
	uint32_t entry_point;
	uint16_t cfg_offset;
	uint16_t cfg_count;
	uint32_t affinity_mask;
	uint16_t instance_max_count;
	uint16_t instance_stack_size;
	struct sof_man4_segment_desc	segments[3];
} __packed;

struct sof_man4_module_config {
	uint32_t par[4];	/* module parameters */
	uint32_t is_bytes;	/* actual size of instance .bss (bytes) */
	uint32_t cps;		/* cycles per second */
	uint32_t ibs;		/* input buffer size (bytes) */
	uint32_t obs;		/* output buffer size (bytes) */
	uint32_t module_flags;	/* flags, reserved for future use */
	uint32_t cpc;		/* cycles per single run */
	uint32_t obls;		/* output block size, reserved for future use */
} __packed;

#endif /* __SOF_FIRMWARE_EXT_MANIFEST4_H__ */
