/*-
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef R88E_ROM_IMAGE_H
#define R88E_ROM_IMAGE_H

#include <dev/rtwn/rtl8188e/r88e_rom_defs.h>

/*
 * RTL8188EU ROM image.
 */
struct r88e_rom {
	uint8_t		reserved1[16];
	uint8_t		cck_tx_pwr[R88E_GROUP_2G];
	uint8_t		ht40_tx_pwr[R88E_GROUP_2G - 1];
	uint8_t		tx_pwr_diff;
	uint8_t		reserved2[156];
	uint8_t		channel_plan;
	uint8_t		crystalcap;
#define R88E_ROM_CRYSTALCAP_DEF		0x20

	uint8_t		thermal_meter;
	uint8_t		reserved3[6];
	uint8_t		rf_board_opt;
	uint8_t		rf_feature_opt;
	uint8_t		rf_bt_opt;
	uint8_t		version;
	uint8_t		customer_id;
	uint8_t		reserved4[3];
	uint8_t		rf_ant_opt;
	uint8_t		reserved5[6];

	union {
		struct {
			uint16_t	vid;
			uint16_t	pid;
			uint8_t		usb_opt;
			uint8_t		reserved6[2];
			uint8_t		macaddr[IEEE80211_ADDR_LEN];
		} __packed usb;

		struct {
			uint8_t		macaddr[IEEE80211_ADDR_LEN];
			uint16_t	vid;
			uint16_t	pid;
			uint8_t		reserved6[3];
		} __packed pci;
	} __packed diff_d0;

	uint8_t		reserved7[2];
	uint8_t		string[33];	/* "realtek 802.11n NIC" */
	uint8_t		reserved8[256];
} __packed;

_Static_assert(sizeof(struct r88e_rom) == R88E_EFUSE_MAP_LEN,
    "R88E_EFUSE_MAP_LEN must be equal to sizeof(struct r88e_rom)!");

#endif	/* R88E_ROM_IMAGE_H */
