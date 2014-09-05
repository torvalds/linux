/*
 * Copyright (C) 2008-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <sound/soc.h>

#include "imx-audmux.h"

static int imx_audmux_config(int slave, int master)
{
	unsigned int ptcr, pdcr;
	slave = slave - 1;
	master = master - 1;

	ptcr = IMX_AUDMUX_V2_PTCR_SYN |
		IMX_AUDMUX_V2_PTCR_TFSDIR |
		IMX_AUDMUX_V2_PTCR_TFSEL(slave) |
		IMX_AUDMUX_V2_PTCR_TCLKDIR |
		IMX_AUDMUX_V2_PTCR_TCSEL(slave);
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(slave);
	imx_audmux_v2_configure_port(master, ptcr, pdcr);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN;
	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(master);
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);

	return 0;
}

static int imx_si476x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	u32 channels = params_channels(params);
	u32 rate = params_rate(params);
	u32 bclk = rate * channels * 32;
	int ret = 0;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai fmt\n");
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
			channels == 1 ? 0xfffffffe : 0xfffffffc,
			channels == 1 ? 0xfffffffe : 0xfffffffc,
			2, 32);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set dai tdm slot\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, bclk, SND_SOC_CLOCK_OUT);
	if (ret)
		dev_err(cpu_dai->dev, "failed to set sysclk\n");

	return ret;
}

static struct snd_soc_ops imx_si476x_ops = {
	.hw_params = imx_si476x_hw_params,
};

static struct snd_soc_dai_link imx_dai = {
	.name = "imx-si476x",
	.stream_name = "imx-si476x",
	.codec_dai_name = "si476x-codec",
	.codec_name = "si476x-codec.355",
	.ops = &imx_si476x_ops,
};

static struct snd_soc_card snd_soc_card_imx_3stack = {
	.name = "imx-audio-si476x",
	.dai_link = &imx_dai,
	.num_links = 1,
};

static int imx_si476x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_3stack;
	struct device_node *ssi_np, *np = pdev->dev.of_node;
	struct platform_device *ssi_pdev;
	struct i2c_client *fm_dev;
	struct device_node *fm_np;
	int int_port, ext_port, ret;

	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	imx_audmux_config(int_port, ext_port);

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	if (!ssi_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		return -EINVAL;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto end;
	}

	fm_np = of_parse_phandle(pdev->dev.of_node, "fm-controller", 0);
	if (!fm_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto end;
	}

	fm_dev = of_find_i2c_device_by_node(fm_np);
	if (!fm_dev) {
		dev_err(&pdev->dev, "failed to find FM platform device\n");
		ret = -EINVAL;
		goto end;
	}

	card->dev = &pdev->dev;
	card->dai_link->cpu_dai_name = dev_name(&ssi_pdev->dev);
	card->dai_link->platform_of_node = ssi_np;

	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card: %d\n", ret);

end:
	if (ssi_np)
		of_node_put(ssi_np);
	if (fm_np)
		of_node_put(fm_np);

	return ret;
}

static int imx_si476x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_card_imx_3stack;

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id imx_si476x_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-si476x", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_si476x_dt_ids);

static struct platform_driver imx_si476x_driver = {
	.driver = {
		.name = "imx-tuner-si476x",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_si476x_dt_ids,
	},
	.probe = imx_si476x_probe,
	.remove = imx_si476x_remove,
};

module_platform_driver(imx_si476x_driver);

/* Module information */
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("ALSA SoC i.MX si476x");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-tuner-si476x");
