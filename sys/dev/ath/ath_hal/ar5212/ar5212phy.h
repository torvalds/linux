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
#ifndef _DEV_ATH_AR5212PHY_H_
#define _DEV_ATH_AR5212PHY_H_

/* PHY registers */
#define	AR_PHY_BASE		0x9800		/* base address of phy regs */
#define	AR_PHY(_n)		(AR_PHY_BASE + ((_n)<<2))

#define AR_PHY_TEST             0x9800          /* PHY test control */
#define PHY_AGC_CLR             0x10000000      /* disable AGC to A2 */

#define	AR_PHY_TESTCTRL		0x9808		/* PHY Test Control/Status */
#define	AR_PHY_TESTCTRL_TXHOLD	0x3800		/* Select Tx hold */
#define AR_PHY_TESTCTRL_TXSRC_ALT	0x00000080	/* Select input to tsdac along with bit 1 */
#define AR_PHY_TESTCTRL_TXSRC_ALT_S	7
#define AR_PHY_TESTCTRL_TXSRC_SRC	0x00000002	/* Used with bit 7 */
#define AR_PHY_TESTCTRL_TXSRC_SRC_S	1

#define	AR_PHY_TURBO		0x9804		/* frame control register */
#define	AR_PHY_FC_TURBO_MODE	0x00000001	/* Set turbo mode bits */
#define	AR_PHY_FC_TURBO_SHORT	0x00000002	/* Set short symbols to turbo mode setting */
#define AR_PHY_FC_TURBO_MIMO    0x00000004      /* Set turbo for mimo mode */

#define	AR_PHY_TIMING3		0x9814		/* Timing control 3 */
#define	AR_PHY_TIMING3_DSC_MAN	0xFFFE0000
#define	AR_PHY_TIMING3_DSC_MAN_S	17
#define	AR_PHY_TIMING3_DSC_EXP	0x0001E000
#define	AR_PHY_TIMING3_DSC_EXP_S	13

#define	AR_PHY_CHIP_ID		0x9818		/* PHY chip revision ID */
#define	AR_PHY_CHIP_ID_REV_2	0x42		/* 5212 Rev 2 BB w. TPC fix */
#define	AR_PHY_CHIP_ID_REV_3	0x43		/* 5212 Rev 3 5213 */
#define	AR_PHY_CHIP_ID_REV_4	0x44		/* 5212 Rev 4 2313 and up */

#define	AR_PHY_ACTIVE		0x981C		/* activation register */
#define	AR_PHY_ACTIVE_EN	0x00000001	/* Activate PHY chips */
#define	AR_PHY_ACTIVE_DIS	0x00000000	/* Deactivate PHY chips */

#define AR_PHY_TX_CTL		0x9824
#define AR_PHY_TX_FRAME_TO_TX_DATA_START	0x0000000f
#define AR_PHY_TX_FRAME_TO_TX_DATA_START_S	0

#define	AR_PHY_ADC_CTL		0x982C
#define	AR_PHY_ADC_CTL_OFF_INBUFGAIN	0x00000003
#define	AR_PHY_ADC_CTL_OFF_INBUFGAIN_S	0
#define	AR_PHY_ADC_CTL_OFF_PWDDAC	0x00002000
#define	AR_PHY_ADC_CTL_OFF_PWDBANDGAP	0x00004000 /* BB Rev 4.2+ only */
#define	AR_PHY_ADC_CTL_OFF_PWDADC	0x00008000 /* BB Rev 4.2+ only */
#define	AR_PHY_ADC_CTL_ON_INBUFGAIN	0x00030000
#define	AR_PHY_ADC_CTL_ON_INBUFGAIN_S	16

#define	AR_PHY_BB_XP_PA_CTL	0x9838
#define AR_PHY_BB_XPAA_ACTIVE_HIGH	0x00000001
#define	AR_PHY_BB_XPAB_ACTIVE_HIGH	0x00000002
#define	AR_PHY_BB_XPAB_ACTIVE_HIGH_S	1

#define AR_PHY_TSTDAC_CONST	0x983C
#define AR_PHY_TSTDAC_CONST_Q	0x0003FE00
#define AR_PHY_TSTDAC_CONST_Q_S	9
#define AR_PHY_TSTDAC_CONST_I	0x000001FF


#define	AR_PHY_SETTLING		0x9844
#define AR_PHY_SETTLING_AGC 0x0000007F
#define AR_PHY_SETTLING_AGC_S   0
#define	AR_PHY_SETTLING_SWITCH	0x00003F80
#define	AR_PHY_SETTLING_SWITCH_S	7

#define	AR_PHY_RXGAIN		0x9848
#define	AR_PHY_RXGAIN_TXRX_ATTEN	0x0003F000
#define	AR_PHY_RXGAIN_TXRX_ATTEN_S	12
#define	AR_PHY_RXGAIN_TXRX_RF_MAX	0x007C0000
#define	AR_PHY_RXGAIN_TXRX_RF_MAX_S	18

#define	AR_PHY_DESIRED_SZ	0x9850
#define	AR_PHY_DESIRED_SZ_ADC		0x000000FF
#define	AR_PHY_DESIRED_SZ_ADC_S		0
#define	AR_PHY_DESIRED_SZ_PGA		0x0000FF00
#define	AR_PHY_DESIRED_SZ_PGA_S		8
#define	AR_PHY_DESIRED_SZ_TOT_DES	0x0FF00000
#define	AR_PHY_DESIRED_SZ_TOT_DES_S	20

#define	AR_PHY_FIND_SIG		 0x9858
#define	AR_PHY_FIND_SIG_FIRSTEP	 0x0003F000
#define	AR_PHY_FIND_SIG_FIRSTEP_S		 12
#define	AR_PHY_FIND_SIG_FIRPWR	 0x03FC0000
#define	AR_PHY_FIND_SIG_FIRPWR_S		 18

#define	AR_PHY_AGC_CTL1		 0x985C
#define	AR_PHY_AGC_CTL1_COARSE_LOW		 0x00007F80
#define	AR_PHY_AGC_CTL1_COARSE_LOW_S		 7
#define	AR_PHY_AGC_CTL1_COARSE_HIGH		 0x003F8000
#define	AR_PHY_AGC_CTL1_COARSE_HIGH_S		 15

#define	AR_PHY_AGC_CONTROL	0x9860		/* chip calibration and noise floor setting */
#define	AR_PHY_AGC_CONTROL_CAL	0x00000001	/* do internal calibration */
#define	AR_PHY_AGC_CONTROL_NF	0x00000002	/* do noise-floor calculation */
#define AR_PHY_AGC_CONTROL_ENABLE_NF     0x00008000 /* Enable noise floor calibration to happen */
#define	AR_PHY_AGC_CONTROL_FLTR_CAL	0x00010000  /* Allow Filter calibration */
#define AR_PHY_AGC_CONTROL_NO_UPDATE_NF  0x00020000 /* Don't update noise floor automatically */

#define	AR_PHY_SFCORR_LOW	 0x986C
#define	AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW	 0x00000001
#define	AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW	 0x00003F00
#define	AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW_S	 8
#define	AR_PHY_SFCORR_LOW_M1_THRESH_LOW	 0x001FC000
#define	AR_PHY_SFCORR_LOW_M1_THRESH_LOW_S	 14
#define	AR_PHY_SFCORR_LOW_M2_THRESH_LOW	 0x0FE00000
#define	AR_PHY_SFCORR_LOW_M2_THRESH_LOW_S	 21

#define	AR_PHY_SFCORR	 	0x9868
#define	AR_PHY_SFCORR_M2COUNT_THR	 0x0000001F
#define	AR_PHY_SFCORR_M2COUNT_THR_S	 0
#define	AR_PHY_SFCORR_M1_THRESH	 0x00FE0000
#define	AR_PHY_SFCORR_M1_THRESH_S	 17
#define	AR_PHY_SFCORR_M2_THRESH	 0x7F000000
#define	AR_PHY_SFCORR_M2_THRESH_S	 24

#define	AR_PHY_SLEEP_CTR_CONTROL	0x9870
#define	AR_PHY_SLEEP_CTR_LIMIT		0x9874
#define	AR_PHY_SLEEP_SCAL		0x9878

#define	AR_PHY_PLL_CTL		0x987c	/* PLL control register */
#define	AR_PHY_PLL_CTL_40	0xaa	/* 40 MHz */
#define	AR_PHY_PLL_CTL_44	0xab	/* 44 MHz for 11b, 11g */
#define	AR_PHY_PLL_CTL_44_5112	0xeb	/* 44 MHz for 11b, 11g */
#define	AR_PHY_PLL_CTL_40_5112	0xea	/* 40 MHz for 11a, turbos */
#define	AR_PHY_PLL_CTL_40_5413  0x04	/* 40 MHz for 11a, turbos with 5413 */
#define	AR_PHY_PLL_CTL_HALF	0x100	/* Half clock for 1/2 chan width */
#define	AR_PHY_PLL_CTL_QUARTER	0x200	/* Quarter clock for 1/4 chan width */

#define	AR_PHY_BIN_MASK_1	0x9900
#define	AR_PHY_BIN_MASK_2	0x9904
#define	AR_PHY_BIN_MASK_3	0x9908

#define	AR_PHY_MASK_CTL		0x990c		/* What are these for?? */
#define	AR_PHY_MASK_CTL_MASK_4	0x00003FFF
#define	AR_PHY_MASK_CTL_MASK_4_S	0
#define	AR_PHY_MASK_CTL_RATE	0xFF000000
#define	AR_PHY_MASK_CTL_RATE_S	24

#define	AR_PHY_RX_DELAY		0x9914		/* analog pow-on time (100ns) */
#define	AR_PHY_RX_DELAY_DELAY	0x00003FFF	/* delay from wakeup to rx ena */

#define	AR_PHY_TIMING_CTRL4		0x9920		/* timing control */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF	0x01F	/* Mask for kcos_theta-1 for q correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF_S	0	/* shift for Q_COFF */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF	0x7E0	/* Mask for sin_theta for i correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_S	5	/* Shift for sin_theta for i correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_ENABLE	0x800	/* enable IQ correction */
#define	AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX	0xF000	/* Mask for max number of samples (logarithmic) */
#define	AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX_S	12	/* Shift for max number of samples */
#define	AR_PHY_TIMING_CTRL4_DO_IQCAL	0x10000		/* perform IQ calibration */
#define	AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER	0x40000000	/* Enable spur filter */
#define	AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK	0x20000000
#define	AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK	0x10000000

#define	AR_PHY_TIMING5		0x9924
#define	AR_PHY_TIMING5_CYCPWR_THR1	0x000000FE
#define	AR_PHY_TIMING5_CYCPWR_THR1_S	1

#define	AR_PHY_PAPD_PROBE	0x9930
#define	AR_PHY_PAPD_PROBE_POWERTX	0x00007E00
#define	AR_PHY_PAPD_PROBE_POWERTX_S	9
#define	AR_PHY_PAPD_PROBE_NEXT_TX	0x00008000	/* command to take next reading */
#define	AR_PHY_PAPD_PROBE_TYPE	0x01800000
#define	AR_PHY_PAPD_PROBE_TYPE_S	23
#define	AR_PHY_PAPD_PROBE_TYPE_OFDM	0
#define	AR_PHY_PAPD_PROBE_TYPE_CCK	2
#define	AR_PHY_PAPD_PROBE_GAINF	0xFE000000
#define	AR_PHY_PAPD_PROBE_GAINF_S	25

#define	AR_PHY_POWER_TX_RATE1	0x9934
#define	AR_PHY_POWER_TX_RATE2	0x9938
#define	AR_PHY_POWER_TX_RATE_MAX	0x993c
#define	AR_PHY_POWER_TX_RATE_MAX_TPC_ENABLE	0x00000040

#define	AR_PHY_FRAME_CTL	0x9944
#define	AR_PHY_FRAME_CTL_TX_CLIP	0x00000038
#define	AR_PHY_FRAME_CTL_TX_CLIP_S	3
#define AR_PHY_FRAME_CTL_ERR_SERV	0x20000000
#define AR_PHY_FRAME_CTL_ERR_SERV_S	29
#define AR_PHY_FRAME_CTL_EMU_M		0x80000000
#define AR_PHY_FRAME_CTL_EMU_S		31
#define AR_PHY_FRAME_CTL_WINLEN		0x00000003
#define AR_PHY_FRAME_CTL_WINLEN_S	0

#define	AR_PHY_TXPWRADJ		0x994C		/* BB Rev 4.2+ only */
#define	AR_PHY_TXPWRADJ_CCK_GAIN_DELTA	0x00000FC0
#define	AR_PHY_TXPWRADJ_CCK_GAIN_DELTA_S	6
#define	AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX	0x00FC0000
#define	AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX_S	18

#define	AR_PHY_RADAR_0		0x9954		/* radar detection settings */
#define	AR_PHY_RADAR_0_ENA	0x00000001	/* Enable radar detection */
#define AR_PHY_RADAR_0_INBAND	0x0000003e	/* Inband pulse threshold */
#define AR_PHY_RADAR_0_INBAND_S	1
#define AR_PHY_RADAR_0_PRSSI	0x00000FC0	/* Pulse rssi threshold */
#define AR_PHY_RADAR_0_PRSSI_S	6
#define AR_PHY_RADAR_0_HEIGHT	0x0003F000	/* Pulse height threshold */
#define AR_PHY_RADAR_0_HEIGHT_S	12
#define AR_PHY_RADAR_0_RRSSI	0x00FC0000	/* Radar rssi threshold */
#define AR_PHY_RADAR_0_RRSSI_S	18
#define AR_PHY_RADAR_0_FIRPWR	0x7F000000	/* Radar firpwr threshold */
#define AR_PHY_RADAR_0_FIRPWR_S	24

/* ar5413 specific */
#define	AR_PHY_RADAR_2		0x9958		/* radar detection settings */
#define	AR_PHY_RADAR_2_ENRELSTEPCHK 0x00002000	/* Enable using max rssi */
#define	AR_PHY_RADAR_2_ENMAXRSSI    0x00004000	/* Enable using max rssi */
#define	AR_PHY_RADAR_2_BLOCKOFDMWEAK 0x00008000	/* En block OFDM weak sig as radar */
#define	AR_PHY_RADAR_2_USEFIR128    0x00400000	/* En measuring pwr over 128 cycles */
#define	AR_PHY_RADAR_2_ENRELPWRCHK  0x00800000	/* Enable using max rssi */
#define	AR_PHY_RADAR_2_MAXLEN	0x000000FF	/* Max Pulse duration threshold */
#define	AR_PHY_RADAR_2_MAXLEN_S	0
#define	AR_PHY_RADAR_2_RELSTEP	0x00001F00	/* Pulse relative step threshold */
#define	AR_PHY_RADAR_2_RELSTEP_S	8
#define	AR_PHY_RADAR_2_RELPWR	0x003F0000	/* pulse relative power threshold */
#define	AR_PHY_RADAR_2_RELPWR_S	16

#define	AR_PHY_SIGMA_DELTA	0x996C      /* AR5312 only */
#define	AR_PHY_SIGMA_DELTA_ADC_SEL	0x00000003
#define	AR_PHY_SIGMA_DELTA_ADC_SEL_S	0
#define	AR_PHY_SIGMA_DELTA_FILT2	0x000000F8
#define	AR_PHY_SIGMA_DELTA_FILT2_S	3
#define	AR_PHY_SIGMA_DELTA_FILT1	0x00001F00
#define	AR_PHY_SIGMA_DELTA_FILT1_S	8
#define	AR_PHY_SIGMA_DELTA_ADC_CLIP	0x01FFE000
#define	AR_PHY_SIGMA_DELTA_ADC_CLIP_S	13

#define	AR_PHY_RESTART		0x9970		/* restart */
#define	AR_PHY_RESTART_DIV_GC	0x001C0000	/* bb_ant_fast_div_gc_limit */
#define	AR_PHY_RESTART_DIV_GC_S	18

#define AR_PHY_RFBUS_REQ    0x997C
#define AR_PHY_RFBUS_REQ_REQUEST    0x00000001

#define	AR_PHY_TIMING7		0x9980		/* Spur mitigation masks */
#define	AR_PHY_TIMING8		0x9984
#define	AR_PHY_TIMING8_PILOT_MASK_2	0x000FFFFF
#define	AR_PHY_TIMING8_PILOT_MASK_2_S	0

#define	AR_PHY_BIN_MASK2_1	0x9988
#define	AR_PHY_BIN_MASK2_2	0x998c
#define	AR_PHY_BIN_MASK2_3	0x9990
#define	AR_PHY_BIN_MASK2_4	0x9994
#define	AR_PHY_BIN_MASK2_4_MASK_4	0x00003FFF
#define	AR_PHY_BIN_MASK2_4_MASK_4_S	0

#define	AR_PHY_TIMING9		0x9998
#define	AR_PHY_TIMING10		0x999c
#define	AR_PHY_TIMING10_PILOT_MASK_2	0x000FFFFF
#define	AR_PHY_TIMING10_PILOT_MASK_2_S	0

#define	AR_PHY_TIMING11			0x99a0		/* Spur Mitigation control */
#define	AR_PHY_TIMING11_SPUR_DELTA_PHASE	0x000FFFFF
#define	AR_PHY_TIMING11_SPUR_DELTA_PHASE_S	0
#define	AR_PHY_TIMING11_SPUR_FREQ_SD		0x3FF00000
#define	AR_PHY_TIMING11_SPUR_FREQ_SD_S		20
#define AR_PHY_TIMING11_USE_SPUR_IN_AGC		0x40000000
#define AR_PHY_TIMING11_USE_SPUR_IN_SELFCOR	0x80000000

#define	AR_PHY_HEAVY_CLIP_ENABLE	0x99E0

#define	AR_PHY_M_SLEEP		0x99f0		/* sleep control registers */
#define	AR_PHY_REFCLKDLY	0x99f4
#define	AR_PHY_REFCLKPD		0x99f8

/* PHY IQ calibration results */
#define	AR_PHY_IQCAL_RES_PWR_MEAS_I	0x9c10	/* power measurement for I */
#define	AR_PHY_IQCAL_RES_PWR_MEAS_Q	0x9c14	/* power measurement for Q */
#define	AR_PHY_IQCAL_RES_IQ_CORR_MEAS	0x9c18	/* IQ correlation measurement */

#define	AR_PHY_CURRENT_RSSI	0x9c1c		/* rssi of current frame rx'd */

#define AR_PHY_RFBUS_GNT    0x9c20
#define AR_PHY_RFBUS_GNT_GRANT  0x1
                                                                                          
#define	AR_PHY_PCDAC_TX_POWER_0	0xA180
#define	AR_PHY_PCDAC_TX_POWER(_n)	(AR_PHY_PCDAC_TX_POWER_0 + ((_n)<<2))

#define	AR_PHY_MODE		0xA200	/* Mode register */
#define AR_PHY_MODE_QUARTER	0x40	/* Quarter Rate */
#define AR_PHY_MODE_HALF	0x20	/* Half Rate */
#define	AR_PHY_MODE_AR5112	0x08	/* AR5112 */
#define	AR_PHY_MODE_AR5111	0x00	/* AR5111/AR2111 */
#define	AR_PHY_MODE_DYNAMIC	0x04	/* dynamic CCK/OFDM mode */
#define	AR_PHY_MODE_RF2GHZ	0x02	/* 2.4 GHz */
#define	AR_PHY_MODE_RF5GHZ	0x00	/* 5 GHz */
#define	AR_PHY_MODE_CCK		0x01	/* CCK */
#define	AR_PHY_MODE_OFDM	0x00	/* OFDM */
#define	AR_PHY_MODE_DYN_CCK_DISABLE 0x100 /* Disable dynamic CCK detection */

#define	AR_PHY_CCK_TX_CTRL	0xA204
#define	AR_PHY_CCK_TX_CTRL_JAPAN	0x00000010

#define	AR_PHY_CCK_DETECT	0xA208
#define	AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK	0x0000003F
#define	AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK_S	0
#define	AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV	0x2000

#define	AR_PHY_GAIN_2GHZ	0xA20C
#define	AR_PHY_GAIN_2GHZ_RXTX_MARGIN	0x00FC0000
#define	AR_PHY_GAIN_2GHZ_RXTX_MARGIN_S	18

#define	AR_PHY_CCK_RXCTRL4	0xA21C
#define	AR_PHY_CCK_RXCTRL4_FREQ_EST_SHORT	0x01F80000
#define	AR_PHY_CCK_RXCTRL4_FREQ_EST_SHORT_S	19

#define	AR_PHY_DAG_CTRLCCK	0xA228
#define	AR_PHY_DAG_CTRLCCK_EN_RSSI_THR	0x00000200 /* BB Rev 4.2+ only */
#define	AR_PHY_DAG_CTRLCCK_RSSI_THR	0x0001FC00 /* BB Rev 4.2+ only */
#define	AR_PHY_DAG_CTRLCCK_RSSI_THR_S	10	   /* BB Rev 4.2+ only */

#define	AR_PHY_POWER_TX_RATE3	0xA234
#define	AR_PHY_POWER_TX_RATE4	0xA238

#define	AR_PHY_FAST_ADC		0xA24C
#define	AR_PHY_BLUETOOTH	0xA254

#define	AR_PHY_TPCRG1	0xA258  /* ar2413 power control */
#define	AR_PHY_TPCRG1_NUM_PD_GAIN	0x0000c000
#define	AR_PHY_TPCRG1_NUM_PD_GAIN_S	14
#define	AR_PHY_TPCRG1_PDGAIN_SETTING1	0x00030000
#define	AR_PHY_TPCRG1_PDGAIN_SETTING1_S	16
#define	AR_PHY_TPCRG1_PDGAIN_SETTING2	0x000c0000
#define	AR_PHY_TPCRG1_PDGAIN_SETTING2_S	18
#define	AR_PHY_TPCRG1_PDGAIN_SETTING3	0x00300000
#define	AR_PHY_TPCRG1_PDGAIN_SETTING3_S	20

#define	AR_PHY_TPCRG5	0xA26C /* ar2413 power control */
#define	AR_PHY_TPCRG5_PD_GAIN_OVERLAP	0x0000000F
#define	AR_PHY_TPCRG5_PD_GAIN_OVERLAP_S		0
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1	0x000003F0
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1_S	4
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2	0x0000FC00
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2_S	10
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3	0x003F0000
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3_S	16
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4	0x0FC00000
#define	AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4_S	22

#endif	/* _DEV_ATH_AR5212PHY_H_ */
