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
#include "bf5xx-i2s.h"

struct bf5xx_i2s_port {
	u16 tcr1;
	u16 rcr1;
	u16 tcr2;
	u16 rcr2;
	int counter;
	int configured;
};

static struct bf5xx_i2s_port bf5xx_i2s;
static int sport_num = CONFIG_SND_BF5XX_SPORT_NUM;

static struct sport_param sport_params[2] = {
	{
		.dma_rx_chan	= CH_SPORT0_RX,
		.dma_tx_chan	= CH_SPORT0_TX,
		.err_irq	= IRQ_SPORT0_ERROR,
		.regs		= (struct sport_register *)SPORT0_TCR1,
	},
	{
		.dma_rx_chan	= CH_SPORT1_RX,
		.dma_tx_chan	= CH_SPORT1_TX,
		.err_irq	= IRQ_SPORT1_ERROR,
		.regs		= (struct sport_register *)SPORT1_TCR1,
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

static int bf5xx_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	int ret = 0;

	/* interface format:support I2S,slave mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		bf5xx_i2s.tcr1 |= TFSR | TCKFE;
		bf5xx_i2s.rcr1 |= RFSR | RCKFE;
		bf5xx_i2s.tcr2 |= TSFSE;
		bf5xx_i2s.rcr2 |= RSFSE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		bf5xx_i2s.tcr1 |= TFSR;
		bf5xx_i2s.rcr1 |= RFSR;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		ret = -EINVAL;
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

static int bf5xx_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	pr_debug("%s enter\n", __func__);

	/*this counter is used for counting how many pcm streams are opened*/
	bf5xx_i2s.counter++;
	return 0;
}

static int bf5xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	bf5xx_i2s.tcr2 &= ~0x1f;
	bf5xx_i2s.rcr2 &= ~0x1f;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		bf5xx_i2s.tcr2 |= 15;
		bf5xx_i2s.rcr2 |= 15;
		sport_handle->wdsize = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bf5xx_i2s.tcr2 |= 23;
		bf5xx_i2s.rcr2 |= 23;
		sport_handle->wdsize = 3;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		bf5xx_i2s.tcr2 |= 31;
		bf5xx_i2s.rcr2 |= 31;
		sport_handle->wdsize = 4;
		break;
	}

	if (!bf5xx_i2s.configured) {
		/*
		 * TX and RX are not independent,they are enabled at the
		 * same time, even if only one side is running. So, we
		 * need to configure both of them at the time when the first
		 * stream is opened.
		 *
		 * CPU DAI:slave mode.
		 */
		bf5xx_i2s.configured = 1;
		ret = sport_config_rx(sport_handle, bf5xx_i2s.rcr1,
				      bf5xx_i2s.rcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}

		ret = sport_config_tx(sport_handle, bf5xx_i2s.tcr1,
				      bf5xx_i2s.tcr2, 0, 0);
		if (ret) {
			pr_err("SPORT is busy!\n");
			return -EBUSY;
		}
	}

	return 0;
}

static void bf5xx_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	pr_debug("%s enter\n", __func__);
	bf5xx_i2s.counter--;
	/* No active stream, SPORT is allowed to be configured again. */
	if (!bf5xx_i2s.counter)
		bf5xx_i2s.configured = 0;
}

static int bf5xx_i2s_probe(struct platform_device *pdev,
			   struct snd_soc_dai *dai)
{
	pr_debug("%s enter\n", __func__);
	if (peripheral_request_list(&sport_req[sport_num][0], "soc-audio")) {
		pr_err("Requesting Peripherals failed\n");
		return -EFAULT;
	}

	/* request DMA for SPORT */
	sport_handle = sport_init(&sport_params[sport_num], 4, \
			2 * sizeof(u32), NULL);
	if (!sport_handle) {
		peripheral_free_list(&sport_req[sport_num][0]);
		return -ENODEV;
	}

	return 0;
}

static void bf5xx_i2s_remove(struct platform_device *pdev,
			struct snd_soc_dai *dai)
{
	pr_debug("%s enter\n", __func__);
	peripheral_free_list(&sport_req[sport_num][0]);
}

#ifdef CONFIG_PM
static int bf5xx_i2s_suspend(struct snd_soc_dai *dai)
{

	pr_debug("%s : sport %d\n", __func__, dai->id);

	if (dai->capture.active)
		sport_rx_stop(sport_handle);
	if (dai->playback.active)
		sport_tx_stop(sport_handle);
	return 0;
}

static int bf5xx_i2s_resume(struct snd_soc_dai *dai)
{
	int ret;

	pr_debug("%s : sport %d\n", __func__, dai->id);

	ret = sport_config_rx(sport_handle, bf5xx_i2s.rcr1,
				      bf5xx_i2s.rcr2, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		return -EBUSY;
	}

	ret = sport_config_tx(sport_handle, bf5xx_i2s.tcr1,
				      bf5xx_i2s.tcr2, 0, 0);
	if (ret) {
		pr_err("SPORT is busy!\n");
		return -EBUSY;
	}

	return 0;
}

#else
#define bf5xx_i2s_suspend	NULL
#define bf5xx_i2s_resume	NULL
#endif

#define BF5XX_I2S_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
		SNDRV_PCM_RATE_96000)

#define BF5XX_I2S_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |\
	SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops bf5xx_i2s_dai_ops = {
	.startup	= bf5xx_i2s_startup,
	.shutdown	= bf5xx_i2s_shutdown,
	.hw_params	= bf5xx_i2s_hw_params,
	.set_fmt	= bf5xx_i2s_set_dai_fmt,
};

struct snd_soc_dai bf5xx_i2s_dai = {
	.name = "bf5xx-i2s",
	.id = 0,
	.probe = bf5xx_i2s_probe,
	.remove = bf5xx_i2s_remove,
	.suspend = bf5xx_i2s_suspend,
	.resume = bf5xx_i2s_resume,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = BF5XX_I2S_RATES,
		.formats = BF5XX_I2S_FORMATS,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = BF5XX_I2S_RATES,
		.formats = BF5XX_I2S_FORMATS,},
	.ops = &bf5xx_i2s_dai_ops,
};
EXPORT_SYMBOL_GPL(bf5xx_i2s_dai);

static int __init bfin_i2s_init(void)
{
	return snd_soc_register_dai(&bf5xx_i2s_dai);
}
module_init(bfin_i2s_init);

static void __exit bfin_i2s_exit(void)
{
	snd_soc_unregister_dai(&bf5xx_i2s_dai);
}
module_exit(bfin_i2s_exit);

/* Module information */
MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("I2S driver for ADI Blackfin");
MODULE_LICENSE("GPL");

