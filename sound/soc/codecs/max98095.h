/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * max98095.h -- MAX98095 ALSA SoC Audio driver
 *
 * Copyright 2011 Maxim Integrated Products
 */

#ifndef _MAX98095_H
#define _MAX98095_H

/*
 * MAX98095 Registers Definition
 */

#define M98095_000_HOST_DATA                0x00
#define M98095_001_HOST_INT_STS             0x01
#define M98095_002_HOST_RSP_STS             0x02
#define M98095_003_HOST_CMD_STS             0x03
#define M98095_004_CODEC_STS                0x04
#define M98095_005_DAI1_ALC_STS             0x05
#define M98095_006_DAI2_ALC_STS             0x06
#define M98095_007_JACK_AUTO_STS            0x07
#define M98095_008_JACK_MANUAL_STS          0x08
#define M98095_009_JACK_VBAT_STS            0x09
#define M98095_00A_ACC_ADC_STS              0x0A
#define M98095_00B_MIC_NG_AGC_STS           0x0B
#define M98095_00C_SPK_L_VOLT_STS           0x0C
#define M98095_00D_SPK_R_VOLT_STS           0x0D
#define M98095_00E_TEMP_SENSOR_STS          0x0E
#define M98095_00F_HOST_CFG                 0x0F
#define M98095_010_HOST_INT_CFG             0x10
#define M98095_011_HOST_INT_EN              0x11
#define M98095_012_CODEC_INT_EN             0x12
#define M98095_013_JACK_INT_EN              0x13
#define M98095_014_JACK_INT_EN              0x14
#define M98095_015_DEC                      0x15
#define M98095_016_RESERVED                 0x16
#define M98095_017_RESERVED                 0x17
#define M98095_018_KEYCODE3                 0x18
#define M98095_019_KEYCODE2                 0x19
#define M98095_01A_KEYCODE1                 0x1A
#define M98095_01B_KEYCODE0                 0x1B
#define M98095_01C_OEMCODE1                 0x1C
#define M98095_01D_OEMCODE0                 0x1D
#define M98095_01E_XCFG1                    0x1E
#define M98095_01F_XCFG2                    0x1F
#define M98095_020_XCFG3                    0x20
#define M98095_021_XCFG4                    0x21
#define M98095_022_XCFG5                    0x22
#define M98095_023_XCFG6                    0x23
#define M98095_024_XGPIO                    0x24
#define M98095_025_XCLKCFG                  0x25
#define M98095_026_SYS_CLK                  0x26
#define M98095_027_DAI1_CLKMODE             0x27
#define M98095_028_DAI1_CLKCFG_HI           0x28
#define M98095_029_DAI1_CLKCFG_LO           0x29
#define M98095_02A_DAI1_FORMAT              0x2A
#define M98095_02B_DAI1_CLOCK               0x2B
#define M98095_02C_DAI1_IOCFG               0x2C
#define M98095_02D_DAI1_TDM                 0x2D
#define M98095_02E_DAI1_FILTERS             0x2E
#define M98095_02F_DAI1_LVL1                0x2F
#define M98095_030_DAI1_LVL2                0x30
#define M98095_031_DAI2_CLKMODE             0x31
#define M98095_032_DAI2_CLKCFG_HI           0x32
#define M98095_033_DAI2_CLKCFG_LO           0x33
#define M98095_034_DAI2_FORMAT              0x34
#define M98095_035_DAI2_CLOCK               0x35
#define M98095_036_DAI2_IOCFG               0x36
#define M98095_037_DAI2_TDM                 0x37
#define M98095_038_DAI2_FILTERS             0x38
#define M98095_039_DAI2_LVL1                0x39
#define M98095_03A_DAI2_LVL2                0x3A
#define M98095_03B_DAI3_CLKMODE             0x3B
#define M98095_03C_DAI3_CLKCFG_HI           0x3C
#define M98095_03D_DAI3_CLKCFG_LO           0x3D
#define M98095_03E_DAI3_FORMAT              0x3E
#define M98095_03F_DAI3_CLOCK               0x3F
#define M98095_040_DAI3_IOCFG               0x40
#define M98095_041_DAI3_TDM                 0x41
#define M98095_042_DAI3_FILTERS             0x42
#define M98095_043_DAI3_LVL1                0x43
#define M98095_044_DAI3_LVL2                0x44
#define M98095_045_CFG_DSP                  0x45
#define M98095_046_DAC_CTRL1                0x46
#define M98095_047_DAC_CTRL2                0x47
#define M98095_048_MIX_DAC_LR               0x48
#define M98095_049_MIX_DAC_M                0x49
#define M98095_04A_MIX_ADC_LEFT             0x4A
#define M98095_04B_MIX_ADC_RIGHT            0x4B
#define M98095_04C_MIX_HP_LEFT              0x4C
#define M98095_04D_MIX_HP_RIGHT             0x4D
#define M98095_04E_CFG_HP                   0x4E
#define M98095_04F_MIX_RCV                  0x4F
#define M98095_050_MIX_SPK_LEFT             0x50
#define M98095_051_MIX_SPK_RIGHT            0x51
#define M98095_052_MIX_SPK_CFG              0x52
#define M98095_053_MIX_LINEOUT1             0x53
#define M98095_054_MIX_LINEOUT2             0x54
#define M98095_055_MIX_LINEOUT_CFG          0x55
#define M98095_056_LVL_SIDETONE_DAI12       0x56
#define M98095_057_LVL_SIDETONE_DAI3        0x57
#define M98095_058_LVL_DAI1_PLAY            0x58
#define M98095_059_LVL_DAI1_EQ              0x59
#define M98095_05A_LVL_DAI2_PLAY            0x5A
#define M98095_05B_LVL_DAI2_EQ              0x5B
#define M98095_05C_LVL_DAI3_PLAY            0x5C
#define M98095_05D_LVL_ADC_L                0x5D
#define M98095_05E_LVL_ADC_R                0x5E
#define M98095_05F_LVL_MIC1                 0x5F
#define M98095_060_LVL_MIC2                 0x60
#define M98095_061_LVL_LINEIN               0x61
#define M98095_062_LVL_LINEOUT1             0x62
#define M98095_063_LVL_LINEOUT2             0x63
#define M98095_064_LVL_HP_L                 0x64
#define M98095_065_LVL_HP_R                 0x65
#define M98095_066_LVL_RCV                  0x66
#define M98095_067_LVL_SPK_L                0x67
#define M98095_068_LVL_SPK_R                0x68
#define M98095_069_MICAGC_CFG               0x69
#define M98095_06A_MICAGC_THRESH            0x6A
#define M98095_06B_SPK_NOISEGATE            0x6B
#define M98095_06C_DAI1_ALC1_TIME           0x6C
#define M98095_06D_DAI1_ALC1_COMP           0x6D
#define M98095_06E_DAI1_ALC1_EXPN           0x6E
#define M98095_06F_DAI1_ALC1_GAIN           0x6F
#define M98095_070_DAI1_ALC2_TIME           0x70
#define M98095_071_DAI1_ALC2_COMP           0x71
#define M98095_072_DAI1_ALC2_EXPN           0x72
#define M98095_073_DAI1_ALC2_GAIN           0x73
#define M98095_074_DAI1_ALC3_TIME           0x74
#define M98095_075_DAI1_ALC3_COMP           0x75
#define M98095_076_DAI1_ALC3_EXPN           0x76
#define M98095_077_DAI1_ALC3_GAIN           0x77
#define M98095_078_DAI2_ALC1_TIME           0x78
#define M98095_079_DAI2_ALC1_COMP           0x79
#define M98095_07A_DAI2_ALC1_EXPN           0x7A
#define M98095_07B_DAI2_ALC1_GAIN           0x7B
#define M98095_07C_DAI2_ALC2_TIME           0x7C
#define M98095_07D_DAI2_ALC2_COMP           0x7D
#define M98095_07E_DAI2_ALC2_EXPN           0x7E
#define M98095_07F_DAI2_ALC2_GAIN           0x7F
#define M98095_080_DAI2_ALC3_TIME           0x80
#define M98095_081_DAI2_ALC3_COMP           0x81
#define M98095_082_DAI2_ALC3_EXPN           0x82
#define M98095_083_DAI2_ALC3_GAIN           0x83
#define M98095_084_HP_NOISE_GATE            0x84
#define M98095_085_AUX_ADC                  0x85
#define M98095_086_CFG_LINE                 0x86
#define M98095_087_CFG_MIC                  0x87
#define M98095_088_CFG_LEVEL                0x88
#define M98095_089_JACK_DET_AUTO            0x89
#define M98095_08A_JACK_DET_MANUAL          0x8A
#define M98095_08B_JACK_KEYSCAN_DBC         0x8B
#define M98095_08C_JACK_KEYSCAN_DLY         0x8C
#define M98095_08D_JACK_KEY_THRESH          0x8D
#define M98095_08E_JACK_DC_SLEW             0x8E
#define M98095_08F_JACK_TEST_CFG            0x8F
#define M98095_090_PWR_EN_IN                0x90
#define M98095_091_PWR_EN_OUT               0x91
#define M98095_092_PWR_EN_OUT               0x92
#define M98095_093_BIAS_CTRL                0x93
#define M98095_094_PWR_DAC_21               0x94
#define M98095_095_PWR_DAC_03               0x95
#define M98095_096_PWR_DAC_CK               0x96
#define M98095_097_PWR_SYS                  0x97

#define M98095_0FF_REV_ID                   0xFF

#define M98095_REG_CNT                      (0xFF+1)
#define M98095_REG_MAX_CACHED               0X97

/* MAX98095 Registers Bit Fields */

/* M98095_007_JACK_AUTO_STS */
	#define M98095_MIC_IN			(1<<3)
	#define M98095_LO_IN			(1<<5)
	#define M98095_HP_IN			(1<<6)
	#define M98095_DDONE			(1<<7)

/* M98095_00F_HOST_CFG */
	#define M98095_SEG                      (1<<0)
	#define M98095_XTEN                     (1<<1)
	#define M98095_MDLLEN                   (1<<2)

/* M98095_013_JACK_INT_EN */
	#define M98095_IMIC_IN			(1<<3)
	#define M98095_ILO_IN			(1<<5)
	#define M98095_IHP_IN			(1<<6)
	#define M98095_IDDONE			(1<<7)

/* M98095_027_DAI1_CLKMODE, M98095_031_DAI2_CLKMODE, M98095_03B_DAI3_CLKMODE */
	#define M98095_CLKMODE_MASK             0xFF

/* M98095_02A_DAI1_FORMAT, M98095_034_DAI2_FORMAT, M98095_03E_DAI3_FORMAT */
	#define M98095_DAI_MAS                  (1<<7)
	#define M98095_DAI_WCI                  (1<<6)
	#define M98095_DAI_BCI                  (1<<5)
	#define M98095_DAI_DLY                  (1<<4)
	#define M98095_DAI_TDM                  (1<<2)
	#define M98095_DAI_FSW                  (1<<1)
	#define M98095_DAI_WS                   (1<<0)

/* M98095_02B_DAI1_CLOCK, M98095_035_DAI2_CLOCK, M98095_03F_DAI3_CLOCK */
	#define M98095_DAI_BSEL64               (1<<0)
	#define M98095_DAI_DOSR_DIV2            (0<<5)
	#define M98095_DAI_DOSR_DIV4            (1<<5)

/* M98095_02C_DAI1_IOCFG, M98095_036_DAI2_IOCFG, M98095_040_DAI3_IOCFG */
	#define M98095_S1NORMAL                 (1<<6)
	#define M98095_S2NORMAL                 (2<<6)
	#define M98095_S3NORMAL                 (3<<6)
	#define M98095_SDATA                    (3<<0)

/* M98095_02E_DAI1_FILTERS, M98095_038_DAI2_FILTERS, M98095_042_DAI3_FILTERS */
	#define M98095_DAI_DHF                  (1<<3)

/* M98095_045_DSP_CFG */
	#define M98095_DSPNORMAL                (5<<4)

/* M98095_048_MIX_DAC_LR */
	#define M98095_DAI1L_TO_DACR            (1<<7)
	#define M98095_DAI1R_TO_DACR            (1<<6)
	#define M98095_DAI2M_TO_DACR            (1<<5)
	#define M98095_DAI1L_TO_DACL            (1<<3)
	#define M98095_DAI1R_TO_DACL            (1<<2)
	#define M98095_DAI2M_TO_DACL            (1<<1)
	#define M98095_DAI3M_TO_DACL            (1<<0)

/* M98095_049_MIX_DAC_M */
	#define M98095_DAI1L_TO_DACM            (1<<3)
	#define M98095_DAI1R_TO_DACM            (1<<2)
	#define M98095_DAI2M_TO_DACM            (1<<1)
	#define M98095_DAI3M_TO_DACM            (1<<0)

/* M98095_04E_MIX_HP_CFG */
	#define M98095_HPNORMAL                 (3<<4)

/* M98095_05F_LVL_MIC1, M98095_060_LVL_MIC2 */
	#define M98095_MICPRE_MASK              (3<<5)
	#define M98095_MICPRE_SHIFT             5

/* M98095_064_LVL_HP_L, M98095_065_LVL_HP_R */
	#define M98095_HP_MUTE                  (1<<7)

/* M98095_066_LVL_RCV */
	#define M98095_REC_MUTE                 (1<<7)

/* M98095_067_LVL_SPK_L, M98095_068_LVL_SPK_R */
	#define M98095_SP_MUTE                  (1<<7)

/* M98095_087_CFG_MIC */
	#define M98095_MICSEL_MASK              (3<<0)
	#define M98095_DIGMIC_L                 (1<<2)
	#define M98095_DIGMIC_R                 (1<<3)
	#define M98095_DIGMIC2L                 (1<<4)
	#define M98095_DIGMIC2R                 (1<<5)

/* M98095_088_CFG_LEVEL */
	#define M98095_VSEN                     (1<<6)
	#define M98095_ZDEN                     (1<<5)
	#define M98095_BQ2EN                    (1<<3)
	#define M98095_BQ1EN                    (1<<2)
	#define M98095_EQ2EN                    (1<<1)
	#define M98095_EQ1EN                    (1<<0)

/* M98095_089_JACK_DET_AUTO */
	#define M98095_PIN5EN			(1<<2)
	#define M98095_JDEN			(1<<7)

/* M98095_090_PWR_EN_IN */
	#define M98095_INEN                     (1<<7)
	#define M98095_MB2EN                    (1<<3)
	#define M98095_MB1EN                    (1<<2)
	#define M98095_MBEN                     (3<<2)
	#define M98095_ADREN                    (1<<1)
	#define M98095_ADLEN                    (1<<0)

/* M98095_091_PWR_EN_OUT */
	#define M98095_HPLEN                    (1<<7)
	#define M98095_HPREN                    (1<<6)
	#define M98095_SPLEN                    (1<<5)
	#define M98095_SPREN                    (1<<4)
	#define M98095_RECEN                    (1<<3)
	#define M98095_DALEN                    (1<<1)
	#define M98095_DAREN                    (1<<0)

/* M98095_092_PWR_EN_OUT */
	#define M98095_SPK_FIXEDSPECTRUM        (0<<4)
	#define M98095_SPK_SPREADSPECTRUM       (1<<4)

/* M98095_097_PWR_SYS */
	#define M98095_SHDNRUN                  (1<<7)
	#define M98095_PERFMODE                 (1<<3)
	#define M98095_HPPLYBACK                (1<<2)
	#define M98095_PWRSV8K                  (1<<1)
	#define M98095_PWRSV                    (1<<0)

#define M98095_COEFS_PER_BAND            5

#define M98095_BYTE1(w) ((w >> 8) & 0xff)
#define M98095_BYTE0(w) (w & 0xff)

/* Equalizer filter coefficients */
#define M98095_110_DAI1_EQ_BASE             0x10
#define M98095_142_DAI2_EQ_BASE             0x42

/* Biquad filter coefficients */
#define M98095_174_DAI1_BQ_BASE             0x74
#define M98095_17E_DAI2_BQ_BASE             0x7E

/* Default Delay used in Slew Rate Calculation for Jack detection */
#define M98095_DEFAULT_SLEW_DELAY		0x18

extern int max98095_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hp_jack, struct snd_soc_jack *mic_jack);

#endif
