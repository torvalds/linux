/*
 * s3c2443-ac97.c  --  ALSA Soc Audio Layer
 *
 * (c) 2007 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  Copyright (C) 2005, Sean Choi <sh428.choi@samsung.com>
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <mach/hardware.h>
#include <plat/regs-ac97.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <plat/audio.h>
#include <asm/dma.h>
#include <mach/dma.h>

#include "s3c24xx-pcm.h"
#include "s3c24xx-ac97.h"

struct s3c24xx_ac97_info {
	void __iomem	*regs;
	struct clk	*ac97_clk;
};
static struct s3c24xx_ac97_info s3c24xx_ac97;

static DECLARE_COMPLETION(ac97_completion);
static u32 codec_ready;
static DECLARE_MUTEX(ac97_mutex);

static unsigned short s3c2443_ac97_read(struct snd_ac97 *ac97,
	unsigned short reg)
{
	u32 ac_glbctrl;
	u32 ac_codec_cmd;
	u32 stat, addr, data;

	down(&ac97_mutex);

	codec_ready = S3C_AC97_GLBSTAT_CODECREADY;
	ac_codec_cmd = readl(s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd = S3C_AC97_CODEC_CMD_READ | AC_CMD_ADDR(reg);
	writel(ac_codec_cmd, s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);

	udelay(50);

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= S3C_AC97_GLBCTRL_CODECREADYIE;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);

	wait_for_completion(&ac97_completion);

	stat = readl(s3c24xx_ac97.regs + S3C_AC97_STAT);
	addr = (stat >> 16) & 0x7f;
	data = (stat & 0xffff);

	if (addr != reg)
		printk(KERN_ERR "s3c24xx-ac97: req addr = %02x,"
				" rep addr = %02x\n", reg, addr);

	up(&ac97_mutex);

	return (unsigned short)data;
}

static void s3c2443_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
	unsigned short val)
{
	u32 ac_glbctrl;
	u32 ac_codec_cmd;

	down(&ac97_mutex);

	codec_ready = S3C_AC97_GLBSTAT_CODECREADY;
	ac_codec_cmd = readl(s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd = AC_CMD_ADDR(reg) | AC_CMD_DATA(val);
	writel(ac_codec_cmd, s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);

	udelay(50);

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl |= S3C_AC97_GLBCTRL_CODECREADYIE;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);

	wait_for_completion(&ac97_completion);

	ac_codec_cmd = readl(s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);
	ac_codec_cmd |= S3C_AC97_CODEC_CMD_READ;
	writel(ac_codec_cmd, s3c24xx_ac97.regs + S3C_AC97_CODEC_CMD);

	up(&ac97_mutex);

}

static void s3c2443_ac97_warm_reset(struct snd_ac97 *ac97)
{
	u32 ac_glbctrl;

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_WARMRESET;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = 0;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);
}

static void s3c2443_ac97_cold_reset(struct snd_ac97 *ac97)
{
	u32 ac_glbctrl;

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_COLDRESET;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = 0;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_ACLINKON;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl |= S3C_AC97_GLBCTRL_TRANSFERDATAENABLE;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl |= S3C_AC97_GLBCTRL_PCMOUTTM_DMA |
		S3C_AC97_GLBCTRL_PCMINTM_DMA | S3C_AC97_GLBCTRL_MICINTM_DMA;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
}

static irqreturn_t s3c2443_ac97_irq(int irq, void *dev_id)
{
	int status;
	u32 ac_glbctrl;

	status = readl(s3c24xx_ac97.regs + S3C_AC97_GLBSTAT) & codec_ready;

	if (status) {
		ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
		ac_glbctrl &= ~S3C_AC97_GLBCTRL_CODECREADYIE;
		writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
		complete(&ac97_completion);
	}
	return IRQ_HANDLED;
}

struct snd_ac97_bus_ops soc_ac97_ops = {
	.read	= s3c2443_ac97_read,
	.write	= s3c2443_ac97_write,
	.warm_reset	= s3c2443_ac97_warm_reset,
	.reset	= s3c2443_ac97_cold_reset,
};

static struct s3c2410_dma_client s3c2443_dma_client_out = {
	.name = "AC97 PCM Stereo out"
};

static struct s3c2410_dma_client s3c2443_dma_client_in = {
	.name = "AC97 PCM Stereo in"
};

static struct s3c2410_dma_client s3c2443_dma_client_micin = {
	.name = "AC97 Mic Mono in"
};

static struct s3c24xx_pcm_dma_params s3c2443_ac97_pcm_stereo_out = {
	.client		= &s3c2443_dma_client_out,
	.channel	= DMACH_PCM_OUT,
	.dma_addr	= S3C2440_PA_AC97 + S3C_AC97_PCM_DATA,
	.dma_size	= 4,
};

static struct s3c24xx_pcm_dma_params s3c2443_ac97_pcm_stereo_in = {
	.client		= &s3c2443_dma_client_in,
	.channel	= DMACH_PCM_IN,
	.dma_addr	= S3C2440_PA_AC97 + S3C_AC97_PCM_DATA,
	.dma_size	= 4,
};

static struct s3c24xx_pcm_dma_params s3c2443_ac97_mic_mono_in = {
	.client		= &s3c2443_dma_client_micin,
	.channel	= DMACH_MIC_IN,
	.dma_addr	= S3C2440_PA_AC97 + S3C_AC97_MIC_DATA,
	.dma_size	= 4,
};

static int s3c2443_ac97_probe(struct platform_device *pdev,
			      struct snd_soc_dai *dai)
{
	int ret;
	u32 ac_glbctrl;

	s3c24xx_ac97.regs = ioremap(S3C2440_PA_AC97, 0x100);
	if (s3c24xx_ac97.regs == NULL)
		return -ENXIO;

	s3c24xx_ac97.ac97_clk = clk_get(&pdev->dev, "ac97");
	if (s3c24xx_ac97.ac97_clk == NULL) {
		printk(KERN_ERR "s3c2443-ac97 failed to get ac97_clock\n");
		iounmap(s3c24xx_ac97.regs);
		return -ENODEV;
	}
	clk_enable(s3c24xx_ac97.ac97_clk);

	s3c2410_gpio_cfgpin(S3C2410_GPE0, S3C2443_GPE0_AC_nRESET);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2443_GPE1_AC_SYNC);
	s3c2410_gpio_cfgpin(S3C2410_GPE2, S3C2443_GPE2_AC_BITCLK);
	s3c2410_gpio_cfgpin(S3C2410_GPE3, S3C2443_GPE3_AC_SDI);
	s3c2410_gpio_cfgpin(S3C2410_GPE4, S3C2443_GPE4_AC_SDO);

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_COLDRESET;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = 0;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	ac_glbctrl = S3C_AC97_GLBCTRL_ACLINKON;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	msleep(1);

	ac_glbctrl |= S3C_AC97_GLBCTRL_TRANSFERDATAENABLE;
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);

	ret = request_irq(IRQ_S3C244x_AC97, s3c2443_ac97_irq,
		IRQF_DISABLED, "AC97", NULL);
	if (ret < 0) {
		printk(KERN_ERR "s3c24xx-ac97: interrupt request failed.\n");
		clk_disable(s3c24xx_ac97.ac97_clk);
		clk_put(s3c24xx_ac97.ac97_clk);
		iounmap(s3c24xx_ac97.regs);
	}
	return ret;
}

static void s3c2443_ac97_remove(struct platform_device *pdev,
				struct snd_soc_dai *dai)
{
	free_irq(IRQ_S3C244x_AC97, NULL);
	clk_disable(s3c24xx_ac97.ac97_clk);
	clk_put(s3c24xx_ac97.ac97_clk);
	iounmap(s3c24xx_ac97.regs);
}

static int s3c2443_ac97_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cpu_dai->dma_data = &s3c2443_ac97_pcm_stereo_out;
	else
		cpu_dai->dma_data = &s3c2443_ac97_pcm_stereo_in;

	return 0;
}

static int s3c2443_ac97_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	u32 ac_glbctrl;

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
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
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			ac_glbctrl &= ~S3C_AC97_GLBCTRL_PCMINTM_MASK;
		else
			ac_glbctrl &= ~S3C_AC97_GLBCTRL_PCMOUTTM_MASK;
		break;
	}
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);

	return 0;
}

static int s3c2443_ac97_hw_mic_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return -ENODEV;
	else
		cpu_dai->dma_data = &s3c2443_ac97_mic_mono_in;

	return 0;
}

static int s3c2443_ac97_mic_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	u32 ac_glbctrl;

	ac_glbctrl = readl(s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ac_glbctrl |= S3C_AC97_GLBCTRL_PCMINTM_DMA;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ac_glbctrl &= ~S3C_AC97_GLBCTRL_PCMINTM_MASK;
	}
	writel(ac_glbctrl, s3c24xx_ac97.regs + S3C_AC97_GLBCTRL);

	return 0;
}

#define s3c2443_AC97_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_ops s3c2443_ac97_dai_ops = {
	.hw_params	= s3c2443_ac97_hw_params,
	.trigger	= s3c2443_ac97_trigger,
};

static struct snd_soc_dai_ops s3c2443_ac97_mic_dai_ops = {
	.hw_params	= s3c2443_ac97_hw_mic_params,
	.trigger	= s3c2443_ac97_mic_trigger,
};

struct snd_soc_dai s3c2443_ac97_dai[] = {
{
	.name = "s3c2443-ac97",
	.id = 0,
	.ac97_control = 1,
	.probe = s3c2443_ac97_probe,
	.remove = s3c2443_ac97_remove,
	.playback = {
		.stream_name = "AC97 Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = s3c2443_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.stream_name = "AC97 Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = s3c2443_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &s3c2443_ac97_dai_ops,
},
{
	.name = "pxa2xx-ac97-mic",
	.id = 1,
	.ac97_control = 1,
	.capture = {
		.stream_name = "AC97 Mic Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = s3c2443_AC97_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = &s3c2443_ac97_mic_dai_ops,
},
};
EXPORT_SYMBOL_GPL(s3c2443_ac97_dai);
EXPORT_SYMBOL_GPL(soc_ac97_ops);

static int __init s3c2443_ac97_init(void)
{
	return snd_soc_register_dais(s3c2443_ac97_dai,
				     ARRAY_SIZE(s3c2443_ac97_dai));
}
module_init(s3c2443_ac97_init);

static void __exit s3c2443_ac97_exit(void)
{
	snd_soc_unregister_dais(s3c2443_ac97_dai,
				ARRAY_SIZE(s3c2443_ac97_dai));
}
module_exit(s3c2443_ac97_exit);


MODULE_AUTHOR("Graeme Gregory");
MODULE_DESCRIPTION("AC97 driver for the Samsung s3c2443 chip");
MODULE_LICENSE("GPL");
