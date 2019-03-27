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
/*
 * Copyright (c) 2002-2005 Atheros Communications, Inc. 
 * All Rights Reserved.
 * 
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 * 
 */

#ifndef _ATH_AR9300PHY_H_
#define _ATH_AR9300PHY_H_

#include "osprey_reg_map.h"

/*
 * BB PHY register map
 */
#define AR_PHY_BASE     offsetof(struct bb_reg_map, bb_chn_reg_map)      /* base address of phy regs */
#define AR_PHY(_n)      (AR_PHY_BASE + ((_n)<<2))

/*
 * Channel Register Map
 */
#define AR_CHAN_BASE      offsetof(struct bb_reg_map, bb_chn_reg_map)
#define AR_CHAN_OFFSET(_x)   (AR_CHAN_BASE + offsetof(struct chn_reg_map, _x))

#define AR_PHY_TIMING1      AR_CHAN_OFFSET(BB_timing_controls_1)
#define AR_PHY_TIMING2      AR_CHAN_OFFSET(BB_timing_controls_2)
#define AR_PHY_TIMING3      AR_CHAN_OFFSET(BB_timing_controls_3)
#define AR_PHY_TIMING4      AR_CHAN_OFFSET(BB_timing_control_4)
#define AR_PHY_TIMING5      AR_CHAN_OFFSET(BB_timing_control_5)
#define AR_PHY_TIMING6      AR_CHAN_OFFSET(BB_timing_control_6)
#define AR_PHY_TIMING11     AR_CHAN_OFFSET(BB_timing_control_11)
#define AR_PHY_SPUR_REG     AR_CHAN_OFFSET(BB_spur_mask_controls)
#define AR_PHY_RX_IQCAL_CORR_B0    AR_CHAN_OFFSET(BB_rx_iq_corr_b0)
#define AR_PHY_TX_IQCAL_CONTROL_3  AR_CHAN_OFFSET(BB_txiqcal_control_3)

/* BB_timing_control_11 */
#define AR_PHY_TIMING11_SPUR_FREQ_SD	0x3FF00000
#define AR_PHY_TIMING11_SPUR_FREQ_SD_S  20

#define AR_PHY_TIMING11_SPUR_DELTA_PHASE 0x000FFFFF
#define AR_PHY_TIMING11_SPUR_DELTA_PHASE_S 0

#define AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC 0x40000000
#define AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC_S 30

#define AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR 0x80000000
#define AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR_S 31

/* BB_spur_mask_controls */
#define AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT		0x4000000
#define AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT_S	26

#define AR_PHY_SPUR_REG_ENABLE_MASK_PPM				0x20000     /* bins move with freq offset */
#define AR_PHY_SPUR_REG_ENABLE_MASK_PPM_S			17
#define AR_PHY_SPUR_REG_SPUR_RSSI_THRESH            0x000000FF
#define AR_PHY_SPUR_REG_SPUR_RSSI_THRESH_S          0
#define AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI			0x00000100
#define AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI_S			8
#define AR_PHY_SPUR_REG_MASK_RATE_CNTL				0x03FC0000
#define AR_PHY_SPUR_REG_MASK_RATE_CNTL_S			18

/* BB_rx_iq_corr_b0 */
#define AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN   0x20000000
#define AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN_S         29
/* BB_txiqcal_control_3 */
#define AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN   0x80000000
#define AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN_S         31
 
#if 0
/* enable vit puncture per rate, 8 bits, lsb is low rate */
#define AR_PHY_SPUR_REG_MASK_RATE_CNTL       (0xFF << 18)
#define AR_PHY_SPUR_REG_MASK_RATE_CNTL_S     18
#define AR_PHY_SPUR_REG_ENABLE_MASK_PPM      0x20000     /* bins move with freq offset */
#define AR_PHY_SPUR_REG_MASK_RATE_SELECT     (0xFF << 9) /* use mask1 or mask2, one per rate */
#define AR_PHY_SPUR_REG_MASK_RATE_SELECT_S   9
#define AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI 0x100
#define AR_PHY_SPUR_REG_SPUR_RSSI_THRESH     0x7F
#define AR_PHY_SPUR_REG_SPUR_RSSI_THRESH_S   0
#endif

#define AR_PHY_FIND_SIG_LOW  AR_CHAN_OFFSET(BB_find_signal_low)
#define AR_PHY_SFCORR           AR_CHAN_OFFSET(BB_sfcorr)
#if 0
#define AR_PHY_SFCORR_M2COUNT_THR    0x0000001F
#define AR_PHY_SFCORR_M2COUNT_THR_S  0
#define AR_PHY_SFCORR_M1_THRESH      0x00FE0000
#define AR_PHY_SFCORR_M1_THRESH_S    17
#define AR_PHY_SFCORR_M2_THRESH      0x7F000000
#define AR_PHY_SFCORR_M2_THRESH_S    24
#endif

#define AR_PHY_SFCORR_LOW       AR_CHAN_OFFSET(BB_self_corr_low)
#define AR_PHY_SFCORR_EXT       AR_CHAN_OFFSET(BB_ext_chan_scorr_thr)
#if 0
#define AR_PHY_SFCORR_EXT_M1_THRESH       0x0000007F   // [06:00]
#define AR_PHY_SFCORR_EXT_M1_THRESH_S     0
#define AR_PHY_SFCORR_EXT_M2_THRESH       0x00003F80   // [13:07]
#define AR_PHY_SFCORR_EXT_M2_THRESH_S     7
#define AR_PHY_SFCORR_EXT_M1_THRESH_LOW   0x001FC000   // [20:14]
#define AR_PHY_SFCORR_EXT_M1_THRESH_LOW_S 14
#define AR_PHY_SFCORR_EXT_M2_THRESH_LOW   0x0FE00000   // [27:21]
#define AR_PHY_SFCORR_EXT_M2_THRESH_LOW_S 21
#define AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S   28
#endif

#define AR_PHY_EXT_CCA              AR_CHAN_OFFSET(BB_ext_chan_pwr_thr_2_b0)
#define AR_PHY_RADAR_0              AR_CHAN_OFFSET(BB_radar_detection)      /* radar detection settings */
#define AR_PHY_RADAR_1              AR_CHAN_OFFSET(BB_radar_detection_2)
#define AR_PHY_RADAR_1_CF_BIN_THRESH	0x07000000
#define AR_PHY_RADAR_1_CF_BIN_THRESH_S	24
#define AR_PHY_RADAR_EXT            AR_CHAN_OFFSET(BB_extension_radar) /* extension channel radar settings */
#define AR_PHY_MULTICHAIN_CTRL      AR_CHAN_OFFSET(BB_multichain_control)
#define AR_PHY_PERCHAIN_CSD         AR_CHAN_OFFSET(BB_per_chain_csd)

#define AR_PHY_TX_PHASE_RAMP_0      AR_CHAN_OFFSET(BB_tx_phase_ramp_b0)
#define AR_PHY_ADC_GAIN_DC_CORR_0   AR_CHAN_OFFSET(BB_adc_gain_dc_corr_b0)
#define AR_PHY_IQ_ADC_MEAS_0_B0     AR_CHAN_OFFSET(BB_iq_adc_meas_0_b0)
#define AR_PHY_IQ_ADC_MEAS_1_B0     AR_CHAN_OFFSET(BB_iq_adc_meas_1_b0)
#define AR_PHY_IQ_ADC_MEAS_2_B0     AR_CHAN_OFFSET(BB_iq_adc_meas_2_b0)
#define AR_PHY_IQ_ADC_MEAS_3_B0     AR_CHAN_OFFSET(BB_iq_adc_meas_3_b0)

#define AR_PHY_TX_IQ_CORR_0         AR_CHAN_OFFSET(BB_tx_iq_corr_b0)
#define AR_PHY_TX_CRC               AR_CHAN_OFFSET(BB_tx_crc)
#define AR_PHY_TST_DAC_CONST        AR_CHAN_OFFSET(BB_tstdac_constant)
#define AR_PHY_SPUR_REPORT_0        AR_CHAN_OFFSET(BB_spur_report_b0)
#define AR_PHY_CHAN_INFO_TAB_0      AR_CHAN_OFFSET(BB_chan_info_chan_tab_b0)


/*
 * Channel Field Definitions
 */
/* BB_timing_controls_2 */
#define AR_PHY_TIMING2_USE_FORCE_PPM    0x00001000
#define AR_PHY_TIMING2_FORCE_PPM_VAL    0x00000fff
#define AR_PHY_TIMING2_HT_Fine_Timing_EN    0x80000000
#define AR_PHY_TIMING2_DC_OFFSET	0x08000000
#define AR_PHY_TIMING2_DC_OFFSET_S	27

/* BB_timing_controls_3 */
#define AR_PHY_TIMING3_DSC_MAN      0xFFFE0000
#define AR_PHY_TIMING3_DSC_MAN_S    17
#define AR_PHY_TIMING3_DSC_EXP      0x0001E000
#define AR_PHY_TIMING3_DSC_EXP_S    13
/* BB_timing_control_4 */
#define AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX 0xF000  /* Mask for max number of samples (logarithmic) */
#define AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX_S   12  /* Shift for max number of samples */
#define AR_PHY_TIMING4_DO_CAL    0x10000     /* perform calibration */
#define AR_PHY_TIMING4_ENABLE_PILOT_MASK	0x10000000
#define AR_PHY_TIMING4_ENABLE_PILOT_MASK_S	28
#define AR_PHY_TIMING4_ENABLE_CHAN_MASK		0x20000000
#define AR_PHY_TIMING4_ENABLE_CHAN_MASK_S	29

#define AR_PHY_TIMING4_ENABLE_SPUR_FILTER 0x40000000
#define AR_PHY_TIMING4_ENABLE_SPUR_FILTER_S 30
#define AR_PHY_TIMING4_ENABLE_SPUR_RSSI 0x80000000
#define AR_PHY_TIMING4_ENABLE_SPUR_RSSI_S 31

/* BB_adc_gain_dc_corr_b0 */
#define AR_PHY_NEW_ADC_GAIN_CORR_ENABLE 0x40000000
#define AR_PHY_NEW_ADC_DC_OFFSET_CORR_ENABLE 0x80000000
/* BB_self_corr_low */
#define AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW  0x00000001
#define AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW    0x00003F00
#define AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW_S  8
#define AR_PHY_SFCORR_LOW_M1_THRESH_LOW      0x001FC000
#define AR_PHY_SFCORR_LOW_M1_THRESH_LOW_S    14
#define AR_PHY_SFCORR_LOW_M2_THRESH_LOW      0x0FE00000
#define AR_PHY_SFCORR_LOW_M2_THRESH_LOW_S    21
/* BB_sfcorr */
#define AR_PHY_SFCORR_M2COUNT_THR    0x0000001F
#define AR_PHY_SFCORR_M2COUNT_THR_S  0
#define AR_PHY_SFCORR_M1_THRESH      0x00FE0000
#define AR_PHY_SFCORR_M1_THRESH_S    17
#define AR_PHY_SFCORR_M2_THRESH      0x7F000000
#define AR_PHY_SFCORR_M2_THRESH_S    24
/* BB_ext_chan_scorr_thr */
#define AR_PHY_SFCORR_EXT_M1_THRESH       0x0000007F   // [06:00]
#define AR_PHY_SFCORR_EXT_M1_THRESH_S     0
#define AR_PHY_SFCORR_EXT_M2_THRESH       0x00003F80   // [13:07]
#define AR_PHY_SFCORR_EXT_M2_THRESH_S     7
#define AR_PHY_SFCORR_EXT_M1_THRESH_LOW   0x001FC000   // [20:14]
#define AR_PHY_SFCORR_EXT_M1_THRESH_LOW_S 14
#define AR_PHY_SFCORR_EXT_M2_THRESH_LOW   0x0FE00000   // [27:21]
#define AR_PHY_SFCORR_EXT_M2_THRESH_LOW_S 21
#define AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD 0x10000000
#define AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD_S 28
#define AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S   28
/* BB_ext_chan_pwr_thr_2_b0 */
#define AR_PHY_EXT_CCA_THRESH62 0x007F0000
#define AR_PHY_EXT_CCA_THRESH62_S       16
#define AR_PHY_EXT_MINCCA_PWR   0x01FF0000
#define AR_PHY_EXT_MINCCA_PWR_S 16
#define AR_PHY_EXT_CYCPWR_THR1 0x0000FE00L 		// [15:09]
#define AR_PHY_EXT_CYCPWR_THR1_S 9
/* BB_timing_control_5 */
#define AR_PHY_TIMING5_CYCPWR_THR1  0x000000FE
#define AR_PHY_TIMING5_CYCPWR_THR1_S    1
#define AR_PHY_TIMING5_CYCPWR_THR1_ENABLE  0x00000001
#define AR_PHY_TIMING5_CYCPWR_THR1_ENABLE_S    0
#define AR_PHY_TIMING5_CYCPWR_THR1A  0x007F0000
#define AR_PHY_TIMING5_CYCPWR_THR1A_S    16
#define AR_PHY_TIMING5_RSSI_THR1A     (0x7F << 16)
#define AR_PHY_TIMING5_RSSI_THR1A_S   16
#define AR_PHY_TIMING5_RSSI_THR1A_ENA (0x1 << 15)
/* BB_radar_detection) */
#define AR_PHY_RADAR_0_ENA  0x00000001  /* Enable radar detection */
#define AR_PHY_RADAR_0_ENA_S  0
#define AR_PHY_RADAR_0_FFT_ENA  0x80000000  /* Enable FFT data */
#define AR_PHY_RADAR_0_INBAND   0x0000003e  /* Inband pulse threshold */
#define AR_PHY_RADAR_0_INBAND_S 1
#define AR_PHY_RADAR_0_PRSSI    0x00000FC0  /* Pulse rssi threshold */
#define AR_PHY_RADAR_0_PRSSI_S  6
#define AR_PHY_RADAR_0_HEIGHT   0x0003F000  /* Pulse height threshold */
#define AR_PHY_RADAR_0_HEIGHT_S 12
#define AR_PHY_RADAR_0_RRSSI    0x00FC0000  /* Radar rssi threshold */
#define AR_PHY_RADAR_0_RRSSI_S  18
#define AR_PHY_RADAR_0_FIRPWR   0x7F000000  /* Radar firpwr threshold */
#define AR_PHY_RADAR_0_FIRPWR_S 24
/* BB_radar_detection_2 */
#define AR_PHY_RADAR_1_RELPWR_ENA       0x00800000  /* enable to check radar relative power */
#define AR_PHY_RADAR_1_USE_FIR128       0x00400000  /* enable to use the average inband power
                                                     * measured over 128 cycles
                                                     */
#define AR_PHY_RADAR_1_RELPWR_THRESH    0x003F0000  /* relative pwr thresh */
#define AR_PHY_RADAR_1_RELPWR_THRESH_S  16
#define AR_PHY_RADAR_1_BLOCK_CHECK      0x00008000  /* Enable to block radar check if weak OFDM
                                                     * sig or pkt is immediately after tx to rx
                                                     * transition
                                                     */
#define AR_PHY_RADAR_1_MAX_RRSSI        0x00004000  /* Enable to use max rssi */
#define AR_PHY_RADAR_1_RELSTEP_CHECK    0x00002000  /* Enable to use pulse relative step check */
#define AR_PHY_RADAR_1_RELSTEP_THRESH   0x00001F00  /* Pulse relative step threshold */
#define AR_PHY_RADAR_1_RELSTEP_THRESH_S 8
#define AR_PHY_RADAR_1_MAXLEN           0x000000FF  /* Max length of radar pulse */
#define AR_PHY_RADAR_1_MAXLEN_S         0
/* BB_extension_radar */
#define AR_PHY_RADAR_EXT_ENA            0x00004000  /* Enable extension channel radar detection */
#define AR_PHY_RADAR_DC_PWR_THRESH      0x007f8000
#define AR_PHY_RADAR_DC_PWR_THRESH_S    15
#define AR_PHY_RADAR_LB_DC_CAP          0x7f800000
#define AR_PHY_RADAR_LB_DC_CAP_S        23
/* per chain csd*/
#define AR_PHY_PERCHAIN_CSD_chn1_2chains    0x0000001f
#define AR_PHY_PERCHAIN_CSD_chn1_2chains_S  0
#define AR_PHY_PERCHAIN_CSD_chn1_3chains    0x000003e0
#define AR_PHY_PERCHAIN_CSD_chn1_3chains_S  5
#define AR_PHY_PERCHAIN_CSD_chn2_3chains    0x00007c00
#define AR_PHY_PERCHAIN_CSD_chn2_3chains_S  10
/* BB_find_signal_low */
#define AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW (0x3f << 6)
#define AR_PHY_FIND_SIG_LOW_FIRSTEP_LOW_S   6
#define AR_PHY_FIND_SIG_LOW_FIRPWR      (0x7f << 12)
#define AR_PHY_FIND_SIG_LOW_FIRPWR_S    12
#define AR_PHY_FIND_SIG_LOW_FIRPWR_SIGN_BIT 19
#define AR_PHY_FIND_SIG_LOW_RELSTEP     0x1f
#define AR_PHY_FIND_SIG_LOW_RELSTEP_S   0
#define AR_PHY_FIND_SIG_LOW_RELSTEP_SIGN_BIT 5
/* BB_chan_info_chan_tab_b* */
#define AR_PHY_CHAN_INFO_TAB_S2_READ    0x00000008
#define AR_PHY_CHAN_INFO_TAB_S2_READ_S           3
/* BB_rx_iq_corr_b* */
#define AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF 0x0000007F   /* Mask for kcos_theta-1 for q correction */
#define AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF_S   0   /* shift for Q_COFF */
#define AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF 0x00003F80   /* Mask for sin_theta for i correction */
#define AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF_S   7   /* Shift for sin_theta for i correction */
#define AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE   0x00004000   /* enable IQ correction */
#define AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_Q_COFF   0x003f8000
#define AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_Q_COFF_S 15
#define AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_I_COFF   0x1fc00000
#define AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_I_COFF_S 22

/*
 * MRC Register Map
 */
#define AR_MRC_BASE      offsetof(struct bb_reg_map, bb_mrc_reg_map)
#define AR_MRC_OFFSET(_x)   (AR_MRC_BASE + offsetof(struct mrc_reg_map, _x))

#define AR_PHY_TIMING_3A       AR_MRC_OFFSET(BB_timing_control_3a)
#define AR_PHY_LDPC_CNTL1      AR_MRC_OFFSET(BB_ldpc_cntl1)
#define AR_PHY_LDPC_CNTL2      AR_MRC_OFFSET(BB_ldpc_cntl2)
#define AR_PHY_PILOT_SPUR_MASK AR_MRC_OFFSET(BB_pilot_spur_mask)
#define AR_PHY_CHAN_SPUR_MASK  AR_MRC_OFFSET(BB_chan_spur_mask)
#define AR_PHY_SGI_DELTA       AR_MRC_OFFSET(BB_short_gi_delta_slope)
#define AR_PHY_ML_CNTL_1       AR_MRC_OFFSET(BB_ml_cntl1)
#define AR_PHY_ML_CNTL_2       AR_MRC_OFFSET(BB_ml_cntl2)
#define AR_PHY_TST_ADC         AR_MRC_OFFSET(BB_tstadc)

/* BB_pilot_spur_mask fields */
#define AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A		0x00000FE0
#define AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A_S	5
#define AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A			0x1F
#define AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A_S		0

/* BB_chan_spur_mask fields */
#define AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A	0x00000FE0
#define AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A_S	5
#define AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A		0x1F
#define AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A_S		0

/*
 * MRC Feild Definitions
 */
#define AR_PHY_SGI_DSC_MAN   0x0007FFF0
#define AR_PHY_SGI_DSC_MAN_S 4
#define AR_PHY_SGI_DSC_EXP   0x0000000F
#define AR_PHY_SGI_DSC_EXP_S 0
/*
 * BBB Register Map
 */
#define AR_BBB_BASE      offsetof(struct bb_reg_map, bb_bbb_reg_map)
#define AR_BBB_OFFSET(_x)   (AR_BBB_BASE + offsetof(struct bbb_reg_map, _x))

#define AR_PHY_BBB_RX_CTRL(_i)  AR_BBB_OFFSET(BB_bbb_rx_ctrl_##_i)

/*
 * AGC Register Map
 */
#define AR_AGC_BASE      offsetof(struct bb_reg_map, bb_agc_reg_map)
#define AR_AGC_OFFSET(_x)   (AR_AGC_BASE + offsetof(struct agc_reg_map, _x))

#define AR_PHY_SETTLING         AR_AGC_OFFSET(BB_settling_time)
#define AR_PHY_FORCEMAX_GAINS_0 AR_AGC_OFFSET(BB_gain_force_max_gains_b0)
#define AR_PHY_GAINS_MINOFF0    AR_AGC_OFFSET(BB_gains_min_offsets_b0)
#define AR_PHY_DESIRED_SZ       AR_AGC_OFFSET(BB_desired_sigsize)
#define AR_PHY_FIND_SIG         AR_AGC_OFFSET(BB_find_signal)
#define AR_PHY_AGC              AR_AGC_OFFSET(BB_agc)
#define AR_PHY_EXT_ATTEN_CTL_0  AR_AGC_OFFSET(BB_ext_atten_switch_ctl_b0)
#define AR_PHY_CCA_0            AR_AGC_OFFSET(BB_cca_b0)
#define AR_PHY_EXT_CCA0         AR_AGC_OFFSET(BB_cca_ctrl_2_b0)
#define AR_PHY_RESTART          AR_AGC_OFFSET(BB_restart)
#define AR_PHY_MC_GAIN_CTRL     AR_AGC_OFFSET(BB_multichain_gain_ctrl)
#define AR_PHY_EXTCHN_PWRTHR1   AR_AGC_OFFSET(BB_ext_chan_pwr_thr_1)
#define AR_PHY_EXT_CHN_WIN      AR_AGC_OFFSET(BB_ext_chan_detect_win)
#define AR_PHY_20_40_DET_THR    AR_AGC_OFFSET(BB_pwr_thr_20_40_det)
#define AR_PHY_RIFS_SRCH        AR_AGC_OFFSET(BB_rifs_srch)
#define AR_PHY_PEAK_DET_CTRL_1  AR_AGC_OFFSET(BB_peak_det_ctrl_1)

#define AR_PHY_PEAK_DET_ENABLE  0x00000002

#define AR_PHY_PEAK_DET_CTRL_2  AR_AGC_OFFSET(BB_peak_det_ctrl_2)
#define AR_PHY_RX_GAIN_BOUNDS_1 AR_AGC_OFFSET(BB_rx_gain_bounds_1)
#define AR_PHY_RX_GAIN_BOUNDS_2 AR_AGC_OFFSET(BB_rx_gain_bounds_2)
#define AR_PHY_RSSI_0           AR_AGC_OFFSET(BB_rssi_b0)
#define AR_PHY_SPUR_CCK_REP0    AR_AGC_OFFSET(BB_spur_est_cck_report_b0)
#define AR_PHY_CCK_DETECT       AR_AGC_OFFSET(BB_bbb_sig_detect)
#define AR_PHY_DAG_CTRLCCK      AR_AGC_OFFSET(BB_bbb_dagc_ctrl)
#define AR_PHY_IQCORR_CTRL_CCK  AR_AGC_OFFSET(BB_iqcorr_ctrl_cck)
#define AR_PHY_DIG_DC_STATUS_I_B0 AR_AGC_OFFSET(BB_agc_dig_dc_status_i_b0)
#define AR_PHY_DIG_DC_STATUS_Q_B0 AR_AGC_OFFSET(BB_agc_dig_dc_status_q_b0)
#define AR_PHY_DIG_DC_C1_RES            0x000001ff
#define AR_PHY_DIG_DC_C1_RES_S          0
#define AR_PHY_DIG_DC_C2_RES            0x0003fe00
#define AR_PHY_DIG_DC_C2_RES_S          9
#define AR_PHY_DIG_DC_C3_RES            0x07fc0000
#define AR_PHY_DIG_DC_C3_RES_S          18

#define AR_PHY_CCK_SPUR_MIT     AR_AGC_OFFSET(BB_cck_spur_mit)
#define AR_PHY_CCK_SPUR_MIT_SPUR_RSSI_THR                           0x000001fe
#define AR_PHY_CCK_SPUR_MIT_SPUR_RSSI_THR_S                                  1
#define AR_PHY_CCK_SPUR_MIT_SPUR_FILTER_TYPE                        0x60000000
#define AR_PHY_CCK_SPUR_MIT_SPUR_FILTER_TYPE_S                              29
#define AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT                        0x00000001
#define AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT_S                               0
#define AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ                           0x1ffffe00
#define AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ_S                                  9

#define AR_PHY_MRC_CCK_CTRL         AR_AGC_OFFSET(BB_mrc_cck_ctrl)
#define AR_PHY_MRC_CCK_ENABLE       0x00000001
#define AR_PHY_MRC_CCK_ENABLE_S              0
#define AR_PHY_MRC_CCK_MUX_REG      0x00000002
#define AR_PHY_MRC_CCK_MUX_REG_S             1

#define AR_PHY_RX_OCGAIN        AR_AGC_OFFSET(BB_rx_ocgain)

#define AR_PHY_CCA_NOM_VAL_OSPREY_2GHZ          -110
#define AR_PHY_CCA_NOM_VAL_OSPREY_5GHZ          -115
#define AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_2GHZ     -125
#define AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_5GHZ     -125
#define AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_2GHZ     -95
#define AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_5GHZ     -100
#define AR_PHY_CCA_NOM_VAL_PEACOCK_5GHZ         -105

#define AR_PHY_CCA_NOM_VAL_JUPITER_2GHZ          -127
#define AR_PHY_CCA_MIN_GOOD_VAL_JUPITER_2GHZ     -127
#define AR_PHY_CCA_NOM_VAL_JUPITER_5GHZ          -127
#define AR_PHY_CCA_MIN_GOOD_VAL_JUPITER_5GHZ     -127

#define AR_PHY_BT_COEX_4        AR_AGC_OFFSET(BB_bt_coex_4)
#define AR_PHY_BT_COEX_5        AR_AGC_OFFSET(BB_bt_coex_5)

/*
 * Noise floor readings at least CW_INT_DELTA above the nominal NF
 * indicate that CW interference is present.
 */
#define AR_PHY_CCA_CW_INT_DELTA 30

/*
 * AGC Field Definitions
 */
/* BB_ext_atten_switch_ctl_b0 */
#define AR_PHY_EXT_ATTEN_CTL_RXTX_MARGIN    0x00FC0000
#define AR_PHY_EXT_ATTEN_CTL_RXTX_MARGIN_S  18
#define AR_PHY_EXT_ATTEN_CTL_BSW_MARGIN     0x00003C00
#define AR_PHY_EXT_ATTEN_CTL_BSW_MARGIN_S   10
#define AR_PHY_EXT_ATTEN_CTL_BSW_ATTEN      0x0000001F
#define AR_PHY_EXT_ATTEN_CTL_BSW_ATTEN_S    0
#define AR_PHY_EXT_ATTEN_CTL_XATTEN2_MARGIN     0x003E0000
#define AR_PHY_EXT_ATTEN_CTL_XATTEN2_MARGIN_S   17
#define AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN     0x0001F000
#define AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN_S   12
#define AR_PHY_EXT_ATTEN_CTL_XATTEN2_DB         0x00000FC0
#define AR_PHY_EXT_ATTEN_CTL_XATTEN2_DB_S       6
#define AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB         0x0000003F
#define AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB_S       0
/* BB_gain_force_max_gains_b0 */
#define AR_PHY_RXGAIN_TXRX_ATTEN    0x0003F000
#define AR_PHY_RXGAIN_TXRX_ATTEN_S  12
#define AR_PHY_RXGAIN_TXRX_RF_MAX   0x007C0000
#define AR_PHY_RXGAIN_TXRX_RF_MAX_S 18
#define AR9280_PHY_RXGAIN_TXRX_ATTEN    0x00003F80
#define AR9280_PHY_RXGAIN_TXRX_ATTEN_S  7
#define AR9280_PHY_RXGAIN_TXRX_MARGIN   0x001FC000
#define AR9280_PHY_RXGAIN_TXRX_MARGIN_S 14
/* BB_settling_time */
#define AR_PHY_SETTLING_SWITCH  0x00003F80
#define AR_PHY_SETTLING_SWITCH_S    7
/* BB_desired_sigsize */
#define AR_PHY_DESIRED_SZ_ADC       0x000000FF
#define AR_PHY_DESIRED_SZ_ADC_S     0
#define AR_PHY_DESIRED_SZ_PGA       0x0000FF00
#define AR_PHY_DESIRED_SZ_PGA_S     8
#define AR_PHY_DESIRED_SZ_TOT_DES   0x0FF00000
#define AR_PHY_DESIRED_SZ_TOT_DES_S 20
/* BB_cca_b0 */
#define AR_PHY_MINCCA_PWR       0x1FF00000
#define AR_PHY_MINCCA_PWR_S     20
#define AR_PHY_CCA_THRESH62     0x0007F000
#define AR_PHY_CCA_THRESH62_S   12
#define AR9280_PHY_MINCCA_PWR       0x1FF00000
#define AR9280_PHY_MINCCA_PWR_S     20
#define AR9280_PHY_CCA_THRESH62     0x000FF000
#define AR9280_PHY_CCA_THRESH62_S   12
/* BB_cca_ctrl_2_b0 */
#define AR_PHY_EXT_CCA0_THRESH62    0x000000FF
#define AR_PHY_EXT_CCA0_THRESH62_S  0
/* BB_bbb_sig_detect */
#define AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK          0x0000003F
#define AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK_S        0
#define AR_PHY_CCK_DETECT_ANT_SWITCH_TIME           0x00001FC0 // [12:6] settling time for antenna switch
#define AR_PHY_CCK_DETECT_ANT_SWITCH_TIME_S         6
#define AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV    0x2000
#define AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV_S  13

/* BB_bbb_dagc_ctrl */
#define AR_PHY_DAG_CTRLCCK_EN_RSSI_THR  0x00000200
#define AR_PHY_DAG_CTRLCCK_EN_RSSI_THR_S  9
#define AR_PHY_DAG_CTRLCCK_RSSI_THR 0x0001FC00
#define AR_PHY_DAG_CTRLCCK_RSSI_THR_S   10

/* BB_rifs_srch */
#define AR_PHY_RIFS_INIT_DELAY         0x3ff0000

/*B_tpc_7*/
#define AR_PHY_TPC_7_TX_GAIN_TABLE_MAX		0x3f
#define AR_PHY_TPC_7_TX_GAIN_TABLE_MAX_S	(0)

/* BB_agc */
#define AR_PHY_AGC_QUICK_DROP_S (22)
#define AR_PHY_AGC_QUICK_DROP (0xf << AR_PHY_AGC_QUICK_DROP_S)
#define AR_PHY_AGC_COARSE_LOW       0x00007F80
#define AR_PHY_AGC_COARSE_LOW_S     7
#define AR_PHY_AGC_COARSE_HIGH      0x003F8000
#define AR_PHY_AGC_COARSE_HIGH_S    15
#define AR_PHY_AGC_COARSE_PWR_CONST 0x0000007F    
#define AR_PHY_AGC_COARSE_PWR_CONST_S   0
/* BB_find_signal */
#define AR_PHY_FIND_SIG_FIRSTEP  0x0003F000
#define AR_PHY_FIND_SIG_FIRSTEP_S        12
#define AR_PHY_FIND_SIG_FIRPWR   0x03FC0000
#define AR_PHY_FIND_SIG_FIRPWR_S         18
#define AR_PHY_FIND_SIG_FIRPWR_SIGN_BIT  25
#define AR_PHY_FIND_SIG_RELPWR   (0x1f << 6)
#define AR_PHY_FIND_SIG_RELPWR_S          6
#define AR_PHY_FIND_SIG_RELPWR_SIGN_BIT  11
#define AR_PHY_FIND_SIG_RELSTEP        0x1f
#define AR_PHY_FIND_SIG_RELSTEP_S         0
#define AR_PHY_FIND_SIG_RELSTEP_SIGN_BIT  5
/* BB_restart */
#define AR_PHY_RESTART_DIV_GC   0x001C0000 /* bb_ant_fast_div_gc_limit */
#define AR_PHY_RESTART_DIV_GC_S 18
#define AR_PHY_RESTART_ENA      0x01       /* enable restart */
#define AR_PHY_DC_RESTART_DIS   0x40000000 /* disable DC restart */

#define AR_PHY_TPC_OLPC_GAIN_DELTA_PAL_ON       0xFF000000 //Mask BIT[31:24]
#define AR_PHY_TPC_OLPC_GAIN_DELTA_PAL_ON_S     24
#define AR_PHY_TPC_OLPC_GAIN_DELTA              0x00FF0000 //Mask BIT[23:16]
#define AR_PHY_TPC_OLPC_GAIN_DELTA_S            16

#define AR_PHY_TPC_6_ERROR_EST_MODE             0x03000000 //Mask BIT[25:24]
#define AR_PHY_TPC_6_ERROR_EST_MODE_S           24

/*
 * SM Register Map
 */
#define AR_SM_BASE      offsetof(struct bb_reg_map, bb_sm_reg_map)
#define AR_SM_OFFSET(_x)   (AR_SM_BASE + offsetof(struct sm_reg_map, _x))

#define AR_PHY_D2_CHIP_ID        AR_SM_OFFSET(BB_D2_chip_id)
#define AR_PHY_GEN_CTRL          AR_SM_OFFSET(BB_gen_controls)
#define AR_PHY_MODE              AR_SM_OFFSET(BB_modes_select)
#define AR_PHY_ACTIVE            AR_SM_OFFSET(BB_active)
#define AR_PHY_SPUR_MASK_A       AR_SM_OFFSET(BB_vit_spur_mask_A)
#define AR_PHY_SPUR_MASK_B       AR_SM_OFFSET(BB_vit_spur_mask_B)
#define AR_PHY_SPECTRAL_SCAN     AR_SM_OFFSET(BB_spectral_scan)
#define AR_PHY_RADAR_BW_FILTER   AR_SM_OFFSET(BB_radar_bw_filter)
#define AR_PHY_SEARCH_START_DELAY AR_SM_OFFSET(BB_search_start_delay)
#define AR_PHY_MAX_RX_LEN        AR_SM_OFFSET(BB_max_rx_length)
#define AR_PHY_FRAME_CTL         AR_SM_OFFSET(BB_frame_control)
#define AR_PHY_RFBUS_REQ         AR_SM_OFFSET(BB_rfbus_request)
#define AR_PHY_RFBUS_GRANT       AR_SM_OFFSET(BB_rfbus_grant)
#define AR_PHY_RIFS              AR_SM_OFFSET(BB_rifs)
#define AR_PHY_RX_CLR_DELAY      AR_SM_OFFSET(BB_rx_clear_delay)
#define AR_PHY_RX_DELAY          AR_SM_OFFSET(BB_analog_power_on_time)
#define AR_PHY_BB_POWERTX_RATE9  AR_SM_OFFSET(BB_powertx_rate9) 
#define AR_PHY_TPC_7			 AR_SM_OFFSET(BB_tpc_7)
#define AR_PHY_CL_MAP_0_B0		 AR_SM_OFFSET(BB_cl_map_0_b0)
#define AR_PHY_CL_MAP_1_B0		 AR_SM_OFFSET(BB_cl_map_1_b0)
#define AR_PHY_CL_MAP_2_B0		 AR_SM_OFFSET(BB_cl_map_2_b0)
#define AR_PHY_CL_MAP_3_B0		 AR_SM_OFFSET(BB_cl_map_3_b0)

#define AR_PHY_RF_CTL(_i)        AR_SM_OFFSET(BB_tx_timing_##_i)

#define AR_PHY_XPA_TIMING_CTL    AR_SM_OFFSET(BB_xpa_timing_control)
#define AR_PHY_MISC_PA_CTL       AR_SM_OFFSET(BB_misc_pa_control)
#define AR_PHY_SWITCH_CHAIN_0    AR_SM_OFFSET(BB_switch_table_chn_b0)
#define AR_PHY_SWITCH_COM        AR_SM_OFFSET(BB_switch_table_com1)
#define AR_PHY_SWITCH_COM_2      AR_SM_OFFSET(BB_switch_table_com2)
#define AR_PHY_RX_CHAINMASK      AR_SM_OFFSET(BB_multichain_enable)
#define AR_PHY_CAL_CHAINMASK     AR_SM_OFFSET(BB_cal_chain_mask)
#define AR_PHY_AGC_CONTROL       AR_SM_OFFSET(BB_agc_control)
#define AR_PHY_CALMODE           AR_SM_OFFSET(BB_iq_adc_cal_mode)
#define AR_PHY_FCAL_1            AR_SM_OFFSET(BB_fcal_1)
#define AR_PHY_FCAL_2_0          AR_SM_OFFSET(BB_fcal_2_b0)
#define AR_PHY_DFT_TONE_CTL_0    AR_SM_OFFSET(BB_dft_tone_ctrl_b0)
#define AR_PHY_CL_CAL_CTL        AR_SM_OFFSET(BB_cl_cal_ctrl)
#define AR_PHY_BBGAINMAP_0_1_0   AR_SM_OFFSET(BB_cl_bbgain_map_0_1_b0)
#define AR_PHY_BBGAINMAP_2_3_0   AR_SM_OFFSET(BB_cl_bbgain_map_2_3_b0)
#define AR_PHY_CL_TAB_0          AR_SM_OFFSET(BB_cl_tab_b0)
#define AR_PHY_SYNTH_CONTROL     AR_SM_OFFSET(BB_synth_control)
#define AR_PHY_ADDAC_CLK_SEL     AR_SM_OFFSET(BB_addac_clk_select)
#define AR_PHY_PLL_CTL           AR_SM_OFFSET(BB_pll_cntl)
#define AR_PHY_ANALOG_SWAP       AR_SM_OFFSET(BB_analog_swap)
#define AR_PHY_ADDAC_PARA_CTL    AR_SM_OFFSET(BB_addac_parallel_control)
#define AR_PHY_XPA_CFG           AR_SM_OFFSET(BB_force_analog)
#define AR_PHY_AIC_CTRL_0_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_ctrl_0_b0)
#define AR_PHY_AIC_CTRL_1_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_ctrl_1_b0)
#define AR_PHY_AIC_CTRL_2_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_ctrl_2_b0)
#define AR_PHY_AIC_CTRL_3_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_ctrl_3_b0)
#define AR_PHY_AIC_STAT_0_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_stat_0_b0)
#define AR_PHY_AIC_STAT_1_B0_10  AR_SM_OFFSET(overlay_0xa580.Jupiter_10.BB_aic_stat_1_b0)
#define AR_PHY_AIC_CTRL_0_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_ctrl_0_b0)
#define AR_PHY_AIC_CTRL_1_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_ctrl_1_b0)
#define AR_PHY_AIC_CTRL_2_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_ctrl_2_b0)
#define AR_PHY_AIC_CTRL_3_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_ctrl_3_b0)
#define AR_PHY_AIC_CTRL_4_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_ctrl_4_b0)
#define AR_PHY_AIC_STAT_0_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_stat_0_b0)
#define AR_PHY_AIC_STAT_1_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_stat_1_b0)
#define AR_PHY_AIC_STAT_2_B0_20  AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_aic_stat_2_b0)
#define AR_PHY_AIC_CTRL_0_B1_10  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_aic_ctrl_0_b1)
#define AR_PHY_AIC_CTRL_1_B1_10  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_aic_ctrl_1_b1)
#define AR_PHY_AIC_STAT_0_B1_10  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_aic_stat_0_b1)
#define AR_PHY_AIC_STAT_1_B1_10  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_aic_stat_1_b1)
#define AR_PHY_AIC_CTRL_0_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_ctrl_0_b1)
#define AR_PHY_AIC_CTRL_1_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_ctrl_1_b1)
#define AR_PHY_AIC_CTRL_4_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_ctrl_4_b1)
#define AR_PHY_AIC_STAT_0_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_stat_0_b1)
#define AR_PHY_AIC_STAT_1_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_stat_1_b1)
#define AR_PHY_AIC_STAT_2_B1_20  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_20.BB_aic_stat_2_b1)
#define AR_PHY_AIC_SRAM_ADDR_B0  AR_SM_OFFSET(BB_tables_intf_addr_b0)
#define AR_PHY_AIC_SRAM_DATA_B0  AR_SM_OFFSET(BB_tables_intf_data_b0)
#define AR_PHY_AIC_SRAM_ADDR_B1  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_tables_intf_addr_b1)
#define AR_PHY_AIC_SRAM_DATA_B1  AR_SM1_OFFSET(overlay_0x4b0.Jupiter_10.BB_tables_intf_data_b1)


/* AIC fields */
#define AR_PHY_AIC_MON_ENABLE                   0x80000000
#define AR_PHY_AIC_MON_ENABLE_S                 31
#define AR_PHY_AIC_CAL_MAX_HOP_COUNT            0x7F000000
#define AR_PHY_AIC_CAL_MAX_HOP_COUNT_S          24
#define AR_PHY_AIC_CAL_MIN_VALID_COUNT          0x00FE0000
#define AR_PHY_AIC_CAL_MIN_VALID_COUNT_S        17
#define AR_PHY_AIC_F_WLAN                       0x0001FC00
#define AR_PHY_AIC_F_WLAN_S                     10
#define AR_PHY_AIC_CAL_CH_VALID_RESET           0x00000200
#define AR_PHY_AIC_CAL_CH_VALID_RESET_S         9
#define AR_PHY_AIC_CAL_ENABLE                   0x00000100
#define AR_PHY_AIC_CAL_ENABLE_S                 8
#define AR_PHY_AIC_BTTX_PWR_THR                 0x000000FE
#define AR_PHY_AIC_BTTX_PWR_THR_S               1
#define AR_PHY_AIC_ENABLE                       0x00000001
#define AR_PHY_AIC_ENABLE_S                     0
#define AR_PHY_AIC_CAL_BT_REF_DELAY             0x78000000
#define AR_PHY_AIC_CAL_BT_REF_DELAY_S           27
#define AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO       0x07000000
#define AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO_S     24
#define AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO       0x00F00000
#define AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO_S     20
#define AR_PHY_AIC_BT_IDLE_CFG                  0x00080000
#define AR_PHY_AIC_BT_IDLE_CFG_S                19
#define AR_PHY_AIC_STDBY_COND                   0x00060000
#define AR_PHY_AIC_STDBY_COND_S                 17
#define AR_PHY_AIC_STDBY_ROT_ATT_DB             0x0001F800
#define AR_PHY_AIC_STDBY_ROT_ATT_DB_S           11
#define AR_PHY_AIC_STDBY_COM_ATT_DB             0x00000700
#define AR_PHY_AIC_STDBY_COM_ATT_DB_S           8
#define AR_PHY_AIC_RSSI_MAX                     0x000000F0
#define AR_PHY_AIC_RSSI_MAX_S                   4
#define AR_PHY_AIC_RSSI_MIN                     0x0000000F
#define AR_PHY_AIC_RSSI_MIN_S                   0
#define AR_PHY_AIC_RADIO_DELAY                  0x7F000000
#define AR_PHY_AIC_RADIO_DELAY_S                24
#define AR_PHY_AIC_CAL_STEP_SIZE_CORR           0x00F00000
#define AR_PHY_AIC_CAL_STEP_SIZE_CORR_S         20
#define AR_PHY_AIC_CAL_ROT_IDX_CORR             0x000F8000
#define AR_PHY_AIC_CAL_ROT_IDX_CORR_S           15
#define AR_PHY_AIC_CAL_CONV_CHECK_FACTOR        0x00006000
#define AR_PHY_AIC_CAL_CONV_CHECK_FACTOR_S      13
#define AR_PHY_AIC_ROT_IDX_COUNT_MAX            0x00001C00
#define AR_PHY_AIC_ROT_IDX_COUNT_MAX_S          10
#define AR_PHY_AIC_CAL_SYNTH_TOGGLE             0x00000200
#define AR_PHY_AIC_CAL_SYNTH_TOGGLE_S           9
#define AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX         0x00000100
#define AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX_S       8
#define AR_PHY_AIC_CAL_SYNTH_SETTLING           0x000000FF
#define AR_PHY_AIC_CAL_SYNTH_SETTLING_S         0
#define AR_PHY_AIC_MON_MAX_HOP_COUNT            0x0FE00000
#define AR_PHY_AIC_MON_MAX_HOP_COUNT_S          21
#define AR_PHY_AIC_MON_MIN_STALE_COUNT          0x001FC000
#define AR_PHY_AIC_MON_MIN_STALE_COUNT_S        14
#define AR_PHY_AIC_MON_PWR_EST_LONG             0x00002000
#define AR_PHY_AIC_MON_PWR_EST_LONG_S           13
#define AR_PHY_AIC_MON_PD_TALLY_SCALING         0x00001800
#define AR_PHY_AIC_MON_PD_TALLY_SCALING_S       11
#define AR_PHY_AIC_MON_PERF_THR                 0x000007C0
#define AR_PHY_AIC_MON_PERF_THR_S               6
#define AR_PHY_AIC_CAL_COM_ATT_DB_FIXED         0x00000020
#define AR_PHY_AIC_CAL_COM_ATT_DB_FIXED_S       5
#define AR_PHY_AIC_CAL_TARGET_MAG_SETTING       0x00000018
#define AR_PHY_AIC_CAL_TARGET_MAG_SETTING_S     3
#define AR_PHY_AIC_CAL_PERF_CHECK_FACTOR        0x00000006
#define AR_PHY_AIC_CAL_PERF_CHECK_FACTOR_S      1
#define AR_PHY_AIC_CAL_PWR_EST_LONG             0x00000001
#define AR_PHY_AIC_CAL_PWR_EST_LONG_S           0
#define AR_PHY_AIC_MON_DONE                     0x80000000
#define AR_PHY_AIC_MON_DONE_S                   31
#define AR_PHY_AIC_MON_ACTIVE                   0x40000000
#define AR_PHY_AIC_MON_ACTIVE_S                 30
#define AR_PHY_AIC_MEAS_COUNT                   0x3F000000
#define AR_PHY_AIC_MEAS_COUNT_S                 24
#define AR_PHY_AIC_CAL_ANT_ISO_EST              0x00FC0000
#define AR_PHY_AIC_CAL_ANT_ISO_EST_S            18
#define AR_PHY_AIC_CAL_HOP_COUNT                0x0003F800
#define AR_PHY_AIC_CAL_HOP_COUNT_S              11
#define AR_PHY_AIC_CAL_VALID_COUNT              0x000007F0
#define AR_PHY_AIC_CAL_VALID_COUNT_S            4
#define AR_PHY_AIC_CAL_BT_TOO_WEAK_ERR          0x00000008
#define AR_PHY_AIC_CAL_BT_TOO_WEAK_ERR_S        3
#define AR_PHY_AIC_CAL_BT_TOO_STRONG_ERR        0x00000004
#define AR_PHY_AIC_CAL_BT_TOO_STRONG_ERR_S      2
#define AR_PHY_AIC_CAL_DONE                     0x00000002
#define AR_PHY_AIC_CAL_DONE_S                   1
#define AR_PHY_AIC_CAL_ACTIVE                   0x00000001
#define AR_PHY_AIC_CAL_ACTIVE_S                 0
#define AR_PHY_AIC_MEAS_MAG_MIN                 0xFFC00000
#define AR_PHY_AIC_MEAS_MAG_MIN_S               22
#define AR_PHY_AIC_MON_STALE_COUNT              0x003F8000
#define AR_PHY_AIC_MON_STALE_COUNT_S            15
#define AR_PHY_AIC_MON_HOP_COUNT                0x00007F00
#define AR_PHY_AIC_MON_HOP_COUNT_S              8
#define AR_PHY_AIC_CAL_AIC_SM                   0x000000F8
#define AR_PHY_AIC_CAL_AIC_SM_S                 3
#define AR_PHY_AIC_SM                           0x00000007
#define AR_PHY_AIC_SM_S                         0
#define AR_PHY_AIC_SRAM_VALID                   0x00000001
#define AR_PHY_AIC_SRAM_VALID_S                 0
#define AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB         0x0000007E
#define AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB_S       1
#define AR_PHY_AIC_SRAM_VGA_QUAD_SIGN           0x00000080
#define AR_PHY_AIC_SRAM_VGA_QUAD_SIGN_S         7
#define AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB          0x00003F00
#define AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB_S        8
#define AR_PHY_AIC_SRAM_VGA_DIR_SIGN            0x00004000
#define AR_PHY_AIC_SRAM_VGA_DIR_SIGN_S          14
#define AR_PHY_AIC_SRAM_COM_ATT_6DB             0x00038000
#define AR_PHY_AIC_SRAM_COM_ATT_6DB_S           15

#define AR_PHY_FRAME_CTL_CF_OVERLAP_WINDOW	3
#define AR_PHY_FRAME_CTL_CF_OVERLAP_WINDOW_S	0

/* BB_cl_tab_bx */
#define AR_PHY_CL_TAB_CARR_LK_DC_ADD_I              0x07FF0000
#define AR_PHY_CL_TAB_CARR_LK_DC_ADD_I_S            16
#define AR_PHY_CL_TAB_CARR_LK_DC_ADD_Q              0x0000FFE0
#define AR_PHY_CL_TAB_CARR_LK_DC_ADD_Q_S            5
#define AR_PHY_CL_TAB_GAIN_MOD                      0x0000001F
#define AR_PHY_CL_TAB_GAIN_MOD_S                    0

/* BB_vit_spur_mask_A fields */
#define AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A		0x0001FC00
#define AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A_S		10
#define AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A			0x3FF
#define AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A_S			0

/* enable_flt_svd*/
#define AR_PHY_ENABLE_FLT_SVD                       0x00001000
#define AR_PHY_ENABLE_FLT_SVD_S                     12

#define AR_PHY_TEST              AR_SM_OFFSET(BB_test_controls)

#define AR_PHY_TEST_BBB_OBS_SEL       0x780000
#define AR_PHY_TEST_BBB_OBS_SEL_S     19 /* bits 19 to 22 are cf_bbb_obs_sel*/ 

#define AR_PHY_TEST_RX_OBS_SEL_BIT5_S 23 
#define AR_PHY_TEST_RX_OBS_SEL_BIT5   (1 << AR_PHY_TEST_RX_OBS_SEL_BIT5_S)// This is bit 5 for cf_rx_obs_sel

#define AR_PHY_TEST_CHAIN_SEL      0xC0000000 
#define AR_PHY_TEST_CHAIN_SEL_S    30 /*bits 30 and 31 are tstdac_out_sel which selects which chain to drive out*/         

#define AR_PHY_TEST_CTL_STATUS   AR_SM_OFFSET(BB_test_controls_status)
#define AR_PHY_TEST_CTL_TSTDAC_EN         0x1
#define AR_PHY_TEST_CTL_TSTDAC_EN_S       0 /*cf_tstdac_en, driver to tstdac bus, 0=disable, 1=enable*/
#define AR_PHY_TEST_CTL_TX_OBS_SEL        0x1C
#define AR_PHY_TEST_CTL_TX_OBS_SEL_S      2 /* cf_tx_obs_sel, bits 2:4*/
#define AR_PHY_TEST_CTL_TX_OBS_MUX_SEL    0x60
#define AR_PHY_TEST_CTL_TX_OBS_MUX_SEL_S  5 /* cf_tx_obs_sel, bits 5:6, setting to 11 selects ADC*/
#define AR_PHY_TEST_CTL_TSTADC_EN         0x100
#define AR_PHY_TEST_CTL_TSTADC_EN_S       8 /*cf_tstadc_en, driver to tstadc bus, 0=disable, 1=enable*/
#define AR_PHY_TEST_CTL_RX_OBS_SEL        0x3C00
#define AR_PHY_TEST_CTL_RX_OBS_SEL_S      10 /* cf_tx_obs_sel, bits 10:13*/
#define AR_PHY_TEST_CTL_DEBUGPORT_SEL     0xe0000000
#define AR_PHY_TEST_CTL_DEBUGPORT_SEL_S   29


#define AR_PHY_TSTDAC            AR_SM_OFFSET(BB_tstdac)

#define AR_PHY_CHAN_STATUS       AR_SM_OFFSET(BB_channel_status)
#define AR_PHY_CHAN_INFO_MEMORY  AR_SM_OFFSET(BB_chaninfo_ctrl)
#define AR_PHY_CHNINFO_NOISEPWR  AR_SM_OFFSET(BB_chan_info_noise_pwr)
#define AR_PHY_CHNINFO_GAINDIFF  AR_SM_OFFSET(BB_chan_info_gain_diff)
#define AR_PHY_CHNINFO_FINETIM   AR_SM_OFFSET(BB_chan_info_fine_timing)
#define AR_PHY_CHAN_INFO_GAIN_0  AR_SM_OFFSET(BB_chan_info_gain_b0)
#define AR_PHY_SCRAMBLER_SEED    AR_SM_OFFSET(BB_scrambler_seed)
#define AR_PHY_CCK_TX_CTRL       AR_SM_OFFSET(BB_bbb_tx_ctrl)

#define AR_PHY_TX_FIR(_i)        AR_SM_OFFSET(BB_bbb_txfir_##_i)

#define AR_PHY_HEAVYCLIP_CTL     AR_SM_OFFSET(BB_heavy_clip_ctrl)
#define AR_PHY_HEAVYCLIP_20      AR_SM_OFFSET(BB_heavy_clip_20)
#define AR_PHY_HEAVYCLIP_40      AR_SM_OFFSET(BB_heavy_clip_40)
#define AR_PHY_ILLEGAL_TXRATE    AR_SM_OFFSET(BB_illegal_tx_rate)

#define AR_PHY_POWER_TX_RATE(_i) AR_SM_OFFSET(BB_powertx_rate##_i)

#define AR_PHY_PWRTX_MAX         AR_SM_OFFSET(BB_powertx_max) /* TPC register */
#define AR_PHY_PWRTX_MAX_TPC_ENABLE 0x00000040
#define AR_PHY_POWER_TX_SUB      AR_SM_OFFSET(BB_powertx_sub)
#define AR_PHY_PER_PACKET_POWERTX_MAX   0x00000040
#define AR_PHY_PER_PACKET_POWERTX_MAX_S 6
#define AR_PHY_POWER_TX_SUB_2_DISABLE 0xFFFFFFC0    /* 2 chain */
#define AR_PHY_POWER_TX_SUB_3_DISABLE 0xFFFFF000    /* 3 chain */

#define AR_PHY_TPC(_i)           AR_SM_OFFSET(BB_tpc_##_i)    /* values 1-3, 7-10 and 12-15 */
#define AR_PHY_TPC_4_B0          AR_SM_OFFSET(BB_tpc_4_b0)
#define AR_PHY_TPC_5_B0          AR_SM_OFFSET(BB_tpc_5_b0)
#define AR_PHY_TPC_6_B0          AR_SM_OFFSET(BB_tpc_6_b0)
#define AR_PHY_TPC_18            AR_SM_OFFSET(BB_tpc_18)
#define AR_PHY_TPC_19            AR_SM_OFFSET(BB_tpc_19)

#define AR_PHY_TX_FORCED_GAIN    AR_SM_OFFSET(BB_tx_forced_gain)

#define AR_PHY_PDADC_TAB_0       AR_SM_OFFSET(BB_pdadc_tab_b0)

#define AR_PHY_RTT_CTRL                 AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_rtt_ctrl)
#define AR_PHY_RTT_TABLE_SW_INTF_B0     AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_rtt_table_sw_intf_b0)
#define AR_PHY_RTT_TABLE_SW_INTF_1_B0   AR_SM_OFFSET(overlay_0xa580.Jupiter_20.BB_rtt_table_sw_intf_1_b0)

#define AR_PHY_TX_IQCAL_CONTROL_0(_ah)                               \
    (AR_SREV_POSEIDON(_ah) ?                                         \
        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiqcal_control_0) : \
        AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiqcal_control_0))

#define AR_PHY_TX_IQCAL_CONTROL_1(_ah)                               \
    (AR_SREV_POSEIDON(_ah) ?                                         \
        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiqcal_control_1) : \
        AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiqcal_control_1))

#define AR_PHY_TX_IQCAL_START(_ah)                                   \
    (AR_SREV_POSEIDON(_ah) ?                                         \
        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiqcal_control_0) : \
        AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiqcal_start))

#define AR_PHY_TX_IQCAL_STATUS_B0(_ah)                               \
    (AR_SREV_POSEIDON(_ah) ?                                         \
        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiqcal_status_b0) : \
        AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiqcal_status_b0))

#define AR_PHY_TX_IQCAL_CORR_COEFF_01_B0    AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiq_corr_coeff_01_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_23_B0    AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiq_corr_coeff_23_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_45_B0    AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiq_corr_coeff_45_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_67_B0    AR_SM_OFFSET(overlay_0xa580.Osprey.BB_txiq_corr_coeff_67_b0)

#define AR_PHY_TX_IQCAL_CORR_COEFF_01_B0_POSEIDON    AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiq_corr_coeff_01_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_23_B0_POSEIDON    AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiq_corr_coeff_23_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_45_B0_POSEIDON    AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiq_corr_coeff_45_b0)
#define AR_PHY_TX_IQCAL_CORR_COEFF_67_B0_POSEIDON    AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_txiq_corr_coeff_67_b0)

#define AR_PHY_TXGAIN_TAB(_i)       AR_SM_OFFSET(BB_tx_gain_tab_##_i) /* values 1-22 */
#define AR_PHY_TXGAIN_TAB_PAL(_i)   AR_SM_OFFSET(BB_tx_gain_tab_pal_##_i) /* values 1-22 */
#define AR_PHY_PANIC_WD_STATUS      AR_SM_OFFSET(BB_panic_watchdog_status)
#define AR_PHY_PANIC_WD_CTL_1       AR_SM_OFFSET(BB_panic_watchdog_ctrl_1)
#define AR_PHY_PANIC_WD_CTL_2       AR_SM_OFFSET(BB_panic_watchdog_ctrl_2)
#define AR_PHY_BT_CTL               AR_SM_OFFSET(BB_bluetooth_cntl)
#define AR_PHY_ONLY_WARMRESET       AR_SM_OFFSET(BB_phyonly_warm_reset)
#define AR_PHY_ONLY_CTL             AR_SM_OFFSET(BB_phyonly_control)
#define AR_PHY_ECO_CTRL             AR_SM_OFFSET(BB_eco_ctrl)
#define AR_PHY_BB_THERM_ADC_1       AR_SM_OFFSET(BB_therm_adc_1)
#define AR_PHY_BB_THERM_ADC_4       AR_SM_OFFSET(BB_therm_adc_4)

#define AR_PHY_65NM(_field)         offsetof(struct radio65_reg, _field)
#define AR_PHY_65NM_CH0_TXRF1       AR_PHY_65NM(ch0_TXRF1)
#define AR_PHY_65NM_CH0_TXRF2       AR_PHY_65NM(ch0_TXRF2)
#define AR_PHY_65NM_CH0_TXRF2_DB2G              0x07000000
#define AR_PHY_65NM_CH0_TXRF2_DB2G_S            24
#define AR_PHY_65NM_CH0_TXRF2_OB2G_CCK          0x00E00000
#define AR_PHY_65NM_CH0_TXRF2_OB2G_CCK_S        21
#define AR_PHY_65NM_CH0_TXRF2_OB2G_PSK          0x001C0000
#define AR_PHY_65NM_CH0_TXRF2_OB2G_PSK_S        18
#define AR_PHY_65NM_CH0_TXRF2_OB2G_QAM          0x00038000
#define AR_PHY_65NM_CH0_TXRF2_OB2G_QAM_S        15
#define AR_PHY_65NM_CH0_TXRF3       AR_PHY_65NM(ch0_TXRF3)
#define AR_PHY_65NM_CH0_TXRF3_CAPDIV2G          0x0000001E
#define AR_PHY_65NM_CH0_TXRF3_CAPDIV2G_S        1
#define AR_PHY_65NM_CH0_TXRF3_OLD_PAL_SPARE     0x00000001
#define AR_PHY_65NM_CH0_TXRF3_OLD_PAL_SPARE_S   0
#define AR_PHY_65NM_CH1_TXRF1       AR_PHY_65NM(ch1_TXRF1)
#define AR_PHY_65NM_CH1_TXRF2       AR_PHY_65NM(ch1_TXRF2)
#define AR_PHY_65NM_CH1_TXRF3       AR_PHY_65NM(ch1_TXRF3)
#define AR_PHY_65NM_CH2_TXRF1       AR_PHY_65NM(ch2_TXRF1)
#define AR_PHY_65NM_CH2_TXRF2       AR_PHY_65NM(ch2_TXRF2)
#define AR_PHY_65NM_CH2_TXRF3       AR_PHY_65NM(ch2_TXRF3)

#define AR_PHY_65NM_CH0_SYNTH4      AR_PHY_65NM(ch0_SYNTH4)
#define AR_PHY_SYNTH4_LONG_SHIFT_SELECT   0x00000002
#define AR_PHY_SYNTH4_LONG_SHIFT_SELECT_S 1
#define AR_PHY_65NM_CH0_SYNTH7      AR_PHY_65NM(ch0_SYNTH7)
#define AR_PHY_65NM_CH0_BIAS1       AR_PHY_65NM(ch0_BIAS1)
#define AR_PHY_65NM_CH0_BIAS2       AR_PHY_65NM(ch0_BIAS2)
#define AR_PHY_65NM_CH0_BIAS4       AR_PHY_65NM(ch0_BIAS4)
#define AR_PHY_65NM_CH0_RXTX4       AR_PHY_65NM(ch0_RXTX4)
#define AR_PHY_65NM_CH0_SYNTH12      AR_PHY_65NM(ch0_SYNTH12)
#define AR_PHY_65NM_CH0_SYNTH12_VREFMUL3          0x00780000
#define AR_PHY_65NM_CH0_SYNTH12_VREFMUL3_S        19
#define AR_PHY_65NM_CH1_RXTX4       AR_PHY_65NM(ch1_RXTX4)
#define AR_PHY_65NM_CH2_RXTX4       AR_PHY_65NM(ch2_RXTX4)
#define AR_PHY_65NM_RXTX4_XLNA_BIAS   0xC0000000
#define AR_PHY_65NM_RXTX4_XLNA_BIAS_S 30

#define AR_PHY_65NM_CH0_TOP         AR_PHY_65NM(overlay_0x16180.Osprey.ch0_TOP)
#define AR_PHY_65NM_CH0_TOP_JUPITER AR_PHY_65NM(overlay_0x16180.Jupiter.ch0_TOP1)
#define AR_PHY_65NM_CH0_TOP_XPABIASLVL         0x00000300
#define AR_PHY_65NM_CH0_TOP_XPABIASLVL_S       8
#define AR_PHY_65NM_CH0_TOP2        AR_PHY_65NM(overlay_0x16180.Osprey.ch0_TOP2)

#define AR_OSPREY_CH0_XTAL              AR_PHY_65NM(overlay_0x16180.Osprey.ch0_XTAL)
#define AR_OSPREY_CHO_XTAL_CAPINDAC     0x7F000000
#define AR_OSPREY_CHO_XTAL_CAPINDAC_S   24
#define AR_OSPREY_CHO_XTAL_CAPOUTDAC    0x00FE0000
#define AR_OSPREY_CHO_XTAL_CAPOUTDAC_S  17

#define AR_PHY_65NM_CH0_THERM       AR_PHY_65NM(overlay_0x16180.Osprey.ch0_THERM)
#define AR_PHY_65NM_CH0_THERM_JUPITER AR_PHY_65NM(overlay_0x16180.Jupiter.ch0_THERM)

#define AR_PHY_65NM_CH0_THERM_XPABIASLVL_MSB   0x00000003
#define AR_PHY_65NM_CH0_THERM_XPABIASLVL_MSB_S 0
#define AR_PHY_65NM_CH0_THERM_XPASHORT2GND     0x00000004
#define AR_PHY_65NM_CH0_THERM_XPASHORT2GND_S   2
#define AR_PHY_65NM_CH0_THERM_SAR_ADC_OUT      0x0000ff00
#define AR_PHY_65NM_CH0_THERM_SAR_ADC_OUT_S    8
#define AR_PHY_65NM_CH0_THERM_START            0x20000000
#define AR_PHY_65NM_CH0_THERM_START_S          29 
#define AR_PHY_65NM_CH0_THERM_LOCAL            0x80000000
#define AR_PHY_65NM_CH0_THERM_LOCAL_S          31

#define AR_PHY_65NM_CH0_RXTX1       AR_PHY_65NM(ch0_RXTX1)
#define AR_PHY_65NM_CH0_RXTX2       AR_PHY_65NM(ch0_RXTX2)
#define AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK         0x00000004
#define AR_PHY_65NM_CH0_RXTX2_SYNTHON_MASK_S       2
#define AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK        0x00000008
#define AR_PHY_65NM_CH0_RXTX2_SYNTHOVR_MASK_S      3
#define AR_PHY_65NM_CH0_RXTX3       AR_PHY_65NM(ch0_RXTX3)
#define AR_PHY_65NM_CH1_RXTX1       AR_PHY_65NM(ch1_RXTX1)
#define AR_PHY_65NM_CH1_RXTX2       AR_PHY_65NM(ch1_RXTX2)
#define AR_PHY_65NM_CH1_RXTX3       AR_PHY_65NM(ch1_RXTX3)
#define AR_PHY_65NM_CH2_RXTX1       AR_PHY_65NM(ch2_RXTX1)
#define AR_PHY_65NM_CH2_RXTX2       AR_PHY_65NM(ch2_RXTX2)
#define AR_PHY_65NM_CH2_RXTX3       AR_PHY_65NM(ch2_RXTX3)

#define AR_PHY_65NM_CH0_BB1         AR_PHY_65NM(ch0_BB1)
#define AR_PHY_65NM_CH0_BB2         AR_PHY_65NM(ch0_BB2)
#define AR_PHY_65NM_CH0_BB3         AR_PHY_65NM(ch0_BB3)
#define AR_PHY_65NM_CH1_BB1         AR_PHY_65NM(ch1_BB1)
#define AR_PHY_65NM_CH1_BB2         AR_PHY_65NM(ch1_BB2)
#define AR_PHY_65NM_CH1_BB3         AR_PHY_65NM(ch1_BB3)
#define AR_PHY_65NM_CH2_BB1         AR_PHY_65NM(ch2_BB1)
#define AR_PHY_65NM_CH2_BB2         AR_PHY_65NM(ch2_BB2)
#define AR_PHY_CH_BB3_SEL_OFST_READBK       0x00000300
#define AR_PHY_CH_BB3_SEL_OFST_READBK_S     8
#define AR_PHY_CH_BB3_OFSTCORRI2VQ          0x03e00000
#define AR_PHY_CH_BB3_OFSTCORRI2VQ_S        21
#define AR_PHY_CH_BB3_OFSTCORRI2VI          0x7c000000
#define AR_PHY_CH_BB3_OFSTCORRI2VI_S        26

#define AR_PHY_RX1DB_BIQUAD_LONG_SHIFT   	0x00380000
#define AR_PHY_RX1DB_BIQUAD_LONG_SHIFT_S 	19
#define AR_PHY_RX6DB_BIQUAD_LONG_SHIFT   	0x00c00000
#define AR_PHY_RX6DB_BIQUAD_LONG_SHIFT_S 	22
#define AR_PHY_LNAGAIN_LONG_SHIFT   		0xe0000000
#define AR_PHY_LNAGAIN_LONG_SHIFT_S 		29
#define AR_PHY_MXRGAIN_LONG_SHIFT   		0x03000000
#define AR_PHY_MXRGAIN_LONG_SHIFT_S 		24
#define AR_PHY_VGAGAIN_LONG_SHIFT   		0x1c000000
#define AR_PHY_VGAGAIN_LONG_SHIFT_S 		26
#define AR_PHY_SCFIR_GAIN_LONG_SHIFT   		0x00000001
#define AR_PHY_SCFIR_GAIN_LONG_SHIFT_S 		0
#define AR_PHY_MANRXGAIN_LONG_SHIFT   		0x00000002
#define AR_PHY_MANRXGAIN_LONG_SHIFT_S 		1
#define AR_PHY_MANTXGAIN_LONG_SHIFT   		0x80000000
#define AR_PHY_MANTXGAIN_LONG_SHIFT_S 		31

/*
 * SM Field Definitions
 */
 
/* BB_cl_cal_ctrl - AR_PHY_CL_CAL_CTL */
#define AR_PHY_CL_CAL_ENABLE          0x00000002    /* do carrier leak calibration after agc_calibrate_done */ 
#define AR_PHY_PARALLEL_CAL_ENABLE    0x00000001 
#define AR_PHY_TPCRG1_PD_CAL_ENABLE   0x00400000
#define AR_PHY_TPCRG1_PD_CAL_ENABLE_S 22
#define AR_PHY_CL_MAP_HW_GEN		  0x80000000
#define AR_PHY_CL_MAP_HW_GEN_S		  31

/* BB_addac_parallel_control - AR_PHY_ADDAC_PARA_CTL */
#define AR_PHY_ADDAC_PARACTL_OFF_PWDADC 0x00008000

/* BB_fcal_2_b0 - AR_PHY_FCAL_2_0 */
#define AR_PHY_FCAL20_CAP_STATUS_0    0x01f00000
#define AR_PHY_FCAL20_CAP_STATUS_0_S  20

/* BB_rfbus_request */
#define AR_PHY_RFBUS_REQ_EN     0x00000001  /* request for RF bus */
/* BB_rfbus_grant */
#define AR_PHY_RFBUS_GRANT_EN   0x00000001  /* RF bus granted */
/* BB_gen_controls */
#define AR_PHY_GC_TURBO_MODE       0x00000001  /* set turbo mode bits */
#define AR_PHY_GC_TURBO_SHORT      0x00000002  /* set short symbols to turbo mode setting */
#define AR_PHY_GC_DYN2040_EN       0x00000004  /* enable dyn 20/40 mode */
#define AR_PHY_GC_DYN2040_PRI_ONLY 0x00000008  /* dyn 20/40 - primary only */
#define AR_PHY_GC_DYN2040_PRI_CH   0x00000010  /* dyn 20/40 - primary ch offset (0=+10MHz, 1=-10MHz)*/
#define AR_PHY_GC_DYN2040_PRI_CH_S 4 

#define AR_PHY_GC_DYN2040_EXT_CH   0x00000020  /* dyn 20/40 - ext ch spacing (0=20MHz/ 1=25MHz) */
#define AR_PHY_GC_HT_EN            0x00000040  /* ht enable */
#define AR_PHY_GC_SHORT_GI_40      0x00000080  /* allow short GI for HT 40 */
#define AR_PHY_GC_WALSH            0x00000100  /* walsh spatial spreading for 2 chains,2 streams TX */
#define AR_PHY_GC_SINGLE_HT_LTF1   0x00000200  /* single length (4us) 1st HT long training symbol */
#define AR_PHY_GC_GF_DETECT_EN     0x00000400  /* enable Green Field detection. Only affects rx, not tx */
#define AR_PHY_GC_ENABLE_DAC_FIFO  0x00000800  /* fifo between bb and dac */

#define AR_PHY_MS_HALF_RATE        0x00000020
#define AR_PHY_MS_QUARTER_RATE     0x00000040

/* BB_analog_power_on_time */
#define AR_PHY_RX_DELAY_DELAY      0x00003FFF  /* delay from wakeup to rx ena */
/* BB_agc_control */
#define AR_PHY_AGC_CONTROL_CAL              0x00000001  /* do internal calibration */
#define AR_PHY_AGC_CONTROL_NF               0x00000002  /* do noise-floor calibration */
#define AR_PHY_AGC_CONTROL_OFFSET_CAL       0x00000800  /* allow offset calibration */
#define AR_PHY_AGC_CONTROL_ENABLE_NF        0x00008000  /* enable noise floor calibration to happen */
#define AR_PHY_AGC_CONTROL_FLTR_CAL         0x00010000  /* allow tx filter calibration */
#define AR_PHY_AGC_CONTROL_NO_UPDATE_NF     0x00020000  /* don't update noise floor automatically */
#define AR_PHY_AGC_CONTROL_EXT_NF_PWR_MEAS  0x00040000  /* extend noise floor power measurement */
#define AR_PHY_AGC_CONTROL_CLC_SUCCESS      0x00080000  /* carrier leak calibration done */
#define AR_PHY_AGC_CONTROL_PKDET_CAL        0x00100000  /* allow peak deteter calibration */

#define AR_PHY_AGC_CONTROL_YCOK_MAX                                 0x000003c0
#define AR_PHY_AGC_CONTROL_YCOK_MAX_S                                        6

/* BB_iq_adc_cal_mode */
#define AR_PHY_CALMODE_IQ           0x00000000
#define AR_PHY_CALMODE_ADC_GAIN     0x00000001
#define AR_PHY_CALMODE_ADC_DC_PER   0x00000002
#define AR_PHY_CALMODE_ADC_DC_INIT  0x00000003
/* BB_analog_swap */
#define AR_PHY_SWAP_ALT_CHAIN       0x00000040
/* BB_modes_select */
#define AR_PHY_MODE_OFDM            0x00000000  /* OFDM */
#define AR_PHY_MODE_CCK             0x00000001  /* CCK */
#define AR_PHY_MODE_DYNAMIC         0x00000004  /* dynamic CCK/OFDM mode */
#define AR_PHY_MODE_DYNAMIC_S		2
#define AR_PHY_MODE_HALF            0x00000020  /* enable half rate */
#define AR_PHY_MODE_QUARTER         0x00000040  /* enable quarter rate */
#define AR_PHY_MAC_CLK_MODE         0x00000080  /* MAC runs at 128/141MHz clock */
#define AR_PHY_MODE_DYN_CCK_DISABLE 0x00000100  /* Disable dynamic CCK detection */
#define AR_PHY_MODE_SVD_HALF        0x00000200  /* enable svd half rate */
#define AR_PHY_MODE_DISABLE_CCK     0x00000100
#define AR_PHY_MODE_DISABLE_CCK_S   8
/* BB_active */
#define AR_PHY_ACTIVE_EN    0x00000001  /* Activate PHY chips */
#define AR_PHY_ACTIVE_DIS   0x00000000  /* Deactivate PHY chips */
/* BB_force_analog */
#define AR_PHY_FORCE_XPA_CFG    0x000000001
#define AR_PHY_FORCE_XPA_CFG_S  0
/* BB_xpa_timing_control */
#define AR_PHY_XPA_TIMING_CTL_TX_END_XPAB_OFF    0xFF000000
#define AR_PHY_XPA_TIMING_CTL_TX_END_XPAB_OFF_S  24
#define AR_PHY_XPA_TIMING_CTL_TX_END_XPAA_OFF    0x00FF0000
#define AR_PHY_XPA_TIMING_CTL_TX_END_XPAA_OFF_S  16
#define AR_PHY_XPA_TIMING_CTL_FRAME_XPAB_ON      0x0000FF00
#define AR_PHY_XPA_TIMING_CTL_FRAME_XPAB_ON_S    8
#define AR_PHY_XPA_TIMING_CTL_FRAME_XPAA_ON      0x000000FF
#define AR_PHY_XPA_TIMING_CTL_FRAME_XPAA_ON_S    0
/* BB_tx_timing_3 */
#define AR_PHY_TX_END_TO_A2_RX_ON       0x00FF0000
#define AR_PHY_TX_END_TO_A2_RX_ON_S     16
/* BB_tx_timing_2 */
#define AR_PHY_TX_END_DATA_START  0x000000FF
#define AR_PHY_TX_END_DATA_START_S  0
#define AR_PHY_TX_END_PA_ON       0x0000FF00
#define AR_PHY_TX_END_PA_ON_S       8
/* BB_tpc_5_b0 */
/* ar2413 power control */
#define AR_PHY_TPCRG5_PD_GAIN_OVERLAP   0x0000000F
#define AR_PHY_TPCRG5_PD_GAIN_OVERLAP_S     0
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1    0x000003F0
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1_S  4
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2    0x0000FC00
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2_S  10
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3    0x003F0000
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3_S  16
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4    0x0FC00000
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4_S  22
/* BB_tpc_1 */
#define AR_PHY_TPCRG1_NUM_PD_GAIN   0x0000c000
#define AR_PHY_TPCRG1_NUM_PD_GAIN_S 14
#define AR_PHY_TPCRG1_PD_GAIN_1    0x00030000
#define AR_PHY_TPCRG1_PD_GAIN_1_S  16
#define AR_PHY_TPCRG1_PD_GAIN_2    0x000C0000
#define AR_PHY_TPCRG1_PD_GAIN_2_S  18
#define AR_PHY_TPCRG1_PD_GAIN_3    0x00300000
#define AR_PHY_TPCRG1_PD_GAIN_3_S  20
#define AR_PHY_TPCGR1_FORCED_DAC_GAIN   0x0000003e
#define AR_PHY_TPCGR1_FORCED_DAC_GAIN_S 1
#define AR_PHY_TPCGR1_FORCE_DAC_GAIN    0x00000001
 
/* BB_tx_forced_gain */
#define AR_PHY_TXGAIN_FORCE               0x00000001
#define AR_PHY_TXGAIN_FORCE_S             0
#define AR_PHY_TXGAIN_FORCED_PADVGNRA     0x00003c00
#define AR_PHY_TXGAIN_FORCED_PADVGNRA_S   10
#define AR_PHY_TXGAIN_FORCED_PADVGNRB     0x0003c000
#define AR_PHY_TXGAIN_FORCED_PADVGNRB_S   14
#define AR_PHY_TXGAIN_FORCED_PADVGNRC     0x003c0000
#define AR_PHY_TXGAIN_FORCED_PADVGNRC_S   18
#define AR_PHY_TXGAIN_FORCED_PADVGNRD     0x00c00000
#define AR_PHY_TXGAIN_FORCED_PADVGNRD_S   22
#define AR_PHY_TXGAIN_FORCED_TXMXRGAIN    0x000003c0
#define AR_PHY_TXGAIN_FORCED_TXMXRGAIN_S  6
#define AR_PHY_TXGAIN_FORCED_TXBB1DBGAIN  0x0000000e
#define AR_PHY_TXGAIN_FORCED_TXBB1DBGAIN_S 1
#define AR_PHY_TXGAIN_FORCED_TXBB6DBGAIN  0x00000030
#define AR_PHY_TXGAIN_FORCED_TXBB6DBGAIN_S 4

/* BB_powertx_rate1 */
#define AR_PHY_POWER_TX_RATE1   0x9934
#define AR_PHY_POWER_TX_RATE2   0x9938
#define AR_PHY_POWER_TX_RATE_MAX    AR_PHY_PWRTX_MAX     
#define AR_PHY_POWER_TX_RATE_MAX_TPC_ENABLE 0x00000040
/* BB_test_controls */
#define PHY_AGC_CLR             0x10000000      /* disable AGC to A2 */
#define RFSILENT_BB             0x00002000      /* shush bb */
/* BB_chan_info_gain_diff */
#define AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_MASK          0xFFF    /* PPM value is 12-bit signed integer */
#define AR_PHY_CHAN_INFO_GAIN_DIFF_PPM_SIGNED_BIT    0x800    /* Sign bit */
#define AR_PHY_CHAN_INFO_GAIN_DIFF_UPPER_LIMIT         320    /* Maximum absolute value */
/* BB_chaninfo_ctrl */
#define AR_PHY_CHAN_INFO_MEMORY_CAPTURE_MASK         0x0001
/* BB_search_start_delay */
#define AR_PHY_RX_DELAY_DELAY   0x00003FFF  /* delay from wakeup to rx ena */
/* BB_bbb_tx_ctrl */
#define AR_PHY_CCK_TX_CTRL_JAPAN    0x00000010
/* BB_spectral_scan */
#define AR_PHY_SPECTRAL_SCAN_ENABLE         0x00000001  /* Enable spectral scan */
#define AR_PHY_SPECTRAL_SCAN_ENABLE_S       0
#define AR_PHY_SPECTRAL_SCAN_ACTIVE         0x00000002  /* Activate spectral scan */
#define AR_PHY_SPECTRAL_SCAN_ACTIVE_S       1
#define AR_PHY_SPECTRAL_SCAN_FFT_PERIOD     0x000000F0  /* Interval for FFT reports */
#define AR_PHY_SPECTRAL_SCAN_FFT_PERIOD_S   4
#define AR_PHY_SPECTRAL_SCAN_PERIOD         0x0000FF00  /* Interval for FFT reports */
#define AR_PHY_SPECTRAL_SCAN_PERIOD_S       8
#define AR_PHY_SPECTRAL_SCAN_COUNT          0x0FFF0000  /* Number of reports */
#define AR_PHY_SPECTRAL_SCAN_COUNT_S        16
#define AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT   0x10000000  /* Short repeat */
#define AR_PHY_SPECTRAL_SCAN_SHORT_REPEAT_S 28
#define AR_PHY_SPECTRAL_SCAN_PRIORITY_HI    0x20000000  /* high priority */
#define AR_PHY_SPECTRAL_SCAN_PRIORITY_HI_S  29
/* BB_channel_status */
#define AR_PHY_CHANNEL_STATUS_RX_CLEAR      0x00000004
/* BB_rtt_ctrl */
#define AR_PHY_RTT_CTRL_ENA_RADIO_RETENTION     0x00000001
#define AR_PHY_RTT_CTRL_ENA_RADIO_RETENTION_S   0
#define AR_PHY_RTT_CTRL_RESTORE_MASK            0x0000007E
#define AR_PHY_RTT_CTRL_RESTORE_MASK_S          1
#define AR_PHY_RTT_CTRL_FORCE_RADIO_RESTORE     0x00000080
#define AR_PHY_RTT_CTRL_FORCE_RADIO_RESTORE_S   7
/* BB_rtt_table_sw_intf_b0 */
#define AR_PHY_RTT_SW_RTT_TABLE_ACCESS_0        0x00000001
#define AR_PHY_RTT_SW_RTT_TABLE_ACCESS_0_S      0
#define AR_PHY_RTT_SW_RTT_TABLE_WRITE_0         0x00000002
#define AR_PHY_RTT_SW_RTT_TABLE_WRITE_0_S       1
#define AR_PHY_RTT_SW_RTT_TABLE_ADDR_0          0x0000001C
#define AR_PHY_RTT_SW_RTT_TABLE_ADDR_0_S        2
/* BB_rtt_table_sw_intf_1_b0 */
#define AR_PHY_RTT_SW_RTT_TABLE_DATA_0          0xFFFFFFF0
#define AR_PHY_RTT_SW_RTT_TABLE_DATA_0_S        4
/* BB_txiqcal_control_0 */
#define AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL   0x80000000
#define AR_PHY_TX_IQCAL_CONTROL_0_ENABLE_TXIQ_CAL_S 31
/* BB_txiqcal_control_1 */
#define AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT             0x01fc0000
#define AR_PHY_TX_IQCAL_CONTROL_1_IQCORR_I_Q_COFF_DELPT_S                   18
/* BB_txiqcal_start */
#define AR_PHY_TX_IQCAL_START_DO_CAL        0x00000001
#define AR_PHY_TX_IQCAL_START_DO_CAL_S      0
/* BB_txiqcal_start for Poseidon */
#define AR_PHY_TX_IQCAL_START_DO_CAL_POSEIDON        0x80000000
#define AR_PHY_TX_IQCAL_START_DO_CAL_POSEIDON_S      31

/* Generic B0, B1, B2 IQ Cal bit fields */
/* BB_txiqcal_status_b* */
#define AR_PHY_TX_IQCAL_STATUS_FAILED    0x00000001
#define AR_PHY_CALIBRATED_GAINS_0_S 1
#define AR_PHY_CALIBRATED_GAINS_0 (0x1f<<AR_PHY_CALIBRATED_GAINS_0_S)
/* BB_txiq_corr_coeff_01_b* */
#define AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE_S    0
#define AR_PHY_TX_IQCAL_CORR_COEFF_00_COEFF_TABLE      0x00003fff
#define AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE_S    14
#define AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE      (0x00003fff<<AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE_S)

/* temp compensation */
/* BB_tpc_18 */
#define AR_PHY_TPC_18_THERM_CAL_VALUE           0xff //Mask bits 7:0
#define AR_PHY_TPC_18_THERM_CAL_VALUE_S         0
/* BB_tpc_19 */
#define AR_PHY_TPC_19_ALPHA_THERM               0xff //Mask bits 7:0
#define AR_PHY_TPC_19_ALPHA_THERM_S             0

/* ch0_RXTX4 */
#define AR_PHY_65NM_CH0_RXTX4_THERM_ON          0x10000000
#define AR_PHY_65NM_CH0_RXTX4_THERM_ON_S        28

/* BB_therm_adc_1 */
#define AR_PHY_BB_THERM_ADC_1_INIT_THERM        0x000000ff
#define AR_PHY_BB_THERM_ADC_1_INIT_THERM_S      0

/* BB_therm_adc_4 */
#define AR_PHY_BB_THERM_ADC_4_LATEST_THERM      0x000000ff
#define AR_PHY_BB_THERM_ADC_4_LATEST_THERM_S    0

/* BB_switch_table_chn_b */
#define AR_PHY_SWITCH_TABLE_R0                  0x00000010
#define AR_PHY_SWITCH_TABLE_R0_S                4
#define AR_PHY_SWITCH_TABLE_R1                  0x00000040
#define AR_PHY_SWITCH_TABLE_R1_S                6
#define AR_PHY_SWITCH_TABLE_R12                 0x00000100
#define AR_PHY_SWITCH_TABLE_R12_S               8      

/*
 * Channel 1 Register Map
 */
#define AR_CHAN1_BASE         offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_chn1_reg_map)
#define AR_CHAN1_OFFSET(_x)   (AR_CHAN1_BASE + offsetof(struct chn1_reg_map, _x))

#define AR_PHY_TIMING4_1            AR_CHAN1_OFFSET(BB_timing_control_4_b1)
#define AR_PHY_EXT_CCA_1            AR_CHAN1_OFFSET(BB_ext_chan_pwr_thr_2_b1)
#define AR_PHY_TX_PHASE_RAMP_1      AR_CHAN1_OFFSET(BB_tx_phase_ramp_b1)
#define AR_PHY_ADC_GAIN_DC_CORR_1   AR_CHAN1_OFFSET(BB_adc_gain_dc_corr_b1)

#define AR_PHY_IQ_ADC_MEAS_0_B1     AR_CHAN_OFFSET(BB_iq_adc_meas_0_b1)
#define AR_PHY_IQ_ADC_MEAS_1_B1     AR_CHAN_OFFSET(BB_iq_adc_meas_1_b1)
#define AR_PHY_IQ_ADC_MEAS_2_B1     AR_CHAN_OFFSET(BB_iq_adc_meas_2_b1)
#define AR_PHY_IQ_ADC_MEAS_3_B1     AR_CHAN_OFFSET(BB_iq_adc_meas_3_b1)

#define AR_PHY_TX_IQ_CORR_1         AR_CHAN1_OFFSET(BB_tx_iq_corr_b1)
#define AR_PHY_SPUR_REPORT_1        AR_CHAN1_OFFSET(BB_spur_report_b1)
#define AR_PHY_CHAN_INFO_TAB_1      AR_CHAN1_OFFSET(BB_chan_info_chan_tab_b1)
#define AR_PHY_RX_IQCAL_CORR_B1     AR_CHAN1_OFFSET(BB_rx_iq_corr_b1)

/*
 * Channel 1 Field Definitions
 */
/* BB_ext_chan_pwr_thr_2_b1 */
#define AR_PHY_CH1_EXT_MINCCA_PWR   0x01FF0000
#define AR_PHY_CH1_EXT_MINCCA_PWR_S 16

/*
 * AGC 1 Register Map
 */
#define AR_AGC1_BASE      offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_agc1_reg_map)
#define AR_AGC1_OFFSET(_x)   (AR_AGC1_BASE + offsetof(struct agc1_reg_map, _x))

#define AR_PHY_FORCEMAX_GAINS_1      AR_AGC1_OFFSET(BB_gain_force_max_gains_b1)
#define AR_PHY_GAINS_MINOFF_1        AR_AGC1_OFFSET(BB_gains_min_offsets_b1)
#define AR_PHY_EXT_ATTEN_CTL_1       AR_AGC1_OFFSET(BB_ext_atten_switch_ctl_b1)
#define AR_PHY_CCA_1                 AR_AGC1_OFFSET(BB_cca_b1)
#define AR_PHY_CCA_CTRL_1            AR_AGC1_OFFSET(BB_cca_ctrl_2_b1)
#define AR_PHY_RSSI_1                AR_AGC1_OFFSET(BB_rssi_b1)
#define AR_PHY_SPUR_CCK_REP_1        AR_AGC1_OFFSET(BB_spur_est_cck_report_b1)
#define AR_PHY_RX_OCGAIN_2           AR_AGC1_OFFSET(BB_rx_ocgain2)
#define AR_PHY_DIG_DC_STATUS_I_B1 AR_AGC1_OFFSET(BB_agc_dig_dc_status_i_b1)
#define AR_PHY_DIG_DC_STATUS_Q_B1 AR_AGC1_OFFSET(BB_agc_dig_dc_status_q_b1)

/*
 * AGC 1 Register Map for Poseidon
 */
#define AR_AGC1_BASE_POSEIDON      offsetof(struct bb_reg_map, overlay_0xa800.Poseidon.bb_agc1_reg_map)
#define AR_AGC1_OFFSET_POSEIDON(_x)   (AR_AGC1_BASE_POSEIDON + offsetof(struct agc1_reg_map, _x))

#define AR_PHY_FORCEMAX_GAINS_1_POSEIDON      AR_AGC1_OFFSET_POSEIDON(BB_gain_force_max_gains_b1)
#define AR_PHY_EXT_ATTEN_CTL_1_POSEIDON       AR_AGC1_OFFSET_POSEIDON(BB_ext_atten_switch_ctl_b1)
#define AR_PHY_RSSI_1_POSEIDON                AR_AGC1_OFFSET_POSEIDON(BB_rssi_b1)
#define AR_PHY_RX_OCGAIN_2_POSEIDON           AR_AGC1_OFFSET_POSEIDON(BB_rx_ocgain2)

/*
 * AGC 1 Field Definitions
 */
/* BB_cca_b1 */
#define AR_PHY_CH1_MINCCA_PWR   0x1FF00000
#define AR_PHY_CH1_MINCCA_PWR_S 20 

/*
 * SM 1 Register Map
 */
#define AR_SM1_BASE      offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_sm1_reg_map)
#define AR_SM1_OFFSET(_x)   (AR_SM1_BASE + offsetof(struct sm1_reg_map, _x))

#define AR_PHY_SWITCH_CHAIN_1    AR_SM1_OFFSET(BB_switch_table_chn_b1)
#define AR_PHY_FCAL_2_1          AR_SM1_OFFSET(BB_fcal_2_b1)
#define AR_PHY_DFT_TONE_CTL_1    AR_SM1_OFFSET(BB_dft_tone_ctrl_b1)
#define AR_PHY_BBGAINMAP_0_1_1   AR_SM1_OFFSET(BB_cl_bbgain_map_0_1_b1)
#define AR_PHY_BBGAINMAP_2_3_1   AR_SM1_OFFSET(BB_cl_bbgain_map_2_3_b1)
#define AR_PHY_CL_TAB_1          AR_SM1_OFFSET(BB_cl_tab_b1)
#define AR_PHY_CHAN_INFO_GAIN_1  AR_SM1_OFFSET(BB_chan_info_gain_b1)
#define AR_PHY_TPC_4_B1          AR_SM1_OFFSET(BB_tpc_4_b1)
#define AR_PHY_TPC_5_B1          AR_SM1_OFFSET(BB_tpc_5_b1)
#define AR_PHY_TPC_6_B1          AR_SM1_OFFSET(BB_tpc_6_b1)
#define AR_PHY_TPC_11_B1         AR_SM1_OFFSET(BB_tpc_11_b1)
#define AR_SCORPION_PHY_TPC_19_B1   AR_SM1_OFFSET(overlay_b440.Scorpion.BB_tpc_19_b1)
#define AR_PHY_PDADC_TAB_1       AR_SM1_OFFSET(overlay_b440.BB_pdadc_tab_b1)


#define AR_PHY_RTT_TABLE_SW_INTF_B1     AR_SM1_OFFSET(overlay_b440.Jupiter_20.BB_rtt_table_sw_intf_b1)
#define AR_PHY_RTT_TABLE_SW_INTF_1_B1   AR_SM1_OFFSET(overlay_b440.Jupiter_20.BB_rtt_table_sw_intf_1_b1)

#define AR_PHY_TX_IQCAL_STATUS_B1   AR_SM1_OFFSET(BB_txiqcal_status_b1)
#define AR_PHY_TX_IQCAL_CORR_COEFF_01_B1    AR_SM1_OFFSET(BB_txiq_corr_coeff_01_b1)
#define AR_PHY_TX_IQCAL_CORR_COEFF_23_B1    AR_SM1_OFFSET(BB_txiq_corr_coeff_23_b1)
#define AR_PHY_TX_IQCAL_CORR_COEFF_45_B1    AR_SM1_OFFSET(BB_txiq_corr_coeff_45_b1)
#define AR_PHY_TX_IQCAL_CORR_COEFF_67_B1    AR_SM1_OFFSET(BB_txiq_corr_coeff_67_b1)
#define AR_PHY_CL_MAP_0_B1		 AR_SM1_OFFSET(BB_cl_map_0_b1)
#define AR_PHY_CL_MAP_1_B1		 AR_SM1_OFFSET(BB_cl_map_1_b1)
#define AR_PHY_CL_MAP_2_B1		 AR_SM1_OFFSET(BB_cl_map_2_b1)
#define AR_PHY_CL_MAP_3_B1		 AR_SM1_OFFSET(BB_cl_map_3_b1)
/*
 * SM 1 Field Definitions
 */
/* BB_rtt_table_sw_intf_b1 */
#define AR_PHY_RTT_SW_RTT_TABLE_ACCESS_1        0x00000001
#define AR_PHY_RTT_SW_RTT_TABLE_ACCESS_1_S      0
#define AR_PHY_RTT_SW_RTT_TABLE_WRITE_1         0x00000002
#define AR_PHY_RTT_SW_RTT_TABLE_WRITE_1_S       1
#define AR_PHY_RTT_SW_RTT_TABLE_ADDR_1          0x0000001C
#define AR_PHY_RTT_SW_RTT_TABLE_ADDR_1_S        2
/* BB_rtt_table_sw_intf_1_b1 */
#define AR_PHY_RTT_SW_RTT_TABLE_DATA_1          0xFFFFFFF0
#define AR_PHY_RTT_SW_RTT_TABLE_DATA_1_S        4

/*
 * SM 1 Register Map for Poseidon
 */
#define AR_SM1_BASE_POSEIDON      offsetof(struct bb_reg_map, overlay_0xa800.Poseidon.bb_sm1_reg_map)
#define AR_SM1_OFFSET_POSEIDON(_x)   (AR_SM1_BASE_POSEIDON + offsetof(struct sm1_reg_map, _x))

#define AR_PHY_SWITCH_CHAIN_1_POSEIDON    AR_SM1_OFFSET_POSEIDON(BB_switch_table_chn_b1)

/*
 * Channel 2 Register Map
 */
#define AR_CHAN2_BASE      offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_chn2_reg_map)
#define AR_CHAN2_OFFSET(_x)   (AR_CHAN2_BASE + offsetof(struct chn2_reg_map, _x))

#define AR_PHY_TIMING4_2            AR_CHAN2_OFFSET(BB_timing_control_4_b2)
#define AR_PHY_EXT_CCA_2            AR_CHAN2_OFFSET(BB_ext_chan_pwr_thr_2_b2)
#define AR_PHY_TX_PHASE_RAMP_2      AR_CHAN2_OFFSET(BB_tx_phase_ramp_b2)
#define AR_PHY_ADC_GAIN_DC_CORR_2   AR_CHAN2_OFFSET(BB_adc_gain_dc_corr_b2)

#define AR_PHY_IQ_ADC_MEAS_0_B2     AR_CHAN_OFFSET(BB_iq_adc_meas_0_b2)
#define AR_PHY_IQ_ADC_MEAS_1_B2     AR_CHAN_OFFSET(BB_iq_adc_meas_1_b2)
#define AR_PHY_IQ_ADC_MEAS_2_B2     AR_CHAN_OFFSET(BB_iq_adc_meas_2_b2)
#define AR_PHY_IQ_ADC_MEAS_3_B2     AR_CHAN_OFFSET(BB_iq_adc_meas_3_b2)

#define AR_PHY_TX_IQ_CORR_2         AR_CHAN2_OFFSET(BB_tx_iq_corr_b2)
#define AR_PHY_SPUR_REPORT_2        AR_CHAN2_OFFSET(BB_spur_report_b2)
#define AR_PHY_CHAN_INFO_TAB_2      AR_CHAN2_OFFSET(BB_chan_info_chan_tab_b2)
#define AR_PHY_RX_IQCAL_CORR_B2     AR_CHAN2_OFFSET(BB_rx_iq_corr_b2)

/*
 * Channel 2 Field Definitions
 */
/* BB_ext_chan_pwr_thr_2_b2 */
#define AR_PHY_CH2_EXT_MINCCA_PWR   0x01FF0000
#define AR_PHY_CH2_EXT_MINCCA_PWR_S 16
/*
 * AGC 2 Register Map
 */
#define AR_AGC2_BASE      offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_agc2_reg_map)
#define AR_AGC2_OFFSET(_x)   (AR_AGC2_BASE + offsetof(struct agc2_reg_map, _x))

#define AR_PHY_FORCEMAX_GAINS_2      AR_AGC2_OFFSET(BB_gain_force_max_gains_b2)
#define AR_PHY_GAINS_MINOFF_2        AR_AGC2_OFFSET(BB_gains_min_offsets_b2)
#define AR_PHY_EXT_ATTEN_CTL_2       AR_AGC2_OFFSET(BB_ext_atten_switch_ctl_b2)
#define AR_PHY_CCA_2                 AR_AGC2_OFFSET(BB_cca_b2)
#define AR_PHY_CCA_CTRL_2            AR_AGC2_OFFSET(BB_cca_ctrl_2_b2)
#define AR_PHY_RSSI_2                AR_AGC2_OFFSET(BB_rssi_b2)
#define AR_PHY_SPUR_CCK_REP_2        AR_AGC2_OFFSET(BB_spur_est_cck_report_b2)

/*
 * AGC 2 Field Definitions
 */
/* BB_cca_b2 */
#define AR_PHY_CH2_MINCCA_PWR   0x1FF00000
#define AR_PHY_CH2_MINCCA_PWR_S 20

/*
 * SM 2 Register Map
 */
#define AR_SM2_BASE      offsetof(struct bb_reg_map, overlay_0xa800.Osprey.bb_sm2_reg_map)
#define AR_SM2_OFFSET(_x)   (AR_SM2_BASE + offsetof(struct sm2_reg_map, _x))

#define AR_PHY_SWITCH_CHAIN_2    AR_SM2_OFFSET(BB_switch_table_chn_b2)
#define AR_PHY_FCAL_2_2          AR_SM2_OFFSET(BB_fcal_2_b2)
#define AR_PHY_DFT_TONE_CTL_2    AR_SM2_OFFSET(BB_dft_tone_ctrl_b2)
#define AR_PHY_BBGAINMAP_0_1_2   AR_SM2_OFFSET(BB_cl_bbgain_map_0_1_b2)
#define AR_PHY_BBGAINMAP_2_3_2   AR_SM2_OFFSET(BB_cl_bbgain_map_2_3_b2)
#define AR_PHY_CL_TAB_2          AR_SM2_OFFSET(BB_cl_tab_b2)
#define AR_PHY_CHAN_INFO_GAIN_2  AR_SM2_OFFSET(BB_chan_info_gain_b2)
#define AR_PHY_TPC_4_B2          AR_SM2_OFFSET(BB_tpc_4_b2)
#define AR_PHY_TPC_5_B2          AR_SM2_OFFSET(BB_tpc_5_b2)
#define AR_PHY_TPC_6_B2          AR_SM2_OFFSET(BB_tpc_6_b2)
#define AR_PHY_TPC_11_B2         AR_SM2_OFFSET(BB_tpc_11_b2)
#define AR_SCORPION_PHY_TPC_19_B2   AR_SM2_OFFSET(overlay_c440.Scorpion.BB_tpc_19_b2)
#define AR_PHY_PDADC_TAB_2          AR_SM2_OFFSET(overlay_c440.BB_pdadc_tab_b2)
#define AR_PHY_TX_IQCAL_STATUS_B2   AR_SM2_OFFSET(BB_txiqcal_status_b2)
#define AR_PHY_TX_IQCAL_CORR_COEFF_01_B2    AR_SM2_OFFSET(BB_txiq_corr_coeff_01_b2)
#define AR_PHY_TX_IQCAL_CORR_COEFF_23_B2    AR_SM2_OFFSET(BB_txiq_corr_coeff_23_b2)
#define AR_PHY_TX_IQCAL_CORR_COEFF_45_B2    AR_SM2_OFFSET(BB_txiq_corr_coeff_45_b2)
#define AR_PHY_TX_IQCAL_CORR_COEFF_67_B2    AR_SM2_OFFSET(BB_txiq_corr_coeff_67_b2)

/*
 * bb_chn_ext_reg_map
 */
#define AR_CHN_EXT_BASE_POSEIDON      offsetof(struct bb_reg_map, overlay_0xa800.Poseidon.bb_chn_ext_reg_map) 
#define AR_CHN_EXT_OFFSET_POSEIDON(_x)   (AR_CHN_EXT_BASE_POSEIDON + offsetof(struct chn_ext_reg_map, _x)) 

#define AR_PHY_PAPRD_VALID_OBDB_POSEIDON    AR_CHN_EXT_OFFSET_POSEIDON(BB_paprd_valid_obdb_b0)
#define AR_PHY_PAPRD_VALID_OBDB_0    0x3f
#define AR_PHY_PAPRD_VALID_OBDB_0_S  0
#define AR_PHY_PAPRD_VALID_OBDB_1    0x3f
#define AR_PHY_PAPRD_VALID_OBDB_1_S  6
#define AR_PHY_PAPRD_VALID_OBDB_2    0x3f
#define AR_PHY_PAPRD_VALID_OBDB_2_S  12
#define AR_PHY_PAPRD_VALID_OBDB_3    0x3f
#define AR_PHY_PAPRD_VALID_OBDB_3_S  18
#define AR_PHY_PAPRD_VALID_OBDB_4    0x3f
#define AR_PHY_PAPRD_VALID_OBDB_4_S  24

/* BB_txiqcal_status_b1 */
#define AR_PHY_TX_IQCAL_STATUS_B2_FAILED    0x00000001

/*
 * AGC 3 Register Map
 */
#define AR_AGC3_BASE      offsetof(struct bb_reg_map, bb_agc3_reg_map)
#define AR_AGC3_OFFSET(_x)   (AR_AGC3_BASE + offsetof(struct agc3_reg_map, _x))

#define AR_PHY_RSSI_3            AR_AGC3_OFFSET(BB_rssi_b3)

/*
 * Misc helper defines
 */
#define AR_PHY_CHAIN_OFFSET     (AR_CHAN1_BASE - AR_CHAN_BASE)

#define AR_PHY_NEW_ADC_DC_GAIN_CORR(_i) (AR_PHY_ADC_GAIN_DC_CORR_0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_SWITCH_CHAIN(_i)     (AR_PHY_SWITCH_CHAIN_0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_EXT_ATTEN_CTL(_i)    (AR_PHY_EXT_ATTEN_CTL_0 + (AR_PHY_CHAIN_OFFSET * (_i)))

#define AR_PHY_RXGAIN(_i)           (AR_PHY_FORCEMAX_GAINS_0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_TPCRG5(_i)           (AR_PHY_TPC_5_B0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_PDADC_TAB(_i)        (AR_PHY_PDADC_TAB_0 + (AR_PHY_CHAIN_OFFSET * (_i)))

#define AR_PHY_CAL_MEAS_0(_i)       (AR_PHY_IQ_ADC_MEAS_0_B0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_CAL_MEAS_1(_i)       (AR_PHY_IQ_ADC_MEAS_1_B0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_CAL_MEAS_2(_i)       (AR_PHY_IQ_ADC_MEAS_2_B0 + (AR_PHY_CHAIN_OFFSET * (_i)))
#define AR_PHY_CAL_MEAS_3(_i)       (AR_PHY_IQ_ADC_MEAS_3_B0 + (AR_PHY_CHAIN_OFFSET * (_i)))

#define AR_PHY_CHIP_ID          0x9818      /* PHY chip revision ID */
#define AR_PHY_CHIP_ID_REV_0        0x80 /* 5416 Rev 0 (owl 1.0) BB */
#define AR_PHY_CHIP_ID_REV_1        0x81 /* 5416 Rev 1 (owl 2.0) BB */
#define AR_PHY_CHIP_ID_SOWL_REV_0       0xb0 /* 9160 Rev 0 (sowl 1.0) BB */

/* BB Panic Watchdog control register 1 */
#define AR_PHY_BB_PANIC_NON_IDLE_ENABLE 0x00000001
#define AR_PHY_BB_PANIC_IDLE_ENABLE     0x00000002
#define AR_PHY_BB_PANIC_IDLE_MASK       0xFFFF0000
#define AR_PHY_BB_PANIC_NON_IDLE_MASK   0x0000FFFC
/* BB Panic Watchdog control register 2 */
#define AR_PHY_BB_PANIC_RST_ENABLE      0x00000002
#define AR_PHY_BB_PANIC_IRQ_ENABLE      0x00000004
#define AR_PHY_BB_PANIC_CNTL2_MASK      0xFFFFFFF9
/* BB Panic Watchdog status register */
#define AR_PHY_BB_WD_STATUS             0x00000007 /* snapshot of r_panic_watchdog_sm */
#define AR_PHY_BB_WD_STATUS_S           0
#define AR_PHY_BB_WD_DET_HANG           0x00000008 /* panic_watchdog_det_hang */
#define AR_PHY_BB_WD_DET_HANG_S         3
#define AR_PHY_BB_WD_RADAR_SM           0x000000F0 /* snapshot of radar state machine r_rdr_sm */
#define AR_PHY_BB_WD_RADAR_SM_S         4
#define AR_PHY_BB_WD_RX_OFDM_SM         0x00000F00 /* snapshot of rx state machine (OFDM) r_rx_sm */
#define AR_PHY_BB_WD_RX_OFDM_SM_S       8
#define AR_PHY_BB_WD_RX_CCK_SM          0x0000F000 /* snapshot of rx state machine (CCK) r_rx_sm_cck */
#define AR_PHY_BB_WD_RX_CCK_SM_S        12
#define AR_PHY_BB_WD_TX_OFDM_SM         0x000F0000 /* snapshot of tx state machine (OFDM) r_tx_sm */
#define AR_PHY_BB_WD_TX_OFDM_SM_S       16
#define AR_PHY_BB_WD_TX_CCK_SM          0x00F00000 /* snapshot of tx state machine (CCK) r_tx_sm_cck */
#define AR_PHY_BB_WD_TX_CCK_SM_S        20
#define AR_PHY_BB_WD_AGC_SM             0x0F000000 /* snapshot of AGC state machine r_agc_sm */
#define AR_PHY_BB_WD_AGC_SM_S           24
#define AR_PHY_BB_WD_SRCH_SM            0xF0000000 /* snapshot of agc search state machine r_srch_sm */
#define AR_PHY_BB_WD_SRCH_SM_S          28

#define AR_PHY_BB_WD_STATUS_CLR         0x00000008 /* write 0 to reset watchdog */


/***** PAPRD *****/
#define AR_PHY_PAPRD_AM2AM                          AR_CHAN_OFFSET(BB_paprd_am2am_mask) 
#define AR_PHY_PAPRD_AM2AM_MASK                     0x01ffffff     
#define AR_PHY_PAPRD_AM2AM_MASK_S                   0

#define AR_PHY_PAPRD_AM2PM                          AR_CHAN_OFFSET(BB_paprd_am2pm_mask) 
#define AR_PHY_PAPRD_AM2PM_MASK                     0x01ffffff     
#define AR_PHY_PAPRD_AM2PM_MASK_S                   0

#define AR_PHY_PAPRD_HT40                           AR_CHAN_OFFSET(BB_paprd_ht40_mask) 
#define AR_PHY_PAPRD_HT40_MASK                      0x01ffffff     
#define AR_PHY_PAPRD_HT40_MASK_S                    0

#define AR_PHY_PAPRD_CTRL0_B0                       AR_CHAN_OFFSET(BB_paprd_ctrl0_b0)
#define AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0			1
#define AR_PHY_PAPRD_CTRL0_B0_PAPRD_ENABLE_0_S			0
#define AR_PHY_PAPRD_CTRL0_B0_USE_SINGLE_TABLE_MASK     0x00000001
#define AR_PHY_PAPRD_CTRL0_B0_USE_SINGLE_TABLE_MASK_S   0x00000001
#define AR_PHY_PAPRD_CTRL0_B0_PAPRD_MAG_THRSH_0			0x1F
#define AR_PHY_PAPRD_CTRL0_B0_PAPRD_MAG_THRSH_0_S		27

#define AR_PHY_PAPRD_CTRL1_B0                       AR_CHAN_OFFSET(BB_paprd_ctrl1_b0)
#define AR_PHY_PAPRD_CTRL1_B0_PAPRD_POWER_AT_AM2AM_CAL_0	0x3f
#define AR_PHY_PAPRD_CTRL1_B0_PAPRD_POWER_AT_AM2AM_CAL_0_S	3
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2PM_ENABLE_0		1
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2PM_ENABLE_0_S		2
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2AM_ENABLE_0		1
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_AM2AM_ENABLE_0_S		1
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA		1
#define AR_PHY_PAPRD_CTRL1_B0_ADAPTIVE_SCALING_ENA_S		0
#define AR_PHY_PAPRD_CTRL1_B0_PA_GAIN_SCALE_FACT_0_MASK       0xFF
#define AR_PHY_PAPRD_CTRL1_B0_PA_GAIN_SCALE_FACT_0_MASK_S     9
#define AR_PHY_PAPRD_CTRL1_B0_PAPRD_MAG_SCALE_FACT_0		0x7FF
#define AR_PHY_PAPRD_CTRL1_B0_PAPRD_MAG_SCALE_FACT_0_S		17

#define AR_PHY_PAPRD_CTRL0_B1                       AR_CHAN1_OFFSET(BB_paprd_ctrl0_b1) 
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_MAG_THRSH_1				0x1F
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_MAG_THRSH_1_S			27
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_1		1
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_1_S	1
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1				1
#define AR_PHY_PAPRD_CTRL0_B1_PAPRD_ENABLE_1_S				0

#define AR_PHY_PAPRD_CTRL1_B1                       AR_CHAN1_OFFSET(BB_paprd_ctrl1_b1) 
#define AR_PHY_PAPRD_CTRL1_B1_PAPRD_POWER_AT_AM2AM_CAL_1	0x3f
#define AR_PHY_PAPRD_CTRL1_B1_PAPRD_POWER_AT_AM2AM_CAL_1_S	3
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2PM_ENABLE_1		1
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2PM_ENABLE_1_S		2
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2AM_ENABLE_1		1
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_AM2AM_ENABLE_1_S		1
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA		1
#define AR_PHY_PAPRD_CTRL1_B1_ADAPTIVE_SCALING_ENA_S		0
#define AR_PHY_PAPRD_CTRL1_B1_PA_GAIN_SCALE_FACT_1_MASK       0xFF
#define AR_PHY_PAPRD_CTRL1_B1_PA_GAIN_SCALE_FACT_1_MASK_S     9
#define AR_PHY_PAPRD_CTRL1_B1_PAPRD_MAG_SCALE_FACT_1		0x7FF
#define AR_PHY_PAPRD_CTRL1_B1_PAPRD_MAG_SCALE_FACT_1_S		17

#define AR_PHY_PAPRD_CTRL0_B2                       AR_CHAN2_OFFSET(BB_paprd_ctrl0_b2) 
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_MAG_THRSH_2				0x1F
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_MAG_THRSH_2_S			27
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_2		1
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_ADAPTIVE_USE_SINGLE_TABLE_2_S	1
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2				1
#define AR_PHY_PAPRD_CTRL0_B2_PAPRD_ENABLE_2_S				0


#define AR_PHY_PAPRD_CTRL1_B2                       AR_CHAN2_OFFSET(BB_paprd_ctrl1_b2) 
#define AR_PHY_PAPRD_CTRL1_B2_PAPRD_POWER_AT_AM2AM_CAL_2	0x3f
#define AR_PHY_PAPRD_CTRL1_B2_PAPRD_POWER_AT_AM2AM_CAL_2_S	3
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2PM_ENABLE_2		1
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2PM_ENABLE_2_S		2
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2AM_ENABLE_2		1
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_AM2AM_ENABLE_2_S		1
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA		1
#define AR_PHY_PAPRD_CTRL1_B2_ADAPTIVE_SCALING_ENA_S		0
#define AR_PHY_PAPRD_CTRL1_B2_PA_GAIN_SCALE_FACT_2_MASK       0xFF
#define AR_PHY_PAPRD_CTRL1_B2_PA_GAIN_SCALE_FACT_2_MASK_S     9
#define AR_PHY_PAPRD_CTRL1_B2_PAPRD_MAG_SCALE_FACT_2		0x7FF
#define AR_PHY_PAPRD_CTRL1_B2_PAPRD_MAG_SCALE_FACT_2_S		17

#define AR_PHY_PAPRD_TRAINER_CNTL1                 AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_cntl1)
#define AR_PHY_PAPRD_TRAINER_CNTL1_POSEIDON        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_cntl1)
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP			0x3f
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_SKIP_S		12
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE		1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_LB_ENABLE_S		11
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE	1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_TX_GAIN_FORCE_S	10
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE	1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_RX_BB_GAIN_FORCE_S	9
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE		1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_IQCORR_ENABLE_S		8
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING		0x3F
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_PAPRD_AGC2_SETTLING_S		1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE		1
#define AR_PHY_PAPRD_TRAINER_CNTL1_CF_CF_PAPRD_TRAIN_ENABLE_S	0

#define AR_PHY_PAPRD_TRAINER_CNTL2                 AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_cntl2)
#define AR_PHY_PAPRD_TRAINER_CNTL2_POSEIDON        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_cntl2)
#define AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN		0xFFFFFFFF
#define AR_PHY_PAPRD_TRAINER_CNTL2_CF_PAPRD_INIT_RX_BB_GAIN_S	0

#define AR_PHY_PAPRD_TRAINER_CNTL3                 AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_cntl3)
#define AR_PHY_PAPRD_TRAINER_CNTL3_POSEIDON        AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_cntl3)
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE		1
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_BBTXMIX_DISABLE_S	29
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN		0xF
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_FINE_CORR_LEN_S		24
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN		0xF
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN_S	20
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN		0xF
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_COARSE_CORR_LEN_S	20
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES		0x7
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_NUM_CORR_STAGES_S	17
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL	0x1F
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_MIN_LOOPBACK_DEL_S	12
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP			0x3F
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_QUICK_DROP_S		6
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE	0x3F
#define AR_PHY_PAPRD_TRAINER_CNTL3_CF_PAPRD_ADC_DESIRED_SIZE_S	0

#define AR_PHY_PAPRD_TRAINER_CNTL4					AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_cntl4)
#define AR_PHY_PAPRD_TRAINER_CNTL4_POSEIDON         AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_cntl4)
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_NUM_TRAIN_SAMPLES	0x3FF
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_NUM_TRAIN_SAMPLES_S	16
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA		0xF
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_SAFETY_DELTA_S		12
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR			0xFFF
#define AR_PHY_PAPRD_TRAINER_CNTL4_CF_PAPRD_MIN_CORR_S			0

#define	AR_PHY_PAPRD_PRE_POST_SCALE_0_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_0_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_0_B0_PAPRD_PRE_POST_SCALING_0_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_0_B0_PAPRD_PRE_POST_SCALING_0_0_S	0

#define	AR_PHY_PAPRD_PRE_POST_SCALE_1_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_1_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_1_B0_PAPRD_PRE_POST_SCALING_1_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_1_B0_PAPRD_PRE_POST_SCALING_1_0_S	0

#define	AR_PHY_PAPRD_PRE_POST_SCALE_2_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_2_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_2_B0_PAPRD_PRE_POST_SCALING_2_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_2_B0_PAPRD_PRE_POST_SCALING_2_0_S	0

#define AR_PHY_PAPRD_PRE_POST_SCALE_3_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_3_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_3_B0_PAPRD_PRE_POST_SCALING_3_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_3_B0_PAPRD_PRE_POST_SCALING_3_0_S	0

#define AR_PHY_PAPRD_PRE_POST_SCALE_4_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_4_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_4_B0_PAPRD_PRE_POST_SCALING_4_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_4_B0_PAPRD_PRE_POST_SCALING_4_0_S	0

#define AR_PHY_PAPRD_PRE_POST_SCALE_5_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_5_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_5_B0_PAPRD_PRE_POST_SCALING_5_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_5_B0_PAPRD_PRE_POST_SCALING_5_0_S	0

#define AR_PHY_PAPRD_PRE_POST_SCALE_6_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_6_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_6_B0_PAPRD_PRE_POST_SCALING_6_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_6_B0_PAPRD_PRE_POST_SCALING_6_0_S	0

#define AR_PHY_PAPRD_PRE_POST_SCALE_7_B0			AR_CHAN_OFFSET(BB_paprd_pre_post_scale_7_b0)
#define AR_PHY_PAPRD_PRE_POST_SCALE_7_B0_PAPRD_PRE_POST_SCALING_7_0		0x3FFFF
#define AR_PHY_PAPRD_PRE_POST_SCALE_7_B0_PAPRD_PRE_POST_SCALING_7_0_S	0

#define AR_PHY_PAPRD_TRAINER_STAT1					AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_stat1)
#define AR_PHY_PAPRD_TRAINER_STAT1_POSEIDON			AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_stat1)
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_AGC2_PWR			0xff
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_AGC2_PWR_S			9		
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_RX_GAIN_IDX		0x1f
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_RX_GAIN_IDX_S		4		
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_ACTIVE		0x1
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_ACTIVE_S		3
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_CORR_ERR			0x1
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_CORR_ERR_S			2
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_INCOMPLETE	0x1
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_INCOMPLETE_S 1
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE			1
#define AR_PHY_PAPRD_TRAINER_STAT1_PAPRD_TRAIN_DONE_S		0

#define AR_PHY_PAPRD_TRAINER_STAT2					AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_stat2)
#define AR_PHY_PAPRD_TRAINER_STAT2_POSEIDON			AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_stat2)
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_IDX			0x3	
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_IDX_S			21
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_COARSE_IDX			0x1F	
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_COARSE_IDX_S		16	
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_VAL			0xffff
#define AR_PHY_PAPRD_TRAINER_STAT2_PAPRD_FINE_VAL_S			0

#define AR_PHY_PAPRD_TRAINER_STAT3					AR_SM_OFFSET(overlay_0xa580.Osprey.BB_paprd_trainer_stat3)
#define AR_PHY_PAPRD_TRAINER_STAT3_POSEIDON			AR_SM_OFFSET(overlay_0xa580.Poseidon.BB_paprd_trainer_stat3)
#define AR_PHY_PAPRD_TRAINER_STAT3_PAPRD_TRAIN_SAMPLES_CNT		0xfffff
#define AR_PHY_PAPRD_TRAINER_STAT3_PAPRD_TRAIN_SAMPLES_CNT_S	0

#define AR_PHY_TPC_12				AR_SM_OFFSET(BB_tpc_12)
#define AR_PHY_TPC_12_DESIRED_SCALE_HT40_5				0x1F
#define AR_PHY_TPC_12_DESIRED_SCALE_HT40_5_S			25

#define AR_PHY_TPC_19_ALT_ALPHA_VOLT               0x1f 
#define AR_PHY_TPC_19_ALT_ALPHA_VOLT_S             16

#define AR_PHY_TPC_18_ALT_THERM_CAL_VALUE           0xff 
#define AR_PHY_TPC_18_ALT_THERM_CAL_VALUE_S         0

#define AR_PHY_TPC_18_ALT_VOLT_CAL_VALUE           0xff 
#define AR_PHY_TPC_18_ALT_VOLT_CAL_VALUE_S         8

#define AR_PHY_THERM_ADC_4				AR_SM_OFFSET(BB_therm_adc_4)
#define AR_PHY_THERM_ADC_4_LATEST_THERM_VALUE		0xFF
#define AR_PHY_THERM_ADC_4_LATEST_THERM_VALUE_S		0
#define AR_PHY_THERM_ADC_4_LATEST_VOLT_VALUE		0xFF
#define AR_PHY_THERM_ADC_4_LATEST_VOLT_VALUE_S		8


#define AR_PHY_TPC_11_B0							AR_SM_OFFSET(BB_tpc_11_b0)
#define AR_PHY_TPC_11_B0_OLPC_GAIN_DELTA_0			0xFF
#define AR_PHY_TPC_11_B0_OLPC_GAIN_DELTA_0_S		16

#define AR_PHY_TPC_11_B1							AR_SM1_OFFSET(BB_tpc_11_b1)
#define AR_PHY_TPC_11_B1_OLPC_GAIN_DELTA_1			0xFF
#define AR_PHY_TPC_11_B1_OLPC_GAIN_DELTA_1_S		16

#define AR_PHY_TPC_11_B2							AR_SM2_OFFSET(BB_tpc_11_b2)
#define AR_PHY_TPC_11_B2_OLPC_GAIN_DELTA_2			0xFF
#define AR_PHY_TPC_11_B2_OLPC_GAIN_DELTA_2_S		16


#define AR_PHY_TX_FORCED_GAIN_FORCED_TXBB1DBGAIN	0x7
#define AR_PHY_TX_FORCED_GAIN_FORCED_TXBB1DBGAIN_S	1
#define AR_PHY_TX_FORCED_GAIN_FORCED_TXBB6DBGAIN    0x3
#define AR_PHY_TX_FORCED_GAIN_FORCED_TXBB6DBGAIN_S	4 
#define AR_PHY_TX_FORCED_GAIN_FORCED_TXMXRGAIN		0xf
#define AR_PHY_TX_FORCED_GAIN_FORCED_TXMXRGAIN_S	6
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNA		0xf				
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNA_S		10
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNB		0xf		
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNB_S		14
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNC		0xf		
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGNC_S		18
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGND		0x3		
#define AR_PHY_TX_FORCED_GAIN_FORCED_PADRVGND_S		22
#define AR_PHY_TX_FORCED_GAIN_FORCED_ENABLE_PAL		1		
#define AR_PHY_TX_FORCED_GAIN_FORCED_ENABLE_PAL_S	24
#define AR_PHY_TX_FORCED_GAIN_FORCE_TX_GAIN			1		
#define AR_PHY_TX_FORCED_GAIN_FORCE_TX_GAIN_S		0

#define AR_PHY_TPC_1							AR_SM_OFFSET(BB_tpc_1)
#define AR_PHY_TPC_1_FORCED_DAC_GAIN				0x1f
#define AR_PHY_TPC_1_FORCED_DAC_GAIN_S				1
#define AR_PHY_TPC_1_FORCE_DAC_GAIN					1
#define AR_PHY_TPC_1_FORCE_DAC_GAIN_S				0

#define AR_PHY_CHAN_INFO_MEMORY_CHANINFOMEM_S2_READ		1
#define AR_PHY_CHAN_INFO_MEMORY_CHANINFOMEM_S2_READ_S	3

#define AR_PHY_PAPRD_MEM_TAB_B0		AR_CHAN_OFFSET(BB_paprd_mem_tab_b0)
#define AR_PHY_PAPRD_MEM_TAB_B1		AR_CHAN1_OFFSET(BB_paprd_mem_tab_b1)
#define AR_PHY_PAPRD_MEM_TAB_B2		AR_CHAN2_OFFSET(BB_paprd_mem_tab_b2)

#define AR_PHY_PA_GAIN123_B0		AR_CHAN_OFFSET(BB_pa_gain123_b0)
#define AR_PHY_PA_GAIN123_B0_PA_GAIN1_0			0x3FF
#define AR_PHY_PA_GAIN123_B0_PA_GAIN1_0_S		0

#define AR_PHY_PA_GAIN123_B1		AR_CHAN1_OFFSET(BB_pa_gain123_b1)
#define AR_PHY_PA_GAIN123_B1_PA_GAIN1_1			0x3FF
#define AR_PHY_PA_GAIN123_B1_PA_GAIN1_1_S		0

#define AR_PHY_PA_GAIN123_B2		AR_CHAN2_OFFSET(BB_pa_gain123_b2)
#define AR_PHY_PA_GAIN123_B2_PA_GAIN1_2			0x3FF
#define AR_PHY_PA_GAIN123_B2_PA_GAIN1_2_S		0

//Legacy 54M
#define AR_PHY_POWERTX_RATE2		AR_SM_OFFSET(BB_powertx_rate2)
#define AR_PHY_POWERTX_RATE2_POWERTX54M_7		0x3F
#define AR_PHY_POWERTX_RATE2_POWERTX54M_7_S	    24

#define AR_PHY_POWERTX_RATE5		AR_SM_OFFSET(BB_powertx_rate5)
#define AR_PHY_POWERTX_RATE5_POWERTXHT20_0		0x3F
#define AR_PHY_POWERTX_RATE5_POWERTXHT20_0_S	0
//HT20 MCS5
#define AR_PHY_POWERTX_RATE5_POWERTXHT20_3		0x3F
#define AR_PHY_POWERTX_RATE5_POWERTXHT20_3_S	24

//HT20 MCS7
#define AR_PHY_POWERTX_RATE6		AR_SM_OFFSET(BB_powertx_rate6)
#define AR_PHY_POWERTX_RATE6_POWERTXHT20_5		0x3F
#define AR_PHY_POWERTX_RATE6_POWERTXHT20_5_S	8
//HT20 MCS6
#define AR_PHY_POWERTX_RATE6_POWERTXHT20_4		0x3F
#define AR_PHY_POWERTX_RATE6_POWERTXHT20_4_S	0

#define AR_PHY_POWERTX_RATE7		AR_SM_OFFSET(BB_powertx_rate7)
//HT40 MCS5
#define AR_PHY_POWERTX_RATE7_POWERTXHT40_3		0x3F
#define AR_PHY_POWERTX_RATE7_POWERTXHT40_3_S	24

//HT40 MCS7
#define AR_PHY_POWERTX_RATE8		AR_SM_OFFSET(BB_powertx_rate8)
#define AR_PHY_POWERTX_RATE8_POWERTXHT40_5		0x3F
#define AR_PHY_POWERTX_RATE8_POWERTXHT40_5_S	8
//HT40 MCS6
#define AR_PHY_POWERTX_RATE8_POWERTXHT40_4		0x3F
#define AR_PHY_POWERTX_RATE8_POWERTXHT40_4_S	0

//HT20 MCS15
#define AR_PHY_POWERTX_RATE10		AR_SM_OFFSET(BB_powertx_rate10)
#define AR_PHY_POWERTX_RATE10_POWERTXHT20_9		0x3F
#define AR_PHY_POWERTX_RATE10_POWERTXHT20_9_S	8

//HT20 MCS23
#define AR_PHY_POWERTX_RATE11		AR_SM_OFFSET(BB_powertx_rate11)
#define AR_PHY_POWERTX_RATE11_POWERTXHT20_13	0x3F
#define AR_PHY_POWERTX_RATE11_POWERTXHT20_13_S	8

#define AR_PHY_CL_TAB_0_CL_GAIN_MOD				0x1F
#define AR_PHY_CL_TAB_0_CL_GAIN_MOD_S			0

#define AR_PHY_CL_TAB_1_CL_GAIN_MOD				0x1F
#define AR_PHY_CL_TAB_1_CL_GAIN_MOD_S			0

#define AR_PHY_CL_TAB_2_CL_GAIN_MOD				0x1F
#define AR_PHY_CL_TAB_2_CL_GAIN_MOD_S			0

/*
 * Hornet/Poseidon Analog Registers
 */
#define AR_HORNET_CH0_TOP               AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_TOP)
#define AR_HORNET_CH0_TOP2              AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_TOP2)
#define AR_HORNET_CH0_TOP2_XPABIASLVL   0xf000
#define AR_HORNET_CH0_TOP2_XPABIASLVL_S 12

#define AR_SCORPION_CH0_TOP              AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_TOP)
#define AR_SCORPION_CH0_TOP_XPABIASLVL   0x3c0
#define AR_SCORPION_CH0_TOP_XPABIASLVL_S 6

#define AR_SCORPION_CH0_XTAL            AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_XTAL)

#define AR_HORNET_CH0_THERM             AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_THERM)

#define AR_HORNET_CH0_XTAL              AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_XTAL)
#define AR_HORNET_CHO_XTAL_CAPINDAC     0x7F000000
#define AR_HORNET_CHO_XTAL_CAPINDAC_S   24
#define AR_HORNET_CHO_XTAL_CAPOUTDAC    0x00FE0000
#define AR_HORNET_CHO_XTAL_CAPOUTDAC_S  17

#define AR_HORNET_CH0_DDR_DPLL2         AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_DDR_DPLL2)
#define AR_HORNET_CH0_DDR_DPLL3         AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_DDR_DPLL3)
#define AR_PHY_CCA_NOM_VAL_HORNET_2GHZ      -118

#define AR_PHY_BB_DPLL1                 AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_BB_DPLL1)
#define AR_PHY_BB_DPLL1_REFDIV          0xF8000000
#define AR_PHY_BB_DPLL1_REFDIV_S        27
#define AR_PHY_BB_DPLL1_NINI            0x07FC0000
#define AR_PHY_BB_DPLL1_NINI_S          18
#define AR_PHY_BB_DPLL1_NFRAC           0x0003FFFF
#define AR_PHY_BB_DPLL1_NFRAC_S         0

#define AR_PHY_BB_DPLL2                 AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_BB_DPLL2)
#define AR_PHY_BB_DPLL2_RANGE           0x80000000
#define AR_PHY_BB_DPLL2_RANGE_S         31
#define AR_PHY_BB_DPLL2_LOCAL_PLL       0x40000000
#define AR_PHY_BB_DPLL2_LOCAL_PLL_S     30
#define AR_PHY_BB_DPLL2_KI              0x3C000000
#define AR_PHY_BB_DPLL2_KI_S            26
#define AR_PHY_BB_DPLL2_KD              0x03F80000
#define AR_PHY_BB_DPLL2_KD_S            19
#define AR_PHY_BB_DPLL2_EN_NEGTRIG      0x00040000
#define AR_PHY_BB_DPLL2_EN_NEGTRIG_S    18
#define AR_PHY_BB_DPLL2_SEL_1SDM        0x00020000
#define AR_PHY_BB_DPLL2_SEL_1SDM_S      17
#define AR_PHY_BB_DPLL2_PLL_PWD         0x00010000
#define AR_PHY_BB_DPLL2_PLL_PWD_S       16
#define AR_PHY_BB_DPLL2_OUTDIV          0x0000E000
#define AR_PHY_BB_DPLL2_OUTDIV_S        13
#define AR_PHY_BB_DPLL2_DELTA           0x00001F80
#define AR_PHY_BB_DPLL2_DELTA_S         7
#define AR_PHY_BB_DPLL2_SPARE           0x0000007F
#define AR_PHY_BB_DPLL2_SPARE_S         0

#define AR_PHY_BB_DPLL3                 AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_BB_DPLL3)
#define AR_PHY_BB_DPLL3_MEAS_AT_TXON    0x80000000
#define AR_PHY_BB_DPLL3_MEAS_AT_TXON_S  31
#define AR_PHY_BB_DPLL3_DO_MEAS         0x40000000
#define AR_PHY_BB_DPLL3_DO_MEAS_S       30
#define AR_PHY_BB_DPLL3_PHASE_SHIFT     0x3F800000
#define AR_PHY_BB_DPLL3_PHASE_SHIFT_S   23
#define AR_PHY_BB_DPLL3_SQSUM_DVC       0x007FFFF8
#define AR_PHY_BB_DPLL3_SQSUM_DVC_S     3
#define AR_PHY_BB_DPLL3_SPARE           0x00000007
#define AR_PHY_BB_DPLL3_SPARE_S         0x0

#define AR_PHY_BB_DPLL4                 AR_PHY_65NM(overlay_0x16180.Poseidon.ch0_BB_DPLL4)
#define AR_PHY_BB_DPLL4_MEAN_DVC        0xFFE00000
#define AR_PHY_BB_DPLL4_MEAN_DVC_S      21
#define AR_PHY_BB_DPLL4_VC_MEAS0        0x001FFFF0
#define AR_PHY_BB_DPLL4_VC_MEAS0_S      4
#define AR_PHY_BB_DPLL4_MEAS_DONE       0x00000008
#define AR_PHY_BB_DPLL4_MEAS_DONE_S     3
#define AR_PHY_BB_DPLL4_SPARE           0x00000007
#define AR_PHY_BB_DPLL4_SPARE_S         0

/*
 * Wasp Analog Registers
 */
#define AR_PHY_PLL_CONTROL              AR_PHY_65NM(overlay_0x16180.Osprey.ch0_pll_cntl)
#define AR_PHY_PLL_MODE                 AR_PHY_65NM(overlay_0x16180.Osprey.ch0_pll_mode)
#define AR_PHY_PLL_BB_DPLL3             AR_PHY_65NM(overlay_0x16180.Osprey.ch0_bb_dpll3)
#define AR_PHY_PLL_BB_DPLL4             AR_PHY_65NM(overlay_0x16180.Osprey.ch0_bb_dpll4)

/*
 * Wasp/Hornet PHY USB PLL control
 */
#define AR_PHY_USB_CTRL1		0x16c84
#define AR_PHY_USB_CTRL2		0x16c88

/*
 * PMU Register Map
 */
#define AR_PHY_PMU(_field)         offsetof(struct pmu_reg, _field)
#define AR_PHY_PMU1                AR_PHY_PMU(ch0_PMU1)
#define AR_PHY_PMU2                AR_PHY_PMU(ch0_PMU2)
#define AR_PHY_JUPITER_PMU(_field) offsetof(struct radio65_reg, _field)
#define AR_PHY_PMU1_JUPITER        AR_PHY_JUPITER_PMU(overlay_0x16180.Jupiter.ch0_PMU1)
#define AR_PHY_PMU2_JUPITER        AR_PHY_JUPITER_PMU(overlay_0x16180.Jupiter.ch0_PMU2)

/*
 * GLB Register Map
 */
#define AR_PHY_GLB(_field) offsetof(struct glb_reg, _field)
#define AR_PHY_GLB_CONTROL_JUPITER                AR_PHY_GLB(overlap_0x20044.Jupiter.GLB_CONTROL)

/*
 * PMU Field Definitions
 */
/* ch0_PMU1 */
#define AR_PHY_PMU1_PWD            0x00000001 /* power down switch regulator */
#define AR_PHY_PMU1_PWD_S          0

/* ch0_PMU2 */
#define AR_PHY_PMU2_PGM            0x00200000
#define AR_PHY_PMU2_PGM_S          21

/* ch0_PHY_CTRL2 */
#define AR_PHY_CTRL2_TX_MAN_CAL         0x03C00000
#define AR_PHY_CTRL2_TX_MAN_CAL_S       22
#define AR_PHY_CTRL2_TX_CAL_SEL         0x00200000
#define AR_PHY_CTRL2_TX_CAL_SEL_S       21
#define AR_PHY_CTRL2_TX_CAL_EN          0x00100000
#define AR_PHY_CTRL2_TX_CAL_EN_S        20

#define PCIE_CO_ERR_CTR_CTRL                 0x40e8
#define PCIE_CO_ERR_CTR_CTR0                 0x40e0
#define PCIE_CO_ERR_CTR_CTR1                 0x40e4


#define RCVD_ERR_CTR_RUN                     0x0001
#define RCVD_ERR_CTR_AUTO_STOP               0x0002
#define BAD_TLP_ERR_CTR_RUN                  0x0004
#define BAD_TLP_ERR_CTR_AUTO_STOP            0x0008
#define BAD_DLLP_ERR_CTR_RUN                 0x0010
#define BAD_DLLP_ERR_CTR_AUTO_STOP           0x0020
#define RPLY_TO_ERR_CTR_RUN                  0x0040
#define RPLY_TO_ERR_CTR_AUTO_STOP            0x0080
#define RPLY_NUM_RO_ERR_CTR_RUN              0x0100
#define RPLY_NUM_RO_ERR_CTR_AUTO_STOP        0x0200

#define RCVD_ERR_MASK                        0x000000ff
#define RCVD_ERR_MASK_S                      0
#define BAD_TLP_ERR_MASK                     0x0000ff00
#define BAD_TLP_ERR_MASK_S                   8
#define BAD_DLLP_ERR_MASK                    0x00ff0000
#define BAD_DLLP_ERR_MASK_S                  16

#define RPLY_TO_ERR_MASK                     0x000000ff
#define RPLY_TO_ERR_MASK_S                   0
#define RPLY_NUM_RO_ERR_MASK                 0x0000ff00
#define RPLY_NUM_RO_ERR_MASK_S               8

#define AR_MERLIN_RADIO_SYNTH4    offsetof(struct merlin2_0_radio_reg_map, SYNTH4)
#define AR_MERLIN_RADIO_SYNTH6    offsetof(struct merlin2_0_radio_reg_map, SYNTH6)
#define AR_MERLIN_RADIO_SYNTH7    offsetof(struct merlin2_0_radio_reg_map, SYNTH7)
#define AR_MERLIN_RADIO_TOP0      offsetof(struct merlin2_0_radio_reg_map, TOP0)
#define AR_MERLIN_RADIO_TOP1      offsetof(struct merlin2_0_radio_reg_map, TOP1)
#define AR_MERLIN_RADIO_TOP2      offsetof(struct merlin2_0_radio_reg_map, TOP2)
#define AR_MERLIN_RADIO_TOP3      offsetof(struct merlin2_0_radio_reg_map, TOP3)
#endif  /* _ATH_AR9300PHY_H_ */
