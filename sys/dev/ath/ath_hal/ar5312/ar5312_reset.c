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

#ifdef AH_SUPPORT_AR5312

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ar5312/ar5312.h"
#include "ar5312/ar5312reg.h"
#include "ar5312/ar5312phy.h"

#include "ah_eeprom_v3.h"

/* Additional Time delay to wait after activiting the Base band */
#define BASE_ACTIVATE_DELAY	100	/* 100 usec */
#define PLL_SETTLE_DELAY	300	/* 300 usec */

extern int16_t ar5212GetNf(struct ath_hal *, const struct ieee80211_channel *);
extern void ar5212SetRateDurationTable(struct ath_hal *,
		const struct ieee80211_channel *);
extern HAL_BOOL ar5212SetTransmitPower(struct ath_hal *ah,
	         const struct ieee80211_channel *chan, uint16_t *rfXpdGain);
extern void ar5212SetDeltaSlope(struct ath_hal *,
		 const struct ieee80211_channel *);
extern HAL_BOOL ar5212SetBoardValues(struct ath_hal *,
		 const struct ieee80211_channel *);
extern void ar5212SetIFSTiming(struct ath_hal *,
		 const struct ieee80211_channel *);
extern HAL_BOOL	ar5212IsSpurChannel(struct ath_hal *,
		 const struct ieee80211_channel *);
extern HAL_BOOL	ar5212ChannelChange(struct ath_hal *,
		 const struct ieee80211_channel *);

static HAL_BOOL ar5312SetResetReg(struct ath_hal *, uint32_t resetMask);

static int
write_common(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
	HAL_BOOL bChannelChange, int writes)
{
#define IS_NO_RESET_TIMER_ADDR(x)                      \
    ( (((x) >= AR_BEACON) && ((x) <= AR_CFP_DUR)) || \
      (((x) >= AR_SLEEP1) && ((x) <= AR_SLEEP3)))
#define	V(r, c)	(ia)->data[((r)*(ia)->cols) + (c)]
	int i;

	/* Write Common Array Parameters */
	for (i = 0; i < ia->rows; i++) {
		uint32_t reg = V(i, 0);
		/* XXX timer/beacon setup registers? */
		/* On channel change, don't reset the PCU registers */
		if (!(bChannelChange && IS_NO_RESET_TIMER_ADDR(reg))) {
			OS_REG_WRITE(ah, reg, V(i, 1));
			DMA_YIELD(writes);
		}
	}
	return writes;
#undef IS_NO_RESET_TIMER_ADDR
#undef V
}

/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 *
 * bChannelChange is used to preserve DMA/PCU registers across
 * a HW Reset during channel change.
 */
HAL_BOOL
ar5312Reset(struct ath_hal *ah, HAL_OPMODE opmode,
	struct ieee80211_channel *chan,
	HAL_BOOL bChannelChange,
	HAL_RESET_TYPE resetType,
	HAL_STATUS *status)
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
#define	FAIL(_code)	do { ecode = _code; goto bad; } while (0)
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_CHANNEL_INTERNAL *ichan;
	const HAL_EEPROM *ee;
	uint32_t saveFrameSeqCount, saveDefAntenna;
	uint32_t macStaId1, synthDelay, txFrm2TxDStart;
	uint16_t rfXpdGain[MAX_NUM_PDGAINS_PER_CHANNEL];
	int16_t cckOfdmPwrDelta = 0;
	u_int modesIndex, freqIndex;
	HAL_STATUS ecode;
	int i, regWrites = 0;
	uint32_t testReg;
	uint32_t saveLedState = 0;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);
	ee = AH_PRIVATE(ah)->ah_eeprom;

	OS_MARK(ah, AH_MARK_RESET, bChannelChange);
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
	HALASSERT(ahp->ah_eeversion >= AR_EEPROM_VER3);

	/* Preserve certain DMA hardware registers on a channel change */
	if (bChannelChange) {
		/*
		 * On Venice, the TSF is almost preserved across a reset;
		 * it requires the doubling writes to the RESET_TSF
		 * bit in the AR_BEACON register; it also has the quirk
		 * of the TSF going back in time on the station (station
		 * latches onto the last beacon's tsf during a reset 50%
		 * of the times); the latter is not a problem for adhoc
		 * stations since as long as the TSF is behind, it will
		 * get resynchronized on receiving the next beacon; the
		 * TSF going backwards in time could be a problem for the
		 * sleep operation (supported on infrastructure stations
		 * only) - the best and most general fix for this situation
		 * is to resynchronize the various sleep/beacon timers on
		 * the receipt of the next beacon i.e. when the TSF itself
		 * gets resynchronized to the AP's TSF - power save is
		 * needed to be temporarily disabled until that time
		 *
		 * Need to save the sequence number to restore it after
		 * the reset!
		 */
		saveFrameSeqCount = OS_REG_READ(ah, AR_D_SEQNUM);
	} else
		saveFrameSeqCount = 0;		/* NB: silence compiler */

	/* If the channel change is across the same mode - perform a fast channel change */
	if ((IS_2413(ah) || IS_5413(ah))) {
		/*
		 * Channel change can only be used when:
		 *  -channel change requested - so it's not the initial reset.
		 *  -it's not a change to the current channel - often called when switching modes
		 *   on a channel
		 *  -the modes of the previous and requested channel are the same - some ugly code for XR
		 */
		if (bChannelChange &&
		    AH_PRIVATE(ah)->ah_curchan != AH_NULL &&
		    (chan->ic_freq != AH_PRIVATE(ah)->ah_curchan->ic_freq) &&
		    ((chan->ic_flags & IEEE80211_CHAN_ALLTURBO) ==
		     (AH_PRIVATE(ah)->ah_curchan->ic_flags & IEEE80211_CHAN_ALLTURBO))) {
			if (ar5212ChannelChange(ah, chan))
				/* If ChannelChange completed - skip the rest of reset */
				return AH_TRUE;
		}
	}

	/*
	 * Preserve the antenna on a channel change
	 */
	saveDefAntenna = OS_REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)		/* XXX magic constants */
		saveDefAntenna = 1;

	/* Save hardware flag before chip reset clears the register */
	macStaId1 = OS_REG_READ(ah, AR_STA_ID1) & 
		(AR_STA_ID1_BASE_RATE_11B | AR_STA_ID1_USE_DEFANT);

	/* Save led state from pci config register */
	if (!IS_5315(ah))
		saveLedState = OS_REG_READ(ah, AR5312_PCICFG) &
			(AR_PCICFG_LEDCTL | AR_PCICFG_LEDMODE | AR_PCICFG_LEDBLINK |
			 AR_PCICFG_LEDSLOW);

	ar5312RestoreClock(ah, opmode);		/* move to refclk operation */

	/*
	 * Adjust gain parameters before reset if
	 * there's an outstanding gain updated.
	 */
	(void) ar5212GetRfgain(ah);

	if (!ar5312ChipReset(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Setup the indices for the next set of register array writes */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		freqIndex  = 2;
		modesIndex = IEEE80211_IS_CHAN_108G(chan) ? 5 :
			     IEEE80211_IS_CHAN_G(chan) ? 4 : 3;
	} else {
		freqIndex  = 1;
		modesIndex = IEEE80211_IS_CHAN_ST(chan) ? 2 : 1;
	}

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	/* Set correct Baseband to analog shift setting to access analog chips. */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	regWrites = ath_hal_ini_write(ah, &ahp->ah_ini_modes, modesIndex, 0);
	regWrites = write_common(ah, &ahp->ah_ini_common, bChannelChange,
		regWrites);
	ahp->ah_rfHal->writeRegs(ah, modesIndex, freqIndex, regWrites);

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	if (IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan))
		ar5212SetIFSTiming(ah, chan);

	/* Overwrite INI values for revised chipsets */
	if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_2) {
		/* ADC_CTL */
		OS_REG_WRITE(ah, AR_PHY_ADC_CTL,
			     SM(2, AR_PHY_ADC_CTL_OFF_INBUFGAIN) |
			     SM(2, AR_PHY_ADC_CTL_ON_INBUFGAIN) |
			     AR_PHY_ADC_CTL_OFF_PWDDAC |
			     AR_PHY_ADC_CTL_OFF_PWDADC);
		
		/* TX_PWR_ADJ */
		if (chan->channel == 2484) {
			cckOfdmPwrDelta = SCALE_OC_DELTA(ee->ee_cckOfdmPwrDelta - ee->ee_scaledCh14FilterCckDelta);
		} else {
			cckOfdmPwrDelta = SCALE_OC_DELTA(ee->ee_cckOfdmPwrDelta);
		}
		
		if (IEEE80211_IS_CHAN_G(chan)) {
			OS_REG_WRITE(ah, AR_PHY_TXPWRADJ,
				     SM((ee->ee_cckOfdmPwrDelta*-1), AR_PHY_TXPWRADJ_CCK_GAIN_DELTA) |
				     SM((cckOfdmPwrDelta*-1), AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX));
		} else {
			OS_REG_WRITE(ah, AR_PHY_TXPWRADJ, 0);
		}
		
		/* Add barker RSSI thresh enable as disabled */
		OS_REG_CLR_BIT(ah, AR_PHY_DAG_CTRLCCK,
			       AR_PHY_DAG_CTRLCCK_EN_RSSI_THR);
		OS_REG_RMW_FIELD(ah, AR_PHY_DAG_CTRLCCK,
				 AR_PHY_DAG_CTRLCCK_RSSI_THR, 2);
		
		/* Set the mute mask to the correct default */
		OS_REG_WRITE(ah, AR_SEQ_MASK, 0x0000000F);
	}
	
	if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_3) {
		/* Clear reg to alllow RX_CLEAR line debug */
		OS_REG_WRITE(ah, AR_PHY_BLUETOOTH,  0);
	}
	if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_4) {
#ifdef notyet
		/* Enable burst prefetch for the data queues */
		OS_REG_RMW_FIELD(ah, AR_D_FPCTL, ... );
		/* Enable double-buffering */
		OS_REG_CLR_BIT(ah, AR_TXCFG, AR_TXCFG_DBL_BUF_DIS);
#endif
	}

	if (IS_5312_2_X(ah)) {
		/* ADC_CTRL */
		OS_REG_WRITE(ah, AR_PHY_SIGMA_DELTA,
			     SM(2, AR_PHY_SIGMA_DELTA_ADC_SEL) |
			     SM(4, AR_PHY_SIGMA_DELTA_FILT2) |
			     SM(0x16, AR_PHY_SIGMA_DELTA_FILT1) |
			     SM(0, AR_PHY_SIGMA_DELTA_ADC_CLIP));

		if (IEEE80211_IS_CHAN_2GHZ(chan))
			OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN, AR_PHY_RXGAIN_TXRX_RF_MAX, 0x0F);

		/* CCK Short parameter adjustment in 11B mode */
		if (IEEE80211_IS_CHAN_B(chan))
			OS_REG_RMW_FIELD(ah, AR_PHY_CCK_RXCTRL4, AR_PHY_CCK_RXCTRL4_FREQ_EST_SHORT, 12);

		/* Set ADC/DAC select values */
		OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x04);

		/* Increase 11A AGC Settling */
		if (IEEE80211_IS_CHAN_A(chan))
			OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING, AR_PHY_SETTLING_AGC, 32);
	} else {
		/* Set ADC/DAC select values */
		OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x0e);
	}

	/* Setup the transmit power values. */
	if (!ar5212SetTransmitPower(ah, chan, rfXpdGain)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error init'ing transmit power\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Write the analog registers */
	if (!ahp->ah_rfHal->setRfRegs(ah, chan, modesIndex, rfXpdGain)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: ar5212SetRfRegs failed\n",
		    __func__);
		FAIL(HAL_EIO);
	}

	/* Write delta slope for OFDM enabled modes (A, G, Turbo) */
	if (IEEE80211_IS_CHAN_OFDM(chan)) {
		if (IS_5413(ah) ||
		   AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER5_3)
			ar5212SetSpurMitigation(ah, chan);
		ar5212SetDeltaSlope(ah, chan);
	}

	/* Setup board specific options for EEPROM version 3 */
	if (!ar5212SetBoardValues(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error setting board options\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Restore certain DMA hardware registers on a channel change */
	if (bChannelChange)
		OS_REG_WRITE(ah, AR_D_SEQNUM, saveFrameSeqCount);

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	OS_REG_WRITE(ah, AR_STA_ID0, LE_READ_4(ahp->ah_macaddr));
	OS_REG_WRITE(ah, AR_STA_ID1, LE_READ_2(ahp->ah_macaddr + 4)
		| macStaId1
		| AR_STA_ID1_RTS_USE_DEF
		| ahp->ah_staId1Defaults
	);
	ar5212SetOperatingMode(ah, opmode);

	/* Set Venice BSSID mask according to current state */
	OS_REG_WRITE(ah, AR_BSSMSKL, LE_READ_4(ahp->ah_bssidmask));
	OS_REG_WRITE(ah, AR_BSSMSKU, LE_READ_2(ahp->ah_bssidmask + 4));

	/* Restore previous led state */
	if (!IS_5315(ah))
		OS_REG_WRITE(ah, AR5312_PCICFG, OS_REG_READ(ah, AR_PCICFG) | saveLedState);

	/* Restore previous antenna */
	OS_REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	/* then our BSSID */
	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid + 4));

	/* Restore bmiss rssi & count thresholds */
	OS_REG_WRITE(ah, AR_RSSI_THR, ahp->ah_rssiThr);

	OS_REG_WRITE(ah, AR_ISR, ~0);		/* cleared on write */

	if (!ar5212SetChannel(ah, chan))
		FAIL(HAL_EIO);

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	ar5212SetCoverageClass(ah, AH_PRIVATE(ah)->ah_coverageClass, 1);

	ar5212SetRateDurationTable(ah, chan);

	/* Set Tx frame start to tx data start delay */
	if (IS_RAD5112_ANY(ah) &&
	    (IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan))) {
		txFrm2TxDStart = 
			IEEE80211_IS_CHAN_HALF(chan) ?
					TX_FRAME_D_START_HALF_RATE:
					TX_FRAME_D_START_QUARTER_RATE;
		OS_REG_RMW_FIELD(ah, AR_PHY_TX_CTL, 
			AR_PHY_TX_FRAME_TO_TX_DATA_START, txFrm2TxDStart);
	}

	/*
	 * Setup fast diversity.
	 * Fast diversity can be enabled or disabled via regadd.txt.
	 * Default is enabled.
	 * For reference,
	 *    Disable: reg        val
	 *             0x00009860 0x00009d18 (if 11a / 11g, else no change)
	 *             0x00009970 0x192bb514
	 *             0x0000a208 0xd03e4648
	 *
	 *    Enable:  0x00009860 0x00009d10 (if 11a / 11g, else no change)
	 *             0x00009970 0x192fb514
	 *             0x0000a208 0xd03e6788
	 */

	/* XXX Setup pre PHY ENABLE EAR additions */

	/* flush SCAL reg */
	if (IS_5312_2_X(ah)) {
		(void) OS_REG_READ(ah, AR_PHY_SLEEP_SCAL);
	}

	/*
	 * Wait for the frequency synth to settle (synth goes on
	 * via AR_PHY_ACTIVE_EN).  Read the phy active delay register.
	 * Value is in 100ns increments.
	 */
	synthDelay = OS_REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IEEE80211_IS_CHAN_B(chan)) {
		synthDelay = (4 * synthDelay) / 22;
	} else {
		synthDelay /= 10;
	}

	/* Activate the PHY (includes baseband activate and synthesizer on) */
	OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/* 
	 * There is an issue if the AP starts the calibration before
	 * the base band timeout completes.  This could result in the
	 * rx_clear false triggering.  As a workaround we add delay an
	 * extra BASE_ACTIVATE_DELAY usecs to ensure this condition
	 * does not happen.
	 */
	if (IEEE80211_IS_CHAN_HALF(chan)) {
		OS_DELAY((synthDelay << 1) + BASE_ACTIVATE_DELAY);
	} else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
		OS_DELAY((synthDelay << 2) + BASE_ACTIVATE_DELAY);
	} else {
		OS_DELAY(synthDelay + BASE_ACTIVATE_DELAY);
	}

	/*
	 * The udelay method is not reliable with notebooks.
	 * Need to check to see if the baseband is ready
	 */
	testReg = OS_REG_READ(ah, AR_PHY_TESTCTRL);
	/* Selects the Tx hold */
	OS_REG_WRITE(ah, AR_PHY_TESTCTRL, AR_PHY_TESTCTRL_TXHOLD);
	i = 0;
	while ((i++ < 20) &&
	       (OS_REG_READ(ah, 0x9c24) & 0x10)) /* test if baseband not ready */		OS_DELAY(200);
	OS_REG_WRITE(ah, AR_PHY_TESTCTRL, testReg);

	/* Calibrate the AGC and start a NF calculation */
	OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		  OS_REG_READ(ah, AR_PHY_AGC_CONTROL)
		| AR_PHY_AGC_CONTROL_CAL
		| AR_PHY_AGC_CONTROL_NF);

	if (!IEEE80211_IS_CHAN_B(chan) && ahp->ah_bIQCalibration != IQ_CAL_DONE) {
		/* Start IQ calibration w/ 2^(INIT_IQCAL_LOG_COUNT_MAX+1) samples */
		OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4, 
			AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,
			INIT_IQCAL_LOG_COUNT_MAX);
		OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4,
			AR_PHY_TIMING_CTRL4_DO_IQCAL);
		ahp->ah_bIQCalibration = IQ_CAL_RUNNING;
	} else
		ahp->ah_bIQCalibration = IQ_CAL_INACTIVE;

	/* Setup compression registers */
	ar5212SetCompRegs(ah);

	/* Set 1:1 QCU to DCU mapping for all queues */
	for (i = 0; i < AR_NUM_DCU; i++)
		OS_REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	ahp->ah_intrTxqs = 0;
	for (i = 0; i < AH_PRIVATE(ah)->ah_caps.halTotalQueues; i++)
		ar5212ResetTxQueue(ah, i);

	/*
	 * Setup interrupt handling.  Note that ar5212ResetTxQueue
	 * manipulates the secondary IMR's as queues are enabled
	 * and disabled.  This is done with RMW ops to insure the
	 * settings we make here are preserved.
	 */
	ahp->ah_maskReg = AR_IMR_TXOK | AR_IMR_TXERR | AR_IMR_TXURN
			| AR_IMR_RXOK | AR_IMR_RXERR | AR_IMR_RXORN
			| AR_IMR_HIUERR
			;
	if (opmode == HAL_M_HOSTAP)
		ahp->ah_maskReg |= AR_IMR_MIB;
	OS_REG_WRITE(ah, AR_IMR, ahp->ah_maskReg);
	/* Enable bus errors that are OR'd to set the HIUERR bit */
	OS_REG_WRITE(ah, AR_IMR_S2,
		OS_REG_READ(ah, AR_IMR_S2)
		| AR_IMR_S2_MCABT | AR_IMR_S2_SSERR | AR_IMR_S2_DPERR);

	if (AH_PRIVATE(ah)->ah_rfkillEnabled)
		ar5212EnableRfKill(ah);

	if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: offset calibration failed to complete in 1ms;"
		    " noisy environment?\n", __func__);
	}

	/*
	 * Set clocks back to 32kHz if they had been using refClk, then
	 * use an external 32kHz crystal when sleeping, if one exists.
	 */
	ar5312SetupClock(ah, opmode);

	/*
	 * Writing to AR_BEACON will start timers. Hence it should
	 * be the last register to be written. Do not reset tsf, do
	 * not enable beacons at this point, but preserve other values
	 * like beaconInterval.
	 */
	OS_REG_WRITE(ah, AR_BEACON,
		(OS_REG_READ(ah, AR_BEACON) &~ (AR_BEACON_EN | AR_BEACON_RESET_TSF)));

	/* XXX Setup post reset EAR additions */

	/*  QoS support */
	if (AH_PRIVATE(ah)->ah_macVersion > AR_SREV_VERSION_VENICE ||
	    (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_VENICE &&
	     AH_PRIVATE(ah)->ah_macRev >= AR_SREV_GRIFFIN_LITE)) {
		OS_REG_WRITE(ah, AR_QOS_CONTROL, 0x100aa);	/* XXX magic */
		OS_REG_WRITE(ah, AR_QOS_SELECT, 0x3210);	/* XXX magic */
	}

	/* Turn on NOACK Support for QoS packets */
	OS_REG_WRITE(ah, AR_NOACK,
		     SM(2, AR_NOACK_2BIT_VALUE) |
		     SM(5, AR_NOACK_BIT_OFFSET) |
		     SM(0, AR_NOACK_BYTE_OFFSET));

	/* Restore user-specified settings */
	if (ahp->ah_miscMode != 0)
		OS_REG_WRITE(ah, AR_MISC_MODE, ahp->ah_miscMode);
	if (ahp->ah_slottime != (u_int) -1)
		ar5212SetSlotTime(ah, ahp->ah_slottime);
	if (ahp->ah_acktimeout != (u_int) -1)
		ar5212SetAckTimeout(ah, ahp->ah_acktimeout);
	if (ahp->ah_ctstimeout != (u_int) -1)
		ar5212SetCTSTimeout(ah, ahp->ah_ctstimeout);
	if (ahp->ah_sifstime != (u_int) -1)
		ar5212SetSifsTime(ah, ahp->ah_sifstime);
	if (AH_PRIVATE(ah)->ah_diagreg != 0)
		OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);

	AH_PRIVATE(ah)->ah_opmode = opmode;	/* record operating mode */

	if (bChannelChange && !IEEE80211_IS_CHAN_DFS(chan)) 
		chan->ic_state &= ~IEEE80211_CHANSTATE_CWINT;

	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: done\n", __func__);

	OS_MARK(ah, AH_MARK_RESET_DONE, 0);

	return AH_TRUE;
bad:
	OS_MARK(ah, AH_MARK_RESET_DONE, ecode);
	if (status != AH_NULL)
		*status = ecode;
	return AH_FALSE;
#undef FAIL
#undef N
}

/*
 * Places the PHY and Radio chips into reset.  A full reset
 * must be called to leave this state.  The PCI/MAC/PCU are
 * not placed into reset as we must receive interrupt to
 * re-enable the hardware.
 */
HAL_BOOL
ar5312PhyDisable(struct ath_hal *ah)
{
    return ar5312SetResetReg(ah, AR_RC_BB);
}

/*
 * Places all of hardware into reset
 */
HAL_BOOL
ar5312Disable(struct ath_hal *ah)
{
	if (!ar5312SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;
	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset.
	 */
	return ar5312SetResetReg(ah, AR_RC_MAC | AR_RC_BB);
}

/*
 * Places the hardware into reset and then pulls it out of reset
 *
 * TODO: Only write the PLL if we're changing to or from CCK mode
 * 
 * WARNING: The order of the PLL and mode registers must be correct.
 */
HAL_BOOL
ar5312ChipReset(struct ath_hal *ah, const struct ieee80211_channel *chan)
{

	OS_MARK(ah, AH_MARK_CHIPRESET, chan ? chan->ic_freq : 0);

	/*
	 * Reset the HW 
	 */
	if (!ar5312SetResetReg(ah, AR_RC_MAC | AR_RC_BB)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: ar5312SetResetReg failed\n",
		    __func__);
		return AH_FALSE;
	}

	/* Bring out of sleep mode (AGAIN) */
	if (!ar5312SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: ar5312SetPowerMode failed\n",
		    __func__);
		return AH_FALSE;
	}

	/* Clear warm reset register */
	if (!ar5312SetResetReg(ah, 0)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: ar5312SetResetReg failed\n",
		    __func__);
		return AH_FALSE;
	}

	/*
	 * Perform warm reset before the mode/PLL/turbo registers
	 * are changed in order to deactivate the radio.  Mode changes
	 * with an active radio can result in corrupted shifts to the
	 * radio device.
	 */

	/*
	 * Set CCK and Turbo modes correctly.
	 */
	if (chan != AH_NULL) {		/* NB: can be null during attach */
		uint32_t rfMode, phyPLL = 0, curPhyPLL, turbo;

		if (IS_RAD5112_ANY(ah)) {
			rfMode = AR_PHY_MODE_AR5112;
			if (!IS_5315(ah)) {
				if (IEEE80211_IS_CHAN_CCK(chan)) {
					phyPLL = AR_PHY_PLL_CTL_44_5312;
				} else {
					if (IEEE80211_IS_CHAN_HALF(chan)) {
						phyPLL = AR_PHY_PLL_CTL_40_5312_HALF;
					} else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
						phyPLL = AR_PHY_PLL_CTL_40_5312_QUARTER;
					} else {
						phyPLL = AR_PHY_PLL_CTL_40_5312;
					}
				}
			} else {
				if (IEEE80211_IS_CHAN_CCK(chan))
					phyPLL = AR_PHY_PLL_CTL_44_5112;
				else
					phyPLL = AR_PHY_PLL_CTL_40_5112;
				if (IEEE80211_IS_CHAN_HALF(chan))
					phyPLL |= AR_PHY_PLL_CTL_HALF;
				else if (IEEE80211_IS_CHAN_QUARTER(chan))
					phyPLL |= AR_PHY_PLL_CTL_QUARTER;
			}
		} else {
			rfMode = AR_PHY_MODE_AR5111;
			if (IEEE80211_IS_CHAN_CCK(chan))
				phyPLL = AR_PHY_PLL_CTL_44;
			else
				phyPLL = AR_PHY_PLL_CTL_40;
			if (IEEE80211_IS_CHAN_HALF(chan))
				phyPLL = AR_PHY_PLL_CTL_HALF;
			else if (IEEE80211_IS_CHAN_QUARTER(chan))
				phyPLL = AR_PHY_PLL_CTL_QUARTER;
		}
		if (IEEE80211_IS_CHAN_G(chan))
			rfMode |= AR_PHY_MODE_DYNAMIC;
		else if (IEEE80211_IS_CHAN_OFDM(chan))
			rfMode |= AR_PHY_MODE_OFDM;
		else
			rfMode |= AR_PHY_MODE_CCK;
		if (IEEE80211_IS_CHAN_5GHZ(chan))
			rfMode |= AR_PHY_MODE_RF5GHZ;
		else
			rfMode |= AR_PHY_MODE_RF2GHZ;
		turbo = IEEE80211_IS_CHAN_TURBO(chan) ?
			(AR_PHY_FC_TURBO_MODE | AR_PHY_FC_TURBO_SHORT) : 0;
		curPhyPLL = OS_REG_READ(ah, AR_PHY_PLL_CTL);
		/*
		 * PLL, Mode, and Turbo values must be written in the correct
		 * order to ensure:
		 * - The PLL cannot be set to 44 unless the CCK or DYNAMIC
		 *   mode bit is set
		 * - Turbo cannot be set at the same time as CCK or DYNAMIC
		 */
		if (IEEE80211_IS_CHAN_CCK(chan)) {
			OS_REG_WRITE(ah, AR_PHY_TURBO, turbo);
			OS_REG_WRITE(ah, AR_PHY_MODE, rfMode);
			if (curPhyPLL != phyPLL) {
				OS_REG_WRITE(ah,  AR_PHY_PLL_CTL,  phyPLL);
				/* Wait for the PLL to settle */
				OS_DELAY(PLL_SETTLE_DELAY);
			}
		} else {
			if (curPhyPLL != phyPLL) {
				OS_REG_WRITE(ah,  AR_PHY_PLL_CTL,  phyPLL);
				/* Wait for the PLL to settle */
				OS_DELAY(PLL_SETTLE_DELAY);
			}
			OS_REG_WRITE(ah, AR_PHY_TURBO, turbo);
			OS_REG_WRITE(ah, AR_PHY_MODE, rfMode);
		}
	}
	return AH_TRUE;
}

/*
 * Write the given reset bit mask into the reset register
 */
static HAL_BOOL
ar5312SetResetReg(struct ath_hal *ah, uint32_t resetMask)
{
	uint32_t mask = resetMask ? resetMask : ~0;
	HAL_BOOL rt;

        if ((rt = ar5312MacReset(ah, mask)) == AH_FALSE) {
		return rt;
	}
        if ((resetMask & AR_RC_MAC) == 0) {
		if (isBigEndian()) {
			/*
			 * Set CFG, little-endian for descriptor accesses.
			 */
#ifdef AH_NEED_DESC_SWAP
			mask = INIT_CONFIG_STATUS | AR_CFG_SWRD;
#else
			mask = INIT_CONFIG_STATUS |
                                AR_CFG_SWTD | AR_CFG_SWRD;
#endif
			OS_REG_WRITE(ah, AR_CFG, mask);
		} else
			OS_REG_WRITE(ah, AR_CFG, INIT_CONFIG_STATUS);
	}
	return rt;
}

/*
 * ar5312MacReset resets (and then un-resets) the specified
 * wireless components.
 * Note: The RCMask cannot be zero on entering from ar5312SetResetReg.
 */

HAL_BOOL
ar5312MacReset(struct ath_hal *ah, unsigned int RCMask)
{
	int wlanNum = AR5312_UNIT(ah);
	uint32_t resetBB, resetBits, regMask;
	uint32_t reg;

	if (RCMask == 0)
		return(AH_FALSE);
#if ( AH_SUPPORT_2316 || AH_SUPPORT_2317 )
	    if (IS_5315(ah)) {
			switch(wlanNum) {
			case 0:
				resetBB = AR5315_RC_BB0_CRES | AR5315_RC_WBB0_RES; 
				/* Warm and cold reset bits for wbb */
				resetBits = AR5315_RC_WMAC0_RES;
				break;
			case 1:
				resetBB = AR5315_RC_BB1_CRES | AR5315_RC_WBB1_RES; 
				/* Warm and cold reset bits for wbb */
				resetBits = AR5315_RC_WMAC1_RES;
				break;
			default:
				return(AH_FALSE);
			}		
			regMask = ~(resetBB | resetBits);

			/* read before */
			reg = OS_REG_READ(ah, 
							  (AR5315_RSTIMER_BASE - ((uint32_t) ah->ah_sh) + AR5315_RESET));

			if (RCMask == AR_RC_BB) {
				/* Put baseband in reset */
				reg |= resetBB;    /* Cold and warm reset the baseband bits */
			} else {
				/*
				 * Reset the MAC and baseband.  This is a bit different than
				 * the PCI version, but holding in reset causes problems.
				 */
				reg &= regMask;
				reg |= (resetBits | resetBB) ;
			}
			OS_REG_WRITE(ah, 
						 (AR5315_RSTIMER_BASE - ((uint32_t) ah->ah_sh)+AR5315_RESET),
						 reg);
			/* read after */
			OS_REG_READ(ah, 
						(AR5315_RSTIMER_BASE - ((uint32_t) ah->ah_sh) +AR5315_RESET));
			OS_DELAY(100);

			/* Bring MAC and baseband out of reset */
			reg &= regMask;
			/* read before */
			OS_REG_READ(ah, 
						(AR5315_RSTIMER_BASE- ((uint32_t) ah->ah_sh) +AR5315_RESET));
			OS_REG_WRITE(ah, 
						 (AR5315_RSTIMER_BASE - ((uint32_t) ah->ah_sh)+AR5315_RESET),
						 reg);
			/* read after */
			OS_REG_READ(ah,
						(AR5315_RSTIMER_BASE- ((uint32_t) ah->ah_sh) +AR5315_RESET));


		} 
        else 
#endif
		{

			switch(wlanNum) {
			case 0:
				resetBB = AR5312_RC_BB0_CRES | AR5312_RC_WBB0_RES;
				/* Warm and cold reset bits for wbb */
				resetBits = AR5312_RC_WMAC0_RES;
				break;
			case 1:
				resetBB = AR5312_RC_BB1_CRES | AR5312_RC_WBB1_RES;
				/* Warm and cold reset bits for wbb */
				resetBits = AR5312_RC_WMAC1_RES;
				break;
			default:
				return(AH_FALSE);
			}
			regMask = ~(resetBB | resetBits);

			/* read before */
			reg = OS_REG_READ(ah,
							  (AR5312_RSTIMER_BASE - ((uint32_t) ah->ah_sh) + AR5312_RESET));

			if (RCMask == AR_RC_BB) {
				/* Put baseband in reset */
				reg |= resetBB;    /* Cold and warm reset the baseband bits */
			} else {
				/*
				 * Reset the MAC and baseband.  This is a bit different than
				 * the PCI version, but holding in reset causes problems.
				 */
				reg &= regMask;
				reg |= (resetBits | resetBB) ;
			}
			OS_REG_WRITE(ah,
						 (AR5312_RSTIMER_BASE - ((uint32_t) ah->ah_sh)+AR5312_RESET),
						 reg);
			/* read after */
			OS_REG_READ(ah,
						(AR5312_RSTIMER_BASE - ((uint32_t) ah->ah_sh) +AR5312_RESET));
			OS_DELAY(100);

			/* Bring MAC and baseband out of reset */
			reg &= regMask;
			/* read before */
			OS_REG_READ(ah,
						(AR5312_RSTIMER_BASE- ((uint32_t) ah->ah_sh) +AR5312_RESET));
			OS_REG_WRITE(ah,
						 (AR5312_RSTIMER_BASE - ((uint32_t) ah->ah_sh)+AR5312_RESET),
						 reg);
			/* read after */
			OS_REG_READ(ah,
						(AR5312_RSTIMER_BASE- ((uint32_t) ah->ah_sh) +AR5312_RESET));
		}
	return(AH_TRUE);
}

#endif /* AH_SUPPORT_AR5312 */
