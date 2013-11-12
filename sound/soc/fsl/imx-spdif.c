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
	struct snd_soc_dai_link dai[2];
	struct snd_soc_card card;
	struct platform_device *txdev;
	struct platform_device *rxdev;
};

static int imx_spdif_audio_probe(struct platform_device *pdev)
{
	struct device_node *spdif_np, *np = pdev->dev.of_node;
	struct imx_spdif_data *data;
	int ret = 0, num_links = 0;

	spdif_np = of_parse_phandle(np, "spdif-controller", 0);
	if (!spdif_np) {
		dev_err(&pdev->dev, "failed to find spdif-controller\n");
		ret = -EINVAL;
		goto end;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto end;
	}

	if (of_property_read_bool(np, "spdif-out")) {
		data->dai[num_links].name = "S/PDIF TX";
		data->dai[num_links].stream_name = "S/PDIF PCM Playback";
		data->dai[num_links].codec_dai_name = "dit-hifi";
		data->dai[num_links].codec_name = "spdif-dit";
		data->dai[num_links].cpu_of_node = spdif_np;
		data->dai[num_links].platform_of_node = spdif_np;
		num_links++;

		data->txdev = platform_device_register_simple("spdif-dit", -1, NULL, 0);
		if (IS_ERR(data->txdev)) {
			ret = PTR_ERR(data->txdev);
			dev_err(&pdev->dev, "register dit failed: %d\n", ret);
			goto end;
		}
	}

	if (of_property_read_bool(np, "spdif-in")) {
		data->dai[num_links].name = "S/PDIF RX";
		data->dai[num_links].stream_name = "S/PDIF PCM Capture";
		data->dai[num_links].codec_dai_name = "dir-hifi";
		data->dai[num_links].codec_name = "spdif-dir";
		data->dai[num_links].cpu_of_node = spdif_np;
		data->dai[num_links].platform_of_node = spdif_np;
		num_links++;

		data->rxdev = platform_device_register_simple("spdif-dir", -1, NULL, 0);
		if (IS_ERR(data->rxdev)) {
			ret = PTR_ERR(data->rxdev);
			dev_err(&pdev->dev, "register dir failed: %d\n", ret);
			goto error_dit;
		}
	}

	if (!num_links) {
		dev_err(&pdev->dev, "no enabled S/PDIF DAI link\n");
		goto error_dir;
	}

	data->card.dev = &pdev->dev;
	data->card.num_links = num_links;
	data->card.dai_link = data->dai;

	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto error_dir;

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed: %d\n", ret);
		goto error_dir;
	}

	platform_set_drvdata(pdev, data);

	goto end;

error_dir:
	if (data->rxdev)
		platform_device_unregister(data->rxdev);
error_dit:
	if (data->txdev)
		platform_device_unregister(data->txdev);
end:
	if (spdif_np)
		of_node_put(spdif_np);

	return ret;
}

static int imx_spdif_audio_remove(struct platform_device *pdev)
{
	struct imx_spdif_data *data = platform_get_drvdata(pdev);

	if (data->rxdev)
		platform_device_unregister(data->rxdev);
	if (data->txdev)
		platform_device_unregister(data->txdev);

	return 0;
}

static const struct of_device_id imx_spdif_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-spdif", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_spdif_dt_ids);

static struct platform_driver imx_spdif_driver = {
	.driver = {
		.name = "imx-spdif",
		.owner = THIS_MODULE,
		.of_match_table = imx_spdif_dt_ids,
	},
	.probe = imx_spdif_audio_probe,
	.remove = imx_spdif_audio_remove,
};

module_platform_driver(imx_spdif_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale i.MX S/PDIF machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-spdif");
