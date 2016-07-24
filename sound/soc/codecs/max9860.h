/*
 * Driver for the MAX9860 Mono Audio Voice Codec
 *
 * Author: Peter Rosin <peda@axentia.s>
 *         Copyright 2016 Axentia Technologies
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _SND_SOC_MAX9860
#define _SND_SOC_MAX9860

#define MAX9860_INTRSTATUS   0x00
#define MAX9860_MICREADBACK  0x01
#define MAX9860_INTEN        0x02
#define MAX9860_SYSCLK       0x03
#define MAX9860_AUDIOCLKHIGH 0x04
#define MAX9860_AUDIOCLKLOW  0x05
#define MAX9860_IFC1A        0x06
#define MAX9860_IFC1B        0x07
#define MAX9860_VOICEFLTR    0x08
#define MAX9860_DACATTN      0x09
#define MAX9860_ADCLEVEL     0x0a
#define MAX9860_DACGAIN      0x0b
#define MAX9860_MICGAIN      0x0c
#define MAX9860_RESERVED     0x0d
#define MAX9860_MICADC       0x0e
#define MAX9860_NOISEGATE    0x0f
#define MAX9860_PWRMAN       0x10
#define MAX9860_REVISION     0xff

#define MAX9860_MAX_REGISTER 0xff

/* INTRSTATUS */
#define MAX9860_CLD          0x80
#define MAX9860_SLD          0x40
#define MAX9860_ULK          0x20

/* MICREADBACK */
#define MAX9860_NG           0xe0
#define MAX9860_AGC          0x1f

/* INTEN */
#define MAX9860_ICLD         0x80
#define MAX9860_ISLD         0x40
#define MAX9860_IULK         0x20

/* SYSCLK */
#define MAX9860_PSCLK        0x30
#define MAX9860_PSCLK_OFF    0x00
#define MAX9860_PSCLK_SHIFT  4
#define MAX9860_FREQ         0x06
#define MAX9860_FREQ_NORMAL  0x00
#define MAX9860_FREQ_12MHZ   0x02
#define MAX9860_FREQ_13MHZ   0x04
#define MAX9860_FREQ_19_2MHZ 0x06
#define MAX9860_16KHZ        0x01

/* AUDIOCLKHIGH */
#define MAX9860_PLL          0x80
#define MAX9860_NHI          0x7f

/* AUDIOCLKLOW */
#define MAX9860_NLO          0xff

/* IFC1A */
#define MAX9860_MASTER       0x80
#define MAX9860_WCI          0x40
#define MAX9860_DBCI         0x20
#define MAX9860_DDLY         0x10
#define MAX9860_HIZ          0x08
#define MAX9860_TDM          0x04

/* IFC1B */
#define MAX9860_ABCI         0x20
#define MAX9860_ADLY         0x10
#define MAX9860_ST           0x08
#define MAX9860_BSEL         0x07
#define MAX9860_BSEL_OFF     0x00
#define MAX9860_BSEL_64X     0x01
#define MAX9860_BSEL_48X     0x02
#define MAX9860_BSEL_PCLK_2  0x04
#define MAX9860_BSEL_PCLK_4  0x05
#define MAX9860_BSEL_PCLK_8  0x06
#define MAX9860_BSEL_PCLK_16 0x07

/* VOICEFLTR */
#define MAX9860_AVFLT        0xf0
#define MAX9860_AVFLT_SHIFT  4
#define MAX9860_AVFLT_COUNT  6
#define MAX9860_DVFLT        0x0f
#define MAX9860_DVFLT_SHIFT  0
#define MAX9860_DVFLT_COUNT  6

/* DACATTN */
#define MAX9860_DVA          0xfe
#define MAX9860_DVA_SHIFT    1
#define MAX9860_DVA_MUTE     0x5e

/* ADCLEVEL */
#define MAX9860_ADCRL        0xf0
#define MAX9860_ADCRL_SHIFT  4
#define MAX9860_ADCLL        0x0f
#define MAX9860_ADCLL_SHIFT  0
#define MAX9860_ADCxL_MIN    15

/* DACGAIN */
#define MAX9860_DVG          0x60
#define MAX9860_DVG_SHIFT    5
#define MAX9860_DVG_MAX      3
#define MAX9860_DVST         0x1f
#define MAX9860_DVST_SHIFT   0
#define MAX9860_DVST_MIN     31

/* MICGAIN */
#define MAX9860_PAM          0x60
#define MAX9860_PAM_SHIFT    5
#define MAX9860_PAM_MAX      3
#define MAX9860_PGAM         0x1f
#define MAX9860_PGAM_SHIFT   0
#define MAX9860_PGAM_MIN     20

/* MICADC */
#define MAX9860_AGCSRC       0x80
#define MAX9860_AGCSRC_SHIFT 7
#define MAX9860_AGCSRC_COUNT 2
#define MAX9860_AGCRLS       0x70
#define MAX9860_AGCRLS_SHIFT 4
#define MAX9860_AGCRLS_COUNT 8
#define MAX9860_AGCATK       0x0c
#define MAX9860_AGCATK_SHIFT 2
#define MAX9860_AGCATK_COUNT 4
#define MAX9860_AGCHLD       0x03
#define MAX9860_AGCHLD_OFF   0x00
#define MAX9860_AGCHLD_SHIFT 0
#define MAX9860_AGCHLD_COUNT 4

/* NOISEGATE */
#define MAX9860_ANTH         0xf0
#define MAX9860_ANTH_SHIFT   4
#define MAX9860_ANTH_MAX     15
#define MAX9860_AGCTH        0x0f
#define MAX9860_AGCTH_SHIFT  0
#define MAX9860_AGCTH_MIN    15

/* PWRMAN */
#define MAX9860_SHDN         0x80
#define MAX9860_DACEN        0x08
#define MAX9860_DACEN_SHIFT  3
#define MAX9860_ADCLEN       0x02
#define MAX9860_ADCLEN_SHIFT 1
#define MAX9860_ADCREN       0x01
#define MAX9860_ADCREN_SHIFT 0

#endif /* _SND_SOC_MAX9860 */
