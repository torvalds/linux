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

#include "ar5416/ar5416.h"
#include "ar5416/ar5416reg.h"
#include "ar5416/ar5416phy.h"

#define TU_TO_USEC(_tu)		((_tu) << 10)
#define	ONE_EIGHTH_TU_TO_USEC(_tu8)	((_tu8) << 7)

/*
 * Return the hardware NextTBTT in TSF
 */
uint64_t
ar5416GetNextTBTT(struct ath_hal *ah)
{
	return OS_REG_READ(ah, AR_NEXT_TBTT);
}

/*
 * Initialize all of the hardware registers used to
 * send beacons.  Note that for station operation the
 * driver calls ar5416SetStaBeaconTimers instead.
 */
void
ar5416SetBeaconTimers(struct ath_hal *ah, const HAL_BEACON_TIMERS *bt)
{
	uint32_t bperiod;
	struct ath_hal_5212 *ahp = AH5212(ah);

	OS_REG_WRITE(ah, AR_NEXT_TBTT, TU_TO_USEC(bt->bt_nexttbtt));
	OS_REG_WRITE(ah, AR_NEXT_DBA, ONE_EIGHTH_TU_TO_USEC(bt->bt_nextdba));
	OS_REG_WRITE(ah, AR_NEXT_SWBA, ONE_EIGHTH_TU_TO_USEC(bt->bt_nextswba));
	OS_REG_WRITE(ah, AR_NEXT_NDP, TU_TO_USEC(bt->bt_nextatim));

	bperiod = TU_TO_USEC(bt->bt_intval & HAL_BEACON_PERIOD);
	ahp->ah_beaconInterval = bt->bt_intval & HAL_BEACON_PERIOD;
	OS_REG_WRITE(ah, AR5416_BEACON_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_DBA_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_SWBA_PERIOD, bperiod);
	OS_REG_WRITE(ah, AR_NDP_PERIOD, bperiod);

	/*
	 * Reset TSF if required.
	 */
	if (bt->bt_intval & AR_BEACON_RESET_TSF)
		ar5416ResetTsf(ah);

	/* enable timers */
	/* NB: flags == 0 handled specially for backwards compatibility */
	OS_REG_SET_BIT(ah, AR_TIMER_MODE,
	    bt->bt_flags != 0 ? bt->bt_flags :
		AR_TIMER_MODE_TBTT | AR_TIMER_MODE_DBA | AR_TIMER_MODE_SWBA);
}

/*
 * Initializes all of the hardware registers used to
 * send beacons.  Note that for station operation the
 * driver calls ar5212SetStaBeaconTimers instead.
 */
void
ar5416BeaconInit(struct ath_hal *ah,
	uint32_t next_beacon, uint32_t beacon_period)
{
	HAL_BEACON_TIMERS bt;

	bt.bt_nexttbtt = next_beacon;
	/* 
	 * TIMER1: in AP/adhoc mode this controls the DMA beacon
	 * alert timer; otherwise it controls the next wakeup time.
	 * TIMER2: in AP mode, it controls the SBA beacon alert
	 * interrupt; otherwise it sets the start of the next CFP.
	 */
	bt.bt_flags = 0;
	switch (AH_PRIVATE(ah)->ah_opmode) {
	case HAL_M_STA:
	case HAL_M_MONITOR:
		bt.bt_nextdba = 0xffff;
		bt.bt_nextswba = 0x7ffff;
		bt.bt_flags |= AR_TIMER_MODE_TBTT;
		break;
	case HAL_M_IBSS:
		OS_REG_SET_BIT(ah, AR_TXCFG, AR_TXCFG_ATIM_TXPOLICY);
		bt.bt_flags |= AR_TIMER_MODE_NDP;
		/* fall thru... */
	case HAL_M_HOSTAP:
		bt.bt_nextdba = (next_beacon -
		    ah->ah_config.ah_dma_beacon_response_time) << 3;	/* 1/8 TU */
		bt.bt_nextswba = (next_beacon -
		    ah->ah_config.ah_sw_beacon_response_time) << 3;	/* 1/8 TU */
		bt.bt_flags |= AR_TIMER_MODE_TBTT
			    |  AR_TIMER_MODE_DBA
			    |  AR_TIMER_MODE_SWBA;
		break;
	}
	/*
	 * Set the ATIM window 
	 * Our hardware does not support an ATIM window of 0
	 * (beacons will not work).  If the ATIM windows is 0,
	 * force it to 1.
	 */
	bt.bt_nextatim = next_beacon + 1;
	bt.bt_intval = beacon_period &
		(AR_BEACON_PERIOD | AR_BEACON_RESET_TSF | AR_BEACON_EN);
	ar5416SetBeaconTimers(ah, &bt);
}

#define AR_BEACON_PERIOD_MAX	0xffff

void
ar5416ResetStaBeaconTimers(struct ath_hal *ah)
{
	uint32_t val;

	OS_REG_WRITE(ah, AR_NEXT_TBTT, 0);		/* no beacons */
	val = OS_REG_READ(ah, AR_STA_ID1);
	val |= AR_STA_ID1_PWR_SAV;		/* XXX */
	/* tell the h/w that the associated AP is not PCF capable */
	OS_REG_WRITE(ah, AR_STA_ID1,
		val & ~(AR_STA_ID1_USE_DEFANT | AR_STA_ID1_PCF));
	OS_REG_WRITE(ah, AR5416_BEACON_PERIOD, AR_BEACON_PERIOD_MAX);
	OS_REG_WRITE(ah, AR_DBA_PERIOD, AR_BEACON_PERIOD_MAX);
}

/*
 * Set all the beacon related bits on the h/w for stations
 * i.e. initializes the corresponding h/w timers;
 * also tells the h/w whether to anticipate PCF beacons
 */
void
ar5416SetStaBeaconTimers(struct ath_hal *ah, const HAL_BEACON_STATE *bs)
{
	uint32_t nextTbtt, nextdtim,beaconintval, dtimperiod;

	HALASSERT(bs->bs_intval != 0);
	
	/* NB: no cfp setting since h/w automatically takes care */

	OS_REG_WRITE(ah, AR_NEXT_TBTT, TU_TO_USEC(bs->bs_nexttbtt));

	/*
	 * Start the beacon timers by setting the BEACON register
	 * to the beacon interval; no need to write tim offset since
	 * h/w parses IEs.
	 */
	OS_REG_WRITE(ah, AR5416_BEACON_PERIOD,
			 TU_TO_USEC(bs->bs_intval & HAL_BEACON_PERIOD));
	OS_REG_WRITE(ah, AR_DBA_PERIOD,
			 TU_TO_USEC(bs->bs_intval & HAL_BEACON_PERIOD));

	/*
	 * Configure the BMISS interrupt.  Note that we
	 * assume the caller blocks interrupts while enabling
	 * the threshold.
	 */
	HALASSERT(bs->bs_bmissthreshold <=
		(AR_RSSI_THR_BM_THR >> AR_RSSI_THR_BM_THR_S));
	OS_REG_RMW_FIELD(ah, AR_RSSI_THR,
		AR_RSSI_THR_BM_THR, bs->bs_bmissthreshold);

	/*
	 * Program the sleep registers to correlate with the beacon setup.
	 */

	/*
	 * Oahu beacons timers on the station were used for power
	 * save operation (waking up in anticipation of a beacon)
	 * and any CFP function; Venice does sleep/power-save timers
	 * differently - so this is the right place to set them up;
	 * don't think the beacon timers are used by venice sta hw
	 * for any useful purpose anymore
	 * Setup venice's sleep related timers
	 * Current implementation assumes sw processing of beacons -
	 *   assuming an interrupt is generated every beacon which
	 *   causes the hardware to become awake until the sw tells
	 *   it to go to sleep again; beacon timeout is to allow for
	 *   beacon jitter; cab timeout is max time to wait for cab
	 *   after seeing the last DTIM or MORE CAB bit
	 */

/*
 * I've bumped these to 30TU for now.
 *
 * Some APs (AR933x/AR934x?) in 2GHz especially seem to not always
 * transmit beacon frames at exactly the right times and with it set
 * to 10TU, the NIC starts not waking up at the right times to hear
 * these slightly-larger-jitering beacons.  It also never recovers
 * from that (it doesn't resync? I'm not sure.)
 *
 * So for now bump this to 30TU.  Ideally we'd cap this based on
 * the beacon interval so the sum of CAB+BEACON timeouts never
 * exceeded the beacon interval.
 *
 * Now, since we're doing all the math in the ath(4) driver in TU
 * rather than TSF, we may be seeing the result of dumb rounding
 * errors causing the jitter to actually be a much bigger problem.
 * I'll have to investigate that with a fine tooth comb.
 */
#define CAB_TIMEOUT_VAL     10 /* in TU */
#define BEACON_TIMEOUT_VAL  10 /* in TU */
#define SLEEP_SLOP          3  /* in TU */

	/*
	 * For max powersave mode we may want to sleep for longer than a
	 * beacon period and not want to receive all beacons; modify the
	 * timers accordingly; make sure to align the next TIM to the
	 * next DTIM if we decide to wake for DTIMs only
	 */
	beaconintval = bs->bs_intval & HAL_BEACON_PERIOD;
	HALASSERT(beaconintval != 0);
	if (bs->bs_sleepduration > beaconintval) {
		HALASSERT(roundup(bs->bs_sleepduration, beaconintval) ==
				bs->bs_sleepduration);
		beaconintval = bs->bs_sleepduration;
	}
	dtimperiod = bs->bs_dtimperiod;
	if (bs->bs_sleepduration > dtimperiod) {
		HALASSERT(dtimperiod == 0 ||
			roundup(bs->bs_sleepduration, dtimperiod) ==
				bs->bs_sleepduration);
		dtimperiod = bs->bs_sleepduration;
	}
	HALASSERT(beaconintval <= dtimperiod);
	if (beaconintval == dtimperiod)
		nextTbtt = bs->bs_nextdtim;
	else
		nextTbtt = bs->bs_nexttbtt;
	nextdtim = bs->bs_nextdtim;

	OS_REG_WRITE(ah, AR_NEXT_DTIM,
		TU_TO_USEC(bs->bs_nextdtim - SLEEP_SLOP));
	OS_REG_WRITE(ah, AR_NEXT_TIM, TU_TO_USEC(nextTbtt - SLEEP_SLOP));

	/* cab timeout is now in 1/8 TU */
	OS_REG_WRITE(ah, AR5416_SLEEP1,
		SM((CAB_TIMEOUT_VAL << 3), AR5416_SLEEP1_CAB_TIMEOUT)
		| AR5416_SLEEP1_ASSUME_DTIM);

	/* XXX autosleep? Use min beacon timeout; check ath9k -adrian */
	/* beacon timeout is now in 1/8 TU */
	OS_REG_WRITE(ah, AR5416_SLEEP2,
		SM((BEACON_TIMEOUT_VAL << 3), AR5416_SLEEP2_BEACON_TIMEOUT));

	/* TIM_PERIOD and DTIM_PERIOD are now in uS. */
	OS_REG_WRITE(ah, AR_TIM_PERIOD, TU_TO_USEC(beaconintval));
	OS_REG_WRITE(ah, AR_DTIM_PERIOD, TU_TO_USEC(dtimperiod));

	OS_REG_SET_BIT(ah, AR_TIMER_MODE,
	     AR_TIMER_MODE_TBTT | AR_TIMER_MODE_TIM | AR_TIMER_MODE_DTIM);

#define	HAL_TSFOOR_THRESHOLD	0x00004240 /* TSF OOR threshold (16k us) */

	/* TSF out of range threshold */
//	OS_REG_WRITE(ah, AR_TSFOOR_THRESHOLD, bs->bs_tsfoor_threshold);
	OS_REG_WRITE(ah, AR_TSFOOR_THRESHOLD, HAL_TSFOOR_THRESHOLD);

	HALDEBUG(ah, HAL_DEBUG_BEACON, "%s: next DTIM %d\n",
	    __func__, bs->bs_nextdtim);
	HALDEBUG(ah, HAL_DEBUG_BEACON, "%s: next beacon %d\n",
	    __func__, nextTbtt);
	HALDEBUG(ah, HAL_DEBUG_BEACON, "%s: beacon period %d\n",
	    __func__, beaconintval);
	HALDEBUG(ah, HAL_DEBUG_BEACON, "%s: DTIM period %d\n",
	    __func__, dtimperiod);
#undef CAB_TIMEOUT_VAL
#undef BEACON_TIMEOUT_VAL
#undef SLEEP_SLOP
}
