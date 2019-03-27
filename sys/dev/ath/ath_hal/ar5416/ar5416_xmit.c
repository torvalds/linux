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
#include "ah_desc.h"
#include "ah_internal.h"

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"
#include "ar5416/ar5416desc.h"

/*
 * Stop transmit on the specified queue
 */
HAL_BOOL
ar5416StopTxDma(struct ath_hal *ah, u_int q)
{
#define	STOP_DMA_TIMEOUT	4000	/* us */
#define	STOP_DMA_ITER		100	/* us */
	u_int i;

	HALASSERT(q < AH_PRIVATE(ah)->ah_caps.halTotalQueues);

	HALASSERT(AH5212(ah)->ah_txq[q].tqi_type != HAL_TX_QUEUE_INACTIVE);

	OS_REG_WRITE(ah, AR_Q_TXD, 1 << q);
	for (i = STOP_DMA_TIMEOUT/STOP_DMA_ITER; i != 0; i--) {
		if (ar5212NumTxPending(ah, q) == 0)
			break;
		OS_DELAY(STOP_DMA_ITER);
	}
#ifdef AH_DEBUG
	if (i == 0) {
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: queue %u DMA did not stop in 400 msec\n", __func__, q);
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: QSTS 0x%x Q_TXE 0x%x Q_TXD 0x%x Q_CBR 0x%x\n", __func__,
		    OS_REG_READ(ah, AR_QSTS(q)), OS_REG_READ(ah, AR_Q_TXE),
		    OS_REG_READ(ah, AR_Q_TXD), OS_REG_READ(ah, AR_QCBRCFG(q)));
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: Q_MISC 0x%x Q_RDYTIMECFG 0x%x Q_RDYTIMESHDN 0x%x\n",
		    __func__, OS_REG_READ(ah, AR_QMISC(q)),
		    OS_REG_READ(ah, AR_QRDYTIMECFG(q)),
		    OS_REG_READ(ah, AR_Q_RDYTIMESHDN));
	}
#endif /* AH_DEBUG */

	/* ar5416 and up can kill packets at the PCU level */
	if (ar5212NumTxPending(ah, q)) {
		uint32_t j;

		HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
		    "%s: Num of pending TX Frames %d on Q %d\n",
		    __func__, ar5212NumTxPending(ah, q), q);

		/* Kill last PCU Tx Frame */
		/* TODO - save off and restore current values of Q1/Q2? */
		for (j = 0; j < 2; j++) {
			uint32_t tsfLow = OS_REG_READ(ah, AR_TSF_L32);
			OS_REG_WRITE(ah, AR_QUIET2,
			    SM(10, AR_QUIET2_QUIET_DUR));
			OS_REG_WRITE(ah, AR_QUIET_PERIOD, 100);
			OS_REG_WRITE(ah, AR_NEXT_QUIET, tsfLow >> 10);
			OS_REG_SET_BIT(ah, AR_TIMER_MODE, AR_TIMER_MODE_QUIET);

			if ((OS_REG_READ(ah, AR_TSF_L32)>>10) == (tsfLow>>10))
				break;

			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: TSF moved while trying to set quiet time "
			    "TSF: 0x%08x\n", __func__, tsfLow);
			HALASSERT(j < 1); /* TSF shouldn't count twice or reg access is taking forever */
		}
		
		OS_REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_CHAN_IDLE);
		
		/* Allow the quiet mechanism to do its work */
		OS_DELAY(200);
		OS_REG_CLR_BIT(ah, AR_TIMER_MODE, AR_TIMER_MODE_QUIET);

		/* Verify the transmit q is empty */
		for (i = STOP_DMA_TIMEOUT/STOP_DMA_ITER; i != 0; i--) {
			if (ar5212NumTxPending(ah, q) == 0)
				break;
			OS_DELAY(STOP_DMA_ITER);
		}
		if (i == 0) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: Failed to stop Tx DMA in %d msec after killing"
			    " last frame\n", __func__, STOP_DMA_TIMEOUT / 1000);
		}
		OS_REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_CHAN_IDLE);
	}

	OS_REG_WRITE(ah, AR_Q_TXD, 0);
	return (i != 0);
#undef STOP_DMA_ITER
#undef STOP_DMA_TIMEOUT
}

#define VALID_KEY_TYPES \
        ((1 << HAL_KEY_TYPE_CLEAR) | (1 << HAL_KEY_TYPE_WEP)|\
         (1 << HAL_KEY_TYPE_AES)   | (1 << HAL_KEY_TYPE_TKIP))
#define isValidKeyType(_t)      ((1 << (_t)) & VALID_KEY_TYPES)

#define set11nTries(_series, _index) \
        (SM((_series)[_index].Tries, AR_XmitDataTries##_index))

#define set11nRate(_series, _index) \
        (SM((_series)[_index].Rate, AR_XmitRate##_index))

#define set11nPktDurRTSCTS(_series, _index) \
        (SM((_series)[_index].PktDuration, AR_PacketDur##_index) |\
         ((_series)[_index].RateFlags & HAL_RATESERIES_RTS_CTS   ?\
         AR_RTSCTSQual##_index : 0))

#define set11nRateFlags(_series, _index) \
        ((_series)[_index].RateFlags & HAL_RATESERIES_2040 ? AR_2040_##_index : 0) \
        |((_series)[_index].RateFlags & HAL_RATESERIES_HALFGI ? AR_GI##_index : 0) \
        |((_series)[_index].RateFlags & HAL_RATESERIES_STBC ? AR_STBC##_index : 0) \
        |SM((_series)[_index].ChSel, AR_ChainSel##_index)

/*
 * Descriptor Access Functions
 */

#define VALID_PKT_TYPES \
        ((1<<HAL_PKT_TYPE_NORMAL)|(1<<HAL_PKT_TYPE_ATIM)|\
         (1<<HAL_PKT_TYPE_PSPOLL)|(1<<HAL_PKT_TYPE_PROBE_RESP)|\
         (1<<HAL_PKT_TYPE_BEACON)|(1<<HAL_PKT_TYPE_AMPDU))
#define isValidPktType(_t)      ((1<<(_t)) & VALID_PKT_TYPES)
#define VALID_TX_RATES \
        ((1<<0x0b)|(1<<0x0f)|(1<<0x0a)|(1<<0x0e)|(1<<0x09)|(1<<0x0d)|\
         (1<<0x08)|(1<<0x0c)|(1<<0x1b)|(1<<0x1a)|(1<<0x1e)|(1<<0x19)|\
	 (1<<0x1d)|(1<<0x18)|(1<<0x1c)|(1<<0x01)|(1<<0x02)|(1<<0x03)|\
	 (1<<0x04)|(1<<0x05)|(1<<0x06)|(1<<0x07)|(1<<0x00))
/* NB: accept HT rates */
#define	isValidTxRate(_r)	((1<<((_r) & 0x7f)) & VALID_TX_RATES)

static inline int
ar5416RateToRateTable(struct ath_hal *ah, uint8_t rate, HAL_BOOL is_ht40)
{

	/*
	 * Handle the non-MCS rates
	 */
	switch (rate) {
	case /*   1 Mb */ 0x1b:
	case /*   1 MbS*/ 0x1b | 0x4:
		return (AH5416(ah)->ah_ratesArray[rate1l]);
	case /*   2 Mb */ 0x1a:
		return (AH5416(ah)->ah_ratesArray[rate2l]);
	case /*   2 MbS*/ 0x1a | 0x4:
		return (AH5416(ah)->ah_ratesArray[rate2s]);
	case /* 5.5 Mb */ 0x19:
		return (AH5416(ah)->ah_ratesArray[rate5_5l]);
	case /* 5.5 MbS*/ 0x19 | 0x4:
		return (AH5416(ah)->ah_ratesArray[rate5_5s]);
	case /*  11 Mb */ 0x18:
		return (AH5416(ah)->ah_ratesArray[rate11l]);
	case /*  11 MbS*/ 0x18 | 0x4:
		return (AH5416(ah)->ah_ratesArray[rate11s]);
	}

	/* OFDM rates */
	switch (rate) {
	case /*   6 Mb */ 0x0b:
		return (AH5416(ah)->ah_ratesArray[rate6mb]);
	case /*   9 Mb */ 0x0f:
		return (AH5416(ah)->ah_ratesArray[rate9mb]);
	case /*  12 Mb */ 0x0a:
		return (AH5416(ah)->ah_ratesArray[rate12mb]);
	case /*  18 Mb */ 0x0e:
		return (AH5416(ah)->ah_ratesArray[rate18mb]);
	case /*  24 Mb */ 0x09:
		return (AH5416(ah)->ah_ratesArray[rate24mb]);
	case /*  36 Mb */ 0x0d:
		return (AH5416(ah)->ah_ratesArray[rate36mb]);
	case /*  48 Mb */ 0x08:
		return (AH5416(ah)->ah_ratesArray[rate48mb]);
	case /*  54 Mb */ 0x0c:
		return (AH5416(ah)->ah_ratesArray[rate54mb]);
	}

	/*
	 * Handle HT20/HT40 - we only have to do MCS0-7;
	 * there's no stream differences.
	 */
	if ((rate & 0x80) && is_ht40) {
		return (AH5416(ah)->ah_ratesArray[rateHt40_0 + (rate & 0x7)]);
	} else if (rate & 0x80) {
		return (AH5416(ah)->ah_ratesArray[rateHt20_0 + (rate & 0x7)]);
	}

	/* XXX default (eg XR, bad bad person!) */
	return (AH5416(ah)->ah_ratesArray[rate6mb]);
}

/*
 * Return the TX power to be used for the given rate/chains/TX power.
 *
 * There are a bunch of tweaks to make to a given TX power based on
 * the current configuration, so...
 */
static uint16_t
ar5416GetTxRatePower(struct ath_hal *ah, uint8_t rate, uint8_t tx_chainmask,
    uint16_t txPower, HAL_BOOL is_ht40)
{
	int n_txpower, max_txpower;
	const int cck_ofdm_delta = 2;
#define	EEP_MINOR(_ah) \
	(AH_PRIVATE(_ah)->ah_eeversion & AR5416_EEP_VER_MINOR_MASK)
#define	IS_EEP_MINOR_V2(_ah)	(EEP_MINOR(_ah) >= AR5416_EEP_MINOR_VER_2)

	/* Take a copy ; we may underflow and thus need to clamp things */
	n_txpower = txPower;

	/* HT40? Need to adjust the TX power by this */
	if (is_ht40)
		n_txpower += AH5416(ah)->ah_ht40PowerIncForPdadc;

	/*
	 * Merlin? Offset the target TX power offset - it defaults to
	 * starting at -5.0dBm, but that can change!
	 *
	 * Kiwi/Kite? Always -5.0dBm offset.
	 */
	if (AR_SREV_KIWI_10_OR_LATER(ah)) {
		n_txpower -= (AR5416_PWR_TABLE_OFFSET_DB * 2);
	} else if (AR_SREV_MERLIN_20_OR_LATER(ah)) {
		int8_t pwr_table_offset = 0;
		/* This is in dBm, convert to 1/2 dBm */
		(void) ath_hal_eepromGet(ah, AR_EEP_PWR_TABLE_OFFSET,
		    &pwr_table_offset);
		n_txpower -= (pwr_table_offset * 2);
	}

	/*
	 * If Open-loop TX power control is used, the CCK rates need
	 * to be offset by that.
	 *
	 * Rates: 2S, 2L, 1S, 1L, 5.5S, 5.5L
	 *
	 * XXX Odd, we don't have a PHY table entry for long preamble
	 * 1mbit CCK?
	 */
	if (AR_SREV_MERLIN_20_OR_LATER(ah) &&
	    ath_hal_eepromGetFlag(ah, AR_EEP_OL_PWRCTRL)) {

		if (rate == 0x19 || rate == 0x1a || rate == 0x1b ||
		    rate == (0x19 | 0x04) || rate == (0x1a | 0x04) ||
		    rate == (0x1b | 0x04)) {
			n_txpower -= cck_ofdm_delta;
		}
	}

	/*
	 * We're now offset by the same amount that the static maximum
	 * PHY power tables are.  So, clamp the value based on that rate.
	 */
	max_txpower = ar5416RateToRateTable(ah, rate, is_ht40);
#if 0
	ath_hal_printf(ah, "%s: n_txpower = %d, max_txpower = %d, "
	    "rate = 0x%x , is_ht40 = %d\n",
	    __func__,
	    n_txpower,
	    max_txpower,
	    rate,
	    is_ht40);
#endif
	n_txpower = MIN(max_txpower, n_txpower);

	/*
	 * We don't have to offset the TX power for two or three
	 * chain operation here - it's done by the AR_PHY_POWER_TX_SUB
	 * register setting via the EEPROM.
	 *
	 * So for vendors that programmed the maximum target power assuming
	 * that 2/3 chains are always on, things will just plain work.
	 * (They won't reach that target power if only one chain is on, but
	 * that's a different problem.)
	 */

	/* Over/underflow? Adjust */
	if (n_txpower < 0)
		n_txpower = 0;
	else if (n_txpower > 63)
		n_txpower = 63;

	/*
	 * For some odd reason the AR9160 with txpower=0 results in a
	 * much higher (max?) TX power.  So, if it's a chipset before
	 * AR9220/AR9280, just clamp the minimum value at 1.
	 */
	if ((! AR_SREV_MERLIN_10_OR_LATER(ah)) && (n_txpower == 0))
		n_txpower = 1;

	return (n_txpower);
#undef	EEP_MINOR
#undef	IS_EEP_MINOR_V2
}

HAL_BOOL
ar5416SetupTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	u_int pktLen,
	u_int hdrLen,
	HAL_PKT_TYPE type,
	u_int txPower,
	u_int txRate0, u_int txTries0,
	u_int keyIx,
	u_int antMode,
	u_int flags,
	u_int rtsctsRate,
	u_int rtsctsDuration,
	u_int compicvLen,
	u_int compivLen,
	u_int comp)
{
#define	RTSCTS	(HAL_TXDESC_RTSENA|HAL_TXDESC_CTSENA)
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath_hal_5416 *ahp = AH5416(ah);

	(void) hdrLen;

	HALASSERT(txTries0 != 0);
	HALASSERT(isValidPktType(type));
	HALASSERT(isValidTxRate(txRate0));
	HALASSERT((flags & RTSCTS) != RTSCTS);
	/* XXX validate antMode */

        txPower = (txPower + AH5212(ah)->ah_txPowerIndexOffset);
        if (txPower > 63)
		txPower = 63;

	/*
	 * XXX For now, just assume that this isn't a HT40 frame.
	 * It'll get over-ridden by the multi-rate TX power setup.
	 */
	if (AH5212(ah)->ah_tpcEnabled) {
		txPower = ar5416GetTxRatePower(ah, txRate0,
		    ahp->ah_tx_chainmask,
		    txPower,
		    AH_FALSE);
	}

	ads->ds_ctl0 = (pktLen & AR_FrameLen)
		     | (txPower << AR_XmitPower_S)
		     | (flags & HAL_TXDESC_VEOL ? AR_VEOL : 0)
		     | (flags & HAL_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		     | (flags & HAL_TXDESC_INTREQ ? AR_TxIntrReq : 0)
		     ;
	ads->ds_ctl1 = (type << AR_FrameType_S)
		     | (flags & HAL_TXDESC_NOACK ? AR_NoAck : 0)
		     | (flags & HAL_TXDESC_HWTS ? AR_InsertTS : 0)
                     ;
	ads->ds_ctl2 = SM(txTries0, AR_XmitDataTries0)
		     | (flags & HAL_TXDESC_DURENA ? AR_DurUpdateEn : 0)
		     ;
	ads->ds_ctl3 = (txRate0 << AR_XmitRate0_S)
		     ;
	ads->ds_ctl4 = 0;
	ads->ds_ctl5 = 0;
	ads->ds_ctl6 = 0;
	ads->ds_ctl7 = SM(ahp->ah_tx_chainmask, AR_ChainSel0) 
		     | SM(ahp->ah_tx_chainmask, AR_ChainSel1)
		     | SM(ahp->ah_tx_chainmask, AR_ChainSel2) 
		     | SM(ahp->ah_tx_chainmask, AR_ChainSel3)
		     ;
	ads->ds_ctl8 = SM(0, AR_AntCtl0);
	ads->ds_ctl9 = SM(0, AR_AntCtl1) | SM(txPower, AR_XmitPower1);
	ads->ds_ctl10 = SM(0, AR_AntCtl2) | SM(txPower, AR_XmitPower2);
	ads->ds_ctl11 = SM(0, AR_AntCtl3) | SM(txPower, AR_XmitPower3);

	if (keyIx != HAL_TXKEYIX_INVALID) {
		/* XXX validate key index */
		ads->ds_ctl1 |= SM(keyIx, AR_DestIdx);
		ads->ds_ctl0 |= AR_DestIdxValid;
		ads->ds_ctl6 |= SM(ahp->ah_keytype[keyIx], AR_EncrType);
	}
	if (flags & RTSCTS) {
		if (!isValidTxRate(rtsctsRate)) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: invalid rts/cts rate 0x%x\n",
			    __func__, rtsctsRate);
			return AH_FALSE;
		}
		/* XXX validate rtsctsDuration */
		ads->ds_ctl0 |= (flags & HAL_TXDESC_CTSENA ? AR_CTSEnable : 0)
			     | (flags & HAL_TXDESC_RTSENA ? AR_RTSEnable : 0)
			     ;
		ads->ds_ctl7 |= (rtsctsRate << AR_RTSCTSRate_S);
	}

	/*
	 * Set the TX antenna to 0 for Kite
	 * To preserve existing behaviour, also set the TPC bits to 0;
	 * when TPC is enabled these should be filled in appropriately.
	 *
	 * XXX TODO: when doing TPC, set the TX power up appropriately?
	 */
	if (AR_SREV_KITE(ah)) {
		ads->ds_ctl8 = SM(0, AR_AntCtl0);
		ads->ds_ctl9 = SM(0, AR_AntCtl1) | SM(0, AR_XmitPower1);
		ads->ds_ctl10 = SM(0, AR_AntCtl2) | SM(0, AR_XmitPower2);
		ads->ds_ctl11 = SM(0, AR_AntCtl3) | SM(0, AR_XmitPower3);
	}
	return AH_TRUE;
#undef RTSCTS
}

HAL_BOOL
ar5416SetupXTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	u_int txRate1, u_int txTries1,
	u_int txRate2, u_int txTries2,
	u_int txRate3, u_int txTries3)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	if (txTries1) {
		HALASSERT(isValidTxRate(txRate1));
		ads->ds_ctl2 |= SM(txTries1, AR_XmitDataTries1);
		ads->ds_ctl3 |= (txRate1 << AR_XmitRate1_S);
	}
	if (txTries2) {
		HALASSERT(isValidTxRate(txRate2));
		ads->ds_ctl2 |= SM(txTries2, AR_XmitDataTries2);
		ads->ds_ctl3 |= (txRate2 << AR_XmitRate2_S);
	}
	if (txTries3) {
		HALASSERT(isValidTxRate(txRate3));
		ads->ds_ctl2 |= SM(txTries3, AR_XmitDataTries3);
		ads->ds_ctl3 |= (txRate3 << AR_XmitRate3_S);
	}
	return AH_TRUE;
}

/*
 * XXX TODO: Figure out if AR_InsertTS is required on all sub-frames
 * of a TX descriptor.
 */
HAL_BOOL
ar5416FillTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList, u_int descId,
	u_int qcuId, HAL_BOOL firstSeg, HAL_BOOL lastSeg,
	const struct ath_desc *ds0)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t segLen = segLenList[0];

	HALASSERT((segLen &~ AR_BufLen) == 0);

	ds->ds_data = bufAddrList[0];

	if (firstSeg) {
		/*
		 * First descriptor, don't clobber xmit control data
		 * setup by ar5212SetupTxDesc.
		 */
		ads->ds_ctl1 |= segLen | (lastSeg ? 0 : AR_TxMore);
	} else if (lastSeg) {		/* !firstSeg && lastSeg */
		/*
		 * Last descriptor in a multi-descriptor frame,
		 * copy the multi-rate transmit parameters from
		 * the first frame for processing on completion. 
		 */
		ads->ds_ctl1 = segLen;
#ifdef AH_NEED_DESC_SWAP
		ads->ds_ctl0 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl0)
		    & AR_TxIntrReq;
		ads->ds_ctl2 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl2);
		ads->ds_ctl3 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl3);
		/* ctl6 - we only need encrtype; the rest are blank */
		ads->ds_ctl6 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl6 & AR_EncrType);
#else
		ads->ds_ctl0 = AR5416DESC_CONST(ds0)->ds_ctl0 & AR_TxIntrReq;
		ads->ds_ctl2 = AR5416DESC_CONST(ds0)->ds_ctl2;
		ads->ds_ctl3 = AR5416DESC_CONST(ds0)->ds_ctl3;
		/* ctl6 - we only need encrtype; the rest are blank */
		ads->ds_ctl6 = AR5416DESC_CONST(ds0)->ds_ctl6 & AR_EncrType;
#endif
	} else {			/* !firstSeg && !lastSeg */
		/*
		 * Intermediate descriptor in a multi-descriptor frame.
		 */
#ifdef AH_NEED_DESC_SWAP
		ads->ds_ctl0 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl0)
		    & AR_TxIntrReq;
		ads->ds_ctl6 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl6 & AR_EncrType);
#else
		ads->ds_ctl0 = AR5416DESC_CONST(ds0)->ds_ctl0 & AR_TxIntrReq;
		ads->ds_ctl6 = AR5416DESC_CONST(ds0)->ds_ctl6 & AR_EncrType;
#endif
		ads->ds_ctl1 = segLen | AR_TxMore;
		ads->ds_ctl2 = 0;
		ads->ds_ctl3 = 0;
	}
	/* XXX only on last descriptor? */
	OS_MEMZERO(ads->u.tx.status, sizeof(ads->u.tx.status));
	return AH_TRUE;
}

/*
 * NB: cipher is no longer used, it's calculated.
 */
HAL_BOOL
ar5416ChainTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	HAL_DMA_ADDR *bufAddrList,
	uint32_t *segLenList,
	u_int pktLen,
	u_int hdrLen,
	HAL_PKT_TYPE type,
	u_int keyIx,
	HAL_CIPHER cipher,
	uint8_t delims,
	HAL_BOOL firstSeg,
	HAL_BOOL lastSeg,
	HAL_BOOL lastAggr)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t *ds_txstatus = AR5416_DS_TXSTATUS(ah,ads);
	struct ath_hal_5416 *ahp = AH5416(ah);
	u_int segLen = segLenList[0];

	int isaggr = 0;
	uint32_t last_aggr = 0;
	
	(void) hdrLen;
	(void) ah;

	HALASSERT((segLen &~ AR_BufLen) == 0);
	ds->ds_data = bufAddrList[0];

	HALASSERT(isValidPktType(type));
	if (type == HAL_PKT_TYPE_AMPDU) {
		type = HAL_PKT_TYPE_NORMAL;
		isaggr = 1;
		if (lastAggr == AH_FALSE)
			last_aggr = AR_MoreAggr;
	}

	/*
	 * Since this function is called before any of the other
	 * descriptor setup functions (at least in this particular
	 * 802.11n aggregation implementation), always bzero() the
	 * descriptor. Previously this would be done for all but
	 * the first segment.
	 * XXX TODO: figure out why; perhaps I'm using this slightly
	 * XXX incorrectly.
	 */
	OS_MEMZERO(ds->ds_hw, AR5416_DESC_TX_CTL_SZ);

	/*
	 * Note: VEOL should only be for the last descriptor in the chain.
	 */
	ads->ds_ctl0 = (pktLen & AR_FrameLen);

	/*
	 * For aggregates:
	 * + IsAggr must be set for all descriptors of all subframes of
	 *   the aggregate
	 * + MoreAggr must be set for all descriptors of all subframes
	 *   of the aggregate EXCEPT the last subframe;
	 * + MoreAggr must be _CLEAR_ for all descrpitors of the last
	 *   subframe of the aggregate.
	 */
	ads->ds_ctl1 = (type << AR_FrameType_S)
			| (isaggr ? (AR_IsAggr | last_aggr) : 0);

	ads->ds_ctl2 = 0;
	ads->ds_ctl3 = 0;
	if (keyIx != HAL_TXKEYIX_INVALID) {
		/* XXX validate key index */
		ads->ds_ctl1 |= SM(keyIx, AR_DestIdx);
		ads->ds_ctl0 |= AR_DestIdxValid;
	}

	ads->ds_ctl6 |= SM(ahp->ah_keytype[keyIx], AR_EncrType);
	if (isaggr) {
		ads->ds_ctl6 |= SM(delims, AR_PadDelim);
	}

	if (firstSeg) {
		ads->ds_ctl1 |= segLen | (lastSeg ? 0 : AR_TxMore);
	} else if (lastSeg) {           /* !firstSeg && lastSeg */
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 |= segLen;
	} else {                        /* !firstSeg && !lastSeg */
		/*
		 * Intermediate descriptor in a multi-descriptor frame.
		 */
		ads->ds_ctl0 = 0;
		ads->ds_ctl1 |= segLen | AR_TxMore;
	}
	ds_txstatus[0] = ds_txstatus[1] = 0;
	ds_txstatus[9] &= ~AR_TxDone;
	
	return AH_TRUE;
}

HAL_BOOL
ar5416SetupFirstTxDesc(struct ath_hal *ah, struct ath_desc *ds,
	u_int aggrLen, u_int flags, u_int txPower,
	u_int txRate0, u_int txTries0, u_int antMode,
	u_int rtsctsRate, u_int rtsctsDuration)
{
#define RTSCTS  (HAL_TXDESC_RTSENA|HAL_TXDESC_CTSENA)
	struct ar5416_desc *ads = AR5416DESC(ds);
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALASSERT(txTries0 != 0);
	HALASSERT(isValidTxRate(txRate0));
	HALASSERT((flags & RTSCTS) != RTSCTS);
	/* XXX validate antMode */
	
	txPower = (txPower + ahp->ah_txPowerIndexOffset );
	if(txPower > 63)  txPower=63;

	ads->ds_ctl0 |= (txPower << AR_XmitPower_S)
		| (flags & HAL_TXDESC_VEOL ? AR_VEOL : 0)
		| (flags & HAL_TXDESC_CLRDMASK ? AR_ClrDestMask : 0)
		| (flags & HAL_TXDESC_INTREQ ? AR_TxIntrReq : 0);
	ads->ds_ctl1 |= (flags & HAL_TXDESC_NOACK ? AR_NoAck : 0);
	ads->ds_ctl2 |= SM(txTries0, AR_XmitDataTries0);
	ads->ds_ctl3 |= (txRate0 << AR_XmitRate0_S);
	ads->ds_ctl7 = SM(AH5416(ah)->ah_tx_chainmask, AR_ChainSel0) 
		| SM(AH5416(ah)->ah_tx_chainmask, AR_ChainSel1)
		| SM(AH5416(ah)->ah_tx_chainmask, AR_ChainSel2) 
		| SM(AH5416(ah)->ah_tx_chainmask, AR_ChainSel3);
	
	/* NB: no V1 WAR */
	ads->ds_ctl8 = SM(0, AR_AntCtl0);
	ads->ds_ctl9 = SM(0, AR_AntCtl1) | SM(txPower, AR_XmitPower1);
	ads->ds_ctl10 = SM(0, AR_AntCtl2) | SM(txPower, AR_XmitPower2);
	ads->ds_ctl11 = SM(0, AR_AntCtl3) | SM(txPower, AR_XmitPower3);

	ads->ds_ctl6 &= ~(0xffff);
	ads->ds_ctl6 |= SM(aggrLen, AR_AggrLen);

	if (flags & RTSCTS) {
		/* XXX validate rtsctsDuration */
		ads->ds_ctl0 |= (flags & HAL_TXDESC_CTSENA ? AR_CTSEnable : 0)
			| (flags & HAL_TXDESC_RTSENA ? AR_RTSEnable : 0);
	}

	/*
	 * Set the TX antenna to 0 for Kite
	 * To preserve existing behaviour, also set the TPC bits to 0;
	 * when TPC is enabled these should be filled in appropriately.
	 */
	if (AR_SREV_KITE(ah)) {
		ads->ds_ctl8 = SM(0, AR_AntCtl0);
		ads->ds_ctl9 = SM(0, AR_AntCtl1) | SM(0, AR_XmitPower1);
		ads->ds_ctl10 = SM(0, AR_AntCtl2) | SM(0, AR_XmitPower2);
		ads->ds_ctl11 = SM(0, AR_AntCtl3) | SM(0, AR_XmitPower3);
	}
	
	return AH_TRUE;
#undef RTSCTS
}

HAL_BOOL
ar5416SetupLastTxDesc(struct ath_hal *ah, struct ath_desc *ds,
		const struct ath_desc *ds0)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 &= ~AR_MoreAggr;
	ads->ds_ctl6 &= ~AR_PadDelim;

	/* hack to copy rate info to last desc for later processing */
#ifdef AH_NEED_DESC_SWAP
	ads->ds_ctl2 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl2);
	ads->ds_ctl3 = __bswap32(AR5416DESC_CONST(ds0)->ds_ctl3);
#else
	ads->ds_ctl2 = AR5416DESC_CONST(ds0)->ds_ctl2;
	ads->ds_ctl3 = AR5416DESC_CONST(ds0)->ds_ctl3;
#endif
	return AH_TRUE;
}

#ifdef AH_NEED_DESC_SWAP
/* Swap transmit descriptor */
static __inline void
ar5416SwapTxDesc(struct ath_desc *ds)
{
	ds->ds_data = __bswap32(ds->ds_data);
	ds->ds_ctl0 = __bswap32(ds->ds_ctl0);
	ds->ds_ctl1 = __bswap32(ds->ds_ctl1);
	ds->ds_hw[0] = __bswap32(ds->ds_hw[0]);
	ds->ds_hw[1] = __bswap32(ds->ds_hw[1]);
	ds->ds_hw[2] = __bswap32(ds->ds_hw[2]);
	ds->ds_hw[3] = __bswap32(ds->ds_hw[3]);
}
#endif

/*
 * Processing of HW TX descriptor.
 */
HAL_STATUS
ar5416ProcTxDesc(struct ath_hal *ah,
	struct ath_desc *ds, struct ath_tx_status *ts)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t *ds_txstatus = AR5416_DS_TXSTATUS(ah,ads);

#ifdef AH_NEED_DESC_SWAP
	if ((ds_txstatus[9] & __bswap32(AR_TxDone)) == 0)
		return HAL_EINPROGRESS;
	ar5416SwapTxDesc(ds);
#else
	if ((ds_txstatus[9] & AR_TxDone) == 0)
		return HAL_EINPROGRESS;
#endif

	/* Update software copies of the HW status */
	ts->ts_seqnum = MS(ds_txstatus[9], AR_SeqNum);
	ts->ts_tstamp = AR_SendTimestamp(ds_txstatus);
	ts->ts_tid = MS(ds_txstatus[9], AR_TxTid);

	ts->ts_status = 0;
	if (ds_txstatus[1] & AR_ExcessiveRetries)
		ts->ts_status |= HAL_TXERR_XRETRY;
	if (ds_txstatus[1] & AR_Filtered)
		ts->ts_status |= HAL_TXERR_FILT;
	if (ds_txstatus[1] & AR_FIFOUnderrun)
		ts->ts_status |= HAL_TXERR_FIFO;
	if (ds_txstatus[9] & AR_TxOpExceeded)
		ts->ts_status |= HAL_TXERR_XTXOP;
	if (ds_txstatus[1] & AR_TxTimerExpired)
		ts->ts_status |= HAL_TXERR_TIMER_EXPIRED;

	ts->ts_flags  = 0;
	if (ds_txstatus[0] & AR_TxBaStatus) {
		ts->ts_flags |= HAL_TX_BA;
		ts->ts_ba_low = AR_BaBitmapLow(ds_txstatus);
		ts->ts_ba_high = AR_BaBitmapHigh(ds_txstatus);
	}
	if (ds->ds_ctl1 & AR_IsAggr)
		ts->ts_flags |= HAL_TX_AGGR;
	if (ds_txstatus[1] & AR_DescCfgErr)
		ts->ts_flags |= HAL_TX_DESC_CFG_ERR;
	if (ds_txstatus[1] & AR_TxDataUnderrun)
		ts->ts_flags |= HAL_TX_DATA_UNDERRUN;
	if (ds_txstatus[1] & AR_TxDelimUnderrun)
		ts->ts_flags |= HAL_TX_DELIM_UNDERRUN;

	/*
	 * Extract the transmit rate used and mark the rate as
	 * ``alternate'' if it wasn't the series 0 rate.
	 */
	ts->ts_finaltsi =  MS(ds_txstatus[9], AR_FinalTxIdx);
	switch (ts->ts_finaltsi) {
	case 0:
		ts->ts_rate = MS(ads->ds_ctl3, AR_XmitRate0);
		break;
	case 1:
		ts->ts_rate = MS(ads->ds_ctl3, AR_XmitRate1);
		break;
	case 2:
		ts->ts_rate = MS(ads->ds_ctl3, AR_XmitRate2);
		break;
	case 3:
		ts->ts_rate = MS(ads->ds_ctl3, AR_XmitRate3);
		break;
	}

	ts->ts_rssi = MS(ds_txstatus[5], AR_TxRSSICombined);
	ts->ts_rssi_ctl[0] = MS(ds_txstatus[0], AR_TxRSSIAnt00);
	ts->ts_rssi_ctl[1] = MS(ds_txstatus[0], AR_TxRSSIAnt01);
	ts->ts_rssi_ctl[2] = MS(ds_txstatus[0], AR_TxRSSIAnt02);
	ts->ts_rssi_ext[0] = MS(ds_txstatus[5], AR_TxRSSIAnt10);
	ts->ts_rssi_ext[1] = MS(ds_txstatus[5], AR_TxRSSIAnt11);
	ts->ts_rssi_ext[2] = MS(ds_txstatus[5], AR_TxRSSIAnt12);
	ts->ts_evm0 = AR_TxEVM0(ds_txstatus);
	ts->ts_evm1 = AR_TxEVM1(ds_txstatus);
	ts->ts_evm2 = AR_TxEVM2(ds_txstatus);

	ts->ts_shortretry = MS(ds_txstatus[1], AR_RTSFailCnt);
	ts->ts_longretry = MS(ds_txstatus[1], AR_DataFailCnt);
	/*
	 * The retry count has the number of un-acked tries for the
	 * final series used.  When doing multi-rate retry we must
	 * fixup the retry count by adding in the try counts for
	 * each series that was fully-processed.  Beware that this
	 * takes values from the try counts in the final descriptor.
	 * These are not required by the hardware.  We assume they
	 * are placed there by the driver as otherwise we have no
	 * access and the driver can't do the calculation because it
	 * doesn't know the descriptor format.
	 */
	switch (ts->ts_finaltsi) {
	case 3: ts->ts_longretry += MS(ads->ds_ctl2, AR_XmitDataTries2);
	case 2: ts->ts_longretry += MS(ads->ds_ctl2, AR_XmitDataTries1);
	case 1: ts->ts_longretry += MS(ads->ds_ctl2, AR_XmitDataTries0);
	}

	/*
	 * These fields are not used. Zero these to preserve compatibility
	 * with existing drivers.
	 */
	ts->ts_virtcol = MS(ads->ds_ctl1, AR_VirtRetryCnt);
	ts->ts_antenna = 0; /* We don't switch antennas on Owl*/

	/* handle tx trigger level changes internally */
	if ((ts->ts_status & HAL_TXERR_FIFO) ||
	    (ts->ts_flags & (HAL_TX_DATA_UNDERRUN | HAL_TX_DELIM_UNDERRUN)))
		ar5212UpdateTxTrigLevel(ah, AH_TRUE);

	return HAL_OK;
}

HAL_BOOL
ar5416SetGlobalTxTimeout(struct ath_hal *ah, u_int tu)
{
	struct ath_hal_5416 *ahp = AH5416(ah);

	if (tu > 0xFFFF) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: bad global tx timeout %u\n",
		    __func__, tu);
		/* restore default handling */
		ahp->ah_globaltxtimeout = (u_int) -1;
		return AH_FALSE;
	}
	OS_REG_RMW_FIELD(ah, AR_GTXTO, AR_GTXTO_TIMEOUT_LIMIT, tu);
	ahp->ah_globaltxtimeout = tu;
	return AH_TRUE;
}

u_int
ar5416GetGlobalTxTimeout(struct ath_hal *ah)
{
	return MS(OS_REG_READ(ah, AR_GTXTO), AR_GTXTO_TIMEOUT_LIMIT);
}

#define	HT_RC_2_MCS(_rc)	((_rc) & 0x0f)
static const u_int8_t baDurationDelta[] = {
	24,	//  0: BPSK
	12,	//  1: QPSK 1/2
	12,	//  2: QPSK 3/4
	4,	//  3: 16-QAM 1/2
	4,	//  4: 16-QAM 3/4
	4,	//  5: 64-QAM 2/3
	4,	//  6: 64-QAM 3/4
	4,	//  7: 64-QAM 5/6
	24,	//  8: BPSK
	12,	//  9: QPSK 1/2
	12,	// 10: QPSK 3/4
	4,	// 11: 16-QAM 1/2
	4,	// 12: 16-QAM 3/4
	4,	// 13: 64-QAM 2/3
	4,	// 14: 64-QAM 3/4
	4,	// 15: 64-QAM 5/6
};

void
ar5416Set11nRateScenario(struct ath_hal *ah, struct ath_desc *ds,
        u_int durUpdateEn, u_int rtsctsRate,
	HAL_11N_RATE_SERIES series[], u_int nseries, u_int flags)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t ds_ctl0;

	HALASSERT(nseries == 4);
	(void)nseries;

	/*
	 * Only one of RTS and CTS enable must be set.
	 * If a frame has both set, just do RTS protection -
	 * that's enough to satisfy legacy protection.
	 */
	if (flags & (HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA)) {
		ds_ctl0 = ads->ds_ctl0;

		if (flags & HAL_TXDESC_RTSENA) {
			ds_ctl0 &= ~AR_CTSEnable;
			ds_ctl0 |= AR_RTSEnable;
		} else {
			ds_ctl0 &= ~AR_RTSEnable;
			ds_ctl0 |= AR_CTSEnable;
		}

		ads->ds_ctl0 = ds_ctl0;
	} else {
		ads->ds_ctl0 =
		    (ads->ds_ctl0 & ~(AR_RTSEnable | AR_CTSEnable));
	}

	ads->ds_ctl2 = set11nTries(series, 0)
		     | set11nTries(series, 1)
		     | set11nTries(series, 2)
		     | set11nTries(series, 3)
		     | (durUpdateEn ? AR_DurUpdateEn : 0);

	ads->ds_ctl3 = set11nRate(series, 0)
		     | set11nRate(series, 1)
		     | set11nRate(series, 2)
		     | set11nRate(series, 3);

	ads->ds_ctl4 = set11nPktDurRTSCTS(series, 0)
		     | set11nPktDurRTSCTS(series, 1);

	ads->ds_ctl5 = set11nPktDurRTSCTS(series, 2)
		     | set11nPktDurRTSCTS(series, 3);

	ads->ds_ctl7 = set11nRateFlags(series, 0)
		     | set11nRateFlags(series, 1)
		     | set11nRateFlags(series, 2)
		     | set11nRateFlags(series, 3)
		     | SM(rtsctsRate, AR_RTSCTSRate);

	/*
	 * Doing per-packet TPC - update the TX power for the first
	 * field; program in the other series.
	 */
	if (AH5212(ah)->ah_tpcEnabled) {
		uint32_t ds_ctl0;
		uint16_t txPower;

		/* Modify the tx power field for rate 0 */
		txPower = ar5416GetTxRatePower(ah, series[0].Rate,
		    series[0].ChSel,
		    series[0].tx_power_cap,
		    !! (series[0].RateFlags & HAL_RATESERIES_2040));
		ds_ctl0 = ads->ds_ctl0 & ~AR_XmitPower;
		ds_ctl0 |= (txPower << AR_XmitPower_S);
		ads->ds_ctl0 = ds_ctl0;

		/*
		 * Override the whole descriptor field for each TX power.
		 *
		 * This will need changing if we ever support antenna control
		 * programming.
		 */
		txPower = ar5416GetTxRatePower(ah, series[1].Rate,
		    series[1].ChSel,
		    series[1].tx_power_cap,
		    !! (series[1].RateFlags & HAL_RATESERIES_2040));
		ads->ds_ctl9 = SM(0, AR_AntCtl1) | SM(txPower, AR_XmitPower1);

		txPower = ar5416GetTxRatePower(ah, series[2].Rate,
		    series[2].ChSel,
		    series[2].tx_power_cap,
		    !! (series[2].RateFlags & HAL_RATESERIES_2040));
		ads->ds_ctl10 = SM(0, AR_AntCtl2) | SM(txPower, AR_XmitPower2);

		txPower = ar5416GetTxRatePower(ah, series[3].Rate,
		    series[3].ChSel,
		    series[3].tx_power_cap,
		    !! (series[3].RateFlags & HAL_RATESERIES_2040));
		ads->ds_ctl11 = SM(0, AR_AntCtl3) | SM(txPower, AR_XmitPower3);
	}
}

/*
 * Note: this should be called before calling ar5416SetBurstDuration()
 * (if it is indeed called) in order to ensure that the burst duration
 * is correctly updated with the BA delta workaround.
 */
void
ar5416Set11nAggrFirst(struct ath_hal *ah, struct ath_desc *ds, u_int aggrLen,
    u_int numDelims)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t flags;
	uint32_t burstDur;
	uint8_t rate;

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ads->ds_ctl6 &= ~(AR_AggrLen | AR_PadDelim);
	ads->ds_ctl6 |= SM(aggrLen, AR_AggrLen);
	ads->ds_ctl6 |= SM(numDelims, AR_PadDelim);

	if (! AR_SREV_MERLIN_10_OR_LATER(ah)) {
		/*
		 * XXX It'd be nice if I were passed in the rate scenario
		 * at this point..
		 */
		rate = MS(ads->ds_ctl3, AR_XmitRate0);
		flags = ads->ds_ctl0 & (AR_CTSEnable | AR_RTSEnable);
		/*
		 * WAR - MAC assumes normal ACK time instead of
		 * block ACK while computing packet duration.
		 * Add this delta to the burst duration in the descriptor.
		 */
		if (flags && (ads->ds_ctl1 & AR_IsAggr)) {
			burstDur = baDurationDelta[HT_RC_2_MCS(rate)];
			ads->ds_ctl2 &= ~(AR_BurstDur);
			ads->ds_ctl2 |= SM(burstDur, AR_BurstDur);
		}
	}
}

void
ar5416Set11nAggrMiddle(struct ath_hal *ah, struct ath_desc *ds, u_int numDelims)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t *ds_txstatus = AR5416_DS_TXSTATUS(ah,ads);

	ads->ds_ctl1 |= (AR_IsAggr | AR_MoreAggr);

	ads->ds_ctl6 &= ~AR_PadDelim;
	ads->ds_ctl6 |= SM(numDelims, AR_PadDelim);
	ads->ds_ctl6 &= ~AR_AggrLen;

	/*
	 * Clear the TxDone status here, may need to change
	 * func name to reflect this
	 */
	ds_txstatus[9] &= ~AR_TxDone;
}

void
ar5416Set11nAggrLast(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 |= AR_IsAggr;
	ads->ds_ctl1 &= ~AR_MoreAggr;
	ads->ds_ctl6 &= ~AR_PadDelim;
}

void
ar5416Clr11nAggr(struct ath_hal *ah, struct ath_desc *ds)
{
	struct ar5416_desc *ads = AR5416DESC(ds);

	ads->ds_ctl1 &= (~AR_IsAggr & ~AR_MoreAggr);
	ads->ds_ctl6 &= ~AR_PadDelim;
	ads->ds_ctl6 &= ~AR_AggrLen;
}

void
ar5416Set11nVirtualMoreFrag(struct ath_hal *ah, struct ath_desc *ds,
    u_int vmf)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	if (vmf)
		ads->ds_ctl0 |= AR_VirtMoreFrag;
	else
		ads->ds_ctl0 &= ~AR_VirtMoreFrag;
}

/*
 * Program the burst duration, with the included BA delta if it's
 * applicable.
 */
void
ar5416Set11nBurstDuration(struct ath_hal *ah, struct ath_desc *ds,
                                                  u_int burstDuration)
{
	struct ar5416_desc *ads = AR5416DESC(ds);
	uint32_t burstDur = 0;
	uint8_t rate;

	if (! AR_SREV_MERLIN_10_OR_LATER(ah)) {
		/*
		 * XXX It'd be nice if I were passed in the rate scenario
		 * at this point..
		 */
		rate = MS(ads->ds_ctl3, AR_XmitDataTries0);
		/*
		 * WAR - MAC assumes normal ACK time instead of
		 * block ACK while computing packet duration.
		 * Add this delta to the burst duration in the descriptor.
		 */
		if (ads->ds_ctl1 & AR_IsAggr) {
			burstDur = baDurationDelta[HT_RC_2_MCS(rate)];
		}
	}

	ads->ds_ctl2 &= ~AR_BurstDur;
	ads->ds_ctl2 |= SM(burstDur + burstDuration, AR_BurstDur);
}

/*
 * Retrieve the rate table from the given TX completion descriptor
 */
HAL_BOOL
ar5416GetTxCompletionRates(struct ath_hal *ah, const struct ath_desc *ds0, int *rates, int *tries)
{
	const struct ar5416_desc *ads = AR5416DESC_CONST(ds0);

	rates[0] = MS(ads->ds_ctl3, AR_XmitRate0);
	rates[1] = MS(ads->ds_ctl3, AR_XmitRate1);
	rates[2] = MS(ads->ds_ctl3, AR_XmitRate2);
	rates[3] = MS(ads->ds_ctl3, AR_XmitRate3);

	tries[0] = MS(ads->ds_ctl2, AR_XmitDataTries0);
	tries[1] = MS(ads->ds_ctl2, AR_XmitDataTries1);
	tries[2] = MS(ads->ds_ctl2, AR_XmitDataTries2);
	tries[3] = MS(ads->ds_ctl2, AR_XmitDataTries3);

	return AH_TRUE;
}


/*
 * TX queue management routines - AR5416 and later chipsets
 */

/*
 * Allocate and initialize a tx DCU/QCU combination.
 */
int
ar5416SetupTxQueue(struct ath_hal *ah, HAL_TX_QUEUE type,
	const HAL_TXQ_INFO *qInfo)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_TX_QUEUE_INFO *qi;
	HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
	int q, defqflags;

	/* by default enable OK+ERR+DESC+URN interrupts */
	defqflags = HAL_TXQ_TXOKINT_ENABLE
		  | HAL_TXQ_TXERRINT_ENABLE
		  | HAL_TXQ_TXDESCINT_ENABLE
		  | HAL_TXQ_TXURNINT_ENABLE;
	/* XXX move queue assignment to driver */
	switch (type) {
	case HAL_TX_QUEUE_BEACON:
		q = pCap->halTotalQueues-1;	/* highest priority */
		defqflags |= HAL_TXQ_DBA_GATED
		       | HAL_TXQ_CBR_DIS_QEMPTY
		       | HAL_TXQ_ARB_LOCKOUT_GLOBAL
		       | HAL_TXQ_BACKOFF_DISABLE;
		break;
	case HAL_TX_QUEUE_CAB:
		q = pCap->halTotalQueues-2;	/* next highest priority */
		defqflags |= HAL_TXQ_DBA_GATED
		       | HAL_TXQ_CBR_DIS_QEMPTY
		       | HAL_TXQ_CBR_DIS_BEMPTY
		       | HAL_TXQ_ARB_LOCKOUT_GLOBAL
		       | HAL_TXQ_BACKOFF_DISABLE;
		break;
	case HAL_TX_QUEUE_PSPOLL:
		q = 1;				/* lowest priority */
		defqflags |= HAL_TXQ_DBA_GATED
		       | HAL_TXQ_CBR_DIS_QEMPTY
		       | HAL_TXQ_CBR_DIS_BEMPTY
		       | HAL_TXQ_ARB_LOCKOUT_GLOBAL
		       | HAL_TXQ_BACKOFF_DISABLE;
		break;
	case HAL_TX_QUEUE_UAPSD:
		q = pCap->halTotalQueues-3;	/* nextest highest priority */
		if (ahp->ah_txq[q].tqi_type != HAL_TX_QUEUE_INACTIVE) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: no available UAPSD tx queue\n", __func__);
			return -1;
		}
		break;
	case HAL_TX_QUEUE_DATA:
		for (q = 0; q < pCap->halTotalQueues; q++)
			if (ahp->ah_txq[q].tqi_type == HAL_TX_QUEUE_INACTIVE)
				break;
		if (q == pCap->halTotalQueues) {
			HALDEBUG(ah, HAL_DEBUG_ANY,
			    "%s: no available tx queue\n", __func__);
			return -1;
		}
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_ANY,
		    "%s: bad tx queue type %u\n", __func__, type);
		return -1;
	}

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: queue %u\n", __func__, q);

	qi = &ahp->ah_txq[q];
	if (qi->tqi_type != HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: tx queue %u already active\n",
		    __func__, q);
		return -1;
	}
	OS_MEMZERO(qi, sizeof(HAL_TX_QUEUE_INFO));
	qi->tqi_type = type;
	if (qInfo == AH_NULL) {
		qi->tqi_qflags = defqflags;
		qi->tqi_aifs = INIT_AIFS;
		qi->tqi_cwmin = HAL_TXQ_USEDEFAULT;	/* NB: do at reset */
		qi->tqi_cwmax = INIT_CWMAX;
		qi->tqi_shretry = INIT_SH_RETRY;
		qi->tqi_lgretry = INIT_LG_RETRY;
		qi->tqi_physCompBuf = 0;
	} else {
		qi->tqi_physCompBuf = qInfo->tqi_compBuf;
		(void) ar5212SetTxQueueProps(ah, q, qInfo);
	}
	/* NB: must be followed by ar5212ResetTxQueue */
	return q;
}

/*
 * Update the h/w interrupt registers to reflect a tx q's configuration.
 */
static void
setTxQInterrupts(struct ath_hal *ah, HAL_TX_QUEUE_INFO *qi)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
	    "%s: tx ok 0x%x err 0x%x desc 0x%x eol 0x%x urn 0x%x\n", __func__,
	    ahp->ah_txOkInterruptMask, ahp->ah_txErrInterruptMask,
	    ahp->ah_txDescInterruptMask, ahp->ah_txEolInterruptMask,
	    ahp->ah_txUrnInterruptMask);

	OS_REG_WRITE(ah, AR_IMR_S0,
		  SM(ahp->ah_txOkInterruptMask, AR_IMR_S0_QCU_TXOK)
		| SM(ahp->ah_txDescInterruptMask, AR_IMR_S0_QCU_TXDESC)
	);
	OS_REG_WRITE(ah, AR_IMR_S1,
		  SM(ahp->ah_txErrInterruptMask, AR_IMR_S1_QCU_TXERR)
		| SM(ahp->ah_txEolInterruptMask, AR_IMR_S1_QCU_TXEOL)
	);
	OS_REG_RMW_FIELD(ah, AR_IMR_S2,
		AR_IMR_S2_QCU_TXURN, ahp->ah_txUrnInterruptMask);
}

/*
 * Set the retry, aifs, cwmin/max, readyTime regs for specified queue
 * Assumes:
 *  phwChannel has been set to point to the current channel
 */
#define	TU_TO_USEC(_tu)		((_tu) << 10)
HAL_BOOL
ar5416ResetTxQueue(struct ath_hal *ah, u_int q)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;
	const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
	HAL_TX_QUEUE_INFO *qi;
	uint32_t cwMin, chanCwMin, qmisc, dmisc;

	if (q >= pCap->halTotalQueues) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: invalid queue num %u\n",
		    __func__, q);
		return AH_FALSE;
	}
	qi = &ahp->ah_txq[q];
	if (qi->tqi_type == HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: inactive queue %u\n",
		    __func__, q);
		return AH_TRUE;		/* XXX??? */
	}

	HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: reset queue %u\n", __func__, q);

	if (qi->tqi_cwmin == HAL_TXQ_USEDEFAULT) {
		/*
		 * Select cwmin according to channel type.
		 * NB: chan can be NULL during attach
		 */
		if (chan && IEEE80211_IS_CHAN_B(chan))
			chanCwMin = INIT_CWMIN_11B;
		else
			chanCwMin = INIT_CWMIN;
		/* make sure that the CWmin is of the form (2^n - 1) */
		for (cwMin = 1; cwMin < chanCwMin; cwMin = (cwMin << 1) | 1)
			;
	} else
		cwMin = qi->tqi_cwmin;

	/* set cwMin/Max and AIFS values */
	OS_REG_WRITE(ah, AR_DLCL_IFS(q),
		  SM(cwMin, AR_D_LCL_IFS_CWMIN)
		| SM(qi->tqi_cwmax, AR_D_LCL_IFS_CWMAX)
		| SM(qi->tqi_aifs, AR_D_LCL_IFS_AIFS));

	/* Set retry limit values */
	OS_REG_WRITE(ah, AR_DRETRY_LIMIT(q), 
		   SM(INIT_SSH_RETRY, AR_D_RETRY_LIMIT_STA_SH)
		 | SM(INIT_SLG_RETRY, AR_D_RETRY_LIMIT_STA_LG)
		 | SM(qi->tqi_lgretry, AR_D_RETRY_LIMIT_FR_LG)
		 | SM(qi->tqi_shretry, AR_D_RETRY_LIMIT_FR_SH)
	);

	/* NB: always enable early termination on the QCU */
	qmisc = AR_Q_MISC_DCU_EARLY_TERM_REQ
	      | SM(AR_Q_MISC_FSP_ASAP, AR_Q_MISC_FSP);

	/* NB: always enable DCU to wait for next fragment from QCU */
	dmisc = AR_D_MISC_FRAG_WAIT_EN;

	/* Enable exponential backoff window */
	dmisc |= AR_D_MISC_BKOFF_PERSISTENCE;

	/* 
	 * The chip reset default is to use a DCU backoff threshold of 0x2.
	 * Restore this when programming the DCU MISC register.
	 */
	dmisc |= 0x2;

	/* multiqueue support */
	if (qi->tqi_cbrPeriod) {
		OS_REG_WRITE(ah, AR_QCBRCFG(q), 
			  SM(qi->tqi_cbrPeriod,AR_Q_CBRCFG_CBR_INTERVAL)
			| SM(qi->tqi_cbrOverflowLimit, AR_Q_CBRCFG_CBR_OVF_THRESH));
		qmisc = (qmisc &~ AR_Q_MISC_FSP) | AR_Q_MISC_FSP_CBR;
		if (qi->tqi_cbrOverflowLimit)
			qmisc |= AR_Q_MISC_CBR_EXP_CNTR_LIMIT;
	}

	if (qi->tqi_readyTime && (qi->tqi_type != HAL_TX_QUEUE_CAB)) {
		OS_REG_WRITE(ah, AR_QRDYTIMECFG(q),
			  SM(qi->tqi_readyTime, AR_Q_RDYTIMECFG_INT)
			| AR_Q_RDYTIMECFG_ENA);
	}
	
	OS_REG_WRITE(ah, AR_DCHNTIME(q),
		  SM(qi->tqi_burstTime, AR_D_CHNTIME_DUR)
		| (qi->tqi_burstTime ? AR_D_CHNTIME_EN : 0));

	if (qi->tqi_readyTime &&
	    (qi->tqi_qflags & HAL_TXQ_RDYTIME_EXP_POLICY_ENABLE))
		qmisc |= AR_Q_MISC_RDYTIME_EXP_POLICY;
	if (qi->tqi_qflags & HAL_TXQ_DBA_GATED)
		qmisc = (qmisc &~ AR_Q_MISC_FSP) | AR_Q_MISC_FSP_DBA_GATED;
	if (MS(qmisc, AR_Q_MISC_FSP) != AR_Q_MISC_FSP_ASAP) {
		/*
		 * These are meangingful only when not scheduled asap.
		 */
		if (qi->tqi_qflags & HAL_TXQ_CBR_DIS_BEMPTY)
			qmisc |= AR_Q_MISC_CBR_INCR_DIS0;
		else
			qmisc &= ~AR_Q_MISC_CBR_INCR_DIS0;
		if (qi->tqi_qflags & HAL_TXQ_CBR_DIS_QEMPTY)
			qmisc |= AR_Q_MISC_CBR_INCR_DIS1;
		else
			qmisc &= ~AR_Q_MISC_CBR_INCR_DIS1;
	}

	if (qi->tqi_qflags & HAL_TXQ_BACKOFF_DISABLE)
		dmisc |= AR_D_MISC_POST_FR_BKOFF_DIS;
	if (qi->tqi_qflags & HAL_TXQ_FRAG_BURST_BACKOFF_ENABLE)
		dmisc |= AR_D_MISC_FRAG_BKOFF_EN;
	if (qi->tqi_qflags & HAL_TXQ_ARB_LOCKOUT_GLOBAL)
		dmisc |= SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
			    AR_D_MISC_ARB_LOCKOUT_CNTRL);
	else if (qi->tqi_qflags & HAL_TXQ_ARB_LOCKOUT_INTRA)
		dmisc |= SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_INTRA_FR,
			    AR_D_MISC_ARB_LOCKOUT_CNTRL);
	if (qi->tqi_qflags & HAL_TXQ_IGNORE_VIRTCOL)
		dmisc |= SM(AR_D_MISC_VIR_COL_HANDLING_IGNORE,
			    AR_D_MISC_VIR_COL_HANDLING);
	if (qi->tqi_qflags & HAL_TXQ_SEQNUM_INC_DIS)
		dmisc |= AR_D_MISC_SEQ_NUM_INCR_DIS;

	/*
	 * Fillin type-dependent bits.  Most of this can be
	 * removed by specifying the queue parameters in the
	 * driver; it's here for backwards compatibility.
	 */
	switch (qi->tqi_type) {
	case HAL_TX_QUEUE_BEACON:		/* beacon frames */
		qmisc |= AR_Q_MISC_FSP_DBA_GATED
		      |  AR_Q_MISC_BEACON_USE
		      |  AR_Q_MISC_CBR_INCR_DIS1;

		dmisc |= SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
			    AR_D_MISC_ARB_LOCKOUT_CNTRL)
		      |  AR_D_MISC_BEACON_USE
		      |  AR_D_MISC_POST_FR_BKOFF_DIS;
		break;
	case HAL_TX_QUEUE_CAB:			/* CAB  frames */
		/* 
		 * No longer Enable AR_Q_MISC_RDYTIME_EXP_POLICY,
		 * There is an issue with the CAB Queue
		 * not properly refreshing the Tx descriptor if
		 * the TXE clear setting is used.
		 */
		qmisc |= AR_Q_MISC_FSP_DBA_GATED
		      |  AR_Q_MISC_CBR_INCR_DIS1
		      |  AR_Q_MISC_CBR_INCR_DIS0;
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE, "%s: CAB: tqi_readyTime = %d\n",
		    __func__, qi->tqi_readyTime);
		if (qi->tqi_readyTime) {
			HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
			    "%s: using tqi_readyTime\n", __func__);
			OS_REG_WRITE(ah, AR_QRDYTIMECFG(q),
			    SM(qi->tqi_readyTime, AR_Q_RDYTIMECFG_INT) |
			    AR_Q_RDYTIMECFG_ENA);
		} else {
			int value;
			/*
			 * NB: don't set default ready time if driver
			 * has explicitly specified something.  This is
			 * here solely for backwards compatibility.
			 */
			/*
			 * XXX for now, hard-code a CAB interval of 70%
			 * XXX of the total beacon interval.
			 *
			 * XXX This keeps Merlin and later based MACs
			 * XXX quite a bit happier (stops stuck beacons,
			 * XXX which I gather is because of such a long
			 * XXX cabq time.)
			 */
			value = (ahp->ah_beaconInterval * 50 / 100)
				- ah->ah_config.ah_additional_swba_backoff
				- ah->ah_config.ah_sw_beacon_response_time
				+ ah->ah_config.ah_dma_beacon_response_time;
			/*
			 * XXX Ensure it isn't too low - nothing lower
			 * XXX than 10 TU
			 */
			if (value < 10)
				value = 10;
			HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
			    "%s: defaulting to rdytime = %d uS\n",
			    __func__, value);
			OS_REG_WRITE(ah, AR_QRDYTIMECFG(q),
			    SM(TU_TO_USEC(value), AR_Q_RDYTIMECFG_INT) |
			    AR_Q_RDYTIMECFG_ENA);
		}
		dmisc |= SM(AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL,
			    AR_D_MISC_ARB_LOCKOUT_CNTRL);
		break;
	case HAL_TX_QUEUE_PSPOLL:
		qmisc |= AR_Q_MISC_CBR_INCR_DIS1;
		break;
	case HAL_TX_QUEUE_UAPSD:
		dmisc |= AR_D_MISC_POST_FR_BKOFF_DIS;
		break;
	default:			/* NB: silence compiler */
		break;
	}

	OS_REG_WRITE(ah, AR_QMISC(q), qmisc);
	OS_REG_WRITE(ah, AR_DMISC(q), dmisc);

	/* Setup compression scratchpad buffer */
	/* 
	 * XXX: calling this asynchronously to queue operation can
	 *      cause unexpected behavior!!!
	 */
	if (qi->tqi_physCompBuf) {
		HALASSERT(qi->tqi_type == HAL_TX_QUEUE_DATA ||
			  qi->tqi_type == HAL_TX_QUEUE_UAPSD);
		OS_REG_WRITE(ah, AR_Q_CBBS, (80 + 2*q));
		OS_REG_WRITE(ah, AR_Q_CBBA, qi->tqi_physCompBuf);
		OS_REG_WRITE(ah, AR_Q_CBC,  HAL_COMP_BUF_MAX_SIZE/1024);
		OS_REG_WRITE(ah, AR_Q0_MISC + 4*q,
			     OS_REG_READ(ah, AR_Q0_MISC + 4*q)
			     | AR_Q_MISC_QCU_COMP_EN);
	}
	
	/*
	 * Always update the secondary interrupt mask registers - this
	 * could be a new queue getting enabled in a running system or
	 * hw getting re-initialized during a reset!
	 *
	 * Since we don't differentiate between tx interrupts corresponding
	 * to individual queues - secondary tx mask regs are always unmasked;
	 * tx interrupts are enabled/disabled for all queues collectively
	 * using the primary mask reg
	 */
	if (qi->tqi_qflags & HAL_TXQ_TXOKINT_ENABLE)
		ahp->ah_txOkInterruptMask |= 1 << q;
	else
		ahp->ah_txOkInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXERRINT_ENABLE)
		ahp->ah_txErrInterruptMask |= 1 << q;
	else
		ahp->ah_txErrInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXDESCINT_ENABLE)
		ahp->ah_txDescInterruptMask |= 1 << q;
	else
		ahp->ah_txDescInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXEOLINT_ENABLE)
		ahp->ah_txEolInterruptMask |= 1 << q;
	else
		ahp->ah_txEolInterruptMask &= ~(1 << q);
	if (qi->tqi_qflags & HAL_TXQ_TXURNINT_ENABLE)
		ahp->ah_txUrnInterruptMask |= 1 << q;
	else
		ahp->ah_txUrnInterruptMask &= ~(1 << q);
	setTxQInterrupts(ah, qi);

	return AH_TRUE;
}
#undef	TU_TO_USEC
