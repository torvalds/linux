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
#include "ah_devid.h"
#ifdef AH_DEBUG
#include "ah_desc.h"                    /* NB: for HAL_PHYERR* */
#endif

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"
#include "ar9300/ar9300desc.h"

static u_int32_t ar9300_read_loc_timer(struct ath_hal *ah);

void
ar9300_get_hw_hangs(struct ath_hal *ah, hal_hw_hangs_t *hangs)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    *hangs = 0;

    if (ar9300_get_capability(ah, HAL_CAP_BB_RIFS_HANG, 0, AH_NULL) == HAL_OK) {
        *hangs |= HAL_RIFS_BB_HANG_WAR;
    }
    if (ar9300_get_capability(ah, HAL_CAP_BB_DFS_HANG, 0, AH_NULL) == HAL_OK) {
        *hangs |= HAL_DFS_BB_HANG_WAR;
    }
    if (ar9300_get_capability(ah, HAL_CAP_BB_RX_CLEAR_STUCK_HANG, 0, AH_NULL)
        == HAL_OK)
    {
        *hangs |= HAL_RX_STUCK_LOW_BB_HANG_WAR;
    }
    if (ar9300_get_capability(ah, HAL_CAP_MAC_HANG, 0, AH_NULL) == HAL_OK) {
        *hangs |= HAL_MAC_HANG_WAR;
    }
    if (ar9300_get_capability(ah, HAL_CAP_PHYRESTART_CLR_WAR, 0, AH_NULL)
        == HAL_OK)
    {
        *hangs |= HAL_PHYRESTART_CLR_WAR;
    }

    ahp->ah_hang_wars = *hangs;
}

/*
 * XXX FreeBSD: the HAL version of ath_hal_mac_usec() knows about
 * HT20, HT40, fast-clock, turbo mode, etc.
 */
static u_int
ar9300_mac_to_usec(struct ath_hal *ah, u_int clks)
{
#if 0
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;

    if (chan && IEEE80211_IS_CHAN_HT40(chan)) {
        return (ath_hal_mac_usec(ah, clks) / 2);
    } else {
        return (ath_hal_mac_usec(ah, clks));
    }
#endif
    return (ath_hal_mac_usec(ah, clks));
}

u_int
ar9300_mac_to_clks(struct ath_hal *ah, u_int usecs)
{
#if 0
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;

    if (chan && IEEE80211_IS_CHAN_HT40(chan)) {
        return (ath_hal_mac_clks(ah, usecs) * 2);
    } else {
        return (ath_hal_mac_clks(ah, usecs));
    }
#endif
    return (ath_hal_mac_clks(ah, usecs));
}

void
ar9300_get_mac_address(struct ath_hal *ah, u_int8_t *mac)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    OS_MEMCPY(mac, ahp->ah_macaddr, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar9300_set_mac_address(struct ath_hal *ah, const u_int8_t *mac)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    OS_MEMCPY(ahp->ah_macaddr, mac, IEEE80211_ADDR_LEN);
    return AH_TRUE;
}

void
ar9300_get_bss_id_mask(struct ath_hal *ah, u_int8_t *mask)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    OS_MEMCPY(mask, ahp->ah_bssid_mask, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar9300_set_bss_id_mask(struct ath_hal *ah, const u_int8_t *mask)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* save it since it must be rewritten on reset */
    OS_MEMCPY(ahp->ah_bssid_mask, mask, IEEE80211_ADDR_LEN);

    OS_REG_WRITE(ah, AR_BSSMSKL, LE_READ_4(ahp->ah_bssid_mask));
    OS_REG_WRITE(ah, AR_BSSMSKU, LE_READ_2(ahp->ah_bssid_mask + 4));
    return AH_TRUE;
}

/*
 * Attempt to change the cards operating regulatory domain to the given value
 * Returns: A_EINVAL for an unsupported regulatory domain.
 *          A_HARDWARE for an unwritable EEPROM or bad EEPROM version
 */
HAL_BOOL
ar9300_set_regulatory_domain(struct ath_hal *ah,
        u_int16_t reg_domain, HAL_STATUS *status)
{
    HAL_STATUS ecode;

    if (AH_PRIVATE(ah)->ah_currentRD == 0) {
        AH_PRIVATE(ah)->ah_currentRD = reg_domain;
        return AH_TRUE;
    }
    ecode = HAL_EIO;

#if 0
bad:
#endif
    if (status) {
        *status = ecode;
    }
    return AH_FALSE;
}

/*
 * Return the wireless modes (a,b,g,t) supported by hardware.
 *
 * This value is what is actually supported by the hardware
 * and is unaffected by regulatory/country code settings.
 *
 */
u_int
ar9300_get_wireless_modes(struct ath_hal *ah)
{
    return AH_PRIVATE(ah)->ah_caps.halWirelessModes;
}

/*
 * Set the interrupt and GPIO values so the ISR can disable RF
 * on a switch signal.  Assumes GPIO port and interrupt polarity
 * are set prior to call.
 */
void
ar9300_enable_rf_kill(struct ath_hal *ah)
{
    /* TODO - can this really be above the hal on the GPIO interface for
     * TODO - the client only?
     */
    struct ath_hal_9300    *ahp = AH9300(ah);

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
    	/* Check RF kill GPIO before set/clear RFSILENT bits. */
    	if (ar9300_gpio_get(ah, ahp->ah_gpio_select) == ahp->ah_polarity) {
            OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_RFSILENT), 
                           AR_RFSILENT_FORCE);
            OS_REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
        }
        else {
            OS_REG_CLR_BIT(ah, AR_HOSTIF_REG(ah, AR_RFSILENT), 
                           AR_RFSILENT_FORCE);
            OS_REG_CLR_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
        }
    }
    else {
        /* Connect rfsilent_bb_l to baseband */
        OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL),
            AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);

        /* Set input mux for rfsilent_bb_l to GPIO #0 */
        OS_REG_CLR_BIT(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX2),
            AR_GPIO_INPUT_MUX2_RFSILENT);
        OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX2),
            (ahp->ah_gpio_select & 0x0f) << 4);

        /*
         * Configure the desired GPIO port for input and
         * enable baseband rf silence
         */
        ath_hal_gpioCfgInput(ah, ahp->ah_gpio_select);
        OS_REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
    }

    /*
     * If radio disable switch connection to GPIO bit x is enabled
     * program GPIO interrupt.
     * If rfkill bit on eeprom is 1, setupeeprommap routine has already
     * verified that it is a later version of eeprom, it has a place for
     * rfkill bit and it is set to 1, indicating that GPIO bit x hardware
     * connection is present.
     */
     /*
      * RFKill uses polling not interrupt,
      * disable interrupt to avoid Eee PC 2.6.21.4 hang up issue
      */
    if (ath_hal_hasrfkill_int(ah)) {
        if (ahp->ah_gpio_bit == ar9300_gpio_get(ah, ahp->ah_gpio_select)) {
            /* switch already closed, set to interrupt upon open */
            ar9300_gpio_set_intr(ah, ahp->ah_gpio_select, !ahp->ah_gpio_bit);
        } else {
            ar9300_gpio_set_intr(ah, ahp->ah_gpio_select, ahp->ah_gpio_bit);
        }
    }
}

/*
 * Change the LED blinking pattern to correspond to the connectivity
 */
void
ar9300_set_led_state(struct ath_hal *ah, HAL_LED_STATE state)
{
    static const u_int32_t ledbits[8] = {
        AR_CFG_LED_ASSOC_NONE,     /* HAL_LED_RESET */
        AR_CFG_LED_ASSOC_PENDING,  /* HAL_LED_INIT  */
        AR_CFG_LED_ASSOC_PENDING,  /* HAL_LED_READY */
        AR_CFG_LED_ASSOC_PENDING,  /* HAL_LED_SCAN  */
        AR_CFG_LED_ASSOC_PENDING,  /* HAL_LED_AUTH  */
        AR_CFG_LED_ASSOC_ACTIVE,   /* HAL_LED_ASSOC */
        AR_CFG_LED_ASSOC_ACTIVE,   /* HAL_LED_RUN   */
        AR_CFG_LED_ASSOC_NONE,
    };

    OS_REG_RMW_FIELD(ah, AR_CFG_LED, AR_CFG_LED_ASSOC_CTL, ledbits[state]);
}

/*
 * Sets the Power LED on the cardbus without affecting the Network LED.
 */
void
ar9300_set_power_led_state(struct ath_hal *ah, u_int8_t enabled)
{
    u_int32_t    val;

    val = enabled ? AR_CFG_LED_MODE_POWER_ON : AR_CFG_LED_MODE_POWER_OFF;
    OS_REG_RMW_FIELD(ah, AR_CFG_LED, AR_CFG_LED_POWER, val);
}

/*
 * Sets the Network LED on the cardbus without affecting the Power LED.
 */
void
ar9300_set_network_led_state(struct ath_hal *ah, u_int8_t enabled)
{
    u_int32_t    val;

    val = enabled ? AR_CFG_LED_MODE_NETWORK_ON : AR_CFG_LED_MODE_NETWORK_OFF;
    OS_REG_RMW_FIELD(ah, AR_CFG_LED, AR_CFG_LED_NETWORK, val);
}

/*
 * Change association related fields programmed into the hardware.
 * Writing a valid BSSID to the hardware effectively enables the hardware
 * to synchronize its TSF to the correct beacons and receive frames coming
 * from that BSSID. It is called by the SME JOIN operation.
 */
void
ar9300_write_associd(struct ath_hal *ah, const u_int8_t *bssid,
    u_int16_t assoc_id)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* save bssid and assoc_id for restore on reset */
    OS_MEMCPY(ahp->ah_bssid, bssid, IEEE80211_ADDR_LEN);
    ahp->ah_assoc_id = assoc_id;

    OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
    OS_REG_WRITE(ah, AR_BSS_ID1, LE_READ_2(ahp->ah_bssid + 4) |
                                 ((assoc_id & 0x3fff) << AR_BSS_ID1_AID_S));
}

/*
 * Get the current hardware tsf for stamlme
 */
u_int64_t
ar9300_get_tsf64(struct ath_hal *ah)
{
    u_int64_t tsf;

    /* XXX sync multi-word read? */
    tsf = OS_REG_READ(ah, AR_TSF_U32);
    tsf = (tsf << 32) | OS_REG_READ(ah, AR_TSF_L32);
    return tsf;
}

void
ar9300_set_tsf64(struct ath_hal *ah, u_int64_t tsf)
{
    OS_REG_WRITE(ah, AR_TSF_L32, (tsf & 0xffffffff));
    OS_REG_WRITE(ah, AR_TSF_U32, ((tsf >> 32) & 0xffffffff));
}

/*
 * Get the current hardware tsf for stamlme
 */
u_int32_t
ar9300_get_tsf32(struct ath_hal *ah)
{
    return OS_REG_READ(ah, AR_TSF_L32);
}

u_int32_t
ar9300_get_tsf2_32(struct ath_hal *ah)
{
    return OS_REG_READ(ah, AR_TSF2_L32);
}

/*
 * Reset the current hardware tsf for stamlme.
 */
void
ar9300_reset_tsf(struct ath_hal *ah)
{
    int count;

    count = 0;
    while (OS_REG_READ(ah, AR_SLP32_MODE) & AR_SLP32_TSF_WRITE_STATUS) {
        count++;
        if (count > 10) {
            HALDEBUG(ah, HAL_DEBUG_RESET,
                "%s: AR_SLP32_TSF_WRITE_STATUS limit exceeded\n", __func__);
            break;
        }
        OS_DELAY(10);
    }
    OS_REG_WRITE(ah, AR_RESET_TSF, AR_RESET_TSF_ONCE);
}

/*
 * Set or clear hardware basic rate bit
 * Set hardware basic rate set if basic rate is found
 * and basic rate is equal or less than 2Mbps
 */
void
ar9300_set_basic_rate(struct ath_hal *ah, HAL_RATE_SET *rs)
{
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    u_int32_t reg;
    u_int8_t xset;
    int i;

    if (chan == AH_NULL || !IEEE80211_IS_CHAN_CCK(chan)) {
        return;
    }
    xset = 0;
    for (i = 0; i < rs->rs_count; i++) {
        u_int8_t rset = rs->rs_rates[i];
        /* Basic rate defined? */
        if ((rset & 0x80) && (rset &= 0x7f) >= xset) {
            xset = rset;
        }
    }
    /*
     * Set the h/w bit to reflect whether or not the basic
     * rate is found to be equal or less than 2Mbps.
     */
    reg = OS_REG_READ(ah, AR_STA_ID1);
    if (xset && xset / 2 <= 2) {
        OS_REG_WRITE(ah, AR_STA_ID1, reg | AR_STA_ID1_BASE_RATE_11B);
    } else {
        OS_REG_WRITE(ah, AR_STA_ID1, reg &~ AR_STA_ID1_BASE_RATE_11B);
    }
}

/*
 * Grab a semi-random value from hardware registers - may not
 * change often
 */
u_int32_t
ar9300_get_random_seed(struct ath_hal *ah)
{
    u_int32_t nf;

    nf = (OS_REG_READ(ah, AR_PHY(25)) >> 19) & 0x1ff;
    if (nf & 0x100) {
        nf = 0 - ((nf ^ 0x1ff) + 1);
    }
    return (OS_REG_READ(ah, AR_TSF_U32) ^
        OS_REG_READ(ah, AR_TSF_L32) ^ nf);
}

/*
 * Detect if our card is present
 */
HAL_BOOL
ar9300_detect_card_present(struct ath_hal *ah)
{
    u_int16_t mac_version, mac_rev;
    u_int32_t v;

    /*
     * Read the Silicon Revision register and compare that
     * to what we read at attach time.  If the same, we say
     * a card/device is present.
     */
    v = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_SREV)) & AR_SREV_ID;
    if (v == 0xFF) {
        /* new SREV format */
        v = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_SREV));
        /*
         * Include 6-bit Chip Type (masked to 0) to differentiate
         * from pre-Sowl versions
         */
        mac_version = (v & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
        mac_rev = MS(v, AR_SREV_REVISION2);
    } else {
        mac_version = MS(v, AR_SREV_VERSION);
        mac_rev = v & AR_SREV_REVISION;
    }
    return (AH_PRIVATE(ah)->ah_macVersion == mac_version &&
            AH_PRIVATE(ah)->ah_macRev == mac_rev);
}

/*
 * Update MIB Counters
 */
void
ar9300_update_mib_mac_stats(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_MIB_STATS* stats = &ahp->ah_stats.ast_mibstats;

    stats->ackrcv_bad += OS_REG_READ(ah, AR_ACK_FAIL);
    stats->rts_bad    += OS_REG_READ(ah, AR_RTS_FAIL);
    stats->fcs_bad    += OS_REG_READ(ah, AR_FCS_FAIL);
    stats->rts_good   += OS_REG_READ(ah, AR_RTS_OK);
    stats->beacons    += OS_REG_READ(ah, AR_BEACON_CNT);
}

void
ar9300_get_mib_mac_stats(struct ath_hal *ah, HAL_MIB_STATS* stats)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_MIB_STATS* istats = &ahp->ah_stats.ast_mibstats;

    stats->ackrcv_bad = istats->ackrcv_bad;
    stats->rts_bad    = istats->rts_bad;
    stats->fcs_bad    = istats->fcs_bad;
    stats->rts_good   = istats->rts_good;
    stats->beacons    = istats->beacons;
}

/*
 * Detect if the HW supports spreading a CCK signal on channel 14
 */
HAL_BOOL
ar9300_is_japan_channel_spread_supported(struct ath_hal *ah)
{
    return AH_TRUE;
}

/*
 * Get the rssi of frame curently being received.
 */
u_int32_t
ar9300_get_cur_rssi(struct ath_hal *ah)
{
    /* XXX return (OS_REG_READ(ah, AR_PHY_CURRENT_RSSI) & 0xff); */
    /* get combined RSSI */
    return (OS_REG_READ(ah, AR_PHY_RSSI_3) & 0xff);
}

#if ATH_GEN_RANDOMNESS
/*
 * Get the rssi value from BB on ctl chain0.
 */
u_int32_t
ar9300_get_rssi_chain0(struct ath_hal *ah)
{
    /* get ctl chain0 RSSI */
    return OS_REG_READ(ah, AR_PHY_RSSI_0) & 0xff;
}
#endif

u_int
ar9300_get_def_antenna(struct ath_hal *ah)
{
    return (OS_REG_READ(ah, AR_DEF_ANTENNA) & 0x7);
}

/* Setup coverage class */
void
ar9300_set_coverage_class(struct ath_hal *ah, u_int8_t coverageclass, int now)
{
}

void
ar9300_set_def_antenna(struct ath_hal *ah, u_int antenna)
{
    OS_REG_WRITE(ah, AR_DEF_ANTENNA, (antenna & 0x7));
}

HAL_BOOL
ar9300_set_antenna_switch(struct ath_hal *ah,
    HAL_ANT_SETTING settings, const struct ieee80211_channel *chan,
    u_int8_t *tx_chainmask, u_int8_t *rx_chainmask, u_int8_t *antenna_cfgd)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /*
     * Owl does not support diversity or changing antennas.
     *
     * Instead this API and function are defined differently for AR9300.
     * To support Tablet PC's, this interface allows the system
     * to dramatically reduce the TX power on a particular chain.
     *
     * Based on the value of (redefined) diversity_control, the
     * reset code will decrease power on chain 0 or chain 1/2.
     *
     * Based on the value of bit 0 of antenna_switch_swap,
     * the mapping between OID call and chain is defined as:
     *  0:  map A -> 0, B -> 1;
     *  1:  map A -> 1, B -> 0;
     *
     * NOTE:
     *   The devices that use this OID should use a tx_chain_mask and
     *   tx_chain_select_legacy setting of 5 or 3 if ANTENNA_FIXED_B is
     *   used in order to ensure an active transmit antenna.  This
     *   API will allow the host to turn off the only transmitting
     *   antenna to ensure the antenna closest to the user's body is
     *   powered-down.
     */
    /*
     * Set antenna control for use during reset sequence by
     * ar9300_decrease_chain_power()
     */
    ahp->ah_diversity_control = settings;

    return AH_TRUE;
}

HAL_BOOL
ar9300_is_sleep_after_beacon_broken(struct ath_hal *ah)
{
    return AH_TRUE;
}

HAL_BOOL
ar9300_set_slot_time(struct ath_hal *ah, u_int us)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    if (us < HAL_SLOT_TIME_9 || us > ar9300_mac_to_usec(ah, 0xffff)) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: bad slot time %u\n", __func__, us);
        ahp->ah_slot_time = (u_int) -1;  /* restore default handling */
        return AH_FALSE;
    } else {
        /* convert to system clocks */
        OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, ar9300_mac_to_clks(ah, us));
        ahp->ah_slot_time = us;
        return AH_TRUE;
    }
}

HAL_BOOL
ar9300_set_ack_timeout(struct ath_hal *ah, u_int us)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (us > ar9300_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_ACK))) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: bad ack timeout %u\n", __func__, us);
        ahp->ah_ack_timeout = (u_int) -1; /* restore default handling */
        return AH_FALSE;
    } else {
        /* convert to system clocks */
        OS_REG_RMW_FIELD(ah,
            AR_TIME_OUT, AR_TIME_OUT_ACK, ar9300_mac_to_clks(ah, us));
        ahp->ah_ack_timeout = us;
        return AH_TRUE;
    }
}

u_int
ar9300_get_ack_timeout(struct ath_hal *ah)
{
    u_int clks = MS(OS_REG_READ(ah, AR_TIME_OUT), AR_TIME_OUT_ACK);
    return ar9300_mac_to_usec(ah, clks);      /* convert from system clocks */
}

HAL_STATUS
ar9300_set_quiet(struct ath_hal *ah, u_int32_t period, u_int32_t duration,
                 u_int32_t next_start, HAL_QUIET_FLAG flag)
{
#define	TU_TO_USEC(_tu)		((_tu) << 10)
    HAL_STATUS status = HAL_EIO;
    u_int32_t tsf = 0, j, next_start_us = 0;
    if (flag & HAL_QUIET_ENABLE) {
        for (j = 0; j < 2; j++) {
            next_start_us = TU_TO_USEC(next_start);
            tsf = OS_REG_READ(ah, AR_TSF_L32);
            if ((!next_start) || (flag & HAL_QUIET_ADD_CURRENT_TSF)) {
                next_start_us += tsf;
            }
            if (flag & HAL_QUIET_ADD_SWBA_RESP_TIME) {
                next_start_us += 
                    ah->ah_config.ah_sw_beacon_response_time;
            }
            OS_REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1); 
            OS_REG_WRITE(ah, AR_QUIET2, SM(duration, AR_QUIET2_QUIET_DUR));
            OS_REG_WRITE(ah, AR_QUIET_PERIOD, TU_TO_USEC(period));
            OS_REG_WRITE(ah, AR_NEXT_QUIET_TIMER, next_start_us);
            OS_REG_SET_BIT(ah, AR_TIMER_MODE, AR_QUIET_TIMER_EN);
            if ((OS_REG_READ(ah, AR_TSF_L32) >> 10) == tsf >> 10) {
                status = HAL_OK;
                break;
            }
            HALDEBUG(ah, HAL_DEBUG_QUEUE, "%s: TSF have moved "
                "while trying to set quiet time TSF: 0x%08x\n", __func__, tsf);
            /* TSF shouldn't count twice or reg access is taking forever */
            HALASSERT(j < 1);
        }
    } else {
        OS_REG_CLR_BIT(ah, AR_TIMER_MODE, AR_QUIET_TIMER_EN);
        status = HAL_OK;
    }

    return status;
#undef	TU_TO_USEC
}

//#ifdef ATH_SUPPORT_DFS
void
ar9300_cac_tx_quiet(struct ath_hal *ah, HAL_BOOL enable)
{
    uint32_t reg1, reg2;

    reg1 = OS_REG_READ(ah, AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE));
    reg2 = OS_REG_READ(ah, AR_MAC_PCU_OFFSET(MAC_PCU_QUIET_TIME_1));
    AH9300(ah)->ah_cac_quiet_enabled = enable;

    if (enable) {
        OS_REG_WRITE(ah, AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE),
                     reg1 | AR_PCU_FORCE_QUIET_COLL);
        OS_REG_WRITE(ah, AR_MAC_PCU_OFFSET(MAC_PCU_QUIET_TIME_1),
                     reg2 & ~AR_QUIET1_QUIET_ACK_CTS_ENABLE);
    } else {
        OS_REG_WRITE(ah, AR_MAC_PCU_OFFSET(MAC_PCU_MISC_MODE),
                     reg1 & ~AR_PCU_FORCE_QUIET_COLL);
        OS_REG_WRITE(ah, AR_MAC_PCU_OFFSET(MAC_PCU_QUIET_TIME_1),
                     reg2 | AR_QUIET1_QUIET_ACK_CTS_ENABLE);
    }
}
//#endif /* ATH_SUPPORT_DFS */

void
ar9300_set_pcu_config(struct ath_hal *ah)
{
    ar9300_set_operating_mode(ah, AH_PRIVATE(ah)->ah_opmode);
}

HAL_STATUS
ar9300_get_capability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
    u_int32_t capability, u_int32_t *result)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    const HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;
    struct ar9300_ani_state *ani;

    switch (type) {
    case HAL_CAP_CIPHER:            /* cipher handled in hardware */
        switch (capability) {
        case HAL_CIPHER_AES_CCM:
        case HAL_CIPHER_AES_OCB:
        case HAL_CIPHER_TKIP:
        case HAL_CIPHER_WEP:
        case HAL_CIPHER_MIC:
        case HAL_CIPHER_CLR:
            return HAL_OK;
        default:
            return HAL_ENOTSUPP;
        }
    case HAL_CAP_TKIP_MIC:          /* handle TKIP MIC in hardware */
        switch (capability) {
        case 0:         /* hardware capability */
            return HAL_OK;
        case 1:
            return (ahp->ah_sta_id1_defaults &
                    AR_STA_ID1_CRPT_MIC_ENABLE) ?  HAL_OK : HAL_ENXIO;
        default:
            return HAL_ENOTSUPP;
        }
    case HAL_CAP_TKIP_SPLIT:        /* hardware TKIP uses split keys */
        switch (capability) {
        case 0: /* hardware capability */
            return p_cap->halTkipMicTxRxKeySupport ? HAL_ENXIO : HAL_OK;
        case 1: /* current setting */
            return (ahp->ah_misc_mode & AR_PCU_MIC_NEW_LOC_ENA) ?
                HAL_ENXIO : HAL_OK;
        default:
            return HAL_ENOTSUPP;
        }
    case HAL_CAP_WME_TKIPMIC:
        /* hardware can do TKIP MIC when WMM is turned on */
        return HAL_OK;
    case HAL_CAP_PHYCOUNTERS:       /* hardware PHY error counters */
        return HAL_OK;
    case HAL_CAP_DIVERSITY:         /* hardware supports fast diversity */
        switch (capability) {
        case 0:                 /* hardware capability */
            return HAL_OK;
        case 1:                 /* current setting */
            return (OS_REG_READ(ah, AR_PHY_CCK_DETECT) &
                            AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV) ?
                            HAL_OK : HAL_ENXIO;
        }
        return HAL_EINVAL;
    case HAL_CAP_TPC:
        switch (capability) {
        case 0:                 /* hardware capability */
            return HAL_OK;
        case 1:
            return ah->ah_config.ath_hal_desc_tpc ?
                               HAL_OK : HAL_ENXIO;
        }
        return HAL_OK;
    case HAL_CAP_PHYDIAG:           /* radar pulse detection capability */
        return HAL_OK;
    case HAL_CAP_MCAST_KEYSRCH:     /* multicast frame keycache search */
        switch (capability) {
        case 0:                 /* hardware capability */
            return HAL_OK;
        case 1:
            if (OS_REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_ADHOC) {
                /*
                 * Owl and Merlin have problems in mcast key search.
                 * Disable this cap. in Ad-hoc mode. see Bug 25776 and
                 * 26802
                 */
                return HAL_ENXIO;
            } else {
                return (ahp->ah_sta_id1_defaults &
                        AR_STA_ID1_MCAST_KSRCH) ? HAL_OK : HAL_ENXIO;
            }
        }
        return HAL_EINVAL;
    case HAL_CAP_TSF_ADJUST:        /* hardware has beacon tsf adjust */
        switch (capability) {
        case 0:                 /* hardware capability */
            return p_cap->halTsfAddSupport ? HAL_OK : HAL_ENOTSUPP;
        case 1:
            return (ahp->ah_misc_mode & AR_PCU_TX_ADD_TSF) ?
                HAL_OK : HAL_ENXIO;
        }
        return HAL_EINVAL;
    case HAL_CAP_RFSILENT:      /* rfsilent support  */
        if (capability == 3) {  /* rfkill interrupt */
            /*
             * XXX: Interrupt-based notification of RF Kill state
             *      changes not working yet. Report that this feature
             *      is not supported so that polling is used instead.
             */
            return (HAL_ENOTSUPP);
        }
        return ath_hal_getcapability(ah, type, capability, result);
    case HAL_CAP_4ADDR_AGGR:
        return HAL_OK;
    case HAL_CAP_BB_RIFS_HANG:
        return HAL_ENOTSUPP;
    case HAL_CAP_BB_DFS_HANG:
        return HAL_ENOTSUPP;
    case HAL_CAP_BB_RX_CLEAR_STUCK_HANG:
        /* Track chips that are known to have BB hangs related
         * to rx_clear stuck low.
         */
        return HAL_ENOTSUPP;
    case HAL_CAP_MAC_HANG:
        /* Track chips that are known to have MAC hangs.
         */
        return HAL_OK;
    case HAL_CAP_RIFS_RX_ENABLED:
        /* Is RIFS RX currently enabled */
        return (ahp->ah_rifs_enabled == AH_TRUE) ?  HAL_OK : HAL_ENOTSUPP;
#if 0
    case HAL_CAP_ANT_CFG_2GHZ:
        *result = p_cap->halNumAntCfg2Ghz;
        return HAL_OK;
    case HAL_CAP_ANT_CFG_5GHZ:
        *result = p_cap->halNumAntCfg5Ghz;
        return HAL_OK;
    case HAL_CAP_RX_STBC:
        *result = p_cap->hal_rx_stbc_support;
        return HAL_OK;
    case HAL_CAP_TX_STBC:
        *result = p_cap->hal_tx_stbc_support;
        return HAL_OK;
#endif
    case HAL_CAP_LDPC:
        *result = p_cap->halLDPCSupport;
        return HAL_OK;
    case HAL_CAP_DYNAMIC_SMPS:
        return HAL_OK;
    case HAL_CAP_DS:
        return (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah) ||
                (p_cap->halTxChainMask & 0x3) != 0x3 ||
                (p_cap->halRxChainMask & 0x3) != 0x3) ?
            HAL_ENOTSUPP : HAL_OK;
    case HAL_CAP_TS:
        return (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah) ||
                (p_cap->halTxChainMask & 0x7) != 0x7 ||
                (p_cap->halRxChainMask & 0x7) != 0x7) ?
            HAL_ENOTSUPP : HAL_OK;
    case HAL_CAP_OL_PWRCTRL:
        return (ar9300_eeprom_get(ahp, EEP_OL_PWRCTRL)) ?
            HAL_OK : HAL_ENOTSUPP;
    case HAL_CAP_CRDC:
#if ATH_SUPPORT_CRDC
        return (AR_SREV_WASP(ah) && 
                ah->ah_config.ath_hal_crdc_enable) ? 
                    HAL_OK : HAL_ENOTSUPP;
#else
        return HAL_ENOTSUPP;
#endif
#if 0
    case HAL_CAP_MAX_WEP_TKIP_HT20_TX_RATEKBPS:
        *result = (u_int32_t)(-1);
        return HAL_OK;
    case HAL_CAP_MAX_WEP_TKIP_HT40_TX_RATEKBPS:
        *result = (u_int32_t)(-1);
        return HAL_OK;
#endif
    case HAL_CAP_BB_PANIC_WATCHDOG:
        return HAL_OK;
    case HAL_CAP_PHYRESTART_CLR_WAR:
        if ((AH_PRIVATE((ah))->ah_macVersion == AR_SREV_VERSION_OSPREY) &&
            (AH_PRIVATE((ah))->ah_macRev < AR_SREV_REVISION_AR9580_10)) 
        {
            return HAL_OK;
        }
        else
        {
            return HAL_ENOTSUPP;
        }
    case HAL_CAP_ENTERPRISE_MODE:
        *result = ahp->ah_enterprise_mode >> 16;
        /*
         * WAR for EV 77658 - Add delimiters to first sub-frame when using
         * RTS/CTS with aggregation and non-enterprise Osprey.
         *
         * Bug fixed in AR9580/Peacock, Wasp1.1 and later
         */
        if ((ahp->ah_enterprise_mode & AR_ENT_OTP_MIN_PKT_SIZE_DISABLE) &&
                !AR_SREV_AR9580_10_OR_LATER(ah) && (!AR_SREV_WASP(ah) ||
                AR_SREV_WASP_10(ah))) {
            *result |= AH_ENT_RTSCTS_DELIM_WAR;
        }
        return HAL_OK;
    case HAL_CAP_LDPCWAR:
        /* WAR for RIFS+LDPC issue is required for all chips currently 
         * supported by ar9300 HAL.
         */
        return HAL_OK;    
    case HAL_CAP_ENABLE_APM:
        *result = p_cap->halApmEnable;
        return HAL_OK;
    case HAL_CAP_PCIE_LCR_EXTSYNC_EN:
        return (p_cap->hal_pcie_lcr_extsync_en == AH_TRUE) ? HAL_OK : HAL_ENOTSUPP;
    case HAL_CAP_PCIE_LCR_OFFSET:
        *result = p_cap->hal_pcie_lcr_offset;
        return HAL_OK;
    case HAL_CAP_SMARTANTENNA:
        /* FIXME A request is pending with h/w team to add feature bit in
         * caldata to detect if board has smart antenna or not, once added
         * we need to fix his piece of code to read and return value without
         * any compile flags
         */
#if UMAC_SUPPORT_SMARTANTENNA
        /* enable smart antenna for  Peacock, Wasp and scorpion 
           for future chips need to modify */
        if (AR_SREV_AR9580_10(ah) || (AR_SREV_WASP(ah)) || AR_SREV_SCORPION(ah)) {
            return HAL_OK;
        } else {
            return HAL_ENOTSUPP;
        }
#else
        return HAL_ENOTSUPP;
#endif

#ifdef ATH_TRAFFIC_FAST_RECOVER
    case HAL_CAP_TRAFFIC_FAST_RECOVER:
        if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_WASP_11(ah)) {
            return HAL_OK;
        } else {
            return HAL_ENOTSUPP;
        }
#endif

    /* FreeBSD ANI */
    case HAL_CAP_INTMIT:            /* interference mitigation */
            switch (capability) {
            case HAL_CAP_INTMIT_PRESENT:            /* hardware capability */
                    return HAL_OK;
            case HAL_CAP_INTMIT_ENABLE:
                    return (ahp->ah_proc_phy_err & HAL_PROCESS_ANI) ?
                            HAL_OK : HAL_ENXIO;
            case HAL_CAP_INTMIT_NOISE_IMMUNITY_LEVEL:
            case HAL_CAP_INTMIT_OFDM_WEAK_SIGNAL_LEVEL:
//            case HAL_CAP_INTMIT_CCK_WEAK_SIGNAL_THR:
            case HAL_CAP_INTMIT_FIRSTEP_LEVEL:
            case HAL_CAP_INTMIT_SPUR_IMMUNITY_LEVEL:
                    ani = ar9300_ani_get_current_state(ah);
                    if (ani == AH_NULL)
                            return HAL_ENXIO;
                    switch (capability) {
                    /* XXX AR9300 HAL has OFDM/CCK noise immunity level params? */
                    case 2: *result = ani->ofdm_noise_immunity_level; break;
                    case 3: *result = !ani->ofdm_weak_sig_detect_off; break;
 //                   case 4: *result = ani->cck_weak_sig_threshold; break;
                    case 5: *result = ani->firstep_level; break;
                    case 6: *result = ani->spur_immunity_level; break;
                    }
                    return HAL_OK;
            }
            return HAL_EINVAL;
    case HAL_CAP_ENFORCE_TXOP:
        if (capability == 0)
            return (HAL_OK);
        if (capability != 1)
            return (HAL_ENOTSUPP);
        (*result) = !! (ahp->ah_misc_mode & AR_PCU_TXOP_TBTT_LIMIT_ENA);
        return (HAL_OK);
    case HAL_CAP_TOA_LOCATIONING:
        if (capability == 0)
            return HAL_OK;
        if (capability == 2) {
            *result = ar9300_read_loc_timer(ah);
            return (HAL_OK);
        }
        return HAL_ENOTSUPP;
    default:
        return ath_hal_getcapability(ah, type, capability, result);
    }
}

HAL_BOOL
ar9300_set_capability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
        u_int32_t capability, u_int32_t setting, HAL_STATUS *status)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    const HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;
    u_int32_t v;

    switch (type) {
    case HAL_CAP_TKIP_SPLIT:        /* hardware TKIP uses split keys */
        if (! p_cap->halTkipMicTxRxKeySupport)
            return AH_FALSE;

        if (setting)
            ahp->ah_misc_mode &= ~AR_PCU_MIC_NEW_LOC_ENA;
        else
            ahp->ah_misc_mode |= AR_PCU_MIC_NEW_LOC_ENA;

        OS_REG_WRITE(ah, AR_PCU_MISC, ahp->ah_misc_mode);
        return AH_TRUE;

    case HAL_CAP_TKIP_MIC:          /* handle TKIP MIC in hardware */
        if (setting) {
            ahp->ah_sta_id1_defaults |= AR_STA_ID1_CRPT_MIC_ENABLE;
        } else {
            ahp->ah_sta_id1_defaults &= ~AR_STA_ID1_CRPT_MIC_ENABLE;
        }
        return AH_TRUE;
    case HAL_CAP_DIVERSITY:
        v = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
        if (setting) {
            v |= AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
        } else {
            v &= ~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
        }
        OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, v);
        return AH_TRUE;
    case HAL_CAP_DIAG:              /* hardware diagnostic support */
        /*
         * NB: could split this up into virtual capabilities,
         *     (e.g. 1 => ACK, 2 => CTS, etc.) but it hardly
         *     seems worth the additional complexity.
         */
#ifdef AH_DEBUG
        AH_PRIVATE(ah)->ah_diagreg = setting;
#else
        AH_PRIVATE(ah)->ah_diagreg = setting & 0x6;     /* ACK+CTS */
#endif
        OS_REG_WRITE(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);
        return AH_TRUE;
    case HAL_CAP_TPC:
        ah->ah_config.ath_hal_desc_tpc = (setting != 0);
        return AH_TRUE;
    case HAL_CAP_MCAST_KEYSRCH:     /* multicast frame keycache search */
        if (setting) {
            ahp->ah_sta_id1_defaults |= AR_STA_ID1_MCAST_KSRCH;
        } else {
            ahp->ah_sta_id1_defaults &= ~AR_STA_ID1_MCAST_KSRCH;
        }
        return AH_TRUE;
    case HAL_CAP_TSF_ADJUST:        /* hardware has beacon tsf adjust */
        if (p_cap->halTsfAddSupport) {
            if (setting) {
                ahp->ah_misc_mode |= AR_PCU_TX_ADD_TSF;
            } else {
                ahp->ah_misc_mode &= ~AR_PCU_TX_ADD_TSF;
            }
            return AH_TRUE;
        }
        return AH_FALSE;

    /* FreeBSD interrupt mitigation / ANI */
    case HAL_CAP_INTMIT: {          /* interference mitigation */
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
#define N(a)    (sizeof(a) / sizeof(a[0]))
            return capability < N(cmds) ?
                    ar9300_ani_control(ah, cmds[capability], setting) :
                    AH_FALSE;
#undef N
    }

    case HAL_CAP_RXBUFSIZE:         /* set MAC receive buffer size */
        ahp->rx_buf_size = setting & AR_DATABUF_MASK;
        OS_REG_WRITE(ah, AR_DATABUF, ahp->rx_buf_size);
        return AH_TRUE;

    case HAL_CAP_ENFORCE_TXOP:
        if (capability != 1)
            return AH_FALSE;
        if (setting) {
            ahp->ah_misc_mode |= AR_PCU_TXOP_TBTT_LIMIT_ENA;
            OS_REG_SET_BIT(ah, AR_PCU_MISC, AR_PCU_TXOP_TBTT_LIMIT_ENA);
        } else {
            ahp->ah_misc_mode &= ~AR_PCU_TXOP_TBTT_LIMIT_ENA;
            OS_REG_CLR_BIT(ah, AR_PCU_MISC, AR_PCU_TXOP_TBTT_LIMIT_ENA);
        }
        return AH_TRUE;

    case HAL_CAP_TOA_LOCATIONING:
        if (capability == 0)
            return AH_TRUE;
        if (capability == 1) {
            ar9300_update_loc_ctl_reg(ah, setting);
            return AH_TRUE;
        }
        return AH_FALSE;
        /* fall thru... */
    default:
        return ath_hal_setcapability(ah, type, capability, setting, status);
    }
}

#ifdef AH_DEBUG
static void
ar9300_print_reg(struct ath_hal *ah, u_int32_t args)
{
    u_int32_t i = 0;

    /* Read 0x80d0 to trigger pcie analyzer */
    HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
        "0x%04x 0x%08x\n", 0x80d0, OS_REG_READ(ah, 0x80d0));

    if (args & HAL_DIAG_PRINT_REG_COUNTER) {
        struct ath_hal_9300 *ahp = AH9300(ah);
        u_int32_t tf, rf, rc, cc;

        tf = OS_REG_READ(ah, AR_TFCNT);
        rf = OS_REG_READ(ah, AR_RFCNT);
        rc = OS_REG_READ(ah, AR_RCCNT);
        cc = OS_REG_READ(ah, AR_CCCNT);

        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "AR_TFCNT Diff= 0x%x\n", tf - ahp->last_tf);
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "AR_RFCNT Diff= 0x%x\n", rf - ahp->last_rf);
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "AR_RCCNT Diff= 0x%x\n", rc - ahp->last_rc);
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "AR_CCCNT Diff= 0x%x\n", cc - ahp->last_cc);

        ahp->last_tf = tf;
        ahp->last_rf = rf;
        ahp->last_rc = rc;
        ahp->last_cc = cc;

        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG0 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_0));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG1 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_1));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG2 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_2));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG3 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_3));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG4 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_4));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG5 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_5));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG6 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_6));
        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "DMADBG7 = 0x%x\n", OS_REG_READ(ah, AR_DMADBG_7));
    }

    if (args & HAL_DIAG_PRINT_REG_ALL) {
        for (i = 0x8; i <= 0xB8; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x800; i <= (0x800 + (10 << 2)); i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "0x%04x 0x%08x\n", 0x840, OS_REG_READ(ah, i));

        HALDEBUG(ah, HAL_DEBUG_PRINT_REG,
            "0x%04x 0x%08x\n", 0x880, OS_REG_READ(ah, i));

        for (i = 0x8C0; i <= (0x8C0 + (10 << 2)); i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x1F00; i <= 0x1F04; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x4000; i <= 0x408C; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x5000; i <= 0x503C; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x7040; i <= 0x7058; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x8000; i <= 0x8098; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x80D4; i <= 0x8200; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x8240; i <= 0x97FC; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x9800; i <= 0x99f0; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0x9c10; i <= 0x9CFC; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }

        for (i = 0xA200; i <= 0xA26C; i += sizeof(u_int32_t)) {
            HALDEBUG(ah, HAL_DEBUG_PRINT_REG, "0x%04x 0x%08x\n",
                i, OS_REG_READ(ah, i));
        }
    }
}
#endif

HAL_BOOL
ar9300_get_diag_state(struct ath_hal *ah, int request,
        const void *args, u_int32_t argsize,
        void **result, u_int32_t *resultsize)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani;

    (void) ahp;
    if (ath_hal_getdiagstate(ah, request, args, argsize, result, resultsize)) {
        return AH_TRUE;
    }
    switch (request) {
#ifdef AH_PRIVATE_DIAG
    case HAL_DIAG_EEPROM:
        *result = &ahp->ah_eeprom;
        *resultsize = sizeof(ar9300_eeprom_t);
        return AH_TRUE;

#if 0   /* XXX - TODO */
    case HAL_DIAG_EEPROM_EXP_11A:
    case HAL_DIAG_EEPROM_EXP_11B:
    case HAL_DIAG_EEPROM_EXP_11G:
        pe = &ahp->ah_mode_power_array2133[request - HAL_DIAG_EEPROM_EXP_11A];
        *result = pe->p_channels;
        *resultsize = (*result == AH_NULL) ? 0 :
            roundup(sizeof(u_int16_t) * pe->num_channels,
            sizeof(u_int32_t)) +
                sizeof(EXPN_DATA_PER_CHANNEL_2133) * pe->num_channels;
        return AH_TRUE;
#endif
    case HAL_DIAG_RFGAIN:
        *result = &ahp->ah_gain_values;
        *resultsize = sizeof(GAIN_VALUES);
        return AH_TRUE;
    case HAL_DIAG_RFGAIN_CURSTEP:
        *result = (void *) ahp->ah_gain_values.curr_step;
        *resultsize = (*result == AH_NULL) ?
                0 : sizeof(GAIN_OPTIMIZATION_STEP);
        return AH_TRUE;
#if 0   /* XXX - TODO */
    case HAL_DIAG_PCDAC:
        *result = ahp->ah_pcdac_table;
        *resultsize = ahp->ah_pcdac_table_size;
        return AH_TRUE;
#endif
    case HAL_DIAG_ANI_CURRENT:

        ani = ar9300_ani_get_current_state(ah);
        if (ani == AH_NULL)
            return AH_FALSE;
        /* Convert ar9300 HAL to FreeBSD HAL ANI state */
        bzero(&ahp->ext_ani_state, sizeof(ahp->ext_ani_state));
        ahp->ext_ani_state.noiseImmunityLevel = ani->ofdm_noise_immunity_level;
        ahp->ext_ani_state.spurImmunityLevel = ani->spur_immunity_level;
        ahp->ext_ani_state.firstepLevel = ani->firstep_level;
        ahp->ext_ani_state.ofdmWeakSigDetectOff = ani->ofdm_weak_sig_detect_off;
        ahp->ext_ani_state.mrcCck = !! ani->mrc_cck_off;
        ahp->ext_ani_state.cckNoiseImmunityLevel = ani->cck_noise_immunity_level;

        ahp->ext_ani_state.listenTime = ani->listen_time;

        *result = &ahp->ext_ani_state;
        *resultsize = sizeof(ahp->ext_ani_state);
#if 0
        *result = ar9300_ani_get_current_state(ah);
        *resultsize = (*result == AH_NULL) ?
            0 : sizeof(struct ar9300_ani_state);
#endif
        return AH_TRUE;
    case HAL_DIAG_ANI_STATS:
        *result = ar9300_ani_get_current_stats(ah);
        *resultsize = (*result == AH_NULL) ?
            0 : sizeof(HAL_ANI_STATS);
        return AH_TRUE;
    case HAL_DIAG_ANI_CMD:
    {
        HAL_ANI_CMD savefunc = ahp->ah_ani_function;
        if (argsize != 2*sizeof(u_int32_t)) {
            return AH_FALSE;
        }
        /* temporarly allow all functions so we can override */
        ahp->ah_ani_function = HAL_ANI_ALL;
        ar9300_ani_control(
            ah, ((const u_int32_t *)args)[0], ((const u_int32_t *)args)[1]);
        ahp->ah_ani_function = savefunc;
        return AH_TRUE;
    }
#if 0
    case HAL_DIAG_TXCONT:
        /*AR9300_CONTTXMODE(ah, (struct ath_desc *)args, argsize );*/
        return AH_TRUE;
#endif /* 0 */
#endif /* AH_PRIVATE_DIAG */
    case HAL_DIAG_CHANNELS:
#if 0
        *result = &(ahp->ah_priv.ah_channels[0]);
        *resultsize =
            sizeof(ahp->ah_priv.ah_channels[0]) * ahp->ah_priv.priv.ah_nchan;
#endif
        return AH_TRUE;
#ifdef AH_DEBUG
    case HAL_DIAG_PRINT_REG:
        ar9300_print_reg(ah, *((const u_int32_t *)args));
        return AH_TRUE;
#endif
    default:
        break;
    }

    return AH_FALSE;
}

void
ar9300_dma_reg_dump(struct ath_hal *ah)
{
#ifdef AH_DEBUG
#define NUM_DMA_DEBUG_REGS  8
#define NUM_QUEUES          10

    u_int32_t val[NUM_DMA_DEBUG_REGS];
    int       qcu_offset = 0, dcu_offset = 0;
    u_int32_t *qcu_base  = &val[0], *dcu_base = &val[4], reg;
    int       i, j, k;
    int16_t nfarray[HAL_NUM_NF_READINGS];
#ifdef	ATH_NF_PER_CHAN
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, AH_PRIVATE(ah)->ah_curchan);
#endif	/* ATH_NF_PER_CHAN */
    HAL_NFCAL_HIST_FULL *h = AH_HOME_CHAN_NFCAL_HIST(ah, ichan);

     /* selecting DMA OBS 8 */
    OS_REG_WRITE(ah, AR_MACMISC, 
        ((AR_MACMISC_DMA_OBS_LINE_8 << AR_MACMISC_DMA_OBS_S) | 
         (AR_MACMISC_MISC_OBS_BUS_1 << AR_MACMISC_MISC_OBS_BUS_MSB_S)));
 
    ath_hal_printf(ah, "Raw DMA Debug values:\n");
    for (i = 0; i < NUM_DMA_DEBUG_REGS; i++) {
        if (i % 4 == 0) {
            ath_hal_printf(ah, "\n");
        }

        val[i] = OS_REG_READ(ah, AR_DMADBG_0 + (i * sizeof(u_int32_t)));
        ath_hal_printf(ah, "%d: %08x ", i, val[i]);
    }

    ath_hal_printf(ah, "\n\n");
    ath_hal_printf(ah, "Num QCU: chain_st fsp_ok fsp_st DCU: chain_st\n");

    for (i = 0; i < NUM_QUEUES; i++, qcu_offset += 4, dcu_offset += 5) {
        if (i == 8) {
            /* only 8 QCU entries in val[0] */
            qcu_offset = 0;
            qcu_base++;
        }

        if (i == 6) {
            /* only 6 DCU entries in val[4] */
            dcu_offset = 0;
            dcu_base++;
        }

        ath_hal_printf(ah,
            "%2d          %2x      %1x     %2x           %2x\n",
            i,
            (*qcu_base & (0x7 << qcu_offset)) >> qcu_offset,
            (*qcu_base & (0x8 << qcu_offset)) >> (qcu_offset + 3),
            val[2] & (0x7 << (i * 3)) >> (i * 3),
            (*dcu_base & (0x1f << dcu_offset)) >> dcu_offset);
    }

    ath_hal_printf(ah, "\n");
    ath_hal_printf(ah,
        "qcu_stitch state:   %2x    qcu_fetch state:        %2x\n",
        (val[3] & 0x003c0000) >> 18, (val[3] & 0x03c00000) >> 22);
    ath_hal_printf(ah,
        "qcu_complete state: %2x    dcu_complete state:     %2x\n",
        (val[3] & 0x1c000000) >> 26, (val[6] & 0x3));
    ath_hal_printf(ah,
        "dcu_arb state:      %2x    dcu_fp state:           %2x\n",
        (val[5] & 0x06000000) >> 25, (val[5] & 0x38000000) >> 27);
    ath_hal_printf(ah,
        "chan_idle_dur:     %3d    chan_idle_dur_valid:     %1d\n",
        (val[6] & 0x000003fc) >> 2, (val[6] & 0x00000400) >> 10);
    ath_hal_printf(ah,
        "txfifo_valid_0:      %1d    txfifo_valid_1:          %1d\n",
        (val[6] & 0x00000800) >> 11, (val[6] & 0x00001000) >> 12);
    ath_hal_printf(ah,
        "txfifo_dcu_num_0:   %2d    txfifo_dcu_num_1:       %2d\n",
        (val[6] & 0x0001e000) >> 13, (val[6] & 0x001e0000) >> 17);
    ath_hal_printf(ah, "pcu observe 0x%x \n", OS_REG_READ(ah, AR_OBS_BUS_1)); 
    ath_hal_printf(ah, "AR_CR 0x%x \n", OS_REG_READ(ah, AR_CR));

    ar9300_upload_noise_floor(ah, 1, nfarray);
    ath_hal_printf(ah, "2G:\n");
    ath_hal_printf(ah, "Min CCA Out:\n");
    ath_hal_printf(ah, "\t\tChain 0\t\tChain 1\t\tChain 2\n");
    ath_hal_printf(ah, "Control:\t%8d\t%8d\t%8d\n",
                   nfarray[0], nfarray[1], nfarray[2]);
    ath_hal_printf(ah, "Extension:\t%8d\t%8d\t%8d\n\n",
                   nfarray[3], nfarray[4], nfarray[5]);

    ar9300_upload_noise_floor(ah, 0, nfarray);
    ath_hal_printf(ah, "5G:\n");
    ath_hal_printf(ah, "Min CCA Out:\n");
    ath_hal_printf(ah, "\t\tChain 0\t\tChain 1\t\tChain 2\n");
    ath_hal_printf(ah, "Control:\t%8d\t%8d\t%8d\n",
                   nfarray[0], nfarray[1], nfarray[2]);
    ath_hal_printf(ah, "Extension:\t%8d\t%8d\t%8d\n\n",
                   nfarray[3], nfarray[4], nfarray[5]);

    for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
        ath_hal_printf(ah, "%s Chain %d NF History:\n",
                       ((i < 3) ? "Control " : "Extension "), i%3);
        for (j = 0, k = h->base.curr_index;
             j < HAL_NF_CAL_HIST_LEN_FULL;
             j++, k++) {
            ath_hal_printf(ah, "Element %d: %d\n",
                j, h->nf_cal_buffer[k % HAL_NF_CAL_HIST_LEN_FULL][i]);
        }
        ath_hal_printf(ah, "Last Programmed NF: %d\n\n", h->base.priv_nf[i]);
    }

    reg = OS_REG_READ(ah, AR_PHY_FIND_SIG_LOW);
    ath_hal_printf(ah, "FIRStep Low = 0x%x (%d)\n",
                   MS(reg, AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW),
                   MS(reg, AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW));
    reg = OS_REG_READ(ah, AR_PHY_DESIRED_SZ);
    ath_hal_printf(ah, "Total Desired = 0x%x (%d)\n",
                   MS(reg, AR_PHY_DESIRED_SZ_TOT_DES),
                   MS(reg, AR_PHY_DESIRED_SZ_TOT_DES));
    ath_hal_printf(ah, "ADC Desired = 0x%x (%d)\n",
                   MS(reg, AR_PHY_DESIRED_SZ_ADC),
                   MS(reg, AR_PHY_DESIRED_SZ_ADC));
    reg = OS_REG_READ(ah, AR_PHY_FIND_SIG);
    ath_hal_printf(ah, "FIRStep = 0x%x (%d)\n",
                   MS(reg, AR_PHY_FIND_SIG_FIRSTEP),
                   MS(reg, AR_PHY_FIND_SIG_FIRSTEP));
    reg = OS_REG_READ(ah, AR_PHY_AGC);
    ath_hal_printf(ah, "Coarse High = 0x%x (%d)\n",
                   MS(reg, AR_PHY_AGC_COARSE_HIGH),
                   MS(reg, AR_PHY_AGC_COARSE_HIGH));
    ath_hal_printf(ah, "Coarse Low = 0x%x (%d)\n",
                   MS(reg, AR_PHY_AGC_COARSE_LOW),
                   MS(reg, AR_PHY_AGC_COARSE_LOW));
    ath_hal_printf(ah, "Coarse Power Constant = 0x%x (%d)\n",
                   MS(reg, AR_PHY_AGC_COARSE_PWR_CONST),
                   MS(reg, AR_PHY_AGC_COARSE_PWR_CONST));
    reg = OS_REG_READ(ah, AR_PHY_TIMING5);
    ath_hal_printf(ah, "Enable Cyclic Power Thresh = %d\n",
                   MS(reg, AR_PHY_TIMING5_CYCPWR_THR1_ENABLE));
    ath_hal_printf(ah, "Cyclic Power Thresh = 0x%x (%d)\n",
                   MS(reg, AR_PHY_TIMING5_CYCPWR_THR1),
                   MS(reg, AR_PHY_TIMING5_CYCPWR_THR1));
    ath_hal_printf(ah, "Cyclic Power Thresh 1A= 0x%x (%d)\n",
                   MS(reg, AR_PHY_TIMING5_CYCPWR_THR1A),
                   MS(reg, AR_PHY_TIMING5_CYCPWR_THR1A));
    reg = OS_REG_READ(ah, AR_PHY_DAG_CTRLCCK);
    ath_hal_printf(ah, "Barker RSSI Thresh Enable = %d\n",
                   MS(reg, AR_PHY_DAG_CTRLCCK_EN_RSSI_THR));
    ath_hal_printf(ah, "Barker RSSI Thresh = 0x%x (%d)\n",
                   MS(reg, AR_PHY_DAG_CTRLCCK_RSSI_THR),
                   MS(reg, AR_PHY_DAG_CTRLCCK_RSSI_THR));


    /* Step 1a: Set bit 23 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x00800000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2a: Set register 0xa364 to 0x1000 */
    reg = 0x1000;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3a: Read bits 17:0 of register 0x9c20 */
    reg = OS_REG_READ(ah, 0x9c20);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x1000] 0x9c20[17:0] = 0x%x\n",
        __func__, reg);

    /* Step 1b: Set bit 23 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x00800000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2b: Set register 0xa364 to 0x1400 */
    reg = 0x1400;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3b: Read bits 17:0 of register 0x9c20 */
    reg = OS_REG_READ(ah, 0x9c20);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x1400] 0x9c20[17:0] = 0x%x\n",
        __func__, reg);

    /* Step 1c: Set bit 23 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x00800000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2c: Set register 0xa364 to 0x3C00 */
    reg = 0x3c00;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3c: Read bits 17:0 of register 0x9c20 */
    reg = OS_REG_READ(ah, 0x9c20);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x3C00] 0x9c20[17:0] = 0x%x\n",
        __func__, reg);

    /* Step 1d: Set bit 24 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x001040000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2d: Set register 0xa364 to 0x5005D */
    reg = 0x5005D;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3d: Read bits 17:0 of register 0xa368 */
    reg = OS_REG_READ(ah, 0xa368);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x5005D] 0xa368[17:0] = 0x%x\n",
        __func__, reg);

    /* Step 1e: Set bit 24 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x001040000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2e: Set register 0xa364 to 0x7005D */
    reg = 0x7005D;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3e: Read bits 17:0 of register 0xa368 */
    reg = OS_REG_READ(ah, 0xa368);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x7005D] 0xa368[17:0] = 0x%x\n",
       __func__, reg);

    /* Step 1f: Set bit 24 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x001000000;
    reg |= 0x40000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2f: Set register 0xa364 to 0x3005D */
    reg = 0x3005D;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3f: Read bits 17:0 of register 0xa368 */
    reg = OS_REG_READ(ah, 0xa368);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x3005D] 0xa368[17:0] = 0x%x\n",
        __func__, reg);

    /* Step 1g: Set bit 24 of register 0xa360 to 0 */
    reg = OS_REG_READ(ah, 0xa360);
    reg &= ~0x001000000;
    reg |= 0x40000;
    OS_REG_WRITE(ah, 0xa360, reg);

    /* Step 2g: Set register 0xa364 to 0x6005D */
    reg = 0x6005D;
    OS_REG_WRITE(ah, 0xa364, reg);

    /* Step 3g: Read bits 17:0 of register 0xa368 */
    reg = OS_REG_READ(ah, 0xa368);
    reg &= 0x0003ffff;
    ath_hal_printf(ah,
        "%s: Test Control Status [0x6005D] 0xa368[17:0] = 0x%x\n",
        __func__, reg);
#endif /* AH_DEBUG */
}

/*
 * Return the busy for rx_frame, rx_clear, and tx_frame
 */
u_int32_t
ar9300_get_mib_cycle_counts_pct(struct ath_hal *ah, u_int32_t *rxc_pcnt,
    u_int32_t *rxf_pcnt, u_int32_t *txf_pcnt)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t good = 1;

    u_int32_t rc = OS_REG_READ(ah, AR_RCCNT);
    u_int32_t rf = OS_REG_READ(ah, AR_RFCNT);
    u_int32_t tf = OS_REG_READ(ah, AR_TFCNT);
    u_int32_t cc = OS_REG_READ(ah, AR_CCCNT); /* read cycles last */

    if (ahp->ah_cycles == 0 || ahp->ah_cycles > cc) {
        /*
         * Cycle counter wrap (or initial call); it's not possible
         * to accurately calculate a value because the registers
         * right shift rather than wrap--so punt and return 0.
         */
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: cycle counter wrap. ExtBusy = 0\n", __func__);
        good = 0;
    } else {
        u_int32_t cc_d = cc - ahp->ah_cycles;
        u_int32_t rc_d = rc - ahp->ah_rx_clear;
        u_int32_t rf_d = rf - ahp->ah_rx_frame;
        u_int32_t tf_d = tf - ahp->ah_tx_frame;

        if (cc_d != 0) {
            *rxc_pcnt = rc_d * 100 / cc_d;
            *rxf_pcnt = rf_d * 100 / cc_d;
            *txf_pcnt = tf_d * 100 / cc_d;
        } else {
            good = 0;
        }
    }

    ahp->ah_cycles = cc;
    ahp->ah_rx_frame = rf;
    ahp->ah_rx_clear = rc;
    ahp->ah_tx_frame = tf;

    return good;
}

/*
 * Return approximation of extension channel busy over an time interval
 * 0% (clear) -> 100% (busy)
 * -1 for invalid estimate 
 */
uint32_t
ar9300_get_11n_ext_busy(struct ath_hal *ah)
{
    /*
     * Overflow condition to check before multiplying to get %
     * (x * 100 > 0xFFFFFFFF ) => (x > 0x28F5C28)
     */
#define OVERFLOW_LIMIT  0x28F5C28
#define ERROR_CODE      -1    

    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t busy = 0; /* percentage */
    int8_t busyper = 0;
    u_int32_t cycle_count, ctl_busy, ext_busy;

    /* cycle_count will always be the first to wrap; therefore, read it last
     * This sequence of reads is not atomic, and MIB counter wrap
     * could happen during it ?
     */
    ctl_busy = OS_REG_READ(ah, AR_RCCNT);
    ext_busy = OS_REG_READ(ah, AR_EXTRCCNT);
    cycle_count = OS_REG_READ(ah, AR_CCCNT);

    if ((ahp->ah_cycle_count == 0) || (ahp->ah_cycle_count > cycle_count) ||
        (ahp->ah_ctl_busy > ctl_busy) || (ahp->ah_ext_busy > ext_busy))
    {
        /*
         * Cycle counter wrap (or initial call); it's not possible
         * to accurately calculate a value because the registers
         * right shift rather than wrap--so punt and return 0.
         */
        busyper = ERROR_CODE;
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: cycle counter wrap. ExtBusy = 0\n", __func__);
    } else {
        u_int32_t cycle_delta = cycle_count - ahp->ah_cycle_count;
        u_int32_t ext_busy_delta = ext_busy - ahp->ah_ext_busy;

        /*
         * Compute extension channel busy percentage
         * Overflow condition: 0xFFFFFFFF < ext_busy_delta * 100
         * Underflow condition/Divide-by-zero: check that cycle_delta >> 7 != 0
         * Will never happen, since (ext_busy_delta < cycle_delta) always,
         * and shift necessitated by large ext_busy_delta.
         * Due to timing difference to read the registers and counter overflow,
         * it may still happen that cycle_delta >> 7 = 0.
         *
         */
        if (cycle_delta) {
            if (ext_busy_delta > OVERFLOW_LIMIT) {
                if (cycle_delta >> 7) {
                    busy = ((ext_busy_delta >> 7) * 100) / (cycle_delta  >> 7);
                } else {
                    busyper = ERROR_CODE;
                }
            } else {
                busy = (ext_busy_delta * 100) / cycle_delta;
            }
        } else {
            busyper = ERROR_CODE;
        }

        if (busy > 100) {
            busy = 100;
        }
        if ( busyper != ERROR_CODE ) {
            busyper = busy;
        }
    }

    ahp->ah_cycle_count = cycle_count;
    ahp->ah_ctl_busy = ctl_busy;
    ahp->ah_ext_busy = ext_busy;

    return busyper;
#undef OVERFLOW_LIMIT
#undef ERROR_CODE    
}

/* BB Panic Watchdog declarations */
#define HAL_BB_PANIC_WD_HT20_FACTOR         74  /* 0.74 */
#define HAL_BB_PANIC_WD_HT40_FACTOR         37  /* 0.37 */

void
ar9300_config_bb_panic_watchdog(struct ath_hal *ah)
{
#define HAL_BB_PANIC_IDLE_TIME_OUT 0x0a8c0000
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    u_int32_t idle_tmo_ms = AH9300(ah)->ah_bb_panic_timeout_ms;
    u_int32_t val, idle_count;

    if (idle_tmo_ms != 0) {
        /* enable IRQ, disable chip-reset for BB panic */
        val = OS_REG_READ(ah, AR_PHY_PANIC_WD_CTL_2) &
            AR_PHY_BB_PANIC_CNTL2_MASK;
        OS_REG_WRITE(ah, AR_PHY_PANIC_WD_CTL_2,
            (val | AR_PHY_BB_PANIC_IRQ_ENABLE) & ~AR_PHY_BB_PANIC_RST_ENABLE);
        /* bound limit to 10 secs */
        if (idle_tmo_ms > 10000) {
            idle_tmo_ms = 10000;
        }
        if (chan != AH_NULL && IEEE80211_IS_CHAN_HT40(chan)) {
            idle_count = (100 * idle_tmo_ms) / HAL_BB_PANIC_WD_HT40_FACTOR;
        } else {
            idle_count = (100 * idle_tmo_ms) / HAL_BB_PANIC_WD_HT20_FACTOR;
        }
        /*
         * enable panic in non-IDLE mode,
         * disable in IDLE mode,
         * set idle time-out
         */

        // EV92527 : Enable IDLE mode panic

        OS_REG_WRITE(ah, AR_PHY_PANIC_WD_CTL_1, 
                     AR_PHY_BB_PANIC_NON_IDLE_ENABLE | 
                     AR_PHY_BB_PANIC_IDLE_ENABLE |
                     (AR_PHY_BB_PANIC_IDLE_MASK & HAL_BB_PANIC_IDLE_TIME_OUT) |
                     (AR_PHY_BB_PANIC_NON_IDLE_MASK & (idle_count << 2)));
    } else {
        /* disable IRQ, disable chip-reset for BB panic */
        OS_REG_WRITE(ah, AR_PHY_PANIC_WD_CTL_2, 
            OS_REG_READ(ah, AR_PHY_PANIC_WD_CTL_2) &
            ~(AR_PHY_BB_PANIC_RST_ENABLE | AR_PHY_BB_PANIC_IRQ_ENABLE));
        /* disable panic in non-IDLE mode, disable in IDLE mode */
        OS_REG_WRITE(ah, AR_PHY_PANIC_WD_CTL_1, 
            OS_REG_READ(ah, AR_PHY_PANIC_WD_CTL_1) &
            ~(AR_PHY_BB_PANIC_NON_IDLE_ENABLE | AR_PHY_BB_PANIC_IDLE_ENABLE));
    }

    HALDEBUG(ah, HAL_DEBUG_RFPARAM, "%s: %s BB Panic Watchdog tmo=%ums\n", 
             __func__, idle_tmo_ms ? "Enabled" : "Disabled", idle_tmo_ms);
#undef HAL_BB_PANIC_IDLE_TIME_OUT
}


void
ar9300_handle_bb_panic(struct ath_hal *ah)
{
    u_int32_t status;
    /*
     * we want to avoid printing in ISR context so we save 
     * panic watchdog status to be printed later in DPC context
     */
    AH9300(ah)->ah_bb_panic_last_status = status =
        OS_REG_READ(ah, AR_PHY_PANIC_WD_STATUS);
    /*
     * panic watchdog timer should reset on status read
     * but to make sure we write 0 to the watchdog status bit
     */
    OS_REG_WRITE(ah, AR_PHY_PANIC_WD_STATUS, status & ~AR_PHY_BB_WD_STATUS_CLR);
}

int
ar9300_get_bb_panic_info(struct ath_hal *ah, struct hal_bb_panic_info *bb_panic)
{
    bb_panic->status = AH9300(ah)->ah_bb_panic_last_status;

    /*
     * For signature 04000539 do not print anything.
     * This is a very common occurence as a compromise between
     * BB Panic and AH_FALSE detects (EV71009). It indicates 
     * radar hang, which can be cleared by reprogramming
     * radar related register and does not requre a chip reset 
     */

    /* Suppress BB Status mesg following signature */
    switch (bb_panic->status) {
        case 0x04000539:
        case 0x04008009:    
        case 0x04000b09:
        case 0x1300000a:
        return -1;
    }

    bb_panic->tsf = ar9300_get_tsf32(ah);
    bb_panic->wd = MS(bb_panic->status, AR_PHY_BB_WD_STATUS);
    bb_panic->det = MS(bb_panic->status, AR_PHY_BB_WD_DET_HANG);
    bb_panic->rdar = MS(bb_panic->status, AR_PHY_BB_WD_RADAR_SM);
    bb_panic->r_odfm = MS(bb_panic->status, AR_PHY_BB_WD_RX_OFDM_SM);
    bb_panic->r_cck = MS(bb_panic->status, AR_PHY_BB_WD_RX_CCK_SM);
    bb_panic->t_odfm = MS(bb_panic->status, AR_PHY_BB_WD_TX_OFDM_SM);
    bb_panic->t_cck = MS(bb_panic->status, AR_PHY_BB_WD_TX_CCK_SM);
    bb_panic->agc = MS(bb_panic->status, AR_PHY_BB_WD_AGC_SM);
    bb_panic->src = MS(bb_panic->status, AR_PHY_BB_WD_SRCH_SM);
    bb_panic->phy_panic_wd_ctl1 = OS_REG_READ(ah, AR_PHY_PANIC_WD_CTL_1);
    bb_panic->phy_panic_wd_ctl2 = OS_REG_READ(ah, AR_PHY_PANIC_WD_CTL_2);
    bb_panic->phy_gen_ctrl = OS_REG_READ(ah, AR_PHY_GEN_CTRL);
    bb_panic->rxc_pcnt = bb_panic->rxf_pcnt = bb_panic->txf_pcnt = 0;
    bb_panic->cycles = ar9300_get_mib_cycle_counts_pct(ah, 
                                        &bb_panic->rxc_pcnt,
                                        &bb_panic->rxf_pcnt, 
                                        &bb_panic->txf_pcnt);

    if (ah->ah_config.ath_hal_show_bb_panic) {
        ath_hal_printf(ah, "\n==== BB update: BB status=0x%08x, "
            "tsf=0x%08x ====\n", bb_panic->status, bb_panic->tsf);
        ath_hal_printf(ah, "** BB state: wd=%u det=%u rdar=%u rOFDM=%d "
            "rCCK=%u tOFDM=%u tCCK=%u agc=%u src=%u **\n",
            bb_panic->wd, bb_panic->det, bb_panic->rdar,
            bb_panic->r_odfm, bb_panic->r_cck, bb_panic->t_odfm,
            bb_panic->t_cck, bb_panic->agc, bb_panic->src);
        ath_hal_printf(ah, "** BB WD cntl: cntl1=0x%08x cntl2=0x%08x **\n",
            bb_panic->phy_panic_wd_ctl1, bb_panic->phy_panic_wd_ctl2);
        ath_hal_printf(ah, "** BB mode: BB_gen_controls=0x%08x **\n", 
            bb_panic->phy_gen_ctrl);
        if (bb_panic->cycles) {
            ath_hal_printf(ah, "** BB busy times: rx_clear=%d%%, "
                "rx_frame=%d%%, tx_frame=%d%% **\n", bb_panic->rxc_pcnt, 
                bb_panic->rxf_pcnt, bb_panic->txf_pcnt);
        }
        ath_hal_printf(ah, "==== BB update: done ====\n\n");
    }

    return 0; //The returned data will be stored for athstats to retrieve it
}

/* set the reason for HAL reset */
void 
ar9300_set_hal_reset_reason(struct ath_hal *ah, u_int8_t resetreason)
{
    AH9300(ah)->ah_reset_reason = resetreason;
}

/*
 * Configure 20/40 operation
 *
 * 20/40 = joint rx clear (control and extension)
 * 20    = rx clear (control)
 *
 * - NOTE: must stop MAC (tx) and requeue 40 MHz packets as 20 MHz
 *         when changing from 20/40 => 20 only
 */
void
ar9300_set_11n_mac2040(struct ath_hal *ah, HAL_HT_MACMODE mode)
{
    u_int32_t macmode;

    /* Configure MAC for 20/40 operation */
    if (mode == HAL_HT_MACMODE_2040 &&
        !ah->ah_config.ath_hal_cwm_ignore_ext_cca) {
        macmode = AR_2040_JOINED_RX_CLEAR;
    } else {
        macmode = 0;
    }
    OS_REG_WRITE(ah, AR_2040_MODE, macmode);
}

/*
 * Get Rx clear (control/extension channel)
 *
 * Returns active low (busy) for ctrl/ext channel
 * Owl 2.0
 */
HAL_HT_RXCLEAR
ar9300_get_11n_rx_clear(struct ath_hal *ah)
{
    HAL_HT_RXCLEAR rxclear = 0;
    u_int32_t val;

    val = OS_REG_READ(ah, AR_DIAG_SW);

    /* control channel */
    if (val & AR_DIAG_RX_CLEAR_CTL_LOW) {
        rxclear |= HAL_RX_CLEAR_CTL_LOW;
    }
    /* extension channel */
    if (val & AR_DIAG_RX_CLEAR_EXT_LOW) {
        rxclear |= HAL_RX_CLEAR_EXT_LOW;
    }
    return rxclear;
}

/*
 * Set Rx clear (control/extension channel)
 *
 * Useful for forcing the channel to appear busy for
 * debugging/diagnostics
 * Owl 2.0
 */
void
ar9300_set_11n_rx_clear(struct ath_hal *ah, HAL_HT_RXCLEAR rxclear)
{
    /* control channel */
    if (rxclear & HAL_RX_CLEAR_CTL_LOW) {
        OS_REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_CLEAR_CTL_LOW);
    } else {
        OS_REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_CLEAR_CTL_LOW);
    }
    /* extension channel */
    if (rxclear & HAL_RX_CLEAR_EXT_LOW) {
        OS_REG_SET_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_CLEAR_EXT_LOW);
    } else {
        OS_REG_CLR_BIT(ah, AR_DIAG_SW, AR_DIAG_RX_CLEAR_EXT_LOW);
    }
}


/*
 * HAL support code for force ppm tracking workaround.
 */

u_int32_t
ar9300_ppm_get_rssi_dump(struct ath_hal *ah)
{
    u_int32_t retval;
    u_int32_t off1;
    u_int32_t off2;

    if (OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) & AR_PHY_SWAP_ALT_CHAIN) {
        off1 = 0x2000;
        off2 = 0x1000;
    } else {
        off1 = 0x1000;
        off2 = 0x2000;
    }

    retval = ((0xff & OS_REG_READ(ah, AR_PHY_CHAN_INFO_GAIN_0       )) << 0) |
             ((0xff & OS_REG_READ(ah, AR_PHY_CHAN_INFO_GAIN_0 + off1)) << 8) |
             ((0xff & OS_REG_READ(ah, AR_PHY_CHAN_INFO_GAIN_0 + off2)) << 16);

    return retval;
}

u_int32_t
ar9300_ppm_force(struct ath_hal *ah)
{
    u_int32_t data_fine;
    u_int32_t data4;
    //u_int32_t off1;
    //u_int32_t off2;
    HAL_BOOL signed_val = AH_FALSE;

//    if (OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) & AR_PHY_SWAP_ALT_CHAIN) {
//        off1 = 0x2000;
//        off2 = 0x1000;
//    } else {
//        off1 = 0x1000;
//        off2 = 0x2000;
//    }
    data_fine =
        AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_MASK &
        OS_REG_READ(ah, AR_PHY_CHNINFO_GAINDIFF);

    /*
     * bit [11-0] is new ppm value. bit 11 is the signed bit.
     * So check value from bit[10:0].
     * Now get the abs val of the ppm value read in bit[0:11].
     * After that do bound check on abs value.
     * if value is off limit, CAP the value and and restore signed bit.
     */
    if (data_fine & AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_SIGNED_BIT)
    {
        /* get the positive value */
        data_fine = (~data_fine + 1) & AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_MASK;
        signed_val = AH_TRUE;
    }
    if (data_fine > AR_PHY_CHAN_INFO_GAIN_DIFF_UPPER_LIMIT)
    {
        HALDEBUG(ah, HAL_DEBUG_REGIO,
            "%s Correcting ppm out of range %x\n",
            __func__, (data_fine & 0x7ff));
        data_fine = AR_PHY_CHAN_INFO_GAIN_DIFF_UPPER_LIMIT;
    }
    /*
     * Restore signed value if changed above.
     * Use typecast to avoid compilation errors
     */
    if (signed_val) {
        data_fine = (-(int32_t)data_fine) &
            AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_MASK;
    }

    /* write value */
    data4 = OS_REG_READ(ah, AR_PHY_TIMING2) &
        ~(AR_PHY_TIMING2_USE_FORCE_PPM | AR_PHY_TIMING2_FORCE_PPM_VAL);
    OS_REG_WRITE(ah, AR_PHY_TIMING2,
        data4 | data_fine | AR_PHY_TIMING2_USE_FORCE_PPM);

    return data_fine;
}

void
ar9300_ppm_un_force(struct ath_hal *ah)
{
    u_int32_t data4;

    data4 = OS_REG_READ(ah, AR_PHY_TIMING2) & ~AR_PHY_TIMING2_USE_FORCE_PPM;
    OS_REG_WRITE(ah, AR_PHY_TIMING2, data4);
}

u_int32_t
ar9300_ppm_arm_trigger(struct ath_hal *ah)
{
    u_int32_t val;
    u_int32_t ret;

    val = OS_REG_READ(ah, AR_PHY_CHAN_INFO_MEMORY);
    ret = OS_REG_READ(ah, AR_TSF_L32);
    OS_REG_WRITE(ah, AR_PHY_CHAN_INFO_MEMORY,
        val | AR_PHY_CHAN_INFO_MEMORY_CAPTURE_MASK);

    /* return low word of TSF at arm time */
    return ret;
}

int
ar9300_ppm_get_trigger(struct ath_hal *ah)
{
    if (OS_REG_READ(ah, AR_PHY_CHAN_INFO_MEMORY) &
        AR_PHY_CHAN_INFO_MEMORY_CAPTURE_MASK)
    {
        /* has not triggered yet, return AH_FALSE */
        return 0;
    }

    /* else triggered, return AH_TRUE */
    return 1;
}

void
ar9300_mark_phy_inactive(struct ath_hal *ah)
{
    OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
}

/* DEBUG */
u_int32_t
ar9300_ppm_get_force_state(struct ath_hal *ah)
{
    return
        OS_REG_READ(ah, AR_PHY_TIMING2) &
        (AR_PHY_TIMING2_USE_FORCE_PPM | AR_PHY_TIMING2_FORCE_PPM_VAL);
}

/*
 * Return the Cycle counts for rx_frame, rx_clear, and tx_frame
 */
HAL_BOOL
ar9300_get_mib_cycle_counts(struct ath_hal *ah, HAL_SURVEY_SAMPLE *hs)
{
    /*
     * XXX FreeBSD todo: reimplement this
     */
#if 0
    p_cnts->tx_frame_count = OS_REG_READ(ah, AR_TFCNT);
    p_cnts->rx_frame_count = OS_REG_READ(ah, AR_RFCNT);
    p_cnts->rx_clear_count = OS_REG_READ(ah, AR_RCCNT);
    p_cnts->cycle_count   = OS_REG_READ(ah, AR_CCCNT);
    p_cnts->is_tx_active   = (OS_REG_READ(ah, AR_TFCNT) ==
                           p_cnts->tx_frame_count) ? AH_FALSE : AH_TRUE;
    p_cnts->is_rx_active   = (OS_REG_READ(ah, AR_RFCNT) ==
                           p_cnts->rx_frame_count) ? AH_FALSE : AH_TRUE;
#endif
    return AH_FALSE;
}

void
ar9300_clear_mib_counters(struct ath_hal *ah)
{
    u_int32_t reg_val;

    reg_val = OS_REG_READ(ah, AR_MIBC);
    OS_REG_WRITE(ah, AR_MIBC, reg_val | AR_MIBC_CMC);
    OS_REG_WRITE(ah, AR_MIBC, reg_val & ~AR_MIBC_CMC);
}


/* Enable or Disable RIFS Rx capability as part of SW WAR for Bug 31602 */
HAL_BOOL
ar9300_set_rifs_delay(struct ath_hal *ah, HAL_BOOL enable)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL *ichan =
      ath_hal_checkchannel(ah, AH_PRIVATE(ah)->ah_curchan);
    HAL_BOOL is_chan_2g = IS_CHAN_2GHZ(ichan);
    u_int32_t tmp = 0;

    if (enable) {
        if (ahp->ah_rifs_enabled == AH_TRUE) {
            return AH_TRUE;
        }

        OS_REG_WRITE(ah, AR_PHY_SEARCH_START_DELAY, ahp->ah_rifs_reg[0]);
        OS_REG_WRITE(ah, AR_PHY_RIFS_SRCH,
                     ahp->ah_rifs_reg[1]);

        ahp->ah_rifs_enabled = AH_TRUE;
        OS_MEMZERO(ahp->ah_rifs_reg, sizeof(ahp->ah_rifs_reg));
    } else {
        if (ahp->ah_rifs_enabled == AH_TRUE) {
            ahp->ah_rifs_reg[0] = OS_REG_READ(ah,
                                              AR_PHY_SEARCH_START_DELAY);
            ahp->ah_rifs_reg[1] = OS_REG_READ(ah, AR_PHY_RIFS_SRCH);
        }
        /* Change rifs init delay to 0 */
        OS_REG_WRITE(ah, AR_PHY_RIFS_SRCH,
                     (ahp->ah_rifs_reg[1] & ~(AR_PHY_RIFS_INIT_DELAY)));
        tmp = 0xfffff000 & OS_REG_READ(ah, AR_PHY_SEARCH_START_DELAY);        
        if (is_chan_2g) {
            if (IEEE80211_IS_CHAN_HT40(AH_PRIVATE(ah)->ah_curchan)) {
                OS_REG_WRITE(ah, AR_PHY_SEARCH_START_DELAY, tmp | 500);
            } else { /* Sowl 2G HT-20 default is 0x134 for search start delay */
                OS_REG_WRITE(ah, AR_PHY_SEARCH_START_DELAY, tmp | 250);
            }
        } else {
            if (IEEE80211_IS_CHAN_HT40(AH_PRIVATE(ah)->ah_curchan)) {
                OS_REG_WRITE(ah, AR_PHY_SEARCH_START_DELAY, tmp | 0x370);
            } else { /* Sowl 5G HT-20 default is 0x1b8 for search start delay */
                OS_REG_WRITE(ah, AR_PHY_SEARCH_START_DELAY, tmp | 0x1b8);
            }
        }

        ahp->ah_rifs_enabled = AH_FALSE;
    }
    return AH_TRUE;

} /* ar9300_set_rifs_delay () */

/* Set the current RIFS Rx setting */
HAL_BOOL
ar9300_set_11n_rx_rifs(struct ath_hal *ah, HAL_BOOL enable)
{
    /* Non-Owl 11n chips */
    if ((ath_hal_getcapability(ah, HAL_CAP_RIFS_RX, 0, AH_NULL) == HAL_OK)) {
        if (ar9300_get_capability(ah, HAL_CAP_LDPCWAR, 0, AH_NULL) == HAL_OK) {
            return ar9300_set_rifs_delay(ah, enable);
        }
        return AH_FALSE;
    }

    return AH_TRUE;
} /* ar9300_set_11n_rx_rifs () */

static hal_mac_hangs_t
ar9300_compare_dbg_hang(struct ath_hal *ah, mac_dbg_regs_t mac_dbg,
  hal_mac_hang_check_t hang_check, hal_mac_hangs_t hangs, u_int8_t *dcu_chain)
{
    int i = 0;
    hal_mac_hangs_t found_hangs = 0;

    if (hangs & dcu_chain_state) {
        for (i = 0; i < 6; i++) {
            if (((mac_dbg.dma_dbg_4 >> (5 * i)) & 0x1f) ==
                 hang_check.dcu_chain_state)
            {
                found_hangs |= dcu_chain_state;
                *dcu_chain = i;
            }
        }
        for (i = 0; i < 4; i++) {
            if (((mac_dbg.dma_dbg_5 >> (5 * i)) & 0x1f) ==
                  hang_check.dcu_chain_state)
            {
                found_hangs |= dcu_chain_state;
                *dcu_chain = i + 6;
            }
        }
    }

    if (hangs & dcu_complete_state) {
        if ((mac_dbg.dma_dbg_6 & 0x3) == hang_check.dcu_complete_state) {
            found_hangs |= dcu_complete_state;
        }
    }

    return found_hangs;

} /* end - ar9300_compare_dbg_hang */

#define NUM_STATUS_READS 50
HAL_BOOL
ar9300_detect_mac_hang(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    mac_dbg_regs_t mac_dbg;
    hal_mac_hang_check_t hang_sig1_val = {0x6, 0x1, 0, 0, 0, 0, 0, 0};
    hal_mac_hangs_t      hang_sig1 = (dcu_chain_state | dcu_complete_state);
    int i = 0;
    u_int8_t dcu_chain = 0, current_dcu_chain_state, shift_val;

    if (!(ahp->ah_hang_wars & HAL_MAC_HANG_WAR)) {
        return AH_FALSE;
    }

    OS_MEMZERO(&mac_dbg, sizeof(mac_dbg));

    mac_dbg.dma_dbg_4 = OS_REG_READ(ah, AR_DMADBG_4);
    mac_dbg.dma_dbg_5 = OS_REG_READ(ah, AR_DMADBG_5);
    mac_dbg.dma_dbg_6 = OS_REG_READ(ah, AR_DMADBG_6);

    HALDEBUG(ah, HAL_DEBUG_DFS, " dma regs: %X %X %X \n",
            mac_dbg.dma_dbg_4, mac_dbg.dma_dbg_5,
            mac_dbg.dma_dbg_6);

    if (hang_sig1 != 
            ar9300_compare_dbg_hang(ah, mac_dbg,
                 hang_sig1_val, hang_sig1, &dcu_chain))
    {
        HALDEBUG(ah, HAL_DEBUG_DFS, " hang sig1 not found \n");
        return AH_FALSE;
    }

    shift_val = (dcu_chain >= 6) ? (dcu_chain-6) : (dcu_chain); 
    shift_val *= 5;

    for (i = 1; i <= NUM_STATUS_READS; i++) {
        if (dcu_chain < 6) {
            mac_dbg.dma_dbg_4 = OS_REG_READ(ah, AR_DMADBG_4);
            current_dcu_chain_state = 
                     ((mac_dbg.dma_dbg_4 >> shift_val) & 0x1f); 
        } else {
            mac_dbg.dma_dbg_5 = OS_REG_READ(ah, AR_DMADBG_5);
            current_dcu_chain_state = ((mac_dbg.dma_dbg_5 >> shift_val) & 0x1f);
        }
        mac_dbg.dma_dbg_6 = OS_REG_READ(ah, AR_DMADBG_6);

        if (((mac_dbg.dma_dbg_6 & 0x3) != hang_sig1_val.dcu_complete_state) 
            || (current_dcu_chain_state != hang_sig1_val.dcu_chain_state)) {
            return AH_FALSE;
        }
    }
    HALDEBUG(ah, HAL_DEBUG_DFS, "%s sig5count=%d sig6count=%d ", __func__,
             ahp->ah_hang[MAC_HANG_SIG1], ahp->ah_hang[MAC_HANG_SIG2]);
    ahp->ah_hang[MAC_HANG_SIG1]++;
    return AH_TRUE;

} /* end - ar9300_detect_mac_hang */

/* Determine if the baseband is hung by reading the Observation Bus Register */
HAL_BOOL
ar9300_detect_bb_hang(struct ath_hal *ah)
{
#define N(a) (sizeof(a) / sizeof(a[0]))
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t hang_sig = 0;
    int i = 0;
    /* Check the PCU Observation Bus 1 register (0x806c) NUM_STATUS_READS times
     *
     * 4 known BB hang signatures -
     * [1] bits 8,9,11 are 0. State machine state (bits 25-31) is 0x1E
     * [2] bits 8,9 are 1, bit 11 is 0. State machine state (bits 25-31) is 0x52
     * [3] bits 8,9 are 1, bit 11 is 0. State machine state (bits 25-31) is 0x18
     * [4] bit 10 is 1, bit 11 is 0. WEP state (bits 12-17) is 0x2,
     *     Rx State (bits 20-24) is 0x7.
     */
    hal_hw_hang_check_t hang_list [] =
    {
     /* Offset        Reg Value   Reg Mask    Hang Offset */
       {AR_OBS_BUS_1, 0x1E000000, 0x7E000B00, BB_HANG_SIG1},
       {AR_OBS_BUS_1, 0x52000B00, 0x7E000B00, BB_HANG_SIG2},
       {AR_OBS_BUS_1, 0x18000B00, 0x7E000B00, BB_HANG_SIG3},
       {AR_OBS_BUS_1, 0x00702400, 0x7E7FFFEF, BB_HANG_SIG4}
    };

    if (!(ahp->ah_hang_wars & (HAL_RIFS_BB_HANG_WAR |
                               HAL_DFS_BB_HANG_WAR |
                               HAL_RX_STUCK_LOW_BB_HANG_WAR))) {
        return AH_FALSE;
    }

    hang_sig = OS_REG_READ(ah, AR_OBS_BUS_1);
    for (i = 1; i <= NUM_STATUS_READS; i++) {
        if (hang_sig != OS_REG_READ(ah, AR_OBS_BUS_1)) {
            return AH_FALSE;
        }
    }

    for (i = 0; i < N(hang_list); i++) {
        if ((hang_sig & hang_list[i].hang_mask) == hang_list[i].hang_val) {
            ahp->ah_hang[hang_list[i].hang_offset]++;
            HALDEBUG(ah, HAL_DEBUG_DFS, "%s sig1count=%d sig2count=%d "
                     "sig3count=%d sig4count=%d\n", __func__,
                     ahp->ah_hang[BB_HANG_SIG1], ahp->ah_hang[BB_HANG_SIG2],
                     ahp->ah_hang[BB_HANG_SIG3], ahp->ah_hang[BB_HANG_SIG4]);
            return AH_TRUE;
        }
    }

    HALDEBUG(ah, HAL_DEBUG_DFS, "%s Found an unknown BB hang signature! "
                              "<0x806c>=0x%x\n", __func__, hang_sig);

    return AH_FALSE;

#undef N
} /* end - ar9300_detect_bb_hang () */

#undef NUM_STATUS_READS

HAL_STATUS
ar9300_select_ant_config(struct ath_hal *ah, u_int32_t cfg)
{
    struct ath_hal_9300     *ahp = AH9300(ah);
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    HAL_CHANNEL_INTERNAL    *ichan = ath_hal_checkchannel(ah, chan);
    const HAL_CAPABILITIES  *p_cap = &AH_PRIVATE(ah)->ah_caps;
    u_int16_t               ant_config;
    u_int32_t               hal_num_ant_config;

    hal_num_ant_config = IS_CHAN_2GHZ(ichan) ?
        p_cap->halNumAntCfg2GHz: p_cap->halNumAntCfg5GHz;

    if (cfg < hal_num_ant_config) {
        if (HAL_OK == ar9300_eeprom_get_ant_cfg(ahp, chan, cfg, &ant_config)) {
            OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, ant_config);
            return HAL_OK;
        }
    }

    return HAL_EINVAL;
}

/*
 * Functions to get/set DCS mode
 */
void
ar9300_set_dcs_mode(struct ath_hal *ah, u_int32_t mode)
{
    AH9300(ah)->ah_dcs_enable = mode;
}

u_int32_t
ar9300_get_dcs_mode(struct ath_hal *ah)
{
    return AH9300(ah)->ah_dcs_enable;
}

#if ATH_BT_COEX
void
ar9300_set_bt_coex_info(struct ath_hal *ah, HAL_BT_COEX_INFO *btinfo)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp->ah_bt_module = btinfo->bt_module;
    ahp->ah_bt_coex_config_type = btinfo->bt_coex_config;
    ahp->ah_bt_active_gpio_select = btinfo->bt_gpio_bt_active;
    ahp->ah_bt_priority_gpio_select = btinfo->bt_gpio_bt_priority;
    ahp->ah_wlan_active_gpio_select = btinfo->bt_gpio_wlan_active;
    ahp->ah_bt_active_polarity = btinfo->bt_active_polarity;
    ahp->ah_bt_coex_single_ant = btinfo->bt_single_ant;
    ahp->ah_bt_wlan_isolation = btinfo->bt_isolation;
}

void
ar9300_bt_coex_config(struct ath_hal *ah, HAL_BT_COEX_CONFIG *btconf)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL rx_clear_polarity;

    /*
     * For Kiwi and Osprey, the polarity of rx_clear is active high.
     * The bt_rxclear_polarity flag from ath_dev needs to be inverted.
     */
    rx_clear_polarity = !btconf->bt_rxclear_polarity;

    ahp->ah_bt_coex_mode = (ahp->ah_bt_coex_mode & AR_BT_QCU_THRESH) |
        SM(btconf->bt_time_extend, AR_BT_TIME_EXTEND) |
        SM(btconf->bt_txstate_extend, AR_BT_TXSTATE_EXTEND) |
        SM(btconf->bt_txframe_extend, AR_BT_TX_FRAME_EXTEND) |
        SM(btconf->bt_mode, AR_BT_MODE) |
        SM(btconf->bt_quiet_collision, AR_BT_QUIET) |
        SM(rx_clear_polarity, AR_BT_RX_CLEAR_POLARITY) |
        SM(btconf->bt_priority_time, AR_BT_PRIORITY_TIME) |
        SM(btconf->bt_first_slot_time, AR_BT_FIRST_SLOT_TIME);

    ahp->ah_bt_coex_mode2 |= SM(btconf->bt_hold_rxclear, AR_BT_HOLD_RX_CLEAR);

    if (ahp->ah_bt_coex_single_ant == AH_FALSE) {
        /* Enable ACK to go out even though BT has higher priority. */
        ahp->ah_bt_coex_mode2 |= AR_BT_DISABLE_BT_ANT;
    }
}

void
ar9300_bt_coex_set_qcu_thresh(struct ath_hal *ah, int qnum)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* clear the old value, then set the new value */
    ahp->ah_bt_coex_mode &= ~AR_BT_QCU_THRESH;
    ahp->ah_bt_coex_mode |= SM(qnum, AR_BT_QCU_THRESH);
}

void
ar9300_bt_coex_set_weights(struct ath_hal *ah, u_int32_t stomp_type)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp->ah_bt_coex_bt_weight[0] = AR9300_BT_WGHT;
    ahp->ah_bt_coex_bt_weight[1] = AR9300_BT_WGHT;
    ahp->ah_bt_coex_bt_weight[2] = AR9300_BT_WGHT;
    ahp->ah_bt_coex_bt_weight[3] = AR9300_BT_WGHT;

    switch (stomp_type) {
    case HAL_BT_COEX_STOMP_ALL:
        ahp->ah_bt_coex_wlan_weight[0] = AR9300_STOMP_ALL_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = AR9300_STOMP_ALL_WLAN_WGHT1;
        break;
    case HAL_BT_COEX_STOMP_LOW:
        ahp->ah_bt_coex_wlan_weight[0] = AR9300_STOMP_LOW_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = AR9300_STOMP_LOW_WLAN_WGHT1;
        break;
    case HAL_BT_COEX_STOMP_ALL_FORCE:
        ahp->ah_bt_coex_wlan_weight[0] = AR9300_STOMP_ALL_FORCE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = AR9300_STOMP_ALL_FORCE_WLAN_WGHT1;
        break;
    case HAL_BT_COEX_STOMP_LOW_FORCE:
        ahp->ah_bt_coex_wlan_weight[0] = AR9300_STOMP_LOW_FORCE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = AR9300_STOMP_LOW_FORCE_WLAN_WGHT1;
        break;
    case HAL_BT_COEX_STOMP_NONE:
    case HAL_BT_COEX_NO_STOMP:
        ahp->ah_bt_coex_wlan_weight[0] = AR9300_STOMP_NONE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = AR9300_STOMP_NONE_WLAN_WGHT1;
        break;
    default:
        /* There is a force_weight from registry */
        ahp->ah_bt_coex_wlan_weight[0] = stomp_type;
        ahp->ah_bt_coex_wlan_weight[1] = stomp_type;
        break;
    }
}

void
ar9300_bt_coex_setup_bmiss_thresh(struct ath_hal *ah, u_int32_t thresh)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* clear the old value, then set the new value */
    ahp->ah_bt_coex_mode2 &= ~AR_BT_BCN_MISS_THRESH;
    ahp->ah_bt_coex_mode2 |= SM(thresh, AR_BT_BCN_MISS_THRESH);
}

static void
ar9300_bt_coex_antenna_diversity(struct ath_hal *ah, u_int32_t value)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
#if ATH_ANT_DIV_COMB
    //struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
#endif

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called, value=%d\n", __func__, value);

    if (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_ANT_DIV_ALLOW)
    {
        if (ahp->ah_diversity_control == HAL_ANT_VARIABLE)
        {
            /* Config antenna diversity */
#if ATH_ANT_DIV_COMB
            ar9300_ant_ctrl_set_lna_div_use_bt_ant(ah, value, chan);
#endif
        }
    }
}


void
ar9300_bt_coex_set_parameter(struct ath_hal *ah, u_int32_t type,
    u_int32_t value)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);

    switch (type) {
        case HAL_BT_COEX_SET_ACK_PWR:
            if (value) {
                ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_LOW_ACK_PWR;
            } else {
                ahp->ah_bt_coex_flag &= ~HAL_BT_COEX_FLAG_LOW_ACK_PWR;
            }
            ar9300_set_tx_power_limit(ah, ahpriv->ah_powerLimit,
                ahpriv->ah_extraTxPow, 0);
            break;

        case HAL_BT_COEX_ANTENNA_DIVERSITY:
            if (AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
                ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_ANT_DIV_ALLOW;
                if (value) {
                    ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_ANT_DIV_ENABLE;
                }
                else {
                    ahp->ah_bt_coex_flag &= ~HAL_BT_COEX_FLAG_ANT_DIV_ENABLE;
                }
                ar9300_bt_coex_antenna_diversity(ah, value);
            }
            break;
        case HAL_BT_COEX_LOWER_TX_PWR:
            if (value) {
                ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_LOWER_TX_PWR;
            }
            else {
                ahp->ah_bt_coex_flag &= ~HAL_BT_COEX_FLAG_LOWER_TX_PWR;
            }
            ar9300_set_tx_power_limit(ah, ahpriv->ah_powerLimit,
                                      ahpriv->ah_extraTxPow, 0);
            break;
#if ATH_SUPPORT_MCI
        case HAL_BT_COEX_MCI_MAX_TX_PWR:
            if ((ah->ah_config.ath_hal_mci_config & 
                 ATH_MCI_CONFIG_CONCUR_TX) == ATH_MCI_CONCUR_TX_SHARED_CHN)
            {
                if (value) {
                    ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR;
                    ahp->ah_mci_concur_tx_en = AH_TRUE;
                }
                else {
                    ahp->ah_bt_coex_flag &= ~HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR;
                    ahp->ah_mci_concur_tx_en = AH_FALSE;
                }
                ar9300_set_tx_power_limit(ah, ahpriv->ah_powerLimit,
                                          ahpriv->ah_extraTxPow, 0);
            }
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) concur_tx_en = %d\n", 
                     ahp->ah_mci_concur_tx_en);
            break;
        case HAL_BT_COEX_MCI_FTP_STOMP_RX:
            if (value) {
                ahp->ah_bt_coex_flag |= HAL_BT_COEX_FLAG_MCI_FTP_STOMP_RX;
            }
            else {
                ahp->ah_bt_coex_flag &= ~HAL_BT_COEX_FLAG_MCI_FTP_STOMP_RX;
            }
            break;
#endif
        default:
            break;
    }
}

void
ar9300_bt_coex_disable(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* Always drive rx_clear_external output as 0 */
    ath_hal_gpioCfgOutput(ah, ahp->ah_wlan_active_gpio_select,
        HAL_GPIO_OUTPUT_MUX_AS_OUTPUT);

    if (ahp->ah_bt_coex_single_ant == AH_TRUE) {
        OS_REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1); 
        OS_REG_RMW_FIELD(ah, AR_PCU_MISC, AR_PCU_BT_ANT_PREVENT_RX, 0);
    }

    OS_REG_WRITE(ah, AR_BT_COEX_MODE, AR_BT_QUIET | AR_BT_MODE);
    OS_REG_WRITE(ah, AR_BT_COEX_MODE2, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS0, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS1, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS0, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS1, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS2, 0);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS3, 0);

    ahp->ah_bt_coex_enabled = AH_FALSE;
}

int
ar9300_bt_coex_enable(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* Program coex mode and weight registers to actually enable coex */
    OS_REG_WRITE(ah, AR_BT_COEX_MODE, ahp->ah_bt_coex_mode);
    OS_REG_WRITE(ah, AR_BT_COEX_MODE2, ahp->ah_bt_coex_mode2);
    OS_REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS0, ahp->ah_bt_coex_wlan_weight[0]);
    OS_REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS1, ahp->ah_bt_coex_wlan_weight[1]);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS0, ahp->ah_bt_coex_bt_weight[0]);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS1, ahp->ah_bt_coex_bt_weight[1]);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS2, ahp->ah_bt_coex_bt_weight[2]);
    OS_REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS3, ahp->ah_bt_coex_bt_weight[3]);

    if (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_LOW_ACK_PWR) {
        OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_LOW_ACK_POWER);
    } else {
        OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_HIGH_ACK_POWER);
    }

    OS_REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
    if (ahp->ah_bt_coex_single_ant == AH_TRUE) {       
        OS_REG_RMW_FIELD(ah, AR_PCU_MISC, AR_PCU_BT_ANT_PREVENT_RX, 1);
    } else {
        OS_REG_RMW_FIELD(ah, AR_PCU_MISC, AR_PCU_BT_ANT_PREVENT_RX, 0);
    }

    if (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_3WIRE) {
        /* For 3-wire, configure the desired GPIO port for rx_clear */
        ath_hal_gpioCfgOutput(ah,
            ahp->ah_wlan_active_gpio_select,
            HAL_GPIO_OUTPUT_MUX_AS_WLAN_ACTIVE);
    }
    else if ((ahp->ah_bt_coex_config_type >= HAL_BT_COEX_CFG_2WIRE_2CH) &&
        (ahp->ah_bt_coex_config_type <= HAL_BT_COEX_CFG_2WIRE_CH0))
    {
        /* For 2-wire, configure the desired GPIO port for TX_FRAME output */
        ath_hal_gpioCfgOutput(ah,
            ahp->ah_wlan_active_gpio_select,
            HAL_GPIO_OUTPUT_MUX_AS_TX_FRAME);
    }

    /*
     * Enable a weak pull down on BT_ACTIVE.
     * When BT device is disabled, BT_ACTIVE might be floating.
     */
    OS_REG_RMW(ah, AR_HOSTIF_REG(ah, AR_GPIO_PDPU),
        (AR_GPIO_PULL_DOWN << (ahp->ah_bt_active_gpio_select * 2)), 
        (AR_GPIO_PDPU_OPTION << (ahp->ah_bt_active_gpio_select * 2)));

    ahp->ah_bt_coex_enabled = AH_TRUE;

    return 0;
}

u_int32_t ar9300_get_bt_active_gpio(struct ath_hal *ah, u_int32_t reg)
{
    return 0;
}

u_int32_t ar9300_get_wlan_active_gpio(struct ath_hal *ah, u_int32_t reg,u_int32_t bOn)
{
    return bOn;
}

void
ar9300_init_bt_coex(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_3WIRE) {
        OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL),
                   (AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB |
                    AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB));

        /*
         * Set input mux for bt_prority_async and
         * bt_active_async to GPIO pins
         */
        OS_REG_RMW_FIELD(ah,
            AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1),
            AR_GPIO_INPUT_MUX1_BT_ACTIVE,
            ahp->ah_bt_active_gpio_select);
        OS_REG_RMW_FIELD(ah,
            AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1),
            AR_GPIO_INPUT_MUX1_BT_PRIORITY,
            ahp->ah_bt_priority_gpio_select);

        /* Configure the desired GPIO ports for input */
        ath_hal_gpioCfgInput(ah, ahp->ah_bt_active_gpio_select);
        ath_hal_gpioCfgInput(ah, ahp->ah_bt_priority_gpio_select);

        if (ahp->ah_bt_coex_enabled) {
            ar9300_bt_coex_enable(ah);
        } else {
            ar9300_bt_coex_disable(ah);
        }
    }
    else if ((ahp->ah_bt_coex_config_type >= HAL_BT_COEX_CFG_2WIRE_2CH) &&
        (ahp->ah_bt_coex_config_type <= HAL_BT_COEX_CFG_2WIRE_CH0))
    {
        /* 2-wire */
        if (ahp->ah_bt_coex_enabled) {
            /* Connect bt_active_async to baseband */
            OS_REG_CLR_BIT(ah,
                AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL), 
                (AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF |
                 AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF));
            OS_REG_SET_BIT(ah,
                AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL),
                AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB);

            /*
             * Set input mux for bt_prority_async and
             * bt_active_async to GPIO pins
             */
            OS_REG_RMW_FIELD(ah,
                AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1),
                AR_GPIO_INPUT_MUX1_BT_ACTIVE,
                ahp->ah_bt_active_gpio_select);

            /* Configure the desired GPIO ports for input */
            ath_hal_gpioCfgInput(ah, ahp->ah_bt_active_gpio_select);

            /* Enable coexistence on initialization */
            ar9300_bt_coex_enable(ah);
        }
    }
#if ATH_SUPPORT_MCI
    else if (ahp->ah_bt_coex_config_type == HAL_BT_COEX_CFG_MCI) {
        if (ahp->ah_bt_coex_enabled) {
            ar9300_mci_bt_coex_enable(ah);
        }
        else {
            ar9300_mci_bt_coex_disable(ah);
        }
    }
#endif /* ATH_SUPPORT_MCI */
}

#endif /* ATH_BT_COEX */

HAL_STATUS ar9300_set_proxy_sta(struct ath_hal *ah, HAL_BOOL enable)
{
    u_int32_t val;
    int wasp_mm_rev;

#define AR_SOC_RST_REVISION_ID      0xB8060090
#define REG_READ(_reg)              *((volatile u_int32_t *)(_reg))
    wasp_mm_rev = (REG_READ(AR_SOC_RST_REVISION_ID) &
            AR_SREV_REVISION_WASP_MINOR_MINOR_MASK) >>
            AR_SREV_REVISION_WASP_MINOR_MINOR_SHIFT;
#undef AR_SOC_RST_REVISION_ID
#undef REG_READ

    /*
     * Azimuth (ProxySTA) Mode is only supported correctly by
     * Peacock or WASP 1.3.0.1 or later (hopefully) chips.
     *
     * Enable this feature for Scorpion at this time. The silicon
     * still needs to be validated.
     */
    if (!(AH_PRIVATE((ah))->ah_macVersion == AR_SREV_VERSION_AR9580) && 
        !(AH_PRIVATE((ah))->ah_macVersion == AR_SREV_VERSION_SCORPION) && 
        !((AH_PRIVATE((ah))->ah_macVersion == AR_SREV_VERSION_WASP) &&  
          ((AH_PRIVATE((ah))->ah_macRev > AR_SREV_REVISION_WASP_13) ||
           (AH_PRIVATE((ah))->ah_macRev == AR_SREV_REVISION_WASP_13 && 
            wasp_mm_rev >= 0 /* 1 */))))
    {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s error: current chip (ver 0x%x, "
                "rev 0x%x, minor minor rev 0x%x) cannot support Azimuth Mode\n",
                __func__, AH_PRIVATE((ah))->ah_macVersion,
                AH_PRIVATE((ah))->ah_macRev, wasp_mm_rev);
        return HAL_ENOTSUPP;
    }

    OS_REG_WRITE(ah,
        AR_MAC_PCU_LOGIC_ANALYZER, AR_MAC_PCU_LOGIC_ANALYZER_PSTABUG75996);

    /* turn on mode bit[24] for proxy sta */
    OS_REG_WRITE(ah, AR_PCU_MISC_MODE2, 
        OS_REG_READ(ah, AR_PCU_MISC_MODE2) | AR_PCU_MISC_MODE2_PROXY_STA);

    val = OS_REG_READ(ah, AR_AZIMUTH_MODE);
    if (enable) {
        val |= AR_AZIMUTH_KEY_SEARCH_AD1 | 
               AR_AZIMUTH_CTS_MATCH_TX_AD2 | 
               AR_AZIMUTH_BA_USES_AD1;
        /* turn off filter pass hold (bit 9) */
        val &= ~AR_AZIMUTH_FILTER_PASS_HOLD;
    } else {
        val &= ~(AR_AZIMUTH_KEY_SEARCH_AD1 | 
                 AR_AZIMUTH_CTS_MATCH_TX_AD2 | 
                 AR_AZIMUTH_BA_USES_AD1);
    }
    OS_REG_WRITE(ah, AR_AZIMUTH_MODE, val);

    /* enable promiscous mode */
    OS_REG_WRITE(ah, AR_RX_FILTER, 
        OS_REG_READ(ah, AR_RX_FILTER) | HAL_RX_FILTER_PROM);
    /* enable promiscous in azimuth mode */
    OS_REG_WRITE(ah, AR_PCU_MISC_MODE2, AR_PCU_MISC_MODE2_PROM_VC_MODE);
    OS_REG_WRITE(ah, AR_MAC_PCU_LOGIC_ANALYZER, AR_MAC_PCU_LOGIC_ANALYZER_VC_MODE);

    /* turn on filter pass hold (bit 9) */
    OS_REG_WRITE(ah, AR_AZIMUTH_MODE,
        OS_REG_READ(ah, AR_AZIMUTH_MODE) | AR_AZIMUTH_FILTER_PASS_HOLD);

    return HAL_OK;
}

#if 0
void ar9300_mat_enable(struct ath_hal *ah, int enable)
{
    /*
     * MAT (s/w ProxySTA) implementation requires to turn off interrupt
     * mitigation and turn on key search always for better performance.
     */
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_hal_private *ap = AH_PRIVATE(ah);

    ahp->ah_intr_mitigation_rx = !enable;
    if (ahp->ah_intr_mitigation_rx) {
        /*
         * Enable Interrupt Mitigation for Rx.
         * If no build-specific limits for the rx interrupt mitigation
         * timer have been specified, use conservative defaults.
         */
        #ifndef AH_RIMT_VAL_LAST
            #define AH_RIMT_LAST_MICROSEC 500
        #endif
        #ifndef AH_RIMT_VAL_FIRST
            #define AH_RIMT_FIRST_MICROSEC 2000
        #endif
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, AH_RIMT_LAST_MICROSEC);
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, AH_RIMT_FIRST_MICROSEC);
    } else {
        OS_REG_WRITE(ah, AR_RIMT, 0);
    }

    ahp->ah_enable_keysearch_always = !!enable;
    ar9300_enable_keysearch_always(ah, ahp->ah_enable_keysearch_always);
}
#endif

void ar9300_enable_tpc(struct ath_hal *ah)
{
    u_int32_t val = 0;

    ah->ah_config.ath_hal_desc_tpc = 1;

    /* Enable TPC */
    OS_REG_RMW_FIELD(ah, AR_PHY_PWRTX_MAX, AR_PHY_PER_PACKET_POWERTX_MAX, 1);

    /*
     * Disable per chain power reduction since we are already
     * accounting for this in our calculations
     */
    val = OS_REG_READ(ah, AR_PHY_POWER_TX_SUB);
    if (AR_SREV_WASP(ah)) {
        OS_REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
                         val & AR_PHY_POWER_TX_SUB_2_DISABLE);
    } else {
        OS_REG_WRITE(ah, AR_PHY_POWER_TX_SUB,
                         val & AR_PHY_POWER_TX_SUB_3_DISABLE);
    }
}


/*
 * ar9300_force_tsf_sync 
 * This function forces the TSF sync to the given bssid, this is implemented
 * as a temp hack to get the AoW demo, and is primarily used in the WDS client
 * mode of operation, where we sync the TSF to RootAP TSF values
 */
void
ar9300_force_tsf_sync(struct ath_hal *ah, const u_int8_t *bssid,
    u_int16_t assoc_id)
{
    ar9300_set_operating_mode(ah, HAL_M_STA);
    ar9300_write_associd(ah, bssid, assoc_id);
}

void ar9300_chk_rssi_update_tx_pwr(struct ath_hal *ah, int rssi)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t           temp_obdb_reg_val = 0, temp_tcp_reg_val;
    u_int32_t           temp_powertx_rate9_reg_val;
    int8_t              olpc_power_offset = 0;
    int8_t              tmp_olpc_val = 0;
    HAL_RSSI_TX_POWER   old_greentx_status;
    u_int8_t            target_power_val_t[ar9300_rate_size];
    int8_t              tmp_rss1_thr1, tmp_rss1_thr2;

    if ((AH_PRIVATE(ah)->ah_opmode != HAL_M_STA) || 
        !ah->ah_config.ath_hal_sta_update_tx_pwr_enable) {
        return;
    }
    
    old_greentx_status = AH9300(ah)->green_tx_status;
    if (ahp->ah_hw_green_tx_enable) {
        tmp_rss1_thr1 = AR9485_HW_GREEN_TX_THRES1_DB;
        tmp_rss1_thr2 = AR9485_HW_GREEN_TX_THRES2_DB;
    } else {
        tmp_rss1_thr1 = WB225_SW_GREEN_TX_THRES1_DB;
        tmp_rss1_thr2 = WB225_SW_GREEN_TX_THRES2_DB;
    }
    
    if ((ah->ah_config.ath_hal_sta_update_tx_pwr_enable_S1) 
        && (rssi > tmp_rss1_thr1)) 
    {
        if (old_greentx_status != HAL_RSSI_TX_POWER_SHORT) {
            AH9300(ah)->green_tx_status = HAL_RSSI_TX_POWER_SHORT;
        }
    } else if (ah->ah_config.ath_hal_sta_update_tx_pwr_enable_S2 
        && (rssi > tmp_rss1_thr2)) 
    {
        if (old_greentx_status != HAL_RSSI_TX_POWER_MIDDLE) {
            AH9300(ah)->green_tx_status = HAL_RSSI_TX_POWER_MIDDLE;
        }
    } else if (ah->ah_config.ath_hal_sta_update_tx_pwr_enable_S3) {
        if (old_greentx_status != HAL_RSSI_TX_POWER_LONG) {
            AH9300(ah)->green_tx_status = HAL_RSSI_TX_POWER_LONG;
        }
    }

    /* If status is not change, don't do anything */
    if (old_greentx_status == AH9300(ah)->green_tx_status) {
        return;
    }
    
    /* for Poseidon which ath_hal_sta_update_tx_pwr_enable is enabled */
    if ((AH9300(ah)->green_tx_status != HAL_RSSI_TX_POWER_NONE) 
        && AR_SREV_POSEIDON(ah)) 
    {
        if (ahp->ah_hw_green_tx_enable) {
            switch (AH9300(ah)->green_tx_status) {
            case HAL_RSSI_TX_POWER_SHORT:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, ar9485_hw_gtx_tp_distance_short,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 0;
                /* 2. Store OB/DB */
                /* 3. Store TPC settting */
                temp_tcp_reg_val = (SM(14, AR_TPC_ACK) |
                                    SM(14, AR_TPC_CTS) |
                                    SM(14, AR_TPC_CHIRP) |
                                    SM(14, AR_TPC_RPT));
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                    AR9485_BBPWRTXRATE9_HW_GREEN_TX_SHORT_VALUE;
                break;
            case HAL_RSSI_TX_POWER_MIDDLE:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, ar9485_hw_gtx_tp_distance_middle,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 0;
                /* 2. Store OB/DB */
                /* 3. Store TPC settting */
                temp_tcp_reg_val = (SM(18, AR_TPC_ACK) |
                                    SM(18, AR_TPC_CTS) |
                                    SM(18, AR_TPC_CHIRP) |
                                    SM(18, AR_TPC_RPT));
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                    AR9485_BBPWRTXRATE9_HW_GREEN_TX_MIDDLE_VALUE;
                break;
            case HAL_RSSI_TX_POWER_LONG:
            default:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, ahp->ah_default_tx_power,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 0;
                /* 2. Store OB/DB1/DB2 */
                /* 3. Store TPC settting */
                temp_tcp_reg_val = 
                    AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_TPC];
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                  AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_BB_PWRTX_RATE9];
                break;
            }
        } else {
            switch (AH9300(ah)->green_tx_status) {
            case HAL_RSSI_TX_POWER_SHORT:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, wb225_sw_gtx_tp_distance_short,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 
                    wb225_gtx_olpc_cal_offset[WB225_OB_GREEN_TX_SHORT_VALUE] -
                    wb225_gtx_olpc_cal_offset[WB225_OB_CALIBRATION_VALUE];
                /* 2. Store OB/DB */
                temp_obdb_reg_val =
                    AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_OBDB];
                temp_obdb_reg_val &= ~(AR_PHY_65NM_CH0_TXRF2_DB2G | 
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_CCK |
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_PSK |
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_QAM);
                temp_obdb_reg_val |= (SM(5, AR_PHY_65NM_CH0_TXRF2_DB2G) |
                SM(WB225_OB_GREEN_TX_SHORT_VALUE,
                    AR_PHY_65NM_CH0_TXRF2_OB2G_CCK) |
                SM(WB225_OB_GREEN_TX_SHORT_VALUE,
                    AR_PHY_65NM_CH0_TXRF2_OB2G_PSK) |
                SM(WB225_OB_GREEN_TX_SHORT_VALUE,
                    AR_PHY_65NM_CH0_TXRF2_OB2G_QAM));
                /* 3. Store TPC settting */
                temp_tcp_reg_val = (SM(6, AR_TPC_ACK) |
                                    SM(6, AR_TPC_CTS) |
                                    SM(6, AR_TPC_CHIRP) |
                                    SM(6, AR_TPC_RPT));
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                    WB225_BBPWRTXRATE9_SW_GREEN_TX_SHORT_VALUE;
                break;
            case HAL_RSSI_TX_POWER_MIDDLE:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, wb225_sw_gtx_tp_distance_middle,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 
                    wb225_gtx_olpc_cal_offset[WB225_OB_GREEN_TX_MIDDLE_VALUE] -
                    wb225_gtx_olpc_cal_offset[WB225_OB_CALIBRATION_VALUE];
                /* 2. Store OB/DB */
                temp_obdb_reg_val =
                    AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_OBDB];
                temp_obdb_reg_val &= ~(AR_PHY_65NM_CH0_TXRF2_DB2G | 
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_CCK |
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_PSK |
                                       AR_PHY_65NM_CH0_TXRF2_OB2G_QAM);
                temp_obdb_reg_val |= (SM(5, AR_PHY_65NM_CH0_TXRF2_DB2G) |
                    SM(WB225_OB_GREEN_TX_MIDDLE_VALUE,
                        AR_PHY_65NM_CH0_TXRF2_OB2G_CCK) |
                    SM(WB225_OB_GREEN_TX_MIDDLE_VALUE,
                        AR_PHY_65NM_CH0_TXRF2_OB2G_PSK) |
                    SM(WB225_OB_GREEN_TX_MIDDLE_VALUE,
                        AR_PHY_65NM_CH0_TXRF2_OB2G_QAM));
                /* 3. Store TPC settting */
                temp_tcp_reg_val = (SM(14, AR_TPC_ACK) |
                                    SM(14, AR_TPC_CTS) |
                                    SM(14, AR_TPC_CHIRP) |
                                    SM(14, AR_TPC_RPT));
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                    WB225_BBPWRTXRATE9_SW_GREEN_TX_MIDDLE_VALUE;
                break;
            case HAL_RSSI_TX_POWER_LONG:
            default:
                /* 1. TxPower Config */
                OS_MEMCPY(target_power_val_t, ahp->ah_default_tx_power,
                    sizeof(target_power_val_t));
                /* 1.1 Store OLPC Delta Calibration Offset*/
                olpc_power_offset = 
                    wb225_gtx_olpc_cal_offset[WB225_OB_GREEN_TX_LONG_VALUE] -
                    wb225_gtx_olpc_cal_offset[WB225_OB_CALIBRATION_VALUE];
                /* 2. Store OB/DB1/DB2 */
                temp_obdb_reg_val =
                    AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_OBDB];
                /* 3. Store TPC settting */
                temp_tcp_reg_val =
                    AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_TPC];
                /* 4. Store BB_powertx_rate9 value */
                temp_powertx_rate9_reg_val = 
                  AH9300(ah)->ah_ob_db1[POSEIDON_STORED_REG_BB_PWRTX_RATE9];
                break;
            }
        }
        /* 1.1 Do OLPC Delta Calibration Offset */
        tmp_olpc_val = 
            (int8_t) AH9300(ah)->ah_db2[POSEIDON_STORED_REG_G2_OLPC_OFFSET];
        tmp_olpc_val += olpc_power_offset;
        OS_REG_RMW(ah, AR_PHY_TPC_11_B0, 
            (tmp_olpc_val << AR_PHY_TPC_OLPC_GAIN_DELTA_S), 
            AR_PHY_TPC_OLPC_GAIN_DELTA);
 
        /* 1.2 TxPower Config */
        ar9300_transmit_power_reg_write(ah, target_power_val_t);     
        /* 2. Config OB/DB */
        if (!ahp->ah_hw_green_tx_enable) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF2, temp_obdb_reg_val);
        }
        /* 3. config TPC settting */
        OS_REG_WRITE(ah, AR_TPC, temp_tcp_reg_val);
        /* 4. config BB_powertx_rate9 value */
        OS_REG_WRITE(ah, AR_PHY_BB_POWERTX_RATE9, temp_powertx_rate9_reg_val);
    }
}

#if 0
void
ar9300_get_vow_stats(
    struct ath_hal *ah, HAL_VOWSTATS* p_stats, u_int8_t vow_reg_flags)
{
    if (vow_reg_flags & AR_REG_TX_FRM_CNT) {
        p_stats->tx_frame_count = OS_REG_READ(ah, AR_TFCNT);
    }
    if (vow_reg_flags & AR_REG_RX_FRM_CNT) {
        p_stats->rx_frame_count = OS_REG_READ(ah, AR_RFCNT);
    }
    if (vow_reg_flags & AR_REG_RX_CLR_CNT) {
        p_stats->rx_clear_count = OS_REG_READ(ah, AR_RCCNT);
    }
    if (vow_reg_flags & AR_REG_CYCLE_CNT) {
        p_stats->cycle_count   = OS_REG_READ(ah, AR_CCCNT);
    }
    if (vow_reg_flags & AR_REG_EXT_CYCLE_CNT) {
        p_stats->ext_cycle_count   = OS_REG_READ(ah, AR_EXTRCCNT);
    }
}
#endif

/*
 * ar9300_is_skip_paprd_by_greentx
 *
 * This function check if we need to skip PAPRD tuning 
 * when GreenTx in specific state.
 */
HAL_BOOL
ar9300_is_skip_paprd_by_greentx(struct ath_hal *ah)
{
    if (AR_SREV_POSEIDON(ah) && 
        ah->ah_config.ath_hal_sta_update_tx_pwr_enable &&
        ((AH9300(ah)->green_tx_status == HAL_RSSI_TX_POWER_SHORT) || 
         (AH9300(ah)->green_tx_status == HAL_RSSI_TX_POWER_MIDDLE))) 
    {
        return AH_TRUE;
    }
    return AH_FALSE;
}

void
ar9300_control_signals_for_green_tx_mode(struct ath_hal *ah)
{
    unsigned int valid_obdb_0_b0 = 0x2d; // 5,5 - dB[0:2],oB[5:3]  
    unsigned int valid_obdb_1_b0 = 0x25; // 4,5 - dB[0:2],oB[5:3]  
    unsigned int valid_obdb_2_b0 = 0x1d; // 3,5 - dB[0:2],oB[5:3] 
    unsigned int valid_obdb_3_b0 = 0x15; // 2,5 - dB[0:2],oB[5:3] 
    unsigned int valid_obdb_4_b0 = 0xd;  // 1,5 - dB[0:2],oB[5:3]
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (AR_SREV_POSEIDON(ah) && ahp->ah_hw_green_tx_enable) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_VALID_OBDB_POSEIDON, 
                             AR_PHY_PAPRD_VALID_OBDB_0, valid_obdb_0_b0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_VALID_OBDB_POSEIDON, 
                             AR_PHY_PAPRD_VALID_OBDB_1, valid_obdb_1_b0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_VALID_OBDB_POSEIDON, 
                             AR_PHY_PAPRD_VALID_OBDB_2, valid_obdb_2_b0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_VALID_OBDB_POSEIDON, 
                             AR_PHY_PAPRD_VALID_OBDB_3, valid_obdb_3_b0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_VALID_OBDB_POSEIDON, 
                             AR_PHY_PAPRD_VALID_OBDB_4, valid_obdb_4_b0);
    }
}

void ar9300_hwgreentx_set_pal_spare(struct ath_hal *ah, int value)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (AR_SREV_POSEIDON(ah) && ahp->ah_hw_green_tx_enable) {
        if ((value == 0) || (value == 1)) {
            OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_TXRF3, 
                             AR_PHY_65NM_CH0_TXRF3_OLD_PAL_SPARE, value);
        }
    }
}

void ar9300_reset_hw_beacon_proc_crc(struct ath_hal *ah)
{
    OS_REG_SET_BIT(ah, AR_HWBCNPROC1, AR_HWBCNPROC1_RESET_CRC);
}

int32_t ar9300_get_hw_beacon_rssi(struct ath_hal *ah)
{
    int32_t val = OS_REG_READ_FIELD(ah, AR_BCN_RSSI_AVE, AR_BCN_RSSI_AVE_VAL);

    /* RSSI format is 8.4.  Ignore lowest four bits */
    val = val >> 4;
    return val;
}

void ar9300_set_hw_beacon_rssi_threshold(struct ath_hal *ah,
                                        u_int32_t rssi_threshold)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    OS_REG_RMW_FIELD(ah, AR_RSSI_THR, AR_RSSI_THR_VAL, rssi_threshold);

    /* save value for restoring after chip reset */
    ahp->ah_beacon_rssi_threshold = rssi_threshold;
}

void ar9300_reset_hw_beacon_rssi(struct ath_hal *ah)
{
    OS_REG_SET_BIT(ah, AR_RSSI_THR, AR_RSSI_BCN_RSSI_RST);
}

void ar9300_set_hw_beacon_proc(struct ath_hal *ah, HAL_BOOL on)
{
    if (on) {
        OS_REG_SET_BIT(ah, AR_HWBCNPROC1, AR_HWBCNPROC1_CRC_ENABLE |
                       AR_HWBCNPROC1_EXCLUDE_TIM_ELM);
    }
    else {
        OS_REG_CLR_BIT(ah, AR_HWBCNPROC1, AR_HWBCNPROC1_CRC_ENABLE |
                       AR_HWBCNPROC1_EXCLUDE_TIM_ELM);
    }
}
/*
 * Gets the contents of the specified key cache entry.
 */
HAL_BOOL
ar9300_print_keycache(struct ath_hal *ah)
{

    const HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;
    u_int32_t key0, key1, key2, key3, key4;
    u_int32_t mac_hi, mac_lo;
    u_int16_t entry = 0;
    u_int32_t valid = 0;
    u_int32_t key_type;

    ath_hal_printf(ah, "Slot   Key\t\t\t          Valid  Type  Mac  \n");

    for (entry = 0 ; entry < p_cap->halKeyCacheSize; entry++) {
        key0 = OS_REG_READ(ah, AR_KEYTABLE_KEY0(entry));
        key1 = OS_REG_READ(ah, AR_KEYTABLE_KEY1(entry));
        key2 = OS_REG_READ(ah, AR_KEYTABLE_KEY2(entry));
        key3 = OS_REG_READ(ah, AR_KEYTABLE_KEY3(entry));
        key4 = OS_REG_READ(ah, AR_KEYTABLE_KEY4(entry));

        key_type = OS_REG_READ(ah, AR_KEYTABLE_TYPE(entry));

        mac_lo = OS_REG_READ(ah, AR_KEYTABLE_MAC0(entry));
        mac_hi = OS_REG_READ(ah, AR_KEYTABLE_MAC1(entry));

        if (mac_hi & AR_KEYTABLE_VALID) {
            valid = 1;
        } else {
            valid = 0;
        }

        if ((mac_hi != 0) && (mac_lo != 0)) {
            mac_hi &= ~0x8000;
            mac_hi <<= 1;
            mac_hi |= ((mac_lo & (1 << 31) )) >> 31;
            mac_lo <<= 1;
        }

        ath_hal_printf(ah,
            "%03d    "
            "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
            "   %02d     %02d    "
            "%02x:%02x:%02x:%02x:%02x:%02x \n",
            entry,
            (key0 << 24) >> 24, (key0 << 16) >> 24,
            (key0 << 8) >> 24, key0 >> 24,
            (key1 << 24) >> 24, (key1 << 16) >> 24,
            //(key1 << 8) >> 24, key1 >> 24,
            (key2 << 24) >> 24, (key2 << 16) >> 24,
            (key2 << 8) >> 24, key2 >> 24,
            (key3 << 24) >> 24, (key3 << 16) >> 24,
            //(key3 << 8) >> 24, key3 >> 24,
            (key4 << 24) >> 24, (key4 << 16) >> 24,
            (key4 << 8) >> 24, key4 >> 24,
            valid, key_type,
            (mac_lo << 24) >> 24, (mac_lo << 16) >> 24, (mac_lo << 8) >> 24,
            (mac_lo) >> 24, (mac_hi << 24) >> 24, (mac_hi << 16) >> 24 );
    }

    return AH_TRUE;
}

/* enable/disable smart antenna mode */
HAL_BOOL
ar9300_set_smart_antenna(struct ath_hal *ah, HAL_BOOL enable)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (enable) {
        OS_REG_SET_BIT(ah, AR_XRTO, AR_ENABLE_SMARTANTENNA);
    } else {
        OS_REG_CLR_BIT(ah, AR_XRTO, AR_ENABLE_SMARTANTENNA);
    }

    /* if scropion and smart antenna is enabled, write swcom1 with 0x440
     * and swcom2 with 0
     * FIXME Ideally these registers need to be made read from caldata.
     * Until the calibration team gets them, keep them along with board
     * configuration.
     */
    if (enable && AR_SREV_SCORPION(ah) &&
           (HAL_OK == ar9300_get_capability(ah, HAL_CAP_SMARTANTENNA, 0,0))) {

       OS_REG_WRITE(ah, AR_PHY_SWITCH_COM, 0x440);
       OS_REG_WRITE(ah, AR_PHY_SWITCH_COM_2, 0);
    }

    ahp->ah_smartantenna_enable = enable;
    return 1;
}

#ifdef ATH_TX99_DIAG
#ifndef ATH_SUPPORT_HTC
void
ar9300_tx99_channel_pwr_update(struct ath_hal *ah, HAL_CHANNEL *c, 
    u_int32_t txpower)
{
#define PWR_MAS(_r, _s)     (((_r) & 0x3f) << (_s))
    static int16_t p_pwr_array[ar9300_rate_size] = { 0 };
    int32_t i;
     
    /* The max power is limited to 63 */
    if (txpower <= AR9300_MAX_RATE_POWER) {
        for (i = 0; i < ar9300_rate_size; i++) {
            p_pwr_array[i] = txpower;
        }
    } else {
        for (i = 0; i < ar9300_rate_size; i++) {
            p_pwr_array[i] = AR9300_MAX_RATE_POWER;
        }
    }

    OS_REG_WRITE(ah, 0xa458, 0);

    /* Write the OFDM power per rate set */
    /* 6 (LSB), 9, 12, 18 (MSB) */
    OS_REG_WRITE(ah, 0xa3c0,
        PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_6_24], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_6_24], 16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_6_24],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_6_24],  0)
    );
    /* 24 (LSB), 36, 48, 54 (MSB) */
    OS_REG_WRITE(ah, 0xa3c4,
        PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_54], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_48], 16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_36],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_6_24],  0)
    );

    /* Write the CCK power per rate set */
    /* 1L (LSB), reserved, 2L, 2S (MSB) */  
    OS_REG_WRITE(ah, 0xa3c8,
        PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_1L_5L], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  16)
          /* | PWR_MAS(txPowerTimes2,  8) */ /* this is reserved for Osprey */
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],   0)
    );
    /* 5.5L (LSB), 5.5S, 11L, 11S (MSB) */
    OS_REG_WRITE(ah, 0xa3cc,
        PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_11S], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_11L], 16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_5S],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_LEGACY_1L_5L],  0)
    );

    /* Write the HT20 power per rate set */
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, 0xa3d0,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT20_5], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_4],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_1_3_9_11_17_19],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_0_8_16],   0)
    );
    
    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, 0xa3d4,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT20_13], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_12],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_7],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_6],   0)
    );

    /* 14 (LSB), 15, 20, 21 */
    OS_REG_WRITE(ah, 0xa3e4,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT20_21], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_20],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_15],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_14],   0)
    );

    /* Mixed HT20 and HT40 rates */
    /* HT20 22 (LSB), HT20 23, HT40 22, HT40 23 (MSB) */
    OS_REG_WRITE(ah, 0xa3e8,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT40_23], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_22],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_23],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT20_22],   0)
    );
    
    /* Write the HT40 power per rate set */
    /* correct PAR difference between HT40 and HT20/LEGACY */
    /* 0/8/16 (LSB), 1-3/9-11/17-19, 4, 5 (MSB) */
    OS_REG_WRITE(ah, 0xa3d8,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT40_5], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_4],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_1_3_9_11_17_19],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_0_8_16],   0)
    );

    /* 6 (LSB), 7, 12, 13 (MSB) */
    OS_REG_WRITE(ah, 0xa3dc,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT40_13], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_12],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_7], 8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_6], 0)
    );

    /* 14 (LSB), 15, 20, 21 */
    OS_REG_WRITE(ah, 0xa3ec,
        PWR_MAS(p_pwr_array[ALL_TARGET_HT40_21], 24)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_20],  16)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_15],  8)
          | PWR_MAS(p_pwr_array[ALL_TARGET_HT40_14],   0)
    );  
#undef PWR_MAS
}

void
ar9300_tx99_chainmsk_setup(struct ath_hal *ah, int tx_chainmask)
{
    if (tx_chainmask == 0x5) {
        OS_REG_WRITE(ah, AR_PHY_ANALOG_SWAP, 
            OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) | AR_PHY_SWAP_ALT_CHAIN);
    }
    OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, tx_chainmask);
    OS_REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, tx_chainmask);

    OS_REG_WRITE(ah, AR_SELFGEN_MASK, tx_chainmask);
    if (tx_chainmask == 0x5) {
        OS_REG_WRITE(ah, AR_PHY_ANALOG_SWAP, 
            OS_REG_READ(ah, AR_PHY_ANALOG_SWAP) | AR_PHY_SWAP_ALT_CHAIN);
    }
}

void
ar9300_tx99_set_single_carrier(struct ath_hal *ah, int tx_chain_mask, 
    int chtype)
{
    OS_REG_WRITE(ah, 0x98a4, OS_REG_READ(ah, 0x98a4) | (0x7ff << 11) | 0x7ff);
    OS_REG_WRITE(ah, 0xa364, OS_REG_READ(ah, 0xa364) | (1 << 7) | (1 << 1));
    OS_REG_WRITE(ah, 0xa350, 
        (OS_REG_READ(ah, 0xa350) | (1 << 31) | (1 << 15)) & ~(1 << 13));
    
    /* 11G mode */
    if (!chtype) {
        OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, 
            OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2) | (0x1 << 3) | (0x1 << 2));
        if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP) & ~(0x1 << 4)); 
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
                        | (0x1 << 26)  | (0x7 << 24)) 
                        & ~(0x1 << 22));
        } else {
            OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, 
                OS_REG_READ(ah, AR_HORNET_CH0_TOP) & ~(0x1 << 4)); 
            OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, 
                (OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
                        | (0x1 << 26)  | (0x7 << 24)) 
                        & ~(0x1 << 22));
        }                                                    
        
        /* chain zero */
        if ((tx_chain_mask & 0x01) == 0x01) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX1, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX1)
                      | (0x1 << 31) | (0x5 << 15) 
                      | (0x3 << 9)) & ~(0x1 << 27) 
                      & ~(0x1 << 12));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                      | (0x1 << 12) | (0x1 << 10) 
                      | (0x1 << 9)  | (0x1 << 8)  
                      | (0x1 << 7)) & ~(0x1 << 11));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
                      | (0x1 << 29) | (0x1 << 25) 
                      | (0x1 << 23) | (0x1 << 19) 
                      | (0x1 << 10) | (0x1 << 9)  
                      | (0x1 << 8)  | (0x1 << 3))
                      & ~(0x1 << 28)& ~(0x1 << 24)
                      & ~(0x1 << 22)& ~(0x1 << 7));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
                      | (0x1 << 23))& ~(0x1 << 21));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB1, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_BB1)
                      | (0x1 << 12) | (0x1 << 10)
                      | (0x1 << 9)  | (0x1 << 8)
                      | (0x1 << 6)  | (0x1 << 5)
                      | (0x1 << 4)  | (0x1 << 3)
                      | (0x1 << 2));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB2, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_BB2) | (0x1 << 31));
        }
        if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            /* chain one */
            if ((tx_chain_mask & 0x02) == 0x02 ) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX1)
                          | (0x1 << 31) | (0x5 << 15) 
                          | (0x3 << 9)) & ~(0x1 << 27) 
                          & ~(0x1 << 12));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                          | (0x1 << 12) | (0x1 << 10) 
                          | (0x1 << 9)  | (0x1 << 8)  
                          | (0x1 << 7)) & ~(0x1 << 11));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
                          | (0x1 << 29) | (0x1 << 25) 
                          | (0x1 << 23) | (0x1 << 19) 
                          | (0x1 << 10) | (0x1 << 9)  
                          | (0x1 << 8)  | (0x1 << 3))
                          & ~(0x1 << 28)& ~(0x1 << 24)
                          & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
                          | (0x1 << 23))& ~(0x1 << 21));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_BB1)
                          | (0x1 << 12) | (0x1 << 10)
                          | (0x1 << 9)  | (0x1 << 8)
                          | (0x1 << 6)  | (0x1 << 5)
                          | (0x1 << 4)  | (0x1 << 3)
                          | (0x1 << 2));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_BB2) | (0x1 << 31));
            }
        }
        if (AR_SREV_OSPREY(ah)) {
            /* chain two */
            if ((tx_chain_mask & 0x04) == 0x04 ) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX1)
                          | (0x1 << 31) | (0x5 << 15) 
                          | (0x3 << 9)) & ~(0x1 << 27)
                          & ~(0x1 << 12));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                          | (0x1 << 12) | (0x1 << 10) 
                          | (0x1 << 9)  | (0x1 << 8)  
                          | (0x1 << 7)) & ~(0x1 << 11));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
                          | (0x1 << 29) | (0x1 << 25) 
                          | (0x1 << 23) | (0x1 << 19) 
                          | (0x1 << 10) | (0x1 << 9)  
                          | (0x1 << 8)  | (0x1 << 3)) 
                          & ~(0x1 << 28)& ~(0x1 << 24) 
                          & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
                          | (0x1 << 23))& ~(0x1 << 21));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_BB1)
                          | (0x1 << 12) | (0x1 << 10)
                          | (0x1 << 9)  | (0x1 << 8)
                          | (0x1 << 6)  | (0x1 << 5)
                          | (0x1 << 4)  | (0x1 << 3)
                          | (0x1 << 2));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_BB2) | (0x1 << 31));
            }
        }

        OS_REG_WRITE(ah, 0xa28c, 0x11111);
        OS_REG_WRITE(ah, 0xa288, 0x111);      
    } else {
        /* chain zero */
        if ((tx_chain_mask & 0x01) == 0x01) {
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX1, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX1)
                      | (0x1 << 31) | (0x1 << 27)
                      | (0x3 << 23) | (0x1 << 19)
                      | (0x1 << 15) | (0x3 << 9))
                      & ~(0x1 << 12));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                      | (0x1 << 12) | (0x1 << 10) 
                      | (0x1 << 9)  | (0x1 << 8)  
                      | (0x1 << 7)  | (0x1 << 3)  
                      | (0x1 << 2)  | (0x1 << 1)) 
                      & ~(0x1 << 11)& ~(0x1 << 0));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
                      | (0x1 << 29) | (0x1 << 25) 
                      | (0x1 << 23) | (0x1 << 19) 
                      | (0x1 << 10) | (0x1 << 9)  
                      | (0x1 << 8)  | (0x1 << 3))
                      & ~(0x1 << 28)& ~(0x1 << 24)
                      & ~(0x1 << 22)& ~(0x1 << 7));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1)
                      | (0x1 << 23))& ~(0x1 << 21));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF2, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF2)
                      | (0x3 << 3)  | (0x3 << 0));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF3, 
                (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF3)
                      | (0x3 << 29) | (0x3 << 26)
                      | (0x2 << 23) | (0x2 << 20)
                      | (0x2 << 17))& ~(0x1 << 14));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB1, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_BB1)
                      | (0x1 << 12) | (0x1 << 10)
                      | (0x1 << 9)  | (0x1 << 8)
                      | (0x1 << 6)  | (0x1 << 5)
                      | (0x1 << 4)  | (0x1 << 3)
                      | (0x1 << 2));
            OS_REG_WRITE(ah, AR_PHY_65NM_CH0_BB2, 
                OS_REG_READ(ah, AR_PHY_65NM_CH0_BB2) | (0x1 << 31));
            if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP) & ~(0x1 << 4));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
                          | (0x1 << 26) | (0x7 << 24)
                          | (0x3 << 22));
            } else {
                OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, 
                    OS_REG_READ(ah, AR_HORNET_CH0_TOP) & ~(0x1 << 4));
                OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, 
                    OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
                          | (0x1 << 26) | (0x7 << 24)
                          | (0x3 << 22));
            }
                                    
            if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                          | (0x1 << 3)  | (0x1 << 2)
                          | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
                          | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1) | (0x1 << 23));
            }
            if (AR_SREV_OSPREY(ah)) { 
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                          | (0x1 << 3)  | (0x1 << 2)
                          | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
                          | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1) | (0x1 << 23));
            }
        }
        if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
            /* chain one */
            if ((tx_chain_mask & 0x02) == 0x02 ) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                          | (0x1 << 3)  | (0x1 << 2)
                          | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
                          | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1) | (0x1 << 23));
                if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP) & ~(0x1 << 4));
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
                              | (0x1 << 26) | (0x7 << 24)
                              | (0x3 << 22));
                } else {
                    OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, 
                        OS_REG_READ(ah, AR_HORNET_CH0_TOP) & ~(0x1 << 4));
                    OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, 
                        OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
                              | (0x1 << 26) | (0x7 << 24)
                              | (0x3 << 22));
                }
                
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX1)
                          | (0x1 << 31) | (0x1 << 27)
                          | (0x3 << 23) | (0x1 << 19)
                          | (0x1 << 15) | (0x3 << 9)) 
                          & ~(0x1 << 12));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                          | (0x1 << 12) | (0x1 << 10) 
                          | (0x1 << 9)  | (0x1 << 8)  
                          | (0x1 << 7)  | (0x1 << 3)  
                          | (0x1 << 2)  | (0x1 << 1))  
                          & ~(0x1 << 11)& ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
                          | (0x1 << 29) | (0x1 << 25) 
                          | (0x1 << 23) | (0x1 << 19) 
                          | (0x1 << 10) | (0x1 << 9)  
                          | (0x1 << 8)  | (0x1 << 3))
                          & ~(0x1 << 28)& ~(0x1 << 24)
                          & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1)
                          | (0x1 << 23))& ~(0x1 << 21));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF2)
                          | (0x3 << 3)  | (0x3 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF3)
                          | (0x3 << 29) | (0x3 << 26)
                          | (0x2 << 23) | (0x2 << 20)
                          | (0x2 << 17))& ~(0x1 << 14));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_BB1)
                          | (0x1 << 12) | (0x1 << 10)
                          | (0x1 << 9)  | (0x1 << 8)
                          | (0x1 << 6)  | (0x1 << 5)
                          | (0x1 << 4)  | (0x1 << 3)
                          | (0x1 << 2));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_BB2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_BB2) | (0x1 << 31));

                if (AR_SREV_OSPREY(ah)) {
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, 
                        (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                              | (0x1 << 3)  | (0x1 << 2)
                              | (0x1 << 1)) & ~(0x1 << 0));
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
                              | (0x1 << 19) | (0x1 << 3));
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1) | (0x1 << 23));
                }
            }
        }
        if (AR_SREV_OSPREY(ah)) {
            /* chain two */
            if ((tx_chain_mask & 0x04) == 0x04 ) {
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2)
                          | (0x1 << 3)  | (0x1 << 2)
                          | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_RXTX3, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX3)
                          | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TXRF1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF1) | (0x1 << 23));
                if (AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah)) {
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP) & ~(0x1 << 4));
                    OS_REG_WRITE(ah, AR_PHY_65NM_CH0_TOP2, 
                        OS_REG_READ(ah, AR_PHY_65NM_CH0_TOP2)
                              | (0x1 << 26) | (0x7 << 24)
                              | (0x3 << 22));
                } else {
                    OS_REG_WRITE(ah, AR_HORNET_CH0_TOP, 
                        OS_REG_READ(ah, AR_HORNET_CH0_TOP) & ~(0x1 << 4));
                    OS_REG_WRITE(ah, AR_HORNET_CH0_TOP2, 
                        OS_REG_READ(ah, AR_HORNET_CH0_TOP2)
                              | (0x1 << 26) | (0x7 << 24)
                              | (0x3 << 22));
                }

                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX2)
                          | (0x1 << 3)  | (0x1 << 2)
                          | (0x1 << 1)) & ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX3, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_RXTX3)
                          | (0x1 << 19) | (0x1 << 3));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH1_TXRF1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH1_TXRF1) | (0x1 << 23));

                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX1)
                          | (0x1 << 31) | (0x1 << 27)
                          | (0x3 << 23) | (0x1 << 19)
                          | (0x1 << 15) | (0x3 << 9)) 
                          & ~(0x1 << 12));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX2, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX2)
                          | (0x1 << 12) | (0x1 << 10) 
                          | (0x1 << 9)  | (0x1 << 8)  
                          | (0x1 << 7)  | (0x1 << 3)  
                          | (0x1 << 2)  | (0x1 << 1))  
                          & ~(0x1 << 11)& ~(0x1 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_RXTX3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_RXTX3)
                          | (0x1 << 29) | (0x1 << 25) 
                          | (0x1 << 23) | (0x1 << 19) 
                          | (0x1 << 10) | (0x1 << 9)  
                          | (0x1 << 8)  | (0x1 << 3))
                          & ~(0x1 << 28)& ~(0x1 << 24)
                          & ~(0x1 << 22)& ~(0x1 << 7));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF1, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF1)
                          | (0x1 << 23))& ~(0x1 << 21));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF2)
                          | (0x3 << 3)  | (0x3 << 0));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_TXRF3, 
                    (OS_REG_READ(ah, AR_PHY_65NM_CH2_TXRF3)
                          | (0x3 << 29) | (0x3 << 26)
                          | (0x2 << 23) | (0x2 << 20)
                          | (0x2 << 17))& ~(0x1 << 14));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB1, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_BB1)
                          | (0x1 << 12) | (0x1 << 10)
                          | (0x1 << 9)  | (0x1 << 8)
                          | (0x1 << 6)  | (0x1 << 5)
                          | (0x1 << 4)  | (0x1 << 3)
                          | (0x1 << 2));
                OS_REG_WRITE(ah, AR_PHY_65NM_CH2_BB2, 
                    OS_REG_READ(ah, AR_PHY_65NM_CH2_BB2) | (0x1 << 31));
            }
        }

        OS_REG_WRITE(ah, 0xa28c, 0x22222);
        OS_REG_WRITE(ah, 0xa288, 0x222);
    }
}

void 
ar9300_tx99_start(struct ath_hal *ah, u_int8_t *data)
{
    u_int32_t val;
    u_int32_t qnum = (u_int32_t)data;

    /* Disable AGC to A2 */
    OS_REG_WRITE(ah, AR_PHY_TEST, (OS_REG_READ(ah, AR_PHY_TEST) | PHY_AGC_CLR));
    OS_REG_WRITE(ah, AR_DIAG_SW, OS_REG_READ(ah, AR_DIAG_SW) &~ AR_DIAG_RX_DIS);

    OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);     /* set receive disable */
    /* set CW_MIN and CW_MAX both to 0, AIFS=2 */
    OS_REG_WRITE(ah, AR_DLCL_IFS(qnum), 0);
    OS_REG_WRITE(ah, AR_D_GBL_IFS_SIFS, 20); /* 50 OK */
    OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, 20);
    /* 200 ok for HT20, 400 ok for HT40 */
    OS_REG_WRITE(ah, AR_TIME_OUT, 0x00000400);
    OS_REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
    
    /* set QCU modes to early termination */
    val = OS_REG_READ(ah, AR_QMISC(qnum));
    OS_REG_WRITE(ah, AR_QMISC(qnum), val | AR_Q_MISC_DCU_EARLY_TERM_REQ);
}

void 
ar9300_tx99_stop(struct ath_hal *ah)
{
    /* this should follow the setting of start */
    OS_REG_WRITE(ah, AR_PHY_TEST, OS_REG_READ(ah, AR_PHY_TEST) &~ PHY_AGC_CLR);
    OS_REG_WRITE(ah, AR_DIAG_SW, OS_REG_READ(ah, AR_DIAG_SW) | AR_DIAG_RX_DIS);
}
#endif /* ATH_TX99_DIAG */
#endif /* ATH_SUPPORT_HTC */

HAL_BOOL 
ar9300Get3StreamSignature(struct ath_hal *ah)
{
    return AH_FALSE;
}

HAL_BOOL
ar9300ForceVCS(struct ath_hal *ah)
{
   return AH_FALSE;
}

HAL_BOOL
ar9300SetDfs3StreamFix(struct ath_hal *ah, u_int32_t val)
{
   return AH_FALSE;
}

static u_int32_t
ar9300_read_loc_timer(struct ath_hal *ah)
{

    return OS_REG_READ(ah, AR_LOC_TIMER_REG);
}

HAL_BOOL
ar9300_set_ctl_pwr(struct ath_hal *ah, u_int8_t *ctl_array)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    ar9300_eeprom_t *p_eep_data = &ahp->ah_eeprom;
    u_int8_t *ctl_index;
    u_int32_t offset = 0;

    if (!ctl_array)
        return AH_FALSE;

    /* copy 2G ctl freqbin and power data */
    ctl_index = p_eep_data->ctl_index_2g;
    OS_MEMCPY(ctl_index + OSPREY_NUM_CTLS_2G, ctl_array,
                OSPREY_NUM_CTLS_2G * OSPREY_NUM_BAND_EDGES_2G +     /* ctl_freqbin_2G */
                OSPREY_NUM_CTLS_2G * sizeof(OSP_CAL_CTL_DATA_2G));  /* ctl_power_data_2g */
    offset = (OSPREY_NUM_CTLS_2G * OSPREY_NUM_BAND_EDGES_2G) +
            ( OSPREY_NUM_CTLS_2G * sizeof(OSP_CAL_CTL_DATA_2G));


    /* copy 2G ctl freqbin and power data */
    ctl_index = p_eep_data->ctl_index_5g;
    OS_MEMCPY(ctl_index + OSPREY_NUM_CTLS_5G, ctl_array + offset,
                OSPREY_NUM_CTLS_5G * OSPREY_NUM_BAND_EDGES_5G +     /* ctl_freqbin_5G */
                OSPREY_NUM_CTLS_5G * sizeof(OSP_CAL_CTL_DATA_5G));  /* ctl_power_data_5g */

    return AH_FALSE;
}

void
ar9300_set_txchainmaskopt(struct ath_hal *ah, u_int8_t mask)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* optional txchainmask should be subset of primary txchainmask */
    if ((mask & ahp->ah_tx_chainmask) != mask) {
        ahp->ah_tx_chainmaskopt = 0;
        ath_hal_printf(ah, "Error: ah_tx_chainmask=%d, mask=%d\n", ahp->ah_tx_chainmask, mask);
        return;
    }
    
    ahp->ah_tx_chainmaskopt = mask;
}
