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

#ifndef _AUDIN_REGS_H_
#define _AUDIN_REGS_H_

/*
 * Note :
 * Datasheet issue page 196
 * AUDIN_MUTE_VAL 0x35 => impossible: Already assigned to AUDIN_FIFO1_PTR
 * AUDIN_FIFO1_PTR is more likely to be correct here since surrounding registers
 * also deal with AUDIN_FIFO1
 *
 * Clarification needed from Amlogic
 */

#define AUDIN_SPDIF_MODE		0x000
#define AUDIN_SPDIF_FS_CLK_RLTN		0x004
#define AUDIN_SPDIF_CHNL_STS_A		0x008
#define AUDIN_SPDIF_CHNL_STS_B		0x00C
#define AUDIN_SPDIF_MISC		0x010
#define AUDIN_SPDIF_NPCM_PCPD		0x014
#define AUDIN_SPDIF_END			0x03C	/* Unknown */
#define AUDIN_I2SIN_CTRL		0x040
#define AUDIN_SOURCE_SEL		0x044
#define AUDIN_DECODE_FORMAT		0x048
#define AUDIN_DECODE_CONTROL_STATUS	0x04C
#define AUDIN_DECODE_CHANNEL_STATUS_A_0	0x050
#define AUDIN_DECODE_CHANNEL_STATUS_A_1	0x054
#define AUDIN_DECODE_CHANNEL_STATUS_A_2	0x058
#define AUDIN_DECODE_CHANNEL_STATUS_A_3	0x05C
#define AUDIN_DECODE_CHANNEL_STATUS_A_4	0x060
#define AUDIN_DECODE_CHANNEL_STATUS_A_5	0x064
#define AUDIN_FIFO0_START		0x080
#define AUDIN_FIFO0_END			0x084
#define AUDIN_FIFO0_PTR			0x088
#define AUDIN_FIFO0_INTR		0x08C
#define AUDIN_FIFO0_RDPTR		0x090
#define AUDIN_FIFO0_CTRL		0x094
#define AUDIN_FIFO0_CTRL1		0x098
#define AUDIN_FIFO0_LVL0		0x09C
#define AUDIN_FIFO0_LVL1		0x0A0
#define AUDIN_FIFO0_LVL2		0x0A4
#define AUDIN_FIFO0_REQID		0x0C0
#define AUDIN_FIFO0_WRAP		0x0C4
#define AUDIN_FIFO1_START		0x0CC
#define AUDIN_FIFO1_END			0x0D0
#define AUDIN_FIFO1_PTR			0x0D4
#define AUDIN_FIFO1_INTR		0x0D8
#define AUDIN_FIFO1_RDPTR		0x0DC
#define AUDIN_FIFO1_CTRL		0x0E0
#define AUDIN_FIFO1_CTRL1		0x0E4
#define AUDIN_FIFO1_LVL0		0x100
#define AUDIN_FIFO1_LVL1		0x104
#define AUDIN_FIFO1_LVL2		0x108
#define AUDIN_FIFO1_REQID		0x10C
#define AUDIN_FIFO1_WRAP		0x110
#define AUDIN_FIFO2_START		0x114
#define AUDIN_FIFO2_END			0x118
#define AUDIN_FIFO2_PTR			0x11C
#define AUDIN_FIFO2_INTR		0x120
#define AUDIN_FIFO2_RDPTR		0x124
#define AUDIN_FIFO2_CTRL		0x128
#define AUDIN_FIFO2_CTRL1		0x12C
#define AUDIN_FIFO2_LVL0		0x130
#define AUDIN_FIFO2_LVL1		0x134
#define AUDIN_FIFO2_LVL2		0x138
#define AUDIN_FIFO2_REQID		0x13C
#define AUDIN_FIFO2_WRAP		0x140
#define AUDIN_INT_CTRL			0x144
#define AUDIN_FIFO_INT			0x148
#define PCMIN_CTRL0			0x180
#define PCMIN_CTRL1			0x184
#define PCMIN1_CTRL0			0x188
#define PCMIN1_CTRL1			0x18C
#define PCMOUT_CTRL0			0x1C0
#define PCMOUT_CTRL1			0x1C4
#define PCMOUT_CTRL2			0x1C8
#define PCMOUT_CTRL3			0x1CC
#define PCMOUT1_CTRL0			0x1D0
#define PCMOUT1_CTRL1			0x1D4
#define PCMOUT1_CTRL2			0x1D8
#define PCMOUT1_CTRL3			0x1DC
#define AUDOUT_CTRL			0x200
#define AUDOUT_CTRL1			0x204
#define AUDOUT_BUF0_STA			0x208
#define AUDOUT_BUF0_EDA			0x20C
#define AUDOUT_BUF0_WPTR		0x210
#define AUDOUT_BUF1_STA			0x214
#define AUDOUT_BUF1_EDA			0x218
#define AUDOUT_BUF1_WPTR		0x21C
#define AUDOUT_FIFO_RPTR		0x220
#define AUDOUT_INTR_PTR			0x224
#define AUDOUT_FIFO_STS			0x228
#define AUDOUT1_CTRL			0x240
#define AUDOUT1_CTRL1			0x244
#define AUDOUT1_BUF0_STA		0x248
#define AUDOUT1_BUF0_EDA		0x24C
#define AUDOUT1_BUF0_WPTR		0x250
#define AUDOUT1_BUF1_STA		0x254
#define AUDOUT1_BUF1_EDA		0x258
#define AUDOUT1_BUF1_WPTR		0x25C
#define AUDOUT1_FIFO_RPTR		0x260
#define AUDOUT1_INTR_PTR		0x264
#define AUDOUT1_FIFO_STS		0x268
#define AUDIN_HDMI_MEAS_CTRL		0x280
#define AUDIN_HDMI_MEAS_CYCLES_M1	0x284
#define AUDIN_HDMI_MEAS_INTR_MASKN	0x288
#define AUDIN_HDMI_MEAS_INTR_STAT	0x28C
#define AUDIN_HDMI_REF_CYCLES_STAT_0	0x290
#define AUDIN_HDMI_REF_CYCLES_STAT_1	0x294
#define AUDIN_HDMIRX_AFIFO_STAT		0x298
#define AUDIN_FIFO0_PIO_STS		0x2C0
#define AUDIN_FIFO0_PIO_RDL		0x2C4
#define AUDIN_FIFO0_PIO_RDH		0x2C8
#define AUDIN_FIFO1_PIO_STS		0x2CC
#define AUDIN_FIFO1_PIO_RDL		0x2D0
#define AUDIN_FIFO1_PIO_RDH		0x2D4
#define AUDIN_FIFO2_PIO_STS		0x2D8
#define AUDIN_FIFO2_PIO_RDL		0x2DC
#define AUDIN_FIFO2_PIO_RDH		0x2E0
#define AUDOUT_FIFO_PIO_STS		0x2E4
#define AUDOUT_FIFO_PIO_WRL		0x2E8
#define AUDOUT_FIFO_PIO_WRH		0x2EC
#define AUDOUT1_FIFO_PIO_STS		0x2F0	/* Unknown */
#define AUDOUT1_FIFO_PIO_WRL		0x2F4	/* Unknown */
#define AUDOUT1_FIFO_PIO_WRH		0x2F8	/* Unknown */
#define AUD_RESAMPLE_CTRL0		0x2FC
#define AUD_RESAMPLE_CTRL1		0x300
#define AUD_RESAMPLE_STATUS		0x304

#endif /* _AUDIN_REGS_H_ */
