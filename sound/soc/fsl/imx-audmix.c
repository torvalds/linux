// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017 NXP
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * https://www.opensource.org/licenses/gpl-license.html
 * https://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/pm_runtime.h>
#include "fsl_sai.h"
#include "fsl_audmix.h"

struct imx_audmix {
	struct platform_device *pdev;
	struct snd_soc_card card;
	struct platform_device *audmix_pdev;
	struct platform_device *out_pdev;
	struct clk *cpu_mclk;
	int num_dai;
	struct snd_soc_dai_link *dai;
	int num_dai_conf;
	struct snd_soc_codec_conf *dai_conf;
	int num_dapm_routes;
	struct snd_soc_dapm_route *dapm_routes;
};

static const u32 imx_audmix_rates[] = {
	8000, 12000, 16000, 24000, 32000, 48000, 64000, 96000,
};

static const struct snd_pcm_hw_constraint_list imx_audmix_rate_constraints = {
	.count = ARRAY_SIZE(imx_audmix_rates),
	.list = imx_audmix_rates,
};

static int imx_audmix_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct imx_audmix *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *dev = rtd->card->dev;
	unsigned long clk_rate = clk_get_rate(priv->cpu_mclk);
	int ret;

	if (clk_rate % 24576000 == 0) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 &imx_audmix_rate_constraints);
		if (ret < 0)
			return ret;
	} else {
		dev_warn(dev, "mclk may be not supported %lu\n", clk_rate);
	}

	ret = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS,
					   1, 8);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT,
					    FSL_AUDMIX_FORMATS);
}

static int imx_audmix_fe_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct device *dev = rtd->card->dev;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF;
	u32 channels = params_channels(params);
	int ret, dir;

	/* For playback the AUDMIX is consumer, and for record is provider */
	fmt |= tx ? SND_SOC_DAIFMT_BP_FP : SND_SOC_DAIFMT_BC_FC;
	dir  = tx ? SND_SOC_CLOCK_OUT : SND_SOC_CLOCK_IN;

	/* set DAI configuration */
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0), fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(asoc_rtd_to_cpu(rtd, 0), FSL_SAI_CLK_MAST1, 0, dir);
	if (ret) {
		dev_err(dev, "failed to set cpu sysclk: %d\n", ret);
		return ret;
	}

	/*
	 * Per datasheet, AUDMIX expects 8 slots and 32 bits
	 * for every slot in TDM mode.
	 */
	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), BIT(channels) - 1,
				       BIT(channels) - 1, 8, 32);
	if (ret)
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);

	return ret;
}

static int imx_audmix_be_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct device *dev = rtd->card->dev;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF;
	int ret;

	if (!tx)
		return 0;

	/* For playback the AUDMIX is consumer */
	fmt |= SND_SOC_DAIFMT_BC_FC;

	/* set AUDMIX DAI configuration */
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0), fmt);
	if (ret)
		dev_err(dev, "failed to set AUDMIX DAI fmt: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops imx_audmix_fe_ops = {
	.startup = imx_audmix_fe_startup,
	.hw_params = imx_audmix_fe_hw_params,
};

static const struct snd_soc_ops imx_audmix_be_ops = {
	.hw_params = imx_audmix_be_hw_params,
};

static int imx_audmix_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *audmix_np = NULL, *out_cpu_np = NULL;
	struct platform_device *audmix_pdev = NULL;
	struct platform_device *cpu_pdev;
	struct of_phandle_args args;
	struct imx_audmix *priv;
	int i, num_dai, ret;
	const char *fe_name_pref = "HiFi-AUDMIX-FE-";
	char *be_name, *be_pb, *be_cp, *dai_name, *capture_dai_name;

	if (pdev->dev.parent) {
		audmix_np = pdev->dev.parent->of_node;
	} else {
		dev_err(&pdev->dev, "Missing parent device.\n");
		return -EINVAL;
	}

	if (!audmix_np) {
		dev_err(&pdev->dev, "Missing DT node for parent device.\n");
		return -EINVAL;
	}

	audmix_pdev = of_find_device_by_node(audmix_np);
	if (!audmix_pdev) {
		dev_err(&pdev->dev, "Missing AUDMIX platform device for %s\n",
			np->full_name);
		return -EINVAL;
	}
	put_device(&audmix_pdev->dev);

	num_dai = of_count_phandle_with_args(audmix_np, "dais", NULL);
	if (num_dai != FSL_AUDMIX_MAX_DAIS) {
		dev_err(&pdev->dev, "Need 2 dais to be provided for %s\n",
			audmix_np->full_name);
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num_dai = 2 * num_dai;
	priv->dai = devm_kcalloc(&pdev->dev, priv->num_dai,
				 sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!priv->dai)
		return -ENOMEM;

	priv->num_dai_conf = num_dai;
	priv->dai_conf = devm_kcalloc(&pdev->dev, priv->num_dai_conf,
				      sizeof(struct snd_soc_codec_conf),
				      GFP_KERNEL);
	if (!priv->dai_conf)
		return -ENOMEM;

	priv->num_dapm_routes = 3 * num_dai;
	priv->dapm_routes = devm_kcalloc(&pdev->dev, priv->num_dapm_routes,
					 sizeof(struct snd_soc_dapm_route),
					 GFP_KERNEL);
	if (!priv->dapm_routes)
		return -ENOMEM;

	for (i = 0; i < num_dai; i++) {
		struct snd_soc_dai_link_component *dlc;

		/* for CPU/Codec/Platform x 2 */
		dlc = devm_kcalloc(&pdev->dev, 6, sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		ret = of_parse_phandle_with_args(audmix_np, "dais", NULL, i,
						 &args);
		if (ret < 0) {
			dev_err(&pdev->dev, "of_parse_phandle_with_args failed\n");
			return ret;
		}

		cpu_pdev = of_find_device_by_node(args.np);
		if (!cpu_pdev) {
			dev_err(&pdev->dev, "failed to find SAI platform device\n");
			return -EINVAL;
		}
		put_device(&cpu_pdev->dev);

		dai_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%s",
					  fe_name_pref, args.np->full_name + 1);
		if (!dai_name)
			return -ENOMEM;

		dev_info(pdev->dev.parent, "DAI FE name:%s\n", dai_name);

		if (i == 0) {
			out_cpu_np = args.np;
			capture_dai_name =
				devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
					       dai_name, "CPU-Capture");
			if (!capture_dai_name)
				return -ENOMEM;
		}

		priv->dai[i].cpus = &dlc[0];
		priv->dai[i].codecs = &dlc[1];
		priv->dai[i].platforms = &dlc[2];

		priv->dai[i].num_cpus = 1;
		priv->dai[i].num_codecs = 1;
		priv->dai[i].num_platforms = 1;

		priv->dai[i].name = dai_name;
		priv->dai[i].stream_name = "HiFi-AUDMIX-FE";
		priv->dai[i].codecs->dai_name = "snd-soc-dummy-dai";
		priv->dai[i].codecs->name = "snd-soc-dummy";
		priv->dai[i].cpus->of_node = args.np;
		priv->dai[i].cpus->dai_name = dev_name(&cpu_pdev->dev);
		priv->dai[i].platforms->of_node = args.np;
		priv->dai[i].dynamic = 1;
		priv->dai[i].dpcm_playback = 1;
		priv->dai[i].dpcm_capture = (i == 0 ? 1 : 0);
		priv->dai[i].ignore_pmdown_time = 1;
		priv->dai[i].ops = &imx_audmix_fe_ops;

		/* Add AUDMIX Backend */
		be_name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					 "audmix-%d", i);
		be_pb = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "AUDMIX-Playback-%d", i);
		be_cp = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "AUDMIX-Capture-%d", i);
		if (!be_name || !be_pb || !be_cp)
			return -ENOMEM;

		priv->dai[num_dai + i].cpus = &dlc[3];
		priv->dai[num_dai + i].codecs = &dlc[4];
		priv->dai[num_dai + i].platforms = &dlc[5];

		priv->dai[num_dai + i].num_cpus = 1;
		priv->dai[num_dai + i].num_codecs = 1;
		priv->dai[num_dai + i].num_platforms = 1;

		priv->dai[num_dai + i].name = be_name;
		priv->dai[num_dai + i].codecs->dai_name = "snd-soc-dummy-dai";
		priv->dai[num_dai + i].codecs->name = "snd-soc-dummy";
		priv->dai[num_dai + i].cpus->of_node = audmix_np;
		priv->dai[num_dai + i].cpus->dai_name = be_name;
		priv->dai[num_dai + i].platforms->name = "snd-soc-dummy";
		priv->dai[num_dai + i].no_pcm = 1;
		priv->dai[num_dai + i].dpcm_playback = 1;
		priv->dai[num_dai + i].dpcm_capture  = 1;
		priv->dai[num_dai + i].ignore_pmdown_time = 1;
		priv->dai[num_dai + i].ops = &imx_audmix_be_ops;

		priv->dai_conf[i].dlc.of_node = args.np;
		priv->dai_conf[i].name_prefix = dai_name;

		priv->dapm_routes[i].source =
			devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
				       dai_name, "CPU-Playback");
		if (!priv->dapm_routes[i].source)
			return -ENOMEM;

		priv->dapm_routes[i].sink = be_pb;
		priv->dapm_routes[num_dai + i].source   = be_pb;
		priv->dapm_routes[num_dai + i].sink     = be_cp;
		priv->dapm_routes[2 * num_dai + i].source = be_cp;
		priv->dapm_routes[2 * num_dai + i].sink   = capture_dai_name;
	}

	cpu_pdev = of_find_device_by_node(out_cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SAI platform device\n");
		return -EINVAL;
	}
	put_device(&cpu_pdev->dev);

	priv->cpu_mclk = devm_clk_get(&cpu_pdev->dev, "mclk1");
	if (IS_ERR(priv->cpu_mclk)) {
		ret = PTR_ERR(priv->cpu_mclk);
		dev_err(&cpu_pdev->dev, "failed to get DAI mclk1: %d\n", ret);
		return ret;
	}

	priv->audmix_pdev = audmix_pdev;
	priv->out_pdev  = cpu_pdev;

	priv->card.dai_link = priv->dai;
	priv->card.num_links = priv->num_dai;
	priv->card.codec_conf = priv->dai_conf;
	priv->card.num_configs = priv->num_dai_conf;
	priv->card.dapm_routes = priv->dapm_routes;
	priv->card.num_dapm_routes = priv->num_dapm_routes;
	priv->card.dev = &pdev->dev;
	priv->card.owner = THIS_MODULE;
	priv->card.name = "imx-audmix";

	platform_set_drvdata(pdev, &priv->card);
	snd_soc_card_set_drvdata(&priv->card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, &priv->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed\n");
		return ret;
	}

	return ret;
}

static struct platform_driver imx_audmix_driver = {
	.probe = imx_audmix_probe,
	.driver = {
		.name = "imx-audmix",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(imx_audmix_driver);

MODULE_DESCRIPTION("NXP AUDMIX ASoC machine driver");
MODULE_AUTHOR("Viorel Suman <viorel.suman@nxp.com>");
MODULE_ALIAS("platform:imx-audmix");
MODULE_LICENSE("GPL v2");
