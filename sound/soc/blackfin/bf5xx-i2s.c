/*
 * File:         sound/soc/blackfin/bf5xx-i2s.c
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 * Description:  Blackfin I2S CPU DAI driver
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
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
#include <linux/delay.h>
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
#include "bf5xx-i2s-pcm.h"

struct bf5xx_i2s_port {
	u16 tcr1;
	u16 rcr1;
	u16 tcr2;
	u16 rcr2;
	int configured;

	unsigned int slots;
	unsigned int tx_mask;
	unsigned int rx_mask;

	struct bf5xx_i2s_pcm_data tx_dma_data;
	struct bf5xx_i2s_pcm_data rx_dma_data;
};

static int bf5xx_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(cpu_dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;
	int ret = 0;

	/* interface format:support I2S,slave mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		bf5xx_i2s->tcr1 |= TFSR | TCKFE;
		bf5xx_i2s->rcr1 |= RFSR | RCKFE;
		bf5xx_i2s->tcr2 |= TSFSE;
		bf5xx_i2s->rcr2 |= RSFSE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		bf5xx_i2s->tcr1 |= TFSR;
		bf5xx_i2s->rcr1 |= RFSR;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ret = -EINVAL;
		break;
	default:
		dev_err(cpu_dai->dev, "%s: Unknown DAI format type\n",
			__func__);
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
		dev_err(cpu_dai->dev, "%s: Unknown DAI master type\n",
			__func__);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bf5xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;
	int ret = 0;

	bf5xx_i2s->tcr2 &= ~0x1f;
	bf5xx_i2s->rcr2 &= ~0x1f;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		bf5xx_i2s->tcr2 |= 7;
		bf5xx_i2s->rcr2 |= 7;
		sport_handle->wdsize = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		bf5xx_i2s->tcr2 |= 15;
		bf5xx_i2s->rcr2 |= 15;
		sport_handle->wdsize = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bf5xx_i2s->tcr2 |= 23;
		bf5xx_i2s->rcr2 |= 23;
		sport_handle->wdsize = 3;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bf5xx_i2s->tcr2 |= 31;
		bf5xx_i2s->rcr2 |= 31;
		sport_handle->wdsize = 4;
		break;
	}

	if (!bf5xx_i2s->configured) {
		/*
		 * TX and RX are not independent,they are enabled at the
		 * same time, even if only one side is running. So, we
		 * need to configure both of them at the time when the first
		 * stream is opened.
		 *
		 * CPU DAI:slave mode.
		 */
		bf5xx_i2s->configured = 1;
		ret = sport_config_rx(sport_handle, bf5xx_i2s->rcr1,
				      bf5xx_i2s->rcr2, 0, 0);
		if (ret) {
			dev_err(dai->dev, "SPORT is busy!\n");
			return -EBUSY;
		}

		ret = sport_config_tx(sport_handle, bf5xx_i2s->tcr1,
				      bf5xx_i2s->tcr2, 0, 0);
		if (ret) {
			dev_err(dai->dev, "SPORT is busy!\n");
			return -EBUSY;
		}
	}

	return 0;
}

static void bf5xx_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;

	dev_dbg(dai->dev, "%s enter\n", __func__);
	/* No active stream, SPORT is allowed to be configured again. */
	if (!dai->active)
		bf5xx_i2s->configured = 0;
}

static int bf5xx_i2s_set_channel_map(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;
	unsigned int tx_mapped = 0, rx_mapped = 0;
	unsigned int slot;
	int i;

	if ((tx_num > BFIN_TDM_DAI_MAX_SLOTS) ||
			(rx_num > BFIN_TDM_DAI_MAX_SLOTS))
		return -EINVAL;

	for (i = 0; i < tx_num; i++) {
		slot = tx_slot[i];
		if ((slot < BFIN_TDM_DAI_MAX_SLOTS) &&
				(!(tx_mapped & (1 << slot)))) {
			bf5xx_i2s->tx_dma_data.map[i] = slot;
			tx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}
	for (i = 0; i < rx_num; i++) {
		slot = rx_slot[i];
		if ((slot < BFIN_TDM_DAI_MAX_SLOTS) &&
				(!(rx_mapped & (1 << slot)))) {
			bf5xx_i2s->rx_dma_data.map[i] = slot;
			rx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}

	return 0;
}

static int bf5xx_i2s_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
	unsigned int rx_mask, int slots, int width)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;

	if (slots % 8 != 0 || slots > 8)
		return -EINVAL;

	if (width != 32)
		return -EINVAL;

	bf5xx_i2s->slots = slots;
	bf5xx_i2s->tx_mask = tx_mask;
	bf5xx_i2s->rx_mask = rx_mask;

	bf5xx_i2s->tx_dma_data.tdm_mode = slots != 0;
	bf5xx_i2s->rx_dma_data.tdm_mode = slots != 0;

	return sport_set_multichannel(sport_handle, slots, tx_mask, rx_mask, 0);
}

#ifdef CONFIG_PM
static int bf5xx_i2s_suspend(struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dai->dev, "%s : sport %d\n", __func__, dai->id);

	if (dai->capture_active)
		sport_rx_stop(sport_handle);
	if (dai->playback_active)
		sport_tx_stop(sport_handle);
	return 0;
}

static int bf5xx_i2s_resume(struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;
	int ret;

	dev_dbg(dai->dev, "%s : sport %d\n", __func__, dai->id);

	ret = sport_config_rx(sport_handle, bf5xx_i2s->rcr1,
				      bf5xx_i2s->rcr2, 0, 0);
	if (ret) {
		dev_err(dai->dev, "SPORT is busy!\n");
		return -EBUSY;
	}

	ret = sport_config_tx(sport_handle, bf5xx_i2s->tcr1,
				      bf5xx_i2s->tcr2, 0, 0);
	if (ret) {
		dev_err(dai->dev, "SPORT is busy!\n");
		return -EBUSY;
	}

	return sport_set_multichannel(sport_handle, bf5xx_i2s->slots,
			bf5xx_i2s->tx_mask, bf5xx_i2s->rx_mask, 0);
}

#else
#define bf5xx_i2s_suspend	NULL
#define bf5xx_i2s_resume	NULL
#endif

static int bf5xx_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct sport_device *sport_handle = snd_soc_dai_get_drvdata(dai);
	struct bf5xx_i2s_port *bf5xx_i2s = sport_handle->private_data;
	unsigned int i;

	for (i = 0; i < BFIN_TDM_DAI_MAX_SLOTS; i++) {
		bf5xx_i2s->tx_dma_data.map[i] = i;
		bf5xx_i2s->rx_dma_data.map[i] = i;
	}

	dai->playback_dma_data = &bf5xx_i2s->tx_dma_data;
	dai->capture_dma_data = &bf5xx_i2s->rx_dma_data;

	return 0;
}

#define BF5XX_I2S_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
		SNDRV_PCM_RATE_96000)

#define BF5XX_I2S_FORMATS \
	(SNDRV_PCM_FMTBIT_S8 | \
	 SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops bf5xx_i2s_dai_ops = {
	.shutdown	 = bf5xx_i2s_shutdown,
	.hw_params	 = bf5xx_i2s_hw_params,
	.set_fmt	 = bf5xx_i2s_set_dai_fmt,
	.set_tdm_slot	 = bf5xx_i2s_set_tdm_slot,
	.set_channel_map = bf5xx_i2s_set_channel_map,
};

static struct snd_soc_dai_driver bf5xx_i2s_dai = {
	.probe = bf5xx_i2s_dai_probe,
	.suspend = bf5xx_i2s_suspend,
	.resume = bf5xx_i2s_resume,
	.playback = {
		.channels_min = 2,
		.channels_max = 8,
		.rates = BF5XX_I2S_RATES,
		.formats = BF5XX_I2S_FORMATS,},
	.capture = {
		.channels_min = 2,
		.channels_max = 8,
		.rates = BF5XX_I2S_RATES,
		.formats = BF5XX_I2S_FORMATS,},
	.ops = &bf5xx_i2s_dai_ops,
};

static const struct snd_soc_component_driver bf5xx_i2s_component = {
	.name		= "bf5xx-i2s",
};

static int bf5xx_i2s_probe(struct platform_device *pdev)
{
	struct sport_device *sport_handle;
	int ret;

	/* configure SPORT for I2S */
	sport_handle = sport_init(pdev, 4, 8 * sizeof(u32),
		sizeof(struct bf5xx_i2s_port));
	if (!sport_handle)
		return -ENODEV;

	/* register with the ASoC layers */
	ret = snd_soc_register_component(&pdev->dev, &bf5xx_i2s_component,
					 &bf5xx_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register DAI: %d\n", ret);
		sport_done(sport_handle);
		return ret;
	}

	return 0;
}

static int bf5xx_i2s_remove(struct platform_device *pdev)
{
	struct sport_device *sport_handle = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s enter\n", __func__);

	snd_soc_unregister_component(&pdev->dev);
	sport_done(sport_handle);

	return 0;
}

static struct platform_driver bfin_i2s_driver = {
	.probe  = bf5xx_i2s_probe,
	.remove = bf5xx_i2s_remove,
	.driver = {
		.name = "bfin-i2s",
	},
};

module_platform_driver(bfin_i2s_driver);

/* Module information */
MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("I2S driver for ADI Blackfin");
MODULE_LICENSE("GPL");

