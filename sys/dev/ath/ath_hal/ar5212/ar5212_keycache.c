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
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212desc.h"

/*
 * Note: The key cache hardware requires that each double-word
 * pair be written in even/odd order (since the destination is
 * a 64-bit register).  Don't reorder the writes in this code
 * w/o considering this!
 */
#define	KEY_XOR			0xaa

#define	IS_MIC_ENABLED(ah) \
	(AH5212(ah)->ah_staId1Defaults & AR_STA_ID1_CRPT_MIC_ENABLE)

/*
 * Return the size of the hardware key cache.
 */
uint32_t
ar5212GetKeyCacheSize(struct ath_hal *ah)
{
	return AH_PRIVATE(ah)->ah_caps.halKeyCacheSize;
}

/*
 * Return true if the specific key cache entry is valid.
 */
HAL_BOOL
ar5212IsKeyCacheEntryValid(struct ath_hal *ah, uint16_t entry)
{
	if (entry < AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
		uint32_t val = OS_REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Clear the specified key cache entry and any associated MIC entry.
 */
HAL_BOOL
ar5212ResetKeyCacheEntry(struct ath_hal *ah, uint16_t entry)
{
	uint32_t keyType;

	if (entry >= AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: entry %u out of range\n",
		    __func__, entry);
		return AH_FALSE;
	}
	keyType = OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry));

	/* XXX why not clear key type/valid bit first? */
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), AR_KEYTABLE_TYPE_CLR);
	OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), 0);
	OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), 0);
	if (keyType == AR_KEYTABLE_TYPE_TKIP && IS_MIC_ENABLED(ah)) {
		uint16_t micentry = entry+64;	/* MIC goes at slot+64 */

		HALASSERT(micentry < AH_PRIVATE(ah)->ah_caps.halKeyCacheSize);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);
		/* NB: key type and MAC are known to be ok */
	}
	return AH_TRUE;
}

/*
 * Sets the mac part of the specified key cache entry (and any
 * associated MIC entry) and mark them valid.
 *
 * Since mac[0] is shifted off and not presented to the hardware,
 * it does double duty as a "don't use for unicast, use for multicast
 * matching" flag. This interface should later be extended to
 * explicitly do that rather than overloading a bit in the MAC
 * address.
 */
HAL_BOOL
ar5212SetKeyCacheEntryMac(struct ath_hal *ah, uint16_t entry, const uint8_t *mac)
{
	uint32_t macHi, macLo;
	uint32_t unicast_flag = AR_KEYTABLE_VALID;

	if (entry >= AH_PRIVATE(ah)->ah_caps.halKeyCacheSize) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: entry %u out of range\n",
		    __func__, entry);
		return AH_FALSE;
	}
	/*
	 * Set MAC address -- shifted right by 1.  MacLo is
	 * the 4 MSBs, and MacHi is the 2 LSBs.
	 */
	if (mac != AH_NULL) {
		/*
		 * AR_KEYTABLE_VALID indicates that the address is a unicast
		 * address, which must match the transmitter address for
		 * decrypting frames.
		 * Not setting this bit allows the hardware to use the key
		 * for multicast frame decryption.
		 */
		if (mac[0] & 0x01)
			unicast_flag = 0;

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
	OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), macHi | unicast_flag);
	return AH_TRUE;
}

/*
 * Sets the contents of the specified key cache entry
 * and any associated MIC entry.
 */
HAL_BOOL
ar5212SetKeyCacheEntry(struct ath_hal *ah, uint16_t entry,
                       const HAL_KEYVAL *k, const uint8_t *mac,
                       int xorKey)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
	uint32_t key0, key1, key2, key3, key4;
	uint32_t keyType;
	uint32_t xorMask = xorKey ?
		(KEY_XOR << 24 | KEY_XOR << 16 | KEY_XOR << 8 | KEY_XOR) : 0;

	if (entry >= pCap->halKeyCacheSize) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: entry %u out of range\n",
		    __func__, entry);
		return AH_FALSE;
	}
	switch (k->kv_type) {
	case HAL_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case HAL_CIPHER_AES_CCM:
		if (!pCap->halCipherAesCcmSupport) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: AES-CCM not supported by mac rev 0x%x\n",
			    __func__, AH_PRIVATE(ah)->ah_macRev);
			return AH_FALSE;
		}
		keyType = AR_KEYTABLE_TYPE_CCM;
		break;
	case HAL_CIPHER_TKIP:
		keyType = AR_KEYTABLE_TYPE_TKIP;
		if (IS_MIC_ENABLED(ah) && entry+64 >= pCap->halKeyCacheSize) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: entry %u inappropriate for TKIP\n",
			    __func__, entry);
			return AH_FALSE;
		}
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
	 * Note: key cache hardware requires that each double-word
	 * pair be written in even/odd order (since the destination is
	 * a 64-bit register).  Don't reorder these writes w/o
	 * considering this!
	 */
	if (keyType == AR_KEYTABLE_TYPE_TKIP && IS_MIC_ENABLED(ah)) {
		uint16_t micentry = entry+64;	/* MIC goes at slot+64 */
		uint32_t mic0, mic1, mic2, mic3, mic4;

		/*
		 * Invalidate the encrypt/decrypt key until the MIC
		 * key is installed so pending rx frames will fail
		 * with decrypt errors rather than a MIC error.
		 */
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), ~key0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), ~key1);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);
		(void) ar5212SetKeyCacheEntryMac(ah, entry, mac);


		/*
		 * Write MIC entry according to new or old key layout.
		 * The MISC_MODE register is assumed already set so
		 * these writes will be handled properly (happens on
		 * attach and at every reset).
		 */
		/* RX mic */
		mic0 = LE_READ_4(k->kv_mic+0);
		mic2 = LE_READ_4(k->kv_mic+4);
		if (ahp->ah_miscMode & AR_MISC_MODE_MIC_NEW_LOC_ENABLE) {
			/*
			 * Both RX and TX mic values can be combined into
			 * one cache slot entry:
			 *  8*N + 800         31:0    RX Michael key 0
			 *  8*N + 804         15:0    TX Michael key 0 [31:16]
			 *  8*N + 808         31:0    RX Michael key 1
			 *  8*N + 80C         15:0    TX Michael key 0 [15:0]
			 *  8*N + 810         31:0    TX Michael key 1
			 *  8*N + 814         15:0    reserved
			 *  8*N + 818         31:0    reserved
			 *  8*N + 81C         14:0    reserved
			 *                    15      key valid == 0
			 */
			/* TX mic */
			mic1 = LE_READ_2(k->kv_txmic+2) & 0xffff;
			mic3 = LE_READ_2(k->kv_txmic+0) & 0xffff;
			mic4 = LE_READ_4(k->kv_txmic+4);
		} else {
			mic1 = mic3 = mic4 = 0;
		}
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), mic1);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), mic3);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), mic4);
		OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
			AR_KEYTABLE_TYPE_CLR);
		/* NB: MIC key is not marked valid and has no MAC address */
		OS_REG_WRITE(ah, AR_KEYTABLE_MAC0(micentry), 0);
		OS_REG_WRITE(ah, AR_KEYTABLE_MAC1(micentry), 0);

		/* correct intentionally corrupted key */
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
	} else {
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);
		OS_REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		OS_REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		(void) ar5212SetKeyCacheEntryMac(ah, entry, mac);
	}
	return AH_TRUE;
}
