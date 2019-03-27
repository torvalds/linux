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

#include "ar5212/ar5212.h"
#include "ar5212/ar5212reg.h"
#include "ar5212/ar5212desc.h"

/*
 * Return the hardware NextTBTT in TSF
 */
uint64_t
ar5212GetNextTBTT(struct ath_hal *ah)
{
#define TU_TO_TSF(_tu)	(((uint64_t)(_tu)) << 10)
	return TU_TO_TSF(OS_REG_READ(ah, AR_TIMER0));
#undef TU_TO_TSF
}

/*
 * Initialize all of the hardware registers used to
 * send beacons.  Note that for station operation the
 * driver calls ar5212SetStaBeaconTimers instead.
 */
void
ar5212SetBeaconTimers(struct ath_hal *ah, const HAL_BEACON_TIMERS *bt)
{
	struct ath_hal_5212 *ahp = AH5212(ah);

	/*
	 * Limit the timers to their specific resolutions:
	 *
	 * + Timer 0 - 0..15 0xffff TU
	 * + Timer 1 - 0..18 0x7ffff TU/8
	 * + Timer 2 - 0..24 0x1ffffff TU/8
	 * + Timer 3 - 0..15 0xffff TU
	 */
	OS_REG_WRITE(ah, AR_TIMER0, bt->bt_nexttbtt & 0xffff);
	OS_REG_WRITE(ah, AR_TIMER1, bt->bt_nextdba & 0x7ffff);
	OS_REG_WRITE(ah, AR_TIMER2, bt->bt_nextswba & 0x1ffffff);
	/* XXX force nextatim to be non-zero? */
	OS_REG_WRITE(ah, AR_TIMER3, bt->bt_nextatim & 0xffff);
	/*
	 * Set the Beacon register after setting all timers.
	 */
	if (bt->bt_intval & AR_BEACON_RESET_TSF) {
		/*
		 * When resetting the TSF,
		 * write twice to the corresponding register; each
		 * write to the RESET_TSF bit toggles the internal
		 * signal to cause a reset of the TSF - but if the signal
		 * is left high, it will reset the TSF on the next
		 * chip reset also! writing the bit an even number
		 * of times fixes this issue
		 */
		OS_REG_WRITE(ah, AR_BEACON, AR_BEACON_RESET_TSF);
	}
	OS_REG_WRITE(ah, AR_BEACON, bt->bt_intval);
	ahp->ah_beaconInterval = (bt->bt_intval & HAL_BEACON_PERIOD);
}

/*
 * Old api for setting up beacon timer registers when
 * operating in !station mode.  Note the fixed constants
 * adjusting the DBA and SWBA timers and the fixed ATIM
 * window.
 */
void
ar5212BeaconInit(struct ath_hal *ah,
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
	switch (AH_PRIVATE(ah)->ah_opmode) {
	case HAL_M_STA:
	case HAL_M_MONITOR:
		bt.bt_nextdba = 0xffff;
		bt.bt_nextswba = 0x7ffff;
		break;
	case HAL_M_HOSTAP:
	case HAL_M_IBSS:
		bt.bt_nextdba = (next_beacon -
		    ah->ah_config.ah_dma_beacon_response_time) << 3; /* 1/8 TU */
		bt.bt_nextswba = (next_beacon -
		    ah->ah_config.ah_sw_beacon_response_time) << 3;	/* 1/8 TU */
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
	ar5212SetBeaconTimers(ah, &bt);
}

void
ar5212ResetStaBeaconTimers(struct ath_hal *ah)
{
	uint32_t val;

	OS_REG_WRITE(ah, AR_TIMER0, 0);		/* no beacons */
	val = OS_REG_READ(ah, AR_STA_ID1);
	val |= AR_STA_ID1_PWR_SAV;		/* XXX */
	/* tell the h/w that the associated AP is not PCF capable */
	OS_REG_WRITE(ah, AR_STA_ID1,
		val & ~(AR_STA_ID1_USE_DEFANT | AR_STA_ID1_PCF));
	OS_REG_WRITE(ah, AR_BEACON, AR_BEACON_PERIOD);
}

/*
 * Set all the beacon related bits on the h/w for stations
 * i.e. initializes the corresponding h/w timers;
 * also tells the h/w whether to anticipate PCF beacons
 */
void
ar5212SetStaBeaconTimers(struct ath_hal *ah, const HAL_BEACON_STATE *bs)
{
	struct ath_hal_5212 *ahp = AH5212(ah);
	uint32_t nextTbtt, nextdtim,beaconintval, dtimperiod;

	HALASSERT(bs->bs_intval != 0);
	/* if the AP will do PCF */
	if (bs->bs_cfpmaxduration != 0) {
		/* tell the h/w that the associated AP is PCF capable */
		OS_REG_WRITE(ah, AR_STA_ID1,
			OS_REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PCF);

		/* set CFP_PERIOD(1.024ms) register */
		OS_REG_WRITE(ah, AR_CFP_PERIOD, bs->bs_cfpperiod);

		/* set CFP_DUR(1.024ms) register to max cfp duration */
		OS_REG_WRITE(ah, AR_CFP_DUR, bs->bs_cfpmaxduration);

		/* set TIMER2(128us) to anticipated time of next CFP */
		OS_REG_WRITE(ah, AR_TIMER2, bs->bs_cfpnext << 3);
	} else {
		/* tell the h/w that the associated AP is not PCF capable */
		OS_REG_WRITE(ah, AR_STA_ID1,
			OS_REG_READ(ah, AR_STA_ID1) &~ AR_STA_ID1_PCF);
	}

	/*
	 * Set TIMER0(1.024ms) to the anticipated time of the next beacon.
	 */
	OS_REG_WRITE(ah, AR_TIMER0, bs->bs_nexttbtt);

	/*
	 * Start the beacon timers by setting the BEACON register
	 * to the beacon interval; also write the tim offset which
	 * we should know by now.  The code, in ar5211WriteAssocid,
	 * also sets the tim offset once the AID is known which can
	 * be left as such for now.
	 */
	OS_REG_WRITE(ah, AR_BEACON, 
		(OS_REG_READ(ah, AR_BEACON) &~ (AR_BEACON_PERIOD|AR_BEACON_TIM))
		| SM(bs->bs_intval, AR_BEACON_PERIOD)
		| SM(bs->bs_timoffset ? bs->bs_timoffset + 4 : 0, AR_BEACON_TIM)
	);

	/*
	 * Configure the BMISS interrupt.  Note that we
	 * assume the caller blocks interrupts while enabling
	 * the threshold.
	 */
	HALASSERT(bs->bs_bmissthreshold <= MS(0xffffffff, AR_RSSI_THR_BM_THR));
	ahp->ah_rssiThr = (ahp->ah_rssiThr &~ AR_RSSI_THR_BM_THR)
			| SM(bs->bs_bmissthreshold, AR_RSSI_THR_BM_THR);
	OS_REG_WRITE(ah, AR_RSSI_THR, ahp->ah_rssiThr);

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

	OS_REG_WRITE(ah, AR_SLEEP1,
		  SM((nextdtim - SLEEP_SLOP) << 3, AR_SLEEP1_NEXT_DTIM)
		| SM(CAB_TIMEOUT_VAL, AR_SLEEP1_CAB_TIMEOUT)
		| AR_SLEEP1_ASSUME_DTIM
		| AR_SLEEP1_ENH_SLEEP_ENA
	);
	OS_REG_WRITE(ah, AR_SLEEP2,
		  SM((nextTbtt - SLEEP_SLOP) << 3, AR_SLEEP2_NEXT_TIM)
		| SM(BEACON_TIMEOUT_VAL, AR_SLEEP2_BEACON_TIMEOUT)
	);
	OS_REG_WRITE(ah, AR_SLEEP3,
		  SM(beaconintval, AR_SLEEP3_TIM_PERIOD)
		| SM(dtimperiod, AR_SLEEP3_DTIM_PERIOD)
	);
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
