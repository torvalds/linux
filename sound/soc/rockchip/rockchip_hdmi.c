// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip machine ASoC driver for Rockchip HDMI audio
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd
 *
 * Authors: XiaoTan Luo <lxt@rock-chips.com>
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

#define DRV_NAME "rk-hdmi-sound"
#define MAX_CODECS	2
#define DEFAULT_MCLK_FS	256

struct rk_hdmi_data {
	struct snd_soc_card	card;
	struct snd_soc_dai_link	dai;
	struct snd_soc_jack	hdmi_jack;
	struct snd_soc_jack_pin	hdmi_jack_pin;
	unsigned int		mclk_fs;
	bool			jack_det;
};

static int rk_hdmi_fill_widget_info(struct device *dev,
		struct snd_soc_dapm_widget *w, enum snd_soc_dapm_type id,
		void *priv, const char *wname, const char *stream,
		struct snd_kcontrol_new *wc, int numkc,
		int (*event)(struct snd_soc_dapm_widget *,
		struct snd_kcontrol *, int), unsigned short event_flags)
{
	w->id = id;
	w->name = devm_kstrdup(dev, wname, GFP_KERNEL);
	if (!w->name)
		return -ENOMEM;

	w->sname = stream;
	w->reg = SND_SOC_NOPM;
	w->shift = 0;
	w->kcontrol_news = wc;
	w->num_kcontrols = numkc;
	w->priv = priv;
	w->event = event;
	w->event_flags = event_flags;

	return 0;
}

static int rk_dailink_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct rk_hdmi_data *rk_data = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = rtd->card->dev;
	int ret = 0;
	struct snd_soc_dapm_widget *widgets;

	if (!rk_data->jack_det)
		return 0;

	widgets = devm_kcalloc(card->dapm.dev, 1,
			       sizeof(*widgets), GFP_KERNEL);
	if (!widgets)
		return -ENOMEM;

	ret = rk_hdmi_fill_widget_info(card->dapm.dev, widgets,
				       snd_soc_dapm_line, NULL,
				       rk_data->hdmi_jack_pin.pin,
				       NULL, NULL, 0, NULL, 0);
	if (ret < 0)
		return ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, widgets, 1);
	if (ret < 0)
		return ret;

	ret = snd_soc_dapm_new_widgets(rtd->card);
	if (ret < 0)
		return ret;

	ret = snd_soc_card_jack_new(rtd->card,
				    rk_data->hdmi_jack_pin.pin,
				    rk_data->hdmi_jack_pin.mask,
				    &rk_data->hdmi_jack,
				    &rk_data->hdmi_jack_pin, 1);
	if (ret) {
		dev_err(dev, "Can't new HDMI Jack %d\n", ret);
		return ret;
	}
	return snd_soc_component_set_jack(codec_dai->component,
					  &rk_data->hdmi_jack, NULL);

}

static int rk_hdmi_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct rk_hdmi_data *rk_data = snd_soc_card_get_drvdata(rtd->card);
	unsigned int mclk;
	int ret;

	mclk = params_rate(params) * rk_data->mclk_fs;

	ret = snd_soc_dai_set_sysclk(codec_dai, substream->stream, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(codec_dai->dev,
			"Set codec_dai sysclk failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, substream->stream, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP) {
		dev_err(cpu_dai->dev,
			"Set cpu_dai sysclk failed: %d\n", ret);
		return ret;
	}

	return 0;

}

static const struct snd_soc_ops rk_ops = {
	.hw_params = rk_hdmi_hw_params,
};

static unsigned int rk_hdmi_parse_daifmt(struct device_node *node,
				struct device_node *cpu,
				char *prefix)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, prefix,
					 &bitclkmaster, &framemaster);
	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (!bitclkmaster || cpu == bitclkmaster)
		daifmt |= (!framemaster || cpu == framemaster) ?
			SND_SOC_DAIFMT_CBS_CFS : SND_SOC_DAIFMT_CBS_CFM;
	else
		daifmt |= (!framemaster || cpu == framemaster) ?
			SND_SOC_DAIFMT_CBM_CFS : SND_SOC_DAIFMT_CBM_CFM;

	/*
	 * If there is NULL format means that the format isn't specified, we
	 * need to set i2s format by default.
	 */
	if (!(daifmt & SND_SOC_DAIFMT_FORMAT_MASK))
		daifmt |= SND_SOC_DAIFMT_I2S;

	of_node_put(bitclkmaster);
	of_node_put(framemaster);
	return daifmt;
}

static int rk_hdmi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_dai_link_component *codecs;
	struct snd_soc_dai_link_component *platforms;
	struct snd_soc_dai_link_component *cpus;
	struct of_phandle_args args;
	struct device_node *cpu_np;
	struct rk_hdmi_data *rk_data;
	int count;
	u32 val;
	int ret = 0, i = 0, idx = 0;

	rk_data = devm_kzalloc(&pdev->dev, sizeof(*rk_data), GFP_KERNEL);
	if (!rk_data)
		return -ENOMEM;

	cpus = devm_kzalloc(&pdev->dev, sizeof(*cpus), GFP_KERNEL);
	if (!cpus)
		return -ENOMEM;

	platforms = devm_kzalloc(&pdev->dev, sizeof(*platforms), GFP_KERNEL);
	if (!platforms)
		return -ENOMEM;

	rk_data->card.dev = &pdev->dev;
	rk_data->dai.init = &rk_dailink_init;
	rk_data->dai.ops = &rk_ops;
	rk_data->dai.cpus = cpus;
	rk_data->dai.platforms = platforms;
	rk_data->dai.num_cpus = 1;
	rk_data->dai.num_platforms = 1;
	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(&rk_data->card, "rockchip,card-name");
	if (ret < 0)
		return ret;
	rk_data->dai.name = rk_data->card.name;
	rk_data->dai.stream_name = rk_data->card.name;
	count = of_count_phandle_with_args(np, "rockchip,codec", "#sound-dai-cells");
	if (count < 0 || count > MAX_CODECS)
		return -EINVAL;

	/* refine codecs, remove unavailable node */
	for (i = 0; i < count; i++) {
		ret = of_parse_phandle_with_args(np, "rockchip,codec", "#sound-dai-cells", i, &args);
		if (ret) {
			dev_err(&pdev->dev, "error getting codec phandle index %d\n", i);
			return -ENODEV;
		}
		if (of_device_is_available(args.np))
			idx++;
		of_node_put(args.np);
	}

	if (!idx)
		return -ENODEV;

	codecs = devm_kcalloc(&pdev->dev, idx,
			      sizeof(*codecs), GFP_KERNEL);
	rk_data->dai.codecs = codecs;
	rk_data->dai.num_codecs = idx;
	idx = 0;
	for (i = 0; i < count; i++) {
		ret = of_parse_phandle_with_args(np, "rockchip,codec", "#sound-dai-cells", i, &args);
		if (ret) {
			dev_err(&pdev->dev, "error getting codec phandle index %d\n", i);
			return -ENODEV;
		}
		if (!of_device_is_available(args.np)) {
			of_node_put(args.np);
			continue;
		}
		codecs[idx].of_node = args.np;
		ret = snd_soc_get_dai_name(&args, &codecs[idx].dai_name);
		if (ret)
			return ret;
		idx++;
	}

	cpu_np = of_parse_phandle(np, "rockchip,cpu", 0);
	if (!cpu_np)
		return -ENODEV;

	rk_data->dai.dai_fmt = rk_hdmi_parse_daifmt(np, cpu_np, "rockchip,");
	rk_data->mclk_fs = DEFAULT_MCLK_FS;
	if (!of_property_read_u32(np, "rockchip,mclk-fs", &val))
		rk_data->mclk_fs = val;

	rk_data->jack_det =
		of_property_read_bool(np, "rockchip,jack-det");

	rk_data->dai.cpus->of_node = cpu_np;
	rk_data->dai.platforms->of_node = cpu_np;
	of_node_put(cpu_np);

	rk_data->hdmi_jack_pin.pin = rk_data->card.name;
	rk_data->hdmi_jack_pin.mask = SND_JACK_LINEOUT;
	rk_data->card.num_links = 1;
	rk_data->card.owner = THIS_MODULE;
	rk_data->card.dai_link = &rk_data->dai;

	snd_soc_card_set_drvdata(&rk_data->card, rk_data);
	ret = devm_snd_soc_register_card(&pdev->dev, &rk_data->card);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret) {
		dev_err(&pdev->dev, "card register failed %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, &rk_data->card);

	return ret;
}

static const struct of_device_id rockchip_sound_of_match[] = {
	{ .compatible = "rockchip,hdmi", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_sound_of_match);

static struct platform_driver rockchip_sound_driver = {
	.probe = rk_hdmi_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_sound_of_match,
	},
};

module_platform_driver(rockchip_sound_driver);

MODULE_AUTHOR("XiaoTan Luo <lxt@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip HDMI ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
