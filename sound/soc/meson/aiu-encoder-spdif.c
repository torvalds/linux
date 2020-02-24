// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <sound/pcm_params.h>
#include <sound/pcm_iec958.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "aiu.h"

#define AIU_958_MISC_NON_PCM		BIT(0)
#define AIU_958_MISC_MODE_16BITS	BIT(1)
#define AIU_958_MISC_16BITS_ALIGN	GENMASK(6, 5)
#define AIU_958_MISC_MODE_32BITS	BIT(7)
#define AIU_958_MISC_U_FROM_STREAM	BIT(12)
#define AIU_958_MISC_FORCE_LR		BIT(13)
#define AIU_958_CTRL_HOLD_EN		BIT(0)
#define AIU_CLK_CTRL_958_DIV_EN		BIT(1)
#define AIU_CLK_CTRL_958_DIV		GENMASK(5, 4)
#define AIU_CLK_CTRL_958_DIV_MORE	BIT(12)

#define AIU_CS_WORD_LEN			4
#define AIU_958_INTERNAL_DIV		2

static void
aiu_encoder_spdif_divider_enable(struct snd_soc_component *component,
				 bool enable)
{
	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_958_DIV_EN,
				      enable ? AIU_CLK_CTRL_958_DIV_EN : 0);
}

static void aiu_encoder_spdif_hold(struct snd_soc_component *component,
				   bool enable)
{
	snd_soc_component_update_bits(component, AIU_958_CTRL,
				      AIU_958_CTRL_HOLD_EN,
				      enable ? AIU_958_CTRL_HOLD_EN : 0);
}

static int
aiu_encoder_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		aiu_encoder_spdif_hold(component, false);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		aiu_encoder_spdif_hold(component, true);
		return 0;

	default:
		return -EINVAL;
	}
}

static int aiu_encoder_spdif_setup_cs_word(struct snd_soc_component *component,
					   struct snd_pcm_hw_params *params)
{
	u8 cs[AIU_CS_WORD_LEN];
	unsigned int val;
	int ret;

	ret = snd_pcm_create_iec958_consumer_hw_params(params, cs,
						       AIU_CS_WORD_LEN);
	if (ret < 0)
		return ret;

	/* Write the 1st half word */
	val = cs[1] | cs[0] << 8;
	snd_soc_component_write(component, AIU_958_CHSTAT_L0, val);
	snd_soc_component_write(component, AIU_958_CHSTAT_R0, val);

	/* Write the 2nd half word */
	val = cs[3] | cs[2] << 8;
	snd_soc_component_write(component, AIU_958_CHSTAT_L1, val);
	snd_soc_component_write(component, AIU_958_CHSTAT_R1, val);

	return 0;
}

static int aiu_encoder_spdif_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct aiu *aiu = snd_soc_component_get_drvdata(component);
	unsigned int val = 0, mrate;
	int ret;

	/* Disable the clock while changing the settings */
	aiu_encoder_spdif_divider_enable(component, false);

	switch (params_physical_width(params)) {
	case 16:
		val |= AIU_958_MISC_MODE_16BITS;
		val |= FIELD_PREP(AIU_958_MISC_16BITS_ALIGN, 2);
		break;
	case 32:
		val |= AIU_958_MISC_MODE_32BITS;
		break;
	default:
		dev_err(dai->dev, "Unsupport physical width\n");
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AIU_958_MISC,
				      AIU_958_MISC_NON_PCM |
				      AIU_958_MISC_MODE_16BITS |
				      AIU_958_MISC_16BITS_ALIGN |
				      AIU_958_MISC_MODE_32BITS |
				      AIU_958_MISC_FORCE_LR |
				      AIU_958_MISC_U_FROM_STREAM,
				      val);

	/* Set the stream channel status word */
	ret = aiu_encoder_spdif_setup_cs_word(component, params);
	if (ret) {
		dev_err(dai->dev, "failed to set channel status word\n");
		return ret;
	}

	snd_soc_component_update_bits(component, AIU_CLK_CTRL,
				      AIU_CLK_CTRL_958_DIV |
				      AIU_CLK_CTRL_958_DIV_MORE,
				      FIELD_PREP(AIU_CLK_CTRL_958_DIV,
						 __ffs(AIU_958_INTERNAL_DIV)));

	/* 2 * 32bits per subframe * 2 channels = 128 */
	mrate = params_rate(params) * 128 * AIU_958_INTERNAL_DIV;
	ret = clk_set_rate(aiu->spdif.clks[MCLK].clk, mrate);
	if (ret) {
		dev_err(dai->dev, "failed to set mclk rate\n");
		return ret;
	}

	aiu_encoder_spdif_divider_enable(component, true);

	return 0;
}

static int aiu_encoder_spdif_hw_free(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	aiu_encoder_spdif_divider_enable(component, false);

	return 0;
}

static int aiu_encoder_spdif_startup(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/*
	 * NOTE: Make sure the spdif block is on its own divider.
	 *
	 * The spdif can be clocked by the i2s master clock or its own
	 * clock. We should (in theory) change the source depending on the
	 * origin of the data.
	 *
	 * However, considering the clocking scheme used on these platforms,
	 * the master clocks will pick the same PLL source when they are
	 * playing from the same FIFO. The clock should be in sync so, it
	 * should not be necessary to reparent the spdif master clock.
	 */
	ret = clk_set_parent(aiu->spdif.clks[MCLK].clk,
			     aiu->spdif_mclk);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(aiu->spdif.clk_num, aiu->spdif.clks);
	if (ret)
		dev_err(dai->dev, "failed to enable spdif clocks\n");

	return ret;
}

static void aiu_encoder_spdif_shutdown(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct aiu *aiu = snd_soc_component_get_drvdata(dai->component);

	clk_bulk_disable_unprepare(aiu->spdif.clk_num, aiu->spdif.clks);
}

const struct snd_soc_dai_ops aiu_encoder_spdif_dai_ops = {
	.trigger	= aiu_encoder_spdif_trigger,
	.hw_params	= aiu_encoder_spdif_hw_params,
	.hw_free	= aiu_encoder_spdif_hw_free,
	.startup	= aiu_encoder_spdif_startup,
	.shutdown	= aiu_encoder_spdif_shutdown,
};

/*
static struct snd_soc_dai_driver aiu_encoder_spdif_dai_drv = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000  |
			  SNDRV_PCM_RATE_44100  |
			  SNDRV_PCM_RATE_48000  |
			  SNDRV_PCM_RATE_88200  |
			  SNDRV_PCM_RATE_96000  |
			  SNDRV_PCM_RATE_176400 |
			  SNDRV_PCM_RATE_192000),
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_LE |
			    SNDRV_PCM_FMTBIT_S24_LE),
	},
	.ops = &aiu_encoder_spdif_dai_ops,
};

static int
aiu_encoder_spdif_component_probe(struct snd_soc_component *component)
{
	struct aiu *aiu =
		snd_soc_component_get_drvdata(dai->component);
	struct device *dev = component->dev;
	struct regmap *map;
	int ret;

	ret = clk_prepare_enable(encoder->pclk);
	if (ret)
	        return ret;

	map = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "Could not get regmap\n");
		return PTR_ERR(map);
	}

	snd_soc_component_init_regmap(component, map);

	return 0;
}

static void
aiu_encoder_spdif_component_remove(struct snd_soc_component *component)
{
	struct aiu *aiu =
		snd_soc_component_get_drvdata(dai->component);

	clk_disable_unprepare(encoder->pclk);
}

static const struct snd_soc_component_driver aiu_encoder_spdif_component = {
	.probe 			= aiu_encoder_spdif_component_probe,
	.remove			= aiu_encoder_spdif_component_remove,
};

static int aiu_encoder_spdif_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aiu *aiu;

	encoder = devm_kzalloc(dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return -ENOMEM;
	platform_set_drvdata(pdev, encoder);

	encoder->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(encoder->pclk)) {
		if (PTR_ERR(encoder->pclk) != -EPROBE_DEFER)
			dev_err(dev,
				"Can't get the dai clock gate\n");
		return PTR_ERR(encoder->pclk);
	}

	encoder->aoclk = devm_clk_get(dev, "aoclk");
	if (IS_ERR(encoder->aoclk)) {
		if (PTR_ERR(encoder->aoclk) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the ao clock\n");
		return PTR_ERR(encoder->aoclk);
	}

	encoder->mclk_i958 = devm_clk_get(dev, "mclk_i958");
	if (IS_ERR(encoder->mclk_i958)) {
		if (PTR_ERR(encoder->mclk_i958) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the spdif master clock\n");
		return PTR_ERR(encoder->mclk_i958);
	}
*/
	/*
	 * NOTE: the spdif can be clock by i2s master clock or its own clock,
	 * We should (maybe) change the source depending on the origin of the
	 * data.
	 * However, considering the clocking scheme used on these platforms,
	 * the master clocks should pick the same PLL source when they are
	 * playing from the same FIFO. The clock should be in sync so, it
	 * should not be necessary to reparent the spdif master clock.
	 */
/*
	encoder->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(encoder->mclk)) {
		if (PTR_ERR(encoder->mclk) != -EPROBE_DEFER)
			dev_err(dev, "Can't get the spdif input mux clock\n");
		return PTR_ERR(encoder->mclk);
	}

	return devm_snd_soc_register_component(dev, &aiu_encoder_spdif_component,
					       &aiu_encoder_spdif_dai_drv, 1);
}

static const struct of_device_id aiu_encoder_spdif_of_match[] = {
	{ .compatible = "amlogic,aiu-spdif-encode", },
	{}
};
MODULE_DEVICE_TABLE(of, aiu_encoder_spdif_of_match);

static struct platform_driver aiu_encoder_spdif_pdrv = {
	.probe = aiu_encoder_spdif_probe,
	.driver = {
		.name = "meson-aiu-spdif-encode",
		.of_match_table = aiu_encoder_spdif_of_match,
	},
};
module_platform_driver(aiu_encoder_spdif_pdrv);

MODULE_DESCRIPTION("Meson AIU SPDIF Encode Driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
*/
