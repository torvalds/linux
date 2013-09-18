/*
 * ALSA SoC SPDIF Out Audio Layer for spear processors
 *
 * Copyright (C) 2012 ST Microelectronics
 * Vipin Kumar <vipin.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/spear_dma.h>
#include <sound/spear_spdif.h>
#include "spdif_out_regs.h"

struct spdif_out_params {
	u32 rate;
	u32 core_freq;
	u32 mute;
};

struct spdif_out_dev {
	struct clk *clk;
	struct spear_dma_data dma_params;
	struct spdif_out_params saved_params;
	u32 running;
	void __iomem *io_base;
};

static void spdif_out_configure(struct spdif_out_dev *host)
{
	writel(SPDIF_OUT_RESET, host->io_base + SPDIF_OUT_SOFT_RST);
	mdelay(1);
	writel(readl(host->io_base + SPDIF_OUT_SOFT_RST) & ~SPDIF_OUT_RESET,
			host->io_base + SPDIF_OUT_SOFT_RST);

	writel(SPDIF_OUT_FDMA_TRIG_16 | SPDIF_OUT_MEMFMT_16_16 |
			SPDIF_OUT_VALID_HW | SPDIF_OUT_USER_HW |
			SPDIF_OUT_CHNLSTA_HW | SPDIF_OUT_PARITY_HW,
			host->io_base + SPDIF_OUT_CFG);

	writel(0x7F, host->io_base + SPDIF_OUT_INT_STA_CLR);
	writel(0x7F, host->io_base + SPDIF_OUT_INT_EN_CLR);
}

static int spdif_out_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	ret = clk_enable(host->clk);
	if (ret)
		return ret;

	host->running = true;
	spdif_out_configure(host);

	return 0;
}

static void spdif_out_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(dai);

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return;

	clk_disable(host->clk);
	host->running = false;
}

static void spdif_out_clock(struct spdif_out_dev *host, u32 core_freq,
		u32 rate)
{
	u32 divider, ctrl;

	clk_set_rate(host->clk, core_freq);
	divider = DIV_ROUND_CLOSEST(clk_get_rate(host->clk), (rate * 128));

	ctrl = readl(host->io_base + SPDIF_OUT_CTRL);
	ctrl &= ~SPDIF_DIVIDER_MASK;
	ctrl |= (divider << SPDIF_DIVIDER_SHIFT) & SPDIF_DIVIDER_MASK;
	writel(ctrl, host->io_base + SPDIF_OUT_CTRL);
}

static int spdif_out_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(dai);
	u32 rate, core_freq;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	rate = params_rate(params);

	switch (rate) {
	case 8000:
	case 16000:
	case 32000:
	case 64000:
		/*
		 * The clock is multiplied by 10 to bring it to feasible range
		 * of frequencies for sscg
		 */
		core_freq = 64000 * 128 * 10;	/* 81.92 MHz */
		break;
	case 5512:
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		core_freq = 176400 * 128;	/* 22.5792 MHz */
		break;
	case 48000:
	case 96000:
	case 192000:
	default:
		core_freq = 192000 * 128;	/* 24.576 MHz */
		break;
	}

	spdif_out_clock(host, core_freq, rate);
	host->saved_params.core_freq = core_freq;
	host->saved_params.rate = rate;

	return 0;
}

static int spdif_out_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(dai);
	u32 ctrl;
	int ret = 0;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			ctrl = readl(host->io_base + SPDIF_OUT_CTRL);
			ctrl &= ~SPDIF_OPMODE_MASK;
			if (!host->saved_params.mute)
				ctrl |= SPDIF_OPMODE_AUD_DATA |
					SPDIF_STATE_NORMAL;
			else
				ctrl |= SPDIF_OPMODE_MUTE_PCM;
			writel(ctrl, host->io_base + SPDIF_OUT_CTRL);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ctrl = readl(host->io_base + SPDIF_OUT_CTRL);
		ctrl &= ~SPDIF_OPMODE_MASK;
		ctrl |= SPDIF_OPMODE_OFF;
		writel(ctrl, host->io_base + SPDIF_OUT_CTRL);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int spdif_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(dai);
	u32 val;

	host->saved_params.mute = mute;
	val = readl(host->io_base + SPDIF_OUT_CTRL);
	val &= ~SPDIF_OPMODE_MASK;

	if (mute)
		val |= SPDIF_OPMODE_MUTE_PCM;
	else {
		if (host->running)
			val |= SPDIF_OPMODE_AUD_DATA | SPDIF_STATE_NORMAL;
		else
			val |= SPDIF_OPMODE_OFF;
	}

	writel(val, host->io_base + SPDIF_OUT_CTRL);
	return 0;
}

static int spdif_mute_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct snd_soc_pcm_runtime *rtd = card->rtd;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.integer.value[0] = host->saved_params.mute;
	return 0;
}

static int spdif_mute_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct snd_soc_pcm_runtime *rtd = card->rtd;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(cpu_dai);

	if (host->saved_params.mute == ucontrol->value.integer.value[0])
		return 0;

	spdif_digital_mute(cpu_dai, ucontrol->value.integer.value[0]);

	return 1;
}
static const struct snd_kcontrol_new spdif_out_controls[] = {
	SOC_SINGLE_BOOL_EXT("IEC958 Playback Switch", 0,
			spdif_mute_get, spdif_mute_put),
};

static int spdif_soc_dai_probe(struct snd_soc_dai *dai)
{
	struct spdif_out_dev *host = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &host->dma_params;

	return snd_soc_add_dai_controls(dai, spdif_out_controls,
				ARRAY_SIZE(spdif_out_controls));
}

static const struct snd_soc_dai_ops spdif_out_dai_ops = {
	.digital_mute	= spdif_digital_mute,
	.startup	= spdif_out_startup,
	.shutdown	= spdif_out_shutdown,
	.trigger	= spdif_out_trigger,
	.hw_params	= spdif_out_hw_params,
};

static struct snd_soc_dai_driver spdif_out_dai = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
				 SNDRV_PCM_RATE_192000),
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.probe = spdif_soc_dai_probe,
	.ops = &spdif_out_dai_ops,
};

static const struct snd_soc_component_driver spdif_out_component = {
	.name		= "spdif-out",
};

static int spdif_out_probe(struct platform_device *pdev)
{
	struct spdif_out_dev *host;
	struct spear_spdif_platform_data *pdata;
	struct resource *res;
	int ret;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		dev_warn(&pdev->dev, "kzalloc fail\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->io_base))
		return PTR_ERR(host->io_base);

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	pdata = dev_get_platdata(&pdev->dev);

	host->dma_params.data = pdata->dma_params;
	host->dma_params.addr = res->start + SPDIF_OUT_FIFO_DATA;
	host->dma_params.max_burst = 16;
	host->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->dma_params.filter = pdata->filter;

	dev_set_drvdata(&pdev->dev, host);

	ret = snd_soc_register_component(&pdev->dev, &spdif_out_component,
					 &spdif_out_dai, 1);
	return ret;
}

static int spdif_out_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int spdif_out_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spdif_out_dev *host = dev_get_drvdata(&pdev->dev);

	if (host->running)
		clk_disable(host->clk);

	return 0;
}

static int spdif_out_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spdif_out_dev *host = dev_get_drvdata(&pdev->dev);

	if (host->running) {
		clk_enable(host->clk);
		spdif_out_configure(host);
		spdif_out_clock(host, host->saved_params.core_freq,
				host->saved_params.rate);
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(spdif_out_dev_pm_ops, spdif_out_suspend, \
		spdif_out_resume);

#define SPDIF_OUT_DEV_PM_OPS (&spdif_out_dev_pm_ops)

#else
#define SPDIF_OUT_DEV_PM_OPS NULL

#endif

static struct platform_driver spdif_out_driver = {
	.probe		= spdif_out_probe,
	.remove		= spdif_out_remove,
	.driver		= {
		.name	= "spdif-out",
		.owner	= THIS_MODULE,
		.pm	= SPDIF_OUT_DEV_PM_OPS,
	},
};

module_platform_driver(spdif_out_driver);

MODULE_AUTHOR("Vipin Kumar <vipin.kumar@st.com>");
MODULE_DESCRIPTION("SPEAr SPDIF OUT SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spdif_out");
