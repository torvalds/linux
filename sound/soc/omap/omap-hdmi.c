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
#include <sound/asound.h>
#include <sound/asoundef.h>
#include <sound/dmaengine_pcm.h>
#include <video/omapdss.h>

#include "omap-hdmi.h"

#define DRV_NAME "omap-hdmi-audio-dai"

struct hdmi_priv {
	struct snd_dmaengine_dai_dma_data dma_data;
	unsigned int dma_req;
	struct omap_dss_audio dss_audio;
	struct snd_aes_iec958 iec;
	struct snd_cea_861_aud_if cea;
	struct omap_dss_device *dssdev;
};

static int omap_hdmi_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct hdmi_priv *priv = snd_soc_dai_get_drvdata(dai);
	int err;
	/*
	 * Make sure that the period bytes are multiple of the DMA packet size.
	 * Largest packet size we use is 32 32-bit words = 128 bytes
	 */
	err = snd_pcm_hw_constraint_step(substream->runtime, 0,
				 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 128);
	if (err < 0) {
		dev_err(dai->dev, "could not apply constraint\n");
		return err;
	}

	if (!priv->dssdev->driver->audio_supported(priv->dssdev)) {
		dev_err(dai->dev, "audio not supported\n");
		return -ENODEV;
	}

	snd_soc_dai_set_dma_data(dai, substream, &priv->dma_data);

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
	struct snd_aes_iec958 *iec = &priv->iec;
	struct snd_cea_861_aud_if *cea = &priv->cea;
	int err = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		priv->dma_data.maxburst = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		priv->dma_data.maxburst = 32;
		break;
	default:
		dev_err(dai->dev, "format not supported!\n");
		return -EINVAL;
	}

	/*
	 * fill the IEC-60958 channel status word
	 */
	/* initialize the word bytes */
	memset(iec->status, 0, sizeof(iec->status));

	/* specify IEC-60958-3 (commercial use) */
	iec->status[0] &= ~IEC958_AES0_PROFESSIONAL;

	/* specify that the audio is LPCM*/
	iec->status[0] &= ~IEC958_AES0_NONAUDIO;

	iec->status[0] |= IEC958_AES0_CON_NOT_COPYRIGHT;

	iec->status[0] |= IEC958_AES0_CON_EMPHASIS_NONE;

	iec->status[0] |= IEC958_AES1_PRO_MODE_NOTID;

	iec->status[1] = IEC958_AES1_CON_GENERAL;

	iec->status[2] |= IEC958_AES2_CON_SOURCE_UNSPEC;

	iec->status[2] |= IEC958_AES2_CON_CHANNEL_UNSPEC;

	switch (params_rate(params)) {
	case 32000:
		iec->status[3] |= IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		iec->status[3] |= IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		iec->status[3] |= IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		iec->status[3] |= IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		iec->status[3] |= IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		iec->status[3] |= IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		iec->status[3] |= IEC958_AES3_CON_FS_192000;
		break;
	default:
		dev_err(dai->dev, "rate not supported!\n");
		return -EINVAL;
	}

	/* specify the clock accuracy */
	iec->status[3] |= IEC958_AES3_CON_CLOCK_1000PPM;

	/*
	 * specify the word length. The same word length value can mean
	 * two different lengths. Hence, we need to specify the maximum
	 * word length as well.
	 */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		iec->status[4] |= IEC958_AES4_CON_WORDLEN_20_16;
		iec->status[4] &= ~IEC958_AES4_CON_MAX_WORDLEN_24;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iec->status[4] |= IEC958_AES4_CON_WORDLEN_24_20;
		iec->status[4] |= IEC958_AES4_CON_MAX_WORDLEN_24;
		break;
	default:
		dev_err(dai->dev, "format not supported!\n");
		return -EINVAL;
	}

	/*
	 * Fill the CEA-861 audio infoframe (see spec for details)
	 */

	cea->db1_ct_cc = (params_channels(params) - 1)
		& CEA861_AUDIO_INFOFRAME_DB1CC;
	cea->db1_ct_cc |= CEA861_AUDIO_INFOFRAME_DB1CT_FROM_STREAM;

	cea->db2_sf_ss = CEA861_AUDIO_INFOFRAME_DB2SF_FROM_STREAM;
	cea->db2_sf_ss |= CEA861_AUDIO_INFOFRAME_DB2SS_FROM_STREAM;

	cea->db3 = 0; /* not used, all zeros */

	/*
	 * The OMAP HDMI IP requires to use the 8-channel channel code when
	 * transmitting more than two channels.
	 */
	if (params_channels(params) == 2)
		cea->db4_ca = 0x0;
	else
		cea->db4_ca = 0x13;

	cea->db5_dminh_lsv = CEA861_AUDIO_INFOFRAME_DB5_DM_INH_PROHIBITED;
	/* the expression is trivial but makes clear what we are doing */
	cea->db5_dminh_lsv |= (0 & CEA861_AUDIO_INFOFRAME_DB5_LSV);

	priv->dss_audio.iec = iec;
	priv->dss_audio.cea = cea;

	err = priv->dssdev->driver->audio_config(priv->dssdev,
						 &priv->dss_audio);

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
	.prepare	= omap_hdmi_dai_prepare,
	.trigger	= omap_hdmi_dai_trigger,
	.shutdown	= omap_hdmi_dai_shutdown,
};

static struct snd_soc_dai_driver omap_hdmi_dai = {
	.playback = {
		.channels_min = 2,
		.channels_max = 8,
		.rates = OMAP_HDMI_RATES,
		.formats = OMAP_HDMI_FORMATS,
	},
	.ops = &omap_hdmi_dai_ops,
};

static const struct snd_soc_component_driver omap_hdmi_component = {
	.name		= DRV_NAME,
};

static int omap_hdmi_probe(struct platform_device *pdev)
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

	hdmi_data->dma_data.addr = hdmi_rsrc->start + OMAP_HDMI_AUDIO_DMA_PORT;

	hdmi_rsrc = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!hdmi_rsrc) {
		dev_err(&pdev->dev, "Cannot obtain IORESOURCE_DMA HDMI\n");
		return -ENODEV;
	}

	hdmi_data->dma_req = hdmi_rsrc->start;
	hdmi_data->dma_data.filter_data = &hdmi_data->dma_req;
	hdmi_data->dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

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
	ret = snd_soc_register_component(&pdev->dev, &omap_hdmi_component,
					 &omap_hdmi_dai, 1);

	return ret;
}

static int omap_hdmi_remove(struct platform_device *pdev)
{
	struct hdmi_priv *hdmi_data = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_component(&pdev->dev);

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
	.remove = omap_hdmi_remove,
};

module_platform_driver(hdmi_dai_driver);

MODULE_AUTHOR("Jorge Candelaria <jorge.candelaria@ti.com>");
MODULE_AUTHOR("Ricardo Neri <ricardo.neri@ti.com>");
MODULE_DESCRIPTION("OMAP HDMI SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
