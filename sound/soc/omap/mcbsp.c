/*
 * sound/soc/omap/mcbsp.c
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <plat/mcbsp.h>

#include "mcbsp.h"

static void omap_mcbsp_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	void __iomem *addr = mcbsp->io_base + reg * mcbsp->pdata->reg_step;

	if (mcbsp->pdata->reg_size == 2) {
		((u16 *)mcbsp->reg_cache)[reg] = (u16)val;
		__raw_writew((u16)val, addr);
	} else {
		((u32 *)mcbsp->reg_cache)[reg] = val;
		__raw_writel(val, addr);
	}
}

static int omap_mcbsp_read(struct omap_mcbsp *mcbsp, u16 reg, bool from_cache)
{
	void __iomem *addr = mcbsp->io_base + reg * mcbsp->pdata->reg_step;

	if (mcbsp->pdata->reg_size == 2) {
		return !from_cache ? __raw_readw(addr) :
				     ((u16 *)mcbsp->reg_cache)[reg];
	} else {
		return !from_cache ? __raw_readl(addr) :
				     ((u32 *)mcbsp->reg_cache)[reg];
	}
}

static void omap_mcbsp_st_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	__raw_writel(val, mcbsp->st_data->io_base_st + reg);
}

static int omap_mcbsp_st_read(struct omap_mcbsp *mcbsp, u16 reg)
{
	return __raw_readl(mcbsp->st_data->io_base_st + reg);
}

#define MCBSP_READ(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 0)
#define MCBSP_WRITE(mcbsp, reg, val) \
		omap_mcbsp_write(mcbsp, OMAP_MCBSP_REG_##reg, val)
#define MCBSP_READ_CACHE(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 1)

#define MCBSP_ST_READ(mcbsp, reg) \
			omap_mcbsp_st_read(mcbsp, OMAP_ST_REG_##reg)
#define MCBSP_ST_WRITE(mcbsp, reg, val) \
			omap_mcbsp_st_write(mcbsp, OMAP_ST_REG_##reg, val)

static void omap_mcbsp_dump_reg(struct omap_mcbsp *mcbsp)
{
	dev_dbg(mcbsp->dev, "**** McBSP%d regs ****\n", mcbsp->id);
	dev_dbg(mcbsp->dev, "DRR2:  0x%04x\n",
			MCBSP_READ(mcbsp, DRR2));
	dev_dbg(mcbsp->dev, "DRR1:  0x%04x\n",
			MCBSP_READ(mcbsp, DRR1));
	dev_dbg(mcbsp->dev, "DXR2:  0x%04x\n",
			MCBSP_READ(mcbsp, DXR2));
	dev_dbg(mcbsp->dev, "DXR1:  0x%04x\n",
			MCBSP_READ(mcbsp, DXR1));
	dev_dbg(mcbsp->dev, "SPCR2: 0x%04x\n",
			MCBSP_READ(mcbsp, SPCR2));
	dev_dbg(mcbsp->dev, "SPCR1: 0x%04x\n",
			MCBSP_READ(mcbsp, SPCR1));
	dev_dbg(mcbsp->dev, "RCR2:  0x%04x\n",
			MCBSP_READ(mcbsp, RCR2));
	dev_dbg(mcbsp->dev, "RCR1:  0x%04x\n",
			MCBSP_READ(mcbsp, RCR1));
	dev_dbg(mcbsp->dev, "XCR2:  0x%04x\n",
			MCBSP_READ(mcbsp, XCR2));
	dev_dbg(mcbsp->dev, "XCR1:  0x%04x\n",
			MCBSP_READ(mcbsp, XCR1));
	dev_dbg(mcbsp->dev, "SRGR2: 0x%04x\n",
			MCBSP_READ(mcbsp, SRGR2));
	dev_dbg(mcbsp->dev, "SRGR1: 0x%04x\n",
			MCBSP_READ(mcbsp, SRGR1));
	dev_dbg(mcbsp->dev, "PCR0:  0x%04x\n",
			MCBSP_READ(mcbsp, PCR0));
	dev_dbg(mcbsp->dev, "***********************\n");
}

static irqreturn_t omap_mcbsp_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp = dev_id;
	u16 irqst;

	irqst = MCBSP_READ(mcbsp, IRQST);
	dev_dbg(mcbsp->dev, "IRQ callback : 0x%x\n", irqst);

	if (irqst & RSYNCERREN)
		dev_err(mcbsp->dev, "RX Frame Sync Error!\n");
	if (irqst & RFSREN)
		dev_dbg(mcbsp->dev, "RX Frame Sync\n");
	if (irqst & REOFEN)
		dev_dbg(mcbsp->dev, "RX End Of Frame\n");
	if (irqst & RRDYEN)
		dev_dbg(mcbsp->dev, "RX Buffer Threshold Reached\n");
	if (irqst & RUNDFLEN)
		dev_err(mcbsp->dev, "RX Buffer Underflow!\n");
	if (irqst & ROVFLEN)
		dev_err(mcbsp->dev, "RX Buffer Overflow!\n");

	if (irqst & XSYNCERREN)
		dev_err(mcbsp->dev, "TX Frame Sync Error!\n");
	if (irqst & XFSXEN)
		dev_dbg(mcbsp->dev, "TX Frame Sync\n");
	if (irqst & XEOFEN)
		dev_dbg(mcbsp->dev, "TX End Of Frame\n");
	if (irqst & XRDYEN)
		dev_dbg(mcbsp->dev, "TX Buffer threshold Reached\n");
	if (irqst & XUNDFLEN)
		dev_err(mcbsp->dev, "TX Buffer Underflow!\n");
	if (irqst & XOVFLEN)
		dev_err(mcbsp->dev, "TX Buffer Overflow!\n");
	if (irqst & XEMPTYEOFEN)
		dev_dbg(mcbsp->dev, "TX Buffer empty at end of frame\n");

	MCBSP_WRITE(mcbsp, IRQST, irqst);

	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_tx = dev_id;
	u16 irqst_spcr2;

	irqst_spcr2 = MCBSP_READ(mcbsp_tx, SPCR2);
	dev_dbg(mcbsp_tx->dev, "TX IRQ callback : 0x%x\n", irqst_spcr2);

	if (irqst_spcr2 & XSYNC_ERR) {
		dev_err(mcbsp_tx->dev, "TX Frame Sync Error! : 0x%x\n",
			irqst_spcr2);
		/* Writing zero to XSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp_tx, SPCR2, MCBSP_READ_CACHE(mcbsp_tx, SPCR2));
	}

	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_rx = dev_id;
	u16 irqst_spcr1;

	irqst_spcr1 = MCBSP_READ(mcbsp_rx, SPCR1);
	dev_dbg(mcbsp_rx->dev, "RX IRQ callback : 0x%x\n", irqst_spcr1);

	if (irqst_spcr1 & RSYNC_ERR) {
		dev_err(mcbsp_rx->dev, "RX Frame Sync Error! : 0x%x\n",
			irqst_spcr1);
		/* Writing zero to RSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp_rx, SPCR1, MCBSP_READ_CACHE(mcbsp_rx, SPCR1));
	}

	return IRQ_HANDLED;
}

/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */
void omap_mcbsp_config(struct omap_mcbsp *mcbsp,
		       const struct omap_mcbsp_reg_cfg *config)
{
	dev_dbg(mcbsp->dev, "Configuring McBSP%d  phys_base: 0x%08lx\n",
			mcbsp->id, mcbsp->phys_base);

	/* We write the given config */
	MCBSP_WRITE(mcbsp, SPCR2, config->spcr2);
	MCBSP_WRITE(mcbsp, SPCR1, config->spcr1);
	MCBSP_WRITE(mcbsp, RCR2, config->rcr2);
	MCBSP_WRITE(mcbsp, RCR1, config->rcr1);
	MCBSP_WRITE(mcbsp, XCR2, config->xcr2);
	MCBSP_WRITE(mcbsp, XCR1, config->xcr1);
	MCBSP_WRITE(mcbsp, SRGR2, config->srgr2);
	MCBSP_WRITE(mcbsp, SRGR1, config->srgr1);
	MCBSP_WRITE(mcbsp, MCR2, config->mcr2);
	MCBSP_WRITE(mcbsp, MCR1, config->mcr1);
	MCBSP_WRITE(mcbsp, PCR0, config->pcr0);
	if (mcbsp->pdata->has_ccr) {
		MCBSP_WRITE(mcbsp, XCCR, config->xccr);
		MCBSP_WRITE(mcbsp, RCCR, config->rccr);
	}
	/* Enable wakeup behavior */
	if (mcbsp->pdata->has_wakeup)
		MCBSP_WRITE(mcbsp, WAKEUPEN, XRDYEN | RRDYEN);

	/* Enable TX/RX sync error interrupts by default */
	if (mcbsp->irq)
		MCBSP_WRITE(mcbsp, IRQEN, RSYNCERREN | XSYNCERREN);
}

/**
 * omap_mcbsp_dma_reg_params - returns the address of mcbsp data register
 * @id - mcbsp id
 * @stream - indicates the direction of data flow (rx or tx)
 *
 * Returns the address of mcbsp data transmit register or data receive register
 * to be used by DMA for transferring/receiving data based on the value of
 * @stream for the requested mcbsp given by @id
 */
static int omap_mcbsp_dma_reg_params(struct omap_mcbsp *mcbsp,
				     unsigned int stream)
{
	int data_reg;

	if (mcbsp->pdata->reg_size == 2) {
		if (stream)
			data_reg = OMAP_MCBSP_REG_DRR1;
		else
			data_reg = OMAP_MCBSP_REG_DXR1;
	} else {
		if (stream)
			data_reg = OMAP_MCBSP_REG_DRR;
		else
			data_reg = OMAP_MCBSP_REG_DXR;
	}

	return mcbsp->phys_dma_base + data_reg * mcbsp->pdata->reg_step;
}

static void omap_st_on(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	if (mcbsp->pdata->enable_st_clock)
		mcbsp->pdata->enable_st_clock(mcbsp->id, 1);

	/* Enable McBSP Sidetone */
	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w | SIDETONEEN);

	/* Enable Sidetone from Sidetone Core */
	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w | ST_SIDETONEEN);
}

static void omap_st_off(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w & ~(ST_SIDETONEEN));

	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w & ~(SIDETONEEN));

	if (mcbsp->pdata->enable_st_clock)
		mcbsp->pdata->enable_st_clock(mcbsp->id, 0);
}

static void omap_st_fir_write(struct omap_mcbsp *mcbsp, s16 *fir)
{
	u16 val, i;

	val = MCBSP_ST_READ(mcbsp, SSELCR);

	if (val & ST_COEFFWREN)
		MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	MCBSP_ST_WRITE(mcbsp, SSELCR, val | ST_COEFFWREN);

	for (i = 0; i < 128; i++)
		MCBSP_ST_WRITE(mcbsp, SFIRCR, fir[i]);

	i = 0;

	val = MCBSP_ST_READ(mcbsp, SSELCR);
	while (!(val & ST_COEFFWRDONE) && (++i < 1000))
		val = MCBSP_ST_READ(mcbsp, SSELCR);

	MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	if (i == 1000)
		dev_err(mcbsp->dev, "McBSP FIR load error!\n");
}

static void omap_st_chgain(struct omap_mcbsp *mcbsp)
{
	u16 w;
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	w = MCBSP_ST_READ(mcbsp, SSELCR);

	MCBSP_ST_WRITE(mcbsp, SGAINCR, ST_CH0GAIN(st_data->ch0gain) | \
		      ST_CH1GAIN(st_data->ch1gain));
}

int omap_st_set_chgain(struct omap_mcbsp *mcbsp, int channel, s16 chgain)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		st_data->ch0gain = chgain;
	else if (channel == 1)
		st_data->ch1gain = chgain;
	else
		ret = -EINVAL;

	if (st_data->enabled)
		omap_st_chgain(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

int omap_st_get_chgain(struct omap_mcbsp *mcbsp, int channel, s16 *chgain)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		*chgain = st_data->ch0gain;
	else if (channel == 1)
		*chgain = st_data->ch1gain;
	else
		ret = -EINVAL;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

static int omap_st_start(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data->enabled && !st_data->running) {
		omap_st_fir_write(mcbsp, st_data->taps);
		omap_st_chgain(mcbsp);

		if (!mcbsp->free) {
			omap_st_on(mcbsp);
			st_data->running = 1;
		}
	}

	return 0;
}

int omap_st_enable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	st_data->enabled = 1;
	omap_st_start(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return 0;
}

static int omap_st_stop(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data->running) {
		if (!mcbsp->free) {
			omap_st_off(mcbsp);
			st_data->running = 0;
		}
	}

	return 0;
}

int omap_st_disable(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int ret = 0;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	omap_st_stop(mcbsp);
	st_data->enabled = 0;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}

int omap_st_is_enabled(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	return st_data->enabled;
}

/*
 * omap_mcbsp_set_rx_threshold configures the transmit threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH2 register.
 */
void omap_mcbsp_set_tx_threshold(struct omap_mcbsp *mcbsp, u16 threshold)
{
	if (mcbsp->pdata->buffer_size == 0)
		return;

	if (threshold && threshold <= mcbsp->max_tx_thres)
		MCBSP_WRITE(mcbsp, THRSH2, threshold - 1);
}

/*
 * omap_mcbsp_set_rx_threshold configures the receive threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH1 register.
 */
void omap_mcbsp_set_rx_threshold(struct omap_mcbsp *mcbsp, u16 threshold)
{
	if (mcbsp->pdata->buffer_size == 0)
		return;

	if (threshold && threshold <= mcbsp->max_rx_thres)
		MCBSP_WRITE(mcbsp, THRSH1, threshold - 1);
}

/*
 * omap_mcbsp_get_tx_delay returns the number of used slots in the McBSP FIFO
 */
u16 omap_mcbsp_get_tx_delay(struct omap_mcbsp *mcbsp)
{
	u16 buffstat;

	if (mcbsp->pdata->buffer_size == 0)
		return 0;

	/* Returns the number of free locations in the buffer */
	buffstat = MCBSP_READ(mcbsp, XBUFFSTAT);

	/* Number of slots are different in McBSP ports */
	return mcbsp->pdata->buffer_size - buffstat;
}

/*
 * omap_mcbsp_get_rx_delay returns the number of free slots in the McBSP FIFO
 * to reach the threshold value (when the DMA will be triggered to read it)
 */
u16 omap_mcbsp_get_rx_delay(struct omap_mcbsp *mcbsp)
{
	u16 buffstat, threshold;

	if (mcbsp->pdata->buffer_size == 0)
		return 0;

	/* Returns the number of used locations in the buffer */
	buffstat = MCBSP_READ(mcbsp, RBUFFSTAT);
	/* RX threshold */
	threshold = MCBSP_READ(mcbsp, THRSH1);

	/* Return the number of location till we reach the threshold limit */
	if (threshold <= buffstat)
		return 0;
	else
		return threshold - buffstat;
}

int omap_mcbsp_request(struct omap_mcbsp *mcbsp)
{
	void *reg_cache;
	int err;

	reg_cache = kzalloc(mcbsp->reg_cache_size, GFP_KERNEL);
	if (!reg_cache) {
		return -ENOMEM;
	}

	spin_lock(&mcbsp->lock);
	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n",
			mcbsp->id);
		err = -EBUSY;
		goto err_kfree;
	}

	mcbsp->free = false;
	mcbsp->reg_cache = reg_cache;
	spin_unlock(&mcbsp->lock);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->request)
		mcbsp->pdata->ops->request(mcbsp->id - 1);

	/*
	 * Make sure that transmitter, receiver and sample-rate generator are
	 * not running before activating IRQs.
	 */
	MCBSP_WRITE(mcbsp, SPCR1, 0);
	MCBSP_WRITE(mcbsp, SPCR2, 0);

	if (mcbsp->irq) {
		err = request_irq(mcbsp->irq, omap_mcbsp_irq_handler, 0,
				  "McBSP", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request IRQ\n");
			goto err_clk_disable;
		}
	} else {
		err = request_irq(mcbsp->tx_irq, omap_mcbsp_tx_irq_handler, 0,
				  "McBSP TX", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request TX IRQ\n");
			goto err_clk_disable;
		}

		err = request_irq(mcbsp->rx_irq, omap_mcbsp_rx_irq_handler, 0,
				  "McBSP RX", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request RX IRQ\n");
			goto err_free_irq;
		}
	}

	return 0;
err_free_irq:
	free_irq(mcbsp->tx_irq, (void *)mcbsp);
err_clk_disable:
	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(mcbsp->id - 1);

	/* Disable wakeup behavior */
	if (mcbsp->pdata->has_wakeup)
		MCBSP_WRITE(mcbsp, WAKEUPEN, 0);

	spin_lock(&mcbsp->lock);
	mcbsp->free = true;
	mcbsp->reg_cache = NULL;
err_kfree:
	spin_unlock(&mcbsp->lock);
	kfree(reg_cache);

	return err;
}

void omap_mcbsp_free(struct omap_mcbsp *mcbsp)
{
	void *reg_cache;

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(mcbsp->id - 1);

	/* Disable wakeup behavior */
	if (mcbsp->pdata->has_wakeup)
		MCBSP_WRITE(mcbsp, WAKEUPEN, 0);

	/* Disable interrupt requests */
	if (mcbsp->irq)
		MCBSP_WRITE(mcbsp, IRQEN, 0);

	if (mcbsp->irq) {
		free_irq(mcbsp->irq, (void *)mcbsp);
	} else {
		free_irq(mcbsp->rx_irq, (void *)mcbsp);
		free_irq(mcbsp->tx_irq, (void *)mcbsp);
	}

	reg_cache = mcbsp->reg_cache;

	/*
	 * Select CLKS source from internal source unconditionally before
	 * marking the McBSP port as free.
	 * If the external clock source via MCBSP_CLKS pin has been selected the
	 * system will refuse to enter idle if the CLKS pin source is not reset
	 * back to internal source.
	 */
	if (!cpu_class_is_omap1())
		omap2_mcbsp_set_clks_src(mcbsp, MCBSP_CLKS_PRCM_SRC);

	spin_lock(&mcbsp->lock);
	if (mcbsp->free)
		dev_err(mcbsp->dev, "McBSP%d was not reserved\n", mcbsp->id);
	else
		mcbsp->free = true;
	mcbsp->reg_cache = NULL;
	spin_unlock(&mcbsp->lock);

	if (reg_cache)
		kfree(reg_cache);
}

/*
 * Here we start the McBSP, by enabling transmitter, receiver or both.
 * If no transmitter or receiver is active prior calling, then sample-rate
 * generator and frame sync are started.
 */
void omap_mcbsp_start(struct omap_mcbsp *mcbsp, int tx, int rx)
{
	int enable_srg = 0;
	u16 w;

	if (mcbsp->st_data)
		omap_st_start(mcbsp);

	/* Only enable SRG, if McBSP is master */
	w = MCBSP_READ_CACHE(mcbsp, PCR0);
	if (w & (FSXM | FSRM | CLKXM | CLKRM))
		enable_srg = !((MCBSP_READ_CACHE(mcbsp, SPCR2) |
				MCBSP_READ_CACHE(mcbsp, SPCR1)) & 1);

	if (enable_srg) {
		/* Start the sample generator */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w | (1 << 6));
	}

	/* Enable transmitter and receiver */
	tx &= 1;
	w = MCBSP_READ_CACHE(mcbsp, SPCR2);
	MCBSP_WRITE(mcbsp, SPCR2, w | tx);

	rx &= 1;
	w = MCBSP_READ_CACHE(mcbsp, SPCR1);
	MCBSP_WRITE(mcbsp, SPCR1, w | rx);

	/*
	 * Worst case: CLKSRG*2 = 8000khz: (1/8000) * 2 * 2 usec
	 * REVISIT: 100us may give enough time for two CLKSRG, however
	 * due to some unknown PM related, clock gating etc. reason it
	 * is now at 500us.
	 */
	udelay(500);

	if (enable_srg) {
		/* Start frame sync */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w | (1 << 7));
	}

	if (mcbsp->pdata->has_ccr) {
		/* Release the transmitter and receiver */
		w = MCBSP_READ_CACHE(mcbsp, XCCR);
		w &= ~(tx ? XDISABLE : 0);
		MCBSP_WRITE(mcbsp, XCCR, w);
		w = MCBSP_READ_CACHE(mcbsp, RCCR);
		w &= ~(rx ? RDISABLE : 0);
		MCBSP_WRITE(mcbsp, RCCR, w);
	}

	/* Dump McBSP Regs */
	omap_mcbsp_dump_reg(mcbsp);
}

void omap_mcbsp_stop(struct omap_mcbsp *mcbsp, int tx, int rx)
{
	int idle;
	u16 w;

	/* Reset transmitter */
	tx &= 1;
	if (mcbsp->pdata->has_ccr) {
		w = MCBSP_READ_CACHE(mcbsp, XCCR);
		w |= (tx ? XDISABLE : 0);
		MCBSP_WRITE(mcbsp, XCCR, w);
	}
	w = MCBSP_READ_CACHE(mcbsp, SPCR2);
	MCBSP_WRITE(mcbsp, SPCR2, w & ~tx);

	/* Reset receiver */
	rx &= 1;
	if (mcbsp->pdata->has_ccr) {
		w = MCBSP_READ_CACHE(mcbsp, RCCR);
		w |= (rx ? RDISABLE : 0);
		MCBSP_WRITE(mcbsp, RCCR, w);
	}
	w = MCBSP_READ_CACHE(mcbsp, SPCR1);
	MCBSP_WRITE(mcbsp, SPCR1, w & ~rx);

	idle = !((MCBSP_READ_CACHE(mcbsp, SPCR2) |
			MCBSP_READ_CACHE(mcbsp, SPCR1)) & 1);

	if (idle) {
		/* Reset the sample rate generator */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w & ~(1 << 6));
	}

	if (mcbsp->st_data)
		omap_st_stop(mcbsp);
}

int omap2_mcbsp_set_clks_src(struct omap_mcbsp *mcbsp, u8 fck_src_id)
{
	const char *src;

	if (fck_src_id == MCBSP_CLKS_PAD_SRC)
		src = "clks_ext";
	else if (fck_src_id == MCBSP_CLKS_PRCM_SRC)
		src = "clks_fclk";
	else
		return -EINVAL;

	if (mcbsp->pdata->set_clk_src)
		return mcbsp->pdata->set_clk_src(mcbsp->dev, mcbsp->fclk, src);
	else
		return -EINVAL;
}

int omap_mcbsp_6pin_src_mux(struct omap_mcbsp *mcbsp, u8 mux)
{
	const char *signal, *src;

	if (mcbsp->pdata->mux_signal)
		return -EINVAL;

	switch (mux) {
	case CLKR_SRC_CLKR:
		signal = "clkr";
		src = "clkr";
		break;
	case CLKR_SRC_CLKX:
		signal = "clkr";
		src = "clkx";
		break;
	case FSR_SRC_FSR:
		signal = "fsr";
		src = "fsr";
		break;
	case FSR_SRC_FSX:
		signal = "fsr";
		src = "fsx";
		break;
	default:
		return -EINVAL;
	}

	return mcbsp->pdata->mux_signal(mcbsp->dev, signal, src);
}

#define max_thres(m)			(mcbsp->pdata->buffer_size)
#define valid_threshold(m, val)		((val) <= max_thres(m))
#define THRESHOLD_PROP_BUILDER(prop)					\
static ssize_t prop##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%u\n", mcbsp->prop);			\
}									\
									\
static ssize_t prop##_store(struct device *dev,				\
				struct device_attribute *attr,		\
				const char *buf, size_t size)		\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
	unsigned long val;						\
	int status;							\
									\
	status = strict_strtoul(buf, 0, &val);				\
	if (status)							\
		return status;						\
									\
	if (!valid_threshold(mcbsp, val))				\
		return -EDOM;						\
									\
	mcbsp->prop = val;						\
	return size;							\
}									\
									\
static DEVICE_ATTR(prop, 0644, prop##_show, prop##_store);

THRESHOLD_PROP_BUILDER(max_tx_thres);
THRESHOLD_PROP_BUILDER(max_rx_thres);

static const char *dma_op_modes[] = {
	"element", "threshold",
};

static ssize_t dma_op_mode_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	int dma_op_mode, i = 0;
	ssize_t len = 0;
	const char * const *s;

	dma_op_mode = mcbsp->dma_op_mode;

	for (s = &dma_op_modes[i]; i < ARRAY_SIZE(dma_op_modes); s++, i++) {
		if (dma_op_mode == i)
			len += sprintf(buf + len, "[%s] ", *s);
		else
			len += sprintf(buf + len, "%s ", *s);
	}
	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t dma_op_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	const char * const *s;
	int i = 0;

	for (s = &dma_op_modes[i]; i < ARRAY_SIZE(dma_op_modes); s++, i++)
		if (sysfs_streq(buf, *s))
			break;

	if (i == ARRAY_SIZE(dma_op_modes))
		return -EINVAL;

	spin_lock_irq(&mcbsp->lock);
	if (!mcbsp->free) {
		size = -EBUSY;
		goto unlock;
	}
	mcbsp->dma_op_mode = i;

unlock:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(dma_op_mode, 0644, dma_op_mode_show, dma_op_mode_store);

static const struct attribute *additional_attrs[] = {
	&dev_attr_max_tx_thres.attr,
	&dev_attr_max_rx_thres.attr,
	&dev_attr_dma_op_mode.attr,
	NULL,
};

static const struct attribute_group additional_attr_group = {
	.attrs = (struct attribute **)additional_attrs,
};

static ssize_t st_taps_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	ssize_t status = 0;
	int i;

	spin_lock_irq(&mcbsp->lock);
	for (i = 0; i < st_data->nr_taps; i++)
		status += sprintf(&buf[status], (i ? ", %d" : "%d"),
				  st_data->taps[i]);
	if (i)
		status += sprintf(&buf[status], "\n");
	spin_unlock_irq(&mcbsp->lock);

	return status;
}

static ssize_t st_taps_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int val, tmp, status, i = 0;

	spin_lock_irq(&mcbsp->lock);
	memset(st_data->taps, 0, sizeof(st_data->taps));
	st_data->nr_taps = 0;

	do {
		status = sscanf(buf, "%d%n", &val, &tmp);
		if (status < 0 || status == 0) {
			size = -EINVAL;
			goto out;
		}
		if (val < -32768 || val > 32767) {
			size = -EINVAL;
			goto out;
		}
		st_data->taps[i++] = val;
		buf += tmp;
		if (*buf != ',')
			break;
		buf++;
	} while (1);

	st_data->nr_taps = i;

out:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(st_taps, 0644, st_taps_show, st_taps_store);

static const struct attribute *sidetone_attrs[] = {
	&dev_attr_st_taps.attr,
	NULL,
};

static const struct attribute_group sidetone_attr_group = {
	.attrs = (struct attribute **)sidetone_attrs,
};

static int __devinit omap_st_add(struct omap_mcbsp *mcbsp,
				 struct resource *res)
{
	struct omap_mcbsp_st_data *st_data;
	int err;

	st_data = devm_kzalloc(mcbsp->dev, sizeof(*mcbsp->st_data), GFP_KERNEL);
	if (!st_data)
		return -ENOMEM;

	st_data->io_base_st = devm_ioremap(mcbsp->dev, res->start,
					   resource_size(res));
	if (!st_data->io_base_st)
		return -ENOMEM;

	err = sysfs_create_group(&mcbsp->dev->kobj, &sidetone_attr_group);
	if (err)
		return err;

	mcbsp->st_data = st_data;
	return 0;
}

/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
int __devinit omap_mcbsp_init(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	struct resource *res;
	int ret = 0;

	spin_lock_init(&mcbsp->lock);
	mcbsp->free = true;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!res) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			dev_err(mcbsp->dev, "invalid memory resource\n");
			return -ENOMEM;
		}
	}
	if (!devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
				     dev_name(&pdev->dev))) {
		dev_err(mcbsp->dev, "memory region already claimed\n");
		return -ENODEV;
	}

	mcbsp->phys_base = res->start;
	mcbsp->reg_cache_size = resource_size(res);
	mcbsp->io_base = devm_ioremap(&pdev->dev, res->start,
				      resource_size(res));
	if (!mcbsp->io_base)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
	if (!res)
		mcbsp->phys_dma_base = mcbsp->phys_base;
	else
		mcbsp->phys_dma_base = res->start;

	/*
	 * OMAP1, 2 uses two interrupt lines: TX, RX
	 * OMAP2430, OMAP3 SoC have combined IRQ line as well.
	 * OMAP4 and newer SoC only have the combined IRQ line.
	 * Use the combined IRQ if available since it gives better debugging
	 * possibilities.
	 */
	mcbsp->irq = platform_get_irq_byname(pdev, "common");
	if (mcbsp->irq == -ENXIO) {
		mcbsp->tx_irq = platform_get_irq_byname(pdev, "tx");

		if (mcbsp->tx_irq == -ENXIO) {
			mcbsp->irq = platform_get_irq(pdev, 0);
			mcbsp->tx_irq = 0;
		} else {
			mcbsp->rx_irq = platform_get_irq_byname(pdev, "rx");
			mcbsp->irq = 0;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "rx");
	if (!res) {
		dev_err(&pdev->dev, "invalid rx DMA channel\n");
		return -ENODEV;
	}
	/* RX DMA request number, and port address configuration */
	mcbsp->dma_data[1].name = "Audio Capture";
	mcbsp->dma_data[1].dma_req = res->start;
	mcbsp->dma_data[1].port_addr = omap_mcbsp_dma_reg_params(mcbsp, 1);

	res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "tx");
	if (!res) {
		dev_err(&pdev->dev, "invalid tx DMA channel\n");
		return -ENODEV;
	}
	/* TX DMA request number, and port address configuration */
	mcbsp->dma_data[0].name = "Audio Playback";
	mcbsp->dma_data[0].dma_req = res->start;
	mcbsp->dma_data[0].port_addr = omap_mcbsp_dma_reg_params(mcbsp, 0);

	mcbsp->fclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(mcbsp->fclk)) {
		ret = PTR_ERR(mcbsp->fclk);
		dev_err(mcbsp->dev, "unable to get fck: %d\n", ret);
		return ret;
	}

	mcbsp->dma_op_mode = MCBSP_DMA_MODE_ELEMENT;
	if (mcbsp->pdata->buffer_size) {
		/*
		 * Initially configure the maximum thresholds to a safe value.
		 * The McBSP FIFO usage with these values should not go under
		 * 16 locations.
		 * If the whole FIFO without safety buffer is used, than there
		 * is a possibility that the DMA will be not able to push the
		 * new data on time, causing channel shifts in runtime.
		 */
		mcbsp->max_tx_thres = max_thres(mcbsp) - 0x10;
		mcbsp->max_rx_thres = max_thres(mcbsp) - 0x10;

		ret = sysfs_create_group(&mcbsp->dev->kobj,
					 &additional_attr_group);
		if (ret) {
			dev_err(mcbsp->dev,
				"Unable to create additional controls\n");
			goto err_thres;
		}
	} else {
		mcbsp->max_tx_thres = -EINVAL;
		mcbsp->max_rx_thres = -EINVAL;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sidetone");
	if (res) {
		ret = omap_st_add(mcbsp, res);
		if (ret) {
			dev_err(mcbsp->dev,
				"Unable to create sidetone controls\n");
			goto err_st;
		}
	}

	return 0;

err_st:
	if (mcbsp->pdata->buffer_size)
		sysfs_remove_group(&mcbsp->dev->kobj, &additional_attr_group);
err_thres:
	clk_put(mcbsp->fclk);
	return ret;
}

void __devexit omap_mcbsp_sysfs_remove(struct omap_mcbsp *mcbsp)
{
	if (mcbsp->pdata->buffer_size)
		sysfs_remove_group(&mcbsp->dev->kobj, &additional_attr_group);

	if (mcbsp->st_data)
		sysfs_remove_group(&mcbsp->dev->kobj, &sidetone_attr_group);
}
