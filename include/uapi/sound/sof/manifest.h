/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_UAPI_SOUND_SOF_USER_MANIFEST_H__
#define __INCLUDE_UAPI_SOUND_SOF_USER_MANIFEST_H__

/* start offset for base FW module */
#define SOF_MAN_ELF_TEXT_OFFSET		0x2000

/* FW Extended Manifest Header id = $AE1 */
#define SOF_MAN_EXT_HEADER_MAGIC	0x31454124

/* module type load type */
#define SOF_MAN_MOD_TYPE_BUILTIN	0
#define SOF_MAN_MOD_TYPE_MODULE		1

struct sof_man_module_type {
	uint32_t load_type:4;	/* SOF_MAN_MOD_TYPE_ */
	uint32_t auto_start:1;
	uint32_t domain_ll:1;
	uint32_t domain_dp:1;
	uint32_t rsvd_:25;
};

/* segment flags.type */
#define SOF_MAN_SEGMENT_TEXT		0
#define SOF_MAN_SEGMENT_RODATA		1
#define SOF_MAN_SEGMENT_DATA		1
#define SOF_MAN_SEGMENT_BSS		2
#define SOF_MAN_SEGMENT_EMPTY		15

union sof_man_segment_flags {
	uint32_t ul;
	struct {
		uint32_t contents:1;
		uint32_t alloc:1;
		uint32_t load:1;
		uint32_t readonly:1;
		uint32_t code:1;
		uint32_t data:1;
		uint32_t _rsvd0:2;
		uint32_t type:4;	/* MAN_SEGMENT_ */
		uint32_t _rsvd1:4;
		uint32_t length:16;	/* of segment in pages */
	} r;
} __packed;

/*
 * Module segment descriptor. Used by ROM - Immutable.
 */
struct sof_man_segment_desc {
	union sof_man_segment_flags flags;
	uint32_t v_base_addr;
	uint32_t file_offset;
} __packed;

/*
 * The firmware binary can be split into several modules.
 */

#define SOF_MAN_MOD_ID_LEN		4
#define SOF_MAN_MOD_NAME_LEN		8
#define SOF_MAN_MOD_SHA256_LEN		32
#define SOF_MAN_MOD_ID			{'$', 'A', 'M', 'E'}

/*
 * Each module has an entry in the FW header. Used by ROM - Immutable.
 */
struct sof_man_module {
	uint8_t struct_id[SOF_MAN_MOD_ID_LEN];	/* SOF_MAN_MOD_ID */
	uint8_t name[SOF_MAN_MOD_NAME_LEN];
	uint8_t uuid[16];
	struct sof_man_module_type type;
	uint8_t hash[SOF_MAN_MOD_SHA256_LEN];
	uint32_t entry_point;
	uint16_t cfg_offset;
	uint16_t cfg_count;
	uint32_t affinity_mask;
	uint16_t instance_max_count;	/* max number of instances */
	uint16_t instance_bss_size;	/* instance (pages) */
	struct sof_man_segment_desc segment[3];
} __packed;

/*
 * Each module has a configuration in the FW header. Used by ROM - Immutable.
 */
struct sof_man_mod_config {
	uint32_t par[4];	/* module parameters */
	uint32_t is_pages;	/* actual size of instance .bss (pages) */
	uint32_t cps;		/* cycles per second */
	uint32_t ibs;		/* input buffer size (bytes) */
	uint32_t obs;		/* output buffer size (bytes) */
	uint32_t module_flags;	/* flags, reserved for future use */
	uint32_t cpc;		/* cycles per single run */
	uint32_t obls;		/* output block size, reserved for future use */
} __packed;

/*
 * FW Manifest Header
 */

#define SOF_MAN_FW_HDR_FW_NAME_LEN	8
#define SOF_MAN_FW_HDR_ID		{'$', 'A', 'M', '1'}
#define SOF_MAN_FW_HDR_NAME		"ADSPFW"
#define SOF_MAN_FW_HDR_FLAGS		0x0
#define SOF_MAN_FW_HDR_FEATURES		0xff

/*
 * The firmware has a standard header that is checked by the ROM on firmware
 * loading. preload_page_count is used by DMA code loader and is entire
 * image size on CNL. i.e. CNL: total size of the binary’s .text and .rodata
 * Used by ROM - Immutable.
 */
struct sof_man_fw_header {
	uint8_t header_id[4];
	uint32_t header_len;
	uint8_t name[SOF_MAN_FW_HDR_FW_NAME_LEN];
	/* number of pages of preloaded image loaded by driver */
	uint32_t preload_page_count;
	uint32_t fw_image_flags;
	uint32_t feature_mask;
	uint16_t major_version;
	uint16_t minor_version;
	uint16_t hotfix_version;
	uint16_t build_version;
	uint32_t num_module_entries;
	uint32_t hw_buf_base_addr;
	uint32_t hw_buf_length;
	/* target address for binary loading as offset in IMR - must be == base offset */
	uint32_t load_offset;
} __packed;

/*
 * Firmware manifest descriptor. This can contain N modules and N module
 * configs. Used by ROM - Immutable.
 */
struct sof_man_fw_desc {
	struct sof_man_fw_header header;

	/* Warning - hack for module arrays. For some unknown reason the we
	 * have a variable size array of struct man_module followed by a
	 * variable size array of struct mod_config. These should have been
	 * merged into a variable array of a parent structure. We have to hack
	 * around this in many places....
	 *
	 * struct sof_man_module man_module[];
	 * struct sof_man_mod_config mod_config[];
	 */

} __packed;

/*
 * Component Descriptor. Used by ROM - Immutable.
 */
struct sof_man_component_desc {
	uint32_t reserved[2];	/* all 0 */
	uint32_t version;
	uint8_t hash[SOF_MAN_MOD_SHA256_LEN];
	uint32_t base_offset;
	uint32_t limit_offset;
	uint32_t attributes[4];
} __packed;

/*
 * Audio DSP extended metadata. Used by ROM - Immutable.
 */
struct sof_man_adsp_meta_file_ext {
	uint32_t ext_type;	/* always 17 for ADSP extension */
	uint32_t ext_len;
	uint32_t imr_type;
	uint8_t reserved[16];	/* all 0 */
	struct sof_man_component_desc comp_desc[1];
} __packed;

/*
 * Module Manifest for rimage module metadata. Not used by ROM.
 */
struct sof_man_module_manifest {
	struct sof_man_module module;
	uint32_t text_size;
} __packed;

#endif
