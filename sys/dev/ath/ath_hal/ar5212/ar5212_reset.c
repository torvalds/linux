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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212phy.h"

#include "ah_eeprom_v3.h"

/* Additional Time delay to wait after activiting the Base band */
#define BASE_ACTIVATE_DELAY	100	/* 100 usec */
#define PLL_SETTLE_DELAY	300	/* 300 usec */

static HAL_BOOL ar5212SetResetReg(struct ath_hal *, uint32_t resetMask);
/* NB: public for 5312 use */
HAL_BOOL	ar5212IsSpurChannel(struct ath_hal *,
		    const struct ieee80211_channel *);
HAL_BOOL	ar5212ChannelChange(struct ath_hal *,
		    const struct ieee80211_channel *);
int16_t		ar5212GetNf(struct ath_hal *, struct ieee80211_channel *);
HAL_BOOL	ar5212SetBoardValues(struct ath_hal *,
		    const struct ieee80211_channel *);
void		ar5212SetDeltaSlope(struct ath_hal *,
		    const struct ieee80211_channel *);
HAL_BOOL	ar5212SetTransmitPower(struct ath_hal *ah,
		   const struct ieee80211_channel *chan, uint16_t *rfXpdGain);
static HAL_BOOL ar5212SetRateTable(struct ath_hal *, 
		   const struct ieee80211_channel *, int16_t tpcScaleReduction,
		   int16_t powerLimit,
		   HAL_BOOL commit, int16_t *minPower, int16_t *maxPower);
static void ar5212CorrectGainDelta(struct ath_hal *, int twiceOfdmCckDelta);
static void ar5212GetTargetPowers(struct ath_hal *,
		   const struct ieee80211_channel *,
		   const TRGT_POWER_INFO *pPowerInfo, uint16_t numChannels,
		   TRGT_POWER_INFO *pNewPower);
static uint16_t ar5212GetMaxEdgePower(uint16_t channel,
		   const RD_EDGES_POWER  *pRdEdgesPower);
void		ar5212SetRateDurationTable(struct ath_hal *,
		    const struct ieee80211_channel *);
void		ar5212SetIFSTiming(struct ath_hal *,
		    const struct ieee80211_channel *);

/* NB: public for RF backend use */
void		ar5212GetLowerUpperValues(uint16_t value,
		   uint16_t *pList, uint16_t listSize,
		   uint16_t *pLowerValue, uint16_t *pUpperValue);
void		ar5212ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32,
		   uint32_t numBits, uint32_t firstBit, uint32_t column);

static int
write_common(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
	HAL_BOOL bChannelChange, int writes)
{
#define IS_NO_RESET_TIMER_ADDR(x)                      \
    ( (((x) >= AR_BEACON) && ((x) <= AR_CFP_DUR)) || \
      (((x) >= AR_SLEEP1) && ((x) <= AR_SLEEP3)))
#define	V(r, c)	(ia)->data[((r)*(ia)->cols) + (c)]
	int r;

	/* Write Common Array Parameters */
	for (r = 0; r < ia->rows; r++) {
		uint32_t reg = V(r, 0);
		/* XXX timer/beacon setup registers? */
		/* On channel change, don't reset the PCU registers */
		if (!(bChannelChange && IS_NO_RESET_TIMER_ADDR(reg))) {
			OS_REG_WRITE(ah, reg, V(r, 1));
			DMA_YIELD(writes);
		}
	}
	return writes;
#undef IS_NO_RESET_TIMER_ADDR
#undef V
}

#define IS_DISABLE_FAST_ADC_CHAN(x) (((x) == 2462) || ((x) == 2467))

/*
 * XXX NDIS 5.x code had MAX_RESET_WAIT set to 2000 for AP code
 * and 10 for Client code
 */
#define	MAX_RESET_WAIT			10

#define	TX_QUEUEPEND_CHECK		1
#define	TX_ENABLE_CHECK			2
#define	RX_ENABLE_CHECK			4

/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 *
 * bChannelChange is used to preserve DMA/PCU registers across
 * a HW Reset during channel change.
 */
HAL_BOOL
ar5212Reset(struct ath_hal *ah, HAL_OPMODE opmode,
	struct ieee80211_channel *chan,
	HAL_BOOL bChannelChange,
	HAL_RESET_TYPE resetType,
	HAL_STATUS *status)
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
#define	FAIL(_code)	do { ecode = _code; goto bad; } while (0)
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_CHANNEL_INTERNAL *ichan = AH_NULL;
	const HAL_EEPROM *ee;
	uint32_t softLedCfg, softLedState;
	uint32_t saveFrameSeqCount, saveDefAntenna, saveLedState;
	uint32_t macStaId1, synthDelay, txFrm2TxDStart;
	uint16_t rfXpdGain[MAX_NUM_PDGAINS_PER_CHANNEL];
	int16_t cckOfdmPwrDelta = 0;
	u_int modesIndex, freqIndex;
	HAL_STATUS ecode;
	int i, regWrites;
	uint32_t testReg, powerVal;
	int8_t twiceAntennaGain, twiceAntennaReduction;
	uint32_t ackTpcPow, ctsTpcPow, chirpTpcPow;
	HAL_BOOL isBmode = AH_FALSE;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);
	ee = AH_PRIVATE(ah)->ah_eeprom;

	OS_MARK(ah, AH_MARK_RESET, bChannelChange);

	/* Bring out of sleep mode */
	if (!ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip did not wakeup\n",
		    __func__);
		FAIL(HAL_EIO);
	}

	/*
	 * Map public channel to private.
	 */
	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL)
		FAIL(HAL_EINVAL);
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
	HALASSERT(AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER3);

	SAVE_CCK(ah, chan, isBmode);

	/* Preserve certain DMA hardware registers on a channel change */
	if (bChannelChange) {
		/*
		 * On Venice, the TSF is almost preserved across a reset;
		 * it requires doubling writes to the RESET_TSF
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

	/* Blank the channel survey statistics */
	ath_hal_survey_clear(ah);

#if 0
	/*
	 * XXX disable for now; this appears to sometimes cause OFDM
	 * XXX timing error floods when ani is enabled and bg scanning
	 * XXX kicks in
	 */
	/* If the channel change is across the same mode - perform a fast channel change */
	if (IS_2413(ah) || IS_5413(ah)) {
		/*
		 * Fast channel change can only be used when:
		 *  -channel change requested - so it's not the initial reset.
		 *  -it's not a change to the current channel -
		 *	often called when switching modes on a channel
		 *  -the modes of the previous and requested channel are the
		 *	same
		 * XXX opmode shouldn't change either?
		 */
		if (bChannelChange &&
		    (AH_PRIVATE(ah)->ah_curchan != AH_NULL) &&
		    (chan->ic_freq != AH_PRIVATE(ah)->ah_curchan->ic_freq) &&
		    ((chan->ic_flags & IEEE80211_CHAN_ALLTURBO) ==
		     (AH_PRIVATE(ah)->ah_curchan->ic_flags & IEEE80211_CHAN_ALLTURBO))) {
			if (ar5212ChannelChange(ah, chan)) {
				/* If ChannelChange completed - skip the rest of reset */
				/* XXX ani? */
				goto done;
			}
		}
	}
#endif
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
	saveLedState = OS_REG_READ(ah, AR_PCICFG) &
		(AR_PCICFG_LEDCTL | AR_PCICFG_LEDMODE | AR_PCICFG_LEDBLINK |
		 AR_PCICFG_LEDSLOW);
	softLedCfg = OS_REG_READ(ah, AR_GPIOCR);
	softLedState = OS_REG_READ(ah, AR_GPIODO);

	ar5212RestoreClock(ah, opmode);		/* move to refclk operation */

	/*
	 * Adjust gain parameters before reset if
	 * there's an outstanding gain updated.
	 */
	(void) ar5212GetRfgain(ah);

	if (!ar5212ChipReset(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Setup the indices for the next set of register array writes */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		freqIndex  = 2;
		if (IEEE80211_IS_CHAN_108G(chan))
			modesIndex = 5;
		else if (IEEE80211_IS_CHAN_G(chan))
			modesIndex = 4;
		else if (IEEE80211_IS_CHAN_B(chan))
			modesIndex = 3;
		else {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid channel %u/0x%x\n",
			    __func__, chan->ic_freq, chan->ic_flags);
			FAIL(HAL_EINVAL);
		}
	} else {
		freqIndex  = 1;
		if (IEEE80211_IS_CHAN_TURBO(chan))
			modesIndex = 2;
		else if (IEEE80211_IS_CHAN_A(chan))
			modesIndex = 1;
		else {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid channel %u/0x%x\n",
			    __func__, chan->ic_freq, chan->ic_flags);
			FAIL(HAL_EINVAL);
		}
	}

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	/* Set correct Baseband to analog shift setting to access analog chips. */
	OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);

	regWrites = ath_hal_ini_write(ah, &ahp->ah_ini_modes, modesIndex, 0);
	regWrites = write_common(ah, &ahp->ah_ini_common, bChannelChange,
		regWrites);
#ifdef AH_RXCFG_SDMAMW_4BYTES
	/*
	 * Nala doesn't work with 128 byte bursts on pb42(hydra) (ar71xx),
	 * use 4 instead.  Enabling it on all platforms would hurt performance,
	 * so we only enable it on the ones that are affected by it.
	 */
	OS_REG_WRITE(ah, AR_RXCFG, 0);
#endif
	ahp->ah_rfHal->writeRegs(ah, modesIndex, freqIndex, regWrites);

	OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

	if (IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan)) {
		ar5212SetIFSTiming(ah, chan);
		if (IS_5413(ah)) {
			/*
			 * Force window_length for 1/2 and 1/4 rate channels,
			 * the ini file sets this to zero otherwise.
			 */
			OS_REG_RMW_FIELD(ah, AR_PHY_FRAME_CTL,
				AR_PHY_FRAME_CTL_WINLEN, 3);
		}
	}

	/* Overwrite INI values for revised chipsets */
	if (AH_PRIVATE(ah)->ah_phyRev >= AR_PHY_CHIP_ID_REV_2) {
		/* ADC_CTL */
		OS_REG_WRITE(ah, AR_PHY_ADC_CTL,
			SM(2, AR_PHY_ADC_CTL_OFF_INBUFGAIN) |
			SM(2, AR_PHY_ADC_CTL_ON_INBUFGAIN) |
			AR_PHY_ADC_CTL_OFF_PWDDAC |
			AR_PHY_ADC_CTL_OFF_PWDADC);

		/* TX_PWR_ADJ */
		if (ichan->channel == 2484) {
			cckOfdmPwrDelta = SCALE_OC_DELTA(
			    ee->ee_cckOfdmPwrDelta -
			    ee->ee_scaledCh14FilterCckDelta);
		} else {
			cckOfdmPwrDelta = SCALE_OC_DELTA(
			    ee->ee_cckOfdmPwrDelta);
		}

		if (IEEE80211_IS_CHAN_G(chan)) {
		    OS_REG_WRITE(ah, AR_PHY_TXPWRADJ,
			SM((ee->ee_cckOfdmPwrDelta*-1),
			    AR_PHY_TXPWRADJ_CCK_GAIN_DELTA) |
			SM((cckOfdmPwrDelta*-1),
			    AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX));
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

	/* Set ADC/DAC select values */
	OS_REG_WRITE(ah, AR_PHY_SLEEP_SCAL, 0x0e);

	if (IS_5413(ah) || IS_2417(ah)) {
		uint32_t newReg = 1;
		if (IS_DISABLE_FAST_ADC_CHAN(ichan->channel))
			newReg = 0;
		/* As it's a clock changing register, only write when the value needs to be changed */
		if (OS_REG_READ(ah, AR_PHY_FAST_ADC) != newReg)
			OS_REG_WRITE(ah, AR_PHY_FAST_ADC, newReg);
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
	OS_REG_WRITE(ah, AR_PCICFG, OS_REG_READ(ah, AR_PCICFG) | saveLedState);

	/* Restore soft Led state to GPIO */
	OS_REG_WRITE(ah, AR_GPIOCR, softLedCfg);
	OS_REG_WRITE(ah, AR_GPIODO, softLedState);

	/* Restore previous antenna */
	OS_REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	/* then our BSSID and associate id */
	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid + 4) |
	    (ahp->ah_assocId & 0x3fff) << AR_BSS_ID1_AID_S);

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
	ar5212SetupClock(ah, opmode);

	/*
	 * Writing to AR_BEACON will start timers. Hence it should
	 * be the last register to be written. Do not reset tsf, do
	 * not enable beacons at this point, but preserve other values
	 * like beaconInterval.
	 */
	OS_REG_WRITE(ah, AR_BEACON,
		(OS_REG_READ(ah, AR_BEACON) &~ (AR_BEACON_EN | AR_BEACON_RESET_TSF)));

	/* XXX Setup post reset EAR additions */

	/* QoS support */
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

	/* Get Antenna Gain reduction */
	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		ath_hal_eepromGet(ah, AR_EEP_ANTGAINMAX_5, &twiceAntennaGain);
	} else {
		ath_hal_eepromGet(ah, AR_EEP_ANTGAINMAX_2, &twiceAntennaGain);
	}
	twiceAntennaReduction =
		ath_hal_getantennareduction(ah, chan, twiceAntennaGain);

	/* TPC for self-generated frames */

	ackTpcPow = MS(ahp->ah_macTPC, AR_TPC_ACK);
	if ((ackTpcPow-ahp->ah_txPowerIndexOffset) > chan->ic_maxpower)
		ackTpcPow = chan->ic_maxpower+ahp->ah_txPowerIndexOffset;

	if (ackTpcPow > (2*chan->ic_maxregpower - twiceAntennaReduction))
		ackTpcPow = (2*chan->ic_maxregpower - twiceAntennaReduction)
			+ ahp->ah_txPowerIndexOffset;

	ctsTpcPow = MS(ahp->ah_macTPC, AR_TPC_CTS);
	if ((ctsTpcPow-ahp->ah_txPowerIndexOffset) > chan->ic_maxpower)
		ctsTpcPow = chan->ic_maxpower+ahp->ah_txPowerIndexOffset;

	if (ctsTpcPow > (2*chan->ic_maxregpower - twiceAntennaReduction))
		ctsTpcPow = (2*chan->ic_maxregpower - twiceAntennaReduction)
			+ ahp->ah_txPowerIndexOffset;

	chirpTpcPow = MS(ahp->ah_macTPC, AR_TPC_CHIRP);
	if ((chirpTpcPow-ahp->ah_txPowerIndexOffset) > chan->ic_maxpower)
		chirpTpcPow = chan->ic_maxpower+ahp->ah_txPowerIndexOffset;

	if (chirpTpcPow > (2*chan->ic_maxregpower - twiceAntennaReduction))
		chirpTpcPow = (2*chan->ic_maxregpower - twiceAntennaReduction)
			+ ahp->ah_txPowerIndexOffset;

	if (ackTpcPow > 63)
		ackTpcPow = 63;
	if (ctsTpcPow > 63)
		ctsTpcPow = 63;
	if (chirpTpcPow > 63)
		chirpTpcPow = 63;

	powerVal = SM(ackTpcPow, AR_TPC_ACK) |
		SM(ctsTpcPow, AR_TPC_CTS) |
		SM(chirpTpcPow, AR_TPC_CHIRP);

	OS_REG_WRITE(ah, AR_TPC, powerVal);

	/* Restore user-specified settings */
	if (ahp->ah_miscMode != 0)
		OS_REG_WRITE(ah, AR_MISC_MODE, ahp->ah_miscMode);
	if (ahp->ah_sifstime != (u_int) -1)
		ar5212SetSifsTime(ah, ahp->ah_sifstime);
	if (ahp->ah_slottime != (u_int) -1)
		ar5212SetSlotTime(ah, ahp->ah_slottime);
	if (ahp->ah_acktimeout != (u_int) -1)
		ar5212SetAckTimeout(ah, ahp->ah_acktimeout);
	if (ahp->ah_ctstimeout != (u_int) -1)
		ar5212SetCTSTimeout(ah, ahp->ah_ctstimeout);
	if (AH_PRIVATE(ah)->ah_diagreg != 0)
		OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);

	AH_PRIVATE(ah)->ah_opmode = opmode;	/* record operating mode */
#if 0
done:
#endif
	if (bChannelChange && !IEEE80211_IS_CHAN_DFS(chan)) 
		chan->ic_state &= ~IEEE80211_CHANSTATE_CWINT;

	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: done\n", __func__);

	RESTORE_CCK(ah, chan, isBmode);
	
	OS_MARK(ah, AH_MARK_RESET_DONE, 0);

	return AH_TRUE;
bad:
	RESTORE_CCK(ah, chan, isBmode);

	OS_MARK(ah, AH_MARK_RESET_DONE, ecode);
	if (status != AH_NULL)
		*status = ecode;
	return AH_FALSE;
#undef FAIL
#undef N
}

/*
 * Call the rf backend to change the channel.
 */
HAL_BOOL
ar5212SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	/* Change the synth */
	if (!ahp->ah_rfHal->setChannel(ah, chan))
		return AH_FALSE;
	return AH_TRUE;
}

/*
 * This channel change evaluates whether the selected hardware can
 * perform a synthesizer-only channel change (no reset).  If the
 * TX is not stopped, or the RFBus cannot be granted in the given
 * time, the function returns false as a reset is necessary
 */
HAL_BOOL
ar5212ChannelChange(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t       ulCount;
	uint32_t   data, synthDelay, qnum;
	uint16_t   rfXpdGain[MAX_NUM_PDGAINS_PER_CHANNEL];
	HAL_BOOL    txStopped = AH_TRUE;
	HAL_CHANNEL_INTERNAL *ichan;

	/*
	 * Map public channel to private.
	 */
	ichan = ath_hal_checkchannel(ah, chan);

	/* TX must be stopped or RF Bus grant will not work */
	for (qnum = 0; qnum < AH_PRIVATE(ah)->ah_caps.halTotalQueues; qnum++) {
		if (ar5212NumTxPending(ah, qnum)) {
			txStopped = AH_FALSE;
			break;
		}
	}
	if (!txStopped)
		return AH_FALSE;

	/* Kill last Baseband Rx Frame */
	OS_REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_REQUEST); /* Request analog bus grant */
	for (ulCount = 0; ulCount < 100; ulCount++) {
		if (OS_REG_READ(ah, AR_PHY_RFBUS_GNT))
			break;
		OS_DELAY(5);
	}
	if (ulCount >= 100)
		return AH_FALSE;

	/* Change the synth */
	if (!ar5212SetChannel(ah, chan))
		return AH_FALSE;

	/*
	 * Wait for the frequency synth to settle (synth goes on via PHY_ACTIVE_EN).
	 * Read the phy active delay register. Value is in 100ns increments.
	 */
	data = OS_REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IEEE80211_IS_CHAN_B(chan)) {
		synthDelay = (4 * data) / 22;
	} else {
		synthDelay = data / 10;
	}
	OS_DELAY(synthDelay + BASE_ACTIVATE_DELAY);

	/* Setup the transmit power values. */
	if (!ar5212SetTransmitPower(ah, chan, rfXpdGain)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error init'ing transmit power\n", __func__);
		return AH_FALSE;
	}

	/* Write delta slope for OFDM enabled modes (A, G, Turbo) */
	if (IEEE80211_IS_CHAN_OFDM(chan)) {
		if (IS_5413(ah) ||
		    AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER5_3)
			ar5212SetSpurMitigation(ah, chan);
		ar5212SetDeltaSlope(ah, chan);
	}

	/* Release the RFBus Grant */
	OS_REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);

	/* Start Noise Floor Cal */
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	return AH_TRUE;
}

void
ar5212SetOperatingMode(struct ath_hal *ah, int opmode)
{
	uint32_t val;

	val = OS_REG_READ(ah, AR_STA_ID1);
	val &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC);
	switch (opmode) {
	case HAL_M_HOSTAP:
		OS_REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_STA_AP
					| AR_STA_ID1_KSRCH_MODE);
		OS_REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case HAL_M_IBSS:
		OS_REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_ADHOC
					| AR_STA_ID1_KSRCH_MODE);
		OS_REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case HAL_M_STA:
	case HAL_M_MONITOR:
		OS_REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_KSRCH_MODE);
		break;
	}
}

/*
 * Places the PHY and Radio chips into reset.  A full reset
 * must be called to leave this state.  The PCI/MAC/PCU are
 * not placed into reset as we must receive interrupt to
 * re-enable the hardware.
 */
HAL_BOOL
ar5212PhyDisable(struct ath_hal *ah)
{
	return ar5212SetResetReg(ah, AR_RC_BB);
}

/*
 * Places all of hardware into reset
 */
HAL_BOOL
ar5212Disable(struct ath_hal *ah)
{
	if (!ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;
	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset.
	 */
	return ar5212SetResetReg(ah, AR_RC_MAC | AR_RC_BB | AR_RC_PCI);
}

/*
 * Places the hardware into reset and then pulls it out of reset
 *
 * TODO: Only write the PLL if we're changing to or from CCK mode
 * 
 * WARNING: The order of the PLL and mode registers must be correct.
 */
HAL_BOOL
ar5212ChipReset(struct ath_hal *ah, const struct ieee80211_channel *chan)
{

	OS_MARK(ah, AH_MARK_CHIPRESET, chan ? chan->ic_freq : 0);

	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset
	 */
	if (!ar5212SetResetReg(ah, AR_RC_MAC | AR_RC_BB | AR_RC_PCI))
		return AH_FALSE;

	/* Bring out of sleep mode (AGAIN) */
	if (!ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/* Clear warm reset register */
	if (!ar5212SetResetReg(ah, 0))
		return AH_FALSE;

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

		if (IS_5413(ah)) {	/* NB: =>'s 5424 also */
			rfMode = AR_PHY_MODE_AR5112;
			if (IEEE80211_IS_CHAN_HALF(chan))
				rfMode |= AR_PHY_MODE_HALF;
			else if (IEEE80211_IS_CHAN_QUARTER(chan))
				rfMode |= AR_PHY_MODE_QUARTER;

			if (IEEE80211_IS_CHAN_CCK(chan))
				phyPLL = AR_PHY_PLL_CTL_44_5112;
			else
				phyPLL = AR_PHY_PLL_CTL_40_5413;
		} else if (IS_RAD5111(ah)) {
			rfMode = AR_PHY_MODE_AR5111;
			if (IEEE80211_IS_CHAN_CCK(chan))
				phyPLL = AR_PHY_PLL_CTL_44;
			else
				phyPLL = AR_PHY_PLL_CTL_40;
			if (IEEE80211_IS_CHAN_HALF(chan))
				phyPLL = AR_PHY_PLL_CTL_HALF;
			else if (IEEE80211_IS_CHAN_QUARTER(chan))
				phyPLL = AR_PHY_PLL_CTL_QUARTER;
		} else {		/* 5112, 2413, 2316, 2317 */
			rfMode = AR_PHY_MODE_AR5112;
			if (IEEE80211_IS_CHAN_CCK(chan))
				phyPLL = AR_PHY_PLL_CTL_44_5112;
			else
				phyPLL = AR_PHY_PLL_CTL_40_5112;
			if (IEEE80211_IS_CHAN_HALF(chan))
				phyPLL |= AR_PHY_PLL_CTL_HALF;
			else if (IEEE80211_IS_CHAN_QUARTER(chan))
				phyPLL |= AR_PHY_PLL_CTL_QUARTER;
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
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
HAL_BOOL
ar5212PerCalibrationN(struct ath_hal *ah,
	struct ieee80211_channel *chan,
	u_int chainMask, HAL_BOOL longCal, HAL_BOOL *isCalDone)
{
#define IQ_CAL_TRIES    10
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_CHANNEL_INTERNAL *ichan;
	int32_t qCoff, qCoffDenom;
	int32_t iqCorrMeas, iCoff, iCoffDenom;
	uint32_t powerMeasQ, powerMeasI;
	HAL_BOOL isBmode = AH_FALSE;

	OS_MARK(ah, AH_MARK_PERCAL, chan->ic_freq);
	*isCalDone = AH_FALSE;
	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}
	SAVE_CCK(ah, chan, isBmode);

	if (ahp->ah_bIQCalibration == IQ_CAL_DONE ||
	    ahp->ah_bIQCalibration == IQ_CAL_INACTIVE)
		*isCalDone = AH_TRUE;

	/* IQ calibration in progress. Check to see if it has finished. */
	if (ahp->ah_bIQCalibration == IQ_CAL_RUNNING &&
	    !(OS_REG_READ(ah, AR_PHY_TIMING_CTRL4) & AR_PHY_TIMING_CTRL4_DO_IQCAL)) {
		int i;

		/* IQ Calibration has finished. */
		ahp->ah_bIQCalibration = IQ_CAL_INACTIVE;
		*isCalDone = AH_TRUE;

		/* workaround for misgated IQ Cal results */
		i = 0;
		do {
			/* Read calibration results. */
			powerMeasI = OS_REG_READ(ah, AR_PHY_IQCAL_RES_PWR_MEAS_I);
			powerMeasQ = OS_REG_READ(ah, AR_PHY_IQCAL_RES_PWR_MEAS_Q);
			iqCorrMeas = OS_REG_READ(ah, AR_PHY_IQCAL_RES_IQ_CORR_MEAS);
			if (powerMeasI && powerMeasQ)
				break;
			/* Do we really need this??? */
			OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4,
			    AR_PHY_TIMING_CTRL4_DO_IQCAL);
		} while (++i < IQ_CAL_TRIES);

		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: IQ cal finished: %d tries\n", __func__, i);
		HALDEBUG(ah, HAL_DEBUG_PERCAL,
		    "%s: powerMeasI %u powerMeasQ %u iqCorrMeas %d\n",
		    __func__, powerMeasI, powerMeasQ, iqCorrMeas);

		/*
		 * Prescale these values to remove 64-bit operation
		 * requirement at the loss of a little precision.
		 */
		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 128;
		qCoffDenom = powerMeasQ / 128;

		/* Protect against divide-by-0 and loss of sign bits. */
		if (iCoffDenom != 0 && qCoffDenom >= 2) {
			iCoff = (int8_t)(-iqCorrMeas) / iCoffDenom;
			/* IQCORR_Q_I_COFF is a signed 6 bit number */
			if (iCoff < -32) {
				iCoff = -32;
			} else if (iCoff > 31) {
				iCoff = 31;
			}

			/* IQCORR_Q_Q_COFF is a signed 5 bit number */
			qCoff = (powerMeasI / qCoffDenom) - 128;
			if (qCoff < -16) {
				qCoff = -16;
			} else if (qCoff > 15) {
				qCoff = 15;
			}

			HALDEBUG(ah, HAL_DEBUG_PERCAL,
			    "%s: iCoff %d qCoff %d\n", __func__, iCoff, qCoff);

			/* Write values and enable correction */
			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
				AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, iCoff);
			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
				AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, qCoff);
			OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4, 
				AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);

			ahp->ah_bIQCalibration = IQ_CAL_DONE;
			ichan->privFlags |= CHANNEL_IQVALID;
			ichan->iCoff = iCoff;
			ichan->qCoff = qCoff;
		}
	} else if (!IEEE80211_IS_CHAN_B(chan) &&
	    ahp->ah_bIQCalibration == IQ_CAL_DONE &&
	    (ichan->privFlags & CHANNEL_IQVALID) == 0) {
		/*
		 * Start IQ calibration if configured channel has changed.
		 * Use a magic number of 15 based on default value.
		 */
		OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
			AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX,
			INIT_IQCAL_LOG_COUNT_MAX);
		OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4,
			AR_PHY_TIMING_CTRL4_DO_IQCAL);
		ahp->ah_bIQCalibration = IQ_CAL_RUNNING;
	}
	/* XXX EAR */

	if (longCal) {
		/* Check noise floor results */
		ar5212GetNf(ah, chan);
		if (!IEEE80211_IS_CHAN_CWINT(chan)) {
			/* Perform cal for 5Ghz channels and any OFDM on 5112 */
			if (IEEE80211_IS_CHAN_5GHZ(chan) ||
			    (IS_RAD5112(ah) && IEEE80211_IS_CHAN_OFDM(chan)))
				ar5212RequestRfgain(ah);
		}
	}
	RESTORE_CCK(ah, chan, isBmode);

	return AH_TRUE;
#undef IQ_CAL_TRIES
}

HAL_BOOL
ar5212PerCalibration(struct ath_hal *ah,  struct ieee80211_channel *chan,
	HAL_BOOL *isIQdone)
{
	return ar5212PerCalibrationN(ah, chan, 0x1, AH_TRUE, isIQdone);
}

HAL_BOOL
ar5212ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	HAL_CHANNEL_INTERNAL *ichan;

	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}
	ichan->privFlags &= ~CHANNEL_IQVALID;
	return AH_TRUE;
}

/**************************************************************
 * ar5212MacStop
 *
 * Disables all active QCUs and ensure that the mac is in a
 * quiessence state.
 */
static HAL_BOOL
ar5212MacStop(struct ath_hal *ah)
{
	HAL_BOOL     status;
	uint32_t    count;
	uint32_t    pendFrameCount;
	uint32_t    macStateFlag;
	uint32_t    queue;

	status = AH_FALSE;

	/* Disable Rx Operation ***********************************/
	OS_REG_SET_BIT(ah, AR_CR, AR_CR_RXD);

	/* Disable TX Operation ***********************************/
#ifdef NOT_YET
	ar5212SetTxdpInvalid(ah);
#endif
	OS_REG_SET_BIT(ah, AR_Q_TXD, AR_Q_TXD_M);

	/* Polling operation for completion of disable ************/
	macStateFlag = TX_ENABLE_CHECK | RX_ENABLE_CHECK;

	for (count = 0; count < MAX_RESET_WAIT; count++) {
		if (macStateFlag & RX_ENABLE_CHECK) {
			if (!OS_REG_IS_BIT_SET(ah, AR_CR, AR_CR_RXE)) {
				macStateFlag &= ~RX_ENABLE_CHECK;
			}
		}

		if (macStateFlag & TX_ENABLE_CHECK) {
			if (!OS_REG_IS_BIT_SET(ah, AR_Q_TXE, AR_Q_TXE_M)) {
				macStateFlag &= ~TX_ENABLE_CHECK;
				macStateFlag |= TX_QUEUEPEND_CHECK;
			}
		}
		if (macStateFlag & TX_QUEUEPEND_CHECK) {
			pendFrameCount = 0;
			for (queue = 0; queue < AR_NUM_DCU; queue++) {
				pendFrameCount += OS_REG_READ(ah,
				    AR_Q0_STS + (queue * 4)) &
				    AR_Q_STS_PEND_FR_CNT;
			}
			if (pendFrameCount == 0) {
				macStateFlag &= ~TX_QUEUEPEND_CHECK;
			}
		}
		if (macStateFlag == 0) {
			status = AH_TRUE;
			break;
		}
		OS_DELAY(50);
	}

	if (status != AH_TRUE) {
		HALDEBUG(ah, HAL_DEBUG_RESET,
		    "%s:Failed to stop the MAC state 0x%x\n",
		    __func__, macStateFlag);
	}

	return status;
}


/*
 * Write the given reset bit mask into the reset register
 */
static HAL_BOOL
ar5212SetResetReg(struct ath_hal *ah, uint32_t resetMask)
{
	uint32_t mask = resetMask ? resetMask : ~0;
	HAL_BOOL rt;

	/* Never reset the PCIE core */
	if (AH_PRIVATE(ah)->ah_ispcie) {
		resetMask &= ~AR_RC_PCI;
	}

	if (resetMask & (AR_RC_MAC | AR_RC_PCI)) {
		/*
		 * To ensure that the driver can reset the
		 * MAC, wake up the chip
		 */
		rt = ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE);

		if (rt != AH_TRUE) {
			return rt;
		}

		/*
		 * Disable interrupts
		 */
		OS_REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
		OS_REG_READ(ah, AR_IER);

		if (ar5212MacStop(ah) != AH_TRUE) {
			/*
			 * Failed to stop the MAC gracefully; let's be more forceful then
			 */

			/* need some delay before flush any pending MMR writes */
			OS_DELAY(15);
			OS_REG_READ(ah, AR_RXDP);

			resetMask |= AR_RC_MAC | AR_RC_BB;
			/* _Never_ reset PCI Express core */
			if (! AH_PRIVATE(ah)->ah_ispcie) {
				resetMask |= AR_RC_PCI;
			}
#if 0
			/*
			 * Flush the park address of the PCI controller
			*/
			/* Read PCI slot information less than Hainan revision */
			if (AH_PRIVATE(ah)->ah_bustype == HAL_BUS_TYPE_PCI) {
				if (!IS_5112_REV5_UP(ah)) {
#define PCI_COMMON_CONFIG_STATUS    0x06
					u_int32_t    i;
					u_int16_t    reg16;

					for (i = 0; i < 32; i++) {
						ath_hal_read_pci_config_space(ah,
						    PCI_COMMON_CONFIG_STATUS,
						    &reg16, sizeof(reg16));
					}
				}
#undef PCI_COMMON_CONFIG_STATUS
			}
#endif
		} else {
			/*
			 * MAC stopped gracefully; no need to warm-reset the PCI bus
			 */

			resetMask &= ~AR_RC_PCI;

			/* need some delay before flush any pending MMR writes */
			OS_DELAY(15);
			OS_REG_READ(ah, AR_RXDP);
		}
	}

	(void) OS_REG_READ(ah, AR_RXDP);/* flush any pending MMR writes */
	OS_REG_WRITE(ah, AR_RC, resetMask);
	OS_DELAY(15);			/* need to wait at least 128 clocks
					   when reseting PCI before read */
	mask &= (AR_RC_MAC | AR_RC_BB);
	resetMask &= (AR_RC_MAC | AR_RC_BB);
	rt = ath_hal_wait(ah, AR_RC, mask, resetMask);
        if ((resetMask & AR_RC_MAC) == 0) {
		if (isBigEndian()) {
			/*
			 * Set CFG, little-endian for descriptor accesses.
			 */
			mask = INIT_CONFIG_STATUS | AR_CFG_SWRD;
#ifndef AH_NEED_DESC_SWAP
			mask |= AR_CFG_SWTD;
#endif
			OS_REG_WRITE(ah, AR_CFG, mask);
		} else
			OS_REG_WRITE(ah, AR_CFG, INIT_CONFIG_STATUS);
		if (ar5212SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
			(void) OS_REG_READ(ah, AR_ISR_RAC);
	}

	/* track PHY power state so we don't try to r/w BB registers */
	AH5212(ah)->ah_phyPowerOn = ((resetMask & AR_RC_BB) == 0);
	return rt;
}

int16_t
ar5212GetNoiseFloor(struct ath_hal *ah)
{
	int16_t nf = (OS_REG_READ(ah, AR_PHY(25)) >> 19) & 0x1ff;
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	return nf;
}

static HAL_BOOL
getNoiseFloorThresh(struct ath_hal *ah, const struct ieee80211_channel *chan,
	int16_t *nft)
{
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		*nft = ee->ee_noiseFloorThresh[headerInfo11A];
		break;
	case IEEE80211_CHAN_B:
		*nft = ee->ee_noiseFloorThresh[headerInfo11B];
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_PUREG:	/* NB: really 108G */
		*nft = ee->ee_noiseFloorThresh[headerInfo11G];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel flags %u/0x%x\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}
	return AH_TRUE;
}

/*
 * Setup the noise floor cal history buffer.
 */
void 
ar5212InitNfCalHistBuffer(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	int i;

	ahp->ah_nfCalHist.first_run = 1;	
	ahp->ah_nfCalHist.currIndex = 0;
	ahp->ah_nfCalHist.privNF = AR5212_CCA_MAX_GOOD_VALUE;
	ahp->ah_nfCalHist.invalidNFcount = AR512_NF_CAL_HIST_MAX;
	for (i = 0; i < AR512_NF_CAL_HIST_MAX; i ++)
		ahp->ah_nfCalHist.nfCalBuffer[i] = AR5212_CCA_MAX_GOOD_VALUE;
}

/*
 * Add a noise floor value to the ring buffer.
 */
static __inline void
updateNFHistBuff(struct ar5212NfCalHist *h, int16_t nf)
{
 	h->nfCalBuffer[h->currIndex] = nf;
     	if (++h->currIndex >= AR512_NF_CAL_HIST_MAX)
		h->currIndex = 0;
}	

/*
 * Return the median noise floor value in the ring buffer.
 */
int16_t 
ar5212GetNfHistMid(const int16_t calData[AR512_NF_CAL_HIST_MAX])
{
	int16_t sort[AR512_NF_CAL_HIST_MAX];
	int i, j;

	OS_MEMCPY(sort, calData, AR512_NF_CAL_HIST_MAX*sizeof(int16_t));
	for (i = 0; i < AR512_NF_CAL_HIST_MAX-1; i ++) {
		for (j = 1; j < AR512_NF_CAL_HIST_MAX-i; j ++) {
			if (sort[j] > sort[j-1]) {
				int16_t nf = sort[j];
				sort[j] = sort[j-1];
				sort[j-1] = nf;
			}
		}
	}
	return sort[(AR512_NF_CAL_HIST_MAX-1)>>1];
}

/*
 * Read the NF and check it against the noise floor threshold
 */
int16_t
ar5212GetNf(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	struct ar5212NfCalHist *h = &ahp->ah_nfCalHist;
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	int16_t nf, nfThresh;
 	int32_t val;

	if (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: NF did not complete in calibration window\n", __func__);
		ichan->rawNoiseFloor = h->privNF;	/* most recent value */
		return ichan->rawNoiseFloor;
	}

	/*
	 * Finished NF cal, check against threshold.
	 */
	nf = ar5212GetNoiseFloor(ah);
	if (getNoiseFloorThresh(ah, chan, &nfThresh)) {
		if (nf > nfThresh) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: noise floor failed detected; detected %u, "
			    "threshold %u\n", __func__, nf, nfThresh);
			/*
			 * NB: Don't discriminate 2.4 vs 5Ghz, if this
			 *     happens it indicates a problem regardless
			 *     of the band.
			 */
			chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
			nf = 0;
		}
	} else
		nf = 0;

	/*
	 * Pass through histogram and write median value as
	 * calculated from the accrued window.  We require a
	 * full window of in-range values to be seen before we
	 * start using the history.
	 */
	updateNFHistBuff(h, nf);
	if (h->first_run) {
		if (nf < AR5212_CCA_MIN_BAD_VALUE ||
		    nf > AR5212_CCA_MAX_HIGH_VALUE) {
			nf = AR5212_CCA_MAX_GOOD_VALUE;
			h->invalidNFcount = AR512_NF_CAL_HIST_MAX;
		} else if (--(h->invalidNFcount) == 0) {
			h->first_run = 0;
			h->privNF = nf = ar5212GetNfHistMid(h->nfCalBuffer);
		} else {
			nf = AR5212_CCA_MAX_GOOD_VALUE;
		}
	} else {
		h->privNF = nf = ar5212GetNfHistMid(h->nfCalBuffer);
	}

	val = OS_REG_READ(ah, AR_PHY(25));
	val &= 0xFFFFFE00;
	val |= (((uint32_t)nf << 1) & 0x1FF);
	OS_REG_WRITE(ah, AR_PHY(25), val);
	OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF, 0)) {
#ifdef AH_DEBUG
		ath_hal_printf(ah, "%s: AGC not ready AGC_CONTROL 0x%x\n",
		    __func__, OS_REG_READ(ah, AR_PHY_AGC_CONTROL));
#endif
	}

	/*
	 * Now load a high maxCCAPower value again so that we're
	 * not capped by the median we just loaded
	 */
	val &= 0xFFFFFE00;
	val |= (((uint32_t)(-50) << 1) & 0x1FF);
	OS_REG_WRITE(ah, AR_PHY(25), val);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

	return (ichan->rawNoiseFloor = nf);
}

/*
 * Set up compression configuration registers
 */
void
ar5212SetCompRegs(struct ath_hal *ah)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	int i;

        /* Check if h/w supports compression */
	if (!AH_PRIVATE(ah)->ah_caps.halCompressSupport)
		return;

	OS_REG_WRITE(ah, AR_DCCFG, 1);

	OS_REG_WRITE(ah, AR_CCFG,
		(AR_COMPRESSION_WINDOW_SIZE >> 8) & AR_CCFG_WIN_M);

	OS_REG_WRITE(ah, AR_CCFG,
		OS_REG_READ(ah, AR_CCFG) | AR_CCFG_MIB_INT_EN);
	OS_REG_WRITE(ah, AR_CCUCFG,
		AR_CCUCFG_RESET_VAL | AR_CCUCFG_CATCHUP_EN);

	OS_REG_WRITE(ah, AR_CPCOVF, 0);

	/* reset decompression mask */
	for (i = 0; i < HAL_DECOMP_MASK_SIZE; i++) {
		OS_REG_WRITE(ah, AR_DCM_A, i);
		OS_REG_WRITE(ah, AR_DCM_D, ahp->ah_decompMask[i]);
	}
}

HAL_BOOL
ar5212SetAntennaSwitchInternal(struct ath_hal *ah, HAL_ANT_SETTING settings,
	const struct ieee80211_channel *chan)
{
#define	ANT_SWITCH_TABLE1	AR_PHY(88)
#define	ANT_SWITCH_TABLE2	AR_PHY(89)
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint32_t antSwitchA, antSwitchB;
	int ix;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);
	HALASSERT(ahp->ah_phyPowerOn);

	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		ix = 0;
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_PUREG:		/* NB: 108G */
		ix = 2;
		break;
	case IEEE80211_CHAN_B:
		if (IS_2425(ah) || IS_2417(ah)) {
			/* NB: Nala/Swan: 11b is handled using 11g */
			ix = 2;
		} else
			ix = 1;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	antSwitchA =  ee->ee_antennaControl[1][ix]
		   | (ee->ee_antennaControl[2][ix] << 6)
		   | (ee->ee_antennaControl[3][ix] << 12) 
		   | (ee->ee_antennaControl[4][ix] << 18)
		   | (ee->ee_antennaControl[5][ix] << 24)
		   ;
	antSwitchB =  ee->ee_antennaControl[6][ix]
		   | (ee->ee_antennaControl[7][ix] << 6)
		   | (ee->ee_antennaControl[8][ix] << 12)
		   | (ee->ee_antennaControl[9][ix] << 18)
		   | (ee->ee_antennaControl[10][ix] << 24)
		   ;
	/*
	 * For fixed antenna, give the same setting for both switch banks
	 */
	switch (settings) {
	case HAL_ANT_FIXED_A:
		antSwitchB = antSwitchA;
		break;
	case HAL_ANT_FIXED_B:
		antSwitchA = antSwitchB;
		break;
	case HAL_ANT_VARIABLE:
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad antenna setting %u\n",
		    __func__, settings);
		return AH_FALSE;
	}
	if (antSwitchB == antSwitchA) {
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Setting fast diversity off.\n", __func__);
		OS_REG_CLR_BIT(ah,AR_PHY_CCK_DETECT, 
			       AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
		ahp->ah_diversity = AH_FALSE;
	} else {
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Setting fast diversity on.\n", __func__);
		OS_REG_SET_BIT(ah,AR_PHY_CCK_DETECT, 
			       AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
		ahp->ah_diversity = AH_TRUE;
	}
	ahp->ah_antControl = settings;

	OS_REG_WRITE(ah, ANT_SWITCH_TABLE1, antSwitchA);
	OS_REG_WRITE(ah, ANT_SWITCH_TABLE2, antSwitchB);

	return AH_TRUE;
#undef ANT_SWITCH_TABLE2
#undef ANT_SWITCH_TABLE1
}

HAL_BOOL
ar5212IsSpurChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	uint32_t clockFreq =
	    ((IS_5413(ah) || IS_RAD5112_ANY(ah) || IS_2417(ah)) ? 40 : 32);
	return ( ((freq % clockFreq) != 0)
              && (((freq % clockFreq) < 10)
             || (((freq) % clockFreq) > 22)) );
}

/*
 * Read EEPROM header info and program the device for correct operation
 * given the channel value.
 */
HAL_BOOL
ar5212SetBoardValues(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define NO_FALSE_DETECT_BACKOFF   2
#define CB22_FALSE_DETECT_BACKOFF 6
#define	AR_PHY_BIS(_ah, _reg, _mask, _val) \
	OS_REG_WRITE(_ah, AR_PHY(_reg), \
		(OS_REG_READ(_ah, AR_PHY(_reg)) & _mask) | (_val));
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	int arrayMode, falseDectectBackoff;
	int is2GHz = IEEE80211_IS_CHAN_2GHZ(chan);
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	int8_t adcDesiredSize, pgaDesiredSize;
	uint16_t switchSettling, txrxAtten, rxtxMargin;
	int iCoff, qCoff;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	switch (chan->ic_flags & IEEE80211_CHAN_ALLTURBOFULL) {
	case IEEE80211_CHAN_A:
	case IEEE80211_CHAN_ST:
		arrayMode = headerInfo11A;
		if (!IS_RAD5112_ANY(ah) && !IS_2413(ah) && !IS_5413(ah))
			OS_REG_RMW_FIELD(ah, AR_PHY_FRAME_CTL,
				AR_PHY_FRAME_CTL_TX_CLIP,
				ahp->ah_gainValues.currStep->paramVal[GP_TXCLIP]);
		break;
	case IEEE80211_CHAN_B:
		arrayMode = headerInfo11B;
		break;
	case IEEE80211_CHAN_G:
	case IEEE80211_CHAN_108G:
		arrayMode = headerInfo11G;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	/* Set the antenna register(s) correctly for the chip revision */
	AR_PHY_BIS(ah, 68, 0xFFFFFC06,
		(ee->ee_antennaControl[0][arrayMode] << 4) | 0x1);

	ar5212SetAntennaSwitchInternal(ah, ahp->ah_antControl, chan);

	/* Set the Noise Floor Thresh on ar5211 devices */
	OS_REG_WRITE(ah, AR_PHY(90),
		(ee->ee_noiseFloorThresh[arrayMode] & 0x1FF)
		| (1 << 9));

	if (ee->ee_version >= AR_EEPROM_VER5_0 && IEEE80211_IS_CHAN_TURBO(chan)) {
		switchSettling = ee->ee_switchSettlingTurbo[is2GHz];
		adcDesiredSize = ee->ee_adcDesiredSizeTurbo[is2GHz];
		pgaDesiredSize = ee->ee_pgaDesiredSizeTurbo[is2GHz];
		txrxAtten = ee->ee_txrxAttenTurbo[is2GHz];
		rxtxMargin = ee->ee_rxtxMarginTurbo[is2GHz];
	} else {
		switchSettling = ee->ee_switchSettling[arrayMode];
		adcDesiredSize = ee->ee_adcDesiredSize[arrayMode];
		pgaDesiredSize = ee->ee_pgaDesiredSize[is2GHz];
		txrxAtten = ee->ee_txrxAtten[is2GHz];
		rxtxMargin = ee->ee_rxtxMargin[is2GHz];
	}

	OS_REG_RMW_FIELD(ah, AR_PHY_SETTLING, 
			 AR_PHY_SETTLING_SWITCH, switchSettling);
	OS_REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			 AR_PHY_DESIRED_SZ_ADC, adcDesiredSize);
	OS_REG_RMW_FIELD(ah, AR_PHY_DESIRED_SZ,
			 AR_PHY_DESIRED_SZ_PGA, pgaDesiredSize);
	OS_REG_RMW_FIELD(ah, AR_PHY_RXGAIN,
			 AR_PHY_RXGAIN_TXRX_ATTEN, txrxAtten);
	OS_REG_WRITE(ah, AR_PHY(13),
		(ee->ee_txEndToXPAOff[arrayMode] << 24)
		| (ee->ee_txEndToXPAOff[arrayMode] << 16)
		| (ee->ee_txFrameToXPAOn[arrayMode] << 8)
		| ee->ee_txFrameToXPAOn[arrayMode]);
	AR_PHY_BIS(ah, 10, 0xFFFF00FF,
		ee->ee_txEndToXLNAOn[arrayMode] << 8);
	AR_PHY_BIS(ah, 25, 0xFFF80FFF,
		(ee->ee_thresh62[arrayMode] << 12) & 0x7F000);

	/*
	 * False detect backoff - suspected 32 MHz spur causes false
	 * detects in OFDM, causing Tx Hangs.  Decrease weak signal
	 * sensitivity for this card.
	 */
	falseDectectBackoff = NO_FALSE_DETECT_BACKOFF;
	if (ee->ee_version < AR_EEPROM_VER3_3) {
		/* XXX magic number */
		if (AH_PRIVATE(ah)->ah_subvendorid == 0x1022 &&
		    IEEE80211_IS_CHAN_OFDM(chan))
			falseDectectBackoff += CB22_FALSE_DETECT_BACKOFF;
	} else {
		if (ar5212IsSpurChannel(ah, chan))
			falseDectectBackoff += ee->ee_falseDetectBackoff[arrayMode];
	}
	AR_PHY_BIS(ah, 73, 0xFFFFFF01, (falseDectectBackoff << 1) & 0xFE);

	if (ichan->privFlags & CHANNEL_IQVALID) {
		iCoff = ichan->iCoff;
		qCoff = ichan->qCoff;
	} else {
		iCoff = ee->ee_iqCalI[is2GHz];
		qCoff = ee->ee_iqCalQ[is2GHz];
	}

	/* write previous IQ results */
	OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
		AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, iCoff);
	OS_REG_RMW_FIELD(ah, AR_PHY_TIMING_CTRL4,
		AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, qCoff);
	OS_REG_SET_BIT(ah, AR_PHY_TIMING_CTRL4,
		AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);

	if (ee->ee_version >= AR_EEPROM_VER4_1) {
		if (!IEEE80211_IS_CHAN_108G(chan) || ee->ee_version >= AR_EEPROM_VER5_0)
			OS_REG_RMW_FIELD(ah, AR_PHY_GAIN_2GHZ,
				AR_PHY_GAIN_2GHZ_RXTX_MARGIN, rxtxMargin);
	}
	if (ee->ee_version >= AR_EEPROM_VER5_1) {
		/* for now always disabled */
		OS_REG_WRITE(ah,  AR_PHY_HEAVY_CLIP_ENABLE,  0);
	}

	return AH_TRUE;
#undef AR_PHY_BIS
#undef NO_FALSE_DETECT_BACKOFF
#undef CB22_FALSE_DETECT_BACKOFF
}

/*
 * Apply Spur Immunity to Boards that require it.
 * Applies only to OFDM RX operation.
 */

void
ar5212SetSpurMitigation(struct ath_hal *ah,
	const struct ieee80211_channel *chan)
{
	uint32_t pilotMask[2] = {0, 0}, binMagMask[4] = {0, 0, 0 , 0};
	uint16_t i, finalSpur, curChanAsSpur, binWidth = 0, spurDetectWidth, spurChan;
	int32_t spurDeltaPhase = 0, spurFreqSd = 0, spurOffset, binOffsetNumT16, curBinOffset;
	int16_t numBinOffsets;
	static const uint16_t magMapFor4[4] = {1, 2, 2, 1};
	static const uint16_t magMapFor3[3] = {1, 2, 1};
	const uint16_t *pMagMap;
	HAL_BOOL is2GHz = IEEE80211_IS_CHAN_2GHZ(chan);
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	uint32_t val;

#define CHAN_TO_SPUR(_f, _freq)   ( ((_freq) - ((_f) ? 2300 : 4900)) * 10 )
	if (IS_2417(ah)) {
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: no spur mitigation\n",
		    __func__);
		return;
	}

	curChanAsSpur = CHAN_TO_SPUR(is2GHz, ichan->channel);

	if (ichan->mainSpur) {
		/* Pull out the saved spur value */
		finalSpur = ichan->mainSpur;
	} else {
		/*
		 * Check if spur immunity should be performed for this channel
		 * Should only be performed once per channel and then saved
		 */
		finalSpur = AR_NO_SPUR;
		spurDetectWidth = HAL_SPUR_CHAN_WIDTH;
		if (IEEE80211_IS_CHAN_TURBO(chan))
			spurDetectWidth *= 2;

		/* Decide if any spur affects the current channel */
		for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
			spurChan = ath_hal_getSpurChan(ah, i, is2GHz);
			if (spurChan == AR_NO_SPUR) {
				break;
			}
			if ((curChanAsSpur - spurDetectWidth <= (spurChan & HAL_SPUR_VAL_MASK)) &&
			    (curChanAsSpur + spurDetectWidth >= (spurChan & HAL_SPUR_VAL_MASK))) {
				finalSpur = spurChan & HAL_SPUR_VAL_MASK;
				break;
			}
		}
		/* Save detected spur (or no spur) for this channel */
		ichan->mainSpur = finalSpur;
	}

	/* Write spur immunity data */
	if (finalSpur == AR_NO_SPUR) {
		/* Disable Spur Immunity Regs if they appear set */
		if (OS_REG_READ(ah, AR_PHY_TIMING_CTRL4) & AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER) {
			/* Clear Spur Delta Phase, Spur Freq, and enable bits */
			OS_REG_RMW_FIELD(ah, AR_PHY_MASK_CTL, AR_PHY_MASK_CTL_RATE, 0);
			val = OS_REG_READ(ah, AR_PHY_TIMING_CTRL4);
			val &= ~(AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
				 AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
				 AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
			OS_REG_WRITE(ah, AR_PHY_MASK_CTL, val);
			OS_REG_WRITE(ah, AR_PHY_TIMING11, 0);

			/* Clear pilot masks */
			OS_REG_WRITE(ah, AR_PHY_TIMING7, 0);
			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING8, AR_PHY_TIMING8_PILOT_MASK_2, 0);
			OS_REG_WRITE(ah, AR_PHY_TIMING9, 0);
			OS_REG_RMW_FIELD(ah, AR_PHY_TIMING10, AR_PHY_TIMING10_PILOT_MASK_2, 0);

			/* Clear magnitude masks */
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK_1, 0);
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK_2, 0);
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK_3, 0);
			OS_REG_RMW_FIELD(ah, AR_PHY_MASK_CTL, AR_PHY_MASK_CTL_MASK_4, 0);
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_1, 0);
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_2, 0);
			OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_3, 0);
			OS_REG_RMW_FIELD(ah, AR_PHY_BIN_MASK2_4, AR_PHY_BIN_MASK2_4_MASK_4, 0);
		}
	} else {
		spurOffset = finalSpur - curChanAsSpur;
		/*
		 * Spur calculations:
		 * spurDeltaPhase is (spurOffsetIn100KHz / chipFrequencyIn100KHz) << 21
		 * spurFreqSd is (spurOffsetIn100KHz / sampleFrequencyIn100KHz) << 11
		 */
		if (IEEE80211_IS_CHAN_TURBO(chan)) {
			/* Chip Frequency & sampleFrequency are 80 MHz */
			spurDeltaPhase = (spurOffset << 16) / 25;
			spurFreqSd = spurDeltaPhase >> 10;
			binWidth = HAL_BIN_WIDTH_TURBO_100HZ;
		} else if (IEEE80211_IS_CHAN_G(chan)) {
			/* Chip Frequency is 44MHz, sampleFrequency is 40 MHz */
			spurFreqSd = (spurOffset << 8) / 55;
			spurDeltaPhase = (spurOffset << 17) / 25;
			binWidth = HAL_BIN_WIDTH_BASE_100HZ;
		} else {
			HALASSERT(!IEEE80211_IS_CHAN_B(chan));
			/* Chip Frequency & sampleFrequency are 40 MHz */
			spurDeltaPhase = (spurOffset << 17) / 25;
			spurFreqSd = spurDeltaPhase >> 10;
			binWidth = HAL_BIN_WIDTH_BASE_100HZ;
		}

		/* Compute Pilot Mask */
		binOffsetNumT16 = ((spurOffset * 1000) << 4) / binWidth;
		/* The spur is on a bin if it's remainder at times 16 is 0 */
		if (binOffsetNumT16 & 0xF) {
			numBinOffsets = 4;
			pMagMap = magMapFor4;
		} else {
			numBinOffsets = 3;
			pMagMap = magMapFor3;
		}
		for (i = 0; i < numBinOffsets; i++) {
			if ((binOffsetNumT16 >> 4) > HAL_MAX_BINS_ALLOWED) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "Too man bins in spur mitigation\n");
				return;
			}

			/* Get Pilot Mask values */
			curBinOffset = (binOffsetNumT16 >> 4) + i + 25;
			if ((curBinOffset >= 0) && (curBinOffset <= 32)) {
				if (curBinOffset <= 25)
					pilotMask[0] |= 1 << curBinOffset;
				else if (curBinOffset >= 27)
					pilotMask[0] |= 1 << (curBinOffset - 1);
			} else if ((curBinOffset >= 33) && (curBinOffset <= 52))
				pilotMask[1] |= 1 << (curBinOffset - 33);

			/* Get viterbi values */
			if ((curBinOffset >= -1) && (curBinOffset <= 14))
				binMagMask[0] |= pMagMap[i] << (curBinOffset + 1) * 2;
			else if ((curBinOffset >= 15) && (curBinOffset <= 30))
				binMagMask[1] |= pMagMap[i] << (curBinOffset - 15) * 2;
			else if ((curBinOffset >= 31) && (curBinOffset <= 46))
				binMagMask[2] |= pMagMap[i] << (curBinOffset -31) * 2;
			else if((curBinOffset >= 47) && (curBinOffset <= 53))
				binMagMask[3] |= pMagMap[i] << (curBinOffset -47) * 2;
		}

		/* Write Spur Delta Phase, Spur Freq, and enable bits */
		OS_REG_RMW_FIELD(ah, AR_PHY_MASK_CTL, AR_PHY_MASK_CTL_RATE, 0xFF);
		val = OS_REG_READ(ah, AR_PHY_TIMING_CTRL4);
		val |= (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
			AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK | 
			AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
		OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4, val);
		OS_REG_WRITE(ah, AR_PHY_TIMING11, AR_PHY_TIMING11_USE_SPUR_IN_AGC |		
			     SM(spurFreqSd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
			     SM(spurDeltaPhase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));

		/* Write pilot masks */
		OS_REG_WRITE(ah, AR_PHY_TIMING7, pilotMask[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_TIMING8, AR_PHY_TIMING8_PILOT_MASK_2, pilotMask[1]);
		OS_REG_WRITE(ah, AR_PHY_TIMING9, pilotMask[0]);
		OS_REG_RMW_FIELD(ah, AR_PHY_TIMING10, AR_PHY_TIMING10_PILOT_MASK_2, pilotMask[1]);

		/* Write magnitude masks */
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK_1, binMagMask[0]);
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK_2, binMagMask[1]);
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK_3, binMagMask[2]);
		OS_REG_RMW_FIELD(ah, AR_PHY_MASK_CTL, AR_PHY_MASK_CTL_MASK_4, binMagMask[3]);
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_1, binMagMask[0]);
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_2, binMagMask[1]);
		OS_REG_WRITE(ah, AR_PHY_BIN_MASK2_3, binMagMask[2]);
		OS_REG_RMW_FIELD(ah, AR_PHY_BIN_MASK2_4, AR_PHY_BIN_MASK2_4_MASK_4, binMagMask[3]);
	}
#undef CHAN_TO_SPUR
}


/*
 * Delta slope coefficient computation.
 * Required for OFDM operation.
 */
void
ar5212SetDeltaSlope(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define COEF_SCALE_S 24
#define INIT_CLOCKMHZSCALED	0x64000000
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	unsigned long coef_scaled, coef_exp, coef_man, ds_coef_exp, ds_coef_man;
	unsigned long clockMhzScaled = INIT_CLOCKMHZSCALED;

	if (IEEE80211_IS_CHAN_TURBO(chan))
		clockMhzScaled *= 2;
	/* half and quarter rate can divide the scaled clock by 2 or 4 respectively */
	/* scale for selected channel bandwidth */ 
	if (IEEE80211_IS_CHAN_HALF(chan)) {
		clockMhzScaled = clockMhzScaled >> 1;
	} else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
		clockMhzScaled = clockMhzScaled >> 2;
	} 

	/*
	 * ALGO -> coef = 1e8/fcarrier*fclock/40;
	 * scaled coef to provide precision for this floating calculation 
	 */
	coef_scaled = clockMhzScaled / freq;

	/*
	 * ALGO -> coef_exp = 14-floor(log2(coef)); 
	 * floor(log2(x)) is the highest set bit position
	 */
	for (coef_exp = 31; coef_exp > 0; coef_exp--)
		if ((coef_scaled >> coef_exp) & 0x1)
			break;
	/* A coef_exp of 0 is a legal bit position but an unexpected coef_exp */
	HALASSERT(coef_exp);
	coef_exp = 14 - (coef_exp - COEF_SCALE_S);

	/*
	 * ALGO -> coef_man = floor(coef* 2^coef_exp+0.5);
	 * The coefficient is already shifted up for scaling
	 */
	coef_man = coef_scaled + (1 << (COEF_SCALE_S - coef_exp - 1));
	ds_coef_man = coef_man >> (COEF_SCALE_S - coef_exp);
	ds_coef_exp = coef_exp - 16;

	OS_REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
	OS_REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);
#undef INIT_CLOCKMHZSCALED
#undef COEF_SCALE_S
}

/*
 * Set a limit on the overall output power.  Used for dynamic
 * transmit power control and the like.
 *
 * NB: limit is in units of 0.5 dbM.
 */
HAL_BOOL
ar5212SetTxPowerLimit(struct ath_hal *ah, uint32_t limit)
{
	/* XXX blech, construct local writable copy */
	struct ieee80211_channel dummy = *AH_PRIVATE(ah)->ah_curchan;
	uint16_t dummyXpdGains[2];
	HAL_BOOL isBmode;

	SAVE_CCK(ah, &dummy, isBmode);
	AH_PRIVATE(ah)->ah_powerLimit = AH_MIN(limit, MAX_RATE_POWER);
	return ar5212SetTransmitPower(ah, &dummy, dummyXpdGains);
}

/*
 * Set the transmit power in the baseband for the given
 * operating channel and mode.
 */
HAL_BOOL
ar5212SetTransmitPower(struct ath_hal *ah,
	const struct ieee80211_channel *chan, uint16_t *rfXpdGain)
{
#define	POW_OFDM(_r, _s)	(((0 & 1)<< ((_s)+6)) | (((_r) & 0x3f) << (_s)))
#define	POW_CCK(_r, _s)		(((_r) & 0x3f) << (_s))
#define	N(a)			(sizeof (a) / sizeof (a[0]))
	static const uint16_t tpcScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	int16_t minPower, maxPower, tpcInDb, powerLimit;
	int i;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	OS_MEMZERO(ahp->ah_pcdacTable, ahp->ah_pcdacTableSize);
	OS_MEMZERO(ahp->ah_ratesArray, sizeof(ahp->ah_ratesArray));

	powerLimit = AH_MIN(MAX_RATE_POWER, AH_PRIVATE(ah)->ah_powerLimit);
	if (powerLimit >= MAX_RATE_POWER || powerLimit == 0)
		tpcInDb = tpcScaleReductionTable[AH_PRIVATE(ah)->ah_tpScale];
	else
		tpcInDb = 0;
	if (!ar5212SetRateTable(ah, chan, tpcInDb, powerLimit,
				AH_TRUE, &minPower, &maxPower)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unable to set rate table\n",
		    __func__);
		return AH_FALSE;
	}
	if (!ahp->ah_rfHal->setPowerTable(ah,
		&minPower, &maxPower, chan, rfXpdGain)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unable to set power table\n",
		    __func__);
		return AH_FALSE;
	}

	/* 
	 * Adjust XR power/rate up by 2 dB to account for greater peak
	 * to avg ratio - except in newer avg power designs
	 */
	if (!IS_2413(ah) && !IS_5413(ah))
		ahp->ah_ratesArray[15] += 4;
	/* 
	 * txPowerIndexOffset is set by the SetPowerTable() call -
	 *  adjust the rate table 
	 */
	for (i = 0; i < N(ahp->ah_ratesArray); i++) {
		ahp->ah_ratesArray[i] += ahp->ah_txPowerIndexOffset;
		if (ahp->ah_ratesArray[i] > 63) 
			ahp->ah_ratesArray[i] = 63;
	}

	if (ee->ee_eepMap < 2) {
		/* 
		 * Correct gain deltas for 5212 G operation -
		 * Removed with revised chipset
		 */
		if (AH_PRIVATE(ah)->ah_phyRev < AR_PHY_CHIP_ID_REV_2 &&
		    IEEE80211_IS_CHAN_G(chan)) {
			uint16_t cckOfdmPwrDelta;

			if (freq == 2484) 
				cckOfdmPwrDelta = SCALE_OC_DELTA(
					ee->ee_cckOfdmPwrDelta - 
					ee->ee_scaledCh14FilterCckDelta);
			else 
				cckOfdmPwrDelta = SCALE_OC_DELTA(
					ee->ee_cckOfdmPwrDelta);
			ar5212CorrectGainDelta(ah, cckOfdmPwrDelta);
		}
		/* 
		 * Finally, write the power values into the
		 * baseband power table
		 */
		for (i = 0; i < (PWR_TABLE_SIZE/2); i++) {
			OS_REG_WRITE(ah, AR_PHY_PCDAC_TX_POWER(i),
				 ((((ahp->ah_pcdacTable[2*i + 1] << 8) | 0xff) & 0xffff) << 16)
				| (((ahp->ah_pcdacTable[2*i]     << 8) | 0xff) & 0xffff)
			);
		}
	}

	/* Write the OFDM power per rate set */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE1, 
		POW_OFDM(ahp->ah_ratesArray[3], 24)
	      | POW_OFDM(ahp->ah_ratesArray[2], 16)
	      | POW_OFDM(ahp->ah_ratesArray[1],  8)
	      | POW_OFDM(ahp->ah_ratesArray[0],  0)
	);
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE2, 
		POW_OFDM(ahp->ah_ratesArray[7], 24)
	      | POW_OFDM(ahp->ah_ratesArray[6], 16)
	      | POW_OFDM(ahp->ah_ratesArray[5],  8)
	      | POW_OFDM(ahp->ah_ratesArray[4],  0)
	);

	/* Write the CCK power per rate set */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE3,
		POW_CCK(ahp->ah_ratesArray[10], 24)
	      | POW_CCK(ahp->ah_ratesArray[9],  16)
	      | POW_CCK(ahp->ah_ratesArray[15],  8)	/* XR target power */
	      | POW_CCK(ahp->ah_ratesArray[8],   0)
	);
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE4,
		POW_CCK(ahp->ah_ratesArray[14], 24)
	      | POW_CCK(ahp->ah_ratesArray[13], 16)
	      | POW_CCK(ahp->ah_ratesArray[12],  8)
	      | POW_CCK(ahp->ah_ratesArray[11],  0)
	);

	/*
	 * Set max power to 30 dBm and, optionally,
	 * enable TPC in tx descriptors.
	 */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE_MAX, MAX_RATE_POWER |
		(ahp->ah_tpcEnabled ? AR_PHY_POWER_TX_RATE_MAX_TPC_ENABLE : 0));

	return AH_TRUE;
#undef N
#undef POW_CCK
#undef POW_OFDM
}

/*
 * Sets the transmit power in the baseband for the given
 * operating channel and mode.
 */
static HAL_BOOL
ar5212SetRateTable(struct ath_hal *ah, const struct ieee80211_channel *chan,
	int16_t tpcScaleReduction, int16_t powerLimit, HAL_BOOL commit,
	int16_t *pMinPower, int16_t *pMaxPower)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	uint16_t *rpow = ahp->ah_ratesArray;
	uint16_t twiceMaxEdgePower = MAX_RATE_POWER;
	uint16_t twiceMaxEdgePowerCck = MAX_RATE_POWER;
	uint16_t twiceMaxRDPower = MAX_RATE_POWER;
	int i;
	uint8_t cfgCtl;
	int8_t twiceAntennaGain, twiceAntennaReduction;
	const RD_EDGES_POWER *rep;
	TRGT_POWER_INFO targetPowerOfdm, targetPowerCck;
	int16_t scaledPower, maxAvailPower = 0;
	int16_t r13, r9, r7, r0;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	twiceMaxRDPower = chan->ic_maxregpower * 2;
	*pMaxPower = -MAX_RATE_POWER;
	*pMinPower = MAX_RATE_POWER;

	/* Get conformance test limit maximum for this channel */
	cfgCtl = ath_hal_getctl(ah, chan);
	for (i = 0; i < ee->ee_numCtls; i++) {
		uint16_t twiceMinEdgePower;

		if (ee->ee_ctl[i] == 0)
			continue;
		if (ee->ee_ctl[i] == cfgCtl ||
		    cfgCtl == ((ee->ee_ctl[i] & CTL_MODE_M) | SD_NO_CTL)) {
			rep = &ee->ee_rdEdgesPower[i * NUM_EDGES];
			twiceMinEdgePower = ar5212GetMaxEdgePower(freq, rep);
			if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
				/* Find the minimum of all CTL edge powers that apply to this channel */
				twiceMaxEdgePower = AH_MIN(twiceMaxEdgePower, twiceMinEdgePower);
			} else {
				twiceMaxEdgePower = twiceMinEdgePower;
				break;
			}
		}
	}

	if (IEEE80211_IS_CHAN_G(chan)) {
		/* Check for a CCK CTL for 11G CCK powers */
		cfgCtl = (cfgCtl & ~CTL_MODE_M) | CTL_11B;
		for (i = 0; i < ee->ee_numCtls; i++) {
			uint16_t twiceMinEdgePowerCck;

			if (ee->ee_ctl[i] == 0)
				continue;
			if (ee->ee_ctl[i] == cfgCtl ||
			    cfgCtl == ((ee->ee_ctl[i] & CTL_MODE_M) | SD_NO_CTL)) {
				rep = &ee->ee_rdEdgesPower[i * NUM_EDGES];
				twiceMinEdgePowerCck = ar5212GetMaxEdgePower(freq, rep);
				if ((cfgCtl & ~CTL_MODE_M) == SD_NO_CTL) {
					/* Find the minimum of all CTL edge powers that apply to this channel */
					twiceMaxEdgePowerCck = AH_MIN(twiceMaxEdgePowerCck, twiceMinEdgePowerCck);
				} else {
					twiceMaxEdgePowerCck = twiceMinEdgePowerCck;
					break;
				}
			}
		}
	} else {
		/* Set the 11B cck edge power to the one found before */
		twiceMaxEdgePowerCck = twiceMaxEdgePower;
	}

	/* Get Antenna Gain reduction */
	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		ath_hal_eepromGet(ah, AR_EEP_ANTGAINMAX_5, &twiceAntennaGain);
	} else {
		ath_hal_eepromGet(ah, AR_EEP_ANTGAINMAX_2, &twiceAntennaGain);
	}
	twiceAntennaReduction =
		ath_hal_getantennareduction(ah, chan, twiceAntennaGain);

	if (IEEE80211_IS_CHAN_OFDM(chan)) {
		/* Get final OFDM target powers */
		if (IEEE80211_IS_CHAN_2GHZ(chan)) { 
			ar5212GetTargetPowers(ah, chan, ee->ee_trgtPwr_11g,
				ee->ee_numTargetPwr_11g, &targetPowerOfdm);
		} else {
			ar5212GetTargetPowers(ah, chan, ee->ee_trgtPwr_11a,
				ee->ee_numTargetPwr_11a, &targetPowerOfdm);
		}

		/* Get Maximum OFDM power */
		/* Minimum of target and edge powers */
		scaledPower = AH_MIN(twiceMaxEdgePower,
				twiceMaxRDPower - twiceAntennaReduction);

		/*
		 * If turbo is set, reduce power to keep power
		 * consumption under 2 Watts.  Note that we always do
		 * this unless specially configured.  Then we limit
		 * power only for non-AP operation.
		 */
		if (IEEE80211_IS_CHAN_TURBO(chan)
#ifdef AH_ENABLE_AP_SUPPORT
		    && AH_PRIVATE(ah)->ah_opmode != HAL_M_HOSTAP
#endif
		) {
			/*
			 * If turbo is set, reduce power to keep power
			 * consumption under 2 Watts
			 */
			if (ee->ee_version >= AR_EEPROM_VER3_1)
				scaledPower = AH_MIN(scaledPower,
					ee->ee_turbo2WMaxPower5);
			/*
			 * EEPROM version 4.0 added an additional
			 * constraint on 2.4GHz channels.
			 */
			if (ee->ee_version >= AR_EEPROM_VER4_0 &&
			    IEEE80211_IS_CHAN_2GHZ(chan))
				scaledPower = AH_MIN(scaledPower,
					ee->ee_turbo2WMaxPower2);
		}

		maxAvailPower = AH_MIN(scaledPower,
					targetPowerOfdm.twicePwr6_24);

		/* Reduce power by max regulatory domain allowed restrictions */
		scaledPower = maxAvailPower - (tpcScaleReduction * 2);
		scaledPower = (scaledPower < 0) ? 0 : scaledPower;
		scaledPower = AH_MIN(scaledPower, powerLimit);

		if (commit) {
			/* Set OFDM rates 9, 12, 18, 24 */
			r0 = rpow[0] = rpow[1] = rpow[2] = rpow[3] = rpow[4] = scaledPower;

			/* Set OFDM rates 36, 48, 54, XR */
			rpow[5] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr36);
			rpow[6] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr48);
			r7 = rpow[7] = AH_MIN(rpow[0], targetPowerOfdm.twicePwr54);

			if (ee->ee_version >= AR_EEPROM_VER4_0) {
				/* Setup XR target power from EEPROM */
				rpow[15] = AH_MIN(scaledPower, IEEE80211_IS_CHAN_2GHZ(chan) ?
						  ee->ee_xrTargetPower2 : ee->ee_xrTargetPower5);
			} else {
				/* XR uses 6mb power */
				rpow[15] = rpow[0];
			}
			ahp->ah_ofdmTxPower = *pMaxPower;

		} else {
			r0 = scaledPower;
			r7 = AH_MIN(r0, targetPowerOfdm.twicePwr54);
		}
		*pMinPower = r7;
		*pMaxPower = r0;

		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: MaxRD: %d TurboMax: %d MaxCTL: %d "
		    "TPC_Reduction %d chan=%d (0x%x) maxAvailPower=%d pwr6_24=%d, maxPower=%d\n",
		    __func__, twiceMaxRDPower, ee->ee_turbo2WMaxPower5,
		    twiceMaxEdgePower, tpcScaleReduction * 2,
		    chan->ic_freq, chan->ic_flags,
		    maxAvailPower, targetPowerOfdm.twicePwr6_24, *pMaxPower);
	}

	if (IEEE80211_IS_CHAN_CCK(chan)) {
		/* Get final CCK target powers */
		ar5212GetTargetPowers(ah, chan, ee->ee_trgtPwr_11b,
			ee->ee_numTargetPwr_11b, &targetPowerCck);

		/* Reduce power by max regulatory domain allowed restrictions */
		scaledPower = AH_MIN(twiceMaxEdgePowerCck,
			twiceMaxRDPower - twiceAntennaReduction);
		if (maxAvailPower < AH_MIN(scaledPower, targetPowerCck.twicePwr6_24))
			maxAvailPower = AH_MIN(scaledPower, targetPowerCck.twicePwr6_24);

		/* Reduce power by user selection */
		scaledPower = AH_MIN(scaledPower, targetPowerCck.twicePwr6_24) - (tpcScaleReduction * 2);
		scaledPower = (scaledPower < 0) ? 0 : scaledPower;
		scaledPower = AH_MIN(scaledPower, powerLimit);

		if (commit) {
			/* Set CCK rates 2L, 2S, 5.5L, 5.5S, 11L, 11S */
			rpow[8]  = AH_MIN(scaledPower, targetPowerCck.twicePwr6_24);
			r9 = rpow[9]  = AH_MIN(scaledPower, targetPowerCck.twicePwr36);
			rpow[10] = rpow[9];
			rpow[11] = AH_MIN(scaledPower, targetPowerCck.twicePwr48);
			rpow[12] = rpow[11];
			r13 = rpow[13] = AH_MIN(scaledPower, targetPowerCck.twicePwr54);
			rpow[14] = rpow[13];
		} else {
			r9 = AH_MIN(scaledPower, targetPowerCck.twicePwr36);
			r13 = AH_MIN(scaledPower, targetPowerCck.twicePwr54);
		}

		/* Set min/max power based off OFDM values or initialization */
		if (r13 < *pMinPower)
			*pMinPower = r13;
		if (r9 > *pMaxPower)
			*pMaxPower = r9;

		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: cck: MaxRD: %d MaxCTL: %d "
		    "TPC_Reduction %d chan=%d (0x%x) maxAvailPower=%d pwr6_24=%d, maxPower=%d\n",
		    __func__, twiceMaxRDPower, twiceMaxEdgePowerCck,
		    tpcScaleReduction * 2, chan->ic_freq, chan->ic_flags,
		    maxAvailPower, targetPowerCck.twicePwr6_24, *pMaxPower);
	}
	if (commit) {
		ahp->ah_tx6PowerInHalfDbm = *pMaxPower;
		AH_PRIVATE(ah)->ah_maxPowerLevel = ahp->ah_tx6PowerInHalfDbm;
	}
	return AH_TRUE;
}

HAL_BOOL
ar5212GetChipPowerLimits(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
#if 0
	static const uint16_t tpcScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };
	int16_t tpcInDb, powerLimit;
#endif
	int16_t minPower, maxPower;

	/*
	 * Get Pier table max and min powers.
	 */
	if (ahp->ah_rfHal->getChannelMaxMinPower(ah, chan, &maxPower, &minPower)) {
		/* NB: rf code returns 1/4 dBm units, convert */
		chan->ic_maxpower = maxPower / 2;
		chan->ic_minpower = minPower / 2;
	} else {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: no min/max power for %u/0x%x\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		chan->ic_maxpower = MAX_RATE_POWER;
		chan->ic_minpower = 0;
	}
#if 0
	/*
	 * Now adjust to reflect any global scale and/or CTL's.
	 * (XXX is that correct?)
	 */
	powerLimit = AH_MIN(MAX_RATE_POWER, AH_PRIVATE(ah)->ah_powerLimit);
	if (powerLimit >= MAX_RATE_POWER || powerLimit == 0)
		tpcInDb = tpcScaleReductionTable[AH_PRIVATE(ah)->ah_tpScale];
	else
		tpcInDb = 0;
	if (!ar5212SetRateTable(ah, chan, tpcInDb, powerLimit,
				AH_FALSE, &minPower, &maxPower)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: unable to find max/min power\n",__func__);
		return AH_FALSE;
	}
	if (maxPower < chan->ic_maxpower)
		chan->ic_maxpower = maxPower;
	if (minPower < chan->ic_minpower)
		chan->ic_minpower = minPower;
	HALDEBUG(ah, HAL_DEBUG_RESET,
	    "Chan %d: MaxPow = %d MinPow = %d\n",
	    chan->ic_freq, chan->ic_maxpower, chans->ic_minpower);
#endif
	return AH_TRUE;
}

/*
 * Correct for the gain-delta between ofdm and cck mode target
 * powers. Write the results to the rate table and the power table.
 *
 *   Conventions :
 *   1. rpow[ii] is the integer value of 2*(desired power
 *    for the rate ii in dBm) to provide 0.5dB resolution. rate
 *    mapping is as following :
 *     [0..7]  --> ofdm 6, 9, .. 48, 54
 *     [8..14] --> cck 1L, 2L, 2S, .. 11L, 11S
 *     [15]    --> XR (all rates get the same power)
 *   2. powv[ii]  is the pcdac corresponding to ii/2 dBm.
 */
static void
ar5212CorrectGainDelta(struct ath_hal *ah, int twiceOfdmCckDelta)
{
#define	N(_a)	(sizeof(_a) / sizeof(_a[0]))
	struct ath_hal_5212 *ahp = AH5212(ah);
	const HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	int16_t ratesIndex[N(ahp->ah_ratesArray)];
	uint16_t ii, jj, iter;
	int32_t cckIndex;
	int16_t gainDeltaAdjust;

	HALASSERT(ah->ah_magic == AR5212_MAGIC);

	gainDeltaAdjust = ee->ee_cckOfdmGainDelta;

	/* make a local copy of desired powers as initial indices */
	OS_MEMCPY(ratesIndex, ahp->ah_ratesArray, sizeof(ratesIndex));

	/* fix only the CCK indices */
	for (ii = 8; ii < 15; ii++) {
		/* apply a gain_delta correction of -15 for CCK */
		ratesIndex[ii] -= gainDeltaAdjust;

		/* Now check for contention with all ofdm target powers */
		jj = 0;
		iter = 0;
		/* indicates not all ofdm rates checked forcontention yet */
		while (jj < 16) {
			if (ratesIndex[ii] < 0)
				ratesIndex[ii] = 0;
			if (jj == 8) {		/* skip CCK rates */
				jj = 15;
				continue;
			}
			if (ratesIndex[ii] == ahp->ah_ratesArray[jj]) {
				if (ahp->ah_ratesArray[jj] == 0)
					ratesIndex[ii]++;
				else if (iter > 50) {
					/*
					 * To avoid pathological case of of
					 * dm target powers 0 and 0.5dBm
					 */
					ratesIndex[ii]++;
				} else
					ratesIndex[ii]--;
				/* check with all rates again */
				jj = 0;
				iter++;
			} else
				jj++;
		}
		if (ratesIndex[ii] >= PWR_TABLE_SIZE)
			ratesIndex[ii] = PWR_TABLE_SIZE -1;
		cckIndex = ahp->ah_ratesArray[ii] - twiceOfdmCckDelta;
		if (cckIndex < 0)
			cckIndex = 0;

		/* 
		 * Validate that the indexes for the powv are not
		 * out of bounds.
		 */
		HALASSERT(cckIndex < PWR_TABLE_SIZE);
		HALASSERT(ratesIndex[ii] < PWR_TABLE_SIZE);
		ahp->ah_pcdacTable[ratesIndex[ii]] =
			ahp->ah_pcdacTable[cckIndex];
	}
	/* Override rate per power table with new values */
	for (ii = 8; ii < 15; ii++)
		ahp->ah_ratesArray[ii] = ratesIndex[ii];
#undef N
}

/*
 * Find the maximum conformance test limit for the given channel and CTL info
 */
static uint16_t
ar5212GetMaxEdgePower(uint16_t channel, const RD_EDGES_POWER *pRdEdgesPower)
{
	/* temp array for holding edge channels */
	uint16_t tempChannelList[NUM_EDGES];
	uint16_t clo, chi, twiceMaxEdgePower;
	int i, numEdges;

	/* Get the edge power */
	for (i = 0; i < NUM_EDGES; i++) {
		if (pRdEdgesPower[i].rdEdge == 0)
			break;
		tempChannelList[i] = pRdEdgesPower[i].rdEdge;
	}
	numEdges = i;

	ar5212GetLowerUpperValues(channel, tempChannelList,
		numEdges, &clo, &chi);
	/* Get the index for the lower channel */
	for (i = 0; i < numEdges && clo != tempChannelList[i]; i++)
		;
	/* Is lower channel ever outside the rdEdge? */
	HALASSERT(i != numEdges);

	if ((clo == chi && clo == channel) || (pRdEdgesPower[i].flag)) {
		/* 
		 * If there's an exact channel match or an inband flag set
		 * on the lower channel use the given rdEdgePower 
		 */
		twiceMaxEdgePower = pRdEdgesPower[i].twice_rdEdgePower;
		HALASSERT(twiceMaxEdgePower > 0);
	} else
		twiceMaxEdgePower = MAX_RATE_POWER;
	return twiceMaxEdgePower;
}

/*
 * Returns interpolated or the scaled up interpolated value
 */
static uint16_t
interpolate(uint16_t target, uint16_t srcLeft, uint16_t srcRight,
	uint16_t targetLeft, uint16_t targetRight)
{
	uint16_t rv;
	int16_t lRatio;

	/* to get an accurate ratio, always scale, if want to scale, then don't scale back down */
	if ((targetLeft * targetRight) == 0)
		return 0;

	if (srcRight != srcLeft) {
		/*
		 * Note the ratio always need to be scaled,
		 * since it will be a fraction.
		 */
		lRatio = (target - srcLeft) * EEP_SCALE / (srcRight - srcLeft);
		if (lRatio < 0) {
		    /* Return as Left target if value would be negative */
		    rv = targetLeft;
		} else if (lRatio > EEP_SCALE) {
		    /* Return as Right target if Ratio is greater than 100% (SCALE) */
		    rv = targetRight;
		} else {
			rv = (lRatio * targetRight + (EEP_SCALE - lRatio) *
					targetLeft) / EEP_SCALE;
		}
	} else {
		rv = targetLeft;
	}
	return rv;
}

/*
 * Return the four rates of target power for the given target power table 
 * channel, and number of channels
 */
static void
ar5212GetTargetPowers(struct ath_hal *ah, const struct ieee80211_channel *chan,
	const TRGT_POWER_INFO *powInfo,
	uint16_t numChannels, TRGT_POWER_INFO *pNewPower)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	/* temp array for holding target power channels */
	uint16_t tempChannelList[NUM_TEST_FREQUENCIES];
	uint16_t clo, chi, ixlo, ixhi;
	int i;

	/* Copy the target powers into the temp channel list */
	for (i = 0; i < numChannels; i++)
		tempChannelList[i] = powInfo[i].testChannel;

	ar5212GetLowerUpperValues(freq, tempChannelList,
		numChannels, &clo, &chi);

	/* Get the indices for the channel */
	ixlo = ixhi = 0;
	for (i = 0; i < numChannels; i++) {
		if (clo == tempChannelList[i]) {
			ixlo = i;
		}
		if (chi == tempChannelList[i]) {
			ixhi = i;
			break;
		}
	}

	/*
	 * Get the lower and upper channels, target powers,
	 * and interpolate between them.
	 */
	pNewPower->twicePwr6_24 = interpolate(freq, clo, chi,
		powInfo[ixlo].twicePwr6_24, powInfo[ixhi].twicePwr6_24);
	pNewPower->twicePwr36 = interpolate(freq, clo, chi,
		powInfo[ixlo].twicePwr36, powInfo[ixhi].twicePwr36);
	pNewPower->twicePwr48 = interpolate(freq, clo, chi,
		powInfo[ixlo].twicePwr48, powInfo[ixhi].twicePwr48);
	pNewPower->twicePwr54 = interpolate(freq, clo, chi,
		powInfo[ixlo].twicePwr54, powInfo[ixhi].twicePwr54);
}

static uint32_t
udiff(uint32_t u, uint32_t v)
{
	return (u >= v ? u - v : v - u);
}

/*
 * Search a list for a specified value v that is within
 * EEP_DELTA of the search values.  Return the closest
 * values in the list above and below the desired value.
 * EEP_DELTA is a factional value; everything is scaled
 * so only integer arithmetic is used.
 *
 * NB: the input list is assumed to be sorted in ascending order
 */
void
ar5212GetLowerUpperValues(uint16_t v, uint16_t *lp, uint16_t listSize,
                          uint16_t *vlo, uint16_t *vhi)
{
	uint32_t target = v * EEP_SCALE;
	uint16_t *ep = lp+listSize;

	/*
	 * Check first and last elements for out-of-bounds conditions.
	 */
	if (target < (uint32_t)(lp[0] * EEP_SCALE - EEP_DELTA)) {
		*vlo = *vhi = lp[0];
		return;
	}
	if (target > (uint32_t)(ep[-1] * EEP_SCALE + EEP_DELTA)) {
		*vlo = *vhi = ep[-1];
		return;
	}

	/* look for value being near or between 2 values in list */
	for (; lp < ep; lp++) {
		/*
		 * If value is close to the current value of the list
		 * then target is not between values, it is one of the values
		 */
		if (udiff(lp[0] * EEP_SCALE, target) < EEP_DELTA) {
			*vlo = *vhi = lp[0];
			return;
		}
		/*
		 * Look for value being between current value and next value
		 * if so return these 2 values
		 */
		if (target < (uint32_t)(lp[1] * EEP_SCALE - EEP_DELTA)) {
			*vlo = lp[0];
			*vhi = lp[1];
			return;
		}
	}
	HALASSERT(AH_FALSE);		/* should not reach here */
}

/*
 * Perform analog "swizzling" of parameters into their location
 *
 * NB: used by RF backends
 */
void
ar5212ModifyRfBuffer(uint32_t *rfBuf, uint32_t reg32, uint32_t numBits,
                     uint32_t firstBit, uint32_t column)
{
#define	MAX_ANALOG_START	319		/* XXX */
	uint32_t tmp32, mask, arrayEntry, lastBit;
	int32_t bitPosition, bitsLeft;

	HALASSERT(column <= 3);
	HALASSERT(numBits <= 32);
	HALASSERT(firstBit + numBits <= MAX_ANALOG_START);

	tmp32 = ath_hal_reverseBits(reg32, numBits);
	arrayEntry = (firstBit - 1) / 8;
	bitPosition = (firstBit - 1) % 8;
	bitsLeft = numBits;
	while (bitsLeft > 0) {
		lastBit = (bitPosition + bitsLeft > 8) ?
			8 : bitPosition + bitsLeft;
		mask = (((1 << lastBit) - 1) ^ ((1 << bitPosition) - 1)) <<
			(column * 8);
		rfBuf[arrayEntry] &= ~mask;
		rfBuf[arrayEntry] |= ((tmp32 << bitPosition) <<
			(column * 8)) & mask;
		bitsLeft -= 8 - bitPosition;
		tmp32 = tmp32 >> (8 - bitPosition);
		bitPosition = 0;
		arrayEntry++;
	}
#undef MAX_ANALOG_START
}

/*
 * Sets the rate to duration values in MAC - used for multi-
 * rate retry.
 * The rate duration table needs to cover all valid rate codes;
 * the 11g table covers all ofdm rates, while the 11b table
 * covers all cck rates => all valid rates get covered between
 * these two mode's ratetables!
 * But if we're turbo, the ofdm phy is replaced by the turbo phy
 * and cck is not valid with turbo => all rates get covered
 * by the turbo ratetable only
 */
void
ar5212SetRateDurationTable(struct ath_hal *ah,
	const struct ieee80211_channel *chan)
{
	const HAL_RATE_TABLE *rt;
	int i;

	/* NB: band doesn't matter for 1/2 and 1/4 rate */
	if (IEEE80211_IS_CHAN_HALF(chan)) {
		rt = ar5212GetRateTable(ah, HAL_MODE_11A_HALF_RATE);
	} else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
		rt = ar5212GetRateTable(ah, HAL_MODE_11A_QUARTER_RATE);
	} else {
		rt = ar5212GetRateTable(ah,
			IEEE80211_IS_CHAN_TURBO(chan) ? HAL_MODE_TURBO : HAL_MODE_11G);
	}

	for (i = 0; i < rt->rateCount; ++i)
		OS_REG_WRITE(ah,
			AR_RATE_DURATION(rt->info[i].rateCode),
			ath_hal_computetxtime(ah, rt,
				WLAN_CTRL_FRAME_SIZE,
				rt->info[i].controlRate, AH_FALSE, AH_TRUE));
	if (!IEEE80211_IS_CHAN_TURBO(chan)) {
		/* 11g Table is used to cover the CCK rates. */
		rt = ar5212GetRateTable(ah, HAL_MODE_11G);
		for (i = 0; i < rt->rateCount; ++i) {
			uint32_t reg = AR_RATE_DURATION(rt->info[i].rateCode);

			if (rt->info[i].phy != IEEE80211_T_CCK)
				continue;

			OS_REG_WRITE(ah, reg,
				ath_hal_computetxtime(ah, rt,
					WLAN_CTRL_FRAME_SIZE,
					rt->info[i].controlRate, AH_FALSE,
					AH_TRUE));
			/* cck rates have short preamble option also */
			if (rt->info[i].shortPreamble) {
				reg += rt->info[i].shortPreamble << 2;
				OS_REG_WRITE(ah, reg,
					ath_hal_computetxtime(ah, rt,
						WLAN_CTRL_FRAME_SIZE,
						rt->info[i].controlRate,
						AH_TRUE, AH_TRUE));
			}
		}
	}
}

/* Adjust various register settings based on half/quarter rate clock setting.
 * This includes: +USEC, TX/RX latency, 
 *                + IFS params: slot, eifs, misc etc.
 */
void 
ar5212SetIFSTiming(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t txLat, rxLat, usec, slot, refClock, eifs, init_usec;

	HALASSERT(IEEE80211_IS_CHAN_HALF(chan) ||
		  IEEE80211_IS_CHAN_QUARTER(chan));

	refClock = OS_REG_READ(ah, AR_USEC) & AR_USEC_USEC32;
	if (IEEE80211_IS_CHAN_HALF(chan)) {
		slot = IFS_SLOT_HALF_RATE;
		rxLat = RX_NON_FULL_RATE_LATENCY << AR5212_USEC_RX_LAT_S;
		txLat = TX_HALF_RATE_LATENCY << AR5212_USEC_TX_LAT_S;
		usec = HALF_RATE_USEC;
		eifs = IFS_EIFS_HALF_RATE;
		init_usec = INIT_USEC >> 1;
	} else { /* quarter rate */
		slot = IFS_SLOT_QUARTER_RATE;
		rxLat = RX_NON_FULL_RATE_LATENCY << AR5212_USEC_RX_LAT_S;
		txLat = TX_QUARTER_RATE_LATENCY << AR5212_USEC_TX_LAT_S;
		usec = QUARTER_RATE_USEC;
		eifs = IFS_EIFS_QUARTER_RATE;
		init_usec = INIT_USEC >> 2;
	}

	OS_REG_WRITE(ah, AR_USEC, (usec | refClock | txLat | rxLat));
	OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, slot);
	OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, eifs);
	OS_REG_RMW_FIELD(ah, AR_D_GBL_IFS_MISC,
				AR_D_GBL_IFS_MISC_USEC_DURATION, init_usec);
}
