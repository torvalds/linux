/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#ifndef _ATH_AH_DEBUG_H_
#define _ATH_AH_DEBUG_H_
/*
 * Atheros Device Hardware Access Layer (HAL).
 *
 * Debug mask definitions.
 */
enum {
	HAL_DEBUG_REGDOMAIN	= 0x00000001,	/* regulatory handling */
	HAL_DEBUG_ATTACH	= 0x00000002,	/* work done in attach */
	HAL_DEBUG_RESET		= 0x00000004,	/* reset work */
	HAL_DEBUG_NFCAL		= 0x00000008,	/* noise floor calibration */
	HAL_DEBUG_PERCAL	= 0x00000010,	/* periodic calibration */
	HAL_DEBUG_ANI		= 0x00000020,	/* ANI operation */
	HAL_DEBUG_PHYIO		= 0x00000040,	/* phy i/o operations */
	HAL_DEBUG_REGIO		= 0x00000080,	/* register i/o operations */
	HAL_DEBUG_RFPARAM	= 0x00000100,
	HAL_DEBUG_TXQUEUE	= 0x00000200,	/* tx queue handling */
	HAL_DEBUG_TX		= 0x00000400,
	HAL_DEBUG_TXDESC	= 0x00000800,
	HAL_DEBUG_RX		= 0x00001000,
	HAL_DEBUG_RXDESC	= 0x00002000,
	HAL_DEBUG_KEYCACHE	= 0x00004000,	/* keycache handling */
	HAL_DEBUG_EEPROM	= 0x00008000,
	HAL_DEBUG_BEACON	= 0x00010000,	/* beacon setup work */
	HAL_DEBUG_POWER		= 0x00020000,	/* power management */
	HAL_DEBUG_GPIO		= 0x00040000,	/* GPIO debugging */
	HAL_DEBUG_INTERRUPT	= 0x00080000,	/* interrupt handling */
	HAL_DEBUG_DIVERSITY	= 0x00100000,	/* diversity debugging */
	HAL_DEBUG_DFS		= 0x00200000,	/* DFS debugging */
	HAL_DEBUG_HANG		= 0x00400000,	/* BB/MAC hang debugging */
	HAL_DEBUG_CALIBRATE	= 0x00800000,	/* setup calibration */
	HAL_DEBUG_POWER_MGMT	= 0x01000000,	/* power calibration */
	HAL_DEBUG_CHANNEL	= 0x02000000,
	HAL_DEBUG_QUEUE		= 0x04000000,
	HAL_DEBUG_PRINT_REG	= 0x08000000,
	HAL_DEBUG_FCS_RTT	= 0x10000000,
	HAL_DEBUG_BT_COEX	= 0x20000000,
	HAL_DEBUG_SPECTRAL	= 0x40000000,

	HAL_DEBUG_UNMASKABLE	= 0x80000000,	/* always printed */
	HAL_DEBUG_ANY		= 0xffffffff
};
#endif /* _ATH_AH_DEBUG_H_ */
