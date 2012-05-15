/*
 * linux/sound/soc/codecs/tlv320aic326x.h
 *
 * Copyright (C) 2011 TI Solutions Pvt Ltd.
 *
 * Based on sound/soc/codecs/tlv320aic3262.c
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * The TLV320AIC3262 is a flexible, low-power, low-voltage stereo audio
 * codec with digital microphone inputs and programmable outputs.
 *
 * History:
 *
 * Rev 0.1   ASoC driver support    TI         20-01-2011
 *
 *                       The AIC325x ASoC driver is ported for the codec AIC3262.
 * Rev 0.2   ASoC driver support    TI         21-03-2011
 *                       The AIC326x ASoC driver is updated for linux 2.6.32 Kernel.
 * Rev 0.3   ASoC driver support    TI         20-04-2011
 *                       The AIC326x ASoC driver is ported to 2.6.35 omap4 kernel
 */

#ifndef _TLV320AIC3262_H
#define _TLV320AIC3262_H
#include "aic3xxx_cfw.h"
#include "aic3xxx_cfw_ops.h"
#include <linux/switch.h>

/*#ifdef DEBUG
        #define dprintk(x...)   printk(x)
        #define DBG(x...)       printk(x)
#else
        #define dprintk(x...)
        #define DBG(x...)
#endif
*/
  
#define AUDIO_NAME "aic3262"
#define AIC3262_VERSION "1.1"
/* Macro to enable the inclusion of tiload kernel driver */
#define AIC3262_TiLoad



//#define AIC3262_SYNC_MODE
#undef AIC3262_SYNC_MODE

#define AIC3262_ASI1_MASTER
//#undef AIC3262_ASI1_MASTER
//#define AIC3262_ASI2_MASTER
#undef AIC3262_ASI2_MASTER
//#define AIC3262_ASI3_MASTER
#undef AIC3262_ASI3_MASTER
/* Macro for McBsp master / slave configuration */
#define AIC3262_MCBSP_SLAVE	/*3262 master*/
//#undef AIC3262_MCBSP_SLAVE

/* Enable this macro allow for different ASI formats */
//#define ASI_MULTI_FMT
#undef ASI_MULTI_FMT
/* Enable register caching on write */
//#define EN_REG_CACHE


/* Enable or disable controls to have Input routing*/
//#define FULL_IN_CNTL
#undef FULL_IN_CNTL
/* AIC3262 supported sample rate are 8k to 192k */
#define AIC3262_RATES	SNDRV_PCM_RATE_8000_192000

/* AIC3262 supports the word formats 16bits, 20bits, 24bits and 32 bits */
#define AIC3262_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE \
			 | SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE)

#define AIC3262_FREQ_11289600 11289600
#define AIC3262_FREQ_12000000 12000000
#define AIC3262_FREQ_19200000 19200000
#define AIC3262_FREQ_24000000 24000000
#define AIC3262_FREQ_38400000 38400000
/* Audio data word length = 16-bits (default setting) */
#define AIC3262_WORD_LEN_16BITS		0x00
#define AIC3262_WORD_LEN_20BITS		0x01
#define AIC3262_WORD_LEN_24BITS		0x02
#define AIC3262_WORD_LEN_32BITS		0x03

/* sink: name of target widget */
#define AIC3262_WIDGET_NAME			0
/* control: mixer control name */
#define AIC3262_CONTROL_NAME		1
/* source: name of source name */
#define AIC3262_SOURCE_NAME			2

/* D15..D8 aic3262 register offset */
#define AIC3262_REG_OFFSET_INDEX    0
/* D7...D0 register data */
#define AIC3262_REG_DATA_INDEX      1

/* Serial data bus uses I2S mode (Default mode) */
#define AIC3262_I2S_MODE				0x00
#define AIC3262_DSP_MODE				0x01
#define AIC3262_RIGHT_JUSTIFIED_MODE	0x02
#define AIC3262_LEFT_JUSTIFIED_MODE		0x03

/* 8 bit mask value */
#define AIC3262_8BITS_MASK           0xFF

/* shift value for CLK_REG_3 register */
#define CLK_REG_3_SHIFT					6
/* shift value for DAC_OSR_MSB register */
#define DAC_OSR_MSB_SHIFT				4

/* number of codec specific register for configuration */
#define NO_FEATURE_REGS     				2

/* AIC3262 register space */
#define	AIC3262_CACHEREGNUM				1024 /* Updated from 256 to support Page 3 registers */
#define DAC_FLAG					37
#define ADC_FLAG					36
 
#define DSP_NON_SYNC_MODE(state)   (!( (state & 0x03) && (state & 0x30) ))
/*
 *----------------------------------------------------------------------------
 * @struct  aic3262_setup_data |
 *          i2c specific data setup for AIC3262.
 * @field   unsigned short |i2c_address |
 *          Unsigned short for i2c address.
 *----------------------------------------------------------------------------
 */
/*struct aic3262_setup_data {
	unsigned short i2c_address;
};*/
struct aic3262_jack_data {
        struct snd_soc_jack *jack;
        int report;
        struct switch_dev sdev;
};
/*configs |
 *          AIC3262 initialization data which has register offset and register
 *          value.
 * @field   u8 | book_no |
 *          AIC3262 Book Number Offsets required for initialization..
 * @field   u16 | reg_offset |
 *          AIC3262 Register offsets required for initialization..
 * @field   u8 | reg_val |
 *          value to set the AIC3262 register to initialize the AIC3262.
 *----------------------------------------------------------------------------
 */
struct aic3262_priv {
	u32 sysclk;
	s32 master;
    	u8 book_no;
	u8 page_no;
	u8 process_flow;
	u8 mute_codec;
	u8 stream_status;
	int current_dac_config[2];
	int current_adc_config[2];
	struct aic3262_jack_data hs_jack; 
	struct workqueue_struct *workqueue;
	struct delayed_work delayed_work;
	struct snd_soc_codec *codec;
	struct mutex mutex;
	struct mutex cfw_mutex;
	struct cfw_state cfw_ps;    
	struct cfw_state *cfw_p;    
	struct aic3262_pdata *pdata;
	int mute_asi; // Bit 0 -> ASI1, Bit 1-> ASI2, Bit 2 -> ASI3
	int dsp_runstate;
	struct firmware *cur_fw;
	int isdefault_fw;
};
/*struct aic3262_configs {
	u8 book_no;
    	u16 reg_offset;
	u8  reg_val;
};
*/
/*
 *----------------------------------------------------------------------------
 * @struct  aic3262_rate_divs |
 *          Setting up the values to get different freqencies
 *
 * @field   u32 | mclk |
 *          Master clock
 * @field   u32 | rate |
 *          sample rate
 * @field   u8 | p_val |
 *          value of p in PLL
 * @field   u32 | pll_j |
 *          value for pll_j
 * @field   u32 | pll_d |
 *          value for pll_d
 * @field   u32 | dosr |
 *          value to store dosr
 * @field   u32 | ndac |
 *          value for ndac
 * @field   u32 | mdac |
 *          value for mdac
 * @field   u32 | aosr |
 *          value for aosr
 * @field   u32 | nadc |
 *          value for nadc
 * @field   u32 | madc |
 *          value for madc
 * @field   u32 | blck_N |
 *          value for block N
 * @field   u32 | aic3262_configs |
 *          configurations for aic3262 register value
 *----------------------------------------------------------------------------
 */
struct aic3262_rate_divs {
	u32 mclk;
	u32 rate;
	u8 p_val;
	u8 pll_j;
	u16 pll_d;
	u16 dosr;
	u8 ndac;
	u8 mdac;
	u8 aosr;
	u8 nadc;
	u8 madc;
	u8 blck_N;
//	struct aic3262_configs codec_specific_regs[NO_FEATURE_REGS];
};

/*
 *----------------------------------------------------------------------------
 * @struct  snd_soc_codec_dai |
 *          It is SoC Codec DAI structure which has DAI capabilities viz.,
 *          playback and capture, DAI runtime information viz. state of DAI
 *			and pop wait state, and DAI private data.
 *----------------------------------------------------------------------------
 */
extern struct snd_soc_dai tlv320aic3262_dai;

/*
 *----------------------------------------------------------------------------
nt aic3262_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
 * @struct  snd_soc_codec_device |
 *          This structure is soc audio codec device sturecute which pointer
 *          to basic functions aic3262_probe(), aic3262_remove(),
 *			aic3262_suspend() and aic3262_resume()
 *
 */
extern struct snd_soc_codec_device soc_codec_dev_aic3262;
extern const aic3xxx_codec_ops aic3262_cfw_codec_ops;
void aic3262_hs_jack_detect(struct snd_soc_codec *codec,
				struct snd_soc_jack *jack, int report);

unsigned int aic3262_read(struct snd_soc_codec *codec, unsigned int reg);
 int aic3262_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value);
#endif				/* _TLV320AIC3262_H */

