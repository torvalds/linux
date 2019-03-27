/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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

#include "ar5211/ar5211.h"
#include "ar5211/ar5211reg.h"

/*
 *  Chips-specific key cache routines.
 */

#define	AR_KEYTABLE_SIZE	128
#define	KEY_XOR			0xaa

/*
 * Return the size of the hardware key cache.
 */
uint32_t
ar5211GetKeyCacheSize(struct ath_hal *ah)
{
	return AR_KEYTABLE_SIZE;
}

/*
 * Return true if the specific key cache entry is valid.
 */
HAL_BOOL
ar5211IsKeyCacheEntryValid(struct ath_hal *ah, uint16_t entry)
{
	if (entry < AR_KEYTABLE_SIZE) {
		uint32_t val = OS_REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Clear the specified key cache entry
 */
HAL_BOOL
ar5211ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry)
{
	if (entry < AR_KEYTABLE_SIZE) {
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), 0);
		return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Sets the mac part of the specified key cache entry and mark it valid.
 */
HAL_BOOL
ar5211SetKeyCacheEntryMac(struct ath_hal *ah, uint16_t entry, const uint8_t *mac)
{
	uint32_t macHi, macLo;

	if (entry >= AR_KEYTABLE_SIZE) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: entry %u out of range\n",
		    __func__, entry);
		return AH_FALSE;
	}

	/*
	 * Set MAC address -- shifted right by 1.  MacLo is
	 * the 4 MSBs, and MacHi is the 2 LSBs.
	 */
	if (mac != AH_NULL) {
		macHi = (mac[5] << 8) | mac[4];
		macLo = (mac[3] << 24)| (mac[2] << 16)
		      | (mac[1] << 8) | mac[0];
		macLo >>= 1;
		macLo |= (macHi & 1) << 31;	/* carry */
		macHi >>= 1;
	} else {
		macLo = macHi = 0;
	}

	OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), macLo);
	OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), macHi | AR_KEYTABLE_VALID);
	return AH_TRUE;
}

/*
 * Sets the contents of the specified key cache entry.
 */
HAL_BOOL
ar5211SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
                       const HAL_KEYVAL *k, const uint8_t *mac,
                       int xorKey)
{
	uint32_t key0, key1, key2, key3, key4;
	uint32_t keyType;
	uint32_t xorMask= xorKey ?
		(KEY_XOR << 24 | KEY_XOR << 16 | KEY_XOR << 8 | KEY_XOR) : 0;

	if (entry >= AR_KEYTABLE_SIZE) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: entry %u out of range\n",
		    __func__, entry);
		return AH_FALSE;
	}
	switch (k->kv_type) {
	case HAL_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case HAL_CIPHER_WEP:
		if (k->kv_len < 40 / NBBY) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: WEP key length %u too small\n",
			    __func__, k->kv_len);
			return AH_FALSE;
		}
		if (k->kv_len <= 40 / NBBY)
			keyType = AR_KEYTABLE_TYPE_40;
		else if (k->kv_len <= 104 / NBBY)
			keyType = AR_KEYTABLE_TYPE_104;
		else
			keyType = AR_KEYTABLE_TYPE_128;
		break;
	case HAL_CIPHER_CLR:
		keyType = AR_KEYTABLE_TYPE_CLR;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: cipher %u not supported\n",
			__func__, k->kv_type);
		return AH_FALSE;
	}

	key0 = LE_READ_4(k->kv_val+0) ^ xorMask;
	key1 = (LE_READ_2(k->kv_val+4) ^ xorMask) & 0xffff;
	key2 = LE_READ_4(k->kv_val+6) ^ xorMask;
	key3 = (LE_READ_2(k->kv_val+10) ^ xorMask) & 0xffff;
	key4 = LE_READ_4(k->kv_val+12) ^ xorMask;
	if (k->kv_len <= 104 / NBBY)
		key4 &= 0xff;


	/*
	 * Note: WEP key cache hardware requires that each double-word
	 * pair be written in even/odd order (since the destination is
	 * a 64-bit register).  Don't reorder these writes w/o
	 * understanding this!
	 */
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
	OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);
	return ar5211SetKeyCacheEntryMac(ah, entry, mac);
}
