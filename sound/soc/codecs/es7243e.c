/*
 * ALSA SoC ES7243E adc driver
 *
 * Author:      David Yang, <yangxiaohua@everest-semi.com>
 *		or 
 *		<info@everest-semi.com>
 * Copyright:   (C) 2017 Everest Semiconductor Co Ltd.,
 *
 * Based on sound/soc/codecs/es7243.c by DavidYang
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Notes:
 *  ES7243E is a stereo Audio ADC for Microphone Array 
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/initval.h>
#include <linux/regmap.h>

#include "es7243e_usr_cfg.h"

static struct i2c_client *i2c_clt[(ES7243E_CHANNELS_MAX) / 2];

/* codec private data */
struct es7243e_priv {
	struct regmap *regmap;
	struct i2c_client *i2c;
	unsigned int sysclk;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
	bool dmic;
	u8 mclksrc;
	bool mclkinv;
	bool bclkinv;
	u8 tdm;
	u8 vdda;
};
static struct snd_soc_component *tron_component[8];
static int es7243e_codec_num = 0;

static const struct regmap_config es7243e_regmap_config = {
	.reg_bits = 8,		//Number of bits in a register address
	.val_bits = 8,		//Number of bits in a register value
};

/*
* ES7243 register cache
*/
static const u8 es7243_reg[] = {
	0x00, 0x00, 0x10, 0x04,	/* 0 */
	0x02, 0x13, 0x00, 0x3f,	/* 4 */
	0x11, 0x00, 0xc0, 0xc0,	/* 8 */
	0x12, 0xa0, 0x40,	/* 12 */
};

static const struct reg_default es7243_reg_defaults[] = {
	{0x00, 0x00}, {0x01, 0x00}, {0x02, 0x10}, {0x03, 0x04},	/* 0 */
	{0x04, 0x02}, {0x05, 0x13}, {0x06, 0x00}, {0x07, 0x3f},	/* 4 */
	{0x08, 0x11}, {0x09, 0x00}, {0x0a, 0xc0}, {0x0b, 0xc0},	/* 8 */
	{0x0c, 0x12}, {0x0d, 0xa0}, {0x0e, 0x40},	/* 12 */
};

static int es7243e_read(u8 reg, u8 * rt_value, struct i2c_client *client)
{
	int ret;
	u8 read_cmd[3] = { 0 };
	u8 cmd_len = 0;

	read_cmd[0] = reg;
	cmd_len = 1;

	if (client->adapter == NULL)
		pr_err("es7243_read client->adapter==NULL\n");

	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len) {
		pr_err("es7243_read error1\n");
		return -1;
	}

	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1) {
		pr_err("es7243_read error2, ret = %d.\n", ret);
		return -1;
	}

	return 0;
}

static int
es7243e_write(u8 reg, unsigned char value, struct i2c_client *client)
{
	int ret = 0;
	u8 write_cmd[2] = { 0 };

	write_cmd[0] = reg;
	write_cmd[1] = value;

	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2) {
		pr_err("es7243_write error->[REG-0x%02x,val-0x%02x]\n",
		       reg, value);
		return -1;
	}

	return 0;
}

static int
es7243e_update_bits(u8 reg, u8 mask, u8 value, struct i2c_client *client)
{
	u8 val_old = 0, val_new = 0;

	es7243e_read(reg, &val_old, client);
	val_new = (val_old & ~mask) | (value & mask);
	if (val_new != val_old) {
		es7243e_write(reg, val_new, client);
	}

	return 0;
}

struct _coeff_div {
	u32 mclk;		//mclk frequency
	u32 sr_rate;		//sample rate
	u8 osr;			//adc over sample rate
	u8 prediv_premulti;	//adcclk and dacclk divider
	u8 cf_dsp_div;		//adclrck divider and daclrck divider
	u8 scale;
	u8 lrckdiv_h;
	u8 lrckdiv_l;
	u8 bclkdiv;		//sclk divider
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	//mclk     lrck,  osr, pre, div  ,scale, lrckdiv_h, lrckdiv_l, bclkdiv
	/* 24.576MHZ */
	{24576000, 8000, 0x20, 0x50, 0x00, 0x00, 0x0b, 0xff, 0x2f},
	{24576000, 12000, 0x20, 0x30, 0x00, 0x00, 0x07, 0xff, 0x1f},
	{24576000, 16000, 0x20, 0x20, 0x00, 0x00, 0x05, 0xff, 0x17},
	{24576000, 24000, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{24576000, 32000, 0x20, 0x21, 0x00, 0x00, 0x02, 0xff, 0x0b},
	{24576000, 48000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	/* 12.288MHZ */
	{12288000, 8000, 0x20, 0x20, 0x00, 0x00, 0x05, 0xff, 0x17},
	{12288000, 12000, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{12288000, 16000, 0x20, 0x21, 0x00, 0x00, 0x02, 0xff, 0x0b},
	{12288000, 24000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{12288000, 32000, 0x20, 0x22, 0x00, 0x00, 0x01, 0x7f, 0x05},
	{12288000, 48000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	/* 6.144MHZ */
	{6144000, 8000, 0x20, 0x21, 0x00, 0x00, 0x02, 0xff, 0x0b},
	{6144000, 12000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{6144000, 16000, 0x20, 0x22, 0x00, 0x00, 0x01, 0x7f, 0x05},
	{6144000, 24000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{6144000, 32000, 0x20, 0x23, 0x00, 0x00, 0x00, 0xbf, 0x02},
	{6144000, 48000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	/* 3.072MHZ */
	{3072000, 8000, 0x20, 0x22, 0x00, 0x00, 0x01, 0x7f, 0x05},
	{3072000, 12000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{3072000, 16000, 0x20, 0x23, 0x00, 0x00, 0x00, 0xbf, 0x02},
	{3072000, 24000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{3072000, 32000, 0x10, 0x03, 0x20, 0x04, 0x00, 0x5f, 0x02},
	{3072000, 48000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
	/* 1.536MHZ */
	{1536000, 8000, 0x20, 0x23, 0x00, 0x00, 0x00, 0xbf, 0x02},
	{1536000, 12000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{1536000, 16000, 0x10, 0x03, 0x20, 0x04, 0x00, 0x5f, 0x00},
	{1536000, 24000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
	/* 32.768MHZ */
	{32768000, 8000, 0x20, 0x70, 0x00, 0x00, 0x0f, 0xff, 0x3f},
	{32768000, 16000, 0x20, 0x30, 0x00, 0x00, 0x07, 0xff, 0x1f},
	{32768000, 32000, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	/* 16.384MHZ */
	{16384000, 8000, 0x20, 0x30, 0x00, 0x00, 0x07, 0xff, 0x1f},
	{16384000, 16000, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{16384000, 32000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	/* 8.192MHZ */
	{8192000, 8000, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{8192000, 16000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{8192000, 32000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	/* 4.096MHZ */
	{4096000, 8000, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{4096000, 16000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{4096000, 32000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	/* 2.048MHZ */
	{2048000, 8000, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{2048000, 16000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{2048000, 32000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
	/* 1.024MHZ */
	{1024000, 8000, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{1024000, 16000, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
	/* 22.5792MHz */
	{22579200, 11025, 0x20, 0x30, 0x00, 0x00, 0x07, 0xff, 0x1f},
	{22579200, 22050, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{22579200, 44100, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	/* 11.2896MHz */
	{11289600, 11025, 0x20, 0x10, 0x00, 0x00, 0x03, 0xff, 0x0f},
	{11289600, 22050, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{11289600, 44100, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	/* 5.6448MHz */
	{56448000, 11025, 0x20, 0x00, 0x00, 0x00, 0x01, 0xff, 0x07},
	{56448000, 22050, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{56448000, 44100, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	/* 2.8224MHz */
	{28224000, 11025, 0x20, 0x01, 0x00, 0x00, 0x00, 0xff, 0x03},
	{28224000, 22050, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{28224000, 44100, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
	/* 1.4112MHz */
	{14112000, 11025, 0x20, 0x02, 0x00, 0x00, 0x00, 0x7f, 0x01},
	{14112000, 22050, 0x20, 0x03, 0x00, 0x00, 0x00, 0x3f, 0x00},
};

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].sr_rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	return -EINVAL;
}

/* The set of rates we can generate from the above for each SYSCLK */

static unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 32000, 48000, 64000, 96000, 128000, 192000,
};

static unsigned int rates_8192[] = {
	8000, 16000, 32000, 64000, 128000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count = ARRAY_SIZE(rates_12288),
	.list = rates_12288,
};

static struct snd_pcm_hw_constraint_list constraints_8192 = {
	.count = ARRAY_SIZE(rates_8192),
	.list = rates_8192,
};

static unsigned int rates_112896[] = {
	8000, 11025, 22050, 44100,
};

static struct snd_pcm_hw_constraint_list constraints_112896 = {
	.count = ARRAY_SIZE(rates_112896),
	.list = rates_112896,
};

#if 0
static unsigned int rates_12[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
	.count = ARRAY_SIZE(rates_12),
	.list = rates_12,
};
#endif
/*
* Note that this should be called from init rather than from hw_params.
*/
static int
es7243e_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		       int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es7243e_priv *es7243e = snd_soc_component_get_drvdata(component);
	printk("Enter into %s(), freq = %d\n", __func__, freq);
	switch (freq) {
	case 11289600:
	case 22579200:
		es7243e->sysclk_constraints = &constraints_112896;
		es7243e->sysclk = freq;
		return 0;
	case 12288000:
	case 24576000:
		es7243e->sysclk_constraints = &constraints_12288;
		es7243e->sysclk = freq;
		return 0;
	case 4096000:
	case 8192000:
		es7243e->sysclk_constraints = &constraints_8192;
		es7243e->sysclk = freq;
		return 0;
/*
	case 12000000:
	case 24000000:
		es7243e->sysclk_constraints = &constraints_12;
		es7243e->sysclk = freq;
		return 0;
*/
	}
//      return -EINVAL;
	return 0;
}

static int es7243e_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
#if 0
	u8 iface = 0;
	u8 adciface = 0;
	u8 clksel = 0;
	u8 i;
	printk("Enter into %s()\n", __func__);
	for (i = 0; i < (ES7243E_CHANNELS_MAX) / 2; i++) {
		es7243e_read(0x0b, &adciface, i2c_clt[i]);	//get interface format
		es7243e_read(0x00, &iface, i2c_clt[i]);	//get spd interface
		es7243e_read(0x02, &clksel, i2c_clt[i]);	//get spd interface

		/* set master/slave audio interface */
		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:	// MASTER MODE
			iface |= 0x40;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:	// SLAVE MODE
			iface &= 0xbf;
			break;
		default:
			return -EINVAL;
		}
		/* interface format */
		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			adciface &= 0xFC;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			return -EINVAL;
		case SND_SOC_DAIFMT_LEFT_J:
			adciface &= 0xFC;
			adciface |= 0x01;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			adciface &= 0xDC;
			adciface |= 0x03;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			adciface &= 0xDC;
			adciface |= 0x23;
			break;
		default:
			return -EINVAL;
		}

		/* clock inversion */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			adciface &= 0xdf;
			clksel &= 0xfe;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			adciface |= 0x20;
			clksel |= 0x01;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			adciface &= 0xdf;
			clksel |= 0x01;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			adciface |= 0x20;
			clksel &= 0xfe;
			break;
		default:
			return -EINVAL;
		}

		es7243e_write(0x00, iface, i2c_clt[i]);
		es7243e_write(0x02, clksel, i2c_clt[i]);
		es7243e_write(0x0b, adciface, i2c_clt[i]);
	}
#endif
	return 0;
}

static int
es7243e_pcm_startup(struct snd_pcm_substream *substream,
		    struct snd_soc_dai *dai)
{

	return 0;
}

static int
es7243e_pcm_hw_params(struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *params,
		      struct snd_soc_dai *dai)
{
//      struct snd_soc_pcm_runtime *rtd = substream->private_data;
//      struct snd_soc_codec *codec = rtd->codec;
//      struct es7243e_priv *es7243e = snd_soc_codec_get_drvdata(codec);
	u8 index, regv = 0;
//      int coeff;
	printk("Enter into %s()\n", __func__);
#if 0
	coeff = get_coeff(es7243e->sysclk, params_rate(params));
	if (coeff < 0) {
		printk("Unable to configure sample rate %dHz with %dHz MCLK",
		       params_rate(params), es7243e->sysclk);
		return coeff;
	}
	/*
	 * set clock parameters
	 */
	if (coeff >= 0) {
		for (index = 0; index < (ES7243E_CHANNELS_MAX) / 2; index++) {
			es7243e_write(0x03, coeff_div[coeff].osr,
				      i2c_clt[index]);
			es7243e_write(0x04,
				      coeff_div[coeff].prediv_premulti,
				      i2c_clt[index]);
			es7243e_write(0x05, coeff_div[coeff].cf_dsp_div,
				      i2c_clt[index]);
			es7243e_write(0x0d, coeff_div[coeff].scale,
				      i2c_clt[index]);
			es7243e_write(0x03, coeff_div[coeff].osr,
				      i2c_clt[index]);

			es7243e_read(0x07, &regv, i2c_clt[index]);
			regv &= 0xf0;
			regv |= (coeff_div[coeff].lrckdiv_h & 0x0f);
			es7243e_write(0x07, regv, i2c_clt[index]);
			es7243e_write(0x06, coeff_div[coeff].bclkdiv,
				      i2c_clt[index]);

		}
	}
#endif
	/*
	 * set data length
	 */
	for (index = 0; index < (ES7243E_CHANNELS_MAX) / 2; index++) {
		es7243e_read(0x0b, &regv, i2c_clt[index]);
		regv &= 0xe3;
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			regv |= 0x0c;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			regv |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			regv |= 0x10;
			break;
		default:
			regv |= 0x0c;
			break;
		}
		es7243e_write(0x0b, regv, i2c_clt[index]);
	}
	msleep(50);
	for (index = 0; index < (ES7243E_CHANNELS_MAX) / 2; index++) {
		es7243e_read(0x0b, &regv, i2c_clt[index]);
		regv &= 0x3f;
		es7243e_write(0x0b, regv, i2c_clt[index]);
	}
	return 0;
}

static int es7243e_mute(struct snd_soc_dai *dai, int mute)
{
	//struct snd_soc_codec *codec = dai->codec;
	u8 i;
	printk("Enter into %s()\n", __func__);
	for (i = 0; i < (ES7243E_CHANNELS_MAX) / 2; i++) {
		if (mute)
			es7243e_update_bits(0x0b, 0xc0, 0xc0, i2c_clt[i]);
		else
			es7243e_update_bits(0x0b, 0xc0, 0x00, i2c_clt[i]);
	}
	return 0;
}

static int
es7243e_set_bias_level(struct snd_soc_component *component,
		       enum snd_soc_bias_level level)
{
	u8 i, regv = 0;
	switch (level) {
	case SND_SOC_BIAS_ON:
		printk("%s on\n", __func__);
		break;
	case SND_SOC_BIAS_PREPARE:
		printk("%s prepare\n", __func__);
		break;
	case SND_SOC_BIAS_STANDBY:
		printk("%s standby\n", __func__);
		for (i = 0; i < (ES7243E_CHANNELS_MAX) / 2; i++) {
			/*
			 * enable clock 
			 */
			es7243e_read(0x01, &regv, i2c_clt[i]);
			regv |= 0x0A;
			es7243e_write(0x01, regv, i2c_clt[i]);
			es7243e_write(0x16, 0x00, i2c_clt[i]);	//power up analog
			/*
			 * enable mic input 1
			 */
			es7243e_read(0x20, &regv, i2c_clt[i]);
			regv |= 0x10;
			es7243e_write(0x20, regv, i2c_clt[i]);
			/*
			 * enable mic input 2
			 */
			es7243e_read(0x21, &regv, i2c_clt[i]);
			regv |= 0x10;
			es7243e_write(0x21, regv, i2c_clt[i]);
			msleep(100);
			es7243e_update_bits(0x0b, 0xc0, 0x00, i2c_clt[i]);	//mute SDP
		}
		break;
	case SND_SOC_BIAS_OFF:
		printk("%s off\n", __func__);
		for (i = 0; i < (ES7243E_CHANNELS_MAX) / 2; i++) {
			es7243e_update_bits(0x0b, 0xc0, 0xc0, i2c_clt[i]);	//mute SDP
			/*
			 * disable mic input 1
			 */
			es7243e_read(0x20, &regv, i2c_clt[i]);
			regv &= 0xef;
			es7243e_write(0x20, regv, i2c_clt[i]);
			/*
			 * disable mic input 2
			 */
			es7243e_read(0x21, &regv, i2c_clt[i]);
			regv &= 0xef;
			es7243e_write(0x21, regv, i2c_clt[i]);

			es7243e_write(0x16, 0xff, i2c_clt[i]);	//power down analog

			/*
			 * disable clock 
			 */
			es7243e_read(0x01, &regv, i2c_clt[i]);
			regv &= 0xf5;
			es7243e_write(0x01, regv, i2c_clt[i]);
		}
		break;
	}
	//codec->dapm.bias_level = level;
	return 0;
}

/*
* snd_controls for PGA gain, Mute, suspend and resume
*/
static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, 0, 300, 0);

#if ES7243E_CHANNELS_MAX > 0
static int
es7243e_micboost1_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[0]);
	return 0;
}

static int
es7243e_micboost1_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[0]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost2_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[0]);
	return 0;
}

static int
es7243e_micboost2_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[0]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc1_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[0]);
	return 0;
}

static int
es7243e_adc1_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[0]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc2_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[0]);
	return 0;
}

static int
es7243e_adc2_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[0]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc1adc2_suspend_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[0]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 1;
	return 0;
}

static int
es7243e_adc1adc2_suspend_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	//u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[0]);
		es7243e_write(0x04, 0x01, i2c_clt[0]);
		es7243e_write(0xf7, 0x30, i2c_clt[0]);
		es7243e_write(0xf9, 0x01, i2c_clt[0]);
		es7243e_write(0x16, 0xff, i2c_clt[0]);
		es7243e_write(0x17, 0x00, i2c_clt[0]);
		es7243e_write(0x01, 0x38, i2c_clt[0]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[0]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[0]);
		es7243e_write(0x00, 0x8e, i2c_clt[0]);
		es7243e_write(0x01, 0x30, i2c_clt[0]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[0]);
		es7243e_write(0x00, 0x80, i2c_clt[0]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[0]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[0]);
		es7243e_write(0x16, 0x00, i2c_clt[0]);
		es7243e_write(0x17, 0x02, i2c_clt[0]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 2
static int
es7243e_micboost3_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[1]);
	return 0;
}

static int
es7243e_micboost3_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[1]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost4_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[1]);
	return 0;
}

static int
es7243e_micboost4_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[1]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc3_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[1]);
	return 0;
}

static int
es7243e_adc3_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[1]);
	ucontrol->value.integer.value[0] = (val & 0X40) >> 6;
	return 0;
}

static int
es7243e_adc4_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[1]);
	return 0;
}

static int
es7243e_adc4_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[1]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc3adc4_suspend_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[1]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 1;
	return 0;
}

static int
es7243e_adc3adc4_suspend_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	//u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[1]);
		es7243e_write(0x04, 0x01, i2c_clt[1]);
		es7243e_write(0xf7, 0x30, i2c_clt[1]);
		es7243e_write(0xf9, 0x01, i2c_clt[1]);
		es7243e_write(0x16, 0xff, i2c_clt[1]);
		es7243e_write(0x17, 0x00, i2c_clt[1]);
		es7243e_write(0x01, 0x38, i2c_clt[1]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[1]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[1]);
		es7243e_write(0x00, 0x8e, i2c_clt[1]);
		es7243e_write(0x01, 0x30, i2c_clt[1]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[1]);
		es7243e_write(0x00, 0x80, i2c_clt[1]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[1]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[1]);
		es7243e_write(0x16, 0x00, i2c_clt[1]);
		es7243e_write(0x17, 0x02, i2c_clt[1]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 4
static int
es7243e_micboost5_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[2]);
	return 0;
}

static int
es7243e_micboost5_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[2]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost6_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[2]);
	return 0;
}

static int
es7243e_micboost6_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[2]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc5_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[2]);
	return 0;
}

static int
es7243e_adc5_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[2]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc6_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[2]);
	return 0;
}

static int
es7243e_adc6_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[2]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc5adc6_suspend_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[2]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 1;
	return 0;
}

static int
es7243e_adc5adc6_suspend_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	//u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[2]);
		es7243e_write(0x04, 0x01, i2c_clt[2]);
		es7243e_write(0xf7, 0x30, i2c_clt[2]);
		es7243e_write(0xf9, 0x01, i2c_clt[2]);
		es7243e_write(0x16, 0xff, i2c_clt[2]);
		es7243e_write(0x17, 0x00, i2c_clt[2]);
		es7243e_write(0x01, 0x38, i2c_clt[2]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[2]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[2]);
		es7243e_write(0x00, 0x8e, i2c_clt[2]);
		es7243e_write(0x01, 0x30, i2c_clt[2]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[2]);
		es7243e_write(0x00, 0x80, i2c_clt[2]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[2]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[2]);
		es7243e_write(0x16, 0x00, i2c_clt[2]);
		es7243e_write(0x17, 0x02, i2c_clt[2]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 6
static int
es7243e_micboost7_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[3]);
	return 0;
}

static int
es7243e_micboost7_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[3]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost8_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[3]);
	return 0;
}

static int
es7243e_micboost8_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[3]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc7_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[3]);
	return 0;
}

static int
es7243e_adc7_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[3]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc8_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[3]);
	return 0;
}

static int
es7243e_adc8_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[3]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc7adc8_suspend_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[3]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 2;
	return 0;
}

static int
es7243e_adc7adc8_suspend_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
//      u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[3]);
		es7243e_write(0x04, 0x01, i2c_clt[3]);
		es7243e_write(0xf7, 0x30, i2c_clt[3]);
		es7243e_write(0xf9, 0x01, i2c_clt[3]);
		es7243e_write(0x16, 0xff, i2c_clt[3]);
		es7243e_write(0x17, 0x00, i2c_clt[3]);
		es7243e_write(0x01, 0x38, i2c_clt[3]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[3]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[3]);
		es7243e_write(0x00, 0x8e, i2c_clt[3]);
		es7243e_write(0x01, 0x30, i2c_clt[3]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[3]);
		es7243e_write(0x00, 0x80, i2c_clt[3]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[3]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[3]);
		es7243e_write(0x16, 0x00, i2c_clt[3]);
		es7243e_write(0x17, 0x02, i2c_clt[3]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 8
static int
es7243e_micboost9_setting_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[4]);
	return 0;
}

static int
es7243e_micboost9_setting_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[4]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost10_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[4]);
	return 0;
}

static int
es7243e_micboost10_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[4]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc9_mute_set(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[4]);
	return 0;
}

static int
es7243e_adc9_mute_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[4]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc10_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[4]);
	return 0;
}

static int
es7243e_adc10_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[4]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc9adc10_suspend_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[4]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 2;
	return 0;
}

static int
es7243e_adc9adc10_suspend_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
//      u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[4]);
		es7243e_write(0x04, 0x01, i2c_clt[4]);
		es7243e_write(0xf7, 0x30, i2c_clt[4]);
		es7243e_write(0xf9, 0x01, i2c_clt[4]);
		es7243e_write(0x16, 0xff, i2c_clt[4]);
		es7243e_write(0x17, 0x00, i2c_clt[4]);
		es7243e_write(0x01, 0x38, i2c_clt[4]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[4]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[4]);
		es7243e_write(0x00, 0x8e, i2c_clt[4]);
		es7243e_write(0x01, 0x30, i2c_clt[4]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[4]);
		es7243e_write(0x00, 0x80, i2c_clt[4]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[4]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[4]);
		es7243e_write(0x16, 0x00, i2c_clt[4]);
		es7243e_write(0x17, 0x02, i2c_clt[4]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 10
static int
es7243e_micboost11_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[5]);
	return 0;
}

static int
es7243e_micboost11_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[5]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost12_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[5]);
	return 0;
}

static int
es7243e_micboost12_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[5]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc11_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[5]);
	return 0;
}

static int
es7243e_adc11_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[5]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc12_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[5]);
	return 0;
}

static int
es7243e_adc12_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[5]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc11adc12_suspend_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[5]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 2;
	return 0;
}

static int
es7243e_adc11adc12_suspend_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
//      u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[5]);
		es7243e_write(0x04, 0x01, i2c_clt[5]);
		es7243e_write(0xf7, 0x30, i2c_clt[5]);
		es7243e_write(0xf9, 0x01, i2c_clt[5]);
		es7243e_write(0x16, 0xff, i2c_clt[5]);
		es7243e_write(0x17, 0x00, i2c_clt[5]);
		es7243e_write(0x01, 0x38, i2c_clt[5]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[5]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[5]);
		es7243e_write(0x00, 0x8e, i2c_clt[5]);
		es7243e_write(0x01, 0x30, i2c_clt[5]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[5]);
		es7243e_write(0x00, 0x80, i2c_clt[5]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[5]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[5]);
		es7243e_write(0x16, 0x00, i2c_clt[5]);
		es7243e_write(0x17, 0x02, i2c_clt[5]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 12
static int
es7243e_micboost13_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[6]);
	return 0;
}

static int
es7243e_micboost13_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[6]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost14_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[6]);
	return 0;
}

static int
es7243e_micboost14_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[6]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc13_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[6]);
	return 0;
}

static int
es7243e_adc13_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[6]);
	ucontrol->value.integer.value[0] = (val & 0x40) >> 6;
	return 0;
}

static int
es7243e_adc14_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[6]);
	return 0;
}

static int
es7243e_adc14_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[6]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc13adc14_suspend_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[6]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 2;
	return 0;
}

static int
es7243e_adc13adc14_suspend_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
//      u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[6]);
		es7243e_write(0x04, 0x01, i2c_clt[6]);
		es7243e_write(0xf7, 0x30, i2c_clt[6]);
		es7243e_write(0xf9, 0x01, i2c_clt[6]);
		es7243e_write(0x16, 0xff, i2c_clt[6]);
		es7243e_write(0x17, 0x00, i2c_clt[6]);
		es7243e_write(0x01, 0x38, i2c_clt[6]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[6]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[6]);
		es7243e_write(0x00, 0x8e, i2c_clt[6]);
		es7243e_write(0x01, 0x30, i2c_clt[6]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[6]);
		es7243e_write(0x00, 0x80, i2c_clt[6]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[6]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[6]);
		es7243e_write(0x16, 0x00, i2c_clt[6]);
		es7243e_write(0x17, 0x02, i2c_clt[6]);
	}
	return 0;
}
#endif
#if ES7243E_CHANNELS_MAX > 14
static int
es7243e_micboost15_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x20, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[7]);
	return 0;
}

static int
es7243e_micboost15_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x20, &val, i2c_clt[7]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_micboost16_setting_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x21, 0x0F,
			    ucontrol->value.integer.value[0] & 0x0f,
			    i2c_clt[7]);
	return 0;
}

static int
es7243e_micboost16_setting_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x21, &val, i2c_clt[7]);
	ucontrol->value.integer.value[0] = val & 0x0f;
	return 0;
}

static int
es7243e_adc15_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x40,
			    (ucontrol->value.integer.value[0] & 0x01) << 6,
			    i2c_clt[7]);
	return 0;
}

static int
es7243e_adc15_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[7]);
	ucontrol->value.integer.value[0] = (val 0x40) >> 6;
	return 0;
}

static int
es7243e_adc16_mute_set(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	es7243e_update_bits(0x0B, 0x80,
			    (ucontrol->value.integer.value[0] & 0x01) << 7,
			    i2c_clt[7]);
	return 0;
}

static int
es7243e_adc16_mute_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x0B, &val, i2c_clt[7]);
	ucontrol->value.integer.value[0] = (val & 0x80) >> 7;
	return 0;
}

static int
es7243e_adc15adc16_suspend_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u8 val = 0;
	es7243e_read(0x17, &val, i2c_clt[7]);
	ucontrol->value.integer.value[0] = (val & 0x02) >> 2;
	return 0;
}

static int
es7243e_adc15adc16_suspend_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
//      u8 val;
	if ((ucontrol->value.integer.value[0] & 0x01) == 1) {	//suspend
		es7243e_write(0x04, 0x02, i2c_clt[7]);
		es7243e_write(0x04, 0x01, i2c_clt[7]);
		es7243e_write(0xf7, 0x30, i2c_clt[7]);
		es7243e_write(0xf9, 0x01, i2c_clt[7]);
		es7243e_write(0x16, 0xff, i2c_clt[7]);
		es7243e_write(0x17, 0x00, i2c_clt[7]);
		es7243e_write(0x01, 0x38, i2c_clt[7]);
		es7243e_update_bits(0x20, 0x10, 0x00, i2c_clt[7]);
		es7243e_update_bits(0x21, 0x10, 0x00, i2c_clt[7]);
		es7243e_write(0x00, 0x8e, i2c_clt[7]);
		es7243e_write(0x01, 0x30, i2c_clt[7]);
	} else {		//resume
		es7243e_write(0x01, 0x3a, i2c_clt[7]);
		es7243e_write(0x00, 0x80, i2c_clt[7]);
		es7243e_update_bits(0x20, 0x10, 0x10, i2c_clt[7]);
		es7243e_update_bits(0x21, 0x10, 0x10, i2c_clt[7]);
		es7243e_write(0x16, 0x00, i2c_clt[7]);
		es7243e_write(0x17, 0x02, i2c_clt[7]);
	}
	return 0;
}
#endif
static const struct snd_kcontrol_new es7243e_snd_controls[] = {
#if ES7243E_CHANNELS_MAX > 0
	SOC_SINGLE_EXT_TLV("PGA1_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost1_setting_get,
			   es7243e_micboost1_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA2_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost2_setting_get,
			   es7243e_micboost2_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC1_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc1_mute_get, es7243e_adc1_mute_set),
	SOC_SINGLE_EXT("ADC2_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc2_mute_get, es7243e_adc2_mute_set),
	SOC_SINGLE_EXT("ADC1ADC2_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc1adc2_suspend_get,
		       es7243e_adc1adc2_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 2
	SOC_SINGLE_EXT_TLV("PGA3_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost3_setting_get,
			   es7243e_micboost3_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA4_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost4_setting_get,
			   es7243e_micboost4_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC3_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc3_mute_get, es7243e_adc3_mute_set),
	SOC_SINGLE_EXT("ADC4_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc4_mute_get,
		       es7243e_adc4_mute_set),
	SOC_SINGLE_EXT("ADC3ADC4_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc3adc4_suspend_get,
		       es7243e_adc3adc4_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 4
	SOC_SINGLE_EXT_TLV("PGA5_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost5_setting_get,
			   es7243e_micboost5_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA6_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost6_setting_get,
			   es7243e_micboost6_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC5_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc5_mute_get, es7243e_adc5_mute_set),
	SOC_SINGLE_EXT("ADC6_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc6_mute_get, es7243e_adc6_mute_set),
	SOC_SINGLE_EXT("ADC5ADC6_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc5adc6_suspend_get,
		       es7243e_adc5adc6_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 6
	SOC_SINGLE_EXT_TLV("PGA7_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost7_setting_get,
			   es7243e_micboost7_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA8_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost8_setting_get,
			   es7243e_micboost8_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC7_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc7_mute_get, es7243e_adc7_mute_set),
	SOC_SINGLE_EXT("ADC8_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc8_mute_get, es7243e_adc8_mute_set),
	SOC_SINGLE_EXT("ADC7ADC8_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc7adc8_suspend_get,
		       es7243e_adc7adc8_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 8
	SOC_SINGLE_EXT_TLV("PGA9_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost9_setting_get,
			   es7243e_micboost9_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA10_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost10_setting_get,
			   es7243e_micboost10_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC9_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc9_mute_get, es7243e_adc9_mute_set),
	SOC_SINGLE_EXT("ADC10_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc10_mute_get, es7243e_adc10_mute_set),
	SOC_SINGLE_EXT("ADC9ADC10_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc9adc10_suspend_get,
		       es7243e_adc9adc10_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 10
	SOC_SINGLE_EXT_TLV("PGA11_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost11_setting_get,
			   es7243e_micboost11_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA12_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost12_setting_get,
			   es7243e_micboost12_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC11_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc11_mute_get, es7243e_adc11_mute_set),
	SOC_SINGLE_EXT("ADC12_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc12_mute_get, es7243e_adc12_mute_set),
	SOC_SINGLE_EXT("ADC11ADC12_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc11adc12_suspend_get,
		       es7243e_adc11adc12_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 12
	SOC_SINGLE_EXT_TLV("PGA13_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost13_setting_get,
			   es7243e_micboost13_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA14_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost14_setting_get,
			   es7243e_micboost14_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC13_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc13_mute_get, es7243e_adc13_mute_set),
	SOC_SINGLE_EXT("ADC14_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc14_mute_get, es7243e_adc14_mute_set),
	SOC_SINGLE_EXT("ADC13ADC14_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc13adc14_suspend_get,
		       es7243e_adc13adc14_suspend_set),
#endif
#if ES7243E_CHANNELS_MAX > 14
	SOC_SINGLE_EXT_TLV("PGA15_setting", 0x20, 0, 0x0F, 0,
			   es7243e_micboost15_setting_get,
			   es7243e_micboost15_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA16_setting", 0x21, 0, 0x0F, 0,
			   es7243e_micboost16_setting_get,
			   es7243e_micboost16_setting_set,
			   mic_boost_tlv),
	SOC_SINGLE_EXT("ADC15_MUTE", 0x0B, 1, 0x40, 0,
		       es7243e_adc15_mute_get, es7243e_adc15_mute_set),
	SOC_SINGLE_EXT("ADC16_MUTE", 0x0B, 1, 0x80, 0,
		       es7243e_adc16_mute_get, es7243e_adc16_mute_set),
	SOC_SINGLE_EXT("ADC15ADC16_SUSPEND", 0x17, 1, 1, 0,
		       es7243e_adc15adc16_suspend_get,
		       es7243e_adc15adc16_suspend_set),
#endif
};

#define es7243e_RATES SNDRV_PCM_RATE_8000_48000

#define es7243e_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops es7243e_ops = {
	.startup = es7243e_pcm_startup,
	.hw_params = es7243e_pcm_hw_params,
	.set_fmt = es7243e_set_dai_fmt,
	.set_sysclk = es7243e_set_dai_sysclk,
	.digital_mute = es7243e_mute,
};

#if ES7243E_CHANNELS_MAX > 0
static struct snd_soc_dai_driver es7243e_dai0 = {
	.name = "ES7243E HiFi 0",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 8,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 2
static struct snd_soc_dai_driver es7243e_dai1 = {
	.name = "ES7243E HiFi 1",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 8,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 4
static struct snd_soc_dai_driver es7243e_dai2 = {
	.name = "ES7243E HiFi 2",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 8,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 6
static struct snd_soc_dai_driver es7243e_dai3 = {
	.name = "ES7243E HiFi 3",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 8,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 8
static struct snd_soc_dai_driver es7243e_dai5 = {
	.name = "ES7243E HiFi 4",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 10
static struct snd_soc_dai_driver es7243e_dai6 = {
	.name = "ES7243E HiFi 5",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 12
static struct snd_soc_dai_driver es7243e_dai7 = {
	.name = "ES7243E HiFi 6",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
#if ES7243E_CHANNELS_MAX > 14
static struct snd_soc_dai_driver es7243e_dai8 = {
	.name = "ES7243E HiFi 7",
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = es7243e_RATES,
		    .formats = es7243e_FORMATS,
		    },
	.ops = &es7243e_ops,
	.symmetric_rates = 1,
};
#endif
static struct snd_soc_dai_driver *es7243e_dai[] = {
#if ES7243E_CHANNELS_MAX > 0
	&es7243e_dai0,
#endif
#if ES7243E_CHANNELS_MAX > 2
	&es7243e_dai1,
#endif
#if ES7243E_CHANNELS_MAX > 4
	&es7243e_dai2,
#endif
#if ES7243E_CHANNELS_MAX > 6
	&es7243e_dai3,
#endif
#if ES7243E_CHANNELS_MAX > 8
	&es7243e_dai4,
#endif
#if ES7243E_CHANNELS_MAX > 10
	&es7243e_dai5,
#endif
#if ES7243E_CHANNELS_MAX > 12
	&es7243e_dai6,
#endif
#if ES7243E_CHANNELS_MAX > 14
	&es7243e_dai7,
#endif
};

static int es7243e_suspend(struct snd_soc_component *component)
{
	es7243e_set_bias_level(component, SND_SOC_BIAS_OFF);
	return 0;
}

static int es7243e_resume(struct snd_soc_component *component)
{
	es7243e_set_bias_level(component, SND_SOC_BIAS_STANDBY);
	return 0;
}

struct _mclk_lrck_ratio {
	u16 ratio;		//ratio between mclk and lrck
	u8 nfs;			//nfs mode, =0, nfs mode disabled
	u8 osr;			//adc over sample rate
	u8 prediv_premulti;	//adcclk and dacclk divider
	u8 cf_dsp_div;		//adclrck divider and daclrck divider
	u8 scale;
};

/* codec hifi mclk clock divider coefficients */
static const struct _mclk_lrck_ratio ratio_div[] = {
	//ratio     nfs,  osr,  pre,  div  ,scale 
	{3072, 0, 0x20, 0x50, 0x00, 0x00},
	{3072, 2, 0x20, 0xb0, 0x00, 0x00},

	{2048, 0, 0x20, 0x30, 0x00, 0x00},
	{2048, 2, 0x20, 0x70, 0x00, 0x00},
	{2048, 3, 0x20, 0xb0, 0x00, 0x00},
	{2048, 4, 0x20, 0xf0, 0x00, 0x00},

	{1536, 0, 0x20, 0x20, 0x00, 0x00},
	{1536, 2, 0x20, 0x50, 0x00, 0x00},
	{1536, 3, 0x20, 0x80, 0x00, 0x00},
	{1536, 4, 0x20, 0xb0, 0x00, 0x00},

	{1024, 0, 0x20, 0x10, 0x00, 0x00},
	{1024, 2, 0x20, 0x30, 0x00, 0x00},
	{1024, 3, 0x20, 0x50, 0x00, 0x00},
	{1024, 4, 0x20, 0x70, 0x00, 0x00},
	{1024, 5, 0x20, 0x90, 0x00, 0x00},
	{1024, 6, 0x20, 0xb0, 0x00, 0x00},
	{1024, 7, 0x20, 0xd0, 0x00, 0x00},
	{1024, 8, 0x20, 0xf0, 0x00, 0x00},

	{768, 0, 0x20, 0x21, 0x00, 0x00},
	{768, 2, 0x20, 0x20, 0x00, 0x00},
	{768, 3, 0x20, 0x81, 0x00, 0x00},
	{768, 4, 0x20, 0x50, 0x00, 0x00},
	{768, 5, 0x20, 0xe1, 0x00, 0x00},
	{768, 6, 0x20, 0x80, 0x00, 0x00},
	{768, 8, 0x20, 0xb0, 0x00, 0x00},

	{512, 0, 0x20, 0x00, 0x00, 0x00},
	{512, 2, 0x20, 0x10, 0x00, 0x00},
	{512, 3, 0x20, 0x20, 0x00, 0x00},
	{512, 4, 0x20, 0x30, 0x00, 0x00},
	{512, 5, 0x20, 0x40, 0x00, 0x00},
	{512, 6, 0x20, 0x50, 0x00, 0x00},
	{512, 7, 0x20, 0x60, 0x00, 0x00},
	{512, 8, 0x20, 0x70, 0x00, 0x00},

	{384, 0, 0x20, 0x22, 0x00, 0x00},
	{384, 2, 0x20, 0x21, 0x00, 0x00},
	{384, 3, 0x20, 0x82, 0x00, 0x00},
	{384, 4, 0x20, 0x20, 0x00, 0x00},
	{384, 5, 0x20, 0xe2, 0x00, 0x00},
	{384, 6, 0x20, 0x81, 0x00, 0x00},
	{384, 8, 0x20, 0x50, 0x00, 0x00},

	{256, 0, 0x20, 0x01, 0x00, 0x00},
	{256, 2, 0x20, 0x00, 0x00, 0x00},
	{256, 3, 0x20, 0x21, 0x00, 0x00},
	{256, 4, 0x20, 0x10, 0x00, 0x00},
	{256, 5, 0x20, 0x41, 0x00, 0x00},
	{256, 6, 0x20, 0x20, 0x00, 0x00},
	{256, 7, 0x20, 0x61, 0x00, 0x00},
	{256, 8, 0x20, 0x30, 0x00, 0x00},

	{192, 0, 0x20, 0x23, 0x00, 0x00},
	{192, 2, 0x20, 0x22, 0x00, 0x00},
	{192, 3, 0x20, 0x83, 0x00, 0x00},
	{192, 4, 0x20, 0x21, 0x00, 0x00},
	{192, 5, 0x20, 0xe3, 0x00, 0x00},
	{192, 6, 0x20, 0x82, 0x00, 0x00},
	{192, 8, 0x20, 0x20, 0x00, 0x00},

	{128, 0, 0x20, 0x02, 0x00, 0x00},
	{128, 2, 0x20, 0x01, 0x00, 0x00},
	{128, 3, 0x20, 0x22, 0x00, 0x00},
	{128, 4, 0x20, 0x00, 0x00, 0x00},
	{128, 5, 0x20, 0x42, 0x00, 0x00},
	{128, 6, 0x20, 0x21, 0x00, 0x00},
	{128, 7, 0x20, 0x62, 0x00, 0x00},
	{128, 8, 0x20, 0x10, 0x00, 0x00},

	{64, 0, 0x20, 0x03, 0x00, 0x00},
	{64, 2, 0x20, 0x02, 0x00, 0x00},
	{64, 3, 0x20, 0x23, 0x00, 0x00},
	{64, 4, 0x20, 0x01, 0x00, 0x00},
	{64, 5, 0x20, 0x43, 0x00, 0x00},
	{64, 6, 0x20, 0x22, 0x00, 0x00},
	{64, 7, 0x20, 0x63, 0x00, 0x00},
	{64, 8, 0x20, 0x00, 0x00, 0x00},
};

static inline int get_mclk_lrck_ratio(int clk_ratio, int n_fs)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ratio_div); i++) {
		if (ratio_div[i].ratio == clk_ratio
		    && ratio_div[i].nfs == n_fs)

			return i;
	}

	return -EINVAL;
}

static int es7243e_probe(struct snd_soc_component *component)
{
	struct es7243e_priv *es7243e = snd_soc_component_get_drvdata(component);
	int ret = 0;
	u8 index, regv = 0, chn, work_mode, ratio_index, datbits;
	u16 ratio;
	u8 digital_vol[16], pga_gain[16];

	printk("begin->>>>>>>>>>%s!\n", __func__);
#if !ES7243E_CODEC_RW_TEST_EN
	//ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);//8,8
#else
	component->control_data = devm_regmap_init_i2c(es7243e->i2c,
						   &es7243e_regmap_config);
	ret = PTR_ERR_OR_ZERO(component->control_data);
#endif
	if (ret < 0) {
		dev_err(component->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	tron_component[es7243e_codec_num++] = component;
	index = 0;
#if ES7243E_CHANNELS_MAX > 0
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_1;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN1_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_2;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN2_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 2
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_3;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN3_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_4;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN4_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 4
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_5;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN5_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_6;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN6_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 6
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_7;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN7_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_8;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN8_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 8
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_9;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN9_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_10;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN10_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 10
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_11;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN11_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_12;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN12_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 12
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_13;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN13_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_14;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN14_PGA;
	index++;
#endif
#if ES7243E_CHANNELS_MAX > 14
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_15;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN15_PGA;
	index++;
	digital_vol[index] = ES7243E_DIGITAL_VOLUME_16;
	pga_gain[index] = ES7243E_MIC_ARRAY_AIN16_PGA;
	index++;
#endif

	for (index = 0; index < (ES7243E_CHANNELS_MAX) / 2; index++) {
		printk("%s(), index = %d\n", __func__, index);
		es7243e_read(0x02, &regv, i2c_clt[index]);
		if (es7243e->mclksrc == FROM_MCLK_PIN)
			regv &= 0x7f;
		else
			regv |= 0x80;
		regv &= 0xfd;
		if (es7243e->mclkinv == true) {
			regv |= 0x20;
		}
		regv &= 0xfe;
		if (es7243e->bclkinv == true) {
			regv |= 0x01;
		}
		es7243e_write(0x02, regv, i2c_clt[index]);
		/*
		 *      set data bits 
		 */
		es7243e_read(0x0b, &regv, i2c_clt[index]);
		regv &= 0xe3;
		datbits = ES7243E_DATA_LENGTH;
		switch (datbits) {
		case DATA_16BITS:
			regv |= 0x0c;
			break;
		case DATA_24BITS:
			break;
		case DATA_32BITS:
			regv |= 0x10;
			break;
		default:
			regv |= 0x0c;
			break;
		}
		es7243e_write(0x0b, regv, i2c_clt[index]);
		/*
		 *      set sdp format and tdm mode
		 */
		chn = ES7243E_CHANNELS_MAX / 2;
		switch (es7243e->tdm) {
		case ES7243E_NORMAL_I2S:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xfc;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			work_mode = 0;
			break;
		case ES7243E_NORMAL_LJ:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xfc;
			regv |= 0x01;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			work_mode = 0;
			break;
		case ES7243E_NORMAL_DSPA:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xdc;
			regv |= 0x03;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			work_mode = 0;
			break;
		case ES7243E_NORMAL_DSPB:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xdc;
			regv |= 0x23;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			work_mode = 0;
			break;
		case ES7243E_TDM_A:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xdc;
			regv |= 0x03;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			regv |= 0x08;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			work_mode = 0;
			break;
		case ES7243E_NFS_I2S:
			es7243e_read(0x0b, &regv, i2c_clt[index]);
			regv &= 0xfc;
			es7243e_write(0x0b, regv, i2c_clt[index]);
			es7243e_read(0x0c, &regv, i2c_clt[index]);
			regv &= 0xc0;
			switch (chn) {
			case 2:
				regv |= 0x01;
				work_mode = 2;
				break;
			case 3:
				regv |= 0x02;
				work_mode = 3;
				break;
			case 4:
				regv |= 0x03;
				work_mode = 4;
				break;
			case 5:
				regv |= 0x04;
				work_mode = 5;
				break;
			case 6:
				regv |= 0x05;
				work_mode = 6;
				break;
			case 7:
				regv |= 0x06;
				work_mode = 7;
				break;
			case 8:
				regv |= 0x07;
				work_mode = 8;
				break;
			default:
				work_mode = 0;
				break;
			}
			/*
			 * the last chip generate flag bits, others chip in sync mode
			 */
			if (index == ((ES7243E_CHANNELS_MAX / 2) - 1))
				regv |= 0x10;
			else
				regv |= 0x20;
			es7243e_write(0x0c, regv, i2c_clt[index]);
			break;
		default:
			work_mode = 0;
			break;
		}

		/*
		 *      set clock divider and multiplexer according clock ratio and nfs mode
		 */
		ratio = ES7243E_MCLK_LRCK_RATIO;
		ratio_index = get_mclk_lrck_ratio(ratio, work_mode);
		if (ratio_index < 0) {
			printk
			    ("can't get configuration for %d ratio with %d es7243e",
			     ratio, work_mode);
			es7243e_write(0x03, 0x20, i2c_clt[index]);
			es7243e_write(0x0d, 0x00, i2c_clt[index]);
			es7243e_write(0x04, 0x00, i2c_clt[index]);
			es7243e_write(0x05, 0x00, i2c_clt[index]);
		} else {
			es7243e_write(0x03, ratio_div[ratio_index].osr,
				      i2c_clt[index]);
			es7243e_write(0x0d, ratio_div[ratio_index].scale,
				      i2c_clt[index]);
			es7243e_write(0x04,
				      ratio_div
				      [ratio_index].prediv_premulti,
				      i2c_clt[index]);
			es7243e_write(0x05,
				      ratio_div[ratio_index].cf_dsp_div,
				      i2c_clt[index]);
		}

		es7243e_write(0x09, 0xe0, i2c_clt[index]);
		es7243e_write(0x0a, 0xa0, i2c_clt[index]);

		es7243e_write(0x0e, digital_vol[index * 2], i2c_clt[index]);

		es7243e_write(0x0f, 0x80, i2c_clt[index]);
		es7243e_write(0x14, 0x0c, i2c_clt[index]);
		es7243e_write(0x15, 0x0c, i2c_clt[index]);
		if (es7243e->vdda == VDDA_3V3) {
			es7243e_write(0x18, 0x26, i2c_clt[index]);
			es7243e_write(0x17, 0x02, i2c_clt[index]);
			es7243e_write(0x19, 0x77, i2c_clt[index]);
			es7243e_write(0x1a, 0xf4, i2c_clt[index]);
			es7243e_write(0x1b, 0x66, i2c_clt[index]);
			es7243e_write(0x1c, 0x44, i2c_clt[index]);
			es7243e_write(0x1d, 0x3c, i2c_clt[index]);
			es7243e_write(0x1e, 0x00, i2c_clt[index]);
			es7243e_write(0x1f, 0x0c, i2c_clt[index]);
		} else {
			es7243e_write(0x16, 0x00, i2c_clt[index]);
			es7243e_write(0x18, 0x26, i2c_clt[index]);
			es7243e_write(0x17, 0x02, i2c_clt[index]);
			es7243e_write(0x19, 0x66, i2c_clt[index]);
			es7243e_write(0x1a, 0x44, i2c_clt[index]);
			es7243e_write(0x1b, 0x44, i2c_clt[index]);
			es7243e_write(0x1c, 0x44, i2c_clt[index]);
			es7243e_write(0x1d, 0x3c, i2c_clt[index]);
			es7243e_write(0x1e, 0x0f, i2c_clt[index]);
			es7243e_write(0x1f, 0x07, i2c_clt[index]);
		}
		es7243e_write(0x00, 0x80, i2c_clt[index]);
		es7243e_write(0x01, 0x3a, i2c_clt[index]);
		es7243e_write(0x16, 0x00, i2c_clt[index]);

		es7243e_write(0x20, (0x10 | pga_gain[index * 2]),
			      i2c_clt[index]);
		es7243e_write(0x21, (0x10 | pga_gain[index * 2 + 1]),
			      i2c_clt[index]);

		//es7243e_write(0x1f, 0x03, i2c_clt[index]);
		/*
		 * reset PGA
		 */
		msleep(100);
		es7243e_write(0x16, 0x03, i2c_clt[index]);
		msleep(100);
		es7243e_write(0x16, 0x00, i2c_clt[index]);
	}
	return 0;
}

static void es7243e_remove(struct snd_soc_component *component)
{
	es7243e_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static struct snd_soc_component_driver soc_codec_dev_es7243e = {
	.probe = es7243e_probe,
	.remove = es7243e_remove,
	.suspend = es7243e_suspend,
	.resume = es7243e_resume,
	.set_bias_level = es7243e_set_bias_level,
	//.idle_bias_off = true,
	//.reg_word_size = sizeof(u8),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
#if ES7243E_CODEC_RW_TEST_EN
	.read = es7243e_codec_read,
	.write = es7243e_codec_write,
#endif
	//.component_driver = {
			     .controls = es7243e_snd_controls,
			     .num_controls = ARRAY_SIZE(es7243e_snd_controls),
	//		     }
	//,

};

static ssize_t
es7243e_store(struct device *dev,
	      struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0, flag = 0;
	u8 i = 0, reg, num, value_w, value_r;

	struct es7243e_priv *es7243e = dev_get_drvdata(dev);
	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xFF;

	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		printk("\nWrite: start REG:0x%02x,val:0x%02x,count:0x%02x\n",
		       reg, value_w, flag);
		while (flag--) {
			es7243e_write(reg, value_w, es7243e->i2c);
			printk("Write 0x%02x to REG:0x%02x\n", value_w, reg);
			reg++;
		}
	} else {
		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		printk("\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);
		do {
			value_r = 0;
			es7243e_read(reg, &value_r, es7243e->i2c);
			printk("REG[0x%02x]: 0x%02x;  \n", reg, value_r);
			reg++;
			i++;
			if ((i == num) || (i % 4 == 0))
				printk("\n");
		}
		while (i < num);
	}

	return count;
}

static ssize_t
es7243e_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("echo flag|reg|val > es7243e\n");
	printk("eg read star addres=0x06,count 0x10:echo 0610 >es7243e\n");
	printk
	    ("eg write star addres=0x90,value=0x3c,count=4:echo 4903c >es7243\n");
	//printk("eg write value:0xfe to address:0x06 :echo 106fe > es7243\n");
	return 0;
}

static DEVICE_ATTR(es7243e, 0644, es7243e_show, es7243e_store);

static struct attribute *es7243e_debug_attrs[] = {
	&dev_attr_es7243e.attr,
	NULL,
};

static struct attribute_group es7243e_debug_attr_group = {
	.name = "es7243e_debug",
	.attrs = es7243e_debug_attrs,
};

/*
 * If the i2c layer weren't so broken, we could pass this kind of data
 * around
 */
static int
es7243e_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *i2c_id)
{
	struct es7243e_priv *es7243e;
	int ret;

	printk("begin->>>>>>>>>>%s!\n", __func__);

	es7243e = devm_kzalloc(&i2c->dev, sizeof(struct es7243e_priv),
			       GFP_KERNEL);
	if (es7243e == NULL)
		return -ENOMEM;
	es7243e->i2c = i2c;
	es7243e->tdm = ES7243E_WORK_MODE;	//to initialize tdm mode
	es7243e->mclksrc = ES7243E_MCLK_SOURCE;
	es7243e->dmic = DMIC_INTERFACE;
	es7243e->mclkinv = MCLK_INVERTED_OR_NOT;
	es7243e->bclkinv = BCLK_INVERTED_OR_NOT;
	es7243e->vdda = VDDA_VOLTAGE;

	dev_set_drvdata(&i2c->dev, es7243e);
	//i2c_set_clientdata(i2c, es7243e);
	//es7243e->regmap = devm_regmap_init_i2c(i2c, &es7243e_regmap);
	//      if (IS_ERR(es7243e->regmap)) {
	//      ret = PTR_ERR(es7243e->regmap);
	//      dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
	//      ret);
	//      return ret;
	//      }
	if (i2c_id->driver_data < (ES7243E_CHANNELS_MAX) / 2) {
		i2c_clt[i2c_id->driver_data] = i2c;
		ret = devm_snd_soc_register_component(&i2c->dev,
					     &soc_codec_dev_es7243e,
					     es7243e_dai
					     [i2c_id->driver_data], 1);
		if (ret < 0) {
			devm_kfree(&i2c->dev, es7243e);
			printk("%s(), failed to register codec device\n",
			       __func__);
			return ret;
		}
	}
	ret = sysfs_create_group(&i2c->dev.kobj, &es7243e_debug_attr_group);
	if (ret) {
		pr_err("failed to create attr group\n");
	}
	return ret;
}

static int __exit es7243e_i2c_remove(struct i2c_client *i2c)
{
	sysfs_remove_group(&i2c->dev.kobj, &es7243e_debug_attr_group);

	return 0;
}

#if !ES7243E_MATCH_DTS_EN
static int
es7243e_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	printk("Enter into %s()\n", __func__);
	if (adapter->nr == ES7243E_I2C_BUS_NUM) {
#if ES7243E_CHANNELS_MAX > 0
		if (client->addr == ES7243E_I2C_CHIP_ADDRESS_0) {
			strlcpy(info->type, "ES7243E_MicArray_0", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 2
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_1) {
			strlcpy(info->type, "ES7243E_MicArray_1", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 4
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_2) {
			strlcpy(info->type, "ES7243E_MicArray_2", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 6
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_3) {
			strlcpy(info->type, "ES7243E_MicArray_3", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 8
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_4) {
			strlcpy(info->type, "ES7243E_MicArray_4", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 10
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_5) {
			strlcpy(info->type, "ES7243E_MicArray_5", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 12
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_6) {
			strlcpy(info->type, "ES7243E_MicArray_6", I2C_NAME_SIZE);
			return 0;
		}
#endif
#if ES7243E_CHANNELS_MAX > 14
		else if (client->addr == ES7243E_I2C_CHIP_ADDRESS_7) {
			strlcpy(info->type, "ES7243E_MicArray_7", I2C_NAME_SIZE);
			return 0;
		}
#endif
	}
	return -ENODEV;
}
#endif

static const unsigned short es7243e_i2c_addr[] = {
#if ES7243E_CHANNELS_MAX > 0
	ES7243E_I2C_CHIP_ADDRESS_0,
#endif

#if ES7243E_CHANNELS_MAX > 2
	ES7243E_I2C_CHIP_ADDRESS_1,
#endif

#if ES7243E_CHANNELS_MAX > 4
	ES7243E_I2C_CHIP_ADDRESS_2,
#endif

#if ES7243E_CHANNELS_MAX > 6
	ES7243E_I2C_CHIP_ADDRESS_3,
#endif

#if ES7243E_CHANNELS_MAX > 8
	ES7243E_I2C_CHIP_ADDRESS_4,
#endif

#if ES7243E_CHANNELS_MAX > 10
	ES7243E_I2C_CHIP_ADDRESS_5,
#endif

#if ES7243E_CHANNELS_MAX > 12
	ES7243E_I2C_CHIP_ADDRESS_6,
#endif

#if ES7243E_CHANNELS_MAX > 14
	ES7243E_I2C_CHIP_ADDRESS_7,
#endif

	I2C_CLIENT_END,
};

/*
* device tree source or i2c_board_info both use to transfer hardware information to linux kernel, 
* use one of them wil be OK
*/
#if !ES7243E_MATCH_DTS_EN
static struct i2c_board_info es7243e_i2c_board_info[] = {
#if ES7243E_CHANNELS_MAX > 0
	{I2C_BOARD_INFO("ES7243E_MicArray_0", ES7243E_I2C_CHIP_ADDRESS_0),},	//es7243e_0
#endif

#if ES7243E_CHANNELS_MAX > 2
	{I2C_BOARD_INFO("ES7243E_MicArray_1", ES7243E_I2C_CHIP_ADDRESS_1),},	//es7243e_1
#endif

#if ES7243E_CHANNELS_MAX > 4
	{I2C_BOARD_INFO("ES7243E_MicArray_2", ES7243E_I2C_CHIP_ADDRESS_2),},	//es7243e_2
#endif

#if ES7243E_CHANNELS_MAX > 6
	{I2C_BOARD_INFO("ES7243E_MicArray_3", ES7243E_I2C_CHIP_ADDRESS_3),},	//es7243e_3
#endif
#if ES7243E_CHANNELS_MAX > 8
	{I2C_BOARD_INFO("ES7243E_MicArray_4", ES7243E_I2C_CHIP_ADDRESS_4),},	//es7243e_4
#endif

#if ES7243E_CHANNELS_MAX > 10
	{I2C_BOARD_INFO("ES7243E_MicArray_5", ES7243E_I2C_CHIP_ADDRESS_5),},	//es7243e_5
#endif

#if ES7243E_CHANNELS_MAX > 12
	{I2C_BOARD_INFO("ES7243E_MicArray_6", ES7243E_I2C_CHIP_ADDRESS_6),},	//es7243e_6
#endif

#if ES7243E_CHANNELS_MAX > 14
	{I2C_BOARD_INFO("ES7243E_MicArray_7", ES7243E_I2C_CHIP_ADDRESS_7),},	//es7243e_7
#endif
};
#endif
static const struct i2c_device_id es7243e_i2c_id[] = {
#if ES7243E_CHANNELS_MAX > 0
	{"ES7243E_MicArray_0", 0},	//es7243e_0
#endif

#if ES7243E_CHANNELS_MAX > 2
	{"ES7243E_MicArray_1", 1},	//es7243e_1
#endif

#if ES7243E_CHANNELS_MAX > 4
	{"ES7243E_MicArray_2", 2},	//es7243e_2
#endif

#if ES7243E_CHANNELS_MAX > 6
	{"ES7243E_MicArray_3", 3},	//es7243e_3
#endif

#if ES7243E_CHANNELS_MAX > 8
	{"ES7243E_MicArray_4", 4},	//es7243e_4
#endif

#if ES7243E_CHANNELS_MAX > 10
	{"ES7243E_MicArray_5", 5},	//es7243e_5
#endif

#if ES7243E_CHANNELS_MAX > 12
	{"ES7243E_MicArray_6", 6},	//es7243e_6
#endif

#if ES7243E_CHANNELS_MAX > 14
	{"ES7243E_MicArray_7", 7},	//es7243e_7
#endif
	{}
};

MODULE_DEVICE_TABLE(i2c, es7243e_i2c_id);

static const struct of_device_id es7243e_dt_ids[] = {
#if ES7243E_CHANNELS_MAX > 0
	{.compatible = "ES7243E_MicArray_0",},	//es7243e_0
#endif

#if ES7243E_CHANNELS_MAX > 2
	{.compatible = "ES7243E_MicArray_1",},	//es7243e_1
#endif

#if ES7243E_CHANNELS_MAX > 4
	{.compatible = "ES7243E_MicArray_2",},	//es7243e_2
#endif

#if ES7243E_CHANNELS_MAX > 6
	{.compatible = "ES7243E_MicArray_3",},	//es7243e_3
#endif

#if ES7243E_CHANNELS_MAX > 8
	{.compatible = "ES7243E_MicArray_4",},	//es7243e_4
#endif

#if ES7243E_CHANNELS_MAX > 10
	{.compatible = "ES7243E_MicArray_5",},	//es7243e_5
#endif

#if ES7243E_CHANNELS_MAX > 12
	{.compatible = "ES7243E_MicArray_6",},	//es7243e_6
#endif

#if ES7243E_CHANNELS_MAX > 14
	{.compatible = "ES7243E_MicArray_7",},	//es7243e_7
#endif
	{}
};

MODULE_DEVICE_TABLE(of, es7243e_dt_ids);

static struct i2c_driver es7243e_i2c_driver = {
	.driver = {
		   .name = "es7243e",
		   .owner = THIS_MODULE,
#if ES7243E_MATCH_DTS_EN
		   .of_match_table = es7243e_dt_ids,
#endif
		   },
	.probe = es7243e_i2c_probe,
	.remove = __exit_p(es7243e_i2c_remove),
	.class = I2C_CLASS_HWMON,
	.id_table = es7243e_i2c_id,

#if !ES7243E_MATCH_DTS_EN
	.address_list = es7243e_i2c_addr,
	.detect = es7243e_i2c_detect,
#endif

};

static int __init es7243e_modinit(void)
{
	int ret;
#if 0
	int i;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
#endif

#if 0
	adapter = i2c_get_adapter(ES7243E_I2C_BUS_NUM);
	if (!adapter) {
		printk("i2c_get_adapter() fail!\n");
		return -ENODEV;
	}
	printk("%s() begin0000\n", __func__);

	for (i = 0; i < ES7243E_CHANNELS_MAX / 2; i++) {
		client = i2c_new_device(adapter, &es7243e_i2c_board_info[i]);
		printk("%s() i2c_new_device\n", __func__);
		if (!client)
			return -ENODEV;
	}
	i2c_put_adapter(adapter);
#endif
	ret = i2c_add_driver(&es7243e_i2c_driver);
	if (ret != 0)
		printk("Failed to register es7243 i2c driver : %d \n", ret);
	return ret;
}

//late_initcall(es7243e_modinit);
module_init(es7243e_modinit);
static void __exit es7243e_exit(void)
{
	i2c_del_driver(&es7243e_i2c_driver);
}

module_exit(es7243e_exit);
MODULE_DESCRIPTION("ASoC ES7243E audio adc driver");
MODULE_AUTHOR("David Yang <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL v2");
