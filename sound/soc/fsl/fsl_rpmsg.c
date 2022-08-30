// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018-2021 NXP

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_rpmsg.h"
#include "imx-pcm.h"

#define FSL_RPMSG_RATES        (SNDRV_PCM_RATE_8000 | \
				SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_48000)
#define FSL_RPMSG_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

/* 192kHz/32bit/2ch/60s size is 0x574e00 */
#define LPA_LARGE_BUFFER_SIZE  (0x6000000)

static const unsigned int fsl_rpmsg_rates[] = {
	8000, 11025, 16000, 22050, 44100,
	32000, 48000, 96000, 88200, 176400, 192000,
	352800, 384000, 705600, 768000, 1411200, 2822400,
};

static const struct snd_pcm_hw_constraint_list fsl_rpmsg_rate_constraints = {
	.count = ARRAY_SIZE(fsl_rpmsg_rates),
	.list = fsl_rpmsg_rates,
};

static int fsl_rpmsg_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct fsl_rpmsg *rpmsg = snd_soc_dai_get_drvdata(dai);
	struct clk *p = rpmsg->mclk, *pll = NULL, *npll = NULL;
	u64 rate = params_rate(params);
	int ret = 0;

	/* Get current pll parent */
	while (p && rpmsg->pll8k && rpmsg->pll11k) {
		struct clk *pp = clk_get_parent(p);

		if (clk_is_match(pp, rpmsg->pll8k) ||
		    clk_is_match(pp, rpmsg->pll11k)) {
			pll = pp;
			break;
		}
		p = pp;
	}

	/* Switch to another pll parent if needed. */
	if (pll) {
		npll = (do_div(rate, 8000) ? rpmsg->pll11k : rpmsg->pll8k);
		if (!clk_is_match(pll, npll)) {
			ret = clk_set_parent(p, npll);
			if (ret < 0)
				dev_warn(dai->dev, "failed to set parent %s: %d\n",
					 __clk_get_name(npll), ret);
		}
	}

	if (!(rpmsg->mclk_streams & BIT(substream->stream))) {
		ret = clk_prepare_enable(rpmsg->mclk);
		if (ret) {
			dev_err(dai->dev, "failed to enable mclk: %d\n", ret);
			return ret;
		}

		rpmsg->mclk_streams |= BIT(substream->stream);
	}

	return ret;
}

static int fsl_rpmsg_hw_free(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct fsl_rpmsg *rpmsg = snd_soc_dai_get_drvdata(dai);

	if (rpmsg->mclk_streams & BIT(substream->stream)) {
		clk_disable_unprepare(rpmsg->mclk);
		rpmsg->mclk_streams &= ~BIT(substream->stream);
	}

	return 0;
}

static int fsl_rpmsg_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *cpu_dai)
{
	int ret;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &fsl_rpmsg_rate_constraints);

	return ret;
}

static const struct snd_soc_dai_ops fsl_rpmsg_dai_ops = {
	.startup	= fsl_rpmsg_startup,
	.hw_params      = fsl_rpmsg_hw_params,
	.hw_free        = fsl_rpmsg_hw_free,
};

static struct snd_soc_dai_driver fsl_rpmsg_dai = {
	.playback = {
		.stream_name = "CPU-Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_RPMSG_FORMATS,
	},
	.capture = {
		.stream_name = "CPU-Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_RPMSG_FORMATS,
	},
	.symmetric_rate        = 1,
	.symmetric_channels    = 1,
	.symmetric_sample_bits = 1,
	.ops = &fsl_rpmsg_dai_ops,
};

static const struct snd_soc_component_driver fsl_component = {
	.name			= "fsl-rpmsg",
	.legacy_dai_naming	= 1,
};

static const struct fsl_rpmsg_soc_data imx7ulp_data = {
	.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
		 SNDRV_PCM_RATE_48000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
};

static const struct fsl_rpmsg_soc_data imx8mm_data = {
	.rates = SNDRV_PCM_RATE_KNOT,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_DSD_U8 |
		   SNDRV_PCM_FMTBIT_DSD_U16_LE | SNDRV_PCM_FMTBIT_DSD_U32_LE,
};

static const struct fsl_rpmsg_soc_data imx8mn_data = {
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
		 SNDRV_PCM_RATE_192000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
};

static const struct fsl_rpmsg_soc_data imx8mp_data = {
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
		 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
		 SNDRV_PCM_RATE_192000,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
};

static const struct of_device_id fsl_rpmsg_ids[] = {
	{ .compatible = "fsl,imx7ulp-rpmsg-audio", .data = &imx7ulp_data},
	{ .compatible = "fsl,imx8mm-rpmsg-audio", .data = &imx8mm_data},
	{ .compatible = "fsl,imx8mn-rpmsg-audio", .data = &imx8mn_data},
	{ .compatible = "fsl,imx8mp-rpmsg-audio", .data = &imx8mp_data},
	{ .compatible = "fsl,imx8ulp-rpmsg-audio", .data = &imx7ulp_data},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_rpmsg_ids);

static int fsl_rpmsg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_rpmsg *rpmsg;
	int ret;

	rpmsg = devm_kzalloc(&pdev->dev, sizeof(struct fsl_rpmsg), GFP_KERNEL);
	if (!rpmsg)
		return -ENOMEM;

	rpmsg->soc_data = of_device_get_match_data(&pdev->dev);

	fsl_rpmsg_dai.playback.rates = rpmsg->soc_data->rates;
	fsl_rpmsg_dai.capture.rates = rpmsg->soc_data->rates;
	fsl_rpmsg_dai.playback.formats = rpmsg->soc_data->formats;
	fsl_rpmsg_dai.capture.formats = rpmsg->soc_data->formats;

	if (of_property_read_bool(np, "fsl,enable-lpa")) {
		rpmsg->enable_lpa = 1;
		rpmsg->buffer_size = LPA_LARGE_BUFFER_SIZE;
	} else {
		rpmsg->buffer_size = IMX_DEFAULT_DMABUF_SIZE;
	}

	/* Get the optional clocks */
	rpmsg->ipg = devm_clk_get_optional(&pdev->dev, "ipg");
	if (IS_ERR(rpmsg->ipg))
		return PTR_ERR(rpmsg->ipg);

	rpmsg->mclk = devm_clk_get_optional(&pdev->dev, "mclk");
	if (IS_ERR(rpmsg->mclk))
		return PTR_ERR(rpmsg->mclk);

	rpmsg->dma = devm_clk_get_optional(&pdev->dev, "dma");
	if (IS_ERR(rpmsg->dma))
		return PTR_ERR(rpmsg->dma);

	rpmsg->pll8k = devm_clk_get_optional(&pdev->dev, "pll8k");
	if (IS_ERR(rpmsg->pll8k))
		return PTR_ERR(rpmsg->pll8k);

	rpmsg->pll11k = devm_clk_get_optional(&pdev->dev, "pll11k");
	if (IS_ERR(rpmsg->pll11k))
		return PTR_ERR(rpmsg->pll11k);

	platform_set_drvdata(pdev, rpmsg);
	pm_runtime_enable(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &fsl_component,
					      &fsl_rpmsg_dai, 1);
	if (ret)
		return ret;

	rpmsg->card_pdev = platform_device_register_data(&pdev->dev,
							 "imx-audio-rpmsg",
							 PLATFORM_DEVID_NONE,
							 NULL,
							 0);
	if (IS_ERR(rpmsg->card_pdev)) {
		dev_err(&pdev->dev, "failed to register rpmsg card\n");
		ret = PTR_ERR(rpmsg->card_pdev);
		return ret;
	}

	return 0;
}

static int fsl_rpmsg_remove(struct platform_device *pdev)
{
	struct fsl_rpmsg *rpmsg = platform_get_drvdata(pdev);

	if (rpmsg->card_pdev)
		platform_device_unregister(rpmsg->card_pdev);

	return 0;
}

#ifdef CONFIG_PM
static int fsl_rpmsg_runtime_resume(struct device *dev)
{
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(rpmsg->ipg);
	if (ret) {
		dev_err(dev, "failed to enable ipg clock: %d\n", ret);
		goto ipg_err;
	}

	ret = clk_prepare_enable(rpmsg->dma);
	if (ret) {
		dev_err(dev, "Failed to enable dma clock %d\n", ret);
		goto dma_err;
	}

	return 0;

dma_err:
	clk_disable_unprepare(rpmsg->ipg);
ipg_err:
	return ret;
}

static int fsl_rpmsg_runtime_suspend(struct device *dev)
{
	struct fsl_rpmsg *rpmsg = dev_get_drvdata(dev);

	clk_disable_unprepare(rpmsg->dma);
	clk_disable_unprepare(rpmsg->ipg);

	return 0;
}
#endif

static const struct dev_pm_ops fsl_rpmsg_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_rpmsg_runtime_suspend,
			   fsl_rpmsg_runtime_resume,
			   NULL)
};

static struct platform_driver fsl_rpmsg_driver = {
	.probe  = fsl_rpmsg_probe,
	.remove = fsl_rpmsg_remove,
	.driver = {
		.name = "fsl_rpmsg",
		.pm = &fsl_rpmsg_pm_ops,
		.of_match_table = fsl_rpmsg_ids,
	},
};
module_platform_driver(fsl_rpmsg_driver);

MODULE_DESCRIPTION("Freescale SoC Audio PRMSG CPU Interface");
MODULE_AUTHOR("Shengjiu Wang <shengjiu.wang@nxp.com>");
MODULE_ALIAS("platform:fsl_rpmsg");
MODULE_LICENSE("GPL");
