/*
 * rk3036_codec.c
 *
 * Driver for rockchip rk3036 codec
 * Copyright (C) 2014
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <asm/dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include "rk3036_codec.h"

#define DEBUGOPEN 0

#if DEBUGOPEN
#define dbg_codec(level, fmt, arg...) do {	\
	if (debug >= level)					\
		printk(fmt , ## arg); }				\
	while (0)

#else
#define	dbg_codec(level, fmt, arg...)
#endif

#define INVALID_GPIO -1
#define SPK_CTRL_OPEN	1
#define SPK_CTRL_CLOSE	0

/* volume setting
 *  0: -39dB
 *  26: 0dB
 *  31: 6dB
 *  Step: 1.5dB
*/
#define  OUT_VOLUME    31

/*with capacity or not*/
#define WITH_CAP

struct rk3036_codec_priv {
	void __iomem	*regbase;
	struct snd_soc_codec *codec;

	unsigned int stereo_sysclk;
	unsigned int rate;

	struct delayed_work codec_delayed_work;
	int spk_ctl_gpio;
	int delay_time;

	struct clk	*pclk;
};
static struct rk3036_codec_priv *rk3036_priv;

static const unsigned int rk3036_reg_defaults[RK3036_CODEC_REG28+1] = {
	[RK3036_CODEC_RESET] = 0x03,
	[RK3036_CODEC_REG03] = 0x00,
	[RK3036_CODEC_REG04] = 0x50,
	[RK3036_CODEC_REG05] = 0x0E,
	[RK3036_CODEC_REG22] = 0x00,
	[RK3036_CODEC_REG23] = 0x00,
	[RK3036_CODEC_REG24] = 0x00,
	[RK3036_CODEC_REG25] = 0x00,
	[RK3036_CODEC_REG26] = 0x00,
	[RK3036_CODEC_REG27] = 0x05,
	[RK3036_CODEC_REG28] = 0x00,
};

/* function declare: */
static int rk3036_codec_register(
	struct snd_soc_codec *codec, unsigned int reg);
static int rk3036_volatile_register(
	struct snd_soc_codec *codec, unsigned int reg);
static int rk3036_set_bias_level(
	struct snd_soc_codec *codec, enum snd_soc_bias_level level);
static unsigned int rk3036_codec_read(
	struct snd_soc_codec *codec, unsigned int reg);
static inline void rk3036_write_reg_cache(
	struct snd_soc_codec *codec, unsigned int reg, unsigned int value);

static inline unsigned int rk3036_read_reg_cache(struct snd_soc_codec *
	codec, unsigned int reg)
{
	unsigned int *cache = codec->reg_cache;

	if (rk3036_codec_register(codec, reg))
		return  cache[reg];

	dbg_codec(2, "%s : reg error!\n", __func__);

	return -EINVAL;
}

static inline void rk3036_write_reg_cache(struct snd_soc_codec *
	codec, unsigned int reg, unsigned int value)
{
	unsigned int *cache = codec->reg_cache;

	if (rk3036_codec_register(codec, reg)) {
		cache[reg] = value;
		return;
	}

	dbg_codec(2, "%s : reg error!\n", __func__);
}

static int rk3036_reset(struct snd_soc_codec *codec)
{
	writel(0x00, rk3036_priv->regbase+RK3036_CODEC_RESET);
	mdelay(10);
	writel(0x03, rk3036_priv->regbase+RK3036_CODEC_RESET);
	mdelay(10);
	memcpy(codec->
		reg_cache, rk3036_reg_defaults,
		sizeof(rk3036_reg_defaults));
	return 0;
}

static int rk3036_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rk3036_codec_priv *rk3036 = rk3036_priv;
	unsigned int dac_aif1 = 0, dac_aif2  = 0;

	if (!rk3036) {
		dbg_codec(2, "%s : rk3036 is NULL\n", __func__);
		return -EINVAL;
	}
	dac_aif2 |= RK3036_CR05_FRAMEH_32BITS;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dac_aif1 |= RK3036_CR04_HFVALID_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dac_aif1 |= RK3036_CR04_HFVALID_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dac_aif1 |= RK3036_CR04_HFVALID_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dac_aif1 |= RK3036_CR04_HFVALID_32BITS;
		break;
	default:
		return -EINVAL;
	}

	dac_aif1 |= RK3036_CR04_LR_SWAP_DIS;
	dac_aif2 |= RK3036_CR05_DAC_RESET_DIS;

	snd_soc_update_bits(
		codec, RK3036_CODEC_REG04,
		RK3036_CR04_HFVALID_MASK
		|RK3036_CR04_LR_SWAP_MASK, dac_aif1);
	snd_soc_update_bits(
		codec, RK3036_CODEC_REG05,
		RK3036_CR05_FRAMEH_MASK
		|RK3036_CR05_DAC_RESET_MASK, dac_aif2);

	return 0;
}

static int rk3036_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int fmt_ms = 0,  dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		fmt_ms |= RK3036_CR03_DIRECTION_IN|RK3036_CR03_I2SMODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		fmt_ms |= RK3036_CR03_DIRECTION_IOUT|RK3036_CR03_I2SMODE_MASTER;
		break;
	default:
		dbg_codec(2, "%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		dac_aif1 |= RK3036_CR04_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		dac_aif1 |= RK3036_CR04_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dac_aif1 |= RK3036_CR04_MODE_RIGHT;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dac_aif1 |= RK3036_CR04_MODE_LEFT;
		break;
	default:
		dbg_codec(2, "%s : set format failed!\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		dac_aif1 |= RK3036_CR04_I2SLRC_NORMAL;
		dac_aif2 |= RK3036_CR05_BCLKPOL_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dac_aif1 |= RK3036_CR04_I2SLRC_REVERSAL;
		dac_aif2 |= RK3036_CR05_BCLKPOL_REVERSAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dac_aif1 |= RK3036_CR04_I2SLRC_REVERSAL;
		dac_aif2 |= RK3036_CR05_BCLKPOL_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		dac_aif1 |= RK3036_CR04_I2SLRC_NORMAL;
		dac_aif2 |= RK3036_CR05_BCLKPOL_REVERSAL;
		break;
	default:
		dbg_codec(2, "%s : set dai format failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_update_bits(
		codec, RK3036_CODEC_REG03,
		RK3036_CR03_DIRECTION_MASK
		|RK3036_CR03_I2SMODE_MASK, fmt_ms);
	snd_soc_update_bits(
		codec, RK3036_CODEC_REG04,
		RK3036_CR04_I2SLRC_MASK
		| RK3036_CR04_MODE_MASK, dac_aif1);
	snd_soc_update_bits(
		codec, RK3036_CODEC_REG05,
		RK3036_CR05_BCLKPOL_MASK, dac_aif2);
	return 0;
}

static int rk3036_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct rk3036_codec_priv *rk3036 = rk3036_priv;

	if (!rk3036) {
		dbg_codec(2, "%s : rk3036 is NULL\n", __func__);
		return -EINVAL;
	}

	rk3036->stereo_sysclk = freq;

	return 0;
}

static int rk3036_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val;

	if (mute) {
		val = snd_soc_read(codec, RK3036_CODEC_REG27);
		if (val & (RK3036_CR27_HPOUTL_G_WORK
			|RK3036_CR27_HPOUTR_G_WORK)) {
			val &= ~RK3036_CR27_HPOUTL_G_WORK;
			val &= ~RK3036_CR27_HPOUTR_G_WORK;
			snd_soc_write(codec, RK3036_CODEC_REG27, val);
		}
	} else {
		val = snd_soc_read(codec, RK3036_CODEC_REG27);
		if ((val | ~RK3036_CR27_HPOUTL_G_WORK) || (val
			| ~RK3036_CR27_HPOUTR_G_WORK)) {
			val |= RK3036_CR27_HPOUTL_G_WORK
				|RK3036_CR27_HPOUTR_G_WORK;
			snd_soc_write(codec, RK3036_CODEC_REG27, val);
		}
	}
	return 0;
}

static struct rk3036_reg_val_typ playback_power_up_list[] = {
	/*01*/{RK3036_CODEC_REG24,
		RK3036_CR24_DAC_SOURCE_WORK},
	/* open current source. */

	/*02*/{RK3036_CODEC_REG22,
	RK3036_CR22_DACL_PATH_REFV_WORK|RK3036_CR22_DACR_PATH_REFV_WORK},
	/* power on dac path reference voltage. */

	/*03*/{RK3036_CODEC_REG27,
	RK3036_CR27_HPOUTL_POP_WORK|RK3036_CR27_HPOUTR_POP_WORK},
	/* pop precharge work. */

	/*04*/{RK3036_CODEC_REG23,
	RK3036_CR23_HPOUTL_EN_WORK|RK3036_CR23_HPOUTR_EN_WORK},
	/* start-up HPOUTL HPOUTR */

	/*05*/{RK3036_CODEC_REG23,
	RK3036_CR23_HPOUTL_EN_WORK|RK3036_CR23_HPOUTR_EN_WORK
	|RK3036_CR23_HPOUTL_WORK|RK3036_CR23_HPOUTR_WORK},
	/* end the init state of HPOUTL HPOUTR */

	/*06*/{RK3036_CODEC_REG24,
	RK3036_CR24_DAC_SOURCE_WORK
	|RK3036_CR24_DAC_PRECHARGE
	|RK3036_CR24_DACL_REFV_WORK|RK3036_CR24_DACR_REFV_WORK},
	/* start-up special ref_v of DACL DACR */

	/*07*/{RK3036_CODEC_REG22,
	RK3036_CR22_DACL_PATH_REFV_WORK|RK3036_CR22_DACR_PATH_REFV_WORK
	|RK3036_CR22_DACL_CLK_WORK|RK3036_CR22_DACR_CLK_WORK},
	/* start-up clock modul of LR channel */

	/*08*/{RK3036_CODEC_REG22,
	RK3036_CR22_DACL_PATH_REFV_WORK|RK3036_CR22_DACR_PATH_REFV_WORK
	|RK3036_CR22_DACL_CLK_WORK|RK3036_CR22_DACR_CLK_WORK
	|RK3036_CR22_DACL_WORK|RK3036_CR22_DACR_WORK},
	/* start-up DACL DACR module */

	/*09*/{RK3036_CODEC_REG27,
	RK3036_CR27_HPOUTL_POP_WORK|RK3036_CR27_HPOUTR_POP_WORK
	|RK3036_CR27_DACL_WORK|RK3036_CR27_DACR_WORK},
	/* end the init state of DACL DACR */

	/*10*/{RK3036_CODEC_REG25, OUT_VOLUME},
	/*11*/{RK3036_CODEC_REG26, OUT_VOLUME},

	/*12*/{RK3036_CODEC_REG24,
	RK3036_CR24_DAC_SOURCE_WORK
	|RK3036_CR24_DAC_PRECHARGE
	|RK3036_CR24_DACL_REFV_WORK|RK3036_CR24_DACR_REFV_WORK
	|RK3036_CR24_VOUTL_ZEROD_WORK|RK3036_CR24_VOUTR_ZEROD_WORK},
	/* according to the need, open the zero-crossing detection function. */

	/*13*/{RK3036_CODEC_REG27,
	RK3036_CR27_HPOUTL_POP_WORK|RK3036_CR27_HPOUTR_POP_WORK
	|RK3036_CR27_DACL_WORK|RK3036_CR27_DACR_WORK
	|RK3036_CR27_HPOUTL_G_WORK|RK3036_CR27_HPOUTR_G_WORK},
	/* end initial status of HPOUTL HPOUTR module,start normal output. */
};

#define PLAYBACK_POWER_UP_LIST_LEN ARRAY_SIZE(playback_power_up_list)

static int rk3036_codec_power_up(void)
{
	struct snd_soc_codec *codec = rk3036_priv->codec;
	int i, volume = 0;

	if (!rk3036_priv || !rk3036_priv->codec)
		return -EINVAL;

	for (i = 0; i < PLAYBACK_POWER_UP_LIST_LEN; i++) {
		if ((playback_power_up_list[i].reg ==
			RK3036_CODEC_REG25) && (volume < OUT_VOLUME)) {
			snd_soc_write(codec, RK3036_CODEC_REG25, volume);
			snd_soc_write(codec, RK3036_CODEC_REG26, volume);
			volume++;
			mdelay(10);
			i--;
		} else {
			snd_soc_write(codec, playback_power_up_list[i].
				reg, playback_power_up_list[i].value);
			mdelay(1);
		}
	}

	return 0;
}

#define RK3036_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK3036_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)


static struct snd_soc_dai_ops rk3036_dai_ops = {
	.hw_params	= rk3036_hw_params,
	.set_fmt	= rk3036_set_dai_fmt,
	.set_sysclk	= rk3036_set_dai_sysclk,
	.digital_mute	= rk3036_digital_mute,
};

static struct snd_soc_dai_driver rk3036_dai[] = {
	{
		.name = "rk3036-voice",
		.id = RK3036_VOICE,
		.playback = {
			.stream_name = "RK3036 CODEC PCM",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3036_PLAYBACK_RATES,
			.formats = RK3036_FORMATS,
		},
		.ops = &rk3036_dai_ops,
	},
};

static unsigned int rk3036_codec_read(struct snd_soc_codec *
	codec, unsigned int reg)
{
	unsigned int value;

	if (!rk3036_priv) {
		dbg_codec(2, "%s : rk3036 is NULL\n", __func__);
		return -EINVAL;
	}

	if (!rk3036_codec_register(codec, reg)) {
		dbg_codec(2, "%s : reg error!\n", __func__);
		return -EINVAL;
	}

	value = readl_relaxed(rk3036_priv->regbase+reg);
	dbg_codec(2, "%s : reg = 0x%x, val= 0x%x\n", __func__, reg, value);

	return value;
}

static int rk3036_hw_write(const struct i2c_client *
	client, const char *buf, int count)
{
	unsigned int reg, value;

	if (!rk3036_priv || !rk3036_priv->codec)
		return -EINVAL;

	if (count == 2) {
		reg = (unsigned int)buf[0];
		value = (unsigned int)buf[1];
		writel(value, rk3036_priv->regbase+reg);
	} else {
		dbg_codec(2, "%s : i2c len error\n", __func__);
	}

	return  count;
}

static int rk3036_codec_write(struct snd_soc_codec *
	codec, unsigned int reg, unsigned int value)
{
	int new_value = -1;

	if (!rk3036_priv) {
		dbg_codec(2, "%s : rk3036 is NULL\n", __func__);
		return -EINVAL;
	} else if (!rk3036_codec_register(codec, reg)) {
		dbg_codec(2, "%s : reg error!\n", __func__);
		return -EINVAL;
	}

	/*new_value = rk3036_set_init_value(codec, reg, value);*/
	if (new_value == -1) {
		writel(value, rk3036_priv->regbase+reg);
		rk3036_write_reg_cache(codec, reg, value);
	}

	return 0;
}

static void spk_ctrl_fun(int status)
{
	if (rk3036_priv == NULL)
		return;

	if (rk3036_priv->spk_ctl_gpio != INVALID_GPIO)
		gpio_set_value(rk3036_priv->spk_ctl_gpio, status);
}

static void rk3036_delayedwork_fun(struct work_struct *work)
{
	if (rk3036_priv == NULL)
		return;
	dbg_codec(2, "rk3036_delayedwork_fun\n");
	spk_ctrl_fun(SPK_CTRL_OPEN);
}

static int rk3036_probe(struct snd_soc_codec *codec)
{
	struct rk3036_codec_priv *rk3036_codec
		= snd_soc_codec_get_drvdata(codec);
	unsigned int val;
	int ret;

	rk3036_codec->codec = codec;

	clk_prepare_enable(rk3036_codec->pclk);

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret != 0)
		goto err__;

	codec->hw_read = rk3036_codec_read;
	codec->hw_write = (hw_write_t)rk3036_hw_write;
	codec->read = rk3036_codec_read;
	codec->write = rk3036_codec_write;

	INIT_DELAYED_WORK(&rk3036_codec->
		codec_delayed_work, rk3036_delayedwork_fun);

	val = snd_soc_read(codec, RK3036_CODEC_RESET);
	if (val != rk3036_reg_defaults[RK3036_CODEC_RESET]) {
		ret = -ENODEV;
		goto err__;
	}

	/* config i2s output to acodec module. */
	val = readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_CON0);
	writel_relaxed(val | 0x04000400, RK_GRF_VIRT + RK3036_GRF_SOC_CON0);

	rk3036_reset(codec);

	snd_soc_write(codec, RK3036_CODEC_REG24, RK3036_CR24_DAC_SOURCE_STOP
						| RK3036_CR24_DAC_PRECHARGE
						| RK3036_CR24_DACL_REFV_STOP
						| RK3036_CR24_DACR_REFV_STOP
						| RK3036_CR24_VOUTL_ZEROD_STOP
						| RK3036_CR24_VOUTR_ZEROD_STOP);
#ifdef WITH_CAP
	/*set for capacity output,clear up noise*/
	snd_soc_write(codec, RK3036_CODEC_REG28, 0x37);
	mdelay(50);
	/*snd_soc_write(codec, 0xbc,0x28);*/
#endif

	rk3036_codec_power_up();

	schedule_delayed_work(&rk3036_codec->
		codec_delayed_work, msecs_to_jiffies(5000));
	codec->dapm.bias_level = SND_SOC_BIAS_PREPARE;

	return 0;

err__:
	dbg_codec(2, "%s err ret=%d\n", __func__, ret);
	return ret;
}

static int rk3036_remove(struct snd_soc_codec *codec)
{
	if (!rk3036_priv) {
		dbg_codec(2, "%s : rk3036_priv is NULL\n", __func__);
		return 0;
	}

	spk_ctrl_fun(SPK_CTRL_CLOSE);

	mdelay(10);
	snd_soc_write(codec, RK3036_CODEC_RESET, 0xfc);
	mdelay(10);
	snd_soc_write(codec, RK3036_CODEC_RESET, 0x3);
	mdelay(10);

	return 0;
}

static int rk3036_suspend(struct snd_soc_codec *codec)
{
	rk3036_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rk3036_resume(struct snd_soc_codec *codec)
{
	rk3036_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int rk3036_set_bias_level(struct snd_soc_codec *codec, enum
	snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int rk3036_volatile_register(struct snd_soc_codec *
				codec, unsigned int reg)
{
	switch (reg) {
	case RK3036_CODEC_RESET:
		return 1;
	default:
		return 0;
	}
}

static int rk3036_codec_register(struct snd_soc_codec *codec, unsigned int reg)
{
	switch (reg) {
	case RK3036_CODEC_RESET:
	case RK3036_CODEC_REG03:
	case RK3036_CODEC_REG04:
	case RK3036_CODEC_REG05:
	case RK3036_CODEC_REG22:
	case RK3036_CODEC_REG23:
	case RK3036_CODEC_REG24:
	case RK3036_CODEC_REG25:
	case RK3036_CODEC_REG26:
	case RK3036_CODEC_REG27:
	case RK3036_CODEC_REG28:
		return 1;
	default:
		return 0;
	}
}

static struct snd_soc_codec_driver soc_codec_dev_rk3036 = {
	.probe = rk3036_probe,
	.remove = rk3036_remove,
	.suspend = rk3036_suspend,
	.resume = rk3036_resume,
	.set_bias_level = rk3036_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(rk3036_reg_defaults),
	.reg_word_size = sizeof(unsigned int),
	.reg_cache_default = rk3036_reg_defaults,
	.volatile_register = rk3036_volatile_register,
	.readable_register = rk3036_codec_register,
	.reg_cache_step = sizeof(unsigned int),
};

static int rk3036_platform_probe(struct platform_device *pdev)
{
	struct device_node *rk3036_np = pdev->dev.of_node;
	struct rk3036_codec_priv *rk3036;
	struct resource *res;
	int ret;

	rk3036 = devm_kzalloc(&pdev->dev, sizeof(*rk3036), GFP_KERNEL);
	if (!rk3036) {
		dbg_codec(2, "%s : rk3036 priv kzalloc failed!\n", __func__);
		return -ENOMEM;
	}
	rk3036_priv = rk3036;
	platform_set_drvdata(pdev, rk3036);

	rk3036->spk_ctl_gpio = of_get_named_gpio(rk3036_np, "spk_ctl_io", 0);
	if (!gpio_is_valid(rk3036->spk_ctl_gpio)) {
		dbg_codec(2, "invalid reset_gpio: %d\n", rk3036->spk_ctl_gpio);
		ret = -ENOENT;
		goto err__;
	}

	ret = devm_gpio_request(&pdev->dev, rk3036->spk_ctl_gpio, "spk_ctl");
	if (ret < 0) {
		dbg_codec(2, "rk3036_platform_probe spk_ctl_gpio fail\n");
		goto err__;
	}

	gpio_direction_output(rk3036->spk_ctl_gpio, SPK_CTRL_CLOSE);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk3036->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rk3036->regbase))
		return PTR_ERR(rk3036->regbase);

	rk3036->pclk = devm_clk_get(&pdev->dev, "g_pclk_acodec");
	if (IS_ERR(rk3036->pclk)) {
		dev_err(&pdev->dev, "Unable to get acodec hclk\n");
		ret = -ENXIO;
		goto err__;
	}

	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_rk3036,
				rk3036_dai, ARRAY_SIZE(rk3036_dai));

err__:
	platform_set_drvdata(pdev, NULL);
	rk3036_priv = NULL;
	return ret;
}

static int rk3036_platform_remove(struct platform_device *pdev)
{
	rk3036_priv = NULL;
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

void rk3036_platform_shutdown(struct platform_device *pdev)
{
	if (!rk3036_priv || !rk3036_priv->codec)
		return;

	spk_ctrl_fun(SPK_CTRL_CLOSE);

	mdelay(10);
	writel(0xfc, rk3036_priv->regbase+RK3036_CODEC_RESET);
	mdelay(10);
	writel(0x03, rk3036_priv->regbase+RK3036_CODEC_RESET);
}

#ifdef CONFIG_OF
static const struct of_device_id rk3036codec_of_match[] = {
		{ .compatible = "rk3036-codec"},
		{},
};
MODULE_DEVICE_TABLE(of, rk3036codec_of_match);
#endif

static struct platform_driver rk3036_codec_driver = {
	.driver = {
		   .name = "rk3036-codec",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3036codec_of_match),
		   },
	.probe = rk3036_platform_probe,
	.remove = rk3036_platform_remove,
	.shutdown = rk3036_platform_shutdown,
};
module_platform_driver(rk3036_codec_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP i2s ASoC Interface");
MODULE_LICENSE("GPL");
