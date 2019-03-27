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
#include "ah_devid.h"

#include "ah_eeprom_v14.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

/*
 * Read 16 bits of data from offset into *data
 */
HAL_BOOL
ar5416EepromRead(struct ath_hal *ah, u_int off, uint16_t *data)
{
        OS_REG_READ(ah,  AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));
       	if (!ath_hal_wait(ah, AR_EEPROM_STATUS_DATA,
	    AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0))
		return AH_FALSE;
       	*data = MS(OS_REG_READ(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);
	return AH_TRUE;
}
