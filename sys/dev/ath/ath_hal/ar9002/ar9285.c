/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2008-2009 Sam Leffler, Errno Consulting
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
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ah_eeprom_v14.h"

#include "ar9002/ar9280.h"
#include "ar9002/ar9285.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/*
 * The ordering of nfarray is thus:
 *
 * nfarray[0]: Chain 0 ctl
 * nfarray[1]: Chain 1 ctl
 * nfarray[2]: Chain 2 ctl
 * nfarray[3]: Chain 0 ext
 * nfarray[4]: Chain 1 ext
 * nfarray[5]: Chain 2 ext
 */
static void
ar9285GetNoiseFloor(struct ath_hal *ah, int16_t nfarray[])
{
	int16_t nf;

	nf = MS(OS_REG_READ(ah, AR_PHY_CCA), AR9280_PHY_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ctl] [chain 0] is %d\n", nf);
	nfarray[0] = nf;


	nf = MS(OS_REG_READ(ah, AR_PHY_EXT_CCA), AR9280_PHY_EXT_MINCCA_PWR);
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	HALDEBUG(ah, HAL_DEBUG_NFCAL,
	    "NF calibrated [ext] [chain 0] is %d\n", nf);
	nfarray[3] = nf;

	/* Chain 1 - invalid */
	nfarray[1] = 0;
	nfarray[4] = 0;

	/* Chain 2 - invalid */
	nfarray[2] = 0;
	nfarray[5] = 0;
}

HAL_BOOL
ar9285RfAttach(struct ath_hal *ah, HAL_STATUS *status)
{
	if (ar9280RfAttach(ah, status) == AH_FALSE)
		return AH_FALSE;

	AH_PRIVATE(ah)->ah_getNoiseFloor = ar9285GetNoiseFloor;

	return AH_TRUE;
}

static HAL_BOOL
ar9285RfProbe(struct ath_hal *ah)
{
	return (AR_SREV_KITE(ah));
}

AH_RF(RF9285, ar9285RfProbe, ar9285RfAttach);
