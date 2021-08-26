/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cs35l35.h -- CS35L35 ALSA SoC audio driver
 *
 * Copyright 2016 Cirrus Logic, Inc.
 *
 * Author: Brian Austin <brian.austin@cirrus.com>
 */

#ifndef __CS35L35_H__
#define __CS35L35_H__

#define CS35L35_FIRSTREG		0x01
#define CS35L35_LASTREG			0x7E
#define CS35L35_CHIP_ID			0x00035A35
#define CS35L35_DEVID_AB		0x01	/* Device ID A & B [RO] */
#define CS35L35_DEVID_CD		0x02    /* Device ID C & D [RO] */
#define CS35L35_DEVID_E			0x03    /* Device ID E [RO] */
#define CS35L35_FAB_ID			0x04	/* Fab ID [RO] */
#define CS35L35_REV_ID			0x05	/* Revision ID [RO] */
#define CS35L35_PWRCTL1			0x06    /* Power Ctl 1 */
#define CS35L35_PWRCTL2			0x07    /* Power Ctl 2 */
#define CS35L35_PWRCTL3			0x08	/* Power Ctl 3 */
#define CS35L35_CLK_CTL1		0x0A	/* Clocking Ctl 1 */
#define CS35L35_CLK_CTL2		0x0B	/* Clocking Ctl 2 */
#define CS35L35_CLK_CTL3		0x0C	/* Clocking Ctl 3 */
#define CS35L35_SP_FMT_CTL1		0x0D	/* Serial Port Format CTL1 */
#define CS35L35_SP_FMT_CTL2		0x0E	/* Serial Port Format CTL2 */
#define CS35L35_SP_FMT_CTL3		0x0F	/* Serial Port Format CTL3 */
#define CS35L35_MAG_COMP_CTL		0x13	/* Magnitude Comp CTL */
#define CS35L35_AMP_INP_DRV_CTL		0x14	/* Amp Input Drive Ctl */
#define CS35L35_AMP_DIG_VOL_CTL		0x15	/* Amplifier Dig Volume Ctl */
#define CS35L35_AMP_DIG_VOL		0x16	/* Amplifier Dig Volume */
#define CS35L35_ADV_DIG_VOL		0x17	/* Advisory Digital Volume */
#define CS35L35_PROTECT_CTL		0x18	/* Amp Gain - Prot Ctl Param */
#define CS35L35_AMP_GAIN_AUD_CTL	0x19	/* Amp Serial Port Gain Ctl */
#define CS35L35_AMP_GAIN_PDM_CTL	0x1A	/* Amplifier Gain PDM Ctl */
#define CS35L35_AMP_GAIN_ADV_CTL	0x1B	/* Amplifier Gain Ctl */
#define CS35L35_GPI_CTL			0x1C	/* GPI Ctl */
#define CS35L35_BST_CVTR_V_CTL		0x1D	/* Boost Conv Voltage Ctl */
#define CS35L35_BST_PEAK_I		0x1E	/* Boost Conv Peak Current */
#define CS35L35_BST_RAMP_CTL		0x20	/* Boost Conv Soft Ramp Ctl */
#define CS35L35_BST_CONV_COEF_1		0x21	/* Boost Conv Coefficients 1 */
#define CS35L35_BST_CONV_COEF_2		0x22	/* Boost Conv Coefficients 2 */
#define CS35L35_BST_CONV_SLOPE_COMP	0x23	/* Boost Conv Slope Comp */
#define CS35L35_BST_CONV_SW_FREQ	0x24	/* Boost Conv L BST SW Freq */
#define CS35L35_CLASS_H_CTL		0x30	/* CLS H Control */
#define CS35L35_CLASS_H_HEADRM_CTL	0x31	/* CLS H Headroom Ctl */
#define CS35L35_CLASS_H_RELEASE_RATE	0x32	/* CLS H Release Rate */
#define CS35L35_CLASS_H_FET_DRIVE_CTL	0x33	/* CLS H Weak FET Drive Ctl */
#define CS35L35_CLASS_H_VP_CTL		0x34	/* CLS H VP Ctl */
#define CS35L35_CLASS_H_STATUS		0x38	/* CLS H Status */
#define CS35L35_VPBR_CTL		0x3A	/* VPBR Ctl */
#define CS35L35_VPBR_VOL_CTL		0x3B	/* VPBR Volume Ctl */
#define CS35L35_VPBR_TIMING_CTL		0x3C	/* VPBR Timing Ctl */
#define CS35L35_VPBR_MODE_VOL_CTL	0x3D	/* VPBR Mode/Attack Vol Ctl */
#define CS35L35_VPBR_ATTEN_STATUS	0x4B	/* VPBR Attenuation Status */
#define CS35L35_SPKR_MON_CTL		0x4E	/* Speaker Monitoring Ctl */
#define CS35L35_IMON_SCALE_CTL		0x51	/* IMON Scale Ctl */
#define CS35L35_AUDIN_RXLOC_CTL		0x52	/* Audio Input RX Loc Ctl */
#define CS35L35_ADVIN_RXLOC_CTL		0x53	/* Advisory Input RX Loc Ctl */
#define CS35L35_VMON_TXLOC_CTL		0x54	/* VMON TX Loc Ctl */
#define CS35L35_IMON_TXLOC_CTL		0x55	/* IMON TX Loc Ctl */
#define CS35L35_VPMON_TXLOC_CTL		0x56	/* VPMON TX Loc Ctl */
#define CS35L35_VBSTMON_TXLOC_CTL	0x57	/* VBSTMON TX Loc Ctl */
#define CS35L35_VPBR_STATUS_TXLOC_CTL	0x58	/* VPBR Status TX Loc Ctl */
#define CS35L35_ZERO_FILL_LOC_CTL	0x59	/* Zero Fill Loc Ctl */
#define CS35L35_AUDIN_DEPTH_CTL		0x5A	/* Audio Input Depth Ctl */
#define CS35L35_SPKMON_DEPTH_CTL	0x5B	/* SPK Mon Output Depth Ctl */
#define CS35L35_SUPMON_DEPTH_CTL	0x5C	/* Supply Mon Out Depth Ctl */
#define CS35L35_ZEROFILL_DEPTH_CTL	0x5D	/* Zero Fill Mon Output Ctl */
#define CS35L35_MULT_DEV_SYNCH1		0x62	/* Multidevice Synch */
#define CS35L35_MULT_DEV_SYNCH2		0x63	/* Multidevice Synch 2 */
#define CS35L35_PROT_RELEASE_CTL	0x64	/* Protection Release Ctl */
#define CS35L35_DIAG_MODE_REG_LOCK	0x68	/* Diagnostic Mode Reg Lock */
#define CS35L35_DIAG_MODE_CTL_1		0x69	/* Diagnostic Mode Ctl 1 */
#define CS35L35_DIAG_MODE_CTL_2		0x6A	/* Diagnostic Mode Ctl 2 */
#define CS35L35_INT_MASK_1		0x70	/* Interrupt Mask 1 */
#define CS35L35_INT_MASK_2		0x71	/* Interrupt Mask 2 */
#define CS35L35_INT_MASK_3		0x72	/* Interrupt Mask 3 */
#define CS35L35_INT_MASK_4		0x73	/* Interrupt Mask 4 */
#define CS35L35_INT_STATUS_1		0x74	/* Interrupt Status 1 */
#define CS35L35_INT_STATUS_2		0x75	/* Interrupt Status 2 */
#define CS35L35_INT_STATUS_3		0x76	/* Interrupt Status 3 */
#define CS35L35_INT_STATUS_4		0x77	/* Interrupt Status 4 */
#define CS35L35_PLL_STATUS		0x78	/* PLL Status */
#define CS35L35_OTP_TRIM_STATUS		0x7E	/* OTP Trim Status */

#define CS35L35_MAX_REGISTER		0x7F

/* CS35L35_PWRCTL1 */
#define CS35L35_SFT_RST			0x80
#define CS35L35_DISCHG_FLT		0x02
#define CS35L35_PDN_ALL			0x01

/* CS35L35_PWRCTL2 */
#define CS35L35_PDN_VMON		0x80
#define CS35L35_PDN_IMON		0x40
#define CS35L35_PDN_CLASSH		0x20
#define CS35L35_PDN_VPBR		0x10
#define CS35L35_PDN_BST			0x04
#define CS35L35_PDN_AMP			0x01

/* CS35L35_PWRCTL3 */
#define CS35L35_PDN_VBSTMON_OUT		0x10
#define CS35L35_PDN_VMON_OUT		0x08

#define CS35L35_AUDIN_DEPTH_MASK	0x03
#define CS35L35_AUDIN_DEPTH_SHIFT	0
#define CS35L35_ADVIN_DEPTH_MASK	0x0C
#define CS35L35_ADVIN_DEPTH_SHIFT	2
#define CS35L35_SDIN_DEPTH_8		0x01
#define CS35L35_SDIN_DEPTH_16		0x02
#define CS35L35_SDIN_DEPTH_24		0x03

#define CS35L35_SDOUT_DEPTH_8		0x01
#define CS35L35_SDOUT_DEPTH_12		0x02
#define CS35L35_SDOUT_DEPTH_16		0x03

#define CS35L35_AUD_IN_LR_MASK		0x80
#define CS35L35_AUD_IN_LR_SHIFT		7
#define CS35L35_ADV_IN_LR_MASK		0x80
#define CS35L35_ADV_IN_LR_SHIFT		7
#define CS35L35_AUD_IN_LOC_MASK		0x0F
#define CS35L35_AUD_IN_LOC_SHIFT	0
#define CS35L35_ADV_IN_LOC_MASK		0x0F
#define CS35L35_ADV_IN_LOC_SHIFT	0

#define CS35L35_IMON_DEPTH_MASK		0x03
#define CS35L35_IMON_DEPTH_SHIFT	0
#define CS35L35_VMON_DEPTH_MASK		0x0C
#define CS35L35_VMON_DEPTH_SHIFT	2
#define CS35L35_VBSTMON_DEPTH_MASK	0x03
#define CS35L35_VBSTMON_DEPTH_SHIFT	0
#define CS35L35_VPMON_DEPTH_MASK	0x0C
#define CS35L35_VPMON_DEPTH_SHIFT	2
#define CS35L35_VPBRSTAT_DEPTH_MASK	0x30
#define CS35L35_VPBRSTAT_DEPTH_SHIFT	4
#define CS35L35_ZEROFILL_DEPTH_MASK	0x03
#define CS35L35_ZEROFILL_DEPTH_SHIFT	0x00

#define CS35L35_MON_TXLOC_MASK		0x3F
#define CS35L35_MON_TXLOC_SHIFT		0
#define CS35L35_MON_FRM_MASK		0x80
#define CS35L35_MON_FRM_SHIFT		7

#define CS35L35_IMON_SCALE_MASK		0xF8
#define CS35L35_IMON_SCALE_SHIFT	3

#define CS35L35_MS_MASK			0x80
#define CS35L35_MS_SHIFT		7
#define CS35L35_SPMODE_MASK		0x40
#define CS35L35_SP_DRV_MASK		0x10
#define CS35L35_SP_DRV_SHIFT		4
#define CS35L35_CLK_CTL2_MASK		0xFF
#define CS35L35_PDM_MODE_MASK		0x40
#define CS35L35_PDM_MODE_SHIFT		6
#define CS35L35_CLK_SOURCE_MASK		0x03
#define CS35L35_CLK_SOURCE_SHIFT	0
#define CS35L35_CLK_SOURCE_MCLK		0
#define CS35L35_CLK_SOURCE_SCLK		1
#define CS35L35_CLK_SOURCE_PDM		2

#define CS35L35_SP_SCLKS_MASK		0x0F
#define CS35L35_SP_SCLKS_SHIFT		0x00
#define CS35L35_SP_SCLKS_16FS		0x03
#define CS35L35_SP_SCLKS_32FS		0x07
#define CS35L35_SP_SCLKS_48FS		0x0B
#define CS35L35_SP_SCLKS_64FS		0x0F
#define CS35L35_SP_RATE_MASK		0xC0
#define CS35L35_SP_RATE_SHIFT		6

#define CS35L35_PDN_BST_MASK		0x06
#define CS35L35_PDN_BST_FETON_SHIFT	1
#define CS35L35_PDN_BST_FETOFF_SHIFT	2
#define CS35L35_PWR2_PDN_MASK		0xE0
#define CS35L35_PWR3_PDN_MASK		0x1E
#define CS35L35_PDN_ALL_MASK		0x01
#define CS35L35_DISCHG_FILT_MASK	0x02
#define CS35L35_DISCHG_FILT_SHIFT	1
#define CS35L35_MCLK_DIS_MASK		0x04
#define CS35L35_MCLK_DIS_SHIFT		2

#define CS35L35_BST_CTL_MASK		0x7F
#define CS35L35_BST_CTL_SHIFT		0
#define CS35L35_BST_IPK_MASK		0x1F
#define CS35L35_BST_IPK_SHIFT		0
#define CS35L35_AMP_MUTE_MASK		0x20
#define CS35L35_AMP_MUTE_SHIFT		5
#define CS35L35_AMP_GAIN_ZC_MASK	0x10
#define CS35L35_AMP_GAIN_ZC_SHIFT	4

#define CS35L35_AMP_DIGSFT_MASK		0x02
#define CS35L35_AMP_DIGSFT_SHIFT	1

/* CS35L35_SP_FMT_CTL3 */
#define CS35L35_SP_I2S_DRV_MASK		0x03
#define CS35L35_SP_I2S_DRV_SHIFT	0

/* Boost Converter Config */
#define CS35L35_BST_CONV_COEFF_MASK	0xFF
#define CS35L35_BST_CONV_SLOPE_MASK	0xFF
#define CS35L35_BST_CONV_LBST_MASK	0x03
#define CS35L35_BST_CONV_SWFREQ_MASK	0xF0

/* Class H Algorithm Control */
#define CS35L35_CH_STEREO_MASK		0x40
#define CS35L35_CH_STEREO_SHIFT		6
#define CS35L35_CH_BST_OVR_MASK		0x04
#define CS35L35_CH_BST_OVR_SHIFT	2
#define CS35L35_CH_BST_LIM_MASK		0x08
#define CS35L35_CH_BST_LIM_SHIFT	3
#define CS35L35_CH_MEM_DEPTH_MASK	0x01
#define CS35L35_CH_MEM_DEPTH_SHIFT	0
#define CS35L35_CH_HDRM_CTL_MASK	0x3F
#define CS35L35_CH_HDRM_CTL_SHIFT	0
#define CS35L35_CH_REL_RATE_MASK	0xFF
#define CS35L35_CH_REL_RATE_SHIFT	0
#define CS35L35_CH_WKFET_DIS_MASK	0x80
#define CS35L35_CH_WKFET_DIS_SHIFT	7
#define CS35L35_CH_WKFET_DEL_MASK	0x70
#define CS35L35_CH_WKFET_DEL_SHIFT	4
#define CS35L35_CH_WKFET_THLD_MASK	0x0F
#define CS35L35_CH_WKFET_THLD_SHIFT	0
#define CS35L35_CH_VP_AUTO_MASK		0x80
#define CS35L35_CH_VP_AUTO_SHIFT	7
#define CS35L35_CH_VP_RATE_MASK		0x60
#define CS35L35_CH_VP_RATE_SHIFT	5
#define CS35L35_CH_VP_MAN_MASK		0x1F
#define CS35L35_CH_VP_MAN_SHIFT		0

/* CS35L35_PROT_RELEASE_CTL */
#define CS35L35_CAL_ERR_RLS		0x80
#define CS35L35_SHORT_RLS		0x04
#define CS35L35_OTW_RLS			0x02
#define CS35L35_OTE_RLS			0x01

/* INT Mask Registers */
#define CS35L35_INT1_CRIT_MASK		0x38
#define CS35L35_INT2_CRIT_MASK		0xEF
#define CS35L35_INT3_CRIT_MASK		0xEE
#define CS35L35_INT4_CRIT_MASK		0xFF

/* PDN DONE Masks */
#define CS35L35_M_PDN_DONE_SHIFT	4
#define CS35L35_M_PDN_DONE_MASK		0x10

/* CS35L35_INT_1 */
#define CS35L35_CAL_ERR			0x80
#define CS35L35_OTP_ERR			0x40
#define CS35L35_LRCLK_ERR		0x20
#define CS35L35_SPCLK_ERR		0x10
#define CS35L35_MCLK_ERR		0x08
#define CS35L35_AMP_SHORT		0x04
#define CS35L35_OTW			0x02
#define CS35L35_OTE			0x01

/* CS35L35_INT_2 */
#define CS35L35_PDN_DONE		0x10
#define CS35L35_VPBR_ERR		0x02
#define CS35L35_VPBR_CLR		0x01

/* CS35L35_INT_3 */
#define CS35L35_BST_HIGH		0x10
#define CS35L35_BST_HIGH_FLAG		0x08
#define CS35L35_BST_IPK_FLAG		0x04
#define CS35L35_LBST_SHORT		0x01

/* CS35L35_INT_4 */
#define CS35L35_VMON_OVFL		0x08
#define CS35L35_IMON_OVFL		0x04

#define CS35L35_FORMATS (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

struct  cs35l35_private {
	struct device *dev;
	struct cs35l35_platform_data pdata;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[2];
	int num_supplies;
	int sysclk;
	int sclk;
	bool pdm_mode;
	bool i2s_mode;
	bool clock_consumer;
	/* GPIO for /RST */
	struct gpio_desc *reset_gpio;
	struct completion pdn_done;
};

static const char * const cs35l35_supplies[] = {
	"VA",
	"VP",
};

#endif
