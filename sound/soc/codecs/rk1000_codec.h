/*
 * rk1000_codec.h  --  rk1000 ALSA Soc Audio driver
 *
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef _RK1000_CODEC_H
#define _RK1000_CODEC_H

/* RK1000 register space */
/* ADC High Pass Filter / DSM */
#define ACCELCODEC_R00	0x00
/* DITHER power */
#define ACCELCODEC_R01	0x01
/* DITHER power */
#define ACCELCODEC_R02	0x02
/* DITHER power */
#define ACCELCODEC_R03	0x03
/* Soft mute / sidetone gain control */
#define ACCELCODEC_R04	0x04
/* Right interpolate filter volume control (MSB) */
#define ACCELCODEC_R05	0x05
/* Right interpolate filter volume control (LSB) */
#define ACCELCODEC_R06	0x06
/* Left interpolate filter volume control (MSB) */
#define ACCELCODEC_R07	0x07
/* Left interpolate filter volume control (LSB) */
#define ACCELCODEC_R08	0x08
/* Audio interface control */
#define ACCELCODEC_R09	0x09
/* Sample Rate / CLK control */
#define ACCELCODEC_R0A	0x0A
/* Decimation filter / Interpolate filter enable */
#define ACCELCODEC_R0B	0x0B
/* LIN volume */
#define ACCELCODEC_R0C	0x0C
/* LIP volume */
#define ACCELCODEC_R0D	0x0D
/* AL volume */
#define ACCELCODEC_R0E	0x0E
/* Input volume */
#define ACCELCODEC_R12	0x12
/* Left out mix */
#define ACCELCODEC_R13	0x13
/* Right out mix */
#define ACCELCODEC_R14	0x14
/* LPF out mix / SCF */
#define ACCELCODEC_R15	0x15
/* SCF control */
#define ACCELCODEC_R16	0x16
/* LOUT (AOL) volume */
#define ACCELCODEC_R17	0x17
/* ROUT (AOR) volume */
#define ACCELCODEC_R18	0x18
/* MONOOUT (AOM) volume */
#define ACCELCODEC_R19	0x19
/* MONOOUT / Reference control */
#define ACCELCODEC_R1A	0x1A
/* Bias Current control */
#define ACCELCODEC_R1B	0x1B
/* ADC control */
#define ACCELCODEC_R1C	0x1C
/* Power Mrg 1 */
#define ACCELCODEC_R1D	0x1D
/* Power Mrg 2 */
#define ACCELCODEC_R1E	0x1E
/* Power Mrg 3 */
#define ACCELCODEC_R1F	0x1F

/* ACCELCODEC_R00 */
/* high_pass filter */
#define ASC_HPF_ENABLE		(0x1)
#define ASC_HPF_DISABLE		(0x0)

#define ASC_DSM_MODE_ENABLE	(0x1 << 1)
#define ASC_DSM_MODE_DISABLE	(0x0 << 1)

#define ASC_SCRAMBLE_ENABLE	(0x1 << 2)
#define ASC_SCRAMBLE_DISABLE	(0x0 << 2)

#define ASC_DITHER_ENABLE	(0x1 << 3)
#define ASC_DITHER_DISABLE	(0x0 << 3)

#define ASC_BCLKDIV_4		(0x1 << 4)
#define ASC_BCLKDIV_8		(0x2 << 4)
#define ASC_BCLKDIV_16		(0x3 << 4)

/* ACCECODEC_R04 */
#define ASC_INT_MUTE_L		(0x1)
#define ASC_INT_ACTIVE_L	(0x0)
#define ASC_INT_MUTE_R		(0x1 << 1)
#define ASC_INT_ACTIVE_R	(0x0 << 1)

#define ASC_SIDETONE_L_OFF	(0x0 << 2)
#define ASC_SIDETONE_L_GAIN_MAX	(0x1 << 2)
#define ASC_SIDETONE_R_OFF	(0x0 << 5)
#define ASC_SIDETONE_R_GAIN_MAX	(0x1 << 5)

/* ACCELCODEC_R05 */
#define ASC_INT_VOL_0DB		(0x0)

/* ACCELCODEC_R09 */
#define ASC_DSP_MODE		(0x3)
#define ASC_I2S_MODE		(0x2)
#define ASC_LEFT_MODE		(0x1)
#define ASC_RIGHT_MODE		(0x0)

#define ASC_32BIT_MODE		(0x3 << 2)
#define ASC_24BIT_MODE		(0x2 << 2)
#define ASC_20BIT_MODE		(0x1 << 2)
#define ASC_16BIT_MODE		(0x0 << 2)

#define ASC_INVERT_LRCLK	(0x1 << 4)
#define ASC_NORMAL_LRCLK	(0x0 << 4)

#define ASC_LRSWAP_ENABLE	(0x1 << 5)
#define ASC_LRSWAP_DISABLE	(0x0 << 5)

#define ASC_MASTER_MODE		(0x1 << 6)
#define ASC_SLAVE_MODE		(0x0 << 6)

#define ASC_INVERT_BCLK		(0x1 << 7)
#define ASC_NORMAL_BCLK		(0x0 << 7)

/* ACCELCODEC_R0A */
#define ASC_USB_MODE		(0x1)
#define ASC_NORMAL_MODE		(0x0)

#define ASC_CLKDIV2		(0x1 << 6)
#define ASC_CLKNODIV		(0x0 << 6)

#define ASC_CLK_ENABLE		(0x1 << 7)
#define ASC_CLK_DISABLE		(0x0 << 7)

/* ACCELCODEC_R0B */
#define ASC_DEC_ENABLE		(0x1)
#define ASC_DEC_DISABLE		(0x0)
#define ASC_INT_ENABLE		(0x1 << 1)
#define ASC_INT_DISABLE		(0x0 << 1)

#define ASC_INPUT_MUTE		(0x1 << 7)
#define ASC_INPUT_ACTIVE	(0x0 << 7)
#define ASC_INPUT_VOL_0DB	(0x0)

/* ACCELCODEC_R12 */
#define ASC_LINE_INPUT		(0)
#define ASC_MIC_INPUT		(1 << 7)

#define ASC_MIC_BOOST_0DB	(0)
#define ASC_MIC_BOOST_20DB	(1 << 5)

/* ACCELCODEC_R13 */
#define ASC_LPGAMXVOL_0DB	(0x5)
/* the left channel PGA output is directly fed into the left mixer */
#define ASC_LPGAMX_ENABLE	(0x1 << 3)
#define ASC_LPGAMX_DISABLE	(0x0 << 3)
#define ASC_ALMXVOL_0DB		(0x5 << 4)
/* the left second line input is directly fed into the left mixer */
#define ASC_ALMX_ENABLE		(0x1 << 7)
#define ASC_ALMX_DISABLE	(0x0 << 7)

/* ACCELCODEC_R14 */
#define ASC_RPGAMXVOL_0DB	(0x5)
/* the right channel PGA output is directly fed into the right mixer */
#define ASC_RPGAMX_ENABLE	(0x1 << 3)
#define ASC_RPGAMX_DISABLE	(0x0 << 3)
#define ASC_ARMXVOL_0DB		(0x5 << 4)
/* the right second line input is directly fed into the right mixer */
#define ASC_ARMX_ENABLE		(0x1 << 7)
#define ASC_ARMX_DISABLE	(0x0 << 7)

/*ACCELCODEC_R15 */
/*the left differential signal from DAC is directly fed into the left mixer*/
#define ASC_LDAMX_ENABLE	(0x1 << 2)
#define ASC_LDAMX_DISABLE	(0x0 << 2)
/* the right differential signal --> the right mixer */
#define ASC_RDAMX_ENABLE	(0x1 << 3)
#define ASC_RDAMX_DISABLE	(0x0 << 3)
/* the left channel LPF is mute */
#define ASC_LSCF_MUTE		(0x1 << 4)
#define ASC_LSCF_ACTIVE		(0x0 << 4)
/* the right channel LPF is mute */
#define ASC_RSCF_MUTE		(0x1 << 5)
#define ASC_RSCF_ACTIVE		(0x0 << 5)
/* the left channel LPF output is fed into the left into the mixer */
#define ASC_LLPFMX_ENABLE	(0x1 << 6)
#define ASC_LLPFMX_DISABLE	(0x0 << 6)
/* the right channel LPF output is fed into the right into the mixer. */
#define ASC_RLPFMX_ENABLE	(0x1 << 7)
#define ASC_RLPFMX_DISABLE	(0x0 << 7)

/* ACCELCODEC_R17/R18 */
#define ASC_OUTPUT_MUTE		(0x1 << 6)
#define ASC_OUTPUT_ACTIVE	(0x0 << 6)
#define ASC_CROSSZERO_EN	(0x1 << 7)
#define ASC_OUTPUT_VOL_0DB	(0x0F)
/* ACCELCODEC_R19 */
#define ASC_MONO_OUTPUT_MUTE	(0x1 << 7)
#define ASC_MONO_OUTPUT_ACTIVE	(0x0 << 7)
#define ASC_MONO_CROSSZERO_EN	(0x1 << 6)

/* ACCELCODEC_R1A */
#define ASC_VMDSCL_SLOWEST	(0x0 << 2)
#define ASC_VMDSCL_SLOW		(0x1 << 2)
#define ASC_VMDSCL_FAST		(0x2 << 2)
#define ASC_VMDSCL_FASTEST	(0x3 << 2)

#define ASC_MICBIAS_09		(0x1 << 4)
#define ASC_MICBIAS_06		(0x0 << 4)

/* the right channel LPF output is fed to mono PA */
#define ASC_L2M_ENABLE		(0x1 << 5)
#define ASC_L2M_DISABLE		(0x0 << 5)
/* the left channel LPF output is fed to mono PA */
#define ASC_R2M_ENABLE		(0x1 << 6)
#define ASC_R2M_DISABLE		(0x0 << 6)
/* the capless connection is enable */
#define ASC_CAPLESS_ENABLE	(0x1 << 7)
#define ASC_CAPLESS_DISABLE	(0x0 << 7)

/* ACCELCODEC_R1C */
/* the amplitude setting of the ASDM dither(div=vdd/48) */
#define ASC_DITH_0_DIV		(0x0 << 3)
#define ASC_DITH_2_DIV		(0x1 << 3)
#define ASC_DITH_4_DIV		(0x2 << 3)
#define ASC_DITH_8_DIV		(0x3 << 3)

/* the ASDM dither is enabled */
#define ASC_DITH_ENABLE		(0x1 << 5)
#define ASC_DITH_DISABLE	(0x0 << 5)

/* the ASDM DEM is enabled */
#define ASC_DEM_ENABLE		(0x1 << 7)
#define ASC_DEM_DISABLE		(0x0 << 7)

/* ACCELCODEC_R1D */
/* the VMID reference is powered down. VMID is connected to GND */
#define ASC_PDVMID_ENABLE	(0x1)
#define ASC_PDVMID_DISABLE	(0x0)
/* the PGA S2D buffer is power down */
#define ASC_PDSDL_ENABLE	(0x1 << 2)
#define ASC_PDSDL_DISABLE	(0x0 << 2)
/* the micphone input Op-Amp is power down */
#define ASC_PDBSTL_ENABLE	(0x1 << 4)
#define ASC_PDBSTL_DISABLE	(0x0 << 4)
/* the PGA is power down */
#define ASC_PDPGAL_ENABLE	(0x1 << 6)
#define ASC_PDPGAL_DISABLE	(0x0 << 6)
/* reference generator is power down */
#define ASC_PDREF_ENABLE	(0x1 << 7)
#define ASC_PDREF_DISABLE	(0x0 << 7)

/* ACCELCODEC_R1E */
/* the right channel PA is power down */
#define ASC_PDPAR_ENABLE	(0x1)
#define ASC_PDPAR_DISABLE	(0x0)
/* the left channel power amplifier is power down */
#define ASC_PDPAL_ENABLE	(0x1 << 1)
#define ASC_PDPAL_DISABLE	(0x0 << 1)
/* the right mixer is power down */
#define ASC_PDMIXR_ENABLE	(0x1 << 2)
#define ASC_PDMIXR_DISABLE	(0x0 << 2)
/* the left mixer is power down */
#define ASC_PDMIXL_ENABLE	(0x1 << 3)
#define ASC_PDMIXL_DISABLE	(0x0 << 3)
/* the right RC LPF is power down */
#define ASC_PDLPFR_ENABLE	(0x1 << 4)
#define ASC_PDLPFR_DISABLE	(0x0 << 4)
/* the left channel RC LPF is power down */
#define ASC_PDLPFL_ENABLE	(0x1 << 5)
#define ASC_PDLPFL_DISABLE	(0x0 << 5)
/* the ASDM is power down */
#define ASC_PDASDML_ENABLE	(0x1 << 7)
#define ASC_PDASDML_DISABLE	(0x0 << 7)

/* ACCELCODEC_R1F */
/* the right channel DAC is power down */
#define ASC_PDSCFR_ENABLE	(0x1 << 1)
#define ASC_PDSCFR_DISABLE	(0x0 << 1)
/* the left channel DAC is power down */
#define ASC_PDSCFL_ENABLE	(0x1 << 2)
#define ASC_PDSCFL_DISABLE	(0x0 << 2)
/* the micbias is power down */
#define ASC_PDMICB_ENABLE	(0x1 << 4)
#define ASC_PDMICB_DISABLE	(0x0 << 4)
/* the left channel LPF is power down */
#define ASC_PDIB_ENABLE		(0x1 << 5)
#define ASC_PDIB_DISABLE	(0x0 << 5)
/* the mon mixer is power down */
#define ASC_PDMIXM_ENABLE	(0x1 << 6)
#define ASC_PDMIXM_DISABLE	(0x0 << 6)
/* the mono PA is power down. */
#define ASC_PDPAM_ENABLE	(0x1 << 7)
#define ASC_PDPAM_DISABLE	(0x0 << 7)

/* left and right PA gain */
#define LINE_2_MIXER_GAIN	(0x5)
#define RK1000_CODEC_NUM_REG	0x20

#define GPIO_HIGH		1
#define GPIO_LOW		0

/* rk1000 ctl register */
#define CODEC_CON		0x01
#define CODEC_ON		0X00
#define CODEC_OFF		0x0d

#endif
