/*
 * sound\soc\sun4i\spdif\sun4i_spdif.c
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
#include <plat/sys_config.h>

#include <mach/hardware.h>
#include <asm/dma.h>
#include <plat/dma.h>

#include "sun4i_spdma.h"
#include "sun4i_spdif.h"

static int regsave[6];
static int spdif_used = 0;

static struct sw_dma_client sun4i_dma_client_out = {
	.name = "SPDIF out"
};

static struct sw_dma_client sun4i_dma_client_in = {
	.name = "SPDIF in"
};

static struct sun4i_dma_params sun4i_spdif_stereo_out = {
	.client		=	&sun4i_dma_client_out,
	.channel	=	DMACH_NSPDIF,
	.dma_addr 	=	SUN4I_SPDIFBASE + SUN4I_SPDIF_TXFIFO,
	.dma_size 	=   4,               /* dma transfer 32bits */
};

static struct sun4i_dma_params sun4i_spdif_stereo_in = {
	.client		=	&sun4i_dma_client_in,
	.channel	=	DMACH_NSPDIF,
	.dma_addr 	=	SUN4I_SPDIFBASE + SUN4I_SPDIF_RXFIFO,
	.dma_size 	=   4,               /* dma transfer 32bits */
};

struct sun4i_spdif_info sun4i_spdif;
static u32 spdif_handle = 0;
static struct clk *spdif_apbclk, *spdif_pll2clk, *spdif_pllx8, *spdif_moduleclk;

void sun4i_snd_txctrl(struct snd_pcm_substream *substream, int on)
{
	u32 reg_val;
	
	if (substream->runtime->channels == 1) {
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		reg_val |= SUN4I_SPDIF_TXCFG_SINGLEMOD;
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
	}
	
	//soft reset SPDIF
	writel(0x1, sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	
	//MCLK OUTPUT enable
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	reg_val |= SUN4I_SPDIF_CTL_MCLKOUTEN;
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	
	//flush TX FIFO
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_FCTL);
	reg_val |= SUN4I_SPDIF_FCTL_FTX;	
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_FCTL);
	
	//clear interrupt status
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_ISTA);
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_ISTA);
	
	//clear TX counter
	writel(0, sun4i_spdif.regs + SUN4I_SPDIF_TXCNT);

	if (on) {
		//SPDIF TX ENBALE
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		reg_val |= SUN4I_SPDIF_TXCFG_TXEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		
		//DRQ ENABLE
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_INT);
		reg_val |= SUN4I_SPDIF_INT_TXDRQEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_INT);
		
		//global enable
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
		reg_val |= SUN4I_SPDIF_CTL_GEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);		
	} else {
		//SPDIF TX DISABALE
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		reg_val &= ~SUN4I_SPDIF_TXCFG_TXEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		
		//DRQ DISABLE
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_INT);
		reg_val &= ~SUN4I_SPDIF_INT_TXDRQEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_INT);
		
		//global disable
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
		reg_val &= ~SUN4I_SPDIF_CTL_GEN;	
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	}
}

void sun4i_snd_rxctrl(int on)
{
}

static inline int sun4i_snd_is_clkmaster(void)
{
	return 0;
}

static int sun4i_spdif_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 reg_val;
		
	reg_val = 0;
	reg_val &= ~SUN4I_SPDIF_TXCFG_SINGLEMOD;
	reg_val |= SUN4I_SPDIF_TXCFG_ASS;
	reg_val &= ~SUN4I_SPDIF_TXCFG_NONAUDIO;
	reg_val |= SUN4I_SPDIF_TXCFG_FMT16BIT;
	reg_val |= SUN4I_SPDIF_TXCFG_CHSTMODE;
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
	
	reg_val = 0;
	reg_val &= ~SUN4I_SPDIF_FCTL_FIFOSRC;
	reg_val |= SUN4I_SPDIF_FCTL_TXTL(16);
	reg_val |= SUN4I_SPDIF_FCTL_RXTL(15);
	reg_val |= SUN4I_SPDIF_FCTL_TXIM(1);
	reg_val |= SUN4I_SPDIF_FCTL_RXOM(3);
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_FCTL);
	
	if (!fmt) {//PCM
		reg_val = 0;
		reg_val |= (SUN4I_SPDIF_TXCHSTA0_CHNUM(2));
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
		
		reg_val = 0;
		reg_val |= (SUN4I_SPDIF_TXCHSTA1_SAMWORDLEN(1));
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
	} else {  //non PCM	
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		reg_val |= SUN4I_SPDIF_TXCFG_NONAUDIO;
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
		
		reg_val = 0;
		reg_val |= (SUN4I_SPDIF_TXCHSTA0_CHNUM(2));
		reg_val |= SUN4I_SPDIF_TXCHSTA0_AUDIO;
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
		
		reg_val = 0;
		reg_val |= (SUN4I_SPDIF_TXCHSTA1_SAMWORDLEN(1));
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
	}
	
	return 0;
}

static int sun4i_spdif_hw_params(struct snd_pcm_substream *substream,
																struct snd_pcm_hw_params *params,
																struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_dma_params *dma_data;
	
	/* play or record */
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &sun4i_spdif_stereo_out;
	else
		dma_data = &sun4i_spdif_stereo_in;
		
	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_data);
	
	return 0;
}						

static int sun4i_spdif_trigger(struct snd_pcm_substream *substream,
                              int cmd, struct snd_soc_dai *dai)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_dma_params *dma_data = 
					snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sun4i_snd_rxctrl(1);
			} else {
				sun4i_snd_txctrl(substream, 1);
			}
			sw_dma_ctrl(dma_data->channel, SW_DMAOP_STARTED);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sun4i_snd_rxctrl(0);
			} else {
			  sun4i_snd_txctrl(substream, 0);
			}
			break;
		default:
			ret = -EINVAL;
			break;
	}

		return ret;
}					

//freq:   1: 22.5792MHz   0: 24.576MHz  
static int sun4i_spdif_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id, 
                                 unsigned int freq, int dir)
{
	if (!freq) {
		clk_set_rate(spdif_pll2clk, 24576000);
	} else {
		clk_set_rate(spdif_pll2clk, 22579200);
	}

	return 0;
}		

static int sun4i_spdif_set_clkdiv(struct snd_soc_dai *cpu_dai, int div_id, int div)
{
	u32 reg_val = 0;

	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
	reg_val &= ~(SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0xf));
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
		
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
	reg_val &= ~(SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0xf));
  	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);	
	
	switch(div_id) {
		case SUN4I_DIV_MCLK:
		{
			reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
			reg_val &= ~SUN4I_SPDIF_TXCFG_TXRATIO(0x1F);	
			reg_val |= SUN4I_SPDIF_TXCFG_TXRATIO(div-1);
			writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
			
			if(clk_get_rate(spdif_pll2clk) == 24576000)
			{
				switch(div)
				{
					//24KHZ
					case 8:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x6));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0x9));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
						
					//32KHZ
					case 6:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x3));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0xC));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
						
					//48KHZ
					case 4:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x2));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
				
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0xD));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);				
						break;
						
					//96KHZ
					case 2:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0xA));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0x5));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
						
					//192KHZ
					case 1:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0xE));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
					
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);	
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0x1));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
						
					default:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(1));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
				
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
				}
			}else{  //22.5792MHz		
				switch(div)
				{
					//22.05khz
					case 8:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x4));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
				
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0xb));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);				
						break;
						
					//44.1KHZ
					case 4:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x0));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
				
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0xF));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);				
						break;
						
					//88.2khz
					case 2:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0x8));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
					
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0x7));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
			
					//176.4KHZ
					case 1:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(0xC));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
					
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0x3));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
						
					default:
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
						reg_val |= (SUN4I_SPDIF_TXCHSTA0_SAMFREQ(1));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
				
						reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						reg_val |= (SUN4I_SPDIF_TXCHSTA1_ORISAMFREQ(0));
						writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
						break;
				}
			}			
		}
		break;
		case SUN4I_DIV_BCLK:
		break;
			
		default:
			return -EINVAL;
	}

	return 0;
}

u32 sun4i_spdif_get_clockrate(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(sun4i_spdif_get_clockrate);

static int sun4i_spdif_dai_probe(struct snd_soc_dai *dai)
{			
	return 0;
}
static int sun4i_spdif_dai_remove(struct snd_soc_dai *dai)
{
	return 0;
}

static void spdifregsave(void)
{
	regsave[0] = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	regsave[1] = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
	regsave[2] = readl(sun4i_spdif.regs + SUN4I_SPDIF_FCTL) | (0x3<<16);
	regsave[3] = readl(sun4i_spdif.regs + SUN4I_SPDIF_INT);
	regsave[4] = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
	regsave[5] = readl(sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
}

static void spdifregrestore(void)
{
	writel(regsave[0], sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	writel(regsave[1], sun4i_spdif.regs + SUN4I_SPDIF_TXCFG);
	writel(regsave[2], sun4i_spdif.regs + SUN4I_SPDIF_FCTL);
	writel(regsave[3], sun4i_spdif.regs + SUN4I_SPDIF_INT);
	writel(regsave[4], sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA0);
	writel(regsave[5], sun4i_spdif.regs + SUN4I_SPDIF_TXCHSTA1);
}

//#ifdef CONFIG_PM
static int sun4i_spdif_suspend(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
	printk("[SPDIF]Enter %s\n", __func__);
	
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	reg_val &= ~SUN4I_SPDIF_CTL_GEN;
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);

	spdifregsave();
	
	//disable the module clock
	clk_disable(spdif_moduleclk);
	
	clk_disable(spdif_apbclk);	

	printk("[SPDIF]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[SPDIF]SPECIAL CLK 0x01c200C0 = %#x, line= %d\n", *(volatile int*)0xF1C200C0, __LINE__);
	
	return 0;
}

static int sun4i_spdif_resume(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
	printk("[SPDIF]Enter %s\n", __func__);

	//disable the module clock
	clk_enable(spdif_apbclk);		

	//enable the module clock
	clk_enable(spdif_moduleclk);
	
	spdifregrestore();
	
	reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	reg_val |= SUN4I_SPDIF_CTL_GEN;
	writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);
	
	//printk("[SPDIF]PLL2 0x01c20008 = %#x\n", *(volatile int*)0xF1C20008);
	printk("[SPDIF]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[SPDIF]SPECIAL CLK 0x01c200C0 = %#x, line = %d\n", *(volatile int*)0xF1C200C0, __LINE__);
	
	return 0;
}

#define SUN4I_SPDIF_RATES (SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT)
static struct snd_soc_dai_ops sun4i_spdif_dai_ops = {
	.trigger 		= sun4i_spdif_trigger,
	.hw_params 	= sun4i_spdif_hw_params,
	.set_fmt 		= sun4i_spdif_set_fmt,
	.set_clkdiv = sun4i_spdif_set_clkdiv,
	.set_sysclk = sun4i_spdif_set_sysclk, 
};
static struct snd_soc_dai_driver sun4i_spdif_dai = {
	.probe 		= sun4i_spdif_dai_probe,
	.suspend 	= sun4i_spdif_suspend,
	.resume 	= sun4i_spdif_resume,
	.remove 	= sun4i_spdif_dai_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUN4I_SPDIF_RATES,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUN4I_SPDIF_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.symmetric_rates = 1,
	.ops = &sun4i_spdif_dai_ops,
};		

static int __devinit sun4i_spdif_dev_probe(struct platform_device *pdev)
{
	int reg_val = 0;
	int ret = 0;
	
	sun4i_spdif.regs = ioremap(SUN4I_SPDIFBASE, 0x100);
	if(sun4i_spdif.regs == NULL)
		return -ENXIO;
	
		//spdif apbclk
		spdif_apbclk = clk_get(NULL, "apb_spdif");
		if(-1 == clk_enable(spdif_apbclk)){
			printk("spdif_apbclk failed! line = %d\n", __LINE__);
		}

		spdif_pllx8 = clk_get(NULL, "audio_pllx8");

		//spdif pll2clk
		spdif_pll2clk = clk_get(NULL, "audio_pll");

		//spdif module clk
		spdif_moduleclk = clk_get(NULL, "spdif");

		if(clk_set_parent(spdif_moduleclk, spdif_pll2clk)){
			printk("try to set parent of spdif_moduleclk to spdif_pll2ck failed! line = %d\n",__LINE__);
		}
		
		if(clk_set_rate(spdif_moduleclk, 24576000/8)){
			printk("set spdif_moduleclk clock freq to 24576000 failed! line = %d\n", __LINE__);
		}
		
		if(-1 == clk_enable(spdif_moduleclk)){
			printk("open spdif_moduleclk failed! line = %d\n", __LINE__);
		}
	
		//global enbale
		reg_val = readl(sun4i_spdif.regs + SUN4I_SPDIF_CTL);
		reg_val |= SUN4I_SPDIF_CTL_GEN;
		writel(reg_val, sun4i_spdif.regs + SUN4I_SPDIF_CTL);
		
		ret = snd_soc_register_dai(&pdev->dev, &sun4i_spdif_dai);
			
		iounmap(sun4i_spdif.ioregs);
			
	return 0;
}

static int __devexit sun4i_spdif_dev_remove(struct platform_device *pdev)
{
	if (spdif_used) {
		spdif_used = 0;
		//release the module clock
		clk_disable(spdif_moduleclk);
	
		//release pllx8clk
		clk_put(spdif_pllx8);
		
		//release pll2clk
		clk_put(spdif_pll2clk);
	
		//release apbclk
		clk_put(spdif_apbclk);
		
		gpio_release(spdif_handle, 2);
		snd_soc_unregister_dai(&pdev->dev);
		platform_set_drvdata(pdev, NULL);
	}
	
	return 0;
}

static struct platform_device sun4i_spdif_device = {
	.name = "sun4i-spdif",
};

static struct platform_driver sun4i_spdif_driver = {
	.probe = sun4i_spdif_dev_probe,
	.remove = __devexit_p(sun4i_spdif_dev_remove),
	.driver = {
		.name = "sun4i-spdif",
		.owner = THIS_MODULE,
	},	
};

static int __init sun4i_spdif_init(void)
{
	int err = 0;
	int ret;
	
	ret = script_parser_fetch("spdif_para","spdif_used", &spdif_used, sizeof(int));
	if (ret) {
        printk("[SPDIF]sun4i_spdif_init fetch spdif using configuration failed\n");
    } 
    
 	if (spdif_used) {	
		spdif_handle = gpio_request_ex("spdif_para", NULL);
		
		if((platform_device_register(&sun4i_spdif_device))<0)
			return err;

		if ((err = platform_driver_register(&sun4i_spdif_driver)) < 0)
			return err;
			
	} else {
        printk("[SPDIF]sun4i-spdif cannot find any using configuration for controllers, return directly!\n");
        return 0;
    }
 
	return 0;
}
module_init(sun4i_spdif_init);

static void __exit sun4i_spdif_exit(void)
{	
	platform_driver_unregister(&sun4i_spdif_driver);
}
module_exit(sun4i_spdif_exit);

/* Module information */
MODULE_AUTHOR("ALLWINNER");
MODULE_DESCRIPTION("sun4i SPDIF SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun4i-spdif");
