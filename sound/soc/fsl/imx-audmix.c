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
#include "fsl_sai.h"
#include "fsl_audmix.h"

struct imx_audmix {
	struct platform_device *pdev;
	struct snd_soc_card card;
	struct platform_device *audmix_pdev;
	struct platform_device *out_pdev;
	int num_dai;
	struct snd_soc_dai_link *dai;
	int num_dai_conf;
	struct snd_soc_codec_conf *dai_conf;
	int num_dapm_routes;
	struct snd_soc_dapm_route *dapm_routes;
};

static int imx_audmix_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

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
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct device *dev = rtd->card->dev;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF;
	u32 channels = params_channels(params);
	int ret, dir;

	/* For playback the AUDMIX is consumer, and for record is provider */
	fmt |= tx ? SND_SOC_DAIFMT_BP_FP : SND_SOC_DAIFMT_BC_FC;
	dir  = tx ? SND_SOC_CLOCK_OUT : SND_SOC_CLOCK_IN;

	/* set DAI configuration */
	ret = snd_soc_dai_set_fmt(snd_soc_rtd_to_cpu(rtd, 0), fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(snd_soc_rtd_to_cpu(rtd, 0), FSL_SAI_CLK_MAST1, 0, dir);
	if (ret) {
		dev_err(dev, "failed to set cpu sysclk: %d\n", ret);
		return ret;
	}

	/*
	 * Per datasheet, AUDMIX expects 8 slots and 32 bits
	 * for every slot in TDM mode.
	 */
	ret = snd_soc_dai_set_tdm_slot(snd_soc_rtd_to_cpu(rtd, 0), BIT(channels) - 1,
				       BIT(channels) - 1, 8, 32);
	if (ret)
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);

	return ret;
}

static int imx_audmix_be_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct device *dev = rtd->card->dev;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF;
	int ret;

	if (!tx)
		return 0;

	/* For playback the AUDMIX is consumer */
	fmt |= SND_SOC_DAIFMT_BC_FC;

	/* set AUDMIX DAI configuration */
	ret = snd_soc_dai_set_fmt(snd_soc_rtd_to_cpu(rtd, 0), fmt);
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

static const char *name[][3] = {
	{"HiFi-AUDMIX-FE-0", "HiFi-AUDMIX-FE-1", "HiFi-AUDMIX-FE-2"},
	{"sai-tx", "sai-tx", "sai-rx"},
	{"AUDMIX-Playback-0", "AUDMIX-Playback-1", "SAI-Capture"},
	{"SAI-Playback", "SAI-Playback", "AUDMIX-Capture-0"},
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
	char *be_name, *dai_name;

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

	num_dai += 1;
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

	priv->num_dapm_routes = num_dai;
	priv->dapm_routes = devm_kcalloc(&pdev->dev, priv->num_dapm_routes,
					 sizeof(struct snd_soc_dapm_route),
					 GFP_KERNEL);
	if (!priv->dapm_routes)
		return -ENOMEM;

	for (i = 0; i < num_dai; i++) {
		struct snd_soc_dai_link_component *dlc;

		/* for CPU x 2 */
		dlc = devm_kcalloc(&pdev->dev, 2, sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		if (i == num_dai - 1)
			ret = of_parse_phandle_with_args(audmix_np, "dais", NULL, 0,
							 &args);
		else
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
					  fe_name_pref, args.np->full_name);
		if (!dai_name)
			return -ENOMEM;

		dev_info(pdev->dev.parent, "DAI FE name:%s\n", dai_name);

		if (i == num_dai - 1)
			out_cpu_np = args.np;

		/*
		 * CPU == Platform
		 * platform is using soc-generic-dmaengine-pcm
		 */
		priv->dai[i].cpus	=
		priv->dai[i].platforms	= &dlc[0];
		priv->dai[i].codecs	= &snd_soc_dummy_dlc;

		priv->dai[i].num_cpus = 1;
		priv->dai[i].num_codecs = 1;
		priv->dai[i].num_platforms = 1;
		priv->dai[i].name = name[0][i];
		priv->dai[i].stream_name = "HiFi-AUDMIX-FE";
		priv->dai[i].cpus->of_node = args.np;
		priv->dai[i].cpus->dai_name = name[1][i];

		priv->dai[i].dynamic = 1;
		if (i == num_dai - 1)
			priv->dai[i].capture_only  = 1;
		else
			priv->dai[i].playback_only = 1;
		priv->dai[i].ignore_pmdown_time = 1;
		priv->dai[i].ops = &imx_audmix_fe_ops;

		/* Add AUDMIX Backend */
		be_name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
					 "audmix-%d", i);
		if (!be_name)
			return -ENOMEM;

		priv->dai[num_dai + i].cpus	= &dlc[1];
		priv->dai[num_dai + i].codecs	= &snd_soc_dummy_dlc;

		priv->dai[num_dai + i].num_cpus = 1;
		priv->dai[num_dai + i].num_codecs = 1;

		priv->dai[num_dai + i].name = be_name;
		priv->dai[num_dai + i].cpus->of_node = audmix_np;
		priv->dai[num_dai + i].cpus->dai_name = be_name;
		priv->dai[num_dai + i].no_pcm = 1;
		if (i == num_dai - 1)
			priv->dai[num_dai + i].capture_only  = 1;
		else
			priv->dai[num_dai + i].playback_only = 1;
		priv->dai[num_dai + i].ignore_pmdown_time = 1;
		priv->dai[num_dai + i].ops = &imx_audmix_be_ops;

		priv->dai_conf[i].dlc.of_node = args.np;
		priv->dai_conf[i].name_prefix = dai_name;

		if (i == num_dai - 1) {
			priv->dapm_routes[i].sink =
				devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
					       dai_name, name[2][i]);
			if (!priv->dapm_routes[i].sink)
				return -ENOMEM;

			priv->dapm_routes[i].source = name[3][i];
		} else {
			priv->dapm_routes[i].source =
				devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
					       dai_name, name[3][i]);
			if (!priv->dapm_routes[i].source)
				return -ENOMEM;

			priv->dapm_routes[i].sink = name[2][i];
		}
	}

	cpu_pdev = of_find_device_by_node(out_cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SAI platform device\n");
		return -EINVAL;
	}
	put_device(&cpu_pdev->dev);

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
