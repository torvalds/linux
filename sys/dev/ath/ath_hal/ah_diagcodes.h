/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
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
#ifndef _ATH_AH_DIAGCODES_H_
#define _ATH_AH_DIAGCODES_H_
/*
 * Atheros Device Hardware Access Layer (HAL).
 *
 * Internal diagnostic API definitions.
 */

/*
 * Diagnostic interface.  This is an open-ended interface that
 * is opaque to applications.  Diagnostic programs use this to
 * retrieve internal data structures, etc.  There is no guarantee
 * that calling conventions for calls other than HAL_DIAG_REVS
 * are stable between HAL releases; a diagnostic application must
 * use the HAL revision information to deal with ABI/API differences.
 *
 * NB: do not renumber these, certain codes are publicly used.
 */
enum {
	HAL_DIAG_REVS		= 0,	/* MAC/PHY/Radio revs */
	HAL_DIAG_EEPROM		= 1,	/* EEPROM contents */
	HAL_DIAG_EEPROM_EXP_11A	= 2,	/* EEPROM 5112 power exp for 11a */
	HAL_DIAG_EEPROM_EXP_11B	= 3,	/* EEPROM 5112 power exp for 11b */
	HAL_DIAG_EEPROM_EXP_11G	= 4,	/* EEPROM 5112 power exp for 11g */
	HAL_DIAG_ANI_CURRENT	= 5,	/* ANI current channel state */
	HAL_DIAG_ANI_OFDM	= 6,	/* ANI OFDM timing error stats */
	HAL_DIAG_ANI_CCK	= 7,	/* ANI CCK timing error stats */
	HAL_DIAG_ANI_STATS	= 8,	/* ANI statistics */
	HAL_DIAG_RFGAIN		= 9,	/* RfGain GAIN_VALUES */
	HAL_DIAG_RFGAIN_CURSTEP	= 10,	/* RfGain GAIN_OPTIMIZATION_STEP */
	HAL_DIAG_PCDAC		= 11,	/* PCDAC table */
	HAL_DIAG_TXRATES	= 12,	/* Transmit rate table */
	HAL_DIAG_REGS		= 13,	/* Registers */
	HAL_DIAG_ANI_CMD	= 14,	/* ANI issue command (XXX do not change!) */
	HAL_DIAG_SETKEY		= 15,	/* Set keycache backdoor */
	HAL_DIAG_RESETKEY	= 16,	/* Reset keycache backdoor */
	HAL_DIAG_EEREAD		= 17,	/* Read EEPROM word */
	HAL_DIAG_EEWRITE	= 18,	/* Write EEPROM word */
	/* 19-26 removed, do not reuse */
	HAL_DIAG_RDWRITE	= 27,	/* Write regulatory domain */
	HAL_DIAG_RDREAD		= 28,	/* Get regulatory domain */
	HAL_DIAG_FATALERR	= 29,	/* Read cached interrupt state */
	HAL_DIAG_11NCOMPAT	= 30,	/* 11n compatibility tweaks */
	HAL_DIAG_ANI_PARAMS	= 31,	/* ANI noise immunity parameters */
	HAL_DIAG_CHECK_HANGS	= 32,	/* check h/w hangs */
	HAL_DIAG_SETREGS	= 33,	/* write registers */
	HAL_DIAG_CHANSURVEY	= 34,	/* channel survey */
	HAL_DIAG_PRINT_REG	= 35,
	HAL_DIAG_PRINT_REG_ALL	= 36,
	HAL_DIAG_CHANNELS	= 37,
	HAL_DIAG_PRINT_REG_COUNTER	= 38,
};

#endif /* _ATH_AH_DIAGCODES_H_ */
