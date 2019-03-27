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
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_eeprom_v1.h"

static HAL_STATUS
v1EepromGet(struct ath_hal *ah, int param, void *val)
{
	HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint32_t sum;
	uint16_t eeval;
	uint8_t *macaddr;
	int i;

	switch (param) {
        case AR_EEP_MACADDR:		/* Get MAC Address */
		sum = 0;
		macaddr = val;
		for (i = 0; i < 3; i++) {
			if (!ath_hal_eepromRead(ah, AR_EEPROM_MAC(i), &eeval)) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "%s: cannot read EEPROM location %u\n",
				    __func__, i);
				return HAL_EEREAD;
			}
			sum += eeval;
			macaddr[2*i + 0] = eeval >> 8;
			macaddr[2*i + 1] = eeval & 0xff;
		}
		if (sum == 0 || sum == 0xffff*3) {
			HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad mac address %s\n",
			    __func__, ath_hal_ether_sprintf(macaddr));
			return HAL_EEBADMAC;
		}
		return HAL_OK;
        case AR_EEP_REGDMN_0:
		*(uint16_t *) val = ee->ee_regDomain[0];
		return HAL_OK;
        case AR_EEP_RFKILL:
		HALASSERT(val == AH_NULL);
		return ee->ee_rfKill ? HAL_OK : HAL_EIO;
	case AR_EEP_WRITEPROTECT:
		HALASSERT(val == AH_NULL);
		return (ee->ee_protect & AR_EEPROM_PROTOTECT_WP_128_191) ?
		    HAL_OK : HAL_EIO;
        default:
		HALASSERT(0);
		return HAL_EINVAL;
	}
}

static HAL_STATUS
v1EepromSet(struct ath_hal *ah, int param, int v)
{
	return HAL_EINVAL;
}

static HAL_BOOL
v1EepromDiag(struct ath_hal *ah, int request,
     const void *args, uint32_t argsize, void **result, uint32_t *resultsize)
{
	HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;

	switch (request) {
	case HAL_DIAG_EEPROM:
		*result = ee;
		*resultsize = sizeof(*ee);
		return AH_TRUE;
	}
	return AH_FALSE;
}

static uint16_t 
v1EepromGetSpurChan(struct ath_hal *ah, int ix, HAL_BOOL is2GHz)
{ 
	return AR_NO_SPUR;
}

/*
 * Reclaim any EEPROM-related storage.
 */
static void
v1EepromDetach(struct ath_hal *ah)
{
	HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;

	ath_hal_free(ee);
	AH_PRIVATE(ah)->ah_eeprom = AH_NULL;
}

HAL_STATUS
ath_hal_v1EepromAttach(struct ath_hal *ah)
{
	HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint16_t athvals[AR_EEPROM_ATHEROS_MAX];	/* XXX off stack */
	uint16_t protect, eeprom_version, eeval;
	uint32_t sum;
	int i, loc;

	HALASSERT(ee == AH_NULL);

	if (!ath_hal_eepromRead(ah, AR_EEPROM_MAGIC, &eeval)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read EEPROM magic number\n", __func__);
		return HAL_EEREAD;
	}
	if (eeval != 0x5aa5) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid EEPROM magic number 0x%x\n", __func__, eeval);
		return HAL_EEMAGIC;
	}

	if (!ath_hal_eepromRead(ah, AR_EEPROM_PROTECT, &protect)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read EEPROM protection bits; read locked?\n",
		    __func__);
		return HAL_EEREAD;
	}
	HALDEBUG(ah, HAL_DEBUG_ATTACH, "EEPROM protect 0x%x\n", protect);
	/* XXX check proper access before continuing */

	if (!ath_hal_eepromRead(ah, AR_EEPROM_VERSION, &eeprom_version)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unable to read EEPROM version\n", __func__);
		return HAL_EEREAD;
	}
	if (((eeprom_version>>12) & 0xf) != 1) {
		/*
		 * This code only groks the version 1 EEPROM layout.
		 */
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unsupported EEPROM version 0x%x found\n",
		    __func__, eeprom_version);
		return HAL_EEVERSION;
	}

	/*
	 * Read the Atheros EEPROM entries and calculate the checksum.
	 */
	sum = 0;
	for (i = 0; i < AR_EEPROM_ATHEROS_MAX; i++) {
		if (!ath_hal_eepromRead(ah, AR_EEPROM_ATHEROS(i), &athvals[i]))
			return HAL_EEREAD;
		sum ^= athvals[i];
	}
	if (sum != 0xffff) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad EEPROM checksum 0x%x\n",
		    __func__, sum);
		return HAL_EEBADSUM;
	}

	/*
	 * Valid checksum, fetch the regulatory domain and save values.
	 */
	if (!ath_hal_eepromRead(ah, AR_EEPROM_REG_DOMAIN, &eeval)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read regdomain from EEPROM\n", __func__);
		return HAL_EEREAD;
	}

	ee = ath_hal_malloc(sizeof(HAL_EEPROM_v1));
	if (ee == AH_NULL) {
		/* XXX message */
		return HAL_ENOMEM;
	}

	ee->ee_version		= eeprom_version;
	ee->ee_protect		= protect;
	ee->ee_antenna		= athvals[2];
	ee->ee_biasCurrents	= athvals[3];
	ee->ee_thresh62	= athvals[4] & 0xff;
	ee->ee_xlnaOn		= (athvals[4] >> 8) & 0xff;
	ee->ee_xpaOn		= athvals[5] & 0xff;
	ee->ee_xpaOff		= (athvals[5] >> 8) & 0xff;
	ee->ee_regDomain[0]	= (athvals[6] >> 8) & 0xff;
	ee->ee_regDomain[1]	= athvals[6] & 0xff;
	ee->ee_regDomain[2]	= (athvals[7] >> 8) & 0xff;
	ee->ee_regDomain[3]	= athvals[7] & 0xff;
	ee->ee_rfKill		= athvals[8] & 0x1;
	ee->ee_devType		= (athvals[8] >> 1) & 0x7;

	for (i = 0, loc = AR_EEPROM_ATHEROS_TP_SETTINGS; i < AR_CHANNELS_MAX; i++, loc += AR_TP_SETTINGS_SIZE) {
		struct tpcMap *chan = &ee->ee_tpc[i];

		/* Copy pcdac and gain_f values from EEPROM */
		chan->pcdac[0]	= (athvals[loc] >> 10) & 0x3F;
		chan->gainF[0]	= (athvals[loc] >> 4) & 0x3F;
		chan->pcdac[1]	= ((athvals[loc] << 2) & 0x3C)
				| ((athvals[loc+1] >> 14) & 0x03);
		chan->gainF[1]	= (athvals[loc+1] >> 8) & 0x3F;
		chan->pcdac[2]	= (athvals[loc+1] >> 2) & 0x3F;
		chan->gainF[2]	= ((athvals[loc+1] << 4) & 0x30)
				| ((athvals[loc+2] >> 12) & 0x0F);
		chan->pcdac[3]	= (athvals[loc+2] >> 6) & 0x3F;
		chan->gainF[3]	= athvals[loc+2] & 0x3F;
		chan->pcdac[4]	= (athvals[loc+3] >> 10) & 0x3F;
		chan->gainF[4]	= (athvals[loc+3] >> 4) & 0x3F;
		chan->pcdac[5]	= ((athvals[loc+3] << 2) & 0x3C)
				| ((athvals[loc+4] >> 14) & 0x03);
		chan->gainF[5]	= (athvals[loc+4] >> 8) & 0x3F;
		chan->pcdac[6]	= (athvals[loc+4] >> 2) & 0x3F;
		chan->gainF[6]	= ((athvals[loc+4] << 4) & 0x30)
				| ((athvals[loc+5] >> 12) & 0x0F);
		chan->pcdac[7]	= (athvals[loc+5] >> 6) & 0x3F;
		chan->gainF[7]	= athvals[loc+5] & 0x3F;
		chan->pcdac[8]	= (athvals[loc+6] >> 10) & 0x3F;
		chan->gainF[8]	= (athvals[loc+6] >> 4) & 0x3F;
		chan->pcdac[9]	= ((athvals[loc+6] << 2) & 0x3C)
				| ((athvals[loc+7] >> 14) & 0x03);
		chan->gainF[9]	= (athvals[loc+7] >> 8) & 0x3F;
		chan->pcdac[10]	= (athvals[loc+7] >> 2) & 0x3F;
		chan->gainF[10]	= ((athvals[loc+7] << 4) & 0x30)
				| ((athvals[loc+8] >> 12) & 0x0F);

		/* Copy Regulatory Domain and Rate Information from EEPROM */
		chan->rate36	= (athvals[loc+8] >> 6) & 0x3F;
		chan->rate48	= athvals[loc+8] & 0x3F;
		chan->rate54	= (athvals[loc+9] >> 10) & 0x3F;
		chan->regdmn[0]	= (athvals[loc+9] >> 4) & 0x3F;
		chan->regdmn[1]	= ((athvals[loc+9] << 2) & 0x3C)
				| ((athvals[loc+10] >> 14) & 0x03);
		chan->regdmn[2]	= (athvals[loc+10] >> 8) & 0x3F;
		chan->regdmn[3]	= (athvals[loc+10] >> 2) & 0x3F;
	}

	AH_PRIVATE(ah)->ah_eeprom = ee;
	AH_PRIVATE(ah)->ah_eeversion = eeprom_version;
	AH_PRIVATE(ah)->ah_eepromDetach = v1EepromDetach;
	AH_PRIVATE(ah)->ah_eepromGet = v1EepromGet;
	AH_PRIVATE(ah)->ah_eepromSet = v1EepromSet;
	AH_PRIVATE(ah)->ah_getSpurChan = v1EepromGetSpurChan;
	AH_PRIVATE(ah)->ah_eepromDiag = v1EepromDiag;
	return HAL_OK;
}
