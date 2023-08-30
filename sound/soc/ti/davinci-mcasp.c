// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/bitmap.h>
#include <linux/gpio/driver.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "edma-pcm.h"
#include "sdma-pcm.h"
#include "udma-pcm.h"
#include "davinci-mcasp.h"

#define MCASP_MAX_AFIFO_DEPTH	64

#ifdef CONFIG_PM
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
	DAVINCI_MCASP_PFUNC_REG,
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
#endif

struct davinci_mcasp_ruledata {
	struct davinci_mcasp *mcasp;
	int serializers;
};

struct davinci_mcasp {
	struct snd_dmaengine_dai_dma_data dma_data[2];
	struct davinci_mcasp_pdata *pdata;
	void __iomem *base;
	u32 fifo_base;
	struct device *dev;
	struct snd_pcm_substream *substreams[2];
	unsigned int dai_fmt;

	u32 iec958_status;

	/* Audio can not be enabled due to missing parameter(s) */
	bool	missing_audio_param;

	/* McASP specific data */
	int	tdm_slots;
	u32	tdm_mask[2];
	int	slot_width;
	u8	op_mode;
	u8	dismod;
	u8	num_serializer;
	u8	*serial_dir;
	u8	version;
	u8	bclk_div;
	int	streams;
	u32	irq_request[2];

	int	sysclk_freq;
	bool	bclk_master;
	u32	auxclk_fs_ratio;

	unsigned long pdir; /* Pin direction bitfield */

	/* McASP FIFO related */
	u8	txnumevt;
	u8	rxnumevt;

	bool	dat_port;

	/* Used for comstraint setting on the second stream */
	u32	channels;
	int	max_format_width;
	u8	active_serializers[2];

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif

#ifdef CONFIG_PM
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

static inline void mcasp_set_clk_pdir(struct davinci_mcasp *mcasp, bool enable)
{
	u32 bit = PIN_BIT_AMUTE;

	for_each_set_bit_from(bit, &mcasp->pdir, PIN_BIT_AFSR + 1) {
		if (enable)
			mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(bit));
		else
			mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(bit));
	}
}

static inline void mcasp_set_axr_pdir(struct davinci_mcasp *mcasp, bool enable)
{
	u32 bit;

	for_each_set_bit(bit, &mcasp->pdir, PIN_BIT_AMUTE) {
		if (enable)
			mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(bit));
		else
			mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(bit));
	}
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
		mcasp_set_clk_pdir(mcasp, true);
	}

	/* Activate serializer(s) */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG, 0xFFFFFFFF);
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
	mcasp_set_clk_pdir(mcasp, true);

	/* Activate serializer(s) */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, 0xFFFFFFFF);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, TXSERCLR);

	/* wait for XDATA to be cleared */
	cnt = 0;
	while ((mcasp_get_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG) & XRDATA) &&
	       (cnt < 100000))
		cnt++;

	mcasp_set_axr_pdir(mcasp, true);

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
	if (mcasp_is_synchronous(mcasp) && !mcasp->streams) {
		mcasp_set_clk_pdir(mcasp, false);
		mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, 0);
	}

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
	else
		mcasp_set_clk_pdir(mcasp, false);


	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTLX_REG, val);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, 0xFFFFFFFF);

	if (mcasp->txnumevt) {	/* disable FIFO */
		u32 reg = mcasp->fifo_base + MCASP_WFIFOCTL_OFFSET;

		mcasp_clr_bits(mcasp, reg, FIFO_ENABLE);
	}

	mcasp_set_axr_pdir(mcasp, false);
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
		if (substream)
			snd_pcm_stop_xrun(substream);
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
		if (substream)
			snd_pcm_stop_xrun(substream);
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

	if (!fmt)
		return 0;

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
	case SND_SOC_DAIFMT_RIGHT_J:
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

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* codec is clock and frame slave */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		/* BCLK */
		set_bit(PIN_BIT_ACLKX, &mcasp->pdir);
		set_bit(PIN_BIT_ACLKR, &mcasp->pdir);
		/* Frame Sync */
		set_bit(PIN_BIT_AFSX, &mcasp->pdir);
		set_bit(PIN_BIT_AFSR, &mcasp->pdir);

		mcasp->bclk_master = 1;
		break;
	case SND_SOC_DAIFMT_BP_FC:
		/* codec is clock slave and frame master */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		/* BCLK */
		set_bit(PIN_BIT_ACLKX, &mcasp->pdir);
		set_bit(PIN_BIT_ACLKR, &mcasp->pdir);
		/* Frame Sync */
		clear_bit(PIN_BIT_AFSX, &mcasp->pdir);
		clear_bit(PIN_BIT_AFSR, &mcasp->pdir);

		mcasp->bclk_master = 1;
		break;
	case SND_SOC_DAIFMT_BC_FP:
		/* codec is clock master and frame slave */
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		/* BCLK */
		clear_bit(PIN_BIT_ACLKX, &mcasp->pdir);
		clear_bit(PIN_BIT_ACLKR, &mcasp->pdir);
		/* Frame Sync */
		set_bit(PIN_BIT_AFSX, &mcasp->pdir);
		set_bit(PIN_BIT_AFSR, &mcasp->pdir);

		mcasp->bclk_master = 0;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* codec is clock and frame master */
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE);

		mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, ACLKRE);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_RXFMCTL_REG, AFSRE);

		/* BCLK */
		clear_bit(PIN_BIT_ACLKX, &mcasp->pdir);
		clear_bit(PIN_BIT_ACLKR, &mcasp->pdir);
		/* Frame Sync */
		clear_bit(PIN_BIT_AFSX, &mcasp->pdir);
		clear_bit(PIN_BIT_AFSR, &mcasp->pdir);

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

	mcasp->dai_fmt = fmt;
out:
	pm_runtime_put(mcasp->dev);
	return ret;
}

static int __davinci_mcasp_set_clkdiv(struct davinci_mcasp *mcasp, int div_id,
				      int div, bool explicit)
{
	pm_runtime_get_sync(mcasp->dev);
	switch (div_id) {
	case MCASP_CLKDIV_AUXCLK:			/* MCLK divider */
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG,
			       AHCLKXDIV(div - 1), AHCLKXDIV_MASK);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG,
			       AHCLKRDIV(div - 1), AHCLKRDIV_MASK);
		break;

	case MCASP_CLKDIV_BCLK:			/* BCLK divider */
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG,
			       ACLKXDIV(div - 1), ACLKXDIV_MASK);
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG,
			       ACLKRDIV(div - 1), ACLKRDIV_MASK);
		if (explicit)
			mcasp->bclk_div = div;
		break;

	case MCASP_CLKDIV_BCLK_FS_RATIO:
		/*
		 * BCLK/LRCLK ratio descries how many bit-clock cycles
		 * fit into one frame. The clock ratio is given for a
		 * full period of data (for I2S format both left and
		 * right channels), so it has to be divided by number
		 * of tdm-slots (for I2S - divided by 2).
		 * Instead of storing this ratio, we calculate a new
		 * tdm_slot width by dividing the ratio by the
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
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	return __davinci_mcasp_set_clkdiv(mcasp, div_id, div, 1);
}

static int davinci_mcasp_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				    unsigned int freq, int dir)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_get_sync(mcasp->dev);

	if (dir == SND_SOC_CLOCK_IN) {
		switch (clk_id) {
		case MCASP_CLK_HCLK_AHCLK:
			mcasp_clr_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG,
				       AHCLKXE);
			mcasp_clr_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG,
				       AHCLKRE);
			clear_bit(PIN_BIT_AHCLKX, &mcasp->pdir);
			break;
		case MCASP_CLK_HCLK_AUXCLK:
			mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG,
				       AHCLKXE);
			mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG,
				       AHCLKRE);
			set_bit(PIN_BIT_AHCLKX, &mcasp->pdir);
			break;
		default:
			dev_err(mcasp->dev, "Invalid clk id: %d\n", clk_id);
			goto out;
		}
	} else {
		/* Select AUXCLK as HCLK */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, AHCLKXE);
		mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, AHCLKRE);
		set_bit(PIN_BIT_AHCLKX, &mcasp->pdir);
	}
	/*
	 * When AHCLK X/R is selected to be output it means that the HCLK is
	 * the same clock - coming via AUXCLK.
	 */
	mcasp->sysclk_freq = freq;
out:
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

	for (i = 1; i <= slots; i++)
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

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		return 0;

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
	u32 tx_rotate, rx_rotate, slot_width;
	u32 mask = (1ULL << sample_width) - 1;

	if (mcasp->slot_width)
		slot_width = mcasp->slot_width;
	else if (mcasp->max_format_width)
		slot_width = mcasp->max_format_width;
	else
		slot_width = sample_width;
	/*
	 * TX rotation:
	 * right aligned formats: rotate w/ slot_width
	 * left aligned formats: rotate w/ sample_width
	 *
	 * RX rotation:
	 * right aligned formats: no rotation needed
	 * left aligned formats: rotate w/ (slot_width - sample_width)
	 */
	if ((mcasp->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) ==
	    SND_SOC_DAIFMT_RIGHT_J) {
		tx_rotate = (slot_width / 4) & 0x7;
		rx_rotate = 0;
	} else {
		tx_rotate = (sample_width / 4) & 0x7;
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
	} else {
		/*
		 * according to the TRM it should be TXROT=0, this one works:
		 * 16 bit to 23-8 (TXROT=6, rotate 24 bits)
		 * 24 bit to 23-0 (TXROT=0, rotate 0 bits)
		 *
		 * TXROT = 0 only works with 24bit samples
		 */
		tx_rotate = (sample_width / 4 + 2) & 0x7;

		mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXROT(tx_rotate),
			       TXROT(7));
		mcasp_mod_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXSSZ(15),
			       TXSSZ(0x0F));
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
	u8 max_active_serializers, max_rx_serializers, max_tx_serializers;
	int active_serializers, numevt;
	u32 reg;

	/* In DIT mode we only allow maximum of one serializers for now */
	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		max_active_serializers = 1;
	else
		max_active_serializers = DIV_ROUND_UP(channels, slots);

	/* Default configuration */
	if (mcasp->version < MCASP_VERSION_3)
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PWREMUMGT_REG, MCASP_SOFT);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_TXSTAT_REG, 0xFFFFFFFF);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_XEVTCTL_REG, TXDATADMADIS);
		max_tx_serializers = max_active_serializers;
		max_rx_serializers =
			mcasp->active_serializers[SNDRV_PCM_STREAM_CAPTURE];
	} else {
		mcasp_set_reg(mcasp, DAVINCI_MCASP_RXSTAT_REG, 0xFFFFFFFF);
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_REVTCTL_REG, RXDATADMADIS);
		max_tx_serializers =
			mcasp->active_serializers[SNDRV_PCM_STREAM_PLAYBACK];
		max_rx_serializers = max_active_serializers;
	}

	for (i = 0; i < mcasp->num_serializer; i++) {
		mcasp_set_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
			       mcasp->serial_dir[i]);
		if (mcasp->serial_dir[i] == TX_MODE &&
					tx_ser < max_tx_serializers) {
			mcasp_mod_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
				       mcasp->dismod, DISMOD_MASK);
			set_bit(PIN_BIT_AXR(i), &mcasp->pdir);
			tx_ser++;
		} else if (mcasp->serial_dir[i] == RX_MODE &&
					rx_ser < max_rx_serializers) {
			clear_bit(PIN_BIT_AXR(i), &mcasp->pdir);
			rx_ser++;
		} else {
			/* Inactive or unused pin, set it to inactive */
			mcasp_mod_bits(mcasp, DAVINCI_MCASP_XRSRCTL_REG(i),
				       SRMOD_INACTIVE, SRMOD_MASK);
			/* If unused, set DISMOD for the pin */
			if (mcasp->serial_dir[i] != INACTIVE_MODE)
				mcasp_mod_bits(mcasp,
					       DAVINCI_MCASP_XRSRCTL_REG(i),
					       mcasp->dismod, DISMOD_MASK);
			clear_bit(PIN_BIT_AXR(i), &mcasp->pdir);
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

		goto out;
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

out:
	mcasp->active_serializers[stream] = active_serializers;

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
		active_serializers = DIV_ROUND_UP(channels, active_slots);
		if (active_serializers == 1)
			active_slots = channels;
		for (i = 0; i < total_slots; i++) {
			if ((1 << i) & mcasp->tdm_mask[stream]) {
				mask |= (1 << i);
				if (--active_slots <= 0)
					break;
			}
		}
	} else {
		active_serializers = DIV_ROUND_UP(channels, total_slots);
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
	u8 *cs_bytes = (u8 *)&mcasp->iec958_status;

	if (!mcasp->dat_port)
		mcasp_set_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXSEL);
	else
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_TXFMT_REG, TXSEL);

	/* Set TX frame synch : DIT Mode, 1 bit width, internal, rising edge */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXFMCTL_REG, AFSXE | FSXMOD(0x180));

	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXMASK_REG, 0xFFFF);

	/* Set the TX tdm : for all the slots */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_TXTDM_REG, 0xFFFFFFFF);

	/* Set the TX clock controls : div = 1 and internal */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ACLKXE | TX_ASYNC);

	mcasp_clr_bits(mcasp, DAVINCI_MCASP_XEVTCTL_REG, TXDATADMADIS);

	/* Set S/PDIF channel status bits */
	cs_bytes[3] &= ~IEC958_AES3_CON_FS;
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
		dev_err(mcasp->dev, "unsupported sampling rate: %d\n", rate);
		return -EINVAL;
	}

	mcasp_set_reg(mcasp, DAVINCI_MCASP_DITCSRA_REG, mcasp->iec958_status);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_DITCSRB_REG, mcasp->iec958_status);

	/* Enable the DIT */
	mcasp_set_bits(mcasp, DAVINCI_MCASP_TXDITCTL_REG, DITEN);

	return 0;
}

static int davinci_mcasp_calc_clk_div(struct davinci_mcasp *mcasp,
				      unsigned int sysclk_freq,
				      unsigned int bclk_freq, bool set)
{
	u32 reg = mcasp_get_reg(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG);
	int div = sysclk_freq / bclk_freq;
	int rem = sysclk_freq % bclk_freq;
	int error_ppm;
	int aux_div = 1;

	if (div > (ACLKXDIV_MASK + 1)) {
		if (reg & AHCLKXE) {
			aux_div = div / (ACLKXDIV_MASK + 1);
			if (div % (ACLKXDIV_MASK + 1))
				aux_div++;

			sysclk_freq /= aux_div;
			div = sysclk_freq / bclk_freq;
			rem = sysclk_freq % bclk_freq;
		} else if (set) {
			dev_warn(mcasp->dev, "Too fast reference clock (%u)\n",
				 sysclk_freq);
		}
	}

	if (rem != 0) {
		if (div == 0 ||
		    ((sysclk_freq / div) - bclk_freq) >
		    (bclk_freq - (sysclk_freq / (div+1)))) {
			div++;
			rem = rem - bclk_freq;
		}
	}
	error_ppm = (div*1000000 + (int)div64_long(1000000LL*rem,
		     (int)bclk_freq)) / div - 1000000;

	if (set) {
		if (error_ppm)
			dev_info(mcasp->dev, "Sample-rate is off by %d PPM\n",
				 error_ppm);

		__davinci_mcasp_set_clkdiv(mcasp, MCASP_CLKDIV_BCLK, div, 0);
		if (reg & AHCLKXE)
			__davinci_mcasp_set_clkdiv(mcasp, MCASP_CLKDIV_AUXCLK,
						   aux_div, 0);
	}

	return error_ppm;
}

static inline u32 davinci_mcasp_tx_delay(struct davinci_mcasp *mcasp)
{
	if (!mcasp->txnumevt)
		return 0;

	return mcasp_get_reg(mcasp, mcasp->fifo_base + MCASP_WFIFOSTS_OFFSET);
}

static inline u32 davinci_mcasp_rx_delay(struct davinci_mcasp *mcasp)
{
	if (!mcasp->rxnumevt)
		return 0;

	return mcasp_get_reg(mcasp, mcasp->fifo_base + MCASP_RFIFOSTS_OFFSET);
}

static snd_pcm_sframes_t davinci_mcasp_delay(
			struct snd_pcm_substream *substream,
			struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	u32 fifo_use;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		fifo_use = davinci_mcasp_tx_delay(mcasp);
	else
		fifo_use = davinci_mcasp_rx_delay(mcasp);

	/*
	 * Divide the used locations with the channel count to get the
	 * FIFO usage in samples (don't care about partial samples in the
	 * buffer).
	 */
	return fifo_use / substream->runtime->channels;
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

	ret = davinci_mcasp_set_dai_fmt(cpu_dai, mcasp->dai_fmt);
	if (ret)
		return ret;

	/*
	 * If mcasp is BCLK master, and a BCLK divider was not provided by
	 * the machine driver, we need to calculate the ratio.
	 */
	if (mcasp->bclk_master && mcasp->bclk_div == 0 && mcasp->sysclk_freq) {
		int slots = mcasp->tdm_slots;
		int rate = params_rate(params);
		int sbits = params_width(params);
		unsigned int bclk_target;

		if (mcasp->slot_width)
			sbits = mcasp->slot_width;

		if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE)
			bclk_target = rate * sbits * slots;
		else
			bclk_target = rate * 128;

		davinci_mcasp_calc_clk_div(mcasp, mcasp->sysclk_freq,
					   bclk_target, true);
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

	davinci_config_channel_size(mcasp, word_length);

	if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE) {
		mcasp->channels = channels;
		if (!mcasp->max_format_width)
			mcasp->max_format_width = word_length;
	}

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

static int davinci_mcasp_hw_rule_slot_width(struct snd_pcm_hw_params *params,
					    struct snd_pcm_hw_rule *rule)
{
	struct davinci_mcasp_ruledata *rd = rule->private;
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_mask nfmt;
	int i, slot_width;

	snd_mask_none(&nfmt);
	slot_width = rd->mcasp->slot_width;

	for (i = 0; i <= SNDRV_PCM_FORMAT_LAST; i++) {
		if (snd_mask_test(fmt, i)) {
			if (snd_pcm_format_width(i) <= slot_width) {
				snd_mask_set(&nfmt, i);
			}
		}
	}

	return snd_mask_refine(fmt, &nfmt);
}

static int davinci_mcasp_hw_rule_format_width(struct snd_pcm_hw_params *params,
					      struct snd_pcm_hw_rule *rule)
{
	struct davinci_mcasp_ruledata *rd = rule->private;
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	struct snd_mask nfmt;
	int i, format_width;

	snd_mask_none(&nfmt);
	format_width = rd->mcasp->max_format_width;

	for (i = 0; i <= SNDRV_PCM_FORMAT_LAST; i++) {
		if (snd_mask_test(fmt, i)) {
			if (snd_pcm_format_width(i) == format_width) {
				snd_mask_set(&nfmt, i);
			}
		}
	}

	return snd_mask_refine(fmt, &nfmt);
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
			uint bclk_freq = sbits * slots *
					 davinci_mcasp_dai_rates[i];
			unsigned int sysclk_freq;
			int ppm;

			if (rd->mcasp->auxclk_fs_ratio)
				sysclk_freq =  davinci_mcasp_dai_rates[i] *
					       rd->mcasp->auxclk_fs_ratio;
			else
				sysclk_freq = rd->mcasp->sysclk_freq;

			ppm = davinci_mcasp_calc_clk_div(rd->mcasp, sysclk_freq,
							 bclk_freq, false);
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

	for (i = 0; i <= SNDRV_PCM_FORMAT_LAST; i++) {
		if (snd_mask_test(fmt, i)) {
			uint sbits = snd_pcm_format_width(i);
			unsigned int sysclk_freq;
			int ppm;

			if (rd->mcasp->auxclk_fs_ratio)
				sysclk_freq =  rate *
					       rd->mcasp->auxclk_fs_ratio;
			else
				sysclk_freq = rd->mcasp->sysclk_freq;

			if (rd->mcasp->slot_width)
				sbits = rd->mcasp->slot_width;

			ppm = davinci_mcasp_calc_clk_div(rd->mcasp, sysclk_freq,
							 sbits * slots * rate,
							 false);
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

static int davinci_mcasp_hw_rule_min_periodsize(
		struct snd_pcm_hw_params *params, struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *period_size = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
	struct snd_interval frames;

	snd_interval_any(&frames);
	frames.min = 64;
	frames.integer = 1;

	return snd_interval_refine(period_size, &frames);
}

static int davinci_mcasp_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);
	struct davinci_mcasp_ruledata *ruledata =
					&mcasp->ruledata[substream->stream];
	u32 max_channels = 0;
	int i, dir, ret;
	int tdm_slots = mcasp->tdm_slots;

	/* Do not allow more then one stream per direction */
	if (mcasp->substreams[substream->stream])
		return -EBUSY;

	mcasp->substreams[substream->stream] = substream;

	if (mcasp->tdm_mask[substream->stream])
		tdm_slots = hweight32(mcasp->tdm_mask[substream->stream]);

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
	ruledata->mcasp = mcasp;
	max_channels *= tdm_slots;
	/*
	 * If the already active stream has less channels than the calculated
	 * limit based on the seirializers * tdm_slots, and only one serializer
	 * is in use we need to use that as a constraint for the second stream.
	 * Otherwise (first stream or less allowed channels or more than one
	 * serializer in use) we use the calculated constraint.
	 */
	if (mcasp->channels && mcasp->channels < max_channels &&
	    ruledata->serializers == 1)
		max_channels = mcasp->channels;
	/*
	 * But we can always allow channels upto the amount of
	 * the available tdm_slots.
	 */
	if (max_channels < tdm_slots)
		max_channels = tdm_slots;

	snd_pcm_hw_constraint_minmax(substream->runtime,
				     SNDRV_PCM_HW_PARAM_CHANNELS,
				     0, max_channels);

	snd_pcm_hw_constraint_list(substream->runtime,
				   0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &mcasp->chconstr[substream->stream]);

	if (mcasp->max_format_width) {
		/*
		 * Only allow formats which require same amount of bits on the
		 * bus as the currently running stream
		 */
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_FORMAT,
					  davinci_mcasp_hw_rule_format_width,
					  ruledata,
					  SNDRV_PCM_HW_PARAM_FORMAT, -1);
		if (ret)
			return ret;
	}
	else if (mcasp->slot_width) {
		/* Only allow formats require <= slot_width bits on the bus */
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_FORMAT,
					  davinci_mcasp_hw_rule_slot_width,
					  ruledata,
					  SNDRV_PCM_HW_PARAM_FORMAT, -1);
		if (ret)
			return ret;
	}

	/*
	 * If we rely on implicit BCLK divider setting we should
	 * set constraints based on what we can provide.
	 */
	if (mcasp->bclk_master && mcasp->bclk_div == 0 && mcasp->sysclk_freq) {
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

	snd_pcm_hw_rule_add(substream->runtime, 0,
			    SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
			    davinci_mcasp_hw_rule_min_periodsize, NULL,
			    SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);

	return 0;
}

static void davinci_mcasp_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *cpu_dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);

	mcasp->substreams[substream->stream] = NULL;
	mcasp->active_serializers[substream->stream] = 0;

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE)
		return;

	if (!snd_soc_dai_active(cpu_dai)) {
		mcasp->channels = 0;
		mcasp->max_format_width = 0;
	}
}

static const struct snd_soc_dai_ops davinci_mcasp_dai_ops = {
	.startup	= davinci_mcasp_startup,
	.shutdown	= davinci_mcasp_shutdown,
	.trigger	= davinci_mcasp_trigger,
	.delay		= davinci_mcasp_delay,
	.hw_params	= davinci_mcasp_hw_params,
	.set_fmt	= davinci_mcasp_set_dai_fmt,
	.set_clkdiv	= davinci_mcasp_set_clkdiv,
	.set_sysclk	= davinci_mcasp_set_sysclk,
	.set_tdm_slot	= davinci_mcasp_set_tdm_slot,
};

static int davinci_mcasp_iec958_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int davinci_mcasp_iec958_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uctl)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);

	memcpy(uctl->value.iec958.status, &mcasp->iec958_status,
	       sizeof(mcasp->iec958_status));

	return 0;
}

static int davinci_mcasp_iec958_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *uctl)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);

	memcpy(&mcasp->iec958_status, uctl->value.iec958.status,
	       sizeof(mcasp->iec958_status));

	return 0;
}

static int davinci_mcasp_iec958_con_mask_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(cpu_dai);

	memset(ucontrol->value.iec958.status, 0xff, sizeof(mcasp->iec958_status));
	return 0;
}

static const struct snd_kcontrol_new davinci_mcasp_iec958_ctls[] = {
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_VOLATILE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = davinci_mcasp_iec958_info,
		.get = davinci_mcasp_iec958_get,
		.put = davinci_mcasp_iec958_put,
	}, {
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info = davinci_mcasp_iec958_info,
		.get = davinci_mcasp_iec958_con_mask_get,
	},
};

static void davinci_mcasp_init_iec958_status(struct davinci_mcasp *mcasp)
{
	unsigned char *cs = (u8 *)&mcasp->iec958_status;

	cs[0] = IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS_NONE;
	cs[1] = IEC958_AES1_CON_PCM_CODER;
	cs[2] = IEC958_AES2_CON_SOURCE_UNSPEC | IEC958_AES2_CON_CHANNEL_UNSPEC;
	cs[3] = IEC958_AES3_CON_CLOCK_1000PPM;
}

static int davinci_mcasp_dai_probe(struct snd_soc_dai *dai)
{
	struct davinci_mcasp *mcasp = snd_soc_dai_get_drvdata(dai);
	int stream;

	for_each_pcm_streams(stream)
		snd_soc_dai_dma_data_set(dai, stream, &mcasp->dma_data[stream]);

	if (mcasp->op_mode == DAVINCI_MCASP_DIT_MODE) {
		davinci_mcasp_init_iec958_status(mcasp);
		snd_soc_add_dai_controls(dai, davinci_mcasp_iec958_ctls,
					 ARRAY_SIZE(davinci_mcasp_iec958_ctls));
	}

	return 0;
}

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
		.playback	= {
			.stream_name = "IIS Playback",
			.channels_min	= 1,
			.channels_max	= 32 * 16,
			.rates 		= DAVINCI_MCASP_RATES,
			.formats	= DAVINCI_MCASP_PCM_FMTS,
		},
		.capture 	= {
			.stream_name = "IIS Capture",
			.channels_min 	= 1,
			.channels_max	= 32 * 16,
			.rates 		= DAVINCI_MCASP_RATES,
			.formats	= DAVINCI_MCASP_PCM_FMTS,
		},
		.ops 		= &davinci_mcasp_dai_ops,

		.symmetric_rate		= 1,
	},
	{
		.name		= "davinci-mcasp.1",
		.probe		= davinci_mcasp_dai_probe,
		.playback 	= {
			.stream_name = "DIT Playback",
			.channels_min	= 1,
			.channels_max	= 384,
			.rates		= DAVINCI_MCASP_RATES,
			.formats	= SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops 		= &davinci_mcasp_dai_ops,
	},

};

static const struct snd_soc_component_driver davinci_mcasp_component = {
	.name			= "davinci-mcasp",
	.legacy_dai_naming	= 1,
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
	/* The CFG port offset will be calculated if it is needed */
	.tx_dma_offset = 0,
	.rx_dma_offset = 0,
	.version = MCASP_VERSION_4,
};

static struct davinci_mcasp_pdata omap_mcasp_pdata = {
	.tx_dma_offset = 0x200,
	.rx_dma_offset = 0,
	.version = MCASP_VERSION_OMAP,
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
	{
		.compatible = "ti,omap4-mcasp-audio",
		.data = &omap_mcasp_pdata,
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

	dev_warn(&pdev->dev, "Update the bindings to use assigned-clocks!\n");

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

static bool davinci_mcasp_have_gpiochip(struct davinci_mcasp *mcasp)
{
#ifdef CONFIG_OF_GPIO
	return of_property_read_bool(mcasp->dev->of_node, "gpio-controller");
#else
	return false;
#endif
}

static int davinci_mcasp_get_config(struct davinci_mcasp *mcasp,
				    struct platform_device *pdev)
{
	const struct of_device_id *match = of_match_device(mcasp_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct davinci_mcasp_pdata *pdata = NULL;
	const u32 *of_serial_dir32;
	u32 val;
	int i;

	if (pdev->dev.platform_data) {
		pdata = pdev->dev.platform_data;
		pdata->dismod = DISMOD_LOW;
		goto out;
	} else if (match) {
		pdata = devm_kmemdup(&pdev->dev, match->data, sizeof(*pdata),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
	} else {
		dev_err(&pdev->dev, "No compatible match found\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "op-mode", &val) == 0) {
		pdata->op_mode = val;
	} else {
		mcasp->missing_audio_param = true;
		goto out;
	}

	if (of_property_read_u32(np, "tdm-slots", &val) == 0) {
		if (val < 2 || val > 32) {
			dev_err(&pdev->dev, "tdm-slots must be in rage [2-32]\n");
			return -EINVAL;
		}

		pdata->tdm_slots = val;
	} else if (pdata->op_mode == DAVINCI_MCASP_IIS_MODE) {
		mcasp->missing_audio_param = true;
		goto out;
	}

	of_serial_dir32 = of_get_property(np, "serial-dir", &val);
	val /= sizeof(u32);
	if (of_serial_dir32) {
		u8 *of_serial_dir = devm_kzalloc(&pdev->dev,
						 (sizeof(*of_serial_dir) * val),
						 GFP_KERNEL);
		if (!of_serial_dir)
			return -ENOMEM;

		for (i = 0; i < val; i++)
			of_serial_dir[i] = be32_to_cpup(&of_serial_dir32[i]);

		pdata->num_serializer = val;
		pdata->serial_dir = of_serial_dir;
	} else {
		mcasp->missing_audio_param = true;
		goto out;
	}

	if (of_property_read_u32(np, "tx-num-evt", &val) == 0)
		pdata->txnumevt = val;

	if (of_property_read_u32(np, "rx-num-evt", &val) == 0)
		pdata->rxnumevt = val;

	if (of_property_read_u32(np, "auxclk-fs-ratio", &val) == 0)
		mcasp->auxclk_fs_ratio = val;

	if (of_property_read_u32(np, "dismod", &val) == 0) {
		if (val == 0 || val == 2 || val == 3) {
			pdata->dismod = DISMOD_VAL(val);
		} else {
			dev_warn(&pdev->dev, "Invalid dismod value: %u\n", val);
			pdata->dismod = DISMOD_LOW;
		}
	} else {
		pdata->dismod = DISMOD_LOW;
	}

out:
	mcasp->pdata = pdata;

	if (mcasp->missing_audio_param) {
		if (davinci_mcasp_have_gpiochip(mcasp)) {
			dev_dbg(&pdev->dev, "Missing DT parameter(s) for audio\n");
			return 0;
		}

		dev_err(&pdev->dev, "Insufficient DT parameter(s)\n");
		return -ENODEV;
	}

	mcasp->op_mode = pdata->op_mode;
	/* sanity check for tdm slots parameter */
	if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE) {
		if (pdata->tdm_slots < 2) {
			dev_warn(&pdev->dev, "invalid tdm slots: %d\n",
				 pdata->tdm_slots);
			mcasp->tdm_slots = 2;
		} else if (pdata->tdm_slots > 32) {
			dev_warn(&pdev->dev, "invalid tdm slots: %d\n",
				 pdata->tdm_slots);
			mcasp->tdm_slots = 32;
		} else {
			mcasp->tdm_slots = pdata->tdm_slots;
		}
	} else {
		mcasp->tdm_slots = 32;
	}

	mcasp->num_serializer = pdata->num_serializer;
#ifdef CONFIG_PM
	mcasp->context.xrsr_regs = devm_kcalloc(&pdev->dev,
						mcasp->num_serializer, sizeof(u32),
						GFP_KERNEL);
	if (!mcasp->context.xrsr_regs)
		return -ENOMEM;
#endif
	mcasp->serial_dir = pdata->serial_dir;
	mcasp->version = pdata->version;
	mcasp->txnumevt = pdata->txnumevt;
	mcasp->rxnumevt = pdata->rxnumevt;
	mcasp->dismod = pdata->dismod;

	return 0;
}

enum {
	PCM_EDMA,
	PCM_SDMA,
	PCM_UDMA,
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
	chan = dma_request_chan(mcasp->dev, tmp);
	if (IS_ERR(chan))
		return dev_err_probe(mcasp->dev, PTR_ERR(chan),
				     "Can't verify DMA configuration\n");
	if (WARN_ON(!chan->device || !chan->device->dev)) {
		dma_release_channel(chan);
		return -EINVAL;
	}

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
	else if (strstr(tmp, "udmap"))
		return PCM_UDMA;
	else if (strstr(tmp, "bcdma"))
		return PCM_UDMA;

	return PCM_EDMA;
}

static u32 davinci_mcasp_txdma_offset(struct davinci_mcasp_pdata *pdata)
{
	int i;
	u32 offset = 0;

	if (pdata->version != MCASP_VERSION_4)
		return pdata->tx_dma_offset;

	for (i = 0; i < pdata->num_serializer; i++) {
		if (pdata->serial_dir[i] == TX_MODE) {
			if (!offset) {
				offset = DAVINCI_MCASP_TXBUF_REG(i);
			} else {
				pr_err("%s: Only one serializer allowed!\n",
				       __func__);
				break;
			}
		}
	}

	return offset;
}

static u32 davinci_mcasp_rxdma_offset(struct davinci_mcasp_pdata *pdata)
{
	int i;
	u32 offset = 0;

	if (pdata->version != MCASP_VERSION_4)
		return pdata->rx_dma_offset;

	for (i = 0; i < pdata->num_serializer; i++) {
		if (pdata->serial_dir[i] == RX_MODE) {
			if (!offset) {
				offset = DAVINCI_MCASP_RXBUF_REG(i);
			} else {
				pr_err("%s: Only one serializer allowed!\n",
				       __func__);
				break;
			}
		}
	}

	return offset;
}

#ifdef CONFIG_GPIOLIB
static int davinci_mcasp_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);

	if (mcasp->num_serializer && offset < mcasp->num_serializer &&
	    mcasp->serial_dir[offset] != INACTIVE_MODE) {
		dev_err(mcasp->dev, "AXR%u pin is  used for audio\n", offset);
		return -EBUSY;
	}

	/* Do not change the PIN yet */
	return pm_runtime_resume_and_get(mcasp->dev);
}

static void davinci_mcasp_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);

	/* Set the direction to input */
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(offset));

	/* Set the pin as McASP pin */
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_PFUNC_REG, BIT(offset));

	pm_runtime_put_sync(mcasp->dev);
}

static int davinci_mcasp_gpio_direction_out(struct gpio_chip *chip,
					    unsigned offset, int value)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);
	u32 val;

	if (value)
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDOUT_REG, BIT(offset));
	else
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDOUT_REG, BIT(offset));

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_PFUNC_REG);
	if (!(val & BIT(offset))) {
		/* Set the pin as GPIO pin */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PFUNC_REG, BIT(offset));

		/* Set the direction to output */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(offset));
	}

	return 0;
}

static void davinci_mcasp_gpio_set(struct gpio_chip *chip, unsigned offset,
				  int value)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);

	if (value)
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PDOUT_REG, BIT(offset));
	else
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDOUT_REG, BIT(offset));
}

static int davinci_mcasp_gpio_direction_in(struct gpio_chip *chip,
					   unsigned offset)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);
	u32 val;

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_PFUNC_REG);
	if (!(val & BIT(offset))) {
		/* Set the direction to input */
		mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, BIT(offset));

		/* Set the pin as GPIO pin */
		mcasp_set_bits(mcasp, DAVINCI_MCASP_PFUNC_REG, BIT(offset));
	}

	return 0;
}

static int davinci_mcasp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);
	u32 val;

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_PDSET_REG);
	if (val & BIT(offset))
		return 1;

	return 0;
}

static int davinci_mcasp_gpio_get_direction(struct gpio_chip *chip,
					    unsigned offset)
{
	struct davinci_mcasp *mcasp = gpiochip_get_data(chip);
	u32 val;

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_PDIR_REG);
	if (val & BIT(offset))
		return 0;

	return 1;
}

static const struct gpio_chip davinci_mcasp_template_chip = {
	.owner			= THIS_MODULE,
	.request		= davinci_mcasp_gpio_request,
	.free			= davinci_mcasp_gpio_free,
	.direction_output	= davinci_mcasp_gpio_direction_out,
	.set			= davinci_mcasp_gpio_set,
	.direction_input	= davinci_mcasp_gpio_direction_in,
	.get			= davinci_mcasp_gpio_get,
	.get_direction		= davinci_mcasp_gpio_get_direction,
	.base			= -1,
	.ngpio			= 32,
};

static int davinci_mcasp_init_gpiochip(struct davinci_mcasp *mcasp)
{
	if (!davinci_mcasp_have_gpiochip(mcasp))
		return 0;

	mcasp->gpio_chip = davinci_mcasp_template_chip;
	mcasp->gpio_chip.label = dev_name(mcasp->dev);
	mcasp->gpio_chip.parent = mcasp->dev;

	return devm_gpiochip_add_data(mcasp->dev, &mcasp->gpio_chip, mcasp);
}

#else /* CONFIG_GPIOLIB */
static inline int davinci_mcasp_init_gpiochip(struct davinci_mcasp *mcasp)
{
	return 0;
}
#endif /* CONFIG_GPIOLIB */

static int davinci_mcasp_probe(struct platform_device *pdev)
{
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct resource *mem, *dat;
	struct davinci_mcasp *mcasp;
	char *irq_name;
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

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!mem) {
		dev_warn(&pdev->dev,
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

	dev_set_drvdata(&pdev->dev, mcasp);
	pm_runtime_enable(&pdev->dev);

	mcasp->dev = &pdev->dev;
	ret = davinci_mcasp_get_config(mcasp, pdev);
	if (ret)
		goto err;

	/* All PINS as McASP */
	pm_runtime_get_sync(mcasp->dev);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PFUNC_REG, 0x00000000);
	pm_runtime_put(mcasp->dev);

	/* Skip audio related setup code if the configuration is not adequat */
	if (mcasp->missing_audio_param)
		goto no_audio;

	irq = platform_get_irq_byname_optional(pdev, "common");
	if (irq > 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_common",
					  dev_name(&pdev->dev));
		if (!irq_name) {
			ret = -ENOMEM;
			goto err;
		}
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

	irq = platform_get_irq_byname_optional(pdev, "rx");
	if (irq > 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_rx",
					  dev_name(&pdev->dev));
		if (!irq_name) {
			ret = -ENOMEM;
			goto err;
		}
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						davinci_mcasp_rx_irq_handler,
						IRQF_ONESHOT, irq_name, mcasp);
		if (ret) {
			dev_err(&pdev->dev, "RX IRQ request failed\n");
			goto err;
		}

		mcasp->irq_request[SNDRV_PCM_STREAM_CAPTURE] = ROVRN;
	}

	irq = platform_get_irq_byname_optional(pdev, "tx");
	if (irq > 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_tx",
					  dev_name(&pdev->dev));
		if (!irq_name) {
			ret = -ENOMEM;
			goto err;
		}
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
	dma_data->filter_data = "tx";
	if (dat) {
		dma_data->addr = dat->start;
		/*
		 * According to the TRM there should be 0x200 offset added to
		 * the DAT port address
		 */
		if (mcasp->version == MCASP_VERSION_OMAP)
			dma_data->addr += davinci_mcasp_txdma_offset(mcasp->pdata);
	} else {
		dma_data->addr = mem->start + davinci_mcasp_txdma_offset(mcasp->pdata);
	}


	/* RX is not valid in DIT mode */
	if (mcasp->op_mode != DAVINCI_MCASP_DIT_MODE) {
		dma_data = &mcasp->dma_data[SNDRV_PCM_STREAM_CAPTURE];
		dma_data->filter_data = "rx";
		if (dat)
			dma_data->addr = dat->start;
		else
			dma_data->addr =
				mem->start + davinci_mcasp_rxdma_offset(mcasp->pdata);
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
		devm_kcalloc(mcasp->dev,
			     32 + mcasp->num_serializer - 1,
			     sizeof(unsigned int),
			     GFP_KERNEL);

	mcasp->chconstr[SNDRV_PCM_STREAM_CAPTURE].list =
		devm_kcalloc(mcasp->dev,
			     32 + mcasp->num_serializer - 1,
			     sizeof(unsigned int),
			     GFP_KERNEL);

	if (!mcasp->chconstr[SNDRV_PCM_STREAM_PLAYBACK].list ||
	    !mcasp->chconstr[SNDRV_PCM_STREAM_CAPTURE].list) {
		ret = -ENOMEM;
		goto err;
	}

	ret = davinci_mcasp_set_ch_constraints(mcasp);
	if (ret)
		goto err;

	mcasp_reparent_fck(pdev);

	ret = devm_snd_soc_register_component(&pdev->dev, &davinci_mcasp_component,
					      &davinci_mcasp_dai[mcasp->op_mode], 1);

	if (ret != 0)
		goto err;

	ret = davinci_mcasp_get_dma_type(mcasp);
	switch (ret) {
	case PCM_EDMA:
		ret = edma_pcm_platform_register(&pdev->dev);
		break;
	case PCM_SDMA:
		if (mcasp->op_mode == DAVINCI_MCASP_IIS_MODE)
			ret = sdma_pcm_platform_register(&pdev->dev, "tx", "rx");
		else
			ret = sdma_pcm_platform_register(&pdev->dev, "tx", NULL);
		break;
	case PCM_UDMA:
		ret = udma_pcm_platform_register(&pdev->dev);
		break;
	default:
		dev_err(&pdev->dev, "No DMA controller found (%d)\n", ret);
		fallthrough;
	case -EPROBE_DEFER:
		goto err;
	}

	if (ret) {
		dev_err(&pdev->dev, "register PCM failed: %d\n", ret);
		goto err;
	}

no_audio:
	ret = davinci_mcasp_init_gpiochip(mcasp);
	if (ret) {
		dev_err(&pdev->dev, "gpiochip registration failed: %d\n", ret);
		goto err;
	}

	return 0;
err:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static void davinci_mcasp_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM
static int davinci_mcasp_runtime_suspend(struct device *dev)
{
	struct davinci_mcasp *mcasp = dev_get_drvdata(dev);
	struct davinci_mcasp_context *context = &mcasp->context;
	u32 reg;
	int i;

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

	return 0;
}

static int davinci_mcasp_runtime_resume(struct device *dev)
{
	struct davinci_mcasp *mcasp = dev_get_drvdata(dev);
	struct davinci_mcasp_context *context = &mcasp->context;
	u32 reg;
	int i;

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

	return 0;
}

#endif

static const struct dev_pm_ops davinci_mcasp_pm_ops = {
	SET_RUNTIME_PM_OPS(davinci_mcasp_runtime_suspend,
			   davinci_mcasp_runtime_resume,
			   NULL)
};

static struct platform_driver davinci_mcasp_driver = {
	.probe		= davinci_mcasp_probe,
	.remove_new	= davinci_mcasp_remove,
	.driver		= {
		.name	= "davinci-mcasp",
		.pm     = &davinci_mcasp_pm_ops,
		.of_match_table = mcasp_dt_ids,
	},
};

module_platform_driver(davinci_mcasp_driver);

MODULE_AUTHOR("Steve Chen");
MODULE_DESCRIPTION("TI DAVINCI McASP SoC Interface");
MODULE_LICENSE("GPL");
