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
#ifndef _DEV_ATH_AR5312PHY_H_
#define _DEV_ATH_AR5312PHY_H_

#include "ar5212/ar5212phy.h"

/* PHY registers */

#define AR_PHY_PLL_CTL_44_5312  0x14d6          /* 44 MHz for 11b, 11g */
#define AR_PHY_PLL_CTL_40_5312  0x14d4          /* 40 MHz for 11a, turbos */
#define AR_PHY_PLL_CTL_40_5312_HALF  0x15d4	/* 40 MHz for 11a, turbos (Half)*/
#define AR_PHY_PLL_CTL_40_5312_QUARTER  0x16d4	/* 40 MHz for 11a, turbos (Quarter)*/

#endif	/* _DEV_ATH_AR5312PHY_H_ */
