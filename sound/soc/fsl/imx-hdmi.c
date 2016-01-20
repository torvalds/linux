/*
 * ASoC HDMI Transmitter driver for IMX development boards
 *
 * Copyright (C) 2011-2016 Freescale Semiconductor, Inc.
 *
 * based on stmp3780_devb_hdmi.c
 *
 * Vladimir Barinov <vbarinov@embeddedalley.com>
 *
 * Copyright 2008 SigmaTel, Inc
 * Copyright 2008 Embedded Alley Solutions, Inc
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/mfd/mxc-hdmi-core.h>
#include <sound/soc.h>

#include "imx-hdmi.h"

/* imx digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link imx_hdmi_dai_link = {
	.name = "i.MX HDMI Audio Tx",
	.stream_name = "i.MX HDMI Audio Tx",
	.codec_dai_name = "hdmi-hifi",
	.codec_name = "hdmi-audio-codec",
	.platform_name = "imx-hdmi-audio",
};

static struct snd_soc_card snd_soc_card_imx_hdmi = {
	.name = "imx-hdmi-soc",
	.dai_link = &imx_hdmi_dai_link,
	.num_links = 1,
	.owner = THIS_MODULE,
};

static int imx_hdmi_audio_probe(struct platform_device *pdev)
{
	struct device_node *hdmi_np, *np = pdev->dev.of_node;
	struct snd_soc_card *card = &snd_soc_card_imx_hdmi;
	struct platform_device *hdmi_pdev;
	int ret = 0;

	if (!hdmi_get_registered()) {
		dev_err(&pdev->dev, "initialize HDMI-audio failed. load HDMI-video first!\n");
		return -ENODEV;
	}

	hdmi_np = of_parse_phandle(np, "hdmi-controller", 0);
	if (!hdmi_np) {
		dev_err(&pdev->dev, "failed to find hdmi-audio cpudai\n");
		ret = -EINVAL;
		goto end;
	}

	hdmi_pdev = of_find_device_by_node(hdmi_np);
	if (!hdmi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto end;
	}

	card->dev = &pdev->dev;
	card->dai_link->cpu_dai_name = dev_name(&hdmi_pdev->dev);

	platform_set_drvdata(pdev, card);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "failed to register card: %d\n", ret);

end:
	if (hdmi_np)
		of_node_put(hdmi_np);

	return ret;
}

static int imx_hdmi_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id imx_hdmi_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-hdmi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_hdmi_dt_ids);

static struct platform_driver imx_hdmi_audio_driver = {
	.probe = imx_hdmi_audio_probe,
	.remove = imx_hdmi_audio_remove,
	.driver = {
		.of_match_table = imx_hdmi_dt_ids,
		.name = "imx-audio-hdmi",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(imx_hdmi_audio_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IMX HDMI TX ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-audio-hdmi");
