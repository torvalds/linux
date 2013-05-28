/*
 * File:         sound/soc/blackfin/bf5xx-tdm.c
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      Thurs June 04 2009
 * Description:  Blackfin I2S(TDM) CPU DAI driver
 *              Even though TDM mode can be as part of I2S DAI, but there
 *              are so much difference in configuration and data flow,
 *              it's very ugly to integrate I2S and TDM into a module
 *
 * Modified:
 *               Copyright 2009 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <asm/irq.h>
#include <asm/portmux.h>
#include <linux/mutex.h>
#include <linux/gpio.h>

#include "bf5xx-sport.h"
#include "bf5xx-tdm.h"

static int bf5xx_tdm_set_dai_fmt(struct snd_soc_dai *cpu_dai,
	unsigned int fmt)
{
	int ret = 0;

	/* interface format:support TDM,slave mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		break;
	default:
		printk(KERN_ERR "%s: Unknown DAI format type\n", __func__);
		ret = -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		printk(KERN_ERR "%s: Unknown DAI master type\n", __func__);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bf5xx_tdm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_tdm_port *bf5xx_tdm = sport_handle->private_data;
	int ret = 0;

	bf5xx_tdm->tcr2 &= ~0x1f;
	bf5xx_tdm->rcr2 &= ~0x1f;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		bf5xx_tdm->tcr2 |= 31;
		bf5xx_tdm->rcr2 |= 31;
		sport_handle->wdsize = 4;
		break;
		/* at present, we only support 32bit transfer */
	default:
		pr_err("not supported PCM format yet\n");
		return -EINVAL;
		break;
	}

	if (!bf5xx_tdm->configured) {
		/*
		 * TX and RX are not independent,they are enabled at the
		 * same time, even if only one side is running. So, we
		 * need to configure both of them at the time when the first
		 * stream is opened.
		 *
		 * CPU DAI:slave mode.
		 */
		ret = sport_config_rx(sport_handle, bf5xx_tdm->rcr1,
			bf5xx_tdm->rcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}

		ret = sport_config_tx(sport_handle, bf5xx_tdm->tcr1,
			bf5xx_tdm->tcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}

		bf5xx_tdm->configured = 1;
	}

	return 0;
}

static void bf5xx_tdm_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_tdm_port *bf5xx_tdm = sport_handle->private_data;

	/* No active stream, SPORT is allowed to be configured again. */
	if (!dai->active)
		bf5xx_tdm->configured = 0;
}

static int bf5xx_tdm_set_channel_map(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_tdm_port *bf5xx_tdm = sport_handle->private_data;
	int i;
	unsigned int slot;
	unsigned int tx_mapped = 0, rx_mapped = 0;

	if ((tx_num > BFIN_TDM_DAI_MAX_SLOTS) ||
			(rx_num > BFIN_TDM_DAI_MAX_SLOTS))
		return -EINVAL;

	for (i = 0; i < tx_num; i++) {
		slot = tx_slot[i];
		if ((slot < BFIN_TDM_DAI_MAX_SLOTS) &&
				(!(tx_mapped & (1 << slot)))) {
			bf5xx_tdm->tx_map[i] = slot;
			tx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}
	for (i = 0; i < rx_num; i++) {
		slot = rx_slot[i];
		if ((slot < BFIN_TDM_DAI_MAX_SLOTS) &&
				(!(rx_mapped & (1 << slot)))) {
			bf5xx_tdm->rx_map[i] = slot;
			rx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int bf5xx_tdm_suspend(struct snd_soc_dai *dai)
{
	struct sport_device *sport = snd_soc_dai_get_drvdata(dai);

	if (dai->playback_active)
		sport_tx_stop(sport);
	if (dai->capture_active)
		sport_rx_stop(sport);

	/* isolate sync/clock pins from codec while sports resume */
	peripheral_free_list(sport->pin_req);

	return 0;
}

static int bf5xx_tdm_resume(struct snd_soc_dai *dai)
{
	int ret;
	struct sport_device *sport = snd_soc_dai_get_drvdata(dai);

	ret = sport_set_multichannel(sport, 8, 0xFF, 0xFF, 1);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	ret = sport_config_rx(sport, 0, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	ret = sport_config_tx(sport, 0, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	peripheral_request_list(sport->pin_req, "soc-audio");

	return 0;
}

#else
#define bf5xx_tdm_suspend      NULL
#define bf5xx_tdm_resume       NULL
#endif

static const struct snd_soc_dai_ops bf5xx_tdm_dai_ops = {
	.hw_params      = bf5xx_tdm_hw_params,
	.set_fmt        = bf5xx_tdm_set_dai_fmt,
	.shutdown       = bf5xx_tdm_shutdown,
	.set_channel_map   = bf5xx_tdm_set_channel_map,
};

static struct snd_soc_dai_driver bf5xx_tdm_dai = {
	.suspend = bf5xx_tdm_suspend,
	.resume = bf5xx_tdm_resume,
	.playback = {
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,},
	.ops = &bf5xx_tdm_dai_ops,
};

static const struct snd_soc_component_driver bf5xx_tdm_component = {
	.name		= "bf5xx-tdm",
};

static int bfin_tdm_probe(struct platform_device *pdev)
{
	struct sport_device *sport_handle;
	int ret;

	/* configure SPORT for TDM */
	sport_handle = sport_init(pdev, 4, 8 * sizeof(u32),
		sizeof(struct bf5xx_tdm_port));
	if (!sport_handle)
		return -ENODEV;

	/* SPORT works in TDM mode */
	ret = sport_set_multichannel(sport_handle, 8, 0xFF, 0xFF, 1);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = sport_config_rx(sport_handle, 0, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = sport_config_tx(sport_handle, 0, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = snd_soc_register_component(&pdev->dev, &bf5xx_tdm_component,
					 &bf5xx_tdm_dai, 1);
	if (ret) {
		pr_err("Failed to register DAI: %d\n", ret);
		goto sport_config_err;
	}

	return 0;

sport_config_err:
	sport_done(sport_handle);
	return ret;
}

static int bfin_tdm_remove(struct platform_device *pdev)
{
	struct sport_device *sport_handle = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);
	sport_done(sport_handle);

	return 0;
}

static struct platform_driver bfin_tdm_driver = {
	.probe  = bfin_tdm_probe,
	.remove = bfin_tdm_remove,
	.driver = {
		.name   = "bfin-tdm",
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(bfin_tdm_driver);

/* Module information */
MODULE_AUTHOR("Barry Song");
MODULE_DESCRIPTION("TDM driver for ADI Blackfin");
MODULE_LICENSE("GPL");

