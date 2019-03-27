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

#ifndef R92C_ROM_DEFS_H
#define R92C_ROM_DEFS_H

#define R92C_MAX_CHAINS			2
#define R92C_GROUP_2G			3

#define R92C_EFUSE_MAX_LEN		512
#define R92C_EFUSE_MAP_LEN		128

/*
 * Some generic rom parsing macros.
 */
#define RTWN_GET_ROM_VAR(var, def)	(((var) != 0xff) ? (var) : (def))
#define RTWN_SIGN4TO8(val)		(((val) & 0x08) ? (val) | 0xf0 : (val))

#define LOW_PART_M	0x0f
#define LOW_PART_S	0
#define HIGH_PART_M	0xf0
#define HIGH_PART_S	4

/* Bits for rf_board_opt (rf_opt1) field. */
#define R92C_ROM_RF1_REGULATORY_M	0x07
#define R92C_ROM_RF1_REGULATORY_S	0
#define R92C_ROM_RF1_BOARD_TYPE_M	0xe0
#define R92C_ROM_RF1_BOARD_TYPE_S	5

/* Generic board types. */
#define R92C_BOARD_TYPE_DONGLE		0
#define R92C_BOARD_TYPE_HIGHPA		1
#define R92C_BOARD_TYPE_MINICARD	2
#define R92C_BOARD_TYPE_SOLO		3
#define R92C_BOARD_TYPE_COMBO		4

/* Bits for channel_plan field. */
#define R92C_CHANNEL_PLAN_BY_HW		0x80

#endif	/* R92C_ROM_DEFS_H */
