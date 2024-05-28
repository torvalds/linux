/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Intel Corporation, 2022
 * Copyright Everest Semiconductor Co.,Ltd
 */

/* Definitions extracted from ASL file provided at
 * https://github.com/thesofproject/linux/files/9398723/ESSX8326.zip
 */

#ifndef _ES83XX_DSM_COMMON_H
#define _ES83XX_DSM_COMMON_H

/***************************************************
 *  DSM arguments                                  *
 ***************************************************/

#define PLATFORM_MAINMIC_TYPE_ARG		0x00
#define PLATFORM_HPMIC_TYPE_ARG			0x01
#define PLATFORM_SPK_TYPE_ARG			0x02
#define PLATFORM_HPDET_INV_ARG			0x03
#define PLATFORM_PCM_TYPE_ARG			0x04

#define PLATFORM_MIC_DE_POP_ARG			0x06
#define PLATFORM_CODEC_TYPE_ARG			0x0E
#define PLATFORM_BUS_SLOT_ARG			0x0F

#define HP_CODEC_LINEIN_PGA_GAIN_ARG		0x10
#define MAIN_CODEC_LINEIN_PGA_GAIN_ARG		0x20

#define HP_CODEC_D2SEPGA_GAIN_ARG		0x11
#define MAIN_CODEC_D2SEPGA_GAIN_ARG		0x21

#define HP_CODEC_ADC_VOLUME_ARG			0x12
#define MAIN_CODEC_ADC_VOLUME_ARG		0x22

#define HP_CODEC_ADC_ALC_ENABLE_ARG		0x13
#define MAIN_CODEC_ADC_ALC_ENABLE_ARG		0x23

#define HP_CODEC_ADC_ALC_TARGET_LEVEL_ARG	0x14
#define MAIN_CODEC_ADC_ALC_TARGET_LEVEL_ARG	0x24

#define HP_CODEC_ADC_ALC_MAXGAIN_ARG		0x15
#define MAIN_CODEC_ADC_ALC_MAXGAIN_ARG		0x25

#define HP_CODEC_ADC_ALC_MINGAIN_ARG		0x16
#define MAIN_CODEC_ADC_ALC_MINGAIN_ARG		0x26

#define HP_CODEC_ADC_ALC_HLDTIME_ARG		0x17
#define MAIN_CODEC_ADC_ALC_HLDTIME_ARG		0x27

#define HP_CODEC_ADC_ALC_DCYTIME_ARG		0x18
#define MAIN_CODEC_ADC_ALC_DCYTIME_ARG		0x28

#define HP_CODEC_ADC_ALC_ATKTIME_ARG		0x19
#define MAIN_CODEC_ADC_ALC_ATKTIME_ARG		0x29

#define HP_CODEC_ADC_ALC_NGTYPE_ARG		0x1a
#define MAIN_CODEC_ADC_ALC_NGTYPE_ARG		0x2a

#define HP_CODEC_ADC_ALC_NGTHLD_ARG		0x1b
#define MAIN_CODEC_ADC_ALC_NGTHLD_ARG		0x2b

#define MAIN_CODEC_ADC_GUI_STEP_ARG		0x2c
#define MAIN_CODEC_ADC_GUI_GAIN_RANGE_ARG	0x2c

#define HEADPHONE_DUMMY_REMOVE_ENABLE_ARG	0x2e

#define HP_CODEC_DAC_HPMIX_HIGAIN_ARG		0x40
#define SPK_CODEC_DAC_HPMIX_HIGAIN_ARG		0x50

#define HP_CODEC_DAC_HPMIX_VOLUME_ARG		0x41
#define SPK_CODEC_DAC_HPMIX_VOLUME_ARG		0x51

#define HP_CODEC_DAC_HPOUT_VOLUME_ARG		0x42
#define SPK_CODEC_DAC_HPOUT_VOLUME_ARG		0x52

#define HP_CODEC_LDAC_VOLUME_ARG		0x44
#define HP_CODEC_RDAC_VOLUME_ARG		0x54

#define SPK_CODEC_LDAC_VOLUME_ARG		0x45
#define SPK_CODEC_RDAC_VOLUME_ARG		0x55

#define HP_CODEC_DAC_AUTOMUTE_ARG		0x46
#define SPK_CODEC_DAC_AUTOMUTE_ARG		0x56

#define HP_CODEC_DAC_MONO_ARG			0x4A
#define SPK_CODEC_DAC_MONO_ARG			0x5A

#define HP_CTL_IO_LEVEL_ARG			0x4B
#define SPK_CTL_IO_LEVEL_ARG			0x5B

#define CODEC_GPIO0_FUNC_ARG			0x80
#define CODEC_GPIO1_FUNC_ARG			0x81
#define CODEC_GPIO2_FUNC_ARG			0x82
#define CODEC_GPIO3_FUNC_ARG			0x83
#define CODEC_GPIO4_FUNC_ARG			0x84

#define PLATFORM_MCLK_LRCK_FREQ_ARG		0x85

/***************************************************
 *  Values for arguments                           *
 ***************************************************/

/* Main and HP Mic */
#define PLATFORM_MIC_DMIC_HIGH_LEVEL		0xAA
#define PLATFORM_MIC_DMIC_LOW_LEVEL		0x55
#define PLATFORM_MIC_AMIC_LIN1RIN1		0xBB
#define PLATFORM_MIC_AMIC_LIN2RIN2		0xCC

/* Speaker */
#define PLATFORM_SPK_NONE			0x00
#define PLATFORM_SPK_MONO			0x01
#define PLATFORM_SPK_STEREO			0x02

/* Jack Detection */
#define PLATFORM_HPDET_NORMAL			0x00
#define PLATFORM_HPDET_INVERTED			0x01

/* PCM type (Port number + protocol) */
/*
 * RETURNED VALUE = 0x00,   PCM PORT0, I2S
 *       0x01,   PCM PORT0, LJ
 *       0x02,   PCM PORT0, RJ
 *       0x03,   PCM PORT0, DSP-A
 *       0x04,   PCM PORT0, DSP-B
 *       0x10,   PCM PORT1, I2S
 *       0x11,   PCM PORT1, LJ
 *       0x12,   PCM PORT1, RJ
 *       0x13,   PCM PORT1, DSP-A
 *       0x14,   PCM PORT1, DSP-B
 *       0xFF,   Use default
 *
 * This is not used in Linux (defined by topology) and in
 * Windows it's always DSP-A
 */

/* Depop */
#define PLATFORM_MIC_DE_POP_OFF			0x00
#define PLATFORM_MIC_DE_POP_ON			0x01

/* Codec type */
#define PLATFORM_CODEC_8316			16
#define PLATFORM_CODEC_8326			26
#define PLATFORM_CODEC_8336			36
#define PLATFORM_CODEC_8395			95
#define PLATFORM_CODEC_8396			96

/* Bus slot (on the host) */
/* BIT[3:0] FOR BUS NUMBER, BIT[7:4] FOR SLOT NUMBER
 * BIT[3:0] 0 for I2S0, 1 for IS21, 2 for I2S2.
 *
 * On Intel platforms this refers to SSP0..2. This information
 * is not really useful for Linux, the information is already
 * inferred from NHLT but can be used to double-check NHLT
 */

/* Volume - Gain */
#define LINEIN_GAIN_0db				0x00 /* gain =  0db */
#define LINEIN_GAIN_3db				0x01 /* gain = +3db */
#define LINEIN_GAIN_6db				0x02 /* gain = +6db */
#define LINEIN_GAIN_9db				0x03 /* gain = +9db */
#define LINEIN_GAIN_12db			0x04 /* gain = +12db */
#define LINEIN_GAIN_15db			0x05 /* gain = +15db */
#define LINEIN_GAIN_18db			0x06 /* gain = +18db */
#define LINEIN_GAIN_21db			0x07 /* gain = +21db */
#define LINEIN_GAIN_24db			0x08 /* gain = +24db */
#define LINEIN_GAIN_27db			0x09 /* gain = +27db */
#define LINEIN_GAIN_30db			0x0a /* gain = +30db */

#define ADC_GUI_STEP_3db			0x03 /* gain = +3db */
#define ADC_GUI_STEP_6db			0x06 /* gain = +6db */
#define ADC_GUI_STEP_10db			0x0a /* gain = +10db */

#define D2SEPGA_GAIN_0db			0x00 /* gain =   0db */
#define D2SEPGA_GAIN_15db			0x01 /* gain = +15db */

/* ADC volume: base = 0db, -0.5db/setp, 0xc0 <-> -96db */

#define ADC_ALC_DISABLE				0x00
#define ADC_ALC_ENABLE				0x01

#define ADC_ALC_TARGET_LEVEL_m16_5db		0x00 /* gain = -16.5db */
#define ADC_ALC_TARGET_LEVEL_m15db		0x01 /* gain = -15db */
#define ADC_ALC_TARGET_LEVEL_m13_5db		0x02 /* gain = -13.5db */
#define ADC_ALC_TARGET_LEVEL_m12db		0x03 /* gain = -12db */
#define ADC_ALC_TARGET_LEVEL_m10_5db		0x04 /* gain = -10.5db */
#define ADC_ALC_TARGET_LEVEL_m9db		0x05 /* gain = -9db */
#define ADC_ALC_TARGET_LEVEL_m7_5db		0x06 /* gain = -7.5db */
#define ADC_ALC_TARGET_LEVEL_m6db		0x07 /* gain = -6db */
#define ADC_ALC_TARGET_LEVEL_m4_5db		0x08 /* gain = -4.5db */
#define ADC_ALC_TARGET_LEVEL_m_3db		0x09 /* gain = -3db */
#define ADC_ALC_TARGET_LEVEL_m1_5db		0x0a /* gain = -1.5db */

#define ADC_ALC_MAXGAIN_m6_5db			0x00  /* gain = -6.5db */
#define ADC_ALC_MAXGAIN_m5db			0x01  /* gain = -5db */
#define ADC_ALC_MAXGAIN_m3_5db			0x02  /* gain = -3.5db */
#define ADC_ALC_MAXGAIN_m2db			0x03  /* gain = -2db */
#define ADC_ALC_MAXGAIN_m0_5db			0x04  /* gain = -0.5db */
#define ADC_ALC_MAXGAIN_1db			0x05  /* gain = +1db */
#define ADC_ALC_MAXGAIN_2_5db			0x06  /* gain = +2.5db */
#define ADC_ALC_MAXGAIN_4db			0x07  /* gain = +4db */
#define ADC_ALC_MAXGAIN_5_5db			0x08  /* gain = +5.5db */
#define ADC_ALC_MAXGAIN_7db			0x09  /* gain = +7db */
#define ADC_ALC_MAXGAIN_8_5db			0x0a  /* gain = +8.5db */
#define ADC_ALC_MAXGAIN_10db			0x0b  /* gain = +10db */
#define ADC_ALC_MAXGAIN_11_5db			0x0c  /* gain = +11.5db */
#define ADC_ALC_MAXGAIN_13db			0x0d  /* gain = +13db */
#define ADC_ALC_MAXGAIN_14_5db			0x0e  /* gain = +14.5db */
#define ADC_ALC_MAXGAIN_16db			0x0f  /* gain = +16db */
#define ADC_ALC_MAXGAIN_17_5db			0x10  /* gain = +17.5db */
#define ADC_ALC_MAXGAIN_19db			0x11  /* gain = +19db */
#define ADC_ALC_MAXGAIN_20_5db			0x12  /* gain = +20.5db */
#define ADC_ALC_MAXGAIN_22db			0x13  /* gain = +22db */
#define ADC_ALC_MAXGAIN_23_5db			0x14  /* gain = +23.5db */
#define ADC_ALC_MAXGAIN_25db			0x15  /* gain = +25db */
#define ADC_ALC_MAXGAIN_26_5db			0x16  /* gain = +26.5db */
#define ADC_ALC_MAXGAIN_28db			0x17  /* gain = +28db */
#define ADC_ALC_MAXGAIN_29_5db			0x18  /* gain = +29.5db */
#define ADC_ALC_MAXGAIN_31db			0x19  /* gain = +31db */
#define ADC_ALC_MAXGAIN_32_5db			0x1a  /* gain = +32.5db */
#define ADC_ALC_MAXGAIN_34db			0x1b  /* gain = +34db */
#define ADC_ALC_MAXGAIN_35_5db			0x1c  /* gain = +35.5db */

#define ADC_ALC_MINGAIN_m12db			0x00 /* gain = -12db */
#define ADC_ALC_MINGAIN_m10_5db			0x01 /* gain = -10.5db */
#define ADC_ALC_MINGAIN_m9db			0x02 /* gain = -9db */
#define ADC_ALC_MINGAIN_m7_5db			0x03 /* gain = -7.5db */
#define ADC_ALC_MINGAIN_m6db			0x04 /* gain = -6db */
#define ADC_ALC_MINGAIN_m4_51db			0x05 /* gain = -4.51db */
#define ADC_ALC_MINGAIN_m3db			0x06 /* gain = -3db */
#define ADC_ALC_MINGAIN_m1_5db			0x07 /* gain = -1.5db */
#define ADC_ALC_MINGAIN_0db			0x08 /* gain = 0db */
#define ADC_ALC_MINGAIN_1_5db			0x09 /* gain = +1.5db */
#define ADC_ALC_MINGAIN_3db			0x0a /* gain = +3db */
#define ADC_ALC_MINGAIN_4_5db			0x0b /* gain = +4.5db */
#define ADC_ALC_MINGAIN_6db			0x0c /* gain = +6db */
#define ADC_ALC_MINGAIN_7_5db			0x0d /* gain = +7.5db */
#define ADC_ALC_MINGAIN_9db			0x0e /* gain = +9db */
#define ADC_ALC_MINGAIN_10_5db			0x0f /* gain = +10.5db */
#define ADC_ALC_MINGAIN_12db			0x10 /* gain = +12db */
#define ADC_ALC_MINGAIN_13_5db			0x11 /* gain = +13.5db */
#define ADC_ALC_MINGAIN_15db			0x12 /* gain = +15db */
#define ADC_ALC_MINGAIN_16_5db			0x13 /* gain = +16.5db */
#define ADC_ALC_MINGAIN_18db			0x14 /* gain = +18db */
#define ADC_ALC_MINGAIN_19_5db			0x15 /* gain = +19.5db */
#define ADC_ALC_MINGAIN_21db			0x16 /* gain = +21db */
#define ADC_ALC_MINGAIN_22_5db			0x17 /* gain = +22.5db */
#define ADC_ALC_MINGAIN_24db			0x18 /* gain = +24db */
#define ADC_ALC_MINGAIN_25_5db			0x19 /* gain = +25.5db */
#define ADC_ALC_MINGAIN_27db			0x1a /* gain = +27db */
#define ADC_ALC_MINGAIN_28_5db			0x1b /* gain = +28.5db */
#define ADC_ALC_MINGAIN_30db			0x1c /* gain = +30db */

/* ADC volume: step 1dB */

/* ALC Hold, Decay, Attack */
#define ADC_ALC_HLDTIME_0_US			0x00
#define ADC_ALC_HLDTIME_0000266_US		0x01 //time = 2.67ms
#define ADC_ALC_HLDTIME_0000533_US		0x02 //time = 5.33ms
#define ADC_ALC_HLDTIME_0001066_US		0x03 //time = 10.66ms
#define ADC_ALC_HLDTIME_0002132_US		0x04 //time = 21.32ms
#define ADC_ALC_HLDTIME_0004264_US		0x05 //time = 42.64ms
#define ADC_ALC_HLDTIME_0008538_US		0x06 //time = 85.38ms
#define ADC_ALC_HLDTIME_0017076_US		0x07 //time = 170.76ms
#define ADC_ALC_HLDTIME_0034152_US		0x08 //time = 341.52ms
#define ADC_ALC_HLDTIME_0680000_US		0x09 //time = 0.68s
#define ADC_ALC_HLDTIME_1360000_US		0x0a //time = 1.36s

#define ADC_ALC_DCYTIME_000410_US		0x00 //time = 410us
#define ADC_ALC_DCYTIME_000820_US		0x01 //time = 820us
#define ADC_ALC_DCYTIME_001640_US		0x02 //time = 1.64ms
#define ADC_ALC_DCYTIME_003280_US		0x03 //time = 3.28ms
#define ADC_ALC_DCYTIME_006560_US		0x04 //time = 6.56ms
#define ADC_ALC_DCYTIME_013120_US		0x05 //time = 13.12ms
#define ADC_ALC_DCYTIME_026240_US		0x06 //time = 26.24ms
#define ADC_ALC_DCYTIME_058480_US		0x07 //time = 52.48ms
#define ADC_ALC_DCYTIME_104960_US		0x08 //time = 104.96ms
#define ADC_ALC_DCYTIME_209920_US		0x09 //time = 209.92ms
#define ADC_ALC_DCYTIME_420000_US		0x0a //time = 420ms

#define ADC_ALC_ATKTIME_000104_US		0x00 //time = 104us
#define ADC_ALC_ATKTIME_000208_US		0x01 //time = 208us
#define ADC_ALC_ATKTIME_000416_US		0x02 //time = 416ms
#define ADC_ALC_ATKTIME_003832_US		0x03 //time = 832ms
#define ADC_ALC_ATKTIME_001664_US		0x04 //time = 1.664ms
#define ADC_ALC_ATKTIME_003328_US		0x05 //time = 3.328ms
#define ADC_ALC_ATKTIME_006656_US		0x06 //time = 6.656ms
#define ADC_ALC_ATKTIME_013312_US		0x07 //time = 13.312ms
#define ADC_ALC_ATKTIME_026624_US		0x08 //time = 26.624ms
#define ADC_ALC_ATKTIME_053248_US		0x09 //time = 53.248ms
#define ADC_ALC_ATKTIME_106496_US		0x0a //time = 106.496ms

/* ALC Noise Gate */
#define ADC_ALC_NGTYPE_DISABLE			0x00 //noise gate disable
#define ADC_ALC_NGTYPE_ENABLE_HOLD		0x01 //noise gate enable, hold gain type
#define ADC_ALC_NGTYPE_ENABLE_MUTE		0x03 //noise gate enable, mute type

#define ADC_ALC_NGTHLD_m76_5db			0x00 /* Threshold = -76.5db */
#define ADC_ALC_NGTHLD_m75db			0x01 /* Threshold = -75db   */
#define ADC_ALC_NGTHLD_m73_5db			0x02 /* Threshold = -73.5db */
#define ADC_ALC_NGTHLD_m72db			0x03 /* Threshold = -72db   */
#define ADC_ALC_NGTHLD_m70_5db			0x04 /* Threshold = -70.5db */
#define ADC_ALC_NGTHLD_m69db			0x05 /* Threshold = -69db   */
#define ADC_ALC_NGTHLD_m67_5db			0x06 /* Threshold = -67.5db */
#define ADC_ALC_NGTHLD_m66db			0x07 /* Threshold = -66db   */
#define ADC_ALC_NGTHLD_m64_5db			0x08 /* Threshold = -64.5db */
#define ADC_ALC_NGTHLD_m63db			0x09 /* Threshold = -63db   */
#define ADC_ALC_NGTHLD_m61_5db			0x0a /* Threshold = -61.5db */
#define ADC_ALC_NGTHLD_m60db			0x0b /* Threshold = -60db   */
#define ADC_ALC_NGTHLD_m58_5db			0x0c /* Threshold = -58.5db */
#define ADC_ALC_NGTHLD_m57db			0x0d /* Threshold = -57db   */
#define ADC_ALC_NGTHLD_m55_5db			0x0e /* Threshold = -55.5db */
#define ADC_ALC_NGTHLD_m54db			0x0f /* Threshold = -54db   */
#define ADC_ALC_NGTHLD_m52_5db			0x10 /* Threshold = -52.5db */
#define ADC_ALC_NGTHLD_m51db			0x11 /* Threshold = -51db   */
#define ADC_ALC_NGTHLD_m49_5db			0x12 /* Threshold = -49.5db */
#define ADC_ALC_NGTHLD_m48db			0x13 /* Threshold = -48db   */
#define ADC_ALC_NGTHLD_m46_5db			0x14 /* Threshold = -46.5db */
#define ADC_ALC_NGTHLD_m45db			0x15 /* Threshold = -45db   */
#define ADC_ALC_NGTHLD_m43_5db			0x16 /* Threshold = -43.5db */
#define ADC_ALC_NGTHLD_m42db			0x17 /* Threshold = -42db   */
#define ADC_ALC_NGTHLD_m40_5db			0x18 /* Threshold = -40.5db */
#define ADC_ALC_NGTHLD_m39db			0x19 /* Threshold = -39db   */
#define ADC_ALC_NGTHLD_m37_5db			0x1a /* Threshold = -37.5db */
#define ADC_ALC_NGTHLD_m36db			0x1b /* Threshold = -36db   */
#define ADC_ALC_NGTHLD_m34_5db			0x1c /* Threshold = -34.5db */
#define ADC_ALC_NGTHLD_m33db			0x1d /* Threshold = -33db   */
#define ADC_ALC_NGTHLD_m31_5db			0x1e /* Threshold = -31.5db */
#define ADC_ALC_NGTHLD_m30db			0x1f /* Threshold = -30db   */

/* Headphone dummy - Windows Specific flag, not needed for Linux */

/* HPMIX HIGAIN and VOLUME */
#define DAC_HPMIX_HIGAIN_0db			0x00 /* gain =  0db      */
#define DAC_HPMIX_HIGAIN_m6db			0x88 /* gain = -6db      */

#define DAC_HPMIX_VOLUME_m12db			0x00 /* volume = -12db   */
#define DAC_HPMIX_VOLUME_m10_5db		0x11 /* volume = -10.5db */
#define DAC_HPMIX_VOLUME_m9db			0x22 /* volume = -9db    */
#define DAC_HPMIX_VOLUME_m7_5db			0x33 /* volume = -7.5db  */
#define DAC_HPMIX_VOLUME_m6db			0x44 /* volume = -6db    */
#define DAC_HPMIX_VOLUME_m4_5db			0x88 /* volume = -4.5db  */
#define DAC_HPMIX_VOLUME_m3db			0x99 /* volume = -3db    */
#define DAC_HPMIX_VOLUME_m1_5db			0xaa /* volume = -1.5db  */
#define DAC_HPMIX_VOLUME_0db			0xbb /* volume =  0db    */

/* HPOUT VOLUME */
#define DAC_HPOUT_VOLUME_0db			0x00 /* volume =   0db   */
#define DAC_HPOUT_VOLUME_m12db			0x11 /* volume = -12db   */
#define DAC_HPOUT_VOLUME_m24db			0x22 /* volume = -24db   */
#define DAC_HPOUT_VOLUME_m48db			0x33 /* volume = -48db   */

/* LDAC/RDAC volume = 0db, -0.5db/setp, 0xc0 <-> -96db */

/* Automute */
#define DAC_AUTOMUTE_NONE			0x00 /* no automute  */
#define DAC_AUTOMUTE_DIGITAL			0x01 /* digital mute */
#define DAC_AUTOMUTE_ANALOG			0x02 /* analog mute  */

/* Mono - Windows specific, on Linux the information comes from DAI/topology */
#define HEADPHONE_MONO                          0x01 /* on channel */
#define HEADPHONE_STEREO                        0x00 /* stereo */

/* Speaker and headphone GPIO control */
#define GPIO_CTL_IO_LEVEL_LOW			0x00 /* low level enable */
#define GPIO_CTL_IO_LEVEL_HIGH			0x01 /* high level enable */

/* GPIO */
/* FIXME: for ES8396, no need to use */

/* Platform clocks */
/*
 * BCLK AND MCLK FREQ
 * BIT[7:4] MCLK FREQ
 * 0 - 19.2MHz
 * 1 - 24MHz
 * 2 - 12.288MHz
 * F - Default for 19.2MHz
 *
 * BIT[3:0] BCLK FREQ
 * 0 - 4.8MHz
 * 1 - 2.4MHz
 * 2 - 2.304MHz
 * 3 - 3.072MHz
 * 4 - 4.096MHz
 * F - Default for 4.8MHz
 */

int es83xx_dsm(struct device *dev, int arg, int *value);
int es83xx_dsm_dump(struct device *dev);

#endif
