/*
 * Copyright (C) 2017 BayLibre, SAS
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _AIU_REGS_H_
#define _AIU_REGS_H_

#define AIU_958_BPF			0x000
#define AIU_958_BRST			0x004
#define AIU_958_LENGTH			0x008
#define AIU_958_PADDSIZE		0x00C
#define AIU_958_MISC			0x010
#define AIU_958_FORCE_LEFT		0x014	/* Unknown */
#define AIU_958_DISCARD_NUM		0x018
#define AIU_958_DCU_FF_CTRL		0x01C
#define AIU_958_CHSTAT_L0		0x020
#define AIU_958_CHSTAT_L1		0x024
#define AIU_958_CTRL			0x028
#define AIU_958_RPT			0x02C
#define AIU_I2S_MUTE_SWAP		0x030
#define AIU_I2S_SOURCE_DESC		0x034
#define AIU_I2S_MED_CTRL		0x038
#define AIU_I2S_MED_THRESH		0x03C
#define AIU_I2S_DAC_CFG			0x040
#define AIU_I2S_SYNC			0x044	/* Unknown */
#define AIU_I2S_MISC			0x048
#define AIU_I2S_OUT_CFG			0x04C
#define AIU_I2S_FF_CTRL			0x050	/* Unknown */
#define AIU_RST_SOFT			0x054
#define AIU_CLK_CTRL			0x058
#define AIU_MIX_ADCCFG			0x05C
#define AIU_MIX_CTRL			0x060
#define AIU_CLK_CTRL_MORE		0x064
#define AIU_958_POP			0x068
#define AIU_MIX_GAIN			0x06C
#define AIU_958_SYNWORD1		0x070
#define AIU_958_SYNWORD2		0x074
#define AIU_958_SYNWORD3		0x078
#define AIU_958_SYNWORD1_MASK		0x07C
#define AIU_958_SYNWORD2_MASK		0x080
#define AIU_958_SYNWORD3_MASK		0x084
#define AIU_958_FFRDOUT_THD		0x088
#define AIU_958_LENGTH_PER_PAUSE	0x08C
#define AIU_958_PAUSE_NUM		0x090
#define AIU_958_PAUSE_PAYLOAD		0x094
#define AIU_958_AUTO_PAUSE		0x098
#define AIU_958_PAUSE_PD_LENGTH		0x09C
#define AIU_CODEC_DAC_LRCLK_CTRL	0x0A0
#define AIU_CODEC_ADC_LRCLK_CTRL	0x0A4
#define AIU_HDMI_CLK_DATA_CTRL		0x0A8
#define AIU_CODEC_CLK_DATA_CTRL		0x0AC
#define AIU_ACODEC_CTRL			0x0B0
#define AIU_958_CHSTAT_R0		0x0C0
#define AIU_958_CHSTAT_R1		0x0C4
#define AIU_958_VALID_CTRL		0x0C8
#define AIU_AUDIO_AMP_REG0		0x0F0	/* Unknown */
#define AIU_AUDIO_AMP_REG1		0x0F4	/* Unknown */
#define AIU_AUDIO_AMP_REG2		0x0F8	/* Unknown */
#define AIU_AUDIO_AMP_REG3		0x0FC	/* Unknown */
#define AIU_AIFIFO2_CTRL		0x100
#define AIU_AIFIFO2_STATUS		0x104
#define AIU_AIFIFO2_GBIT		0x108
#define AIU_AIFIFO2_CLB			0x10C
#define AIU_CRC_CTRL			0x110
#define AIU_CRC_STATUS			0x114
#define AIU_CRC_SHIFT_REG		0x118
#define AIU_CRC_IREG			0x11C
#define AIU_CRC_CAL_REG1		0x120
#define AIU_CRC_CAL_REG0		0x124
#define AIU_CRC_POLY_COEF1		0x128
#define AIU_CRC_POLY_COEF0		0x12C
#define AIU_CRC_BIT_SIZE1		0x130
#define AIU_CRC_BIT_SIZE0		0x134
#define AIU_CRC_BIT_CNT1		0x138
#define AIU_CRC_BIT_CNT0		0x13C
#define AIU_AMCLK_GATE_HI		0x140
#define AIU_AMCLK_GATE_LO		0x144
#define AIU_AMCLK_MSR			0x148
#define AIU_AUDAC_CTRL0			0x14C	/* Unknown */
#define AIU_DELTA_SIGMA0		0x154	/* Unknown */
#define AIU_DELTA_SIGMA1		0x158	/* Unknown */
#define AIU_DELTA_SIGMA2		0x15C	/* Unknown */
#define AIU_DELTA_SIGMA3		0x160	/* Unknown */
#define AIU_DELTA_SIGMA4		0x164	/* Unknown */
#define AIU_DELTA_SIGMA5		0x168	/* Unknown */
#define AIU_DELTA_SIGMA6		0x16C	/* Unknown */
#define AIU_DELTA_SIGMA7		0x170	/* Unknown */
#define AIU_DELTA_SIGMA_LCNTS		0x174	/* Unknown */
#define AIU_DELTA_SIGMA_RCNTS		0x178	/* Unknown */
#define AIU_MEM_I2S_START_PTR		0x180
#define AIU_MEM_I2S_RD_PTR		0x184
#define AIU_MEM_I2S_END_PTR		0x188
#define AIU_MEM_I2S_MASKS		0x18C
#define AIU_MEM_I2S_CONTROL		0x190
#define AIU_MEM_IEC958_START_PTR	0x194
#define AIU_MEM_IEC958_RD_PTR		0x198
#define AIU_MEM_IEC958_END_PTR		0x19C
#define AIU_MEM_IEC958_MASKS		0x1A0
#define AIU_MEM_IEC958_CONTROL		0x1A4
#define AIU_MEM_AIFIFO2_START_PTR	0x1A8
#define AIU_MEM_AIFIFO2_CURR_PTR	0x1AC
#define AIU_MEM_AIFIFO2_END_PTR		0x1B0
#define AIU_MEM_AIFIFO2_BYTES_AVAIL	0x1B4
#define AIU_MEM_AIFIFO2_CONTROL		0x1B8
#define AIU_MEM_AIFIFO2_MAN_WP		0x1BC
#define AIU_MEM_AIFIFO2_MAN_RP		0x1C0
#define AIU_MEM_AIFIFO2_LEVEL		0x1C4
#define AIU_MEM_AIFIFO2_BUF_CNTL	0x1C8
#define AIU_MEM_I2S_MAN_WP		0x1CC
#define AIU_MEM_I2S_MAN_RP		0x1D0
#define AIU_MEM_I2S_LEVEL		0x1D4
#define AIU_MEM_I2S_BUF_CNTL		0x1D8
#define AIU_MEM_I2S_BUF_WRAP_COUNT	0x1DC
#define AIU_MEM_I2S_MEM_CTL		0x1E0
#define AIU_MEM_IEC958_MEM_CTL		0x1E4
#define AIU_MEM_IEC958_WRAP_COUNT	0x1E8
#define AIU_MEM_IEC958_IRQ_LEVEL	0x1EC
#define AIU_MEM_IEC958_MAN_WP		0x1F0
#define AIU_MEM_IEC958_MAN_RP		0x1F4
#define AIU_MEM_IEC958_LEVEL		0x1F8
#define AIU_MEM_IEC958_BUF_CNTL		0x1FC
#define AIU_AIFIFO_CTRL			0x200
#define AIU_AIFIFO_STATUS		0x204
#define AIU_AIFIFO_GBIT			0x208
#define AIU_AIFIFO_CLB			0x20C
#define AIU_MEM_AIFIFO_START_PTR	0x210
#define AIU_MEM_AIFIFO_CURR_PTR		0x214
#define AIU_MEM_AIFIFO_END_PTR		0x218
#define AIU_MEM_AIFIFO_BYTES_AVAIL	0x21C
#define AIU_MEM_AIFIFO_CONTROL		0x220
#define AIU_MEM_AIFIFO_MAN_WP		0x224
#define AIU_MEM_AIFIFO_MAN_RP		0x228
#define AIU_MEM_AIFIFO_LEVEL		0x22C
#define AIU_MEM_AIFIFO_BUF_CNTL		0x230
#define AIU_MEM_AIFIFO_BUF_WRAP_COUNT	0x234
#define AIU_MEM_AIFIFO2_BUF_WRAP_COUNT	0x238
#define AIU_MEM_AIFIFO_MEM_CTL		0x23C
#define AIFIFO_TIME_STAMP_CNTL		0x240
#define AIFIFO_TIME_STAMP_SYNC_0	0x244
#define AIFIFO_TIME_STAMP_SYNC_1	0x248
#define AIFIFO_TIME_STAMP_0		0x24C
#define AIFIFO_TIME_STAMP_1		0x250
#define AIFIFO_TIME_STAMP_2		0x254
#define AIFIFO_TIME_STAMP_3		0x258
#define AIFIFO_TIME_STAMP_LENGTH	0x25C
#define AIFIFO2_TIME_STAMP_CNTL		0x260
#define AIFIFO2_TIME_STAMP_SYNC_0	0x264
#define AIFIFO2_TIME_STAMP_SYNC_1	0x268
#define AIFIFO2_TIME_STAMP_0		0x26C
#define AIFIFO2_TIME_STAMP_1		0x270
#define AIFIFO2_TIME_STAMP_2		0x274
#define AIFIFO2_TIME_STAMP_3		0x278
#define AIFIFO2_TIME_STAMP_LENGTH	0x27C
#define IEC958_TIME_STAMP_CNTL		0x280
#define IEC958_TIME_STAMP_SYNC_0	0x284
#define IEC958_TIME_STAMP_SYNC_1	0x288
#define IEC958_TIME_STAMP_0		0x28C
#define IEC958_TIME_STAMP_1		0x290
#define IEC958_TIME_STAMP_2		0x294
#define IEC958_TIME_STAMP_3		0x298
#define IEC958_TIME_STAMP_LENGTH	0x29C
#define AIU_MEM_AIFIFO2_MEM_CTL		0x2A0
#define AIU_I2S_CBUS_DDR_CNTL		0x2A4
#define AIU_I2S_CBUS_DDR_WDATA		0x2A8
#define AIU_I2S_CBUS_DDR_ADDR		0x2AC

#endif /* _AIU_REGS_H_ */
