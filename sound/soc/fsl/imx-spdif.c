/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/soc.h>

struct imx_spdif_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
};

static int imx_spdif_audio_probe(struct platform_device *pdev)
{
	struct device_node *spdif_np, *np = pdev->dev.of_node;
	struct imx_spdif_data *data;
	int ret = 0;

	spdif_np = of_parse_phandle(np, "spdif-controller", 0);
	if (!spdif_np) {
		dev_err(&pdev->dev, "failed to find spdif-controller\n");
		ret = -EINVAL;
		goto end;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto end;
	}

	data->dai.name = "S/PDIF PCM";
	data->dai.stream_name = "S/PDIF PCM";
	data->dai.codec_dai_name = "snd-soc-dummy-dai";
	data->dai.codec_name = "snd-soc-dummy";
	data->dai.cpu_of_node = spdif_np;
	data->dai.platform_of_node = spdif_np;
	data->dai.playback_only = true;
	data->dai.capture_only = true;

	if (of_property_read_bool(np, "spdif-out"))
		data->dai.capture_only = false;

	if (of_property_read_bool(np, "spdif-in"))
		data->dai.playback_only = false;

	if (data->dai.playback_only && data->dai.capture_only) {
		dev_err(&pdev->dev, "no enabled S/PDIF DAI link\n");
		goto end;
	}

	data->card.dev = &pdev->dev;
	data->card.dai_link = &data->dai;
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;

	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto end;

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed: %d\n", ret);
		goto end;
	}

	platform_set_drvdata(pdev, data);

end:
	of_node_put(spdif_np);

	return ret;
}

static const struct of_device_id imx_spdif_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-spdif", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_spdif_dt_ids);

static struct platform_driver imx_spdif_driver = {
	.driver = {
		.name = "imx-spdif",
		.of_match_table = imx_spdif_dt_ids,
	},
	.probe = imx_spdif_audio_probe,
};

module_platform_driver(imx_spdif_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale i.MX S/PDIF machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-spdif");
