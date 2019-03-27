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
#include "ar9300.h"
#include "ah_internal.h"
#include "ar9300paprd.h"
#include "ar9300reg.h"


#if ATH_SUPPORT_PAPRD

static struct ar9300_paprd_pwr_adjust ar9300_paprd_pwr_adj_array[] = {
/*   rate index     ,   register offset   ,  mask of register               , */
  {ALL_TARGET_HT20_5, AR_PHY_POWERTX_RATE5, AR_PHY_POWERTX_RATE5_POWERTXHT20_3,
/* mask offset of register             ,  offset  dB*/
   AR_PHY_POWERTX_RATE5_POWERTXHT20_3_S,      1},
  {ALL_TARGET_HT20_6, AR_PHY_POWERTX_RATE6, AR_PHY_POWERTX_RATE6_POWERTXHT20_4,
   AR_PHY_POWERTX_RATE6_POWERTXHT20_4_S,      2},
  {ALL_TARGET_HT20_7, AR_PHY_POWERTX_RATE6, AR_PHY_POWERTX_RATE6_POWERTXHT20_5,
   AR_PHY_POWERTX_RATE6_POWERTXHT20_5_S,      2},
  {ALL_TARGET_HT40_5, AR_PHY_POWERTX_RATE7, AR_PHY_POWERTX_RATE7_POWERTXHT40_3,
   AR_PHY_POWERTX_RATE7_POWERTXHT40_3_S,      1},
  {ALL_TARGET_HT40_6, AR_PHY_POWERTX_RATE8, AR_PHY_POWERTX_RATE8_POWERTXHT40_4,
   AR_PHY_POWERTX_RATE8_POWERTXHT40_4_S,      2},
  {ALL_TARGET_HT40_7, AR_PHY_POWERTX_RATE8, AR_PHY_POWERTX_RATE8_POWERTXHT40_5,
   AR_PHY_POWERTX_RATE8_POWERTXHT40_5_S,      2},
 {ALL_TARGET_LEGACY_54, AR_PHY_POWERTX_RATE2, AR_PHY_POWERTX_RATE2_POWERTX54M_7,
   AR_PHY_POWERTX_RATE2_POWERTX54M_7_S,       2},
};

HAL_BOOL create_pa_curve(u_int32_t * paprd_train_data_l,
    u_int32_t *paprd_train_data_u, u_int32_t *pa_table, u_int32_t *g_fxp_ext,
    int * pa_in);

static int
ar9300_paprd_setup_single_table(struct ath_hal *ah, struct ieee80211_channel * chan)
{
    int is_2g = IEEE80211_IS_CHAN_2GHZ(chan);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    struct ath_hal_9300 *ahp = AH9300(ah);
    int is_ht40 = 0;
    u_int32_t am_mask = 0;
    u_int32_t val = OS_REG_READ(ah, AR_2040_MODE);
    u_int8_t target_power_val_t2[ar9300_rate_size];
    int      power_tblindex = 0, power_delta = 0;
    int      paprd_scale_factor = 5;  

    const u_int8_t mask2num[8] = {
        0 /* 000 */,
        1 /* 001 */,
        1 /* 010 */,
        2 /* 011 */,
        1 /* 100 */,
        2 /* 101 */,
        2 /* 110 */,
        3 /* 111 */
    };

    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

#define ABS(_x, _y) ((int)_x > (int)_y ? (int)_x - (int)_y : (int)_y - (int)_x)

    ar9300_set_target_power_from_eeprom(ah, ichan->channel, target_power_val_t2);
    if (val & HAL_HT_MACMODE_2040) {
        is_ht40 = 1;
    }

    /*
     * Note on paprd_scale_factor
     * This factor is saved in eeprom as 3 bit fields in following fashion. 
     * In 5G there are 3 scale factors -- upper, mid and lower band.
     * Upper band scale factor is coded in bits 25-27 of
     * modal_header_5g.paprd_rate_mask_ht20.
     * Mid band scale factor is coded in bits 28-30 of
     * modal_header_5g.paprd_rate_mask_ht40.
     * Lower band scale factor is coded in bits 25-27 of
     * modal_header_5g.paprd_rate_mask_ht40.
     * For 2G there is only one scale factor. It is saved in bits 25-27 of
     * modal_header_2g.paprd_rate_mask_ht20.
     */
    AH_PAPRD_GET_SCALE_FACTOR(paprd_scale_factor, eep, is_2g, ichan->channel);
    if (is_2g) {
        if (is_ht40) {
            am_mask = ahp->ah_2g_paprd_rate_mask_ht40 & AH_PAPRD_AM_PM_MASK;
            power_tblindex = ALL_TARGET_HT40_0_8_16;
        } else {
            am_mask = ahp->ah_2g_paprd_rate_mask_ht20 & AH_PAPRD_AM_PM_MASK;
            power_tblindex = ALL_TARGET_HT20_0_8_16;
        }
        if (AR_SREV_HORNET(ah) || AR_SREV_WASP(ah) || AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            if (is_ht40) {
                ahp->paprd_training_power = 
                    target_power_val_t2[ALL_TARGET_HT40_7] + 2;
            } else {
                ahp->paprd_training_power = 
                    target_power_val_t2[ALL_TARGET_HT20_7] + 2;
            }
		} else if (AR_SREV_POSEIDON(ah)) {
            ahp->paprd_training_power = 25;
        } else {
            ahp->paprd_training_power =
                OS_REG_READ_FIELD_ALT(ah, AR_PHY_POWERTX_RATE5,
                AR_PHY_POWERTX_RATE5_POWERTXHT20_0);
            if (ABS(target_power_val_t2[power_tblindex], 
                ahp->paprd_training_power) >  paprd_scale_factor)
            {
                HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                    "%s[%d]: Chan %d paprd failing EEP PWR 0x%08x" 
                    "TGT PWR 0x%08x\n", __func__, __LINE__, ichan->channel,
                    target_power_val_t2[power_tblindex], 
                    ahp->paprd_training_power);
                goto FAILED;
            }
            
            power_delta =
                ABS(ahp->paprd_training_power, 
                    target_power_val_t2[power_tblindex]);

            power_delta = power_delta > 4 ? 0 : 4 - power_delta;
            ahp->paprd_training_power = 
                ahp->paprd_training_power - power_delta;
        }


    } else {
        if (is_ht40) {
            ahp->paprd_training_power =
			    OS_REG_READ_FIELD_ALT(ah, AR_PHY_POWERTX_RATE8,
				AR_PHY_POWERTX_RATE8_POWERTXHT40_5);
            am_mask = ahp->ah_5g_paprd_rate_mask_ht40 & AH_PAPRD_AM_PM_MASK;
            switch (mask2num[ahp->ah_tx_chainmask])
            {
            case 1:
                power_delta = 6;
                break;
            case 2:
                power_delta = 4;
                break;
            case 3:
                power_delta = 2;
                break;
            default:
                goto FAILED;
                break;
            }
            power_tblindex = ALL_TARGET_HT40_7;
        } else {
            ahp->paprd_training_power =
			    OS_REG_READ_FIELD_ALT(ah, AR_PHY_POWERTX_RATE6,
				AR_PHY_POWERTX_RATE6_POWERTXHT20_5);
            am_mask = ahp->ah_5g_paprd_rate_mask_ht20 & AH_PAPRD_AM_PM_MASK;
            switch (mask2num[ahp->ah_tx_chainmask])
            {
            case 1:
                power_delta = 6;
                break;
            case 2:
                power_delta = 4;
                break;
            case 3:
                power_delta = 2;
                break;
            default:
                goto FAILED;
                break;
            }
            power_tblindex = ALL_TARGET_HT20_7;
        }
        /* Adjust for scale factor */
        ahp->paprd_training_power += paprd_scale_factor;
        /*
        ath_hal_printf(ah, "%s[%d] paprd_scale_factor %d power_delta %d\n",
            __func__, __LINE__, paprd_scale_factor, power_delta);
         */
        if (ABS(target_power_val_t2[power_tblindex], ahp->paprd_training_power)
            >  paprd_scale_factor)
        {
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s[%d]: Chan %d paprd failing EEP PWR 0x%08x TGT PWR 0x%08x\n",
                __func__, __LINE__, ichan->channel,
                target_power_val_t2[power_tblindex], ahp->paprd_training_power);
            goto FAILED;
        }
        ahp->paprd_training_power = ahp->paprd_training_power + power_delta;
    }

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s 2G %d HT40 %d am_mask 0x%08x\n",
        __func__, is_2g, is_ht40, am_mask);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_AM2AM, AR_PHY_PAPRD_AM2AM_MASK,
        am_mask);
    if (AR_SREV_HORNET(ah)) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_AM2PM, AR_PHY_PAPRD_AM2PM_MASK,
            0);
    }
    else {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_AM2PM, AR_PHY_PAPRD_AM2PM_MASK,
            am_mask);
    }

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_HT40, AR_PHY_PAPRD_HT40_MASK,
        AR_PHY_PAPRD_HT40_MASK);
    /* chain0 */
    if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN0_MASK) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
            AR_PHY_PAPRD_CTRL0_B0_USE_SINGLE_TABLE_MASK, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2PM_ENABLE_0, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2AM_ENABLE_0, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_PA_GAIN_SCALE_FACT_0_MASK, 181);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_PAPRD_MAG_SCALE_FACT_0, 361);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
            AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
            AR_PHY_PAPRD_CTRL0_B0_PAPRD_MAG_THRSH_0, 3);
    }

    /* chain1 */
    if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN1_MASK) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
            AR_PHY_PAPRD_CTRL0_B1_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_1, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2PM_ENABLE_1, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2AM_ENABLE_1, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_PA_GAIN_SCALE_FACT_1_MASK, 181);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_PAPRD_MAG_SCALE_FACT_1, 361);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
            AR_PHY_PAPRD_CTRL0_B1_PAPRD_MAG_THRSH_1, 3);
    }

    /* chain2 */
    if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
            AR_PHY_PAPRD_CTRL0_B2_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_2, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2PM_ENABLE_2, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2AM_ENABLE_2, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_PA_GAIN_SCALE_FACT_2_MASK, 181);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_PAPRD_MAG_SCALE_FACT_2, 361);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
            AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
            AR_PHY_PAPRD_CTRL0_B2_PAPRD_MAG_THRSH_2, 3);
    }

    ar9300_enable_paprd(ah, AH_FALSE, chan);
    if (AR_SREV_POSEIDON(ah)) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP, 0x30);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING, 28);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN, 148);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN, 4);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN, 4);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES, 7);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
	        AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, -3);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE, -15);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR, 400);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_NUM_TRAIN_SAMPLES, 100);
    } else {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP, 0x30);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING, 28);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE, 1);
        if (is_2g) {
        	 if(AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)){ 
             OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2,
                 AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN, 0x91);
           }else{
             OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2,
                 AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN, 147);          	
           }
        }
		else if (AR_SREV_WASP(ah) && !is_2g) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2,
	            AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN, 137);
		} else {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2,
	            AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN, 147);
        }
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN, 4);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN, 4);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES, 7);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL, 1);
        if (AR_SREV_HORNET(ah) || AR_SREV_WASP(ah) || AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, -3);
        } else {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, -6);
        }
        if (is_2g) {
            if(AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)){       	
                OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                    AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE, -10);
            }else{
                OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                    AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE, -15);         	           
            }
        }
        else {
             OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                 AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE, -10);
        }
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE, 1);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA, 0);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR, 400);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_NUM_TRAIN_SAMPLES, 100);
    }

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_0_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_0_B0_PAPRD_PRE_POST_SCALING_0_0, 261376);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_1_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_1_B0_PAPRD_PRE_POST_SCALING_1_0, 248079);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_2_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_2_B0_PAPRD_PRE_POST_SCALING_2_0, 233759);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_3_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_3_B0_PAPRD_PRE_POST_SCALING_3_0, 220464);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_4_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_4_B0_PAPRD_PRE_POST_SCALING_4_0, 208194);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_5_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_5_B0_PAPRD_PRE_POST_SCALING_5_0, 196949);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_6_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_6_B0_PAPRD_PRE_POST_SCALING_6_0, 185706);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_PRE_POST_SCALE_7_B0,
        AR_PHY_PAPRD_PRE_POST_SCALE_7_B0_PAPRD_PRE_POST_SCALING_7_0, 175487);
    return 0;
    
FAILED:
    return -1;
#undef ABS    
}

/*
 * XXX There's another copy of this in ar9300_reset.c, use that!
 */
#if 0
static inline HAL_CHANNEL_INTERNAL*
ar9300_check_chan(struct ath_hal *ah, HAL_CHANNEL *chan)
{
    if ((AR9300_IS_CHAN(chan, CHANNEL_2GHZ) ^
         AR9300_IS_CHAN(chan, CHANNEL_5GHZ)) == 0)
    {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; not marked as 2GHz or 5GHz\n",
            __func__, chan->channel, chan->channel_flags);
        return AH_NULL;
    }

    if ((AR9300_IS_CHAN(chan, CHANNEL_OFDM)     ^
         AR9300_IS_CHAN(chan, CHANNEL_CCK)      ^
         AR9300_IS_CHAN(chan, CHANNEL_HT20)     ^
         AR9300_IS_CHAN(chan, CHANNEL_HT40PLUS) ^
         AR9300_IS_CHAN(chan, CHANNEL_HT40MINUS)) == 0)
    {
        HALDEBUG(ah, HAL_DEBUG_CHANNEL,
            "%s: invalid channel %u/0x%x; not marked as "
            "OFDM or CCK or HT20 or HT40PLUS or HT40MINUS\n", __func__,
            chan->channel, chan->channel_flags);
        return AH_NULL;
    }

    return (ath_hal_checkchannel(ah, chan));
}
#endif

void ar9300_enable_paprd(struct ath_hal *ah, HAL_BOOL enable_flag,
    struct ieee80211_channel * chan)
{
    HAL_BOOL enable = enable_flag;
    u_int32_t am_mask = 0;
    u_int32_t val = OS_REG_READ(ah, AR_2040_MODE);
    int is_2g = IEEE80211_IS_CHAN_2GHZ(chan);
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
    int is_ht40 = 0;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (val & HAL_HT_MACMODE_2040) {
        is_ht40 = 1;
    }
    if (enable_flag == AH_TRUE) {
        ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;

        if (!is_2g) {
            /*
             * 3 bits for modal_header_5g.paprd_rate_mask_ht20
             * is used for sub band disabling of paprd.
             * 5G band is divided into 3 sub bands -- upper, mid, lower.
             * If bit 30 of modal_header_5g.paprd_rate_mask_ht20 is set
             * to one -- disable paprd for upper 5G
             * If bit 29 of modal_header_5g.paprd_rate_mask_ht20 is set
             * to one -- disable paprd for mid 5G
             * If bit 28 of modal_header_5g.paprd_rate_mask_ht20 is set
             * to one -- disable paprd for lower 5G
             * u_int32_t am_mask = eep->modal_header_5g.paprd_rate_mask_ht20;
             */
            if (ichan->channel >= UPPER_5G_SUB_BANDSTART) {
                if (eep->modal_header_5g.paprd_rate_mask_ht20 & (1 << 30)) {
                    enable = AH_FALSE;
                }
            } else if (ichan->channel >= MID_5G_SUB_BANDSTART) {
                if (eep->modal_header_5g.paprd_rate_mask_ht20 & (1 << 29)) {
                    enable = AH_FALSE;
                }
            } else { /* must be in the lower 5G subband */
                if (eep->modal_header_5g.paprd_rate_mask_ht20 & (1 << 28)) {
                    enable = AH_FALSE;
                }
            }
        }

        if (ahp->ah_paprd_broken) {
            ahp->ah_paprd_broken = AH_FALSE;
            enable = AH_FALSE;

            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, 
                     "%s: PAPRD is in bad state. Don't enable PAPRD\n",
                     __func__);
        }
    }
    if (enable) {
        HAL_CHANNEL_INTERNAL *ichan;
        if (is_2g) {
            if (is_ht40) {
                am_mask = ahp->ah_2g_paprd_rate_mask_ht40 & AH_PAPRD_AM_PM_MASK;
            } else {
                am_mask = ahp->ah_2g_paprd_rate_mask_ht20 & AH_PAPRD_AM_PM_MASK;
            }
        } else {
            if (is_ht40) {
                am_mask = ahp->ah_5g_paprd_rate_mask_ht40 & AH_PAPRD_AM_PM_MASK;
            } else {
                am_mask = ahp->ah_5g_paprd_rate_mask_ht20 & AH_PAPRD_AM_PM_MASK;
            }
        }
        /* Earlier we promgrammed TGT Power with Scaled down value, since
         * PAPRD CAL was not done.
         * Now we finish PAPRD CAL, so bump up the TGT PWR to original
         * EEPROM Power. CTLs calc and Maverickd in
         * "ar9300_eeprom_set_transmit_power"
         */
        ichan = ar9300_check_chan(ah, chan);
        ichan->paprd_table_write_done = 1;
//        chan->paprd_table_write_done = 1;
        /*
        ath_hal_printf(ah, "%s[%d] eeprom_set_transmit_power PAPRD\n",
            __func__, __LINE__);
         */
        if (ar9300_eeprom_set_transmit_power(ah, &ahp->ah_eeprom, chan,
            ath_hal_getctl(ah, chan), ath_hal_getantennaallowed(ah, chan),
            ath_hal_get_twice_max_regpower(AH_PRIVATE(ah), ichan, chan),
            AH_MIN(MAX_RATE_POWER, AH_PRIVATE(ah)->ah_powerLimit)) != HAL_OK) {
            ichan->paprd_table_write_done = 0;
//            chan->paprd_table_write_done = 0;
            /* Intentional print */
            ath_hal_printf(ah,
                "%s[%d] eeprom_set_transmit_power failed ABORT PAPRD\n",
                __func__, __LINE__);
            
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
                AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0, 0);
            if (!AR_SREV_POSEIDON(ah) && !AR_SREV_HORNET(ah)) {
                OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
                    AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1, 0);
                if (!AR_SREV_JUPITER(ah) || (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK)) {
                    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                        AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2, 0);

                }
            }
            return;
        }

        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s 2G %d HT40 %d am_mask 0x%08x\n",
            __func__, is_2g, is_ht40, am_mask);
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_AM2AM, AR_PHY_PAPRD_AM2AM_MASK,
            am_mask);
        if (AR_SREV_HORNET(ah)) {
            OS_REG_RMW_FIELD_ALT(ah,
                AR_PHY_PAPRD_AM2PM, AR_PHY_PAPRD_AM2PM_MASK, 0);
        } else {
            OS_REG_RMW_FIELD_ALT(ah,
                AR_PHY_PAPRD_AM2PM, AR_PHY_PAPRD_AM2PM_MASK, am_mask);
        }

        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_HT40, AR_PHY_PAPRD_HT40_MASK,
            AR_PHY_PAPRD_HT40_MASK);
        /* chain0 */
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN0_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
                AR_PHY_PAPRD_CTRL0_B0_USE_SINGLE_TABLE_MASK, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2PM_ENABLE_0, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2AM_ENABLE_0, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_PA_GAIN_SCALE_FACT_0_MASK, 181);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_PAPRD_MAG_SCALE_FACT_0, 361);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
                AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
                AR_PHY_PAPRD_CTRL0_B0_PAPRD_MAG_THRSH_0, 3);
        }
        /* chain1 */
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN1_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
                AR_PHY_PAPRD_CTRL0_B1_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_1, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2PM_ENABLE_1, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2AM_ENABLE_1, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_PA_GAIN_SCALE_FACT_1_MASK, 181);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_PAPRD_MAG_SCALE_FACT_1, 361);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
                AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
                AR_PHY_PAPRD_CTRL0_B1_PAPRD_MAG_THRSH_1, 3);
        }
        /* chain2 */
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                AR_PHY_PAPRD_CTRL0_B2_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_2, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2PM_ENABLE_2, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2AM_ENABLE_2, 1);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_PA_GAIN_SCALE_FACT_2_MASK, 181);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_PAPRD_MAG_SCALE_FACT_2, 361);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA, 0);
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                AR_PHY_PAPRD_CTRL0_B2_PAPRD_MAG_THRSH_2, 3);
        }
        
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN0_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
                AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0, 1);
        }
        
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN1_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
                AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1, 1);
        }

        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2, 1);
        }
        
    } else {
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN0_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
                AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0, 0);
        }
        
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN1_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
                AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1, 0);
        }
        
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2, 0);
        }
    }
}

static void ar9300_gain_table_entries(struct ath_hal *ah)
{
    int i;
    u_int32_t reg;
    u_int32_t *gain_table_entries = AH9300(ah)->paprd_gain_table_entries;
    u_int32_t *gain_vs_table_index = AH9300(ah)->paprd_gain_table_index;

    reg = AR_PHY_TXGAIN_TAB(1);

    for (i = 0; i < 32; i++) {
        gain_table_entries[i] = OS_REG_READ(ah, reg);
        gain_vs_table_index[i] = (gain_table_entries[i] >> 24) & 0xff;
        /*
         * ath_hal_printf(
         *     ah, "+++reg 0x%08x gain_table_entries[%d] = 0x%08x \n", 
         *     reg, i, gain_table_entries[i]);
         */
        reg = reg + 4;
    }
}

/* Get gain index for Target power */
static unsigned int ar9300_get_desired_gain_for_chain(struct ath_hal *ah,
    int chain_num, int target_power)
{
    int olpc_gain_delta = 0;
    int alpha_therm = 0, alpha_volt = 0;
    int therm_cal_value = 0, volt_cal_value = 0;
    int latest_therm_value = 0, latest_volt_value = 0, olpc_gain_delta_tmp = 0;
    int thermal_gain_corr = 0, voltage_gain_corr = 0, desired_scale = 0;
    int desired_gain = 0;
    int cl_gain_mod = 0;

    /* Clear the training done bit */
    if (AR_SREV_POSEIDON(ah)) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
    } else {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
    }
    /*field_read("BB_tpc_12.desired_scale_ht40_5", &desired_scale);*/
    desired_scale =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_12,
        AR_PHY_TPC_12_DESIRED_SCALE_HT40_5);
    /*field_read("BB_tpc_19.alpha_therm", &alpha_therm);*/
    alpha_therm =
        OS_REG_READ_FIELD(ah, AR_PHY_TPC_19, AR_PHY_TPC_19_ALPHA_THERM);
    /*field_read("BB_tpc_19.alpha_volt", &alpha_volt);*/
    alpha_volt =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_19, AR_PHY_TPC_19_ALT_ALPHA_VOLT);

    /*field_read("BB_tpc_18.therm_cal_value", &therm_cal_value);*/
    therm_cal_value =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_18,
        AR_PHY_TPC_18_ALT_THERM_CAL_VALUE);
    /*field_read("BB_tpc_18.volt_cal_value", &volt_cal_value);*/
    volt_cal_value =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_18,
        AR_PHY_TPC_18_ALT_VOLT_CAL_VALUE);

    /*field_read("BB_therm_adc_4.latest_therm_value", &latest_therm_value);*/
    latest_therm_value =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_THERM_ADC_4,
        AR_PHY_THERM_ADC_4_LATEST_THERM_VALUE);
    /*field_read("BB_therm_adc_4.latest_volt_value", &latest_volt_value);*/
    latest_volt_value =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_THERM_ADC_4,
        AR_PHY_THERM_ADC_4_LATEST_VOLT_VALUE);

    /*
     * sprintf(
     *     field_name, "%s%d%s%d\0", "BB_tpc_11_b", 
     *     chain_num, ".olpc_gain_delta_", chain_num);
     */
    /*field_read(field_name, &olpc_gain_delta_tmp);*/


    if (chain_num == 0) {
        olpc_gain_delta_tmp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_11_B0,
            AR_PHY_TPC_11_B0_OLPC_GAIN_DELTA_0);
        cl_gain_mod = OS_REG_READ_FIELD_ALT(ah, AR_PHY_CL_TAB_0, 
            AR_PHY_CL_TAB_0_CL_GAIN_MOD);
    } else if (chain_num == 1) {
        if (!AR_SREV_POSEIDON(ah)) {
            olpc_gain_delta_tmp =
                OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_11_B1,
                AR_PHY_TPC_11_B1_OLPC_GAIN_DELTA_1);
            cl_gain_mod = OS_REG_READ_FIELD_ALT(ah, AR_PHY_CL_TAB_1, 
                AR_PHY_CL_TAB_1_CL_GAIN_MOD);
        }
    } else if (chain_num == 2) {
        if (!AR_SREV_POSEIDON(ah)) {
            olpc_gain_delta_tmp =
                OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_11_B2,
                AR_PHY_TPC_11_B2_OLPC_GAIN_DELTA_2);
            cl_gain_mod = OS_REG_READ_FIELD_ALT(ah, AR_PHY_CL_TAB_2, 
                AR_PHY_CL_TAB_2_CL_GAIN_MOD);
        }
    } else {
        /* invalid chain_num */
    }

    if (olpc_gain_delta_tmp < 128) {
        olpc_gain_delta = olpc_gain_delta_tmp;
    } else {
        olpc_gain_delta = olpc_gain_delta_tmp - 256;
    }

    thermal_gain_corr =
        (int) (alpha_therm * (latest_therm_value - therm_cal_value) +
        128) >> 8;
    voltage_gain_corr =
        (int) (alpha_volt * (latest_volt_value - volt_cal_value) + 64) >> 7;
    desired_gain =
        target_power - olpc_gain_delta - thermal_gain_corr -
        voltage_gain_corr + desired_scale + cl_gain_mod;
    /*
     * printf(
     *     "olpc_gain_delta %d, desired_gain %d\n", 
     *     olpc_gain_delta, desired_gain);
     */
#if 0
    ath_hal_printf(ah,
        "+++ target_power %d olpc_gain_delta %d, cl_gain_mod %d,"
        "thermal_gain_corr %d  voltage_gain_corr %d desired_scale %d"
        "desired_gain %d\n",
        target_power, olpc_gain_delta, cl_gain_mod, thermal_gain_corr,
        voltage_gain_corr,
        desired_scale, desired_gain);
#endif
    return (unsigned int) desired_gain;
}

static void ar9300_tx_force_gain(struct ath_hal *ah, unsigned int gain_index)
{
    int selected_gain_entry, txbb1dbgain, txbb6dbgain, txmxrgain;
    int padrvgn_a, padrvgn_b, padrvgn_c, padrvgn_d;
    u_int32_t *gain_table_entries = AH9300(ah)->paprd_gain_table_entries;

    /*u_int32_t *gain_vs_table_index = ah->paprd_gain_table_index;*/
    selected_gain_entry = gain_table_entries[gain_index];
    txbb1dbgain = selected_gain_entry & 0x7;
    txbb6dbgain = (selected_gain_entry >> 3) & 0x3;
    txmxrgain = (selected_gain_entry >> 5) & 0xf;
    padrvgn_a = (selected_gain_entry >> 9) & 0xf;
    padrvgn_b = (selected_gain_entry >> 13) & 0xf;
    padrvgn_c = (selected_gain_entry >> 17) & 0xf;
    padrvgn_d = (selected_gain_entry >> 21) & 0x3;

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXBB1DBGAIN, txbb1dbgain);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXBB6DBGAIN, txbb6dbgain);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXMXRGAIN, txmxrgain);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNA, padrvgn_a);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNB, padrvgn_b);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNC, padrvgn_c);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGND, padrvgn_d);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_ENABLE_PAL, 0);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCE_TX_GAIN, 0);

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TPC_1, AR_PHY_TPC_1_FORCED_DAC_GAIN, 0);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_TPC_1, AR_PHY_TPC_1_FORCE_DAC_GAIN, 0);
}

#define HAL_DEBUG_PAPRD HAL_DEBUG_CALIBRATE  /* driver: conditionally print */

#if defined(ART_PAPRD_DEBUG) || defined(AH_DEBUG)
static void ar9300_paprd_debug_print(struct ath_hal *ah)
{
    int temp;
    int txbb1dbgain, txbb6dbgain, txmxrgain;
    int padrvgn_a, padrvgn_b, padrvgn_c, padrvgn_d;

    if (AR_SREV_POSEIDON(ah)) {
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_lb_skip", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_lb_skip=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_lb_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_lb_enable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_tx_gain_force", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_tx_gain_force=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl1.cf_paprd_rx_bb_gain_force", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_rx_bb_gain_force=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_iqcorr_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_iqcorr_enable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_agc2_settling", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_agc2_settling=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_train_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_train_enable=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl2.cf_paprd_init_rx_bb_gain", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl2.cf_paprd_init_rx_bb_gain=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl3.cf_paprd_fine_corr_len", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_fine_corr_len=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl3.cf_paprd_coarse_corr_len", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_coarse_corr_len=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl3.cf_paprd_num_corr_stages", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_num_corr_stages=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl3.cf_paprd_min_loopback_del", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_min_loopback_del=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl3.cf_paprd_quick_drop", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_quick_drop=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl3.cf_paprd_adc_desired_size", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_adc_desired_size=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl3.cf_paprd_bbtxmix_disable", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_bbtxmix_disable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl4.cf_paprd_safety_delta", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl4.cf_paprd_safety_delta=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl4.cf_paprd_min_corr", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl4.cf_paprd_min_corr=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_agc2_pwr", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_AGC2_PWR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_agc2_pwr              = 0x%02x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_rx_gain_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_RX_GAIN_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_rx_gain_idx           = 0x%02x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_active", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_ACTIVE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_active          = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_corr_err", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_CORR_ERR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_corr_err              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_incomplete", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_INCOMPLETE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_incomplete      = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_done", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_done            = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_fine_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_fine_idx              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_coarse_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_COARSE_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_coarse_idx            = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_fine_val", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_VAL);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_fine_val              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat3.paprd_train_samples_cnt", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT3_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT3_PAPRD_TRAIN_SAMPLES_CNT);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_samples_cnt     = 0x%08x\n", temp);
    } else {
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_lb_skip", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_lb_skip=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_lb_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_lb_enable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_tx_gain_force", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_tx_gain_force=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl1.cf_paprd_rx_bb_gain_force", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_rx_bb_gain_force=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_iqcorr_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_iqcorr_enable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_agc2_settling", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_agc2_settling=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl1.cf_paprd_train_enable", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL1,
            AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl1.cf_paprd_train_enable=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl2.cf_paprd_init_rx_bb_gain", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL2,
            AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl2.cf_paprd_init_rx_bb_gain=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl3.cf_paprd_fine_corr_len", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_fine_corr_len=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl3.cf_paprd_coarse_corr_len", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_coarse_corr_len=0x%x\n", temp);
        /*
         * field_read("BB_paprd_trainer_cntl3.cf_paprd_num_corr_stages", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_num_corr_stages=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl3.cf_paprd_min_loopback_del", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_min_loopback_del=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl3.cf_paprd_quick_drop", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_quick_drop=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl3.cf_paprd_adc_desired_size", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_adc_desired_size=0x%x\n", temp);
        /*
         * field_read(
         *     "BB_paprd_trainer_cntl3.cf_paprd_bbtxmix_disable", &temp);
         */
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
            AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl3.cf_paprd_bbtxmix_disable=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl4.cf_paprd_safety_delta", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl4.cf_paprd_safety_delta=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_cntl4.cf_paprd_min_corr", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL4,
            AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "BB_paprd_trainer_cntl4.cf_paprd_min_corr=0x%x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_agc2_pwr", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_AGC2_PWR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_agc2_pwr              = 0x%02x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_rx_gain_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_RX_GAIN_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_rx_gain_idx           = 0x%02x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_active", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_ACTIVE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_active          = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_corr_err", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_CORR_ERR);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_corr_err              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_incomplete", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_INCOMPLETE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_incomplete      = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat1.paprd_train_done", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_done            = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_fine_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_fine_idx              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_coarse_idx", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_COARSE_IDX);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_coarse_idx            = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat2.paprd_fine_val", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT2,
            AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_VAL);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_fine_val              = 0x%08x\n", temp);
        /*field_read("BB_paprd_trainer_stat3.paprd_train_samples_cnt", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT3,
            AR_PHY_PAPRD_TRAINER_STAT3_PAPRD_TRAIN_SAMPLES_CNT);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    paprd_train_samples_cnt     = 0x%08x\n", temp);
    }

    /*field_read("BB_tpc_1.force_dac_gain", &temp);*/
    temp =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_1, AR_PHY_TPC_1_FORCE_DAC_GAIN);
    HALDEBUG(ah, HAL_DEBUG_PAPRD, "    dac_gain_forced     = 0x%08x\n",
        temp);
    /*field_read("BB_tpc_1.forced_dac_gain", &temp);*/
    temp =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TPC_1, AR_PHY_TPC_1_FORCED_DAC_GAIN);
    HALDEBUG(ah, HAL_DEBUG_PAPRD, "    forced_dac_gain     = 0x%08x\n",
        temp);

    /*field_read("BB_paprd_ctrl0_b0.paprd_enable_0", &temp);*/
    temp =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B0,
        AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0);
    HALDEBUG(ah, HAL_DEBUG_PAPRD,
        "    BB_paprd_ctrl0_b0.paprd_enable_0     = 0x%08x\n", temp);
    if (!AR_SREV_POSEIDON(ah)) {
        /*field_read("BB_paprd_ctrl0_b1.paprd_enable_1", &temp);*/
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B1,
            AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1);
        HALDEBUG(ah, HAL_DEBUG_PAPRD,
            "    BB_paprd_ctrl0_b1.paprd_enable_1     = 0x%08x\n", temp);
        if (AH9300(ah)->ah_tx_chainmask & AR9300_CHAIN2_MASK) {
            /*field_read("BB_paprd_ctrl0_b2.paprd_enable_2", &temp);*/
            temp =
                OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL0_B2,
                AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2);
            HALDEBUG(ah, HAL_DEBUG_PAPRD,
                "    BB_paprd_ctrl0_b2.paprd_enable_2     = 0x%08x\n", temp);
        }
    }

    /*field_read("BB_tx_forced_gain.forced_txbb1dbgain", &txbb1dbgain);*/
    txbb1dbgain =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXBB1DBGAIN);
    /*field_read("BB_tx_forced_gain.forced_txbb6dbgain", &txbb6dbgain);*/
    txbb6dbgain =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXBB6DBGAIN);
    /*field_read("BB_tx_forced_gain.forced_txmxrgain", &txmxrgain);*/
    txmxrgain =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_TXMXRGAIN);
    /*field_read("BB_tx_forced_gain.forced_padrvgn_a", &padrvgn_a);*/
    padrvgn_a =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNA);
    /*field_read("BB_tx_forced_gain.forced_padrvgn_b", &padrvgn_b);*/
    padrvgn_b =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNB);
    /*field_read("BB_tx_forced_gain.forced_padrvgn_c", &padrvgn_c);*/
    padrvgn_c =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNC);
    /*field_read("BB_tx_forced_gain.forced_padrvgn_d", &padrvgn_d);*/
    padrvgn_d =
        OS_REG_READ_FIELD_ALT(ah, AR_PHY_TX_FORCED_GAIN,
        AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGND);

    HALDEBUG(ah, HAL_DEBUG_PAPRD,
        "txbb1dbgain=0x%x, txbb6dbgain=0x%x, txmxrgain=0x%x\n",
        txbb1dbgain, txbb6dbgain, txmxrgain);
    HALDEBUG(ah, HAL_DEBUG_PAPRD,
        "padrvgn_a=0x%x, padrvgn_b=0x%x\n", padrvgn_a, padrvgn_b);
    HALDEBUG(ah, HAL_DEBUG_PAPRD,
        "padrvgn_c=0x%x, padrvgn_d=0x%x\n", padrvgn_c, padrvgn_d);
}
#else
#define ar9300_paprd_debug_print(ah) /* dummy macro */
#endif /* defined(ART_PAPRD_DEBUG) || defined(AH_DEBUG) */

static int ar9300_create_pa_curve(struct ath_hal *ah, u_int32_t * pa_table,
    u_int32_t * small_signal_gain, int * pa_in)
{
    int i;
    int status;
    /*char field_name[100];*/
    u_int32_t paprd_train_data_l[48], paprd_train_data_u[48];
    u_int32_t reg;

    ar9300_paprd_debug_print(ah);
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_CHAN_INFO_MEMORY,
        AR_PHY_CHAN_INFO_MEMORY_CHANINFOMEM_S2_READ, 0);
    reg = AR_PHY_CHAN_INFO_TAB_0;

    for (i = 0; i < 48; i++) {
        /*
         * sprintf(
         *     field_name, "%s%d%s\0", "BB_chan_info_chan_tab_b0[", 
         *     i, "].chaninfo_word");
         */
        /*field_read(field_name, &paprd_train_data_l[i]);*/
        paprd_train_data_l[i] = OS_REG_READ(ah, reg);
        reg = reg + 4;
    }

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_CHAN_INFO_MEMORY,
        AR_PHY_CHAN_INFO_MEMORY_CHANINFOMEM_S2_READ, 1);
    reg = AR_PHY_CHAN_INFO_TAB_0;

    for (i = 0; i < 48; i++) {
        /*
         * sprintf(
         *     field_name, "%s%d%s\0", "BB_chan_info_chan_tab_b0[", 
         *     i, "].chaninfo_word");
         */
        /*field_read(field_name, &paprd_train_data_u[i]);*/
        paprd_train_data_u[i] = OS_REG_READ(ah, reg);
        reg = reg + 4;
    }

    /*
     * for(i=0; i<48; i++)
     *     ath_hal_printf(
     *         ah, "%08x%08x\n", paprd_train_data_u[i], paprd_train_data_l[i]);
     */
    status = 0;
    if (create_pa_curve(
            paprd_train_data_l, paprd_train_data_u, 
            pa_table, small_signal_gain, pa_in) ==
            AH_FALSE)
    {
        status = -2;
    }
    /* Clear the training done bit */
    if (AR_SREV_POSEIDON(ah)) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
    } else {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
            AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
    }
    return status;
}

static int find_expn(int num)
{
    int tmp, exp;

    exp = 0;
    tmp = num >> 1;

    while (tmp != 0) {
        tmp = tmp >> 1;
        exp++;
    }

    return exp;
}

static int find_proper_scale(int expn, int n)
{
    int q_pw;

    q_pw = (expn > n) ? expn - 10 : 0;
    return q_pw;
}

static int find_max(int *array, int length)
{
    int i, loc_max;

    loc_max = 0;

    for (i = 0; i < length; i++) {
        if (array[i] > loc_max) {
            loc_max = array[i];
        }
    }

    return loc_max;
}

static int paprd_abs(int num)
{
    if (num < 0) {
        return -num;
    }
    return num;
}

#define NUM_BIN 23

HAL_BOOL create_pa_curve(u_int32_t * paprd_train_data_l,
    u_int32_t * paprd_train_data_u, u_int32_t * pa_table, 
    u_int32_t * g_fxp_ext, int *pa_in)
{
    unsigned int accum_cnt[NUM_BIN + 1];
    unsigned int accum_tx[NUM_BIN + 1];
    unsigned int accum_rx[NUM_BIN + 1];
    unsigned int accum_ang[NUM_BIN + 1];
    unsigned int thresh_accum_cnt;

    int max_index;
    int scale_factor;

    int x_est[NUM_BIN + 1];
    int y[NUM_BIN + 1];
    int theta[NUM_BIN + 1];
    int y_sqr[NUM_BIN + 1];
    int y_quad[NUM_BIN + 1];
    int theta_tilde[NUM_BIN + 1];
    int pa_angle[NUM_BIN + 1];

    int b1_tmp[NUM_BIN + 1];
    int b2_tmp[NUM_BIN + 1];
    int b1_abs[NUM_BIN + 1];
    int b2_abs[NUM_BIN + 1];

    int y_lin[NUM_BIN + 1];
    int y_est[NUM_BIN + 1];
    int x_est_fxp1_nonlin[NUM_BIN + 1];
    int x_tilde[NUM_BIN + 1];
    int x_tilde_abs[NUM_BIN + 1];

    int g_fxp;
    int y_intercept;
    int order_x_by_y;
    int m, half_lo, half_hi;
    int sum_y_sqr;
    int sum_y_quad;
    int q_x, q_b1, q_b2;
    int beta_raw, alpha_raw, scale_b;
    int q_scale_b, q_beta, q_alpha;
    int alpha, beta;
    int order_1, order_2;
    int order1_5x, order2_3x;
    int order1_5x_rem, order2_3x_rem;
    int y5, y3, tmp;
    int bin, idx;
    int theta_low_bin = 0;

    /*
     * [15:00] u16, accum_cnt[15:00]: number of samples in the bin
     * [42:16] u27, accum_tx[26:00]: sum(tx amplitude) of the bin
     * [63:43] u21, accum_rx[20:00]: 
     *     sum(rx amplitude distance to lower bin edge) of the bin
     * [90:64] s27, accum_ang[26:00]: sum(angles) of the bin
     */
    max_index = 0;
    /*
     * Disregard any bin that contains less than 
     * or equal to 16 counts of samples
     */
    thresh_accum_cnt = 16;      
    scale_factor = 5;

    for (bin = 0; bin < NUM_BIN; bin++) {
        accum_cnt[bin] = paprd_train_data_l[bin] & 0xffff;
        /* lower 16 bit OR-ed  upper 11 bits */
        accum_tx[bin] = 
            ((paprd_train_data_l[bin] >> 16) & 0xffff) | 
            ((paprd_train_data_u[bin] & 0x7ff) << 16);
        accum_rx[bin] =
            ((paprd_train_data_u[bin] >> 11) & 0x1f) |
             ((paprd_train_data_l[bin + 23] & 0xffff) << 5);
        accum_ang[bin] =
            ((paprd_train_data_l[bin + 23] >> 16) & 0xffff) |
             ((paprd_train_data_u[bin + 23] & 0x7ff) << 16);
        /*
         * printf(
         *     "%d\t%d\t%d\t%d\n", accum_cnt[bin], accum_tx[bin],
         *     accum_rx[bin], accum_ang[bin]);
         */
        if (accum_cnt[bin] > thresh_accum_cnt) {
            /* accum_cnt[i] will be non-zero at this point */
            x_est[bin + 1] =
                ((((accum_tx[bin] << scale_factor) +
                    accum_cnt[bin]) / accum_cnt[bin]) + 32) >> scale_factor;
            y[bin + 1] =
                (((((accum_rx[bin] << scale_factor) +
                     accum_cnt[bin]) / accum_cnt[bin]) + 32) >> scale_factor) +
                (1 << scale_factor) * max_index + 16;
            if (accum_ang[bin] >= (1 << 26)) {
                theta[bin + 1] =
                    ((accum_ang[bin] - (1 << 27)) * (1 << scale_factor) +
                    accum_cnt[bin]);
                theta[bin + 1] = theta[bin + 1] / (int) accum_cnt[bin];
                /*
                 *  theta[i+1] = 
                 *      ((accum_ang[i] - (1 << 27)) * 
                 *      (1 << scale_factor) + zz) / zz;
                 */
            } else {
                theta[bin + 1] =
                    ((accum_ang[bin] * (1 << scale_factor)) +
                    accum_cnt[bin]) / accum_cnt[bin];
            }
            max_index++;
        }
        /*
         * printf(
         *     "i=%d, theta[i+1]=%d\t%d\t%d\t%d\t%d\n", 
         *     i, theta[i+1], accum_cnt[i], 
         *     accum_tx[i], accum_rx[i], accum_ang[i]);
         */
    }

    /*
     * Find average theta of first 5 bin and all of those to same value. 
     * Curve is linear at that range.
     */
    for (bin = 1; bin < 6; bin++) {
        theta_low_bin += theta[bin];
    }
    theta_low_bin = theta_low_bin / 5;
    for (bin = 1; bin < 6; bin++) {
        theta[bin] = theta_low_bin;
    }

    /* Set values at origin */
    theta[0] = theta_low_bin;

    for (bin = 0; bin <= max_index; bin++) {
        theta[bin] = theta[bin] - theta_low_bin;
        /*printf("bin=%d, theta[bin] = %d\n", bin, theta[bin]);*/
    }

    x_est[0] = 0;
    y[0] = 0;
    scale_factor = 8;
    /* low signal gain */
    if (x_est[6] == x_est[3]) {
        return AH_FALSE;
    }
    g_fxp =
        (((y[6] - y[3]) * 1 << scale_factor) + (x_est[6] - x_est[3])) /
        (x_est[6] - x_est[3]);
    if (g_fxp == 0) {
        /*
         * ath_hal_printf(
         *     NULL, "%s[%d] Potential divide by zero error\n",
         *     __func__, __LINE__);
         */
        return AH_FALSE;
    }

    for (bin = 0; bin <= max_index; bin++) {
        y_lin[bin] =
            (g_fxp * (x_est[bin] - x_est[3]) + (1 << scale_factor)) /
            (1 << scale_factor) + y[3];
    }
    y_intercept = y_lin[0];

    for (bin = 0; bin <= max_index; bin++) {
        y_est[bin] = y[bin] - y_intercept;
        y_lin[bin] = y_lin[bin] - y_intercept;
    }

    for (bin = 0; bin <= 3; bin++) {
        y_est[bin] = bin * 32;
        /* g_fxp was checked for zero already */
        x_est[bin] = ((y_est[bin] * 1 << scale_factor) + g_fxp) / g_fxp;
    }

    /*
     *  for (bin = 0; bin <= max_index; bin++) {
     *      printf("y_est[%d] = %d, x_est[%d]=%d\n",
     *          bin, y_est[bin], bin, x_est[bin]);
     *  }
     */
    for (bin = 0; bin <= max_index; bin++) {
        x_est_fxp1_nonlin[bin] =
            x_est[bin] - ((1 << scale_factor) * y_est[bin] + g_fxp) / g_fxp;
        /*printf("x_est_fxp1_nonlin[%d] = %d\n", bin, x_est_fxp1_nonlin[bin]);*/
    }

    /* Check for divide by 0 */
    if (y_est[max_index] == 0) {
        return AH_FALSE;
    }
    order_x_by_y =
        (x_est_fxp1_nonlin[max_index] + y_est[max_index]) / y_est[max_index];
    if (order_x_by_y == 0) {
        m = 10;
    } else if (order_x_by_y == 1) {
        m = 9;
    } else {
        m = 8;
    }

    half_lo = (max_index > 15) ? 7 : max_index >> 1;
    half_hi = max_index - half_lo;
    scale_factor = 8;
    sum_y_sqr = 0;
    sum_y_quad = 0;

    for (bin = 0; bin <= half_hi; bin++) {
        if (y_est[bin + half_lo] == 0) {
            /*
             * ath_hal_printf(
             *     NULL, "%s Potential divide by zero error\n", __func__);
             */
            return AH_FALSE;
        }

        x_tilde[bin] =
            (x_est_fxp1_nonlin[bin + half_lo] * (1 << m) +
             y_est[bin + half_lo]) / y_est[bin + half_lo];
        x_tilde[bin] = (x_tilde[bin] * (1 << m) + y_est[bin + half_lo]) /
            y_est[bin + half_lo];
        x_tilde[bin] = (x_tilde[bin] * (1 << m) + y_est[bin + half_lo]) /
            y_est[bin + half_lo];

        y_sqr[bin] =
            (y_est[bin + half_lo] * y_est[bin + half_lo] +
             (scale_factor * scale_factor)) / (scale_factor * scale_factor);
        x_tilde_abs[bin] = paprd_abs(x_tilde[bin]);
        y_quad[bin] = y_sqr[bin] * y_sqr[bin];
        sum_y_sqr = sum_y_sqr + y_sqr[bin];
        sum_y_quad = sum_y_quad + y_quad[bin];
    }

    /*printf("sum_y_sqr = %d, sum_y_quad=%d\n", sum_y_sqr, sum_y_quad);*/

    for (bin = 0; bin <= half_hi; bin++) {
        b1_tmp[bin] = y_sqr[bin] * (half_hi + 1) - sum_y_sqr;
        b2_tmp[bin] = sum_y_quad - sum_y_sqr * y_sqr[bin];
        b1_abs[bin] = paprd_abs(b1_tmp[bin]);
        b2_abs[bin] = paprd_abs(b2_tmp[bin]);

        /*
         * printf(
         *     "bin=%d, b1_tmp[bin] = %d, b2_tmp[bin] = %d\n", 
         *     bin, b1_tmp[bin], b2_tmp[bin]);
         */
    }

    q_x = find_proper_scale(find_expn(find_max(x_tilde_abs, half_hi + 1)), 10);
    q_b1 = find_proper_scale(find_expn(find_max(b1_abs, half_hi + 1)), 10);
    q_b2 = find_proper_scale(find_expn(find_max(b2_abs, half_hi + 1)), 10);

    beta_raw = 0;
    alpha_raw = 0;

    for (bin = 0; bin <= half_hi; bin++) {
        x_tilde[bin] = x_tilde[bin] / (1 << q_x);
        b1_tmp[bin] = b1_tmp[bin] / (1 << q_b1);
        b2_tmp[bin] = b2_tmp[bin] / (1 << q_b2);

        /*
         * printf(
         *     "bin=%d, b1_tmp[bin]=%d b2_tmp[bin]=%d x_tilde[bin] = %d\n", 
         *     bin, b1_tmp[bin], b2_tmp[bin], x_tilde[bin]);
         */
        beta_raw = beta_raw + b1_tmp[bin] * x_tilde[bin];
        alpha_raw = alpha_raw + b2_tmp[bin] * x_tilde[bin];
    }

    scale_b =
        ((sum_y_quad / scale_factor) * (half_hi + 1) -
        (sum_y_sqr / scale_factor) * sum_y_sqr) * scale_factor;
    q_scale_b = find_proper_scale(find_expn(paprd_abs(scale_b)), 10);
    scale_b = scale_b / (1 << q_scale_b);
    /* Check for divide by 0 */
    if (scale_b == 0) {
        return AH_FALSE;
    }
    q_beta = find_proper_scale(find_expn(paprd_abs(beta_raw)), 10);
    q_alpha = find_proper_scale(find_expn(paprd_abs(alpha_raw)), 10);

    beta_raw = beta_raw / (1 << q_beta);
    alpha_raw = alpha_raw / (1 << q_alpha);
    alpha = (alpha_raw << 10) / scale_b;
    beta = (beta_raw << 10) / scale_b;
    order_1 = 3 * m - q_x - q_b1 - q_beta + 10 + q_scale_b;
    order_2 = 3 * m - q_x - q_b2 - q_alpha + 10 + q_scale_b;

    order1_5x = order_1 / 5;
    order2_3x = order_2 / 3;

    order1_5x_rem = order_1 - 5 * order1_5x;
    order2_3x_rem = order_2 - 3 * order2_3x;

    for (idx = 0; idx < AR9300_PAPRD_TABLE_SZ; idx++) {
        tmp = idx * 32;
        y5 = ((beta * tmp) >> 6) >> order1_5x;
        y5 = (y5 * tmp) >> order1_5x;
        y5 = (y5 * tmp) >> order1_5x;
        y5 = (y5 * tmp) >> order1_5x;
        y5 = (y5 * tmp) >> order1_5x;

        y5 = y5 >> order1_5x_rem;
        y3 = (alpha * tmp) >> order2_3x;
        y3 = (y3 * tmp) >> order2_3x;
        y3 = (y3 * tmp) >> order2_3x;

        y3 = y3 >> order2_3x_rem;
        /* g_fxp was checked for zero already */
        pa_in[idx] = y5 + y3 + (256 * tmp) / g_fxp;
    }

    for (idx = 1; idx < 23; idx++) {
        tmp = pa_in[idx + 1] - pa_in[idx];
        if (tmp < 0) {
            pa_in[idx + 1] = pa_in[idx] + (pa_in[idx] - pa_in[idx - 1]);
        }
    }

    for (idx = 0; idx < AR9300_PAPRD_TABLE_SZ; idx++) {
        pa_in[idx] = (pa_in[idx] < 1400) ? pa_in[idx] : 1400;
        /*printf("idx=%d, pa_in[idx]=%d\n", i, pa_in[idx]);*/
    }

    beta_raw = 0;
    alpha_raw = 0;

    for (bin = 0; bin <= half_hi; bin++) {
        /*
         *  printf(
         *      "bin=%d half_lo=%d m=%d theta[bin+half_lo]=%d "
         *      "y_est[bin+half_lo]=%d\n", 
         *      bin, half_lo, m, theta[bin+half_lo], y_est[bin+half_lo]);
         */
        /* y_est[] was already checked for zero */
        theta_tilde[bin] =
            ((theta[bin + half_lo] << m) + y_est[bin + half_lo]) /
            y_est[bin + half_lo];
        theta_tilde[bin] = ((theta_tilde[bin] << m) + y_est[bin + half_lo]) /
            y_est[bin + half_lo];
        theta_tilde[bin] = ((theta_tilde[bin] << m) + y_est[bin + half_lo]) /
            y_est[bin + half_lo];

        /*printf("bin=%d theta_tilde[bin]=%d\n", bin, theta_tilde[bin]);*/
        beta_raw = beta_raw + b1_tmp[bin] * theta_tilde[bin];
        alpha_raw = alpha_raw + b2_tmp[bin] * theta_tilde[bin];

        /*
        printf("bin=%d, alpha_raw=%d, beta_raw=%d\n", bin, alpha_raw, beta_raw);
         */
    }

    q_beta = find_proper_scale(find_expn(paprd_abs(beta_raw)), 10);
    q_alpha = find_proper_scale(find_expn(paprd_abs(alpha_raw)), 10);

    beta_raw = beta_raw / (1 << q_beta);
    alpha_raw = alpha_raw / (1 << q_alpha);
    /* scale_b checked for zero previously */
    alpha = (alpha_raw << 10) / scale_b;
    beta = (beta_raw << 10) / scale_b;
    order_1 = 3 * m - q_x - q_b1 - q_beta + 10 + q_scale_b + 5;
    order_2 = 3 * m - q_x - q_b2 - q_alpha + 10 + q_scale_b + 5;

    order1_5x = order_1 / 5;
    order2_3x = order_2 / 3;

    order1_5x_rem = order_1 - 5 * order1_5x;
    order2_3x_rem = order_2 - 3 * order2_3x;

    for (idx = 0; idx < AR9300_PAPRD_TABLE_SZ; idx++) {
        tmp = idx * 32;

        if (beta > 0) {
            y5 = (((beta * tmp - 64) >> 6) -
                (1 << order1_5x)) / (1 << order1_5x);
        } else {
            y5 = ((((beta * tmp - 64) >> 6) +
                    (1 << order1_5x)) / (1 << order1_5x));
        }

        y5 = (y5 * tmp) / (1 << order1_5x);
        y5 = (y5 * tmp) / (1 << order1_5x);
        y5 = (y5 * tmp) / (1 << order1_5x);
        y5 = (y5 * tmp) / (1 << order1_5x);

        y5 = y5 / (1 << order1_5x_rem);

        if (beta > 0) {
            y3 = (alpha * tmp - (1 << order2_3x)) / (1 << order2_3x);
        } else {
            y3 = (alpha * tmp + (1 << order2_3x)) / (1 << order2_3x);
        }

        y3 = (y3 * tmp) / (1 << order2_3x);
        y3 = (y3 * tmp) / (1 << order2_3x);

        y3 = y3 / (1 << order2_3x_rem);
        pa_angle[idx] = y5 + y3;
        /*printf("idx=%d, y5 = %d, y3=%d\n", idx, y5, y3);*/
        pa_angle[idx] =
            (pa_angle[idx] < -150) ? -150 : ((pa_angle[idx] >
                150) ? 150 : pa_angle[idx]);
    }

    pa_angle[0] = 0;
    pa_angle[1] = 0;
    pa_angle[2] = 0;
    pa_angle[3] = 0;

    pa_angle[4] = (pa_angle[5] + 2) >> 1;

    for (idx = 0; idx < AR9300_PAPRD_TABLE_SZ; idx++) {
        pa_table[idx] = ((pa_in[idx] & 0x7ff) << 11) + (pa_angle[idx] & 0x7ff);
        /*
         * HALDEBUG(
         *     NULL, HAL_DEBUG_UNMASKABLE,"%d\t%d\t0x%x\n", 
         *     pa_in[idx], pa_angle[idx], pa_table[idx]);
         */
    }

    /*HALDEBUG(NULL, HAL_DEBUG_UNMASKABLE, "g_fxp = %d\n", g_fxp);*/
    *g_fxp_ext = g_fxp;
    return AH_TRUE;
}

// Due to a hardware bug, when transmitting with just one chain the papd
// data for chain 0 is always used. So when using chain 2 or 4, the 
// corresponding data must be copied into the chain 0 area.
void ar9300_swizzle_paprd_entries(struct ath_hal *ah, unsigned int txchain)
{
    int i;
    u_int32_t *paprd_table_val = NULL;
    u_int32_t small_signal_gain = 0;
    u_int32_t reg = 0;

    reg = AR_PHY_PAPRD_MEM_TAB_B0;
    switch (txchain) {
    case 0x1: 
    case 0x3:
    case 0x7:
	paprd_table_val = &AH9300(ah)->pa_table[0][0];
	small_signal_gain = AH9300(ah)->small_signal_gain[0];
	break;
    case 0x2:
	paprd_table_val = &AH9300(ah)->pa_table[1][0];
	small_signal_gain = AH9300(ah)->small_signal_gain[1];
	break;
    case 0x4:
	paprd_table_val = &AH9300(ah)->pa_table[2][0];
	small_signal_gain = AH9300(ah)->small_signal_gain[2];
	break;
    default:
	// Error out.
	ath_hal_printf(ah, "YAK! Bad chain mask %x\n", txchain);
	return;
    }
    for (i = 0; i < AR9300_PAPRD_TABLE_SZ; i++) { 
        OS_REG_WRITE(ah, reg, paprd_table_val[i]);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n", __func__,
		 __LINE__, reg, paprd_table_val[i]);
        
        reg = reg + 4;
    }
    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PA_GAIN123_B0,AR_PHY_PA_GAIN123_B0_PA_GAIN1_0, small_signal_gain);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x small_signal_gain 0x%08x\n", __func__, __LINE__,
	     (unsigned) AR_PHY_PA_GAIN123_B0, OS_REG_READ(ah, AR_PHY_PA_GAIN123_B0));

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0, AR_PHY_PAPRD_CTRL1_B0_PAPRD_POWER_AT_AM2AM_CAL_0,
			 AH9300(ah)->paprd_training_power);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n", __func__, __LINE__, 
	     (unsigned) AR_PHY_PAPRD_CTRL1_B0, OS_REG_READ(ah, AR_PHY_PAPRD_CTRL1_B0));

}

void ar9300_populate_paprd_single_table(struct ath_hal *ah,
    struct ieee80211_channel *chan, int chain_num)
{
    int i, j, bad_read = 0;
#ifdef	AH_DEBUG
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);
#endif
    u_int32_t *paprd_table_val = &AH9300(ah)->pa_table[chain_num][0];
    u_int32_t small_signal_gain = AH9300(ah)->small_signal_gain[chain_num];
    u_int32_t reg = 0;

    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "%s[%d]: channel %d paprd_done %d write %d\n", __func__, __LINE__,
        ichan->channel, ichan->paprd_done, ichan->paprd_table_write_done);

    if (chain_num == 0) {
        reg = AR_PHY_PAPRD_MEM_TAB_B0;
    } else if (chain_num == 1) {
        reg = AR_PHY_PAPRD_MEM_TAB_B1;
    } else if (chain_num == 2) {
        reg = AR_PHY_PAPRD_MEM_TAB_B2;
    }

    for (i = 0; i < AR9300_PAPRD_TABLE_SZ; i++) {
        if (AR_SREV_POSEIDON(ah)) {
            HALASSERT(chain_num == 0x1);
            if ((reg == AR_PHY_PAPRD_MEM_TAB_B1) || 
                (reg == AR_PHY_PAPRD_MEM_TAB_B2)) {
                continue;
            }
        }
        /*
         * sprintf(
         *     field_name, "%s%d[%d]%s\0", "BB_paprd_mem_tab_b", 
         *     chain_num, i, ".paprd_mem");
         */
        OS_REG_WRITE(ah, reg, paprd_table_val[i]);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n", __func__,
            __LINE__, reg, paprd_table_val[i]);
        /*
         * printf(
         *     "%s[%d] reg %08x = 0x%08x\n", 
         *     __func__, __LINE__, reg, paprd_table_val[i]);
         */
         if (OS_REG_READ(ah, reg) == 0xdeadbeef) {
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, 
                     "%s: Reg0x%x = 0xdeadbeef\n", __func__, reg);
            bad_read++;
            for (j = AR_PHY_PAPRD_MEM_TAB_B0; j < (AR_PHY_PAPRD_MEM_TAB_B0 + 0x10); j+=4)
            {
                if (OS_REG_READ(ah, j) == 0xdeadbeef) {
                    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, 
                             "%s: Reg0x%x = 0xdeadbeef\n", __func__, j);
                    bad_read++;
                }
            }
            for (j = AR_PHY_PAPRD_MEM_TAB_B1; j < (AR_PHY_PAPRD_MEM_TAB_B1 + 0x10); j+=4)
            {
                if (OS_REG_READ(ah, j) == 0xdeadbeef) {
                    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, 
                             "%s: Reg0x%x = 0xdeadbeef\n", __func__, j);
                    bad_read++;
                }
            }
         }
         
        reg = reg + 4;
    }

    if (bad_read > 4) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, 
                 "%s: Get %d 0xdeadbeef. Mark PAPRD as broken.\n", 
                 __func__, bad_read);
        AH9300(ah)->ah_paprd_broken = AH_TRUE;
    }

    if (chain_num == 0) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PA_GAIN123_B0,
            AR_PHY_PA_GAIN123_B0_PA_GAIN1_0, small_signal_gain);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
            "%s[%d] reg %08x small_signal_gain 0x%08x\n", __func__, __LINE__,
            (unsigned) AR_PHY_PA_GAIN123_B0,
            OS_REG_READ(ah, AR_PHY_PA_GAIN123_B0));
    } else if (chain_num == 1) {
        if (!AR_SREV_POSEIDON(ah) && !AR_SREV_HORNET(ah) && !AR_SREV_APHRODITE(ah)) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PA_GAIN123_B1,
                AR_PHY_PA_GAIN123_B1_PA_GAIN1_1, small_signal_gain);
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s[%d] reg %08x small_signal_gain 0x%08x\n",
                __func__, __LINE__,
                (unsigned) AR_PHY_PA_GAIN123_B1,
                OS_REG_READ(ah, AR_PHY_PA_GAIN123_B1));
        }
    } else if (chain_num == 2) {
        if (!AR_SREV_POSEIDON(ah) && !AR_SREV_HORNET(ah) && !AR_SREV_APHRODITE(ah)) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PA_GAIN123_B2,
                AR_PHY_PA_GAIN123_B2_PA_GAIN1_2, small_signal_gain);
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s[%d] reg %08x small_signal_gain 0x%08x\n",
                __func__, __LINE__,
                (unsigned) AR_PHY_PA_GAIN123_B2,
                OS_REG_READ(ah, AR_PHY_PA_GAIN123_B2));
        }
    } else {
        /* invalid channel number */
    }

    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B0,
        AR_PHY_PAPRD_CTRL1_B0_PAPRD_POWER_AT_AM2AM_CAL_0,
        AH9300(ah)->paprd_training_power);
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n", __func__,
        __LINE__, (unsigned) AR_PHY_PAPRD_CTRL1_B0,
        OS_REG_READ(ah, AR_PHY_PAPRD_CTRL1_B0));
    if (!AR_SREV_POSEIDON(ah) && !AR_SREV_HORNET(ah) && !AR_SREV_APHRODITE(ah)) {
        OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B1,
            AR_PHY_PAPRD_CTRL1_B1_PAPRD_POWER_AT_AM2AM_CAL_1,
            AH9300(ah)->paprd_training_power);
        HALDEBUG(ah, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n", __func__,
            __LINE__, (unsigned) AR_PHY_PAPRD_CTRL1_B1,
            OS_REG_READ(ah, AR_PHY_PAPRD_CTRL1_B1));
        if (!AR_SREV_WASP(ah) && !AR_SREV_JUPITER(ah)) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_CTRL1_B2,
                AR_PHY_PAPRD_CTRL1_B2_PAPRD_POWER_AT_AM2AM_CAL_2,
                AH9300(ah)->paprd_training_power);
            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                "%s[%d] reg %08x = 0x%08x\n", __func__,
                __LINE__, (unsigned) AR_PHY_PAPRD_CTRL1_B2,
                OS_REG_READ(ah, AR_PHY_PAPRD_CTRL1_B2));
        }
    }
    /*ar9300_enable_paprd(ah, AH_TRUE);*/
}

HAL_STATUS ar9300_paprd_setup_gain_table(struct ath_hal *ah, int chain_num)
{
    unsigned int i, desired_gain, gain_index;
    HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
        "Run papredistortion single table algorithm:: Training power = %d\n",
        AH9300(ah)->paprd_training_power / 2);

    if (AH9300(ah)->ah_tx_chainmask & (1 << chain_num)) {
        /* this is an active chain */
        desired_gain = ar9300_get_desired_gain_for_chain(
            ah, chain_num, AH9300(ah)->paprd_training_power);
        /* find out gain index */
        gain_index = 0;

        for (i = 0; i < 32; i++) {
            if (AH9300(ah)->paprd_gain_table_index[i] < desired_gain) {
                gain_index = gain_index + 1;
            } else {
                break;
            }
        }

        /*printf("gain_index = %d\n", gain_index);*/
        /*ath_hal_printf(ah, "++++ gain_index = %d\n", gain_index);*/
        ar9300_tx_force_gain(ah, gain_index);
        if (AR_SREV_POSEIDON(ah)) {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
                AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
        } else {
            OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
                AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE, 0);
        }
    }

    return HAL_OK;
}

static HAL_BOOL ar9300_paprd_retrain_pain(struct ath_hal * ah, int * pa_in)
{
    int count = 0, i;
    int capdiv_offset = 0, quick_drop_offset;
    int capdiv2g, quick_drop;

    capdiv2g = (OS_REG_READ(ah, AR_PHY_65NM_CH0_TXRF3) >> 1) & 0xF;
    if (!AR_SREV_POSEIDON(ah)) {
        quick_drop = 
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3,
                AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP);
    } else {
        quick_drop = 
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON,
                AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP);
    }

    if ( quick_drop != 0 ) {
        quick_drop -= 0x40; 
    }
    for (i = 0; i < (NUM_BIN + 1); i++) {
        if (pa_in[i] == 1400) {
            count++;
        }            
    }

    if (AR_SREV_POSEIDON(ah)) {
        if ((pa_in[23] < 800) || (pa_in[23] == 1400)) {
            if (pa_in[23] < 800) {
                capdiv_offset = (int)((1000 - pa_in[23] + 75) / 150);
                capdiv2g = capdiv2g + capdiv_offset;
                if (capdiv2g > 7) {
                    capdiv2g = 7;
                    if (pa_in[23] < 600) {
                        quick_drop = quick_drop + 1;
                        if (quick_drop > 0) {
                            quick_drop = 0;
                        }
                    }
                }
                
                OS_REG_RMW_FIELD(ah, 
                    AR_PHY_65NM_CH0_TXRF3, 
    			    AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
    			    capdiv2g);

                OS_REG_RMW_FIELD_ALT(ah, 
                    AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON, 
                    AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, 
                    quick_drop);
                
                return AH_TRUE;
            } /* end of if (pa_in[23] < 800) */
            else if (pa_in[23] == 1400) {
                quick_drop_offset = (int)(count / 3);
                if (quick_drop_offset > 2) {
                    quick_drop_offset = 2;
                }
                quick_drop = quick_drop + quick_drop_offset;
                capdiv2g = capdiv2g + (int)(quick_drop_offset / 2); 
                if (capdiv2g > 7) {
                    capdiv2g = 7;
                }
                if (quick_drop > 0) {
                    quick_drop = 0;
                    capdiv2g = capdiv2g - (int)(quick_drop_offset / 1);
    				if (capdiv2g < 0) {
                        capdiv2g = 0;
    				}
                }
                OS_REG_RMW_FIELD(ah, 
                        AR_PHY_65NM_CH0_TXRF3, 
    			        AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
    			        capdiv2g);

                OS_REG_RMW_FIELD_ALT(ah, 
                        AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON, 
        			    AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, 
                        quick_drop);
                
                return AH_TRUE;
                /* sleep(1); */            
            } /* end of if (pa_in[23] == 1400)*/
        } /* end of if ((pa_in[23] < 800) || (pa_in[23] == 1400)) */
    }else if (AR_SREV_HORNET(ah)) {
        if ((pa_in[23] < 1000) || (pa_in[23] == 1400)) {
            if (pa_in[23] < 1000) {
                capdiv_offset = ((1000 - pa_in[23]) / 100);   
                capdiv2g = capdiv2g + capdiv_offset;
                if (capdiv_offset > 3) {
                    quick_drop_offset = 1;
                    quick_drop = quick_drop - quick_drop_offset;
                    capdiv2g = capdiv2g + 1;
                    if (capdiv2g > 6) {
                        capdiv2g = 6;
                    }
                    if (quick_drop < -4) {
                        quick_drop = -4;
                    }
                    OS_REG_RMW_FIELD(ah, 
                        AR_PHY_65NM_CH0_TXRF3, 
                        AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
                        capdiv2g);
                    OS_REG_RMW_FIELD_ALT(ah, 
                        AR_PHY_PAPRD_TRAINER_CNTL3, 
                        AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, 
                        quick_drop);
                    return AH_TRUE;
                } else {
                    capdiv2g = capdiv2g + capdiv_offset;
                    if (capdiv2g > 6) {
                        capdiv2g = 6;
                    }
                    if (quick_drop < -4) {
                        quick_drop = -4;
                    }
                    OS_REG_RMW_FIELD(ah, 
                        AR_PHY_65NM_CH0_TXRF3, 
                        AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
                        capdiv2g);
                    OS_REG_RMW_FIELD_ALT(ah, 
                        AR_PHY_PAPRD_TRAINER_CNTL3, 
                        AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, 
                        quick_drop);
                    return AH_TRUE;
                }
            } /* end of if (PA_in[23] < 1000) */
            else if (pa_in[23] == 1400) {
                if (count > 3) {
                    quick_drop_offset = 1;
                    quick_drop = quick_drop + quick_drop_offset;
                    capdiv2g = capdiv2g - (count / 4);  
                    if (capdiv2g < 0) {
                        capdiv2g = 0;
                    }
                    if (quick_drop > -2) {
                        quick_drop = -2;
                    }
                    OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_TXRF3, 
                        AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
                        capdiv2g);
                    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3, 
                        AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP,
                        quick_drop);
                    return AH_TRUE;
                } else {
                    capdiv2g = capdiv2g - 1;  
                    if (capdiv2g < 0) {
                        capdiv2g = 0;
                    }
                    OS_REG_RMW_FIELD(ah, AR_PHY_65NM_CH0_TXRF3, 
                        AR_PHY_65NM_CH0_TXRF3_CAPDIV2G, 
                        capdiv2g);
                    OS_REG_RMW_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_CNTL3, 
                        AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP, 
                        quick_drop);
                    return AH_TRUE; 
                }
            } /* end of if (PA_in[23] == 1400)*/
        } /* end of if ((PA_in[23] < 1000) || (PA_in[23] == 1400)) */
    }

    return AH_FALSE;
}

HAL_STATUS ar9300_paprd_create_curve(struct ath_hal * ah,
  struct ieee80211_channel * chan, int chain_num)
{
    int status = 0;
    u_int32_t *pa_table, small_signal_gain;
    int pa_in[NUM_BIN + 1];

    if (AH9300(ah)->ah_tx_chainmask & (1 << chain_num)) {
        pa_table = &AH9300(ah)->pa_table[chain_num][0];
        /* Compute PA table and gain index */
        status = ar9300_create_pa_curve(ah, &pa_table[0], &small_signal_gain, 
                    &pa_in[0]);

		if (AR_SREV_WASP(ah)) {
			OS_DELAY(1000);
		}

        if (status != 0) {
            ath_hal_printf(ah, "ERROR:: paprd failed with error code = %d\n",
                status);
            return -1;
        }
        AH9300(ah)->small_signal_gain[chain_num] = small_signal_gain;

        if (AR_SREV_POSEIDON(ah) || AR_SREV_HORNET(ah)) {
            if (ar9300_paprd_retrain_pain(ah, pa_in)) {
                /* need re-train PAPRD */
                return HAL_EINPROGRESS;
		    }
        }
    }
    return HAL_OK;
}

int ar9300_paprd_init_table(struct ath_hal *ah, struct ieee80211_channel * chan)
{
    HAL_CHANNEL_INTERNAL *ichan = ath_hal_checkchannel(ah, chan);

    if ((AR_SREV_WASP(ah) && IS_CHAN_5GHZ(ichan)) ||
         ar9300_paprd_setup_single_table(ah, chan)) {
        goto FAIL;
    }
    OS_MEMZERO(AH9300(ah)->paprd_gain_table_entries,
        sizeof(AH9300(ah)->paprd_gain_table_entries));
    OS_MEMZERO(AH9300(ah)->paprd_gain_table_index,
        sizeof(AH9300(ah)->paprd_gain_table_index));

    ar9300_gain_table_entries(ah);
    return 0;
FAIL:
    return -1;
}

int ar9300_paprd_is_done(struct ath_hal *ah)
{
    int temp, agc2_pwr;

    /*field_read("BB_paprd_trainer_stat1.paprd_train_done", &temp);*/
    if (!AR_SREV_POSEIDON(ah)) {
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
                AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE);

        if (temp == 0x1) {
            agc2_pwr =
                OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1,
                   AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_AGC2_PWR);

            HALDEBUG(ah, HAL_DEBUG_CALIBRATE,
                   "%s AGC2_PWR=0x%2x Training done=0x%2x\n",
                   __func__, agc2_pwr, temp);

            /* Retrain if agc2_pwr is not in ideal range */
            if (agc2_pwr <= AH_PAPRD_IDEAL_AGC2_PWR_RANGE) {
                temp = 0;
            }
		}
    } else {
        temp =
            OS_REG_READ_FIELD_ALT(ah, AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON,
                AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE);
    }
    if (!temp) {
        /*ath_hal_printf(ah, "%s[%d] PAPRD TEMp Error\n", __func__, __LINE__);*/
    }

    return temp;
}

/*
 * ar9300_paprd_dec_tx_pwr
 *
 * This function do decrease tx power if Paprd is off or train failed.
 */
void 
ar9300_paprd_dec_tx_pwr(struct ath_hal *ah)
{
    u_int32_t   pwr_temp, pwr_reg;
    int         i, loop_cnt;
    struct ar9300_paprd_pwr_adjust  *p_item;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (AR_SREV_POSEIDON(ah)) {
        loop_cnt = 
        (sizeof(ar9300_paprd_pwr_adj_array) / 
            sizeof(struct ar9300_paprd_pwr_adjust));
        for (i = 0; i < loop_cnt; i++ )
        {
            p_item = &ar9300_paprd_pwr_adj_array[i];
            pwr_reg = OS_REG_READ(ah, p_item->reg_addr);
            pwr_temp = ahp->ah_default_tx_power[p_item->target_rate];
            pwr_temp -= (p_item->sub_db * 2);
            pwr_temp = pwr_temp << p_item->reg_mask_offset;
            pwr_temp |= (pwr_reg&~(p_item->reg_mask <<p_item->reg_mask_offset));

            if (pwr_temp != pwr_reg) 
            {
                OS_REG_WRITE(ah, p_item->reg_addr, pwr_temp);
            }
        }
    }
    return;
}

int ar9300_paprd_thermal_send(struct ath_hal *ah)
{
    if (AR_SREV_HORNET(ah)) {
        return OS_REG_READ(ah, AR_TFCNT);
    } else {
        return 1;
    }
}

#if 0
void ar9300_paprd_test_prints(struct ath_hal *ah)
{
    u_int32_t i, reg = 0;

    HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "=====ar9300_paprd_test_prints=======\n");
    /*printf("=====ar9300_paprd_test_prints=======\n");*/
    HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "BB_paprd_ctrl0_b0 = 0x%08x\n",
        OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B0));
    /*
     * printf(
     *     "BB_paprd_ctrl0_b0 = 0x%08x\n", 
     *     OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B0));
     */
    if (!AR_SREV_POSEIDON(ah) && !AR_SREV_HORNET(ah)) {
        HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "BB_paprd_ctrl0_b1 = 0x%08x\n",
            OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B1));
        /*
         * printf(
         *     "BB_paprd_ctrl0_b1 = 0x%08x\n", 
         *     OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B1));
         */
        HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "BB_paprd_ctrl0_b2 = 0x%08x\n",
            OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B2));
        /*
         * printf(
         *     "BB_paprd_ctrl0_b2 = 0x%08x\n", 
         *     OS_REG_READ(ah, AR_PHY_PAPRD_CTRL0_B2));
         */
    }

    reg = AR_PHY_PAPRD_MEM_TAB_B0;
    HALDEBUG(NULL, HAL_DEBUG_CALIBRATE,
        "%s[%d] reg %08lx small_signal_gain ch0 0x%08x\n", __func__, __LINE__,
        AR_PHY_PA_GAIN123_B0, OS_REG_READ(ah, AR_PHY_PA_GAIN123_B0));
    /*
     * printf(
     *     "%s[%d] reg %08lx small_signal_gain ch0 0x%08x\n",
     *     __func__,  __LINE__, AR_PHY_PA_GAIN123_B0, 
     *     OS_REG_READ(ah, AR_PHY_PA_GAIN123_B0));
     */

    for (i = 0; i < 24; i++) {
        HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n",
            __func__, __LINE__, reg, OS_REG_READ(ah, reg));
        /*
         * printf(
         *     "%s[%d] reg %08x = 0x%08x\n", __func__, __LINE__,
         *     reg, OS_REG_READ(ah, reg));
         */
        reg = reg + 4;
    }

    ar9300_paprd_debug_print(ah);
    HALDEBUG(NULL, HAL_DEBUG_CALIBRATE,
        "=====ar9300_paprd_test_prints end=======\n");
    /*printf("=====ar9300_paprd_test_prints end=======\n");*/

    if (!AR_SREV_POSEIDON(ah)) {
        reg = AR_PHY_PAPRD_MEM_TAB_B1;
        printf("%s[%d] reg %08lx small_signal_gain ch1 0x%08x\n",
            __func__, __LINE__,
            AR_PHY_PA_GAIN123_B1, OS_REG_READ(ah, AR_PHY_PA_GAIN123_B1));
        for (i = 0; i < 24; i++) {
            OS_REG_WRITE(ah, reg, paprd_table_val[i]);
            HALDEBUG(NULL, HAL_DEBUG_CALIBRATE, "%s[%d] reg %08x = 0x%08x\n",
                __func__, __LINE__, reg, OS_REG_READ(ah, reg));
            printf("%s[%d] reg %08x = 0x%08x\n", __func__, __LINE__, reg,
                OS_REG_READ(ah, reg));
            reg = reg + 4;
        }
    
        reg = AR_PHY_PAPRD_MEM_TAB_B2;
        printf("%s[%d] reg %08lx small_signal_gain ch2 0x%08x\n",
            __func__, __LINE__,
            AR_PHY_PA_GAIN123_B2, OS_REG_READ(ah, AR_PHY_PA_GAIN123_B2));
    }
}
#endif

#else
int
ar9300_paprd_init_table(struct ath_hal *ah, HAL_CHANNEL * chan)
{
    return 0;
}

HAL_STATUS
ar9300_paprd_setup_gain_table(struct ath_hal * ah, int chain_num)
{
    return HAL_OK;
}

HAL_STATUS
ar9300_paprd_create_curve(struct ath_hal * ah, HAL_CHANNEL * chan,
    int chain_num)
{
    return HAL_OK;
}

int
ar9300_paprd_is_done(struct ath_hal *ah)
{
    return 0;
}

void
ar9300_enable_paprd(struct ath_hal *ah, HAL_BOOL enable_flag, HAL_CHANNEL * chan)
{
    return;
}

void
ar9300_populate_paprd_single_table(struct ath_hal *ah, HAL_CHANNEL * chan,
    int chain_num)
{
    return;
}

void 
ar9300_paprd_dec_tx_pwr(struct ath_hal *ah)
{
    return;
}

int ar9300_paprd_thermal_send(struct ath_hal *ah)
{
    return 1;
}
#endif                          /* ATH_SUPPORT_PAPRD */
