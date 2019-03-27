/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2006 Atheros Communications, Inc.
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
#ifndef _DEV_ATH_AR5211PHY_H
#define _DEV_ATH_AR5211PHY_H

/*
 * Definitions for the PHY on the Atheros AR5211/5311 chipset.
 */

/* PHY registers */
#define	AR_PHY_BASE	0x9800	/* PHY registers base address */
#define	AR_PHY(_n)	(AR_PHY_BASE + ((_n)<<2))

#define	AR_PHY_TURBO	0x9804	/* PHY frame control register */
#define	AR_PHY_FC_TURBO_MODE	0x00000001	/* Set turbo mode bits */
#define	AR_PHY_FC_TURBO_SHORT	0x00000002	/* Set short symbols to turbo mode setting */

#define	AR_PHY_CHIP_ID	0x9818	/* PHY chip revision ID */

#define	AR_PHY_ACTIVE	0x981C	/* PHY activation register */
#define	AR_PHY_ACTIVE_EN	0x00000001	/* Activate PHY chips */
#define	AR_PHY_ACTIVE_DIS	0x00000000	/* Deactivate PHY chips */

#define	AR_PHY_AGC_CONTROL	0x9860	/* PHY chip calibration and noise floor setting */
#define	AR_PHY_AGC_CONTROL_CAL	0x00000001	/* Perform PHY chip internal calibration */
#define	AR_PHY_AGC_CONTROL_NF	0x00000002	/* Perform PHY chip noise-floor calculation */

#define	AR_PHY_PLL_CTL	0x987c	/* PLL control register */
#define	AR_PHY_PLL_CTL_44	0x19	/* 44 MHz for 11b channels and FPGA */
#define	AR_PHY_PLL_CTL_40	0x18	/* 40 MHz */
#define	AR_PHY_PLL_CTL_20	0x13	/* 20 MHz half rate 11a for emulation */


#define	AR_PHY_RX_DELAY	0x9914	/* PHY analog_power_on_time, in 100ns increments */
#define	AR_PHY_RX_DELAY_M	0x00003FFF	/* Mask for delay from active assertion (wake up) */
				/* to enable_receiver */

#define	AR_PHY_TIMING_CTRL4	0x9920	/* PHY */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF_M	0x0000001F	/* Mask for kcos_theta-1 for q correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_M	0x000007E0	/* Mask for sin_theta for i correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_S	5         	/* Shift for sin_theta for i correction */
#define	AR_PHY_TIMING_CTRL4_IQCORR_ENABLE	0x00000800	/* enable IQ correction */
#define	AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX_M	0x0000F000	/* Mask for max number of samples (logarithmic) */
#define	AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX_S	12        	/* Shift for max number of samples */
#define	AR_PHY_TIMING_CTRL4_DO_IQCAL	0x00010000	/* perform IQ calibration */

#define	AR_PHY_PAPD_PROBE	0x9930
#define	AR_PHY_PAPD_PROBE_POWERTX	0x00007E00
#define	AR_PHY_PAPD_PROBE_POWERTX_S	9
#define	AR_PHY_PAPD_PROBE_NEXT_TX	0x00008000	/* command to take next reading */
#define	AR_PHY_PAPD_PROBE_GAINF	0xFE000000
#define	AR_PHY_PAPD_PROBE_GAINF_S	25

#define	AR_PHY_POWER_TX_RATE1		0x9934
#define	AR_PHY_POWER_TX_RATE2		0x9938
#define	AR_PHY_POWER_TX_RATE_MAX	0x993c

#define	AR_PHY_FRAME_CTL	0x9944
#define	AR_PHY_FRAME_CTL_TX_CLIP	0x00000038
#define	AR_PHY_FRAME_CTL_TX_CLIP_S	3
#define AR_PHY_FRAME_CTL_ERR_SERV	0x20000000
#define AR_PHY_FRAME_CTL_ERR_SERV_S	29

#define	AR_PHY_RADAR_0	0x9954	/* PHY radar detection settings */
#define	AR_PHY_RADAR_0_ENA	0x00000001	/* Enable radar detection */

#define	AR_PHY_IQCAL_RES_PWR_MEAS_I	0x9c10	/*PHY IQ calibration results - power measurement for I */
#define	AR_PHY_IQCAL_RES_PWR_MEAS_Q	0x9c14	/*PHY IQ calibration results - power measurement for Q */
#define	AR_PHY_IQCAL_RES_IQ_CORR_MEAS	0x9c18	/*PHY IQ calibration results - IQ correlation measurement */
#define	AR_PHY_CURRENT_RSSI	0x9c1c	/* rssi of current frame being received */

#define	AR5211_PHY_MODE	0xA200	/* Mode register */
#define	AR5211_PHY_MODE_OFDM	0x0	/* bit 0 = 0 for OFDM */
#define	AR5211_PHY_MODE_CCK	0x1	/* bit 0 = 1 for CCK */
#define	AR5211_PHY_MODE_RF5GHZ	0x0	/* bit 1 = 0 for 5 GHz */
#define	AR5211_PHY_MODE_RF2GHZ	0x2	/* bit 1 = 1 for 2.4 GHz */

#endif /* _DEV_ATH_AR5211PHY_H */
