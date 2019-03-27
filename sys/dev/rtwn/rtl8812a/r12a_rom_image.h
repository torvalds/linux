/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef R12A_ROM_IMAGE_H
#define R12A_ROM_IMAGE_H

#include <dev/rtwn/rtl8812a/r12a_rom_defs.h>

#define R12A_DEF_TX_PWR_2G	0x2d
#define R12A_DEF_TX_PWR_5G	0xfe

struct r12a_tx_pwr_2g {
	uint8_t		cck[R12A_GROUP_2G];
	uint8_t		ht40[R12A_GROUP_2G - 1];
} __packed;

struct r12a_tx_pwr_diff_2g {
	uint8_t		ht20_ofdm;
	struct {
		uint8_t	ht40_ht20;
		uint8_t	ofdm_cck;
	} __packed	diff123[R12A_MAX_TX_COUNT - 1];
} __packed;

struct r12a_tx_pwr_5g {
	uint8_t		ht40[R12A_GROUP_5G];
} __packed;

struct r12a_tx_pwr_diff_5g {
	uint8_t		ht20_ofdm;
	uint8_t		ht40_ht20[R12A_MAX_TX_COUNT - 1];
	uint8_t		ofdm_ofdm[2];
	uint8_t		ht80_ht160[R12A_MAX_TX_COUNT];
} __packed;

struct r12a_tx_pwr {
	struct r12a_tx_pwr_2g		pwr_2g;
	struct r12a_tx_pwr_diff_2g	pwr_diff_2g;
	struct r12a_tx_pwr_5g		pwr_5g;
	struct r12a_tx_pwr_diff_5g	pwr_diff_5g;
} __packed;

/*
 * RTL8812AU/RTL8821AU ROM image.
 */
struct r12a_rom {
	uint8_t			reserved1[16];
	struct r12a_tx_pwr	tx_pwr[R12A_MAX_RF_PATH];
	uint8_t			channel_plan;
	uint8_t			crystalcap;
#define R12A_ROM_CRYSTALCAP_DEF		0x20

	uint8_t			thermal_meter;
	uint8_t			iqk_lck;
	uint8_t			pa_type;
#define R12A_ROM_IS_PA_EXT_2GHZ(pa_type)	(((pa_type) & 0x30) == 0x30)
#define R12A_ROM_IS_PA_EXT_5GHZ(pa_type)	(((pa_type) & 0x03) == 0x03)
#define R21A_ROM_IS_PA_EXT_2GHZ(pa_type)	(((pa_type) & 0x10) == 0x10)
#define R21A_ROM_IS_PA_EXT_5GHZ(pa_type)	(((pa_type) & 0x01) == 0x01)

	uint8_t			lna_type_2g;
#define R12A_ROM_IS_LNA_EXT(lna_type)		(((lna_type) & 0x88) == 0x88)
#define R21A_ROM_IS_LNA_EXT(lna_type)		(((lna_type) & 0x08) == 0x08)

#define R12A_GET_ROM_PA_TYPE(lna_type, chain)		\
	(((lna_type) >> ((chain) * 4 + 2)) & 0x01)
#define R12A_GET_ROM_LNA_TYPE(lna_type, chain)	\
	(((lna_type) >> ((chain) * 4)) & 0x03)

	uint8_t			reserved2;
	uint8_t			lna_type_5g;
	uint8_t			reserved3;
	uint8_t			rf_board_opt;
#define R12A_BOARD_TYPE_COMBO_MF	5

	uint8_t			rf_feature_opt;
	uint8_t			rf_bt_opt;
#define R12A_RF_BT_OPT_ANT_NUM		0x01

	uint8_t			version;
	uint8_t			customer_id;
	uint8_t			tx_bbswing_2g;
	uint8_t			tx_bbswing_5g;
	uint8_t			tx_pwr_calib_rate;
	uint8_t			rf_ant_opt;
	uint8_t			rfe_option;
	uint8_t			reserved4[5];
	uint16_t		vid_12a;
	uint16_t		pid_12a;
	uint8_t			reserved5[3];
	uint8_t			macaddr_12a[IEEE80211_ADDR_LEN];
	uint8_t			reserved6[2];
	uint8_t			string_12a[8];	/* "Realtek " */
	uint8_t			reserved7[25];
	uint16_t		vid_21a;
	uint16_t		pid_21a;
	uint8_t			reserved8[3];
	uint8_t			macaddr_21a[IEEE80211_ADDR_LEN];
	uint8_t			reserved9[2];
	uint8_t			string_21a[8];	/* "Realtek " */
	uint8_t			reserved10[2];
	uint8_t			string_ven[23];	/* XXX variable length? */
	uint8_t			reserved11[208];
} __packed;

_Static_assert(sizeof(struct r12a_rom) == R12A_EFUSE_MAP_LEN,
    "R12A_EFUSE_MAP_LEN must be equal to sizeof(struct r12a_rom)!");

#endif	/* R12A_ROM_IMAGE_H */
