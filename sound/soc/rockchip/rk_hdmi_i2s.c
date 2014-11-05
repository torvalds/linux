/*
 * rk_hdmi_i2s.c  --  HDMI i2s audio for rockchip
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "card_info.h"
#include "rk_pcm.h"
#include "rk_i2s.h"

#if 0
#define DBG(x...) pr_info("rk_hdmi_i2s:"x)
#else
#define DBG(x...) do { } while (0)
#endif

static int hdmi_i2s_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int pll_out = 0, dai_fmt = rtd->dai_link->dai_fmt;
	int div_bclk,div_mclk;
	int ret;

	DBG("Enter::%s----%d\n", __func__, __LINE__);

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		pr_err("%s():failed to set the format for cpu side\n",
			__func__);
		return ret;
	}

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
		pll_out = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		pll_out = 11289600;
		break;
	case 176400:
		pll_out = 11289600*2;
		break;
	case 192000:
		pll_out = 12288000*2;
		break;
	default:
		pr_err("Enter:%s, %d, Error rate=%d\n",
			__func__, __LINE__, params_rate(params));
		return -EINVAL;
		break;
	}

	DBG("Enter:%s, %d, rate=%d\n",
		__func__, __LINE__,
		params_rate(params));

	div_bclk = 63;
	div_mclk = pll_out/(params_rate(params)*(div_bclk+1))-1;

	snd_soc_dai_set_sysclk(cpu_dai, 0, pll_out, 0);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_BCLK,div_bclk);
	snd_soc_dai_set_clkdiv(cpu_dai, ROCKCHIP_DIV_MCLK, div_mclk);

	DBG("Enter:%s, %d, div_bclk: %d, div_mclk: %d\n",
		__func__, __LINE__, div_bclk, div_mclk);

	return 0;
}



static struct snd_soc_ops hdmi_i2s_hifi_ops = {
	.hw_params = hdmi_i2s_hifi_hw_params,
};

static struct snd_soc_dai_link hdmi_i2s_dai = {
	.name = "HDMI I2S",
	.stream_name = "HDMI PCM",
	.codec_dai_name = "rk-hdmi-i2s-hifi",
	.ops = &hdmi_i2s_hifi_ops,
};

static struct snd_soc_card rockchip_hdmi_i2s_snd_card = {
	.name = "RK-HDMI-I2S",
	.dai_link = &hdmi_i2s_dai,
	.num_links = 1,
};

static int rockchip_hdmi_i2s_audio_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card = &rockchip_hdmi_i2s_snd_card;

	card->dev = &pdev->dev;

	ret = rockchip_of_get_sound_card_info(card);
	if (ret) {
		pr_err("%s() get sound card info failed:%d\n",
			__func__, ret);
		return ret;
	}

	ret = snd_soc_register_card(card);
	if (ret)
		pr_err("%s() register card failed:%d\n",
			__func__, ret);

	return ret;
}

static int rockchip_hdmi_i2s_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_hdmi_i2s_of_match[] = {
	{ .compatible = "rockchip-hdmi-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_hdmi_i2s_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rockchip_hdmi_i2s_audio_driver = {
	.driver         = {
		.name   = "rockchip-hdmi-i2s",
		.owner  = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(rockchip_hdmi_i2s_of_match),
	},
	.probe          = rockchip_hdmi_i2s_audio_probe,
	.remove         = rockchip_hdmi_i2s_audio_remove,
};

module_platform_driver(rockchip_hdmi_i2s_audio_driver);

/* Module information */
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("ROCKCHIP hdmi i2s ASoC Interface");
MODULE_LICENSE("GPL");
