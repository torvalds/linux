// SPDX-License-Identifier: GPL-2.0+
/*
 * rk3528_codec.c - Rockchip RK3528 SoC Codec Driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "rk3528_codec.h"

#define CODEC_DRV_NAME			"rk3528-acodec"

struct rk3528_codec_priv {
	const struct device *plat_dev;
	struct reset_control *reset;
	struct regmap *regmap;
	struct clk *pclk;
	struct clk *mclk;
	struct gpio_desc *pa_ctl_gpio;
	struct snd_soc_component *component;
	u32 pa_ctl_delay_ms;
};

static void rk3528_codec_pa_ctrl(struct rk3528_codec_priv *rk3528, bool on)
{
	if (!rk3528->pa_ctl_gpio)
		return;

	if (on) {
		gpiod_direction_output(rk3528->pa_ctl_gpio, on);
		msleep(rk3528->pa_ctl_delay_ms);
	} else {
		gpiod_direction_output(rk3528->pa_ctl_gpio, on);
	}
}

static int rk3528_codec_reset(struct snd_soc_component *component)
{
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	reset_control_assert(rk3528->reset);
	usleep_range(10000, 11000);		/* estimated value */
	reset_control_deassert(rk3528->reset);

	regmap_update_bits(rk3528->regmap, ACODEC_DIG00,
			   ACODEC_DAC_RST_MASK |
			   ACODEC_SYS_RST_MASK,
			   ACODEC_DAC_RST_N |
			   ACODEC_SYS_RST_N);
	regmap_update_bits(rk3528->regmap, ACODEC_DIG02,
			   ACODEC_DAC_I2S_RST_MASK,
			   ACODEC_DAC_I2S_RST_N);
	usleep_range(10000, 11000);		/* estimated value */
	regmap_update_bits(rk3528->regmap, ACODEC_DIG00,
			   ACODEC_DAC_RST_MASK |
			   ACODEC_SYS_RST_MASK,
			   ACODEC_DAC_RST_P |
			   ACODEC_SYS_RST_P);
	regmap_update_bits(rk3528->regmap, ACODEC_DIG02,
			   ACODEC_DAC_I2S_RST_MASK,
			   ACODEC_DAC_I2S_RST_P);

	return 0;
}

static int rk3528_codec_power_on(struct rk3528_codec_priv *rk3528)
{
	/* vendor step 0, Supply the power of the digital part and reset the audio codec. */
	/* vendor step 1 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_POP_CTRL_MASK,
			   ACODEC_DAC_L_POP_CTRL_ON);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_POP_CTRL_MASK,
			   ACODEC_DAC_R_POP_CTRL_ON);
	/* vendor step 2 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA01,
			   ACODEC_VREF_SEL_MASK, ACODEC_VREF_SEL(0xff));
	/* vendor step 3, supply the power of the analog part */
	/* vendor step 4 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA00,
			   ACODEC_VREF_MASK, ACODEC_VREF_EN);

	/* vendor step 5, Wait until the voltage of VCM keeps stable at the AVDD/2. */
	usleep_range(20000, 22000);
	/* vendor step 6 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA01,
			   ACODEC_VREF_SEL_MASK, ACODEC_VREF_SEL(2));
	return 0;
}

static int rk3528_codec_power_off(struct rk3528_codec_priv *rk3528)
{
	/*
	 * vendor step 0. Keep the power on and disable the DAC and ADC path.
	 */
	/* vendor step 1 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA01,
			   ACODEC_VREF_SEL_MASK, ACODEC_VREF_SEL(0xff));
	/* vendor step 2 */
	/* vendor step 3 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA00,
			   ACODEC_VREF_MASK, ACODEC_VREF_DIS);
	/* vendor step 4. Wait until the voltage of VCM keep stable at AGND. */
	usleep_range(20000, 22000);
	/* vendor step 5, power off the analog power supply */
	/* vendor step 6, power off the digital power supply */

	return 0;
}

static int rk3528_codec_dac_enable(struct rk3528_codec_priv *rk3528)
{
	/* vendor step 0, power up the codec and input the mute signal */
	/* vendor step 1 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA00,
			   ACODEC_IBIAS_DAC_MASK,
			   ACODEC_IBIAS_DAC_EN);
	/* vendor step 2 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_BUF_MASK,
			   ACODEC_DAC_L_BUF_EN);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_BUF_MASK,
			   ACODEC_DAC_R_BUF_EN);
	/* vendor step 3 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_POP_CTRL_MASK,
			   ACODEC_DAC_L_POP_CTRL_ON);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_POP_CTRL_MASK,
			   ACODEC_DAC_R_POP_CTRL_ON);
	/* vendor step 4 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_MASK,
			   ACODEC_LINEOUT_L_EN);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_MASK,
			   ACODEC_LINEOUT_R_EN);
	/* vendor step 5 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_INIT_MASK,
			   ACODEC_LINEOUT_L_INIT_WORK);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_INIT_MASK,
			   ACODEC_LINEOUT_R_INIT_WORK);
	/* vendor step 6 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_VREF_MASK,
			   ACODEC_DAC_L_VREF_EN);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_VREF_MASK,
			   ACODEC_DAC_R_VREF_EN);
	/* vendor step 7 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_CLK_MASK,
			   ACODEC_DAC_L_CLK_EN);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_CLK_MASK,
			   ACODEC_DAC_R_CLK_EN);
	/* vendor step 8 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_MASK,
			   ACODEC_DAC_L_EN);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_MASK,
			   ACODEC_DAC_R_EN);
	/* vendor step 9 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_INIT_MASK,
			   ACODEC_DAC_L_WORK);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_INIT_MASK,
			   ACODEC_DAC_R_WORK);
	/* vendor step 10 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_MUTE_MASK,
			   ACODEC_LINEOUT_L_WORK);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_MUTE_MASK,
			   ACODEC_LINEOUT_R_WORK);
	/* vendor step 11, select the gain */
	/* vendor step 12, play music */

	return 0;
}

static int rk3528_codec_dac_disable(struct rk3528_codec_priv *rk3528)
{
	/* vendor step 0, keep the dac channel work and input the mute signal */
	/* vendor step 1, select the gain */
	/* vendor step 2 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_MUTE_MASK,
			   ACODEC_LINEOUT_L_MUTE);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_MUTE_MASK,
			   ACODEC_LINEOUT_R_MUTE);
	/* vendor step 3 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_INIT_MASK,
			   ACODEC_LINEOUT_L_INIT);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_INIT_MASK,
			   ACODEC_LINEOUT_R_INIT);
	/* vendor step 4 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA09,
			   ACODEC_LINEOUT_L_MASK,
			   ACODEC_LINEOUT_L_DIS);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0D,
			   ACODEC_LINEOUT_R_MASK,
			   ACODEC_LINEOUT_R_DIS);
	/* vendor step 5 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_MASK,
			   ACODEC_DAC_L_DIS);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_MASK,
			   ACODEC_DAC_R_DIS);
	/* vendor step 6 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_CLK_MASK,
			   ACODEC_DAC_L_CLK_DIS);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_CLK_MASK,
			   ACODEC_DAC_R_CLK_DIS);
	/* vendor step 7 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_VREF_MASK,
			   ACODEC_DAC_L_VREF_DIS);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_VREF_MASK,
			   ACODEC_DAC_R_VREF_DIS);
	/* vendor step 8 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_POP_CTRL_MASK,
			   ACODEC_DAC_L_POP_CTRL_OFF);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_POP_CTRL_MASK,
			   ACODEC_DAC_R_POP_CTRL_OFF);
	/* vendor step 9 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_BUF_MASK,
			   ACODEC_DAC_L_BUF_DIS);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_BUF_MASK,
			   ACODEC_DAC_R_BUF_DIS);
	/* vendor step 10 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA00,
			   ACODEC_IBIAS_DAC_MASK,
			   ACODEC_IBIAS_DAC_DIS);
	/* vendor step 9 */
	regmap_update_bits(rk3528->regmap, ACODEC_ANA08,
			   ACODEC_DAC_L_INIT_MASK,
			   ACODEC_DAC_L_INIT);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0C,
			   ACODEC_DAC_R_INIT_MASK,
			   ACODEC_DAC_R_INIT);

	return 0;
}

static int rk3528_codec_dac_dig_config(struct rk3528_codec_priv *rk3528,
				       struct snd_pcm_hw_params *params)
{
	unsigned int dac_aif1 = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dac_aif1 |= ACODEC_DAC_I2S_16B;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dac_aif1 |= ACODEC_DAC_I2S_20B;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dac_aif1 |= ACODEC_DAC_I2S_24B;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dac_aif1 |= ACODEC_DAC_I2S_32B;
		break;
	default:
		return -EINVAL;
	}

	dac_aif1 |= ACODEC_DAC_I2S_I2S;
	regmap_update_bits(rk3528->regmap, ACODEC_DIG01,
			   ACODEC_DAC_I2S_WL_MASK |
			   ACODEC_DAC_I2S_FMT_MASK,
			   dac_aif1);
	regmap_update_bits(rk3528->regmap, ACODEC_DIG02,
			   ACODEC_DAC_I2S_RST_MASK,
			   ACODEC_DAC_I2S_RST_P);

	return 0;
}

static int rk3528_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);
	unsigned int dac_aif1 = 0, dac_aif2 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		dac_aif2 |= ACODEC_DAC_I2S_MST_FUNC_SLAVE;
		dac_aif2 |= ACODEC_DAC_I2S_MST_IO_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		dac_aif2 |= ACODEC_DAC_I2S_MST_FUNC_MASTER;
		dac_aif2 |= ACODEC_DAC_I2S_MST_IO_MASTER;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dac_aif1 |= ACODEC_DAC_I2S_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dac_aif1 |= ACODEC_DAC_I2S_LJM;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rk3528->regmap, ACODEC_DIG01,
			   ACODEC_DAC_I2S_FMT_MASK,
			   dac_aif1);
	regmap_update_bits(rk3528->regmap, ACODEC_DIG02,
			   ACODEC_DAC_I2S_MST_FUNC_MASK |
			   ACODEC_DAC_I2S_MST_IO_MASK,
			   dac_aif2);

	return 0;
}

static int rk3528_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mute) {
			/* Mute DAC LINEOUT */
			regmap_update_bits(rk3528->regmap,
					   ACODEC_ANA09,
					   ACODEC_LINEOUT_L_MUTE_MASK,
					   ACODEC_LINEOUT_L_MUTE);
			regmap_update_bits(rk3528->regmap,
					   ACODEC_ANA0D,
					   ACODEC_LINEOUT_R_MUTE_MASK,
					   ACODEC_LINEOUT_R_MUTE);
			rk3528_codec_pa_ctrl(rk3528, false);
		} else {
			/* Unmute DAC LINEOUT */
			regmap_update_bits(rk3528->regmap,
					   ACODEC_ANA09,
					   ACODEC_LINEOUT_L_MUTE_MASK,
					   ACODEC_LINEOUT_L_WORK);
			regmap_update_bits(rk3528->regmap,
					   ACODEC_ANA0D,
					   ACODEC_LINEOUT_R_MUTE_MASK,
					   ACODEC_LINEOUT_R_WORK);
			rk3528_codec_pa_ctrl(rk3528, true);
		}
	}

	return 0;
}

static int rk3528_codec_default_gains(struct rk3528_codec_priv *rk3528)
{
	/* Prepare DAC gains */
	/* set LINEOUT default gains */
	regmap_update_bits(rk3528->regmap, ACODEC_DIG06,
			   ACODEC_DAC_DIG_GAIN_MASK,
			   ACODEC_DAC_DIG_GAIN(ACODEC_DAC_DIG_0DB));
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0B,
			   ACODEC_LINEOUT_L_GAIN_MASK,
			   ACODEC_DAC_LINEOUT_GAIN_0DB);
	regmap_update_bits(rk3528->regmap, ACODEC_ANA0F,
			   ACODEC_LINEOUT_R_GAIN_MASK,
			   ACODEC_DAC_LINEOUT_GAIN_0DB);

	return 0;
}

static int rk3528_codec_open_playback(struct rk3528_codec_priv *rk3528)
{
	rk3528_codec_dac_enable(rk3528);

	return 0;
}

static int rk3528_codec_close_playback(struct rk3528_codec_priv *rk3528)
{
	rk3528_codec_dac_disable(rk3528);
	return 0;
}

static int rk3528_codec_dlp_down(struct rk3528_codec_priv *rk3528)
{
	return 0;
}

static int rk3528_codec_dlp_up(struct rk3528_codec_priv *rk3528)
{
	rk3528_codec_power_on(rk3528);

	return 0;
}

static int rk3528_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		rk3528_codec_open_playback(rk3528);
		rk3528_codec_dac_dig_config(rk3528, params);
	}

	return 0;
}

static void rk3528_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rk3528_codec_close_playback(rk3528);

	regcache_cache_only(rk3528->regmap, false);
	regcache_sync(rk3528->regmap);
}

static int rk3528_codec_prepare(struct rk3528_codec_priv *rk3528)
{
	/* Clear registers for ADC and DAC */
	rk3528_codec_close_playback(rk3528);
	rk3528_codec_default_gains(rk3528);

	return 0;
}

static int rk3528_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);
	int ret;

	if (!freq)
		return 0;

	ret = clk_set_rate(rk3528->mclk, freq);
	if (ret)
		dev_err(rk3528->plat_dev, "Failed to set mclk %d\n", ret);

	return ret;
}

static const struct snd_soc_dai_ops rk3528_dai_ops = {
	.hw_params = rk3528_hw_params,
	.set_fmt = rk3528_set_dai_fmt,
	.mute_stream = rk3528_mute_stream,
	.shutdown = rk3528_pcm_shutdown,
	.set_sysclk = rk3528_set_sysclk,
};

static struct snd_soc_dai_driver rk3528_dai[] = {
	{
		.name = "rk3528-hifi",
		.id = ACODEC_HIFI,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rk3528_dai_ops,
	},
};

static int rk3528_codec_probe(struct snd_soc_component *component)
{
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	rk3528->component = component;
	rk3528_codec_reset(component);
	rk3528_codec_dlp_up(rk3528);
	rk3528_codec_prepare(rk3528);
	regcache_cache_only(rk3528->regmap, false);
	regcache_sync(rk3528->regmap);

	return 0;
}

static void rk3528_codec_remove(struct snd_soc_component *component)
{
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	rk3528_codec_pa_ctrl(rk3528, false);
	rk3528_codec_power_off(rk3528);
	regcache_cache_only(rk3528->regmap, false);
	regcache_sync(rk3528->regmap);
}

static int rk3528_codec_suspend(struct snd_soc_component *component)
{
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);

	rk3528_codec_dlp_down(rk3528);
	regcache_cache_only(rk3528->regmap, true);
	clk_disable_unprepare(rk3528->mclk);
	clk_disable_unprepare(rk3528->pclk);

	return 0;
}

static int rk3528_codec_resume(struct snd_soc_component *component)
{
	struct rk3528_codec_priv *rk3528 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	ret = clk_prepare_enable(rk3528->pclk);
	if (ret < 0) {
		dev_err(rk3528->plat_dev,
			"Failed to enable acodec pclk: %d\n", ret);
		goto pclk_error;
	}

	ret = clk_prepare_enable(rk3528->mclk);
	if (ret < 0) {
		dev_err(rk3528->plat_dev,
			"Failed to enable acodec mclk: %d\n", ret);
		goto mclk_error;
	}

	regcache_cache_only(rk3528->regmap, false);
	ret = regcache_sync(rk3528->regmap);
	if (ret)
		goto reg_error;

	rk3528_codec_dlp_up(rk3528);

	return 0;
reg_error:
	clk_disable_unprepare(rk3528->mclk);
mclk_error:
	clk_disable_unprepare(rk3528->pclk);
pclk_error:
	return ret;
}

static const DECLARE_TLV_DB_SCALE(rk3528_codec_dac_lineout_gain_tlv,
				  -3900, 150, 600);

static const struct snd_kcontrol_new rk3528_codec_dapm_controls[] = {
	/* DAC LINEOUT */
	SOC_SINGLE_RANGE_TLV("DAC LEFT LINEOUT Volume",
			     ACODEC_ANA0B,
			     ACODEC_LINEOUT_L_GAIN_SHIFT,
			     ACODEC_DAC_LINEOUT_GAIN_MIN,
			     ACODEC_DAC_LINEOUT_GAIN_MAX,
			     0, rk3528_codec_dac_lineout_gain_tlv),
	SOC_SINGLE_RANGE_TLV("DAC RIGHT LINEOUT Volume",
			     ACODEC_ANA0F,
			     ACODEC_LINEOUT_R_GAIN_SHIFT,
			     ACODEC_DAC_LINEOUT_GAIN_MIN,
			     ACODEC_DAC_LINEOUT_GAIN_MAX,
			     0, rk3528_codec_dac_lineout_gain_tlv),
};

static const struct snd_soc_component_driver soc_codec_dev_rk3528 = {
	.probe = rk3528_codec_probe,
	.remove = rk3528_codec_remove,
	.suspend = rk3528_codec_suspend,
	.resume = rk3528_codec_resume,
	.controls = rk3528_codec_dapm_controls,
	.num_controls = ARRAY_SIZE(rk3528_codec_dapm_controls),
};

/* Set the default value or reset value */
static const struct reg_default rk3528_codec_reg_defaults[] = {
	{ ACODEC_DIG00, 0x71 },
	{ ACODEC_DIG03, 0x53 },
	{ ACODEC_DIG07, 0x03 },
	{ ACODEC_DIG08, 0xc3 },
	{ ACODEC_DIG09, 0x28 },
	{ ACODEC_DIG0A, 0x1 },
	{ ACODEC_DIG0B, 0x80 },
	{ ACODEC_DIG0D, 0xc3 },
	{ ACODEC_DIG0E, 0xc3 },
	{ ACODEC_DIG10, 0xf1 },
	{ ACODEC_DIG11, 0xf1 },
	{ ACODEC_ANA02, 0x77 },
	{ ACODEC_ANA08, 0x20 },
	{ ACODEC_ANA0A, 0x8 },
	{ ACODEC_ANA0C, 0x20 },
	{ ACODEC_ANA0E, 0x8 },
};

static bool rk3528_codec_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ACODEC_DIG00:
		return true;
	default:
		return false;
	}
	return true;
}

static const struct regmap_config rk3528_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = ACODEC_REG_MAX,
	.volatile_reg = rk3528_codec_volatile_reg,
	.reg_defaults = rk3528_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk3528_codec_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static const struct of_device_id rk3528_codec_of_match[] = {
	{ .compatible = "rockchip,rk3528-codec", },
	{},
};

MODULE_DEVICE_TABLE(of, rk3528_codec_of_match);

static int rk3528_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk3528_codec_priv *rk3528;
	struct resource *res;
	void __iomem *base;
	int ret;

	rk3528 = devm_kzalloc(&pdev->dev, sizeof(*rk3528), GFP_KERNEL);
	if (!rk3528)
		return -ENOMEM;

	rk3528->plat_dev = &pdev->dev;
	rk3528->reset = devm_reset_control_get_optional_exclusive(&pdev->dev, "acodec");
	if (IS_ERR(rk3528->reset))
		return PTR_ERR(rk3528->reset);

	rk3528->pa_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "pa-ctl",
						       GPIOD_OUT_LOW);
	if (IS_ERR(rk3528->pa_ctl_gpio)) {
		dev_err(&pdev->dev, "Unable to claim gpio pa-ctl\n");
		return -EINVAL;
	}

	if (rk3528->pa_ctl_gpio)
		of_property_read_u32(np, "pa-ctl-delay-ms",
				     &rk3528->pa_ctl_delay_ms);

	dev_info(&pdev->dev, "%s pa_ctl_gpio and pa_ctl_delay_ms: %d\n",
		rk3528->pa_ctl_gpio ? "Use" : "No use",
		rk3528->pa_ctl_delay_ms);

	/* Close external PA during startup. */
	rk3528_codec_pa_ctrl(rk3528, false);

	rk3528->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(rk3528->pclk)) {
		dev_err(&pdev->dev, "Can't get acodec pclk\n");
		return -EINVAL;
	}

	rk3528->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(rk3528->mclk)) {
		dev_err(&pdev->dev, "Can't get acodec mclk\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(rk3528->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk3528->mclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable acodec mclk: %d\n", ret);
		goto failed_1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		dev_err(&pdev->dev, "Failed to ioremap resource\n");
		goto failed;
	}

	rk3528->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &rk3528_codec_regmap_config);
	if (IS_ERR(rk3528->regmap)) {
		ret = PTR_ERR(rk3528->regmap);
		dev_err(&pdev->dev, "Failed to regmap mmio\n");
		goto failed;
	}

	platform_set_drvdata(pdev, rk3528);
	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rk3528,
					      rk3528_dai, ARRAY_SIZE(rk3528_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d\n", ret);
		goto failed;
	}

	return ret;

failed:
	clk_disable_unprepare(rk3528->mclk);
failed_1:
	clk_disable_unprepare(rk3528->pclk);

	return ret;
}

static int rk3528_platform_remove(struct platform_device *pdev)
{
	struct rk3528_codec_priv *rk3528 =
		(struct rk3528_codec_priv *)platform_get_drvdata(pdev);

	clk_disable_unprepare(rk3528->mclk);
	clk_disable_unprepare(rk3528->pclk);

	return 0;
}

static struct platform_driver rk3528_codec_driver = {
	.driver = {
		   .name = CODEC_DRV_NAME,
		   .of_match_table = of_match_ptr(rk3528_codec_of_match),
	},
	.probe = rk3528_platform_probe,
	.remove = rk3528_platform_remove,
};
module_platform_driver(rk3528_codec_driver);

MODULE_DESCRIPTION("ASoC rk3528 Codec Driver");
MODULE_AUTHOR("Jason Zhu <jason.zhu@rock-chips.com>");
MODULE_LICENSE("GPL");
