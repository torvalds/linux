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

//#ifdef AH_SUPPORT_AR9300

#include "ah.h"
#include "ah_desc.h"
#include "ah_internal.h"

#include "ar9300_freebsd_inc.h"

#include "ar9300/ar9300phy.h"
#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300desc.h"

#if ATH_SUPPORT_SPECTRAL

/*
 * Default 9300 spectral scan parameters
 */
#define AR9300_SPECTRAL_SCAN_ENA                0
#define AR9300_SPECTRAL_SCAN_ACTIVE             0
#define AR9300_SPECTRAL_SCAN_FFT_PERIOD         8
#define AR9300_SPECTRAL_SCAN_PERIOD             1
#define AR9300_SPECTRAL_SCAN_COUNT              16 /* used to be 128 */
#define AR9300_SPECTRAL_SCAN_SHORT_REPEAT       1

/* constants */
#define MAX_RADAR_DC_PWR_THRESH 127
#define MAX_RADAR_RSSI_THRESH 0x3f
#define MAX_RADAR_HEIGHT 0x3f
#define MAX_CCA_THRESH 127
#define ENABLE_ALL_PHYERR 0xffffffff

void ar9300_disable_cck(struct ath_hal *ah);
void ar9300_disable_radar(struct ath_hal *ah);
void ar9300_disable_restart(struct ath_hal *ah);
void ar9300_set_radar_dc_thresh(struct ath_hal *ah);
void ar9300_disable_weak_signal(struct ath_hal *ah);
void ar9300_disable_strong_signal(struct ath_hal *ah);
void ar9300_prep_spectral_scan(struct ath_hal *ah);
void ar9300_disable_dc_offset(struct ath_hal *ah);
void ar9300_enable_cck_detect(struct ath_hal *ah);

void
ar9300_disable_cck(struct ath_hal *ah)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_PHY_MODE);
    val &= ~(AR_PHY_MODE_DYN_CCK_DISABLE);

    OS_REG_WRITE(ah, AR_PHY_MODE, val);
}

void
ar9300_disable_radar(struct ath_hal *ah)
{
    u_int32_t val;

    /* Enable radar FFT */
    val = OS_REG_READ(ah, AR_PHY_RADAR_0);
    val |= AR_PHY_RADAR_0_FFT_ENA;

    /* set radar detect thresholds to max to effectively disable radar */
    val &= ~AR_PHY_RADAR_0_RRSSI;
    val |= SM(MAX_RADAR_RSSI_THRESH, AR_PHY_RADAR_0_RRSSI);

    val &= ~AR_PHY_RADAR_0_HEIGHT;
    val |= SM(MAX_RADAR_HEIGHT, AR_PHY_RADAR_0_HEIGHT);

    val &= ~(AR_PHY_RADAR_0_ENA);
    OS_REG_WRITE(ah, AR_PHY_RADAR_0, val);

    /* disable extension radar detect */
    val = OS_REG_READ(ah, AR_PHY_RADAR_EXT);
    OS_REG_WRITE(ah, AR_PHY_RADAR_EXT, val & ~AR_PHY_RADAR_EXT_ENA);

    val = OS_REG_READ(ah, AR_RX_FILTER);
    val |= (1 << 13);
    OS_REG_WRITE(ah, AR_RX_FILTER, val);
}

void ar9300_disable_restart(struct ath_hal *ah)
{
    u_int32_t val;
    val = OS_REG_READ(ah, AR_PHY_RESTART);
    val &= ~AR_PHY_RESTART_ENA;
    OS_REG_WRITE(ah, AR_PHY_RESTART, val);

    val = OS_REG_READ(ah, AR_PHY_RESTART);
}

void ar9300_set_radar_dc_thresh(struct ath_hal *ah)
{
    u_int32_t val;
    val = OS_REG_READ(ah, AR_PHY_RADAR_EXT);
    val &= ~AR_PHY_RADAR_DC_PWR_THRESH;
    val |= SM(MAX_RADAR_DC_PWR_THRESH, AR_PHY_RADAR_DC_PWR_THRESH);
    OS_REG_WRITE(ah, AR_PHY_RADAR_EXT, val);

    val = OS_REG_READ(ah, AR_PHY_RADAR_EXT);
}

void
ar9300_disable_weak_signal(struct ath_hal *ah)
{
    /* set firpwr to max (signed) */
    OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_FIRPWR, 0x7f);
    OS_REG_CLR_BIT(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_FIRPWR_SIGN_BIT);

    /* set firstep to max */
    OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_FIRSTEP, 0x3f);

    /* set relpwr to max (signed) */
    OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_RELPWR, 0x1f);
    OS_REG_CLR_BIT(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_RELPWR_SIGN_BIT);

    /* set relstep to max (signed) */
    OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_RELSTEP, 0x1f);
    OS_REG_CLR_BIT(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_RELSTEP_SIGN_BIT);
 
    /* set firpwr_low to max (signed) */
    OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_FIRPWR, 0x7f);
    OS_REG_CLR_BIT(
        ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_FIRPWR_SIGN_BIT);

    /* set firstep_low to max */
    OS_REG_RMW_FIELD(
        ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW, 0x3f);

    /* set relstep_low to max (signed) */
    OS_REG_RMW_FIELD(
        ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_RELSTEP, 0x1f);
    OS_REG_CLR_BIT(
        ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_RELSTEP_SIGN_BIT);
}

void
ar9300_disable_strong_signal(struct ath_hal *ah)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_PHY_TIMING5);
    val |= AR_PHY_TIMING5_RSSI_THR1A_ENA;
    OS_REG_WRITE(ah, AR_PHY_TIMING5, val);

    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING5, AR_PHY_TIMING5_RSSI_THR1A, 0x7f);

}
void
ar9300_set_cca_threshold(struct ath_hal *ah, u_int8_t thresh62)
{
    OS_REG_RMW_FIELD(ah, AR_PHY_CCA_0, AR_PHY_CCA_THRESH62, thresh62);
    OS_REG_RMW_FIELD(ah, AR_PHY_EXT_CCA0, AR_PHY_EXT_CCA0_THRESH62, thresh62);
    /*
    OS_REG_RMW_FIELD(ah,
        AR_PHY_EXTCHN_PWRTHR1, AR_PHY_EXT_CCA0_THRESH62, thresh62);
     */
    OS_REG_RMW_FIELD(ah, AR_PHY_EXT_CCA, AR_PHY_EXT_CCA_THRESH62, thresh62);
}

static void ar9300_classify_strong_bins(struct ath_hal *ah)
{
    OS_REG_RMW_FIELD(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_CF_BIN_THRESH, 0x1);
}

void ar9300_disable_dc_offset(struct ath_hal *ah)
{
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING2, AR_PHY_TIMING2_DC_OFFSET, 0);
}

void ar9300_enable_cck_detect(struct ath_hal *ah)
{
    OS_REG_RMW_FIELD(ah, AR_PHY_MODE, AR_PHY_MODE_DISABLE_CCK, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_MODE, AR_PHY_MODE_DYNAMIC, 1);
}

void ar9300_prep_spectral_scan(struct ath_hal *ah)
{
    ar9300_disable_radar(ah);
    ar9300_classify_strong_bins(ah);
    ar9300_disable_dc_offset(ah);
    if (AH_PRIVATE(ah)->ah_curchan &&
        IS_5GHZ_FAST_CLOCK_EN(ah, AH_PRIVATE(ah)->ah_curchan))
    { /* fast clock */
        ar9300_enable_cck_detect(ah);
    }
#ifdef DEMO_MODE
    ar9300_disable_strong_signal(ah);
    ar9300_disable_weak_signal(ah);
    ar9300_set_radar_dc_thresh(ah);
    ar9300_set_cca_threshold(ah, MAX_CCA_THRESH);
    /*ar9300_disable_restart(ah);*/
#endif
    OS_REG_WRITE(ah, AR_PHY_ERR, HAL_PHYERR_SPECTRAL);
}


//#define TEST_NOISE_PWR_WITHOUT_EEPROM 1
#ifdef TEST_NOISE_PWR_WITHOUT_EEPROM
struct nf_cal {
    int cal;
    int pwr;
};
struct nf_cal_table_t {
    int freq;
    struct nf_cal chain[AH_MAX_CHAINS];
};

static const struct nf_cal_table_t nf_cal_table[] =
{
/* ch 1  */ {2412, { {N2DBM(-101, 00),  N2DBM( -94, 25)},
                     {N2DBM(-107, 75),  N2DBM( -99, 75)},
                   } },
/* ch 6  */ {2437, { {N2DBM(-102, 25),  N2DBM( -94, 25)},
                     {N2DBM(-106, 00),  N2DBM( -97, 25)},
                   } },
/* ch 11 */ {2462, { {N2DBM(-101, 50),  N2DBM( -95, 00)},
                     {N2DBM(-105, 50),  N2DBM( -98, 00)},
                   } },
/* ch 36 */ {5180, { {N2DBM(-114, 25),  N2DBM( -95, 00)},
                     {N2DBM(-114, 75),  N2DBM( -94, 00)},
                   } },
/* ch 44 */ {5220, { {N2DBM(-113, 00),  N2DBM( -95, 00)},
                     {N2DBM(-115, 00),  N2DBM( -94, 50)},
                   } },
/* ch 64 */ {5320, { {N2DBM(-113, 00),  N2DBM( -95, 00)}, // not cal'ed
                     {N2DBM(-115, 00),  N2DBM( -94, 50)},
                   } },
/* ch 100*/ {5500, { {N2DBM(-111, 50),  N2DBM( -93, 75)},
                     {N2DBM(-112, 00),  N2DBM( -95, 25)},
                   } },
/* ch 120*/ {5600, { {N2DBM(-111, 50),  N2DBM( -93, 75)},
                     {N2DBM(-112, 00),  N2DBM( -95, 25)},
                   } },
/* ch 140*/ {5700, { {N2DBM(-111, 75),  N2DBM( -95, 00)},
                     {N2DBM(-111, 75),  N2DBM( -96, 00)},
                   } },
/* ch 157*/ {5785, { {N2DBM(-112, 50),  N2DBM( -94, 75)},
                     {N2DBM(-111, 75),  N2DBM( -95, 50)},
                   } },
/* ch 165*/ {5825, { {N2DBM(-111, 50),  N2DBM( -95, 00)},
                     {N2DBM(-112, 00),  N2DBM( -95, 00)},
                   } },
                   {0}
};

static int
ar9300_noise_floor_get(struct ath_hal *ah, int freq_mhz, int ch)
{
    int i;
    for (i = 0; nf_cal_table[i].freq != 0; i++) {
        if (nf_cal_table[i + 0].freq == freq_mhz ||
            nf_cal_table[i + 1].freq > freq_mhz ||
            nf_cal_table[i + 1].freq == 0) {
            return nf_cal_table[i].chain[ch].cal;
        }
    }

    ath_hal_printf(ah,
        "%s: **Warning: device %d.%d: "
        "no nf cal offset found for freq %d chain %d\n",
        __func__, (AH_PRIVATE(ah))->ah_macVersion, 
        (AH_PRIVATE(ah))->ah_macRev, freq_mhz, ch);
    return 0;
}

static int
ar9300_noise_floor_power_get(struct ath_hal *ah, int freq_mhz, int ch)
{
    int i;
    for (i = 0; nf_cal_table[i].freq != 0; i++) {
        if (nf_cal_table[i + 0].freq == freq_mhz ||
            nf_cal_table[i + 1].freq > freq_mhz ||
            nf_cal_table[i + 1].freq == 0) {
            return nf_cal_table[i].chain[ch].pwr;
        }
    }

    ath_hal_printf(ah,
        "%s: **Warning: device %d.%d: "
        "no nf pwr offset found for freq %d chain %d\n",
        __func__, (AH_PRIVATE(ah))->ah_macVersion, 
        (AH_PRIVATE(ah))->ah_macRev, freq_mhz, ch);
    return 0;
}
#else
#define ar9300_noise_floor_get(_ah,_f,_ich)          ar9300_noise_floor_cal_or_power_get((_ah), (_f), (_ich), 1/*use_cal*/)
#define ar9300_noise_floor_power_get(_ah,_f,_ich)    ar9300_noise_floor_cal_or_power_get((_ah), (_f), (_ich), 0/*use_cal*/)
#endif


void
ar9300_configure_spectral_scan(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss)
{
    u_int32_t val;
    //uint32_t i;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL asleep = ahp->ah_chip_full_sleep;
    //int16_t nf_buf[HAL_NUM_NF_READINGS];

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE);
    }

    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "%s: called\n", __func__);

    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_fft_period=%d\n", ss->ss_fft_period);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_period=%d\n", ss->ss_period);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_count=%d\n", ss->ss_count);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_short_report=%d\n", ss->ss_short_report);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_spectral_pri=%d\n", ss->ss_spectral_pri);

    ar9300_prep_spectral_scan(ah);

#if 0
    if (ss->ss_spectral_pri) {
        for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
            nf_buf[i] = NOISE_PWR_DBM_2_INT(ss->ss_nf_cal[i]);
        }
        ar9300_load_nf(ah, nf_buf);
#ifdef DEMO_MODE
        ar9300_disable_strong_signal(ah);
        ar9300_disable_weak_signal(ah);
        ar9300_set_radar_dc_thresh(ah);
        ar9300_set_cca_threshold(ah, MAX_CCA_THRESH);
        /*ar9300_disable_restart(ah);*/
#endif
    }   
#endif

    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

    if (ss->ss_fft_period != HAL_SPECTRAL_PARAM_NOVAL) {
        val &= ~AR_PHY_SPECTRAL_SCAN_FFT_PERIOD;
        val |= SM(ss->ss_fft_period, AR_PHY_SPECTRAL_SCAN_FFT_PERIOD);
    }

    if (ss->ss_period != HAL_SPECTRAL_PARAM_NOVAL) {
        val &= ~AR_PHY_SPECTRAL_SCAN_PERIOD;
        val |= SM(ss->ss_period, AR_PHY_SPECTRAL_SCAN_PERIOD);
    }

    if (ss->ss_count != HAL_SPECTRAL_PARAM_NOVAL) {
        val &= ~AR_PHY_SPECTRAL_SCAN_COUNT;
        /* Remnants of a Merlin bug, 128 translates to 0 for
         * continuous scanning. Instead we do piecemeal captures
         * of 64 samples for Osprey.
         */
        if (ss->ss_count == 128) {
            val |= SM(0, AR_PHY_SPECTRAL_SCAN_COUNT);
        } else {
            val |= SM(ss->ss_count, AR_PHY_SPECTRAL_SCAN_COUNT);
        }
    }

    if (ss->ss_period != HAL_SPECTRAL_PARAM_NOVAL) {
        val &= ~AR_PHY_SPECTRAL_SCAN_PERIOD;
        val |= SM(ss->ss_period, AR_PHY_SPECTRAL_SCAN_PERIOD);
    }

    if (ss->ss_short_report != HAL_SPECTRAL_PARAM_NOVAL) {
    if (ss->ss_short_report == AH_TRUE) {
        val |= AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
    } else {
        val &= ~AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT;
    }
    }
    
    /* if noise power cal, force high priority */
    if (ss->ss_spectral_pri != HAL_SPECTRAL_PARAM_NOVAL) {
    if (ss->ss_spectral_pri) {
        val |= AR_PHY_SPECTRAL_SCAN_PRIORITY_HI;
    } else {
        val &= ~AR_PHY_SPECTRAL_SCAN_PRIORITY_HI;
    }
    }
    
    /* enable spectral scan */
    OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val | AR_PHY_SPECTRAL_SCAN_ENABLE);

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
    }
}

/*
 * Get the spectral parameter values and return them in the pe
 * structure
 */

void
ar9300_get_spectral_params(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss)
{
    u_int32_t val;
    HAL_CHANNEL_INTERNAL *chan = NULL;
    const struct ieee80211_channel *c;
    int i, ichain, rx_chain_status;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL asleep = ahp->ah_chip_full_sleep;

    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "%s: called\n", __func__);

    c = AH_PRIVATE(ah)->ah_curchan;
    if (c != NULL)
        chan = ath_hal_checkchannel(ah, c);

    // XXX TODO: just always wake up all chips?
    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE);
    }

    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

    ss->ss_fft_period = MS(val, AR_PHY_SPECTRAL_SCAN_FFT_PERIOD);
    ss->ss_period = MS(val, AR_PHY_SPECTRAL_SCAN_PERIOD);
    ss->ss_count = MS(val, AR_PHY_SPECTRAL_SCAN_COUNT);
    ss->ss_short_report = (val & AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT) ? 1:0;
    ss->ss_spectral_pri = ( val & AR_PHY_SPECTRAL_SCAN_PRIORITY_HI) ? 1:0;
    ss->ss_enabled = !! (val & AR_PHY_SPECTRAL_SCAN_ENABLE);
    ss->ss_active = !! (val & AR_PHY_SPECTRAL_SCAN_ACTIVE);

    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_fft_period=%d\n", ss->ss_fft_period);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_period=%d\n", ss->ss_period);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_count=%d\n", ss->ss_count);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_short_report=%d\n", ss->ss_short_report);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_spectral_pri=%d\n", ss->ss_spectral_pri);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_enabled=%d\n", ss->ss_enabled);
    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "ss_active=%d\n", ss->ss_active);

    OS_MEMZERO(ss->ss_nf_cal, sizeof(ss->ss_nf_cal)); 
    OS_MEMZERO(ss->ss_nf_pwr, sizeof(ss->ss_nf_cal)); 
    ss->ss_nf_temp_data = 0;

    if (chan != NULL) {
        rx_chain_status = OS_REG_READ(ah, AR_PHY_RX_CHAINMASK) & 0x7;
        for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
            ichain = i % 3;
            if (rx_chain_status & (1 << ichain)) {
                ss->ss_nf_cal[i] =
                    ar9300_noise_floor_get(ah, chan->channel, ichain);     
                ss->ss_nf_pwr[i] =
                    ar9300_noise_floor_power_get(ah, chan->channel, ichain);
            }
        }
        ss->ss_nf_temp_data = OS_REG_READ_FIELD(ah, AR_PHY_BB_THERM_ADC_4, AR_PHY_BB_THERM_ADC_4_LATEST_THERM);
    } else {
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
            "%s: chan is NULL - no ss nf values\n", __func__);
    }

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
    }
}

HAL_BOOL
ar9300_is_spectral_active(struct ath_hal *ah)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
    return MS(val, AR_PHY_SPECTRAL_SCAN_ACTIVE);
}

HAL_BOOL
ar9300_is_spectral_enabled(struct ath_hal *ah)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
    return MS(val, AR_PHY_SPECTRAL_SCAN_ENABLE);
}

void ar9300_start_spectral_scan(struct ath_hal *ah)
{
    u_int32_t val;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL asleep = ahp->ah_chip_full_sleep;

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE);
    }

    HALDEBUG(ah, HAL_DEBUG_SPECTRAL, "%s: called\n", __func__);

    ar9300_prep_spectral_scan(ah);

    /* activate spectral scan */
    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
    /* This is a hardware bug fix, the enable and active bits should 
     * not be set/reset in the same write operation to the register 
     */
    if (!(val & AR_PHY_SPECTRAL_SCAN_ENABLE)) {
        val |= AR_PHY_SPECTRAL_SCAN_ENABLE;
        OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
        val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);
    }
    val |= AR_PHY_SPECTRAL_SCAN_ACTIVE;
    OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
    
    /* Reset the PHY_ERR_MASK */
    val = OS_REG_READ(ah, AR_PHY_ERR_MASK_REG);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_REG, val | AR_PHY_ERR_RADAR);

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
    }
}

void ar9300_stop_spectral_scan(struct ath_hal *ah)
{
    u_int32_t val;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL asleep = ahp->ah_chip_full_sleep;

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE);
    }
    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

    /* deactivate spectral scan */
    /* HW Bug fix -- Do not disable the spectral scan
     * only turn off the active bit
     */
    //val &= ~AR_PHY_SPECTRAL_SCAN_ENABLE;
    val &= ~AR_PHY_SPECTRAL_SCAN_ACTIVE;
    OS_REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

    OS_REG_RMW_FIELD(ah, AR_PHY_RADAR_1, AR_PHY_RADAR_1_CF_BIN_THRESH,
                     ahp->ah_radar1);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING2, AR_PHY_TIMING2_DC_OFFSET,
                     ahp->ah_dc_offset);
    OS_REG_WRITE(ah, AR_PHY_ERR, 0);

    if (AH_PRIVATE(ah)->ah_curchan &&
        IS_5GHZ_FAST_CLOCK_EN(ah, AH_PRIVATE(ah)->ah_curchan))
    { /* fast clock */
        OS_REG_RMW_FIELD(ah, AR_PHY_MODE, AR_PHY_MODE_DISABLE_CCK,
                         ahp->ah_disable_cck);
    }

    val = OS_REG_READ(ah, AR_PHY_ERR);
    
    val = OS_REG_READ(ah, AR_PHY_ERR_MASK_REG) & (~AR_PHY_ERR_RADAR);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_REG, val);
    
    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
    }
}

u_int32_t ar9300_get_spectral_config(struct ath_hal *ah)
{
    u_int32_t val;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL asleep = ahp->ah_chip_full_sleep;

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE);
    }

    val = OS_REG_READ(ah, AR_PHY_SPECTRAL_SCAN);

    if ((AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) && asleep) {
        ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);
    }
    return val;
}

int16_t ar9300_get_ctl_chan_nf(struct ath_hal *ah)
{
    int16_t nf;
#if 0
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
#endif

    if ( (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0) {
        /* Noise floor calibration value is ready */
        nf = MS(OS_REG_READ(ah, AR_PHY_CCA_0), AR_PHY_MINCCA_PWR);
    } else {
        /* NF calibration is not done, return nominal value */
        nf = AH9300(ah)->nfp->nominal;
    }
    if (nf & 0x100) {
        nf = (0 - ((nf ^ 0x1ff) + 1));
    }
    return nf;
}

int16_t ar9300_get_ext_chan_nf(struct ath_hal *ah)
{
    int16_t nf;
#if 0
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
#endif

    if ((OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0) {
        /* Noise floor calibration value is ready */
        nf = MS(OS_REG_READ(ah, AR_PHY_EXT_CCA), AR_PHY_EXT_MINCCA_PWR);
    } else {
        /* NF calibration is not done, return nominal value */
        nf = AH9300(ah)->nfp->nominal;
    }
    if (nf & 0x100) {
        nf = (0 - ((nf ^ 0x1ff) + 1));
    }
    return nf;
}

#endif /* ATH_SUPPORT_SPECTRAL */
//#endif

