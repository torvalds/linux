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
#include "ah_desc.h"
//#include "ah_pktlog.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"

extern  void ar9300_set_rx_filter(struct ath_hal *ah, u_int32_t bits);
extern  u_int32_t ar9300_get_rx_filter(struct ath_hal *ah);

#define HAL_ANI_DEBUG 1

/*
 * Anti noise immunity support.  We track phy errors and react
 * to excessive errors by adjusting the noise immunity parameters.
 */

/******************************************************************************
 *
 * New Ani Algorithm for Station side only
 *
 *****************************************************************************/

#define HAL_ANI_OFDM_TRIG_HIGH     1000 /* units are errors per second */
#define HAL_ANI_OFDM_TRIG_LOW       400 /* units are errors per second */
#define HAL_ANI_CCK_TRIG_HIGH       600 /* units are errors per second */
#define HAL_ANI_CCK_TRIG_LOW        300 /* units are errors per second */
#define HAL_ANI_USE_OFDM_WEAK_SIG  AH_TRUE
#define HAL_ANI_ENABLE_MRC_CCK     AH_TRUE /* default is enabled */
#define HAL_ANI_DEF_SPUR_IMMUNE_LVL   3
#define HAL_ANI_DEF_FIRSTEP_LVL       2
#define HAL_ANI_RSSI_THR_HIGH        40
#define HAL_ANI_RSSI_THR_LOW          7
#define HAL_ANI_PERIOD             1000

#define HAL_NOISE_DETECT_PERIOD     100
#define HAL_NOISE_RECOVER_PERIOD    5000

#define HAL_SIG_FIRSTEP_SETTING_MIN   0
#define HAL_SIG_FIRSTEP_SETTING_MAX  20
#define HAL_SIG_SPUR_IMM_SETTING_MIN  0
#define HAL_SIG_SPUR_IMM_SETTING_MAX 22

#define HAL_EP_RND(x, mul) \
    ((((x) % (mul)) >= ((mul) / 2)) ? ((x) + ((mul) - 1)) / (mul) : (x) / (mul))
#define BEACON_RSSI(ahp) \
    HAL_EP_RND(ahp->ah_stats.ast_nodestats.ns_avgbrssi, \
        HAL_RSSI_EP_MULTIPLIER)

typedef int TABLE[];
/*
 *                            level:    0   1   2   3   4   5   6   7   8
 * firstep_table:    lvl 0-8, default 2
 */
static const TABLE firstep_table    = { -4, -2,  0,  2,  4,  6,  8, 10, 12};
/* cycpwr_thr1_table: lvl 0-7, default 3 */
static const TABLE cycpwr_thr1_table = { -6, -4, -2,  0,  2,  4,  6,  8 };
/* values here are relative to the INI */

typedef struct _HAL_ANI_OFDM_LEVEL_ENTRY {
    int spur_immunity_level;
    int fir_step_level;
    int ofdm_weak_signal_on;
} HAL_ANI_OFDM_LEVEL_ENTRY;
static const HAL_ANI_OFDM_LEVEL_ENTRY ofdm_level_table[] = {
/*     SI  FS  WS */
     {  0,  0,  1  }, /* lvl 0 */
     {  1,  1,  1  }, /* lvl 1 */
     {  2,  2,  1  }, /* lvl 2 */
     {  3,  2,  1  }, /* lvl 3  (default) */
     {  4,  3,  1  }, /* lvl 4 */
     {  5,  4,  1  }, /* lvl 5 */
     {  6,  5,  1  }, /* lvl 6 */
     {  7,  6,  1  }, /* lvl 7 */
     {  7,  7,  1  }, /* lvl 8 */
     {  7,  8,  0  }  /* lvl 9 */
};
#define HAL_ANI_OFDM_NUM_LEVEL \
    (sizeof(ofdm_level_table) / sizeof(ofdm_level_table[0]))
#define HAL_ANI_OFDM_MAX_LEVEL (HAL_ANI_OFDM_NUM_LEVEL - 1)
#define HAL_ANI_OFDM_DEF_LEVEL 3 /* default level - matches the INI settings */

typedef struct _HAL_ANI_CCK_LEVEL_ENTRY {
    int fir_step_level;
    int mrc_cck_on;
} HAL_ANI_CCK_LEVEL_ENTRY;

static const HAL_ANI_CCK_LEVEL_ENTRY cck_level_table[] = {
/*     FS  MRC-CCK */
     {  0,  1  },  /* lvl 0 */
     {  1,  1  },  /* lvl 1 */
     {  2,  1  },  /* lvl 2  (default) */
     {  3,  1  },  /* lvl 3 */
     {  4,  0  },  /* lvl 4 */
     {  5,  0  },  /* lvl 5 */
     {  6,  0  },  /* lvl 6 */
     {  7,  0  },  /* lvl 7 (only for high rssi) */
     {  8,  0  }   /* lvl 8 (only for high rssi) */
};
#define HAL_ANI_CCK_NUM_LEVEL \
    (sizeof(cck_level_table) / sizeof(cck_level_table[0]))
#define HAL_ANI_CCK_MAX_LEVEL           (HAL_ANI_CCK_NUM_LEVEL - 1)
#define HAL_ANI_CCK_MAX_LEVEL_LOW_RSSI  (HAL_ANI_CCK_NUM_LEVEL - 3)
#define HAL_ANI_CCK_DEF_LEVEL 2 /* default level - matches the INI settings */

/*
 * register values to turn OFDM weak signal detection OFF
 */
static const int m1_thresh_low_off     = 127;
static const int m2_thresh_low_off     = 127;
static const int m1_thresh_off         = 127;
static const int m2_thresh_off         = 127;
static const int m2_count_thr_off      =  31;
static const int m2_count_thr_low_off  =  63;
static const int m1_thresh_low_ext_off = 127;
static const int m2_thresh_low_ext_off = 127;
static const int m1_thresh_ext_off     = 127;
static const int m2_thresh_ext_off     = 127;

void
ar9300_enable_mib_counters(struct ath_hal *ah)
{
    HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Enable MIB counters\n", __func__);
    /* Clear the mib counters and save them in the stats */
    ar9300_update_mib_mac_stats(ah);

    OS_REG_WRITE(ah, AR_FILT_OFDM, 0);
    OS_REG_WRITE(ah, AR_FILT_CCK, 0);
    OS_REG_WRITE(ah, AR_MIBC,
        ~(AR_MIBC_COW | AR_MIBC_FMC | AR_MIBC_CMC | AR_MIBC_MCS) & 0x0f);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

}

void
ar9300_disable_mib_counters(struct ath_hal *ah)
{
    HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Disabling MIB counters\n", __func__);

    OS_REG_WRITE(ah, AR_MIBC,  AR_MIBC_FMC | AR_MIBC_CMC);

    /* Clear the mib counters and save them in the stats */
    ar9300_update_mib_mac_stats(ah);

    OS_REG_WRITE(ah, AR_FILT_OFDM, 0);
    OS_REG_WRITE(ah, AR_FILT_CCK, 0);
}

/*
 * This routine returns the index into the ani_state array that
 * corresponds to the channel in *chan.  If no match is found and the
 * array is still not fully utilized, a new entry is created for the
 * channel.  We assume the attach function has already initialized the
 * ah_ani values and only the channel field needs to be set.
 */
static int
ar9300_get_ani_channel_index(struct ath_hal *ah,
  const struct ieee80211_channel *chan)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i;

    for (i = 0; i < ARRAY_LENGTH(ahp->ah_ani); i++) {
        /* XXX this doesn't distinguish between 20/40 channels */
        if (ahp->ah_ani[i].c.ic_freq == chan->ic_freq) {
            return i;
        }
        if (ahp->ah_ani[i].c.ic_freq == 0) {
            ahp->ah_ani[i].c.ic_freq = chan->ic_freq;
            ahp->ah_ani[i].c.ic_flags = chan->ic_flags;
            return i;
        }
    }
    /* XXX statistic */
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
        "%s: No more channel states left. Using channel 0\n", __func__);
    return 0;        /* XXX gotta return something valid */
}

/*
 * Return the current ANI state of the channel we're on
 */
struct ar9300_ani_state *
ar9300_ani_get_current_state(struct ath_hal *ah)
{
    return AH9300(ah)->ah_curani;
}

/*
 * Return the current statistics.
 */
HAL_ANI_STATS *
ar9300_ani_get_current_stats(struct ath_hal *ah)
{
    return &AH9300(ah)->ah_stats;
}

/*
 * Setup ANI handling.  Sets all thresholds and levels to default level AND
 * resets the channel statistics
 */

void
ar9300_ani_attach(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i;

    OS_MEMZERO(ahp->ah_ani, sizeof(ahp->ah_ani));
    for (i = 0; i < ARRAY_LENGTH(ahp->ah_ani); i++) {
        ahp->ah_ani[i].ofdm_trig_high = HAL_ANI_OFDM_TRIG_HIGH;
        ahp->ah_ani[i].ofdm_trig_low = HAL_ANI_OFDM_TRIG_LOW;
        ahp->ah_ani[i].cck_trig_high = HAL_ANI_CCK_TRIG_HIGH;
        ahp->ah_ani[i].cck_trig_low = HAL_ANI_CCK_TRIG_LOW;
        ahp->ah_ani[i].rssi_thr_high = HAL_ANI_RSSI_THR_HIGH;
        ahp->ah_ani[i].rssi_thr_low = HAL_ANI_RSSI_THR_LOW;
        ahp->ah_ani[i].ofdm_noise_immunity_level = HAL_ANI_OFDM_DEF_LEVEL;
        ahp->ah_ani[i].cck_noise_immunity_level = HAL_ANI_CCK_DEF_LEVEL;
        ahp->ah_ani[i].ofdm_weak_sig_detect_off = !HAL_ANI_USE_OFDM_WEAK_SIG;
        ahp->ah_ani[i].spur_immunity_level = HAL_ANI_DEF_SPUR_IMMUNE_LVL;
        ahp->ah_ani[i].firstep_level = HAL_ANI_DEF_FIRSTEP_LVL;
        ahp->ah_ani[i].mrc_cck_off = !HAL_ANI_ENABLE_MRC_CCK;
        ahp->ah_ani[i].ofdms_turn = AH_TRUE;
        ahp->ah_ani[i].must_restore = AH_FALSE;
    }

    /*
     * Since we expect some ongoing maintenance on the tables,
     * let's sanity check here.
     * The default level should not modify INI setting.
     */
    HALASSERT(firstep_table[HAL_ANI_DEF_FIRSTEP_LVL] == 0);
    HALASSERT(cycpwr_thr1_table[HAL_ANI_DEF_SPUR_IMMUNE_LVL] == 0);
    HALASSERT(
        ofdm_level_table[HAL_ANI_OFDM_DEF_LEVEL].fir_step_level ==
        HAL_ANI_DEF_FIRSTEP_LVL);
    HALASSERT(
        ofdm_level_table[HAL_ANI_OFDM_DEF_LEVEL].spur_immunity_level ==
        HAL_ANI_DEF_SPUR_IMMUNE_LVL);
    HALASSERT(
        cck_level_table[HAL_ANI_CCK_DEF_LEVEL].fir_step_level ==
        HAL_ANI_DEF_FIRSTEP_LVL);

    /* Initialize and enable MIB Counters */
    OS_REG_WRITE(ah, AR_PHY_ERR_1, 0);
    OS_REG_WRITE(ah, AR_PHY_ERR_2, 0);
    ar9300_enable_mib_counters(ah);

    ahp->ah_ani_period = HAL_ANI_PERIOD;
    if (ah->ah_config.ath_hal_enable_ani) {
        ahp->ah_proc_phy_err |= HAL_PROCESS_ANI;
    }
}

/*
 * Cleanup any ANI state setup.
 */
void
ar9300_ani_detach(struct ath_hal *ah)
{
    HALDEBUG(ah, HAL_DEBUG_ANI, "%s: Detaching Ani\n", __func__);
    ar9300_disable_mib_counters(ah);
    OS_REG_WRITE(ah, AR_PHY_ERR_1, 0);
    OS_REG_WRITE(ah, AR_PHY_ERR_2, 0);
}

/*
 * Initialize the ANI register values with default (ini) values.
 * This routine is called during a (full) hardware reset after
 * all the registers are initialised from the INI.
 */
void
ar9300_ani_init_defaults(struct ath_hal *ah, HAL_HT_MACMODE macmode)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    int index;
    u_int32_t val;

    HALASSERT(chan != AH_NULL);
    index = ar9300_get_ani_channel_index(ah, chan);
    ani_state = &ahp->ah_ani[index];
    ahp->ah_curani = ani_state;

    HALDEBUG(ah, HAL_DEBUG_ANI,
        "%s: ver %d.%d opmode %u chan %d Mhz/0x%x macmode %d\n",
        __func__, AH_PRIVATE(ah)->ah_macVersion, AH_PRIVATE(ah)->ah_macRev,
        AH_PRIVATE(ah)->ah_opmode, chan->ic_freq, chan->ic_flags, macmode);

    val = OS_REG_READ(ah, AR_PHY_SFCORR);
    ani_state->ini_def.m1_thresh = MS(val, AR_PHY_SFCORR_M1_THRESH);
    ani_state->ini_def.m2_thresh = MS(val, AR_PHY_SFCORR_M2_THRESH);
    ani_state->ini_def.m2_count_thr = MS(val, AR_PHY_SFCORR_M2COUNT_THR);

    val = OS_REG_READ(ah, AR_PHY_SFCORR_LOW);
    ani_state->ini_def.m1_thresh_low =
        MS(val, AR_PHY_SFCORR_LOW_M1_THRESH_LOW);
    ani_state->ini_def.m2_thresh_low =
        MS(val, AR_PHY_SFCORR_LOW_M2_THRESH_LOW);
    ani_state->ini_def.m2_count_thr_low =
        MS(val, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW);

    val = OS_REG_READ(ah, AR_PHY_SFCORR_EXT);
    ani_state->ini_def.m1_thresh_ext = MS(val, AR_PHY_SFCORR_EXT_M1_THRESH);
    ani_state->ini_def.m2_thresh_ext = MS(val, AR_PHY_SFCORR_EXT_M2_THRESH);
    ani_state->ini_def.m1_thresh_low_ext =
        MS(val, AR_PHY_SFCORR_EXT_M1_THRESH_LOW);
    ani_state->ini_def.m2_thresh_low_ext =
        MS(val, AR_PHY_SFCORR_EXT_M2_THRESH_LOW);

    ani_state->ini_def.firstep =
        OS_REG_READ_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_FIRSTEP);
    ani_state->ini_def.firstep_low =
        OS_REG_READ_FIELD(
            ah, AR_PHY_FIND_SIG_LOW, AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW);
    ani_state->ini_def.cycpwr_thr1 =
        OS_REG_READ_FIELD(ah, AR_PHY_TIMING5, AR_PHY_TIMING5_CYCPWR_THR1);
    ani_state->ini_def.cycpwr_thr1_ext =
        OS_REG_READ_FIELD(ah, AR_PHY_EXT_CCA, AR_PHY_EXT_CYCPWR_THR1);

    /* these levels just got reset to defaults by the INI */
    ani_state->spur_immunity_level = HAL_ANI_DEF_SPUR_IMMUNE_LVL;
    ani_state->firstep_level = HAL_ANI_DEF_FIRSTEP_LVL;
    ani_state->ofdm_weak_sig_detect_off = !HAL_ANI_USE_OFDM_WEAK_SIG;
    ani_state->mrc_cck_off = !HAL_ANI_ENABLE_MRC_CCK;

    ani_state->cycle_count = 0;
}

/*
 * Set the ANI settings to match an OFDM level.
 */
static void
ar9300_ani_set_odfm_noise_immunity_level(struct ath_hal *ah,
                                   u_int8_t ofdm_noise_immunity_level)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state = ahp->ah_curani;

    ani_state->rssi = BEACON_RSSI(ahp);
    HALDEBUG(ah, HAL_DEBUG_ANI,
        "**** %s: ofdmlevel %d=>%d, rssi=%d[lo=%d hi=%d]\n", __func__,
        ani_state->ofdm_noise_immunity_level, ofdm_noise_immunity_level,
        ani_state->rssi, ani_state->rssi_thr_low, ani_state->rssi_thr_high);

    ani_state->ofdm_noise_immunity_level = ofdm_noise_immunity_level;

    if (ani_state->spur_immunity_level !=
        ofdm_level_table[ofdm_noise_immunity_level].spur_immunity_level)
    {
        ar9300_ani_control(
            ah, HAL_ANI_SPUR_IMMUNITY_LEVEL,
            ofdm_level_table[ofdm_noise_immunity_level].spur_immunity_level);
    }

    if (ani_state->firstep_level !=
            ofdm_level_table[ofdm_noise_immunity_level].fir_step_level &&
        ofdm_level_table[ofdm_noise_immunity_level].fir_step_level >=
            cck_level_table[ani_state->cck_noise_immunity_level].fir_step_level)
    {
        ar9300_ani_control(
            ah, HAL_ANI_FIRSTEP_LEVEL,
            ofdm_level_table[ofdm_noise_immunity_level].fir_step_level);
    }

    if ((AH_PRIVATE(ah)->ah_opmode != HAL_M_STA ||
        ani_state->rssi <= ani_state->rssi_thr_high))
    {
        if (ani_state->ofdm_weak_sig_detect_off) {
            /*
             * force on ofdm weak sig detect.
             */
            ar9300_ani_control(ah, HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION, AH_TRUE);
        }
    } else if (ani_state->ofdm_weak_sig_detect_off ==
               ofdm_level_table[ofdm_noise_immunity_level].ofdm_weak_signal_on)
    {
        ar9300_ani_control(
            ah, HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
            ofdm_level_table[ofdm_noise_immunity_level].ofdm_weak_signal_on);
    }
}

/*
 * Set the ANI settings to match a CCK level.
 */
static void
ar9300_ani_set_cck_noise_immunity_level(struct ath_hal *ah,
                                  u_int8_t cck_noise_immunity_level)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state = ahp->ah_curani;
    int level;

    ani_state->rssi = BEACON_RSSI(ahp);
    HALDEBUG(ah, HAL_DEBUG_ANI,
        "**** %s: ccklevel %d=>%d, rssi=%d[lo=%d hi=%d]\n",
        __func__, ani_state->cck_noise_immunity_level, cck_noise_immunity_level,
        ani_state->rssi, ani_state->rssi_thr_low, ani_state->rssi_thr_high);

    if (AH_PRIVATE(ah)->ah_opmode == HAL_M_STA &&
        ani_state->rssi <= ani_state->rssi_thr_low &&
        cck_noise_immunity_level > HAL_ANI_CCK_MAX_LEVEL_LOW_RSSI)
    {
        cck_noise_immunity_level = HAL_ANI_CCK_MAX_LEVEL_LOW_RSSI;
    }

    ani_state->cck_noise_immunity_level = cck_noise_immunity_level;

    level = ani_state->ofdm_noise_immunity_level;
    if (ani_state->firstep_level !=
            cck_level_table[cck_noise_immunity_level].fir_step_level &&
        cck_level_table[cck_noise_immunity_level].fir_step_level >=
            ofdm_level_table[level].fir_step_level)
    {
        ar9300_ani_control(
            ah, HAL_ANI_FIRSTEP_LEVEL,
            cck_level_table[cck_noise_immunity_level].fir_step_level);
    }

    if (ani_state->mrc_cck_off ==
        cck_level_table[cck_noise_immunity_level].mrc_cck_on)
    {
        ar9300_ani_control(
            ah, HAL_ANI_MRC_CCK,
            cck_level_table[cck_noise_immunity_level].mrc_cck_on);
    }
}

/*
 * Control Adaptive Noise Immunity Parameters
 */
HAL_BOOL
ar9300_ani_control(struct ath_hal *ah, HAL_ANI_CMD cmd, int param)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state = ahp->ah_curani;
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    int32_t value, value2;
    u_int level = param;
    u_int is_on;

    HALDEBUG(ah, HAL_DEBUG_ANI, "%s: cmd=%d, param=%d, chan=%p, funcmask=0x%08x\n",
      __func__,
      cmd,
      param,
      chan,
      ahp->ah_ani_function);


    if (chan == NULL && cmd != HAL_ANI_MODE) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s: ignoring cmd 0x%02x - no channel\n", __func__, cmd);
        return AH_FALSE;
    }

    /*
     * These two control the top-level cck/ofdm immunity levels and will
     * program the rest of the values.
     */
    if (cmd == HAL_ANI_NOISE_IMMUNITY_LEVEL) {
        if (param > HAL_ANI_OFDM_NUM_LEVEL)
          return AH_FALSE;
        ar9300_ani_set_odfm_noise_immunity_level(ah, param);
        return AH_TRUE;
    }

    if (cmd == HAL_ANI_CCK_NOISE_IMMUNITY_LEVEL) {
        if (param > HAL_ANI_CCK_NUM_LEVEL)
          return AH_FALSE;
        ar9300_ani_set_cck_noise_immunity_level(ah, param);
        return AH_TRUE;
    }

    /*
     * Check to see if this command is available in the
     * current operating mode.
     */
    if (((1 << cmd) & ahp->ah_ani_function) == 0) {
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: early check: invalid cmd 0x%02x (allowed=0x%02x)\n",
            __func__, cmd, ahp->ah_ani_function);
        return AH_FALSE;
    }

    /*
     * The rest of these program in the requested parameter values
     * into the PHY.
     */
    switch (cmd) {

    case HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION: 
        {
            int m1_thresh_low, m2_thresh_low;
            int m1_thresh, m2_thresh;
            int m2_count_thr, m2_count_thr_low;
            int m1_thresh_low_ext, m2_thresh_low_ext;
            int m1_thresh_ext, m2_thresh_ext;
            /*
             * is_on == 1 means ofdm weak signal detection is ON
             * (default, less noise imm)
             * is_on == 0 means ofdm weak signal detection is OFF
             * (more noise imm)
             */
            is_on = param ? 1 : 0;

            if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah))
                goto skip_ws_det;

            /*
             * make register setting for default (weak sig detect ON)
             * come from INI file
             */
            m1_thresh_low    = is_on ?
                ani_state->ini_def.m1_thresh_low    : m1_thresh_low_off;
            m2_thresh_low    = is_on ?
                ani_state->ini_def.m2_thresh_low    : m2_thresh_low_off;
            m1_thresh       = is_on ?
                ani_state->ini_def.m1_thresh       : m1_thresh_off;
            m2_thresh       = is_on ?
                ani_state->ini_def.m2_thresh       : m2_thresh_off;
            m2_count_thr     = is_on ?
                ani_state->ini_def.m2_count_thr     : m2_count_thr_off;
            m2_count_thr_low  = is_on ?
                ani_state->ini_def.m2_count_thr_low  : m2_count_thr_low_off;
            m1_thresh_low_ext = is_on ?
                ani_state->ini_def.m1_thresh_low_ext : m1_thresh_low_ext_off;
            m2_thresh_low_ext = is_on ?
                ani_state->ini_def.m2_thresh_low_ext : m2_thresh_low_ext_off;
            m1_thresh_ext    = is_on ?
                ani_state->ini_def.m1_thresh_ext    : m1_thresh_ext_off;
            m2_thresh_ext    = is_on ?
                ani_state->ini_def.m2_thresh_ext    : m2_thresh_ext_off;
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                AR_PHY_SFCORR_LOW_M1_THRESH_LOW, m1_thresh_low);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                AR_PHY_SFCORR_LOW_M2_THRESH_LOW, m2_thresh_low);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR, AR_PHY_SFCORR_M1_THRESH,
                m1_thresh);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR, AR_PHY_SFCORR_M2_THRESH,
                m2_thresh);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR, AR_PHY_SFCORR_M2COUNT_THR,
                m2_count_thr);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, m2_count_thr_low);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                AR_PHY_SFCORR_EXT_M1_THRESH_LOW, m1_thresh_low_ext);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                AR_PHY_SFCORR_EXT_M2_THRESH_LOW, m2_thresh_low_ext);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT, AR_PHY_SFCORR_EXT_M1_THRESH,
                m1_thresh_ext);
            OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT, AR_PHY_SFCORR_EXT_M2_THRESH,
                m2_thresh_ext);
skip_ws_det:
            if (is_on) {
                OS_REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
                    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
            } else {
                OS_REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
                    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
            }
            if ((!is_on) != ani_state->ofdm_weak_sig_detect_off) {
                HALDEBUG(ah, HAL_DEBUG_ANI,
                    "%s: ** ch %d: ofdm weak signal: %s=>%s\n",
                    __func__, chan->ic_freq,
                    !ani_state->ofdm_weak_sig_detect_off ? "on" : "off",
                    is_on ? "on" : "off");
                if (is_on) {
                    ahp->ah_stats.ast_ani_ofdmon++;
                } else {
                    ahp->ah_stats.ast_ani_ofdmoff++;
                }
                ani_state->ofdm_weak_sig_detect_off = !is_on;
            }
            break;
        }
    case HAL_ANI_FIRSTEP_LEVEL:
        if (level >= ARRAY_LENGTH(firstep_table)) {
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                "%s: HAL_ANI_FIRSTEP_LEVEL level out of range (%u > %u)\n",
                __func__, level, (unsigned) ARRAY_LENGTH(firstep_table));
            return AH_FALSE;
        }
        /*
         * make register setting relative to default
         * from INI file & cap value
         */
        value =
            firstep_table[level] -
            firstep_table[HAL_ANI_DEF_FIRSTEP_LVL] +
            ani_state->ini_def.firstep;
        if (value < HAL_SIG_FIRSTEP_SETTING_MIN) {
            value = HAL_SIG_FIRSTEP_SETTING_MIN;
        }
        if (value > HAL_SIG_FIRSTEP_SETTING_MAX) {
            value = HAL_SIG_FIRSTEP_SETTING_MAX;
        }
        OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG, AR_PHY_FIND_SIG_FIRSTEP, value);
        /*
         * we need to set first step low register too
         * make register setting relative to default from INI file & cap value
         */
        value2 =
            firstep_table[level] -
            firstep_table[HAL_ANI_DEF_FIRSTEP_LVL] +
            ani_state->ini_def.firstep_low;
        if (value2 < HAL_SIG_FIRSTEP_SETTING_MIN) {
            value2 = HAL_SIG_FIRSTEP_SETTING_MIN;
        }
        if (value2 > HAL_SIG_FIRSTEP_SETTING_MAX) {
            value2 = HAL_SIG_FIRSTEP_SETTING_MAX;
        }
        OS_REG_RMW_FIELD(ah, AR_PHY_FIND_SIG_LOW,
            AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW, value2);

        if (level != ani_state->firstep_level) {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: ** ch %d: level %d=>%d[def:%d] firstep[level]=%d ini=%d\n",
                __func__, chan->ic_freq, ani_state->firstep_level, level,
                HAL_ANI_DEF_FIRSTEP_LVL, value, ani_state->ini_def.firstep);
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: ** ch %d: level %d=>%d[def:%d] "
                "firstep_low[level]=%d ini=%d\n",
                __func__, chan->ic_freq, ani_state->firstep_level, level,
                HAL_ANI_DEF_FIRSTEP_LVL, value2,
                ani_state->ini_def.firstep_low);
            if (level > ani_state->firstep_level) {
                ahp->ah_stats.ast_ani_stepup++;
            } else if (level < ani_state->firstep_level) {
                ahp->ah_stats.ast_ani_stepdown++;
            }
            ani_state->firstep_level = level;
        }
        break;
    case HAL_ANI_SPUR_IMMUNITY_LEVEL:
        if (level >= ARRAY_LENGTH(cycpwr_thr1_table)) {
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                "%s: HAL_ANI_SPUR_IMMUNITY_LEVEL level "
                "out of range (%u > %u)\n",
                __func__, level, (unsigned) ARRAY_LENGTH(cycpwr_thr1_table));
            return AH_FALSE;
        }
        /*
         * make register setting relative to default from INI file & cap value
         */
        value =
            cycpwr_thr1_table[level] -
            cycpwr_thr1_table[HAL_ANI_DEF_SPUR_IMMUNE_LVL] +
            ani_state->ini_def.cycpwr_thr1;
        if (value < HAL_SIG_SPUR_IMM_SETTING_MIN) {
            value = HAL_SIG_SPUR_IMM_SETTING_MIN;
        }
        if (value > HAL_SIG_SPUR_IMM_SETTING_MAX) {
            value = HAL_SIG_SPUR_IMM_SETTING_MAX;
        }
        OS_REG_RMW_FIELD(ah, AR_PHY_TIMING5, AR_PHY_TIMING5_CYCPWR_THR1, value);

        /*
         * set AR_PHY_EXT_CCA for extension channel
         * make register setting relative to default from INI file & cap value
         */
        value2 =
            cycpwr_thr1_table[level] -
            cycpwr_thr1_table[HAL_ANI_DEF_SPUR_IMMUNE_LVL] +
            ani_state->ini_def.cycpwr_thr1_ext;
        if (value2 < HAL_SIG_SPUR_IMM_SETTING_MIN) {
            value2 = HAL_SIG_SPUR_IMM_SETTING_MIN;
        }
        if (value2 > HAL_SIG_SPUR_IMM_SETTING_MAX) {
            value2 = HAL_SIG_SPUR_IMM_SETTING_MAX;
        }
        OS_REG_RMW_FIELD(ah, AR_PHY_EXT_CCA, AR_PHY_EXT_CYCPWR_THR1, value2);

        if (level != ani_state->spur_immunity_level) {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: ** ch %d: level %d=>%d[def:%d] "
                "cycpwr_thr1[level]=%d ini=%d\n",
                __func__, chan->ic_freq, ani_state->spur_immunity_level, level,
                HAL_ANI_DEF_SPUR_IMMUNE_LVL, value,
                ani_state->ini_def.cycpwr_thr1);
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: ** ch %d: level %d=>%d[def:%d] "
                "cycpwr_thr1_ext[level]=%d ini=%d\n",
                __func__, chan->ic_freq, ani_state->spur_immunity_level, level,
                HAL_ANI_DEF_SPUR_IMMUNE_LVL, value2,
                ani_state->ini_def.cycpwr_thr1_ext);
            if (level > ani_state->spur_immunity_level) {
                ahp->ah_stats.ast_ani_spurup++;
            } else if (level < ani_state->spur_immunity_level) {
                ahp->ah_stats.ast_ani_spurdown++;
            }
            ani_state->spur_immunity_level = level;
        }
        break;
    case HAL_ANI_MRC_CCK:
        /*
         * is_on == 1 means MRC CCK ON (default, less noise imm)
         * is_on == 0 means MRC CCK is OFF (more noise imm)
         */
        is_on = param ? 1 : 0;
        if (!AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
                AR_PHY_MRC_CCK_ENABLE, is_on);
            OS_REG_RMW_FIELD(ah, AR_PHY_MRC_CCK_CTRL,
                AR_PHY_MRC_CCK_MUX_REG, is_on);
        }
        if ((!is_on) != ani_state->mrc_cck_off) {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: ** ch %d: MRC CCK: %s=>%s\n", __func__, chan->ic_freq,
                !ani_state->mrc_cck_off ? "on" : "off", is_on ? "on" : "off");
            if (is_on) {
                ahp->ah_stats.ast_ani_ccklow++;
            } else {
                ahp->ah_stats.ast_ani_cckhigh++;
            }
            ani_state->mrc_cck_off = !is_on;
        }
        break;
    case HAL_ANI_PRESENT:
        break;
#ifdef AH_PRIVATE_DIAG
    case HAL_ANI_MODE:
        if (param == 0) {
            ahp->ah_proc_phy_err &= ~HAL_PROCESS_ANI;
            /* Turn off HW counters if we have them */
            ar9300_ani_detach(ah);
            if (AH_PRIVATE(ah)->ah_curchan == NULL) {
                return AH_TRUE;
            }
            /* if we're turning off ANI, reset regs back to INI settings */
            if (ah->ah_config.ath_hal_enable_ani) {
                HAL_ANI_CMD savefunc = ahp->ah_ani_function;
                /* temporarly allow all functions so we can reset */
                ahp->ah_ani_function = HAL_ANI_ALL;
                HALDEBUG(ah, HAL_DEBUG_ANI,
                    "%s: disable all ANI functions\n", __func__);
                ar9300_ani_set_odfm_noise_immunity_level(
                    ah, HAL_ANI_OFDM_DEF_LEVEL);
                ar9300_ani_set_cck_noise_immunity_level(
                    ah, HAL_ANI_CCK_DEF_LEVEL);
                ahp->ah_ani_function = savefunc;
            }
        } else {            /* normal/auto mode */
            HALDEBUG(ah, HAL_DEBUG_ANI, "%s: enabled\n", __func__);
            ahp->ah_proc_phy_err |= HAL_PROCESS_ANI;
            if (AH_PRIVATE(ah)->ah_curchan == NULL) {
                return AH_TRUE;
            }
            ar9300_enable_mib_counters(ah);
            ar9300_ani_reset(ah, AH_FALSE);
            ani_state = ahp->ah_curani;
        }
        HALDEBUG(ah, HAL_DEBUG_ANI, "5 ANC: ahp->ah_proc_phy_err %x \n",
                 ahp->ah_proc_phy_err);
        break;
    case HAL_ANI_PHYERR_RESET:
        ahp->ah_stats.ast_ani_ofdmerrs = 0;
        ahp->ah_stats.ast_ani_cckerrs = 0;
        break;
#endif /* AH_PRIVATE_DIAG */
    default:
#if HAL_ANI_DEBUG
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: invalid cmd 0x%02x (allowed=0x%02x)\n",
            __func__, cmd, ahp->ah_ani_function);
#endif
        return AH_FALSE;
    }

#if HAL_ANI_DEBUG
    HALDEBUG(ah, HAL_DEBUG_ANI,
        "%s: ANI parameters: SI=%d, ofdm_ws=%s FS=%d MRCcck=%s listen_time=%d "
        "CC=%d listen=%d ofdm_errs=%d cck_errs=%d\n",
        __func__, ani_state->spur_immunity_level,
        !ani_state->ofdm_weak_sig_detect_off ? "on" : "off",
        ani_state->firstep_level, !ani_state->mrc_cck_off ? "on" : "off",
        ani_state->listen_time, ani_state->cycle_count,
        ani_state->listen_time, ani_state->ofdm_phy_err_count,
        ani_state->cck_phy_err_count);
#endif

#ifndef REMOVE_PKT_LOG
    /* do pktlog */
    {
        struct log_ani log_data;

        /* Populate the ani log record */
        log_data.phy_stats_disable = DO_ANI(ah);
        log_data.noise_immun_lvl = ani_state->ofdm_noise_immunity_level;
        log_data.spur_immun_lvl = ani_state->spur_immunity_level;
        log_data.ofdm_weak_det = ani_state->ofdm_weak_sig_detect_off;
        log_data.cck_weak_thr = ani_state->cck_noise_immunity_level;
        log_data.fir_lvl = ani_state->firstep_level;
        log_data.listen_time = ani_state->listen_time;
        log_data.cycle_count = ani_state->cycle_count;
        /* express ofdm_phy_err_count as errors/second */
        log_data.ofdm_phy_err_count = ani_state->listen_time ?
            ani_state->ofdm_phy_err_count * 1000 / ani_state->listen_time : 0;
        /* express cck_phy_err_count as errors/second */
        log_data.cck_phy_err_count =  ani_state->listen_time ?
            ani_state->cck_phy_err_count * 1000 / ani_state->listen_time  : 0;
        log_data.rssi = ani_state->rssi;

        /* clear interrupt context flag */
        ath_hal_log_ani(AH_PRIVATE(ah)->ah_sc, &log_data, 0);
    }
#endif

    return AH_TRUE;
}

static void
ar9300_ani_restart(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;

    if (!DO_ANI(ah)) {
        return;
    }

    ani_state = ahp->ah_curani;

    ani_state->listen_time = 0;

    OS_REG_WRITE(ah, AR_PHY_ERR_1, 0);
    OS_REG_WRITE(ah, AR_PHY_ERR_2, 0);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);

    /* Clear the mib counters and save them in the stats */
    ar9300_update_mib_mac_stats(ah);

    ani_state->ofdm_phy_err_count = 0;
    ani_state->cck_phy_err_count = 0;
}

static void
ar9300_ani_ofdm_err_trigger(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;

    if (!DO_ANI(ah)) {
        return;
    }

    ani_state = ahp->ah_curani;

    if (ani_state->ofdm_noise_immunity_level < HAL_ANI_OFDM_MAX_LEVEL) {
        ar9300_ani_set_odfm_noise_immunity_level(
            ah, ani_state->ofdm_noise_immunity_level + 1);
    }
}

static void
ar9300_ani_cck_err_trigger(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;

    if (!DO_ANI(ah)) {
        return;
    }

    ani_state = ahp->ah_curani;

    if (ani_state->cck_noise_immunity_level < HAL_ANI_CCK_MAX_LEVEL) {
        ar9300_ani_set_cck_noise_immunity_level(
            ah, ani_state->cck_noise_immunity_level + 1);
    }
}

/*
 * Restore the ANI parameters in the HAL and reset the statistics.
 * This routine should be called for every hardware reset and for
 * every channel change.
 */
void
ar9300_ani_reset(struct ath_hal *ah, HAL_BOOL is_scanning)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;
    const struct ieee80211_channel *chan = AH_PRIVATE(ah)->ah_curchan;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    int index;

    HALASSERT(chan != AH_NULL);

    if (!DO_ANI(ah)) {
        return;
    }

    /*
     * we need to re-point to the correct ANI state since the channel
     * may have changed due to a fast channel change
    */
    index = ar9300_get_ani_channel_index(ah, chan);
    ani_state = &ahp->ah_ani[index];
    HALASSERT(ani_state != AH_NULL);
    ahp->ah_curani = ani_state;

    ahp->ah_stats.ast_ani_reset++;

    ani_state->phy_noise_spur = 0;

    /* only allow a subset of functions in AP mode */
    if (AH_PRIVATE(ah)->ah_opmode == HAL_M_HOSTAP) {
        if (IS_CHAN_2GHZ(ichan)) {
            ahp->ah_ani_function = (1 << HAL_ANI_SPUR_IMMUNITY_LEVEL) |
                                   (1 << HAL_ANI_FIRSTEP_LEVEL) |
                                   (1 << HAL_ANI_MRC_CCK);
        } else {
            ahp->ah_ani_function = 0;
        }
    } else {
      ahp->ah_ani_function = HAL_ANI_ALL;
    }

    /* always allow mode (on/off) to be controlled */
    ahp->ah_ani_function |= HAL_ANI_MODE;

    if (is_scanning ||
        (AH_PRIVATE(ah)->ah_opmode != HAL_M_STA &&
         AH_PRIVATE(ah)->ah_opmode != HAL_M_IBSS))
    {
        /*
         * If we're scanning or in AP mode, the defaults (ini) should be
         * in place.
         * For an AP we assume the historical levels for this channel are
         * probably outdated so start from defaults instead.
         */
        if (ani_state->ofdm_noise_immunity_level != HAL_ANI_OFDM_DEF_LEVEL ||
            ani_state->cck_noise_immunity_level != HAL_ANI_CCK_DEF_LEVEL)
        {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: Restore defaults: opmode %u chan %d Mhz/0x%x "
                "is_scanning=%d restore=%d ofdm:%d cck:%d\n",
                __func__, AH_PRIVATE(ah)->ah_opmode, chan->ic_freq,
                chan->ic_flags, is_scanning, ani_state->must_restore,
                ani_state->ofdm_noise_immunity_level,
                ani_state->cck_noise_immunity_level);
            /*
             * for STA/IBSS, we want to restore the historical values later
             * (when we're not scanning)
             */
            if (AH_PRIVATE(ah)->ah_opmode == HAL_M_STA ||
                AH_PRIVATE(ah)->ah_opmode == HAL_M_IBSS)
            {
                ar9300_ani_control(ah, HAL_ANI_SPUR_IMMUNITY_LEVEL,
                    HAL_ANI_DEF_SPUR_IMMUNE_LVL);
                ar9300_ani_control(
                    ah, HAL_ANI_FIRSTEP_LEVEL, HAL_ANI_DEF_FIRSTEP_LVL);
                ar9300_ani_control(ah, HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,
                    HAL_ANI_USE_OFDM_WEAK_SIG);
                ar9300_ani_control(ah, HAL_ANI_MRC_CCK, HAL_ANI_ENABLE_MRC_CCK);
                ani_state->must_restore = AH_TRUE;
            } else {
                ar9300_ani_set_odfm_noise_immunity_level(
                    ah, HAL_ANI_OFDM_DEF_LEVEL);
                ar9300_ani_set_cck_noise_immunity_level(
                    ah, HAL_ANI_CCK_DEF_LEVEL);
            }
        }
    } else {
        /*
         * restore historical levels for this channel
         */
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: Restore history: opmode %u chan %d Mhz/0x%x is_scanning=%d "
            "restore=%d ofdm:%d cck:%d\n",
            __func__, AH_PRIVATE(ah)->ah_opmode, chan->ic_freq,
            chan->ic_flags, is_scanning, ani_state->must_restore,
            ani_state->ofdm_noise_immunity_level,
            ani_state->cck_noise_immunity_level);
        ar9300_ani_set_odfm_noise_immunity_level(
            ah, ani_state->ofdm_noise_immunity_level);
        ar9300_ani_set_cck_noise_immunity_level(
            ah, ani_state->cck_noise_immunity_level);
        ani_state->must_restore = AH_FALSE;
    }

    /* enable phy counters */
    ar9300_ani_restart(ah);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_1, AR_PHY_ERR_OFDM_TIMING);
    OS_REG_WRITE(ah, AR_PHY_ERR_MASK_2, AR_PHY_ERR_CCK_TIMING);
}

/*
 * Process a MIB interrupt.  We may potentially be invoked because
 * any of the MIB counters overflow/trigger so don't assume we're
 * here because a PHY error counter triggered.
 */
void
ar9300_process_mib_intr(struct ath_hal *ah, const HAL_NODE_STATS *stats)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t phy_cnt1, phy_cnt2;

#if 0
    HALDEBUG(ah, HAL_DEBUG_ANI, "%s: Processing Mib Intr\n", __func__);
#endif

    /* Reset these counters regardless */
    OS_REG_WRITE(ah, AR_FILT_OFDM, 0);
    OS_REG_WRITE(ah, AR_FILT_CCK, 0);
    if (!(OS_REG_READ(ah, AR_SLP_MIB_CTRL) & AR_SLP_MIB_PENDING)) {
        OS_REG_WRITE(ah, AR_SLP_MIB_CTRL, AR_SLP_MIB_CLEAR);
    }

    /* Clear the mib counters and save them in the stats */
    ar9300_update_mib_mac_stats(ah);
    ahp->ah_stats.ast_nodestats = *stats;

    if (!DO_ANI(ah)) {
        /*
         * We must always clear the interrupt cause by resetting
         * the phy error regs.
         */
        OS_REG_WRITE(ah, AR_PHY_ERR_1, 0);
        OS_REG_WRITE(ah, AR_PHY_ERR_2, 0);
        return;
    }

    /* NB: these are not reset-on-read */
    phy_cnt1 = OS_REG_READ(ah, AR_PHY_ERR_1);
    phy_cnt2 = OS_REG_READ(ah, AR_PHY_ERR_2);
#if HAL_ANI_DEBUG
    HALDEBUG(ah, HAL_DEBUG_ANI,
        "%s: Errors: OFDM=0x%08x-0x0=%d   CCK=0x%08x-0x0=%d\n",
        __func__, phy_cnt1, phy_cnt1, phy_cnt2, phy_cnt2);
#endif
    if (((phy_cnt1 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK) ||
        ((phy_cnt2 & AR_MIBCNT_INTRMASK) == AR_MIBCNT_INTRMASK)) {
        /* NB: always restart to insure the h/w counters are reset */
        ar9300_ani_restart(ah);
    }
}


static void
ar9300_ani_lower_immunity(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state = ahp->ah_curani;

    if (ani_state->ofdm_noise_immunity_level > 0 &&
        (ani_state->ofdms_turn || ani_state->cck_noise_immunity_level == 0)) {
        /*
         * lower OFDM noise immunity
         */
        ar9300_ani_set_odfm_noise_immunity_level(
            ah, ani_state->ofdm_noise_immunity_level - 1);

        /*
         * only lower either OFDM or CCK errors per turn
         * we lower the other one next time
         */
        return;
    }

    if (ani_state->cck_noise_immunity_level > 0) {
        /*
         * lower CCK noise immunity
         */
        ar9300_ani_set_cck_noise_immunity_level(
            ah, ani_state->cck_noise_immunity_level - 1);
    }
}

/* convert HW counter values to ms using mode specifix clock rate */
//#define CLOCK_RATE(_ah)  (ath_hal_chan_2_clock_rate_mhz(_ah) * 1000)
#define CLOCK_RATE(_ah)  (ath_hal_mac_clks(ah, 1000))

/*
 * Return an approximation of the time spent ``listening'' by
 * deducting the cycles spent tx'ing and rx'ing from the total
 * cycle count since our last call.  A return value <0 indicates
 * an invalid/inconsistent time.
 */
static int32_t
ar9300_ani_get_listen_time(struct ath_hal *ah, HAL_ANISTATS *ani_stats)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;
    u_int32_t tx_frame_count, rx_frame_count, cycle_count;
    u_int32_t rx_busy_count, rx_ext_busy_count;
    int32_t listen_time;

    tx_frame_count = OS_REG_READ(ah, AR_TFCNT);
    rx_frame_count = OS_REG_READ(ah, AR_RFCNT);
    rx_busy_count = OS_REG_READ(ah, AR_RCCNT);
    rx_ext_busy_count = OS_REG_READ(ah, AR_EXTRCCNT);
    cycle_count = OS_REG_READ(ah, AR_CCCNT);

    ani_state = ahp->ah_curani;
    if (ani_state->cycle_count == 0 || ani_state->cycle_count > cycle_count) {
        /*
         * Cycle counter wrap (or initial call); it's not possible
         * to accurately calculate a value because the registers
         * right shift rather than wrap--so punt and return 0.
         */
        listen_time = 0;
        ahp->ah_stats.ast_ani_lzero++;
#if HAL_ANI_DEBUG
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: 1st call: ani_state->cycle_count=%d\n",
            __func__, ani_state->cycle_count);
#endif
    } else {
        int32_t ccdelta = cycle_count - ani_state->cycle_count;
        int32_t rfdelta = rx_frame_count - ani_state->rx_frame_count;
        int32_t tfdelta = tx_frame_count - ani_state->tx_frame_count;
        int32_t rcdelta = rx_busy_count - ani_state->rx_busy_count;
        int32_t extrcdelta = rx_ext_busy_count - ani_state->rx_ext_busy_count;
        listen_time = (ccdelta - rfdelta - tfdelta) / CLOCK_RATE(ah);
//#if HAL_ANI_DEBUG
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: cyclecount=%d, rfcount=%d, tfcount=%d, rcdelta=%d, extrcdelta=%d, listen_time=%d "
            "CLOCK_RATE=%d\n",
            __func__, ccdelta, rfdelta, tfdelta, rcdelta, extrcdelta,
            listen_time, CLOCK_RATE(ah));
//#endif
            /* Populate as appropriate */
            ani_stats->cyclecnt_diff = ccdelta;
            ani_stats->rxclr_cnt = rcdelta;
            ani_stats->txframecnt_diff = tfdelta;
            ani_stats->rxframecnt_diff = rfdelta;
            ani_stats->extrxclr_cnt = extrcdelta;
            ani_stats->listen_time = listen_time;
            ani_stats->valid = AH_TRUE;
    }
    ani_state->cycle_count = cycle_count;
    ani_state->tx_frame_count = tx_frame_count;
    ani_state->rx_frame_count = rx_frame_count;
    ani_state->rx_busy_count = rx_busy_count;
    ani_state->rx_ext_busy_count = rx_ext_busy_count;
    return listen_time;
}

/*
 * Do periodic processing.  This routine is called from a timer
 */
void
ar9300_ani_ar_poll(struct ath_hal *ah, const HAL_NODE_STATS *stats,
                const struct ieee80211_channel *chan, HAL_ANISTATS *ani_stats)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;
    int32_t listen_time;
    u_int32_t ofdm_phy_err_rate, cck_phy_err_rate;
    u_int32_t ofdm_phy_err_cnt, cck_phy_err_cnt;
    HAL_BOOL old_phy_noise_spur;

    ani_state = ahp->ah_curani;
    ahp->ah_stats.ast_nodestats = *stats;        /* XXX optimize? */

    if (ani_state == NULL) {
        /* should not happen */
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s: can't poll - no ANI not initialized for this channel\n",
            __func__);
        return;
    }

    /*
     * ar9300_ani_ar_poll is never called while scanning but we may have been
     * scanning and now just restarted polling.  In this case we need to
     * restore historical values.
     */
    if (ani_state->must_restore) {
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: must restore - calling ar9300_ani_restart\n", __func__);
        ar9300_ani_reset(ah, AH_FALSE);
        return;
    }

    listen_time = ar9300_ani_get_listen_time(ah, ani_stats);
    if (listen_time <= 0) {
        ahp->ah_stats.ast_ani_lneg++;
        /* restart ANI period if listen_time is invalid */
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: listen_time=%d - calling ar9300_ani_restart\n",
            __func__, listen_time);
        ar9300_ani_restart(ah);
        return;
    }
    /* XXX beware of overflow? */
    ani_state->listen_time += listen_time;

    /* Clear the mib counters and save them in the stats */
    ar9300_update_mib_mac_stats(ah);
    /* NB: these are not reset-on-read */
    ofdm_phy_err_cnt = OS_REG_READ(ah, AR_PHY_ERR_1);
    cck_phy_err_cnt = OS_REG_READ(ah, AR_PHY_ERR_2);

    /* Populate HAL_ANISTATS */
    /* XXX TODO: are these correct? */
    if (ani_stats) {
            ani_stats->cckphyerr_cnt =
               cck_phy_err_cnt - ani_state->cck_phy_err_count;
            ani_stats->ofdmphyerrcnt_diff =
              ofdm_phy_err_cnt - ani_state->ofdm_phy_err_count;
    }

    /* NB: only use ast_ani_*errs with AH_PRIVATE_DIAG */
    ahp->ah_stats.ast_ani_ofdmerrs +=
        ofdm_phy_err_cnt - ani_state->ofdm_phy_err_count;
    ani_state->ofdm_phy_err_count = ofdm_phy_err_cnt;

    ahp->ah_stats.ast_ani_cckerrs +=
        cck_phy_err_cnt - ani_state->cck_phy_err_count;
    ani_state->cck_phy_err_count = cck_phy_err_cnt;

    /*
     * Note - the ANI code is using the aggregate listen time.
     * The AR_PHY_CNT1/AR_PHY_CNT2 registers here are also
     * free running, not clear-on-read and are free-running.
     *
     * So, ofdm_phy_err_rate / cck_phy_err_rate are accumulating
     * the same as listenTime is accumulating.
     */

#if HAL_ANI_DEBUG
    HALDEBUG(ah, HAL_DEBUG_ANI,
        "%s: Errors: OFDM=0x%08x-0x0=%d   CCK=0x%08x-0x0=%d\n",
        __func__, ofdm_phy_err_cnt, ofdm_phy_err_cnt,
        cck_phy_err_cnt, cck_phy_err_cnt);
#endif

    /*
     * If ani is not enabled, return after we've collected
     * statistics
     */
    if (!DO_ANI(ah)) {
        return;
    }

    /*
     * Calculate the OFDM/CCK phy error rate over the listen time interval.
     * This is used in subsequent math to see if the OFDM/CCK phy error rate
     * is above or below the threshold checks.
     */

    ofdm_phy_err_rate =
        ani_state->ofdm_phy_err_count * 1000 / ani_state->listen_time;
    cck_phy_err_rate =
        ani_state->cck_phy_err_count * 1000 / ani_state->listen_time;

    HALDEBUG(ah, HAL_DEBUG_ANI,
        "%s: listen_time=%d (total: %d) OFDM:%d errs=%d/s CCK:%d errs=%d/s ofdm_turn=%d\n",
        __func__, listen_time,
        ani_state->listen_time,
        ani_state->ofdm_noise_immunity_level, ofdm_phy_err_rate,
        ani_state->cck_noise_immunity_level, cck_phy_err_rate,
        ani_state->ofdms_turn);

    /*
     * Check for temporary noise spurs.  This is intended to be used by
     * rate control to check if we should try higher packet rates or not.
     * If the noise period is short enough then we shouldn't avoid trying
     * higher rates but if the noise is high/sustained then it's likely
     * not a great idea to try the higher MCS rates.
     */
    if (ani_state->listen_time >= HAL_NOISE_DETECT_PERIOD) {
        old_phy_noise_spur = ani_state->phy_noise_spur;
        if (ofdm_phy_err_rate <= ani_state->ofdm_trig_low &&
            cck_phy_err_rate <= ani_state->cck_trig_low) {
            if (ani_state->listen_time >= HAL_NOISE_RECOVER_PERIOD) {
                ani_state->phy_noise_spur = 0;
            }
        } else {
            ani_state->phy_noise_spur = 1;
        }
        if (old_phy_noise_spur != ani_state->phy_noise_spur) {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                     "%s: environment change from %d to %d\n",
                     __func__, old_phy_noise_spur, ani_state->phy_noise_spur);
        }
    }

    if (ani_state->listen_time > 5 * ahp->ah_ani_period) {
        /*
         * Check to see if need to lower immunity if
         * 5 ani_periods have passed
         */
        if (ofdm_phy_err_rate <= ani_state->ofdm_trig_low &&
            cck_phy_err_rate <= ani_state->cck_trig_low)
        {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: 1. listen_time=%d OFDM:%d errs=%d/s(<%d)  "
                "CCK:%d errs=%d/s(<%d) -> ar9300_ani_lower_immunity\n",
                __func__, ani_state->listen_time,
                ani_state->ofdm_noise_immunity_level, ofdm_phy_err_rate,
                ani_state->ofdm_trig_low, ani_state->cck_noise_immunity_level,
                cck_phy_err_rate, ani_state->cck_trig_low);
            ar9300_ani_lower_immunity(ah);
            ani_state->ofdms_turn = !ani_state->ofdms_turn;
        }
        /*
         * Force an ANI restart regardless of whether the lower immunity
         * level was met.
         */
        HALDEBUG(ah, HAL_DEBUG_ANI,
            "%s: 1 listen_time=%d ofdm=%d/s cck=%d/s - "
            "calling ar9300_ani_restart\n",
            __func__, ani_state->listen_time,
            ofdm_phy_err_rate, cck_phy_err_rate);
        ar9300_ani_restart(ah);
     } else if (ani_state->listen_time > ahp->ah_ani_period) {
        /* check to see if need to raise immunity */
        if (ofdm_phy_err_rate > ani_state->ofdm_trig_high &&
            (cck_phy_err_rate <= ani_state->cck_trig_high ||
             ani_state->ofdms_turn))
        {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: 2 listen_time=%d OFDM:%d errs=%d/s(>%d) -> "
                "ar9300_ani_ofdm_err_trigger\n",
                __func__, ani_state->listen_time,
                ani_state->ofdm_noise_immunity_level, ofdm_phy_err_rate,
                ani_state->ofdm_trig_high);
            ar9300_ani_ofdm_err_trigger(ah);
            ar9300_ani_restart(ah);
            ani_state->ofdms_turn = AH_FALSE;
        } else if (cck_phy_err_rate > ani_state->cck_trig_high) {
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "%s: 3 listen_time=%d CCK:%d errs=%d/s(>%d) -> "
                "ar9300_ani_cck_err_trigger\n",
                __func__, ani_state->listen_time,
                ani_state->cck_noise_immunity_level, cck_phy_err_rate,
                ani_state->cck_trig_high);
            ar9300_ani_cck_err_trigger(ah);
            ar9300_ani_restart(ah);
            ani_state->ofdms_turn = AH_TRUE;
        }
    }

    /*
     * Note that currently this poll function doesn't reset the listen
     * time after it accumulates a second worth of error samples.
     * It will continue to accumulate samples until a counter overflows,
     * or a raise threshold is met, or 5 seconds passes.
     */
}

/*
 * The poll function above calculates short noise spurs, caused by non-80211
 * devices, based on OFDM/CCK Phy errs.
 * If the noise is short enough, we don't want our ratectrl Algo to stop probing
 * higher rates, due to bad PER.
 */
HAL_BOOL
ar9300_is_ani_noise_spur(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_ani_state *ani_state;

    ani_state = ahp->ah_curani;

    return ani_state->phy_noise_spur;
}

