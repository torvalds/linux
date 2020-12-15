/*
 * Rockchip machine ASoC driver for Rockchip Multi-codecs audio
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * Authors: Sugar Zhang <sugar.zhang@rock-chips.com>,
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
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../codecs/rk3308_codec_provider.h"

#define DRV_NAME "rk-multicodecs"
#define MAX_CODECS	2
#define WAIT_CARDS	(SNDRV_CARDS - 1)
#define DEFAULT_MCLK_FS	256

struct multicodecs_data {
	struct snd_soc_card snd_card;
	struct snd_soc_dai_link dai_link;
	unsigned int mclk_fs;
	bool codec_hp_det;
};

static struct snd_soc_jack mc_hp_jack;

static int rk_multicodecs_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct multicodecs_data *mc_data = snd_soc_card_get_drvdata(rtd->card);
	unsigned int mclk;
	int ret;

	mclk = params_rate(params) * mc_data->mclk_fs;

	ret = snd_soc_dai_set_sysclk(codec_dai, substream->stream, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		pr_err("Set codec_dai sysclk failed: %d\n", ret);
		goto out;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, substream->stream, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP) {
		pr_err("Set cpu_dai sysclk failed: %d\n", ret);
		goto out;
	}

	return 0;

out:
	return ret;
}

static int rk_dailink_init(struct snd_soc_pcm_runtime *rtd)
{
	struct multicodecs_data *mc_data = snd_soc_card_get_drvdata(rtd->card);

	if (mc_data->codec_hp_det) {
		snd_soc_card_jack_new(rtd->card, "Headphones",
				      SND_JACK_HEADPHONE,
				      &mc_hp_jack, NULL, 0);

#ifdef CONFIG_SND_SOC_RK3308
		if (rk3308_codec_set_jack_detect_cb)
			rk3308_codec_set_jack_detect_cb(rtd->codec_dai->component, &mc_hp_jack);
#endif
	}

	return 0;
}

static int rk_multicodecs_parse_daifmt(struct device_node *node,
				       struct device_node *codec,
				       struct multicodecs_data *mc_data,
				       const char *prefix)
{
	struct snd_soc_dai_link *dai_link = &mc_data->dai_link;
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, prefix,
					 &bitclkmaster, &framemaster);

	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (strlen(prefix) && !bitclkmaster && !framemaster) {
		/*
		 * No dai-link level and master setting was not found from
		 * sound node level, revert back to legacy DT parsing and
		 * take the settings from codec node.
		 */
		pr_debug("%s: Revert to legacy daifmt parsing\n", __func__);

		daifmt = snd_soc_of_parse_daifmt(codec, NULL, NULL, NULL) |
			(daifmt & ~SND_SOC_DAIFMT_CLOCK_MASK);
	} else {
		if (codec == bitclkmaster)
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBM_CFM : SND_SOC_DAIFMT_CBM_CFS;
		else
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBS_CFM : SND_SOC_DAIFMT_CBS_CFS;
	}

	/*
	 * If there is NULL format means that the format isn't specified, we
	 * need to set i2s format by default.
	 */
	if (!(daifmt & SND_SOC_DAIFMT_FORMAT_MASK))
		daifmt |= SND_SOC_DAIFMT_I2S;

	dai_link->dai_fmt = daifmt;

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	return 0;
}

static int wait_locked_card(struct device_node *np, struct device *dev)
{
	char *propname = "rockchip,wait-card-locked";
	u32 cards[WAIT_CARDS];
	int num;
	int ret;
#ifndef MODULE
	int i;
#endif

	ret = of_property_count_u32_elems(np, propname);
	if (ret < 0) {
		if (ret == -EINVAL) {
			/*
			 * -EINVAL means the property does not exist, this is
			 * fine.
			 */
			return 0;
		}

		dev_err(dev, "Property '%s' elems could not be read: %d\n",
			propname, ret);
		return ret;
	}

	num = ret;
	if (num > WAIT_CARDS)
		num = WAIT_CARDS;

	ret = of_property_read_u32_array(np, propname, cards, num);
	if (ret < 0) {
		if (ret == -EINVAL) {
			/*
			 * -EINVAL means the property does not exist, this is
			 * fine.
			 */
			return 0;
		}

		dev_err(dev, "Property '%s' could not be read: %d\n",
			propname, ret);
		return ret;
	}

	ret = 0;
#ifndef MODULE
	for (i = 0; i < num; i++) {
		if (!snd_card_locked(cards[i])) {
			dev_warn(dev, "card: %d has not been locked, re-probe again\n",
				 cards[i]);
			ret = -EPROBE_DEFER;
			break;
		}
	}
#endif

	return ret;
}

static struct snd_soc_ops rk_ops = {
	.hw_params = rk_multicodecs_hw_params,
};

static int rk_multicodecs_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link *link;
	struct snd_soc_dai_link_component *codecs;
	struct multicodecs_data *mc_data;
	struct of_phandle_args args;
	struct device_node *node;
	u32 val;
	int count;
	int ret = 0, i = 0, idx = 0;
	const char *prefix = "rockchip,";

	ret = wait_locked_card(np, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "check_lock_card failed: %d\n", ret);
		return ret;
	}

	mc_data = devm_kzalloc(&pdev->dev, sizeof(*mc_data), GFP_KERNEL);
	if (!mc_data)
		return -ENOMEM;

	card = &mc_data->snd_card;
	card->dev = &pdev->dev;

	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(card, "rockchip,card-name");
	if (ret < 0)
		return ret;

	link = &mc_data->dai_link;
	link->name = "dailink-multicodecs";
	link->stream_name = link->name;
	link->init = rk_dailink_init;
	link->ops = &rk_ops;

	card->dai_link = link;
	card->num_links = 1;
	card->num_aux_devs = 0;

	count = of_count_phandle_with_args(np, "rockchip,codec", NULL);
	if (count < 0 || count > MAX_CODECS)
		return -EINVAL;

	/* refine codecs, remove unavailable node */
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "rockchip,codec", i);
		if (!node)
			return -ENODEV;
		if (of_device_is_available(node))
			idx++;
	}

	if (!idx)
		return -ENODEV;

	codecs = devm_kcalloc(&pdev->dev, idx,
			      sizeof(*codecs), GFP_KERNEL);
	link->codecs = codecs;
	link->num_codecs = idx;
	idx = 0;
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "rockchip,codec", i);
		if (!node)
			return -ENODEV;
		if (!of_device_is_available(node))
			continue;

		ret = of_parse_phandle_with_fixed_args(np, "rockchip,codec",
						       0, i, &args);
		if (ret)
			return ret;

		codecs[idx].of_node = node;
		ret = snd_soc_get_dai_name(&args, &codecs[idx].dai_name);
		if (ret)
			return ret;
		idx++;
	}

	/* Only reference the codecs[0].of_node which maybe as master. */
	rk_multicodecs_parse_daifmt(np, codecs[0].of_node, mc_data, prefix);

	link->cpu_of_node = of_parse_phandle(np, "rockchip,cpu", 0);
	if (!link->cpu_of_node)
		return -ENODEV;

	link->platform_of_node = link->cpu_of_node;

	mc_data->mclk_fs = DEFAULT_MCLK_FS;
	if (!of_property_read_u32(np, "rockchip,mclk-fs", &val))
		mc_data->mclk_fs = val;

	mc_data->codec_hp_det =
		of_property_read_bool(np, "rockchip,codec-hp-det");

	snd_soc_card_set_drvdata(card, mc_data);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret) {
		dev_err(&pdev->dev, "card register failed %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static const struct of_device_id rockchip_multicodecs_of_match[] = {
	{ .compatible = "rockchip,multicodecs-card", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_multicodecs_of_match);

static struct platform_driver rockchip_multicodecs_driver = {
	.probe = rk_multicodecs_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_multicodecs_of_match,
	},
};

module_platform_driver(rockchip_multicodecs_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip General Multicodecs ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
