/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef R92E_ROM_IMAGE_H
#define R92E_ROM_IMAGE_H

#include <dev/rtwn/rtl8192e/r92e_rom_defs.h>

#define	R92E_DEF_TX_PWR_2G		0x2d
#define	R92E_DEF_TX_PWR_HT20_DIFF	0x02
#define	R92E_DEF_TX_PWR_DIFF		0xfe

struct r92e_tx_pwr_2g {
	uint8_t		cck[R92E_GROUP_2G];
	uint8_t		ht40[R92E_GROUP_2G - 1];
} __packed;

struct r92e_tx_pwr_diff_2g {
	uint8_t		ht20_ofdm;
	struct {
		uint8_t	ht40_ht20;
		uint8_t	ofdm_cck;
	} __packed	diff123[R92E_MAX_TX_COUNT - 1];
} __packed;

struct r92e_tx_pwr {
	struct r92e_tx_pwr_2g		pwr_2g;
	struct r92e_tx_pwr_diff_2g	pwr_diff_2g;
	uint8_t				reserved[24];
} __packed;

/*
 * RTL8192EU ROM image.
 */
struct r92e_rom {
	uint8_t			reserved1[16];
	struct r92e_tx_pwr	tx_pwr[R92E_MAX_RF_PATH];
	uint8_t			channel_plan;
	uint8_t			crystalcap;
#define R92E_ROM_CRYSTALCAP_DEF		0x20

	uint8_t			thermal_meter;
	uint8_t			iqk_lck;
	uint8_t			pa_type;
	uint8_t			lna_type_2g;
	uint8_t			reserved2;
	uint8_t			lna_type_5g;
	uint8_t			reserved3;
	uint8_t			rf_board_opt;
	uint8_t			rf_feature_opt;
	uint8_t			rf_bt_opt;
	uint8_t			version;
	uint8_t			customer_id;
	uint8_t			tx_bbswing_2g;
	uint8_t			tx_bbswing_5g;
	uint8_t			tx_pwr_calib_rate;
	uint8_t			rf_ant_opt;
	uint8_t			rfe_option;
	uint8_t			reserved4[5];
	uint16_t		vid;
	uint16_t		pid;
	uint8_t			reserved5[3];
	uint8_t			macaddr[IEEE80211_ADDR_LEN];
	uint8_t			reserved6[2];
	uint8_t			string[7];	/* "Realtek" */
	uint8_t			reserved7[282];
} __packed;

_Static_assert(sizeof(struct r92e_rom) == R92E_EFUSE_MAP_LEN,
    "R92E_EFUSE_MAP_LEN must be equal to sizeof(struct r92e_rom)!");

#endif	/* R92E_ROM_IMAGE_H */
