/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
#include "ah_desc.h"			/* NB: for HAL_PHYERR* */

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#include "ah_eeprom_v3.h"

#define	AR_NUM_GPIO	6		/* 6 GPIO pins */
#define	AR_GPIOD_MASK	0x0000002F	/* GPIO data reg r/w mask */

void
ar5212GetMacAddress(struct ath_hal *ah, uint8_t *mac)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	OS_MEMCPY(mac, ahp->ah_macaddr, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5212SetMacAddress(struct ath_hal *ah, const uint8_t *mac)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	OS_MEMCPY(ahp->ah_macaddr, mac, IEEE80211_ADDR_LEN);
	return AH_TRUE;
}

void
ar5212GetBssIdMask(struct ath_hal *ah, uint8_t *mask)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	OS_MEMCPY(mask, ahp->ah_bssidmask, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5212SetBssIdMask(struct ath_hal *ah, const uint8_t *mask)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	/* save it since it must be rewritten on reset */
	OS_MEMCPY(ahp->ah_bssidmask, mask, IEEE80211_ADDR_LEN);

	OS_REG_WRITE(ah, AR_BSSMSKL, LE_READ_4(ahp->ah_bssidmask));
	OS_REG_WRITE(ah, AR_BSSMSKU, LE_READ_2(ahp->ah_bssidmask + 4));
	return AH_TRUE;
}

/*
 * Attempt to change the cards operating regulatory domain to the given value
 */
HAL_BOOL
ar5212SetRegulatoryDomain(struct ath_hal *ah,
	uint16_t regDomain, HAL_STATUS *status)
{
	HAL_STATUS ecode;

	if (AH_PRIVATE(ah)->ah_currentRD == regDomain) {
		ecode = HAL_EINVAL;
		goto bad;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_WRITEPROTECT)) {
		ecode = HAL_EEWRITE;
		goto bad;
	}
#ifdef AH_SUPPORT_WRITE_REGDOMAIN
	if (ath_hal_eepromWrite(ah, AR_EEPROM_REG_DOMAIN, regDomain)) {
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
 */
u_int
ar5212GetWirelessModes(struct ath_hal *ah)
{
	u_int mode = 0;

	if (ath_hal_eepromGetFlag(ah, AR_EEP_AMODE)) {
		mode = HAL_MODE_11A;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO5DISABLE))
			mode |= HAL_MODE_TURBO | HAL_MODE_108A;
		if (AH_PRIVATE(ah)->ah_caps.halChanHalfRate)
			mode |= HAL_MODE_11A_HALF_RATE;
		if (AH_PRIVATE(ah)->ah_caps.halChanQuarterRate)
			mode |= HAL_MODE_11A_QUARTER_RATE;
	}
	if (ath_hal_eepromGetFlag(ah, AR_EEP_BMODE))
		mode |= HAL_MODE_11B;
	if (ath_hal_eepromGetFlag(ah, AR_EEP_GMODE) &&
	    AH_PRIVATE(ah)->ah_subvendorid != AR_SUBVENDOR_ID_NOG) {
		mode |= HAL_MODE_11G;
		if (!ath_hal_eepromGetFlag(ah, AR_EEP_TURBO2DISABLE))
			mode |= HAL_MODE_108G;
		if (AH_PRIVATE(ah)->ah_caps.halChanHalfRate)
			mode |= HAL_MODE_11G_HALF_RATE;
		if (AH_PRIVATE(ah)->ah_caps.halChanQuarterRate)
			mode |= HAL_MODE_11G_QUARTER_RATE;
	}
	return mode;
}

/*
 * Set the interrupt and GPIO values so the ISR can disable RF
 * on a switch signal.  Assumes GPIO port and interrupt polarity
 * are set prior to call.
 */
void
ar5212EnableRfKill(struct ath_hal *ah)
{
	uint16_t rfsilent = AH_PRIVATE(ah)->ah_rfsilent;
	int select = MS(rfsilent, AR_EEPROM_RFSILENT_GPIO_SEL);
	int polarity = MS(rfsilent, AR_EEPROM_RFSILENT_POLARITY);

	/*
	 * Configure the desired GPIO port for input
	 * and enable baseband rf silence.
	 */
	ath_hal_gpioCfgInput(ah, select);
	OS_REG_SET_BIT(ah, AR_PHY(0), 0x00002000);
	/*
	 * If radio disable switch connection to GPIO bit x is enabled
	 * program GPIO interrupt.
	 * If rfkill bit on eeprom is 1, setupeeprommap routine has already
	 * verified that it is a later version of eeprom, it has a place for
	 * rfkill bit and it is set to 1, indicating that GPIO bit x hardware
	 * connection is present.
	 */
	ath_hal_gpioSetIntr(ah, select,
	    (ath_hal_gpioGet(ah, select) == polarity ? !polarity : polarity));
}

/*
 * Change the LED blinking pattern to correspond to the connectivity
 */
void
ar5212SetLedState(struct ath_hal *ah, HAL_LED_STATE state)
{
	static const uint32_t ledbits[8] = {
		AR_PCICFG_LEDCTL_NONE,	/* HAL_LED_INIT */
		AR_PCICFG_LEDCTL_PEND,	/* HAL_LED_SCAN */
		AR_PCICFG_LEDCTL_PEND,	/* HAL_LED_AUTH */
		AR_PCICFG_LEDCTL_ASSOC,	/* HAL_LED_ASSOC*/
		AR_PCICFG_LEDCTL_ASSOC,	/* HAL_LED_RUN */
		AR_PCICFG_LEDCTL_NONE,
		AR_PCICFG_LEDCTL_NONE,
		AR_PCICFG_LEDCTL_NONE,
	};
	uint32_t bits;

	bits = OS_REG_READ(ah, AR_PCICFG);
	if (IS_2417(ah)) {
		/*
		 * Enable LED for Nala. There is a bit marked reserved
		 * that must be set and we also turn on the power led.
		 * Because we mark s/w LED control setting the control
		 * status bits below is meangless (the driver must flash
		 * the LED(s) using the GPIO lines).
		 */
		bits = (bits &~ AR_PCICFG_LEDMODE)
		     | SM(AR_PCICFG_LEDMODE_POWON, AR_PCICFG_LEDMODE)
#if 0
		     | SM(AR_PCICFG_LEDMODE_NETON, AR_PCICFG_LEDMODE)
#endif
		     | 0x08000000;
	}
	bits = (bits &~ AR_PCICFG_LEDCTL)
	     | SM(ledbits[state & 0x7], AR_PCICFG_LEDCTL);
	OS_REG_WRITE(ah, AR_PCICFG, bits);
}

/*
 * Change association related fields programmed into the hardware.
 * Writing a valid BSSID to the hardware effectively enables the hardware
 * to synchronize its TSF to the correct beacons and receive frames coming
 * from that BSSID. It is called by the SME JOIN operation.
 */
void
ar5212WriteAssocid(struct ath_hal *ah, const uint8_t *bssid, uint16_t assocId)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	/* save bssid for possible re-use on reset */
	OS_MEMCPY(ahp->ah_bssid, bssid, IEEE80211_ADDR_LEN);
	ahp->ah_assocId = assocId;
	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid+4) |
				     ((assocId & 0x3fff)<<AR_BSS_ID1_AID_S));
}

/*
 * Get the current hardware tsf for stamlme
 */
uint64_t
ar5212GetTsf64(struct ath_hal *ah)
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
		 * we take this approach as it costs one less PCI read
		 * which can be noticeable when doing things like
		 * timestamping packets in monitor mode.
		 */
		u32++;
	}
	return (((uint64_t) u32) << 32) | ((uint64_t) low2);
}

/*
 * Get the current hardware tsf for stamlme
 */
uint32_t
ar5212GetTsf32(struct ath_hal *ah)
{
	return OS_REG_READ(ah, AR_TSF_L32);
}

void
ar5212SetTsf64(struct ath_hal *ah, uint64_t tsf64)
{
	OS_REG_WRITE(ah, AR_TSF_L32, tsf64 & 0xffffffff);
	OS_REG_WRITE(ah, AR_TSF_U32, (tsf64 >> 32) & 0xffffffff);
}

/*
 * Reset the current hardware tsf for stamlme.
 */
void
ar5212ResetTsf(struct ath_hal *ah)
{

	uint32_t val = OS_REG_READ(ah, AR_BEACON);

	OS_REG_WRITE(ah, AR_BEACON, val | AR_BEACON_RESET_TSF);
	/*
	 * When resetting the TSF, write twice to the
	 * corresponding register; each write to the RESET_TSF bit toggles
	 * the internal signal to cause a reset of the TSF - but if the signal
	 * is left high, it will reset the TSF on the next chip reset also!
	 * writing the bit an even number of times fixes this issue
	 */
	OS_REG_WRITE(ah, AR_BEACON, val | AR_BEACON_RESET_TSF);
}

/*
 * Set or clear hardware basic rate bit
 * Set hardware basic rate set if basic rate is found
 * and basic rate is equal or less than 2Mbps
 */
void
ar5212SetBasicRate(struct ath_hal *ah, HAL_RATE_SET *rs)
{
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	uint32_t reg;
	uint8_t xset;
	int i;

	if (chan == AH_NULL || !IEEE80211_IS_CHAN_CCK(chan))
		return;
	xset = 0;
	for (i = 0; i < rs->rs_count; i++) {
		uint8_t rset = rs->rs_rates[i];
		/* Basic rate defined? */
		if ((rset & 0x80) && (rset &= 0x7f) >= xset)
			xset = rset;
	}
	/*
	 * Set the h/w bit to reflect whether or not the basic
	 * rate is found to be equal or less than 2Mbps.
	 */
	reg = OS_REG_READ(ah, AR_STA_ID1);
	if (xset && xset/2 <= 2)
		OS_REG_WRITE(ah, AR_STA_ID1, reg | AR_STA_ID1_BASE_RATE_11B);
	else
		OS_REG_WRITE(ah, AR_STA_ID1, reg &~ AR_STA_ID1_BASE_RATE_11B);
}

/*
 * Grab a semi-random value from hardware registers - may not
 * change often
 */
uint32_t
ar5212GetRandomSeed(struct ath_hal *ah)
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
ar5212DetectCardPresent(struct ath_hal *ah)
{
	uint16_t macVersion, macRev;
	uint32_t v;

	/*
	 * Read the Silicon Revision register and compare that
	 * to what we read at attach time.  If the same, we say
	 * a card/device is present.
	 */
	v = OS_REG_READ(ah, AR_SREV) & AR_SREV_ID;
	macVersion = v >> AR_SREV_ID_S;
	macRev = v & AR_SREV_REVISION;
	return (AH_PRIVATE(ah)->ah_macVersion == macVersion &&
		AH_PRIVATE(ah)->ah_macRev == macRev);
}

void
ar5212EnableMibCounters(struct ath_hal *ah)
{
	/* NB: this just resets the mib counter machinery */
	OS_REG_WRITE(ah, AR_MIBC,
	    ~(AR_MIBC_COW | AR_MIBC_FMC | AR_MIBC_CMC | AR_MIBC_MCS) & 0x0f);
}

void 
ar5212DisableMibCounters(struct ath_hal *ah)
{
	OS_REG_WRITE(ah, AR_MIBC,  AR_MIBC | AR_MIBC_CMC);
}

/*
 * Update MIB Counters
 */
void
ar5212UpdateMibCounters(struct ath_hal *ah, HAL_MIB_STATS* stats)
{
	stats->ackrcv_bad += OS_REG_READ(ah, AR_ACK_FAIL);
	stats->rts_bad	  += OS_REG_READ(ah, AR_RTS_FAIL);
	stats->fcs_bad	  += OS_REG_READ(ah, AR_FCS_FAIL);
	stats->rts_good	  += OS_REG_READ(ah, AR_RTS_OK);
	stats->beacons	  += OS_REG_READ(ah, AR_BEACON_CNT);
}

/*
 * Detect if the HW supports spreading a CCK signal on channel 14
 */
HAL_BOOL
ar5212IsJapanChannelSpreadSupported(struct ath_hal *ah)
{
	return AH_TRUE;
}

/*
 * Get the rssi of frame curently being received.
 */
uint32_t
ar5212GetCurRssi(struct ath_hal *ah)
{
	return (OS_REG_READ(ah, AR_PHY_CURRENT_RSSI) & 0xff);
}

u_int
ar5212GetDefAntenna(struct ath_hal *ah)
{   
	return (OS_REG_READ(ah, AR_DEF_ANTENNA) & 0x7);
}   

void
ar5212SetDefAntenna(struct ath_hal *ah, u_int antenna)
{
	OS_REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}

HAL_ANT_SETTING
ar5212GetAntennaSwitch(struct ath_hal *ah)
{
	return AH5212(ah)->ah_antControl;
}

HAL_BOOL
ar5212SetAntennaSwitch(struct ath_hal *ah, HAL_ANT_SETTING setting)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;

	if (!ahp->ah_phyPowerOn || chan == AH_NULL) {
		/* PHY powered off, just stash settings */
		ahp->ah_antControl = setting;
		ahp->ah_diversity = (setting == HAL_ANT_VARIABLE);
		return AH_TRUE;
	}
	return ar5212SetAntennaSwitchInternal(ah, setting, chan);
}

HAL_BOOL
ar5212IsSleepAfterBeaconBroken(struct ath_hal *ah)
{
	return AH_TRUE;
}

HAL_BOOL
ar5212SetSifsTime(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	if (us > ath_hal_mac_usec(ah, 0xffff)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad SIFS time %u\n",
		    __func__, us);
		ahp->ah_sifstime = (u_int) -1;	/* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_WRITE(ah, AR_D_GBL_IFS_SIFS, ath_hal_mac_clks(ah, us-2));
		ahp->ah_sifstime = us;
		return AH_TRUE;
	}
}

u_int
ar5212GetSifsTime(struct ath_hal *ah)
{
	u_int clks = OS_REG_READ(ah, AR_D_GBL_IFS_SIFS) & 0xffff;
	return ath_hal_mac_usec(ah, clks)+2;	/* convert from system clocks */
}

HAL_BOOL
ar5212SetSlotTime(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	if (us < HAL_SLOT_TIME_6 || us > ath_hal_mac_usec(ah, 0xffff)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad slot time %u\n",
		    __func__, us);
		ahp->ah_slottime = (u_int) -1;	/* restore default handling */
		return AH_FALSE;
	} else {
		/* convert to system clocks */
		OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, ath_hal_mac_clks(ah, us));
		ahp->ah_slottime = us;
		return AH_TRUE;
	}
}

u_int
ar5212GetSlotTime(struct ath_hal *ah)
{
	u_int clks = OS_REG_READ(ah, AR_D_GBL_IFS_SLOT) & 0xffff;
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

HAL_BOOL
ar5212SetAckTimeout(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

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
ar5212GetAckTimeout(struct ath_hal *ah)
{
	u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_ACK);
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

u_int
ar5212GetAckCTSRate(struct ath_hal *ah)
{
	return ((AH5212(ah)->ah_staId1Defaults & AR_STA_ID1_ACKCTS_6MB) == 0);
}

HAL_BOOL
ar5212SetAckCTSRate(struct ath_hal *ah, u_int high)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

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
ar5212SetCTSTimeout(struct ath_hal *ah, u_int us)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

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
ar5212GetCTSTimeout(struct ath_hal *ah)
{
	u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_CTS);
	return ath_hal_mac_usec(ah, clks);	/* convert from system clocks */
}

/* Setup decompression for given key index */
HAL_BOOL
ar5212SetDecompMask(struct ath_hal *ah, uint16_t keyidx, int en)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

        if (keyidx >= HAL_DECOMP_MASK_SIZE)
                return AH_FALSE;
        OS_REG_WRITE(ah, AR_DCM_A, keyidx);
        OS_REG_WRITE(ah, AR_DCM_D, en ? AR_DCM_D_EN : 0);
        ahp->ah_decompMask[keyidx] = en;

        return AH_TRUE;
}

/* Setup coverage class */
void
ar5212SetCoverageClass(struct ath_hal *ah, uint8_t coverageclass, int now)
{
	uint32_t slot, timeout, eifs;
	u_int clkRate;

	AH_PRIVATE(ah)->ah_coverageClass = coverageclass;

	if (now) {
		if (AH_PRIVATE(ah)->ah_coverageClass == 0)
			return;

		/* Don't apply coverage class to non A channels */
		if (!IEEE80211_IS_CHAN_A(AH_PRIVATE(ah)->ah_curchan))
			return;

		/* Get core clock rate */
		clkRate = ath_hal_mac_clks(ah, 1);

		/* Compute EIFS */
		slot = coverageclass * 3 * clkRate;
		eifs = coverageclass * 6 * clkRate;
		if (IEEE80211_IS_CHAN_HALF(AH_PRIVATE(ah)->ah_curchan)) {
			slot += IFS_SLOT_HALF_RATE;
			eifs += IFS_EIFS_HALF_RATE;
		} else if (IEEE80211_IS_CHAN_QUARTER(AH_PRIVATE(ah)->ah_curchan)) {
			slot += IFS_SLOT_QUARTER_RATE;
			eifs += IFS_EIFS_QUARTER_RATE;
		} else { /* full rate */
			slot += IFS_SLOT_FULL_RATE;
			eifs += IFS_EIFS_FULL_RATE;
		}

		/*
		 * Add additional time for air propagation for ACK and CTS
		 * timeouts. This value is in core clocks.
  		 */
		timeout = ACK_CTS_TIMEOUT_11A + (coverageclass * 3 * clkRate);
	
		/*
		 * Write the values: slot, eifs, ack/cts timeouts.
		 */
		OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, slot);
		OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, eifs);
		OS_REG_WRITE(ah, AR_TIME_OUT,
			  SM(timeout, AR_TIME_OUT_CTS)
			| SM(timeout, AR_TIME_OUT_ACK));
	}
}

HAL_STATUS
ar5212SetQuiet(struct ath_hal *ah, uint32_t period, uint32_t duration,
    uint32_t nextStart, HAL_QUIET_FLAG flag)
{
	OS_REG_WRITE(ah, AR_QUIET2, period | (duration << AR_QUIET2_QUIET_DUR_S));
	if (flag & HAL_QUIET_ENABLE) {
		OS_REG_WRITE(ah, AR_QUIET1, nextStart | (1 << 16));
	}
	else {
		OS_REG_WRITE(ah, AR_QUIET1, nextStart);
	}
	return HAL_OK;
}

void
ar5212SetPCUConfig(struct ath_hal *ah)
{
	ar5212SetOperatingMode(ah, AH_PRIVATE(ah)->ah_opmode);
}

/*
 * Return whether an external 32KHz crystal should be used
 * to reduce power consumption when sleeping.  We do so if
 * the crystal is present (obtained from EEPROM) and if we
 * are not running as an AP and are configured to use it.
 */
HAL_BOOL
ar5212Use32KHzclock(struct ath_hal *ah, HAL_OPMODE opmode)
{
	if (opmode != HAL_M_HOSTAP) {
		struct ath_hal_5212 *ahp = AH5212(ah);
		return ath_hal_eepromGetFlag(ah, AR_EEP_32KHZCRYSTAL) &&
		       (ahp->ah_enable32kHzClock == USE_32KHZ ||
		        ahp->ah_enable32kHzClock == AUTO_32KHZ);
	} else
		return AH_FALSE;
}

/*
 * If 32KHz clock exists, use it to lower power consumption during sleep
 *
 * Note: If clock is set to 32 KHz, delays on accessing certain
 *       baseband registers (27-31, 124-127) are required.
 */
void
ar5212SetupClock(struct ath_hal *ah, HAL_OPMODE opmode)
{
	if (ar5212Use32KHzclock(ah, opmode)) {
		/*
		 * Enable clocks to be turned OFF in BB during sleep
		 * and also enable turning OFF 32MHz/40MHz Refclk
		 * from A2.
		 */
		OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_CONTROL, 0x1f);
		OS_REG_WRITE(ah, AR_PHY_REFCLKPD,
		    IS_RAD5112_ANY(ah) || IS_5413(ah) ? 0x14 : 0x18);
		OS_REG_RMW_FIELD(ah, AR_USEC, AR_USEC_USEC32, 1);
		OS_REG_WRITE(ah, AR_TSF_PARM, 61);  /* 32 KHz TSF incr */
		OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_SEL, 1);

		if (IS_2413(ah) || IS_5413(ah) || IS_2417(ah)) {
			OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_LIMIT,   0x26);
			OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL,        0x0d);
			OS_REG_WRITE(ah, AR_PHY_M_SLEEP,           0x07);
			OS_REG_WRITE(ah, AR_PHY_REFCLKDLY,         0x3f);
			/* # Set sleep clock rate to 32 KHz. */
			OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_RATE_IND, 0x2);
		} else {
			OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_LIMIT,   0x0a);
			OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL,        0x0c);
			OS_REG_WRITE(ah, AR_PHY_M_SLEEP,           0x03);
			OS_REG_WRITE(ah, AR_PHY_REFCLKDLY,         0x20);
			OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_RATE_IND, 0x3);
		}
	} else {
		OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_RATE_IND, 0x0);
		OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_SEL, 0);

		OS_REG_WRITE(ah, AR_TSF_PARM, 1);	/* 32MHz TSF inc */

		OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_CONTROL, 0x1f);
		OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_LIMIT,   0x7f);

		if (IS_2417(ah))
			OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x0a);
		else if (IS_HB63(ah))
			OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x32);
		else
			OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x0e);
		OS_REG_WRITE(ah, AR_PHY_M_SLEEP,           0x0c);
		OS_REG_WRITE(ah, AR_PHY_REFCLKDLY,         0xff);
		OS_REG_WRITE(ah, AR_PHY_REFCLKPD,
		    IS_RAD5112_ANY(ah) || IS_5413(ah) || IS_2417(ah) ? 0x14 : 0x18);
		OS_REG_RMW_FIELD(ah, AR_USEC, AR_USEC_USEC32,
		    IS_RAD5112_ANY(ah) || IS_5413(ah) ? 39 : 31);
	}
}

/*
 * If 32KHz clock exists, turn it off and turn back on the 32Mhz
 */
void
ar5212RestoreClock(struct ath_hal *ah, HAL_OPMODE opmode)
{
	if (ar5212Use32KHzclock(ah, opmode)) {
		/* # Set sleep clock rate back to 32 MHz. */
		OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_RATE_IND, 0);
		OS_REG_RMW_FIELD(ah, AR_PCICFG, AR_PCICFG_SCLK_SEL, 0);

		OS_REG_WRITE(ah, AR_TSF_PARM, 1);	/* 32 MHz TSF incr */
		OS_REG_RMW_FIELD(ah, AR_USEC, AR_USEC_USEC32,
		    IS_RAD5112_ANY(ah) || IS_5413(ah) ? 39 : 31);

		/*
		 * Restore BB registers to power-on defaults
		 */
		OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_CONTROL, 0x1f);
		OS_REG_WRITE(ah, AR_PHY_SLEEP_CTR_LIMIT,   0x7f);
		OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL,        0x0e);
		OS_REG_WRITE(ah, AR_PHY_M_SLEEP,           0x0c);
		OS_REG_WRITE(ah, AR_PHY_REFCLKDLY,         0xff);
		OS_REG_WRITE(ah, AR_PHY_REFCLKPD,
		    IS_RAD5112_ANY(ah) || IS_5413(ah) ?  0x14 : 0x18);
	}
}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 * Default method: this may be overridden by the rf backend.
 */
int16_t
ar5212GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	static const struct {
		uint16_t freqLow;
		int16_t	  adjust;
	} adjustDef[] = {
		{ 5790,	11 },	/* NB: ordered high -> low */
		{ 5730, 10 },
		{ 5690,  9 },
		{ 5660,  8 },
		{ 5610,  7 },
		{ 5530,  5 },
		{ 5450,  4 },
		{ 5379,  2 },
		{ 5209,  0 },
		{ 3000,  1 },
		{    0,  0 },
	};
	int i;

	for (i = 0; c->channel <= adjustDef[i].freqLow; i++)
		;
	return adjustDef[i].adjust;
}

HAL_STATUS
ar5212GetCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t *result)
{
#define	MACVERSION(ah)	AH_PRIVATE(ah)->ah_macVersion
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
	const struct ar5212AniState *ani;

	switch (type) {
	case HAL_CAP_CIPHER:		/* cipher handled in hardware */
		switch (capability) {
		case HAL_CIPHER_AES_CCM:
			return pCap->halCipherAesCcmSupport ?
				HAL_OK : HAL_ENOTSUPP;
		case HAL_CIPHER_AES_OCB:
		case HAL_CIPHER_TKIP:
		case HAL_CIPHER_WEP:
		case HAL_CIPHER_MIC:
		case HAL_CIPHER_CLR:
			return HAL_OK;
		default:
			return HAL_ENOTSUPP;
		}
	case HAL_CAP_TKIP_MIC:		/* handle TKIP MIC in hardware */
		switch (capability) {
		case 0:			/* hardware capability */
			return HAL_OK;
		case 1:
			return (ahp->ah_staId1Defaults &
			    AR_STA_ID1_CRPT_MIC_ENABLE) ?  HAL_OK : HAL_ENXIO;
		}
		return HAL_EINVAL;
	case HAL_CAP_TKIP_SPLIT:	/* hardware TKIP uses split keys */
		switch (capability) {
		case 0:			/* hardware capability */
			return pCap->halTkipMicTxRxKeySupport ?
				HAL_ENXIO : HAL_OK;
		case 1:			/* current setting */
			return (ahp->ah_miscMode &
			    AR_MISC_MODE_MIC_NEW_LOC_ENABLE) ? HAL_ENXIO : HAL_OK;
		}
		return HAL_EINVAL;
	case HAL_CAP_WME_TKIPMIC:	/* hardware can do TKIP MIC w/ WMM */
		/* XXX move to capability bit */
		return MACVERSION(ah) > AR_SREV_VERSION_VENICE ||
		    (MACVERSION(ah) == AR_SREV_VERSION_VENICE &&
		     AH_PRIVATE(ah)->ah_macRev >= 8) ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_DIVERSITY:		/* hardware supports fast diversity */
		switch (capability) {
		case 0:			/* hardware capability */
			return HAL_OK;
		case 1:			/* current setting */
			return ahp->ah_diversity ? HAL_OK : HAL_ENXIO;
		case HAL_CAP_STRONG_DIV:
			*result = OS_REG_READ(ah, AR_PHY_RESTART);
			*result = MS(*result, AR_PHY_RESTART_DIV_GC);
			return HAL_OK;
		}
		return HAL_EINVAL;
	case HAL_CAP_DIAG:
		*result = AH_PRIVATE(ah)->ah_diagreg;
		return HAL_OK;
	case HAL_CAP_TPC:
		switch (capability) {
		case 0:			/* hardware capability */
			return HAL_OK;
		case 1:
			return ahp->ah_tpcEnabled ? HAL_OK : HAL_ENXIO;
		}
		return HAL_OK;
	case HAL_CAP_PHYDIAG:		/* radar pulse detection capability */
		switch (capability) {
		case HAL_CAP_RADAR:
			return ath_hal_eepromGetFlag(ah, AR_EEP_AMODE) ?
			    HAL_OK: HAL_ENXIO;
		case HAL_CAP_AR:
			return (ath_hal_eepromGetFlag(ah, AR_EEP_GMODE) ||
			    ath_hal_eepromGetFlag(ah, AR_EEP_BMODE)) ?
			       HAL_OK: HAL_ENXIO;
		}
		return HAL_ENXIO;
	case HAL_CAP_MCAST_KEYSRCH:	/* multicast frame keycache search */
		switch (capability) {
		case 0:			/* hardware capability */
			return pCap->halMcastKeySrchSupport ? HAL_OK : HAL_ENXIO;
		case 1:
			return (ahp->ah_staId1Defaults &
			    AR_STA_ID1_MCAST_KSRCH) ? HAL_OK : HAL_ENXIO;
		}
		return HAL_EINVAL;
	case HAL_CAP_TSF_ADJUST:	/* hardware has beacon tsf adjust */
		switch (capability) {
		case 0:			/* hardware capability */
			return pCap->halTsfAddSupport ? HAL_OK : HAL_ENOTSUPP;
		case 1:
			return (ahp->ah_miscMode & AR_MISC_MODE_TX_ADD_TSF) ?
				HAL_OK : HAL_ENXIO;
		}
		return HAL_EINVAL;
	case HAL_CAP_TPC_ACK:
		*result = MS(ahp->ah_macTPC, AR_TPC_ACK);
		return HAL_OK;
	case HAL_CAP_TPC_CTS:
		*result = MS(ahp->ah_macTPC, AR_TPC_CTS);
		return HAL_OK;
	case HAL_CAP_INTMIT:		/* interference mitigation */
		switch (capability) {
		case HAL_CAP_INTMIT_PRESENT:		/* hardware capability */
			return HAL_OK;
		case HAL_CAP_INTMIT_ENABLE:
			return (ahp->ah_procPhyErr & HAL_ANI_ENA) ?
				HAL_OK : HAL_ENXIO;
		case HAL_CAP_INTMIT_NOISE_IMMUNITY_LEVEL:
		case HAL_CAP_INTMIT_OFDM_WEAK_SIGNAL_LEVEL:
		case HAL_CAP_INTMIT_CCK_WEAK_SIGNAL_THR:
		case HAL_CAP_INTMIT_FIRSTEP_LEVEL:
		case HAL_CAP_INTMIT_SPUR_IMMUNITY_LEVEL:
			ani = ar5212AniGetCurrentState(ah);
			if (ani == AH_NULL)
				return HAL_ENXIO;
			switch (capability) {
			case 2:	*result = ani->noiseImmunityLevel; break;
			case 3: *result = !ani->ofdmWeakSigDetectOff; break;
			case 4: *result = ani->cckWeakSigThreshold; break;
			case 5: *result = ani->firstepLevel; break;
			case 6: *result = ani->spurImmunityLevel; break;
			}
			return HAL_OK;
		}
		return HAL_EINVAL;
	default:
		return ath_hal_getcapability(ah, type, capability, result);
	}
#undef MACVERSION
}

HAL_BOOL
ar5212SetCapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t setting, HAL_STATUS *status)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
	uint32_t v;

	switch (type) {
	case HAL_CAP_TKIP_MIC:		/* handle TKIP MIC in hardware */
		if (setting)
			ahp->ah_staId1Defaults |= AR_STA_ID1_CRPT_MIC_ENABLE;
		else
			ahp->ah_staId1Defaults &= ~AR_STA_ID1_CRPT_MIC_ENABLE;
		return AH_TRUE;
	case HAL_CAP_TKIP_SPLIT:	/* hardware TKIP uses split keys */
		if (!pCap->halTkipMicTxRxKeySupport)
			return AH_FALSE;
		/* NB: true =>'s use split key cache layout */
		if (setting)
			ahp->ah_miscMode &= ~AR_MISC_MODE_MIC_NEW_LOC_ENABLE;
		else
			ahp->ah_miscMode |= AR_MISC_MODE_MIC_NEW_LOC_ENABLE;
		/* NB: write here so keys can be setup w/o a reset */
		OS_REG_WRITE(ah, AR_MISC_MODE, OS_REG_READ(ah, AR_MISC_MODE) | ahp->ah_miscMode);
		return AH_TRUE;
	case HAL_CAP_DIVERSITY:
		switch (capability) {
		case 0:
			return AH_FALSE;
		case 1:	/* setting */
			if (ahp->ah_phyPowerOn) {
				if (capability == HAL_CAP_STRONG_DIV) {
					v = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
					if (setting)
						v |= AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
					else
						v &= ~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
					OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, v);
				}
			}
			ahp->ah_diversity = (setting != 0);
			return AH_TRUE;

		case HAL_CAP_STRONG_DIV:
			if (! ahp->ah_phyPowerOn)
				return AH_FALSE;
			v = OS_REG_READ(ah, AR_PHY_RESTART);
			v &= ~AR_PHY_RESTART_DIV_GC;
			v |= SM(setting, AR_PHY_RESTART_DIV_GC);
			OS_REG_WRITE(ah, AR_PHY_RESTART, v);
			return AH_TRUE;
		default:
			return AH_FALSE;
		}
	case HAL_CAP_DIAG:		/* hardware diagnostic support */
		/*
		 * NB: could split this up into virtual capabilities,
		 *     (e.g. 1 => ACK, 2 => CTS, etc.) but it hardly
		 *     seems worth the additional complexity.
		 */
		AH_PRIVATE(ah)->ah_diagreg = setting;
		OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);
		return AH_TRUE;
	case HAL_CAP_TPC:
		ahp->ah_tpcEnabled = (setting != 0);
		return AH_TRUE;
	case HAL_CAP_MCAST_KEYSRCH:	/* multicast frame keycache search */
		if (setting)
			ahp->ah_staId1Defaults |= AR_STA_ID1_MCAST_KSRCH;
		else
			ahp->ah_staId1Defaults &= ~AR_STA_ID1_MCAST_KSRCH;
		return AH_TRUE;
	case HAL_CAP_TPC_ACK:
	case HAL_CAP_TPC_CTS:
		setting += ahp->ah_txPowerIndexOffset;
		if (setting > 63)
			setting = 63;
		if (type == HAL_CAP_TPC_ACK) {
			ahp->ah_macTPC &= AR_TPC_ACK;
			ahp->ah_macTPC |= MS(setting, AR_TPC_ACK);
		} else {
			ahp->ah_macTPC &= AR_TPC_CTS;
			ahp->ah_macTPC |= MS(setting, AR_TPC_CTS);
		}
		OS_REG_WRITE(ah, AR_TPC, ahp->ah_macTPC);
		return AH_TRUE;
	case HAL_CAP_INTMIT: {		/* interference mitigation */
		/* This maps the public ANI commands to the internal ANI commands */
		/* Private: HAL_ANI_CMD; Public: HAL_CAP_INTMIT_CMD */
		static const HAL_ANI_CMD cmds[] = {
			HAL_ANI_PRESENT,
			HAL_ANI_MODE,
			HAL_ANI_NOISE_IMMUNITY_LEVEL,
			HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
			HAL_ANI_CCK_WEAK_SIGNAL_THR,
			HAL_ANI_FIRSTEP_LEVEL,
			HAL_ANI_SPUR_IMMUNITY_LEVEL,
		};
		return capability < N(cmds) ?
			AH5212(ah)->ah_aniControl(ah, cmds[capability], setting) :
			AH_FALSE;
	}
	case HAL_CAP_TSF_ADJUST:	/* hardware has beacon tsf adjust */
		if (pCap->halTsfAddSupport) {
			if (setting)
				ahp->ah_miscMode |= AR_MISC_MODE_TX_ADD_TSF;
			else
				ahp->ah_miscMode &= ~AR_MISC_MODE_TX_ADD_TSF;
			return AH_TRUE;
		}
		/* fall thru... */
	default:
		return ath_hal_setcapability(ah, type, capability,
				setting, status);
	}
#undef N
}

HAL_BOOL
ar5212GetDiagState(struct ath_hal *ah, int request,
	const void *args, uint32_t argsize,
	void **result, uint32_t *resultsize)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_ANI_STATS *astats;

	(void) ahp;
	if (ath_hal_getdiagstate(ah, request, args, argsize, result, resultsize))
		return AH_TRUE;
	switch (request) {
	case HAL_DIAG_EEPROM:
	case HAL_DIAG_EEPROM_EXP_11A:
	case HAL_DIAG_EEPROM_EXP_11B:
	case HAL_DIAG_EEPROM_EXP_11G:
	case HAL_DIAG_RFGAIN:
		return ath_hal_eepromDiag(ah, request,
		    args, argsize, result, resultsize);
	case HAL_DIAG_RFGAIN_CURSTEP:
		*result = __DECONST(void *, ahp->ah_gainValues.currStep);
		*resultsize = (*result == AH_NULL) ?
			0 : sizeof(GAIN_OPTIMIZATION_STEP);
		return AH_TRUE;
	case HAL_DIAG_PCDAC:
		*result = ahp->ah_pcdacTable;
		*resultsize = ahp->ah_pcdacTableSize;
		return AH_TRUE;
	case HAL_DIAG_TXRATES:
		*result = &ahp->ah_ratesArray[0];
		*resultsize = sizeof(ahp->ah_ratesArray);
		return AH_TRUE;
	case HAL_DIAG_ANI_CURRENT:
		*result = ar5212AniGetCurrentState(ah);
		*resultsize = (*result == AH_NULL) ?
			0 : sizeof(struct ar5212AniState);
		return AH_TRUE;
	case HAL_DIAG_ANI_STATS:
		OS_MEMZERO(&ahp->ext_ani_stats, sizeof(ahp->ext_ani_stats));
		astats = ar5212AniGetCurrentStats(ah);
		if (astats == NULL) {
			*result = NULL;
			*resultsize = 0;
		} else {
			OS_MEMCPY(&ahp->ext_ani_stats, astats, sizeof(HAL_ANI_STATS));
			*result = &ahp->ext_ani_stats;
			*resultsize = sizeof(ahp->ext_ani_stats);
		}
		return AH_TRUE;
	case HAL_DIAG_ANI_CMD:
		if (argsize != 2*sizeof(uint32_t))
			return AH_FALSE;
		AH5212(ah)->ah_aniControl(ah, ((const uint32_t *)args)[0],
			((const uint32_t *)args)[1]);
		return AH_TRUE;
	case HAL_DIAG_ANI_PARAMS:
		/*
		 * NB: We assume struct ar5212AniParams is identical
		 * to HAL_ANI_PARAMS; if they diverge then we'll need
		 * to handle it here
		 */
		if (argsize == 0 && args == AH_NULL) {
			struct ar5212AniState *aniState =
			    ar5212AniGetCurrentState(ah);
			if (aniState == AH_NULL)
				return AH_FALSE;
			*result = __DECONST(void *, aniState->params);
			*resultsize = sizeof(struct ar5212AniParams);
			return AH_TRUE;
		} else {
			if (argsize != sizeof(struct ar5212AniParams))
				return AH_FALSE;
			return ar5212AniSetParams(ah, args, args);
		}
		break;
	}
	return AH_FALSE;
}

/*
 * Check whether there's an in-progress NF completion.
 *
 * Returns AH_TRUE if there's a in-progress NF calibration, AH_FALSE
 * otherwise.
 */
HAL_BOOL
ar5212IsNFCalInProgress(struct ath_hal *ah)
{
	if (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF)
		return AH_TRUE;
	return AH_FALSE;
}

/*
 * Wait for an in-progress NF calibration to complete.
 *
 * The completion function waits "i" times 10uS.
 * It returns AH_TRUE if the NF calibration completed (or was never
 * in progress); AH_FALSE if it was still in progress after "i" checks.
 */
HAL_BOOL
ar5212WaitNFCalComplete(struct ath_hal *ah, int i)
{
	int j;
	if (i <= 0)
		i = 1;	  /* it should run at least once */
	for (j = 0; j < i; j++) {
		if (! ar5212IsNFCalInProgress(ah))
			return AH_TRUE;
		OS_DELAY(10);
	}
	return AH_FALSE;
}

void
ar5212EnableDfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
	uint32_t val;
	val = OS_REG_READ(ah, AR_PHY_RADAR_0);

	if (pe->pe_firpwr != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_FIRPWR;
		val |= SM(pe->pe_firpwr, AR_PHY_RADAR_0_FIRPWR);
	}
	if (pe->pe_rrssi != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_RRSSI;
		val |= SM(pe->pe_rrssi, AR_PHY_RADAR_0_RRSSI);
	}
	if (pe->pe_height != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_HEIGHT;
		val |= SM(pe->pe_height, AR_PHY_RADAR_0_HEIGHT);
	}
	if (pe->pe_prssi != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_PRSSI;
		val |= SM(pe->pe_prssi, AR_PHY_RADAR_0_PRSSI);
	}
	if (pe->pe_inband != HAL_PHYERR_PARAM_NOVAL) {
		val &= ~AR_PHY_RADAR_0_INBAND;
		val |= SM(pe->pe_inband, AR_PHY_RADAR_0_INBAND);
	}
	if (pe->pe_enabled)
		val |= AR_PHY_RADAR_0_ENA;
	else
		val &= ~ AR_PHY_RADAR_0_ENA;

	if (IS_5413(ah)) {

		if (pe->pe_blockradar == 1)
			OS_REG_SET_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_BLOCKOFDMWEAK);
		else
			OS_REG_CLR_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_BLOCKOFDMWEAK);

		if (pe->pe_en_relstep_check == 1)
			OS_REG_SET_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENRELSTEPCHK);
		else
			OS_REG_CLR_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENRELSTEPCHK);

		if (pe->pe_usefir128 == 1)
			OS_REG_SET_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_USEFIR128);
		else
			OS_REG_CLR_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_USEFIR128);

		if (pe->pe_enmaxrssi == 1)
			OS_REG_SET_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENMAXRSSI);
		else
			OS_REG_CLR_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENMAXRSSI);

		if (pe->pe_enrelpwr == 1)
			OS_REG_SET_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENRELPWRCHK);
		else
			OS_REG_CLR_BIT(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_ENRELPWRCHK);

		if (pe->pe_relpwr != HAL_PHYERR_PARAM_NOVAL)
			OS_REG_RMW_FIELD(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_RELPWR, pe->pe_relpwr);

		if (pe->pe_relstep != HAL_PHYERR_PARAM_NOVAL)
			OS_REG_RMW_FIELD(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_RELSTEP, pe->pe_relstep);

		if (pe->pe_maxlen != HAL_PHYERR_PARAM_NOVAL)
			OS_REG_RMW_FIELD(ah, AR_PHY_RADAR_2,
			    AR_PHY_RADAR_2_MAXLEN, pe->pe_maxlen);
	}

	OS_REG_WRITE(ah, AR_PHY_RADAR_0, val);
}

/*
 * Parameters for the AR5212 PHY.
 */
#define	AR5212_DFS_FIRPWR	-35
#define	AR5212_DFS_RRSSI	20
#define	AR5212_DFS_HEIGHT	14
#define	AR5212_DFS_PRSSI	6
#define	AR5212_DFS_INBAND	4

/*
 * Default parameters for the AR5413 PHY.
 */
#define	AR5413_DFS_FIRPWR	-34
#define	AR5413_DFS_RRSSI	20
#define	AR5413_DFS_HEIGHT	10
#define	AR5413_DFS_PRSSI	15
#define	AR5413_DFS_INBAND	6
#define	AR5413_DFS_RELPWR	8
#define	AR5413_DFS_RELSTEP	31
#define	AR5413_DFS_MAXLEN	255

HAL_BOOL
ar5212GetDfsDefaultThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{

	if (IS_5413(ah)) {
		pe->pe_firpwr = AR5413_DFS_FIRPWR;
		pe->pe_rrssi = AR5413_DFS_RRSSI;
		pe->pe_height = AR5413_DFS_HEIGHT;
		pe->pe_prssi = AR5413_DFS_PRSSI;
		pe->pe_inband = AR5413_DFS_INBAND;
		pe->pe_relpwr = AR5413_DFS_RELPWR;
		pe->pe_relstep = AR5413_DFS_RELSTEP;
		pe->pe_maxlen = AR5413_DFS_MAXLEN;
		pe->pe_usefir128 = 0;
		pe->pe_blockradar = 1;
		pe->pe_enmaxrssi = 1;
		pe->pe_enrelpwr = 1;
		pe->pe_en_relstep_check = 0;
	} else {
		pe->pe_firpwr = AR5212_DFS_FIRPWR;
		pe->pe_rrssi = AR5212_DFS_RRSSI;
		pe->pe_height = AR5212_DFS_HEIGHT;
		pe->pe_prssi = AR5212_DFS_PRSSI;
		pe->pe_inband = AR5212_DFS_INBAND;
		pe->pe_relpwr = 0;
		pe->pe_relstep = 0;
		pe->pe_maxlen = 0;
		pe->pe_usefir128 = 0;
		pe->pe_blockradar = 0;
		pe->pe_enmaxrssi = 0;
		pe->pe_enrelpwr = 0;
		pe->pe_en_relstep_check = 0;
	}

	return (AH_TRUE);
}

void
ar5212GetDfsThresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe)
{
	uint32_t val,temp;

	val = OS_REG_READ(ah, AR_PHY_RADAR_0);

	temp = MS(val,AR_PHY_RADAR_0_FIRPWR);
	temp |= 0xFFFFFF80;
	pe->pe_firpwr = temp;
	pe->pe_rrssi = MS(val, AR_PHY_RADAR_0_RRSSI);
	pe->pe_height =  MS(val, AR_PHY_RADAR_0_HEIGHT);
	pe->pe_prssi = MS(val, AR_PHY_RADAR_0_PRSSI);
	pe->pe_inband = MS(val, AR_PHY_RADAR_0_INBAND);
	pe->pe_enabled = !! (val & AR_PHY_RADAR_0_ENA);

	pe->pe_relpwr = 0;
	pe->pe_relstep = 0;
	pe->pe_maxlen = 0;
	pe->pe_usefir128 = 0;
	pe->pe_blockradar = 0;
	pe->pe_enmaxrssi = 0;
	pe->pe_enrelpwr = 0;
	pe->pe_en_relstep_check = 0;
	pe->pe_extchannel = AH_FALSE;

	if (IS_5413(ah)) {
		val = OS_REG_READ(ah, AR_PHY_RADAR_2);
		pe->pe_relpwr = !! MS(val, AR_PHY_RADAR_2_RELPWR);
		pe->pe_relstep = !! MS(val, AR_PHY_RADAR_2_RELSTEP);
		pe->pe_maxlen = !! MS(val, AR_PHY_RADAR_2_MAXLEN);

		pe->pe_usefir128 = !! (val & AR_PHY_RADAR_2_USEFIR128);
		pe->pe_blockradar = !! (val & AR_PHY_RADAR_2_BLOCKOFDMWEAK);
		pe->pe_enmaxrssi = !! (val & AR_PHY_RADAR_2_ENMAXRSSI);
		pe->pe_enrelpwr = !! (val & AR_PHY_RADAR_2_ENRELPWRCHK);
		pe->pe_en_relstep_check =
		    !! (val & AR_PHY_RADAR_2_ENRELSTEPCHK);
	}
}

/*
 * Process the radar phy error and extract the pulse duration.
 */
HAL_BOOL
ar5212ProcessRadarEvent(struct ath_hal *ah, struct ath_rx_status *rxs,
    uint64_t fulltsf, const char *buf, HAL_DFS_EVENT *event)
{
	uint8_t dur;
	uint8_t rssi;

	/* Check whether the given phy error is a radar event */
	if ((rxs->rs_phyerr != HAL_PHYERR_RADAR) &&
	    (rxs->rs_phyerr != HAL_PHYERR_FALSE_RADAR_EXT))
		return AH_FALSE;

	/*
	 * The first byte is the pulse width - if there's
	 * no data, simply set the duration to 0
	 */
	if (rxs->rs_datalen >= 1)
		/* The pulse width is byte 0 of the data */
		dur = ((uint8_t) buf[0]) & 0xff;
	else
		dur = 0;

	/* Pulse RSSI is the normal reported RSSI */
	rssi = (uint8_t) rxs->rs_rssi;

	/* 0 duration/rssi is not a valid radar event */
	if (dur == 0 && rssi == 0)
		return AH_FALSE;

	HALDEBUG(ah, HAL_DEBUG_DFS, "%s: rssi=%d, dur=%d\n",
	    __func__, rssi, dur);

	/* Record the event */
	event->re_full_ts = fulltsf;
	event->re_ts = rxs->rs_tstamp;
	event->re_rssi = rssi;
	event->re_dur = dur;
	event->re_flags = HAL_DFS_EVENT_PRICH;

	return AH_TRUE;
}

/*
 * Return whether 5GHz fast-clock (44MHz) is enabled.
 * It's always disabled for AR5212 series NICs.
 */
HAL_BOOL
ar5212IsFastClockEnabled(struct ath_hal *ah)
{
	return AH_FALSE;
}

/*
 * Return what percentage of the extension channel is busy.
 * This is always disabled for AR5212 series NICs.
 */
uint32_t
ar5212Get11nExtBusy(struct ath_hal *ah)
{
	return 0;
}

/*
 * Channel survey support.
 */
HAL_BOOL
ar5212GetMibCycleCounts(struct ath_hal *ah, HAL_SURVEY_SAMPLE *hsample)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	u_int32_t good = AH_TRUE;

	/* XXX freeze/unfreeze mib counters */
	uint32_t rc = OS_REG_READ(ah, AR_RCCNT);
	uint32_t rf = OS_REG_READ(ah, AR_RFCNT);
	uint32_t tf = OS_REG_READ(ah, AR_TFCNT);
	uint32_t cc = OS_REG_READ(ah, AR_CCCNT); /* read cycles last */

	if (ahp->ah_cycleCount == 0 || ahp->ah_cycleCount > cc) {
		/*
		 * Cycle counter wrap (or initial call); it's not possible
		 * to accurately calculate a value because the registers
		 * right shift rather than wrap--so punt and return 0.
		 */
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: cycle counter wrap. ExtBusy = 0\n", __func__);
		good = AH_FALSE;
	} else {
		hsample->cycle_count = cc - ahp->ah_cycleCount;
		hsample->chan_busy = rc - ahp->ah_ctlBusy;
		hsample->ext_chan_busy = 0;
		hsample->rx_busy = rf - ahp->ah_rxBusy;
		hsample->tx_busy = tf - ahp->ah_txBusy;
	}

	/*
	 * Keep a copy of the MIB results so the next sample has something
	 * to work from.
	 */
	ahp->ah_cycleCount = cc;
	ahp->ah_rxBusy = rf;
	ahp->ah_ctlBusy = rc;
	ahp->ah_txBusy = tf;

	return (good);
}

void
ar5212SetChainMasks(struct ath_hal *ah, uint32_t tx_chainmask,
    uint32_t rx_chainmask)
{
}
