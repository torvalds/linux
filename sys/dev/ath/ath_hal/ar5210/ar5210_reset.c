/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2004 Atheros Communications, Inc.
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

#include "ar5210/ar5210.h"
#include "ar5210/ar5210reg.h"
#include "ar5210/ar5210phy.h"

#include "ah_eeprom_v1.h"

typedef struct {
	uint32_t	Offset;
	uint32_t	Value;
} REGISTER_VAL;

static const REGISTER_VAL ar5k0007_init[] = {
#include "ar5210/ar5k_0007.ini"
};

/* Default Power Settings for channels outside of EEPROM range */
static const uint8_t ar5k0007_pwrSettings[17] = {
/*	gain delta			pc dac */
/* 54  48  36  24  18  12   9   54  48  36  24  18  12   9   6  ob  db	  */
    9,  9,  0,  0,  0,  0,  0,   2,  2,  6,  6,  6,  6,  6,  6,  2,  2
};

/*
 * The delay, in usecs, between writing AR_RC with a reset
 * request and waiting for the chip to settle.  If this is
 * too short then the chip does not come out of sleep state.
 * Note this value was empirically derived and may be dependent
 * on the host machine (don't know--the problem was identified
 * on an IBM 570e laptop; 10us delays worked on other systems).
 */
#define	AR_RC_SETTLE_TIME	20000

static HAL_BOOL ar5210SetResetReg(struct ath_hal *,
		uint32_t resetMask, u_int delay);
static HAL_BOOL ar5210SetChannel(struct ath_hal *, struct ieee80211_channel *);
static void ar5210SetOperatingMode(struct ath_hal *, int opmode);

/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 *
 * bChannelChange is used to preserve DMA/PCU registers across
 * a HW Reset during channel change.
 */
HAL_BOOL
ar5210Reset(struct ath_hal *ah, HAL_OPMODE opmode,
	struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
	HAL_RESET_TYPE resetType,
	HAL_STATUS *status)
{
#define	N(a)	(sizeof (a) /sizeof (a[0]))
#define	FAIL(_code)	do { ecode = _code; goto bad; } while (0)
	struct ath_hal_5210 *ahp = AH5210(ah);
	const HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;
	HAL_CHANNEL_INTERNAL *ichan;
	HAL_STATUS ecode;
	uint32_t ledstate;
	int i, q;

	HALDEBUG(ah, HAL_DEBUG_RESET,
	    "%s: opmode %u channel %u/0x%x %s channel\n", __func__,
	    opmode, chan->ic_freq, chan->ic_flags,
	    bChannelChange ? "change" : "same");

	if (!IEEE80211_IS_CHAN_5GHZ(chan)) {
		/* Only 11a mode */
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: channel not 5GHz\n", __func__);
		FAIL(HAL_EINVAL);
	}
	/*
	 * Map public channel to private.
	 */
	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		FAIL(HAL_EINVAL);
	}
	switch (opmode) {
	case HAL_M_STA:
	case HAL_M_IBSS:
	case HAL_M_HOSTAP:
	case HAL_M_MONITOR:
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid operating mode %u\n",
		    __func__, opmode);
		FAIL(HAL_EINVAL);
		break;
	}

	ledstate = OS_REG_READ(ah, AR_PCICFG) &
		(AR_PCICFG_LED_PEND | AR_PCICFG_LED_ACT);

	if (!ar5210ChipReset(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n",
		    __func__);
		FAIL(HAL_EIO);
	}

	OS_REG_WRITE(ah, AR_STA_ID0, LE_READ_4(ahp->ah_macaddr));
	OS_REG_WRITE(ah, AR_STA_ID1, LE_READ_2(ahp->ah_macaddr + 4));
	ar5210SetOperatingMode(ah, opmode);

	switch (opmode) {
	case HAL_M_HOSTAP:
		OS_REG_WRITE(ah, AR_BCR, INIT_BCON_CNTRL_REG);
		OS_REG_WRITE(ah, AR_PCICFG,
			AR_PCICFG_LED_ACT | AR_PCICFG_LED_BCTL);
		break;
	case HAL_M_IBSS:
		OS_REG_WRITE(ah, AR_BCR, INIT_BCON_CNTRL_REG | AR_BCR_BCMD);
		OS_REG_WRITE(ah, AR_PCICFG,
			AR_PCICFG_CLKRUNEN | AR_PCICFG_LED_PEND | AR_PCICFG_LED_BCTL);
		break;
	case HAL_M_STA:
		OS_REG_WRITE(ah, AR_BCR, INIT_BCON_CNTRL_REG);
		OS_REG_WRITE(ah, AR_PCICFG,
			AR_PCICFG_CLKRUNEN | AR_PCICFG_LED_PEND | AR_PCICFG_LED_BCTL);
		break;
	case HAL_M_MONITOR:
		OS_REG_WRITE(ah, AR_BCR, INIT_BCON_CNTRL_REG);
		OS_REG_WRITE(ah, AR_PCICFG,
			AR_PCICFG_LED_ACT | AR_PCICFG_LED_BCTL);
		break;
	}

	/* Restore previous led state */
	OS_REG_WRITE(ah, AR_PCICFG, OS_REG_READ(ah, AR_PCICFG) | ledstate);

#if 0
	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid + 4));
#endif
	/* BSSID, association id, ps-poll */
	ar5210WriteAssocid(ah, ahp->ah_bssid, ahp->ah_associd);

	OS_REG_WRITE(ah, AR_TXDP0, 0);
	OS_REG_WRITE(ah, AR_TXDP1, 0);
	OS_REG_WRITE(ah, AR_RXDP, 0);

	/*
	 * Initialize interrupt state.
	 */
	(void) OS_REG_READ(ah, AR_ISR);		/* cleared on read */
	OS_REG_WRITE(ah, AR_IMR, 0);
	OS_REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
	ahp->ah_maskReg = 0;

	(void) OS_REG_READ(ah, AR_BSR);		/* cleared on read */
	OS_REG_WRITE(ah, AR_TXCFG, AR_DMASIZE_128B);
	OS_REG_WRITE(ah, AR_RXCFG, AR_DMASIZE_128B);

	OS_REG_WRITE(ah, AR_TOPS, 8);		/* timeout prescale */
	OS_REG_WRITE(ah, AR_RXNOFRM, 8);	/* RX no frame timeout */
	OS_REG_WRITE(ah, AR_RPGTO, 0);		/* RX frame gap timeout */
	OS_REG_WRITE(ah, AR_TXNOFRM, 0);	/* TX no frame timeout */

	OS_REG_WRITE(ah, AR_SFR, 0);
	OS_REG_WRITE(ah, AR_MIBC, 0);		/* unfreeze ctrs + clr state */
	OS_REG_WRITE(ah, AR_RSSI_THR, ahp->ah_rssiThr);
	OS_REG_WRITE(ah, AR_CFP_DUR, 0);

	ar5210SetRxFilter(ah, 0);		/* nothing for now */
	OS_REG_WRITE(ah, AR_MCAST_FIL0, 0);	/* multicast filter */
	OS_REG_WRITE(ah, AR_MCAST_FIL1, 0);	/* XXX was 2 */

	OS_REG_WRITE(ah, AR_TX_MASK0, 0);
	OS_REG_WRITE(ah, AR_TX_MASK1, 0);
	OS_REG_WRITE(ah, AR_CLR_TMASK, 1);
	OS_REG_WRITE(ah, AR_TRIG_LEV, 1);	/* minimum */

	ar5210UpdateDiagReg(ah, 0);

	OS_REG_WRITE(ah, AR_CFP_PERIOD, 0);
	OS_REG_WRITE(ah, AR_TIMER0, 0);		/* next beacon time */
	OS_REG_WRITE(ah, AR_TSF_L32, 0);	/* local clock */
	OS_REG_WRITE(ah, AR_TIMER1, ~0);	/* next DMA beacon alert */
	OS_REG_WRITE(ah, AR_TIMER2, ~0);	/* next SW beacon alert */
	OS_REG_WRITE(ah, AR_TIMER3, 1);		/* next ATIM window */

	/* Write the INI values for PHYreg initialization */
	for (i = 0; i < N(ar5k0007_init); i++) {
		uint32_t reg = ar5k0007_init[i].Offset;
		/* On channel change, don't reset the PCU registers */
		if (!(bChannelChange && (0x8000 <= reg && reg < 0x9000)))
			OS_REG_WRITE(ah, reg, ar5k0007_init[i].Value);
	}

	/* Setup the transmit power values for cards since 0x0[0-2]05 */
	if (!ar5210SetTransmitPower(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error init'ing transmit power\n", __func__);
		FAIL(HAL_EIO);
	}

	OS_REG_WRITE(ah, AR_PHY(10),
		(OS_REG_READ(ah, AR_PHY(10)) & 0xFFFF00FF) |
		(ee->ee_xlnaOn << 8));
	OS_REG_WRITE(ah, AR_PHY(13),
		(ee->ee_xpaOff << 24) | (ee->ee_xpaOff << 16) |
		(ee->ee_xpaOn << 8) | ee->ee_xpaOn);
	OS_REG_WRITE(ah, AR_PHY(17),
		(OS_REG_READ(ah, AR_PHY(17)) & 0xFFFFC07F) |
		((ee->ee_antenna >> 1) & 0x3F80));
	OS_REG_WRITE(ah, AR_PHY(18),
		(OS_REG_READ(ah, AR_PHY(18)) & 0xFFFC0FFF) |
		((ee->ee_antenna << 10) & 0x3F000));
	OS_REG_WRITE(ah, AR_PHY(25),
		(OS_REG_READ(ah, AR_PHY(25)) & 0xFFF80FFF) |
		((ee->ee_thresh62 << 12) & 0x7F000));
	OS_REG_WRITE(ah, AR_PHY(68),
		(OS_REG_READ(ah, AR_PHY(68)) & 0xFFFFFFFC) |
		(ee->ee_antenna & 0x3));

	if (!ar5210SetChannel(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unable to set channel\n",
		    __func__);
		FAIL(HAL_EIO);
	}
	if (bChannelChange && !IEEE80211_IS_CHAN_DFS(chan)) 
		chan->ic_state &= ~IEEE80211_CHANSTATE_CWINT;

	/* Activate the PHY */
	OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ENABLE);

	OS_DELAY(1000);		/* Wait a bit (1 msec) */

	/* calibrate the HW and poll the bit going to 0 for completion */
	OS_REG_WRITE(ah, AR_PHY_AGCCTL,
		OS_REG_READ(ah, AR_PHY_AGCCTL) | AR_PHY_AGC_CAL);
	(void) ath_hal_wait(ah, AR_PHY_AGCCTL, AR_PHY_AGC_CAL, 0);

	/* Perform noise floor calibration and set status */
	if (!ar5210CalNoiseFloor(ah, ichan)) {
		chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: noise floor calibration failed\n", __func__);
		FAIL(HAL_EIO);
	}

	for (q = 0; q < HAL_NUM_TX_QUEUES; q++)
		ar5210ResetTxQueue(ah, q);

	if (AH_PRIVATE(ah)->ah_rfkillEnabled)
		ar5210EnableRfKill(ah);

	/*
	 * Writing to AR_BEACON will start timers. Hence it should be
	 * the last register to be written. Do not reset tsf, do not
	 * enable beacons at this point, but preserve other values
	 * like beaconInterval.
	 */
	OS_REG_WRITE(ah, AR_BEACON,
		(OS_REG_READ(ah, AR_BEACON) &
			~(AR_BEACON_EN | AR_BEACON_RESET_TSF)));

	/* Restore user-specified slot time and timeouts */
	if (ahp->ah_sifstime != (u_int) -1)
		ar5210SetSifsTime(ah, ahp->ah_sifstime);
	if (ahp->ah_slottime != (u_int) -1)
		ar5210SetSlotTime(ah, ahp->ah_slottime);
	if (ahp->ah_acktimeout != (u_int) -1)
		ar5210SetAckTimeout(ah, ahp->ah_acktimeout);
	if (ahp->ah_ctstimeout != (u_int) -1)
		ar5210SetCTSTimeout(ah, ahp->ah_ctstimeout);
	if (AH_PRIVATE(ah)->ah_diagreg != 0)
		ar5210UpdateDiagReg(ah, AH_PRIVATE(ah)->ah_diagreg);

	AH_PRIVATE(ah)->ah_opmode = opmode;	/* record operating mode */

	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: done\n", __func__);

	return AH_TRUE;
bad:
	if (status != AH_NULL)
		*status = ecode;
	return AH_FALSE;
#undef FAIL
#undef N
}

static void
ar5210SetOperatingMode(struct ath_hal *ah, int opmode)
{
	struct ath_hal_5210 *ahp = AH5210(ah);
	uint32_t val;

	val = OS_REG_READ(ah, AR_STA_ID1) & 0xffff;
	switch (opmode) {
	case HAL_M_HOSTAP:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_AP
			| AR_STA_ID1_NO_PSPOLL
			| AR_STA_ID1_DESC_ANTENNA
			| ahp->ah_staId1Defaults);
		break;
	case HAL_M_IBSS:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_ADHOC
			| AR_STA_ID1_NO_PSPOLL
			| AR_STA_ID1_DESC_ANTENNA
			| ahp->ah_staId1Defaults);
		break;
	case HAL_M_STA:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_NO_PSPOLL
			| AR_STA_ID1_PWR_SV
			| ahp->ah_staId1Defaults);
		break;
	case HAL_M_MONITOR:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_NO_PSPOLL
			| ahp->ah_staId1Defaults);
		break;
	}
}

void
ar5210SetPCUConfig(struct ath_hal *ah)
{
	ar5210SetOperatingMode(ah, AH_PRIVATE(ah)->ah_opmode);
}

/*
 * Places the PHY and Radio chips into reset.  A full reset
 * must be called to leave this state.  The PCI/MAC/PCU are
 * not placed into reset as we must receive interrupt to
 * re-enable the hardware.
 */
HAL_BOOL
ar5210PhyDisable(struct ath_hal *ah)
{
	return ar5210SetResetReg(ah, AR_RC_RPHY, 10);
}

/*
 * Places all of hardware into reset
 */
HAL_BOOL
ar5210Disable(struct ath_hal *ah)
{
#define	AR_RC_HW (AR_RC_RPCU | AR_RC_RDMA | AR_RC_RPHY | AR_RC_RMAC)
	if (!ar5210SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset
	 */
	if (!ar5210SetResetReg(ah, AR_RC_HW, AR_RC_SETTLE_TIME))
		return AH_FALSE;
	OS_DELAY(1000);
	(void) ar5210SetResetReg(ah, AR_RC_HW | AR_RC_RPCI, AR_RC_SETTLE_TIME);
	OS_DELAY(2100);   /* 8245 @ 96Mhz hangs with 2000us. */

	return AH_TRUE;
#undef AR_RC_HW
}

/*
 * Places the hardware into reset and then pulls it out of reset
 */
HAL_BOOL
ar5210ChipReset(struct ath_hal *ah, struct ieee80211_channel *chan)
{
#define	AR_RC_HW (AR_RC_RPCU | AR_RC_RDMA | AR_RC_RPHY | AR_RC_RMAC)

	HALDEBUG(ah, HAL_DEBUG_RESET, "%s turbo %s\n", __func__,
		chan && IEEE80211_IS_CHAN_TURBO(chan) ?
		"enabled" : "disabled");

	if (!ar5210SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/* Place chip in turbo before reset to cleanly reset clocks */
	OS_REG_WRITE(ah, AR_PHY_FRCTL,
		chan && IEEE80211_IS_CHAN_TURBO(chan) ? AR_PHY_TURBO_MODE : 0);

	/*
	 * Reset the HW.
	 * PCI must be reset after the rest of the device has been reset.
	 */
	if (!ar5210SetResetReg(ah, AR_RC_HW, AR_RC_SETTLE_TIME))
		return AH_FALSE;
	OS_DELAY(1000);
	if (!ar5210SetResetReg(ah, AR_RC_HW | AR_RC_RPCI, AR_RC_SETTLE_TIME))
		return AH_FALSE;
	OS_DELAY(2100);   /* 8245 @ 96Mhz hangs with 2000us. */

	/*
	 * Bring out of sleep mode (AGAIN)
	 *
	 * WARNING WARNING WARNING
	 *
	 * There is a problem with the chip where it doesn't always indicate
	 * that it's awake, so initializePowerUp() will fail.
	 */
	if (!ar5210SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/* Clear warm reset reg */
	return ar5210SetResetReg(ah, 0, 10);
#undef AR_RC_HW
}

enum {
	FIRPWR_M	= 0x03fc0000,
	FIRPWR_S	= 18,
	KCOARSEHIGH_M   = 0x003f8000,
	KCOARSEHIGH_S   = 15,
	KCOARSELOW_M	= 0x00007f80,
	KCOARSELOW_S	= 7,
	ADCSAT_ICOUNT_M	= 0x0001f800,
	ADCSAT_ICOUNT_S	= 11,
	ADCSAT_THRESH_M	= 0x000007e0,
	ADCSAT_THRESH_S	= 5
};

/*
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
HAL_BOOL
ar5210PerCalibrationN(struct ath_hal *ah,
	struct ieee80211_channel *chan, u_int chainMask,
	HAL_BOOL longCal, HAL_BOOL *isCalDone)
{
	uint32_t regBeacon;
	uint32_t reg9858, reg985c, reg9868;
	HAL_CHANNEL_INTERNAL *ichan;

	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL)
		return AH_FALSE;
	/* Disable tx and rx */
	ar5210UpdateDiagReg(ah,
		OS_REG_READ(ah, AR_DIAG_SW) | (AR_DIAG_SW_DIS_TX | AR_DIAG_SW_DIS_RX));

	/* Disable Beacon Enable */
	regBeacon = OS_REG_READ(ah, AR_BEACON);
	OS_REG_WRITE(ah, AR_BEACON, regBeacon & ~AR_BEACON_EN);

	/* Delay 4ms to ensure that all tx and rx activity has ceased */
	OS_DELAY(4000);

	/* Disable AGC to radio traffic */
	OS_REG_WRITE(ah, 0x9808, OS_REG_READ(ah, 0x9808) | 0x08000000);
	/* Wait for the AGC traffic to cease. */
	OS_DELAY(10);

	/* Change Channel to relock synth */
	if (!ar5210SetChannel(ah, chan))
		return AH_FALSE;

	/* wait for the synthesizer lock to stabilize */
	OS_DELAY(1000);

	/* Re-enable AGC to radio traffic */
	OS_REG_WRITE(ah, 0x9808, OS_REG_READ(ah, 0x9808) & (~0x08000000));

	/*
	 * Configure the AGC so that it is highly unlikely (if not
	 * impossible) for it to send any gain changes to the analog
	 * chip.  We store off the current values so that they can
	 * be rewritten below. Setting the following values:
	 * firpwr	 = -1
	 * Kcoursehigh   = -1
	 * Kcourselow	 = -127
	 * ADCsat_icount = 2
	 * ADCsat_thresh = 12
	 */
	reg9858 = OS_REG_READ(ah, 0x9858);
	reg985c = OS_REG_READ(ah, 0x985c);
	reg9868 = OS_REG_READ(ah, 0x9868);

	OS_REG_WRITE(ah, 0x9858, (reg9858 & ~FIRPWR_M) |
					 ((-1 << FIRPWR_S) & FIRPWR_M));
	OS_REG_WRITE(ah, 0x985c,
		 (reg985c & ~(KCOARSEHIGH_M | KCOARSELOW_M)) |
		 ((-1 << KCOARSEHIGH_S) & KCOARSEHIGH_M) |
		 ((-127 << KCOARSELOW_S) & KCOARSELOW_M));
	OS_REG_WRITE(ah, 0x9868,
		 (reg9868 & ~(ADCSAT_ICOUNT_M | ADCSAT_THRESH_M)) |
		 ((2 << ADCSAT_ICOUNT_S) & ADCSAT_ICOUNT_M) |
		 ((12 << ADCSAT_THRESH_S) & ADCSAT_THRESH_M));

	/* Wait for AGC changes to be enacted */
	OS_DELAY(20);

	/*
	 * We disable RF mix/gain stages for the PGA to avoid a
	 * race condition that will occur with receiving a frame
	 * and performing the AGC calibration.  This will be
	 * re-enabled at the end of offset cal.  We turn off AGC
	 * writes during this write as it will go over the analog bus.
	 */
	OS_REG_WRITE(ah, 0x9808, OS_REG_READ(ah, 0x9808) | 0x08000000);
	OS_DELAY(10);		 /* wait for the AGC traffic to cease */
	OS_REG_WRITE(ah, 0x98D4, 0x21);
	OS_REG_WRITE(ah, 0x9808, OS_REG_READ(ah, 0x9808) & (~0x08000000));

	/* wait to make sure that additional AGC traffic has quiesced */
	OS_DELAY(1000);

	/* AGC calibration (this was added to make the NF threshold check work) */
	OS_REG_WRITE(ah, AR_PHY_AGCCTL,
		 OS_REG_READ(ah, AR_PHY_AGCCTL) | AR_PHY_AGC_CAL);
	if (!ath_hal_wait(ah, AR_PHY_AGCCTL, AR_PHY_AGC_CAL, 0)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: AGC calibration timeout\n",
		    __func__);
	}

	/* Rewrite our AGC values we stored off earlier (return AGC to normal operation) */
	OS_REG_WRITE(ah, 0x9858, reg9858);
	OS_REG_WRITE(ah, 0x985c, reg985c);
	OS_REG_WRITE(ah, 0x9868, reg9868);

	/* Perform noise floor and set status */
	if (!ar5210CalNoiseFloor(ah, ichan)) {
		/*
		 * Delay 5ms before retrying the noise floor -
		 * just to make sure.  We're in an error
		 * condition here
		 */
		HALDEBUG(ah, HAL_DEBUG_NFCAL | HAL_DEBUG_PERCAL,
		    "%s: Performing 2nd Noise Cal\n", __func__);
		OS_DELAY(5000);
		if (!ar5210CalNoiseFloor(ah, ichan))
			chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
	}

	/* Clear tx and rx disable bit */
	ar5210UpdateDiagReg(ah,
		 OS_REG_READ(ah, AR_DIAG_SW) & ~(AR_DIAG_SW_DIS_TX | AR_DIAG_SW_DIS_RX));

	/* Re-enable Beacons */
	OS_REG_WRITE(ah, AR_BEACON, regBeacon);

	*isCalDone = AH_TRUE;

	return AH_TRUE;
}

HAL_BOOL
ar5210PerCalibration(struct ath_hal *ah, struct ieee80211_channel *chan,
	HAL_BOOL *isIQdone)
{
	return ar5210PerCalibrationN(ah,  chan, 0x1, AH_TRUE, isIQdone);
}

HAL_BOOL
ar5210ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	return AH_TRUE;
}

/*
 * Writes the given reset bit mask into the reset register
 */
static HAL_BOOL
ar5210SetResetReg(struct ath_hal *ah, uint32_t resetMask, u_int delay)
{
	uint32_t mask = resetMask ? resetMask : ~0;
	HAL_BOOL rt;

	OS_REG_WRITE(ah, AR_RC, resetMask);
	/* need to wait at least 128 clocks when reseting PCI before read */
	OS_DELAY(delay);

	resetMask &= AR_RC_RPCU | AR_RC_RDMA | AR_RC_RPHY | AR_RC_RMAC;
	mask &= AR_RC_RPCU | AR_RC_RDMA | AR_RC_RPHY | AR_RC_RMAC;
	rt = ath_hal_wait(ah, AR_RC, mask, resetMask);
        if ((resetMask & AR_RC_RMAC) == 0) {
		if (isBigEndian()) {
			/*
			 * Set CFG, little-endian for descriptor accesses.
			 */
			mask = INIT_CONFIG_STATUS | AR_CFG_SWTD | AR_CFG_SWRD;
			OS_REG_WRITE(ah, AR_CFG, mask);
		} else
			OS_REG_WRITE(ah, AR_CFG, INIT_CONFIG_STATUS);
	}
	return rt;
}


/*
 * Returns: the pcdac value
 */
static uint8_t
getPcdac(struct ath_hal *ah, const struct tpcMap *pRD, uint8_t dBm)
{
	int32_t	 i;
	int useNextEntry = AH_FALSE;
	uint32_t interp;

	for (i = AR_TP_SCALING_ENTRIES - 1; i >= 0; i--) {
		/* Check for exact entry */
		if (dBm == AR_I2DBM(i)) {
			if (pRD->pcdac[i] != 63)
				return pRD->pcdac[i];
			useNextEntry = AH_TRUE;
		} else if (dBm + 1 == AR_I2DBM(i) && i > 0) {
			/* Interpolate for between entry with a logish scale */
			if (pRD->pcdac[i] != 63 && pRD->pcdac[i-1] != 63) {
				interp = (350 * (pRD->pcdac[i] - pRD->pcdac[i-1])) + 999;
				interp = (interp / 1000) + pRD->pcdac[i-1];
				return interp;
			}
			useNextEntry = AH_TRUE;
		} else if (useNextEntry == AH_TRUE) {
			/* Grab the next lowest */
			if (pRD->pcdac[i] != 63)
				return pRD->pcdac[i];
		}
	}

	/* Return the lowest Entry if we haven't returned */
	for (i = 0; i < AR_TP_SCALING_ENTRIES; i++)
		if (pRD->pcdac[i] != 63)
			return pRD->pcdac[i];

	/* No value to return from table */
#ifdef AH_DEBUG
	ath_hal_printf(ah, "%s: empty transmit power table?\n", __func__);
#endif
	return 1;
}

/*
 * Find or interpolates the gainF value from the table ptr.
 */
static uint8_t
getGainF(struct ath_hal *ah, const struct tpcMap *pRD,
	uint8_t pcdac, uint8_t *dBm)
{
	uint32_t interp;
	int low, high, i;

	low = high = -1;

	for (i = 0; i < AR_TP_SCALING_ENTRIES; i++) {
		if(pRD->pcdac[i] == 63)
			continue;
		if (pcdac == pRD->pcdac[i]) {
			*dBm = AR_I2DBM(i);
			return pRD->gainF[i];  /* Exact Match */
		}
		if (pcdac > pRD->pcdac[i])
			low = i;
		if (pcdac < pRD->pcdac[i]) {
			high = i;
			if (low == -1) {
				*dBm = AR_I2DBM(i);
				/* PCDAC is lower than lowest setting */
				return pRD->gainF[i];
			}
			break;
		}
	}
	if (i >= AR_TP_SCALING_ENTRIES && low == -1) {
		/* No settings were found */
#ifdef AH_DEBUG
		ath_hal_printf(ah,
			"%s: no valid entries in the pcdac table: %d\n",
			__func__, pcdac);
#endif
		return 63;
	}
	if (i >= AR_TP_SCALING_ENTRIES) {
		/* PCDAC setting was above the max setting in the table */
		*dBm = AR_I2DBM(low);
		return pRD->gainF[low];
	}
	/* Only exact if table has no missing entries */
	*dBm = (low + high) + 3;

	/*
	 * Perform interpolation between low and high values to find gainF
	 * linearly scale the pcdac between low and high
	 */
	interp = ((pcdac - pRD->pcdac[low]) * 1000) /
		  (pRD->pcdac[high] - pRD->pcdac[low]);
	/*
	 * Multiply the scale ratio by the gainF difference
	 * (plus a rnd up factor)
	 */
	interp = ((interp * (pRD->gainF[high] - pRD->gainF[low])) + 999) / 1000;

	/* Add ratioed gain_f to low gain_f value */
	return interp + pRD->gainF[low];
}

HAL_BOOL
ar5210SetTxPowerLimit(struct ath_hal *ah, uint32_t limit)
{
	AH_PRIVATE(ah)->ah_powerLimit = AH_MIN(limit, AR5210_MAX_RATE_POWER);
	/* XXX flush to h/w */
	return AH_TRUE;
}

/*
 * Get TXPower values and set them in the radio
 */
static HAL_BOOL
setupPowerSettings(struct ath_hal *ah, const struct ieee80211_channel *chan,
	uint8_t cp[17])
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	const HAL_EEPROM_v1 *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint8_t gainFRD, gainF36, gainF48, gainF54;
	uint8_t dBmRD, dBm36, dBm48, dBm54, dontcare;
	uint32_t rd, group;
	const struct tpcMap  *pRD;

	/* Set OB/DB Values regardless of channel */
	cp[15] = (ee->ee_biasCurrents >> 4) & 0x7;
	cp[16] = ee->ee_biasCurrents & 0x7;

	if (freq < 5170 || freq > 5320) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel %u\n",
		    __func__, freq);
		return AH_FALSE;
	}

	HALASSERT(ee->ee_version >= AR_EEPROM_VER1 &&
	    ee->ee_version < AR_EEPROM_VER3);

	/* Match regulatory domain */
	for (rd = 0; rd < AR_REG_DOMAINS_MAX; rd++)
		if (AH_PRIVATE(ah)->ah_currentRD == ee->ee_regDomain[rd])
			break;
	if (rd == AR_REG_DOMAINS_MAX) {
#ifdef AH_DEBUG
		ath_hal_printf(ah,
			"%s: no calibrated regulatory domain matches the "
			"current regularly domain (0x%0x)\n", __func__, 
			AH_PRIVATE(ah)->ah_currentRD);
#endif
		return AH_FALSE;
	}
	group = ((freq - 5170) / 10);

	if (group > 11) {
		/* Pull 5.29 into the 5.27 group */
		group--;
	}

	/* Integer divide will set group from 0 to 4 */
	group = group / 3;
	pRD   = &ee->ee_tpc[group];

	/* Set PC DAC Values */
	cp[14] = pRD->regdmn[rd];
	cp[9]  = AH_MIN(pRD->regdmn[rd], pRD->rate36);
	cp[8]  = AH_MIN(pRD->regdmn[rd], pRD->rate48);
	cp[7]  = AH_MIN(pRD->regdmn[rd], pRD->rate54);

	/* Find Corresponding gainF values for RD, 36, 48, 54 */
	gainFRD = getGainF(ah, pRD, pRD->regdmn[rd], &dBmRD);
	gainF36 = getGainF(ah, pRD, cp[9], &dBm36);
	gainF48 = getGainF(ah, pRD, cp[8], &dBm48);
	gainF54 = getGainF(ah, pRD, cp[7], &dBm54);

	/* Power Scale if requested */
	if (AH_PRIVATE(ah)->ah_tpScale != HAL_TP_SCALE_MAX) {
		static const uint16_t tpcScaleReductionTable[5] =
			{ 0, 3, 6, 9, AR5210_MAX_RATE_POWER };
		uint16_t tpScale;

		tpScale = tpcScaleReductionTable[AH_PRIVATE(ah)->ah_tpScale];
		if (dBmRD < tpScale+3)
			dBmRD = 3;		/* min */
		else
			dBmRD -= tpScale;
		cp[14]  = getPcdac(ah, pRD, dBmRD);
		gainFRD = getGainF(ah, pRD, cp[14], &dontcare);
		dBm36   = AH_MIN(dBm36, dBmRD);
		cp[9]   = getPcdac(ah, pRD, dBm36);
		gainF36 = getGainF(ah, pRD, cp[9], &dontcare);
		dBm48   = AH_MIN(dBm48, dBmRD);
		cp[8]   = getPcdac(ah, pRD, dBm48);
		gainF48 = getGainF(ah, pRD, cp[8], &dontcare);
		dBm54   = AH_MIN(dBm54, dBmRD);
		cp[7]   = getPcdac(ah, pRD, dBm54);
		gainF54 = getGainF(ah, pRD, cp[7], &dontcare);
	}
	/* Record current dBm at rate 6 */
	AH_PRIVATE(ah)->ah_maxPowerLevel = 2*dBmRD;

	cp[13] = cp[12] = cp[11] = cp[10] = cp[14];

	/* Set GainF Values */
	cp[0] = gainFRD - gainF54;
	cp[1] = gainFRD - gainF48;
	cp[2] = gainFRD - gainF36;
	/* 9, 12, 18, 24 have no gain_delta from 6 */
	cp[3] = cp[4] = cp[5] = cp[6] = 0;
	return AH_TRUE;
}

/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 */
HAL_BOOL
ar5210SetTransmitPower(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
	static const uint32_t pwr_regs_start[17] = {
		0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0xf0000000,
		0xcc000000, 0x00000000, 0x00000000,
		0x00000000, 0x0a000000, 0x000000e2,
		0x0a000020, 0x01000002, 0x01000018,
		0x40000000, 0x00000418
	};
	uint16_t i;
	uint8_t cp[sizeof(ar5k0007_pwrSettings)];
	uint32_t pwr_regs[17];

	OS_MEMCPY(pwr_regs, pwr_regs_start, sizeof(pwr_regs));
	OS_MEMCPY(cp, ar5k0007_pwrSettings, sizeof(cp));

	/* Check the EEPROM tx power calibration settings */
	if (!setupPowerSettings(ah, chan, cp)) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: unable to setup power settings\n",
			__func__);
#endif
		return AH_FALSE;
	}
	if (cp[15] < 1 || cp[15] > 5) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: OB out of range (%u)\n",
			__func__, cp[15]);
#endif
		return AH_FALSE;
	}
	if (cp[16] < 1 || cp[16] > 5) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: DB out of range (%u)\n",
			__func__, cp[16]);
#endif
		return AH_FALSE;
	}

	/* reverse bits of the transmit power array */
	for (i = 0; i < 7; i++)
		cp[i] = ath_hal_reverseBits(cp[i], 5);
	for (i = 7; i < 15; i++)
		cp[i] = ath_hal_reverseBits(cp[i], 6);

	/* merge transmit power values into the register - quite gross */
	pwr_regs[0] |= ((cp[1] << 5) & 0xE0) | (cp[0] & 0x1F);
	pwr_regs[1] |= ((cp[3] << 7) & 0x80) | ((cp[2] << 2) & 0x7C) | 
			((cp[1] >> 3) & 0x03);
	pwr_regs[2] |= ((cp[4] << 4) & 0xF0) | ((cp[3] >> 1) & 0x0F);
	pwr_regs[3] |= ((cp[6] << 6) & 0xC0) | ((cp[5] << 1) & 0x3E) |
		       ((cp[4] >> 4) & 0x01);
	pwr_regs[4] |= ((cp[7] << 3) & 0xF8) | ((cp[6] >> 2) & 0x07);
	pwr_regs[5] |= ((cp[9] << 7) & 0x80) | ((cp[8] << 1) & 0x7E) |
			((cp[7] >> 5) & 0x01);
	pwr_regs[6] |= ((cp[10] << 5) & 0xE0) | ((cp[9] >> 1) & 0x1F);
	pwr_regs[7] |= ((cp[11] << 3) & 0xF8) | ((cp[10] >> 3) & 0x07);
	pwr_regs[8] |= ((cp[12] << 1) & 0x7E) | ((cp[11] >> 5) & 0x01);
	pwr_regs[9] |= ((cp[13] << 5) & 0xE0);
	pwr_regs[10] |= ((cp[14] << 3) & 0xF8) | ((cp[13] >> 3) & 0x07);
	pwr_regs[11] |= ((cp[14] >> 5) & 0x01);

	/* Set OB */
	pwr_regs[8] |=  (ath_hal_reverseBits(cp[15], 3) << 7) & 0x80;
	pwr_regs[9] |=  (ath_hal_reverseBits(cp[15], 3) >> 1) & 0x03;

	/* Set DB */
	pwr_regs[9] |=  (ath_hal_reverseBits(cp[16], 3) << 2) & 0x1C;

	/* Write the registers */
	for (i = 0; i < N(pwr_regs)-1; i++)
		OS_REG_WRITE(ah, 0x0000989c, pwr_regs[i]);
	/* last write is a flush */
	OS_REG_WRITE(ah, 0x000098d4, pwr_regs[i]);

	return AH_TRUE;
#undef N
}

/*
 * Takes the MHz channel value and sets the Channel value
 *
 * ASSUMES: Writes enabled to analog bus before AGC is active
 *   or by disabling the AGC.
 */
static HAL_BOOL
ar5210SetChannel(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	uint32_t data;

	/* Set the Channel */
	data = ath_hal_reverseBits((freq - 5120)/10, 5);
	data = (data << 1) | 0x41;
	OS_REG_WRITE(ah, AR_PHY(0x27), data);
	OS_REG_WRITE(ah, AR_PHY(0x30), 0);
	AH_PRIVATE(ah)->ah_curchan = chan;
	return AH_TRUE;
}

int16_t
ar5210GetNoiseFloor(struct ath_hal *ah)
{
	int16_t nf;

	nf = (OS_REG_READ(ah, AR_PHY(25)) >> 19) & 0x1ff;
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	return nf;
}

#define NORMAL_NF_THRESH (-72)
/*
 * Peform the noisefloor calibration and check for
 * any constant channel interference
 *
 * Returns: TRUE for a successful noise floor calibration; else FALSE
 */
HAL_BOOL
ar5210CalNoiseFloor(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan)
{
	int32_t nf, nfLoops;

	/* Calibrate the noise floor */
	OS_REG_WRITE(ah, AR_PHY_AGCCTL,
		OS_REG_READ(ah, AR_PHY_AGCCTL) | AR_PHY_AGC_NF);

	/* Do not read noise floor until it has done the first update */
	if (!ath_hal_wait(ah, AR_PHY_AGCCTL, AR_PHY_AGC_NF, 0)) {
#ifdef ATH_HAL_DEBUG
		ath_hal_printf(ah, " -PHY NF Reg state: 0x%x\n",
			OS_REG_READ(ah, AR_PHY_AGCCTL));
		ath_hal_printf(ah, " -MAC Reset Reg state: 0x%x\n",
			OS_REG_READ(ah, AR_RC));
		ath_hal_printf(ah, " -PHY Active Reg state: 0x%x\n",
			OS_REG_READ(ah, AR_PHY_ACTIVE));
#endif /* ATH_HAL_DEBUG */
		return AH_FALSE;
	}

	nf = 0;
	/* Keep checking until the floor is below the threshold or the nf is done */
	for (nfLoops = 0; ((nfLoops < 21) && (nf > NORMAL_NF_THRESH)); nfLoops++) {
		OS_DELAY(1000); /* Sleep for 1 ms */
		nf = ar5210GetNoiseFloor(ah);
	}

	if (nf > NORMAL_NF_THRESH) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: Bad noise cal %d\n",
		    __func__, nf);
		ichan->rawNoiseFloor = 0;
		return AH_FALSE;
	}
	ichan->rawNoiseFloor = nf;
	return AH_TRUE;
}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 */
int16_t
ar5210GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	return 0;
}

HAL_RFGAIN
ar5210GetRfgain(struct ath_hal *ah)
{
	return HAL_RFGAIN_INACTIVE;
}
