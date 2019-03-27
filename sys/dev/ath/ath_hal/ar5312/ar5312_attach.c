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

#include "ar5312/ar5312.h"
#include "ar5312/ar5312reg.h"
#include "ar5312/ar5312phy.h"

/* Add static register initialization vectors */
#define AH_5212_COMMON
#include "ar5212/ar5212.ini"

static  HAL_BOOL ar5312GetMacAddr(struct ath_hal *ah);

static void
ar5312AniSetup(struct ath_hal *ah)
{
	static const struct ar5212AniParams aniparams = {
		.maxNoiseImmunityLevel	= 4,	/* levels 0..4 */
		.totalSizeDesired	= { -41, -41, -48, -48, -48 },
		.coarseHigh		= { -18, -18, -16, -14, -12 },
		.coarseLow		= { -56, -56, -60, -60, -60 },
		.firpwr			= { -72, -72, -75, -78, -80 },
		.maxSpurImmunityLevel	= 2,
		.cycPwrThr1		= { 2, 4, 6 },
		.maxFirstepLevel	= 2,	/* levels 0..2 */
		.firstep		= { 0, 4, 8 },
		.ofdmTrigHigh		= 500,
		.ofdmTrigLow		= 200,
		.cckTrigHigh		= 200,
		.cckTrigLow		= 100,
		.rssiThrHigh		= 40,
		.rssiThrLow		= 7,
		.period			= 100,
	};
	ar5212AniAttach(ah, &aniparams, &aniparams, AH_TRUE);
}

/*
 * Attach for an AR5312 part.
 */
static struct ath_hal *
ar5312Attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config, HAL_STATUS *status)
{
	struct ath_hal_5212 *ahp = AH_NULL;
	struct ath_hal *ah;
	struct ath_hal_rf *rf;
	uint32_t val;
	uint16_t eeval;
	HAL_STATUS ecode;

	HALDEBUG(AH_NULL, HAL_DEBUG_ATTACH, "%s: sc %p st %p sh %p\n",
		 __func__, sc, st, (void*) sh);

	/* NB: memory is returned zero'd */
	ahp = ath_hal_malloc(sizeof (struct ath_hal_5212));
	if (ahp == AH_NULL) {
		HALDEBUG(AH_NULL, HAL_DEBUG_ANY,
		    "%s: cannot allocate memory for state block\n", __func__);
		*status = HAL_ENOMEM;
		return AH_NULL;
	}
	ar5212InitState(ahp, devid, sc, st, sh, status);
	ah = &ahp->ah_priv.h;

	/* override 5212 methods for our needs */
	ah->ah_reset			= ar5312Reset;
	ah->ah_phyDisable		= ar5312PhyDisable;
	ah->ah_setLedState		= ar5312SetLedState;
	ah->ah_detectCardPresent	= ar5312DetectCardPresent;
	ah->ah_setPowerMode		= ar5312SetPowerMode;
	ah->ah_getPowerMode		= ar5312GetPowerMode;
	ah->ah_isInterruptPending	= ar5312IsInterruptPending;

	ahp->ah_priv.ah_eepromRead	= ar5312EepromRead;
#ifdef AH_SUPPORT_WRITE_EEPROM
	ahp->ah_priv.ah_eepromWrite	= ar5312EepromWrite;
#endif
#if ( AH_SUPPORT_2316 || AH_SUPPORT_2317)
	if (IS_5315(ah)) {
		ahp->ah_priv.ah_gpioCfgOutput	= ar5315GpioCfgOutput;
		ahp->ah_priv.ah_gpioCfgInput	= ar5315GpioCfgInput;
		ahp->ah_priv.ah_gpioGet		= ar5315GpioGet;
		ahp->ah_priv.ah_gpioSet		= ar5315GpioSet;
		ahp->ah_priv.ah_gpioSetIntr	= ar5315GpioSetIntr;
	} else
#endif
	{
		ahp->ah_priv.ah_gpioCfgOutput	= ar5312GpioCfgOutput;
		ahp->ah_priv.ah_gpioCfgInput	= ar5312GpioCfgInput;
		ahp->ah_priv.ah_gpioGet		= ar5312GpioGet;
		ahp->ah_priv.ah_gpioSet		= ar5312GpioSet;
		ahp->ah_priv.ah_gpioSetIntr	= ar5312GpioSetIntr;
	}

	ah->ah_gpioCfgInput		= ahp->ah_priv.ah_gpioCfgInput;
	ah->ah_gpioCfgOutput		= ahp->ah_priv.ah_gpioCfgOutput;
	ah->ah_gpioGet			= ahp->ah_priv.ah_gpioGet;
	ah->ah_gpioSet			= ahp->ah_priv.ah_gpioSet;
	ah->ah_gpioSetIntr		= ahp->ah_priv.ah_gpioSetIntr;

	/* setup common ini data; rf backends handle remainder */
	HAL_INI_INIT(&ahp->ah_ini_modes, ar5212Modes, 6);
	HAL_INI_INIT(&ahp->ah_ini_common, ar5212Common, 2);

	if (!ar5312ChipReset(ah, AH_NULL)) {	/* reset chip */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		ecode = HAL_EIO;
		goto bad;
	}

#if ( AH_SUPPORT_2316 || AH_SUPPORT_2317)
	if ((devid == AR5212_AR2315_REV6) ||
	    (devid == AR5212_AR2315_REV7) ||
	    (devid == AR5212_AR2317_REV1) ||
	    (devid == AR5212_AR2317_REV2) ) {
		val = ((OS_REG_READ(ah, (AR5315_RSTIMER_BASE -((uint32_t) sh)) + AR5315_WREV)) >> AR5315_WREV_S)
			& AR5315_WREV_ID;
		AH_PRIVATE(ah)->ah_macVersion = val >> AR5315_WREV_ID_S;
		AH_PRIVATE(ah)->ah_macRev = val & AR5315_WREV_REVISION;
		HALDEBUG(ah, HAL_DEBUG_ATTACH,
		    "%s: Mac Chip Rev 0x%02x.%x\n" , __func__,
		    AH_PRIVATE(ah)->ah_macVersion, AH_PRIVATE(ah)->ah_macRev);
	} else
#endif
	{
		val = OS_REG_READ(ah, (AR5312_RSTIMER_BASE - ((uint32_t) sh)) + 0x0020);
		val = OS_REG_READ(ah, (AR5312_RSTIMER_BASE - ((uint32_t) sh)) + 0x0080);
		/* Read Revisions from Chips */
		val = ((OS_REG_READ(ah, (AR5312_RSTIMER_BASE - ((uint32_t) sh)) + AR5312_WREV)) >> AR5312_WREV_S) & AR5312_WREV_ID;
		AH_PRIVATE(ah)->ah_macVersion = val >> AR5312_WREV_ID_S;
		AH_PRIVATE(ah)->ah_macRev = val & AR5312_WREV_REVISION;
	}
	/* XXX - THIS IS WRONG. NEEDS TO BE FIXED */
	if (((AH_PRIVATE(ah)->ah_macVersion != AR_SREV_VERSION_VENICE &&
              AH_PRIVATE(ah)->ah_macVersion != AR_SREV_VERSION_VENICE) ||
             AH_PRIVATE(ah)->ah_macRev < AR_SREV_D2PLUS) &&
              AH_PRIVATE(ah)->ah_macVersion != AR_SREV_VERSION_COBRA) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: Mac Chip Rev 0x%02x.%x is not supported by "
                         "this driver\n", __func__,
                         AH_PRIVATE(ah)->ah_macVersion,
                         AH_PRIVATE(ah)->ah_macRev);
#endif
		ecode = HAL_ENOTSUPP;
		goto bad;
	}
        
	AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);
        
	if (!ar5212ChipTest(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: hardware self-test failed\n",
		    __func__);
		ecode = HAL_ESELFTEST;
		goto bad;
	}

	/*
	 * Set correct Baseband to analog shift
	 * setting to access analog chips.
	 */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);
        
	/* Read Radio Chip Rev Extract */
	AH_PRIVATE(ah)->ah_analog5GhzRev = ar5212GetRadioRev(ah);

	rf = ath_hal_rfprobe(ah, &ecode);
	if (rf == AH_NULL)
		goto bad;
	if (IS_RAD5112(ah) && !IS_RADX112_REV2(ah)) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: 5112 Rev 1 is not supported by this "
                         "driver (analog5GhzRev 0x%x)\n", __func__,
                         AH_PRIVATE(ah)->ah_analog5GhzRev);
#endif
		ecode = HAL_ENOTSUPP;
		goto bad;
	}

	ecode = ath_hal_legacyEepromAttach(ah);
	if (ecode != HAL_OK) {
		goto bad;
	}

	/*
	 * If Bmode and AR5212, verify 2.4 analog exists
	 */
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE) &&
	    (AH_PRIVATE(ah)->ah_analog5GhzRev & 0xF0) == AR_RAD5111_SREV_MAJOR) {
		/*
		 * Set correct Baseband to analog shift
		 * setting to access analog chips.
		 */
		OS_REG_WRITE(ah, AR_PHY(0), 0x00004007);
		OS_DELAY(2000);
		AH_PRIVATE(ah)->ah_analog2GhzRev = ar5212GetRadioRev(ah);

		/* Set baseband for 5GHz chip */
		OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);
		OS_DELAY(2000);
		if ((AH_PRIVATE(ah)->ah_analog2GhzRev & 0xF0) != AR_RAD2111_SREV_MAJOR) {
#ifdef AH_DEBUG
			ath_hal_printf(ah, "%s: 2G Radio Chip Rev 0x%02X is not "
				"supported by this driver\n", __func__,
				AH_PRIVATE(ah)->ah_analog2GhzRev);
#endif
			ecode = HAL_ENOTSUPP;
			goto bad;
		}
	}

	ecode = ath_hal_eepromGet(ah, AR_EEP_REGDMN_0, &eeval);
	if (ecode != HAL_OK) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cannot read regulatory domain from EEPROM\n",
		    __func__);
		goto bad;
        }
	AH_PRIVATE(ah)->ah_currentRD = eeval;
	/* XXX record serial number */

	/* XXX other capabilities */
	/*
	 * Got everything we need now to setup the capabilities.
	 */
	if (!ar5212FillCapabilityInfo(ah)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: failed ar5212FillCapabilityInfo\n", __func__);
		ecode = HAL_EEREAD;
		goto bad;
	}

	if (!rf->attach(ah, &ecode)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: RF setup failed, status %u\n",
		    __func__, ecode);
		goto bad;
	}
	/* arrange a direct call instead of thunking */
	AH_PRIVATE(ah)->ah_getNfAdjust = ahp->ah_rfHal->getNfAdjust;

	/* Initialize gain ladder thermal calibration structure */
	ar5212InitializeGainValues(ah);

        /* BSP specific call for MAC address of this WMAC device */
        if (!ar5312GetMacAddr(ah)) {
                ecode = HAL_EEBADMAC;
                goto bad;
        }

	ar5312AniSetup(ah);
	ar5212InitNfCalHistBuffer(ah);

	/* XXX EAR stuff goes here */
	return ah;

bad:
	if (ahp)
		ar5212Detach((struct ath_hal *) ahp);
	if (status)
		*status = ecode;
	return AH_NULL;
}

static HAL_BOOL
ar5312GetMacAddr(struct ath_hal *ah)
{
	const struct ar531x_boarddata *board = AR5312_BOARDCONFIG(ah); 
        int wlanNum = AR5312_UNIT(ah);
        const uint8_t *macAddr;

	switch (wlanNum) {
	case 0:
		macAddr = board->wlan0Mac;
		break;
	case 1:
		macAddr = board->wlan1Mac;
		break;
	default:
#ifdef AH_DEBUG
		ath_hal_printf(ah, "Invalid WLAN wmac index (%d)\n",
			       wlanNum);
#endif
		return AH_FALSE;
	}
	OS_MEMCPY(AH5212(ah)->ah_macaddr, macAddr, 6);
	return AH_TRUE;
}

static const char*
ar5312Probe(uint16_t vendorid, uint16_t devid)
{
	if (vendorid == ATHEROS_VENDOR_ID) {
		switch (devid) {
		case AR5212_AR5312_REV2:
		case AR5212_AR5312_REV7:
			return "Atheros 5312 WiSoC";
		case AR5212_AR2313_REV8:
			return "Atheros 2313 WiSoC";
		case AR5212_AR2315_REV6:
		case AR5212_AR2315_REV7:
			return "Atheros 2315 WiSoC";
		case AR5212_AR2317_REV1:
		case AR5212_AR2317_REV2:
			return "Atheros 2317 WiSoC";
		case AR5212_AR2413:
			return "Atheros 2413";
		case AR5212_AR2417:
			return "Atheros 2417";
		}
	}
	return AH_NULL;
}
AH_CHIP(AR5312, ar5312Probe, ar5312Attach);
