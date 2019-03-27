/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef R92C_ROM_IMAGE_H
#define R92C_ROM_IMAGE_H

#include <dev/rtwn/rtl8192c/r92c_rom_defs.h>

/*
 * RTL8192CU ROM image.
 */
struct r92c_rom {
	uint16_t	id;		/* 0x8192 */
	uint8_t		reserved1[5];
	uint8_t		dbg_sel;
	uint16_t	reserved2;
	uint16_t	vid;
	uint16_t	pid;
	uint8_t		usb_opt;
	uint8_t		ep_setting;
	uint16_t	reserved3;
	uint8_t		usb_phy;
	uint8_t		reserved4[3];
	uint8_t		macaddr[IEEE80211_ADDR_LEN];
	uint8_t		string[61];	/* "Realtek" */
	uint8_t		subcustomer_id;
	uint8_t		cck_tx_pwr[R92C_MAX_CHAINS][R92C_GROUP_2G];
	uint8_t		ht40_1s_tx_pwr[R92C_MAX_CHAINS][R92C_GROUP_2G];
	uint8_t		ht40_2s_tx_pwr_diff[R92C_GROUP_2G];
	uint8_t		ht20_tx_pwr_diff[R92C_GROUP_2G];
	uint8_t		ofdm_tx_pwr_diff[R92C_GROUP_2G];
	uint8_t		ht40_max_pwr[R92C_GROUP_2G];
	uint8_t		ht20_max_pwr[R92C_GROUP_2G];
	uint8_t		channel_plan;
	uint8_t		tssi[R92C_MAX_CHAINS];
	uint8_t		thermal_meter;
#define R92C_ROM_THERMAL_METER_M	0x1f
#define R92C_ROM_THERMAL_METER_S	0

	uint8_t		rf_opt1;
	uint8_t		rf_opt2;
	uint8_t		rf_opt3;
	uint8_t		rf_opt4;
	uint8_t		reserved5;
	uint8_t		version;
	uint8_t		customer_id;
} __packed;

_Static_assert(sizeof(struct r92c_rom) == R92C_EFUSE_MAP_LEN,
    "R92C_EFUSE_MAP_LEN must be equal to sizeof(struct r92c_rom)!");

#endif	/* R92C_ROM_IMAGE_H */
