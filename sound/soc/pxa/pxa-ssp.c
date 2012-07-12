/*
 * pxa-ssp.c  --  ALSA Soc Audio Layer
 *
 * Copyright 2005,2008 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood
 *         Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * TODO:
 *  o Test network mode for > 16bit sample size
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pxa2xx_ssp.h>

#include <asm/irq.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/pxa2xx-lib.h>

#include <mach/hardware.h>
#include <mach/dma.h>

#include "../../arm/pxa2xx-pcm.h"
#include "pxa-ssp.h"

/*
 * SSP audio private data
 */
struct ssp_priv {
	struct ssp_device *ssp;
	unsigned int sysclk;
	int dai_fmt;
#ifdef CONFIG_PM
	uint32_t	cr0;
	uint32_t	cr1;
	uint32_t	to;
	uint32_t	psp;
#endif
};

static void dump_registers(struct ssp_device *ssp)
{
	dev_dbg(&ssp->pdev->dev, "SSCR0 0x%08x SSCR1 0x%08x SSTO 0x%08x\n",
		 pxa_ssp_read_reg(ssp, SSCR0), pxa_ssp_read_reg(ssp, SSCR1),
		 pxa_ssp_read_reg(ssp, SSTO));

	dev_dbg(&ssp->pdev->dev, "SSPSP 0x%08x SSSR 0x%08x SSACD 0x%08x\n",
		 pxa_ssp_read_reg(ssp, SSPSP), pxa_ssp_read_reg(ssp, SSSR),
		 pxa_ssp_read_reg(ssp, SSACD));
}

static void pxa_ssp_enable(struct ssp_device *ssp)
{
	uint32_t sscr0;

	sscr0 = __raw_readl(ssp->mmio_base + SSCR0) | SSCR0_SSE;
	__raw_writel(sscr0, ssp->mmio_base + SSCR0);
}

static void pxa_ssp_disable(struct ssp_device *ssp)
{
	uint32_t sscr0;

	sscr0 = __raw_readl(ssp->mmio_base + SSCR0) & ~SSCR0_SSE;
	__raw_writel(sscr0, ssp->mmio_base + SSCR0);
}

struct pxa2xx_pcm_dma_data {
	struct pxa2xx_pcm_dma_params params;
	char name[20];
};

static void pxa_ssp_set_dma_params(struct ssp_device *ssp, int width4,
			int out, struct pxa2xx_pcm_dma_params *dma_data)
{
	struct pxa2xx_pcm_dma_data *dma;

	dma = container_of(dma_data, struct pxa2xx_pcm_dma_data, params);

	snprintf(dma->name, 20, "SSP%d PCM %s %s", ssp->port_id,
			width4 ? "32-bit" : "16-bit", out ? "out" : "in");

	dma->params.name = dma->name;
	dma->params.drcmr = &DRCMR(out ? ssp->drcmr_tx : ssp->drcmr_rx);
	dma->params.dcmd = (out ? (DCMD_INCSRCADDR | DCMD_FLOWTRG) :
				  (DCMD_INCTRGADDR | DCMD_FLOWSRC)) |
			(width4 ? DCMD_WIDTH4 : DCMD_WIDTH2) | DCMD_BURST16;
	dma->params.dev_addr = ssp->phys_base + SSDR;
}

static int pxa_ssp_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	struct pxa2xx_pcm_dma_data *dma;
	int ret = 0;

	if (!cpu_dai->active) {
		clk_enable(ssp->clk);
		pxa_ssp_disable(ssp);
	}

	dma = kzalloc(sizeof(struct pxa2xx_pcm_dma_data), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;
	snd_soc_dai_set_dma_data(cpu_dai, substream, &dma->params);

	return ret;
}

static void pxa_ssp_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *cpu_dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;

	if (!cpu_dai->active) {
		pxa_ssp_disable(ssp);
		clk_disable(ssp->clk);
	}

	kfree(snd_soc_dai_get_dma_data(cpu_dai, substream));
	snd_soc_dai_set_dma_data(cpu_dai, substream, NULL);
}

#ifdef CONFIG_PM

static int pxa_ssp_suspend(struct snd_soc_dai *cpu_dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;

	if (!cpu_dai->active)
		clk_enable(ssp->clk);

	priv->cr0 = __raw_readl(ssp->mmio_base + SSCR0);
	priv->cr1 = __raw_readl(ssp->mmio_base + SSCR1);
	priv->to  = __raw_readl(ssp->mmio_base + SSTO);
	priv->psp = __raw_readl(ssp->mmio_base + SSPSP);

	pxa_ssp_disable(ssp);
	clk_disable(ssp->clk);
	return 0;
}

static int pxa_ssp_resume(struct snd_soc_dai *cpu_dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	uint32_t sssr = SSSR_ROR | SSSR_TUR | SSSR_BCE;

	clk_enable(ssp->clk);

	__raw_writel(sssr, ssp->mmio_base + SSSR);
	__raw_writel(priv->cr0 & ~SSCR0_SSE, ssp->mmio_base + SSCR0);
	__raw_writel(priv->cr1, ssp->mmio_base + SSCR1);
	__raw_writel(priv->to,  ssp->mmio_base + SSTO);
	__raw_writel(priv->psp, ssp->mmio_base + SSPSP);

	if (cpu_dai->active)
		pxa_ssp_enable(ssp);
	else
		clk_disable(ssp->clk);

	return 0;
}

#else
#define pxa_ssp_suspend	NULL
#define pxa_ssp_resume	NULL
#endif

/**
 * ssp_set_clkdiv - set SSP clock divider
 * @div: serial clock rate divider
 */
static void pxa_ssp_set_scr(struct ssp_device *ssp, u32 div)
{
	u32 sscr0 = pxa_ssp_read_reg(ssp, SSCR0);

	if (ssp->type == PXA25x_SSP) {
		sscr0 &= ~0x0000ff00;
		sscr0 |= ((div - 2)/2) << 8; /* 2..512 */
	} else {
		sscr0 &= ~0x000fff00;
		sscr0 |= (div - 1) << 8;     /* 1..4096 */
	}
	pxa_ssp_write_reg(ssp, SSCR0, sscr0);
}

/**
 * pxa_ssp_get_clkdiv - get SSP clock divider
 */
static u32 pxa_ssp_get_scr(struct ssp_device *ssp)
{
	u32 sscr0 = pxa_ssp_read_reg(ssp, SSCR0);
	u32 div;

	if (ssp->type == PXA25x_SSP)
		div = ((sscr0 >> 8) & 0xff) * 2 + 2;
	else
		div = ((sscr0 >> 8) & 0xfff) + 1;
	return div;
}

/*
 * Set the SSP ports SYSCLK.
 */
static int pxa_ssp_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	int val;

	u32 sscr0 = pxa_ssp_read_reg(ssp, SSCR0) &
		~(SSCR0_ECS |  SSCR0_NCS | SSCR0_MOD | SSCR0_ACS);

	dev_dbg(&ssp->pdev->dev,
		"pxa_ssp_set_dai_sysclk id: %d, clk_id %d, freq %u\n",
		cpu_dai->id, clk_id, freq);

	switch (clk_id) {
	case PXA_SSP_CLK_NET_PLL:
		sscr0 |= SSCR0_MOD;
		break;
	case PXA_SSP_CLK_PLL:
		/* Internal PLL is fixed */
		if (ssp->type == PXA25x_SSP)
			priv->sysclk = 1843200;
		else
			priv->sysclk = 13000000;
		break;
	case PXA_SSP_CLK_EXT:
		priv->sysclk = freq;
		sscr0 |= SSCR0_ECS;
		break;
	case PXA_SSP_CLK_NET:
		priv->sysclk = freq;
		sscr0 |= SSCR0_NCS | SSCR0_MOD;
		break;
	case PXA_SSP_CLK_AUDIO:
		priv->sysclk = 0;
		pxa_ssp_set_scr(ssp, 1);
		sscr0 |= SSCR0_ACS;
		break;
	default:
		return -ENODEV;
	}

	/* The SSP clock must be disabled when changing SSP clock mode
	 * on PXA2xx.  On PXA3xx it must be enabled when doing so. */
	if (ssp->type != PXA3xx_SSP)
		clk_disable(ssp->clk);
	val = pxa_ssp_read_reg(ssp, SSCR0) | sscr0;
	pxa_ssp_write_reg(ssp, SSCR0, val);
	if (ssp->type != PXA3xx_SSP)
		clk_enable(ssp->clk);

	return 0;
}

/*
 * Set the SSP clock dividers.
 */
static int pxa_ssp_set_dai_clkdiv(struct snd_soc_dai *cpu_dai,
	int div_id, int div)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	int val;

	switch (div_id) {
	case PXA_SSP_AUDIO_DIV_ACDS:
		val = (pxa_ssp_read_reg(ssp, SSACD) & ~0x7) | SSACD_ACDS(div);
		pxa_ssp_write_reg(ssp, SSACD, val);
		break;
	case PXA_SSP_AUDIO_DIV_SCDB:
		val = pxa_ssp_read_reg(ssp, SSACD);
		val &= ~SSACD_SCDB;
		if (ssp->type == PXA3xx_SSP)
			val &= ~SSACD_SCDX8;
		switch (div) {
		case PXA_SSP_CLK_SCDB_1:
			val |= SSACD_SCDB;
			break;
		case PXA_SSP_CLK_SCDB_4:
			break;
		case PXA_SSP_CLK_SCDB_8:
			if (ssp->type == PXA3xx_SSP)
				val |= SSACD_SCDX8;
			else
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		pxa_ssp_write_reg(ssp, SSACD, val);
		break;
	case PXA_SSP_DIV_SCR:
		pxa_ssp_set_scr(ssp, div);
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

/*
 * Configure the PLL frequency pxa27x and (afaik - pxa320 only)
 */
static int pxa_ssp_set_dai_pll(struct snd_soc_dai *cpu_dai, int pll_id,
	int source, unsigned int freq_in, unsigned int freq_out)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	u32 ssacd = pxa_ssp_read_reg(ssp, SSACD) & ~0x70;

	if (ssp->type == PXA3xx_SSP)
		pxa_ssp_write_reg(ssp, SSACDD, 0);

	switch (freq_out) {
	case 5622000:
		break;
	case 11345000:
		ssacd |= (0x1 << 4);
		break;
	case 12235000:
		ssacd |= (0x2 << 4);
		break;
	case 14857000:
		ssacd |= (0x3 << 4);
		break;
	case 32842000:
		ssacd |= (0x4 << 4);
		break;
	case 48000000:
		ssacd |= (0x5 << 4);
		break;
	case 0:
		/* Disable */
		break;

	default:
		/* PXA3xx has a clock ditherer which can be used to generate
		 * a wider range of frequencies - calculate a value for it.
		 */
		if (ssp->type == PXA3xx_SSP) {
			u32 val;
			u64 tmp = 19968;
			tmp *= 1000000;
			do_div(tmp, freq_out);
			val = tmp;

			val = (val << 16) | 64;
			pxa_ssp_write_reg(ssp, SSACDD, val);

			ssacd |= (0x6 << 4);

			dev_dbg(&ssp->pdev->dev,
				"Using SSACDD %x to supply %uHz\n",
				val, freq_out);
			break;
		}

		return -EINVAL;
	}

	pxa_ssp_write_reg(ssp, SSACD, ssacd);

	return 0;
}

/*
 * Set the active slots in TDM/Network mode
 */
static int pxa_ssp_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	u32 sscr0;

	sscr0 = pxa_ssp_read_reg(ssp, SSCR0);
	sscr0 &= ~(SSCR0_MOD | SSCR0_SlotsPerFrm(8) | SSCR0_EDSS | SSCR0_DSS);

	/* set slot width */
	if (slot_width > 16)
		sscr0 |= SSCR0_EDSS | SSCR0_DataSize(slot_width - 16);
	else
		sscr0 |= SSCR0_DataSize(slot_width);

	if (slots > 1) {
		/* enable network mode */
		sscr0 |= SSCR0_MOD;

		/* set number of active slots */
		sscr0 |= SSCR0_SlotsPerFrm(slots);

		/* set active slot mask */
		pxa_ssp_write_reg(ssp, SSTSA, tx_mask);
		pxa_ssp_write_reg(ssp, SSRSA, rx_mask);
	}
	pxa_ssp_write_reg(ssp, SSCR0, sscr0);

	return 0;
}

/*
 * Tristate the SSP DAI lines
 */
static int pxa_ssp_set_dai_tristate(struct snd_soc_dai *cpu_dai,
	int tristate)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	u32 sscr1;

	sscr1 = pxa_ssp_read_reg(ssp, SSCR1);
	if (tristate)
		sscr1 &= ~SSCR1_TTE;
	else
		sscr1 |= SSCR1_TTE;
	pxa_ssp_write_reg(ssp, SSCR1, sscr1);

	return 0;
}

/*
 * Set up the SSP DAI format.
 * The SSP Port must be inactive before calling this function as the
 * physical interface format is changed.
 */
static int pxa_ssp_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	u32 sscr0, sscr1, sspsp, scfr;

	/* check if we need to change anything at all */
	if (priv->dai_fmt == fmt)
		return 0;

	/* we can only change the settings if the port is not in use */
	if (pxa_ssp_read_reg(ssp, SSCR0) & SSCR0_SSE) {
		dev_err(&ssp->pdev->dev,
			"can't change hardware dai format: stream is in use");
		return -EINVAL;
	}

	/* reset port settings */
	sscr0 = pxa_ssp_read_reg(ssp, SSCR0) &
		~(SSCR0_ECS |  SSCR0_NCS | SSCR0_MOD | SSCR0_ACS);
	sscr1 = SSCR1_RxTresh(8) | SSCR1_TxTresh(7);
	sspsp = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		sscr1 |= SSCR1_SCLKDIR | SSCR1_SFRMDIR | SSCR1_SCFR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		sscr1 |= SSCR1_SCLKDIR | SSCR1_SCFR;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		sspsp |= SSPSP_SFRMP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		sspsp |= SSPSP_SCMODE(2);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		sspsp |= SSPSP_SCMODE(2) | SSPSP_SFRMP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		sscr0 |= SSCR0_PSP;
		sscr1 |= SSCR1_RWOT | SSCR1_TRAIL;
		/* See hw_params() */
		break;

	case SND_SOC_DAIFMT_DSP_A:
		sspsp |= SSPSP_FSRT;
	case SND_SOC_DAIFMT_DSP_B:
		sscr0 |= SSCR0_MOD | SSCR0_PSP;
		sscr1 |= SSCR1_TRAIL | SSCR1_RWOT;
		break;

	default:
		return -EINVAL;
	}

	pxa_ssp_write_reg(ssp, SSCR0, sscr0);
	pxa_ssp_write_reg(ssp, SSCR1, sscr1);
	pxa_ssp_write_reg(ssp, SSPSP, sspsp);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		scfr = pxa_ssp_read_reg(ssp, SSCR1) | SSCR1_SCFR;
		pxa_ssp_write_reg(ssp, SSCR1, scfr);

		while (pxa_ssp_read_reg(ssp, SSSR) & SSSR_BSY)
			cpu_relax();
		break;
	}

	dump_registers(ssp);

	/* Since we are configuring the timings for the format by hand
	 * we have to defer some things until hw_params() where we
	 * know parameters like the sample size.
	 */
	priv->dai_fmt = fmt;

	return 0;
}

/*
 * Set the SSP audio DMA parameters and sample size.
 * Can be called multiple times by oss emulation.
 */
static int pxa_ssp_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *cpu_dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	int chn = params_channels(params);
	u32 sscr0;
	u32 sspsp;
	int width = snd_pcm_format_physical_width(params_format(params));
	int ttsa = pxa_ssp_read_reg(ssp, SSTSA) & 0xf;
	struct pxa2xx_pcm_dma_params *dma_data;

	dma_data = snd_soc_dai_get_dma_data(cpu_dai, substream);

	/* Network mode with one active slot (ttsa == 1) can be used
	 * to force 16-bit frame width on the wire (for S16_LE), even
	 * with two channels. Use 16-bit DMA transfers for this case.
	 */
	pxa_ssp_set_dma_params(ssp,
		((chn == 2) && (ttsa != 1)) || (width == 32),
		substream->stream == SNDRV_PCM_STREAM_PLAYBACK, dma_data);

	/* we can only change the settings if the port is not in use */
	if (pxa_ssp_read_reg(ssp, SSCR0) & SSCR0_SSE)
		return 0;

	/* clear selected SSP bits */
	sscr0 = pxa_ssp_read_reg(ssp, SSCR0) & ~(SSCR0_DSS | SSCR0_EDSS);

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		if (ssp->type == PXA3xx_SSP)
			sscr0 |= SSCR0_FPCKE;
		sscr0 |= SSCR0_DataSize(16);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sscr0 |= (SSCR0_EDSS | SSCR0_DataSize(8));
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		sscr0 |= (SSCR0_EDSS | SSCR0_DataSize(16));
		break;
	}
	pxa_ssp_write_reg(ssp, SSCR0, sscr0);

	switch (priv->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	       sspsp = pxa_ssp_read_reg(ssp, SSPSP);

		if ((pxa_ssp_get_scr(ssp) == 4) && (width == 16)) {
			/* This is a special case where the bitclk is 64fs
			* and we're not dealing with 2*32 bits of audio
			* samples.
			*
			* The SSP values used for that are all found out by
			* trying and failing a lot; some of the registers
			* needed for that mode are only available on PXA3xx.
			*/
			if (ssp->type != PXA3xx_SSP)
				return -EINVAL;

			sspsp |= SSPSP_SFRMWDTH(width * 2);
			sspsp |= SSPSP_SFRMDLY(width * 4);
			sspsp |= SSPSP_EDMYSTOP(3);
			sspsp |= SSPSP_DMYSTOP(3);
			sspsp |= SSPSP_DMYSTRT(1);
		} else {
			/* The frame width is the width the LRCLK is
			 * asserted for; the delay is expressed in
			 * half cycle units.  We need the extra cycle
			 * because the data starts clocking out one BCLK
			 * after LRCLK changes polarity.
			 */
			sspsp |= SSPSP_SFRMWDTH(width + 1);
			sspsp |= SSPSP_SFRMDLY((width + 1) * 2);
			sspsp |= SSPSP_DMYSTRT(1);
		}

		pxa_ssp_write_reg(ssp, SSPSP, sspsp);
		break;
	default:
		break;
	}

	/* When we use a network mode, we always require TDM slots
	 * - complain loudly and fail if they've not been set up yet.
	 */
	if ((sscr0 & SSCR0_MOD) && !ttsa) {
		dev_err(&ssp->pdev->dev, "No TDM timeslot configured\n");
		return -EINVAL;
	}

	dump_registers(ssp);

	return 0;
}

static void pxa_ssp_set_running_bit(struct snd_pcm_substream *substream,
				    struct ssp_device *ssp, int value)
{
	uint32_t sscr0 = pxa_ssp_read_reg(ssp, SSCR0);
	uint32_t sscr1 = pxa_ssp_read_reg(ssp, SSCR1);
	uint32_t sspsp = pxa_ssp_read_reg(ssp, SSPSP);
	uint32_t sssr = pxa_ssp_read_reg(ssp, SSSR);

	if (value && (sscr0 & SSCR0_SSE))
		pxa_ssp_write_reg(ssp, SSCR0, sscr0 & ~SSCR0_SSE);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (value)
			sscr1 |= SSCR1_TSRE;
		else
			sscr1 &= ~SSCR1_TSRE;
	} else {
		if (value)
			sscr1 |= SSCR1_RSRE;
		else
			sscr1 &= ~SSCR1_RSRE;
	}

	pxa_ssp_write_reg(ssp, SSCR1, sscr1);

	if (value) {
		pxa_ssp_write_reg(ssp, SSSR, sssr);
		pxa_ssp_write_reg(ssp, SSPSP, sspsp);
		pxa_ssp_write_reg(ssp, SSCR0, sscr0 | SSCR0_SSE);
	}
}

static int pxa_ssp_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *cpu_dai)
{
	int ret = 0;
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	struct ssp_device *ssp = priv->ssp;
	int val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		pxa_ssp_enable(ssp);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pxa_ssp_set_running_bit(substream, ssp, 1);
		val = pxa_ssp_read_reg(ssp, SSSR);
		pxa_ssp_write_reg(ssp, SSSR, val);
		break;
	case SNDRV_PCM_TRIGGER_START:
		pxa_ssp_set_running_bit(substream, ssp, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pxa_ssp_set_running_bit(substream, ssp, 0);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		pxa_ssp_disable(ssp);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pxa_ssp_set_running_bit(substream, ssp, 0);
		break;

	default:
		ret = -EINVAL;
	}

	dump_registers(ssp);

	return ret;
}

static int pxa_ssp_probe(struct snd_soc_dai *dai)
{
	struct ssp_priv *priv;
	int ret;

	priv = kzalloc(sizeof(struct ssp_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ssp = pxa_ssp_request(dai->id + 1, "SoC audio");
	if (priv->ssp == NULL) {
		ret = -ENODEV;
		goto err_priv;
	}

	priv->dai_fmt = (unsigned int) -1;
	snd_soc_dai_set_drvdata(dai, priv);

	return 0;

err_priv:
	kfree(priv);
	return ret;
}

static int pxa_ssp_remove(struct snd_soc_dai *dai)
{
	struct ssp_priv *priv = snd_soc_dai_get_drvdata(dai);

	pxa_ssp_free(priv->ssp);
	kfree(priv);
	return 0;
}

#define PXA_SSP_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
			  SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |	\
			  SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |	\
			  SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 |	\
			  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define PXA_SSP_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			    SNDRV_PCM_FMTBIT_S24_LE |	\
			    SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops pxa_ssp_dai_ops = {
	.startup	= pxa_ssp_startup,
	.shutdown	= pxa_ssp_shutdown,
	.trigger	= pxa_ssp_trigger,
	.hw_params	= pxa_ssp_hw_params,
	.set_sysclk	= pxa_ssp_set_dai_sysclk,
	.set_clkdiv	= pxa_ssp_set_dai_clkdiv,
	.set_pll	= pxa_ssp_set_dai_pll,
	.set_fmt	= pxa_ssp_set_dai_fmt,
	.set_tdm_slot	= pxa_ssp_set_dai_tdm_slot,
	.set_tristate	= pxa_ssp_set_dai_tristate,
};

static struct snd_soc_dai_driver pxa_ssp_dai = {
		.probe = pxa_ssp_probe,
		.remove = pxa_ssp_remove,
		.suspend = pxa_ssp_suspend,
		.resume = pxa_ssp_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 8,
			.rates = PXA_SSP_RATES,
			.formats = PXA_SSP_FORMATS,
		},
		.capture = {
			 .channels_min = 1,
			 .channels_max = 8,
			.rates = PXA_SSP_RATES,
			.formats = PXA_SSP_FORMATS,
		 },
		.ops = &pxa_ssp_dai_ops,
};

static __devinit int asoc_ssp_probe(struct platform_device *pdev)
{
	return snd_soc_register_dai(&pdev->dev, &pxa_ssp_dai);
}

static int __devexit asoc_ssp_remove(struct platform_device *pdev)
{
	snd_soc_unregister_dai(&pdev->dev);
	return 0;
}

static struct platform_driver asoc_ssp_driver = {
	.driver = {
			.name = "pxa-ssp-dai",
			.owner = THIS_MODULE,
	},

	.probe = asoc_ssp_probe,
	.remove = __devexit_p(asoc_ssp_remove),
};

module_platform_driver(asoc_ssp_driver);

/* Module information */
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("PXA SSP/PCM SoC Interface");
MODULE_LICENSE("GPL");
