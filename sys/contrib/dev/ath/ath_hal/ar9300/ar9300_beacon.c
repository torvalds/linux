/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"

#define TU_TO_USEC(_tu) ((_tu) << 10)
#define	ONE_EIGHTH_TU_TO_USEC(_tu8) ((_tu8) << 7)

extern u_int32_t ar9300_num_tx_pending(struct ath_hal *ah, u_int q);

/*
 * Initializes all of the hardware registers used to
 * send beacons.  Note that for station operation the
 * driver calls ar9300_set_sta_beacon_timers instead.
 */
void
ar9300_beacon_init(struct ath_hal *ah,
    u_int32_t next_beacon, u_int32_t beacon_period, 
    u_int32_t beacon_period_fraction, HAL_OPMODE opmode)
{
    u_int32_t               beacon_period_usec;

    HALASSERT(opmode == HAL_M_IBSS || opmode == HAL_M_HOSTAP);
    if (opmode == HAL_M_IBSS) {
        OS_REG_SET_BIT(ah, AR_TXCFG, AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
    }
    OS_REG_WRITE(ah, AR_NEXT_TBTT_TIMER, ONE_EIGHTH_TU_TO_USEC(next_beacon));
    OS_REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT,
        (ONE_EIGHTH_TU_TO_USEC(next_beacon) -
        ah->ah_config.ah_dma_beacon_response_time));
    OS_REG_WRITE(ah, AR_NEXT_SWBA,
        (ONE_EIGHTH_TU_TO_USEC(next_beacon) -
        ah->ah_config.ah_sw_beacon_response_time));

    beacon_period_usec =
        ONE_EIGHTH_TU_TO_USEC(beacon_period & HAL_BEACON_PERIOD_TU8);

    /* Add the fraction adjustment lost due to unit conversions. */
    beacon_period_usec += beacon_period_fraction;

    HALDEBUG(ah, HAL_DEBUG_BEACON,
        "%s: next_beacon=0x%08x, beacon_period=%d, opmode=%d, beacon_period_usec=%d\n",
        __func__, next_beacon, beacon_period, opmode, beacon_period_usec);

    OS_REG_WRITE(ah, AR_BEACON_PERIOD, beacon_period_usec);
    OS_REG_WRITE(ah, AR_DMA_BEACON_PERIOD, beacon_period_usec);
    OS_REG_WRITE(ah, AR_SWBA_PERIOD, beacon_period_usec);

    /* reset TSF if required */
    if (beacon_period & HAL_BEACON_RESET_TSF) {
        ar9300_reset_tsf(ah);
    }

    /* enable timers */
    OS_REG_SET_BIT(ah, AR_TIMER_MODE,
        AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN);
}

/*
 * Set all the beacon related bits on the h/w for stations
 * i.e. initializes the corresponding h/w timers;
 */
void
ar9300_set_sta_beacon_timers(struct ath_hal *ah, const HAL_BEACON_STATE *bs)
{
    u_int32_t next_tbtt, beaconintval, dtimperiod, beacontimeout;
    HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;

    HALASSERT(bs->bs_intval != 0);

    /* no cfp setting since h/w automatically takes care */
    OS_REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(bs->bs_nexttbtt));

    /*
     * Start the beacon timers by setting the BEACON register
     * to the beacon interval; no need to write tim offset since
     * h/w parses IEs.
     */
    OS_REG_WRITE(ah, AR_BEACON_PERIOD,
                 TU_TO_USEC(bs->bs_intval & HAL_BEACON_PERIOD));
    OS_REG_WRITE(ah, AR_DMA_BEACON_PERIOD,
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
     * Current implementation assumes sw processing of beacons -
     * assuming an interrupt is generated every beacon which
     * causes the hardware to become awake until the sw tells
     * it to go to sleep again; beacon timeout is to allow for
     * beacon jitter; cab timeout is max time to wait for cab
     * after seeing the last DTIM or MORE CAB bit
     */
#define CAB_TIMEOUT_VAL         10 /* in TU */
#define BEACON_TIMEOUT_VAL      10 /* in TU */
#define MIN_BEACON_TIMEOUT_VAL   1 /* in 1/8 TU */
#define SLEEP_SLOP               3 /* in TU */

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
    if (beaconintval == dtimperiod) {
        next_tbtt = bs->bs_nextdtim;
    } else {
        next_tbtt = bs->bs_nexttbtt;
    }

    HALDEBUG(ah, HAL_DEBUG_BEACON,
        "%s: next DTIM %d\n", __func__, bs->bs_nextdtim);
    HALDEBUG(ah, HAL_DEBUG_BEACON,
        "%s: next beacon %d\n", __func__, next_tbtt);
    HALDEBUG(ah, HAL_DEBUG_BEACON,
        "%s: beacon period %d\n", __func__, beaconintval);
    HALDEBUG(ah, HAL_DEBUG_BEACON,
        "%s: DTIM period %d\n", __func__, dtimperiod);

    OS_REG_WRITE(ah, AR_NEXT_DTIM, TU_TO_USEC(bs->bs_nextdtim - SLEEP_SLOP));
    OS_REG_WRITE(ah, AR_NEXT_TIM, TU_TO_USEC(next_tbtt - SLEEP_SLOP));

    /* cab timeout is now in 1/8 TU */
    OS_REG_WRITE(ah, AR_SLEEP1,
        SM((CAB_TIMEOUT_VAL << 3), AR_SLEEP1_CAB_TIMEOUT)
        | AR_SLEEP1_ASSUME_DTIM);

    /* beacon timeout is now in 1/8 TU */
    if (p_cap->halAutoSleepSupport) {
        beacontimeout = (BEACON_TIMEOUT_VAL << 3);
    } else {
        /*
         * Use a very small value to make sure the timeout occurs before
         * the TBTT.  In this case the chip will not go back to sleep
         * automatically, instead it will wait for the SW to explicitly
         * set it to that mode.
         */
        beacontimeout = MIN_BEACON_TIMEOUT_VAL;
    }

    OS_REG_WRITE(ah, AR_SLEEP2,
        SM(beacontimeout, AR_SLEEP2_BEACON_TIMEOUT));

    OS_REG_WRITE(ah, AR_TIM_PERIOD, TU_TO_USEC(beaconintval));
    OS_REG_WRITE(ah, AR_DTIM_PERIOD, TU_TO_USEC(dtimperiod));

    /* clear HOST AP related timers first */    
    OS_REG_CLR_BIT(ah, AR_TIMER_MODE, (AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN));

    OS_REG_SET_BIT(ah, AR_TIMER_MODE, AR_TBTT_TIMER_EN | AR_TIM_TIMER_EN
                    | AR_DTIM_TIMER_EN);

    /* TSF out of range threshold */
    OS_REG_WRITE(ah, AR_TSFOOR_THRESHOLD, bs->bs_tsfoor_threshold);

#undef CAB_TIMEOUT_VAL
#undef BEACON_TIMEOUT_VAL
#undef SLEEP_SLOP
}
