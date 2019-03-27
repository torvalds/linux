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

#include "ar5212/ar5212.h"

/* shorthands to compact tables for readability */
#define	OFDM	IEEE80211_T_OFDM
#define	CCK	IEEE80211_T_CCK
#define	TURBO	IEEE80211_T_TURBO
#define	HALF	IEEE80211_T_OFDM_HALF
#define	QUART	IEEE80211_T_OFDM_QUARTER

HAL_RATE_TABLE ar5212_11a_table = {
	8,  /* number of rates */
	{ 0 },
	{
/*                                                  short            ctrl  */
/*                valid                 rateCode Preamble  dot11Rate Rate */
/*   6 Mb */ {  AH_TRUE, OFDM,    6000,     0x0b,    0x00, (0x80|12),   0 },
/*   9 Mb */ {  AH_TRUE, OFDM,    9000,     0x0f,    0x00,        18,   0 },
/*  12 Mb */ {  AH_TRUE, OFDM,   12000,     0x0a,    0x00, (0x80|24),   2 },
/*  18 Mb */ {  AH_TRUE, OFDM,   18000,     0x0e,    0x00,        36,   2 },
/*  24 Mb */ {  AH_TRUE, OFDM,   24000,     0x09,    0x00, (0x80|48),   4 },
/*  36 Mb */ {  AH_TRUE, OFDM,   36000,     0x0d,    0x00,        72,   4 },
/*  48 Mb */ {  AH_TRUE, OFDM,   48000,     0x08,    0x00,        96,   4 },
/*  54 Mb */ {  AH_TRUE, OFDM,   54000,     0x0c,    0x00,       108,   4 }
	},
};

HAL_RATE_TABLE ar5212_half_table = {
	8,  /* number of rates */
	{ 0 },
	{
/*                                                  short            ctrl  */
/*                valid                 rateCode Preamble  dot11Rate Rate */
/*   3 Mb */ {  AH_TRUE, HALF,    3000,     0x0b,    0x00, (0x80|6),   0 },
/* 4.5 Mb */ {  AH_TRUE, HALF,    4500,     0x0f,    0x00,        9,   0 },
/*   6 Mb */ {  AH_TRUE, HALF,    6000,     0x0a,    0x00, (0x80|12),  2 },
/*   9 Mb */ {  AH_TRUE, HALF,    9000,     0x0e,    0x00,       18,   2 },
/*  12 Mb */ {  AH_TRUE, HALF,   12000,     0x09,    0x00, (0x80|24),  4 },
/*  18 Mb */ {  AH_TRUE, HALF,   18000,     0x0d,    0x00,       36,   4 },
/*  24 Mb */ {  AH_TRUE, HALF,   24000,     0x08,    0x00,       48,   4 },
/*  27 Mb */ {  AH_TRUE, HALF,   27000,     0x0c,    0x00,       54,   4 }
	},
};

HAL_RATE_TABLE ar5212_quarter_table = {
	8,  /* number of rates */
	{ 0 },
	{
/*                                                  short            ctrl  */
/*                valid                 rateCode Preamble  dot11Rate Rate */
/* 1.5 Mb */ {  AH_TRUE, QUART,   1500,     0x0b,    0x00, (0x80|3),   0 },
/*   2 Mb */ {  AH_TRUE, QUART,   2250,     0x0f,    0x00,        4,   0 },
/*   3 Mb */ {  AH_TRUE, QUART,   3000,     0x0a,    0x00, (0x80|6),   2 },
/* 4.5 Mb */ {  AH_TRUE, QUART,   4500,     0x0e,    0x00,        9,   2 },
/*   6 Mb */ {  AH_TRUE, QUART,   6000,     0x09,    0x00, (0x80|12),  4 },
/*   9 Mb */ {  AH_TRUE, QUART,   9000,     0x0d,    0x00,       18,   4 },
/*  12 Mb */ {  AH_TRUE, QUART,  12000,     0x08,    0x00,       24,   4 },
/*13.5 Mb */ {  AH_TRUE, QUART,  13500,     0x0c,    0x00,       27,   4 }
	},
};

HAL_RATE_TABLE ar5212_turbog_table = {
	7,  /* number of rates */
	{ 0 },
	{
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   6 Mb */ {  AH_TRUE, TURBO,  12000,    0x0b,    0x00, (0x80|12),   0 },
/*  12 Mb */ {  AH_TRUE, TURBO,  24000,    0x0a,    0x00, (0x80|24),   1 },
/*  18 Mb */ {  AH_TRUE, TURBO,  36000,    0x0e,    0x00,        36,   1 },
/*  24 Mb */ {  AH_TRUE, TURBO,  48000,    0x09,    0x00, (0x80|48),   2 },
/*  36 Mb */ {  AH_TRUE, TURBO,  72000,    0x0d,    0x00,        72,   2 },
/*  48 Mb */ {  AH_TRUE, TURBO,  96000,    0x08,    0x00,        96,   2 },
/*  54 Mb */ {  AH_TRUE, TURBO, 108000,    0x0c,    0x00,       108,   2 }
	},
};

HAL_RATE_TABLE ar5212_turboa_table = {
	8,  /* number of rates */
	{ 0 },
	{
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   6 Mb */ {  AH_TRUE, TURBO,  12000,    0x0b,    0x00, (0x80|12),   0 },
/*   9 Mb */ {  AH_TRUE, TURBO,  18000,    0x0f,    0x00,        18,   0 },
/*  12 Mb */ {  AH_TRUE, TURBO,  24000,    0x0a,    0x00, (0x80|24),   2 },
/*  18 Mb */ {  AH_TRUE, TURBO,  36000,    0x0e,    0x00,        36,   2 },
/*  24 Mb */ {  AH_TRUE, TURBO,  48000,    0x09,    0x00, (0x80|48),   4 },
/*  36 Mb */ {  AH_TRUE, TURBO,  72000,    0x0d,    0x00,        72,   4 },
/*  48 Mb */ {  AH_TRUE, TURBO,  96000,    0x08,    0x00,        96,   4 },
/*  54 Mb */ {  AH_TRUE, TURBO, 108000,    0x0c,    0x00,       108,   4 }
	},
};

HAL_RATE_TABLE ar5212_11b_table = {
	4,  /* number of rates */
	{ 0 },
	{
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   1 Mb */ {  AH_TRUE,  CCK,    1000,    0x1b,    0x00, (0x80| 2),   0 },
/*   2 Mb */ {  AH_TRUE,  CCK,    2000,    0x1a,    0x04, (0x80| 4),   1 },
/* 5.5 Mb */ {  AH_TRUE,  CCK,    5500,    0x19,    0x04, (0x80|11),   1 },
/*  11 Mb */ {  AH_TRUE,  CCK,   11000,    0x18,    0x04, (0x80|22),   1 }
	},
};


/* Venice TODO: roundUpRate() is broken when the rate table does not represent rates
 * in increasing order  e.g.  5.5, 11, 6, 9.    
 * An average rate of 6 Mbps will currently map to 11 Mbps. 
 */
HAL_RATE_TABLE ar5212_11g_table = {
	12,  /* number of rates */
	{ 0 },
	{
/*                                                 short            ctrl  */
/*                valid                rateCode Preamble  dot11Rate Rate */
/*   1 Mb */ {  AH_TRUE, CCK,     1000,    0x1b,    0x00, (0x80| 2),   0 },
/*   2 Mb */ {  AH_TRUE, CCK,     2000,    0x1a,    0x04, (0x80| 4),   1 },
/* 5.5 Mb */ {  AH_TRUE, CCK,     5500,    0x19,    0x04, (0x80|11),   2 },
/*  11 Mb */ {  AH_TRUE, CCK,    11000,    0x18,    0x04, (0x80|22),   3 },
/* remove rates 6, 9 from rate ctrl */
/*   6 Mb */ { AH_FALSE, OFDM,    6000,    0x0b,    0x00,        12,   4 },
/*   9 Mb */ { AH_FALSE, OFDM,    9000,    0x0f,    0x00,        18,   4 },
/*  12 Mb */ {  AH_TRUE, OFDM,   12000,    0x0a,    0x00,        24,   6 },
/*  18 Mb */ {  AH_TRUE, OFDM,   18000,    0x0e,    0x00,        36,   6 },
/*  24 Mb */ {  AH_TRUE, OFDM,   24000,    0x09,    0x00,        48,   8 },
/*  36 Mb */ {  AH_TRUE, OFDM,   36000,    0x0d,    0x00,        72,   8 },
/*  48 Mb */ {  AH_TRUE, OFDM,   48000,    0x08,    0x00,        96,   8 },
/*  54 Mb */ {  AH_TRUE, OFDM,   54000,    0x0c,    0x00,       108,   8 }
	},
};

#undef	OFDM
#undef	CCK
#undef	TURBO
#undef	XR

const HAL_RATE_TABLE *
ar5212GetRateTable(struct ath_hal *ah, u_int mode)
{
	HAL_RATE_TABLE *rt;
	switch (mode) {
	case HAL_MODE_11A:
		rt = &ar5212_11a_table;
		break;
	case HAL_MODE_11B:
		rt = &ar5212_11b_table;
		break;
	case HAL_MODE_11G:
#ifdef notdef
	case HAL_MODE_PUREG:
#endif
		rt =  &ar5212_11g_table;
		break;
	case HAL_MODE_108A:
	case HAL_MODE_TURBO:
		rt =  &ar5212_turboa_table;
		break;
	case HAL_MODE_108G:
		rt =  &ar5212_turbog_table;
		break;
	case HAL_MODE_11A_HALF_RATE:
	case HAL_MODE_11G_HALF_RATE:
		rt = &ar5212_half_table;
		break;
	case HAL_MODE_11A_QUARTER_RATE:
	case HAL_MODE_11G_QUARTER_RATE:
		rt = &ar5212_quarter_table;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid mode 0x%x\n",
		    __func__, mode);
		return AH_NULL;
	}
	ath_hal_setupratetable(ah, rt);
	return rt;
}
