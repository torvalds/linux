/* sound/soc/samsung/ac97.c
 *
 * ALSA SoC Audio Layer - S3C AC97 Controller driver
 * 	Evolved from s3c2443-ac97.c
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 * 	Author: Jaswinder Singh <jassi.brar@samsung.com>
 * 	Credits: Graeme Gregory, Sean Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/module.h>

#include <sound/soc.h>

#include <mach/dma.h>
#include <plat/regs-ac97.h>
#include <plat/audio.h>

#include "dma.h"

#define AC_CMD_ADDR(x) (x << 16)
#define AC_CMD_DATA(x) (x & 0xffff)

#define S3C_AC97_DAI_PCM 0
#define S3C_AC97_DAI_MIC 1

struct s3c_ac97_info {
	struct clk         *ac97_clk;
	void __iomem	   *regs;
	struct mutex       lock;
	struct completion  done;
};
static struct s3c_ac97_info s3c_ac97;

static struct s3c2410_dma_client s3c_dma_client_out = {
	.name = "AC97 PCMOut"
};

static struct s3c2410_dma_client s3c_dma_client_in = {
	.name = "AC97 PCMIn"
};

static struct s3c2410_dma_client s3c_dma_client_micin = {
	.name = "AC97 MicIn"
};

static struct s3c_dma_params s3c_ac97_pcm_out = {
	.client		= &s3c_dma_client_out,
	.dma_size	= 4,
};

static struct s3c_dma_params s3c_ac97_pcm_in = {
	.client		= &s3c_dma_client_in,
	.dma_size	= 4,
};

static struct s3c_dma_params s3c_ac97_mic_in = {
	.client		= &s3c_dma_client_micin,
	.dma_size	= 4,
};

static void s3c_ac97_activate(struct snd_ac97 *ac97)
{
	u32 ac_glbctrl, stat;

	stat = readl(s3c_ac97.regs + S3C_AC97_GLBSTAT) & 0x7;
	if (stat == S3C_AC97_GLBSTAT_MAINSTATE_ACTIVE)
		return; /* Return if already active */

	INIT_COMPLETION(s3c_ac97.done);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_ACLINKON;
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl |= S3C_AC97_GLBCTRL_TRANSFERDATAENABLE;
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= S3C_AC97_GLBCTRL_CODECREADYIE;
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	if (!wait_for_completion_timeout(&s3c_ac97.done, HZ))
		pr_err("AC97: Unable to activate!");
}

static unsigned short s3c_ac97_read(struct snd_ac97 *ac97,
	unsigned short reg)
{
	u32 ac_glbctrl, ac_codec_cmd;
	u32 stat, addr, data;

	mutex_lock(&s3c_ac97.lock);

	s3c_ac97_activate(ac97);

	INIT_COMPLETION(s3c_ac97.done);

	ac_codec_cmd = readl(s3c_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd = S3C_AC97_CODEC_CMD_READ | AC_CMD_ADDR(reg);
	writel(ac_codec_cmd, s3c_ac97.regs + S3C_AC97_CODEC_CMD);

	udelay(50);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= S3C_AC97_GLBCTRL_CODECREADYIE;
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	if (!wait_for_completion_timeout(&s3c_ac97.done, HZ))
		pr_err("AC97: Unable to read!");

	stat = readl(s3c_ac97.regs + S3C_AC97_STAT);
	addr = (stat >> 16) & 0x7f;
	data = (stat & 0xffff);

	if (addr != reg)
		pr_err("ac97: req addr = %02x, rep addr = %02x\n",
			reg, addr);

	mutex_unlock(&s3c_ac97.lock);

	return (unsigned short)data;
}

static void s3c_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
	unsigned short val)
{
	u32 ac_glbctrl, ac_codec_cmd;

	mutex_lock(&s3c_ac97.lock);

	s3c_ac97_activate(ac97);

	INIT_COMPLETION(s3c_ac97.done);

	ac_codec_cmd = readl(s3c_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd = AC_CMD_ADDR(reg) | AC_CMD_DATA(val);
	writel(ac_codec_cmd, s3c_ac97.regs + S3C_AC97_CODEC_CMD);

	udelay(50);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= S3C_AC97_GLBCTRL_CODECREADYIE;
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	if (!wait_for_completion_timeout(&s3c_ac97.done, HZ))
		pr_err("AC97: Unable to write!");

	ac_codec_cmd = readl(s3c_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd |= S3C_AC97_CODEC_CMD_READ;
	writel(ac_codec_cmd, s3c_ac97.regs + S3C_AC97_CODEC_CMD);

	mutex_unlock(&s3c_ac97.lock);
}

static void s3c_ac97_cold_reset(struct snd_ac97 *ac97)
{
	pr_debug("AC97: Cold reset\n");
	writel(S3C_AC97_GLBCTRL_COLDRESET,
			s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	writel(0, s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);
}

static void s3c_ac97_warm_reset(struct snd_ac97 *ac97)
{
	u32 stat;

	stat = readl(s3c_ac97.regs + S3C_AC97_GLBSTAT) & 0x7;
	if (stat == S3C_AC97_GLBSTAT_MAINSTATE_ACTIVE)
		return; /* Return if already active */

	pr_debug("AC97: Warm reset\n");

	writel(S3C_AC97_GLBCTRL_WARMRESET, s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	writel(0, s3c_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	s3c_ac97_activate(ac97);
}

static irqreturn_t s3c_ac97_irq(int irq, void *dev_id)
{
	u32 ac_glbctrl, ac_glbstat;

	ac_glbstat = readl(s3c_ac97.regs + S3C_AC97_GLBSTAT);

	if (ac_glbstat & S3C_AC97_GLBSTAT_CODECREADY) {

		ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
		ac_glbctrl &= ~S3C_AC97_GLBCTRL_CODECREADYIE;
		writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

		complete(&s3c_ac97.done);
	}

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= (1<<30); /* Clear interrupt */
	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	return IRQ_HANDLED;
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read       = s3c_ac97_read,
	.write      = s3c_ac97_write,
	.warm_reset = s3c_ac97_warm_reset,
	.reset      = s3c_ac97_cold_reset,
};
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int s3c_ac97_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct s3c_dma_params *dma_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &s3c_ac97_pcm_out;
	else
		dma_data = &s3c_ac97_pcm_in;

	snd_soc_dai_set_dma_data(cpu_dai, substream, dma_data);

	return 0;
}

static int s3c_ac97_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	u32 ac_glbctrl;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s3c_dma_params *dma_data =
		snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ac_glbctrl &= ~S3C_AC97_GLBCTRL_PCMINTM_MASK;
	else
		ac_glbctrl &= ~S3C_AC97_GLBCTRL_PCMOUTTM_MASK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			ac_glbctrl |= S3C_AC97_GLBCTRL_PCMINTM_DMA;
		else
			ac_glbctrl |= S3C_AC97_GLBCTRL_PCMOUTTM_DMA;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	}

	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	if (!dma_data->ops)
		dma_data->ops = samsung_dma_get_ops();

	dma_data->ops->started(dma_data->channel);

	return 0;
}

static int s3c_ac97_hw_mic_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return -ENODEV;
	else
		snd_soc_dai_set_dma_data(cpu_dai, substream, &s3c_ac97_mic_in);

	return 0;
}

static int s3c_ac97_mic_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	u32 ac_glbctrl;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s3c_dma_params *dma_data =
		snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	ac_glbctrl = readl(s3c_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl &= ~S3C_AC97_GLBCTRL_MICINTM_MASK;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ac_glbctrl |= S3C_AC97_GLBCTRL_MICINTM_DMA;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	}

	writel(ac_glbctrl, s3c_ac97.regs + S3C_AC97_GLBCTRL);

	if (!dma_data->ops)
		dma_data->ops = samsung_dma_get_ops();

	dma_data->ops->started(dma_data->channel);

	return 0;
}

static const struct snd_soc_dai_ops s3c_ac97_dai_ops = {
	.hw_params	= s3c_ac97_hw_params,
	.trigger	= s3c_ac97_trigger,
};

static const struct snd_soc_dai_ops s3c_ac97_mic_dai_ops = {
	.hw_params	= s3c_ac97_hw_mic_params,
	.trigger	= s3c_ac97_mic_trigger,
};

static struct snd_soc_dai_driver s3c_ac97_dai[] = {
	[S3C_AC97_DAI_PCM] = {
		.name =	"samsung-ac97",
		.ac97_control = 1,
		.playback = {
			.stream_name = "AC97 Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.capture = {
			.stream_name = "AC97 Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.ops = &s3c_ac97_dai_ops,
	},
	[S3C_AC97_DAI_MIC] = {
		.name = "samsung-ac97-mic",
		.ac97_control = 1,
		.capture = {
			.stream_name = "AC97 Mic Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,},
		.ops = &s3c_ac97_mic_dai_ops,
	},
};

static __devinit int s3c_ac97_probe(struct platform_device *pdev)
{
	struct resource *mem_res, *dmatx_res, *dmarx_res, *dmamic_res, *irq_res;
	struct s3c_audio_pdata *ac97_pdata;
	int ret;

	ac97_pdata = pdev->dev.platform_data;
	if (!ac97_pdata || !ac97_pdata->cfg_gpio) {
		dev_err(&pdev->dev, "cfg_gpio callback not provided!\n");
		return -EINVAL;
	}

	/* Check for availability of necessary resource */
	dmatx_res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dmatx_res) {
		dev_err(&pdev->dev, "Unable to get AC97-TX dma resource\n");
		return -ENXIO;
	}

	dmarx_res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!dmarx_res) {
		dev_err(&pdev->dev, "Unable to get AC97-RX dma resource\n");
		return -ENXIO;
	}

	dmamic_res = platform_get_resource(pdev, IORESOURCE_DMA, 2);
	if (!dmamic_res) {
		dev_err(&pdev->dev, "Unable to get AC97-MIC dma resource\n");
		return -ENXIO;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get register resource\n");
		return -ENXIO;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq_res) {
		dev_err(&pdev->dev, "AC97 IRQ not provided!\n");
		return -ENXIO;
	}

	if (!request_mem_region(mem_res->start,
				resource_size(mem_res), "ac97")) {
		dev_err(&pdev->dev, "Unable to request register region\n");
		return -EBUSY;
	}

	s3c_ac97_pcm_out.channel = dmatx_res->start;
	s3c_ac97_pcm_out.dma_addr = mem_res->start + S3C_AC97_PCM_DATA;
	s3c_ac97_pcm_in.channel = dmarx_res->start;
	s3c_ac97_pcm_in.dma_addr = mem_res->start + S3C_AC97_PCM_DATA;
	s3c_ac97_mic_in.channel = dmamic_res->start;
	s3c_ac97_mic_in.dma_addr = mem_res->start + S3C_AC97_MIC_DATA;

	init_completion(&s3c_ac97.done);
	mutex_init(&s3c_ac97.lock);

	s3c_ac97.regs = ioremap(mem_res->start, resource_size(mem_res));
	if (s3c_ac97.regs == NULL) {
		dev_err(&pdev->dev, "Unable to ioremap register region\n");
		ret = -ENXIO;
		goto err1;
	}

	s3c_ac97.ac97_clk = clk_get(&pdev->dev, "ac97");
	if (IS_ERR(s3c_ac97.ac97_clk)) {
		dev_err(&pdev->dev, "ac97 failed to get ac97_clock\n");
		ret = -ENODEV;
		goto err2;
	}
	clk_enable(s3c_ac97.ac97_clk);

	if (ac97_pdata->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to configure gpio\n");
		ret = -EINVAL;
		goto err3;
	}

	ret = request_irq(irq_res->start, s3c_ac97_irq,
					0, "AC97", NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "ac97: interrupt request failed.\n");
		goto err4;
	}

	ret = snd_soc_register_dais(&pdev->dev, s3c_ac97_dai,
			ARRAY_SIZE(s3c_ac97_dai));
	if (ret)
		goto err5;

	return 0;

err5:
	free_irq(irq_res->start, NULL);
err4:
err3:
	clk_disable(s3c_ac97.ac97_clk);
	clk_put(s3c_ac97.ac97_clk);
err2:
	iounmap(s3c_ac97.regs);
err1:
	release_mem_region(mem_res->start, resource_size(mem_res));

	return ret;
}

static __devexit int s3c_ac97_remove(struct platform_device *pdev)
{
	struct resource *mem_res, *irq_res;

	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(s3c_ac97_dai));

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_res)
		free_irq(irq_res->start, NULL);

	clk_disable(s3c_ac97.ac97_clk);
	clk_put(s3c_ac97.ac97_clk);

	iounmap(s3c_ac97.regs);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res)
		release_mem_region(mem_res->start, resource_size(mem_res));

	return 0;
}

static struct platform_driver s3c_ac97_driver = {
	.probe  = s3c_ac97_probe,
	.remove = __devexit_p(s3c_ac97_remove),
	.driver = {
		.name = "samsung-ac97",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(s3c_ac97_driver);

MODULE_AUTHOR("Jaswinder Singh, <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("AC97 driver for the Samsung SoC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:samsung-ac97");
