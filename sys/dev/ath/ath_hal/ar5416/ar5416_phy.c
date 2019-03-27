/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar5416/ar5416.h"

/* shorthands to compact tables for readability */
#define	OFDM	IEEE80211_T_OFDM
#define	CCK	IEEE80211_T_CCK
#define HT      IEEE80211_T_HT

HAL_RATE_TABLE ar5416_11ng_table = {
    28,  /* number of rates */
    { 0 },
    {
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   1 Mb */ {  AH_TRUE, CCK,     1000,    0x1b,    0x00, (0x80| 2),   0 },
/*   2 Mb */ {  AH_TRUE, CCK,     2000,    0x1a,    0x04, (0x80| 4),   1 },
/* 5.5 Mb */ {  AH_TRUE, CCK,     5500,    0x19,    0x04, (0x80|11),   2 },
/*  11 Mb */ {  AH_TRUE, CCK,    11000,    0x18,    0x04, (0x80|22),   3 },
/* Remove rates 6, 9 from rate ctrl */
/*   6 Mb */ { AH_FALSE, OFDM,    6000,    0x0b,    0x00,        12,   4 },
/*   9 Mb */ { AH_FALSE, OFDM,    9000,    0x0f,    0x00,        18,   4 },
/*  12 Mb */ {  AH_TRUE, OFDM,   12000,    0x0a,    0x00,        24,   6 },
/*  18 Mb */ {  AH_TRUE, OFDM,   18000,    0x0e,    0x00,        36,   6 },
/*  24 Mb */ {  AH_TRUE, OFDM,   24000,    0x09,    0x00,        48,   8 },
/*  36 Mb */ {  AH_TRUE, OFDM,   36000,    0x0d,    0x00,        72,   8 },
/*  48 Mb */ {  AH_TRUE, OFDM,   48000,    0x08,    0x00,        96,   8 },
/*  54 Mb */ {  AH_TRUE, OFDM,   54000,    0x0c,    0x00,       108,   8 },
/* 6.5 Mb */ {  AH_TRUE, HT,      6500,    0x80,    0x00,      	  0,   8 },
/*  13 Mb */ {  AH_TRUE, HT,  	 13000,    0x81,    0x00,      	  1,   8 },
/*19.5 Mb */ {  AH_TRUE, HT,     19500,    0x82,    0x00,         2,   8 },
/*  26 Mb */ {  AH_TRUE, HT,  	 26000,    0x83,    0x00,         3,   8 },
/*  39 Mb */ {  AH_TRUE, HT,  	 39000,    0x84,    0x00,         4,   8 },
/*  52 Mb */ {  AH_TRUE, HT,   	 52000,    0x85,    0x00,         5,   8 },
/*58.5 Mb */ {  AH_TRUE, HT,  	 58500,    0x86,    0x00,         6,   8 },
/*  65 Mb */ {  AH_TRUE, HT,  	 65000,    0x87,    0x00,         7,   8 },
/*  13 Mb */ {  AH_TRUE, HT,  	 13000,    0x88,    0x00,         8,   8 },
/*  26 Mb */ {  AH_TRUE, HT,  	 26000,    0x89,    0x00,         9,   8 },
/*  39 Mb */ {  AH_TRUE, HT,  	 39000,    0x8a,    0x00,        10,   8 },
/*  52 Mb */ {  AH_TRUE, HT,  	 52000,    0x8b,    0x00,        11,   8 },
/*  78 Mb */ {  AH_TRUE, HT,  	 78000,    0x8c,    0x00,        12,   8 },
/* 104 Mb */ {  AH_TRUE, HT, 	104000,    0x8d,    0x00,        13,   8 },
/* 117 Mb */ {  AH_TRUE, HT, 	117000,    0x8e,    0x00,        14,   8 },
/* 130 Mb */ {  AH_TRUE, HT, 	130000,    0x8f,    0x00,        15,   8 },
	},
};

static HAL_RATE_TABLE ar5416_11na_table = {
    24,  /* number of rates */
    { 0 },
    {
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   6 Mb */ {  AH_TRUE, OFDM,    6000,    0x0b,    0x00, (0x80|12),   0 },
/*   9 Mb */ {  AH_TRUE, OFDM,    9000,    0x0f,    0x00,        18,   0 },
/*  12 Mb */ {  AH_TRUE, OFDM,   12000,    0x0a,    0x00, (0x80|24),   2 },
/*  18 Mb */ {  AH_TRUE, OFDM,   18000,    0x0e,    0x00,        36,   2 },
/*  24 Mb */ {  AH_TRUE, OFDM,   24000,    0x09,    0x00, (0x80|48),   4 },
/*  36 Mb */ {  AH_TRUE, OFDM,   36000,    0x0d,    0x00,        72,   4 },
/*  48 Mb */ {  AH_TRUE, OFDM,   48000,    0x08,    0x00,        96,   4 },
/*  54 Mb */ {  AH_TRUE, OFDM,   54000,    0x0c,    0x00,       108,   4 },
/* 6.5 Mb */ {  AH_TRUE, HT,      6500,    0x80,    0x00,         0,   4 },
/*  13 Mb */ {  AH_TRUE, HT,  	 13000,    0x81,    0x00,         1,   4 },
/*19.5 Mb */ {  AH_TRUE, HT,     19500,    0x82,    0x00,         2,   4 },
/*  26 Mb */ {  AH_TRUE, HT,  	 26000,    0x83,    0x00,         3,   4 },
/*  39 Mb */ {  AH_TRUE, HT,  	 39000,    0x84,    0x00,         4,   4 },
/*  52 Mb */ {  AH_TRUE, HT,   	 52000,    0x85,    0x00,         5,   4 },
/*58.5 Mb */ {  AH_TRUE, HT,  	 58500,    0x86,    0x00,         6,   4 },
/*  65 Mb */ {  AH_TRUE, HT,  	 65000,    0x87,    0x00,         7,   4 },
/*  13 Mb */ {  AH_TRUE, HT,  	 13000,    0x88,    0x00,         8,   4 },
/*  26 Mb */ {  AH_TRUE, HT,  	 26000,    0x89,    0x00,         9,   4 },
/*  39 Mb */ {  AH_TRUE, HT,  	 39000,    0x8a,    0x00,        10,   4 },
/*  52 Mb */ {  AH_TRUE, HT,  	 52000,    0x8b,    0x00,        11,   4 },
/*  78 Mb */ {  AH_TRUE, HT,  	 78000,    0x8c,    0x00,        12,   4 },
/* 104 Mb */ {  AH_TRUE, HT, 	104000,    0x8d,    0x00,        13,   4 },
/* 117 Mb */ {  AH_TRUE, HT, 	117000,    0x8e,    0x00,        14,   4 },
/* 130 Mb */ {  AH_TRUE, HT, 	130000,    0x8f,    0x00,        15,   4 },
	},
};

#undef	OFDM
#undef	CCK
#undef	HT

const HAL_RATE_TABLE *
ar5416GetRateTable(struct ath_hal *ah, u_int mode)
{
	HAL_RATE_TABLE *rt;
	switch (mode) {
	case HAL_MODE_11NG_HT20:
	case HAL_MODE_11NG_HT40PLUS:
	case HAL_MODE_11NG_HT40MINUS:
		rt = &ar5416_11ng_table;
		break;
	case HAL_MODE_11NA_HT20:
	case HAL_MODE_11NA_HT40PLUS:
	case HAL_MODE_11NA_HT40MINUS:
		rt = &ar5416_11na_table;
		break;
	default:
		return ar5212GetRateTable(ah, mode);
	}
	ath_hal_setupratetable(ah, rt);
	return rt;
}
