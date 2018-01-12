/*
 * Rockchip machine ASoC driver for boards using MAX98357A/RT5514/DA7219
 *
 * Copyright (c) 2016, ROCKCHIP CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "rockchip_i2s.h"
#include "../codecs/da7219.h"
#include "../codecs/da7219-aad.h"
#include "../codecs/rt5514.h"

#define DRV_NAME "rk3399-gru-sound"

#define SOUND_FS	256

static unsigned int dmic_wakeup_delay;

static struct snd_soc_jack rockchip_sound_jack;

static const struct snd_soc_dapm_widget rockchip_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_LINE("HDMI", NULL),
};

static const struct snd_kcontrol_new rockchip_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("HDMI"),
};

static int rockchip_sound_max98357a_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int mclk;
	int ret;

	/* max98357a supports these sample rates */
	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 48000:
	case 96000:
		mclk = params_rate(params) * SOUND_FS;
		break;
	default:
		dev_err(rtd->card->dev, "%s() doesn't support this sample rate: %d\n",
				__func__, params_rate(params));
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(rtd->cpu_dai, 0, mclk, 0);
	if (ret) {
		dev_err(rtd->card->dev, "%s() error setting sysclk to %u: %d\n",
				__func__, mclk, ret);
		return ret;
	}

	return 0;
}

static int rockchip_sound_rt5514_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int mclk;
	int ret;

	mclk = params_rate(params) * SOUND_FS;

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->card->dev, "Can't set cpu clock out %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5514_SCLK_S_MCLK,
				     mclk, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(rtd->card->dev, "%s() error setting sysclk to %u: %d\n",
				__func__, params_rate(params) * 512, ret);
		return ret;
	}

	/* Wait for DMIC stable */
	msleep(dmic_wakeup_delay);

	return 0;
}

static int rockchip_sound_da7219_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk, ret;

	/* in bypass mode, the mclk has to be one of the frequencies below */
	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set cpu clock out %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock in %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set pll sysclk mclk %d\n", ret);
		return ret;
	}

	return 0;
}

static int rockchip_sound_da7219_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec_dais[0]->codec;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* We need default MCLK and PLL settings for the accessory detection */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 12288000,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Init can't set codec clock in %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, 0, DA7219_SYSCLK_MCLK, 0, 0);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Init can't set pll sysclk mclk %d\n", ret);
		return ret;
	}

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_LINEOUT |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &rockchip_sound_jack, NULL, 0);

	if (ret) {
		dev_err(rtd->card->dev, "New Headset Jack failed! (%d)\n", ret);
		return ret;
	}

	snd_jack_set_key(
		rockchip_sound_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(
		rockchip_sound_jack.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(
		rockchip_sound_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(
		rockchip_sound_jack.jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	da7219_aad_jack_det(codec, &rockchip_sound_jack);

	return 0;
}

static int rockchip_sound_cdndp_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk, ret;

	/* in bypass mode, the mclk has to be one of the frequencies below */
	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set cpu clock out %d\n", ret);
		return ret;
	}

	return 0;
}

static int rockchip_sound_dmic_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int mclk;
	int ret;

	mclk = params_rate(params) * SOUND_FS;

	ret = snd_soc_dai_set_sysclk(rtd->cpu_dai, 0, mclk, 0);
	if (ret) {
		dev_err(rtd->card->dev, "%s() error setting sysclk to %u: %d\n",
				__func__, mclk, ret);
		return ret;
	}

	/* Wait for DMIC stable */
	msleep(dmic_wakeup_delay);

	return 0;
}

static const struct snd_soc_ops rockchip_sound_max98357a_ops = {
	.hw_params = rockchip_sound_max98357a_hw_params,
};

static const struct snd_soc_ops rockchip_sound_rt5514_ops = {
	.hw_params = rockchip_sound_rt5514_hw_params,
};

static const struct snd_soc_ops rockchip_sound_da7219_ops = {
	.hw_params = rockchip_sound_da7219_hw_params,
};

static const struct snd_soc_ops rockchip_sound_cdndp_ops = {
	.hw_params = rockchip_sound_cdndp_hw_params,
};

static const struct snd_soc_ops rockchip_sound_dmic_ops = {
	.hw_params = rockchip_sound_dmic_hw_params,
};

static struct snd_soc_card rockchip_sound_card = {
	.name = "rk3399-gru-sound",
	.owner = THIS_MODULE,
	.dapm_widgets = rockchip_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rockchip_dapm_widgets),
	.controls = rockchip_controls,
	.num_controls = ARRAY_SIZE(rockchip_controls),
};

enum {
	DAILINK_CDNDP,
	DAILINK_DA7219,
	DAILINK_DMIC,
	DAILINK_MAX98357A,
	DAILINK_RT5514,
	DAILINK_RT5514_DSP,
};

static const struct snd_soc_dai_link rockchip_dais[] = {
	[DAILINK_CDNDP] = {
		.name = "DP",
		.stream_name = "DP PCM",
		.codec_dai_name = "i2s-hifi",
		.ops = &rockchip_sound_cdndp_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	},
	[DAILINK_DA7219] = {
		.name = "DA7219",
		.stream_name = "DA7219 PCM",
		.codec_dai_name = "da7219-hifi",
		.init = rockchip_sound_da7219_init,
		.ops = &rockchip_sound_da7219_ops,
		/* set da7219 as slave */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	},
	[DAILINK_DMIC] = {
		.name = "DMIC",
		.stream_name = "DMIC PCM",
		.codec_dai_name = "dmic-hifi",
		.ops = &rockchip_sound_dmic_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	},
	[DAILINK_MAX98357A] = {
		.name = "MAX98357A",
		.stream_name = "MAX98357A PCM",
		.codec_dai_name = "HiFi",
		.ops = &rockchip_sound_max98357a_ops,
		/* set max98357a as slave */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	},
	[DAILINK_RT5514] = {
		.name = "RT5514",
		.stream_name = "RT5514 PCM",
		.codec_dai_name = "rt5514-aif1",
		.ops = &rockchip_sound_rt5514_ops,
		/* set rt5514 as slave */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
	},
	/* RT5514 DSP for voice wakeup via spi bus */
	[DAILINK_RT5514_DSP] = {
		.name = "RT5514 DSP",
		.stream_name = "Wake on Voice",
		.codec_dai_name = "rt5514-dsp-cpu-dai",
	},
};

static const struct snd_soc_dapm_route rockchip_sound_cdndp_routes[] = {
	/* Output */
	{"HDMI", NULL, "TX"},
};

static const struct snd_soc_dapm_route rockchip_sound_da7219_routes[] = {
	/* Output */
	{"Headphones", NULL, "HPL"},
	{"Headphones", NULL, "HPR"},

	/* Input */
	{"MIC", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route rockchip_sound_dmic_routes[] = {
	/* Input */
	{"DMic", NULL, "Int Mic"},
};

static const struct snd_soc_dapm_route rockchip_sound_max98357a_routes[] = {
	/* Output */
	{"Speakers", NULL, "Speaker"},
};

static const struct snd_soc_dapm_route rockchip_sound_rt5514_routes[] = {
	/* Input */
	{"DMIC1L", NULL, "Int Mic"},
	{"DMIC1R", NULL, "Int Mic"},
};

struct rockchip_sound_route {
	const struct snd_soc_dapm_route *routes;
	int num_routes;
};

static const struct rockchip_sound_route rockchip_routes[] = {
	[DAILINK_CDNDP] = {
		.routes = rockchip_sound_cdndp_routes,
		.num_routes = ARRAY_SIZE(rockchip_sound_cdndp_routes),
	},
	[DAILINK_DA7219] = {
		.routes = rockchip_sound_da7219_routes,
		.num_routes = ARRAY_SIZE(rockchip_sound_da7219_routes),
	},
	[DAILINK_DMIC] = {
		.routes = rockchip_sound_dmic_routes,
		.num_routes = ARRAY_SIZE(rockchip_sound_dmic_routes),
	},
	[DAILINK_MAX98357A] = {
		.routes = rockchip_sound_max98357a_routes,
		.num_routes = ARRAY_SIZE(rockchip_sound_max98357a_routes),
	},
	[DAILINK_RT5514] = {
		.routes = rockchip_sound_rt5514_routes,
		.num_routes = ARRAY_SIZE(rockchip_sound_rt5514_routes),
	},
	[DAILINK_RT5514_DSP] = {},
};

struct dailink_match_data {
	const char *compatible;
	struct bus_type *bus_type;
};

static const struct dailink_match_data dailink_match[] = {
	[DAILINK_CDNDP] = {
		.compatible = "rockchip,rk3399-cdn-dp",
	},
	[DAILINK_DA7219] = {
		.compatible = "dlg,da7219",
	},
	[DAILINK_DMIC] = {
		.compatible = "dmic-codec",
	},
	[DAILINK_MAX98357A] = {
		.compatible = "maxim,max98357a",
	},
	[DAILINK_RT5514] = {
		.compatible = "realtek,rt5514",
		.bus_type = &i2c_bus_type,
	},
	[DAILINK_RT5514_DSP] = {
		.compatible = "realtek,rt5514",
		.bus_type = &spi_bus_type,
	},
};

static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int rockchip_sound_codec_node_match(struct device_node *np_codec)
{
	struct device *dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(dailink_match); i++) {
		if (!of_device_is_compatible(np_codec,
					     dailink_match[i].compatible))
			continue;

		if (dailink_match[i].bus_type) {
			dev = bus_find_device(dailink_match[i].bus_type, NULL,
					      np_codec, of_dev_node_match);
			if (!dev)
				continue;
			put_device(dev);
		}

		return i;
	}
	return -1;
}

static int rockchip_sound_of_parse_dais(struct device *dev,
					struct snd_soc_card *card)
{
	struct device_node *np_cpu, *np_cpu0, *np_cpu1;
	struct device_node *np_codec;
	struct snd_soc_dai_link *dai;
	struct snd_soc_dapm_route *routes;
	int i, index;
	int num_routes;

	card->dai_link = devm_kzalloc(dev, sizeof(rockchip_dais),
				      GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	num_routes = 0;
	for (i = 0; i < ARRAY_SIZE(rockchip_routes); i++)
		num_routes += rockchip_routes[i].num_routes;
	routes = devm_kzalloc(dev, num_routes * sizeof(*routes),
			      GFP_KERNEL);
	if (!routes)
		return -ENOMEM;
	card->dapm_routes = routes;

	np_cpu0 = of_parse_phandle(dev->of_node, "rockchip,cpu", 0);
	np_cpu1 = of_parse_phandle(dev->of_node, "rockchip,cpu", 1);

	card->num_dapm_routes = 0;
	card->num_links = 0;
	for (i = 0; i < ARRAY_SIZE(rockchip_dais); i++) {
		np_codec = of_parse_phandle(dev->of_node,
					    "rockchip,codec", i);
		if (!np_codec)
			break;

		if (!of_device_is_available(np_codec))
			continue;

		index = rockchip_sound_codec_node_match(np_codec);
		if (index < 0)
			continue;

		np_cpu = (index == DAILINK_CDNDP) ? np_cpu1 : np_cpu0;
		if (!np_cpu) {
			dev_err(dev, "Missing 'rockchip,cpu' for %s\n",
				rockchip_dais[index].name);
			return -EINVAL;
		}

		dai = &card->dai_link[card->num_links++];
		*dai = rockchip_dais[index];

		dai->codec_of_node = np_codec;
		dai->platform_of_node = np_cpu;
		dai->cpu_of_node = np_cpu;

		if (card->num_dapm_routes + rockchip_routes[index].num_routes >
		    num_routes) {
			dev_err(dev, "Too many routes\n");
			return -EINVAL;
		}

		memcpy(routes + card->num_dapm_routes,
		       rockchip_routes[index].routes,
		       rockchip_routes[index].num_routes * sizeof(*routes));
		card->num_dapm_routes += rockchip_routes[index].num_routes;
	}

	return 0;
}

static int rockchip_sound_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &rockchip_sound_card;
	int ret;

	ret = rockchip_sound_of_parse_dais(&pdev->dev, card);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to parse dais: %d\n", ret);
		return ret;
	}

	/* Set DMIC wakeup delay */
	ret = device_property_read_u32(&pdev->dev, "dmic-wakeup-delay-ms",
					&dmic_wakeup_delay);
	if (ret) {
		dmic_wakeup_delay = 0;
		dev_dbg(&pdev->dev,
			"no optional property 'dmic-wakeup-delay-ms' found, default: no delay\n");
	}

	card->dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id rockchip_sound_of_match[] = {
	{ .compatible = "rockchip,rk3399-gru-sound", },
	{},
};

static struct platform_driver rockchip_sound_driver = {
	.probe = rockchip_sound_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = rockchip_sound_of_match,
#ifdef CONFIG_PM
		.pm = &snd_soc_pm_ops,
#endif
	},
};

module_platform_driver(rockchip_sound_driver);

MODULE_AUTHOR("Xing Zheng <zhengxing@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, rockchip_sound_of_match);
