/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org>
 *			 Steven J. Hill <sjhill@realitydiluted.com>
 *			 Thomas Gleixner <tglx@linutronix.de>
 *
 * Contains all JEDEC related definitions
 */

#ifndef __LINUX_MTD_JEDEC_H
#define __LINUX_MTD_JEDEC_H

struct jedec_ecc_info {
	u8 ecc_bits;
	u8 codeword_size;
	__le16 bb_per_lun;
	__le16 block_endurance;
	u8 reserved[2];
} __packed;

/* JEDEC features */
#define JEDEC_FEATURE_16_BIT_BUS	(1 << 0)

struct nand_jedec_params {
	/* rev info and features block */
	/* 'J' 'E' 'S' 'D'  */
	u8 sig[4];
	__le16 revision;
	__le16 features;
	u8 opt_cmd[3];
	__le16 sec_cmd;
	u8 num_of_param_pages;
	u8 reserved0[18];

	/* manufacturer information block */
	char manufacturer[12];
	char model[20];
	u8 jedec_id[6];
	u8 reserved1[10];

	/* memory organization block */
	__le32 byte_per_page;
	__le16 spare_bytes_per_page;
	u8 reserved2[6];
	__le32 pages_per_block;
	__le32 blocks_per_lun;
	u8 lun_count;
	u8 addr_cycles;
	u8 bits_per_cell;
	u8 programs_per_page;
	u8 multi_plane_addr;
	u8 multi_plane_op_attr;
	u8 reserved3[38];

	/* electrical parameter block */
	__le16 async_sdr_speed_grade;
	__le16 toggle_ddr_speed_grade;
	__le16 sync_ddr_speed_grade;
	u8 async_sdr_features;
	u8 toggle_ddr_features;
	u8 sync_ddr_features;
	__le16 t_prog;
	__le16 t_bers;
	__le16 t_r;
	__le16 t_r_multi_plane;
	__le16 t_ccs;
	__le16 io_pin_capacitance_typ;
	__le16 input_pin_capacitance_typ;
	__le16 clk_pin_capacitance_typ;
	u8 driver_strength_support;
	__le16 t_adl;
	u8 reserved4[36];

	/* ECC and endurance block */
	u8 guaranteed_good_blocks;
	__le16 guaranteed_block_endurance;
	struct jedec_ecc_info ecc_info[4];
	u8 reserved5[29];

	/* reserved */
	u8 reserved6[148];

	/* vendor */
	__le16 vendor_rev_num;
	u8 reserved7[88];

	/* CRC for Parameter Page */
	__le16 crc;
} __packed;

#endif /* __LINUX_MTD_JEDEC_H */
