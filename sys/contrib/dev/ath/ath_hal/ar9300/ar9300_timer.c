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
#include "ar9300/ar9300desc.h"

typedef struct gen_timer_configuation {
    u_int32_t   next_addr;
    u_int32_t   period_addr;
    u_int32_t   mode_addr;
    u_int32_t   mode_mask;
}  GEN_TIMER_CONFIGURATION;

#define AR_GEN_TIMERS2_CFG(num) \
    AR_GEN_TIMERS2_ ## num ## _NEXT, \
    AR_GEN_TIMERS2_ ## num ## _PERIOD, \
    AR_GEN_TIMERS2_MODE, \
    (1 << num)
static const GEN_TIMER_CONFIGURATION gen_timer_configuration[] =
{
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_NEXT_NDP_TIMER, AR_NDP_PERIOD, AR_TIMER_MODE, 0x0080},
    {AR_GEN_TIMERS2_CFG(0)},
    {AR_GEN_TIMERS2_CFG(1)},
    {AR_GEN_TIMERS2_CFG(2)},
    {AR_GEN_TIMERS2_CFG(3)},
    {AR_GEN_TIMERS2_CFG(4)},
    {AR_GEN_TIMERS2_CFG(5)},
    {AR_GEN_TIMERS2_CFG(6)},
    {AR_GEN_TIMERS2_CFG(7)}
};

#define AR_GENTMR_BIT(_index)   (1 << (_index))

int
ar9300_alloc_generic_timer(struct ath_hal *ah, HAL_GEN_TIMER_DOMAIN tsf)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t           i, mask;
    u_int32_t           avail_timer_start, avail_timer_end;

    if (tsf == HAL_GEN_TIMER_TSF) {
        avail_timer_start = AR_FIRST_NDP_TIMER;
        avail_timer_end = AR_GEN_TIMER_BANK_1_LEN;
    } else {
        avail_timer_start = AR_GEN_TIMER_BANK_1_LEN;
        avail_timer_end = AR_NUM_GEN_TIMERS;
    }

    /* Find the first availabe timer index */
    i = avail_timer_start;
    mask = ahp->ah_avail_gen_timers >> i;
    for ( ; mask && (i < avail_timer_end) ; mask >>= 1, i++ ) {
        if (mask & 0x1) {
            ahp->ah_avail_gen_timers &= ~(AR_GENTMR_BIT(i));

            if ((tsf == HAL_GEN_TIMER_TSF2) && !ahp->ah_enable_tsf2) {
                ahp->ah_enable_tsf2 = AH_TRUE;
                ar9300_start_tsf2(ah);
            }
            return i;
        }
    }
    return -1;
}

void ar9300_start_tsf2(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (ahp->ah_enable_tsf2) {
        /* Delay might be needed after TSF2 reset */
        OS_REG_SET_BIT(ah, AR_DIRECT_CONNECT, AR_DC_AP_STA_EN);
        OS_REG_SET_BIT(ah, AR_RESET_TSF, AR_RESET_TSF2_ONCE);
    }
}

void
ar9300_free_generic_timer(struct ath_hal *ah, int index)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ar9300_stop_generic_timer(ah, index);
    ahp->ah_avail_gen_timers |= AR_GENTMR_BIT(index);
}

void
ar9300_start_generic_timer(
    struct ath_hal *ah,
    int index,
    u_int32_t timer_next,
    u_int32_t timer_period)
{
    if ((index < AR_FIRST_NDP_TIMER) || (index >= AR_NUM_GEN_TIMERS)) {
        return;
    }

    /*
     * Program generic timer registers
     */
    OS_REG_WRITE(ah, gen_timer_configuration[index].next_addr, timer_next);
    OS_REG_WRITE(ah, gen_timer_configuration[index].period_addr, timer_period);
    OS_REG_SET_BIT(ah,
        gen_timer_configuration[index].mode_addr,
        gen_timer_configuration[index].mode_mask);

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        /*
         * Starting from Jupiter, each generic timer can select which tsf to
         * use. But we still follow the old rule, 0 - 7 use tsf and 8 - 15
         * use tsf2.
         */
        if ((index < AR_GEN_TIMER_BANK_1_LEN)) {
            OS_REG_CLR_BIT(ah, AR_MAC_PCU_GEN_TIMER_TSF_SEL, (1 << index));
        }
        else {
            OS_REG_SET_BIT(ah, AR_MAC_PCU_GEN_TIMER_TSF_SEL, (1 << index));
        }
    }

    /* Enable both trigger and thresh interrupt masks */
    OS_REG_SET_BIT(ah, AR_IMR_S5,
                   (SM(AR_GENTMR_BIT(index), AR_IMR_S5_GENTIMER_THRESH) |
                    SM(AR_GENTMR_BIT(index), AR_IMR_S5_GENTIMER_TRIG)));
}

void
ar9300_stop_generic_timer(struct ath_hal *ah, int index)
{
    if ((index < AR_FIRST_NDP_TIMER) || (index >= AR_NUM_GEN_TIMERS)) {
        return;
    }

    /*
     * Clear generic timer enable bits.
     */
    OS_REG_CLR_BIT(ah,
        gen_timer_configuration[index].mode_addr,
        gen_timer_configuration[index].mode_mask);

    /* Disable both trigger and thresh interrupt masks */
    OS_REG_CLR_BIT(ah, AR_IMR_S5,
                   (SM(AR_GENTMR_BIT(index), AR_IMR_S5_GENTIMER_THRESH) |
                    SM(AR_GENTMR_BIT(index), AR_IMR_S5_GENTIMER_TRIG)));
}

void
ar9300_get_gen_timer_interrupts(
    struct ath_hal *ah,
    u_int32_t *trigger,
    u_int32_t *thresh)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    *trigger = ahp->ah_intr_gen_timer_trigger;
    *thresh = ahp->ah_intr_gen_timer_thresh;
}
