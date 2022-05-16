// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Audio Layer - Samsung S/PDIF Controller driver
//
// Copyright (c) 2010 Samsung Electronics Co. Ltd
//		http://www.samsung.com/

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <linux/platform_data/asoc-s3c.h>

#include "dma.h"
#include "spdif.h"

/* Registers */
#define CLKCON				0x00
#define CON				0x04
#define BSTAS				0x08
#define CSTAS				0x0C
#define DATA_OUTBUF			0x10
#define DCNT				0x14
#define BSTAS_S				0x18
#define DCNT_S				0x1C

#define CLKCTL_MASK			0x7
#define CLKCTL_MCLK_EXT			(0x1 << 2)
#define CLKCTL_PWR_ON			(0x1 << 0)

#define CON_MASK			0x3ffffff
#define CON_FIFO_TH_SHIFT		19
#define CON_FIFO_TH_MASK		(0x7 << 19)
#define CON_USERDATA_23RDBIT		(0x1 << 12)

#define CON_SW_RESET			(0x1 << 5)

#define CON_MCLKDIV_MASK		(0x3 << 3)
#define CON_MCLKDIV_256FS		(0x0 << 3)
#define CON_MCLKDIV_384FS		(0x1 << 3)
#define CON_MCLKDIV_512FS		(0x2 << 3)

#define CON_PCM_MASK			(0x3 << 1)
#define CON_PCM_16BIT			(0x0 << 1)
#define CON_PCM_20BIT			(0x1 << 1)
#define CON_PCM_24BIT			(0x2 << 1)

#define CON_PCM_DATA			(0x1 << 0)

#define CSTAS_MASK			0x3fffffff
#define CSTAS_SAMP_FREQ_MASK		(0xF << 24)
#define CSTAS_SAMP_FREQ_44		(0x0 << 24)
#define CSTAS_SAMP_FREQ_48		(0x2 << 24)
#define CSTAS_SAMP_FREQ_32		(0x3 << 24)
#define CSTAS_SAMP_FREQ_96		(0xA << 24)

#define CSTAS_CATEGORY_MASK		(0xFF << 8)
#define CSTAS_CATEGORY_CODE_CDP		(0x01 << 8)

#define CSTAS_NO_COPYRIGHT		(0x1 << 2)

/**
 * struct samsung_spdif_info - Samsung S/PDIF Controller information
 * @lock: Spin lock for S/PDIF.
 * @dev: The parent device passed to use from the probe.
 * @regs: The pointer to the device register block.
 * @clk_rate: Current clock rate for calcurate ratio.
 * @pclk: The peri-clock pointer for spdif master operation.
 * @sclk: The source clock pointer for making sync signals.
 * @saved_clkcon: Backup clkcon reg. in suspend.
 * @saved_con: Backup con reg. in suspend.
 * @saved_cstas: Backup cstas reg. in suspend.
 * @dma_playback: DMA information for playback channel.
 */
struct samsung_spdif_info {
	spinlock_t	lock;
	struct device	*dev;
	void __iomem	*regs;
	unsigned long	clk_rate;
	struct clk	*pclk;
	struct clk	*sclk;
	u32		saved_clkcon;
	u32		saved_con;
	u32		saved_cstas;
	struct snd_dmaengine_dai_dma_data *dma_playback;
};

static struct snd_dmaengine_dai_dma_data spdif_stereo_out;
static struct samsung_spdif_info spdif_info;

static inline struct samsung_spdif_info
*component_to_info(struct snd_soc_component *component)
{
	return snd_soc_component_get_drvdata(component);
}

static inline struct samsung_spdif_info *to_info(struct snd_soc_dai *cpu_dai)
{
	return snd_soc_dai_get_drvdata(cpu_dai);
}

static void spdif_snd_txctrl(struct samsung_spdif_info *spdif, int on)
{
	void __iomem *regs = spdif->regs;
	u32 clkcon;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	clkcon = readl(regs + CLKCON) & CLKCTL_MASK;
	if (on)
		writel(clkcon | CLKCTL_PWR_ON, regs + CLKCON);
	else
		writel(clkcon & ~CLKCTL_PWR_ON, regs + CLKCON);
}

static int spdif_set_sysclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct samsung_spdif_info *spdif = to_info(cpu_dai);
	u32 clkcon;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	clkcon = readl(spdif->regs + CLKCON);

	if (clk_id == SND_SOC_SPDIF_INT_MCLK)
		clkcon &= ~CLKCTL_MCLK_EXT;
	else
		clkcon |= CLKCTL_MCLK_EXT;

	writel(clkcon, spdif->regs + CLKCON);

	spdif->clk_rate = freq;

	return 0;
}

static int spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct samsung_spdif_info *spdif = to_info(asoc_rtd_to_cpu(rtd, 0));
	unsigned long flags;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 1);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		spin_lock_irqsave(&spdif->lock, flags);
		spdif_snd_txctrl(spdif, 0);
		spin_unlock_irqrestore(&spdif->lock, flags);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int spdif_sysclk_ratios[] = {
	512, 384, 256,
};

static int spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct samsung_spdif_info *spdif = to_info(asoc_rtd_to_cpu(rtd, 0));
	void __iomem *regs = spdif->regs;
	struct snd_dmaengine_dai_dma_data *dma_data;
	u32 con, clkcon, cstas;
	unsigned long flags;
	int i, ratio;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = spdif->dma_playback;
	else {
		dev_err(spdif->dev, "Capture is not supported\n");
		return -EINVAL;
	}

	snd_soc_dai_set_dma_data(asoc_rtd_to_cpu(rtd, 0), substream, dma_data);

	spin_lock_irqsave(&spdif->lock, flags);

	con = readl(regs + CON) & CON_MASK;
	cstas = readl(regs + CSTAS) & CSTAS_MASK;
	clkcon = readl(regs + CLKCON) & CLKCTL_MASK;

	con &= ~CON_FIFO_TH_MASK;
	con |= (0x7 << CON_FIFO_TH_SHIFT);
	con |= CON_USERDATA_23RDBIT;
	con |= CON_PCM_DATA;

	con &= ~CON_PCM_MASK;
	switch (params_width(params)) {
	case 16:
		con |= CON_PCM_16BIT;
		break;
	default:
		dev_err(spdif->dev, "Unsupported data size.\n");
		goto err;
	}

	ratio = spdif->clk_rate / params_rate(params);
	for (i = 0; i < ARRAY_SIZE(spdif_sysclk_ratios); i++)
		if (ratio == spdif_sysclk_ratios[i])
			break;
	if (i == ARRAY_SIZE(spdif_sysclk_ratios)) {
		dev_err(spdif->dev, "Invalid clock ratio %ld/%d\n",
				spdif->clk_rate, params_rate(params));
		goto err;
	}

	con &= ~CON_MCLKDIV_MASK;
	switch (ratio) {
	case 256:
		con |= CON_MCLKDIV_256FS;
		break;
	case 384:
		con |= CON_MCLKDIV_384FS;
		break;
	case 512:
		con |= CON_MCLKDIV_512FS;
		break;
	}

	cstas &= ~CSTAS_SAMP_FREQ_MASK;
	switch (params_rate(params)) {
	case 44100:
		cstas |= CSTAS_SAMP_FREQ_44;
		break;
	case 48000:
		cstas |= CSTAS_SAMP_FREQ_48;
		break;
	case 32000:
		cstas |= CSTAS_SAMP_FREQ_32;
		break;
	case 96000:
		cstas |= CSTAS_SAMP_FREQ_96;
		break;
	default:
		dev_err(spdif->dev, "Invalid sampling rate %d\n",
				params_rate(params));
		goto err;
	}

	cstas &= ~CSTAS_CATEGORY_MASK;
	cstas |= CSTAS_CATEGORY_CODE_CDP;
	cstas |= CSTAS_NO_COPYRIGHT;

	writel(con, regs + CON);
	writel(cstas, regs + CSTAS);
	writel(clkcon, regs + CLKCON);

	spin_unlock_irqrestore(&spdif->lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&spdif->lock, flags);
	return -EINVAL;
}

static void spdif_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct samsung_spdif_info *spdif = to_info(asoc_rtd_to_cpu(rtd, 0));
	void __iomem *regs = spdif->regs;
	u32 con, clkcon;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	con = readl(regs + CON) & CON_MASK;
	clkcon = readl(regs + CLKCON) & CLKCTL_MASK;

	writel(con | CON_SW_RESET, regs + CON);
	cpu_relax();

	writel(clkcon & ~CLKCTL_PWR_ON, regs + CLKCON);
}

#ifdef CONFIG_PM
static int spdif_suspend(struct snd_soc_component *component)
{
	struct samsung_spdif_info *spdif = component_to_info(component);
	u32 con = spdif->saved_con;

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	spdif->saved_clkcon = readl(spdif->regs	+ CLKCON) & CLKCTL_MASK;
	spdif->saved_con = readl(spdif->regs + CON) & CON_MASK;
	spdif->saved_cstas = readl(spdif->regs + CSTAS) & CSTAS_MASK;

	writel(con | CON_SW_RESET, spdif->regs + CON);
	cpu_relax();

	return 0;
}

static int spdif_resume(struct snd_soc_component *component)
{
	struct samsung_spdif_info *spdif = component_to_info(component);

	dev_dbg(spdif->dev, "Entered %s\n", __func__);

	writel(spdif->saved_clkcon, spdif->regs	+ CLKCON);
	writel(spdif->saved_con, spdif->regs + CON);
	writel(spdif->saved_cstas, spdif->regs + CSTAS);

	return 0;
}
#else
#define spdif_suspend NULL
#define spdif_resume NULL
#endif

static const struct snd_soc_dai_ops spdif_dai_ops = {
	.set_sysclk	= spdif_set_sysclk,
	.trigger	= spdif_trigger,
	.hw_params	= spdif_hw_params,
	.shutdown	= spdif_shutdown,
};

static struct snd_soc_dai_driver samsung_spdif_dai = {
	.name = "samsung-spdif",
	.playback = {
		.stream_name = "S/PDIF Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000),
		.formats = SNDRV_PCM_FMTBIT_S16_LE, },
	.ops = &spdif_dai_ops,
};

static const struct snd_soc_component_driver samsung_spdif_component = {
	.name		= "samsung-spdif",
	.suspend	= spdif_suspend,
	.resume		= spdif_resume,
};

static int spdif_probe(struct platform_device *pdev)
{
	struct s3c_audio_pdata *spdif_pdata;
	struct resource *mem_res;
	struct samsung_spdif_info *spdif;
	dma_filter_fn filter;
	int ret;

	spdif_pdata = pdev->dev.platform_data;

	dev_dbg(&pdev->dev, "Entered %s\n", __func__);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get register resource.\n");
		return -ENXIO;
	}

	if (spdif_pdata && spdif_pdata->cfg_gpio
			&& spdif_pdata->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to configure GPIO pins\n");
		return -EINVAL;
	}

	spdif = &spdif_info;
	spdif->dev = &pdev->dev;

	spin_lock_init(&spdif->lock);

	spdif->pclk = devm_clk_get(&pdev->dev, "spdif");
	if (IS_ERR(spdif->pclk)) {
		dev_err(&pdev->dev, "failed to get peri-clock\n");
		ret = -ENOENT;
		goto err0;
	}
	ret = clk_prepare_enable(spdif->pclk);
	if (ret)
		goto err0;

	spdif->sclk = devm_clk_get(&pdev->dev, "sclk_spdif");
	if (IS_ERR(spdif->sclk)) {
		dev_err(&pdev->dev, "failed to get internal source clock\n");
		ret = -ENOENT;
		goto err1;
	}
	ret = clk_prepare_enable(spdif->sclk);
	if (ret)
		goto err1;

	/* Request S/PDIF Register's memory region */
	if (!request_mem_region(mem_res->start,
				resource_size(mem_res), "samsung-spdif")) {
		dev_err(&pdev->dev, "Unable to request register region\n");
		ret = -EBUSY;
		goto err2;
	}

	spdif->regs = ioremap(mem_res->start, 0x100);
	if (spdif->regs == NULL) {
		dev_err(&pdev->dev, "Cannot ioremap registers\n");
		ret = -ENXIO;
		goto err3;
	}

	spdif_stereo_out.addr_width = 2;
	spdif_stereo_out.addr = mem_res->start + DATA_OUTBUF;
	filter = NULL;
	if (spdif_pdata) {
		spdif_stereo_out.filter_data = spdif_pdata->dma_playback;
		filter = spdif_pdata->dma_filter;
	}
	spdif->dma_playback = &spdif_stereo_out;

	ret = samsung_asoc_dma_platform_register(&pdev->dev, filter,
						 NULL, NULL, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register DMA: %d\n", ret);
		goto err4;
	}

	dev_set_drvdata(&pdev->dev, spdif);

	ret = devm_snd_soc_register_component(&pdev->dev,
			&samsung_spdif_component, &samsung_spdif_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "fail to register dai\n");
		goto err4;
	}

	return 0;
err4:
	iounmap(spdif->regs);
err3:
	release_mem_region(mem_res->start, resource_size(mem_res));
err2:
	clk_disable_unprepare(spdif->sclk);
err1:
	clk_disable_unprepare(spdif->pclk);
err0:
	return ret;
}

static int spdif_remove(struct platform_device *pdev)
{
	struct samsung_spdif_info *spdif = &spdif_info;
	struct resource *mem_res;

	iounmap(spdif->regs);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res)
		release_mem_region(mem_res->start, resource_size(mem_res));

	clk_disable_unprepare(spdif->sclk);
	clk_disable_unprepare(spdif->pclk);

	return 0;
}

static struct platform_driver samsung_spdif_driver = {
	.probe	= spdif_probe,
	.remove	= spdif_remove,
	.driver	= {
		.name	= "samsung-spdif",
	},
};

module_platform_driver(samsung_spdif_driver);

MODULE_AUTHOR("Seungwhan Youn, <sw.youn@samsung.com>");
MODULE_DESCRIPTION("Samsung S/PDIF Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:samsung-spdif");
