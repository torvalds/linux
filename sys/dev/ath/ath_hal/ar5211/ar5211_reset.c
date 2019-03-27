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

/*
 * Chips specific device attachment and device info collection
 * Connects Init Reg Vectors, EEPROM Data, and device Functions.
 */
#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ar5211/ar5211.h"
#include "ar5211/ar5211reg.h"
#include "ar5211/ar5211phy.h"

#include "ah_eeprom_v3.h"

/* Add static register initialization vectors */
#include "ar5211/boss.ini"

/*
 * Structure to hold 11b tuning information for Beanie/Sombrero
 * 16 MHz mode, divider ratio = 198 = NP+S. N=16, S=4 or 6, P=12
 */
typedef struct {
	uint32_t	refClkSel;	/* reference clock, 1 for 16 MHz */
	uint32_t	channelSelect;	/* P[7:4]S[3:0] bits */
	uint16_t	channel5111;	/* 11a channel for 5111 */
} CHAN_INFO_2GHZ;

#define CI_2GHZ_INDEX_CORRECTION 19
static const CHAN_INFO_2GHZ chan2GHzData[] = {
	{ 1, 0x46, 96  },	/* 2312 -19 */
	{ 1, 0x46, 97  },	/* 2317 -18 */
	{ 1, 0x46, 98  },	/* 2322 -17 */
	{ 1, 0x46, 99  },	/* 2327 -16 */
	{ 1, 0x46, 100 },	/* 2332 -15 */
	{ 1, 0x46, 101 },	/* 2337 -14 */
	{ 1, 0x46, 102 },	/* 2342 -13 */
	{ 1, 0x46, 103 },	/* 2347 -12 */
	{ 1, 0x46, 104 },	/* 2352 -11 */
	{ 1, 0x46, 105 },	/* 2357 -10 */
	{ 1, 0x46, 106 },	/* 2362  -9 */
	{ 1, 0x46, 107 },	/* 2367  -8 */
	{ 1, 0x46, 108 },	/* 2372  -7 */
	/* index -6 to 0 are pad to make this a nolookup table */
	{ 1, 0x46, 116 },	/*       -6 */
	{ 1, 0x46, 116 },	/*       -5 */
	{ 1, 0x46, 116 },	/*       -4 */
	{ 1, 0x46, 116 },	/*       -3 */
	{ 1, 0x46, 116 },	/*       -2 */
	{ 1, 0x46, 116 },	/*       -1 */
	{ 1, 0x46, 116 },	/*        0 */
	{ 1, 0x46, 116 },	/* 2412   1 */
	{ 1, 0x46, 117 },	/* 2417   2 */
	{ 1, 0x46, 118 },	/* 2422   3 */
	{ 1, 0x46, 119 },	/* 2427   4 */
	{ 1, 0x46, 120 },	/* 2432   5 */
	{ 1, 0x46, 121 },	/* 2437   6 */
	{ 1, 0x46, 122 },	/* 2442   7 */
	{ 1, 0x46, 123 },	/* 2447   8 */
	{ 1, 0x46, 124 },	/* 2452   9 */
	{ 1, 0x46, 125 },	/* 2457  10 */
	{ 1, 0x46, 126 },	/* 2462  11 */
	{ 1, 0x46, 127 },	/* 2467  12 */
	{ 1, 0x46, 128 },	/* 2472  13 */
	{ 1, 0x44, 124 },	/* 2484  14 */
	{ 1, 0x46, 136 },	/* 2512  15 */
	{ 1, 0x46, 140 },	/* 2532  16 */
	{ 1, 0x46, 144 },	/* 2552  17 */
	{ 1, 0x46, 148 },	/* 2572  18 */
	{ 1, 0x46, 152 },	/* 2592  19 */
	{ 1, 0x46, 156 },	/* 2612  20 */
	{ 1, 0x46, 160 },	/* 2632  21 */
	{ 1, 0x46, 164 },	/* 2652  22 */
	{ 1, 0x46, 168 },	/* 2672  23 */
	{ 1, 0x46, 172 },	/* 2692  24 */
	{ 1, 0x46, 176 },	/* 2712  25 */
	{ 1, 0x46, 180 } 	/* 2732  26 */
};

/* Power timeouts in usec to wait for chip to wake-up. */
#define POWER_UP_TIME	2000

#define	DELAY_PLL_SETTLE	300		/* 300 us */
#define	DELAY_BASE_ACTIVATE	100		/* 100 us */

#define NUM_RATES	8

static HAL_BOOL ar5211SetResetReg(struct ath_hal *ah, uint32_t resetMask);
static HAL_BOOL ar5211SetChannel(struct ath_hal *,
		const struct ieee80211_channel *);
static int16_t ar5211RunNoiseFloor(struct ath_hal *,
		uint8_t runTime, int16_t startingNF);
static HAL_BOOL ar5211IsNfGood(struct ath_hal *,
		struct ieee80211_channel *chan);
static HAL_BOOL ar5211SetRf6and7(struct ath_hal *,
		const struct ieee80211_channel *chan);
static HAL_BOOL ar5211SetBoardValues(struct ath_hal *,
		const struct ieee80211_channel *chan);
static void ar5211SetPowerTable(struct ath_hal *,
		PCDACS_EEPROM *pSrcStruct, uint16_t channel);
static HAL_BOOL ar5211SetTransmitPower(struct ath_hal *,
		const struct ieee80211_channel *);
static void ar5211SetRateTable(struct ath_hal *,
		RD_EDGES_POWER *pRdEdgesPower, TRGT_POWER_INFO *pPowerInfo,
		uint16_t numChannels, const struct ieee80211_channel *chan);
static uint16_t ar5211GetScaledPower(uint16_t channel, uint16_t pcdacValue,
		const PCDACS_EEPROM *pSrcStruct);
static HAL_BOOL ar5211FindValueInList(uint16_t channel, uint16_t pcdacValue,
		const PCDACS_EEPROM *pSrcStruct, uint16_t *powerValue);
static uint16_t ar5211GetInterpolatedValue(uint16_t target,
		uint16_t srcLeft, uint16_t srcRight,
		uint16_t targetLeft, uint16_t targetRight, HAL_BOOL scaleUp);
static void ar5211GetLowerUpperValues(uint16_t value,
		const uint16_t *pList, uint16_t listSize,
		uint16_t *pLowerValue, uint16_t *pUpperValue);
static void ar5211GetLowerUpperPcdacs(uint16_t pcdac,
		uint16_t channel, const PCDACS_EEPROM *pSrcStruct,
		uint16_t *pLowerPcdac, uint16_t *pUpperPcdac);

static void ar5211SetRfgain(struct ath_hal *, const GAIN_VALUES *);
static void ar5211RequestRfgain(struct ath_hal *);
static HAL_BOOL ar5211InvalidGainReadback(struct ath_hal *, GAIN_VALUES *);
static HAL_BOOL ar5211IsGainAdjustNeeded(struct ath_hal *, const GAIN_VALUES *);
static int32_t ar5211AdjustGain(struct ath_hal *, GAIN_VALUES *);
static void ar5211SetOperatingMode(struct ath_hal *, int opmode);

/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 *
 * bChannelChange is used to preserve DMA/PCU registers across
 * a HW Reset during channel change.
 */
HAL_BOOL
ar5211Reset(struct ath_hal *ah, HAL_OPMODE opmode,
	struct ieee80211_channel *chan, HAL_BOOL bChannelChange,
	HAL_RESET_TYPE resetType,
	HAL_STATUS *status)
{
uint32_t softLedCfg, softLedState;
#define	N(a)	(sizeof (a) /sizeof (a[0]))
#define	FAIL(_code)	do { ecode = _code; goto bad; } while (0)
	struct ath_hal_5211 *ahp = AH5211(ah);
	HAL_CHANNEL_INTERNAL *ichan;
	uint32_t i, ledstate;
	HAL_STATUS ecode;
	int q;

	uint32_t		data, synthDelay;
	uint32_t		macStaId1;    
	uint16_t		modesIndex = 0, freqIndex = 0;
	uint32_t		saveFrameSeqCount[AR_NUM_DCU];
	uint32_t		saveTsfLow = 0, saveTsfHigh = 0;
	uint32_t		saveDefAntenna;

	HALDEBUG(ah, HAL_DEBUG_RESET,
	     "%s: opmode %u channel %u/0x%x %s channel\n",
	     __func__, opmode, chan->ic_freq, chan->ic_flags,
	     bChannelChange ? "change" : "same");

	OS_MARK(ah, AH_MARK_RESET, bChannelChange);
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
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid operating mode %u\n", __func__, opmode);
		FAIL(HAL_EINVAL);
		break;
	}
	HALASSERT(AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER3);

	/* Preserve certain DMA hardware registers on a channel change */
	if (bChannelChange) {
		/*
		 * Need to save/restore the TSF because of an issue
		 * that accelerates the TSF during a chip reset.
		 *
		 * We could use system timer routines to more
		 * accurately restore the TSF, but
		 * 1. Timer routines on certain platforms are
		 *	not accurate enough (e.g. 1 ms resolution).
		 * 2. It would still not be accurate.
		 *
		 * The most important aspect of this workaround,
		 * is that, after reset, the TSF is behind
		 * other STAs TSFs.  This will allow the STA to
		 * properly resynchronize its TSF in adhoc mode.
		 */
		saveTsfLow  = OS_REG_READ(ah, AR_TSF_L32);
		saveTsfHigh = OS_REG_READ(ah, AR_TSF_U32);

		/* Read frame sequence count */
		if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU) {
			saveFrameSeqCount[0] = OS_REG_READ(ah, AR_D0_SEQNUM);
		} else {
			for (i = 0; i < AR_NUM_DCU; i++)
				saveFrameSeqCount[i] = OS_REG_READ(ah, AR_DSEQNUM(i));
		}
		if (!IEEE80211_IS_CHAN_DFS(chan)) 
			chan->ic_state &= ~IEEE80211_CHANSTATE_CWINT;
	}

	/*
	 * Preserve the antenna on a channel change
	 */
	saveDefAntenna = OS_REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)
		saveDefAntenna = 1;

	/* Save hardware flag before chip reset clears the register */
	macStaId1 = OS_REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

	/* Save led state from pci config register */
	ledstate = OS_REG_READ(ah, AR_PCICFG) &
		(AR_PCICFG_LEDCTL | AR_PCICFG_LEDMODE | AR_PCICFG_LEDBLINK |
		 AR_PCICFG_LEDSLOW);
	softLedCfg = OS_REG_READ(ah, AR_GPIOCR);
	softLedState = OS_REG_READ(ah, AR_GPIODO);

	if (!ar5211ChipReset(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: chip reset failed\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Setup the indices for the next set of register array writes */
	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		freqIndex = 1;
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
	} else {
		freqIndex = 2;
		if (IEEE80211_IS_CHAN_B(chan))
			modesIndex = 3;
		else if (IEEE80211_IS_CHAN_PUREG(chan))
			modesIndex = 4;
		else {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid channel %u/0x%x\n",
			    __func__, chan->ic_freq, chan->ic_flags);
			FAIL(HAL_EINVAL);
		}
	}

	/* Set correct Baseband to analog shift setting to access analog chips. */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU) {
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00000007);
	} else {
		OS_REG_WRITE(ah, AR_PHY_BASE, 0x00000047);
	}

	/* Write parameters specific to AR5211 */
	if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU) {
		if (IEEE80211_IS_CHAN_2GHZ(chan) &&
		    AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER3_1) {
			HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
			uint32_t ob2GHz, db2GHz;

			if (IEEE80211_IS_CHAN_CCK(chan)) {
				ob2GHz = ee->ee_ob2GHz[0];
				db2GHz = ee->ee_db2GHz[0];
			} else {
				ob2GHz = ee->ee_ob2GHz[1];
				db2GHz = ee->ee_db2GHz[1];
			}
			ob2GHz = ath_hal_reverseBits(ob2GHz, 3);
			db2GHz = ath_hal_reverseBits(db2GHz, 3);
			ar5211Mode2_4[25][freqIndex] =
				(ar5211Mode2_4[25][freqIndex] & ~0xC0) |
					((ob2GHz << 6) & 0xC0);
			ar5211Mode2_4[26][freqIndex] =
				(ar5211Mode2_4[26][freqIndex] & ~0x0F) |
					(((ob2GHz >> 2) & 0x1) |
					 ((db2GHz << 1) & 0x0E));
		}
		for (i = 0; i < N(ar5211Mode2_4); i++)
			OS_REG_WRITE(ah, ar5211Mode2_4[i][0],
				ar5211Mode2_4[i][freqIndex]);
	}

	/* Write the analog registers 6 and 7 before other config */
	ar5211SetRf6and7(ah, chan);

	/* Write registers that vary across all modes */
	for (i = 0; i < N(ar5211Modes); i++)
		OS_REG_WRITE(ah, ar5211Modes[i][0], ar5211Modes[i][modesIndex]);

	/* Write RFGain Parameters that differ between 2.4 and 5 GHz */
	for (i = 0; i < N(ar5211BB_RfGain); i++)
		OS_REG_WRITE(ah, ar5211BB_RfGain[i][0], ar5211BB_RfGain[i][freqIndex]);

	/* Write Common Array Parameters */
	for (i = 0; i < N(ar5211Common); i++) {
		uint32_t reg = ar5211Common[i][0];
		/* On channel change, don't reset the PCU registers */
		if (!(bChannelChange && (0x8000 <= reg && reg < 0x9000)))
			OS_REG_WRITE(ah, reg, ar5211Common[i][1]);
	}

	/* Fix pre-AR5211 register values, this includes AR5311s. */
	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_OAHU) {
		/*
		 * The TX and RX latency values have changed locations
		 * within the USEC register in AR5211.  Since they're
		 * set via the .ini, for both AR5211 and AR5311, they
		 * are written properly here for AR5311.
		 */
		data = OS_REG_READ(ah, AR_USEC);
		/* Must be 0 for proper write in AR5311 */
		HALASSERT((data & 0x00700000) == 0);
		OS_REG_WRITE(ah, AR_USEC,
			(data & (AR_USEC_M | AR_USEC_32_M | AR5311_USEC_TX_LAT_M)) |
			((29 << AR5311_USEC_RX_LAT_S) & AR5311_USEC_RX_LAT_M));
		/* The following registers exist only on AR5311. */
		OS_REG_WRITE(ah, AR5311_QDCLKGATE, 0);

		/* Set proper ADC & DAC delays for AR5311. */
		OS_REG_WRITE(ah, 0x00009878, 0x00000008);

		/* Enable the PCU FIFO corruption ECO on AR5311. */
		OS_REG_WRITE(ah, AR_DIAG_SW,
			OS_REG_READ(ah, AR_DIAG_SW) | AR5311_DIAG_SW_USE_ECO);
	}

	/* Restore certain DMA hardware registers on a channel change */
	if (bChannelChange) {
		/* Restore TSF */
		OS_REG_WRITE(ah, AR_TSF_L32, saveTsfLow);
		OS_REG_WRITE(ah, AR_TSF_U32, saveTsfHigh);

		if (AH_PRIVATE(ah)->ah_macVersion >= AR_SREV_VERSION_OAHU) {
			OS_REG_WRITE(ah, AR_D0_SEQNUM, saveFrameSeqCount[0]);
		} else {
			for (i = 0; i < AR_NUM_DCU; i++)
				OS_REG_WRITE(ah, AR_DSEQNUM(i), saveFrameSeqCount[i]);
		}
	}

	OS_REG_WRITE(ah, AR_STA_ID0, LE_READ_4(ahp->ah_macaddr));
	OS_REG_WRITE(ah, AR_STA_ID1, LE_READ_2(ahp->ah_macaddr + 4)
		| macStaId1
	);
	ar5211SetOperatingMode(ah, opmode);

	/* Restore previous led state */
	OS_REG_WRITE(ah, AR_PCICFG, OS_REG_READ(ah, AR_PCICFG) | ledstate);
	OS_REG_WRITE(ah, AR_GPIOCR, softLedCfg);
	OS_REG_WRITE(ah, AR_GPIODO, softLedState);

	/* Restore previous antenna */
	OS_REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
	OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid + 4));

	/* Restore bmiss rssi & count thresholds */
	OS_REG_WRITE(ah, AR_RSSI_THR, ahp->ah_rssiThr);

	OS_REG_WRITE(ah, AR_ISR, ~0);		/* cleared on write */

	/*
	 * for pre-Production Oahu only.
	 * Disable clock gating in all DMA blocks. Helps when using
	 * 11B and AES but results in higher power consumption.
	 */
	if (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_OAHU &&
	    AH_PRIVATE(ah)->ah_macRev < AR_SREV_OAHU_PROD) {
		OS_REG_WRITE(ah, AR_CFG,
			OS_REG_READ(ah, AR_CFG) | AR_CFG_CLK_GATE_DIS);
	}

	/* Setup the transmit power values. */
	if (!ar5211SetTransmitPower(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: error init'ing transmit power\n", __func__);
		FAIL(HAL_EIO);
	}

	/*
	 * Configurable OFDM spoofing for 11n compatibility; used
	 * only when operating in station mode.
	 */
	if (opmode != HAL_M_HOSTAP &&
	    (AH_PRIVATE(ah)->ah_11nCompat & HAL_DIAG_11N_SERVICES) != 0) {
		/* NB: override the .ini setting */
		OS_REG_RMW_FIELD(ah, AR_PHY_FRAME_CTL,
			AR_PHY_FRAME_CTL_ERR_SERV,
			MS(AH_PRIVATE(ah)->ah_11nCompat, HAL_DIAG_11N_SERVICES)&1);
	}

	/* Setup board specific options for EEPROM version 3 */
	ar5211SetBoardValues(ah, chan);

	if (!ar5211SetChannel(ah, chan)) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: unable to set channel\n",
		    __func__);
		FAIL(HAL_EIO);
	}

	/* Activate the PHY */
	if (AH_PRIVATE(ah)->ah_devid == AR5211_FPGA11B &&
	    IEEE80211_IS_CHAN_2GHZ(chan))
		OS_REG_WRITE(ah, 0xd808, 0x502); /* required for FPGA */
	OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/*
	 * Wait for the frequency synth to settle (synth goes on
	 * via AR_PHY_ACTIVE_EN).  Read the phy active delay register.
	 * Value is in 100ns increments.
	 */
	data = OS_REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_M;
	if (IEEE80211_IS_CHAN_CCK(chan)) {
		synthDelay = (4 * data) / 22;
	} else {
		synthDelay = data / 10;
	}
	/*
	 * There is an issue if the AP starts the calibration before
	 * the baseband timeout completes.  This could result in the
	 * rxclear false triggering.  Add an extra delay to ensure this
	 * this does not happen.
	 */
	OS_DELAY(synthDelay + DELAY_BASE_ACTIVATE);

	/* Calibrate the AGC and wait for completion. */
	OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		 OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_CAL);
	(void) ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0);

	/* Perform noise floor and set status */
	if (!ar5211CalNoiseFloor(ah, chan)) {
		if (!IEEE80211_IS_CHAN_CCK(chan))
			chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: noise floor calibration failed\n", __func__);
		FAIL(HAL_EIO);
	}

	/* Start IQ calibration w/ 2^(INIT_IQCAL_LOG_COUNT_MAX+1) samples */
	if (ahp->ah_calibrationTime != 0) {
		OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4,
			AR_PHY_TIMING_CTRL4_DO_IQCAL | (INIT_IQCAL_LOG_COUNT_MAX << AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX_S));
		ahp->ah_bIQCalibration = AH_TRUE;
	}

	/* set 1:1 QCU to DCU mapping for all queues */
	for (q = 0; q < AR_NUM_DCU; q++)
		OS_REG_WRITE(ah, AR_DQCUMASK(q), 1<<q);

	for (q = 0; q < HAL_NUM_TX_QUEUES; q++)
		ar5211ResetTxQueue(ah, q);

	/* Setup QCU0 transmit interrupt masks (TX_ERR, TX_OK, TX_DESC, TX_URN) */
	OS_REG_WRITE(ah, AR_IMR_S0,
		 (AR_IMR_S0_QCU_TXOK & AR_QCU_0) |
		 (AR_IMR_S0_QCU_TXDESC & (AR_QCU_0<<AR_IMR_S0_QCU_TXDESC_S)));
	OS_REG_WRITE(ah, AR_IMR_S1, (AR_IMR_S1_QCU_TXERR & AR_QCU_0));
	OS_REG_WRITE(ah, AR_IMR_S2, (AR_IMR_S2_QCU_TXURN & AR_QCU_0));

	/*
	 * GBL_EIFS must always be written after writing
	 *		to any QCUMASK register.
	 */
	OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, OS_REG_READ(ah, AR_D_GBL_IFS_EIFS));

	/* Now set up the Interrupt Mask Register and save it for future use */
	OS_REG_WRITE(ah, AR_IMR, INIT_INTERRUPT_MASK);
	ahp->ah_maskReg = INIT_INTERRUPT_MASK;

	/* Enable bus error interrupts */
	OS_REG_WRITE(ah, AR_IMR_S2, OS_REG_READ(ah, AR_IMR_S2) |
		 AR_IMR_S2_MCABT | AR_IMR_S2_SSERR | AR_IMR_S2_DPERR);

	/* Enable interrupts specific to AP */
	if (opmode == HAL_M_HOSTAP) {
		OS_REG_WRITE(ah, AR_IMR, OS_REG_READ(ah, AR_IMR) | AR_IMR_MIB);
		ahp->ah_maskReg |= AR_IMR_MIB;
	}

	if (AH_PRIVATE(ah)->ah_rfkillEnabled)
		ar5211EnableRfKill(ah);

	/*
	 * Writing to AR_BEACON will start timers. Hence it should
	 * be the last register to be written. Do not reset tsf, do
	 * not enable beacons at this point, but preserve other values
	 * like beaconInterval.
	 */
	OS_REG_WRITE(ah, AR_BEACON,
		(OS_REG_READ(ah, AR_BEACON) &~ (AR_BEACON_EN | AR_BEACON_RESET_TSF)));

	/* Restore user-specified slot time and timeouts */
	if (ahp->ah_sifstime != (u_int) -1)
		ar5211SetSifsTime(ah, ahp->ah_sifstime);
	if (ahp->ah_slottime != (u_int) -1)
		ar5211SetSlotTime(ah, ahp->ah_slottime);
	if (ahp->ah_acktimeout != (u_int) -1)
		ar5211SetAckTimeout(ah, ahp->ah_acktimeout);
	if (ahp->ah_ctstimeout != (u_int) -1)
		ar5211SetCTSTimeout(ah, ahp->ah_ctstimeout);
	if (AH_PRIVATE(ah)->ah_diagreg != 0)
		OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);

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

/*
 * Places the PHY and Radio chips into reset.  A full reset
 * must be called to leave this state.  The PCI/MAC/PCU are
 * not placed into reset as we must receive interrupt to
 * re-enable the hardware.
 */
HAL_BOOL
ar5211PhyDisable(struct ath_hal *ah)
{
	return ar5211SetResetReg(ah, AR_RC_BB);
}

/*
 * Places all of hardware into reset
 */
HAL_BOOL
ar5211Disable(struct ath_hal *ah)
{
	if (!ar5211SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;
	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset.
	 */
	if (!ar5211SetResetReg(ah, AR_RC_MAC | AR_RC_BB | AR_RC_PCI))
		return AH_FALSE;
	OS_DELAY(2100);	   /* 8245 @ 96Mhz hangs with 2000us. */

	return AH_TRUE;
}

/*
 * Places the hardware into reset and then pulls it out of reset
 *
 * Only write the PLL if we're changing to or from CCK mode
 *
 * Attach calls with channelFlags = 0, as the coldreset should have
 * us in the correct mode and we cannot check the hwchannel flags.
 */
HAL_BOOL
ar5211ChipReset(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	if (!ar5211SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/* NB: called from attach with chan null */
	if (chan != AH_NULL) {
		/* Set CCK and Turbo modes correctly */
		OS_REG_WRITE(ah, AR_PHY_TURBO, IEEE80211_IS_CHAN_TURBO(chan) ?
		    AR_PHY_FC_TURBO_MODE | AR_PHY_FC_TURBO_SHORT : 0);
		if (IEEE80211_IS_CHAN_B(chan)) {
			OS_REG_WRITE(ah, AR5211_PHY_MODE,
			    AR5211_PHY_MODE_CCK | AR5211_PHY_MODE_RF2GHZ);
			OS_REG_WRITE(ah, AR_PHY_PLL_CTL, AR_PHY_PLL_CTL_44);
			/* Wait for the PLL to settle */
			OS_DELAY(DELAY_PLL_SETTLE);
		} else if (AH_PRIVATE(ah)->ah_devid == AR5211_DEVID) {
			OS_REG_WRITE(ah, AR_PHY_PLL_CTL, AR_PHY_PLL_CTL_40);
			OS_DELAY(DELAY_PLL_SETTLE);
			OS_REG_WRITE(ah, AR5211_PHY_MODE,
			    AR5211_PHY_MODE_OFDM | (IEEE80211_IS_CHAN_2GHZ(chan) ?
				AR5211_PHY_MODE_RF2GHZ :
				AR5211_PHY_MODE_RF5GHZ));
		}
	}

	/*
	 * Reset the HW - PCI must be reset after the rest of the
	 * device has been reset
	 */
	if (!ar5211SetResetReg(ah, AR_RC_MAC | AR_RC_BB | AR_RC_PCI))
		return AH_FALSE;
	OS_DELAY(2100);	   /* 8245 @ 96Mhz hangs with 2000us. */

	/* Bring out of sleep mode (AGAIN) */
	if (!ar5211SetPowerMode(ah, HAL_PM_AWAKE, AH_TRUE))
		return AH_FALSE;

	/* Clear warm reset register */
	return ar5211SetResetReg(ah, 0);
}

/*
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
HAL_BOOL
ar5211PerCalibrationN(struct ath_hal *ah,  struct ieee80211_channel *chan,
	u_int chainMask, HAL_BOOL longCal, HAL_BOOL *isCalDone)
{
	struct ath_hal_5211 *ahp = AH5211(ah);
	HAL_CHANNEL_INTERNAL *ichan;
	int32_t qCoff, qCoffDenom;
	uint32_t data;
	int32_t iqCorrMeas;
	int32_t iCoff, iCoffDenom;
	uint32_t powerMeasQ, powerMeasI;

	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return AH_FALSE;
	}
	/* IQ calibration in progress. Check to see if it has finished. */
	if (ahp->ah_bIQCalibration &&
	    !(OS_REG_READ(ah, AR_PHY_TIMING_CTRL4) & AR_PHY_TIMING_CTRL4_DO_IQCAL)) {
		/* IQ Calibration has finished. */
		ahp->ah_bIQCalibration = AH_FALSE;

		/* Read calibration results. */
		powerMeasI = OS_REG_READ(ah, AR_PHY_IQCAL_RES_PWR_MEAS_I);
		powerMeasQ = OS_REG_READ(ah, AR_PHY_IQCAL_RES_PWR_MEAS_Q);
		iqCorrMeas = OS_REG_READ(ah, AR_PHY_IQCAL_RES_IQ_CORR_MEAS);

		/*
		 * Prescale these values to remove 64-bit operation requirement at the loss
		 * of a little precision.
		 */
		iCoffDenom = (powerMeasI / 2 + powerMeasQ / 2) / 128;
		qCoffDenom = powerMeasQ / 64;

		/* Protect against divide-by-0. */
		if (iCoffDenom != 0 && qCoffDenom != 0) {
			iCoff = (-iqCorrMeas) / iCoffDenom;
			/* IQCORR_Q_I_COFF is a signed 6 bit number */
			iCoff = iCoff & 0x3f;

			qCoff = ((int32_t)powerMeasI / qCoffDenom) - 64;
			/* IQCORR_Q_Q_COFF is a signed 5 bit number */
			qCoff = qCoff & 0x1f;

			HALDEBUG(ah, HAL_DEBUG_PERCAL, "powerMeasI = 0x%08x\n",
			    powerMeasI);
			HALDEBUG(ah, HAL_DEBUG_PERCAL, "powerMeasQ = 0x%08x\n",
			    powerMeasQ);
			HALDEBUG(ah, HAL_DEBUG_PERCAL, "iqCorrMeas = 0x%08x\n",
			    iqCorrMeas);
			HALDEBUG(ah, HAL_DEBUG_PERCAL, "iCoff	  = %d\n",
			    iCoff);
			HALDEBUG(ah, HAL_DEBUG_PERCAL, "qCoff	  = %d\n",
			    qCoff);

			/* Write IQ */
			data  = OS_REG_READ(ah, AR_PHY_TIMING_CTRL4) |
				AR_PHY_TIMING_CTRL4_IQCORR_ENABLE |
				(((uint32_t)iCoff) << AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_S) |
				((uint32_t)qCoff);
			OS_REG_WRITE(ah, AR_PHY_TIMING_CTRL4, data);
		}
	}
	*isCalDone = !ahp->ah_bIQCalibration;

	if (longCal) {
		/* Perform noise floor and set status */
		if (!ar5211IsNfGood(ah, chan)) {
			/* report up and clear internal state */
			chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
			return AH_FALSE;
		}
		if (!ar5211CalNoiseFloor(ah, chan)) {
			/*
			 * Delay 5ms before retrying the noise floor
			 * just to make sure, as we are in an error
			 * condition here.
			 */
			OS_DELAY(5000);
			if (!ar5211CalNoiseFloor(ah, chan)) {
				if (!IEEE80211_IS_CHAN_CCK(chan))
					chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
				return AH_FALSE;
			}
		}
		ar5211RequestRfgain(ah);
	}
	return AH_TRUE;
}

HAL_BOOL
ar5211PerCalibration(struct ath_hal *ah, struct ieee80211_channel *chan,
	HAL_BOOL *isIQdone)
{
	return ar5211PerCalibrationN(ah,  chan, 0x1, AH_TRUE, isIQdone);
}

HAL_BOOL
ar5211ResetCalValid(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	/* XXX */
	return AH_TRUE;
}

/*
 * Writes the given reset bit mask into the reset register
 */
static HAL_BOOL
ar5211SetResetReg(struct ath_hal *ah, uint32_t resetMask)
{
	uint32_t mask = resetMask ? resetMask : ~0;
	HAL_BOOL rt;

	(void) OS_REG_READ(ah, AR_RXDP);/* flush any pending MMR writes */
	OS_REG_WRITE(ah, AR_RC, resetMask);

	/* need to wait at least 128 clocks when reseting PCI before read */
	OS_DELAY(15);

	resetMask &= AR_RC_MAC | AR_RC_BB;
	mask &= AR_RC_MAC | AR_RC_BB;
	rt = ath_hal_wait(ah, AR_RC, mask, resetMask);
        if ((resetMask & AR_RC_MAC) == 0) {
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
 * Takes the MHz channel value and sets the Channel value
 *
 * ASSUMES: Writes enabled to analog bus before AGC is active
 *   or by disabling the AGC.
 */
static HAL_BOOL
ar5211SetChannel(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint32_t refClk, reg32, data2111;
	int16_t chan5111, chanIEEE;

	chanIEEE = chan->ic_ieee;
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		const CHAN_INFO_2GHZ* ci =
			&chan2GHzData[chanIEEE + CI_2GHZ_INDEX_CORRECTION];

		data2111 = ((ath_hal_reverseBits(ci->channelSelect, 8) & 0xff)
				<< 5)
			 | (ci->refClkSel << 4);
		chan5111 = ci->channel5111;
	} else {
		data2111 = 0;
		chan5111 = chanIEEE;
	}

	/* Rest of the code is common for 5 GHz and 2.4 GHz. */
	if (chan5111 >= 145 || (chan5111 & 0x1)) {
		reg32 = ath_hal_reverseBits(chan5111 - 24, 8) & 0xFF;
		refClk = 1;
	} else {
		reg32 = ath_hal_reverseBits(((chan5111 - 24) / 2), 8) & 0xFF;
		refClk = 0;
	}

	reg32 = (reg32 << 2) | (refClk << 1) | (1 << 10) | 0x1;
	OS_REG_WRITE(ah, AR_PHY(0x27), ((data2111 & 0xff) << 8) | (reg32 & 0xff));
	reg32 >>= 8;
	OS_REG_WRITE(ah, AR_PHY(0x34), (data2111 & 0xff00) | (reg32 & 0xff));

	AH_PRIVATE(ah)->ah_curchan = chan;
	return AH_TRUE;
}

static int16_t
ar5211GetNoiseFloor(struct ath_hal *ah)
{
	int16_t nf;

	nf = (OS_REG_READ(ah, AR_PHY(25)) >> 19) & 0x1ff;
	if (nf & 0x100)
		nf = 0 - ((nf ^ 0x1ff) + 1);
	return nf;
}

/*
 * Peform the noisefloor calibration for the length of time set
 * in runTime (valid values 1 to 7)
 *
 * Returns: The NF value at the end of the given time (or 0 for failure)
 */
int16_t
ar5211RunNoiseFloor(struct ath_hal *ah, uint8_t runTime, int16_t startingNF)
{
	int i, searchTime;

	HALASSERT(runTime <= 7);

	/* Setup  noise floor run time and starting value */
	OS_REG_WRITE(ah, AR_PHY(25),
		(OS_REG_READ(ah, AR_PHY(25)) & ~0xFFF) |
			 ((runTime << 9) & 0xE00) | (startingNF & 0x1FF));
	/* Calibrate the noise floor */
	OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
		OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_NF);

	/* Compute the required amount of searchTime needed to finish NF */
	if (runTime == 0) {
		/* 8 search windows * 6.4us each */
		searchTime = 8  * 7;
	} else {
		/* 512 * runtime search windows * 6.4us each */
		searchTime = (runTime * 512)  * 7;
	}

	/*
	 * Do not read noise floor until it has been updated
	 *
	 * As a guesstimate - we may only get 1/60th the time on
	 * the air to see search windows  in a heavily congested
	 * network (40 us every 2400 us of time)
	 */
	for (i = 0; i < 60; i++) {
		if ((OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0)
			break;
		OS_DELAY(searchTime);
	}
	if (i >= 60) {
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "NF with runTime %d failed to end on channel %d\n",
		    runTime, AH_PRIVATE(ah)->ah_curchan->ic_freq);
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "  PHY NF Reg state:	 0x%x\n",
		    OS_REG_READ(ah, AR_PHY_AGC_CONTROL));
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "  PHY Active Reg state: 0x%x\n",
		    OS_REG_READ(ah, AR_PHY_ACTIVE));
		return 0;
	}

	return ar5211GetNoiseFloor(ah);
}

static HAL_BOOL
getNoiseFloorThresh(struct ath_hal *ah, const struct ieee80211_channel *chan,
	int16_t *nft)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		*nft = ee->ee_noiseFloorThresh[0];
		break;
	case IEEE80211_CHAN_B:
		*nft = ee->ee_noiseFloorThresh[1];
		break;
	case IEEE80211_CHAN_PUREG:
		*nft = ee->ee_noiseFloorThresh[2];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}
	return AH_TRUE;
}

/*
 * Read the NF and check it against the noise floor threshold
 *
 * Returns: TRUE if the NF is good
 */
static HAL_BOOL
ar5211IsNfGood(struct ath_hal *ah, struct ieee80211_channel *chan)
{
	HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
	int16_t nf, nfThresh;

	if (!getNoiseFloorThresh(ah, chan, &nfThresh))
		return AH_FALSE;
	if (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: NF did not complete in calibration window\n", __func__);
	}
	nf = ar5211GetNoiseFloor(ah);
	if (nf > nfThresh) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: noise floor failed; detected %u, threshold %u\n",
		    __func__, nf, nfThresh);
		/*
		 * NB: Don't discriminate 2.4 vs 5Ghz, if this
		 *     happens it indicates a problem regardless
		 *     of the band.
		 */
		chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
	}
	ichan->rawNoiseFloor = nf;
	return (nf <= nfThresh);
}

/*
 * Peform the noisefloor calibration and check for any constant channel
 * interference.
 *
 * NOTE: preAR5211 have a lengthy carrier wave detection process - hence
 * it is if'ed for MKK regulatory domain only.
 *
 * Returns: TRUE for a successful noise floor calibration; else FALSE
 */
HAL_BOOL
ar5211CalNoiseFloor(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
	/* Check for Carrier Wave interference in MKK regulatory zone */
	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_OAHU &&
	    (chan->ic_flags & CHANNEL_NFCREQUIRED)) {
		static const uint8_t runtime[3] = { 0, 2, 7 };
		HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
		int16_t nf, nfThresh;
		int i;

		if (!getNoiseFloorThresh(ah, chan, &nfThresh))
			return AH_FALSE;
		/*
		 * Run a quick noise floor that will hopefully
		 * complete (decrease delay time).
		 */
		for (i = 0; i < N(runtime); i++) {
			nf = ar5211RunNoiseFloor(ah, runtime[i], 0);
			if (nf > nfThresh) {
				HALDEBUG(ah, HAL_DEBUG_ANY,
				    "%s: run failed with %u > threshold %u "
				    "(runtime %u)\n", __func__,
				    nf, nfThresh, runtime[i]);
				ichan->rawNoiseFloor = 0;
			} else
				ichan->rawNoiseFloor = nf;
		}
		return (i <= N(runtime));
	} else {
		/* Calibrate the noise floor */
		OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
			OS_REG_READ(ah, AR_PHY_AGC_CONTROL) |
				 AR_PHY_AGC_CONTROL_NF);
	}
	return AH_TRUE;
#undef N
}

/*
 * Adjust NF based on statistical values for 5GHz frequencies.
 */
int16_t
ar5211GetNfAdjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
	static const struct {
		uint16_t freqLow;
		int16_t	  adjust;
	} adjust5111[] = {
		{ 5790,	11 },	/* NB: ordered high -> low */
		{ 5730, 10 },
		{ 5690,  9 },
		{ 5660,  8 },
		{ 5610,  7 },
		{ 5530,  5 },
		{ 5450,  4 },
		{ 5379,  2 },
		{ 5209,  0 },	/* XXX? bogus but doesn't matter */
		{    0,  1 },
	};
	int i;

	for (i = 0; c->channel <= adjust5111[i].freqLow; i++)
		;
	/* NB: placeholder for 5111's less severe requirement */
	return adjust5111[i].adjust / 3;
}

/*
 * Reads EEPROM header info from device structure and programs
 * analog registers 6 and 7
 *
 * REQUIRES: Access to the analog device
 */
static HAL_BOOL
ar5211SetRf6and7(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
#define	N(a)	(sizeof (a) / sizeof (a[0]))
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ath_hal_5211 *ahp = AH5211(ah);
	uint16_t rfXpdGain, rfPloSel, rfPwdXpd;
	uint16_t tempOB, tempDB;
	uint16_t freqIndex;
	int i;

	freqIndex = IEEE80211_IS_CHAN_2GHZ(chan) ? 2 : 1;

	/*
	 * TODO: This array mode correspondes with the index used
	 *	 during the read.
	 * For readability, this should be changed to an enum or #define
	 */
	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		if (freq > 4000 && freq < 5260) {
			tempOB = ee->ee_ob1;
			tempDB = ee->ee_db1;
		} else if (freq >= 5260 && freq < 5500) {
			tempOB = ee->ee_ob2;
			tempDB = ee->ee_db2;
		} else if (freq >= 5500 && freq < 5725) {
			tempOB = ee->ee_ob3;
			tempDB = ee->ee_db3;
		} else if (freq >= 5725) {
			tempOB = ee->ee_ob4;
			tempDB = ee->ee_db4;
		} else {
			/* XXX panic?? */
			tempOB = tempDB = 0;
		}

		rfXpdGain = ee->ee_xgain[0];
		rfPloSel  = ee->ee_xpd[0];
		rfPwdXpd  = !ee->ee_xpd[0];

		ar5211Rf6n7[5][freqIndex]  =
			(ar5211Rf6n7[5][freqIndex] & ~0x10000000) |
				(ee->ee_cornerCal.pd84<< 28);
		ar5211Rf6n7[6][freqIndex]  =
			(ar5211Rf6n7[6][freqIndex] & ~0x04000000) |
				(ee->ee_cornerCal.pd90 << 26);
		ar5211Rf6n7[21][freqIndex] =
			(ar5211Rf6n7[21][freqIndex] & ~0x08) |
				(ee->ee_cornerCal.gSel << 3);
		break;
	case IEEE80211_CHAN_B:
		tempOB = ee->ee_obFor24;
		tempDB = ee->ee_dbFor24;
		rfXpdGain = ee->ee_xgain[1];
		rfPloSel  = ee->ee_xpd[1];
		rfPwdXpd  = !ee->ee_xpd[1];
		break;
	case IEEE80211_CHAN_PUREG:
		tempOB = ee->ee_obFor24g;
		tempDB = ee->ee_dbFor24g;
		rfXpdGain = ee->ee_xgain[2];
		rfPloSel  = ee->ee_xpd[2];
		rfPwdXpd  = !ee->ee_xpd[2];
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	HALASSERT(1 <= tempOB && tempOB <= 5);
	HALASSERT(1 <= tempDB && tempDB <= 5);

	/* Set rfXpdGain and rfPwdXpd */
	ar5211Rf6n7[11][freqIndex] =  (ar5211Rf6n7[11][freqIndex] & ~0xC0) |
		(((ath_hal_reverseBits(rfXpdGain, 4) << 7) | (rfPwdXpd << 6)) & 0xC0);
	ar5211Rf6n7[12][freqIndex] =  (ar5211Rf6n7[12][freqIndex] & ~0x07) |
		((ath_hal_reverseBits(rfXpdGain, 4) >> 1) & 0x07);

	/* Set OB */
	ar5211Rf6n7[12][freqIndex] =  (ar5211Rf6n7[12][freqIndex] & ~0x80) |
		((ath_hal_reverseBits(tempOB, 3) << 7) & 0x80);
	ar5211Rf6n7[13][freqIndex] =  (ar5211Rf6n7[13][freqIndex] & ~0x03) |
		((ath_hal_reverseBits(tempOB, 3) >> 1) & 0x03);

	/* Set DB */
	ar5211Rf6n7[13][freqIndex] =  (ar5211Rf6n7[13][freqIndex] & ~0x1C) |
		((ath_hal_reverseBits(tempDB, 3) << 2) & 0x1C);

	/* Set rfPloSel */
	ar5211Rf6n7[17][freqIndex] =  (ar5211Rf6n7[17][freqIndex] & ~0x08) |
		((rfPloSel << 3) & 0x08);

	/* Write the Rf registers 6 & 7 */
	for (i = 0; i < N(ar5211Rf6n7); i++)
		OS_REG_WRITE(ah, ar5211Rf6n7[i][0], ar5211Rf6n7[i][freqIndex]);

	/* Now that we have reprogrammed rfgain value, clear the flag. */
	ahp->ah_rfgainState = RFGAIN_INACTIVE;

	return AH_TRUE;
#undef N
}

HAL_BOOL
ar5211SetAntennaSwitchInternal(struct ath_hal *ah, HAL_ANT_SETTING settings,
	const struct ieee80211_channel *chan)
{
#define	ANT_SWITCH_TABLE1	0x9960
#define	ANT_SWITCH_TABLE2	0x9964
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ath_hal_5211 *ahp = AH5211(ah);
	uint32_t antSwitchA, antSwitchB;
	int ix;

	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:		ix = 0; break;
	case IEEE80211_CHAN_B:		ix = 1; break;
	case IEEE80211_CHAN_PUREG:	ix = 2; break;
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
	ahp->ah_diversityControl = settings;

	OS_REG_WRITE(ah, ANT_SWITCH_TABLE1, antSwitchA);
	OS_REG_WRITE(ah, ANT_SWITCH_TABLE2, antSwitchB);

	return AH_TRUE;
#undef ANT_SWITCH_TABLE1
#undef ANT_SWITCH_TABLE2
}

/*
 * Reads EEPROM header info and programs the device for correct operation
 * given the channel value
 */
static HAL_BOOL
ar5211SetBoardValues(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ath_hal_5211 *ahp = AH5211(ah);
	int arrayMode, falseDectectBackoff;

	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		arrayMode = 0;
		OS_REG_RMW_FIELD(ah, AR_PHY_FRAME_CTL,
			AR_PHY_FRAME_CTL_TX_CLIP, ee->ee_cornerCal.clip);
		break;
	case IEEE80211_CHAN_B:
		arrayMode = 1;
		break;
	case IEEE80211_CHAN_PUREG:
		arrayMode = 2;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	/* Set the antenna register(s) correctly for the chip revision */
	if (AH_PRIVATE(ah)->ah_macVersion < AR_SREV_VERSION_OAHU) {
		OS_REG_WRITE(ah, AR_PHY(68),
			(OS_REG_READ(ah, AR_PHY(68)) & 0xFFFFFFFC) | 0x3);
	} else {
		OS_REG_WRITE(ah, AR_PHY(68),
			(OS_REG_READ(ah, AR_PHY(68)) & 0xFFFFFC06) |
			(ee->ee_antennaControl[0][arrayMode] << 4) | 0x1);

		ar5211SetAntennaSwitchInternal(ah,
			ahp->ah_diversityControl, chan);

		/* Set the Noise Floor Thresh on ar5211 devices */
		OS_REG_WRITE(ah, AR_PHY_BASE + (90 << 2),
			(ee->ee_noiseFloorThresh[arrayMode] & 0x1FF) | (1<<9));
	}
	OS_REG_WRITE(ah, AR_PHY_BASE + (17 << 2),
		(OS_REG_READ(ah, AR_PHY_BASE + (17 << 2)) & 0xFFFFC07F) |
		((ee->ee_switchSettling[arrayMode] << 7) & 0x3F80));
	OS_REG_WRITE(ah, AR_PHY_BASE + (18 << 2),
		(OS_REG_READ(ah, AR_PHY_BASE + (18 << 2)) & 0xFFFC0FFF) |
		((ee->ee_txrxAtten[arrayMode] << 12) & 0x3F000));
	OS_REG_WRITE(ah, AR_PHY_BASE + (20 << 2),
		(OS_REG_READ(ah, AR_PHY_BASE + (20 << 2)) & 0xFFFF0000) |
		((ee->ee_pgaDesiredSize[arrayMode] << 8) & 0xFF00) |
		(ee->ee_adcDesiredSize[arrayMode] & 0x00FF));
	OS_REG_WRITE(ah, AR_PHY_BASE + (13 << 2),
		(ee->ee_txEndToXPAOff[arrayMode] << 24) |
		(ee->ee_txEndToXPAOff[arrayMode] << 16) |
		(ee->ee_txFrameToXPAOn[arrayMode] << 8) |
		ee->ee_txFrameToXPAOn[arrayMode]);
	OS_REG_WRITE(ah, AR_PHY_BASE + (10 << 2),
		(OS_REG_READ(ah, AR_PHY_BASE + (10 << 2)) & 0xFFFF00FF) |
		(ee->ee_txEndToXLNAOn[arrayMode] << 8));
	OS_REG_WRITE(ah, AR_PHY_BASE + (25 << 2),
		(OS_REG_READ(ah, AR_PHY_BASE + (25 << 2)) & 0xFFF80FFF) |
		((ee->ee_thresh62[arrayMode] << 12) & 0x7F000));

#define NO_FALSE_DETECT_BACKOFF   2
#define CB22_FALSE_DETECT_BACKOFF 6
	/*
	 * False detect backoff - suspected 32 MHz spur causes
	 * false detects in OFDM, causing Tx Hangs.  Decrease
	 * weak signal sensitivity for this card.
	 */
	falseDectectBackoff = NO_FALSE_DETECT_BACKOFF;
	if (AH_PRIVATE(ah)->ah_eeversion < AR_EEPROM_VER3_3) {
		if (AH_PRIVATE(ah)->ah_subvendorid == 0x1022 &&
		    IEEE80211_IS_CHAN_OFDM(chan))
			falseDectectBackoff += CB22_FALSE_DETECT_BACKOFF;
	} else {
		uint16_t freq = ath_hal_gethwchannel(ah, chan);
		uint32_t remainder = freq % 32;

		if (remainder && (remainder < 10 || remainder > 22))
			falseDectectBackoff += ee->ee_falseDetectBackoff[arrayMode];
	}
	OS_REG_WRITE(ah, 0x9924,
		(OS_REG_READ(ah, 0x9924) & 0xFFFFFF01)
		| ((falseDectectBackoff << 1) & 0xF7));

	return AH_TRUE;
#undef NO_FALSE_DETECT_BACKOFF
#undef CB22_FALSE_DETECT_BACKOFF
}

/*
 * Set the limit on the overall output power.  Used for dynamic
 * transmit power control and the like.
 *
 * NOTE: The power is passed in is in units of 0.5 dBm.
 */
HAL_BOOL
ar5211SetTxPowerLimit(struct ath_hal *ah, uint32_t limit)
{

	AH_PRIVATE(ah)->ah_powerLimit = AH_MIN(limit, MAX_RATE_POWER);
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE_MAX, limit);
	return AH_TRUE;
}

/*
 * Sets the transmit power in the baseband for the given
 * operating channel and mode.
 */
static HAL_BOOL
ar5211SetTransmitPower(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	TRGT_POWER_INFO *pi;
	RD_EDGES_POWER *rep;
	PCDACS_EEPROM eepromPcdacs;
	u_int nchan, cfgCtl;
	int i;

	/* setup the pcdac struct to point to the correct info, based on mode */
	switch (chan->ic_flags & IEEE80211_CHAN_ALLFULL) {
	case IEEE80211_CHAN_A:
		eepromPcdacs.numChannels = ee->ee_numChannels11a;
		eepromPcdacs.pChannelList= ee->ee_channels11a;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11a;
		nchan = ee->ee_numTargetPwr_11a;
		pi = ee->ee_trgtPwr_11a;
		break;
	case IEEE80211_CHAN_PUREG:
		eepromPcdacs.numChannels = ee->ee_numChannels2_4;
		eepromPcdacs.pChannelList= ee->ee_channels11g;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11g;
		nchan = ee->ee_numTargetPwr_11g;
		pi = ee->ee_trgtPwr_11g;
		break;
	case IEEE80211_CHAN_B:
		eepromPcdacs.numChannels = ee->ee_numChannels2_4;
		eepromPcdacs.pChannelList= ee->ee_channels11b;
		eepromPcdacs.pDataPerChannel = ee->ee_dataPerChannel11b;
		nchan = ee->ee_numTargetPwr_11b;
		pi = ee->ee_trgtPwr_11b;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid channel flags 0x%x\n",
		    __func__, chan->ic_flags);
		return AH_FALSE;
	}

	ar5211SetPowerTable(ah, &eepromPcdacs, freq);

	rep = AH_NULL;
	/* Match CTL to EEPROM value */
	cfgCtl = ath_hal_getctl(ah, chan);
	for (i = 0; i < ee->ee_numCtls; i++)
		if (ee->ee_ctl[i] != 0 && ee->ee_ctl[i] == cfgCtl) {
			rep = &ee->ee_rdEdgesPower[i * NUM_EDGES];
			break;
		}
	ar5211SetRateTable(ah, rep, pi, nchan, chan);

	return AH_TRUE;
}

/*
 * Read the transmit power levels from the structures taken
 * from EEPROM. Interpolate read transmit power values for
 * this channel. Organize the transmit power values into a
 * table for writing into the hardware.
 */
void
ar5211SetPowerTable(struct ath_hal *ah, PCDACS_EEPROM *pSrcStruct,
	uint16_t channel)
{
	static FULL_PCDAC_STRUCT pcdacStruct;
	static uint16_t pcdacTable[PWR_TABLE_SIZE];

	uint16_t	 i, j;
	uint16_t	 *pPcdacValues;
	int16_t	  *pScaledUpDbm;
	int16_t	  minScaledPwr;
	int16_t	  maxScaledPwr;
	int16_t	  pwr;
	uint16_t	 pcdacMin = 0;
	uint16_t	 pcdacMax = 63;
	uint16_t	 pcdacTableIndex;
	uint16_t	 scaledPcdac;
	uint32_t	 addr;
	uint32_t	 temp32;

	OS_MEMZERO(&pcdacStruct, sizeof(FULL_PCDAC_STRUCT));
	OS_MEMZERO(pcdacTable, sizeof(uint16_t) * PWR_TABLE_SIZE);
	pPcdacValues = pcdacStruct.PcdacValues;
	pScaledUpDbm = pcdacStruct.PwrValues;

	/* Initialize the pcdacs to dBM structs pcdacs to be 1 to 63 */
	for (i = PCDAC_START, j = 0; i <= PCDAC_STOP; i+= PCDAC_STEP, j++)
		pPcdacValues[j] = i;

	pcdacStruct.numPcdacValues = j;
	pcdacStruct.pcdacMin = PCDAC_START;
	pcdacStruct.pcdacMax = PCDAC_STOP;

	/* Fill out the power values for this channel */
	for (j = 0; j < pcdacStruct.numPcdacValues; j++ )
		pScaledUpDbm[j] = ar5211GetScaledPower(channel, pPcdacValues[j], pSrcStruct);

	/* Now scale the pcdac values to fit in the 64 entry power table */
	minScaledPwr = pScaledUpDbm[0];
	maxScaledPwr = pScaledUpDbm[pcdacStruct.numPcdacValues - 1];

	/* find minimum and make monotonic */
	for (j = 0; j < pcdacStruct.numPcdacValues; j++) {
		if (minScaledPwr >= pScaledUpDbm[j]) {
			minScaledPwr = pScaledUpDbm[j];
			pcdacMin = j;
		}
		/*
		 * Make the full_hsh monotonically increasing otherwise
		 * interpolation algorithm will get fooled gotta start
		 * working from the top, hence i = 63 - j.
		 */
		i = (uint16_t)(pcdacStruct.numPcdacValues - 1 - j);
		if (i == 0)
			break;
		if (pScaledUpDbm[i-1] > pScaledUpDbm[i]) {
			/*
			 * It could be a glitch, so make the power for
			 * this pcdac the same as the power from the
			 * next highest pcdac.
			 */
			pScaledUpDbm[i - 1] = pScaledUpDbm[i];
		}
	}

	for (j = 0; j < pcdacStruct.numPcdacValues; j++)
		if (maxScaledPwr < pScaledUpDbm[j]) {
			maxScaledPwr = pScaledUpDbm[j];
			pcdacMax = j;
		}

	/* Find the first power level with a pcdac */
	pwr = (uint16_t)(PWR_STEP * ((minScaledPwr - PWR_MIN + PWR_STEP / 2) / PWR_STEP)  + PWR_MIN);

	/* Write all the first pcdac entries based off the pcdacMin */
	pcdacTableIndex = 0;
	for (i = 0; i < (2 * (pwr - PWR_MIN) / EEP_SCALE + 1); i++)
		pcdacTable[pcdacTableIndex++] = pcdacMin;

	i = 0;
	while (pwr < pScaledUpDbm[pcdacStruct.numPcdacValues - 1]) {
		pwr += PWR_STEP;
		/* stop if dbM > max_power_possible */
		while (pwr < pScaledUpDbm[pcdacStruct.numPcdacValues - 1] &&
		       (pwr - pScaledUpDbm[i])*(pwr - pScaledUpDbm[i+1]) > 0)
			i++;
		/* scale by 2 and add 1 to enable round up or down as needed */
		scaledPcdac = (uint16_t)(ar5211GetInterpolatedValue(pwr,
				pScaledUpDbm[i], pScaledUpDbm[i+1],
				(uint16_t)(pPcdacValues[i] * 2),
				(uint16_t)(pPcdacValues[i+1] * 2), 0) + 1);

		pcdacTable[pcdacTableIndex] = scaledPcdac / 2;
		if (pcdacTable[pcdacTableIndex] > pcdacMax)
			pcdacTable[pcdacTableIndex] = pcdacMax;
		pcdacTableIndex++;
	}

	/* Write all the last pcdac entries based off the last valid pcdac */
	while (pcdacTableIndex < PWR_TABLE_SIZE) {
		pcdacTable[pcdacTableIndex] = pcdacTable[pcdacTableIndex - 1];
		pcdacTableIndex++;
	}

	/* Finally, write the power values into the baseband power table */
	addr = AR_PHY_BASE + (608 << 2);
	for (i = 0; i < 32; i++) {
		temp32 = 0xffff & ((pcdacTable[2 * i + 1] << 8) | 0xff);
		temp32 = (temp32 << 16) | (0xffff & ((pcdacTable[2 * i] << 8) | 0xff));
		OS_REG_WRITE(ah, addr, temp32);
		addr += 4;
	}

}

/*
 * Set the transmit power in the baseband for the given
 * operating channel and mode.
 */
static void
ar5211SetRateTable(struct ath_hal *ah, RD_EDGES_POWER *pRdEdgesPower,
	TRGT_POWER_INFO *pPowerInfo, uint16_t numChannels,
	const struct ieee80211_channel *chan)
{
	uint16_t freq = ath_hal_gethwchannel(ah, chan);
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;
	struct ath_hal_5211 *ahp = AH5211(ah);
	static uint16_t ratesArray[NUM_RATES];
	static const uint16_t tpcScaleReductionTable[5] =
		{ 0, 3, 6, 9, MAX_RATE_POWER };

	uint16_t	*pRatesPower;
	uint16_t	lowerChannel, lowerIndex=0, lowerPower=0;
	uint16_t	upperChannel, upperIndex=0, upperPower=0;
	uint16_t	twiceMaxEdgePower=63;
	uint16_t	twicePower = 0;
	uint16_t	i, numEdges;
	uint16_t	tempChannelList[NUM_EDGES]; /* temp array for holding edge channels */
	uint16_t	twiceMaxRDPower;
	int16_t	 scaledPower = 0;		/* for gcc -O2 */
	uint16_t	mask = 0x3f;
	HAL_BOOL	  paPreDEnable = 0;
	int8_t	  twiceAntennaGain, twiceAntennaReduction = 0;

	pRatesPower = ratesArray;
	twiceMaxRDPower = chan->ic_maxregpower * 2;

	if (IEEE80211_IS_CHAN_5GHZ(chan)) {
		twiceAntennaGain = ee->ee_antennaGainMax[0];
	} else {
		twiceAntennaGain = ee->ee_antennaGainMax[1];
	}

	twiceAntennaReduction = ath_hal_getantennareduction(ah, chan, twiceAntennaGain);

	if (pRdEdgesPower) {
		/* Get the edge power */
		for (i = 0; i < NUM_EDGES; i++) {
			if (pRdEdgesPower[i].rdEdge == 0)
				break;
			tempChannelList[i] = pRdEdgesPower[i].rdEdge;
		}
		numEdges = i;

		ar5211GetLowerUpperValues(freq, tempChannelList,
			numEdges, &lowerChannel, &upperChannel);
		/* Get the index for this channel */
		for (i = 0; i < numEdges; i++)
			if (lowerChannel == tempChannelList[i])
				break;
		HALASSERT(i != numEdges);

		if ((lowerChannel == upperChannel &&
		     lowerChannel == freq) ||
		    pRdEdgesPower[i].flag) {
			twiceMaxEdgePower = pRdEdgesPower[i].twice_rdEdgePower;
			HALASSERT(twiceMaxEdgePower > 0);
		}
	}

	/* extrapolate the power values for the test Groups */
	for (i = 0; i < numChannels; i++)
		tempChannelList[i] = pPowerInfo[i].testChannel;

	ar5211GetLowerUpperValues(freq, tempChannelList,
		numChannels, &lowerChannel, &upperChannel);

	/* get the index for the channel */
	for (i = 0; i < numChannels; i++) {
		if (lowerChannel == tempChannelList[i])
			lowerIndex = i;
		if (upperChannel == tempChannelList[i]) {
			upperIndex = i;
			break;
		}
	}

	for (i = 0; i < NUM_RATES; i++) {
		if (IEEE80211_IS_CHAN_OFDM(chan)) {
			/* power for rates 6,9,12,18,24 is all the same */
			if (i < 5) {
				lowerPower = pPowerInfo[lowerIndex].twicePwr6_24;
				upperPower = pPowerInfo[upperIndex].twicePwr6_24;
			} else if (i == 5) {
				lowerPower = pPowerInfo[lowerIndex].twicePwr36;
				upperPower = pPowerInfo[upperIndex].twicePwr36;
			} else if (i == 6) {
				lowerPower = pPowerInfo[lowerIndex].twicePwr48;
				upperPower = pPowerInfo[upperIndex].twicePwr48;
			} else if (i == 7) {
				lowerPower = pPowerInfo[lowerIndex].twicePwr54;
				upperPower = pPowerInfo[upperIndex].twicePwr54;
			}
		} else {
			switch (i) {
			case 0:
			case 1:
				lowerPower = pPowerInfo[lowerIndex].twicePwr6_24;
				upperPower = pPowerInfo[upperIndex].twicePwr6_24;
				break;
			case 2:
			case 3:
				lowerPower = pPowerInfo[lowerIndex].twicePwr36;
				upperPower = pPowerInfo[upperIndex].twicePwr36;
				break;
			case 4:
			case 5:
				lowerPower = pPowerInfo[lowerIndex].twicePwr48;
				upperPower = pPowerInfo[upperIndex].twicePwr48;
				break;
			case 6:
			case 7:
				lowerPower = pPowerInfo[lowerIndex].twicePwr54;
				upperPower = pPowerInfo[upperIndex].twicePwr54;
				break;
			}
		}

		twicePower = ar5211GetInterpolatedValue(freq,
			lowerChannel, upperChannel, lowerPower, upperPower, 0);

		/* Reduce power by band edge restrictions */
		twicePower = AH_MIN(twicePower, twiceMaxEdgePower);

		/*
		 * If turbo is set, reduce power to keep power
		 * consumption under 2 Watts.  Note that we always do
		 * this unless specially configured.  Then we limit
		 * power only for non-AP operation.
		 */
		if (IEEE80211_IS_CHAN_TURBO(chan) &&
		    AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER3_1
#ifdef AH_ENABLE_AP_SUPPORT
		    && AH_PRIVATE(ah)->ah_opmode != HAL_M_HOSTAP
#endif
		) {
			twicePower = AH_MIN(twicePower, ee->ee_turbo2WMaxPower5);
		}

		/* Reduce power by max regulatory domain allowed restrictions */
		pRatesPower[i] = AH_MIN(twicePower, twiceMaxRDPower - twiceAntennaReduction);

		/* Use 6 Mb power level for transmit power scaling reduction */
		/* We don't want to reduce higher rates if its not needed */
		if (i == 0) {
			scaledPower = pRatesPower[0] -
				(tpcScaleReductionTable[AH_PRIVATE(ah)->ah_tpScale] * 2);
			if (scaledPower < 1)
				scaledPower = 1;
		}

		pRatesPower[i] = AH_MIN(pRatesPower[i], scaledPower);
	}

	/* Record txPower at Rate 6 for info gathering */
	ahp->ah_tx6PowerInHalfDbm = pRatesPower[0];

#ifdef AH_DEBUG
	HALDEBUG(ah, HAL_DEBUG_RESET,
	    "%s: final output power setting %d MHz:\n",
	    __func__, chan->ic_freq);
	HALDEBUG(ah, HAL_DEBUG_RESET,
	    "6 Mb %d dBm, MaxRD: %d dBm, MaxEdge %d dBm\n",
	    scaledPower / 2, twiceMaxRDPower / 2, twiceMaxEdgePower / 2);
	HALDEBUG(ah, HAL_DEBUG_RESET, "TPC Scale %d dBm - Ant Red %d dBm\n",
	    tpcScaleReductionTable[AH_PRIVATE(ah)->ah_tpScale] * 2,
	    twiceAntennaReduction / 2);
	if (IEEE80211_IS_CHAN_TURBO(chan) &&
	    AH_PRIVATE(ah)->ah_eeversion >= AR_EEPROM_VER3_1)
		HALDEBUG(ah, HAL_DEBUG_RESET, "Max Turbo %d dBm\n",
		    ee->ee_turbo2WMaxPower5);
	HALDEBUG(ah, HAL_DEBUG_RESET,
	    "  %2d | %2d | %2d | %2d | %2d | %2d | %2d | %2d dBm\n",
	    pRatesPower[0] / 2, pRatesPower[1] / 2, pRatesPower[2] / 2,
	    pRatesPower[3] / 2, pRatesPower[4] / 2, pRatesPower[5] / 2,
	    pRatesPower[6] / 2, pRatesPower[7] / 2);
#endif /* AH_DEBUG */

	/* Write the power table into the hardware */
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE1,
		 ((paPreDEnable & 1)<< 30) | ((pRatesPower[3] & mask) << 24) |
		 ((paPreDEnable & 1)<< 22) | ((pRatesPower[2] & mask) << 16) |
		 ((paPreDEnable & 1)<< 14) | ((pRatesPower[1] & mask) << 8) |
		 ((paPreDEnable & 1)<< 6 ) |  (pRatesPower[0] & mask));
	OS_REG_WRITE(ah, AR_PHY_POWER_TX_RATE2,
		 ((paPreDEnable & 1)<< 30) | ((pRatesPower[7] & mask) << 24) |
		 ((paPreDEnable & 1)<< 22) | ((pRatesPower[6] & mask) << 16) |
		 ((paPreDEnable & 1)<< 14) | ((pRatesPower[5] & mask) << 8) |
		 ((paPreDEnable & 1)<< 6 ) |  (pRatesPower[4] & mask));

	/* set max power to the power value at rate 6 */
	ar5211SetTxPowerLimit(ah, pRatesPower[0]);

	AH_PRIVATE(ah)->ah_maxPowerLevel = pRatesPower[0];
}

/*
 * Get or interpolate the pcdac value from the calibrated data
 */
uint16_t
ar5211GetScaledPower(uint16_t channel, uint16_t pcdacValue,
	const PCDACS_EEPROM *pSrcStruct)
{
	uint16_t powerValue;
	uint16_t lFreq, rFreq;		/* left and right frequency values */
	uint16_t llPcdac, ulPcdac;	/* lower and upper left pcdac values */
	uint16_t lrPcdac, urPcdac;	/* lower and upper right pcdac values */
	uint16_t lPwr, uPwr;		/* lower and upper temp pwr values */
	uint16_t lScaledPwr, rScaledPwr; /* left and right scaled power */

	if (ar5211FindValueInList(channel, pcdacValue, pSrcStruct, &powerValue))
		/* value was copied from srcStruct */
		return powerValue;

	ar5211GetLowerUpperValues(channel, pSrcStruct->pChannelList,
		pSrcStruct->numChannels, &lFreq, &rFreq);
	ar5211GetLowerUpperPcdacs(pcdacValue, lFreq, pSrcStruct,
		&llPcdac, &ulPcdac);
	ar5211GetLowerUpperPcdacs(pcdacValue, rFreq, pSrcStruct,
		&lrPcdac, &urPcdac);

	/* get the power index for the pcdac value */
	ar5211FindValueInList(lFreq, llPcdac, pSrcStruct, &lPwr);
	ar5211FindValueInList(lFreq, ulPcdac, pSrcStruct, &uPwr);
	lScaledPwr = ar5211GetInterpolatedValue(pcdacValue,
				llPcdac, ulPcdac, lPwr, uPwr, 0);

	ar5211FindValueInList(rFreq, lrPcdac, pSrcStruct, &lPwr);
	ar5211FindValueInList(rFreq, urPcdac, pSrcStruct, &uPwr);
	rScaledPwr = ar5211GetInterpolatedValue(pcdacValue,
				lrPcdac, urPcdac, lPwr, uPwr, 0);

	return ar5211GetInterpolatedValue(channel, lFreq, rFreq,
		lScaledPwr, rScaledPwr, 0);
}

/*
 * Find the value from the calibrated source data struct
 */
HAL_BOOL
ar5211FindValueInList(uint16_t channel, uint16_t pcdacValue,
	const PCDACS_EEPROM *pSrcStruct, uint16_t *powerValue)
{
	const DATA_PER_CHANNEL *pChannelData;
	const uint16_t *pPcdac;
	uint16_t i, j;

	pChannelData = pSrcStruct->pDataPerChannel;
	for (i = 0; i < pSrcStruct->numChannels; i++ ) {
		if (pChannelData->channelValue == channel) {
			pPcdac = pChannelData->PcdacValues;
			for (j = 0; j < pChannelData->numPcdacValues; j++ ) {
				if (*pPcdac == pcdacValue) {
					*powerValue = pChannelData->PwrValues[j];
					return AH_TRUE;
				}
				pPcdac++;
			}
		}
		pChannelData++;
	}
	return AH_FALSE;
}

/*
 * Returns interpolated or the scaled up interpolated value
 */
uint16_t
ar5211GetInterpolatedValue(uint16_t target,
	uint16_t srcLeft, uint16_t srcRight,
	uint16_t targetLeft, uint16_t targetRight,
	HAL_BOOL scaleUp)
{
	uint16_t rv;
	int16_t lRatio;
	uint16_t scaleValue = EEP_SCALE;

	/* to get an accurate ratio, always scale, if want to scale, then don't scale back down */
	if ((targetLeft * targetRight) == 0)
		return 0;
	if (scaleUp)
		scaleValue = 1;

	if (srcRight != srcLeft) {
		/*
		 * Note the ratio always need to be scaled,
		 * since it will be a fraction.
		 */
		lRatio = (target - srcLeft) * EEP_SCALE / (srcRight - srcLeft);
		if (lRatio < 0) {
		    /* Return as Left target if value would be negative */
		    rv = targetLeft * (scaleUp ? EEP_SCALE : 1);
		} else if (lRatio > EEP_SCALE) {
		    /* Return as Right target if Ratio is greater than 100% (SCALE) */
		    rv = targetRight * (scaleUp ? EEP_SCALE : 1);
		} else {
			rv = (lRatio * targetRight + (EEP_SCALE - lRatio) *
					targetLeft) / scaleValue;
		}
	} else {
		rv = targetLeft;
		if (scaleUp)
			rv *= EEP_SCALE;
	}
	return rv;
}

/*
 *  Look for value being within 0.1 of the search values
 *  however, NDIS can't do float calculations, so multiply everything
 *  up by EEP_SCALE so can do integer arithmatic
 *
 * INPUT  value	   -value to search for
 * INPUT  pList	   -ptr to the list to search
 * INPUT  listSize	-number of entries in list
 * OUTPUT pLowerValue -return the lower value
 * OUTPUT pUpperValue -return the upper value
 */
void
ar5211GetLowerUpperValues(uint16_t value,
	const uint16_t *pList, uint16_t listSize,
	uint16_t *pLowerValue, uint16_t *pUpperValue)
{
	const uint16_t listEndValue = *(pList + listSize - 1);
	uint32_t target = value * EEP_SCALE;
	int i;

	/*
	 * See if value is lower than the first value in the list
	 * if so return first value
	 */
	if (target < (uint32_t)(*pList * EEP_SCALE - EEP_DELTA)) {
		*pLowerValue = *pList;
		*pUpperValue = *pList;
		return;
	}

	/*
	 * See if value is greater than last value in list
	 * if so return last value
	 */
	if (target > (uint32_t)(listEndValue * EEP_SCALE + EEP_DELTA)) {
		*pLowerValue = listEndValue;
		*pUpperValue = listEndValue;
		return;
	}

	/* look for value being near or between 2 values in list */
	for (i = 0; i < listSize; i++) {
		/*
		 * If value is close to the current value of the list
		 * then target is not between values, it is one of the values
		 */
		if (abs(pList[i] * EEP_SCALE - (int32_t) target) < EEP_DELTA) {
			*pLowerValue = pList[i];
			*pUpperValue = pList[i];
			return;
		}

		/*
		 * Look for value being between current value and next value
		 * if so return these 2 values
		 */
		if (target < (uint32_t)(pList[i + 1] * EEP_SCALE - EEP_DELTA)) {
			*pLowerValue = pList[i];
			*pUpperValue = pList[i + 1];
			return;
		}
	}
}

/*
 * Get the upper and lower pcdac given the channel and the pcdac
 * used in the search
 */
void
ar5211GetLowerUpperPcdacs(uint16_t pcdac, uint16_t channel,
	const PCDACS_EEPROM *pSrcStruct,
	uint16_t *pLowerPcdac, uint16_t *pUpperPcdac)
{
	const DATA_PER_CHANNEL *pChannelData;
	int i;

	/* Find the channel information */
	pChannelData = pSrcStruct->pDataPerChannel;
	for (i = 0; i < pSrcStruct->numChannels; i++) {
		if (pChannelData->channelValue == channel)
			break;
		pChannelData++;
	}
	ar5211GetLowerUpperValues(pcdac, pChannelData->PcdacValues,
		pChannelData->numPcdacValues, pLowerPcdac, pUpperPcdac);
}

#define	DYN_ADJ_UP_MARGIN	15
#define	DYN_ADJ_LO_MARGIN	20

static const GAIN_OPTIMIZATION_LADDER gainLadder = {
	9,					/* numStepsInLadder */
	4,					/* defaultStepNum */
	{ { {4, 1, 1, 1},  6, "FG8"},
	  { {4, 0, 1, 1},  4, "FG7"},
	  { {3, 1, 1, 1},  3, "FG6"},
	  { {4, 0, 0, 1},  1, "FG5"},
	  { {4, 1, 1, 0},  0, "FG4"},	/* noJack */
	  { {4, 0, 1, 0}, -2, "FG3"},	/* halfJack */
	  { {3, 1, 1, 0}, -3, "FG2"},	/* clip3 */
	  { {4, 0, 0, 0}, -4, "FG1"},	/* noJack */
	  { {2, 1, 1, 0}, -6, "FG0"} 	/* clip2 */
	}
};

/*
 * Initialize the gain structure to good values
 */
void
ar5211InitializeGainValues(struct ath_hal *ah)
{
	struct ath_hal_5211 *ahp = AH5211(ah);
	GAIN_VALUES *gv = &ahp->ah_gainValues;

	/* initialize gain optimization values */
	gv->currStepNum = gainLadder.defaultStepNum;
	gv->currStep = &gainLadder.optStep[gainLadder.defaultStepNum];
	gv->active = AH_TRUE;
	gv->loTrig = 20;
	gv->hiTrig = 35;
}

static HAL_BOOL
ar5211InvalidGainReadback(struct ath_hal *ah, GAIN_VALUES *gv)
{
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	uint32_t gStep, g;
	uint32_t L1, L2, L3, L4;

	if (IEEE80211_IS_CHAN_CCK(chan)) {
		gStep = 0x18;
		L1 = 0;
		L2 = gStep + 4;
		L3 = 0x40;
		L4 = L3 + 50;

		gv->loTrig = L1;
		gv->hiTrig = L4+5;
	} else {
		gStep = 0x3f;
		L1 = 0;
		L2 = 50;
		L3 = L1;
		L4 = L3 + 50;

		gv->loTrig = L1 + DYN_ADJ_LO_MARGIN;
		gv->hiTrig = L4 - DYN_ADJ_UP_MARGIN;
	}
	g = gv->currGain;

	return !((g >= L1 && g<= L2) || (g >= L3 && g <= L4));
}

/*
 * Enable the probe gain check on the next packet
 */
static void
ar5211RequestRfgain(struct ath_hal *ah)
{
	struct ath_hal_5211 *ahp = AH5211(ah);

	/* Enable the gain readback probe */
	OS_REG_WRITE(ah, AR_PHY_PAPD_PROBE,
		  SM(ahp->ah_tx6PowerInHalfDbm, AR_PHY_PAPD_PROBE_POWERTX)
		| AR_PHY_PAPD_PROBE_NEXT_TX);

	ahp->ah_rfgainState = HAL_RFGAIN_READ_REQUESTED;
}

/*
 * Exported call to check for a recent gain reading and return
 * the current state of the thermal calibration gain engine.
 */
HAL_RFGAIN
ar5211GetRfgain(struct ath_hal *ah)
{
	struct ath_hal_5211 *ahp = AH5211(ah);
	GAIN_VALUES *gv = &ahp->ah_gainValues;
	uint32_t rddata;

	if (!gv->active)
		return HAL_RFGAIN_INACTIVE;

	if (ahp->ah_rfgainState == HAL_RFGAIN_READ_REQUESTED) {
		/* Caller had asked to setup a new reading. Check it. */
		rddata = OS_REG_READ(ah, AR_PHY_PAPD_PROBE);

		if ((rddata & AR_PHY_PAPD_PROBE_NEXT_TX) == 0) {
			/* bit got cleared, we have a new reading. */
			gv->currGain = rddata >> AR_PHY_PAPD_PROBE_GAINF_S;
			/* inactive by default */
			ahp->ah_rfgainState = HAL_RFGAIN_INACTIVE;

			if (!ar5211InvalidGainReadback(ah, gv) &&
			    ar5211IsGainAdjustNeeded(ah, gv) &&
			    ar5211AdjustGain(ah, gv) > 0) {
				/*
				 * Change needed. Copy ladder info
				 * into eeprom info.
				 */
				ar5211SetRfgain(ah, gv);
				ahp->ah_rfgainState = HAL_RFGAIN_NEED_CHANGE;
			}
		}
	}
	return ahp->ah_rfgainState;
}

/*
 * Check to see if our readback gain level sits within the linear
 * region of our current variable attenuation window
 */
static HAL_BOOL
ar5211IsGainAdjustNeeded(struct ath_hal *ah, const GAIN_VALUES *gv)
{
	return (gv->currGain <= gv->loTrig || gv->currGain >= gv->hiTrig);
}

/*
 * Move the rabbit ears in the correct direction.
 */
static int32_t 
ar5211AdjustGain(struct ath_hal *ah, GAIN_VALUES *gv)
{
	/* return > 0 for valid adjustments. */
	if (!gv->active)
		return -1;

	gv->currStep = &gainLadder.optStep[gv->currStepNum];
	if (gv->currGain >= gv->hiTrig) {
		if (gv->currStepNum == 0) {
			HALDEBUG(ah, HAL_DEBUG_RFPARAM,
			    "%s: Max gain limit.\n", __func__);
			return -1;
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Adding gain: currG=%d [%s] --> ",
		    __func__, gv->currGain, gv->currStep->stepName);
		gv->targetGain = gv->currGain;
		while (gv->targetGain >= gv->hiTrig && gv->currStepNum > 0) {
			gv->targetGain -= 2 * (gainLadder.optStep[--(gv->currStepNum)].stepGain -
				gv->currStep->stepGain);
			gv->currStep = &gainLadder.optStep[gv->currStepNum];
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "targG=%d [%s]\n",
		    gv->targetGain, gv->currStep->stepName);
		return 1;
	}
	if (gv->currGain <= gv->loTrig) {
		if (gv->currStepNum == gainLadder.numStepsInLadder-1) {
			HALDEBUG(ah, HAL_DEBUG_RFPARAM,
			    "%s: Min gain limit.\n", __func__);
			return -2;
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM,
		    "%s: Deducting gain: currG=%d [%s] --> ",
		    __func__, gv->currGain, gv->currStep->stepName);
		gv->targetGain = gv->currGain;
		while (gv->targetGain <= gv->loTrig &&
		      gv->currStepNum < (gainLadder.numStepsInLadder - 1)) {
			gv->targetGain -= 2 *
				(gainLadder.optStep[++(gv->currStepNum)].stepGain - gv->currStep->stepGain);
			gv->currStep = &gainLadder.optStep[gv->currStepNum];
		}
		HALDEBUG(ah, HAL_DEBUG_RFPARAM, "targG=%d [%s]\n",
		    gv->targetGain, gv->currStep->stepName);
		return 2;
	}
	return 0;		/* caller didn't call needAdjGain first */
}

/*
 * Adjust the 5GHz EEPROM information with the desired calibration values.
 */
static void
ar5211SetRfgain(struct ath_hal *ah, const GAIN_VALUES *gv)
{
	HAL_EEPROM *ee = AH_PRIVATE(ah)->ah_eeprom;

	if (!gv->active)
		return;
	ee->ee_cornerCal.clip = gv->currStep->paramVal[0]; /* bb_tx_clip */
	ee->ee_cornerCal.pd90 = gv->currStep->paramVal[1]; /* rf_pwd_90 */
	ee->ee_cornerCal.pd84 = gv->currStep->paramVal[2]; /* rf_pwd_84 */
	ee->ee_cornerCal.gSel = gv->currStep->paramVal[3]; /* rf_rfgainsel */
}

static void
ar5211SetOperatingMode(struct ath_hal *ah, int opmode)
{
	struct ath_hal_5211 *ahp = AH5211(ah);
	uint32_t val;

	val = OS_REG_READ(ah, AR_STA_ID1) & 0xffff;
	switch (opmode) {
	case HAL_M_HOSTAP:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_STA_AP
			| AR_STA_ID1_RTS_USE_DEF
			| ahp->ah_staId1Defaults);
		break;
	case HAL_M_IBSS:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_ADHOC
			| AR_STA_ID1_DESC_ANTENNA
			| ahp->ah_staId1Defaults);
		break;
	case HAL_M_STA:
	case HAL_M_MONITOR:
		OS_REG_WRITE(ah, AR_STA_ID1, val
			| AR_STA_ID1_DEFAULT_ANTENNA
			| ahp->ah_staId1Defaults);
		break;
	}
}

void
ar5211SetPCUConfig(struct ath_hal *ah)
{
	ar5211SetOperatingMode(ah, AH_PRIVATE(ah)->ah_opmode);
}
