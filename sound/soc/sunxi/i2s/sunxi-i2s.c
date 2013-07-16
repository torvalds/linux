/*
 * sound\soc\sunxi\i2s\sunxi-i2s.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <mach/clock.h>
#include <plat/system.h>
#include <plat/sys_config.h>

#include <mach/hardware.h>
#include <asm/dma.h>
#include <plat/dma_compat.h>

#include "sunxi-i2sdma.h"
#include "sunxi-i2s.h"

static int regsave[8];
static int i2s_used = 0;

static struct sunxi_dma_params sunxi_i2s_pcm_stereo_out = {
	.client.name	=	"I2S PCM Stereo out",
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	.channel	=	DMACH_NIIS,
#endif
	.dma_addr 	=	SUNXI_IISBASE + SUNXI_IISTXFIFO,
};

static struct sunxi_dma_params sunxi_i2s_pcm_stereo_in = {
	.client.name	=	"I2S PCM Stereo in",
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	.channel	=	DMACH_NIIS,
#endif
	.dma_addr 	=	SUNXI_IISBASE + SUNXI_IISRXFIFO,
};


 struct sunxi_i2s_info sunxi_iis;
static u32 i2s_handle = 0;
 static struct clk *i2s_apbclk, *i2s_pll2clk, *i2s_pllx8, *i2s_moduleclk;

void sunxi_snd_txctrl_i2s(struct snd_pcm_substream *substream, int on)
{
	u32 reg_val;

	reg_val = readl(sunxi_iis.regs + SUNXI_TXCHSEL);
	reg_val &= ~0x7;
	reg_val |= SUNXI_TXCHSEL_CHNUM(substream->runtime->channels);
	writel(reg_val, sunxi_iis.regs + SUNXI_TXCHSEL);

	reg_val = readl(sunxi_iis.regs + SUNXI_TXCHMAP);
	reg_val = 0;
	if (sunxi_is_sun4i()) {
		if(substream->runtime->channels == 1) {
			reg_val = 0x76543200;
		} else {
			reg_val = 0x76543210;
		}
	} else {
		if(substream->runtime->channels == 1) {
			reg_val = 0x00000000;
		} else {
			reg_val = 0x00000010;
		}
	}
	writel(reg_val, sunxi_iis.regs + SUNXI_TXCHMAP);

	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	if (sunxi_is_sun4i()) {
		reg_val &= ~SUNXI_IISCTL_SDO3EN;
		reg_val &= ~SUNXI_IISCTL_SDO2EN;
		reg_val &= ~SUNXI_IISCTL_SDO1EN;
		reg_val &= ~SUNXI_IISCTL_SDO0EN;
		switch(substream->runtime->channels) {
			case 1:
			case 2:
				reg_val |= SUNXI_IISCTL_SDO0EN;
				break;
			case 3:
			case 4:
				reg_val |= SUNXI_IISCTL_SDO0EN;
				reg_val |= SUNXI_IISCTL_SDO1EN;
				break;
			case 5:
			case 6:
				reg_val |= SUNXI_IISCTL_SDO0EN;
				reg_val |= SUNXI_IISCTL_SDO1EN;
				reg_val |= SUNXI_IISCTL_SDO2EN;
				break;
			case 7:
			case 8:
				reg_val |= SUNXI_IISCTL_SDO0EN;
				reg_val |= SUNXI_IISCTL_SDO1EN;
				reg_val |= SUNXI_IISCTL_SDO2EN;
				reg_val |= SUNXI_IISCTL_SDO3EN;
				break;
			default:
				reg_val |= SUNXI_IISCTL_SDO0EN;
		}
	} else {
		reg_val |= SUNXI_IISCTL_SDO0EN;
	}
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	//flush TX FIFO
	reg_val = readl(sunxi_iis.regs + SUNXI_IISFCTL);
	reg_val |= SUNXI_IISFCTL_FTX;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISFCTL);

	//clear TX counter
	writel(0, sunxi_iis.regs + SUNXI_IISTXCNT);

	if (on) {
		/* IIS TX ENABLE */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val |= SUNXI_IISCTL_TXEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

		/* enable DMA DRQ mode for play */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISINT);
		reg_val |= SUNXI_IISINT_TXDRQEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISINT);

		//Global Enable Digital Audio Interface
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val |= SUNXI_IISCTL_GEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	} else {
		/* IIS TX DISABLE */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val &= ~SUNXI_IISCTL_TXEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

		/* DISBALE dma DRQ mode */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISINT);
		reg_val &= ~SUNXI_IISINT_TXDRQEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISINT);

		//Global disable Digital Audio Interface
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val &= ~SUNXI_IISCTL_GEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);
	}
}

void sunxi_snd_rxctrl_i2s(int on)
{
	u32 reg_val;

	//flush RX FIFO
	reg_val = readl(sunxi_iis.regs + SUNXI_IISFCTL);
	reg_val |= SUNXI_IISFCTL_FRX;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISFCTL);

	//clear RX counter
	writel(0, sunxi_iis.regs + SUNXI_IISRXCNT);

	if (on) {
		/* IIS RX ENABLE */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val |= SUNXI_IISCTL_RXEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

		/* enable DMA DRQ mode for record */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISINT);
		reg_val |= SUNXI_IISINT_RXDRQEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISINT);

		//Global Enable Digital Audio Interface
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val |= SUNXI_IISCTL_GEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	} else {
		/* IIS RX DISABLE */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val &= ~SUNXI_IISCTL_RXEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

		/* DISBALE dma DRQ mode */
		reg_val = readl(sunxi_iis.regs + SUNXI_IISINT);
		reg_val &= ~SUNXI_IISINT_RXDRQEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISINT);

		//Global disable Digital Audio Interface
		reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
		reg_val &= ~SUNXI_IISCTL_GEN;
		writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);
	}
}

static inline int sunxi_snd_is_clkmaster(void)
{
	return ((readl(sunxi_iis.regs + SUNXI_IISCTL) & SUNXI_IISCTL_MS) ? 0 : 1);
}

static int sunxi_i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 reg_val;
	u32 reg_val1;

	//SDO ON
	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	if (sunxi_is_sun4i()) {
		reg_val |= (SUNXI_IISCTL_SDO0EN | SUNXI_IISCTL_SDO1EN |
			    SUNXI_IISCTL_SDO2EN | SUNXI_IISCTL_SDO3EN);
	} else {
		reg_val |= SUNXI_IISCTL_SDO0EN;
	}
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	/* master or slave selection */
	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	switch(fmt & SND_SOC_DAIFMT_MASTER_MASK){
		case SND_SOC_DAIFMT_CBM_CFM:   /* codec clk & frm master */
			reg_val |= SUNXI_IISCTL_MS;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:   /* codec clk & frm slave */
			reg_val &= ~SUNXI_IISCTL_MS;
			break;
		default:
			return -EINVAL;
	}
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	/* pcm or i2s mode selection */
	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	reg_val1 = readl(sunxi_iis.regs + SUNXI_IISFAT0);
	reg_val1 &= ~SUNXI_IISFAT0_FMT_RVD;
	switch(fmt & SND_SOC_DAIFMT_FORMAT_MASK){
		case SND_SOC_DAIFMT_I2S:        /* I2S mode */
			reg_val &= ~SUNXI_IISCTL_PCM;
			reg_val1 |= SUNXI_IISFAT0_FMT_I2S;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:    /* Right Justified mode */
			reg_val &= ~SUNXI_IISCTL_PCM;
			reg_val1 |= SUNXI_IISFAT0_FMT_RGT;
			break;
		case SND_SOC_DAIFMT_LEFT_J:     /* Left Justified mode */
			reg_val &= ~SUNXI_IISCTL_PCM;
			reg_val1 |= SUNXI_IISFAT0_FMT_LFT;
			break;
		case SND_SOC_DAIFMT_DSP_A:      /* L data msb after FRM LRC */
			reg_val |= SUNXI_IISCTL_PCM;
			reg_val1 &= ~SUNXI_IISFAT0_LRCP;
			break;
		case SND_SOC_DAIFMT_DSP_B:      /* L data msb during FRM LRC */
			reg_val |= SUNXI_IISCTL_PCM;
			reg_val1 |= SUNXI_IISFAT0_LRCP;
			break;
		default:
			return -EINVAL;
	}
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);
	writel(reg_val1, sunxi_iis.regs + SUNXI_IISFAT0);

	/* DAI signal inversions */
	reg_val1 = readl(sunxi_iis.regs + SUNXI_IISFAT0);
	switch(fmt & SND_SOC_DAIFMT_INV_MASK){
		case SND_SOC_DAIFMT_NB_NF:     /* normal bit clock + frame */
			reg_val1 &= ~SUNXI_IISFAT0_LRCP;
			reg_val1 &= ~SUNXI_IISFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_NB_IF:     /* normal bclk + inv frm */
			reg_val1 |= SUNXI_IISFAT0_LRCP;
			reg_val1 &= ~SUNXI_IISFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_IB_NF:     /* invert bclk + nor frm */
			reg_val1 &= ~SUNXI_IISFAT0_LRCP;
			reg_val1 |= SUNXI_IISFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_IB_IF:     /* invert bclk + frm */
			reg_val1 |= SUNXI_IISFAT0_LRCP;
			reg_val1 |= SUNXI_IISFAT0_BCP;
			break;
	}
	writel(reg_val1, sunxi_iis.regs + SUNXI_IISFAT0);

	/* word select size */
	reg_val = readl(sunxi_iis.regs + SUNXI_IISFAT0);
	reg_val &= ~SUNXI_IISFAT0_WSS_32BCLK;
	if(sunxi_iis.ws_size == 16)
		reg_val |= SUNXI_IISFAT0_WSS_16BCLK;
	else if(sunxi_iis.ws_size == 20)
		reg_val |= SUNXI_IISFAT0_WSS_20BCLK;
	else if(sunxi_iis.ws_size == 24)
		reg_val |= SUNXI_IISFAT0_WSS_24BCLK;
	else
		reg_val |= SUNXI_IISFAT0_WSS_32BCLK;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISFAT0);

	/* PCM REGISTER setup */
	reg_val = sunxi_iis.pcm_txtype&0x3;
	reg_val |= sunxi_iis.pcm_rxtype<<2;

	if(!sunxi_iis.pcm_sync_type)
		reg_val |= SUNXI_IISFAT1_SSYNC;							//short sync
	if(sunxi_iis.pcm_sw == 16)
		reg_val |= SUNXI_IISFAT1_SW;

	reg_val |=((sunxi_iis.pcm_start_slot - 1)&0x3)<<6;		//start slot index

	reg_val |= sunxi_iis.pcm_lsb_first<<9;			//MSB or LSB first

	if(sunxi_iis.pcm_sync_period == 256)
		reg_val |= 0x4<<12;
	else if (sunxi_iis.pcm_sync_period == 128)
		reg_val |= 0x3<<12;
	else if (sunxi_iis.pcm_sync_period == 64)
		reg_val |= 0x2<<12;
	else if (sunxi_iis.pcm_sync_period == 32)
		reg_val |= 0x1<<12;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISFAT1);

	/* set FIFO control register */
	reg_val = 0 & 0x3;
	reg_val |= (1 & 0x1)<<2;
	reg_val |= SUNXI_IISFCTL_RXTL(0xf);				//RX FIFO trigger level
	reg_val |= SUNXI_IISFCTL_TXTL(0x40);				//TX FIFO empty trigger level
	writel(reg_val, sunxi_iis.regs + SUNXI_IISFCTL);
	return 0;
}

static int sunxi_i2s_hw_params(struct snd_pcm_substream *substream,
																struct snd_pcm_hw_params *params,
																struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sunxi_dma_params *dma_data;

	/* play or record */
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &sunxi_i2s_pcm_stereo_out;
	else
		dma_data = &sunxi_i2s_pcm_stereo_in;

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_data);
	return 0;
}

static int sunxi_i2s_trigger(struct snd_pcm_substream *substream,
                              int cmd, struct snd_soc_dai *dai)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sunxi_dma_params *dma_data =
					snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sunxi_snd_rxctrl_i2s(1);
			} else {
				sunxi_snd_txctrl_i2s(substream, 1);
			}
			sunxi_dma_started(dma_data);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sunxi_snd_rxctrl_i2s(0);
			} else {
			  sunxi_snd_txctrl_i2s(substream, 0);
			}
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

//freq:   1: 22.5792MHz   0: 24.576MHz
static int sunxi_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
                                 unsigned int freq, int dir)
{
	if (!freq) {
		clk_set_rate(i2s_pll2clk, 24576000);
	} else {
		clk_set_rate(i2s_pll2clk, 22579200);
	}

	return 0;
}

static int sunxi_i2s_set_clkdiv(struct snd_soc_dai *cpu_dai, int div_id, int div)
{
	u32 reg;
	switch (div_id) {
		case SUNXI_DIV_MCLK:
			if(div <= 8)
				div  = (div >>1);
			else if(div  == 12)
				div  = 0x5;
			else if(div  == 16)
				div  = 0x6;
			else if(div == 24)
				div = 0x7;
			else if(div == 32)
				div = 0x8;
			else if(div == 48)
				div = 0x9;
			else if(div == 64)
				div = 0xa;
			reg = (readl(sunxi_iis.regs + SUNXI_IISCLKD) & ~SUNXI_IISCLKD_MCLK_MASK) | (div << SUNXI_IISCLKD_MCLK_OFFS);
			writel(reg, sunxi_iis.regs + SUNXI_IISCLKD);
			break;
		case SUNXI_DIV_BCLK:
			if(div <= 8)
				div = (div>>1) - 1;
			else if(div == 12)
				div = 0x4;
			else if(div == 16)
				div = 0x5;
			else if(div == 32)
				div = 0x6;
			else if(div == 64)
				div = 0x7;
			reg = (readl(sunxi_iis.regs + SUNXI_IISCLKD) & ~SUNXI_IISCLKD_BCLK_MASK) | (div <<SUNXI_IISCLKD_BCLK_OFFS);
			writel(reg, sunxi_iis.regs + SUNXI_IISCLKD);
			break;
		default:
			return -EINVAL;
	}

	//diable MCLK output when high samplerate
	reg = readl(sunxi_iis.regs + SUNXI_IISCLKD);
	if (!(reg & 0xF)) {
		reg &= ~SUNXI_IISCLKD_MCLKOEN;
		writel(reg, sunxi_iis.regs + SUNXI_IISCLKD);
	} else {
		reg |= SUNXI_IISCLKD_MCLKOEN;
		writel(reg, sunxi_iis.regs + SUNXI_IISCLKD);
	}

	return 0;
}

static int sunxi_i2s_dai_probe(struct snd_soc_dai *dai)
{
	return 0;
}
static int sunxi_i2s_dai_remove(struct snd_soc_dai *dai)
{
	return 0;
}

static void iisregsave(void)
{
	regsave[0] = readl(sunxi_iis.regs + SUNXI_IISCTL);
	regsave[1] = readl(sunxi_iis.regs + SUNXI_IISFAT0);
	regsave[2] = readl(sunxi_iis.regs + SUNXI_IISFAT1);
	regsave[3] = readl(sunxi_iis.regs + SUNXI_IISFCTL) | (0x3<<24);
	regsave[4] = readl(sunxi_iis.regs + SUNXI_IISINT);
	regsave[5] = readl(sunxi_iis.regs + SUNXI_IISCLKD);
	regsave[6] = readl(sunxi_iis.regs + SUNXI_TXCHSEL);
	regsave[7] = readl(sunxi_iis.regs + SUNXI_TXCHMAP);
}

static void iisregrestore(void)
{
	writel(regsave[0], sunxi_iis.regs + SUNXI_IISCTL);
	writel(regsave[1], sunxi_iis.regs + SUNXI_IISFAT0);
	writel(regsave[2], sunxi_iis.regs + SUNXI_IISFAT1);
	writel(regsave[3], sunxi_iis.regs + SUNXI_IISFCTL);
	writel(regsave[4], sunxi_iis.regs + SUNXI_IISINT);
	writel(regsave[5], sunxi_iis.regs + SUNXI_IISCLKD);
	writel(regsave[6], sunxi_iis.regs + SUNXI_TXCHSEL);
	writel(regsave[7], sunxi_iis.regs + SUNXI_TXCHMAP);
}

static int sunxi_i2s_suspend(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
	printk("[IIS]Entered %s\n", __func__);

	//Global Enable Digital Audio Interface
	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	reg_val &= ~SUNXI_IISCTL_GEN;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	iisregsave();

	//release the module clock
	clk_disable(i2s_moduleclk);

	clk_disable(i2s_apbclk);

	//printk("[IIS]PLL2 0x01c20008 = %#x\n", *(volatile int*)0xF1C20008);
	printk("[IIS]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[IIS]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n", *(volatile int*)0xF1C200B8, __LINE__);

	return 0;
}
static int sunxi_i2s_resume(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
	printk("[IIS]Entered %s\n", __func__);

	//release the module clock
	clk_enable(i2s_apbclk);

	//release the module clock
	clk_enable(i2s_moduleclk);

	iisregrestore();

	//Global Enable Digital Audio Interface
	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	reg_val |= SUNXI_IISCTL_GEN;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	//printk("[IIS]PLL2 0x01c20008 = %#x\n", *(volatile int*)0xF1C20008);
	printk("[IIS]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[IIS]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n", *(volatile int*)0xF1C200B8, __LINE__);

	return 0;
}

#define SUNXI_I2S_RATES (SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT)
static struct snd_soc_dai_ops sunxi_iis_dai_ops = {
	.trigger 	= sunxi_i2s_trigger,
	.hw_params 	= sunxi_i2s_hw_params,
	.set_fmt 	= sunxi_i2s_set_fmt,
	.set_clkdiv = sunxi_i2s_set_clkdiv,
	.set_sysclk = sunxi_i2s_set_sysclk,
};

static struct snd_soc_dai_driver sunxi_iis_dai = {
	.probe 		= sunxi_i2s_dai_probe,
	.suspend 	= sunxi_i2s_suspend,
	.resume 	= sunxi_i2s_resume,
	.remove 	= sunxi_i2s_dai_remove,
	.playback 	= {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUNXI_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture 	= {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUNXI_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.symmetric_rates = 1,
	.ops 		= &sunxi_iis_dai_ops,
};

static int __devinit sunxi_i2s_dev_probe(struct platform_device *pdev)
{
	int reg_val = 0;
	int ret;

	sunxi_iis.regs = ioremap(SUNXI_IISBASE, 0x100);
	if (sunxi_iis.regs == NULL)
		return -ENXIO;

	//i2s apbclk
	i2s_apbclk = clk_get(NULL, "apb_i2s");
	if(-1 == clk_enable(i2s_apbclk)){
		printk("i2s_apbclk failed! line = %d\n", __LINE__);
		goto out;
	}

	i2s_pllx8 = clk_get(NULL, "audio_pllx8");

	//i2s pll2clk
	i2s_pll2clk = clk_get(NULL, "audio_pll");

	//i2s module clk
	i2s_moduleclk = clk_get(NULL, "i2s");

	if(clk_set_parent(i2s_moduleclk, i2s_pll2clk)){
		printk("try to set parent of i2s_moduleclk to i2s_pll2ck failed! line = %d\n",__LINE__);
		goto out1;
	}

	if(clk_set_rate(i2s_moduleclk, 24576000/8)){
		printk("set i2s_moduleclk clock freq to 24576000 failed! line = %d\n", __LINE__);
		goto out1;
	}

	if(-1 == clk_enable(i2s_moduleclk)){
		printk("open i2s_moduleclk failed! line = %d\n", __LINE__);
		goto out1;
	}

	reg_val = readl(sunxi_iis.regs + SUNXI_IISCTL);
	reg_val |= SUNXI_IISCTL_GEN;
	writel(reg_val, sunxi_iis.regs + SUNXI_IISCTL);

	iounmap(sunxi_iis.ioregs);
	ret = snd_soc_register_dai(&pdev->dev, &sunxi_iis_dai);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register DAI\n");
		goto out2;
	}

	goto out;
	out2:
		clk_disable(i2s_moduleclk);
	out1:
		clk_disable(i2s_apbclk);
	out:
	return 0;
}

static int __devexit sunxi_i2s_dev_remove(struct platform_device *pdev)
{
	if(i2s_used) {
		i2s_used = 0;
		//release the module clock
		clk_disable(i2s_moduleclk);

		//release pllx8clk
		clk_put(i2s_pllx8);

		//release pll2clk
		clk_put(i2s_pll2clk);

		//release apbclk
		clk_put(i2s_apbclk);

		gpio_release(i2s_handle, 2);
		snd_soc_unregister_dai(&pdev->dev);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

/*data relating*/
static struct platform_device sunxi_i2s_device = {
	.name = "sunxi-i2s",
};

/*method relating*/
static struct platform_driver sunxi_i2s_driver = {
	.probe = sunxi_i2s_dev_probe,
	.remove = __devexit_p(sunxi_i2s_dev_remove),
	.driver = {
		.name = "sunxi-i2s",
		.owner = THIS_MODULE,
	},
};

static int __init sunxi_i2s_init(void)
{
	int err = 0;
	int ret;

	ret = script_parser_fetch("i2s_para","i2s_used", &i2s_used, sizeof(int));
	if (ret) {
        printk("[I2S]sunxi_i2s_init fetch i2s using configuration failed\n");
    }

 	if (i2s_used) {
		i2s_handle = gpio_request_ex("i2s_para", NULL);

		if((err = platform_device_register(&sunxi_i2s_device)) < 0)
			return err;

		if ((err = platform_driver_register(&sunxi_i2s_driver)) < 0)
			return err;
	} else {
        printk("[I2S]sunxi-i2s cannot find any using configuration for controllers, return directly!\n");
        return 0;
    }
	return 0;
}
module_init(sunxi_i2s_init);

static void __exit sunxi_i2s_exit(void)
{
	platform_driver_unregister(&sunxi_i2s_driver);
}
module_exit(sunxi_i2s_exit);

/* Module information */
MODULE_AUTHOR("ALLWINNER");
MODULE_DESCRIPTION("sunxi I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-i2s");

