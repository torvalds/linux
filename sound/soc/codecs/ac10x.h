/*
 * ac10x.h
 *
 * (C) Copyright 2017-2018
 * Seeed Technology Co., Ltd. <www.seeedstudio.com>
 *
 * PeterYang <linsheng.yang@seeed.cc>
 *
 * (C) Copyright 2010-2017
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * huangxin <huangxin@reuuimllatech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __AC10X_H__
#define __AC10X_H__

#define AC101_I2C_ID		4
#define _MASTER_AC108		0
#define _MASTER_AC101		1
#define _MASTER_MULTI_CODEC	_MASTER_AC101

/* enable headset detecting & headset button pressing */
#define CONFIG_AC101_SWITCH_DETECT

/* obsolete */
#define CONFIG_AC10X_TRIG_LOCK	0

#ifdef AC101_DEBG
    #define AC101_DBG(format,args...)  printk("[AC101] %s() L%d " format, __func__, __LINE__, ##args)
#else
    #define AC101_DBG(...)
#endif

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0)
#define __NO_SND_SOC_CODEC_DRV     1
#else
#define __NO_SND_SOC_CODEC_DRV     0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif
#endif

#if __NO_SND_SOC_CODEC_DRV
#define codec                      component
#define snd_soc_codec              snd_soc_component
#define snd_soc_codec_driver       snd_soc_component_driver
#define snd_soc_codec_get_drvdata  snd_soc_component_get_drvdata
#define snd_soc_codec_get_dapm     snd_soc_component_get_dapm
#define snd_soc_codec_get_bias_level snd_soc_component_get_bias_level
#define snd_soc_kcontrol_codec     snd_soc_kcontrol_component
#define snd_soc_read               snd_soc_component_read32
#define snd_soc_register_codec     snd_soc_register_component
#define snd_soc_unregister_codec   snd_soc_unregister_component
#define snd_soc_update_bits        snd_soc_component_update_bits
#define snd_soc_write              snd_soc_component_write
#define snd_soc_add_codec_controls snd_soc_add_component_controls
#endif


#ifdef CONFIG_AC101_SWITCH_DETECT
enum headphone_mode_u {
	HEADPHONE_IDLE,
	FOUR_HEADPHONE_PLUGIN,
	THREE_HEADPHONE_PLUGIN,
};
#endif

struct ac10x_priv {
	struct i2c_client *i2c[4];
	struct regmap* i2cmap[4];
	int codec_cnt;
	unsigned sysclk;
#define _FREQ_24_576K		24576000
#define _FREQ_22_579K		22579200
	unsigned mclk;	/* master clock or aif_clock/aclk */
	int clk_id;
	unsigned char i2s_mode;
	unsigned char data_protocol;
	struct delayed_work dlywork;
	int tdm_chips_cnt;
	int sysclk_en;

	/* member for ac101 .begin */
	struct snd_soc_codec *codec;
	struct i2c_client *i2c101;
	struct regmap* regmap101;

	struct mutex dac_mutex;
	u8 dac_enable;
	spinlock_t lock;
	u8 aif1_clken;

	struct work_struct codec_resume;
	struct gpio_desc* gpiod_spk_amp_gate;

	#ifdef CONFIG_AC101_SWITCH_DETECT
	struct gpio_desc* gpiod_irq;
	long irq;
	volatile int irq_cntr;
	volatile int pullout_cntr;
	volatile int state;

	enum headphone_mode_u mode;
	struct work_struct work_switch;
	struct work_struct work_clear_irq;

	struct input_dev* inpdev;
	#endif
	/* member for ac101 .end */
};


/* AC101 DAI operations */
int ac101_audio_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai);
void ac101_aif_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai);
int ac101_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt);
int ac101_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *codec_dai);
int ac101_trigger(struct snd_pcm_substream *substream, int cmd,
	 	  struct snd_soc_dai *dai);
int ac101_aif_mute(struct snd_soc_dai *codec_dai, int mute);

/* codec driver specific */
int ac101_codec_probe(struct snd_soc_codec *codec);
int ac101_codec_remove(struct snd_soc_codec *codec);
int ac101_codec_suspend(struct snd_soc_codec *codec);
int ac101_codec_resume(struct snd_soc_codec *codec);
int ac101_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level);

/* i2c device specific */
int ac101_probe(struct i2c_client *i2c, const struct i2c_device_id *id);
void ac101_shutdown(struct i2c_client *i2c);
int ac101_remove(struct i2c_client *i2c);

int ac10x_fill_regcache(struct device* dev, struct regmap* map);

#endif//__AC10X_H__
