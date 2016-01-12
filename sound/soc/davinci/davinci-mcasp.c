/*
 * ALSA SoC McASP Audio Layer for TI DAVINCI processor
 *
 * Multi-channel Audio Serial Port Driver
 *
 * Author: Nirmal Pandey <n-pandey@ti.com>,
 *         Suresh Rajashekara <suresh.r@ti.com>
 *         Steve Chen <schen@.mvista.com>
 *
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_data/davinci_asp.h>
#include <linux/math64.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <sound/omap-pcm.h>

#include "edma-pcm.h"
#include "davinci-mcasp.h"

#define MCASP_MAX_AFIFO_DEPTH	64

static u32 context_regs[] = {
	DAVINCI_MCASP_TXFMCTL_REG,
	DAVINCI_MCASP_RXFMCTL_REG,
	DAVINCI_MCASP_TXFMT_REG,
	DAVINCI_MCASP_RXFMT_REG,
	DAVINCI_MCASP_ACLKXCTL_REG,
	DAVINCI_MCASP_ACLKRCTL_REG,
	DAVINCI_MCASP_AHCLKXCTL_REG,
	DAVINCI_MCASP_AHCLKRCTL_REG,
	DAVINCI_MCASP_PDIR_REG,
	DAVINCI_MCASP_RXMASK_REG,
	DAVINCI_MCASP_TXMASK_REG,
	DAVINCI_MCASP_RXTDM_REG,
	DAVINCI_MCASP_TXTDM_REG,
};

struct davinci_mcasp_context {
	u32	config_regs[ARRAY_SIZE(context_regs)];
	u32	afifo_regs[2]; /* for read/write fifo control registers */
	u32	*xrsr_regs; /* for serializer configuration */
	bool	pm_state;
};

struct davinci_mcasp_ruledata {
	struct davinci_mcasp *mcasp;
	int serializers;
};

struct davinci_mcasp {
	struct snd_dmaengine_dai_dma_data dma_data[2];
	void __iomem *base;
	u32 fifo_base;
	struct device *dev;
	struct snd_pcm_substream *substreams[2];

	/* McASP specific data */
	int	tdm_slots;
	u32	tdm_mask[2];
	int	slot_width;
	u8	op_mode;
	u8	num_serializer;
	u8	*serial_dir;
	u8	version;
	u8	bclk_div;
	int	streams;
	u32	irq_request[2];
	int	dma_request[2];

	int	sysclk_freq;
	bool	bclk_master;

	/* McASP FIFO related */
	u8	txnumevt;
	u8	rxnumevt;

	bool	dat_port;

	/* Used for comstraint setting on the second stream */
	u32	channels;

#ifdef CONFIG_PM_SLEEP
	struct davinci_mcasp_context context;
#endif

	struct davinci_mcasp_ruledata ruledata[2];
	struct snd_pcm_hw_constraint_list chconstr[2];
};

static inline void mcasp_set_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel(__raw_readl(reg) | val, reg);
}

static inline void mcasp_clr_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel((__raw_readl(reg) & ~(val)), reg);
}

static inline void mcasp_mod_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val, u32 mask)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel((__raw_readl(reg) & ~mask) | val, reg);
}

static inline void mcasp_set_reg(struct davinci_mcasp *mcasp, u32 offset,
				 u32 val)
{
	__raw_writel(val, mcasp->base + offset);
}

static inline u32 mcasp_get_reg(struct davinci_mcasp *mcasp, u32 offset)
{
	return (u32)__raw_readl(mcasp->base + offset);
}

static void mcasp_set_ctl_reg(struct davinci_mcasp *mcasp, u32 ctl_reg, u32 val)
{
	int i = 0;

	mcasp_set_bits(mcasp, ctl_reg, val);

	/* programming GBLCTL needs to read back from GBLCTL and verfiy */
	/* loop count is to avoid the lock-up */
	for (i = 0; i < 1000; i++) {
		if ((mcasp_get_reg(mcasp, ctl_reg) & val) == val)
			break;
	}

	if (i == 1000 && ((mcasp_get_reg(mcasp, ctl_reg) & val) != val))
		printk(KERN_ERR "GBLCTL write error\n");
}

static bool mcasp_is_synchronous(struct davinci_mcasp *mcasp)
{
	u32 rxfmctl = mcasp_get_reg(mcasp, DAVINCI_MCASP_RXFMCTL_REG);
	u32 aclkxctl = mcasp_get_reg(mcasp, DAVINCI_MCASP_ACLKXCTL_REG);

	return !(aclkxctl & TX_ASYNC) && rxfmctl & AFSRE;
}

static void mcasp_start_rx(struct davinci_mcasp *mcasp)
{
	if (mcasp->rxnumevt) {	/* enable FIFO */
		u32 reg = mcasp->fifo_base + MCASP_RFIFOCTL_OFFSET;

		mcasp_clr_bits(mcasp, reg, FIFO_ENABLE);
		mcasp_set_bits(mcasp, reg, FIFO_ENABLE);
	}

	/* Start clocks */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, RXHCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, RXCLKRST);
	/*
	 * When ASYNC == 0 the transmit and receive sections operate
	 * synchronously from the transmit clock and frame sync. We need to make
	 * sure that the TX signlas are enabled when starting reception.
	 */
	if (mcasp_is_synchronous(mcasp)) {
		mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXHCLKRST);
		mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXCLKRST);
	}

	/* Activate serializer(s) */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, RXSERCLR);
	/* Release RX state machine */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, RXSMRST);
	/* Release Frame Sync generator */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, RXFSRST);
	if (mcasp_is_synchronous(mcasp))
		mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXFSRST);

	/* enable receive IRQs */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_EVTCTLR_REG,
		       mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE]);
}

static void mcasp_start_tx(struct davinci_mcasp *mcasp)
{
	u32 cnt;

	if (mcasp->txnumevt) {	/* enable FIFO */
		u32 reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;

		mcasp_clr_bits(mcasp, reg, FIFO_ENABLE);
		mcasp_set_bits(mcasp, reg, FIFO_ENABLE);
	}

	/* Start clocks */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXHCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXCLKRST);
	/* Activate serializer(s) */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXSERCLR);

	/* wait for XDATA to be cleared */
	cnt = 0;
	while (!(mcasp_get_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG) &
		 ~XRDATA) && (cnt < 100000))
		cnt++;

	/* Release TX state machine */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXSMRST);
	/* Release Frame Sync generator */
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXFSRST);

	/* enable transmit IRQs */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_EVTCTLX_REG,
		       mcasp->irq_request[SNDRV_PCM_STREAM_PLAYBACK]);
}

static void davinci_mcasp_start(struct davinci_mcasp *mcasp, int stream)
{
	mcasp->streams++;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		mcasp_start_tx(mcasp);
	else
		mcasp_start_rx(mcasp);
}

static void mcasp_stop_rx(struct davinci_mcasp *mcasp)
{
	/* disable IRQ sources */
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_EVTCTLR_REG,
		       mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE]);

	/*
	 * In synchronous mode stop the TX clocks if no other stream is
	 * running
	 */
	if (mcasp_is_synchronous(mcasp) && !mcasp->streams)
		mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, 0);

	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTLR_REG, 0);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG, 0xFFFFFFFF);

	if (mcasp->rxnumevt) {	/* disable FIFO */
		u32 reg = mcasp->fifo_base + MCASP_RFIFOCTL_OFFSET;

		mcasp_clr_bits(mcasp, reg, FIFO_ENABLE);
	}
}

static void mcasp_stop_tx(struct davinci_mcasp *mcasp)
{
	u32 val = 0;

	/* disable IRQ sources */
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_EVTCTLX_REG,
		       mcasp->irq_request[SNDRV_PCM_STREAM_PLAYBACK]);

	/*
	 * In synchronous mode keep TX clocks running if the capture stream is
	 * still running.
	 */
	if (mcasp_is_synchronous(mcasp) && mcasp->streams)
		val =  TXHCLKRST | TXCLKRST | TXFSRST;

	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, val);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, 0xFFFFFFFF);

	if (mcasp->txnumevt) {	/* disable FIFO */
		u32 reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;

		mcasp_clr_bits(mcasp, reg, FIFO_ENABLE);
	}
}

static void davinci_mcasp_stop(struct davinci_mcasp *mcasp, int stream)
{
	mcasp->streams--;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		mcasp_stop_tx(mcasp);
	else
		mcasp_stop_rx(mcasp);
}

static irqreturn_t davinci_mcasp_tx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	struct snd_pcm_substream *substream;
	u32 irq_mask = mcasp->irq_request[SNDRV_PCM_STREAM_PLAYBACK];
	u32 handled_mask = 0;
	u32 stat;

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG);
	if (stat & XUNDRN & irq_mask) {
		dev_warn(mcasp->dev, "Transmit buffer underflow\n");
		handled_mask |= XUNDRN;

		substream = mcasp->substreams[SNDRV_PCM_STREAM_PLAYBACK];
		if (substream) {
			snd_pcm_stream_lock_irq(substream);
			if (snd_pcm_running(substream))
				snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
			snd_pcm_stream_unlock_irq(substream);
		}
	}

	if (!handled_mask)
		dev_warn(mcasp->dev, "unhandled tx event. txstat: 0x%08x\n",
			 stat);

	if (stat & XRERR)
		handled_mask |= XRERR;

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, handled_mask);

	return IRQ_RETVAL(handled_mask);
}

static irqreturn_t davinci_mcasp_rx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	struct snd_pcm_substream *substream;
	u32 irq_mask = mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE];
	u32 handled_mask = 0;
	u32 stat;

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG);
	if (stat & ROVRN & irq_mask) {
		dev_warn(mcasp->dev, "Receive buffer overflow\n");
		handled_mask |= ROVRN;

		substream = mcasp->substreams[SNDRV_PCM_STREAM_CAPTURE];
		if (substream) {
			snd_pcm_stream_lock_irq(substream);
			if (snd_pcm_running(substream))
				snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
			snd_pcm_stream_unlock_irq(substream);
		}
	}

	if (!handled_mask)
		dev_warn(mcasp->dev, "unhandled rx event. rxstat: 0x%08x\n",
			 stat);

	if (stat & XRERR)
		handled_mask |= XRERR;

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG, handled_mask);

	return IRQ_RETVAL(handled_mask);
}

static irqreturn_t davinci_mcasp_common_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	irqreturn_t ret = IRQ_NONE;

	if (mcasp->substreams[SNDRV_PCM_STREAM_PLAYBACK])
		ret = davinci_mcasp_tx_irq_handler(irq, data);

	if (mcasp->substreams[SNDRV_PCM_STREAM_CAPTURE])
		ret |= davinci_mcasp_rx_irq_handler(irq, data);

	return ret;
}

static int davinci_mcasp_set_dai_fmt(struct snd_soc_dai *cpu_dai,
					 unsigned int fmt)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;
	u32 data_delay;
	bool fs_pol_rising;
	bool inv_fs = false;

	pm_runtime_get_sync(mcasp->dev);
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXDUR);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRDUR);
		/* 1st data bit occur one ACLK cycle after the frame sync */
		data_delay = 1;
		break;
	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_AC97:
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXDUR);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRDUR);
		/* No delay after FS */
		data_delay = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		/* configure a full-word SYNC pulse (LRCLK) */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXDUR);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRDUR);
		/* 1st data bit occur one ACLK cycle after the frame sync */
		data_delay = 1;
		/* FS need to be inverted */
		inv_fs = true;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* configure a full-word SYNC pulse (LRCLK) */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXDUR);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRDUR);
		/* No delay after FS */
		data_delay = 0;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, FSXDLY(data_delay),
		       FSXDLY(3));
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RXFMT_REG, FSRDLY(data_delay),
		       FSRDLY(3));

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* codec is clock and frame slave */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, ACLKX | ACLKR);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AFSX | AFSR);
		mcasp->bclk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		/* codec is clock slave and frame master */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, ACLKX | ACLKR);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AFSX | AFSR);
		mcasp->bclk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		/* codec is clock master and frame slave */
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, ACLKX | ACLKR);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AFSX | AFSR);
		mcasp->bclk_master = 0;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* codec is clock and frame master */
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG,
			       ACLKX | AHCLKX | AFSX | ACLKR | AHCLKR | AFSR);
		mcasp->bclk_master = 0;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXPOL);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRPOL);
		fs_pol_rising = true;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXPOL);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRPOL);
		fs_pol_rising = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXPOL);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRPOL);
		fs_pol_rising = false;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXPOL);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRPOL);
		fs_pol_rising = true;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (inv_fs)
		fs_pol_rising = !fs_pol_rising;

	if (fs_pol_rising) {
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXPOL);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRPOL);
	} else {
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, FSXPOL);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, FSRPOL);
	}
out:
	pm_runtime_put(mcasp->dev);
	return ret;
}

static int __davinci_mcasp_set_clkdiv(struct snd_soc_dai *dai, int div_id,
				      int div, bool explicit)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_get_sync(mcasp->dev);
	switch (div_id) {
	case 0:		/* MCLK divider */
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG,
			       AHCLKXDIV(div - 1), AHCLKXDIV_MASK);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG,
			       AHCLKRDIV(div - 1), AHCLKRDIV_MASK);
		break;

	case 1:		/* BCLK divider */
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG,
			       ACLKXDIV(div - 1), ACLKXDIV_MASK);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG,
			       ACLKRDIV(div - 1), ACLKRDIV_MASK);
		if (explicit)
			mcasp->bclk_div = div;
		break;

	case 2:	/*
		 * BCLK/LRCLK ratio descries how many bit-clock cycles
		 * fit into one frame. The clock ratio is given for a
		 * full period of data (for I2S format both left and
		 * right channels), so it has to be divided by number
		 * of tdm-slots (for I2S - divided by 2).
		 * Instead of storing this ratio, we calculate a new
		 * tdm_slot width by dividing the the ratio by the
		 * number of configured tdm slots.
		 */
		mcasp->slot_width = div / mcasp->tdm_slots;
		if (div % mcasp->tdm_slots)
			dev_warn(mcasp->dev,
				 "%s(): BCLK/LRCLK %d is not divisible by %d tdm slots",
				 __func__, div, mcasp->tdm_slots);
		break;

	default:
		return -EINVAL;
	}

	pm_runtime_put(mcasp->dev);
	return 0;
}

static int davinci_mcasp_set_clkdiv(struct snd_soc_dai *dai, int div_id,
				    int div)
{
	return __davinci_mcasp_set_clkdiv(dai, div_id, div, 1);
}

static int davinci_mcasp_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_get_sync(mcasp->dev);
	if (dir == SND_SOC_CLOCK_OUT) {
		mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, AHCLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, AHCLKRE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AHCLKX);
	} else {
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, AHCLKXE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, AHCLKRE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AHCLKX);
	}

	mcasp->sysclk_freq = freq;

	pm_runtime_put(mcasp->dev);
	return 0;
}

/* All serializers must have equal number of channels */
static int davinci_mcasp_ch_constraint(struct davinci_mcasp *mcasp, int stream,
				       int serializers)
{
	struct snd_pcm_hw_constraint_list *cl = &mcasp->chconstr[stream];
	unsigned int *list = (unsigned int *) cl->list;
	int slots = mcasp->tdm_slots;
	int i, count = 0;

	if (mcasp->tdm_mask[stream])
		slots = hweight32(mcasp->tdm_mask[stream]);

	for (i = 2; i <= slots; i++)
		list[count++] = i;

	for (i = 2; i <= serializers; i++)
		list[count++] = i*slots;

	cl->count = count;

	return 0;
}

static int davinci_mcasp_set_ch_constraints(struct davinci_mcasp *mcasp)
{
	int rx_serializers = 0, tx_serializers = 0, ret, i;

	for (i = 0; i < mcasp->num_serializer; i++)
		if (mcasp->serial_dir[i] == TX_MODE)
			tx_serializers++;
		else if (mcasp->serial_dir[i] == RX_MODE)
			rx_serializers++;

	ret = davinci_mcasp_ch_constraint(mcasp, SNDRV_PCM_STREAM_PLAYBACK,
					  tx_serializers);
	if (ret)
		return ret;

	ret = davinci_mcasp_ch_constraint(mcasp, SNDRV_PCM_STREAM_CAPTURE,
					  rx_serializers);

	return ret;
}


static int davinci_mcasp_set_tdm_slot(struct snd_soc_dai *dai,
				      unsigned int tx_mask,
				      unsigned int rx_mask,
				      int slots, int slot_width)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	dev_dbg(mcasp->dev,
		 "%s() tx_mask 0x%08x rx_mask 0x%08x slots %d width %d\n",
		 __func__, tx_mask, rx_mask, slots, slot_width);

	if (tx_mask >= (1<<slots) || rx_mask >= (1<<slots)) {
		dev_err(mcasp->dev,
			"Bad tdm mask tx: 0x%08x rx: 0x%08x slots %d\n",
			tx_mask, rx_mask, slots);
		return -EINVAL;
	}

	if (slot_width &&
	    (slot_width < 8 || slot_width > 32 || slot_width % 4 != 0)) {
		dev_err(mcasp->dev, "%s: Unsupported slot_width %d\n",
			__func__, slot_width);
		return -EINVAL;
	}

	mcasp->tdm_slots = slots;
	mcasp->tdm_mask[SNDRV_PCM_STREAM_PLAYBACK] = tx_mask;
	mcasp->tdm_mask[SNDRV_PCM_STREAM_CAPTURE] = rx_mask;
	mcasp->slot_width = slot_width;

	return davinci_mcasp_set_ch_constraints(mcasp);
}

static int davinci_config_channel_size(struct davinci_mcasp *mcasp,
				       int sample_width)
{
	u32 fmt;
	u32 tx_rotate = (sample_width / 4) & 0x7;
	u32 mask = (1ULL << sample_width) - 1;
	u32 slot_width = sample_width;

	/*
	 * For captured data we should not rotate, inversion and masking is
	 * enoguh to get the data to the right position:
	 * Format	  data from bus		after reverse (XRBUF)
	 * S16_LE:	|LSB|MSB|xxx|xxx|	|xxx|xxx|MSB|LSB|
	 * S24_3LE:	|LSB|DAT|MSB|xxx|	|xxx|MSB|DAT|LSB|
	 * S24_LE:	|LSB|DAT|MSB|xxx|	|xxx|MSB|DAT|LSB|
	 * S32_LE:	|LSB|DAT|DAT|MSB|	|MSB|DAT|DAT|LSB|
	 */
	u32 rx_rotate = 0;

	/*
	 * Setting the tdm slot width either with set_clkdiv() or
	 * set_tdm_slot() allows us to for example send 32 bits per
	 * channel to the codec, while only 16 of them carry audio
	 * payload.
	 */
	if (mcasp->slot_width) {
		/*
		 * When we have more bclk then it is needed for the
		 * data, we need to use the rotation to move the
		 * received samples to have correct alignment.
		 */
		slot_width = mcasp->slot_width;
		rx_rotate = (slot_width - sample_width) / 4;
	}

	/* mapping of the XSSZ bit-field as described in the datasheet */
	fmt = (slot_width >> 1) - 1;

	if (mcasp->op_mode != DAVINCI_MCASP_DIT_MODE) {
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_RXFMT_REG, RXSSZ(fmt),
			       RXSSZ(0x0F));
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXSSZ(fmt),
			       TXSSZ(0x0F));
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXROT(tx_rotate),
			       TXROT(7));
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_RXFMT_REG, RXROT(rx_rotate),
			       RXROT(7));
		mcasp_set_reg(mcasp, DAVINCI_MCASP_RXMASK_REG, mask);
	}

	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXMASK_REG, mask);

	return 0;
}

static int mcasp_common_hw_param(struct davinci_mcasp *mcasp, int stream,
				 int period_words, int channels)
{
	struct snd_dmaengine_dai_dma_data *dma_data = &mcasp->dma_data[stream];
	int i;
	u8 tx_ser = 0;
	u8 rx_ser = 0;
	u8 slots = mcasp->tdm_slots;
	u8 max_active_serializers = (channels + slots - 1) / slots;
	int active_serializers, numevt;
	u32 reg;
	/* Default configuration */
	if (mcasp->version < MCASP_VERSION_3)
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PWREMUMGT_REG, MCASP_SOFT);

	/* All PINS as McASP */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PFUNC_REG, 0x00000000);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, 0xFFFFFFFF);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_XEVTCTL_REG, TXDATADMADIS);
	} else {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG, 0xFFFFFFFF);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_REVTCTL_REG, RXDATADMADIS);
	}

	for (i = 0; i < mcasp->num_serializer; i++) {
		mcasp_set_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
			       mcasp->serial_dir[i]);
		if (mcasp->serial_dir[i] == TX_MODE &&
					tx_ser < max_active_serializers) {
			mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AXR(i));
			mcasp_mod_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
				       DISMOD_LOW, DISMOD_MASK);
			tx_ser++;
		} else if (mcasp->serial_dir[i] == RX_MODE &&
					rx_ser < max_active_serializers) {
			mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AXR(i));
			rx_ser++;
		} else {
			mcasp_mod_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
				       SRMOD_INACTIVE, SRMOD_MASK);
		}
	}

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		active_serializers = tx_ser;
		numevt = mcasp->txnumevt;
		reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;
	} else {
		active_serializers = rx_ser;
		numevt = mcasp->rxnumevt;
		reg = mcasp->fifo_base + MCASP_RFIFOCTL_OFFSET;
	}

	if (active_serializers < max_active_serializers) {
		dev_warn(mcasp->dev, "stream has more channels (%d) than are "
			 "enabled in mcasp (%d)\n", channels,
			 active_serializers * slots);
		return -EINVAL;
	}

	/* AFIFO is not in use */
	if (!numevt) {
		/* Configure the burst size for platform drivers */
		if (active_serializers > 1) {
			/*
			 * If more than one serializers are in use we have one
			 * DMA request to provide data for all serializers.
			 * For example if three serializers are enabled the DMA
			 * need to transfer three words per DMA request.
			 */
			dma_data->maxburst = active_serializers;
		} else {
			dma_data->maxburst = 0;
		}
		return 0;
	}

	if (period_words % active_serializers) {
		dev_err(mcasp->dev, "Invalid combination of period words and "
			"active serializers: %d, %d\n", period_words,
			active_serializers);
		return -EINVAL;
	}

	/*
	 * Calculate the optimal AFIFO depth for platform side:
	 * The number of words for numevt need to be in steps of active
	 * serializers.
	 */
	numevt = (numevt / active_serializers) * active_serializers;

	while (period_words % numevt && numevt > 0)
		numevt -= active_serializers;
	if (numevt <= 0)
		numevt = active_serializers;

	mcasp_mod_bits(mcasp, reg, active_serializers, NUMDMA_MASK);
	mcasp_mod_bits(mcasp, reg, NUMEVT(numevt), NUMEVT_MASK);

	/* Configure the burst size for platform drivers */
	if (numevt == 1)
		numevt = 0;
	dma_data->maxburst = numevt;

	return 0;
}

static int mcasp_i2s_hw_param(struct davinci_mcasp *mcasp, int stream,
			      int channels)
{
	int i, active_slots;
	int total_slots;
	int active_serializers;
	u32 mask = 0;
	u32 busel = 0;

	total_slots = mcasp->tdm_slots;

	/*
	 * If more than one serializer is needed, then use them with
	 * all the specified tdm_slots. Otherwise, one serializer can
	 * cope with the transaction using just as many slots as there
	 * are channels in the stream.
	 */
	if (mcasp->tdm_mask[stream]) {
		active_slots = hweight32(mcasp->tdm_mask[stream]);
		active_serializers = (channels + active_slots - 1) /
			active_slots;
		if (active_serializers == 1) {
			active_slots = channels;
			for (i = 0; i < total_slots; i++) {
				if ((1 << i) & mcasp->tdm_mask[stream]) {
					mask |= (1 << i);
					if (--active_slots <= 0)
						break;
				}
			}
		}
	} else {
		active_serializers = (channels + total_slots - 1) / total_slots;
		if (active_serializers == 1)
			active_slots = channels;
		else
			active_slots = total_slots;

		for (i = 0; i < active_slots; i++)
			mask |= (1 << i);
	}
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, TX_ASYNC);

	if (!mcasp->dat_port)
		busel = TXSEL;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_TXTDM_REG, mask);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, busel | TXORD);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG,
			       FSXMOD(total_slots), FSXMOD(0x1FF));
	} else if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_RXTDM_REG, mask);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMT_REG, busel | RXORD);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG,
			       FSRMOD(total_slots), FSRMOD(0x1FF));
		/*
		 * If McASP is set to be TX/RX synchronous and the playback is
		 * not running already we need to configure the TX slots in
		 * order to have correct FSX on the bus
		 */
		if (mcasp_is_synchronous(mcasp) && !mcasp->channels)
			mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG,
				       FSXMOD(total_slots), FSXMOD(0x1FF));
	}

	return 0;
}

/* S/PDIF */
static int mcasp_dit_hw_param(struct davinci_mcasp *mcasp,
			      unsigned int rate)
{
	u32 cs_value = 0;
	u8 *cs_bytes = (u8*) &cs_value;

	/* Set the TX format : 24 bit right rotation, 32 bit slot, Pad 0
	   and LSB first */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXROT(6) | TXSSZ(15));

	/* Set TX frame synch : DIT Mode, 1 bit width, internal, rising edge */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE | FSXMOD(0x180));

	/* Set the TX tdm : for all the slots */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXTDM_REG, 0xFFFFFFFF);

	/* Set the TX clock controls : div = 1 and internal */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE | TX_ASYNC);

	mcasp_clr_bits(mcasp, DAVINCI_MCASP_XEVTCTL_REG, TXDATADMADIS);

	/* Only 44100 and 48000 are valid, both have the same setting */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, AHCLKXDIV(3));

	/* Enable the DIT */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_TXDITCTL_REG, DITEN);

	/* Set S/PDIF channel status bits */
	cs_bytes[0] = IEC958_AES0_CON_NOT_COPYRIGHT;
	cs_bytes[1] = IEC958_AES1_CON_PCM_CODER;

	switch (rate) {
	case 22050:
		cs_bytes[3] |= IEC958_AES3_CON_FS_22050;
		break;
	case 24000:
		cs_bytes[3] |= IEC958_AES3_CON_FS_24000;
		break;
	case 32000:
		cs_bytes[3] |= IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		cs_bytes[3] |= IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		cs_bytes[3] |= IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		cs_bytes[3] |= IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		cs_bytes[3] |= IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		cs_bytes[3] |= IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		cs_bytes[3] |= IEC958_AES3_CON_FS_192000;
		break;
	default:
		printk(KERN_WARNING "unsupported sampling rate: %d\n", rate);
		return -EINVAL;
	}

	mcasp_set_reg(mcasp, DAVINCI_MCASP_DITCSRA_REG, cs_value);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_DITCSRB_REG, cs_value);

	return 0;
}

static int davinci_mcasp_calc_clk_div(struct davinci_mcasp *mcasp,
				      unsigned int bclk_freq,
				      int *error_ppm)
{
	int div = mcasp->sysclk_freq / bclk_freq;
	int rem = mcasp->sysclk_freq % bclk_freq;

	if (rem != 0) {
		if (div == 0 ||
		    ((mcasp->sysclk_freq / div) - bclk_freq) >
		    (bclk_freq - (mcasp->sysclk_freq / (div+1)))) {
			div++;
			rem = rem - bclk_freq;
		}
	}
	if (error_ppm)
		*error_ppm =
			(div*1000000 + (int)div64_long(1000000LL*rem,
						       (int)bclk_freq))
			/div - 1000000;

	return div;
}

static int davinci_mcasp_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	int word_length;
	int channels = params_channels(params);
	int period_size = params_period_size(params);
	int ret;

	/*
	 * If mcasp is BCLK master, and a BCLK divider was not provided by
	 * the machine driver, we need to calculate the ratio.
	 */
	if (mcasp->bclk_master && mcasp->bclk_div == 0 && mcasp->sysclk_freq) {
		int slots = mcasp->tdm_slots;
		int rate = params_rate(params);
		int sbits = params_width(params);
		int ppm, div;

		if (mcasp->slot_width)
			sbits = mcasp->slot_width;

		div = davinci_mcasp_calc_clk_div(mcasp, rate*sbits*slots,
						 &ppm);
		if (ppm)
			dev_info(mcasp->dev, "Sample-rate is off by %d PPM\n",
				 ppm);

		__davinci_mcasp_set_clkdiv(cpu_dai, 1, div, 0);
	}

	ret = mcasp_common_hw_param(mcasp, substream->stream,
				    period_size * channels, channels);
	if (ret)
		return ret;

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		ret = mcasp_dit_hw_param(mcasp, params_rate(params));
	else
		ret = mcasp_i2s_hw_param(mcasp, substream->stream,
					 channels);

	if (ret)
		return ret;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_S8:
		word_length = 8;
		break;

	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		word_length = 16;
		break;

	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		word_length = 24;
		break;

	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_S24_LE:
		word_length = 24;
		break;

	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		word_length = 32;
		break;

	default:
		printk(KERN_WARNING "davinci-mcasp: unsupported PCM format");
		return -EINVAL;
	}

	davinci_config_channel_size(mcasp, word_length);

	if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE)
		mcasp->channels = channels;

	return 0;
}

static int davinci_mcasp_trigger(struct snd_pcm_substream *substream,
				     int cmd, struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		davinci_mcasp_start(mcasp, substream->stream);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		davinci_mcasp_stop(mcasp, substream->stream);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static const unsigned int davinci_mcasp_dai_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000,
};

#define DAVINCI_MAX_RATE_ERROR_PPM 1000

static int davinci_mcasp_hw_rule_rate(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule)
{
	struct davinci_mcasp_ruledata *rd = rule->private;
	struct snd_interval *ri =
		hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	int sbits = params_width(params);
	int slots = rd->mcasp->tdm_slots;
	struct snd_interval range;
	int i;

	if (rd->mcasp->slot_width)
		sbits = rd->mcasp->slot_width;

	snd_interval_any(&range);
	range.empty = 1;

	for (i = 0; i < ARRAY_SIZE(davinci_mcasp_dai_rates); i++) {
		if (snd_interval_test(ri, davinci_mcasp_dai_rates[i])) {
			uint bclk_freq = sbits*slots*
				davinci_mcasp_dai_rates[i];
			int ppm;

			davinci_mcasp_calc_clk_div(rd->mcasp, bclk_freq, &ppm);
			if (abs(ppm) < DAVINCI_MAX_RATE_ERROR_PPM) {
				if (range.empty) {
					range.min = davinci_mcasp_dai_rates[i];
					range.empty = 0;
				}
				range.max = davinci_mcasp_dai_rates[i];
			}
		}
	}

	dev_dbg(rd->mcasp->dev,
		"Frequencies %d-%d -> %d-%d for %d sbits and %d tdm slots\n",
		ri->min, ri->max, range.min, range.max, sbits, slots);

	return snd_interval_refine(hw_param_interval(params, rule->var),
				   &range);
}

static int davinci_mcasp_hw_rule_format(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	struct davinci_mcasp_ruledata *rd = rule->private;
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_mask nfmt;
	int rate = params_rate(params);
	int slots = rd->mcasp->tdm_slots;
	int i, count = 0;

	snd_mask_none(&nfmt);

	for (i = 0; i < SNDRV_PCM_FORMAT_LAST; i++) {
		if (snd_mask_test(fmt, i)) {
			uint sbits = snd_pcm_format_width(i);
			int ppm;

			if (rd->mcasp->slot_width)
				sbits = rd->mcasp->slot_width;

			davinci_mcasp_calc_clk_div(rd->mcasp, sbits*slots*rate,
						   &ppm);
			if (abs(ppm) < DAVINCI_MAX_RATE_ERROR_PPM) {
				snd_mask_set(&nfmt, i);
				count++;
			}
		}
	}
	dev_dbg(rd->mcasp->dev,
		"%d possible sample format for %d Hz and %d tdm slots\n",
		count, rate, slots);

	return snd_mask_refine(fmt, &nfmt);
}

static int davinci_mcasp_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	struct davinci_mcasp_ruledata *ruledata =
					&mcasp->ruledata[substream->stream];
	u32 max_channels = 0;
	int i, dir;
	int tdm_slots = mcasp->tdm_slots;

	if (mcasp->tdm_mask[substream->stream])
		tdm_slots = hweight32(mcasp->tdm_mask[substream->stream]);

	mcasp->substreams[substream->stream] = substream;

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		return 0;

	/*
	 * Limit the maximum allowed channels for the first stream:
	 * number of serializers for the direction * tdm slots per serializer
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = TX_MODE;
	else
		dir = RX_MODE;

	for (i = 0; i < mcasp->num_serializer; i++) {
		if (mcasp->serial_dir[i] == dir)
			max_channels++;
	}
	ruledata->serializers = max_channels;
	max_channels *= tdm_slots;
	/*
	 * If the already active stream has less channels than the calculated
	 * limnit based on the seirializers * tdm_slots, we need to use that as
	 * a constraint for the second stream.
	 * Otherwise (first stream or less allowed channels) we use the
	 * calculated constraint.
	 */
	if (mcasp->channels && mcasp->channels < max_channels)
		max_channels = mcasp->channels;
	/*
	 * But we can always allow channels upto the amount of
	 * the available tdm_slots.
	 */
	if (max_channels < tdm_slots)
		max_channels = tdm_slots;

	snd_pcm_hw_constraint_minmax(substream->runtime,
				     SNDRV_PCM_HW_PARAM_CHANNELS,
				     2, max_channels);

	snd_pcm_hw_constraint_list(substream->runtime,
				   0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &mcasp->chconstr[substream->stream]);

	if (mcasp->slot_width)
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					     8, mcasp->slot_width);

	/*
	 * If we rely on implicit BCLK divider setting we should
	 * set constraints based on what we can provide.
	 */
	if (mcasp->bclk_master && mcasp->bclk_div == 0 && mcasp->sysclk_freq) {
		int ret;

		ruledata->mcasp = mcasp;

		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  davinci_mcasp_hw_rule_rate,
					  ruledata,
					  SNDRV_PCM_HW_PARAM_FORMAT, -1);
		if (ret)
			return ret;
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_FORMAT,
					  davinci_mcasp_hw_rule_format,
					  ruledata,
					  SNDRV_PCM_HW_PARAM_RATE, -1);
		if (ret)
			return ret;
	}

	return 0;
}

static void davinci_mcasp_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);

	mcasp->substreams[substream->stream] = NULL;

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		return;

	if (!cpu_dai->active)
		mcasp->channels = 0;
}

static const struct snd_soc_dai_ops davinci_mcasp_dai_ops = {
	.startup	= davinci_mcasp_startup,
	.shutdown	= davinci_mcasp_shutdown,
	.trigger	= davinci_mcasp_trigger,
	.hw_params	= davinci_mcasp_hw_params,
	.set_fmt	= davinci_mcasp_set_dai_fmt,
	.set_clkdiv	= davinci_mcasp_set_clkdiv,
	.set_sysclk	= davinci_mcasp_set_sysclk,
	.set_tdm_slot	= davinci_mcasp_set_tdm_slot,
};

static int davinci_mcasp_dai_probe(struct snd_soc_dai *dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &mcasp->dma_data[SNDRV_PCM_STREAM_PLAYBACK];
	dai->capture_dma_data = &mcasp->dma_data[SNDRV_PCM_STREAM_CAPTURE];

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int davinci_mcasp_suspend(struct snd_soc_dai *dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);
	struct davinci_mcasp_context *context = &mcasp->context;
	u32 reg;
	int i;

	context->pm_state = pm_runtime_active(mcasp->dev);
	if (!context->pm_state)
		pm_runtime_get_sync(mcasp->dev);

	for (i = 0; i < ARRAY_SIZE(context_regs); i++)
		context->config_regs[i] = mcasp_get_reg(mcasp, context_regs[i]);

	if (mcasp->txnumevt) {
		reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;
		context->afifo_regs[0] = mcasp_get_reg(mcasp, reg);
	}
	if (mcasp->rxnumevt) {
		reg = mcasp->fifo_base + MCASP_RFIFOCTL_OFFSET;
		context->afifo_regs[1] = mcasp_get_reg(mcasp, reg);
	}

	for (i = 0; i < mcasp->num_serializer; i++)
		context->xrsr_regs[i] = mcasp_get_reg(mcasp,
						DAVINCI_MCASP_XRSRCTL_REG(i));

	pm_runtime_put_sync(mcasp->dev);

	return 0;
}

static int davinci_mcasp_resume(struct snd_soc_dai *dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);
	struct davinci_mcasp_context *context = &mcasp->context;
	u32 reg;
	int i;

	pm_runtime_get_sync(mcasp->dev);

	for (i = 0; i < ARRAY_SIZE(context_regs); i++)
		mcasp_set_reg(mcasp, context_regs[i], context->config_regs[i]);

	if (mcasp->txnumevt) {
		reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;
		mcasp_set_reg(mcasp, reg, context->afifo_regs[0]);
	}
	if (mcasp->rxnumevt) {
		reg = mcasp->fifo_base + MCASP_RFIFOCTL_OFFSET;
		mcasp_set_reg(mcasp, reg, context->afifo_regs[1]);
	}

	for (i = 0; i < mcasp->num_serializer; i++)
		mcasp_set_reg(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
			      context->xrsr_regs[i]);

	if (!context->pm_state)
		pm_runtime_put_sync(mcasp->dev);

	return 0;
}
#else
#define davinci_mcasp_suspend NULL
#define davinci_mcasp_resume NULL
#endif

#define DAVINCI_MCASP_RATES	SNDRV_PCM_RATE_8000_192000

#define DAVINCI_MCASP_PCM_FMTS (SNDRV_PCM_FMTBIT_S8 | \
				SNDRV_PCM_FMTBIT_U8 | \
				SNDRV_PCM_FMTBIT_S16_LE | \
				SNDRV_PCM_FMTBIT_U16_LE | \
				SNDRV_PCM_FMTBIT_S24_LE | \
				SNDRV_PCM_FMTBIT_U24_LE | \
				SNDRV_PCM_FMTBIT_S24_3LE | \
				SNDRV_PCM_FMTBIT_U24_3LE | \
				SNDRV_PCM_FMTBIT_S32_LE | \
				SNDRV_PCM_FMTBIT_U32_LE)

static struct snd_soc_dai_driver davinci_mcasp_dai[] = {
	{
		.name		= "davinci-mcasp.0",
		.probe		= davinci_mcasp_dai_probe,
		.suspend	= davinci_mcasp_suspend,
		.resume		= davinci_mcasp_resume,
		.playback	= {
			.channels_min	= 2,
			.channels_max	= 32 * 16,
			.rates 		= DAVINCI_MCASP_RATES,
			.formats	= DAVINCI_MCASP_PCM_FMTS,
		},
		.capture 	= {
			.channels_min 	= 2,
			.channels_max	= 32 * 16,
			.rates 		= DAVINCI_MCASP_RATES,
			.formats	= DAVINCI_MCASP_PCM_FMTS,
		},
		.ops 		= &davinci_mcasp_dai_ops,

		.symmetric_samplebits	= 1,
		.symmetric_rates	= 1,
	},
	{
		.name		= "davinci-mcasp.1",
		.probe		= davinci_mcasp_dai_probe,
		.playback 	= {
			.channels_min	= 1,
			.channels_max	= 384,
			.rates		= DAVINCI_MCASP_RATES,
			.formats	= DAVINCI_MCASP_PCM_FMTS,
		},
		.ops 		= &davinci_mcasp_dai_ops,
	},

};

static const struct snd_soc_component_driver davinci_mcasp_component = {
	.name		= "davinci-mcasp",
};

/* Some HW specific values and defaults. The rest is filled in from DT. */
static struct davinci_mcasp_pdata dm646x_mcasp_pdata = {
	.tx_dma_offset = 0x400,
	.rx_dma_offset = 0x400,
	.version = MCASP_VERSION_1,
};

static struct davinci_mcasp_pdata da830_mcasp_pdata = {
	.tx_dma_offset = 0x2000,
	.rx_dma_offset = 0x2000,
	.version = MCASP_VERSION_2,
};

static struct davinci_mcasp_pdata am33xx_mcasp_pdata = {
	.tx_dma_offset = 0,
	.rx_dma_offset = 0,
	.version = MCASP_VERSION_3,
};

static struct davinci_mcasp_pdata dra7_mcasp_pdata = {
	.tx_dma_offset = 0x200,
	.rx_dma_offset = 0x284,
	.version = MCASP_VERSION_4,
};

static const struct of_device_id mcasp_dt_ids[] = {
	{
		.compatible = "ti,dm646x-mcasp-audio",
		.data = &dm646x_mcasp_pdata,
	},
	{
		.compatible = "ti,da830-mcasp-audio",
		.data = &da830_mcasp_pdata,
	},
	{
		.compatible = "ti,am33xx-mcasp-audio",
		.data = &am33xx_mcasp_pdata,
	},
	{
		.compatible = "ti,dra7-mcasp-audio",
		.data = &dra7_mcasp_pdata,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mcasp_dt_ids);

static int mcasp_reparent_fck(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk *gfclk, *parent_clk;
	const char *parent_name;
	int ret;

	if (!node)
		return 0;

	parent_name = of_get_property(node, "fck_parent", NULL);
	if (!parent_name)
		return 0;

	gfclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(gfclk)) {
		dev_err(&pdev->dev, "failed to get fck\n");
		return PTR_ERR(gfclk);
	}

	parent_clk = clk_get(NULL, parent_name);
	if (IS_ERR(parent_clk)) {
		dev_err(&pdev->dev, "failed to get parent clock\n");
		ret = PTR_ERR(parent_clk);
		goto err1;
	}

	ret = clk_set_parent(gfclk, parent_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to reparent fck\n");
		goto err2;
	}

err2:
	clk_put(parent_clk);
err1:
	clk_put(gfclk);
	return ret;
}

static struct davinci_mcasp_pdata *davinci_mcasp_set_pdata_from_of(
						struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct davinci_mcasp_pdata *pdata = NULL;
	const struct of_device_id *match =
			of_match_device(mcasp_dt_ids, &pdev->dev);
	struct of_phandle_args dma_spec;

	const u32 *of_serial_dir32;
	u32 val;
	int i, ret = 0;

	if (pdev->dev.platform_data) {
		pdata = pdev->dev.platform_data;
		return pdata;
	} else if (match) {
		pdata = (struct davinci_mcasp_pdata*) match->data;
	} else {
		/* control shouldn't reach here. something is wrong */
		ret = -EINVAL;
		goto nodata;
	}

	ret = of_property_read_u32(np, "op-mode", &val);
	if (ret >= 0)
		pdata->op_mode = val;

	ret = of_property_read_u32(np, "tdm-slots", &val);
	if (ret >= 0) {
		if (val < 2 || val > 32) {
			dev_err(&pdev->dev,
				"tdm-slots must be in rage [2-32]\n");
			ret = -EINVAL;
			goto nodata;
		}

		pdata->tdm_slots = val;
	}

	of_serial_dir32 = of_get_property(np, "serial-dir", &val);
	val /= sizeof(u32);
	if (of_serial_dir32) {
		u8 *of_serial_dir = devm_kzalloc(&pdev->dev,
						 (sizeof(*of_serial_dir) * val),
						 GFP_KERNEL);
		if (!of_serial_dir) {
			ret = -ENOMEM;
			goto nodata;
		}

		for (i = 0; i < val; i++)
			of_serial_dir[i] = be32_to_cpup(&of_serial_dir32[i]);

		pdata->num_serializer = val;
		pdata->serial_dir = of_serial_dir;
	}

	ret = of_property_match_string(np, "dma-names", "tx");
	if (ret < 0)
		goto nodata;

	ret = of_parse_phandle_with_args(np, "dmas", "#dma-cells", ret,
					 &dma_spec);
	if (ret < 0)
		goto nodata;

	pdata->tx_dma_channel = dma_spec.args[0];

	/* RX is not valid in DIT mode */
	if (pdata->op_mode != DAVINCI_MCASP_DIT_MODE) {
		ret = of_property_match_string(np, "dma-names", "rx");
		if (ret < 0)
			goto nodata;

		ret = of_parse_phandle_with_args(np, "dmas", "#dma-cells", ret,
						 &dma_spec);
		if (ret < 0)
			goto nodata;

		pdata->rx_dma_channel = dma_spec.args[0];
	}

	ret = of_property_read_u32(np, "tx-num-evt", &val);
	if (ret >= 0)
		pdata->txnumevt = val;

	ret = of_property_read_u32(np, "rx-num-evt", &val);
	if (ret >= 0)
		pdata->rxnumevt = val;

	ret = of_property_read_u32(np, "sram-size-playback", &val);
	if (ret >= 0)
		pdata->sram_size_playback = val;

	ret = of_property_read_u32(np, "sram-size-capture", &val);
	if (ret >= 0)
		pdata->sram_size_capture = val;

	return  pdata;

nodata:
	if (ret < 0) {
		dev_err(&pdev->dev, "Error populating platform data, err %d\n",
			ret);
		pdata = NULL;
	}
	return  pdata;
}

enum {
	PCM_EDMA,
	PCM_SDMA,
};
static const char *sdma_prefix = "ti,omap";

static int davinci_mcasp_get_dma_type(struct davinci_mcasp *mcasp)
{
	struct dma_chan *chan;
	const char *tmp;
	int ret = PCM_EDMA;

	if (!mcasp->dev->of_node)
		return PCM_EDMA;

	tmp = mcasp->dma_data[SNDRV_PCM_STREAM_PLAYBACK].filter_data;
	chan = dma_request_slave_channel_reason(mcasp->dev, tmp);
	if (IS_ERR(chan)) {
		if (PTR_ERR(chan) != -EPROBE_DEFER)
			dev_err(mcasp->dev,
				"Can't verify DMA configuration (%ld)\n",
				PTR_ERR(chan));
		return PTR_ERR(chan);
	}
	BUG_ON(!chan->device || !chan->device->dev);

	if (chan->device->dev->of_node)
		ret = of_property_read_string(chan->device->dev->of_node,
					      "compatible", &tmp);
	else
		dev_dbg(mcasp->dev, "DMA controller has no of-node\n");

	dma_release_channel(chan);
	if (ret)
		return ret;

	dev_dbg(mcasp->dev, "DMA controller compatible = \"%s\"\n", tmp);
	if (!strncmp(tmp, sdma_prefix, strlen(sdma_prefix)))
		return PCM_SDMA;

	return PCM_EDMA;
}

static int davinci_mcasp_probe(struct platform_device *pdev)
{
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct resource *mem, *res, *dat;
	struct davinci_mcasp_pdata *pdata;
	struct davinci_mcasp *mcasp;
	char *irq_name;
	int *dma;
	int irq;
	int ret;

	if (!pdev->dev.platform_data && !pdev->dev.of_node) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	mcasp = devm_kzalloc(&pdev->dev, sizeof(struct davinci_mcasp),
			   GFP_KERNEL);
	if (!mcasp)
		return	-ENOMEM;

	pdata = davinci_mcasp_set_pdata_from_of(pdev);
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!mem) {
		dev_warn(mcasp->dev,
			 "\"mpu\" mem resource not found, using index 0\n");
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!mem) {
			dev_err(&pdev->dev, "no mem resource?\n");
			return -ENODEV;
		}
	}

	mcasp->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(mcasp->base))
		return PTR_ERR(mcasp->base);

	pm_runtime_enable(&pdev->dev);

	mcasp->op_mode = pdata->op_mode;
	/* sanity check for tdm slots parameter */
	if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE) {
		if (pdata->tdm_slots < 2) {
			dev_err(&pdev->dev, "invalid tdm slots: %d\n",
				pdata->tdm_slots);
			mcasp->tdm_slots = 2;
		} else if (pdata->tdm_slots > 32) {
			dev_err(&pdev->dev, "invalid tdm slots: %d\n",
				pdata->tdm_slots);
			mcasp->tdm_slots = 32;
		} else {
			mcasp->tdm_slots = pdata->tdm_slots;
		}
	}

	mcasp->num_serializer = pdata->num_serializer;
#ifdef CONFIG_PM_SLEEP
	mcasp->context.xrsr_regs = devm_kzalloc(&pdev->dev,
					sizeof(u32) * mcasp->num_serializer,
					GFP_KERNEL);
#endif
	mcasp->serial_dir = pdata->serial_dir;
	mcasp->version = pdata->version;
	mcasp->txnumevt = pdata->txnumevt;
	mcasp->rxnumevt = pdata->rxnumevt;

	mcasp->dev = &pdev->dev;

	irq = platform_get_irq_byname(pdev, "common");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_common",
					  dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						davinci_mcasp_common_irq_handler,
						IRQF_ONESHOT | IRQF_SHARED,
						irq_name, mcasp);
		if (ret) {
			dev_err(&pdev->dev, "common IRQ request failed\n");
			goto err;
		}

		mcasp->irq_request[SNDRV_PCM_STREAM_PLAYBACK] = XUNDRN;
		mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE] = ROVRN;
	}

	irq = platform_get_irq_byname(pdev, "rx");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_rx",
					  dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						davinci_mcasp_rx_irq_handler,
						IRQF_ONESHOT, irq_name, mcasp);
		if (ret) {
			dev_err(&pdev->dev, "RX IRQ request failed\n");
			goto err;
		}

		mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE] = ROVRN;
	}

	irq = platform_get_irq_byname(pdev, "tx");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_tx",
					  dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						davinci_mcasp_tx_irq_handler,
						IRQF_ONESHOT, irq_name, mcasp);
		if (ret) {
			dev_err(&pdev->dev, "TX IRQ request failed\n");
			goto err;
		}

		mcasp->irq_request[SNDRV_PCM_STREAM_PLAYBACK] = XUNDRN;
	}

	dat = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dat");
	if (dat)
		mcasp->dat_port = true;

	dma_data = &mcasp->dma_data[SNDRV_PCM_STREAM_PLAYBACK];
	if (dat)
		dma_data->addr = dat->start;
	else
		dma_data->addr = mem->start + pdata->tx_dma_offset;

	dma = &mcasp->dma_request[SNDRV_PCM_STREAM_PLAYBACK];
	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (res)
		*dma = res->start;
	else
		*dma = pdata->tx_dma_channel;

	/* dmaengine filter data for DT and non-DT boot */
	if (pdev->dev.of_node)
		dma_data->filter_data = "tx";
	else
		dma_data->filter_data = dma;

	/* RX is not valid in DIT mode */
	if (mcasp->op_mode != DAVINCI_MCASP_DIT_MODE) {
		dma_data = &mcasp->dma_data[SNDRV_PCM_STREAM_CAPTURE];
		if (dat)
			dma_data->addr = dat->start;
		else
			dma_data->addr = mem->start + pdata->rx_dma_offset;

		dma = &mcasp->dma_request[SNDRV_PCM_STREAM_CAPTURE];
		res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (res)
			*dma = res->start;
		else
			*dma = pdata->rx_dma_channel;

		/* dmaengine filter data for DT and non-DT boot */
		if (pdev->dev.of_node)
			dma_data->filter_data = "rx";
		else
			dma_data->filter_data = dma;
	}

	if (mcasp->version < MCASP_VERSION_3) {
		mcasp->fifo_base = DAVINCI_MCASP_V2_AFIFO_BASE;
		/* dma_params->dma_addr is pointing to the data port address */
		mcasp->dat_port = true;
	} else {
		mcasp->fifo_base = DAVINCI_MCASP_V3_AFIFO_BASE;
	}

	/* Allocate memory for long enough list for all possible
	 * scenarios. Maximum number tdm slots is 32 and there cannot
	 * be more serializers than given in the configuration.  The
	 * serializer directions could be taken into account, but it
	 * would make code much more complex and save only couple of
	 * bytes.
	 */
	mcasp->chconstr[SNDRV_PCM_STREAM_PLAYBACK].list =
		devm_kzalloc(mcasp->dev, sizeof(unsigned int) *
			     (32 + mcasp->num_serializer - 2),
			     GFP_KERNEL);

	mcasp->chconstr[SNDRV_PCM_STREAM_CAPTURE].list =
		devm_kzalloc(mcasp->dev, sizeof(unsigned int) *
			     (32 + mcasp->num_serializer - 2),
			     GFP_KERNEL);

	if (!mcasp->chconstr[SNDRV_PCM_STREAM_PLAYBACK].list ||
	    !mcasp->chconstr[SNDRV_PCM_STREAM_CAPTURE].list)
		return -ENOMEM;

	ret = davinci_mcasp_set_ch_constraints(mcasp);
	if (ret)
		goto err;

	dev_set_drvdata(&pdev->dev, mcasp);

	mcasp_reparent_fck(pdev);

	ret = devm_snd_soc_register_component(&pdev->dev,
					&davinci_mcasp_component,
					&davinci_mcasp_dai[pdata->op_mode], 1);

	if (ret != 0)
		goto err;

	ret = davinci_mcasp_get_dma_type(mcasp);
	switch (ret) {
	case PCM_EDMA:
#if IS_BUILTIN(CONFIG_SND_EDMA_SOC) || \
	(IS_MODULE(CONFIG_SND_DAVINCI_SOC_MCASP) && \
	 IS_MODULE(CONFIG_SND_EDMA_SOC))
		ret = edma_pcm_platform_register(&pdev->dev);
#else
		dev_err(&pdev->dev, "Missing SND_EDMA_SOC\n");
		ret = -EINVAL;
		goto err;
#endif
		break;
	case PCM_SDMA:
#if IS_BUILTIN(CONFIG_SND_OMAP_SOC) || \
	(IS_MODULE(CONFIG_SND_DAVINCI_SOC_MCASP) && \
	 IS_MODULE(CONFIG_SND_OMAP_SOC))
		ret = omap_pcm_platform_register(&pdev->dev);
#else
		dev_err(&pdev->dev, "Missing SND_SDMA_SOC\n");
		ret = -EINVAL;
		goto err;
#endif
		break;
	default:
		dev_err(&pdev->dev, "No DMA controller found (%d)\n", ret);
	case -EPROBE_DEFER:
		goto err;
		break;
	}

	if (ret) {
		dev_err(&pdev->dev, "register PCM failed: %d\n", ret);
		goto err;
	}

	return 0;

err:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int davinci_mcasp_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver davinci_mcasp_driver = {
	.probe		= davinci_mcasp_probe,
	.remove		= davinci_mcasp_remove,
	.driver		= {
		.name	= "davinci-mcasp",
		.of_match_table = mcasp_dt_ids,
	},
};

module_platform_driver(davinci_mcasp_driver);

MODULE_AUTHOR("Steve Chen");
MODULE_DESCRIPTION("TI DAVINCI McASP SoC Interface");
MODULE_LICENSE("GPL");
