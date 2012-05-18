/*
 * omap-hdmi.c
 *
 * OMAP ALSA SoC DAI driver for HDMI audio on OMAP4 processors.
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 * Authors: Jorge Candelaria <jorge.candelaria@ti.com>
 *          Ricardo Neri <ricardo.neri@ti.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <video/omapdss.h>

#include <plat/dma.h>
#include "omap-pcm.h"
#include "omap-hdmi.h"

#define DRV_NAME "omap-hdmi-audio-dai"

struct hdmi_priv {
	struct omap_pcm_dma_data dma_params;
	struct omap_dss_device *dssdev;
};

static int omap_hdmi_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	int err;
	/*
	 * Make sure that the period bytes are multiple of the DMA packet size.
	 * Largest packet size we use is 32 32-bit words = 128 bytes
	 */
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 128);
	if (err < 0)
		return err;

	return 0;
}

static int omap_hdmi_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdmi_priv *priv = snd_soc_dai_get_drvdata(dai);

	return priv->dssdev->driver->audio_enable(priv->dssdev);
}

static int omap_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct hdmi_priv *priv = snd_soc_dai_get_drvdata(dai);
	int err = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		priv->dma_params.packet_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		priv->dma_params.packet_size = 32;
		break;
	default:
		err = -EINVAL;
	}

	priv->dma_params.data_type = OMAP_DMA_DATA_TYPE_S32;

	snd_soc_dai_set_dma_data(dai, substream,
				 &priv->dma_params);

	return err;
}

static int omap_hdmi_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct hdmi_priv *priv = snd_soc_dai_get_drvdata(dai);
	int err = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		err = priv->dssdev->driver->audio_start(priv->dssdev);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		priv->dssdev->driver->audio_stop(priv->dssdev);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static void omap_hdmi_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdmi_priv *priv = snd_soc_dai_get_drvdata(dai);

	priv->dssdev->driver->audio_disable(priv->dssdev);
}

static const struct snd_soc_dai_ops omap_hdmi_dai_ops = {
	.startup	= omap_hdmi_dai_startup,
	.hw_params	= omap_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver omap_hdmi_dai = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = OMAP_HDMI_RATES,
		.formats = OMAP_HDMI_FORMATS,
	},
	.ops = &omap_hdmi_dai_ops,
};

static __devinit int omap_hdmi_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *hdmi_rsrc;
	struct hdmi_priv *hdmi_data;
	bool hdmi_dev_found = false;

	hdmi_data = devm_kzalloc(&pdev->dev, sizeof(*hdmi_data), GFP_KERNEL);
	if (hdmi_data == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory for HDMI data\n");
		return -ENOMEM;
	}

	hdmi_rsrc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!hdmi_rsrc) {
		dev_err(&pdev->dev, "Cannot obtain IORESOURCE_MEM HDMI\n");
		return -ENODEV;
	}

	hdmi_data->dma_params.port_addr =  hdmi_rsrc->start
		+ OMAP_HDMI_AUDIO_DMA_PORT;

	hdmi_rsrc = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!hdmi_rsrc) {
		dev_err(&pdev->dev, "Cannot obtain IORESOURCE_DMA HDMI\n");
		return -ENODEV;
	}

	hdmi_data->dma_params.dma_req =  hdmi_rsrc->start;
	hdmi_data->dma_params.name = "HDMI playback";
	hdmi_data->dma_params.sync_mode = OMAP_DMA_SYNC_PACKET;

	/*
	 * TODO: We assume that there is only one DSS HDMI device. Future
	 * OMAP implementations may support more than one HDMI devices and
	 * we should provided separate audio support for all of them.
	 */
	/* Find an HDMI device. */
	for_each_dss_dev(hdmi_data->dssdev) {
		omap_dss_get_device(hdmi_data->dssdev);

		if (!hdmi_data->dssdev->driver) {
			omap_dss_put_device(hdmi_data->dssdev);
			continue;
		}

		if (hdmi_data->dssdev->type == OMAP_DISPLAY_TYPE_HDMI) {
			hdmi_dev_found = true;
			break;
		}
	}

	if (!hdmi_dev_found) {
		dev_err(&pdev->dev, "no driver for HDMI display found\n");
		return -ENODEV;
	}

	dev_set_drvdata(&pdev->dev, hdmi_data);
	ret = snd_soc_register_dai(&pdev->dev, &omap_hdmi_dai);

	return ret;
}

static int __devexit omap_hdmi_remove(struct platform_device *pdev)
{
	struct hdmi_priv *hdmi_data = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_dai(&pdev->dev);

	if (hdmi_data == NULL) {
		dev_err(&pdev->dev, "cannot obtain HDMi data\n");
		return -ENODEV;
	}

	omap_dss_put_device(hdmi_data->dssdev);
	return 0;
}

static struct platform_driver hdmi_dai_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = omap_hdmi_probe,
	.remove = __devexit_p(omap_hdmi_remove),
};

module_platform_driver(hdmi_dai_driver);

MODULE_AUTHOR("Jorge Candelaria <jorge.candelaria@ti.com>");
MODULE_AUTHOR("Ricardo Neri <ricardo.neri@ti.com>");
MODULE_DESCRIPTION("OMAP HDMI SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
