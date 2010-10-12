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

static struct bf5xx_tdm_port bf5xx_tdm;
static int sport_num = CONFIG_SND_BF5XX_SPORT_NUM;

static struct sport_param sport_params[2] = {
	{
		.dma_rx_chan    = CH_SPORT0_RX,
		.dma_tx_chan    = CH_SPORT0_TX,
		.err_irq        = IRQ_SPORT0_ERROR,
		.regs           = (struct sport_register *)SPORT0_TCR1,
	},
	{
		.dma_rx_chan    = CH_SPORT1_RX,
		.dma_tx_chan    = CH_SPORT1_TX,
		.err_irq        = IRQ_SPORT1_ERROR,
		.regs           = (struct sport_register *)SPORT1_TCR1,
	}
};

/*
 * Setting the TFS pin selector for SPORT 0 based on whether the selected
 * port id F or G. If the port is F then no conflict should exist for the
 * TFS. When Port G is selected and EMAC then there is a conflict between
 * the PHY interrupt line and TFS.  Current settings prevent the conflict
 * by ignoring the TFS pin when Port G is selected. This allows both
 * codecs and EMAC using Port G concurrently.
 */
#ifdef CONFIG_BF527_SPORT0_PORTG
#define LOCAL_SPORT0_TFS (0)
#else
#define LOCAL_SPORT0_TFS (P_SPORT0_TFS)
#endif

static u16 sport_req[][7] = { {P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_RFS,
	P_SPORT0_DRPRI, P_SPORT0_RSCLK, LOCAL_SPORT0_TFS, 0},
	   {P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_RFS, P_SPORT1_DRPRI,
		   P_SPORT1_RSCLK, P_SPORT1_TFS, 0} };

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
	int ret = 0;

	bf5xx_tdm.tcr2 &= ~0x1f;
	bf5xx_tdm.rcr2 &= ~0x1f;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		bf5xx_tdm.tcr2 |= 31;
		bf5xx_tdm.rcr2 |= 31;
		sport_handle->wdsize = 4;
		break;
		/* at present, we only support 32bit transfer */
	default:
		pr_err("not supported PCM format yet\n");
		return -EINVAL;
		break;
	}

	if (!bf5xx_tdm.configured) {
		/*
		 * TX and RX are not independent,they are enabled at the
		 * same time, even if only one side is running. So, we
		 * need to configure both of them at the time when the first
		 * stream is opened.
		 *
		 * CPU DAI:slave mode.
		 */
		ret = sport_config_rx(sport_handle, bf5xx_tdm.rcr1,
			bf5xx_tdm.rcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}

		ret = sport_config_tx(sport_handle, bf5xx_tdm.tcr1,
			bf5xx_tdm.tcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}

		bf5xx_tdm.configured = 1;
	}

	return 0;
}

static void bf5xx_tdm_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	/* No active stream, SPORT is allowed to be configured again. */
	if (!dai->active)
		bf5xx_tdm.configured = 0;
}

static int bf5xx_tdm_set_channel_map(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot)
{
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
			bf5xx_tdm.tx_map[i] = slot;
			tx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}
	for (i = 0; i < rx_num; i++) {
		slot = rx_slot[i];
		if ((slot < BFIN_TDM_DAI_MAX_SLOTS) &&
				(!(rx_mapped & (1 << slot)))) {
			bf5xx_tdm.rx_map[i] = slot;
			rx_mapped |= 1 << slot;
		} else
			return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int bf5xx_tdm_suspend(struct snd_soc_dai *dai)
{
	struct sport_device *sport = dai->private_data;

	if (!dai->active)
		return 0;
	if (dai->capture.active)
		sport_rx_stop(sport);
	if (dai->playback.active)
		sport_tx_stop(sport);
	return 0;
}

static int bf5xx_tdm_resume(struct snd_soc_dai *dai)
{
	int ret;
	struct sport_device *sport = dai->private_data;

	if (!dai->active)
		return 0;

	ret = sport_set_multichannel(sport, 8, 0xFF, 1);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	ret = sport_config_rx(sport, IRFS, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	ret = sport_config_tx(sport, ITFS, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
	}

	return 0;
}

#else
#define bf5xx_tdm_suspend      NULL
#define bf5xx_tdm_resume       NULL
#endif

static struct snd_soc_dai_ops bf5xx_tdm_dai_ops = {
	.hw_params      = bf5xx_tdm_hw_params,
	.set_fmt        = bf5xx_tdm_set_dai_fmt,
	.shutdown       = bf5xx_tdm_shutdown,
	.set_channel_map   = bf5xx_tdm_set_channel_map,
};

struct snd_soc_dai bf5xx_tdm_dai = {
	.name = "bf5xx-tdm",
	.id = 0,
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
EXPORT_SYMBOL_GPL(bf5xx_tdm_dai);

static int __devinit bfin_tdm_probe(struct platform_device *pdev)
{
	int ret = 0;

	if (peripheral_request_list(&sport_req[sport_num][0], "soc-audio")) {
		pr_err("Requesting Peripherals failed\n");
		return -EFAULT;
	}

	/* request DMA for SPORT */
	sport_handle = sport_init(&sport_params[sport_num], 4, \
		8 * sizeof(u32), NULL);
	if (!sport_handle) {
		peripheral_free_list(&sport_req[sport_num][0]);
		return -ENODEV;
	}

	/* SPORT works in TDM mode */
	ret = sport_set_multichannel(sport_handle, 8, 0xFF, 1);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = sport_config_rx(sport_handle, IRFS, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = sport_config_tx(sport_handle, ITFS, 0x1F, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		ret = -EBUSY;
		goto sport_config_err;
	}

	ret = snd_soc_register_dai(&bf5xx_tdm_dai);
	if (ret) {
		pr_err("Failed to register DAI: %d\n", ret);
		goto sport_config_err;
	}

	sport_handle->private_data = &bf5xx_tdm;
	return 0;

sport_config_err:
	peripheral_free_list(&sport_req[sport_num][0]);
	return ret;
}

static int __devexit bfin_tdm_remove(struct platform_device *pdev)
{
	peripheral_free_list(&sport_req[sport_num][0]);
	snd_soc_unregister_dai(&bf5xx_tdm_dai);

	return 0;
}

static struct platform_driver bfin_tdm_driver = {
	.probe  = bfin_tdm_probe,
	.remove = __devexit_p(bfin_tdm_remove),
	.driver = {
		.name   = "bfin-tdm",
		.owner  = THIS_MODULE,
	},
};

static int __init bfin_tdm_init(void)
{
	return platform_driver_register(&bfin_tdm_driver);
}
module_init(bfin_tdm_init);

static void __exit bfin_tdm_exit(void)
{
	platform_driver_unregister(&bfin_tdm_driver);
}
module_exit(bfin_tdm_exit);

/* Module information */
MODULE_AUTHOR("Barry Song");
MODULE_DESCRIPTION("TDM driver for ADI Blackfin");
MODULE_LICENSE("GPL");

