/*
 * omap4-hdmi-card.c
 *
 * OMAP ALSA SoC machine driver for TI OMAP4 HDMI
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Ricardo Neri <ricardo.neri@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-types.h>
#include <video/omapdss.h>

#define DRV_NAME "omap-hdmi-audio"

static int omap4_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	int i;
	struct omap_overlay_manager *mgr = NULL;
	struct device *dev = substream->pcm->card->dev;

	/* Find DSS HDMI device */
	for (i = 0; i < omap_dss_get_num_overlay_managers(); i++) {
		mgr = omap_dss_get_overlay_manager(i);
		if (mgr && mgr->device
			&& mgr->device->type == OMAP_DISPLAY_TYPE_HDMI)
			break;
	}

	if (i == omap_dss_get_num_overlay_managers()) {
		dev_err(dev, "HDMI display device not found!\n");
		return -ENODEV;
	}

	/* Make sure HDMI is power-on to avoid L3 interconnect errors */
	if (mgr->device->state != OMAP_DSS_DISPLAY_ACTIVE) {
		dev_err(dev, "HDMI display is not active!\n");
		return -EIO;
	}

	return 0;
}

static struct snd_soc_ops omap4_hdmi_dai_ops = {
	.hw_params = omap4_hdmi_dai_hw_params,
};

static struct snd_soc_dai_link omap4_hdmi_dai = {
	.name = "HDMI",
	.stream_name = "HDMI",
	.cpu_dai_name = "omap-hdmi-audio-dai",
	.platform_name = "omap-pcm-audio",
	.codec_name = "hdmi-audio-codec",
	.codec_dai_name = "omap-hdmi-hifi",
	.ops = &omap4_hdmi_dai_ops,
};

static struct snd_soc_card snd_soc_omap4_hdmi = {
	.name = "OMAP4HDMI",
	.owner = THIS_MODULE,
	.dai_link = &omap4_hdmi_dai,
	.num_links = 1,
};

static __devinit int omap4_hdmi_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_omap4_hdmi;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		card->dev = NULL;
		return ret;
	}
	return 0;
}

static int __devexit omap4_hdmi_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	card->dev = NULL;
	return 0;
}

static struct platform_driver omap4_hdmi_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = omap4_hdmi_probe,
	.remove = __devexit_p(omap4_hdmi_remove),
};

module_platform_driver(omap4_hdmi_driver);

MODULE_AUTHOR("Ricardo Neri <ricardo.neri@ti.com>");
MODULE_DESCRIPTION("OMAP4 HDMI machine ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
