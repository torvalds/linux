// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"

#define AIU_I2S_SOURCE_DESC_MODE_8CH	BIT(0)
#define AIU_I2S_SOURCE_DESC_MODE_24BIT	BIT(5)
#define AIU_I2S_SOURCE_DESC_MODE_32BIT	BIT(9)
#define AIU_I2S_SOURCE_DESC_MODE_SPLIT	BIT(11)
#define AIU_RST_SOFT_I2S_FAST		BIT(0)

#define AIU_I2S_DAC_CFG_MSB_FIRST	BIT(2)
#define AIU_I2S_MISC_HOLD_EN		BIT(2)
#define AIU_CLK_CTRL_I2S_DIV_EN		BIT(0)
#define AIU_CLK_CTRL_I2S_DIV		GENMASK(3, 2)
#define AIU_CLK_CTRL_AOCLK_INVERT	BIT(6)
#define AIU_CLK_CTRL_LRCLK_INVERT	BIT(7)
#define AIU_CLK_CTRL_LRCLK_SKEW		GENMASK(9, 8)
#define AIU_CLK_CTRL_MORE_HDMI_AMCLK	BIT(6)
#define AIU_CLK_CTRL_MORE_I2S_DIV	GENMASK(5, 0)
#define AIU_CODEC_DAC_LRCLK_CTRL_DIV	GENMASK(11, 0)

static void aiu_encoder_i2s_divider_enable(struct snd_soc_component *component,
					   bool enable)
{
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV_EN,
				      enable ? AIU_CLK_CTRL_I2S_DIV_EN : 0);
}

static void aiu_encoder_i2s_hold(struct snd_soc_component *component,
				 bool enable)
{
	snd_soc_component_update_bits(component, AIU_I2S_MISC,
				      AIU_I2S_MISC_HOLD_EN,
				      enable ? AIU_I2S_MISC_HOLD_EN : 0);
}

static int aiu_encoder_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		aiu_encoder_i2s_hold(component, false);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		aiu_encoder_i2s_hold(component, true);
		return 0;

	default:
		return -EINVAL;
	}
}

static int aiu_encoder_i2s_setup_desc(struct snd_soc_component *component,
				      struct snd_pcm_hw_params *params)
{
	/* Always operate in split (classic interleaved) mode */
	unsigned int desc = AIU_I2S_SOURCE_DESC_MODE_SPLIT;
	unsigned int val;

	/* Reset required to update the pipeline */
	snd_soc_component_write(component, AIU_RST_SOFT, AIU_RST_SOFT_I2S_FAST);
	snd_soc_component_read(component, AIU_I2S_SYNC, &val);

	switch (params_physical_width(params)) {
	case 16: /* Nothing to do */
		break;

	case 32:
		desc |= (AIU_I2S_SOURCE_DESC_MODE_24BIT |
			 AIU_I2S_SOURCE_DESC_MODE_32BIT);
		break;

	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 2: /* Nothing to do */
		break;
	case 8:
		desc |= AIU_I2S_SOURCE_DESC_MODE_8CH;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_I2S_SOURCE_DESC,
				      AIU_I2S_SOURCE_DESC_MODE_8CH |
				      AIU_I2S_SOURCE_DESC_MODE_24BIT |
				      AIU_I2S_SOURCE_DESC_MODE_32BIT |
				      AIU_I2S_SOURCE_DESC_MODE_SPLIT,
				      desc);

	return 0;
}

static int aiu_encoder_i2s_set_clocks(struct snd_soc_component *component,
				      struct snd_pcm_hw_params *params)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	unsigned int srate = params_rate(params);
	unsigned int fs, bs;

	/* Get the oversampling factor */
	fs = DIV_ROUND_CLOSEST(clk_get_rate(aiu->i2s.clks[MCLK].clk), srate);

	if (fs % 64)
		return -EINVAL;

	/* Send data MSB first */
	snd_soc_component_update_bits(component, AIU_I2S_DAC_CFG,
				      AIU_I2S_DAC_CFG_MSB_FIRST,
				      AIU_I2S_DAC_CFG_MSB_FIRST);

	/* Set bclk to lrlck ratio */
	snd_soc_component_update_bits(component, AIU_CODEC_DAC_LRCLK_CTRL,
				      AIU_CODEC_DAC_LRCLK_CTRL_DIV,
				      FIELD_PREP(AIU_CODEC_DAC_LRCLK_CTRL_DIV,
						 64 - 1));

	/* Use CLK_MORE for mclk to bclk divider */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_I2S_DIV, 0);

	/*
	 * NOTE: this HW is odd.
	 * In most configuration, the i2s divider is 'mclk / blck'.
	 * However, in 16 bits - 8ch mode, this factor needs to be
	 * increased by 50% to get the correct output rate.
	 * No idea why !
	 */
	bs = fs / 64;
	if (params_width(params) == 16 && params_channels(params) == 8) {
		if (bs % 2) {
			dev_err(component->dev,
				"Cannot increase i2s divider by 50%%\n");
			return -EINVAL;
		}
		bs += bs / 2;
	}

	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_I2S_DIV,
				      FIELD_PREP(AIU_CLK_CTRL_MORE_I2S_DIV,
						 bs - 1));

	/* Make sure amclk is used for HDMI i2s as well */
	snd_soc_component_update_bits(component, AIU_CLK_CTRL_MORE,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK,
				      AIU_CLK_CTRL_MORE_HDMI_AMCLK);

	return 0;
}

static int aiu_encoder_i2s_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	/* Disable the clock while changing the settings */
	aiu_encoder_i2s_divider_enable(component, false);

	ret = aiu_encoder_i2s_setup_desc(component, params);
	if (ret) {
		dev_err(dai->dev, "setting i2s desc failed\n");
		return ret;
	}

	ret = aiu_encoder_i2s_set_clocks(component, params);
	if (ret) {
		dev_err(dai->dev, "setting i2s clocks failed\n");
		return ret;
	}

	aiu_encoder_i2s_divider_enable(component, true);

	return 0;
}

static int aiu_encoder_i2s_hw_free(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	aiu_encoder_i2s_divider_enable(component, false);

	return 0;
}

static int aiu_encoder_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	unsigned int inv = fmt & SND_SOC_DAIFMT_INV_MASK;
	unsigned int val = 0;
	unsigned int skew;

	/* Only CPU Master / Codec Slave supported ATM */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	if (inv == SND_SOC_DAIFMT_NB_IF ||
	    inv == SND_SOC_DAIFMT_IB_IF)
		val |= AIU_CLK_CTRL_LRCLK_INVERT;

	if (inv == SND_SOC_DAIFMT_IB_NF ||
	    inv == SND_SOC_DAIFMT_IB_IF)
		val |= AIU_CLK_CTRL_AOCLK_INVERT;

	/* Signal skew */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* Invert sample clock for i2s */
		val ^= AIU_CLK_CTRL_LRCLK_INVERT;
		skew = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		skew = 0;
		break;
	default:
		return -EINVAL;
	}

	val |= FIELD_PREP(AIU_CLK_CTRL_LRCLK_SKEW, skew);
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_LRCLK_INVERT |
				      AIU_CLK_CTRL_AOCLK_INVERT |
				      AIU_CLK_CTRL_LRCLK_SKEW,
				      val);

	return 0;
}

static int aiu_encoder_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (WARN_ON(clk_id != 0))
		return -EINVAL;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = clk_set_rate(aiu->i2s.clks[MCLK].clk, freq);
	if (ret)
		dev_err(dai->dev, "Failed to set sysclk to %uHz", freq);

	return ret;
}

static const unsigned int hw_channels[] = {2, 8};
static const struct snd_pcm_hw_constraint_list hw_channel_constraints = {
	.list = hw_channels,
	.count = ARRAY_SIZE(hw_channels),
	.mask = 0,
};

static int aiu_encoder_i2s_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/* Make sure the encoder gets either 2 or 8 channels */
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &hw_channel_constraints);
	if (ret) {
		dev_err(dai->dev, "adding channels constraints failed\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(aiu->i2s.clk_num, aiu->i2s.clks);
	if (ret)
		dev_err(dai->dev, "failed to enable i2s clocks\n");

	return ret;
}

static void aiu_encoder_i2s_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);

	clk_bulk_disable_unprepare(aiu->i2s.clk_num, aiu->i2s.clks);
}

const struct snd_soc_dai_ops aiu_encoder_i2s_dai_ops = {
	.trigger	= aiu_encoder_i2s_trigger,
	.hw_params	= aiu_encoder_i2s_hw_params,
	.hw_free	= aiu_encoder_i2s_hw_free,
	.set_fmt	= aiu_encoder_i2s_set_fmt,
	.set_sysclk	= aiu_encoder_i2s_set_sysclk,
	.startup	= aiu_encoder_i2s_startup,
	.shutdown	= aiu_encoder_i2s_shutdown,
};

/*
static struct snd_soc_dai_driver aiu_encoder_i2s_dai_drv = {
	.name = "AIU I2S ENCODER",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE)
	},
	.ops = &aiu_encoder_i2s_dai_ops,
};

int aiu_encoder_i2s_component_probe(struct snd_soc_component *component)
{
	struct device *dev = component->dev;
	struct regmap *map;

	map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "Could not get regmap\n");
		return PTR_ERR(map);
	}

	snd_soc_component_init_regmap(component, map);

	return 0;
}

static const struct snd_soc_component_driver aiu_encoder_i2s_component = {
	.probe 			= aiu_encoder_i2s_component_probe,
};

static int aiu_encoder_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aiu_encoder_i2s *encoder;

	encoder = devm_kzalloc(dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return -ENOMEM;
	platform_set_drvdata(pdev, encoder);

	encoder->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(encoder->pclk)) {
		if (PTR_ERR(encoder->pclk) != -EPROBE_DEFER)
			dev_err(dev,
				"Can't get the peripheral clock\n");
		return PTR_ERR(encoder->pclk);
	}

	encoder->aoclk = devm_clk_get(dev, "aoclk");
	if (IS_ERR(encoder->aoclk)) {
		if (PTR_ERR(encoder->aoclk) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the ao clock\n");
		return PTR_ERR(encoder->aoclk);
	}

	encoder->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(encoder->mclk)) {
		if (PTR_ERR(encoder->mclk) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the i2s m\n");
		return PTR_ERR(encoder->mclk);
	}

	return devm_snd_soc_register_component(dev, &aiu_encoder_i2s_component,
					       &aiu_encoder_i2s_dai_drv, 1);
}

static const struct of_device_id aiu_encoder_i2s_of_match[] = {
	{ .compatible = "amlogic,aiu-i2s-encode", },
	{}
};
MODULE_DEVICE_TABLE(of, aiu_encoder_i2s_of_match);

static struct platform_driver aiu_encoder_i2s_pdrv = {
	.probe = aiu_encoder_i2s_probe,
	.driver = {
		.name = "meson-aiu-i2s-encode",
		.of_match_table = aiu_encoder_i2s_of_match,
	},
};
module_platform_driver(aiu_encoder_i2s_pdrv);

MODULE_DESCRIPTION("Meson AIU Encoder I2S Driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
*/
