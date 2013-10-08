/*
 * atmel_wm8904 - Atmel ASoC driver for boards with WM8904 codec.
 *
 * Copyright (C) 2012 Atmel
 *
 * Author: Bo Shen <voice.shen@atmel.com>
 *
 * GPLv2 or later
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>

#include <sound/soc.h>

#include "../codecs/wm8904.h"
#include "atmel_ssc_dai.h"

#define MCLK_RATE 32768

static struct clk *mclk;

static const struct snd_soc_dapm_widget atmel_asoc_wm8904_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
};

static int atmel_asoc_wm8904_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, WM8904_FLL_MCLK, WM8904_FLL_MCLK,
		32768, params_rate(params) * 256);
	if (ret < 0) {
		pr_err("%s - failed to set wm8904 codec PLL.", __func__);
		return ret;
	}

	/*
	 * As here wm8904 use FLL output as its system clock
	 * so calling set_sysclk won't care freq parameter
	 * then we pass 0
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8904_CLK_FLL,
			0, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("%s -failed to set wm8904 SYSCLK\n", __func__);
		return ret;
	}

	return 0;
}

static struct snd_soc_ops atmel_asoc_wm8904_ops = {
	.hw_params = atmel_asoc_wm8904_hw_params,
};

static int atmel_set_bias_level(struct snd_soc_card *card,
		struct snd_soc_dapm_context *dapm,
		enum snd_soc_bias_level level)
{
	if (dapm->bias_level == SND_SOC_BIAS_STANDBY) {
		switch (level) {
		case SND_SOC_BIAS_PREPARE:
			clk_prepare_enable(mclk);
			break;
		case SND_SOC_BIAS_OFF:
			clk_disable_unprepare(mclk);
			break;
		default:
			break;
		}
	}

	return 0;
};

static struct snd_soc_dai_link atmel_asoc_wm8904_dailink = {
	.name = "WM8904",
	.stream_name = "WM8904 PCM",
	.codec_dai_name = "wm8904-hifi",
	.dai_fmt = SND_SOC_DAIFMT_I2S
		| SND_SOC_DAIFMT_NB_NF
		| SND_SOC_DAIFMT_CBM_CFM,
	.ops = &atmel_asoc_wm8904_ops,
};

static struct snd_soc_card atmel_asoc_wm8904_card = {
	.name = "atmel_asoc_wm8904",
	.owner = THIS_MODULE,
	.set_bias_level = atmel_set_bias_level,
	.dai_link = &atmel_asoc_wm8904_dailink,
	.num_links = 1,
	.dapm_widgets = atmel_asoc_wm8904_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(atmel_asoc_wm8904_dapm_widgets),
	.fully_routed = true,
};

static int atmel_asoc_wm8904_dt_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *cpu_np;
	struct snd_soc_card *card = &atmel_asoc_wm8904_card;
	struct snd_soc_dai_link *dailink = &atmel_asoc_wm8904_dailink;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "only device tree supported\n");
		return -EINVAL;
	}

	ret = snd_soc_of_parse_card_name(card, "atmel,model");
	if (ret) {
		dev_err(&pdev->dev, "failed to parse card name\n");
		return ret;
	}

	ret = snd_soc_of_parse_audio_routing(card, "atmel,audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "failed to parse audio routing\n");
		return ret;
	}

	cpu_np = of_parse_phandle(np, "atmel,ssc-controller", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "failed to get dai and pcm info\n");
		ret = -EINVAL;
		return ret;
	}
	dailink->cpu_of_node = cpu_np;
	dailink->platform_of_node = cpu_np;
	of_node_put(cpu_np);

	codec_np = of_parse_phandle(np, "atmel,audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "failed to get codec info\n");
		ret = -EINVAL;
		return ret;
	}
	dailink->codec_of_node = codec_np;
	of_node_put(codec_np);

	return 0;
}

static int atmel_asoc_wm8904_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &atmel_asoc_wm8904_card;
	struct snd_soc_dai_link *dailink = &atmel_asoc_wm8904_dailink;
	struct clk *clk_src;
	struct pinctrl *pinctrl;
	int id, ret;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_err(&pdev->dev, "failed to request pinctrl\n");
		return PTR_ERR(pinctrl);
	}

	card->dev = &pdev->dev;
	ret = atmel_asoc_wm8904_dt_init(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to init dt info\n");
		return ret;
	}

	id = of_alias_get_id((struct device_node *)dailink->cpu_of_node, "ssc");
	ret = atmel_ssc_set_audio(id);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to set SSC %d for audio\n", id);
		return ret;
	}

	mclk = clk_get(NULL, "pck0");
	if (IS_ERR(mclk)) {
		dev_err(&pdev->dev, "failed to get pck0\n");
		ret = PTR_ERR(mclk);
		goto err_set_audio;
	}

	clk_src = clk_get(NULL, "clk32k");
	if (IS_ERR(clk_src)) {
		dev_err(&pdev->dev, "failed to get clk32k\n");
		ret = PTR_ERR(clk_src);
		goto err_set_audio;
	}

	ret = clk_set_parent(mclk, clk_src);
	clk_put(clk_src);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to set MCLK parent\n");
		goto err_set_audio;
	}

	dev_info(&pdev->dev, "setting pck0 to %dHz\n", MCLK_RATE);
	clk_set_rate(mclk, MCLK_RATE);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed\n");
		goto err_set_audio;
	}

	return 0;

err_set_audio:
	atmel_ssc_put_audio(id);
	return ret;
}

static int atmel_asoc_wm8904_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_dai_link *dailink = &atmel_asoc_wm8904_dailink;
	int id;

	id = of_alias_get_id((struct device_node *)dailink->cpu_of_node, "ssc");

	snd_soc_unregister_card(card);
	atmel_ssc_put_audio(id);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atmel_asoc_wm8904_dt_ids[] = {
	{ .compatible = "atmel,asoc-wm8904", },
	{ }
};
#endif

static struct platform_driver atmel_asoc_wm8904_driver = {
	.driver = {
		.name = "atmel-wm8904-audio",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(atmel_asoc_wm8904_dt_ids),
	},
	.probe = atmel_asoc_wm8904_probe,
	.remove = atmel_asoc_wm8904_remove,
};

module_platform_driver(atmel_asoc_wm8904_driver);

/* Module information */
MODULE_AUTHOR("Bo Shen <voice.shen@atmel.com>");
MODULE_DESCRIPTION("ALSA SoC machine driver for Atmel EK with WM8904 codec");
MODULE_LICENSE("GPL");
