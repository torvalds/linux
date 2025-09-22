/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_GSC_BINARY_HEADERS_H_
#define _INTEL_GSC_BINARY_HEADERS_H_

#include <linux/types.h>

struct intel_gsc_version {
	u16 major;
	u16 minor;
	u16 hotfix;
	u16 build;
} __packed;

struct intel_gsc_partition {
	u32 offset;
	u32 size;
} __packed;

struct intel_gsc_layout_pointers {
	u8 rom_bypass_vector[16];

	/* size of pointers layout not including ROM bypass vector */
	u16 size;

	/*
	 * bit0: Backup copy of layout pointers exist
	 * bits1-15: reserved
	 */
	u8 flags;

	u8 reserved;

	u32 crc32;

	struct intel_gsc_partition datap;
	struct intel_gsc_partition boot1;
	struct intel_gsc_partition boot2;
	struct intel_gsc_partition boot3;
	struct intel_gsc_partition boot4;
	struct intel_gsc_partition boot5;
	struct intel_gsc_partition temp_pages;
} __packed;

/* Boot partition structures */
struct intel_gsc_bpdt_header {
	u32 signature;
#define INTEL_GSC_BPDT_HEADER_SIGNATURE 0x000055AA

	u16 descriptor_count; /* num of entries after the header */

	u8 version;
	u8 configuration;

	u32 crc32;

	u32 build_version;
	struct intel_gsc_version tool_version;
} __packed;

struct intel_gsc_bpdt_entry {
	/*
	 * Bits 0-15: BPDT entry type
	 * Bits 16-17: reserved
	 * Bit 18: code sub-partition
	 * Bits 19-31: reserved
	 */
	u32 type;
#define INTEL_GSC_BPDT_ENTRY_TYPE_MASK GENMASK(15, 0)
#define INTEL_GSC_BPDT_ENTRY_TYPE_GSC_RBE 0x1

	u32 sub_partition_offset; /* from the base of the BPDT header */
	u32 sub_partition_size;
} __packed;

/* Code partition directory (CPD) structures */
struct intel_gsc_cpd_header_v2 {
	u32 header_marker;
#define INTEL_GSC_CPD_HEADER_MARKER 0x44504324

	u32 num_of_entries;
	u8 header_version;
	u8 entry_version;
	u8 header_length; /* in bytes */
	u8 flags;
	u32 partition_name;
	u32 crc32;
} __packed;

struct intel_gsc_cpd_entry {
	u8 name[12];

	/*
	 * Bits 0-24: offset from the beginning of the code partition
	 * Bit 25: huffman compressed
	 * Bits 26-31: reserved
	 */
	u32 offset;
#define INTEL_GSC_CPD_ENTRY_OFFSET_MASK GENMASK(24, 0)
#define INTEL_GSC_CPD_ENTRY_HUFFMAN_COMP BIT(25)

	/*
	 * Module/Item length, in bytes. For Huffman-compressed modules, this
	 * refers to the uncompressed size. For software-compressed modules,
	 * this refers to the compressed size.
	 */
	u32 length;

	u8 reserved[4];
} __packed;

struct intel_gsc_manifest_header {
	u32 header_type; /* 0x4 for manifest type */
	u32 header_length; /* in dwords */
	u32 header_version;
	u32 flags;
	u32 vendor;
	u32 date;
	u32 size; /* In dwords, size of entire manifest (header + extensions) */
	u32 header_id;
	u32 internal_data;
	struct intel_gsc_version fw_version;
	u32 security_version;
	struct intel_gsc_version meu_kit_version;
	u32 meu_manifest_version;
	u8 general_data[4];
	u8 reserved3[56];
	u32 modulus_size; /* in dwords */
	u32 exponent_size; /* in dwords */
} __packed;

#endif
