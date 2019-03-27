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
#include "ah_desc.h"

#include "ar9300.h"
#include "ar9300reg.h"
#include "ar9300phy.h"
#include "ar9300desc.h"

#define FIX_NOISE_FLOOR     1


/* Additional Time delay to wait after activiting the Base band */
#define BASE_ACTIVATE_DELAY         100     /* usec */
#define RTC_PLL_SETTLE_DELAY        100     /* usec */
#define COEF_SCALE_S                24
#define HT40_CHANNEL_CENTER_SHIFT   10      /* MHz      */

#define DELPT 32

/* XXX Duplicates! (in ar9300desc.h) */
#if 0
extern  HAL_BOOL ar9300_reset_tx_queue(struct ath_hal *ah, u_int q);
extern  u_int32_t ar9300_num_tx_pending(struct ath_hal *ah, u_int q);
#endif


#define MAX_MEASUREMENT 8
#define MAXIQCAL 3
struct coeff_t {
    int32_t mag_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT][MAXIQCAL];
    int32_t phs_coeff[AR9300_MAX_CHAINS][MAX_MEASUREMENT][MAXIQCAL];
    int32_t iqc_coeff[2];
    int last_nmeasurement;
    HAL_BOOL last_cal;
};

static HAL_BOOL ar9300_tx_iq_cal_hw_run(struct ath_hal *ah);
static void ar9300_tx_iq_cal_post_proc(struct ath_hal *ah,HAL_CHANNEL_INTERNAL *ichan,
       int iqcal_idx, int max_iqcal, HAL_BOOL is_cal_reusable, HAL_BOOL apply_last_corr);
static void ar9300_tx_iq_cal_outlier_detection(struct ath_hal *ah,HAL_CHANNEL_INTERNAL *ichan,
       u_int32_t num_chains, struct coeff_t *coeff, HAL_BOOL is_cal_reusable);
#if ATH_SUPPORT_CAL_REUSE
static void ar9300_tx_iq_cal_apply(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan);
#endif


static inline void ar9300_prog_ini(struct ath_hal *ah, struct ar9300_ini_array *ini_arr, int column);
static inline void ar9300_set_rf_mode(struct ath_hal *ah, struct ieee80211_channel *chan);
static inline HAL_BOOL ar9300_init_cal(struct ath_hal *ah, struct ieee80211_channel *chan, HAL_BOOL skip_if_none, HAL_BOOL apply_last_corr);
static inline void ar9300_init_user_settings(struct ath_hal *ah);

#ifdef HOST_OFFLOAD
/* 
 * For usb offload solution, some USB registers must be tuned 
 * to gain better stability/performance but these registers
 * might be changed while doing wlan reset so do this here 
 */
#define WAR_USB_DISABLE_PLL_LOCK_DETECT(__ah) \
do { \
    if (AR_SREV_HORNET(__ah) || AR_SREV_WASP(__ah)) { \
        volatile u_int32_t *usb_ctrl_r1 = (u_int32_t *) 0xb8116c84; \
        volatile u_int32_t *usb_ctrl_r2 = (u_int32_t *) 0xb8116c88; \
        *usb_ctrl_r1 = (*usb_ctrl_r1 & 0xffefffff); \
        *usb_ctrl_r2 = (*usb_ctrl_r2 & 0xfc1fffff) | (1 << 21) | (3 << 22); \
    } \
} while (0)
#else
#define WAR_USB_DISABLE_PLL_LOCK_DETECT(__ah)
#endif

/*
 * Note: the below is the version that ships with ath9k.
 * The original HAL version is above.
 */

static void
ar9300_disable_pll_lock_detect(struct ath_hal *ah)
{
	/*
	 * On AR9330 and AR9340 devices, some PHY registers must be
	 * tuned to gain better stability/performance. These registers
	 * might be changed while doing wlan reset so the registers must
	 * be reprogrammed after each reset.
	 */
	if (AR_SREV_HORNET(ah) || AR_SREV_WASP(ah)) {
		HALDEBUG(ah, HAL_DEBUG_RESET, "%s: called\n", __func__);
		OS_REG_CLR_BIT(ah, AR_PHY_USB_CTRL1, (1 << 20));
		OS_REG_RMW(ah, AR_PHY_USB_CTRL2,
		    (1 << 21) | (0xf << 22),
		    (1 << 21) | (0x3 << 22));
	}
}

static inline void
ar9300_attach_hw_platform(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp->ah_hwp = HAL_TRUE_CHIP;
    return;
}

/* Adjust various register settings based on half/quarter rate clock setting.
 * This includes: +USEC, TX/RX latency, 
 *                + IFS params: slot, eifs, misc etc.
 * SIFS stays the same.
 */
static void 
ar9300_set_ifs_timing(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t tx_lat, rx_lat, usec, slot, regval, eifs;

    regval = OS_REG_READ(ah, AR_USEC);
    regval &= ~(AR_USEC_RX_LATENCY | AR_USEC_TX_LATENCY | AR_USEC_USEC);
    if (IEEE80211_IS_CHAN_HALF(chan)) { /* half rates */
        slot = ar9300_mac_to_clks(ah, AR_SLOT_HALF);
        eifs = ar9300_mac_to_clks(ah, AR_EIFS_HALF);
        if (IS_5GHZ_FAST_CLOCK_EN(ah, chan)) { /* fast clock */
            rx_lat = SM(AR_RX_LATENCY_HALF_FAST_CLOCK, AR_USEC_RX_LATENCY);
            tx_lat = SM(AR_TX_LATENCY_HALF_FAST_CLOCK, AR_USEC_TX_LATENCY);
            usec = SM(AR_USEC_HALF_FAST_CLOCK, AR_USEC_USEC);
        } else {
            rx_lat = SM(AR_RX_LATENCY_HALF, AR_USEC_RX_LATENCY);
            tx_lat = SM(AR_TX_LATENCY_HALF, AR_USEC_TX_LATENCY);
            usec = SM(AR_USEC_HALF, AR_USEC_USEC);
        }
    } else { /* quarter rate */
        slot = ar9300_mac_to_clks(ah, AR_SLOT_QUARTER);
        eifs = ar9300_mac_to_clks(ah, AR_EIFS_QUARTER);
        if (IS_5GHZ_FAST_CLOCK_EN(ah, chan)) { /* fast clock */
            rx_lat = SM(AR_RX_LATENCY_QUARTER_FAST_CLOCK, AR_USEC_RX_LATENCY);
            tx_lat = SM(AR_TX_LATENCY_QUARTER_FAST_CLOCK, AR_USEC_TX_LATENCY);
            usec = SM(AR_USEC_QUARTER_FAST_CLOCK, AR_USEC_USEC);
        } else {
            rx_lat = SM(AR_RX_LATENCY_QUARTER, AR_USEC_RX_LATENCY);
            tx_lat = SM(AR_TX_LATENCY_QUARTER, AR_USEC_TX_LATENCY);
            usec = SM(AR_USEC_QUARTER, AR_USEC_USEC);
        }
    }

    OS_REG_WRITE(ah, AR_USEC, (usec | regval | tx_lat | rx_lat));
    OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, slot);
    OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, eifs);
}


/*
 * This inline function configures the chip either
 * to encrypt/decrypt management frames or pass thru
 */
static inline void
ar9300_init_mfp(struct ath_hal * ah)
{
    u_int32_t   mfpcap, mfp_qos;

    ath_hal_getcapability(ah, HAL_CAP_MFP, 0, &mfpcap);

    if (mfpcap == HAL_MFP_QOSDATA) {
        /* Treat like legacy hardware. Do not touch the MFP registers. */
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s forced to use QOSDATA\n", __func__);
        return;
    }

    /* MFP support (Sowl 1.0 or greater) */
    if (mfpcap == HAL_MFP_HW_CRYPTO) {
        /* configure hardware MFP support */
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s using HW crypto\n", __func__);
        OS_REG_RMW_FIELD(ah,
            AR_AES_MUTE_MASK1, AR_AES_MUTE_MASK1_FC_MGMT, AR_AES_MUTE_MASK1_FC_MGMT_MFP);
        OS_REG_RMW(ah,
            AR_PCU_MISC_MODE2, AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE,
            AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT);
        /*
        * Mask used to construct AAD for CCMP-AES
        * Cisco spec defined bits 0-3 as mask 
        * IEEE802.11w defined as bit 4.
        */		
        if (ath_hal_get_mfp_qos(ah)) {
            mfp_qos = AR_MFP_QOS_MASK_IEEE;
        } else {
            mfp_qos = AR_MFP_QOS_MASK_CISCO;
        }
        OS_REG_RMW_FIELD(ah,
            AR_PCU_MISC_MODE2, AR_PCU_MISC_MODE2_MGMT_QOS, mfp_qos);
    } else if (mfpcap == HAL_MFP_PASSTHRU) {
        /* Disable en/decrypt by hardware */
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s using passthru\n", __func__);
        OS_REG_RMW(ah,
            AR_PCU_MISC_MODE2,
            AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT,
            AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE);
    }
}

void
ar9300_get_channel_centers(struct ath_hal *ah, const struct ieee80211_channel *chan,
    CHAN_CENTERS *centers)
{
    int8_t      extoff;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    if (!IEEE80211_IS_CHAN_HT40(chan)) {
        centers->ctl_center = centers->ext_center =
        centers->synth_center = ichan->channel;
        return;
    }

    HALASSERT(IEEE80211_IS_CHAN_HT40(chan));

    /*
     * In 20/40 phy mode, the center frequency is
     * "between" the primary and extension channels.
     */
    if (IEEE80211_IS_CHAN_HT40U(chan)) {
        centers->synth_center = ichan->channel + HT40_CHANNEL_CENTER_SHIFT;
        extoff = 1;
    } else {
        centers->synth_center = ichan->channel - HT40_CHANNEL_CENTER_SHIFT;
        extoff = -1;
    }

    centers->ctl_center =
        centers->synth_center - (extoff * HT40_CHANNEL_CENTER_SHIFT);
    centers->ext_center =
        centers->synth_center +
        (extoff * ((ahp->ah_ext_prot_spacing == HAL_HT_EXTPROTSPACING_20) ?
            HT40_CHANNEL_CENTER_SHIFT : 15));
}

/*
 * Read the noise-floor values from the HW.
 * Specifically, read the minimum clear-channel assessment value for
 * each chain, for both the control and extension channels.
 * (The received power level during clear-channel periods is the
 * noise floor.)
 * These noise floor values computed by the HW will be stored in the
 * NF history buffer.
 * The HW sometimes produces bogus NF values.  To avoid using these
 * bogus values, the NF data is (a) range-limited, and (b) filtered.
 * However, this data-processing is done when reading the NF values
 * out of the history buffer.  The history buffer stores the raw values.
 * This allows the NF history buffer to be used to check for interference.
 * A single high NF reading might be a bogus HW value, but if the NF
 * readings are consistently high, it must be due to interference.
 * This is the purpose of storing raw NF values in the history buffer,
 * rather than processed values.  By looking at a history of NF values
 * that have not been range-limited, we can check if they are consistently
 * high (due to interference).
 */
#define AH_NF_SIGN_EXTEND(nf)      \
    ((nf) & 0x100) ?               \
        0 - (((nf) ^ 0x1ff) + 1) : \
        (nf)
void
ar9300_upload_noise_floor(struct ath_hal *ah, int is_2g,
    int16_t nfarray[HAL_NUM_NF_READINGS])
{
    int16_t nf;
    int chan, chain;
    u_int32_t regs[HAL_NUM_NF_READINGS] = {
        /* control channel */
        AR_PHY_CCA_0,     /* chain 0 */
        AR_PHY_CCA_1,     /* chain 1 */
        AR_PHY_CCA_2,     /* chain 2 */
        /* extension channel */
        AR_PHY_EXT_CCA,   /* chain 0 */
        AR_PHY_EXT_CCA_1, /* chain 1 */
        AR_PHY_EXT_CCA_2, /* chain 2 */
    };
    u_int8_t chainmask;

    /*
     * Within a given channel (ctl vs. ext), the CH0, CH1, and CH2
     * masks and shifts are the same, though they differ for the
     * control vs. extension channels.
     */
    u_int32_t masks[2] = {
        AR_PHY_MINCCA_PWR,     /* control channel */
        AR_PHY_EXT_MINCCA_PWR, /* extention channel */
    };
    u_int8_t shifts[2] = {
        AR_PHY_MINCCA_PWR_S,     /* control channel */
        AR_PHY_EXT_MINCCA_PWR_S, /* extention channel */
    };

    /*
     * Force NF calibration for all chains.
     */
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        chainmask = 0x01;
    } else if (AR_SREV_WASP(ah) || AR_SREV_JUPITER(ah) || AR_SREV_HONEYBEE(ah)) {
        chainmask = 0x03;
    } else {
        chainmask = 0x07;
    }

    for (chan = 0; chan < 2 /*ctl,ext*/; chan++) {
        for (chain = 0; chain < AR9300_MAX_CHAINS; chain++) {
            int i;
            
            if (!((chainmask >> chain) & 0x1)) {
                continue;
            }
            i = chan * AR9300_MAX_CHAINS + chain;
            nf = (OS_REG_READ(ah, regs[i]) & masks[chan]) >> shifts[chan];
            nfarray[i] = AH_NF_SIGN_EXTEND(nf);
        }
    }
}

/* ar9300_get_min_cca_pwr -
 * Used by the scan function for a quick read of the noise floor.
 * This is used to detect presence of CW interference such as video bridge.
 * The noise floor is assumed to have been already started during reset
 * called during channel change. The function checks if the noise floor
 * reading is done. In case it has been done, it reads the noise floor value.
 * If the noise floor calibration has not been finished, it assumes this is
 * due to presence of CW interference an returns a high value for noise floor,
 * derived from the CW interference threshold + margin fudge factor. 
 */
#define BAD_SCAN_NF_MARGIN (30)
int16_t ar9300_get_min_cca_pwr(struct ath_hal *ah)
{
    int16_t nf;
//    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);


    if ((OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0) {
        nf = MS(OS_REG_READ(ah, AR_PHY_CCA_0), AR9280_PHY_MINCCA_PWR);
        if (nf & 0x100) {
            nf = 0 - ((nf ^ 0x1ff) + 1);
        }
    } else {
        /* NF calibration is not done, assume CW interference */
        nf = AH9300(ah)->nfp->nominal + AH9300(ah)->nf_cw_int_delta +
            BAD_SCAN_NF_MARGIN;
    }
    return nf;
}


/* 
 * Noise Floor values for all chains. 
 * Most recently updated values from the NF history buffer are used.
 */
void ar9300_chain_noise_floor(struct ath_hal *ah, int16_t *nf_buf,
    struct ieee80211_channel *chan, int is_scan)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i, nf_hist_len, recent_nf_index = 0;
    HAL_NFCAL_HIST_FULL *h;
    u_int8_t rx_chainmask = ahp->ah_rx_chainmask | (ahp->ah_rx_chainmask << 3);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan); 
    HALASSERT(ichan);

#ifdef ATH_NF_PER_CHAN
    /* Fill 0 if valid internal channel is not found */
    if (ichan == AH_NULL) {
        OS_MEMZERO(nf_buf, sizeof(nf_buf[0])*HAL_NUM_NF_READINGS);
        return;
    }
    h = &ichan->nf_cal_hist;
    nf_hist_len = HAL_NF_CAL_HIST_LEN_FULL;
#else
    /*
     * If a scan is not in progress, then the most recent value goes
     * into ahpriv->nf_cal_hist.  If a scan is in progress, then
     * the most recent value goes into ichan->nf_cal_hist.
     * Thus, return the value from ahpriv->nf_cal_hist if there's
     * no scan, and if the specified channel is the current channel.
     * Otherwise, return the noise floor from ichan->nf_cal_hist.
     */
    if ((!is_scan) && chan == AH_PRIVATE(ah)->ah_curchan) {
        h = &AH_PRIVATE(ah)->nf_cal_hist;
        nf_hist_len = HAL_NF_CAL_HIST_LEN_FULL;
    } else {
        /* Fill 0 if valid internal channel is not found */
        if (ichan == AH_NULL) {
            OS_MEMZERO(nf_buf, sizeof(nf_buf[0])*HAL_NUM_NF_READINGS);
            return;
        }
       /*
        * It is okay to treat a HAL_NFCAL_HIST_SMALL struct as if it were a
        * HAL_NFCAL_HIST_FULL struct, as long as only the index 0 of the
        * nf_cal_buffer is used (nf_cal_buffer[0][0:HAL_NUM_NF_READINGS-1])
        */
        h = (HAL_NFCAL_HIST_FULL *) &ichan->nf_cal_hist;
        nf_hist_len = HAL_NF_CAL_HIST_LEN_SMALL;
    }
#endif
    /* Get most recently updated values from nf cal history buffer */
    recent_nf_index =
        (h->base.curr_index) ? h->base.curr_index - 1 : nf_hist_len - 1;

    for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
        /* Fill 0 for unsupported chains */
        if (!(rx_chainmask & (1 << i))) {
            nf_buf[i] = 0;
            continue;
        }
        nf_buf[i] = h->nf_cal_buffer[recent_nf_index][i];
    }
}

/*
 * Return the current NF value in register.
 * If the current NF cal is not completed, return 0.
 */
int16_t ar9300_get_nf_from_reg(struct ath_hal *ah, struct ieee80211_channel *chan, int wait_time)
{
    int16_t nfarray[HAL_NUM_NF_READINGS] = {0};
    int is_2g = 0;
    HAL_CHANNEL_INTERNAL *ichan = NULL;

    ichan = ath_hal_checkchannel(ah, chan);
    if (ichan == NULL)
        return (0);

    if (wait_time <= 0) {
        return 0;
    }

    if (!ath_hal_waitfor(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF, 0, wait_time)) {
        ath_hal_printf(ah, "%s: NF cal is not complete in %dus", __func__, wait_time);
        return 0;
    }
    is_2g = !! (IS_CHAN_2GHZ(ichan));
    ar9300_upload_noise_floor(ah, is_2g, nfarray);

    return nfarray[0];
}

/*
 * Pick up the medium one in the noise floor buffer and update the
 * corresponding range for valid noise floor values
 */
static int16_t
ar9300_get_nf_hist_mid(struct ath_hal *ah, HAL_NFCAL_HIST_FULL *h, int reading,
    int hist_len)
{
    int16_t nfval;
    int16_t sort[HAL_NF_CAL_HIST_LEN_FULL]; /* upper bound for hist_len */
    int i, j;


    for (i = 0; i < hist_len; i++) {
        sort[i] = h->nf_cal_buffer[i][reading];
        HALDEBUG(ah, HAL_DEBUG_NFCAL,
            "nf_cal_buffer[%d][%d] = %d\n", i, reading, (int)sort[i]);
    }
    for (i = 0; i < hist_len - 1; i++) {
        for (j = 1; j < hist_len - i; j++) {
            if (sort[j] > sort[j - 1]) {
                nfval = sort[j];
                sort[j] = sort[j - 1];
                sort[j - 1] = nfval;
            }
        }
    }
    nfval = sort[(hist_len - 1) >> 1];

    return nfval;
}

static int16_t ar9300_limit_nf_range(struct ath_hal *ah, int16_t nf)
{
    if (nf < AH9300(ah)->nfp->min) {
        return AH9300(ah)->nfp->nominal;
    } else if (nf > AH9300(ah)->nfp->max) {
        return AH9300(ah)->nfp->max;
    }
    return nf;
}

#ifndef ATH_NF_PER_CHAN
inline static void
ar9300_reset_nf_hist_buff(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan)
{
    HAL_CHAN_NFCAL_HIST *h = &ichan->nf_cal_hist;
    HAL_NFCAL_HIST_FULL *home = &AH_PRIVATE(ah)->nf_cal_hist;
    int i;
    
    /* 
     * Copy the value for the channel in question into the home-channel
     * NF history buffer.  The channel NF is probably a value filled in by
     * a prior background channel scan, but if no scan has been done then
     * it is the nominal noise floor filled in by ath_hal_init_NF_buffer
     * for this chip and the channel's band.
     * Replicate this channel NF into all entries of the home-channel NF
     * history buffer.
     * If the channel NF was filled in by a channel scan, it has not had
     * bounds limits applied to it yet - do so now.  It is important to
     * apply bounds limits to the priv_nf value that gets loaded into the
     * WLAN chip's min_cca_pwr register field.  It is also necessary to
     * apply bounds limits to the nf_cal_buffer[] elements.  Since we are
     * replicating a single NF reading into all nf_cal_buffer elements,
     * if the single reading were above the CW_INT threshold, the CW_INT
     * check in ar9300_get_nf would immediately conclude that CW interference
     * is present, even though we're not supposed to set CW_INT unless
     * NF values are _consistently_ above the CW_INT threshold.
     * Applying the bounds limits to the nf_cal_buffer contents fixes this
     * problem.
     */
    for (i = 0; i < HAL_NUM_NF_READINGS; i ++) {
        int j;
        int16_t nf;
        /*
         * No need to set curr_index, since it already has a value in
         * the range [0..HAL_NF_CAL_HIST_LEN_FULL), and all nf_cal_buffer
         * values will be the same.
         */
        nf = ar9300_limit_nf_range(ah, h->nf_cal_buffer[0][i]);
        for (j = 0; j < HAL_NF_CAL_HIST_LEN_FULL; j++) {
            home->nf_cal_buffer[j][i] = nf;
        }
        AH_PRIVATE(ah)->nf_cal_hist.base.priv_nf[i] = nf;
    }
}
#endif

/*
 *  Update the noise floor buffer as a ring buffer
 */
static int16_t
ar9300_update_nf_hist_buff(struct ath_hal *ah, HAL_NFCAL_HIST_FULL *h,
   int16_t *nfarray, int hist_len)
{
    int i, nr;
    int16_t nf_no_lim_chain0;

    nf_no_lim_chain0 = ar9300_get_nf_hist_mid(ah, h, 0, hist_len);

    HALDEBUG(ah, HAL_DEBUG_NFCAL, "%s[%d] BEFORE\n", __func__, __LINE__);
    for (nr = 0; nr < HAL_NF_CAL_HIST_LEN_FULL; nr++) {
        for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
            HALDEBUG(ah, HAL_DEBUG_NFCAL,
                "nf_cal_buffer[%d][%d] = %d\n",
                nr, i, (int)h->nf_cal_buffer[nr][i]);
        }
    }
    for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
        h->nf_cal_buffer[h->base.curr_index][i] = nfarray[i];
        h->base.priv_nf[i] = ar9300_limit_nf_range(
            ah, ar9300_get_nf_hist_mid(ah, h, i, hist_len));
    }
    HALDEBUG(ah, HAL_DEBUG_NFCAL, "%s[%d] AFTER\n", __func__, __LINE__);
    for (nr = 0; nr < HAL_NF_CAL_HIST_LEN_FULL; nr++) {
        for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
            HALDEBUG(ah, HAL_DEBUG_NFCAL,
                "nf_cal_buffer[%d][%d] = %d\n",
                nr, i, (int)h->nf_cal_buffer[nr][i]);
        }
    }

    if (++h->base.curr_index >= hist_len) {
        h->base.curr_index = 0;
    }

    return nf_no_lim_chain0;
}

#ifdef UNUSED
static HAL_BOOL
get_noise_floor_thresh(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *chan,
    int16_t *nft)
{
    struct ath_hal_9300 *ahp = AH9300(ah);


    switch (chan->channel_flags & CHANNEL_ALL_NOTURBO) {
    case CHANNEL_A:
    case CHANNEL_A_HT20:
    case CHANNEL_A_HT40PLUS:
    case CHANNEL_A_HT40MINUS:
        *nft = (int8_t)ar9300_eeprom_get(ahp, EEP_NFTHRESH_5);
        break;
    case CHANNEL_B:
    case CHANNEL_G:
    case CHANNEL_G_HT20:
    case CHANNEL_G_HT40PLUS:
    case CHANNEL_G_HT40MINUS:
        *nft = (int8_t)ar9300_eeprom_get(ahp, EEP_NFTHRESH_2);
        break;
    default:
        HALDEBUG(ah, HAL_DEBUG_CHANNEL, "%s: invalid channel flags 0x%x\n",
                __func__, chan->channel_flags);
        return AH_FALSE;
    }
    return AH_TRUE;
}
#endif

/*
 * Read the NF and check it against the noise floor threshhold
 */
#define IS(_c, _f)       (((_c)->channel_flags & _f) || 0)
static int
ar9300_store_new_nf(struct ath_hal *ah, struct ieee80211_channel *chan,
  int is_scan)
{
//    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    int nf_hist_len;
    int16_t nf_no_lim;
    int16_t nfarray[HAL_NUM_NF_READINGS] = {0};
    HAL_NFCAL_HIST_FULL *h;
    int is_2g = 0;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
        u_int32_t tsf32, nf_cal_dur_tsf;
        /*
         * The reason the NF calibration did not complete may just be that
         * not enough time has passed since the NF calibration was started,
         * because under certain conditions (when first moving to a new
         * channel) the NF calibration may be checked very repeatedly.
         * Or, there may be CW interference keeping the NF calibration
         * from completing.  Check the delta time between when the NF
         * calibration was started and now to see whether the NF calibration
         * should have already completed (but hasn't, probably due to CW
         * interference), or hasn't had enough time to finish yet.
         */
        /*
         * AH_NF_CAL_DUR_MAX_TSF - A conservative maximum time that the
         *     HW should need to finish a NF calibration.  If the HW
         *     does not complete a NF calibration within this time period,
         *     there must be a problem - probably CW interference.
         * AH_NF_CAL_PERIOD_MAX_TSF - A conservative maximum time between
         *     check of the HW's NF calibration being finished.
         *     If the difference between the current TSF and the TSF
         *     recorded when the NF calibration started is larger than this
         *     value, the TSF must have been reset.
         *     In general, we expect the TSF to only be reset during
         *     regular operation for STAs, not for APs.  However, an
         *     AP's TSF could be reset when joining an IBSS.
         *     There's an outside chance that this could result in the
         *     CW_INT flag being erroneously set, if the TSF adjustment
         *     is smaller than AH_NF_CAL_PERIOD_MAX_TSF but larger than
         *     AH_NF_CAL_DUR_TSF.  However, even if this does happen,
         *     it shouldn't matter, as the IBSS case shouldn't be
         *     concerned about CW_INT.
         */
        /* AH_NF_CAL_DUR_TSF - 90 sec in usec units */
        #define AH_NF_CAL_DUR_TSF (90 * 1000 * 1000)
        /* AH_NF_CAL_PERIOD_MAX_TSF - 180 sec in usec units */
        #define AH_NF_CAL_PERIOD_MAX_TSF (180 * 1000 * 1000)
        /* wraparound handled by using unsigned values */
        tsf32 = ar9300_get_tsf32(ah);
        nf_cal_dur_tsf = tsf32 - AH9300(ah)->nf_tsf32;
        if (nf_cal_dur_tsf > AH_NF_CAL_PERIOD_MAX_TSF) {
            /*
             * The TSF must have gotten reset during the NF cal -
             * just reset the NF TSF timestamp, so the next time
             * this function is called, the timestamp comparison
             * will be valid.
             */
            AH9300(ah)->nf_tsf32 = tsf32;
        } else if (nf_cal_dur_tsf > AH_NF_CAL_DUR_TSF) {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s: NF did not complete in calibration window\n", __func__);
            /* the NF incompletion is probably due to CW interference */
            chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
        }
        return 0; /* HW's NF measurement not finished */
    }
    HALDEBUG(ah, HAL_DEBUG_NFCAL,
        "%s[%d] chan %d\n", __func__, __LINE__, ichan->channel);
    is_2g = !! IS_CHAN_2GHZ(ichan);
    ar9300_upload_noise_floor(ah, is_2g, nfarray);

    /* Update the NF buffer for each chain masked by chainmask */
#ifdef ATH_NF_PER_CHAN
    h = &ichan->nf_cal_hist;
    nf_hist_len = HAL_NF_CAL_HIST_LEN_FULL;
#else
    if (is_scan) {
        /*
         * This channel's NF cal info is just a HAL_NFCAL_HIST_SMALL struct
         * rather than a HAL_NFCAL_HIST_FULL struct.
         * As long as we only use the first history element of nf_cal_buffer
         * (nf_cal_buffer[0][0:HAL_NUM_NF_READINGS-1]), we can use
         * HAL_NFCAL_HIST_SMALL and HAL_NFCAL_HIST_FULL interchangeably.
         */
        h = (HAL_NFCAL_HIST_FULL *) &ichan->nf_cal_hist;
        nf_hist_len = HAL_NF_CAL_HIST_LEN_SMALL;
    } else {
        h = &AH_PRIVATE(ah)->nf_cal_hist;
        nf_hist_len = HAL_NF_CAL_HIST_LEN_FULL;
    }
#endif

    /*
     * nf_no_lim = median value from NF history buffer without bounds limits,
     * priv_nf = median value from NF history buffer with bounds limits.
     */
    nf_no_lim = ar9300_update_nf_hist_buff(ah, h, nfarray, nf_hist_len);
    ichan->rawNoiseFloor = h->base.priv_nf[0];

    /* check if there is interference */
//    ichan->channel_flags &= (~CHANNEL_CW_INT);
    /*
     * Use AR9300_EMULATION to check for emulation purpose as PCIE Device ID
     * 0xABCD is recognized as valid Osprey as WAR in some EVs.
     */
    if (nf_no_lim > ahp->nfp->nominal + ahp->nf_cw_int_delta) {
        /*
         * Since this CW interference check is being applied to the
         * median element of the NF history buffer, this indicates that
         * the CW interference is persistent.  A single high NF reading
         * will not show up in the median, and thus will not cause the
         * CW_INT flag to be set.
         */
        HALDEBUG(ah, HAL_DEBUG_NFCAL,
            "%s: NF Cal: CW interferer detected through NF: %d\n",
            __func__, nf_no_lim); 
        chan->ic_state |= IEEE80211_CHANSTATE_CWINT;
    }
    return 1; /* HW's NF measurement finished */
}
#undef IS

static inline void
ar9300_get_delta_slope_values(struct ath_hal *ah, u_int32_t coef_scaled,
    u_int32_t *coef_mantissa, u_int32_t *coef_exponent)
{
    u_int32_t coef_exp, coef_man;

    /*
     * ALGO -> coef_exp = 14-floor(log2(coef));
     * floor(log2(x)) is the highest set bit position
     */
    for (coef_exp = 31; coef_exp > 0; coef_exp--) {
        if ((coef_scaled >> coef_exp) & 0x1) {
            break;
        }
    }
    /* A coef_exp of 0 is a legal bit position but an unexpected coef_exp */
    HALASSERT(coef_exp);
    coef_exp = 14 - (coef_exp - COEF_SCALE_S);


    /*
     * ALGO -> coef_man = floor(coef* 2^coef_exp+0.5);
     * The coefficient is already shifted up for scaling
     */
    coef_man = coef_scaled + (1 << (COEF_SCALE_S - coef_exp - 1));

    *coef_mantissa = coef_man >> (COEF_SCALE_S - coef_exp);
    *coef_exponent = coef_exp - 16;
}

#define MAX_ANALOG_START        319             /* XXX */

/*
 * Delta slope coefficient computation.
 * Required for OFDM operation.
 */
static void
ar9300_set_delta_slope(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t coef_scaled, ds_coef_exp, ds_coef_man;
    u_int32_t fclk = COEFF; /* clock * 2.5 */

    u_int32_t clock_mhz_scaled = 0x1000000 * fclk;
    CHAN_CENTERS centers;

    /*
     * half and quarter rate can divide the scaled clock by 2 or 4
     * scale for selected channel bandwidth
     */
    if (IEEE80211_IS_CHAN_HALF(chan)) {
        clock_mhz_scaled = clock_mhz_scaled >> 1;
    } else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
        clock_mhz_scaled = clock_mhz_scaled >> 2;
    }

    /*
     * ALGO -> coef = 1e8/fcarrier*fclock/40;
     * scaled coef to provide precision for this floating calculation
     */
    ar9300_get_channel_centers(ah, chan, &centers);
    coef_scaled = clock_mhz_scaled / centers.synth_center;

    ar9300_get_delta_slope_values(ah, coef_scaled, &ds_coef_man, &ds_coef_exp);

    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING3, AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING3, AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);

    /*
     * For Short GI,
     * scaled coeff is 9/10 that of normal coeff
     */
    coef_scaled = (9 * coef_scaled) / 10;

    ar9300_get_delta_slope_values(ah, coef_scaled, &ds_coef_man, &ds_coef_exp);

    /* for short gi */
    OS_REG_RMW_FIELD(ah, AR_PHY_SGI_DELTA, AR_PHY_SGI_DSC_MAN, ds_coef_man);
    OS_REG_RMW_FIELD(ah, AR_PHY_SGI_DELTA, AR_PHY_SGI_DSC_EXP, ds_coef_exp);
}

#define IS(_c, _f)       (IEEE80211_IS_ ## _f(_c))

/*
 * XXX FreeBSD: This should be turned into something generic in ath_hal!
 */
HAL_CHANNEL_INTERNAL *
ar9300_check_chan(struct ath_hal *ah, const struct ieee80211_channel *chan)
{

    if (chan == NULL) {
        return AH_NULL;
    }

    if ((IS(chan, CHAN_2GHZ) ^ IS(chan, CHAN_5GHZ)) == 0) {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; not marked as 2GHz or 5GHz\n",
            __func__, chan->ic_freq , chan->ic_flags);
        return AH_NULL;
    }

    /*
     * FreeBSD sets multiple flags, so this will fail.
     */
#if 0
    if ((IS(chan, CHAN_OFDM) ^ IS(chan, CHAN_CCK) ^ IS(chan, CHAN_DYN) ^
         IS(chan, CHAN_HT20) ^ IS(chan, CHAN_HT40U) ^
         IS(chan, CHAN_HT40D)) == 0)
    {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; not marked as "
            "OFDM or CCK or DYN or HT20 or HT40PLUS or HT40MINUS\n",
            __func__, chan->ic_freq , chan->ic_flags);
        return AH_NULL;
    }
#endif

    return (ath_hal_checkchannel(ah, chan));
}
#undef IS

static void
ar9300_set_11n_regs(struct ath_hal *ah, struct ieee80211_channel *chan,
    HAL_HT_MACMODE macmode)
{
    u_int32_t phymode;
//    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t enable_dac_fifo;

    /* XXX */
    enable_dac_fifo =
        OS_REG_READ(ah, AR_PHY_GEN_CTRL) & AR_PHY_GC_ENABLE_DAC_FIFO;

    /* Enable 11n HT, 20 MHz */
    phymode =
        AR_PHY_GC_HT_EN | AR_PHY_GC_SINGLE_HT_LTF1 | AR_PHY_GC_SHORT_GI_40
        | enable_dac_fifo;
    /* Configure baseband for dynamic 20/40 operation */
    if (IEEE80211_IS_CHAN_HT40(chan)) {
        phymode |= AR_PHY_GC_DYN2040_EN;
        /* Configure control (primary) channel at +-10MHz */
        if (IEEE80211_IS_CHAN_HT40U(chan)) {
            phymode |= AR_PHY_GC_DYN2040_PRI_CH;
        }

#if 0
        /* Configure 20/25 spacing */
        if (ahp->ah_ext_prot_spacing == HAL_HT_EXTPROTSPACING_25) {
            phymode |= AR_PHY_GC_DYN2040_EXT_CH;
        }
#endif
    }

    /* make sure we preserve INI settings */
    phymode |= OS_REG_READ(ah, AR_PHY_GEN_CTRL);

    /* EV 62881/64991 - turn off Green Field detection for Maverick STA beta */
    phymode &= ~AR_PHY_GC_GF_DETECT_EN;

    OS_REG_WRITE(ah, AR_PHY_GEN_CTRL, phymode);

    /* Set IFS timing for half/quarter rates */
    if (IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan)) {
        u_int32_t modeselect = OS_REG_READ(ah, AR_PHY_MODE);

        if (IEEE80211_IS_CHAN_HALF(chan)) {
            modeselect |= AR_PHY_MS_HALF_RATE;
        } else if (IEEE80211_IS_CHAN_QUARTER(chan)) {
            modeselect |= AR_PHY_MS_QUARTER_RATE;
        }
        OS_REG_WRITE(ah, AR_PHY_MODE, modeselect);

        ar9300_set_ifs_timing(ah, chan);
        OS_REG_RMW_FIELD(
            ah, AR_PHY_FRAME_CTL, AR_PHY_FRAME_CTL_CF_OVERLAP_WINDOW, 0x3);
    }

    /* Configure MAC for 20/40 operation */
    ar9300_set_11n_mac2040(ah, macmode);

    /* global transmit timeout (25 TUs default)*/
    /* XXX - put this elsewhere??? */
    OS_REG_WRITE(ah, AR_GTXTO, 25 << AR_GTXTO_TIMEOUT_LIMIT_S);

    /* carrier sense timeout */
    OS_REG_WRITE(ah, AR_CST, 0xF << AR_CST_TIMEOUT_LIMIT_S);
}

/*
 * Spur mitigation for MRC CCK
 */
static void
ar9300_spur_mitigate_mrc_cck(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    int i;
    /* spur_freq_for_osprey - hardcoded by Systems team for now. */
    u_int32_t spur_freq_for_osprey[4] = { 2420, 2440, 2464, 2480 };
    u_int32_t spur_freq_for_jupiter[2] = { 2440, 2464};
    int cur_bb_spur, negative = 0, cck_spur_freq;
    u_int8_t* spur_fbin_ptr = NULL;
    int synth_freq;
    int range = 10;
    int max_spurcounts = OSPREY_EEPROM_MODAL_SPURS; 
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    /*
     * Need to verify range +/- 10 MHz in control channel, otherwise spur
     * is out-of-band and can be ignored.
     */
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) ||
        AR_SREV_WASP(ah)  || AR_SREV_SCORPION(ah)) {
        spur_fbin_ptr = ar9300_eeprom_get_spur_chans_ptr(ah, 1);
        if (spur_fbin_ptr[0] == 0) {
            return;      /* No spur in the mode */
        }
        if (IEEE80211_IS_CHAN_HT40(chan)) {
            range = 19;
            if (OS_REG_READ_FIELD(ah, AR_PHY_GEN_CTRL, AR_PHY_GC_DYN2040_PRI_CH)
                == 0x0)
            {
                synth_freq = ichan->channel + 10;
            } else {
                synth_freq = ichan->channel - 10;
            }
        } else {
            range = 10;
            synth_freq = ichan->channel;
        }
    } else if(AR_SREV_JUPITER(ah)) {
        range = 5;
        max_spurcounts = 2; /* Hardcoded by Jupiter Systems team for now. */
        synth_freq = ichan->channel;
    } else {
        range = 10;
        max_spurcounts = 4; /* Hardcoded by Osprey Systems team for now. */
        synth_freq = ichan->channel;
    }

    for (i = 0; i < max_spurcounts; i++) {
        negative = 0;

        if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) ||
            AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
            cur_bb_spur = 
                FBIN2FREQ(spur_fbin_ptr[i], HAL_FREQ_BAND_2GHZ) - synth_freq;
        } else if(AR_SREV_JUPITER(ah)) {
            cur_bb_spur = spur_freq_for_jupiter[i] - synth_freq;
        } else {
            cur_bb_spur = spur_freq_for_osprey[i] - synth_freq;
        }
        
        if (cur_bb_spur < 0) {
            negative = 1;
            cur_bb_spur = -cur_bb_spur;
        }
        if (cur_bb_spur < range) {
            cck_spur_freq = (int)((cur_bb_spur << 19) / 11);
            if (negative == 1) {
                cck_spur_freq = -cck_spur_freq;
            }
            cck_spur_freq = cck_spur_freq & 0xfffff;
            /*OS_REG_WRITE_field(ah, BB_agc_control.ycok_max, 0x7);*/
            OS_REG_RMW_FIELD(ah,
                AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_YCOK_MAX, 0x7);
            /*OS_REG_WRITE_field(ah, BB_cck_spur_mit.spur_rssi_thr, 0x7f);*/
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_SPUR_RSSI_THR, 0x7f);
            /*OS_REG_WRITE(ah, BB_cck_spur_mit.spur_filter_type, 0x2);*/
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_SPUR_FILTER_TYPE, 0x2);
            /*OS_REG_WRITE(ah, BB_cck_spur_mit.use_cck_spur_mit, 0x1);*/
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT, 0x1);
            /*OS_REG_WRITE(ah, BB_cck_spur_mit.cck_spur_freq, cck_spur_freq);*/
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ,
                cck_spur_freq);
            return; 
        }
    }

    /*OS_REG_WRITE(ah, BB_agc_control.ycok_max, 0x5);*/
    OS_REG_RMW_FIELD(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_YCOK_MAX, 0x5);
    /*OS_REG_WRITE(ah, BB_cck_spur_mit.use_cck_spur_mit, 0x0);*/
    OS_REG_RMW_FIELD(ah,
        AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT, 0x0);
    /*OS_REG_WRITE(ah, BB_cck_spur_mit.cck_spur_freq, 0x0);*/
    OS_REG_RMW_FIELD(ah,
        AR_PHY_CCK_SPUR_MIT, AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ, 0x0);
}

/* Spur mitigation for OFDM */
static void
ar9300_spur_mitigate_ofdm(struct ath_hal *ah, struct ieee80211_channel *chan)
{ 
    int synth_freq;
    int range = 10;
    int freq_offset = 0;
    int spur_freq_sd = 0;
    int spur_subchannel_sd = 0;
    int spur_delta_phase = 0;
    int mask_index = 0;
    int i;
    int mode;
    u_int8_t* spur_chans_ptr;
    struct ath_hal_9300 *ahp;
    ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    if (IS_CHAN_5GHZ(ichan)) {
        spur_chans_ptr = ar9300_eeprom_get_spur_chans_ptr(ah, 0);
        mode = 0;
    } else {
        spur_chans_ptr = ar9300_eeprom_get_spur_chans_ptr(ah, 1);
        mode = 1;
    }

    if (IEEE80211_IS_CHAN_HT40(chan)) {
        range = 19;
        if (OS_REG_READ_FIELD(ah, AR_PHY_GEN_CTRL, AR_PHY_GC_DYN2040_PRI_CH)
            == 0x0)
        {
            synth_freq = ichan->channel - 10;
        } else {
            synth_freq = ichan->channel + 10;
        }
    } else {
        range = 10;
        synth_freq = ichan->channel;
    }

    /* Clean all spur register fields */
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_FILTER, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING11, AR_PHY_TIMING11_SPUR_FREQ_SD, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING11, AR_PHY_TIMING11_SPUR_DELTA_PHASE, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_SFCORR_EXT, AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_TIMING11, AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_TIMING11, AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_RSSI, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_ENABLE_MASK_PPM, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_PILOT_MASK, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_CHAN_MASK, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_PILOT_SPUR_MASK, AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_SPUR_MASK_A, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_CHAN_SPUR_MASK, AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_PILOT_SPUR_MASK, AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_CHAN_SPUR_MASK, AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A, 0);
    OS_REG_RMW_FIELD(ah,
        AR_PHY_SPUR_MASK_A, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0);
    OS_REG_RMW_FIELD(ah, AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0);

    i = 0;
    while (spur_chans_ptr[i] && i < 5) {
        freq_offset = FBIN2FREQ(spur_chans_ptr[i], mode) - synth_freq;
        if (abs(freq_offset) < range) {
            /*
            printf(
                "Spur Mitigation for OFDM: Synth Frequency = %d, "
                "Spur Frequency = %d\n",
                synth_freq, FBIN2FREQ(spur_chans_ptr[i], mode));
             */
            if (IEEE80211_IS_CHAN_HT40(chan)) {
                if (freq_offset < 0) {
                    if (OS_REG_READ_FIELD(
                        ah, AR_PHY_GEN_CTRL, AR_PHY_GC_DYN2040_PRI_CH) == 0x0)
                    {
                        spur_subchannel_sd = 1;
                    } else {
                        spur_subchannel_sd = 0;
                    }
                    spur_freq_sd = ((freq_offset + 10) << 9) / 11;
                } else {
                    if (OS_REG_READ_FIELD(ah,
                        AR_PHY_GEN_CTRL, AR_PHY_GC_DYN2040_PRI_CH) == 0x0)
                    {
                        spur_subchannel_sd = 0;
                    } else {
                        spur_subchannel_sd = 1;
                    }
                    spur_freq_sd = ((freq_offset - 10) << 9) / 11;
                }
                spur_delta_phase = (freq_offset << 17) / 5;
            } else {
                spur_subchannel_sd = 0;
                spur_freq_sd = (freq_offset << 9) / 11;
                spur_delta_phase = (freq_offset << 18) / 5;
            }
            spur_freq_sd = spur_freq_sd & 0x3ff;
            spur_delta_phase = spur_delta_phase & 0xfffff;
            /*
            printf(
                "spur_subchannel_sd = %d, spur_freq_sd = 0x%x, "
                "spur_delta_phase = 0x%x\n", spur_subchannel_sd,
                spur_freq_sd, spur_delta_phase);
             */

            /* OFDM Spur mitigation */
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_FILTER, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING11, AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING11, AR_PHY_TIMING11_SPUR_DELTA_PHASE,
                spur_delta_phase);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SFCORR_EXT, AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD,
                spur_subchannel_sd);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING11, AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING11, AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR,
                0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_RSSI, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, 34);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI, 1);

            /*
             * Do not subtract spur power from noise floor for wasp.
             * This causes the maximum client test (on Veriwave) to fail
             * when run on spur channel (2464 MHz).
             * Refer to ev#82746 and ev#82744.
             */
            if (!AR_SREV_WASP(ah) && (OS_REG_READ_FIELD(ah, AR_PHY_MODE,
                                           AR_PHY_MODE_DYNAMIC) == 0x1)) {
                OS_REG_RMW_FIELD(ah, AR_PHY_SPUR_REG,
                    AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT, 1);
            }

            mask_index = (freq_offset << 4) / 5;
            if (mask_index < 0) {
                mask_index = mask_index - 1;
            }
            mask_index = mask_index & 0x7f;
            /*printf("Bin 0x%x\n", mask_index);*/

            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_ENABLE_MASK_PPM, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_PILOT_MASK, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_CHAN_MASK, 0x1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_PILOT_SPUR_MASK,
                AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A, mask_index);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_MASK_A, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A,
                mask_index);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CHAN_SPUR_MASK,
                AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A, mask_index);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_PILOT_SPUR_MASK, AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A,
                0xc);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_CHAN_SPUR_MASK, AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A,
                0xc);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_MASK_A, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0xa0);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_SPUR_REG, AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0xff);
            /*
            printf("BB_timing_control_4 = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_TIMING4));
            printf("BB_timing_control_11 = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_TIMING11));
            printf("BB_ext_chan_scorr_thr = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_SFCORR_EXT));
            printf("BB_spur_mask_controls = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_SPUR_REG));
            printf("BB_pilot_spur_mask = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_PILOT_SPUR_MASK));
            printf("BB_chan_spur_mask = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_CHAN_SPUR_MASK));
            printf("BB_vit_spur_mask_A = 0x%x\n",
                OS_REG_READ(ah, AR_PHY_SPUR_MASK_A));
             */
            break;
        }
        i++;
    }
}


/*
 * Convert to baseband spur frequency given input channel frequency 
 * and compute register settings below.
 */
static void
ar9300_spur_mitigate(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    ar9300_spur_mitigate_ofdm(ah, chan);
    ar9300_spur_mitigate_mrc_cck(ah, chan);
}

/**************************************************************
 * ar9300_channel_change
 * Assumes caller wants to change channel, and not reset.
 */
static inline HAL_BOOL
ar9300_channel_change(struct ath_hal *ah, struct ieee80211_channel *chan,
    HAL_CHANNEL_INTERNAL *ichan, HAL_HT_MACMODE macmode)
{

    u_int32_t synth_delay, qnum;
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* TX must be stopped by now */
    for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
        if (ar9300_num_tx_pending(ah, qnum)) {
            HALDEBUG(ah, HAL_DEBUG_QUEUE,
                "%s: Transmit frames pending on queue %d\n", __func__, qnum);
            HALASSERT(0);
            return AH_FALSE;
        }
    }


    /*
     * Kill last Baseband Rx Frame - Request analog bus grant
     */
    OS_REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
    if (!ath_hal_wait(ah, AR_PHY_RFBUS_GRANT, AR_PHY_RFBUS_GRANT_EN,
            AR_PHY_RFBUS_GRANT_EN))
    {
        HALDEBUG(ah, HAL_DEBUG_PHYIO,
            "%s: Could not kill baseband RX\n", __func__);
        return AH_FALSE;
    }


    /* Setup 11n MAC/Phy mode registers */
    ar9300_set_11n_regs(ah, chan, macmode);

    /*
     * Change the synth
     */
    if (!ahp->ah_rf_hal.set_channel(ah, chan)) {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL, "%s: failed to set channel\n", __func__);
        return AH_FALSE;
    }

    /* 
     * Some registers get reinitialized during ATH_INI_POST INI programming. 
     */
    ar9300_init_user_settings(ah);

    /*
     * Setup the transmit power values.
     *
     * After the public to private hal channel mapping, ichan contains the
     * valid regulatory power value.
     * ath_hal_getctl and ath_hal_getantennaallowed look up ichan from chan.
     */
    if (ar9300_eeprom_set_transmit_power(
         ah, &ahp->ah_eeprom, chan, ath_hal_getctl(ah, chan),
         ath_hal_getantennaallowed(ah, chan),
         ath_hal_get_twice_max_regpower(AH_PRIVATE(ah), ichan, chan),
         AH_MIN(MAX_RATE_POWER, AH_PRIVATE(ah)->ah_powerLimit)) != HAL_OK)
    {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: error init'ing transmit power\n", __func__);
        return AH_FALSE;
    }

    /*
     * Release the RFBus Grant.
     */
    OS_REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);

    /*
     * Write spur immunity and delta slope for OFDM enabled modes (A, G, Turbo)
     */
    if (IEEE80211_IS_CHAN_OFDM(chan) || IEEE80211_IS_CHAN_HT(chan)) {
        ar9300_set_delta_slope(ah, chan);
    } else {
        /* Set to Ini default */
        OS_REG_WRITE(ah, AR_PHY_TIMING3, 0x9c0a9f6b);
        OS_REG_WRITE(ah, AR_PHY_SGI_DELTA, 0x00046384);
    }

    ar9300_spur_mitigate(ah, chan);


    /*
     * Wait for the frequency synth to settle (synth goes on via PHY_ACTIVE_EN).
     * Read the phy active delay register. Value is in 100ns increments.
     */
    synth_delay = OS_REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
    if (IEEE80211_IS_CHAN_CCK(chan)) {
        synth_delay = (4 * synth_delay) / 22;
    } else {
        synth_delay /= 10;
    }

    OS_DELAY(synth_delay + BASE_ACTIVATE_DELAY);

    /*
     * Do calibration.
     */

    return AH_TRUE;
}

void
ar9300_set_operating_mode(struct ath_hal *ah, int opmode)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_STA_ID1);
    val &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC);
    switch (opmode) {
    case HAL_M_HOSTAP:
        OS_REG_WRITE(ah, AR_STA_ID1,
            val | AR_STA_ID1_STA_AP | AR_STA_ID1_KSRCH_MODE);
        OS_REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
        break;
    case HAL_M_IBSS:
        OS_REG_WRITE(ah, AR_STA_ID1,
            val | AR_STA_ID1_ADHOC | AR_STA_ID1_KSRCH_MODE);
        OS_REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
        break;
    case HAL_M_STA:
    case HAL_M_MONITOR:
        OS_REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_KSRCH_MODE);
        break;
    }
}

/* XXX need the logic for Osprey */
void
ar9300_init_pll(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t pll;
    u_int8_t clk_25mhz = AH9300(ah)->clk_25mhz;
    HAL_CHANNEL_INTERNAL *ichan = NULL;

    if (chan)
        ichan = ath_hal_checkchannel(ah, chan);

    if (AR_SREV_HORNET(ah)) {
        if (clk_25mhz) {
            /* Hornet uses PLL_CONTROL_2. Xtal is 25MHz for Hornet.
             * REFDIV set to 0x1.
             * $xtal_freq = 25;
             * $PLL2_div = (704/$xtal_freq); # 176 * 4 = 704.
             * MAC and BB run at 176 MHz.
             * $PLL2_divint = int($PLL2_div);
             * $PLL2_divfrac = $PLL2_div - $PLL2_divint;
             * $PLL2_divfrac = int($PLL2_divfrac * 0x4000); # 2^14
             * $PLL2_Val = ($PLL2_divint & 0x3f) << 19 | (0x1) << 14 |
             *     $PLL2_divfrac & 0x3fff;
             * Therefore, $PLL2_Val = 0xe04a3d
             */
#define DPLL2_KD_VAL            0x1D
#define DPLL2_KI_VAL            0x06
#define DPLL3_PHASE_SHIFT_VAL   0x1

            /* Rewrite DDR PLL2 and PLL3 */
            /* program DDR PLL ki and kd value, ki=0x6, kd=0x1d */
            OS_REG_WRITE(ah, AR_HORNET_CH0_DDR_DPLL2, 0x18e82f01);

            /* program DDR PLL phase_shift to 0x1 */
            OS_REG_RMW_FIELD(ah, AR_HORNET_CH0_DDR_DPLL3,
                AR_PHY_BB_DPLL3_PHASE_SHIFT, DPLL3_PHASE_SHIFT_VAL);

            OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x1142c);
            OS_DELAY(1000);

            /* program refdiv, nint, frac to RTC register */
            OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL2, 0xe04a3d);

            /* program BB PLL ki and kd value, ki=0x6, kd=0x1d */
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2,
                AR_PHY_BB_DPLL2_KD, DPLL2_KD_VAL);
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2,
                AR_PHY_BB_DPLL2_KI, DPLL2_KI_VAL);

            /* program BB PLL phase_shift to 0x1 */
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL3,
                AR_PHY_BB_DPLL3_PHASE_SHIFT, DPLL3_PHASE_SHIFT_VAL);
        } else { /* 40MHz */
#undef  DPLL2_KD_VAL
#undef  DPLL2_KI_VAL
#define DPLL2_KD_VAL            0x3D
#define DPLL2_KI_VAL            0x06
            /* Rewrite DDR PLL2 and PLL3 */
            /* program DDR PLL ki and kd value, ki=0x6, kd=0x3d */
            OS_REG_WRITE(ah, AR_HORNET_CH0_DDR_DPLL2, 0x19e82f01);

            /* program DDR PLL phase_shift to 0x1 */
            OS_REG_RMW_FIELD(ah, AR_HORNET_CH0_DDR_DPLL3,
                AR_PHY_BB_DPLL3_PHASE_SHIFT, DPLL3_PHASE_SHIFT_VAL);

            OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x1142c);
            OS_DELAY(1000);

            /* program refdiv, nint, frac to RTC register */
            OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL2, 0x886666);

            /* program BB PLL ki and kd value, ki=0x6, kd=0x3d */
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2,
                AR_PHY_BB_DPLL2_KD, DPLL2_KD_VAL);
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2,
                AR_PHY_BB_DPLL2_KI, DPLL2_KI_VAL);

            /* program BB PLL phase_shift to 0x1 */
            OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL3,
                AR_PHY_BB_DPLL3_PHASE_SHIFT, DPLL3_PHASE_SHIFT_VAL);
        }
        OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x142c);
        OS_DELAY(1000);
    } else if (AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, AR_PHY_BB_DPLL2_PLL_PWD, 0x1);

        /* program BB PLL ki and kd value, ki=0x4, kd=0x40 */
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_KD, 0x40);
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_KI, 0x4);

        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL1, 
            AR_PHY_BB_DPLL1_REFDIV, 0x5);
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL1, 
            AR_PHY_BB_DPLL1_NINI, 0x58);
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL1, 
            AR_PHY_BB_DPLL1_NFRAC, 0x0);

        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_OUTDIV, 0x1);      
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_LOCAL_PLL, 0x1);      
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_EN_NEGTRIG, 0x1); 

        /* program BB PLL phase_shift to 0x6 */
        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL3, 
            AR_PHY_BB_DPLL3_PHASE_SHIFT, 0x6); 

        OS_REG_RMW_FIELD(ah, AR_PHY_BB_DPLL2, 
            AR_PHY_BB_DPLL2_PLL_PWD, 0x0); 
        OS_DELAY(1000);

        OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x142c);
        OS_DELAY(1000);
    } else if (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah)) {
#define SRIF_PLL 1
        u_int32_t regdata, pll2_divint, pll2_divfrac;

#ifndef SRIF_PLL
	u_int32_t pll2_clkmode;
#endif

#ifdef SRIF_PLL
        u_int32_t refdiv;
#endif
        if (clk_25mhz) {
#ifndef SRIF_PLL
            pll2_divint = 0x1c;
            pll2_divfrac = 0xa3d7;
#else
            if (AR_SREV_HONEYBEE(ah)) {
                pll2_divint = 0x1c;
                pll2_divfrac = 0xa3d2;
                refdiv = 1;
            } else {
                pll2_divint = 0x54;
                pll2_divfrac = 0x1eb85;
                refdiv = 3;
            }
#endif
        } else {
#ifndef SRIF_PLL
            pll2_divint = 0x11;
            pll2_divfrac = 0x26666;
#else
            if (AR_SREV_WASP(ah)) {
                pll2_divint = 88;
                pll2_divfrac = 0;
                refdiv = 5;
            } else {
                pll2_divint = 0x11;
                pll2_divfrac = 0x26666;
                refdiv = 1;
            }
#endif
        }
#ifndef SRIF_PLL
        pll2_clkmode = 0x3d;
#endif
        /* PLL programming through SRIF Local Mode */
        OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x1142c); /* Bypass mode */
        OS_DELAY(1000);
        do {
            regdata = OS_REG_READ(ah, AR_PHY_PLL_MODE);
            if (AR_SREV_HONEYBEE(ah)) {
                regdata = regdata | (0x1 << 22);
            } else {
                regdata = regdata | (0x1 << 16);
            }
            OS_REG_WRITE(ah, AR_PHY_PLL_MODE, regdata); /* PWD_PLL set to 1 */
            OS_DELAY(100);
            /* override int, frac, refdiv */
#ifndef SRIF_PLL
            OS_REG_WRITE(ah, AR_PHY_PLL_CONTROL,
                ((1 << 27) | (pll2_divint << 18) | pll2_divfrac));
#else
            OS_REG_WRITE(ah, AR_PHY_PLL_CONTROL,
                ((refdiv << 27) | (pll2_divint << 18) | pll2_divfrac));
#endif
            OS_DELAY(100);
            regdata = OS_REG_READ(ah, AR_PHY_PLL_MODE);
#ifndef SRIF_PLL
            regdata = (regdata & 0x80071fff) |
                (0x1 << 30) | (0x1 << 13) | (0x6 << 26) | (pll2_clkmode << 19);
#else
            if (AR_SREV_WASP(ah)) {
                regdata = (regdata & 0x80071fff) |
                    (0x1 << 30) | (0x1 << 13) | (0x4 << 26) | (0x18 << 19);
            } else if (AR_SREV_HONEYBEE(ah)) {
                /*
                 * Kd=10, Ki=2, Outdiv=1, Local PLL=0, Phase Shift=4 
                 */
                regdata = (regdata & 0x01c00fff) |
                    (0x1 << 31) | (0x2 << 29) | (0xa << 25) | (0x1 << 19) | (0x6 << 12);
            } else {
                regdata = (regdata & 0x80071fff) |
                    (0x3 << 30) | (0x1 << 13) | (0x4 << 26) | (0x60 << 19);
            }
#endif
            /* Ki, Kd, Local PLL, Outdiv */
            OS_REG_WRITE(ah, AR_PHY_PLL_MODE, regdata);
            regdata = OS_REG_READ(ah, AR_PHY_PLL_MODE);
            if (AR_SREV_HONEYBEE(ah)) {
                regdata = (regdata & 0xffbfffff);
            } else {
                regdata = (regdata & 0xfffeffff);
            }
            OS_REG_WRITE(ah, AR_PHY_PLL_MODE, regdata); /* PWD_PLL set to 0 */
            OS_DELAY(1000);
            if (AR_SREV_WASP(ah)) {
                /* clear do measure */
                regdata = OS_REG_READ(ah, AR_PHY_PLL_BB_DPLL3);
                regdata &= ~(1 << 30);
                OS_REG_WRITE(ah, AR_PHY_PLL_BB_DPLL3, regdata);
                OS_DELAY(100);
            
                /* set do measure */
                regdata = OS_REG_READ(ah, AR_PHY_PLL_BB_DPLL3);
                regdata |= (1 << 30);
                OS_REG_WRITE(ah, AR_PHY_PLL_BB_DPLL3, regdata);
            
                /* wait for measure done */
                do {
                    regdata = OS_REG_READ(ah, AR_PHY_PLL_BB_DPLL4);
                } while ((regdata & (1 << 3)) == 0);
            
                /* clear do measure */
                regdata = OS_REG_READ(ah, AR_PHY_PLL_BB_DPLL3);
                regdata &= ~(1 << 30);
                OS_REG_WRITE(ah, AR_PHY_PLL_BB_DPLL3, regdata);
            
                /* get measure sqsum dvc */
                regdata = (OS_REG_READ(ah, AR_PHY_PLL_BB_DPLL3) & 0x007FFFF8) >> 3;
            } else {
                break;
            }
        } while (regdata >= 0x40000);

        /* Remove from Bypass mode */
        OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, 0x142c);
        OS_DELAY(1000);
    } else {
        pll = SM(0x5, AR_RTC_PLL_REFDIV);
  
        /* Supposedly not needed on Osprey */
#if 0
        if (chan && IS_CHAN_HALF_RATE(chan)) {
            pll |= SM(0x1, AR_RTC_PLL_CLKSEL);
        } else if (chan && IS_CHAN_QUARTER_RATE(chan)) {
            pll |= SM(0x2, AR_RTC_PLL_CLKSEL);
        }
#endif
        if (ichan && IS_CHAN_5GHZ(ichan)) {
            pll |= SM(0x28, AR_RTC_PLL_DIV);
            /* 
             * When doing fast clock, set PLL to 0x142c
             */
            if (IS_5GHZ_FAST_CLOCK_EN(ah, chan)) {
                pll = 0x142c;
            }
        } else {
            pll |= SM(0x2c, AR_RTC_PLL_DIV);
        }

        OS_REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);
    }

    /* TODO:
     * For multi-band owl, switch between bands by reiniting the PLL.
     */
    OS_DELAY(RTC_PLL_SETTLE_DELAY);

    OS_REG_WRITE(ah, AR_RTC_SLEEP_CLK,
        AR_RTC_FORCE_DERIVED_CLK | AR_RTC_PCIE_RST_PWDN_EN);

    /* XXX TODO: honeybee? */
    if (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
        if (clk_25mhz) {
            OS_REG_WRITE(ah,
                AR_RTC_DERIVED_RTC_CLK, (0x17c << 1)); /* 32KHz sleep clk */
            OS_REG_WRITE(ah, AR_SLP32_MODE, 0x0010f3d7);
            OS_REG_WRITE(ah, AR_SLP32_INC, 0x0001e7ae);
        } else {
            OS_REG_WRITE(ah,
                AR_RTC_DERIVED_RTC_CLK, (0x261 << 1)); /* 32KHz sleep clk */
            OS_REG_WRITE(ah, AR_SLP32_MODE, 0x0010f400);
            OS_REG_WRITE(ah, AR_SLP32_INC, 0x0001e800);
        }
        OS_DELAY(100);
    }
}

static inline HAL_BOOL
ar9300_set_reset(struct ath_hal *ah, int type)
{
    u_int32_t rst_flags;
    u_int32_t tmp_reg;
    struct ath_hal_9300 *ahp = AH9300(ah);

    HALASSERT(type == HAL_RESET_WARM || type == HAL_RESET_COLD);

    /*
     * RTC Force wake should be done before resetting the MAC.
     * MDK/ART does it that way.
     */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), AH9300(ah)->ah_wa_reg_val);
    OS_DELAY(10); /* delay to allow AR_WA reg write to kick in */
    OS_REG_WRITE(ah,
        AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

    /* Reset AHB */
    /* Bug26871 */
    tmp_reg = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE));
    if (AR_SREV_WASP(ah)) {
        if (tmp_reg & (AR9340_INTR_SYNC_LOCAL_TIMEOUT)) {
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE), 0);
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), AR_RC_HOSTIF);
        }
    } else {
        if (tmp_reg & (AR9300_INTR_SYNC_LOCAL_TIMEOUT | AR9300_INTR_SYNC_RADM_CPL_TIMEOUT)) {
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE), 0);
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), AR_RC_HOSTIF);
        }
        else {
            /* NO AR_RC_AHB in Osprey */
            /*OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), AR_RC_AHB);*/
        }
    }
    
    rst_flags = AR_RTC_RC_MAC_WARM;
    if (type == HAL_RESET_COLD) {
        rst_flags |= AR_RTC_RC_MAC_COLD;
    }

#ifdef AH_SUPPORT_HORNET
    /* Hornet WAR: trigger SoC to reset WMAC if ...
     * (1) doing cold reset. Ref: EV 69254 
     * (2) beacon pending. Ref: EV 70983
     */
    if (AR_SREV_HORNET(ah) &&
        (ar9300_num_tx_pending(
            ah, AH_PRIVATE(ah)->ah_caps.halTotalQueues - 1) != 0 ||
         type == HAL_RESET_COLD))
    {
        u_int32_t time_out;
#define AR_SOC_RST_RESET         0xB806001C
#define AR_SOC_BOOT_STRAP        0xB80600AC
#define AR_SOC_WLAN_RST          0x00000800 /* WLAN reset */
#define REG_WRITE(_reg, _val)    *((volatile u_int32_t *)(_reg)) = (_val);
#define REG_READ(_reg)           *((volatile u_int32_t *)(_reg))
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Hornet SoC reset WMAC.\n", __func__);

        REG_WRITE(AR_SOC_RST_RESET,
            REG_READ(AR_SOC_RST_RESET) | AR_SOC_WLAN_RST);
        REG_WRITE(AR_SOC_RST_RESET,
            REG_READ(AR_SOC_RST_RESET) & (~AR_SOC_WLAN_RST));

        time_out = 0;

        while (1) {
            tmp_reg = REG_READ(AR_SOC_BOOT_STRAP);
            if ((tmp_reg & 0x10) == 0) {
                break;
            }
            if (time_out > 20) {
                break;
            }
            OS_DELAY(10000);
            time_out++;
        }

        OS_REG_WRITE(ah, AR_RTC_RESET, 1);
#undef REG_READ
#undef REG_WRITE
#undef AR_SOC_WLAN_RST
#undef AR_SOC_RST_RESET
#undef AR_SOC_BOOT_STRAP
    }
#endif /* AH_SUPPORT_HORNET */

#ifdef AH_SUPPORT_SCORPION
    if (AR_SREV_SCORPION(ah)) {
#define DDR_CTL_CONFIG_ADDRESS                                       0xb8000000
#define DDR_CTL_CONFIG_OFFSET                                        0x0108
#define DDR_CTL_CONFIG_CLIENT_ACTIVITY_MSB                           29
#define DDR_CTL_CONFIG_CLIENT_ACTIVITY_LSB                           21
#define DDR_CTL_CONFIG_CLIENT_ACTIVITY_MASK                          0x3fe00000
#define DDR_CTL_CONFIG_CLIENT_ACTIVITY_GET(x)                        (((x) & DDR_CTL_CONFIG_CLIENT_ACTIVITY_MASK) >> DDR_CTL_CONFIG_CLIENT_ACTIVITY_LSB)
#define DDR_CTL_CONFIG_CLIENT_ACTIVITY_SET(x)                        (((x) << DDR_CTL_CONFIG_CLIENT_ACTIVITY_LSB) & DDR_CTL_CONFIG_CLIENT_ACTIVITY_MASK)
#define MAC_DMA_CFG_ADDRESS                                          0xb8100000
#define MAC_DMA_CFG_OFFSET                                           0x0014

#define MAC_DMA_CFG_HALT_REQ_MSB                                     11
#define MAC_DMA_CFG_HALT_REQ_LSB                                     11
#define MAC_DMA_CFG_HALT_REQ_MASK                                    0x00000800
#define MAC_DMA_CFG_HALT_REQ_GET(x)                                  (((x) & MAC_DMA_CFG_HALT_REQ_MASK) >> MAC_DMA_CFG_HALT_REQ_LSB)
#define MAC_DMA_CFG_HALT_REQ_SET(x)                                  (((x) << MAC_DMA_CFG_HALT_REQ_LSB) & MAC_DMA_CFG_HALT_REQ_MASK)
#define MAC_DMA_CFG_HALT_ACK_MSB                                     12
#define MAC_DMA_CFG_HALT_ACK_LSB                                     12
#define MAC_DMA_CFG_HALT_ACK_MASK                                    0x00001000
#define MAC_DMA_CFG_HALT_ACK_GET(x)                                  (((x) & MAC_DMA_CFG_HALT_ACK_MASK) >> MAC_DMA_CFG_HALT_ACK_LSB)
#define MAC_DMA_CFG_HALT_ACK_SET(x)                                  (((x) << MAC_DMA_CFG_HALT_ACK_LSB) & MAC_DMA_CFG_HALT_ACK_MASK)

#define RST_RESET                                                    0xB806001c
#define RTC_RESET                                                    (1<<27)

#define REG_READ(_reg)          *((volatile u_int32_t *)(_reg))
#define REG_WRITE(_reg, _val)   *((volatile u_int32_t *)(_reg)) = (_val);

#define DDR_REG_READ(_ah, _reg) \
	    *((volatile u_int32_t *)( DDR_CTL_CONFIG_ADDRESS + (_reg)))
#define DDR_REG_WRITE(_ah, _reg, _val) \
	    *((volatile u_int32_t *)(DDR_CTL_CONFIG_ADDRESS + (_reg))) = (_val)

	    OS_REG_WRITE(ah,MAC_DMA_CFG_OFFSET, (OS_REG_READ(ah,MAC_DMA_CFG_OFFSET) & ~MAC_DMA_CFG_HALT_REQ_MASK) |
			    MAC_DMA_CFG_HALT_REQ_SET(1));

	    {
		    int count;
            u_int32_t data;

		    count = 0;
		    while (!MAC_DMA_CFG_HALT_ACK_GET(OS_REG_READ(ah, MAC_DMA_CFG_OFFSET) ))
		    {
			    count++;
			    if (count > 10) {
				    ath_hal_printf(ah, "Halt ACK timeout\n");
				    break;
			    }
			    OS_DELAY(10);
		    }

		    data = DDR_REG_READ(ah,DDR_CTL_CONFIG_OFFSET);
		    HALDEBUG(ah, HAL_DEBUG_RESET, "check DDR Activity - HIGH\n");

		    count = 0;
		    while (DDR_CTL_CONFIG_CLIENT_ACTIVITY_GET(data)) {
			    //      AVE_DEBUG(0,"DDR Activity - HIGH\n");
			    HALDEBUG(ah, HAL_DEBUG_RESET, "DDR Activity - HIGH\n");
			    count++;
			    OS_DELAY(10);
			    data = DDR_REG_READ(ah,DDR_CTL_CONFIG_OFFSET);
			    if (count > 10) {
				    ath_hal_printf(ah, "DDR Activity timeout\n");
				    break;
			    }
		    }
	    }


	    {
		    //Force RTC reset
		    REG_WRITE(RST_RESET, (REG_READ(RST_RESET) | RTC_RESET));
		    OS_DELAY(10);
		    REG_WRITE(RST_RESET, (REG_READ(RST_RESET) & ~RTC_RESET));
		    OS_DELAY(10);
		    OS_REG_WRITE(ah, AR_RTC_RESET, 0);
		    OS_DELAY(10);
		    OS_REG_WRITE(ah, AR_RTC_RESET, 1);
		    OS_DELAY(10);
		    HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Scorpion SoC RTC reset done.\n", __func__);
	    }
#undef REG_READ
#undef REG_WRITE
    }
#endif  /* AH_SUPPORT_SCORPION */

    /*
     * Set Mac(BB,Phy) Warm Reset
     */
    OS_REG_WRITE(ah, AR_RTC_RC, rst_flags);

    OS_DELAY(50); /* XXX 50 usec */

    /*
     * Clear resets and force wakeup
     */
    OS_REG_WRITE(ah, AR_RTC_RC, 0);
    if (!ath_hal_wait(ah, AR_RTC_RC, AR_RTC_RC_M, 0)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s: RTC stuck in MAC reset\n", __FUNCTION__);
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s: AR_RTC_RC = 0x%x\n", __func__, OS_REG_READ(ah, AR_RTC_RC));
        return AH_FALSE;
    }

    /* Clear AHB reset */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_RC), 0);
    ar9300_disable_pll_lock_detect(ah);

    ar9300_attach_hw_platform(ah);

    ahp->ah_chip_reset_done = 1;
    return AH_TRUE;
}

static inline HAL_BOOL
ar9300_set_reset_power_on(struct ath_hal *ah)
{
    /* Force wake */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), AH9300(ah)->ah_wa_reg_val);
    OS_DELAY(10); /* delay to allow AR_WA reg write to kick in */
    OS_REG_WRITE(ah, AR_RTC_FORCE_WAKE,
        AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    /*
     * RTC reset and clear. Some delay in between is needed 
     * to give the chip time to settle.
     */
    OS_REG_WRITE(ah, AR_RTC_RESET, 0);
    OS_DELAY(2);
    OS_REG_WRITE(ah, AR_RTC_RESET, 1);

    /*
     * Poll till RTC is ON
     */
    if (!ath_hal_wait(ah,
             AR_RTC_STATUS, AR_RTC_STATUS_M,
             AR_RTC_STATUS_ON))
    {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s: RTC not waking up for %d\n", __FUNCTION__, 1000);
        return AH_FALSE;
    }

    /*
     * Read Revisions from Chip right after RTC is on for the first time.
     * This helps us detect the chip type early and initialize it accordingly.
     */
    ar9300_read_revisions(ah);

    /*
     * Warm reset if we aren't really powering on,
     * just restarting the driver.
     */
    return ar9300_set_reset(ah, HAL_RESET_WARM);
}

/*
 * Write the given reset bit mask into the reset register
 */
HAL_BOOL
ar9300_set_reset_reg(struct ath_hal *ah, u_int32_t type)
{
    HAL_BOOL ret = AH_FALSE;

    /*
     * Set force wake
     */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), AH9300(ah)->ah_wa_reg_val);
    OS_DELAY(10); /* delay to allow AR_WA reg write to kick in */
    OS_REG_WRITE(ah, AR_RTC_FORCE_WAKE,
        AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

    switch (type) {
    case HAL_RESET_POWER_ON:
        ret = ar9300_set_reset_power_on(ah);
        break;
    case HAL_RESET_WARM:
    case HAL_RESET_COLD:
        ret = ar9300_set_reset(ah, type);
        break;
    default:
        break;
    }
    
#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
        OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
    }
#endif

    return ret;
}

/*
 * Places the PHY and Radio chips into reset.  A full reset
 * must be called to leave this state.  The PCI/MAC/PCU are
 * not placed into reset as we must receive interrupt to
 * re-enable the hardware.
 */
HAL_BOOL
ar9300_phy_disable(struct ath_hal *ah)
{
    if (!ar9300_set_reset_reg(ah, HAL_RESET_WARM)) {
        return AH_FALSE;
    }

#ifdef ATH_SUPPORT_LED
#define REG_READ(_reg)          *((volatile u_int32_t *)(_reg))
#define REG_WRITE(_reg, _val)   *((volatile u_int32_t *)(_reg)) = (_val);
#define ATH_GPIO_OE             0xB8040000
#define ATH_GPIO_OUT            0xB8040008 /* GPIO Ouput Value reg.*/
    if (AR_SREV_WASP(ah)) {
        if (IS_CHAN_2GHZ((AH_PRIVATE(ah)->ah_curchan))) {
            REG_WRITE(ATH_GPIO_OE, (REG_READ(ATH_GPIO_OE) | (0x1 << 13)));
        }
        else { 
            REG_WRITE(ATH_GPIO_OE, (REG_READ(ATH_GPIO_OE) | (0x1 << 12)));
        }
    }
    else if (AR_SREV_SCORPION(ah)) {
        if (IS_CHAN_2GHZ((AH_PRIVATE(ah)->ah_curchan))) {
            REG_WRITE(ATH_GPIO_OE, (REG_READ(ATH_GPIO_OE) | (0x1 << 13)));
        }
        else {
            REG_WRITE(ATH_GPIO_OE, (REG_READ(ATH_GPIO_OE) | (0x1 << 12)));
        }
        /* Turn off JMPST led */
        REG_WRITE(ATH_GPIO_OUT, (REG_READ(ATH_GPIO_OUT) | (0x1 << 15)));
    }
    else if (AR_SREV_HONEYBEE(ah)) {
        REG_WRITE(ATH_GPIO_OE, (REG_READ(ATH_GPIO_OE) | (0x1 << 12)));
    }
#undef REG_READ
#undef REG_WRITE
#endif

    if ( AR_SREV_OSPREY(ah) ) { 
        OS_REG_RMW(ah, AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX1), 0x0, 0x1f);
    }


    ar9300_init_pll(ah, AH_NULL);
    ar9300_disable_pll_lock_detect(ah);

    return AH_TRUE;
}

/*
 * Places all of hardware into reset
 */
HAL_BOOL
ar9300_disable(struct ath_hal *ah)
{
    if (!ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE)) {
        return AH_FALSE;
    }
    if (!ar9300_set_reset_reg(ah, HAL_RESET_COLD)) {
        return AH_FALSE;
    }

    ar9300_init_pll(ah, AH_NULL);

    return AH_TRUE;
}

/*
 * TODO: Only write the PLL if we're changing to or from CCK mode
 *
 * WARNING: The order of the PLL and mode registers must be correct.
 */
static inline void
ar9300_set_rf_mode(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t rf_mode = 0;

    if (chan == AH_NULL) {
        return;
    }
    switch (AH9300(ah)->ah_hwp) {
    case HAL_TRUE_CHIP:
        rf_mode |= (IEEE80211_IS_CHAN_B(chan) || IEEE80211_IS_CHAN_G(chan)) ?
            AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;
        break;
    default:
        HALASSERT(0);
        break;
    }
    /*  Phy mode bits for 5GHz channels requiring Fast Clock */
    if ( IS_5GHZ_FAST_CLOCK_EN(ah, chan)) {
        rf_mode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);
    }
    OS_REG_WRITE(ah, AR_PHY_MODE, rf_mode);
}

/*
 * Places the hardware into reset and then pulls it out of reset
 */
HAL_BOOL
ar9300_chip_reset(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    struct ath_hal_9300     *ahp = AH9300(ah);
    int type = HAL_RESET_WARM;

    OS_MARK(ah, AH_MARK_CHIPRESET, chan ? chan->ic_freq : 0);

    /*
     * Warm reset is optimistic.
     *
     * If the TX/RX DMA engines aren't shut down (eg, they're
     * wedged) then we're better off doing a full cold reset
     * to try and shake that condition.
     */
    if (ahp->ah_chip_full_sleep ||
        (ah->ah_config.ah_force_full_reset == 1) ||
        OS_REG_READ(ah, AR_Q_TXE) ||
        (OS_REG_READ(ah, AR_CR) & AR_CR_RXE)) {
            type = HAL_RESET_COLD;
    }

    if (!ar9300_set_reset_reg(ah, type)) {
        return AH_FALSE;
    }

    /* Bring out of sleep mode (AGAIN) */
    if (!ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE)) {
        return AH_FALSE;
    }

    ahp->ah_chip_full_sleep = AH_FALSE;

    if (AR_SREV_HORNET(ah)) {
        ar9300_internal_regulator_apply(ah);
    }

    ar9300_init_pll(ah, chan);

    /*
     * Perform warm reset before the mode/PLL/turbo registers
     * are changed in order to deactivate the radio.  Mode changes
     * with an active radio can result in corrupted shifts to the
     * radio device.
     */
    ar9300_set_rf_mode(ah, chan);

    return AH_TRUE;
}

/* ar9300_setup_calibration
 * Setup HW to collect samples used for current cal
 */
inline static void
ar9300_setup_calibration(struct ath_hal *ah, HAL_CAL_LIST *curr_cal)
{
    /* Select calibration to run */
    switch (curr_cal->cal_data->cal_type) {
    case IQ_MISMATCH_CAL:
        /* Start calibration w/ 2^(INIT_IQCAL_LOG_COUNT_MAX+1) samples */
        OS_REG_RMW_FIELD(ah, AR_PHY_TIMING4,
            AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX,
            curr_cal->cal_data->cal_count_max);
        OS_REG_WRITE(ah, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);

        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: starting IQ Mismatch Calibration\n", __func__);

        /* Kick-off cal */
        OS_REG_SET_BIT(ah, AR_PHY_TIMING4, AR_PHY_TIMING4_DO_CAL);

        break;
    case TEMP_COMP_CAL:
        if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) ||
            AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_HORNET_CH0_THERM, AR_PHY_65NM_CH0_THERM_LOCAL, 1);
            OS_REG_RMW_FIELD(ah,
                AR_HORNET_CH0_THERM, AR_PHY_65NM_CH0_THERM_START, 1);
        } else if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_THERM_JUPITER, AR_PHY_65NM_CH0_THERM_LOCAL, 1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_THERM_JUPITER, AR_PHY_65NM_CH0_THERM_START, 1);
        } else {
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_THERM, AR_PHY_65NM_CH0_THERM_LOCAL, 1);
            OS_REG_RMW_FIELD(ah,
                AR_PHY_65NM_CH0_THERM, AR_PHY_65NM_CH0_THERM_START, 1);
        }

        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: starting Temperature Compensation Calibration\n", __func__);
        break;
    default:
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s called with incorrect calibration type.\n", __func__);
    }
}

/* ar9300_reset_calibration
 * Initialize shared data structures and prepare a cal to be run.
 */
inline static void
ar9300_reset_calibration(struct ath_hal *ah, HAL_CAL_LIST *curr_cal)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i;

    /* Setup HW for new calibration */
    ar9300_setup_calibration(ah, curr_cal);

    /* Change SW state to RUNNING for this calibration */
    curr_cal->cal_state = CAL_RUNNING;

    /* Reset data structures shared between different calibrations */
    for (i = 0; i < AR9300_MAX_CHAINS; i++) {
        ahp->ah_meas0.sign[i] = 0;
        ahp->ah_meas1.sign[i] = 0;
        ahp->ah_meas2.sign[i] = 0;
        ahp->ah_meas3.sign[i] = 0;
    }

    ahp->ah_cal_samples = 0;
}

#ifdef XXX_UNUSED_FUNCTION
/*
 * Find out which of the RX chains are enabled
 */
static u_int32_t
ar9300_get_rx_chain_mask(struct ath_hal *ah)
{
    u_int32_t ret_val = OS_REG_READ(ah, AR_PHY_RX_CHAINMASK);
    /* The bits [2:0] indicate the rx chain mask and are to be
     * interpreted as follows:
     * 00x => Only chain 0 is enabled
     * 01x => Chain 1 and 0 enabled
     * 1xx => Chain 2,1 and 0 enabled
     */
    return (ret_val & 0x7);
}
#endif

static void 
ar9300_get_nf_hist_base(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *chan,
    int is_scan, int16_t nf[])
{
    HAL_NFCAL_BASE *h_base;

#ifdef ATH_NF_PER_CHAN
    h_base = &chan->nf_cal_hist.base;
#else
    if (is_scan) {
        /*
         * The channel we are currently on is not the home channel,
         * so we shouldn't use the home channel NF buffer's values on
         * this channel.  Instead, use the NF single value already
         * read for this channel.  (Or, if we haven't read the NF for
         * this channel yet, the SW default for this chip/band will
         * be used.)
         */
        h_base = &chan->nf_cal_hist.base;
    } else {
        /* use the home channel NF info */
        h_base = &AH_PRIVATE(ah)->nf_cal_hist.base;
    }
#endif
    OS_MEMCPY(nf, h_base->priv_nf, sizeof(h_base->priv_nf));
}

HAL_BOOL
ar9300_load_nf(struct ath_hal *ah, int16_t nf[])
{
    int i, j;
    int32_t val;
    /* XXX where are EXT regs defined */
    const u_int32_t ar9300_cca_regs[] = {
        AR_PHY_CCA_0,
        AR_PHY_CCA_1,
        AR_PHY_CCA_2,
        AR_PHY_EXT_CCA,
        AR_PHY_EXT_CCA_1,
        AR_PHY_EXT_CCA_2,
    };
    u_int8_t chainmask;

    /*
     * Force NF calibration for all chains, otherwise Vista station
     * would conduct a bad performance
     */
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        chainmask = 0x9;
    } else if (AR_SREV_WASP(ah) || AR_SREV_JUPITER(ah) || AR_SREV_HONEYBEE(ah)) {
        chainmask = 0x1b;
    } else {
        chainmask = 0x3F;
    }

    /*
     * Write filtered NF values into max_cca_pwr register parameter
     * so we can load below.
     */
    for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
        if (chainmask & (1 << i)) {
            val = OS_REG_READ(ah, ar9300_cca_regs[i]);
            val &= 0xFFFFFE00;
            val |= (((u_int32_t)(nf[i]) << 1) & 0x1ff);
            OS_REG_WRITE(ah, ar9300_cca_regs[i], val);
        }
    }

    HALDEBUG(ah, HAL_DEBUG_NFCAL, "%s: load %d %d %d %d %d %d\n",
      __func__,
      nf[0], nf[1], nf[2],
      nf[3], nf[4], nf[5]);

    /*
     * Load software filtered NF value into baseband internal min_cca_pwr
     * variable.
     */
    OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
    OS_REG_CLR_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
    OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);

    /* Wait for load to complete, should be fast, a few 10s of us. */
    /* Changed the max delay 250us back to 10000us, since 250us often
     * results in NF load timeout and causes deaf condition
     * during stress testing 12/12/2009
     */
    for (j = 0; j < 10000; j++) {
        if ((OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0){
            break;
        }
        OS_DELAY(10);
    }
    if (j == 10000) {
        /*
         * We timed out waiting for the noisefloor to load, probably
         * due to an in-progress rx.  Simply return here and allow
         * the load plenty of time to complete before the next 
         * calibration interval.  We need to avoid trying to load -50
         * (which happens below) while the previous load is still in
         * progress as this can cause rx deafness (see EV 66368,62830).
         * Instead by returning here, the baseband nf cal will 
         * just be capped by our present noisefloor until the next
         * calibration timer.
         */
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
            "%s: *** TIMEOUT while waiting for nf to load: "
            "AR_PHY_AGC_CONTROL=0x%x ***\n", 
            __func__, OS_REG_READ(ah, AR_PHY_AGC_CONTROL));
        return AH_FALSE;
    }

    /*
     * Restore max_cca_power register parameter again so that we're not capped
     * by the median we just loaded.  This will be initial (and max) value
     * of next noise floor calibration the baseband does.
     */
    for (i = 0; i < HAL_NUM_NF_READINGS; i++) {
        if (chainmask & (1 << i)) {
            val = OS_REG_READ(ah, ar9300_cca_regs[i]);
            val &= 0xFFFFFE00;
            val |= (((u_int32_t)(-50) << 1) & 0x1ff);
            OS_REG_WRITE(ah, ar9300_cca_regs[i], val);
        }
    }
    return AH_TRUE;
}

/* ar9300_per_calibration
 * Generic calibration routine.
 * Recalibrate the lower PHY chips to account for temperature/environment
 * changes.
 */
inline static void
ar9300_per_calibration(struct ath_hal *ah,  HAL_CHANNEL_INTERNAL *ichan,
    u_int8_t rxchainmask, HAL_CAL_LIST *curr_cal, HAL_BOOL *is_cal_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* Cal is assumed not done until explicitly set below */
    *is_cal_done = AH_FALSE;

    /* Calibration in progress. */
    if (curr_cal->cal_state == CAL_RUNNING) {
        /* Check to see if it has finished. */
        if (!(OS_REG_READ(ah, AR_PHY_TIMING4) & AR_PHY_TIMING4_DO_CAL)) {
            int i, num_chains = 0;
            for (i = 0; i < AR9300_MAX_CHAINS; i++) {
                if (rxchainmask & (1 << i)) {
                    num_chains++;
                }
            }

            /*
             * Accumulate cal measures for active chains
             */
            curr_cal->cal_data->cal_collect(ah, num_chains);

            ahp->ah_cal_samples++;

            if (ahp->ah_cal_samples >= curr_cal->cal_data->cal_num_samples) {
                /*
                 * Process accumulated data
                 */
                curr_cal->cal_data->cal_post_proc(ah, num_chains);

                /* Calibration has finished. */
                ichan->calValid |= curr_cal->cal_data->cal_type;
                curr_cal->cal_state = CAL_DONE;
                *is_cal_done = AH_TRUE;
            } else {
                /* Set-up collection of another sub-sample until we
                 * get desired number
                 */
                ar9300_setup_calibration(ah, curr_cal);
            }
        }
    } else if (!(ichan->calValid & curr_cal->cal_data->cal_type)) {
        /* If current cal is marked invalid in channel, kick it off */
        ar9300_reset_calibration(ah, curr_cal);
    }
}

static void
ar9300_start_nf_cal(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
    OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
    OS_REG_SET_BIT(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
    AH9300(ah)->nf_tsf32 = ar9300_get_tsf32(ah);

/*  
 *  We are reading the NF values before we start the NF operation, because
 *  of that we are getting very high values like -45.
 *  This triggers the CW_INT detected and EACS module triggers the channel change
 *  chip_reset_done value is used to fix this issue.
 *  chip_reset_flag is set during the RTC reset.
 *  chip_reset_flag is cleared during the starting NF operation.
 *  if flag is set we will clear the flag and will not read the NF values.
 */
    ahp->ah_chip_reset_done = 0;    
}

/* ar9300_calibration
 * Wrapper for a more generic Calibration routine. Primarily to abstract to
 * upper layers whether there is 1 or more calibrations to be run.
 */
HAL_BOOL
ar9300_calibration(struct ath_hal *ah, struct ieee80211_channel *chan, u_int8_t rxchainmask,
    HAL_BOOL do_nf_cal, HAL_BOOL *is_cal_done, int is_scan,
    u_int32_t *sched_cals)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CAL_LIST *curr_cal = ahp->ah_cal_list_curr;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    int16_t nf_buf[HAL_NUM_NF_READINGS];

    *is_cal_done = AH_TRUE;


    /* XXX: For initial wasp bringup - disable periodic calibration */
    /* Invalid channel check */
    if (ichan == AH_NULL) {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; no mapping\n",
            __func__, chan->ic_freq, chan->ic_flags);
        return AH_FALSE;
    }

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: Entering, Doing NF Cal = %d\n", __func__, do_nf_cal);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s: Chain 0 Rx IQ Cal Correction 0x%08x\n",
        __func__, OS_REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B0));
    if (!AR_SREV_HORNET(ah) && !AR_SREV_POSEIDON(ah) && !AR_SREV_APHRODITE(ah)) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Chain 1 Rx IQ Cal Correction 0x%08x\n",
            __func__, OS_REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B1));
        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah) && !AR_SREV_HONEYBEE(ah)) {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s: Chain 2 Rx IQ Cal Correction 0x%08x\n",
                __func__, OS_REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B2));
        }
    }

    OS_MARK(ah, AH_MARK_PERCAL, chan->ic_freq);

    /* For given calibration:
     * 1. Call generic cal routine
     * 2. When this cal is done (is_cal_done) if we have more cals waiting
     *    (eg after reset), mask this to upper layers by not propagating
     *    is_cal_done if it is set to TRUE.
     *    Instead, change is_cal_done to FALSE and setup the waiting cal(s)
     *    to be run.
     */
    if (curr_cal && (curr_cal->cal_data->cal_type & *sched_cals) &&
        (curr_cal->cal_state == CAL_RUNNING ||
         curr_cal->cal_state == CAL_WAITING))
    {
        ar9300_per_calibration(ah, ichan, rxchainmask, curr_cal, is_cal_done);

        if (*is_cal_done == AH_TRUE) {
            ahp->ah_cal_list_curr = curr_cal = curr_cal->cal_next;

            if (curr_cal && curr_cal->cal_state == CAL_WAITING) {
                *is_cal_done = AH_FALSE;
                ar9300_reset_calibration(ah, curr_cal);
            } else {
                *sched_cals &= ~IQ_MISMATCH_CAL;
            }
        }
    }

    /* Do NF cal only at longer intervals */
    if (do_nf_cal) {
        int nf_done;

        /* Get the value from the previous NF cal and update history buffer */
        nf_done = ar9300_store_new_nf(ah, chan, is_scan);
#if 0
        if (ichan->channel_flags & CHANNEL_CW_INT) {
            chan->channel_flags |= CHANNEL_CW_INT;
        }
#endif
        chan->ic_state &= ~IEEE80211_CHANSTATE_CWINT;

        if (nf_done) {
            int ret;
            /*
             * Load the NF from history buffer of the current channel.
             * NF is slow time-variant, so it is OK to use a historical value.
             */
            ar9300_get_nf_hist_base(ah, ichan, is_scan, nf_buf);

            ret = ar9300_load_nf(ah, nf_buf);
            /* start NF calibration, without updating BB NF register*/
            ar9300_start_nf_cal(ah);

            /*
             * If we failed the NF cal then tell the upper layer that we
             * failed so we can do a full reset
             */
            if (! ret)
                return AH_FALSE;
        }
    }
    return AH_TRUE;
}

/* ar9300_iq_cal_collect
 * Collect data from HW to later perform IQ Mismatch Calibration
 */
void
ar9300_iq_cal_collect(struct ath_hal *ah, u_int8_t num_chains)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i;

    /*
     * Accumulate IQ cal measures for active chains
     */
    for (i = 0; i < num_chains; i++) {
        ahp->ah_total_power_meas_i[i] = OS_REG_READ(ah, AR_PHY_CAL_MEAS_0(i));
        ahp->ah_total_power_meas_q[i] = OS_REG_READ(ah, AR_PHY_CAL_MEAS_1(i));
        ahp->ah_total_iq_corr_meas[i] =
            (int32_t) OS_REG_READ(ah, AR_PHY_CAL_MEAS_2(i));
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%d: Chn %d "
            "Reg Offset(0x%04x)pmi=0x%08x; "
            "Reg Offset(0x%04x)pmq=0x%08x; "
            "Reg Offset (0x%04x)iqcm=0x%08x;\n",
            ahp->ah_cal_samples,
            i,
            (unsigned) AR_PHY_CAL_MEAS_0(i),
            ahp->ah_total_power_meas_i[i],
            (unsigned) AR_PHY_CAL_MEAS_1(i),
            ahp->ah_total_power_meas_q[i],
            (unsigned) AR_PHY_CAL_MEAS_2(i),
            ahp->ah_total_iq_corr_meas[i]);
    }
}

/* ar9300_iq_calibration
 * Use HW data to perform IQ Mismatch Calibration
 */
void
ar9300_iq_calibration(struct ath_hal *ah, u_int8_t num_chains)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t power_meas_q, power_meas_i, iq_corr_meas;
    u_int32_t q_coff_denom, i_coff_denom;
    int32_t q_coff, i_coff;
    int iq_corr_neg, i;
    HAL_CHANNEL_INTERNAL *ichan;
    static const u_int32_t offset_array[3] = {
        AR_PHY_RX_IQCAL_CORR_B0,
        AR_PHY_RX_IQCAL_CORR_B1,
        AR_PHY_RX_IQCAL_CORR_B2,
    };

    ichan = ath_hal_checkchannel(ah, AH_PRIVATE(ah)->ah_curchan);

    for (i = 0; i < num_chains; i++) {
        power_meas_i = ahp->ah_total_power_meas_i[i];
        power_meas_q = ahp->ah_total_power_meas_q[i];
        iq_corr_meas = ahp->ah_total_iq_corr_meas[i];

        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "Starting IQ Cal and Correction for Chain %d\n", i);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "Orignal: Chn %diq_corr_meas = 0x%08x\n",
            i, ahp->ah_total_iq_corr_meas[i]);

        iq_corr_neg = 0;

        /* iq_corr_meas is always negative. */
        if (iq_corr_meas > 0x80000000)  {
            iq_corr_meas = (0xffffffff - iq_corr_meas) + 1;
            iq_corr_neg = 1;
        }

        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "Chn %d pwr_meas_i = 0x%08x\n", i, power_meas_i);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "Chn %d pwr_meas_q = 0x%08x\n", i, power_meas_q);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "iq_corr_neg is 0x%08x\n", iq_corr_neg);

        i_coff_denom = (power_meas_i / 2 + power_meas_q / 2) / 256;
        q_coff_denom = power_meas_q / 64;

        /* Protect against divide-by-0 */
        if ((i_coff_denom != 0) && (q_coff_denom != 0)) {
            /* IQ corr_meas is already negated if iqcorr_neg == 1 */
            i_coff = iq_corr_meas / i_coff_denom;
            q_coff = power_meas_i / q_coff_denom - 64;
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Chn %d i_coff = 0x%08x\n", i, i_coff);
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Chn %d q_coff = 0x%08x\n", i, q_coff);

            /* Force bounds on i_coff */
            if (i_coff >= 63) {
                i_coff = 63;
            } else if (i_coff <= -63) {
                i_coff = -63;
            }

            /* Negate i_coff if iq_corr_neg == 0 */
            if (iq_corr_neg == 0x0) {
                i_coff = -i_coff;
            }

            /* Force bounds on q_coff */
            if (q_coff >= 63) {
                q_coff = 63;
            } else if (q_coff <= -63) {
                q_coff = -63;
            }

            i_coff = i_coff & 0x7f;
            q_coff = q_coff & 0x7f;

            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Chn %d : i_coff = 0x%x  q_coff = 0x%x\n", i, i_coff, q_coff);
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Register offset (0x%04x) before update = 0x%x\n",
                offset_array[i], OS_REG_READ(ah, offset_array[i]));

            OS_REG_RMW_FIELD(ah, offset_array[i],
                AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF, i_coff);
            OS_REG_RMW_FIELD(ah, offset_array[i],
                AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF, q_coff);

            /* store the RX cal results */
	    if (ichan != NULL) {
            ahp->ah_rx_cal_corr[i] = OS_REG_READ(ah, offset_array[i]) & 0x7fff;
            ahp->ah_rx_cal_complete = AH_TRUE;
            ahp->ah_rx_cal_chan = ichan->channel;
//            ahp->ah_rx_cal_chan_flag = ichan->channel_flags &~ CHANNEL_PASSIVE; 
            ahp->ah_rx_cal_chan_flag = 0; /* XXX */
	    } else {
	        /* XXX? Is this what I should do? */
            	ahp->ah_rx_cal_complete = AH_FALSE;

	    }

            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Register offset (0x%04x) QI COFF (bitfields 0x%08x) "
                "after update = 0x%x\n",
                offset_array[i], AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF,
                OS_REG_READ(ah, offset_array[i]));
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Register offset (0x%04x) QQ COFF (bitfields 0x%08x) "
                "after update = 0x%x\n",
                offset_array[i], AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF,
                OS_REG_READ(ah, offset_array[i]));
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "IQ Cal and Correction done for Chain %d\n", i);
        }
    }

    OS_REG_SET_BIT(ah,
        AR_PHY_RX_IQCAL_CORR_B0, AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "IQ Cal and Correction (offset 0x%04x) enabled "
        "(bit position 0x%08x). New Value 0x%08x\n",
        (unsigned) (AR_PHY_RX_IQCAL_CORR_B0),
        AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE,
        OS_REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B0));
}

/*
 * When coming back from offchan, we do not perform RX IQ Cal.
 * But the chip reset will clear all previous results
 * We store the previous results and restore here.
 */
static void
ar9300_rx_iq_cal_restore(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t   i_coff, q_coff;
    HAL_BOOL is_restore = AH_FALSE;
    int i;
    static const u_int32_t offset_array[3] = {
        AR_PHY_RX_IQCAL_CORR_B0,
        AR_PHY_RX_IQCAL_CORR_B1,
        AR_PHY_RX_IQCAL_CORR_B2,
    };

    for (i=0; i<AR9300_MAX_CHAINS; i++) {
        if (ahp->ah_rx_cal_corr[i]) {
            i_coff = (ahp->ah_rx_cal_corr[i] &
                        AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF) >>
                        AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF_S;
            q_coff = (ahp->ah_rx_cal_corr[i] &
                        AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF) >>
                        AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF_S;

            OS_REG_RMW_FIELD(ah, offset_array[i],
                AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF, i_coff);
            OS_REG_RMW_FIELD(ah, offset_array[i],
                AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF, q_coff);

            is_restore = AH_TRUE;
        }
    }

    if (is_restore)
        OS_REG_SET_BIT(ah,
            AR_PHY_RX_IQCAL_CORR_B0, AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: IQ Cal and Correction (offset 0x%04x) enabled "
        "(bit position 0x%08x). New Value 0x%08x\n",
        __func__,
        (unsigned) (AR_PHY_RX_IQCAL_CORR_B0),
        AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE,
        OS_REG_READ(ah, AR_PHY_RX_IQCAL_CORR_B0));
}

/*
 * Set a limit on the overall output power.  Used for dynamic
 * transmit power control and the like.
 *
 * NB: limit is in units of 0.5 dbM.
 */
HAL_BOOL
ar9300_set_tx_power_limit(struct ath_hal *ah, u_int32_t limit,
    u_int16_t extra_txpow, u_int16_t tpc_in_db)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    const struct ieee80211_channel *chan = ahpriv->ah_curchan;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    if (NULL == chan) {
        return AH_FALSE;
    }

    ahpriv->ah_powerLimit = AH_MIN(limit, MAX_RATE_POWER);
    ahpriv->ah_extraTxPow = extra_txpow;

    if(chan == NULL) {
        return AH_FALSE;
    }
    if (ar9300_eeprom_set_transmit_power(ah, &ahp->ah_eeprom, chan,
        ath_hal_getctl(ah, chan), ath_hal_getantennaallowed(ah, chan),
        ath_hal_get_twice_max_regpower(ahpriv, ichan, chan),
        AH_MIN(MAX_RATE_POWER, ahpriv->ah_powerLimit)) != HAL_OK)
    {
        return AH_FALSE;
    }
    return AH_TRUE;
}

/*
 * Exported call to check for a recent gain reading and return
 * the current state of the thermal calibration gain engine.
 */
HAL_RFGAIN
ar9300_get_rfgain(struct ath_hal *ah)
{
    return HAL_RFGAIN_INACTIVE;
}

#define HAL_GREEN_AP_RX_MASK 0x1

static inline void
ar9300_init_chain_masks(struct ath_hal *ah, int rx_chainmask, int tx_chainmask)
{
    if (AH9300(ah)->green_ap_ps_on) {
        rx_chainmask = HAL_GREEN_AP_RX_MASK;
    }
    if (rx_chainmask == 0x5) {
        OS_REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP, AR_PHY_SWAP_ALT_CHAIN);
    }
    OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
    OS_REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);

    /*
     * Adaptive Power Management:
     * Some 3 stream chips exceed the PCIe power requirements.
     * This workaround will reduce power consumption by using 2 tx chains
     * for 1 and 2 stream rates (5 GHz only).
     *
     * Set the self gen mask to 2 tx chains when APM is enabled.
     *
     */
    if (AH_PRIVATE(ah)->ah_caps.halApmEnable && (tx_chainmask == 0x7)) {
        OS_REG_WRITE(ah, AR_SELFGEN_MASK, 0x3);
    }
    else {
        OS_REG_WRITE(ah, AR_SELFGEN_MASK, tx_chainmask);
    }

    if (tx_chainmask == 0x5) {
        OS_REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP, AR_PHY_SWAP_ALT_CHAIN);
    }
}

/*
 * Override INI values with chip specific configuration.
 */
static inline void
ar9300_override_ini(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t val;
    HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;

    /*
     * Set the RX_ABORT and RX_DIS and clear it only after
     * RXE is set for MAC. This prevents frames with
     * corrupted descriptor status.
     */
    OS_REG_SET_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));
    /*
     * For Merlin and above, there is a new feature that allows Multicast
     * search based on both MAC Address and Key ID.
     * By default, this feature is enabled.
     * But since the driver is not using this feature, we switch it off;
     * otherwise multicast search based on MAC addr only will fail.
     */
    val = OS_REG_READ(ah, AR_PCU_MISC_MODE2) & (~AR_ADHOC_MCAST_KEYID_ENABLE);
    OS_REG_WRITE(ah, AR_PCU_MISC_MODE2,
        val | AR_BUG_58603_FIX_ENABLE | AR_AGG_WEP_ENABLE);


    /* Osprey revision specific configuration */

    /* Osprey 2.0+ - if SW RAC support is disabled, must also disable
     * the Osprey 2.0 hardware RAC fix.
     */
    if (p_cap->halIsrRacSupport == AH_FALSE) {
        OS_REG_CLR_BIT(ah, AR_CFG, AR_CFG_MISSING_TX_INTR_FIX_ENABLE);
    }

    /* try to enable old pal if it is needed for h/w green tx */
    ar9300_hwgreentx_set_pal_spare(ah, 1);
}

static inline void
ar9300_prog_ini(struct ath_hal *ah, struct ar9300_ini_array *ini_arr,
    int column)
{
    int i, reg_writes = 0;

    /* New INI format: Array may be undefined (pre, core, post arrays) */
    if (ini_arr->ia_array == NULL) {
        return;
    }

    /*
     * New INI format: Pre, core, and post arrays for a given subsystem may be
     * modal (> 2 columns) or non-modal (2 columns).
     * Determine if the array is non-modal and force the column to 1.
     */
    if (column >= ini_arr->ia_columns) {
        column = 1;
    }

    for (i = 0; i < ini_arr->ia_rows; i++) {
        u_int32_t reg = INI_RA(ini_arr, i, 0);
        u_int32_t val = INI_RA(ini_arr, i, column);

        /*
        ** Determine if this is a shift register value 
        ** (reg >= 0x16000 && reg < 0x17000 for Osprey) , 
        ** and insert the configured delay if so. 
        ** -this delay is not required for Osprey (EV#71410)
        */
        OS_REG_WRITE(ah, reg, val);
        WAR_6773(reg_writes);

    }
}

static inline HAL_STATUS
ar9300_process_ini(struct ath_hal *ah, struct ieee80211_channel *chan,
    HAL_CHANNEL_INTERNAL *ichan, HAL_HT_MACMODE macmode)
{
    int reg_writes = 0;
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int modes_index, modes_txgaintable_index = 0;
    int i;
    HAL_STATUS status;
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    /* Setup the indices for the next set of register array writes */
    /* TODO:
     * If the channel marker is indicative of the current mode rather
     * than capability, we do not need to check the phy mode below.
     */
#if 0
    switch (chan->channel_flags & CHANNEL_ALL) {
    case CHANNEL_A:
    case CHANNEL_A_HT20:
        if (AR_SREV_SCORPION(ah)){
            if (chan->channel <= 5350){
                modes_txgaintable_index = 1;
            }else if ((chan->channel > 5350) && (chan->channel <= 5600)){
                modes_txgaintable_index = 3;
            }else if (chan->channel > 5600){
                modes_txgaintable_index = 5;
            }
        }
        modes_index = 1;
        freq_index  = 1;
        break;

    case CHANNEL_A_HT40PLUS:
    case CHANNEL_A_HT40MINUS:
        if (AR_SREV_SCORPION(ah)){
            if (chan->channel <= 5350){
                modes_txgaintable_index = 2;
            }else if ((chan->channel > 5350) && (chan->channel <= 5600)){
                modes_txgaintable_index = 4;
            }else if (chan->channel > 5600){
                modes_txgaintable_index = 6;
            }
        }
        modes_index = 2;
        freq_index  = 1;
        break;

    case CHANNEL_PUREG:
    case CHANNEL_G_HT20:
    case CHANNEL_B:
        if (AR_SREV_SCORPION(ah)){
            modes_txgaintable_index = 8;
        }else if (AR_SREV_HONEYBEE(ah)){
	    modes_txgaintable_index = 1;
	}
        modes_index = 4;
        freq_index  = 2;
        break;

    case CHANNEL_G_HT40PLUS:
    case CHANNEL_G_HT40MINUS:
        if (AR_SREV_SCORPION(ah)){
            modes_txgaintable_index = 7;
        }else if (AR_SREV_HONEYBEE(ah)){
            modes_txgaintable_index = 1;
        }
        modes_index = 3;
        freq_index  = 2;
        break;

    case CHANNEL_108G:
        modes_index = 5;
        freq_index  = 2;
        break;

    default:
        HALASSERT(0);
        return HAL_EINVAL;
    }
#endif

    /* FreeBSD */
    if (IS_CHAN_5GHZ(ichan)) {
        if (IEEE80211_IS_CHAN_HT40U(chan) || IEEE80211_IS_CHAN_HT40D(chan)) {
            if (AR_SREV_SCORPION(ah)){
                if (ichan->channel <= 5350){
                    modes_txgaintable_index = 2;
                }else if ((ichan->channel > 5350) && (ichan->channel <= 5600)){
                    modes_txgaintable_index = 4;
                }else if (ichan->channel > 5600){
                    modes_txgaintable_index = 6;
                }
            }
            modes_index = 2;
        } else if (IEEE80211_IS_CHAN_A(chan) || IEEE80211_IS_CHAN_HT20(chan)) {
            if (AR_SREV_SCORPION(ah)){
                if (ichan->channel <= 5350){
                    modes_txgaintable_index = 1;
                }else if ((ichan->channel > 5350) && (ichan->channel <= 5600)){
                    modes_txgaintable_index = 3;
                }else if (ichan->channel > 5600){
                    modes_txgaintable_index = 5;
                }
            }
            modes_index = 1;
        } else
            return HAL_EINVAL;
    } else if (IS_CHAN_2GHZ(ichan)) {
        if (IEEE80211_IS_CHAN_108G(chan)) {
            modes_index = 5;
        } else if (IEEE80211_IS_CHAN_HT40U(chan) || IEEE80211_IS_CHAN_HT40D(chan)) {
            if (AR_SREV_SCORPION(ah)){
                modes_txgaintable_index = 7;
            } else if (AR_SREV_HONEYBEE(ah)){
                modes_txgaintable_index = 1;
            }
            modes_index = 3;
        } else if (IEEE80211_IS_CHAN_HT20(chan) || IEEE80211_IS_CHAN_G(chan) || IEEE80211_IS_CHAN_B(chan) || IEEE80211_IS_CHAN_PUREG(chan)) {
            if (AR_SREV_SCORPION(ah)){
                modes_txgaintable_index = 8;
            } else if (AR_SREV_HONEYBEE(ah)){
                modes_txgaintable_index = 1;
            }
            modes_index = 4;
        } else
            return HAL_EINVAL;
    } else
            return HAL_EINVAL;

#if 0
    /* Set correct Baseband to analog shift setting to access analog chips. */
    OS_REG_WRITE(ah, AR_PHY(0), 0x00000007);
#endif

    HALDEBUG(ah, HAL_DEBUG_RESET,
        "ar9300_process_ini: "
        "Skipping OS-REG-WRITE(ah, AR-PHY(0), 0x00000007)\n");
    HALDEBUG(ah, HAL_DEBUG_RESET,
        "ar9300_process_ini: no ADDac programming\n");


    /*
     * Osprey 2.0+ - new INI format.
     * Each subsystem has a pre, core, and post array.
     */
    for (i = 0; i < ATH_INI_NUM_SPLIT; i++) {
        ar9300_prog_ini(ah, &ahp->ah_ini_soc[i], modes_index);
        ar9300_prog_ini(ah, &ahp->ah_ini_mac[i], modes_index);
        ar9300_prog_ini(ah, &ahp->ah_ini_bb[i], modes_index);
        ar9300_prog_ini(ah, &ahp->ah_ini_radio[i], modes_index);
        if ((i == ATH_INI_POST) && (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah))) {
            ar9300_prog_ini(ah, &ahp->ah_ini_radio_post_sys2ant, modes_index);
        }

    }

	if (!(AR_SREV_SOC(ah))) {
			/* Doubler issue : Some board doesn't work well with MCS15. Turn off doubler after freq locking is complete*/
			//ath_hal_printf(ah, "%s[%d] ==== before reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			OS_REG_RMW(ah, AR_PHY_65NM_CH0_RXTX2, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0); /*Set synthon, synthover */
			//ath_hal_printf(ah, "%s[%d] ==== after reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			
			OS_REG_RMW(ah, AR_PHY_65NM_CH1_RXTX2, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0); /*Set synthon, synthover */
			OS_REG_RMW(ah, AR_PHY_65NM_CH2_RXTX2, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S, 0); /*Set synthon, synthover */
			OS_DELAY(200);
			
			//ath_hal_printf(ah, "%s[%d] ==== before reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			OS_REG_CLR_BIT(ah, AR_PHY_65NM_CH0_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK); /* clr synthon */
			OS_REG_CLR_BIT(ah, AR_PHY_65NM_CH1_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK); /* clr synthon */
			OS_REG_CLR_BIT(ah, AR_PHY_65NM_CH2_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK); /* clr synthon */
			//ath_hal_printf(ah, "%s[%d] ==== after reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			
			OS_DELAY(1);
			
			//ath_hal_printf(ah, "%s[%d] ==== before reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1); /* set synthon */
			OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH1_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1); /* set synthon */
			OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH2_RXTX2, AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK, 1); /* set synthon */
			//ath_hal_printf(ah, "%s[%d] ==== after reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
			
			OS_DELAY(200);
			
			//ath_hal_printf(ah, "%s[%d] ==== before reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_SYNTH12, OS_REG_READ(ah, AR_PHY_65NM_CH0_SYNTH12));
			OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_SYNTH12, AR_PHY_65NM_CH0_SYNTH12_VREFMUL3, 0xf);
			//OS_REG_CLR_BIT(ah, AR_PHY_65NM_CH0_SYNTH12, 1<< 16); /* clr charge pump */
			//ath_hal_printf(ah, "%s[%d] ==== After  reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_SYNTH12, OS_REG_READ(ah, AR_PHY_65NM_CH0_SYNTH12));
			
			OS_REG_RMW(ah, AR_PHY_65NM_CH0_RXTX2, 0, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S); /*Clr synthon, synthover */
			OS_REG_RMW(ah, AR_PHY_65NM_CH1_RXTX2, 0, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S); /*Clr synthon, synthover */
			OS_REG_RMW(ah, AR_PHY_65NM_CH2_RXTX2, 0, 1 << AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S | 
			               1 << AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S); /*Clr synthon, synthover */
			//ath_hal_printf(ah, "%s[%d] ==== after reg[0x%08x] = 0x%08x\n", __func__, __LINE__, AR_PHY_65NM_CH0_RXTX2, OS_REG_READ(ah, AR_PHY_65NM_CH0_RXTX2));
		}

    /* Write rxgain Array Parameters */
    REG_WRITE_ARRAY(&ahp->ah_ini_modes_rxgain, 1, reg_writes);
    HALDEBUG(ah, HAL_DEBUG_RESET, "ar9300_process_ini: Rx Gain programming\n");

    if (AR_SREV_JUPITER_20_OR_LATER(ah)) {
        /*
         * CUS217 mix LNA mode.
         */
        if (ar9300_rx_gain_index_get(ah) == 2) {
            REG_WRITE_ARRAY(&ahp->ah_ini_modes_rxgain_bb_core, 1, reg_writes);
            REG_WRITE_ARRAY(&ahp->ah_ini_modes_rxgain_bb_postamble,
                modes_index, reg_writes);
        }

        /*
         * 5G-XLNA
         */
        if ((ar9300_rx_gain_index_get(ah) == 2) ||
            (ar9300_rx_gain_index_get(ah) == 3)) {
            REG_WRITE_ARRAY(&ahp->ah_ini_modes_rxgain_xlna, modes_index,
              reg_writes);
        }
    }

    if (AR_SREV_SCORPION(ah)) {
        /* Write rxgain bounds Array */
        REG_WRITE_ARRAY(&ahp->ah_ini_modes_rxgain_bounds, modes_index, reg_writes);
        HALDEBUG(ah, HAL_DEBUG_RESET, "ar9300_process_ini: Rx Gain table bounds programming\n");
    }
    /* UB124 xLNA settings */
    if (AR_SREV_WASP(ah) && ar9300_rx_gain_index_get(ah) == 2) {
#define REG_WRITE(_reg,_val)    *((volatile u_int32_t *)(_reg)) = (_val);
#define REG_READ(_reg)          *((volatile u_int32_t *)(_reg))
        u_int32_t val;
        /* B8040000:  bit[0]=0, bit[3]=0; */
        val = REG_READ(0xB8040000);
        val &= 0xfffffff6;
        REG_WRITE(0xB8040000, val);
        /* B804002c:  bit[31:24]=0x2e; bit[7:0]=0x2f; */
        val = REG_READ(0xB804002c);
        val &= 0x00ffff00;
        val |= 0x2e00002f;
        REG_WRITE(0xB804002c, val);
        /* B804006c:  bit[1]=1; */
        val = REG_READ(0xB804006c);
        val |= 0x2;
        REG_WRITE(0xB804006c, val);
#undef REG_READ
#undef REG_WRITE
    }


    /* Write txgain Array Parameters */
    if (AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah)) {
        REG_WRITE_ARRAY(&ahp->ah_ini_modes_txgain, modes_txgaintable_index, 
            reg_writes);
    }else{
        REG_WRITE_ARRAY(&ahp->ah_ini_modes_txgain, modes_index, reg_writes);
    }
    HALDEBUG(ah, HAL_DEBUG_RESET, "ar9300_process_ini: Tx Gain programming\n");


    /* For 5GHz channels requiring Fast Clock, apply different modal values */
    if (IS_5GHZ_FAST_CLOCK_EN(ah, chan)) {
        HALDEBUG(ah, HAL_DEBUG_RESET,
            "%s: Fast clock enabled, use special ini values\n", __func__);
        REG_WRITE_ARRAY(&ahp->ah_ini_modes_additional, modes_index, reg_writes);
    }

    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah)) {
        HALDEBUG(ah, HAL_DEBUG_RESET,
            "%s: use xtal ini for AH9300(ah)->clk_25mhz: %d\n",
            __func__, AH9300(ah)->clk_25mhz);
        REG_WRITE_ARRAY(
            &ahp->ah_ini_modes_additional, 1/*modes_index*/, reg_writes);
    }

    if (AR_SREV_WASP(ah) && (AH9300(ah)->clk_25mhz == 0)) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Apply 40MHz ini settings\n", __func__);
        REG_WRITE_ARRAY(
            &ahp->ah_ini_modes_additional_40mhz, 1/*modesIndex*/, reg_writes);
    }

    /* Handle Japan Channel 14 channel spreading */
    if (2484 == ichan->channel) {
        ar9300_prog_ini(ah, &ahp->ah_ini_japan2484, 1);
    }

#if 0
    /* XXX TODO! */
    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        ar9300_prog_ini(ah, &ahp->ah_ini_BTCOEX_MAX_TXPWR, 1);
    }
#endif

    /* Override INI with chip specific configuration */
    ar9300_override_ini(ah, chan);

    /* Setup 11n MAC/Phy mode registers */
    ar9300_set_11n_regs(ah, chan, macmode);

    /*
     * Moved ar9300_init_chain_masks() here to ensure the swap bit is set before
     * the pdadc table is written.  Swap must occur before any radio dependent
     * replicated register access.  The pdadc curve addressing in particular
     * depends on the consistent setting of the swap bit.
     */
    ar9300_init_chain_masks(ah, ahp->ah_rx_chainmask, ahp->ah_tx_chainmask);

    /*
     * Setup the transmit power values.
     *
     * After the public to private hal channel mapping, ichan contains the
     * valid regulatory power value.
     * ath_hal_getctl and ath_hal_getantennaallowed look up ichan from chan.
     */
    status = ar9300_eeprom_set_transmit_power(ah, &ahp->ah_eeprom, chan,
             ath_hal_getctl(ah, chan), ath_hal_getantennaallowed(ah, chan),
             ath_hal_get_twice_max_regpower(ahpriv, ichan, chan),
             AH_MIN(MAX_RATE_POWER, ahpriv->ah_powerLimit));
    if (status != HAL_OK) {
        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
            "%s: error init'ing transmit power\n", __func__);
        return HAL_EIO;
    }


    return HAL_OK;
#undef N
}

/* ar9300_is_cal_supp
 * Determine if calibration is supported by device and channel flags
 */
inline static HAL_BOOL
ar9300_is_cal_supp(struct ath_hal *ah, const struct ieee80211_channel *chan,
    HAL_CAL_TYPES cal_type) 
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL retval = AH_FALSE;

    switch (cal_type & ahp->ah_supp_cals) {
    case IQ_MISMATCH_CAL:
        /* Run IQ Mismatch for non-CCK only */
        if (!IEEE80211_IS_CHAN_B(chan)) {
            retval = AH_TRUE;
        }
        break;
    case TEMP_COMP_CAL:
        retval = AH_TRUE;
        break;
    }

    return retval;
}


#if 0
/* ar9285_pa_cal
 * PA Calibration for Kite 1.1 and later versions of Kite.
 * - from system's team.
 */
static inline void
ar9285_pa_cal(struct ath_hal *ah)
{
    u_int32_t reg_val;
    int i, lo_gn, offs_6_1, offs_0;
    u_int8_t reflo;
    u_int32_t phy_test2_reg_val, phy_adc_ctl_reg_val;
    u_int32_t an_top2_reg_val, phy_tst_dac_reg_val;


    /* Kite 1.1 WAR for Bug 35666 
     * Increase the LDO value to 1.28V before accessing analog Reg */
    if (AR_SREV_KITE_11(ah)) {
        OS_REG_WRITE(ah, AR9285_AN_TOP4, (AR9285_AN_TOP4_DEFAULT | 0x14) );
    }
    an_top2_reg_val = OS_REG_READ(ah, AR9285_AN_TOP2);

    /* set pdv2i pdrxtxbb */
    reg_val = OS_REG_READ(ah, AR9285_AN_RXTXBB1);
    reg_val |= ((0x1 << 5) | (0x1 << 7));
    OS_REG_WRITE(ah, AR9285_AN_RXTXBB1, reg_val);

    /* clear pwddb */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G7);
    reg_val &= 0xfffffffd;
    OS_REG_WRITE(ah, AR9285_AN_RF2G7, reg_val);

    /* clear enpacal */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G1);
    reg_val &= 0xfffff7ff;
    OS_REG_WRITE(ah, AR9285_AN_RF2G1, reg_val);

    /* set offcal */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G2);
    reg_val |= (0x1 << 12);
    OS_REG_WRITE(ah, AR9285_AN_RF2G2, reg_val);

    /* set pdpadrv1=pdpadrv2=pdpaout=1 */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G1);
    reg_val |= (0x7 << 23);
    OS_REG_WRITE(ah, AR9285_AN_RF2G1, reg_val);

    /* Read back reflo, increase it by 1 and write it. */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reflo = ((reg_val >> 26) & 0x7);
    
    if (reflo < 0x7) {
        reflo++;
    }
    reg_val = ((reg_val & 0xe3ffffff) | (reflo << 26));
    OS_REG_WRITE(ah, AR9285_AN_RF2G3, reg_val);

    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reflo = ((reg_val >> 26) & 0x7);

    /* use TX single carrier to transmit
     * dac const
     * reg. 15
     */
    phy_tst_dac_reg_val = OS_REG_READ(ah, AR_PHY_TSTDAC_CONST);
    OS_REG_WRITE(ah, AR_PHY_TSTDAC_CONST, ((0x7ff << 11) | 0x7ff)); 
    reg_val = OS_REG_READ(ah, AR_PHY_TSTDAC_CONST);

    /* source is dac const
     * reg. 2
     */
    phy_test2_reg_val = OS_REG_READ(ah, AR_PHY_TEST2);
    OS_REG_WRITE(ah, AR_PHY_TEST2, ((0x1 << 7) | (0x1 << 1)));
    reg_val = OS_REG_READ(ah, AR_PHY_TEST2);

    /* set dac on
     * reg. 11
     */
    phy_adc_ctl_reg_val = OS_REG_READ(ah, AR_PHY_ADC_CTL);
    OS_REG_WRITE(ah, AR_PHY_ADC_CTL, 0x80008000);
    reg_val = OS_REG_READ(ah, AR_PHY_ADC_CTL);

    OS_REG_WRITE(ah, AR9285_AN_TOP2, (0x1 << 27) | (0x1 << 17) | (0x1 << 16) |
              (0x1 << 14) | (0x1 << 12) | (0x1 << 11) |
              (0x1 << 7) | (0x1 << 5));

    OS_DELAY(10); /* 10 usec */

    /* clear off[6:0] */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G6);
    reg_val &= 0xfc0fffff;
    OS_REG_WRITE(ah, AR9285_AN_RF2G6, reg_val);
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reg_val &= 0xfdffffff;
    OS_REG_WRITE(ah, AR9285_AN_RF2G3, reg_val);

    offs_6_1 = 0;
    for (i = 6; i > 0; i--) {
        /* sef off[$k]==1 */
        reg_val = OS_REG_READ(ah, AR9285_AN_RF2G6);
        reg_val &= 0xfc0fffff;
        reg_val = reg_val | (0x1 << (19 + i)) | ((offs_6_1) << 20);
        OS_REG_WRITE(ah, AR9285_AN_RF2G6, reg_val);
        lo_gn = (OS_REG_READ(ah, AR9285_AN_RF2G9)) & 0x1;
        offs_6_1 = offs_6_1 | (lo_gn << (i - 1));
    }

    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G6);
    reg_val &= 0xfc0fffff;
    reg_val = reg_val | ((offs_6_1 - 1) << 20);
    OS_REG_WRITE(ah, AR9285_AN_RF2G6, reg_val);

    /* set off_0=1; */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reg_val &= 0xfdffffff;
    reg_val = reg_val | (0x1 << 25);
    OS_REG_WRITE(ah, AR9285_AN_RF2G3, reg_val);

    lo_gn = OS_REG_READ(ah, AR9285_AN_RF2G9) & 0x1;
    offs_0 = lo_gn;

    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reg_val &= 0xfdffffff;
    reg_val = reg_val | (offs_0 << 25);
    OS_REG_WRITE(ah, AR9285_AN_RF2G3, reg_val);

    /* clear pdv2i */
    reg_val = OS_REG_READ(ah, AR9285_AN_RXTXBB1);
    reg_val &= 0xffffff5f;
    OS_REG_WRITE(ah, AR9285_AN_RXTXBB1, reg_val);

    /* set enpacal */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G1);
    reg_val |= (0x1 << 11);
    OS_REG_WRITE(ah, AR9285_AN_RF2G1, reg_val);

    /* clear offcal */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G2);
    reg_val &= 0xffffefff;
    OS_REG_WRITE(ah, AR9285_AN_RF2G2, reg_val);

    /* set pdpadrv1=pdpadrv2=pdpaout=0 */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G1);
    reg_val &= 0xfc7fffff;
    OS_REG_WRITE(ah, AR9285_AN_RF2G1, reg_val);

    /* Read back reflo, decrease it by 1 and write it. */
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reflo = (reg_val >> 26) & 0x7;
    if (reflo) {
        reflo--;
    }
    reg_val = ((reg_val & 0xe3ffffff) | (reflo << 26));
    OS_REG_WRITE(ah, AR9285_AN_RF2G3, reg_val);
    reg_val = OS_REG_READ(ah, AR9285_AN_RF2G3);
    reflo = (reg_val >> 26) & 0x7;

    /* write back registers */
    OS_REG_WRITE(ah, AR_PHY_TSTDAC_CONST, phy_tst_dac_reg_val);
    OS_REG_WRITE(ah, AR_PHY_TEST2, phy_test2_reg_val);
    OS_REG_WRITE(ah, AR_PHY_ADC_CTL, phy_adc_ctl_reg_val);
    OS_REG_WRITE(ah, AR9285_AN_TOP2, an_top2_reg_val);

    /* Kite 1.1 WAR for Bug 35666 
     * Decrease the LDO value back to 1.20V */
    if (AR_SREV_KITE_11(ah)) {
        OS_REG_WRITE(ah, AR9285_AN_TOP4, AR9285_AN_TOP4_DEFAULT);
    }
}
#endif

/* ar9300_run_init_cals
 * Runs non-periodic calibrations
 */
inline static HAL_BOOL
ar9300_run_init_cals(struct ath_hal *ah, int init_cal_count)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL ichan; /* bogus */
    HAL_BOOL is_cal_done;
    HAL_CAL_LIST *curr_cal;
    const HAL_PERCAL_DATA *cal_data;
    int i;

    curr_cal = ahp->ah_cal_list_curr;
    if (curr_cal == AH_NULL) {
        return AH_FALSE;
    }
    cal_data = curr_cal->cal_data;
    ichan.calValid = 0;

    for (i = 0; i < init_cal_count; i++) {
        /* Reset this Cal */
        ar9300_reset_calibration(ah, curr_cal);
        /* Poll for offset calibration complete */
        if (!ath_hal_wait(
                ah, AR_PHY_TIMING4, AR_PHY_TIMING4_DO_CAL, 0))
        {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s: Cal %d failed to complete in 100ms.\n",
                __func__, curr_cal->cal_data->cal_type);
            /* Re-initialize list pointers for periodic cals */
            ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr
                = AH_NULL;
            return AH_FALSE;
        } 
        /* Run this cal */
        ar9300_per_calibration(
            ah, &ichan, ahp->ah_rx_chainmask, curr_cal, &is_cal_done);
        if (is_cal_done == AH_FALSE) {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s: Not able to run Init Cal %d.\n", __func__,
                curr_cal->cal_data->cal_type);
        }
        if (curr_cal->cal_next) {
            curr_cal = curr_cal->cal_next;
        }
    }

    /* Re-initialize list pointers for periodic cals */
    ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr = AH_NULL;
    return AH_TRUE;
}

#if 0
static void
ar9300_tx_carrier_leak_war(struct ath_hal *ah)
{
    unsigned long tx_gain_table_max;
    unsigned long reg_bb_cl_map_0_b0 = 0xffffffff;
    unsigned long reg_bb_cl_map_1_b0 = 0xffffffff;
    unsigned long reg_bb_cl_map_2_b0 = 0xffffffff;
    unsigned long reg_bb_cl_map_3_b0 = 0xffffffff;
    unsigned long tx_gain, cal_run = 0;
    unsigned long cal_gain[AR_PHY_TPC_7_TX_GAIN_TABLE_MAX + 1];
    unsigned long cal_gain_index[AR_PHY_TPC_7_TX_GAIN_TABLE_MAX + 1];
    unsigned long new_gain[AR_PHY_TPC_7_TX_GAIN_TABLE_MAX + 1];
    int i, j;

    OS_MEMSET(new_gain, 0, sizeof(new_gain));
    /*printf("     Running TxCarrierLeakWAR\n");*/

    /* process tx gain table, we use cl_map_hw_gen=0. */
    OS_REG_RMW_FIELD(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_MAP_HW_GEN, 0);

	//the table we used is txbb_gc[2:0], 1dB[2:1].
    tx_gain_table_max = OS_REG_READ_FIELD(ah,
        AR_PHY_TPC_7, AR_PHY_TPC_7_TX_GAIN_TABLE_MAX);

    for (i = 0; i <= tx_gain_table_max; i++) {
        tx_gain = OS_REG_READ(ah, AR_PHY_TXGAIN_TAB(1) + i * 4);
        cal_gain[i] = (((tx_gain >> 5)& 0x7) << 2) |
            (((tx_gain >> 1) & 0x3) << 0);
        if (i == 0) {
            cal_gain_index[i] = cal_run;
            new_gain[i] = 1;
            cal_run++;
        } else {
            new_gain[i] = 1;
            for (j = 0; j < i; j++) {
                /*
                printf("i=%d, j=%d cal_gain[$i]=0x%04x\n", i, j, cal_gain[i]);
                 */
                if (new_gain[i]) {
                    if ((cal_gain[i] != cal_gain[j])) {
                        new_gain[i] = 1;
                    } else {
                        /* if old gain found, use old cal_run value. */
                        new_gain[i] = 0;
                        cal_gain_index[i] = cal_gain_index[j];
                    }
                }
            }
            /* if new gain found, increase cal_run */
            if (new_gain[i] == 1) {
                cal_gain_index[i] = cal_run;
                cal_run++;
            }
        }

        reg_bb_cl_map_0_b0 = (reg_bb_cl_map_0_b0 & ~(0x1 << i)) |
            ((cal_gain_index[i] >> 0 & 0x1) << i);
        reg_bb_cl_map_1_b0 = (reg_bb_cl_map_1_b0 & ~(0x1 << i)) |
            ((cal_gain_index[i] >> 1 & 0x1) << i);
        reg_bb_cl_map_2_b0 = (reg_bb_cl_map_2_b0 & ~(0x1 << i)) |
            ((cal_gain_index[i] >> 2 & 0x1) << i);
        reg_bb_cl_map_3_b0 = (reg_bb_cl_map_3_b0 & ~(0x1 << i)) |
            ((cal_gain_index[i] >> 3 & 0x1) << i);

        /*
        printf("i=%2d, cal_gain[$i]= 0x%04x, cal_run= %d, "
            "cal_gain_index[i]=%d, new_gain[i] = %d\n",
            i, cal_gain[i], cal_run, cal_gain_index[i], new_gain[i]);
         */
    }
    OS_REG_WRITE(ah, AR_PHY_CL_MAP_0_B0, reg_bb_cl_map_0_b0);
    OS_REG_WRITE(ah, AR_PHY_CL_MAP_1_B0, reg_bb_cl_map_1_b0);
    OS_REG_WRITE(ah, AR_PHY_CL_MAP_2_B0, reg_bb_cl_map_2_b0);
    OS_REG_WRITE(ah, AR_PHY_CL_MAP_3_B0, reg_bb_cl_map_3_b0);
    if (AR_SREV_WASP(ah)) {
        OS_REG_WRITE(ah, AR_PHY_CL_MAP_0_B1, reg_bb_cl_map_0_b0);
        OS_REG_WRITE(ah, AR_PHY_CL_MAP_1_B1, reg_bb_cl_map_1_b0);
        OS_REG_WRITE(ah, AR_PHY_CL_MAP_2_B1, reg_bb_cl_map_2_b0);
        OS_REG_WRITE(ah, AR_PHY_CL_MAP_3_B1, reg_bb_cl_map_3_b0);
    }
}
#endif


static inline void
ar9300_invalidate_saved_cals(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan)
{
#if ATH_SUPPORT_CAL_REUSE
    if (AH_PRIVATE(ah)->ah_config.ath_hal_cal_reuse &
        ATH_CAL_REUSE_REDO_IN_FULL_RESET)
    {
        ichan->one_time_txiqcal_done = AH_FALSE;
        ichan->one_time_txclcal_done = AH_FALSE;
    }
#endif
}

static inline HAL_BOOL
ar9300_restore_rtt_cals(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan)
{
    HAL_BOOL restore_status = AH_FALSE;

    return restore_status;
}

/* ar9300_init_cal
 * Initialize Calibration infrastructure
 */
static inline HAL_BOOL
ar9300_init_cal_internal(struct ath_hal *ah, struct ieee80211_channel *chan,
                         HAL_CHANNEL_INTERNAL *ichan,
                         HAL_BOOL enable_rtt, HAL_BOOL do_rtt_cal, HAL_BOOL skip_if_none, HAL_BOOL apply_last_iqcorr)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL txiqcal_success_flag = AH_FALSE;
    HAL_BOOL cal_done = AH_FALSE;
    int iqcal_idx = 0;
    HAL_BOOL do_sep_iq_cal = AH_FALSE;
    HAL_BOOL do_agc_cal = do_rtt_cal;
    HAL_BOOL is_cal_reusable = AH_TRUE;
#if ATH_SUPPORT_CAL_REUSE
    HAL_BOOL      cal_reuse_enable = AH_PRIVATE(ah)->ah_config.ath_hal_cal_reuse &
                                 ATH_CAL_REUSE_ENABLE;
    HAL_BOOL      clc_success = AH_FALSE;
    int32_t   ch_idx, j, cl_tab_reg;
    u_int32_t BB_cl_tab_entry = MAX_BB_CL_TABLE_ENTRY;
    u_int32_t BB_cl_tab_b[AR9300_MAX_CHAINS] = {
                    AR_PHY_CL_TAB_0,
                    AR_PHY_CL_TAB_1,
                    AR_PHY_CL_TAB_2
                };
#endif

    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        /* Hornet: 1 x 1 */
        ahp->ah_rx_cal_chainmask = 0x1;
        ahp->ah_tx_cal_chainmask = 0x1;
    } else if (AR_SREV_WASP(ah) || AR_SREV_JUPITER(ah) || AR_SREV_HONEYBEE(ah)) {
        /* Wasp/Jupiter: 2 x 2 */
        ahp->ah_rx_cal_chainmask = 0x3;
        ahp->ah_tx_cal_chainmask = 0x3;
    } else {
        /*
         * Osprey needs to be configured for the correct chain mode
         * before running AGC/TxIQ cals.
         */
        if (ahp->ah_enterprise_mode & AR_ENT_OTP_CHAIN2_DISABLE) {
            /* chain 2 disabled - 2 chain mode */
            ahp->ah_rx_cal_chainmask = 0x3;
            ahp->ah_tx_cal_chainmask = 0x3;
        } else {
            ahp->ah_rx_cal_chainmask = 0x7;
            ahp->ah_tx_cal_chainmask = 0x7;
        }
    }
        ar9300_init_chain_masks(ah, ahp->ah_rx_cal_chainmask, ahp->ah_tx_cal_chainmask);


    if (ahp->tx_cl_cal_enable) {
#if ATH_SUPPORT_CAL_REUSE
        /* disable Carrie Leak or set do_agc_cal accordingly */
        if (cal_reuse_enable && ichan->one_time_txclcal_done)
        {
            OS_REG_CLR_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
        } else
#endif /* ATH_SUPPORT_CAL_REUSE */
        {
            OS_REG_SET_BIT(ah, AR_PHY_CL_CAL_CTL, AR_PHY_CL_CAL_ENABLE);
            do_agc_cal = AH_TRUE;
        }
    }

    /* Do Tx IQ Calibration here for osprey hornet and wasp */
    /* XXX: For initial wasp bringup - check and enable this */
    /* EV 74233: Tx IQ fails to complete for half/quarter rates */
    if (!(IEEE80211_IS_CHAN_HALF(chan) || IEEE80211_IS_CHAN_QUARTER(chan))) {
        if (ahp->tx_iq_cal_enable) {
            /* this should be eventually moved to INI file */
            OS_REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_1(ah),
                AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT, DELPT);

            /*
             * For poseidon and later chips,
             * Tx IQ cal HW run will be a part of AGC calibration
             */
            if (ahp->tx_iq_cal_during_agc_cal) {
                /*
                 * txiqcal_success_flag always set to 1 to run
                 *     ar9300_tx_iq_cal_post_proc
                 * if following AGC cal passes
                */
#if ATH_SUPPORT_CAL_REUSE
                if (!cal_reuse_enable || !ichan->one_time_txiqcal_done)
                {
                    txiqcal_success_flag = AH_TRUE;
                    OS_REG_WRITE(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
                        OS_REG_READ(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah)) |
                        AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
                } else {
                    OS_REG_WRITE(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
                        OS_REG_READ(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah)) &
                        (~AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL));
                }
#else
		if (OS_REG_READ_FIELD(ah,
					AR_PHY_TX_IQCAL_CONTROL_0(ah),
					AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL)){
			if (apply_last_iqcorr == AH_TRUE) {
				OS_REG_CLR_BIT(ah, AR_PHY_TX_IQCAL_CONTROL_0(ah),
						AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL);
				txiqcal_success_flag = AH_FALSE;
			} else {
				txiqcal_success_flag = AH_TRUE;
			}
		}else{
			txiqcal_success_flag = AH_FALSE;
		}
#endif
                if (txiqcal_success_flag) {
                    do_agc_cal = AH_TRUE;
                }
            } else
#if ATH_SUPPORT_CAL_REUSE
            if (!cal_reuse_enable || !ichan->one_time_txiqcal_done)
#endif
            {
                do_sep_iq_cal = AH_TRUE;
                do_agc_cal = AH_TRUE;
            }
        }
    }

#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport &&
        IS_CHAN_2GHZ(ichan) &&
        (ahp->ah_mci_bt_state == MCI_BT_AWAKE) &&
        do_agc_cal &&
        !(ah->ah_config.ath_hal_mci_config & 
        ATH_MCI_CONFIG_DISABLE_MCI_CAL))
    {
        u_int32_t payload[4] = {0, 0, 0, 0};

        /* Send CAL_REQ only when BT is AWAKE. */
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Send WLAN_CAL_REQ 0x%X\n",
            __func__, ahp->ah_mci_wlan_cal_seq);
        MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_REQ);
        payload[MCI_GPM_WLAN_CAL_W_SEQUENCE] = ahp->ah_mci_wlan_cal_seq++;
        ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, AH_TRUE, AH_FALSE);

        /* Wait BT_CAL_GRANT for 50ms */
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Wait for BT_CAL_GRANT\n", __func__);
        if (ar9300_mci_wait_for_gpm(ah, MCI_GPM_BT_CAL_GRANT, 0, 50000))
        {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(MCI) %s: Got BT_CAL_GRANT.\n", __func__);
        }
        else {
            is_cal_reusable = AH_FALSE;
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(MCI) %s: BT is not responding.\n", __func__);
        }
    }
#endif /* ATH_SUPPORT_MCI */

    if (do_sep_iq_cal)
    {
        /* enable Tx IQ Calibration HW for osprey/hornet/wasp */
        txiqcal_success_flag = ar9300_tx_iq_cal_hw_run(ah);
        OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
        OS_DELAY(5);
        OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
    }
#if 0
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah)) {
        ar9300_tx_carrier_leak_war(ah);
    }
#endif
    /*
     * Calibrate the AGC
     *
     * Tx IQ cal is a part of AGC cal for Jupiter/Poseidon, etc.
     * please enable the bit of txiqcal_control_0[31] in INI file
     * for Jupiter/Poseidon/etc.
     */
    if(!AR_SREV_SCORPION(ah)) {
        if (do_agc_cal || !skip_if_none) {
            OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
                OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_CAL);

            /* Poll for offset calibration complete */
            cal_done = ath_hal_wait(ah,
                    AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 0);
            if (!cal_done) {
                HALDEBUG(ah, HAL_DEBUG_FCS_RTT,
                    "(FCS) CAL NOT DONE!!! - %d\n", ichan->channel);
            }
        } else { 
            cal_done = AH_TRUE;
        }
            /*
             * Tx IQ cal post-processing in SW
             * This part of code should be common to all chips,
             * no chip specific code for Jupiter/Posdeion except for register names.
             */
            if (txiqcal_success_flag) {
                ar9300_tx_iq_cal_post_proc(ah,ichan, 1, 1,is_cal_reusable, AH_FALSE);
            }
    } else {
        if (!txiqcal_success_flag) {
            OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
                OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_CAL);
            if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL, 
                    0)) {
                HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "%s: offset calibration failed to complete in 1ms; "
                    "noisy environment?\n", __func__);
                return AH_FALSE;
            }
            if (apply_last_iqcorr == AH_TRUE) {
                ar9300_tx_iq_cal_post_proc(ah, ichan, 0, 0, is_cal_reusable, AH_TRUE);
            }
        } else {
            for (iqcal_idx=0;iqcal_idx<MAXIQCAL;iqcal_idx++) {
                OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
                    OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_CAL);

                /* Poll for offset calibration complete */
                if (!ath_hal_wait(ah, AR_PHY_AGC_CONTROL, 
                        AR_PHY_AGC_CONTROL_CAL, 0)) {
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                        "%s: offset calibration failed to complete in 1ms; "
                        "noisy environment?\n", __func__);
                    return AH_FALSE;
                }
                /*
                 * Tx IQ cal post-processing in SW
                 * This part of code should be common to all chips,
                 * no chip specific code for Jupiter/Posdeion except for register names.
                 */
                ar9300_tx_iq_cal_post_proc(ah, ichan, iqcal_idx+1, MAXIQCAL, is_cal_reusable, AH_FALSE);
            }
       }
    }


#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport &&
        IS_CHAN_2GHZ(ichan) &&
        (ahp->ah_mci_bt_state == MCI_BT_AWAKE) &&
        do_agc_cal &&
        !(ah->ah_config.ath_hal_mci_config & 
        ATH_MCI_CONFIG_DISABLE_MCI_CAL))
    {
        u_int32_t payload[4] = {0, 0, 0, 0};

        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Send WLAN_CAL_DONE 0x%X\n",
            __func__, ahp->ah_mci_wlan_cal_done);
        MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_DONE);
        payload[MCI_GPM_WLAN_CAL_W_SEQUENCE] = ahp->ah_mci_wlan_cal_done++;
        ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, AH_TRUE, AH_FALSE);
    }
#endif /* ATH_SUPPORT_MCI */


    if (!cal_done && !AR_SREV_SCORPION(ah) )
    {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: offset calibration failed to complete in 1ms; "
            "noisy environment?\n", __func__);
        return AH_FALSE;
    }

#if 0
     /* Beacon stuck fix, refer to EV 120056 */
    if(IS_CHAN_2GHZ(chan) && AR_SREV_SCORPION(ah))
        OS_REG_WRITE(ah, AR_PHY_TIMING5, OS_REG_READ(ah,AR_PHY_TIMING5) & ~AR_PHY_TIMING5_CYCPWR_THR1_ENABLE);
#endif

#if 0
    /* Do PA Calibration */
    if (AR_SREV_KITE(ah) && AR_SREV_KITE_11_OR_LATER(ah)) {
        ar9285_pa_cal(ah);
    }
#endif

#if ATH_SUPPORT_CAL_REUSE
     if (ichan->one_time_txiqcal_done) {
        ar9300_tx_iq_cal_apply(ah, ichan);
        HALDEBUG(ah, HAL_DEBUG_FCS_RTT,
            "(FCS) TXIQCAL applied - %d\n", ichan->channel);
    }
#endif /* ATH_SUPPORT_CAL_REUSE */

#if ATH_SUPPORT_CAL_REUSE
    if (cal_reuse_enable && ahp->tx_cl_cal_enable)
    {
        clc_success = (OS_REG_READ(ah, AR_PHY_AGC_CONTROL) &
                  AR_PHY_AGC_CONTROL_CLC_SUCCESS) ? 1 : 0;

        if (ichan->one_time_txclcal_done)
        {
            /* reapply CL cal results */
            for (ch_idx = 0; ch_idx < AR9300_MAX_CHAINS; ch_idx++) {
                if ((ahp->ah_tx_cal_chainmask & (1 << ch_idx)) == 0) {
                    continue;
                }
                cl_tab_reg = BB_cl_tab_b[ch_idx];
                for (j = 0; j < BB_cl_tab_entry; j++) {
                    OS_REG_WRITE(ah, cl_tab_reg, ichan->tx_clcal[ch_idx][j]);
                    cl_tab_reg += 4;;
                }
            }
            HALDEBUG(ah, HAL_DEBUG_FCS_RTT,
                "(FCS) TX CL CAL applied - %d\n", ichan->channel);
        }
        else if (is_cal_reusable && clc_success) {
            /* save CL cal results */
            for (ch_idx = 0; ch_idx < AR9300_MAX_CHAINS; ch_idx++) {
                if ((ahp->ah_tx_cal_chainmask & (1 << ch_idx)) == 0) {
                    continue;
                }
                cl_tab_reg = BB_cl_tab_b[ch_idx];
                for (j = 0; j < BB_cl_tab_entry; j++) {
                    ichan->tx_clcal[ch_idx][j] = OS_REG_READ(ah, cl_tab_reg);
                    cl_tab_reg += 4;
                }
            }
            ichan->one_time_txclcal_done = AH_TRUE;
            HALDEBUG(ah, HAL_DEBUG_FCS_RTT,
                "(FCS) TX CL CAL saved - %d\n", ichan->channel);
        }
    }
#endif /* ATH_SUPPORT_CAL_REUSE */

    /* Revert chainmasks to their original values before NF cal */
    ar9300_init_chain_masks(ah, ahp->ah_rx_chainmask, ahp->ah_tx_chainmask);

#if !FIX_NOISE_FLOOR
    /*
     * Do NF calibration after DC offset and other CALs.
     * Per system engineers, noise floor value can sometimes be 20 dB
     * higher than normal value if DC offset and noise floor cal are
     * triggered at the same time.
     */
    OS_REG_WRITE(ah, AR_PHY_AGC_CONTROL,
        OS_REG_READ(ah, AR_PHY_AGC_CONTROL) | AR_PHY_AGC_CONTROL_NF);
#endif 

    /* Initialize list pointers */
    ahp->ah_cal_list = ahp->ah_cal_list_last = ahp->ah_cal_list_curr = AH_NULL;

    /*
     * Enable IQ, ADC Gain, ADC DC Offset Cals
     */
    /* Setup all non-periodic, init time only calibrations */
    /* XXX: Init DC Offset not working yet */
#ifdef not_yet
    if (AH_TRUE == ar9300_is_cal_supp(ah, chan, ADC_DC_INIT_CAL)) {
        INIT_CAL(&ahp->ah_adc_dc_cal_init_data);
        INSERT_CAL(ahp, &ahp->ah_adc_dc_cal_init_data);
    }

    /* Initialize current pointer to first element in list */
    ahp->ah_cal_list_curr = ahp->ah_cal_list;

    if (ahp->ah_cal_list_curr) {
        if (ar9300_run_init_cals(ah, 0) == AH_FALSE) {
            return AH_FALSE;
        }
    }
#endif
    /* end - Init time calibrations */

    /* Do not do RX cal in case of offchan, or cal data already exists on same channel*/
    if (ahp->ah_skip_rx_iq_cal) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Skip RX IQ Cal\n");
        return AH_TRUE;
    }

    /* If Cals are supported, add them to list via INIT/INSERT_CAL */
    if (AH_TRUE == ar9300_is_cal_supp(ah, chan, IQ_MISMATCH_CAL)) {
        INIT_CAL(&ahp->ah_iq_cal_data);
        INSERT_CAL(ahp, &ahp->ah_iq_cal_data);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: enabling IQ Calibration.\n", __func__);
    }
    if (AH_TRUE == ar9300_is_cal_supp(ah, chan, TEMP_COMP_CAL)) {
        INIT_CAL(&ahp->ah_temp_comp_cal_data);
        INSERT_CAL(ahp, &ahp->ah_temp_comp_cal_data);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: enabling Temperature Compensation Calibration.\n", __func__);
    }

    /* Initialize current pointer to first element in list */
    ahp->ah_cal_list_curr = ahp->ah_cal_list;

    /* Reset state within current cal */
    if (ahp->ah_cal_list_curr) {
        ar9300_reset_calibration(ah, ahp->ah_cal_list_curr);
    }

    /* Mark all calibrations on this channel as being invalid */
    ichan->calValid = 0;

    return AH_TRUE;
}

static inline HAL_BOOL
ar9300_init_cal(struct ath_hal *ah, struct ieee80211_channel *chan, HAL_BOOL skip_if_none, HAL_BOOL apply_last_iqcorr)
{
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    HAL_BOOL do_rtt_cal = AH_TRUE;
    HAL_BOOL enable_rtt = AH_FALSE;

    HALASSERT(ichan);

    return ar9300_init_cal_internal(ah, chan, ichan, enable_rtt, do_rtt_cal, skip_if_none, apply_last_iqcorr);
}

/* ar9300_reset_cal_valid
 * Entry point for upper layers to restart current cal.
 * Reset the calibration valid bit in channel.
 */
void
ar9300_reset_cal_valid(struct ath_hal *ah, const struct ieee80211_channel *chan,
    HAL_BOOL *is_cal_done, u_int32_t cal_type)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    HAL_CAL_LIST *curr_cal = ahp->ah_cal_list_curr;

    *is_cal_done = AH_TRUE;

    if (curr_cal == AH_NULL) {
        return;
    }
    if (ichan == AH_NULL) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: invalid channel %u/0x%x; no mapping\n",
            __func__, chan->ic_freq, chan->ic_flags);
        return;
    }

    if (!(cal_type & IQ_MISMATCH_CAL)) {
        *is_cal_done = AH_FALSE;
        return;
    }

    /* Expected that this calibration has run before, post-reset.
     * Current state should be done
     */
    if (curr_cal->cal_state != CAL_DONE) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Calibration state incorrect, %d\n",
            __func__, curr_cal->cal_state);
        return;
    }

    /* Verify Cal is supported on this channel */
    if (ar9300_is_cal_supp(ah, chan, curr_cal->cal_data->cal_type) == AH_FALSE) {
        return;
    }

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: Resetting Cal %d state for channel %u/0x%x\n", __func__,
        curr_cal->cal_data->cal_type, chan->ic_freq, chan->ic_flags);

    /* Disable cal validity in channel */
    ichan->calValid &= ~curr_cal->cal_data->cal_type;
    curr_cal->cal_state = CAL_WAITING;
    /* Indicate to upper layers that we need polling */
    *is_cal_done = AH_FALSE;
}

static inline void
ar9300_set_dma(struct ath_hal *ah)
{
    u_int32_t   regval;
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    HAL_CAPABILITIES *pCap = &ahpriv->ah_caps;

#if 0
    /*
     * set AHB_MODE not to do cacheline prefetches
     */
    regval = OS_REG_READ(ah, AR_AHB_MODE);
    OS_REG_WRITE(ah, AR_AHB_MODE, regval | AR_AHB_PREFETCH_RD_EN);
#endif

    /*
     * let mac dma reads be in 128 byte chunks
     */
    regval = OS_REG_READ(ah, AR_TXCFG) & ~AR_TXCFG_DMASZ_MASK;
    OS_REG_WRITE(ah, AR_TXCFG, regval | AR_TXCFG_DMASZ_128B);

    /*
     * Restore TX Trigger Level to its pre-reset value.
     * The initial value depends on whether aggregation is enabled, and is
     * adjusted whenever underruns are detected.
     */
    /*
    OS_REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, AH_PRIVATE(ah)->ah_tx_trig_level);
     */
    /* 
     * Osprey 1.0 bug (EV 61936). Don't change trigger level from .ini default.
     * Osprey 2.0 - hardware recommends using the default INI settings.
     */
#if 0
    OS_REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, 0x3f);
#endif
    /*
     * let mac dma writes be in 128 byte chunks
     */
    regval = OS_REG_READ(ah, AR_RXCFG) & ~AR_RXCFG_DMASZ_MASK;
    OS_REG_WRITE(ah, AR_RXCFG, regval | AR_RXCFG_DMASZ_128B);

    /*
     * Setup receive FIFO threshold to hold off TX activities
     */
    OS_REG_WRITE(ah, AR_RXFIFO_CFG, 0x200);

    /*
     * reduce the number of usable entries in PCU TXBUF to avoid
     * wrap around bugs. (bug 20428)
     */
    
    if (AR_SREV_WASP(ah) && 
        (AH_PRIVATE((ah))->ah_macRev > AR_SREV_REVISION_WASP_12)) {
        /* Wasp 1.3 fix for EV#85395 requires usable entries 
         * to be set to 0x500 
         */
        OS_REG_WRITE(ah, AR_PCU_TXBUF_CTRL, 0x500);
    } else {
        OS_REG_WRITE(ah, AR_PCU_TXBUF_CTRL, AR_PCU_TXBUF_CTRL_USABLE_SIZE);
    }

    /*
     * Enable HPQ for UAPSD
     */
    if (pCap->halHwUapsdTrig == AH_TRUE) {
    /* Only enable this if HAL capabilities says it is OK */
        if (AH_PRIVATE(ah)->ah_opmode == HAL_M_HOSTAP) {
            OS_REG_WRITE(ah, AR_HP_Q_CONTROL,
                    AR_HPQ_ENABLE | AR_HPQ_UAPSD | AR_HPQ_UAPSD_TRIGGER_EN);
        }
    } else {
        /* use default value from ini file - which disable HPQ queue usage */
    }

    /*
     * set the transmit status ring
     */
    ar9300_reset_tx_status_ring(ah);

    /*
     * set rxbp threshold.  Must be non-zero for RX_EOL to occur.
     * For Osprey 2.0+, keep the original thresholds
     * otherwise performance is lost due to excessive RX EOL interrupts.
     */
    OS_REG_RMW_FIELD(ah, AR_RXBP_THRESH, AR_RXBP_THRESH_HP, 0x1);
    OS_REG_RMW_FIELD(ah, AR_RXBP_THRESH, AR_RXBP_THRESH_LP, 0x1);

    /*
     * set receive buffer size.
     */
    if (ahp->rx_buf_size) {
        OS_REG_WRITE(ah, AR_DATABUF, ahp->rx_buf_size);
    }
}

static inline void
ar9300_init_bb(struct ath_hal *ah, struct ieee80211_channel *chan)
{
    u_int32_t synth_delay;

    /*
     * Wait for the frequency synth to settle (synth goes on
     * via AR_PHY_ACTIVE_EN).  Read the phy active delay register.
     * Value is in 100ns increments.
     */
    synth_delay = OS_REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
    if (IEEE80211_IS_CHAN_CCK(chan)) {
        synth_delay = (4 * synth_delay) / 22;
    } else {
        synth_delay /= 10;
    }

    /* Activate the PHY (includes baseband activate + synthesizer on) */
    OS_REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

    /*
     * There is an issue if the AP starts the calibration before
     * the base band timeout completes.  This could result in the
     * rx_clear false triggering.  As a workaround we add delay an
     * extra BASE_ACTIVATE_DELAY usecs to ensure this condition
     * does not happen.
     */
    OS_DELAY(synth_delay + BASE_ACTIVATE_DELAY);
}

static inline void
ar9300_init_interrupt_masks(struct ath_hal *ah, HAL_OPMODE opmode)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t msi_cfg = 0;
    u_int32_t sync_en_def = AR9300_INTR_SYNC_DEFAULT;

    /*
     * Setup interrupt handling.  Note that ar9300_reset_tx_queue
     * manipulates the secondary IMR's as queues are enabled
     * and disabled.  This is done with RMW ops to insure the
     * settings we make here are preserved.
     */
    ahp->ah_mask_reg =
        AR_IMR_TXERR | AR_IMR_TXURN |
        AR_IMR_RXERR | AR_IMR_RXORN |
        AR_IMR_BCNMISC;

    if (ahp->ah_intr_mitigation_rx) {
        /* enable interrupt mitigation for rx */
        ahp->ah_mask_reg |= AR_IMR_RXINTM | AR_IMR_RXMINTR | AR_IMR_RXOK_HP;
        msi_cfg |= AR_INTCFG_MSI_RXINTM | AR_INTCFG_MSI_RXMINTR;
    } else {
        ahp->ah_mask_reg |= AR_IMR_RXOK_LP | AR_IMR_RXOK_HP;
        msi_cfg |= AR_INTCFG_MSI_RXOK;
    }
    if (ahp->ah_intr_mitigation_tx) {
        /* enable interrupt mitigation for tx */
        ahp->ah_mask_reg |= AR_IMR_TXINTM | AR_IMR_TXMINTR;
        msi_cfg |= AR_INTCFG_MSI_TXINTM | AR_INTCFG_MSI_TXMINTR;
    } else {
        ahp->ah_mask_reg |= AR_IMR_TXOK;
        msi_cfg |= AR_INTCFG_MSI_TXOK;
    }
    if (opmode == HAL_M_HOSTAP) {
        ahp->ah_mask_reg |= AR_IMR_MIB;
    }

    OS_REG_WRITE(ah, AR_IMR, ahp->ah_mask_reg);
    OS_REG_WRITE(ah, AR_IMR_S2, OS_REG_READ(ah, AR_IMR_S2) | AR_IMR_S2_GTT);
    ahp->ah_mask2Reg = OS_REG_READ(ah, AR_IMR_S2);

    if (ah->ah_config.ath_hal_enable_msi) {
        /* Cache MSI register value */
        ahp->ah_msi_reg = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_MSI));
        ahp->ah_msi_reg |= AR_PCIE_MSI_HW_DBI_WR_EN;
        if (AR_SREV_POSEIDON(ah)) {
            ahp->ah_msi_reg &= AR_PCIE_MSI_HW_INT_PENDING_ADDR_MSI_64;
        } else {
            ahp->ah_msi_reg &= AR_PCIE_MSI_HW_INT_PENDING_ADDR;
        }
        /* Program MSI configuration */
        OS_REG_WRITE(ah, AR_INTCFG, msi_cfg);
    }

    /*
     * debug - enable to see all synchronous interrupts status
     */
    /* Clear any pending sync cause interrupts */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE), 0xFFFFFFFF);

    /* Allow host interface sync interrupt sources to set cause bit */
    if (AR_SREV_POSEIDON(ah)) {
        sync_en_def = AR9300_INTR_SYNC_DEF_NO_HOST1_PERR;
    }
    else if (AR_SREV_WASP(ah)) {
        sync_en_def = AR9340_INTR_SYNC_DEFAULT;
    }
    OS_REG_WRITE(ah,
        AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE), sync_en_def);
    
    /* _Disable_ host interface sync interrupt when cause bits set */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK), 0);

    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE), 0);
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_MASK), 0);
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_ENABLE), 0);
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_MASK), 0);
}

static inline void
ar9300_init_qos(struct ath_hal *ah)
{
    OS_REG_WRITE(ah, AR_MIC_QOS_CONTROL, 0x100aa);  /* XXX magic */
    OS_REG_WRITE(ah, AR_MIC_QOS_SELECT, 0x3210);    /* XXX magic */

    /* Turn on NOACK Support for QoS packets */
    OS_REG_WRITE(ah, AR_QOS_NO_ACK,
        SM(2, AR_QOS_NO_ACK_TWO_BIT) |
        SM(5, AR_QOS_NO_ACK_BIT_OFF) |
        SM(0, AR_QOS_NO_ACK_BYTE_OFF));

    /*
     * initialize TXOP for all TIDs
     */
    OS_REG_WRITE(ah, AR_TXOP_X, AR_TXOP_X_VAL);
    OS_REG_WRITE(ah, AR_TXOP_0_3, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_TXOP_4_7, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_TXOP_8_11, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_TXOP_12_15, 0xFFFFFFFF);
}

static inline void
ar9300_init_user_settings(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* Restore user-specified settings */
    HALDEBUG(ah, HAL_DEBUG_RESET,
        "--AP %s ahp->ah_misc_mode 0x%x\n", __func__, ahp->ah_misc_mode);
    if (ahp->ah_misc_mode != 0) {
        OS_REG_WRITE(ah,
            AR_PCU_MISC, OS_REG_READ(ah, AR_PCU_MISC) | ahp->ah_misc_mode);
    }
    if (ahp->ah_get_plcp_hdr) {
        OS_REG_CLR_BIT(ah, AR_PCU_MISC, AR_PCU_SEL_EVM);
    }
    if (ahp->ah_slot_time != (u_int) -1) {
        ar9300_set_slot_time(ah, ahp->ah_slot_time);
    }
    if (ahp->ah_ack_timeout != (u_int) -1) {
        ar9300_set_ack_timeout(ah, ahp->ah_ack_timeout);
    }
    if (AH_PRIVATE(ah)->ah_diagreg != 0) {
        OS_REG_SET_BIT(ah, AR_DIAG_SW, AH_PRIVATE(ah)->ah_diagreg);
    }
    if (ahp->ah_beacon_rssi_threshold != 0) {
        ar9300_set_hw_beacon_rssi_threshold(ah, ahp->ah_beacon_rssi_threshold);
    }
//#ifdef ATH_SUPPORT_DFS
    if (ahp->ah_cac_quiet_enabled) {
        ar9300_cac_tx_quiet(ah, 1);
    }
//#endif /* ATH_SUPPORT_DFS */
}

int
ar9300_get_spur_info(struct ath_hal * ah, int *enable, int len, u_int16_t *freq)
{
//    struct ath_hal_private *ap = AH_PRIVATE(ah);
    int i, j;

    for (i = 0; i < len; i++) {
        freq[i] =  0;
    }

    *enable = ah->ah_config.ath_hal_spur_mode;
    for (i = 0, j = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
        if (AH9300(ah)->ath_hal_spur_chans[i][0] != AR_NO_SPUR) {
            freq[j++] = AH9300(ah)->ath_hal_spur_chans[i][0];
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "1. get spur %d\n", AH9300(ah)->ath_hal_spur_chans[i][0]);
        }
        if (AH9300(ah)->ath_hal_spur_chans[i][1] != AR_NO_SPUR) {
            freq[j++] = AH9300(ah)->ath_hal_spur_chans[i][1];
            HALDEBUG(ah, HAL_DEBUG_ANI,
                "2. get spur %d\n", AH9300(ah)->ath_hal_spur_chans[i][1]);
        }
    }

    return 0;
}

#define ATH_HAL_2GHZ_FREQ_MIN   20000
#define ATH_HAL_2GHZ_FREQ_MAX   29999
#define ATH_HAL_5GHZ_FREQ_MIN   50000
#define ATH_HAL_5GHZ_FREQ_MAX   59999

#if 0
int
ar9300_set_spur_info(struct ath_hal * ah, int enable, int len, u_int16_t *freq)
{
    struct ath_hal_private *ap = AH_PRIVATE(ah);
    int i, j, k;

    ap->ah_config.ath_hal_spur_mode = enable;

    if (ap->ah_config.ath_hal_spur_mode == SPUR_ENABLE_IOCTL) {
        for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
            AH9300(ah)->ath_hal_spur_chans[i][0] = AR_NO_SPUR;
            AH9300(ah)->ath_hal_spur_chans[i][1] = AR_NO_SPUR;
        }
        for (i = 0, j = 0, k = 0; i < len; i++) {
            if (freq[i] > ATH_HAL_2GHZ_FREQ_MIN &&
                freq[i] < ATH_HAL_2GHZ_FREQ_MAX)
            {
                /* 2GHz Spur */
                if (j < AR_EEPROM_MODAL_SPURS) {
                    AH9300(ah)->ath_hal_spur_chans[j++][1] =  freq[i];
                    HALDEBUG(ah, HAL_DEBUG_ANI, "1 set spur %d\n", freq[i]);
                }
            } else if (freq[i] > ATH_HAL_5GHZ_FREQ_MIN &&
                       freq[i] < ATH_HAL_5GHZ_FREQ_MAX)
            {
                /* 5Ghz Spur */
                if (k < AR_EEPROM_MODAL_SPURS) {
                    AH9300(ah)->ath_hal_spur_chans[k++][0] =  freq[i];
                    HALDEBUG(ah, HAL_DEBUG_ANI, "2 set spur %d\n", freq[i]);
                }
            }
        }
    }

    return 0;
}
#endif

#define ar9300_check_op_mode(_opmode) \
    ((_opmode == HAL_M_STA) || (_opmode == HAL_M_IBSS) ||\
     (_opmode == HAL_M_HOSTAP) || (_opmode == HAL_M_MONITOR))
 



#ifndef ATH_NF_PER_CHAN
/*
* To fixed first reset noise floor value not correct issue
* For ART need it to fixed low rate sens too low issue	
*/
static int
First_NFCal(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan, 
    int is_scan, struct ieee80211_channel *chan)
{
    HAL_NFCAL_HIST_FULL *nfh;
    int i, j, k;
    int16_t nfarray[HAL_NUM_NF_READINGS] = {0};
    int is_2g = 0;
    int nf_hist_len;
    int stats = 0;
	
    int16_t nf_buf[HAL_NUM_NF_READINGS];
#define IS(_c, _f)       (((_c)->channel_flags & _f) || 0)


    if ((!is_scan) &&
        chan->ic_freq == AH_PRIVATE(ah)->ah_curchan->ic_freq)
    {
        nfh = &AH_PRIVATE(ah)->nf_cal_hist;
    } else {
        nfh = (HAL_NFCAL_HIST_FULL *) &ichan->nf_cal_hist;
    }

    ar9300_start_nf_cal(ah);
    for (j = 0; j < 10000; j++) {
        if ((OS_REG_READ(ah, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) == 0){
            break;
		}
        OS_DELAY(10);
    }
	if (j < 10000) {
        is_2g = IEEE80211_IS_CHAN_2GHZ(chan);
        ar9300_upload_noise_floor(ah, is_2g, nfarray);

	    if (is_scan) {
			/*
			 * This channel's NF cal info is just a HAL_NFCAL_HIST_SMALL struct
			 * rather than a HAL_NFCAL_HIST_FULL struct.
			 * As long as we only use the first history element of nf_cal_buffer
			 * (nf_cal_buffer[0][0:HAL_NUM_NF_READINGS-1]), we can use
			 * HAL_NFCAL_HIST_SMALL and HAL_NFCAL_HIST_FULL interchangeably.
			 */
            nfh = (HAL_NFCAL_HIST_FULL *) &ichan->nf_cal_hist;
            nf_hist_len = HAL_NF_CAL_HIST_LEN_SMALL;
		} else {
            nfh = &AH_PRIVATE(ah)->nf_cal_hist;
            nf_hist_len = HAL_NF_CAL_HIST_LEN_FULL;
		}

  	    for (i = 0; i < HAL_NUM_NF_READINGS; i ++) {
    		for (k = 0; k < HAL_NF_CAL_HIST_LEN_FULL; k++) {
                nfh->nf_cal_buffer[k][i] = nfarray[i];
            }
            nfh->base.priv_nf[i] = ar9300_limit_nf_range(ah, 
							ar9300_get_nf_hist_mid(ah, nfh, i, nf_hist_len));
  		}


		//ar9300StoreNewNf(ah, ichan, is_scan);

		/*
		 * See if the NF value from the old channel should be
		 * retained when switching to a new channel.
		 * TBD: this may need to be changed, as it wipes out the
		 * purpose of saving NF values for each channel.
		 */
		for (i = 0; i < HAL_NUM_NF_READINGS; i++)
		{
    		if (IEEE80211_IS_CHAN_2GHZ(chan))
    		{     
    			if (nfh->nf_cal_buffer[0][i] <
					AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_2GHZ)
                {
                    ichan->nf_cal_hist.nf_cal_buffer[0][i] =
							AH_PRIVATE(ah)->nf_cal_hist.nf_cal_buffer[0][i];
				}
    		} else {
                if (AR_SREV_AR9580(ah)) {
                    if (nfh->nf_cal_buffer[0][i] <
                        AR_PHY_CCA_NOM_VAL_PEACOCK_5GHZ)  
                    {
                       ichan->nf_cal_hist.nf_cal_buffer[0][i] =
                       AH_PRIVATE(ah)->nf_cal_hist.nf_cal_buffer[0][i];
                    }
                } else {
                   if (nfh->nf_cal_buffer[0][i] <
                       AR_PHY_CCA_NOM_VAL_OSPREY_5GHZ)  
                    {  
                        ichan->nf_cal_hist.nf_cal_buffer[0][i] =
                            AH_PRIVATE(ah)->nf_cal_hist.nf_cal_buffer[0][i];
                     }
                }
            }
        }
		/*
		 * Copy the channel's NF buffer, which may have been modified
		 * just above here, to the full NF history buffer.
		 */
        ar9300_reset_nf_hist_buff(ah, ichan);
        ar9300_get_nf_hist_base(ah, ichan, is_scan, nf_buf);
        ar9300_load_nf(ah, nf_buf);
        /* XXX TODO: handle failure from load_nf */
        stats = 0;
	} else {
        stats = 1;	
	}
#undef IS
    return stats;
}
#endif


/*
 * Places the device in and out of reset and then places sane
 * values in the registers based on EEPROM config, initialization
 * vectors (as determined by the mode), and station configuration
 *
 * b_channel_change is used to preserve DMA/PCU registers across
 * a HW Reset during channel change.
 */
HAL_BOOL
ar9300_reset(struct ath_hal *ah, HAL_OPMODE opmode, struct ieee80211_channel *chan,
    HAL_HT_MACMODE macmode, u_int8_t txchainmask, u_int8_t rxchainmask,
    HAL_HT_EXTPROTSPACING extprotspacing, HAL_BOOL b_channel_change,
    HAL_STATUS *status, int is_scan)
{
#define FAIL(_code)     do { ecode = _code; goto bad; } while (0)
    u_int32_t               save_led_state;
    struct ath_hal_9300     *ahp = AH9300(ah);
    struct ath_hal_private  *ap  = AH_PRIVATE(ah);
    HAL_CHANNEL_INTERNAL    *ichan;
    //const struct ieee80211_channel *curchan = ap->ah_curchan;
#if ATH_SUPPORT_MCI    
    HAL_BOOL                    save_full_sleep = ahp->ah_chip_full_sleep;
#endif    
    u_int32_t               save_def_antenna;
    u_int32_t               mac_sta_id1;
    HAL_STATUS              ecode;
    int                     i, rx_chainmask;
    int                     nf_hist_buff_reset = 0;
    int16_t                 nf_buf[HAL_NUM_NF_READINGS];
#ifdef ATH_FORCE_PPM
    u_int32_t               save_force_val, tmp_reg;
#endif
    u_int8_t                clk_25mhz = AH9300(ah)->clk_25mhz;
    HAL_BOOL                    stopped, cal_ret;
    HAL_BOOL                    apply_last_iqcorr = AH_FALSE;


    if (OS_REG_READ(ah, AR_IER) == AR_IER_ENABLE) {
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "** Reset called with WLAN "
                "interrupt enabled %08x **\n", ar9300_get_interrupts(ah));
    }

    /*
     * Set the status to "ok" by default to cover the cases
     * where we return false without going to "bad"
     */
    HALASSERT(status);
    *status = HAL_OK;
    if ((ah->ah_config.ath_hal_sta_update_tx_pwr_enable)) {
        AH9300(ah)->green_tx_status = HAL_RSSI_TX_POWER_NONE;
    }

#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport &&
        (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)))
    {
        ar9300_mci_2g5g_changed(ah, IEEE80211_IS_CHAN_2GHZ(chan));
    }
#endif

    ahp->ah_ext_prot_spacing = extprotspacing;
    ahp->ah_tx_chainmask = txchainmask & ap->ah_caps.halTxChainMask;
    ahp->ah_rx_chainmask = rxchainmask & ap->ah_caps.halRxChainMask;
    ahp->ah_tx_cal_chainmask = ap->ah_caps.halTxChainMask;
    ahp->ah_rx_cal_chainmask = ap->ah_caps.halRxChainMask;

    /* 
     * Keep the previous optinal txchainmask value
     */

    HALASSERT(ar9300_check_op_mode(opmode));

    OS_MARK(ah, AH_MARK_RESET, b_channel_change);

    /*
     * Map public channel to private.
     */
    ichan = ar9300_check_chan(ah, chan);
    if (ichan == AH_NULL) {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; no mapping\n",
            __func__, chan->ic_freq, chan->ic_flags);
        FAIL(HAL_EINVAL);
    }
    
    ichan->paprd_table_write_done = 0;  /* Clear PAPRD table write flag */
#if 0
    chan->paprd_table_write_done = 0;  /* Clear PAPRD table write flag */
#endif

    if (ar9300_get_power_mode(ah) != HAL_PM_FULL_SLEEP) {
        /* Need to stop RX DMA before reset otherwise chip might hang */
        stopped = ar9300_set_rx_abort(ah, AH_TRUE); /* abort and disable PCU */
        ar9300_set_rx_filter(ah, 0);
        stopped &= ar9300_stop_dma_receive(ah, 0); /* stop and disable RX DMA */
        if (!stopped) {
            /*
             * During the transition from full sleep to reset,
             * recv DMA regs are not available to be read
             */
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                "%s[%d]: ar9300_stop_dma_receive failed\n", __func__, __LINE__);
            b_channel_change = AH_FALSE;
        }
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
            "%s[%d]: Chip is already in full sleep\n", __func__, __LINE__);
    }

#if ATH_SUPPORT_MCI
    if ((AH_PRIVATE(ah)->ah_caps.halMciSupport) &&
        (ahp->ah_mci_bt_state == MCI_BT_CAL_START))
    {
        u_int32_t payload[4] = {0, 0, 0, 0};

        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Stop rx for BT cal.\n", __func__);
        ahp->ah_mci_bt_state = MCI_BT_CAL;

        /*
         * MCIFIX: disable mci interrupt here. This is to avoid SW_MSG_DONE or
         * RX_MSG bits to trigger MCI_INT and lead to mci_intr reentry.
         */
        ar9300_mci_disable_interrupt(ah);

        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Send WLAN_CAL_GRANT\n", __func__);
        MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_GRANT);
        ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, AH_TRUE, AH_FALSE);

        /* Wait BT calibration to be completed for 25ms */
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) %s: BT is calibrating.\n", __func__);
        if (ar9300_mci_wait_for_gpm(ah, MCI_GPM_BT_CAL_DONE, 0, 25000)) {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(MCI) %s: Got BT_CAL_DONE.\n", __func__);
        }
        else {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(MCI) %s: ### BT cal takes too long. Force bt_state to be bt_awake.\n", 
                __func__);
        }
        ahp->ah_mci_bt_state = MCI_BT_AWAKE;
        /* MCIFIX: enable mci interrupt here */
        ar9300_mci_enable_interrupt(ah);

        return AH_TRUE;
    }
#endif

    /* Bring out of sleep mode */
    if (!ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE)) {
        *status = HAL_INV_PMODE;
        return AH_FALSE;
    }

    /* Check the Rx mitigation config again, it might have changed
     * during attach in ath_vap_attach.
     */
    if (ah->ah_config.ath_hal_intr_mitigation_rx != 0) {
        ahp->ah_intr_mitigation_rx = AH_TRUE;
    } else {
        ahp->ah_intr_mitigation_rx = AH_FALSE;
    }

    /*
     * XXX TODO FreeBSD:
     *
     * This is painful because we don't have a non-const channel pointer
     * at this stage.
     *
     * Make sure this gets fixed!
     */
#if 0
    /* Get the value from the previous NF cal and update history buffer */
    if (curchan && (ahp->ah_chip_full_sleep != AH_TRUE)) {

        if(ahp->ah_chip_reset_done){
            ahp->ah_chip_reset_done = 0;
        } else {
        	/*
         	 * is_scan controls updating NF for home channel or off channel.
         	 * Home -> Off, update home channel
         	 * Off -> Home, update off channel
         	 * Home -> Home, uppdate home channel
         	 */
        	if (ap->ah_curchan->channel != chan->channel)
            	ar9300_store_new_nf(ah, curchan, !is_scan);
        	else
            	ar9300_store_new_nf(ah, curchan, is_scan);
        }
    }
#endif

    /*
     * Account for the effect of being in either the 2 GHz or 5 GHz band
     * on the nominal, max allowable, and min allowable noise floor values.
     */
    AH9300(ah)->nfp = IS_CHAN_2GHZ(ichan) ? &ahp->nf_2GHz : &ahp->nf_5GHz;

    /*
     * XXX FreeBSD For now, don't apply the last IQ correction.
     *
     * This should be done when scorpion is enabled on FreeBSD; just be
     * sure to fix this channel match code so it uses net80211 flags
     * instead.
     */
#if 0
    if (AR_SREV_SCORPION(ah) && curchan && (chan->channel == curchan->channel) &&
        ((chan->channel_flags & (CHANNEL_ALL|CHANNEL_HALF|CHANNEL_QUARTER)) ==
         (curchan->channel_flags &
          (CHANNEL_ALL | CHANNEL_HALF | CHANNEL_QUARTER)))) {
            apply_last_iqcorr = AH_TRUE;
    }
#endif
    apply_last_iqcorr = AH_FALSE;

 
#ifndef ATH_NF_PER_CHAN
    /*
     * If there's only one full-size home-channel NF history buffer
     * rather than a full-size NF history buffer per channel, decide
     * whether to (re)initialize the home-channel NF buffer.
     * If this is just a channel change for a scan, or if the channel
     * is not being changed, don't mess up the home channel NF history
     * buffer with NF values from this scanned channel.  If we're
     * changing the home channel to a new channel, reset the home-channel
     * NF history buffer with the most accurate NF known for the new channel.
     */
    if (!is_scan && (!ap->ah_curchan ||
        ap->ah_curchan->ic_freq != chan->ic_freq)) // ||
//        ap->ah_curchan->channel_flags != chan->channel_flags))
    {
        nf_hist_buff_reset = 1;
        ar9300_reset_nf_hist_buff(ah, ichan);
    }
#endif
    /*
     * In case of
     * - offchan scan, or
     * - same channel and RX IQ Cal already available
     * disable RX IQ Cal.
     */
    if (is_scan) {
        ahp->ah_skip_rx_iq_cal = AH_TRUE;
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "Skip RX IQ Cal due to scanning\n");
    } else {
#if 0
        /* XXX FreeBSD: always just do the RX IQ cal */
	/* XXX I think it's just going to speed things up; I don't think it's to avoid chan bugs */
        if (ahp->ah_rx_cal_complete &&
            ahp->ah_rx_cal_chan == ichan->channel &&
            ahp->ah_rx_cal_chan_flag == chan->channel_flags) {
            ahp->ah_skip_rx_iq_cal = AH_TRUE;
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "Skip RX IQ Cal due to same channel with completed RX IQ Cal\n");
        } else
#endif
            ahp->ah_skip_rx_iq_cal = AH_FALSE;
    }

    /* FreeBSD: clear the channel survey data */
    ath_hal_survey_clear(ah);

    /*
     * Fast channel change (Change synthesizer based on channel freq
     * without resetting chip)
     * Don't do it when
     *   - Flag is not set
     *   - Chip is just coming out of full sleep
     *   - Channel to be set is same as current channel
     *   - Channel flags are different, like when moving from 2GHz to 5GHz
     *     channels
     *   - Merlin: Switching in/out of fast clock enabled channels
     *             (not currently coded, since fast clock is enabled
     *             across the 5GHz band
     *             and we already do a full reset when switching in/out
     *             of 5GHz channels)
     */
#if 0
    if (b_channel_change &&
        (ahp->ah_chip_full_sleep != AH_TRUE) &&
        (AH_PRIVATE(ah)->ah_curchan != AH_NULL) &&
        ((chan->channel != AH_PRIVATE(ah)->ah_curchan->channel) &&
        (((CHANNEL_ALL|CHANNEL_HALF|CHANNEL_QUARTER) & chan->channel_flags) == 
        ((CHANNEL_ALL|CHANNEL_HALF|CHANNEL_QUARTER) & AH_PRIVATE(ah)->ah_curchan->channel_flags))))
    {
        if (ar9300_channel_change(ah, chan, ichan, macmode)) {
            chan->channel_flags = ichan->channel_flags;
            chan->priv_flags = ichan->priv_flags;
            AH_PRIVATE(ah)->ah_curchan->ah_channel_time = 0;
            AH_PRIVATE(ah)->ah_curchan->ah_tsf_last = ar9300_get_tsf64(ah);

            /*
             * Load the NF from history buffer of the current channel.
             * NF is slow time-variant, so it is OK to use a historical value.
             */
            ar9300_get_nf_hist_base(ah,
                AH_PRIVATE(ah)->ah_curchan, is_scan, nf_buf);
            ar9300_load_nf(ah, nf_buf);

            /* start NF calibration, without updating BB NF register*/
            ar9300_start_nf_cal(ah);

            /*
             * If channel_change completed and DMA was stopped
             * successfully - skip the rest of reset
             */
            if (AH9300(ah)->ah_dma_stuck != AH_TRUE) {
                ar9300_disable_pll_lock_detect(ah);
#if ATH_SUPPORT_MCI
                if (AH_PRIVATE(ah)->ah_caps.halMciSupport && ahp->ah_mci_ready)
                {
                    ar9300_mci_2g5g_switch(ah, AH_TRUE);
                }
#endif
                return HAL_OK;
            }
         }
    }
#endif /* #if 0 */

#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
        ar9300_mci_disable_interrupt(ah);
        if (ahp->ah_mci_ready && !save_full_sleep) {
            ar9300_mci_mute_bt(ah);
            OS_DELAY(20);
            OS_REG_WRITE(ah, AR_BTCOEX_CTRL, 0);
        }

        ahp->ah_mci_bt_state = MCI_BT_SLEEP;
        ahp->ah_mci_ready = AH_FALSE;
    }
#endif

    AH9300(ah)->ah_dma_stuck = AH_FALSE;
#ifdef ATH_FORCE_PPM
    /* Preserve force ppm state */
    save_force_val =
        OS_REG_READ(ah, AR_PHY_TIMING2) &
        (AR_PHY_TIMING2_USE_FORCE | AR_PHY_TIMING2_FORCE_VAL);
#endif
    /*
     * Preserve the antenna on a channel change
     */
    save_def_antenna = OS_REG_READ(ah, AR_DEF_ANTENNA);
    if (0 == ahp->ah_smartantenna_enable )
    {
        if (save_def_antenna == 0) {
            save_def_antenna = 1;
        }
    } 

    /* Save hardware flag before chip reset clears the register */
    mac_sta_id1 = OS_REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

    /* Save led state from pci config register */
    save_led_state = OS_REG_READ(ah, AR_CFG_LED) &
        (AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL |
        AR_CFG_LED_BLINK_THRESH_SEL | AR_CFG_LED_BLINK_SLOW);

    /* Mark PHY inactive prior to reset, to be undone in ar9300_init_bb () */
    ar9300_mark_phy_inactive(ah);

    if (!ar9300_chip_reset(ah, chan)) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: chip reset failed\n", __func__);
        FAIL(HAL_EIO);
    }

    OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);


    /* Disable JTAG */
    OS_REG_SET_BIT(ah,
        AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL), AR_GPIO_JTAG_DISABLE);

    /*
     * Note that ar9300_init_chain_masks() is called from within
     * ar9300_process_ini() to ensure the swap bit is set before
     * the pdadc table is written.
     */
    ecode = ar9300_process_ini(ah, chan, ichan, macmode);
    if (ecode != HAL_OK) {
        goto bad;
    }
 
    /*
     * Configuring WMAC PLL values for 25/40 MHz 
     */
    if(AR_SREV_WASP(ah) || AR_SREV_HONEYBEE(ah) || AR_SREV_SCORPION(ah) ) {
        if(clk_25mhz) {
            OS_REG_WRITE(ah, AR_RTC_DERIVED_RTC_CLK, (0x17c << 1)); // 32KHz sleep clk
        } else {
            OS_REG_WRITE(ah, AR_RTC_DERIVED_RTC_CLK, (0x261 << 1)); // 32KHz sleep clk
        }
        OS_DELAY(100);
    }

    ahp->ah_immunity_on = AH_FALSE;

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        ahp->tx_iq_cal_enable = OS_REG_READ_FIELD(ah,
                                AR_PHY_TX_IQCAL_CONTROL_0(ah),
                                AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL) ?
                                1 : 0;
    }
    ahp->tx_cl_cal_enable = (OS_REG_READ(ah, AR_PHY_CL_CAL_CTL) &
                                AR_PHY_CL_CAL_ENABLE) ? 1 : 0;

    /* For devices with full HW RIFS Rx support (Sowl/Howl/Merlin, etc),
     * restore register settings from prior to reset.
     */
    if ((AH_PRIVATE(ah)->ah_curchan != AH_NULL) &&
        (ar9300_get_capability(ah, HAL_CAP_LDPCWAR, 0, AH_NULL) == HAL_OK))
    {
        /* Re-program RIFS Rx policy after reset */
        ar9300_set_rifs_delay(ah, ahp->ah_rifs_enabled);
    }

#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
        ar9300_mci_reset(ah, AH_FALSE, IS_CHAN_2GHZ(ichan), save_full_sleep);
    }
#endif

    /* Initialize Management Frame Protection */
    ar9300_init_mfp(ah);

    ahp->ah_immunity_vals[0] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR_LOW,
        AR_PHY_SFCORR_LOW_M1_THRESH_LOW);
    ahp->ah_immunity_vals[1] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR_LOW,
        AR_PHY_SFCORR_LOW_M2_THRESH_LOW);
    ahp->ah_immunity_vals[2] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR,
        AR_PHY_SFCORR_M1_THRESH);
    ahp->ah_immunity_vals[3] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR,
        AR_PHY_SFCORR_M2_THRESH);
    ahp->ah_immunity_vals[4] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR,
        AR_PHY_SFCORR_M2COUNT_THR);
    ahp->ah_immunity_vals[5] = OS_REG_READ_FIELD(ah, AR_PHY_SFCORR_LOW,
        AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW);

    /* Write delta slope for OFDM enabled modes (A, G, Turbo) */
    if (IEEE80211_IS_CHAN_OFDM(chan) || IEEE80211_IS_CHAN_HT(chan)) {
        ar9300_set_delta_slope(ah, chan);
    }

    ar9300_spur_mitigate(ah, chan);
    if (!ar9300_eeprom_set_board_values(ah, chan)) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM,
            "%s: error setting board options\n", __func__);
        FAIL(HAL_EIO);
    }

#ifdef ATH_HAL_WAR_REG16284_APH128
    /* temp work around, will be removed. */
    if (AR_SREV_WASP(ah)) {
        OS_REG_WRITE(ah, 0x16284, 0x1553e000); 
    }
#endif

    OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

    OS_REG_WRITE(ah, AR_STA_ID0, LE_READ_4(ahp->ah_macaddr));
    OS_REG_WRITE(ah, AR_STA_ID1, LE_READ_2(ahp->ah_macaddr + 4)
            | mac_sta_id1
            | AR_STA_ID1_RTS_USE_DEF
            | (ah->ah_config.ath_hal_6mb_ack ? AR_STA_ID1_ACKCTS_6MB : 0)
            | ahp->ah_sta_id1_defaults
    );
    ar9300_set_operating_mode(ah, opmode);

    /* Set Venice BSSID mask according to current state */
    OS_REG_WRITE(ah, AR_BSSMSKL, LE_READ_4(ahp->ah_bssid_mask));
    OS_REG_WRITE(ah, AR_BSSMSKU, LE_READ_2(ahp->ah_bssid_mask + 4));

    /* Restore previous antenna */
    OS_REG_WRITE(ah, AR_DEF_ANTENNA, save_def_antenna);
#ifdef ATH_FORCE_PPM
    /* Restore force ppm state */
    tmp_reg = OS_REG_READ(ah, AR_PHY_TIMING2) &
        ~(AR_PHY_TIMING2_USE_FORCE | AR_PHY_TIMING2_FORCE_VAL);
    OS_REG_WRITE(ah, AR_PHY_TIMING2, tmp_reg | save_force_val);
#endif

    /* then our BSSID and assocID */
    OS_REG_WRITE(ah, AR_BSS_ID0, LE_READ_4(ahp->ah_bssid));
    OS_REG_WRITE(ah, AR_BSS_ID1,
        LE_READ_2(ahp->ah_bssid + 4) |
        ((ahp->ah_assoc_id & 0x3fff) << AR_BSS_ID1_AID_S));

    OS_REG_WRITE(ah, AR_ISR, ~0); /* cleared on write */

    OS_REG_RMW_FIELD(ah, AR_RSSI_THR, AR_RSSI_THR_BM_THR, INIT_RSSI_THR);

    /* HW beacon processing */
    /*
     * XXX what happens if I just leave filter_interval=0?
     * it stays disabled?
     */
    OS_REG_RMW_FIELD(ah, AR_RSSI_THR, AR_RSSI_BCN_WEIGHT,
            INIT_RSSI_BEACON_WEIGHT);
    OS_REG_SET_BIT(ah, AR_HWBCNPROC1, AR_HWBCNPROC1_CRC_ENABLE |
            AR_HWBCNPROC1_EXCLUDE_TIM_ELM);
    if (ah->ah_config.ath_hal_beacon_filter_interval) {
        OS_REG_RMW_FIELD(ah, AR_HWBCNPROC2, AR_HWBCNPROC2_FILTER_INTERVAL,
                ah->ah_config.ath_hal_beacon_filter_interval);
        OS_REG_SET_BIT(ah, AR_HWBCNPROC2,
                AR_HWBCNPROC2_FILTER_INTERVAL_ENABLE);
    }


    /*
     * Set Channel now modifies bank 6 parameters for FOWL workaround
     * to force rf_pwd_icsyndiv bias current as function of synth
     * frequency.Thus must be called after ar9300_process_ini() to ensure
     * analog register cache is valid.
     */
    if (!ahp->ah_rf_hal.set_channel(ah, chan)) {
        FAIL(HAL_EIO);
    }


    OS_MARK(ah, AH_MARK_RESET_LINE, __LINE__);

    /* Set 1:1 QCU to DCU mapping for all queues */
    for (i = 0; i < AR_NUM_DCU; i++) {
        OS_REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);
    }

    ahp->ah_intr_txqs = 0;
    for (i = 0; i < AH_PRIVATE(ah)->ah_caps.halTotalQueues; i++) {
        ar9300_reset_tx_queue(ah, i);
    }

    ar9300_init_interrupt_masks(ah, opmode);

    /* Reset ier reference count to disabled */
//    OS_ATOMIC_SET(&ahp->ah_ier_ref_count, 1); 
    if (ath_hal_isrfkillenabled(ah)) {
        ar9300_enable_rf_kill(ah);
    }

    /* must be called AFTER ini is processed */
    ar9300_ani_init_defaults(ah, macmode);

    ar9300_init_qos(ah);

    ar9300_init_user_settings(ah);


    AH_PRIVATE(ah)->ah_opmode = opmode; /* record operating mode */

    OS_MARK(ah, AH_MARK_RESET_DONE, 0);

    /*
     * disable seq number generation in hw
     */
    OS_REG_WRITE(ah, AR_STA_ID1,
        OS_REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PRESERVE_SEQNUM);

    ar9300_set_dma(ah);

    /*
     * program OBS bus to see MAC interrupts
     */
#if ATH_SUPPORT_MCI
    if (!AH_PRIVATE(ah)->ah_caps.halMciSupport) {
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_OBS), 8);
    }
#else
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_OBS), 8);
#endif


    /* enabling AR_GTTM_IGNORE_IDLE in GTTM register so that
       GTT timer will not increment if the channel idle indicates 
       the air is busy or NAV is still counting down */
    OS_REG_WRITE(ah, AR_GTTM, AR_GTTM_IGNORE_IDLE);

    /*
     * GTT debug mode setting
     */
    /*
    OS_REG_WRITE(ah, 0x64, 0x00320000);
    OS_REG_WRITE(ah, 0x68, 7);
    OS_REG_WRITE(ah, 0x4080, 0xC);
     */
    /*
     * Disable general interrupt mitigation by setting MIRT = 0x0
     * Rx and tx interrupt mitigation are conditionally enabled below.
     */
    OS_REG_WRITE(ah, AR_MIRT, 0);
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
#ifndef HOST_OFFLOAD
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, AH_RIMT_LAST_MICROSEC);
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, AH_RIMT_FIRST_MICROSEC);
#else
        /* lower mitigation level to reduce latency for offload arch. */
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, 
            (AH_RIMT_LAST_MICROSEC >> 2));
        OS_REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, 
            (AH_RIMT_FIRST_MICROSEC >> 2));
#endif
    }

    if (ahp->ah_intr_mitigation_tx) {
        /*
         * Enable Interrupt Mitigation for Tx.
         * If no build-specific limits for the tx interrupt mitigation
         * timer have been specified, use the values preferred for
         * the carrier group's products.
         */
        #ifndef AH_TIMT_LAST
            #define AH_TIMT_LAST_MICROSEC 300
        #endif
        #ifndef AH_TIMT_FIRST
            #define AH_TIMT_FIRST_MICROSEC 750
        #endif
        OS_REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_LAST, AH_TIMT_LAST_MICROSEC);
        OS_REG_RMW_FIELD(ah, AR_TIMT, AR_TIMT_FIRST, AH_TIMT_FIRST_MICROSEC);
    }

    rx_chainmask = ahp->ah_rx_chainmask;

    OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
    OS_REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);

    ar9300_init_bb(ah, chan);

    /* BB Step 7: Calibration */
    /*
     * Only kick off calibration not on offchan.
     * If coming back from offchan, restore prevous Cal results
     * since chip reset will clear existings.
     */
    if (!ahp->ah_skip_rx_iq_cal) {
        int i;
        /* clear existing RX cal data */
        for (i=0; i<AR9300_MAX_CHAINS; i++)
            ahp->ah_rx_cal_corr[i] = 0;

        ahp->ah_rx_cal_complete = AH_FALSE;
//        ahp->ah_rx_cal_chan = chan->channel;
//        ahp->ah_rx_cal_chan_flag = ichan->channel_flags;
        ahp->ah_rx_cal_chan = 0;
        ahp->ah_rx_cal_chan_flag = 0; /* XXX FreeBSD */
    }
    ar9300_invalidate_saved_cals(ah, ichan);
    cal_ret = ar9300_init_cal(ah, chan, AH_FALSE, apply_last_iqcorr);

#if ATH_SUPPORT_MCI
    if (AH_PRIVATE(ah)->ah_caps.halMciSupport && ahp->ah_mci_ready) {
        if (IS_CHAN_2GHZ(ichan) &&
            (ahp->ah_mci_bt_state == MCI_BT_SLEEP))
        {
            if (ar9300_mci_check_int(ah, AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET) ||
                ar9300_mci_check_int(ah, AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE))
            {
                /* 
                 * BT is sleeping. Check if BT wakes up duing WLAN 
                 * calibration. If BT wakes up during WLAN calibration, need
                 * to go through all message exchanges again and recal.
                 */
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) ### %s: BT wakes up during WLAN calibration.\n",
                    __func__);
                OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
                        AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET |
                        AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE);
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) send REMOTE_RESET\n");
                ar9300_mci_remote_reset(ah, AH_TRUE);
                ar9300_mci_send_sys_waking(ah, AH_TRUE);
                OS_DELAY(1);
                if (IS_CHAN_2GHZ(ichan)) {
                    ar9300_mci_send_lna_transfer(ah, AH_TRUE);
                }
                ahp->ah_mci_bt_state = MCI_BT_AWAKE;

                /* Redo calibration */
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Re-calibrate.\n",
                    __func__);
                ar9300_invalidate_saved_cals(ah, ichan);
                cal_ret = ar9300_init_cal(ah, chan, AH_FALSE, apply_last_iqcorr);
            }
        }
        ar9300_mci_enable_interrupt(ah);
    }
#endif

    if (!cal_ret) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: Init Cal Failed\n", __func__);
        FAIL(HAL_ESELFTEST);
    }

    ar9300_init_txbf(ah);
#if 0
    /*
     * WAR for owl 1.0 - restore chain mask for 2-chain cfgs after cal
     */
    rx_chainmask = ahp->ah_rx_chainmask;
    if ((rx_chainmask == 0x5) || (rx_chainmask == 0x3)) {
        OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
        OS_REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);
    }
#endif

    /* Restore previous led state */
    OS_REG_WRITE(ah, AR_CFG_LED, save_led_state | AR_CFG_SCLK_32KHZ);

#if ATH_BT_COEX
    if (ahp->ah_bt_coex_config_type != HAL_BT_COEX_CFG_NONE) {
        ar9300_init_bt_coex(ah);

#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport && ahp->ah_mci_ready) {
            /* Check BT state again to make sure it's not changed. */
            ar9300_mci_sync_bt_state(ah);
            ar9300_mci_2g5g_switch(ah, AH_TRUE);

            if ((ahp->ah_mci_bt_state == MCI_BT_AWAKE) &&
                (ahp->ah_mci_query_bt == AH_TRUE))
            {
                ahp->ah_mci_need_flush_btinfo = AH_TRUE;
            }
        }
#endif
    }
#endif

    /* Start TSF2 for generic timer 8-15. */
    ar9300_start_tsf2(ah);

    /* MIMO Power save setting */
    if (ar9300_get_capability(ah, HAL_CAP_DYNAMIC_SMPS, 0, AH_NULL) == HAL_OK) {
        ar9300_set_sm_power_mode(ah, ahp->ah_sm_power_mode);
    }

    /*
     * For big endian systems turn on swapping for descriptors
     */
#if AH_BYTE_ORDER == AH_BIG_ENDIAN
    if (AR_SREV_HORNET(ah) || AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah)) {
        OS_REG_RMW(ah, AR_CFG, AR_CFG_SWTB | AR_CFG_SWRB, 0);
    } else {
        ar9300_init_cfg_reg(ah);
    }
#endif

    if ( AR_SREV_OSPREY(ah) || AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah) || AR_SREV_HONEYBEE(ah) ) {
        OS_REG_RMW(ah, AR_CFG_LED, AR_CFG_LED_ASSOC_CTL, AR_CFG_LED_ASSOC_CTL);
    }

#if !(defined(ART_BUILD)) && defined(ATH_SUPPORT_LED)
#define REG_WRITE(_reg, _val)   *((volatile u_int32_t *)(_reg)) = (_val);
#define REG_READ(_reg)          *((volatile u_int32_t *)(_reg))
#define ATH_GPIO_OUT_FUNCTION3  0xB8040038
#define ATH_GPIO_OE             0xB8040000
    if ( AR_SREV_WASP(ah)) {
        if (IS_CHAN_2GHZ((AH_PRIVATE(ah)->ah_curchan))) {
            REG_WRITE(ATH_GPIO_OUT_FUNCTION3, ( REG_READ(ATH_GPIO_OUT_FUNCTION3) & (~(0xff << 8))) | (0x33 << 8) );
            REG_WRITE(ATH_GPIO_OE, ( REG_READ(ATH_GPIO_OE) & (~(0x1 << 13) )));
        }
        else {

            /* Disable 2G WLAN LED. During ath_open, reset function is called even before channel is set. 
            So 2GHz is taken as default and it also blinks. Hence 
            to avoid both from blinking, disable 2G led while in 5G mode */

            REG_WRITE(ATH_GPIO_OE, ( REG_READ(ATH_GPIO_OE) | (1 << 13) ));
            REG_WRITE(ATH_GPIO_OUT_FUNCTION3, ( REG_READ(ATH_GPIO_OUT_FUNCTION3) & (~(0xff))) | (0x33) );
            REG_WRITE(ATH_GPIO_OE, ( REG_READ(ATH_GPIO_OE) & (~(0x1 << 12) )));		
        }
 
    }
    else if (AR_SREV_SCORPION(ah)) {
        if (IS_CHAN_2GHZ((AH_PRIVATE(ah)->ah_curchan))) {
            REG_WRITE(ATH_GPIO_OUT_FUNCTION3, ( REG_READ(ATH_GPIO_OUT_FUNCTION3) & (~(0xff << 8))) | (0x2F << 8) );
    	    REG_WRITE(ATH_GPIO_OE, (( REG_READ(ATH_GPIO_OE) & (~(0x1 << 13) )) | (0x1 << 12)));
        } else if (IS_CHAN_5GHZ((AH_PRIVATE(ah)->ah_curchan))) {
            REG_WRITE(ATH_GPIO_OUT_FUNCTION3, ( REG_READ(ATH_GPIO_OUT_FUNCTION3) & (~(0xff))) | (0x2F) );
    	    REG_WRITE(ATH_GPIO_OE, (( REG_READ(ATH_GPIO_OE) & (~(0x1 << 12) )) | (0x1 << 13)));
        }
    }
    else if (AR_SREV_HONEYBEE(ah)) {
            REG_WRITE(ATH_GPIO_OUT_FUNCTION3, ( REG_READ(ATH_GPIO_OUT_FUNCTION3) & (~(0xff))) | (0x32) );
            REG_WRITE(ATH_GPIO_OE, (( REG_READ(ATH_GPIO_OE) & (~(0x1 << 12) ))));
    }
#undef REG_READ
#undef REG_WRITE
#endif

    /* XXX FreeBSD What's this? -adrian */
#if 0
    chan->channel_flags = ichan->channel_flags;
    chan->priv_flags = ichan->priv_flags;
#endif

#if FIX_NOISE_FLOOR
    /* XXX FreeBSD is ichan appropariate? It was curchan.. */
    ar9300_get_nf_hist_base(ah, ichan, is_scan, nf_buf);
    ar9300_load_nf(ah, nf_buf);
    /* XXX TODO: handle NF load failure */
    if (nf_hist_buff_reset == 1)    
    {
        nf_hist_buff_reset = 0;
    #ifndef ATH_NF_PER_CHAN
	    if (First_NFCal(ah, ichan, is_scan, chan)){
            if (ahp->ah_skip_rx_iq_cal && !is_scan) {
                /* restore RX Cal result if existing */
                ar9300_rx_iq_cal_restore(ah);
                ahp->ah_skip_rx_iq_cal = AH_FALSE;
            }
        }
    #endif /* ATH_NF_PER_CHAN */
    } 
    else{
        ar9300_start_nf_cal(ah); 
    }
#endif

#ifdef AH_SUPPORT_AR9300
    /* BB Panic Watchdog */
    if (ar9300_get_capability(ah, HAL_CAP_BB_PANIC_WATCHDOG, 0, AH_NULL) ==
        HAL_OK)
    {
        ar9300_config_bb_panic_watchdog(ah);
    }
#endif

    /* While receiving unsupported rate frame receive state machine
     * gets into a state 0xb and if phy_restart happens when rx
     * state machine is in 0xb state, BB would go hang, if we
     * see 0xb state after first bb panic, make sure that we
     * disable the phy_restart.
     * 
     * There may be multiple panics, make sure that we always do
     * this if we see this panic at least once. This is required
     * because reset seems to be writing from INI file.
     */
    if ((ar9300_get_capability(ah, HAL_CAP_PHYRESTART_CLR_WAR, 0, AH_NULL)
         == HAL_OK) && (((MS((AH9300(ah)->ah_bb_panic_last_status),
                AR_PHY_BB_WD_RX_OFDM_SM)) == 0xb) ||
            AH9300(ah)->ah_phyrestart_disabled) )
    {
        ar9300_disable_phy_restart(ah, 1);
    }



    ahp->ah_radar1 = MS(OS_REG_READ(ah, AR_PHY_RADAR_1),
                        AR_PHY_RADAR_1_CF_BIN_THRESH);
    ahp->ah_dc_offset = MS(OS_REG_READ(ah, AR_PHY_TIMING2),
                        AR_PHY_TIMING2_DC_OFFSET);
    ahp->ah_disable_cck = MS(OS_REG_READ(ah, AR_PHY_MODE),
                        AR_PHY_MODE_DISABLE_CCK);

    if (AH9300(ah)->ah_enable_keysearch_always) {
        ar9300_enable_keysearch_always(ah, 1);
    }

#if ATH_LOW_POWER_ENABLE
#define REG_WRITE(_reg, _val)   *((volatile u_int32_t *)(_reg)) = (_val)
#define REG_READ(_reg)      *((volatile u_int32_t *)(_reg))
    if (AR_SREV_OSPREY(ah)) {
        REG_WRITE(0xb4000080, REG_READ(0xb4000080) | 3);
        OS_REG_WRITE(ah, AR_RTC_RESET, 1);
        OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL),
                        AR_PCIE_PM_CTRL_ENA);
        OS_REG_SET_BIT(ah, AR_HOSTIF_REG(ah, AR_SPARE), 0xffffffff);
    }
#undef REG_READ
#undef REG_WRITE
#endif  /* ATH_LOW_POWER_ENABLE */

    ar9300_disable_pll_lock_detect(ah);

    /* H/W Green TX */
    ar9300_control_signals_for_green_tx_mode(ah);
    /* Smart Antenna, only for 5GHz on Scropion */
    if (IEEE80211_IS_CHAN_2GHZ((AH_PRIVATE(ah)->ah_curchan)) && AR_SREV_SCORPION(ah)) {
        ahp->ah_smartantenna_enable = 0;
    }

    ar9300_set_smart_antenna(ah, ahp->ah_smartantenna_enable);

    if (AR_SREV_APHRODITE(ah) && ahp->ah_lna_div_use_bt_ant_enable)
        OS_REG_SET_BIT(ah, AR_BTCOEX_WL_LNADIV, AR_BTCOEX_WL_LNADIV_FORCE_ON);

    if (ahp->ah_skip_rx_iq_cal && !is_scan) {
        /* restore RX Cal result if existing */
        ar9300_rx_iq_cal_restore(ah);
        ahp->ah_skip_rx_iq_cal = AH_FALSE;
    }


    return AH_TRUE;
bad:
    OS_MARK(ah, AH_MARK_RESET_DONE, ecode);
    *status = ecode;

    if (ahp->ah_skip_rx_iq_cal && !is_scan) {
        /* restore RX Cal result if existing */
        ar9300_rx_iq_cal_restore(ah);
        ahp->ah_skip_rx_iq_cal = AH_FALSE;
    }

    return AH_FALSE;
#undef FAIL
}

void
ar9300_green_ap_ps_on_off( struct ath_hal *ah, u_int16_t on_off)
{
    /* Set/reset the ps flag */
    AH9300(ah)->green_ap_ps_on = !!on_off;
}

/*
 * This function returns 1, where it is possible to do
 * single-chain power save.
 */
u_int16_t
ar9300_is_single_ant_power_save_possible(struct ath_hal *ah)
{
    return AH_TRUE;
}

/* To avoid compilation warnings. Functions not used when EMULATION. */
/*
 * ar9300_find_mag_approx()
 */
static int32_t
ar9300_find_mag_approx(struct ath_hal *ah, int32_t in_re, int32_t in_im)
{
    int32_t abs_i = abs(in_re);
    int32_t abs_q = abs(in_im);
    int32_t max_abs, min_abs;

    if (abs_i > abs_q) {
        max_abs = abs_i;
        min_abs = abs_q;
    } else {
        max_abs = abs_q;
        min_abs = abs_i; 
    }

    return (max_abs - (max_abs / 32) + (min_abs / 8) + (min_abs / 4));
}

/* 
 * ar9300_solve_iq_cal()       
 * solve 4x4 linear equation used in loopback iq cal.
 */
static HAL_BOOL
ar9300_solve_iq_cal(
    struct ath_hal *ah,
    int32_t sin_2phi_1,
    int32_t cos_2phi_1,
    int32_t sin_2phi_2,
    int32_t cos_2phi_2,
    int32_t mag_a0_d0,
    int32_t phs_a0_d0,
    int32_t mag_a1_d0,
    int32_t phs_a1_d0,
    int32_t solved_eq[])
{
    int32_t f1 = cos_2phi_1 - cos_2phi_2;
    int32_t f3 = sin_2phi_1 - sin_2phi_2;
    int32_t f2;
    int32_t mag_tx, phs_tx, mag_rx, phs_rx;
    const int32_t result_shift = 1 << 15;

    f2 = (((int64_t)f1 * (int64_t)f1) / result_shift) + (((int64_t)f3 * (int64_t)f3) / result_shift);

    if (0 == f2) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s: Divide by 0(%d).\n",
            __func__, __LINE__);
        return AH_FALSE;
    }

    /* magnitude mismatch, tx */
    mag_tx = f1 * (mag_a0_d0  - mag_a1_d0) + f3 * (phs_a0_d0 - phs_a1_d0);
    /* phase mismatch, tx */
    phs_tx = f3 * (-mag_a0_d0 + mag_a1_d0) + f1 * (phs_a0_d0 - phs_a1_d0);

    mag_tx = (mag_tx / f2);
    phs_tx = (phs_tx / f2);

    /* magnitude mismatch, rx */
    mag_rx =
        mag_a0_d0 - (cos_2phi_1 * mag_tx + sin_2phi_1 * phs_tx) / result_shift;
    /* phase mismatch, rx */
    phs_rx =
        phs_a0_d0 + (sin_2phi_1 * mag_tx - cos_2phi_1 * phs_tx) / result_shift;

    solved_eq[0] = mag_tx;
    solved_eq[1] = phs_tx;
    solved_eq[2] = mag_rx;
    solved_eq[3] = phs_rx;

    return AH_TRUE;
}

/*
 * ar9300_calc_iq_corr()
 */
static HAL_BOOL
ar9300_calc_iq_corr(struct ath_hal *ah, int32_t chain_idx,
    const int32_t iq_res[], int32_t iqc_coeff[])
{
    int32_t i2_m_q2_a0_d0, i2_p_q2_a0_d0, iq_corr_a0_d0;
    int32_t i2_m_q2_a0_d1, i2_p_q2_a0_d1, iq_corr_a0_d1;
    int32_t i2_m_q2_a1_d0, i2_p_q2_a1_d0, iq_corr_a1_d0;
    int32_t i2_m_q2_a1_d1, i2_p_q2_a1_d1, iq_corr_a1_d1;
    int32_t mag_a0_d0, mag_a1_d0, mag_a0_d1, mag_a1_d1;
    int32_t phs_a0_d0, phs_a1_d0, phs_a0_d1, phs_a1_d1;
    int32_t sin_2phi_1, cos_2phi_1, sin_2phi_2, cos_2phi_2;
    int32_t mag_tx, phs_tx, mag_rx, phs_rx;
    int32_t solved_eq[4], mag_corr_tx, phs_corr_tx, mag_corr_rx, phs_corr_rx;
    int32_t q_q_coff, q_i_coff;
    const int32_t res_scale = 1 << 15;
    const int32_t delpt_shift = 1 << 8;
    int32_t mag1, mag2;

    i2_m_q2_a0_d0 = iq_res[0] & 0xfff;
    i2_p_q2_a0_d0 = (iq_res[0] >> 12) & 0xfff;
    iq_corr_a0_d0 = ((iq_res[0] >> 24) & 0xff) + ((iq_res[1] & 0xf) << 8);

    if (i2_m_q2_a0_d0 > 0x800)  {
        i2_m_q2_a0_d0 = -((0xfff - i2_m_q2_a0_d0) + 1);
    }
    if (iq_corr_a0_d0 > 0x800)  {
        iq_corr_a0_d0 = -((0xfff - iq_corr_a0_d0) + 1);
    }

    i2_m_q2_a0_d1 = (iq_res[1] >> 4) & 0xfff;
    i2_p_q2_a0_d1 = (iq_res[2] & 0xfff); 
    iq_corr_a0_d1 = (iq_res[2] >> 12) & 0xfff;

    if (i2_m_q2_a0_d1 > 0x800)  {
        i2_m_q2_a0_d1 = -((0xfff - i2_m_q2_a0_d1) + 1);
    }
    if (iq_corr_a0_d1 > 0x800)  {
        iq_corr_a0_d1 = -((0xfff - iq_corr_a0_d1) + 1);
    }

    i2_m_q2_a1_d0 = ((iq_res[2] >> 24) & 0xff) + ((iq_res[3] & 0xf) << 8);
    i2_p_q2_a1_d0 = (iq_res[3] >> 4) & 0xfff; 
    iq_corr_a1_d0 = iq_res[4] & 0xfff;

    if (i2_m_q2_a1_d0 > 0x800)  {
        i2_m_q2_a1_d0 = -((0xfff - i2_m_q2_a1_d0) + 1);
    }
    if (iq_corr_a1_d0 > 0x800)  {
        iq_corr_a1_d0 = -((0xfff - iq_corr_a1_d0) + 1);
    }

    i2_m_q2_a1_d1 = (iq_res[4] >> 12) & 0xfff;
    i2_p_q2_a1_d1 = ((iq_res[4] >> 24) & 0xff) + ((iq_res[5] & 0xf) << 8); 
    iq_corr_a1_d1 = (iq_res[5] >> 4) & 0xfff;

    if (i2_m_q2_a1_d1 > 0x800)  {
        i2_m_q2_a1_d1 = -((0xfff - i2_m_q2_a1_d1) + 1);
    }
    if (iq_corr_a1_d1 > 0x800)  {
        iq_corr_a1_d1 = -((0xfff - iq_corr_a1_d1) + 1);
    }

    if ((i2_p_q2_a0_d0 == 0) ||
        (i2_p_q2_a0_d1 == 0) ||
        (i2_p_q2_a1_d0 == 0) ||
        (i2_p_q2_a1_d1 == 0)) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Divide by 0(%d):\na0_d0=%d\na0_d1=%d\na2_d0=%d\na1_d1=%d\n",
            __func__, __LINE__,
            i2_p_q2_a0_d0, i2_p_q2_a0_d1, i2_p_q2_a1_d0, i2_p_q2_a1_d1);
        return AH_FALSE;
    }

    if ((i2_p_q2_a0_d0 <= 1024) || (i2_p_q2_a0_d0 > 2047) ||
            (i2_p_q2_a1_d0 < 0) || (i2_p_q2_a1_d1 < 0) ||
            (i2_p_q2_a0_d0 <= i2_m_q2_a0_d0) ||
            (i2_p_q2_a0_d0 <= iq_corr_a0_d0) ||
            (i2_p_q2_a0_d1 <= i2_m_q2_a0_d1) ||
            (i2_p_q2_a0_d1 <= iq_corr_a0_d1) ||
            (i2_p_q2_a1_d0 <= i2_m_q2_a1_d0) ||
            (i2_p_q2_a1_d0 <= iq_corr_a1_d0) ||
            (i2_p_q2_a1_d1 <= i2_m_q2_a1_d1) ||
            (i2_p_q2_a1_d1 <= iq_corr_a1_d1)) {
        return AH_FALSE;
    }

    mag_a0_d0 = (i2_m_q2_a0_d0 * res_scale) / i2_p_q2_a0_d0;
    phs_a0_d0 = (iq_corr_a0_d0 * res_scale) / i2_p_q2_a0_d0;

    mag_a0_d1 = (i2_m_q2_a0_d1 * res_scale) / i2_p_q2_a0_d1;
    phs_a0_d1 = (iq_corr_a0_d1 * res_scale) / i2_p_q2_a0_d1;

    mag_a1_d0 = (i2_m_q2_a1_d0 * res_scale) / i2_p_q2_a1_d0;
    phs_a1_d0 = (iq_corr_a1_d0 * res_scale) / i2_p_q2_a1_d0;

    mag_a1_d1 = (i2_m_q2_a1_d1 * res_scale) / i2_p_q2_a1_d1;
    phs_a1_d1 = (iq_corr_a1_d1 * res_scale) / i2_p_q2_a1_d1;

    /* without analog phase shift */
    sin_2phi_1 = (((mag_a0_d0 - mag_a0_d1) * delpt_shift) / DELPT);
    /* without analog phase shift */
    cos_2phi_1 = (((phs_a0_d1 - phs_a0_d0) * delpt_shift) / DELPT);
    /* with  analog phase shift */
    sin_2phi_2 = (((mag_a1_d0 - mag_a1_d1) * delpt_shift) / DELPT);
    /* with analog phase shift */
    cos_2phi_2 = (((phs_a1_d1 - phs_a1_d0) * delpt_shift) / DELPT);

    /* force sin^2 + cos^2 = 1; */
    /* find magnitude by approximation */
    mag1 = ar9300_find_mag_approx(ah, cos_2phi_1, sin_2phi_1);
    mag2 = ar9300_find_mag_approx(ah, cos_2phi_2, sin_2phi_2);

    if ((mag1 == 0) || (mag2 == 0)) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Divide by 0(%d): mag1=%d, mag2=%d\n",
            __func__, __LINE__, mag1, mag2);
        return AH_FALSE;
    }

    /* normalization sin and cos by mag */
    sin_2phi_1 = (sin_2phi_1 * res_scale / mag1);
    cos_2phi_1 = (cos_2phi_1 * res_scale / mag1);
    sin_2phi_2 = (sin_2phi_2 * res_scale / mag2);
    cos_2phi_2 = (cos_2phi_2 * res_scale / mag2);

    /* calculate IQ mismatch */
    if (AH_FALSE == ar9300_solve_iq_cal(ah,
            sin_2phi_1, cos_2phi_1, sin_2phi_2, cos_2phi_2, mag_a0_d0,
            phs_a0_d0, mag_a1_d0, phs_a1_d0, solved_eq))
    {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Call to ar9300_solve_iq_cal failed.\n", __func__);
        return AH_FALSE;
    }
   
    mag_tx = solved_eq[0];
    phs_tx = solved_eq[1];
    mag_rx = solved_eq[2];
    phs_rx = solved_eq[3];

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: chain %d: mag mismatch=%d phase mismatch=%d\n",
        __func__, chain_idx, mag_tx / res_scale, phs_tx / res_scale);
  
    if (res_scale == mag_tx) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Divide by 0(%d): mag_tx=%d, res_scale=%d\n",
            __func__, __LINE__, mag_tx, res_scale);
        return AH_FALSE;
    }

    /* calculate and quantize Tx IQ correction factor */
    mag_corr_tx = (mag_tx * res_scale) / (res_scale - mag_tx);
    phs_corr_tx = -phs_tx;

    q_q_coff = (mag_corr_tx * 128 / res_scale);
    q_i_coff = (phs_corr_tx * 256 / res_scale);

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: tx chain %d: mag corr=%d  phase corr=%d\n",
        __func__, chain_idx, q_q_coff, q_i_coff);

    if (q_i_coff < -63) {
        q_i_coff = -63;
    }
    if (q_i_coff > 63) {
        q_i_coff = 63;
    }
    if (q_q_coff < -63) {
        q_q_coff = -63;
    }
    if (q_q_coff > 63) {
        q_q_coff = 63;
    }

    iqc_coeff[0] = (q_q_coff * 128) + (0x7f & q_i_coff);

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s: tx chain %d: iq corr coeff=%x\n",
        __func__, chain_idx, iqc_coeff[0]);  

    if (-mag_rx == res_scale) {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Divide by 0(%d): mag_rx=%d, res_scale=%d\n",
            __func__, __LINE__, mag_rx, res_scale);
        return AH_FALSE;
    }

    /* calculate and quantize Rx IQ correction factors */
    mag_corr_rx = (-mag_rx * res_scale) / (res_scale + mag_rx);
    phs_corr_rx = -phs_rx;

    q_q_coff = (mag_corr_rx * 128 / res_scale);
    q_i_coff = (phs_corr_rx * 256 / res_scale);

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s: rx chain %d: mag corr=%d  phase corr=%d\n",
        __func__, chain_idx, q_q_coff, q_i_coff);

    if (q_i_coff < -63) {
        q_i_coff = -63;
    }
    if (q_i_coff > 63) {
        q_i_coff = 63;
    }
    if (q_q_coff < -63) {
        q_q_coff = -63;
    }
    if (q_q_coff > 63) {
        q_q_coff = 63;
    }
   
    iqc_coeff[1] = (q_q_coff * 128) + (0x7f & q_i_coff);

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s: rx chain %d: iq corr coeff=%x\n",
        __func__, chain_idx, iqc_coeff[1]);  

    return AH_TRUE;
}

#define MAX_MAG_DELTA 11 //maximum magnitude mismatch delta across gains
#define MAX_PHS_DELTA 10 //maximum phase mismatch delta across gains
#define ABS(x) ((x) >= 0 ? (x) : (-(x)))

    u_int32_t tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS] = {
    {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
    {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
        AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
        AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
    };

static void
ar9300_tx_iq_cal_outlier_detection(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan, u_int32_t num_chains,
    struct coeff_t *coeff, HAL_BOOL is_cal_reusable)
{
    int nmeasurement, ch_idx, im;
    int32_t magnitude, phase;
    int32_t magnitude_max, phase_max;
    int32_t magnitude_min, phase_min;

    int32_t magnitude_max_idx, phase_max_idx;
    int32_t magnitude_min_idx, phase_min_idx;

    int32_t magnitude_avg, phase_avg;
    int32_t outlier_mag_idx = 0;
    int32_t outlier_phs_idx = 0;


    if (AR_SREV_POSEIDON(ah)) {
        HALASSERT(num_chains == 0x1);

        tx_corr_coeff[0][0] = AR_PHY_TX_IQCAL_CORR_COEFF_01_B0_POSEIDON;
        tx_corr_coeff[1][0] = AR_PHY_TX_IQCAL_CORR_COEFF_01_B0_POSEIDON;
        tx_corr_coeff[2][0] = AR_PHY_TX_IQCAL_CORR_COEFF_23_B0_POSEIDON;
        tx_corr_coeff[3][0] = AR_PHY_TX_IQCAL_CORR_COEFF_23_B0_POSEIDON;
        tx_corr_coeff[4][0] = AR_PHY_TX_IQCAL_CORR_COEFF_45_B0_POSEIDON;
        tx_corr_coeff[5][0] = AR_PHY_TX_IQCAL_CORR_COEFF_45_B0_POSEIDON;
        tx_corr_coeff[6][0] = AR_PHY_TX_IQCAL_CORR_COEFF_67_B0_POSEIDON;
        tx_corr_coeff[7][0] = AR_PHY_TX_IQCAL_CORR_COEFF_67_B0_POSEIDON;
    }

    for (ch_idx = 0; ch_idx < num_chains; ch_idx++) {
        nmeasurement = OS_REG_READ_FIELD(ah,
            AR_PHY_TX_IQCAL_STATUS_B0(ah), AR_PHY_CALIBRATED_GAINS_0);
        if (nmeasurement > MAX_MEASUREMENT) {
            nmeasurement = MAX_MEASUREMENT;
        }

        if (!AR_SREV_SCORPION(ah)) {
            /*
             * reset max/min variable to min/max values so that
             * we always start with 1st calibrated gain value
             */
            magnitude_max = -64;
            phase_max     = -64;
            magnitude_min = 63;
            phase_min     = 63;
            magnitude_avg = 0;
            phase_avg     = 0;
            magnitude_max_idx = 0;
            magnitude_min_idx = 0;
            phase_max_idx = 0;
            phase_min_idx = 0;

            /* detect outlier only if nmeasurement > 1 */
            if (nmeasurement > 1) {
                /* printf("----------- start outlier detection -----------\n"); */
                /*
                 * find max/min and phase/mag mismatch across all calibrated gains
                 */
                for (im = 0; im < nmeasurement; im++) {
                    magnitude = coeff->mag_coeff[ch_idx][im][0];
                    phase = coeff->phs_coeff[ch_idx][im][0];

                    magnitude_avg = magnitude_avg + magnitude;
                    phase_avg = phase_avg + phase;
                    if (magnitude > magnitude_max) {
                        magnitude_max = magnitude;
                        magnitude_max_idx = im;
                    }
                    if (magnitude < magnitude_min) {
                        magnitude_min = magnitude;
                        magnitude_min_idx = im;
                    }
                    if (phase > phase_max) {
                        phase_max = phase;
                        phase_max_idx = im;
                    }
                    if (phase < phase_min) {
                        phase_min = phase;
                        phase_min_idx = im;
                    }
                }
                /* find average (exclude max abs value) */
                for (im = 0; im < nmeasurement; im++) {
                    magnitude = coeff->mag_coeff[ch_idx][im][0];
                    phase = coeff->phs_coeff[ch_idx][im][0];
                    if ((ABS(magnitude) < ABS(magnitude_max)) ||
                        (ABS(magnitude) < ABS(magnitude_min)))
                    {
                        magnitude_avg = magnitude_avg + magnitude;
                    }
                    if ((ABS(phase) < ABS(phase_max)) ||
                        (ABS(phase) < ABS(phase_min)))
                    {
                        phase_avg = phase_avg + phase;
                    }
                }
                magnitude_avg = magnitude_avg / (nmeasurement - 1);
                phase_avg = phase_avg / (nmeasurement - 1);

                /* detect magnitude outlier */
                if (ABS(magnitude_max - magnitude_min) > MAX_MAG_DELTA) {
                    if (ABS(magnitude_max - magnitude_avg) >
                        ABS(magnitude_min - magnitude_avg))
                    {
                        /* max is outlier, force to avg */
                        outlier_mag_idx = magnitude_max_idx;
                    } else {
                        /* min is outlier, force to avg */
                        outlier_mag_idx = magnitude_min_idx;
                    }
                    coeff->mag_coeff[ch_idx][outlier_mag_idx][0] = magnitude_avg;
                    coeff->phs_coeff[ch_idx][outlier_mag_idx][0] = phase_avg;
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, 
                        "[ch%d][outlier mag gain%d]:: "
                        "mag_avg = %d (/128), phase_avg = %d (/256)\n",
                        ch_idx, outlier_mag_idx, magnitude_avg, phase_avg);
                }
                /* detect phase outlier */
                if (ABS(phase_max - phase_min) > MAX_PHS_DELTA) {
                    if (ABS(phase_max-phase_avg) > ABS(phase_min - phase_avg)) {
                        /* max is outlier, force to avg */
                        outlier_phs_idx = phase_max_idx;
                    } else{
                        /* min is outlier, force to avg */
                        outlier_phs_idx = phase_min_idx;
                    }
                    coeff->mag_coeff[ch_idx][outlier_phs_idx][0] = magnitude_avg;
                    coeff->phs_coeff[ch_idx][outlier_phs_idx][0] = phase_avg;
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, 
                        "[ch%d][outlier phs gain%d]:: " 
                        "mag_avg = %d (/128), phase_avg = %d (/256)\n",
                        ch_idx, outlier_phs_idx, magnitude_avg, phase_avg);
                }
            }
        }

        /*printf("------------ after outlier detection -------------\n");*/
        for (im = 0; im < nmeasurement; im++) {
            magnitude = coeff->mag_coeff[ch_idx][im][0];
            phase = coeff->phs_coeff[ch_idx][im][0];

            #if 0
            printf("[ch%d][gain%d]:: mag = %d (/128), phase = %d (/256)\n",
                ch_idx, im, magnitude, phase);
            #endif

            coeff->iqc_coeff[0] = (phase & 0x7f) | ((magnitude & 0x7f) << 7);

            if ((im % 2) == 0) {
                OS_REG_RMW_FIELD(ah,
                    tx_corr_coeff[im][ch_idx],
                    AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE,
                    coeff->iqc_coeff[0]);
            } else {
                OS_REG_RMW_FIELD(ah,
                    tx_corr_coeff[im][ch_idx],
                    AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
                    coeff->iqc_coeff[0]);
            }
#if ATH_SUPPORT_CAL_REUSE
            ichan->tx_corr_coeff[im][ch_idx] = coeff->iqc_coeff[0];
#endif
        }
#if ATH_SUPPORT_CAL_REUSE
        ichan->num_measures[ch_idx] = nmeasurement;
#endif
    }

    OS_REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_3,
                     AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN, 0x1);
    OS_REG_RMW_FIELD(ah, AR_PHY_RX_IQCAL_CORR_B0,
                     AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN, 0x1);

#if ATH_SUPPORT_CAL_REUSE
    if (is_cal_reusable) {
        ichan->one_time_txiqcal_done = AH_TRUE;
        HALDEBUG(ah, HAL_DEBUG_FCS_RTT,
            "(FCS) TXIQCAL saved - %d\n", ichan->channel);
    }
#endif
}

#if ATH_SUPPORT_CAL_REUSE
static void
ar9300_tx_iq_cal_apply(struct ath_hal *ah, HAL_CHANNEL_INTERNAL *ichan)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int nmeasurement, ch_idx, im;

    u_int32_t tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS] = {
        {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
        {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
            AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
            AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
    };

    if (AR_SREV_POSEIDON(ah)) {
        HALASSERT(ahp->ah_tx_cal_chainmask == 0x1);

        tx_corr_coeff[0][0] = AR_PHY_TX_IQCAL_CORR_COEFF_01_B0_POSEIDON;
        tx_corr_coeff[1][0] = AR_PHY_TX_IQCAL_CORR_COEFF_01_B0_POSEIDON;
        tx_corr_coeff[2][0] = AR_PHY_TX_IQCAL_CORR_COEFF_23_B0_POSEIDON;
        tx_corr_coeff[3][0] = AR_PHY_TX_IQCAL_CORR_COEFF_23_B0_POSEIDON;
        tx_corr_coeff[4][0] = AR_PHY_TX_IQCAL_CORR_COEFF_45_B0_POSEIDON;
        tx_corr_coeff[5][0] = AR_PHY_TX_IQCAL_CORR_COEFF_45_B0_POSEIDON;
        tx_corr_coeff[6][0] = AR_PHY_TX_IQCAL_CORR_COEFF_67_B0_POSEIDON;
        tx_corr_coeff[7][0] = AR_PHY_TX_IQCAL_CORR_COEFF_67_B0_POSEIDON;
    }

    for (ch_idx = 0; ch_idx < AR9300_MAX_CHAINS; ch_idx++) {
        if ((ahp->ah_tx_cal_chainmask & (1 << ch_idx)) == 0) {
            continue;
        }
        nmeasurement = ichan->num_measures[ch_idx];

        for (im = 0; im < nmeasurement; im++) {
            if ((im % 2) == 0) {
                OS_REG_RMW_FIELD(ah,
                    tx_corr_coeff[im][ch_idx],
                    AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE,
                    ichan->tx_corr_coeff[im][ch_idx]);
            } else {
                OS_REG_RMW_FIELD(ah,
                    tx_corr_coeff[im][ch_idx],
                    AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
                    ichan->tx_corr_coeff[im][ch_idx]);
            }
        }
    }

    OS_REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_3,
                     AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN, 0x1);
    OS_REG_RMW_FIELD(ah, AR_PHY_RX_IQCAL_CORR_B0,
                     AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN, 0x1);
}
#endif

/*
 * ar9300_tx_iq_cal_hw_run is only needed for osprey/wasp/hornet
 * It is not needed for jupiter/poseidon.
 */
HAL_BOOL
ar9300_tx_iq_cal_hw_run(struct ath_hal *ah)
{
    int is_tx_gain_forced;

    is_tx_gain_forced = OS_REG_READ_FIELD(ah,
        AR_PHY_TX_FORCED_GAIN, AR_PHY_TXGAIN_FORCE);
    if (is_tx_gain_forced) {
        /*printf("Tx gain can not be forced during tx I/Q cal!\n");*/
        OS_REG_RMW_FIELD(ah, AR_PHY_TX_FORCED_GAIN, AR_PHY_TXGAIN_FORCE, 0);
    }

    /* enable tx IQ cal */
    OS_REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_START(ah),
        AR_PHY_TX_IQCAL_START_DO_CAL, AR_PHY_TX_IQCAL_START_DO_CAL);

    if (!ath_hal_wait(ah,
            AR_PHY_TX_IQCAL_START(ah), AR_PHY_TX_IQCAL_START_DO_CAL, 0))
    {
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s: Tx IQ Cal is never completed.\n", __func__);
        return AH_FALSE;
    }
    return AH_TRUE;
}

static void
ar9300_tx_iq_cal_post_proc(struct ath_hal *ah,HAL_CHANNEL_INTERNAL *ichan,
                           int iqcal_idx, int max_iqcal,HAL_BOOL is_cal_reusable, HAL_BOOL apply_last_corr)
{
    int nmeasurement=0, im, ix, iy, temp;
    struct ath_hal_9300     *ahp = AH9300(ah);
    u_int32_t txiqcal_status[AR9300_MAX_CHAINS] = {
        AR_PHY_TX_IQCAL_STATUS_B0(ah),
        AR_PHY_TX_IQCAL_STATUS_B1,
        AR_PHY_TX_IQCAL_STATUS_B2,
    };
    const u_int32_t chan_info_tab[] = {
        AR_PHY_CHAN_INFO_TAB_0,
        AR_PHY_CHAN_INFO_TAB_1,
        AR_PHY_CHAN_INFO_TAB_2,
    };
    int32_t iq_res[6];
    int32_t ch_idx, j;
    u_int32_t num_chains = 0;
    static struct coeff_t coeff;
    txiqcal_status[0] = AR_PHY_TX_IQCAL_STATUS_B0(ah);

    for (ch_idx = 0; ch_idx < AR9300_MAX_CHAINS; ch_idx++) {
        if (ahp->ah_tx_chainmask & (1 << ch_idx)) {
            num_chains++;
        }
    }

    if (apply_last_corr) {
	    if (coeff.last_cal == AH_TRUE) {
		    int32_t magnitude, phase;
		    int ch_idx, im;
		    u_int32_t tx_corr_coeff[MAX_MEASUREMENT][AR9300_MAX_CHAINS] = {
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_01_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_01_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_01_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_23_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_23_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_23_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_45_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_45_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_45_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
			    {   AR_PHY_TX_IQCAL_CORR_COEFF_67_B0,
				    AR_PHY_TX_IQCAL_CORR_COEFF_67_B1,
				    AR_PHY_TX_IQCAL_CORR_COEFF_67_B2},
		    };
		    for (ch_idx = 0; ch_idx < num_chains; ch_idx++) {
			    for (im = 0; im < coeff.last_nmeasurement; im++) {
				    magnitude = coeff.mag_coeff[ch_idx][im][0];
				    phase = coeff.phs_coeff[ch_idx][im][0];

#if 0
				    printf("[ch%d][gain%d]:: mag = %d (/128), phase = %d (/256)\n",
						    ch_idx, im, magnitude, phase);
#endif

				    coeff.iqc_coeff[0] = (phase & 0x7f) | ((magnitude & 0x7f) << 7);
				    if ((im % 2) == 0) {
					    OS_REG_RMW_FIELD(ah,
							    tx_corr_coeff[im][ch_idx],
							    AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE,
							    coeff.iqc_coeff[0]);
				    } else {
					    OS_REG_RMW_FIELD(ah,
							    tx_corr_coeff[im][ch_idx],
							    AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
							    coeff.iqc_coeff[0]);
				    }
			    }
		    }
		    OS_REG_RMW_FIELD(ah, AR_PHY_TX_IQCAL_CONTROL_3,
				    AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN, 0x1);
	    }
	    return;
    }


    for (ch_idx = 0; ch_idx < num_chains; ch_idx++) {
        nmeasurement = OS_REG_READ_FIELD(ah,
            AR_PHY_TX_IQCAL_STATUS_B0(ah), AR_PHY_CALIBRATED_GAINS_0);
        if (nmeasurement > MAX_MEASUREMENT) {
            nmeasurement = MAX_MEASUREMENT;
        }

        for (im = 0; im < nmeasurement; im++) {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s: Doing Tx IQ Cal for chain %d.\n", __func__, ch_idx);
            if (OS_REG_READ(ah, txiqcal_status[ch_idx]) &
                AR_PHY_TX_IQCAL_STATUS_FAILED)
            {
                HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "%s: Tx IQ Cal failed for chain %d.\n", __func__, ch_idx);
                goto TX_IQ_CAL_FAILED_;
            }

            for (j = 0; j < 3; j++) {
                u_int32_t idx = 2 * j;
                /* 3 registers for each calibration result */
                u_int32_t offset = 4 * (3 * im + j);

                OS_REG_RMW_FIELD(ah, AR_PHY_CHAN_INFO_MEMORY,
                    AR_PHY_CHAN_INFO_TAB_S2_READ, 0);
                /* 32 bits */    
                iq_res[idx] = OS_REG_READ(ah, chan_info_tab[ch_idx] + offset);
                OS_REG_RMW_FIELD(ah, AR_PHY_CHAN_INFO_MEMORY,
                    AR_PHY_CHAN_INFO_TAB_S2_READ, 1);
                /* 16 bits */
                iq_res[idx + 1] = 0xffff &
                    OS_REG_READ(ah, chan_info_tab[ch_idx] + offset);
    
                HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "%s: IQ RES[%d]=0x%x IQ_RES[%d]=0x%x\n",
                    __func__, idx, iq_res[idx], idx + 1, iq_res[idx + 1]);
            }

            if (AH_FALSE == ar9300_calc_iq_corr(
                             ah, ch_idx, iq_res, coeff.iqc_coeff))
            {
                HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "%s: Failed in calculation of IQ correction.\n",
                     __func__);
                goto TX_IQ_CAL_FAILED_;
            }

            coeff.phs_coeff[ch_idx][im][iqcal_idx-1] = coeff.iqc_coeff[0] & 0x7f;
            coeff.mag_coeff[ch_idx][im][iqcal_idx-1] = (coeff.iqc_coeff[0] >> 7) & 0x7f;
            if (coeff.mag_coeff[ch_idx][im][iqcal_idx-1] > 63) {
                coeff.mag_coeff[ch_idx][im][iqcal_idx-1] -= 128;
            }
            if (coeff.phs_coeff[ch_idx][im][iqcal_idx-1] > 63) {
                coeff.phs_coeff[ch_idx][im][iqcal_idx-1] -= 128;     
            }
#if 0
            ath_hal_printf(ah, "IQCAL::[ch%d][gain%d]:: mag = %d phase = %d \n",
                ch_idx, im, coeff.mag_coeff[ch_idx][im][iqcal_idx-1],
                coeff.phs_coeff[ch_idx][im][iqcal_idx-1]);
#endif
        }
    }
    //last iteration; calculate mag and phs
    if (iqcal_idx == max_iqcal) {
        if (max_iqcal>1) {
            for (ch_idx = 0; ch_idx < num_chains; ch_idx++) {
                for (im = 0; im < nmeasurement; im++) {
                    //sort mag and phs
                    for( ix=0;ix<max_iqcal-1;ix++){
                        for( iy=ix+1;iy<=max_iqcal-1;iy++){
                            if(coeff.mag_coeff[ch_idx][im][iy] < 
                                coeff.mag_coeff[ch_idx][im][ix]) {
                                //swap
                                temp=coeff.mag_coeff[ch_idx][im][ix];
                                coeff.mag_coeff[ch_idx][im][ix] = coeff.mag_coeff[ch_idx][im][iy];
                                coeff.mag_coeff[ch_idx][im][iy] = temp;
                            }
                            if(coeff.phs_coeff[ch_idx][im][iy] < 
                                coeff.phs_coeff[ch_idx][im][ix]){
                                //swap
                                temp=coeff.phs_coeff[ch_idx][im][ix];
                                coeff.phs_coeff[ch_idx][im][ix]=coeff.phs_coeff[ch_idx][im][iy];
                                coeff.phs_coeff[ch_idx][im][iy]=temp;
                            }
                        }  
                    }
                    //select median; 3rd entry in the sorted array
                    coeff.mag_coeff[ch_idx][im][0] = 
                        coeff.mag_coeff[ch_idx][im][max_iqcal/2];
                    coeff.phs_coeff[ch_idx][im][0] =
                        coeff.phs_coeff[ch_idx][im][max_iqcal/2];
                    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, 
                        "IQCAL: Median [ch%d][gain%d]:: mag = %d phase = %d \n", 
                        ch_idx, im,coeff.mag_coeff[ch_idx][im][0], 
                        coeff.phs_coeff[ch_idx][im][0]);
                }
            }	
        }
        ar9300_tx_iq_cal_outlier_detection(ah,ichan, num_chains, &coeff,is_cal_reusable);
    }


    coeff.last_nmeasurement = nmeasurement;
    coeff.last_cal = AH_TRUE;

    return;

TX_IQ_CAL_FAILED_:
    /* no need to print this, it is AGC failure not chip stuck */
    /*ath_hal_printf(ah, "Tx IQ Cal failed(%d)\n", line);*/
    coeff.last_cal = AH_FALSE;
    return;
}


/*
 * ar9300_disable_phy_restart
 *
 * In some BBpanics, we can disable the phyrestart
 * disable_phy_restart
 *      != 0, disable the phy restart in h/w
 *      == 0, enable the phy restart in h/w
 */
void ar9300_disable_phy_restart(struct ath_hal *ah, int disable_phy_restart)
{
    u_int32_t val;

    val = OS_REG_READ(ah, AR_PHY_RESTART);
    if (disable_phy_restart) {
        val &= ~AR_PHY_RESTART_ENA;
        AH9300(ah)->ah_phyrestart_disabled = 1;
    } else {
        val |= AR_PHY_RESTART_ENA;
        AH9300(ah)->ah_phyrestart_disabled = 0;
    }
    OS_REG_WRITE(ah, AR_PHY_RESTART, val);

    val = OS_REG_READ(ah, AR_PHY_RESTART);
}

HAL_BOOL
ar9300_interference_is_present(struct ath_hal *ah)
{
    int i;
    struct ath_hal_private  *ahpriv = AH_PRIVATE(ah);
    const struct ieee80211_channel *chan = ahpriv->ah_curchan;
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    if (ichan == NULL) {
        ath_hal_printf(ah, "%s: called with ichan=NULL\n", __func__);
        return AH_FALSE;
    }

    /* This function is called after a stuck beacon, if EACS is enabled.
     * If CW interference is severe, then HW goes into a loop of continuous
     * stuck beacons and resets. On reset the NF cal history is cleared.
     * So the median value of the history cannot be used -
     * hence check if any value (Chain 0/Primary Channel)
     * is outside the bounds.
     */
    HAL_NFCAL_HIST_FULL *h = AH_HOME_CHAN_NFCAL_HIST(ah, ichan);
    for (i = 0; i < HAL_NF_CAL_HIST_LEN_FULL; i++) {
        if (h->nf_cal_buffer[i][0] >
            AH9300(ah)->nfp->nominal + AH9300(ah)->nf_cw_int_delta)
        {
            return AH_TRUE;
        }

    }
    return AH_FALSE;
}

#if ATH_SUPPORT_CRDC
void
ar9300_crdc_rx_notify(struct ath_hal *ah, struct ath_rx_status *rxs)
{
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    int rssi_index;
    
    if ((!AR_SREV_WASP(ah)) ||
        (!ahpriv->ah_config.ath_hal_crdc_enable)) {
        return;
    }

    if (rxs->rs_isaggr && rxs->rs_moreaggr) {
        return;
    }

    if ((rxs->rs_rssi_ctl0 >= HAL_RSSI_BAD) ||
        (rxs->rs_rssi_ctl1 >= HAL_RSSI_BAD)) {
        return;
    }

    rssi_index = ah->ah_crdc_rssi_ptr % HAL_MAX_CRDC_RSSI_SAMPLE;

    ah->ah_crdc_rssi_sample[0][rssi_index] = rxs->rs_rssi_ctl0;
    ah->ah_crdc_rssi_sample[1][rssi_index] = rxs->rs_rssi_ctl1;

    ah->ah_crdc_rssi_ptr++;
}

static int
ar9300_crdc_avg_rssi(struct ath_hal *ah, int chain)
{
    int crdc_rssi_sum = 0;
    int crdc_rssi_ptr = ah->ah_crdc_rssi_ptr, i;
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    int crdc_window = ahpriv->ah_config.ath_hal_crdc_window;

    if (crdc_window > HAL_MAX_CRDC_RSSI_SAMPLE) {
        crdc_window = HAL_MAX_CRDC_RSSI_SAMPLE;
    }

    for (i = 1; i <= crdc_window; i++) {
        crdc_rssi_sum += 
            ah->ah_crdc_rssi_sample[chain]
            [(crdc_rssi_ptr - i) % HAL_MAX_CRDC_RSSI_SAMPLE];
    }

    return crdc_rssi_sum / crdc_window;
}

static void
ar9300_crdc_activate(struct ath_hal *ah, int rssi_diff, int enable)
{
    int val, orig_val;
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    int crdc_numerator = ahpriv->ah_config.ath_hal_crdc_numerator;
    int crdc_denominator = ahpriv->ah_config.ath_hal_crdc_denominator;
    int c = (rssi_diff * crdc_numerator) / crdc_denominator;

    val = orig_val = OS_REG_READ(ah, AR_PHY_MULTICHAIN_CTRL);
    val &= 0xffffff00;
    if (enable) {
        val |= 0x1;
        val |= ((c << 1) & 0xff);
    }
    OS_REG_WRITE(ah, AR_PHY_MULTICHAIN_CTRL, val);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "diff: %02d comp: %02d reg: %08x %08x\n", 
        rssi_diff, c, orig_val, val);
}


void ar9300_chain_rssi_diff_compensation(struct ath_hal *ah)
{
    struct ath_hal_private  *ahpriv = AH_PRIVATE(ah);
    int crdc_window = ahpriv->ah_config.ath_hal_crdc_window;
    int crdc_rssi_ptr = ah->ah_crdc_rssi_ptr;
    int crdc_rssi_thresh = ahpriv->ah_config.ath_hal_crdc_rssithresh;
    int crdc_diff_thresh = ahpriv->ah_config.ath_hal_crdc_diffthresh;
    int avg_rssi[2], avg_rssi_diff;

    if ((!AR_SREV_WASP(ah)) ||
        (!ahpriv->ah_config.ath_hal_crdc_enable)) {
        if (ah->ah_crdc_rssi_ptr) {
            ar9300_crdc_activate(ah, 0, 0);
            ah->ah_crdc_rssi_ptr = 0;
        }
        return;
    }

    if (crdc_window > HAL_MAX_CRDC_RSSI_SAMPLE) {
        crdc_window = HAL_MAX_CRDC_RSSI_SAMPLE;
    }

    if (crdc_rssi_ptr < crdc_window) {
        return;
    }

    avg_rssi[0] = ar9300_crdc_avg_rssi(ah, 0);
    avg_rssi[1] = ar9300_crdc_avg_rssi(ah, 1);
    avg_rssi_diff = avg_rssi[1] - avg_rssi[0];

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "crdc: avg: %02d %02d ", 
        avg_rssi[0], avg_rssi[1]);

    if ((avg_rssi[0] < crdc_rssi_thresh) &&
        (avg_rssi[1] < crdc_rssi_thresh)) {
        ar9300_crdc_activate(ah, 0, 0);
    } else {
        if (ABS(avg_rssi_diff) >= crdc_diff_thresh) {
            ar9300_crdc_activate(ah, avg_rssi_diff, 1);
        } else {
            ar9300_crdc_activate(ah, 0, 1);
        }
    }
}
#endif

#if ATH_ANT_DIV_COMB
HAL_BOOL
ar9300_ant_ctrl_set_lna_div_use_bt_ant(struct ath_hal *ah, HAL_BOOL enable, const struct ieee80211_channel *chan)
{
    u_int32_t value;
    u_int32_t regval;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_CHANNEL_INTERNAL *ichan;
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    HAL_CAPABILITIES *pcap = &ahpriv->ah_caps;

    HALDEBUG(ah, HAL_DEBUG_RESET | HAL_DEBUG_BT_COEX,
      "%s: called; enable=%d\n", __func__, enable);

    if (AR_SREV_POSEIDON(ah)) {
        // Make sure this scheme is only used for WB225(Astra)
        ahp->ah_lna_div_use_bt_ant_enable = enable;

        ichan = ar9300_check_chan(ah, chan);
        if ( ichan == AH_NULL ) {
            HALDEBUG(ah, HAL_DEBUG_CHANNEL, "%s: invalid channel %u/0x%x; no mapping\n",
                     __func__, chan->ic_freq, chan->ic_flags);
            return AH_FALSE;
        }

        if ( enable == TRUE ) {
            pcap->halAntDivCombSupport = TRUE;
        } else {
            pcap->halAntDivCombSupport = pcap->halAntDivCombSupportOrg;
        }

#define AR_SWITCH_TABLE_COM2_ALL (0xffffff)
#define AR_SWITCH_TABLE_COM2_ALL_S (0)
        value = ar9300_ant_ctrl_common2_get(ah, IS_CHAN_2GHZ(ichan));
        if ( enable == TRUE ) {
            value &= ~AR_SWITCH_TABLE_COM2_ALL;
            value |= ah->ah_config.ath_hal_ant_ctrl_comm2g_switch_enable;
        }
	HALDEBUG(ah, HAL_DEBUG_RESET, "%s: com2=0x%08x\n", __func__, value);
        OS_REG_RMW_FIELD(ah, AR_PHY_SWITCH_COM_2, AR_SWITCH_TABLE_COM2_ALL, value);

        value = ar9300_eeprom_get(ahp, EEP_ANTDIV_control);
        /* main_lnaconf, alt_lnaconf, main_tb, alt_tb */
        regval = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
        regval &= (~ANT_DIV_CONTROL_ALL); /* clear bit 25~30 */     
        regval |= (value & 0x3f) << ANT_DIV_CONTROL_ALL_S; 
        /* enable_lnadiv */
        regval &= (~MULTICHAIN_GAIN_CTRL__ENABLE_ANT_DIV_LNADIV__MASK);
        regval |= ((value >> 6) & 0x1) << 
                  MULTICHAIN_GAIN_CTRL__ENABLE_ANT_DIV_LNADIV__SHIFT;
        if ( enable == TRUE ) {
            regval |= ANT_DIV_ENABLE;
        }
        OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);

        /* enable fast_div */
        regval = OS_REG_READ(ah, AR_PHY_CCK_DETECT);
        regval &= (~BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__MASK);
        regval |= ((value >> 7) & 0x1) << 
                  BBB_SIG_DETECT__ENABLE_ANT_FAST_DIV__SHIFT;
        if ( enable == TRUE ) {
            regval |= FAST_DIV_ENABLE;
        }
        OS_REG_WRITE(ah, AR_PHY_CCK_DETECT, regval);
  
        if ( AR_SREV_POSEIDON_11_OR_LATER(ah) ) {
            if (pcap->halAntDivCombSupport) {
                /* If support DivComb, set MAIN to LNA1 and ALT to LNA2 at the first beginning */
                regval = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
                /* clear bit 25~30 main_lnaconf, alt_lnaconf, main_tb, alt_tb */
                regval &= (~(MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__MASK | 
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__MASK | 
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__MASK | 
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__MASK)); 
                regval |= (HAL_ANT_DIV_COMB_LNA1 << 
                           MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__SHIFT); 
                regval |= (HAL_ANT_DIV_COMB_LNA2 << 
                           MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__SHIFT); 
                OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
            }
        }

        return AH_TRUE;
    } else if (AR_SREV_APHRODITE(ah)) {
        ahp->ah_lna_div_use_bt_ant_enable = enable;
        if (enable) {
                OS_REG_SET_BIT(ah, AR_PHY_MC_GAIN_CTRL, ANT_DIV_ENABLE);
                OS_REG_SET_BIT(ah, AR_PHY_MC_GAIN_CTRL, (1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_SW_RX_PROT__SHIFT));
                OS_REG_SET_BIT(ah, AR_PHY_CCK_DETECT, AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
                OS_REG_SET_BIT(ah, AR_PHY_RESTART, RESTART__ENABLE_ANT_FAST_DIV_M2FLAG__MASK);
                OS_REG_SET_BIT(ah, AR_BTCOEX_WL_LNADIV, AR_BTCOEX_WL_LNADIV_FORCE_ON);
        } else {
                OS_REG_CLR_BIT(ah, AR_PHY_MC_GAIN_CTRL, ANT_DIV_ENABLE);
                OS_REG_CLR_BIT(ah, AR_PHY_MC_GAIN_CTRL, (1 << MULTICHAIN_GAIN_CTRL__ENABLE_ANT_SW_RX_PROT__SHIFT));
                OS_REG_CLR_BIT(ah, AR_PHY_CCK_DETECT, AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
                OS_REG_CLR_BIT(ah, AR_PHY_RESTART, RESTART__ENABLE_ANT_FAST_DIV_M2FLAG__MASK);
                OS_REG_CLR_BIT(ah, AR_BTCOEX_WL_LNADIV, AR_BTCOEX_WL_LNADIV_FORCE_ON);

                regval = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
                regval &= (~(MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__MASK |
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__MASK |
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__MASK |
                             MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__MASK));
                regval |= (HAL_ANT_DIV_COMB_LNA1 <<
                           MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__SHIFT);
                regval |= (HAL_ANT_DIV_COMB_LNA2 <<
                           MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__SHIFT);
 
                OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, regval);
        }
        return AH_TRUE;
    }
    return AH_TRUE;
}
#endif /* ATH_ANT_DIV_COMB */
