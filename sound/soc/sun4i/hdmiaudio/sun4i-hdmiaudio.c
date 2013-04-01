/*
 * sound\soc\sun4i\hdmiaudio\sun4i-hdmiaudio.c
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

#include "sun4i-hdmipcm.h"
#include "sun4i-hdmiaudio.h"

//save the register value
static int regsave[8];

static struct sw_dma_client sun4i_dma_client_out = {
	.name = "HDMIAUDIO PCM Stereo out"
};

static struct sw_dma_client sun4i_dma_client_in = {
	.name = "HDMIAUDIO PCM Stereo in"
};

static struct sun4i_dma_params sun4i_hdmiaudio_pcm_stereo_out = {
	.client		=	&sun4i_dma_client_out,
	.channel	=	DMACH_HDMIAUDIO,
	.dma_addr 	=	0,
	.dma_size 	=   4,               /* dma transfer 32bits */
};

static struct sun4i_dma_params sun4i_hdmiaudio_pcm_stereo_in = {
	.client		=	&sun4i_dma_client_in,
	.channel	=	DMACH_HDMIAUDIO,
	.dma_addr 	=	SUN4I_HDMIAUDIOBASE + SUN4I_HDMIAUDIORXFIFO,
	.dma_size 	=   4,               /* dma transfer 32bits */
};

struct sun4i_hdmiaudio_info sun4i_hdmiaudio;

//clock handle
static struct clk *hdmiaudio_apbclk;
static struct clk *hdmiaudio_pll2clk;
static struct clk *hdmiaudio_pllx8;
static struct clk *hdmiaudio_moduleclk;

void sun4i_snd_txctrl_hdmiaudio(struct snd_pcm_substream *substream, int on)
{
	u32 reg_val;

	hdmi_para.channel_num = substream->runtime->channels;
	g_hdmi_func.hdmi_set_audio_para(&hdmi_para);

	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_TXCHSEL);
	reg_val &= ~0x7;
	reg_val |= SUN4I_TXCHSEL_CHNUM(substream->runtime->channels);
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_TXCHSEL);

	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_TXCHMAP);
	reg_val = 0;
	if(substream->runtime->channels == 1) {
		reg_val = 0x76543200;
	} else {
		reg_val = 0x76543210;
	}
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_TXCHMAP);

	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val &= ~SUN4I_HDMIAUDIOCTL_SDO3EN;
	reg_val &= ~SUN4I_HDMIAUDIOCTL_SDO2EN;
	reg_val &= ~SUN4I_HDMIAUDIOCTL_SDO1EN;
	reg_val &= ~SUN4I_HDMIAUDIOCTL_SDO0EN;
	switch(substream->runtime->channels) {
		case 1:
		case 2:
			reg_val |= SUN4I_HDMIAUDIOCTL_SDO0EN; break;
		case 3:
		case 4:
			reg_val |= SUN4I_HDMIAUDIOCTL_SDO0EN | SUN4I_HDMIAUDIOCTL_SDO1EN; break;
		case 5:
		case 6:
			reg_val |= SUN4I_HDMIAUDIOCTL_SDO0EN | SUN4I_HDMIAUDIOCTL_SDO1EN | SUN4I_HDMIAUDIOCTL_SDO2EN; break;
		case 7:
		case 8:
			reg_val |= SUN4I_HDMIAUDIOCTL_SDO0EN | SUN4I_HDMIAUDIOCTL_SDO1EN | SUN4I_HDMIAUDIOCTL_SDO2EN | SUN4I_HDMIAUDIOCTL_SDO3EN; break;
		default:
			reg_val |= SUN4I_HDMIAUDIOCTL_SDO0EN; break;
	}
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	//flush TX FIFO
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);
	reg_val |= SUN4I_HDMIAUDIOFCTL_FTX;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);

	//clear TX counter
	writel(0, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOTXCNT);

	if (on) {
		/* hdmiaudio TX ENABLE */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val |= SUN4I_HDMIAUDIOCTL_TXEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

		/* enable DMA DRQ mode for play */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
		reg_val |= SUN4I_HDMIAUDIOINT_TXDRQEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);

		//Global Enable Digital Audio Interface
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val |= SUN4I_HDMIAUDIOCTL_GEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	}else{
		/* HDMIAUDIO TX DISABLE */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val &= ~SUN4I_HDMIAUDIOCTL_TXEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

		/* DISBALE dma DRQ mode */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
		reg_val &= ~SUN4I_HDMIAUDIOINT_TXDRQEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);

		//Global disable Digital Audio Interface
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val &= ~SUN4I_HDMIAUDIOCTL_GEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	}
}

void sun4i_snd_rxctrl_hdmiaudio(struct snd_pcm_substream *substream, int on)
{
	u32 reg_val;

	//flush RX FIFO
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);
	reg_val |= SUN4I_HDMIAUDIOFCTL_FRX;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);

	//clear RX counter
	writel(0, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIORXCNT);

	if (on) {
		/* HDMIAUDIO RX ENABLE */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val |= SUN4I_HDMIAUDIOCTL_RXEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

		/* enable DMA DRQ mode for record */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
		reg_val |= SUN4I_HDMIAUDIOINT_RXDRQEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);

		//Global Enable Digital Audio Interface
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val |= SUN4I_HDMIAUDIOCTL_GEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	} else {
		/* HDMIAUDIO RX DISABLE */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val &= ~SUN4I_HDMIAUDIOCTL_RXEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

		/* DISBALE dma DRQ mode */
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
		reg_val &= ~SUN4I_HDMIAUDIOINT_RXDRQEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);

		//Global disable Digital Audio Interface
		reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
		reg_val &= ~SUN4I_HDMIAUDIOCTL_GEN;
		writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	}
}

static inline int sun4i_snd_is_clkmaster(void)
{
	return ((readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL) & SUN4I_HDMIAUDIOCTL_MS) ? 0 : 1);
}

static int sun4i_hdmiaudio_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 reg_val;
	u32 reg_val1;

	//SDO ON
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val |= (SUN4I_HDMIAUDIOCTL_SDO0EN | SUN4I_HDMIAUDIOCTL_SDO1EN | SUN4I_HDMIAUDIOCTL_SDO2EN | SUN4I_HDMIAUDIOCTL_SDO3EN);
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	/* master or slave selection */
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	switch(fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBM_CFM:   /* codec clk & frm master */
			reg_val |= SUN4I_HDMIAUDIOCTL_MS;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:   /* codec clk & frm slave */
			reg_val &= ~SUN4I_HDMIAUDIOCTL_MS;
			break;
		default:
			return -EINVAL;
	}
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	/* pcm or hdmiaudio mode selection */
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val1 = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);
	reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_FMT_RVD;
	switch(fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:        /* I2S mode */
			reg_val &= ~SUN4I_HDMIAUDIOCTL_PCM;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_FMT_I2S;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:    /* Right Justified mode */
			reg_val &= ~SUN4I_HDMIAUDIOCTL_PCM;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_FMT_RGT;
			break;
		case SND_SOC_DAIFMT_LEFT_J:     /* Left Justified mode */
			reg_val &= ~SUN4I_HDMIAUDIOCTL_PCM;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_FMT_LFT;
			break;
		case SND_SOC_DAIFMT_DSP_A:      /* L data msb after FRM LRC */
			reg_val |= SUN4I_HDMIAUDIOCTL_PCM;
			reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_LRCP;
			break;
		case SND_SOC_DAIFMT_DSP_B:      /* L data msb during FRM LRC */
			reg_val |= SUN4I_HDMIAUDIOCTL_PCM;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_LRCP;
			break;
		default:
			return -EINVAL;
	}
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	writel(reg_val1, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);

	/* DAI signal inversions */
	reg_val1 = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);
	switch(fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:     /* normal bit clock + frame */
			reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_LRCP;
			reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_NB_IF:     /* normal bclk + inv frm */
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_LRCP;
			reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_IB_NF:     /* invert bclk + nor frm */
			reg_val1 &= ~SUN4I_HDMIAUDIOFAT0_LRCP;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_BCP;
			break;
		case SND_SOC_DAIFMT_IB_IF:     /* invert bclk + frm */
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_LRCP;
			reg_val1 |= SUN4I_HDMIAUDIOFAT0_BCP;
			break;
	}
	writel(reg_val1, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);

	/* word select size */
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);
	reg_val &= ~SUN4I_HDMIAUDIOFAT0_WSS_32BCLK;
	if(sun4i_hdmiaudio.ws_size == 16)
		reg_val |= SUN4I_HDMIAUDIOFAT0_WSS_16BCLK;
	else if(sun4i_hdmiaudio.ws_size == 20)
		reg_val |= SUN4I_HDMIAUDIOFAT0_WSS_20BCLK;
	else if(sun4i_hdmiaudio.ws_size == 24)
		reg_val |= SUN4I_HDMIAUDIOFAT0_WSS_24BCLK;
	else
		reg_val |= SUN4I_HDMIAUDIOFAT0_WSS_32BCLK;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);

	/* PCM REGISTER setup */
	reg_val = sun4i_hdmiaudio.pcm_txtype&0x3;
	reg_val |= sun4i_hdmiaudio.pcm_rxtype<<2;

	if(!sun4i_hdmiaudio.pcm_sync_type)
		reg_val |= SUN4I_HDMIAUDIOFAT1_SSYNC;							//short sync
	if(sun4i_hdmiaudio.pcm_sw == 16)
		reg_val |= SUN4I_HDMIAUDIOFAT1_SW;

	reg_val |=((sun4i_hdmiaudio.pcm_start_slot - 1)&0x3)<<6;		//start slot index

	reg_val |= sun4i_hdmiaudio.pcm_lsb_first<<9;			//MSB or LSB first

	if(sun4i_hdmiaudio.pcm_sync_period == 256)
		reg_val |= 0x4<<12;
	else if (sun4i_hdmiaudio.pcm_sync_period == 128)
		reg_val |= 0x3<<12;
	else if (sun4i_hdmiaudio.pcm_sync_period == 64)
		reg_val |= 0x2<<12;
	else if (sun4i_hdmiaudio.pcm_sync_period == 32)
		reg_val |= 0x1<<12;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT1);

	/* set FIFO control register */
	reg_val = 0 & 0x3;
	reg_val |= (0 & 0x1)<<2;
	reg_val |= SUN4I_HDMIAUDIOFCTL_RXTL(0xf);				//RX FIFO trigger level
	reg_val |= SUN4I_HDMIAUDIOFCTL_TXTL(0x40);				//TX FIFO empty trigger level
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);
	return 0;
}

static int sun4i_hdmiaudio_hw_params(struct snd_pcm_substream *substream,
																struct snd_pcm_hw_params *params,
																struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun4i_dma_params *dma_data;

	/* play or record */
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &sun4i_hdmiaudio_pcm_stereo_out;
	else
		dma_data = &sun4i_hdmiaudio_pcm_stereo_in;

	snd_soc_dai_set_dma_data(rtd->cpu_dai, substream, dma_data);

	return 0;
}

static int sun4i_hdmiaudio_trigger(struct snd_pcm_substream *substream,
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
				sun4i_snd_rxctrl_hdmiaudio(substream, 1);
			} else {
				sun4i_snd_txctrl_hdmiaudio(substream, 1);
			}
		sw_dma_ctrl(dma_data->channel, SW_DMAOP_STARTED);
		break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sun4i_snd_rxctrl_hdmiaudio(substream, 0);
			} else {
			  sun4i_snd_txctrl_hdmiaudio(substream, 0);
			}
			break;
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

//freq:   1: 22.5792MHz   0: 24.576MHz
static int sun4i_hdmiaudio_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
                                 unsigned int freq, int dir)
{
	if (!freq) {
		clk_set_rate(hdmiaudio_pll2clk, 24576000);
	} else {
		clk_set_rate(hdmiaudio_pll2clk, 22579200);
	}

	return 0;
}

static int sun4i_hdmiaudio_set_clkdiv(struct snd_soc_dai *cpu_dai, int div_id, int div)
{
	u32 reg;

	switch (div_id) {
	case SUN4I_DIV_MCLK:
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
		reg = (readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD) & ~SUN4I_HDMIAUDIOCLKD_MCLK_MASK) | (div << SUN4I_HDMIAUDIOCLKD_MCLK_OFFS);
		writel(reg, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
		break;
	case SUN4I_DIV_BCLK:
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
		reg = (readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD) & ~SUN4I_HDMIAUDIOCLKD_BCLK_MASK) | (div <<SUN4I_HDMIAUDIOCLKD_BCLK_OFFS);
		writel(reg, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
		break;
	default:
		return -EINVAL;
	}

	//diable MCLK output when high samplerate
	reg = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
	if(!(reg & 0xF)) {
		reg &= ~SUN4I_HDMIAUDIOCLKD_MCLKOEN;
		writel(reg, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
	} else {
		reg |= SUN4I_HDMIAUDIOCLKD_MCLKOEN;
		writel(reg, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
	}

	return 0;
}

u32 sun4i_hdmiaudio_get_clockrate(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(sun4i_hdmiaudio_get_clockrate);

static int sun4i_hdmiaudio_dai_probe(struct snd_soc_dai *dai)
{
	return 0;
}
static int sun4i_hdmiaudio_dai_remove(struct snd_soc_dai *dai)
{
	return 0;
}

static void hdmiaudioregsave(void)
{
	regsave[0] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	regsave[1] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);
	regsave[2] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT1);
	regsave[3] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL) | (0x3<<24);
	regsave[4] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
	regsave[5] = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
	regsave[6] = readl(sun4i_hdmiaudio.regs + SUN4I_TXCHSEL);
	regsave[7] = readl(sun4i_hdmiaudio.regs + SUN4I_TXCHMAP);
}

static void hdmiaudioregrestore(void)
{
	writel(regsave[0], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	writel(regsave[1], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT0);
	writel(regsave[2], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFAT1);
	writel(regsave[3], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOFCTL);
	writel(regsave[4], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOINT);
	writel(regsave[5], sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCLKD);
	writel(regsave[6], sun4i_hdmiaudio.regs + SUN4I_TXCHSEL);
	writel(regsave[7], sun4i_hdmiaudio.regs + SUN4I_TXCHMAP);
}

static int sun4i_hdmiaudio_suspend(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
 	printk("[HDMIAUDIO]Entered %s\n", __func__);

	//Global Enable Digital Audio Interface
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val &= ~SUN4I_HDMIAUDIOCTL_GEN;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	hdmiaudioregsave();
	//release the module clock
	clk_disable(hdmiaudio_moduleclk);

	clk_disable(hdmiaudio_apbclk);

	//printk("[HDMIAUDIO]PLL2 0x01c20008 = %#x, line = %d\n", *(volatile int*)0xF1C20008, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n", *(volatile int*)0xF1C200B8, __LINE__);

	return 0;
}

static int sun4i_hdmiaudio_resume(struct snd_soc_dai *cpu_dai)
{
	u32 reg_val;
	printk("[HDMIAUDIO]Entered %s\n", __func__);

	//release the module clock
	clk_enable(hdmiaudio_apbclk);

	//release the module clock
	clk_enable(hdmiaudio_moduleclk);

	hdmiaudioregrestore();

	//Global Enable Digital Audio Interface
	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val |= SUN4I_HDMIAUDIOCTL_GEN;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	//printk("[HDMIAUDIO]PLL2 0x01c20008 = %#x, line = %d\n", *(volatile int*)0xF1C20008, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c20068 = %#x, line= %d\n", *(volatile int*)0xF1C20068, __LINE__);
	printk("[HDMIAUDIO]SPECIAL CLK 0x01c200B8 = %#x, line = %d\n", *(volatile int*)0xF1C200B8, __LINE__);

	return 0;
}

#define SUN4I_I2S_RATES (SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT)
static struct snd_soc_dai_ops sun4i_hdmiaudio_dai_ops = {
	.trigger 		= sun4i_hdmiaudio_trigger,
	.hw_params 	= sun4i_hdmiaudio_hw_params,
	.set_fmt 		= sun4i_hdmiaudio_set_fmt,
	.set_clkdiv = sun4i_hdmiaudio_set_clkdiv,
	.set_sysclk = sun4i_hdmiaudio_set_sysclk,
};
static struct snd_soc_dai_driver sun4i_hdmiaudio_dai = {
	.probe 		= sun4i_hdmiaudio_dai_probe,
	.suspend 	= sun4i_hdmiaudio_suspend,
	.resume 	= sun4i_hdmiaudio_resume,
	.remove 	= sun4i_hdmiaudio_dai_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUN4I_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SUN4I_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,},
	.symmetric_rates = 1,
	.ops = &sun4i_hdmiaudio_dai_ops,
};

static int __devinit sun4i_hdmiaudio_dev_probe(struct platform_device *pdev)
{
	int reg_val = 0;
	int ret = 0;

	sun4i_hdmiaudio.regs = ioremap(SUN4I_HDMIAUDIOBASE, 0x100);
	if (sun4i_hdmiaudio.regs == NULL)
		return -ENXIO;

	sun4i_hdmiaudio.ccmregs = ioremap(SUN4I_CCMBASE, 0x100);
	if (sun4i_hdmiaudio.ccmregs == NULL)
		return -ENXIO;

	sun4i_hdmiaudio.ioregs = ioremap(0x01C20800, 0x100);
	if (sun4i_hdmiaudio.ioregs == NULL)
		return -ENXIO;

	//hdmiaudio apbclk
	hdmiaudio_apbclk = clk_get(NULL, "apb_i2s");
	if(-1 == clk_enable(hdmiaudio_apbclk)){
		printk("hdmiaudio_apbclk failed! line = %d\n", __LINE__);
	}

	hdmiaudio_pllx8 = clk_get(NULL, "audio_pllx8");

	//hdmiaudio pll2clk
	hdmiaudio_pll2clk = clk_get(NULL, "audio_pll");

	//hdmiaudio module clk
	hdmiaudio_moduleclk = clk_get(NULL, "i2s");

	if(clk_set_parent(hdmiaudio_moduleclk, hdmiaudio_pll2clk)){
		printk("try to set parent of hdmiaudio_moduleclk to hdmiaudio_pll2ck failed! line = %d\n",__LINE__);
	}


	if(clk_set_rate(hdmiaudio_moduleclk, 24576000/8)){
		printk("set hdmiaudio_moduleclk clock freq to 24576000 failed! line = %d\n", __LINE__);
	}


	if(-1 == clk_enable(hdmiaudio_moduleclk)){
		printk("open hdmiaudio_moduleclk failed! line = %d\n", __LINE__);
	}

	reg_val = readl(sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);
	reg_val |= SUN4I_HDMIAUDIOCTL_GEN;
	writel(reg_val, sun4i_hdmiaudio.regs + SUN4I_HDMIAUDIOCTL);

	ret = snd_soc_register_dai(&pdev->dev, &sun4i_hdmiaudio_dai);

	iounmap(sun4i_hdmiaudio.ioregs);

	return 0;
}

static int __devexit sun4i_hdmiaudio_dev_remove(struct platform_device *pdev)
{
	//release the module clock
	clk_disable(hdmiaudio_moduleclk);

	//release pllx8clk
	clk_put(hdmiaudio_pllx8);

	//release pll2clk
	clk_put(hdmiaudio_pll2clk);

	//release apbclk
	clk_put(hdmiaudio_apbclk);

	snd_soc_unregister_dai(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_device sun4i_hdmiaudio_device = {
	.name = "sun4i-hdmiaudio",
};

static struct platform_driver sun4i_hdmiaudio_driver = {
	.probe = sun4i_hdmiaudio_dev_probe,
	.remove = __devexit_p(sun4i_hdmiaudio_dev_remove),
	.driver = {
		.name = "sun4i-hdmiaudio",
		.owner = THIS_MODULE,
	},
};

static int __init sun4i_hdmiaudio_init(void)
{
	int err = 0;

	if((err = platform_device_register(&sun4i_hdmiaudio_device))<0)
		return err;

	if ((err = platform_driver_register(&sun4i_hdmiaudio_driver)) < 0)
		return err;

	return 0;
}
module_init(sun4i_hdmiaudio_init);

static void __exit sun4i_hdmiaudio_exit(void)
{
	platform_driver_unregister(&sun4i_hdmiaudio_driver);
}
module_exit(sun4i_hdmiaudio_exit);


/* Module information */
MODULE_AUTHOR("ALLWINNER");
MODULE_DESCRIPTION("sun4i hdmiaudio SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: sun4i-hdmiaudio");
