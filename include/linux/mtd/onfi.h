/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 *			 Steven J. Hill <sjhill@realitydiluted.com>
 *			 Thomas Gleixner <tglx@linutronix.de>
 *
 * Contains all ONFI related definitions
 */

#ifndef __LINUX_MTD_ONFI_H
#define __LINUX_MTD_ONFI_H

#include <linux/types.h>

/* ONFI version bits */
#define ONFI_VERSION_1_0		BIT(1)
#define ONFI_VERSION_2_0		BIT(2)
#define ONFI_VERSION_2_1		BIT(3)
#define ONFI_VERSION_2_2		BIT(4)
#define ONFI_VERSION_2_3		BIT(5)
#define ONFI_VERSION_3_0		BIT(6)
#define ONFI_VERSION_3_1		BIT(7)
#define ONFI_VERSION_3_2		BIT(8)
#define ONFI_VERSION_4_0		BIT(9)

/* ONFI features */
#define ONFI_FEATURE_16_BIT_BUS		(1 << 0)
#define ONFI_FEATURE_EXT_PARAM_PAGE	(1 << 7)

/* ONFI timing mode, used in both asynchronous and synchronous mode */
#define ONFI_TIMING_MODE_0		(1 << 0)
#define ONFI_TIMING_MODE_1		(1 << 1)
#define ONFI_TIMING_MODE_2		(1 << 2)
#define ONFI_TIMING_MODE_3		(1 << 3)
#define ONFI_TIMING_MODE_4		(1 << 4)
#define ONFI_TIMING_MODE_5		(1 << 5)
#define ONFI_TIMING_MODE_UNKNOWN	(1 << 6)

/* ONFI feature number/address */
#define ONFI_FEATURE_NUMBER		256
#define ONFI_FEATURE_ADDR_TIMING_MODE	0x1

/* Vendor-specific feature address (Micron) */
#define ONFI_FEATURE_ADDR_READ_RETRY	0x89
#define ONFI_FEATURE_ON_DIE_ECC		0x90
#define   ONFI_FEATURE_ON_DIE_ECC_EN	BIT(3)

/* ONFI subfeature parameters length */
#define ONFI_SUBFEATURE_PARAM_LEN	4

/* ONFI optional commands SET/GET FEATURES supported? */
#define ONFI_OPT_CMD_SET_GET_FEATURES	(1 << 2)

struct nand_onfi_params {
	/* rev info and features block */
	/* 'O' 'N' 'F' 'I'  */
	u8 sig[4];
	__le16 revision;
	__le16 features;
	__le16 opt_cmd;
	u8 reserved0[2];
	__le16 ext_param_page_length; /* since ONFI 2.1 */
	u8 num_of_param_pages;        /* since ONFI 2.1 */
	u8 reserved1[17];

	/* manufacturer information block */
	char manufacturer[12];
	char model[20];
	u8 jedec_id;
	__le16 date_code;
	u8 reserved2[13];

	/* memory organization block */
	__le32 byte_per_page;
	__le16 spare_bytes_per_page;
	__le32 data_bytes_per_ppage;
	__le16 spare_bytes_per_ppage;
	__le32 pages_per_block;
	__le32 blocks_per_lun;
	u8 lun_count;
	u8 addr_cycles;
	u8 bits_per_cell;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 guaranteed_good_blocks;
	__le16 guaranteed_block_endurance;
	u8 programs_per_page;
	u8 ppage_attr;
	u8 ecc_bits;
	u8 interleaved_bits;
	u8 interleaved_ops;
	u8 reserved3[13];

	/* electrical parameter block */
	u8 io_pin_capacitance_max;
	__le16 async_timing_mode;
	__le16 program_cache_timing_mode;
	__le16 t_prog;
	__le16 t_bers;
	__le16 t_r;
	__le16 t_ccs;
	__le16 src_sync_timing_mode;
	u8 src_ssync_features;
	__le16 clk_pin_capacitance_typ;
	__le16 io_pin_capacitance_typ;
	__le16 input_pin_capacitance_typ;
	u8 input_pin_capacitance_max;
	u8 driver_strength_support;
	__le16 t_int_r;
	__le16 t_adl;
	u8 reserved4[8];

	/* vendor */
	__le16 vendor_revision;
	u8 vendor[88];

	__le16 crc;
} __packed;

#define ONFI_CRC_BASE	0x4F4E

/* Extended ECC information Block Definition (since ONFI 2.1) */
struct onfi_ext_ecc_info {
	u8 ecc_bits;
	u8 codeword_size;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 reserved[2];
} __packed;

#define ONFI_SECTION_TYPE_0	0	/* Unused section. */
#define ONFI_SECTION_TYPE_1	1	/* for additional sections. */
#define ONFI_SECTION_TYPE_2	2	/* for ECC information. */
struct onfi_ext_section {
	u8 type;
	u8 length;
} __packed;

#define ONFI_EXT_SECTION_MAX 8

/* Extended Parameter Page Definition (since ONFI 2.1) */
struct onfi_ext_param_page {
	__le16 crc;
	u8 sig[4];             /* 'E' 'P' 'P' 'S' */
	u8 reserved0[10];
	struct onfi_ext_section sections[ONFI_EXT_SECTION_MAX];

	/*
	 * The actual size of the Extended Parameter Page is in
	 * @ext_param_page_length of nand_onfi_params{}.
	 * The following are the variable length sections.
	 * So we do not add any fields below. Please see the ONFI spec.
	 */
} __packed;

/**
 * struct onfi_params - ONFI specific parameters that will be reused
 * @version: ONFI version (BCD encoded), 0 if ONFI is not supported
 * @tPROG: Page program time
 * @tBERS: Block erase time
 * @tR: Page read time
 * @tCCS: Change column setup time
 * @async_timing_mode: Supported asynchronous timing mode
 * @vendor_revision: Vendor specific revision number
 * @vendor: Vendor specific data
 */
struct onfi_params {
	int version;
	u16 tPROG;
	u16 tBERS;
	u16 tR;
	u16 tCCS;
	u16 async_timing_mode;
	u16 vendor_revision;
	u8 vendor[88];
};

#endif /* __LINUX_MTD_ONFI_H */
