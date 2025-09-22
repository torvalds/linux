/*	$OpenBSD: ar5211var.h,v 1.11 2022/01/09 05:42:38 jsg Exp $	*/

/*
 * Copyright (c) 2004, 2005, 2006, 2007 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Specific definitions for the Atheros AR5001 Wireless LAN chipset
 * (AR5211/AR5311).
 */

#ifndef _AR5K_AR5211_VAR_H
#define _AR5K_AR5211_VAR_H

#include <dev/ic/ar5xxx.h>

/*
 * Define a "magic" code for the AR5211 (the HAL layer wants it)
 */

#define AR5K_AR5211_MAGIC		0x0000145b /* 5211 */
#define AR5K_AR5211_TX_NUM_QUEUES	10

#if BYTE_ORDER == BIG_ENDIAN
#define AR5K_AR5211_INIT_CFG	(					\
	AR5K_AR5211_CFG_SWTD | AR5K_AR5211_CFG_SWRD			\
)
#else
#define AR5K_AR5211_INIT_CFG	0x00000000
#endif

/*
 * Internal RX/TX descriptor structures
 * (rX: reserved fields possibly used by future versions of the ar5k chipset)
 */

struct ar5k_ar5211_rx_desc {
	/*
	 * RX control word 0
	 */
	u_int32_t	rx_control_0;

#define AR5K_AR5211_DESC_RX_CTL0			0x00000000

	/*
	 * RX control word 1
	 */
	u_int32_t	rx_control_1;

#define AR5K_AR5211_DESC_RX_CTL1_BUF_LEN		0x00000fff
#define AR5K_AR5211_DESC_RX_CTL1_INTREQ			0x00002000
} __packed;

struct ar5k_ar5211_rx_status {
	/*
	 * RX status word 0
	 */
	u_int32_t	rx_status_0;

#define AR5K_AR5211_DESC_RX_STATUS0_DATA_LEN		0x00000fff
#define AR5K_AR5211_DESC_RX_STATUS0_MORE		0x00001000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_RATE	0x00078000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_RATE_S	15
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_SIGNAL	0x07f80000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_SIGNAL_S	19
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_ANTENNA	0x38000000
#define AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_ANTENNA_S	27

	/*
	 * RX status word 1
	 */
	u_int32_t	rx_status_1;

#define AR5K_AR5211_DESC_RX_STATUS1_DONE		0x00000001
#define AR5K_AR5211_DESC_RX_STATUS1_FRAME_RECEIVE_OK	0x00000002
#define AR5K_AR5211_DESC_RX_STATUS1_CRC_ERROR		0x00000004
#define AR5K_AR5211_DESC_RX_STATUS1_FIFO_OVERRUN	0x00000008
#define AR5K_AR5211_DESC_RX_STATUS1_DECRYPT_CRC_ERROR	0x00000010
#define AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR		0x000000e0
#define AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR_S		5
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX_VALID	0x00000100
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX		0x00007e00
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX_S		9
#define AR5K_AR5211_DESC_RX_STATUS1_RECEIVE_TIMESTAMP	0x0fff8000
#define AR5K_AR5211_DESC_RX_STATUS1_RECEIVE_TIMESTAMP_S	15
#define AR5K_AR5211_DESC_RX_STATUS1_KEY_CACHE_MISS	0x10000000
} __packed;

#define AR5K_AR5211_DESC_RX_PHY_ERROR_NONE		0x00
#define AR5K_AR5211_DESC_RX_PHY_ERROR_TIMING		0x20
#define AR5K_AR5211_DESC_RX_PHY_ERROR_PARITY		0x40
#define AR5K_AR5211_DESC_RX_PHY_ERROR_RATE		0x60
#define AR5K_AR5211_DESC_RX_PHY_ERROR_LENGTH		0x80
#define AR5K_AR5211_DESC_RX_PHY_ERROR_64QAM		0xa0
#define AR5K_AR5211_DESC_RX_PHY_ERROR_SERVICE		0xc0
#define AR5K_AR5211_DESC_RX_PHY_ERROR_TRANSMITOVR	0xe0

struct ar5k_ar5211_tx_desc {
	/*
	 * TX control word 0
	 */
	u_int32_t	tx_control_0;

#define AR5K_AR5211_DESC_TX_CTL0_FRAME_LEN		0x00000fff
#define AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE		0x003c0000
#define AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE_S		18
#define AR5K_AR5211_DESC_TX_CTL0_RTSENA			0x00400000
#define AR5K_AR5211_DESC_TX_CTL0_VEOL			0x00800000
#define AR5K_AR5211_DESC_TX_CTL0_CLRDMASK		0x01000000
#define AR5K_AR5211_DESC_TX_CTL0_ANT_MODE_XMIT		0x1e000000
#define AR5K_AR5211_DESC_TX_CTL0_ANT_MODE_XMIT_S	25
#define AR5K_AR5211_DESC_TX_CTL0_INTREQ			0x20000000
#define AR5K_AR5211_DESC_TX_CTL0_ENCRYPT_KEY_VALID	0x40000000

	/*
	 * TX control word 1
	 */
	u_int32_t	tx_control_1;

#define AR5K_AR5211_DESC_TX_CTL1_BUF_LEN		0x00000fff
#define AR5K_AR5211_DESC_TX_CTL1_MORE			0x00001000
#define AR5K_AR5211_DESC_TX_CTL1_ENCRYPT_KEY_INDEX	0x000fe000
#define AR5K_AR5211_DESC_TX_CTL1_ENCRYPT_KEY_INDEX_S	13
#define AR5K_AR5211_DESC_TX_CTL1_FRAME_TYPE		0x00700000
#define AR5K_AR5211_DESC_TX_CTL1_FRAME_TYPE_S		20
#define AR5K_AR5211_DESC_TX_CTL1_NOACK			0x00800000
} __packed;

struct ar5k_ar5211_tx_status {
	/*
	 * TX status word 0
	 */
	u_int32_t	tx_status_0;

#define AR5K_AR5211_DESC_TX_STATUS0_FRAME_XMIT_OK	0x00000001
#define AR5K_AR5211_DESC_TX_STATUS0_EXCESSIVE_RETRIES	0x00000002
#define AR5K_AR5211_DESC_TX_STATUS0_FIFO_UNDERRUN	0x00000004
#define AR5K_AR5211_DESC_TX_STATUS0_FILTERED		0x00000008
#define AR5K_AR5211_DESC_TX_STATUS0_RTS_FAIL_COUNT	0x000000f0
#define AR5K_AR5211_DESC_TX_STATUS0_RTS_FAIL_COUNT_S	4
#define AR5K_AR5211_DESC_TX_STATUS0_DATA_FAIL_COUNT	0x00000f00
#define AR5K_AR5211_DESC_TX_STATUS0_DATA_FAIL_COUNT_S	8
#define AR5K_AR5211_DESC_TX_STATUS0_VIRT_COLL_COUNT	0x0000f000
#define AR5K_AR5211_DESC_TX_STATUS0_VIRT_COLL_COUNT_S	12
#define AR5K_AR5211_DESC_TX_STATUS0_SEND_TIMESTAMP	0xffff0000
#define AR5K_AR5211_DESC_TX_STATUS0_SEND_TIMESTAMP_S	16

	/*
	 * TX status word 1
	 */
	u_int32_t	tx_status_1;

#define AR5K_AR5211_DESC_TX_STATUS1_DONE		0x00000001
#define AR5K_AR5211_DESC_TX_STATUS1_SEQ_NUM		0x00001ffe
#define AR5K_AR5211_DESC_TX_STATUS1_SEQ_NUM_S		1
#define AR5K_AR5211_DESC_TX_STATUS1_ACK_SIG_STRENGTH	0x001fe000
#define AR5K_AR5211_DESC_TX_STATUS1_ACK_SIG_STRENGTH_S	13
} __packed;

/*
 * Public function prototypes
 */
extern ar5k_attach_t ar5k_ar5211_attach;

/*
 * Initial register values which have to be loaded into the
 * card at boot time and after each reset.
 */

#define AR5K_AR5211_INI {						\
	{ 0x000c, 0x00000000 },						\
	{ 0x0028, 0x84849c9c },						\
	{ 0x002c, 0x7c7c7c7c },						\
	{ 0x0034, 0x00000005 },						\
	{ 0x0040, 0x00000000 },						\
	{ 0x0044, 0x00000008 },						\
	{ 0x0048, 0x00000008 },						\
	{ 0x004c, 0x00000010 },						\
	{ 0x0050, 0x00000000 },						\
	{ 0x0054, 0x0000001f },						\
	{ 0x0800, 0x00000000 },						\
	{ 0x0804, 0x00000000 },						\
	{ 0x0808, 0x00000000 },						\
	{ 0x080c, 0x00000000 },						\
	{ 0x0810, 0x00000000 },						\
	{ 0x0814, 0x00000000 },						\
	{ 0x0818, 0x00000000 },						\
	{ 0x081c, 0x00000000 },						\
	{ 0x0820, 0x00000000 },						\
	{ 0x0824, 0x00000000 },						\
	{ 0x1230, 0x00000000 },						\
	{ 0x8004, 0x00000000 },						\
	{ 0x8008, 0x00000000 },						\
	{ 0x800c, 0x00000000 },						\
	{ 0x8018, 0x00000000 },						\
	{ 0x8024, 0x00000000 },						\
	{ 0x8028, 0x00000030 },						\
	{ 0x802c, 0x0007ffff },						\
	{ 0x8030, 0x01ffffff },						\
	{ 0x8034, 0x00000031 },						\
	{ 0x8038, 0x00000000 },						\
	{ 0x803c, 0x00000000 },						\
	{ 0x8040, 0x00000000 },						\
	{ 0x8044, 0x00000002 },						\
	{ 0x8048, 0x00000000 },						\
	{ 0x8054, 0x00000000 },						\
	{ 0x8058, 0x00000000 },						\
        /* PHY registers */						\
	{ 0x9808, 0x00000000 },						\
	{ 0x980c, 0x2d849093 },						\
	{ 0x9810, 0x7d32e000 },						\
	{ 0x9814, 0x00000f6b },						\
	{ 0x981c, 0x00000000 },						\
	{ 0x982c, 0x00026ffe },						\
	{ 0x9830, 0x00000000 },						\
	{ 0x983c, 0x00020100 },						\
	{ 0x9840, 0x206a017a },						\
	{ 0x984c, 0x1284613c },						\
	{ 0x9854, 0x00000859 },						\
	{ 0x9868, 0x409a4190 },						\
	{ 0x986c, 0x050cb081 },						\
	{ 0x9870, 0x0000000f },						\
	{ 0x9874, 0x00000080 },						\
	{ 0x9878, 0x0000000c },						\
	{ 0x9900, 0x00000000 },						\
	{ 0x9904, 0x00000000 },						\
	{ 0x9908, 0x00000000 },						\
	{ 0x990c, 0x00800000 },						\
	{ 0x9910, 0x00000001 },						\
	{ 0x991c, 0x0000092a },						\
	{ 0x9920, 0x00000000 },						\
	{ 0x9924, 0x00058a05 },						\
	{ 0x9928, 0x00000001 },						\
	{ 0x992c, 0x00000000 },						\
	{ 0x9930, 0x00000000 },						\
	{ 0x9934, 0x00000000 },						\
	{ 0x9938, 0x00000000 },						\
	{ 0x993c, 0x0000003f },						\
	{ 0x9940, 0x00000004 },						\
	{ 0x9948, 0x00000000 },						\
	{ 0x994c, 0x00000000 },						\
	{ 0x9950, 0x00000000 },						\
	{ 0x9954, 0x5d50f14c },						\
	{ 0x9958, 0x00000018 },						\
	{ 0x995c, 0x004b6a8e },						\
	{ 0xa184, 0x06ff05ff },						\
	{ 0xa188, 0x07ff07ff },						\
	{ 0xa18c, 0x08ff08ff },						\
	{ 0xa190, 0x09ff09ff },						\
	{ 0xa194, 0x0aff0aff },						\
	{ 0xa198, 0x0bff0bff },						\
	{ 0xa19c, 0x0cff0cff },						\
	{ 0xa1a0, 0x0dff0dff },						\
	{ 0xa1a4, 0x0fff0eff },						\
	{ 0xa1a8, 0x12ff12ff },						\
	{ 0xa1ac, 0x14ff13ff },						\
	{ 0xa1b0, 0x16ff15ff },						\
	{ 0xa1b4, 0x19ff17ff },						\
	{ 0xa1b8, 0x1bff1aff },						\
	{ 0xa1bc, 0x1eff1dff },						\
	{ 0xa1c0, 0x23ff20ff },						\
	{ 0xa1c4, 0x27ff25ff },						\
	{ 0xa1c8, 0x2cff29ff },						\
	{ 0xa1cc, 0x31ff2fff },						\
	{ 0xa1d0, 0x37ff34ff },						\
	{ 0xa1d4, 0x3aff3aff },						\
	{ 0xa1d8, 0x3aff3aff },						\
	{ 0xa1dc, 0x3aff3aff },						\
	{ 0xa1e0, 0x3aff3aff },						\
	{ 0xa1e4, 0x3aff3aff },						\
	{ 0xa1e8, 0x3aff3aff },						\
	{ 0xa1ec, 0x3aff3aff },						\
	{ 0xa1f0, 0x3aff3aff },						\
	{ 0xa1f4, 0x3aff3aff },						\
	{ 0xa1f8, 0x3aff3aff },						\
	{ 0xa1fc, 0x3aff3aff },						\
        /* BB gain table (64bytes) */					\
	{ 0x9b00, 0x00000000 },						\
	{ 0x9b04, 0x00000020 },						\
	{ 0x9b08, 0x00000010 },						\
	{ 0x9b0c, 0x00000030 },						\
	{ 0x9b10, 0x00000008 },						\
	{ 0x9b14, 0x00000028 },						\
	{ 0x9b18, 0x00000004 },						\
	{ 0x9b1c, 0x00000024 },						\
	{ 0x9b20, 0x00000014 },						\
	{ 0x9b24, 0x00000034 },						\
	{ 0x9b28, 0x0000000c },						\
	{ 0x9b2c, 0x0000002c },						\
	{ 0x9b30, 0x00000002 },						\
	{ 0x9b34, 0x00000022 },						\
	{ 0x9b38, 0x00000012 },						\
	{ 0x9b3c, 0x00000032 },						\
	{ 0x9b40, 0x0000000a },						\
	{ 0x9b44, 0x0000002a },						\
	{ 0x9b48, 0x00000006 },						\
	{ 0x9b4c, 0x00000026 },						\
	{ 0x9b50, 0x00000016 },						\
	{ 0x9b54, 0x00000036 },						\
	{ 0x9b58, 0x0000000e },						\
	{ 0x9b5c, 0x0000002e },						\
	{ 0x9b60, 0x00000001 },						\
	{ 0x9b64, 0x00000021 },						\
	{ 0x9b68, 0x00000011 },						\
	{ 0x9b6c, 0x00000031 },						\
	{ 0x9b70, 0x00000009 },						\
	{ 0x9b74, 0x00000029 },						\
	{ 0x9b78, 0x00000005 },						\
	{ 0x9b7c, 0x00000025 },						\
	{ 0x9b80, 0x00000015 },						\
	{ 0x9b84, 0x00000035 },						\
	{ 0x9b88, 0x0000000d },						\
	{ 0x9b8c, 0x0000002d },						\
	{ 0x9b90, 0x00000003 },						\
	{ 0x9b94, 0x00000023 },						\
	{ 0x9b98, 0x00000013 },						\
	{ 0x9b9c, 0x00000033 },						\
	{ 0x9ba0, 0x0000000b },						\
	{ 0x9ba4, 0x0000002b },						\
	{ 0x9ba8, 0x0000002b },						\
	{ 0x9bac, 0x0000002b },						\
	{ 0x9bb0, 0x0000002b },						\
	{ 0x9bb4, 0x0000002b },						\
	{ 0x9bb8, 0x0000002b },						\
	{ 0x9bbc, 0x0000002b },						\
	{ 0x9bc0, 0x0000002b },						\
	{ 0x9bc4, 0x0000002b },						\
	{ 0x9bc8, 0x0000002b },						\
	{ 0x9bcc, 0x0000002b },						\
	{ 0x9bd0, 0x0000002b },						\
	{ 0x9bd4, 0x0000002b },						\
	{ 0x9bd8, 0x0000002b },						\
	{ 0x9bdc, 0x0000002b },						\
	{ 0x9be0, 0x0000002b },						\
	{ 0x9be4, 0x0000002b },						\
	{ 0x9be8, 0x0000002b },						\
	{ 0x9bec, 0x0000002b },						\
	{ 0x9bf0, 0x0000002b },						\
	{ 0x9bf4, 0x0000002b },						\
	{ 0x9bf8, 0x00000002 },						\
	{ 0x9bfc, 0x00000016 },						\
        /* PHY activation */						\
	{ 0x98d4, 0x00000020 },						\
	{ 0x98d8, 0x00601068 },						\
}

struct ar5k_ar5211_ini_mode {
	u_int16_t	mode_register;
	u_int32_t	mode_value[4];
};

#define AR5K_AR5211_INI_MODE {						\
	{ 0x0030, { 0x00000017, 0x00000017, 0x00000017, 0x00000017 } },	\
	{ 0x1040, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1044, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1048, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x104c, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1050, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1054, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1058, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x105c, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1060, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1064, { 0x002ffc0f, 0x002ffc0f, 0x002ffc1f, 0x002ffc0f } },	\
	{ 0x1070, { 0x00000168, 0x000001e0, 0x000001b8, 0x00000168 } },	\
	{ 0x1030, { 0x00000230, 0x000001e0, 0x000000b0, 0x00000230 } },	\
	{ 0x10b0, { 0x00000d98, 0x00001180, 0x00001f48, 0x00000d98 } },	\
	{ 0x10f0, { 0x0000a0e0, 0x00014068, 0x00005880, 0x0000a0e0 } },	\
	{ 0x8014, { 0x04000400, 0x08000800, 0x20003000, 0x04000400 } },	\
	{ 0x801c, { 0x0e8d8fa7, 0x0e8d8fcf, 0x01608f95, 0x0e8d8fa7 } },	\
	{ 0x9804, { 0x00000000, 0x00000003, 0x00000000, 0x00000000 } },	\
	{ 0x9820, { 0x02020200, 0x02020200, 0x02010200, 0x02020200 } },	\
	{ 0x9824, { 0x00000e0e, 0x00000e0e, 0x00000707, 0x00000e0e } },	\
	{ 0x9828, { 0x0a020001, 0x0a020001, 0x05010000, 0x0a020001 } },	\
	{ 0x9834, { 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e } },	\
	{ 0x9838, { 0x00000007, 0x00000007, 0x0000000b, 0x0000000b } },	\
	{ 0x9844, { 0x1372169c, 0x137216a5, 0x137216a8, 0x1372169c } },	\
	{ 0x9848, { 0x0018ba67, 0x0018ba67, 0x0018ba69, 0x0018ba69 } },	\
	{ 0x9850, { 0x0c28b4e0, 0x0c28b4e0, 0x0c28b4e0, 0x0c28b4e0 } },	\
	{ 0x9858, { 0x7e800d2e, 0x7e800d2e, 0x7ec00d2e, 0x7e800d2e } },	\
	{ 0x985c, { 0x31375d5e, 0x31375d5e, 0x313a5d5e, 0x31375d5e } },	\
	{ 0x9860, { 0x0000bd10, 0x0000bd10, 0x0000bd38, 0x0000bd10 } },	\
	{ 0x9864, { 0x0001ce00, 0x0001ce00, 0x0001ce00, 0x0001ce00 } },	\
	{ 0x9914, { 0x00002710, 0x00002710, 0x0000157c, 0x00002710 } },	\
	{ 0x9918, { 0x00000190, 0x00000190, 0x00000084, 0x00000190 } },	\
	{ 0x9944, { 0x6fe01020, 0x6fe01020, 0x6fe00920, 0x6fe01020 } },	\
	{ 0xa180, { 0x05ff14ff, 0x05ff14ff, 0x05ff14ff, 0x05ff19ff } },	\
	{ 0x98d4, { 0x00000010, 0x00000014, 0x00000010, 0x00000010 } },	\
}

struct ar5k_ar5211_ini_rf {
	u_int16_t	rf_register;
	u_int32_t	rf_value[2];
};

#define AR5K_AR5211_INI_RF	{					\
	{ 0x0000a204, { 0x00000000, 0x00000000 } },			\
	{ 0x0000a208, { 0x503e4646, 0x503e4646 } },			\
	{ 0x0000a20c, { 0x6480416c, 0x6480416c } },			\
	{ 0x0000a210, { 0x0199a003, 0x0199a003 } },			\
	{ 0x0000a214, { 0x044cd610, 0x044cd610 } },			\
	{ 0x0000a218, { 0x13800040, 0x13800040 } },			\
	{ 0x0000a21c, { 0x1be00060, 0x1be00060 } },			\
	{ 0x0000a220, { 0x0c53800a, 0x0c53800a } },			\
	{ 0x0000a224, { 0x0014df3b, 0x0014df3b } },			\
	{ 0x0000a228, { 0x000001b5, 0x000001b5 } },			\
	{ 0x0000a22c, { 0x00000020, 0x00000020 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00380000, 0x00380000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x000400f9, 0x000400f9 } },			\
	{ 0x000098d4, { 0x00000000, 0x00000004 } },			\
									\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x10000000, 0x10000000 } },			\
	{ 0x0000989c, { 0x04000000, 0x04000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x00000000 } },			\
	{ 0x0000989c, { 0x00000000, 0x0a000000 } },			\
	{ 0x0000989c, { 0x00380080, 0x02380080 } },			\
	{ 0x0000989c, { 0x00020006, 0x00000006 } },			\
	{ 0x0000989c, { 0x00000092, 0x00000092 } },			\
	{ 0x0000989c, { 0x000000a0, 0x000000a0 } },			\
	{ 0x0000989c, { 0x00040007, 0x00040007 } },			\
	{ 0x000098d4, { 0x0000001a, 0x0000001a } },			\
	{ 0x0000989c, { 0x00000048, 0x00000048 } },			\
	{ 0x0000989c, { 0x00000010, 0x00000010 } },			\
	{ 0x0000989c, { 0x00000008, 0x00000008 } },			\
	{ 0x0000989c, { 0x0000000f, 0x0000000f } },			\
	{ 0x0000989c, { 0x000000f2, 0x00000062 } },			\
	{ 0x0000989c, { 0x0000904f, 0x0000904c } },			\
	{ 0x0000989c, { 0x0000125a, 0x0000129a } },			\
	{ 0x000098cc, { 0x0000000e, 0x0000000f } },			\
}

#endif /* _AR5K_AR5211_VAR_H */
