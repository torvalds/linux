/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
#include "ar5211/ar5211phy.h"

#include "ah_eeprom_v3.h"

#define	AR_NUM_GPIO	6		/* 6 GPIO bits */
#define	AR_GPIOD_MASK	0x2f		/* 6-bit mask */

void
ar5211GetMacAddress(struct ath_hal *ah, uint8_t *mac)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	OS_MEMCPY(mac, ahp->ah_macaddr, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5211SetMacAddress(struct ath_hal *ah, const uint8_t *mac)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	OS_MEMCPY(ahp->ah_macaddr, mac, IEEE80211_ADDR_LEN);
	return AH_TRUE;
}

void
ar5211GetBssIdMask(struct ath_hal *ah, uint8_t *mask)
{
	static const uint8_t ones[IEEE80211_ADDR_LEN] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	OS_MEMCPY(mask, ones, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5211SetBssIdMask(struct ath_hal *ah, const uint8_t *mask)
{
	return AH_FALSE;
}

/*
 * Read 16 bits of data from the specified EEPROM offset.
 */
HAL_BOOL
ar5211EepromRead(struct ath_hal *ah, u_int off, uint16_t *data)
{
	OS_REG_WRITE(ah, AR_EEPROM_ADDR, off);
	OS_REG_WRITE(ah, AR_EEPROM_CMD, AR_EEPROM_CMD_READ);

	if (!ath_hal_wait(ah, AR_EEPROM_STS,
	    AR_EEPROM_STS_READ_COMPLETE | AR_EEPROM_STS_READ_ERROR,
	    AR_EEPROM_STS_READ_COMPLETE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: read failed for entry 0x%x\n", __func__, off);
		return AH_FALSE;
	}
	*data = OS_REG_READ(ah, AR_EEPROM_DATA) & 0xffff;
	return AH_TRUE;
}

#ifdef AH_SUPPORT_WRITE_EEPROM
/*
 * Write 16 bits of data to the specified EEPROM offset.
 */
HAL_BOOL
ar5211EepromWrite(struct ath_hal *ah, u_int off, uint16_t data)
{
	return AH_FALSE;
}
#endif /* AH_SUPPORT_WRITE_EEPROM */

/*
 * Attempt to change the cards operating regulatory domain to the given value
 */
HAL_BOOL
ar5211SetRegulatoryDomain(struct ath_hal *ah,
	uint16_t regDomain, HAL_STATUS *status)
{
	HAL_STATUS ecode;

	if (AH_PRIVATE(ah)->ah_currentRD == regDomain) {
		ecode = HAL_EINVAL;
		goto bad;
	}
	/*
	 * Check if EEPROM is configured to allow this; must
	 * be a proper version and the protection bits must
	 * permit re-writing that segment of the EEPROM.
	 */
	if (ath_hal_eepromGetFlag(ah, AR_EEP_WRITEPROTECT)) {
		ecode = HAL_EEWRITE;
		goto bad;
	}
#ifdef AH_SUPPORT_WRITE_REGDOMAIN
	if (ar5211EepromWrite(ah, AR_EEPROM_REG_DOMAIN, regDomain)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: set regulatory domain to %u (0x%x)\n",
		    __func__, regDomain, regDomain);
		AH_PRIVATE(ah)->ah_currentRD = regDomain;
		return AH_TRUE;
	}
#endif
	ecode = HAL_EIO;
bad:
	if (status)
		*status = ecode;
	return AH_FALSE;
}

/*
 * Return the wireless modes (a,b,g,t) supported by hardware.
 *
 * This value is what is actually supported by the hardware
 * and is unaffected by regulatory/country code settings.
 *
 */
u_int
ar5211GetWirelessModes(struct ath_hal *ah)
{
	u_int mode = 0;

	if (ath_hal_eepromGetFlag(ah, AR_EEP_AMODE)) {
		mode = HAL_MODE_11A;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO5DISABLE))
			mode |= HAL_MODE_TURBO | HAL_MODE_108A;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE))
		mode |= HAL_MODE_11B;
	return mode;
}

#if 0
HAL_BOOL
ar5211GetTurboDisable(struct ath_hal *ah)
{
	return (AH5211(ah)->ah_turboDisable != 0);
}
#endif

/*
 * Called if RfKill is supported (according to EEPROM).  Set the interrupt and
 * GPIO values so the ISR and can disable RF on a switch signal
 */
void
ar5211EnableRfKill(struct ath_hal *ah)
{
	uint16_t rfsilent = AH_PRIVATE(ah)->ah_rfsilent;
	int select = MS(rfsilent, AR_EEPROM_RFSILENT_GPIO_SEL);
	int polarity = MS(rfsilent, AR_EEPROM_RFSILENT_POLARITY);

	/*
	 * Configure the desired GPIO port for input
	 * and enable baseband rf silence.
	 */
	ar5211GpioCfgInput(ah, select);
	OS_REG_SET_BIT(ah, AR_PHY_BASE, 0x00002000);
	/*
	 * If radio disable switch connection to GPIO bit x is enabled
	 * program GPIO interrupt.
	 * If rfkill bit on eeprom is 1, setupeeprommap routine has already
	 * verified that it is a later version of eeprom, it has a place for
	 * rfkill bit and it is set to 1, indicating that GPIO bit x hardware
	 * connection is present.
	 */
	ar5211GpioSetIntr(ah, select, (ar5211GpioGet(ah, select) != polarity));
}

/*
 * Configure GPIO Output lines
 */
HAL_BOOL
ar5211GpioCfgOutput(struct ath_hal *ah, uint32_t gpio, HAL_GPIO_MUX_TYPE type)
{
	uint32_t reg;

	HALASSERT(gpio < AR_NUM_GPIO);

	reg =  OS_REG_READ(ah, AR_GPIOCR);
	reg &= ~(AR_GPIOCR_0_CR_A << (gpio * AR_GPIOCR_CR_SHIFT));
	reg |= AR_GPIOCR_0_CR_A << (gpio * AR_GPIOCR_CR_SHIFT);

	OS_REG_WRITE(ah, AR_GPIOCR, reg);
	return AH_TRUE;
}

/*
 * Configure GPIO Input lines
 */
HAL_BOOL
ar5211GpioCfgInput(struct ath_hal *ah, uint32_t gpio)
{
	uint32_t reg;

	HALASSERT(gpio < AR_NUM_GPIO);

	reg =  OS_REG_READ(ah, AR_GPIOCR);
	reg &= ~(AR_GPIOCR_0_CR_A << (gpio * AR_GPIOCR_CR_SHIFT));
	reg |= AR_GPIOCR_0_CR_N << (gpio * AR_GPIOCR_CR_SHIFT);

	OS_REG_WRITE(ah, AR_GPIOCR, reg);
	return AH_TRUE;
}

/*
 * Once configured for I/O - set output lines
 */
HAL_BOOL
ar5211GpioSet(struct ath_hal *ah, uint32_t gpio, uint32_t val)
{
	uint32_t reg;

	HALASSERT(gpio < AR_NUM_GPIO);

	reg =  OS_REG_READ(ah, AR_GPIODO);
	reg &= ~(1 << gpio);
	reg |= (val&1) << gpio;

	OS_REG_WRITE(ah, AR_GPIODO, reg);
	return AH_TRUE;
}

/*
 * Once configured for I/O - get input lines
 */
uint32_t
ar5211GpioGet(struct ath_hal *ah, uint32_t gpio)
{
	if (gpio < AR_NUM_GPIO) {
		uint32_t val = OS_REG_READ(ah, AR_GPIODI);
		val = ((val & AR_GPIOD_MASK) >> gpio) & 0x1;
		return val;
	} else  {
		return 0xffffffff;
	}
}

/*
 * Set the GPIO 0 Interrupt (gpio is ignored)
 */
void
ar5211GpioSetIntr(struct ath_hal *ah, u_int gpio, uint32_t ilevel)
{
	uint32_t val = OS_REG_READ(ah, AR_GPIOCR);

	/* Clear the bits that we will modify. */
	val &= ~(AR_GPIOCR_INT_SEL0 | AR_GPIOCR_INT_SELH | AR_GPIOCR_INT_ENA |
			AR_GPIOCR_0_CR_A);

	val |= AR_GPIOCR_INT_SEL0 | AR_GPIOCR_INT_ENA;
	if (ilevel)
		val |= AR_GPIOCR_INT_SELH;

	/* Don't need to change anything for low level interrupt. */
	OS_REG_WRITE(ah, AR_GPIOCR, val);

	/* Change the interrupt mask. */
	ar5211SetInterrupts(ah, AH5211(ah)->ah_maskReg | HAL_INT_GPIO);
}

/*
 * Change the LED blinking pattern to correspond to the connectivity
 */
void
ar5211SetLedState(struct ath_hal *ah, HAL_LED_STATE state)
{
	static const uint32_t ledbits[8] = {
		AR_PCICFG_LEDCTL_NONE|AR_PCICFG_LEDMODE_PROP, /* HAL_LED_INIT */
		AR_PCICFG_LEDCTL_PEND|AR_PCICFG_LEDMODE_PROP, /* HAL_LED_SCAN */
		AR_PCICFG_LEDCTL_PEND|AR_PCICFG_LEDMODE_PROP, /* HAL_LED_AUTH */
		AR_PCICFG_LEDCTL_ASSOC|AR_PCICFG_LEDMODE_PROP,/* HAL_LED_ASSOC*/
		AR_PCICFG_LEDCTL_ASSOC|AR_PCICFG_LEDMODE_PROP,/* HAL_LED_RUN */
		AR_PCICFG_LEDCTL_NONE|AR_PCICFG_LEDMODE_RAND,
		AR_PCICFG_LEDCTL_NONE|AR_PCICFG_LEDMODE_RAND,
		AR_PCICFG_LEDCTL_NONE|AR_PCICFG_LEDMODE_RAND,
	};
	OS_REG_WRITE(ah, AR_PCICFG,
		(OS_REG_READ(ah, AR_PCICFG) &~
			(AR_PCICFG_LEDCTL | AR_PCICFG_LEDMODE))
		| ledbits[state & 0x7]
	);
}

/*
 * Change association related fields programmed into the hardware.
 * Writing a valid BSSID to the hardware effectively enables the hardware
 * to synchronize its TSF to the correct beacons and receive frames coming
 * from that BSSID. It is called by the SME JOIN operation.
 */
void
ar5211WriteAssocid(struct ath_hal *ah, const uint8_t *bssid, uint16_t assocId)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	/* XXX save bssid for possible re-use on reset */
	OS_MEMCPY(ahp->ah_bssid, bssid, IEEE80211_ADDR_LEN);
	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid+4) |
				     ((assocId & 0x3fff)<<AR_BSS_ID1_AID_S));
}

/*
 * Get the current hardware tsf for stamlme.
 */
uint64_t
ar5211GetTsf64(struct ath_hal *ah)
{
	uint32_t low1, low2, u32;

	/* sync multi-word read */
	low1 = OS_REG_READ(ah, AR_TSF_L32);
	u32 = OS_REG_READ(ah, AR_TSF_U32);
	low2 = OS_REG_READ(ah, AR_TSF_L32);
	if (low2 < low1) {	/* roll over */
		/*
		 * If we are not preempted this will work.  If we are
		 * then we re-reading AR_TSF_U32 does no good as the
		 * low bits will be meaningless.  Likewise reading
		 * L32, U32, U32, then comparing the last two reads
		 * to check for rollover doesn't help if preempted--so
		 * we take this approach as it costs one less PCI
		 * read which can be noticeable when doing things
		 * like timestamping packets in monitor mode.
		 */
		u32++;
	}
	return (((uint64_t) u32) << 32) | ((uint64_t) low2);
}

/*
 * Get the current hardware tsf for stamlme.
 */
uint32_t
ar5211GetTsf32(struct ath_hal *ah)
{
	return OS_REG_READ(ah, AR_TSF_L32);
}

/*
 * Reset the current hardware tsf for stamlme
 */
void
ar5211ResetTsf(struct ath_hal *ah)
{
	uint32_t val = OS_REG_READ(ah, AR_BEACON);

	OS_REG_WRITE(ah, AR_BEACON, val | AR_BEACON_RESET_TSF);
}

/*
 * Grab a semi-random value from hardware registers - may not
 * change often
 */
uint32_t
ar5211GetRandomSeed(struct ath_hal *ah)
{
	uint32_t nf;

	nf = (OS_REG_READ(ah, AR_PHY(25)) >> 19) & 0x1ff;
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	return (OS_REG_READ(ah, AR_TSF_U32) ^
		OS_REG_READ(ah, AR_TSF_L32) ^ nf);
}

/*
 * Detect if our card is present
 */
HAL_BOOL
ar5211DetectCardPresent(struct ath_hal *ah)
{
	uint16_t macVersion, macRev;
	uint32_t v;

	/*
	 * Read the Silicon Revision register and compare that
	 * to what we read at attach time.  If the same, we say
	 * a card/device is present.
	 */
	v = OS_REG_READ(ah, AR_SREV) & AR_SREV_ID_M;
	macVersion = v >> AR_SREV_ID_S;
	macRev = v & AR_SREV_REVISION_M;
	return (AH_PRIVATE(ah)->ah_macVersion == macVersion &&
		AH_PRIVATE(ah)->ah_macRev == macRev);
}

/*
 * Update MIB Counters
 */
void
ar5211UpdateMibCounters(struct ath_hal *ah, HAL_MIB_STATS *stats)
{
	stats->ackrcv_bad += OS_REG_READ(ah, AR_ACK_FAIL);
	stats->rts_bad	  += OS_REG_READ(ah, AR_RTS_FAIL);
	stats->fcs_bad	  += OS_REG_READ(ah, AR_FCS_FAIL);
	stats->rts_good	  += OS_REG_READ(ah, AR_RTS_OK);
	stats->beacons	  += OS_REG_READ(ah, AR_BEACON_CNT);
}

HAL_BOOL
ar5211SetSifsTime(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	if (us > ath_hal_mac_usec(ah, 0xffff)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad SIFS time %u\n",
		    __func__, us);
		ahp->ah_sifstime = (u_int) -1;	/* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_WRITE(ah, AR_D_GBL_IFS_SIFS, ath_hal_mac_clks(ah, us));
		ahp->ah_slottime = us;
		return AH_TRUE;
	}
}

u_int
ar5211GetSifsTime(struct ath_hal *ah)
{
	u_int clks = OS_REG_READ(ah, AR_D_GBL_IFS_SIFS) & 0xffff;
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

HAL_BOOL
ar5211SetSlotTime(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	if (us < HAL_SLOT_TIME_9 || us > ath_hal_mac_usec(ah, 0xffff)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad slot time %u\n",
		    __func__, us);
		ahp->ah_slottime = us;	/* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, ath_hal_mac_clks(ah, us));
		ahp->ah_slottime = us;
		return AH_TRUE;
	}
}

u_int
ar5211GetSlotTime(struct ath_hal *ah)
{
	u_int clks = OS_REG_READ(ah, AR_D_GBL_IFS_SLOT) & 0xffff;
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

HAL_BOOL
ar5211SetAckTimeout(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	if (us > ath_hal_mac_usec(ah, MS(0xffffffff, AR_TIME_OUT_ACK))) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad ack timeout %u\n",
		    __func__, us);
		ahp->ah_acktimeout = (u_int) -1; /* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_RMW_FIELD(ah, AR_TIME_OUT,
			AR_TIME_OUT_ACK, ath_hal_mac_clks(ah, us));
		ahp->ah_acktimeout = us;
		return AH_TRUE;
	}
}

u_int
ar5211GetAckTimeout(struct ath_hal *ah)
{
	u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_ACK);
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

u_int
ar5211GetAckCTSRate(struct ath_hal *ah)
{
	return ((AH5211(ah)->ah_staId1Defaults & AR_STA_ID1_ACKCTS_6MB) == 0);
}

HAL_BOOL
ar5211SetAckCTSRate(struct ath_hal *ah, u_int high)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	if (high) {
		OS_REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_ACKCTS_6MB);
		ahp->ah_staId1Defaults &= ~AR_STA_ID1_ACKCTS_6MB;
	} else {
		OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_ACKCTS_6MB);
		ahp->ah_staId1Defaults |= AR_STA_ID1_ACKCTS_6MB;
	}
	return AH_TRUE;
}

HAL_BOOL
ar5211SetCTSTimeout(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	if (us > ath_hal_mac_usec(ah, MS(0xffffffff, AR_TIME_OUT_CTS))) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad cts timeout %u\n",
		    __func__, us);
		ahp->ah_ctstimeout = (u_int) -1; /* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_RMW_FIELD(ah, AR_TIME_OUT,
			AR_TIME_OUT_CTS, ath_hal_mac_clks(ah, us));
		ahp->ah_ctstimeout = us;
		return AH_TRUE;
	}
}

u_int
ar5211GetCTSTimeout(struct ath_hal *ah)
{
	u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_CTS);
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

HAL_BOOL
ar5211SetDecompMask(struct ath_hal *ah, uint16_t keyidx, int en)
{
	/* nothing to do */
        return AH_TRUE;
}

void
ar5211SetCoverageClass(struct ath_hal *ah, uint8_t coverageclass, int now)
{
}

HAL_STATUS
ar5211SetQuiet(struct ath_hal *ah, uint32_t period, uint32_t duration,
    uint32_t next_start, HAL_QUIET_FLAG flags)
{
	return HAL_OK;
}

/*
 * Control Adaptive Noise Immunity Parameters
 */
HAL_BOOL
ar5211AniControl(struct ath_hal *ah, HAL_ANI_CMD cmd, int param)
{
	return AH_FALSE;
}

void
ar5211AniPoll(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
}

void
ar5211RxMonitor(struct ath_hal *ah, const HAL_NODE_STATS *stats,
	const struct ieee80211_channel *chan)
{
}

void
ar5211MibEvent(struct ath_hal *ah, const HAL_NODE_STATS *stats)
{
}

/*
 * Get the rssi of frame curently being received.
 */
uint32_t
ar5211GetCurRssi(struct ath_hal *ah)
{
	return (OS_REG_READ(ah, AR_PHY_CURRENT_RSSI) & 0xff);
}

u_int
ar5211GetDefAntenna(struct ath_hal *ah)
{   
	return (OS_REG_READ(ah, AR_DEF_ANTENNA) & 0x7);
}   

void
ar5211SetDefAntenna(struct ath_hal *ah, u_int antenna)
{
	OS_REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}

HAL_ANT_SETTING
ar5211GetAntennaSwitch(struct ath_hal *ah)
{
	return AH5211(ah)->ah_diversityControl;
}

HAL_BOOL
ar5211SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING settings)
{
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;

	if (chan == AH_NULL) {
		AH5211(ah)->ah_diversityControl = settings;
		return AH_TRUE;
	}
	return ar5211SetAntennaSwitchInternal(ah, settings, chan);
}

HAL_STATUS
ar5211GetCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t *result)
{

	switch (type) {
	case HAL_CAP_CIPHER:		/* cipher handled in hardware */
		switch (capability) {
		case HAL_CIPHER_AES_OCB:
		case HAL_CIPHER_WEP:
		case HAL_CIPHER_CLR:
			return HAL_OK;
		default:
			return HAL_ENOTSUPP;
		}
	default:
		return ath_hal_getcapability(ah, type, capability, result);
	}
}

HAL_BOOL
ar5211SetCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t setting, HAL_STATUS *status)
{
	switch (type) {
	case HAL_CAP_DIAG:		/* hardware diagnostic support */
		/*
		 * NB: could split this up into virtual capabilities,
		 *     (e.g. 1 => ACK, 2 => CTS, etc.) but it hardly
		 *     seems worth the additional complexity.
		 */
#ifdef AH_DEBUG
		AH_PRIVATE(ah)->ah_diagreg = setting;
#else
		AH_PRIVATE(ah)->ah_diagreg = setting & 0x6;	/* ACK+CTS */
#endif
		OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);
		return AH_TRUE;
	default:
		return ath_hal_setcapability(ah, type, capability,
			setting, status);
	}
}

HAL_BOOL
ar5211GetDiagState(struct ath_hal *ah, int request,
	const void *args, uint32_t argsize,
	void **result, uint32_t *resultsize)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	(void) ahp;
	if (ath_hal_getdiagstate(ah, request, args, argsize, result, resultsize))
		return AH_TRUE;
	switch (request) {
	case HAL_DIAG_EEPROM:
		return ath_hal_eepromDiag(ah, request,
		    args, argsize, result, resultsize);
	case HAL_DIAG_RFGAIN:
		*result = &ahp->ah_gainValues;
		*resultsize = sizeof(GAIN_VALUES);
		return AH_TRUE;
	case HAL_DIAG_RFGAIN_CURSTEP:
		*result = __DECONST(void *, ahp->ah_gainValues.currStep);
		*resultsize = (*result == AH_NULL) ?
			0 : sizeof(GAIN_OPTIMIZATION_STEP);
		return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Return what percentage of the extension channel is busy.
 * This is always disabled for AR5211 series NICs.
 */
uint32_t
ar5211Get11nExtBusy(struct ath_hal *ah)
{
	return (0);
}


/*
 * There's no channel survey support for the AR5211.
 */
HAL_BOOL
ar5211GetMibCycleCounts(struct ath_hal *ah, HAL_SURVEY_SAMPLE *hsample)
{

	return (AH_FALSE);
}

void
ar5211SetChainMasks(struct ath_hal *ah, uint32_t txchainmask,
    uint32_t rxchainmask)
{
}

void
ar5211EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
}

void
ar5211GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
}
