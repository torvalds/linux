/*
 * max98090.h -- MAX98090 ALSA SoC Audio driver
 *
 * Copyright 2011 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MAX98090_H
#define _MAX98090_H

/*
 * MAX98090 Registers Definition
 */

#define M98090_000_SW_RESET     			0x00

// Status/Interrupt
#define M98090_001_INT_STS   	         	0x01
#define M98090_002_JACK_STS             	0x02
#define M98090_003_INT_MASK    				0x03

// Quick Setup
#define M98090_004_SYS_CLK_QS               0x04
#define M98090_005_SAMPLERATE_QS            0x05
#define M98090_006_IF_QS		            0x06
#define M98090_007_DAC_PATH_QS	            0x07
#define M98090_008_MIC_TO_ADC_QS	        0x08
#define M98090_009_LINE_TO_QS	            0x09
#define M98090_00A_ANALOG_MIC_LOOP_QS       0x0A
#define M98090_00B_ANALOG_LINE_LOOP_QS      0x0B

// Digital Microphone
#define M98090_00C_DIGITAL_MIC 		        0x0C

// Input Configuration
#define M98090_00D_INPUT_CONFIG	            0x0D
#define M98090_00E_LINEIN_LVL		        0x0E
#define M98090_00F_LINIIN_CONFIG            0x0F
#define M98090_010_MIC1_LVL		            0x10
#define M98090_011_MIC2_LVL		            0x11
#define M98090_012_MIC_BIAS_VOL             0x12

#define M98090_013_RESERVED                 0x13
#define M98090_014_RESERVED                 0x14

// ADC Path
#define M98090_015_MIX_ADC_L                0x15
#define M98090_016_MIX_ADC_R	            0x16
#define M98090_017_ADC_L_LVL                0x17
#define M98090_018_ADC_R_LVL                0x18
#define M98090_019_ADC_BIQUAD_LVL           0x19
#define M98090_01A_SIDETONE                 0x1A

// Clock control
#define M98090_01B_SYS_CLK	                0x1B
#define M98090_01C_CLK_MODE                 0x1C
#define M98090_01D_ANYCLK_CNTL1	            0x1D
#define M98090_01E_ANYCLK_CNTL2             0x1E
#define M98090_01F_ANYCLK_CNTL3             0x1F
#define M98090_020_ANYCLK_CNTL4             0x20
#define M98090_021_MASTER_MODE_CLK          0x21

// Interface control
#define M98090_022_DAI_IF_FORMAT		    0x22
#define M98090_023_DAI_TDM_FORMAT1          0x23
#define M98090_024_DAI_TDM_FORMAT2          0x24
#define M98090_025_DAI_IOCFG	            0x25
#define M98090_026_FILTER_CONFIG            0x26
#define M98090_027_DAI_PLAYBACK_LVL1        0x27
#define M98090_028_DAI_PLAYBACK_LVL2        0x28

// Headphone Configuration
#define M98090_029_MIX_HP_L			        0x29
#define M98090_02A_MIX_HP_R			        0x2A
#define M98090_02B_MIX_HP_CNTL	            0x2B
#define M98090_02C_HP_L_VOL	                0x2C
#define M98090_02D_HP_R_VOL                 0x2D

// Speaker Configuration
#define M98090_02E_MIX_SPK_L		        0x2E
#define M98090_02F_MIX_SPK_R	            0x2F
#define M98090_030_MIX_SPK_CNTL	            0x30
#define M98090_031_SPK_L_VOL	            0x31
#define M98090_032_SPK_R_VOL	            0x32

// ALC Configuration
#define M98090_033_ALC_TIMING	            0x33
#define M98090_034_ALC_COMPRESSOR           0x34
#define M98090_035_ALC_EXPANDER	            0x35
#define M98090_036_ALC_GAIN		            0x36

// LINE_OUTPUT
#define M98090_037_LOUT_L_MIX               0x37
#define M98090_038_LOUT_L_CNTL              0x38
#define M98090_039_LOUT_L_VOL               0x39
#define M98090_03A_LOUT_R_MIX  		        0x3A
#define M98090_03B_LOUT_R_CNTL 		        0x3B
#define M98090_03C_LOUT_R_VOL  		        0x3C

// Enable
#define M98090_03D_JACK_DETECT_CONFIG       0x3D
#define M98090_03E_IPUT_ENABLE		        0x3E
#define M98090_03F_OUTPUT_ENABLE            0x3F
#define M98090_040_LVL_CNTL		            0x40
#define M98090_041_FILTER_ENABLE            0x41

// Power
#define M98090_042_BIAS_CNTL	            0x42
#define M98090_043_DAC_POWER                0x43
#define M98090_044_ADC_POWER                0x44
#define M98090_045_PWR_SYS                 	0x45


#define M98090_0FF_REV_ID                   0xFF

#define M98090_REG_CNT                      (0xFF+1)
#define M98090_REG_MAX_CACHED               0x45

/* MAX98090 Registers Bit Fields */

/* M98090_000_SW_RESET */
	#define M98090_SWRST                   (1<<7)

/* M98090_004_SYS_CLK_QS */
	#define M98090_QS_MCLK_26M			    (1<<7)
	#define M98090_QS_MCLK_19P2M		    (1<<6)
	#define M98090_QS_MCLK_13M			    (1<<5)
	#define M98090_QS_MCLK_12P288M		    (1<<4)
	#define M98090_QS_MCLK_12M			    (1<<3)
	#define M98090_QS_MCLK_11P2896M	        (1<<2)
	#define M98090_QS_MCLK_256FS		    (1<<0)
	#define M98090_QS_MCLK_MASK		         0xFD

/* M98090_005_SAMPLERATE_QS */
	#define M98090_QS_SR_96K		        (1<<5)
	#define M98090_QS_SR_32K		        (1<<4)
	#define M98090_QS_SR_48K		        (1<<3)
	#define M98090_QS_SR_44K1		        (1<<2)
	#define M98090_QS_SR_16K		        (1<<1)
	#define M98090_QS_SR_8K		            (1<<0)
	#define M98090_QS_SR_MASK          0x3F

/* M98090_006_IF_QS */
	#define M98090_QS_DAI_RJ_MAS            (1<<5)
	#define M98090_QS_DAI_RJ_SLV            (1<<4)
	#define M98090_QS_DAI_LJ_MAS            (1<<3)
	#define M98090_QS_DAI_LJ_SLV            (1<<2)
	#define M98090_QS_DAI_I2S_MAS           (1<<1)
	#define M98090_QS_DAI_I2S_SLV           (1<<0)
	#define M98090_QS_DAI_MASK		         0x3F


/* M98090_015_MIX_ADC_L */
	#define M98090_IN12_TO_BYPASSL           (1<<7)
	#define M98090_MIC2_TO_ADCL              (1<<6)
	#define M98090_MIC1_TO_ADCL              (1<<5)
	#define M98090_LINE2_TO_ADCL             (1<<4)
	#define M98090_LINE1_TO_ADCL             (1<<3)
	#define M98090_IN56_TO_ADCL              (1<<2)
	#define M98090_IN34_TO_ADCL              (1<<1)
	#define M98090_IN12_TO_ADCL              (1<<0)

/* M98090_016_MIX_ADC_R */
	#define M98090_IN12_TO_BYPASSR           (1<<7)
	#define M98090_MIC2_TO_ADCR              (1<<6)
	#define M98090_MIC1_TO_ADCR              (1<<5)
	#define M98090_LINE2_TO_ADCR             (1<<4)
	#define M98090_LINE1_TO_ADCR             (1<<3)
	#define M98090_IN56_TO_ADCR              (1<<2)
	#define M98090_IN34_TO_ADCR              (1<<1)
	#define M98090_IN12_TO_ADCR              (1<<0)


/* M98090_007_DAC_PATH_QS */
/* M98090_008_MIC_TO_ADC_QS */
/* M98090_009_LINE_TO_QS */
/* M98090_00A_ANALOG_MIC_LOOP_QS */
/* M98090_00B_ANALOG_LINE_LOOP_QS */

/* M98090_021_MASTER_MODE_CLK */
	#define M98090_DAI_MAS                  (1<<7)

/* M98090_022_DAI_IF_FORMAT */
	#define M98090_DAI_RJ                   (1<<5)
	#define M98090_DAI_WCI                  (1<<4)
	#define M98090_DAI_BCI                  (1<<3)
	#define M98090_DAI_DLY                  (1<<2)
	#define M98090_DAI_WS                   (3<<0)

/* M98090_023_DAI_TDM_FORMAT1 */

/* M98090_024_DAI_TDM_FORMAT2 */

/* M98090_026_FILTER_CONFIG */
	#define M98090_FILTER_MODE              (1<<7)
	#define M98090_FILTER_AHPF              (1<<6)
	#define M98090_FILTER_DHPF              (1<<5)
	#define M98090_FILTER_DHF               (1<<4)

/* M98090_029_MIX_HP_L */
	#define M98090_MIC2_TO_HPL              (1<<5)
	#define M98090_MIC1_TO_HPL              (1<<4)
	#define M98090_IN2_TO_HPL               (1<<3)
	#define M98090_IN1_TO_HPL               (1<<2)
	#define M98090_DACR_TO_HPL              (1<<1)
	#define M98090_DACL_TO_HPL              (1<<0)

/* M98090_02A_MIX_HP_R */
	#define M98090_MIC2_TO_HPR              (1<<5)
	#define M98090_MIC1_TO_HPR              (1<<4)
	#define M98090_IN2_TO_HPR               (1<<3)
	#define M98090_IN1_TO_HPR               (1<<2)
	#define M98090_DACR_TO_HPR              (1<<1)
	#define M98090_DACL_TO_HPR              (1<<0)

/* M98090_02B_MIX_HP_CNTL */
	#define M98090_HPNORMAL                 (3<<4)

/* M98090_02E_MIX_SPK_L */
	#define M98090_MIC2_TO_SPKL            (1<<5)
	#define M98090_MIC1_TO_SPKL            (1<<4)
	#define M98090_IN2_TO_SPKL             (1<<3)
	#define M98090_IN1_TO_SPKL             (1<<2)
	#define M98090_DACR_TO_SPKL            (1<<1)
	#define M98090_DACL_TO_SPKL            (1<<0)

/* M98090_02F_MIX_SPK_R */
	#define M98090_MIC2_TO_SPKR            (1<<5)
	#define M98090_MIC1_TO_SPKR            (1<<4)
	#define M98090_IN2_TO_SPKR             (1<<3)
	#define M98090_IN1_TO_SPKR             (1<<2)
	#define M98090_DACR_TO_SPKR            (1<<1)
	#define M98090_DACL_TO_SPKR            (1<<0)

/* M98090_03E_IPUT_ENABLE */
	#define M98090_MBEN 	                (1<<4)
	#define M98090_LINEAEN                  (1<<3)
	#define M98090_LINEBEN                  (1<<2)
	#define M98090_ADREN                    (1<<1)
	#define M98090_ADLEN                    (1<<0)

/* M98090_03F_OUTPUT_ENABLE */
	#define M98090_HPREN                    (1<<7)
	#define M98090_HPLEN                    (1<<6)
	#define M98090_SPREN                    (1<<5)
	#define M98090_SPLEN                    (1<<4)
	#define M98090_RCVLEN                   (1<<3)
	#define M98090_RCVREN                   (1<<2)
	#define M98090_DAREN                    (1<<1)
	#define M98090_DALEN                    (1<<0)

/* M98090_045_PWR_SYS */
	#define M98090_SHDNRUN                  (1<<7)

#define M98090_COEFS_PER_BAND            5

#define M98090_BYTE1(w) ((w >> 8) & 0xff)
#define M98090_BYTE0(w) (w & 0xff)

/* Equalizer filter coefficients */
#define M98090_110_DAI1_EQ_BASE             0x10
#define M98090_142_DAI2_EQ_BASE             0x42

/* Biquad filter coefficients */
#define M98090_174_DAI1_BQ_BASE             0x74
#define M98090_17E_DAI2_BQ_BASE             0x7E

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
enum playback_path {OFF, REV, SPK, HP, SPK_HP, TV_OUT};
enum record_path   {MAIN, EAR, MIC_OFF};

struct max98090_cdata {
	unsigned int rate;
	unsigned int fmt;
	int eq_sel;
	int bq_sel;
};

struct max98090_priv {
	struct snd_soc_codec *codec;
	void *control_data;
	struct max98090_pdata *pdata;
	unsigned int sysclk;
	struct max98090_cdata dai;
	const char **eq_texts;
	const char **bq_texts;
	struct soc_enum eq_enum;
	struct soc_enum bq_enum;
	int eq_textcnt;
	int bq_textcnt;
	u8 lin_state;
	unsigned int mic1pre;
	unsigned int mic2pre;

	struct delayed_work		work;
	enum playback_path 	cur_path;
	enum record_path 	rec_path;
};

#define MAX98090_NONE	0
#define MAX98090_SPK	1
#define MAX98090_HP		2

extern struct snd_soc_codec *max98090_codec;

void max98090_set_playback_speaker(struct snd_soc_codec *codec);
void max98090_set_playback_headset(struct snd_soc_codec *codec);
void max98090_set_playback_earpiece(struct snd_soc_codec *codec);
void max98090_set_playback_speaker_headset(struct snd_soc_codec *codec);
void max98090_disable_playback_path(struct snd_soc_codec *codec, enum playback_path path);

void max98090_set_record_main_mic(struct snd_soc_codec *codec);
void max98090_set_record_headset_mic(struct snd_soc_codec *codec);

void max98090_disable_record_path(struct snd_soc_codec *codec, enum record_path rec_path);

#endif
