// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap-mcbsp.c  --  OMAP ALSA SoC DAI driver using McBSP port
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "omap-mcbsp-priv.h"
#include "omap-mcbsp.h"
#include "sdma-pcm.h"

#define OMAP_MCBSP_RATES	(SNDRV_PCM_RATE_8000_96000)

enum {
	OMAP_MCBSP_WORD_8 = 0,
	OMAP_MCBSP_WORD_12,
	OMAP_MCBSP_WORD_16,
	OMAP_MCBSP_WORD_20,
	OMAP_MCBSP_WORD_24,
	OMAP_MCBSP_WORD_32,
};

static void omap_mcbsp_dump_reg(struct omap_mcbsp *mcbsp)
{
	dev_dbg(mcbsp->dev, "**** McBSP%d regs ****\n", mcbsp->id);
	dev_dbg(mcbsp->dev, "DRR2:  0x%04x\n", MCBSP_READ(mcbsp, DRR2));
	dev_dbg(mcbsp->dev, "DRR1:  0x%04x\n", MCBSP_READ(mcbsp, DRR1));
	dev_dbg(mcbsp->dev, "DXR2:  0x%04x\n", MCBSP_READ(mcbsp, DXR2));
	dev_dbg(mcbsp->dev, "DXR1:  0x%04x\n", MCBSP_READ(mcbsp, DXR1));
	dev_dbg(mcbsp->dev, "SPCR2: 0x%04x\n", MCBSP_READ(mcbsp, SPCR2));
	dev_dbg(mcbsp->dev, "SPCR1: 0x%04x\n", MCBSP_READ(mcbsp, SPCR1));
	dev_dbg(mcbsp->dev, "RCR2:  0x%04x\n", MCBSP_READ(mcbsp, RCR2));
	dev_dbg(mcbsp->dev, "RCR1:  0x%04x\n", MCBSP_READ(mcbsp, RCR1));
	dev_dbg(mcbsp->dev, "XCR2:  0x%04x\n", MCBSP_READ(mcbsp, XCR2));
	dev_dbg(mcbsp->dev, "XCR1:  0x%04x\n", MCBSP_READ(mcbsp, XCR1));
	dev_dbg(mcbsp->dev, "SRGR2: 0x%04x\n", MCBSP_READ(mcbsp, SRGR2));
	dev_dbg(mcbsp->dev, "SRGR1: 0x%04x\n", MCBSP_READ(mcbsp, SRGR1));
	dev_dbg(mcbsp->dev, "PCR0:  0x%04x\n", MCBSP_READ(mcbsp, PCR0));
	dev_dbg(mcbsp->dev, "***********************\n");
}

static int omap2_mcbsp_set_clks_src(struct omap_mcbsp *mcbsp, u8 fck_src_id)
{
	struct clk *fck_src;
	const char *src;
	int r;

	if (fck_src_id == MCBSP_CLKS_PAD_SRC)
		src = "pad_fck";
	else if (fck_src_id == MCBSP_CLKS_PRCM_SRC)
		src = "prcm_fck";
	else
		return -EINVAL;

	fck_src = clk_get(mcbsp->dev, src);
	if (IS_ERR(fck_src)) {
		dev_err(mcbsp->dev, "CLKS: could not clk_get() %s\n", src);
		return -EINVAL;
	}

	pm_runtime_put_sync(mcbsp->dev);

	r = clk_set_parent(mcbsp->fclk, fck_src);
	if (r)
		dev_err(mcbsp->dev, "CLKS: could not clk_set_parent() to %s\n",
			src);

	pm_runtime_get_sync(mcbsp->dev);

	clk_put(fck_src);

	return r;
}

static irqreturn_t omap_mcbsp_irq_handler(int irq, void *data)
{
	struct omap_mcbsp *mcbsp = data;
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

static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *data)
{
	struct omap_mcbsp *mcbsp = data;
	u16 irqst_spcr2;

	irqst_spcr2 = MCBSP_READ(mcbsp, SPCR2);
	dev_dbg(mcbsp->dev, "TX IRQ callback : 0x%x\n", irqst_spcr2);

	if (irqst_spcr2 & XSYNC_ERR) {
		dev_err(mcbsp->dev, "TX Frame Sync Error! : 0x%x\n",
			irqst_spcr2);
		/* Writing zero to XSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp, SPCR2, MCBSP_READ_CACHE(mcbsp, SPCR2));
	}

	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *data)
{
	struct omap_mcbsp *mcbsp = data;
	u16 irqst_spcr1;

	irqst_spcr1 = MCBSP_READ(mcbsp, SPCR1);
	dev_dbg(mcbsp->dev, "RX IRQ callback : 0x%x\n", irqst_spcr1);

	if (irqst_spcr1 & RSYNC_ERR) {
		dev_err(mcbsp->dev, "RX Frame Sync Error! : 0x%x\n",
			irqst_spcr1);
		/* Writing zero to RSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp, SPCR1, MCBSP_READ_CACHE(mcbsp, SPCR1));
	}

	return IRQ_HANDLED;
}

/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */
static void omap_mcbsp_config(struct omap_mcbsp *mcbsp,
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
		MCBSP_WRITE(mcbsp, IRQEN, RSYNCERREN | XSYNCERREN |
			    RUNDFLEN | ROVFLEN | XUNDFLEN | XOVFLEN);
}

/**
 * omap_mcbsp_dma_reg_params - returns the address of mcbsp data register
 * @mcbsp: omap_mcbsp struct for the McBSP instance
 * @stream: Stream direction (playback/capture)
 *
 * Returns the address of mcbsp data transmit register or data receive register
 * to be used by DMA for transferring/receiving data
 */
static int omap_mcbsp_dma_reg_params(struct omap_mcbsp *mcbsp,
				     unsigned int stream)
{
	int data_reg;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mcbsp->pdata->reg_size == 2)
			data_reg = OMAP_MCBSP_REG_DXR1;
		else
			data_reg = OMAP_MCBSP_REG_DXR;
	} else {
		if (mcbsp->pdata->reg_size == 2)
			data_reg = OMAP_MCBSP_REG_DRR1;
		else
			data_reg = OMAP_MCBSP_REG_DRR;
	}

	return mcbsp->phys_dma_base + data_reg * mcbsp->pdata->reg_step;
}

/*
 * omap_mcbsp_set_rx_threshold configures the transmit threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH2 register.
 */
static void omap_mcbsp_set_tx_threshold(struct omap_mcbsp *mcbsp, u16 threshold)
{
	if (threshold && threshold <= mcbsp->max_tx_thres)
		MCBSP_WRITE(mcbsp, THRSH2, threshold - 1);
}

/*
 * omap_mcbsp_set_rx_threshold configures the receive threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH1 register.
 */
static void omap_mcbsp_set_rx_threshold(struct omap_mcbsp *mcbsp, u16 threshold)
{
	if (threshold && threshold <= mcbsp->max_rx_thres)
		MCBSP_WRITE(mcbsp, THRSH1, threshold - 1);
}

/*
 * omap_mcbsp_get_tx_delay returns the number of used slots in the McBSP FIFO
 */
static u16 omap_mcbsp_get_tx_delay(struct omap_mcbsp *mcbsp)
{
	u16 buffstat;

	/* Returns the number of free locations in the buffer */
	buffstat = MCBSP_READ(mcbsp, XBUFFSTAT);

	/* Number of slots are different in McBSP ports */
	return mcbsp->pdata->buffer_size - buffstat;
}

/*
 * omap_mcbsp_get_rx_delay returns the number of free slots in the McBSP FIFO
 * to reach the threshold value (when the DMA will be triggered to read it)
 */
static u16 omap_mcbsp_get_rx_delay(struct omap_mcbsp *mcbsp)
{
	u16 buffstat, threshold;

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

static int omap_mcbsp_request(struct omap_mcbsp *mcbsp)
{
	void *reg_cache;
	int err;

	reg_cache = kzalloc(mcbsp->reg_cache_size, GFP_KERNEL);
	if (!reg_cache)
		return -ENOMEM;

	spin_lock(&mcbsp->lock);
	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n", mcbsp->id);
		err = -EBUSY;
		goto err_kfree;
	}

	mcbsp->free = false;
	mcbsp->reg_cache = reg_cache;
	spin_unlock(&mcbsp->lock);

	if(mcbsp->pdata->ops && mcbsp->pdata->ops->request)
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
	if(mcbsp->pdata->ops && mcbsp->pdata->ops->free)
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

static void omap_mcbsp_free(struct omap_mcbsp *mcbsp)
{
	void *reg_cache;

	if(mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(mcbsp->id - 1);

	/* Disable wakeup behavior */
	if (mcbsp->pdata->has_wakeup)
		MCBSP_WRITE(mcbsp, WAKEUPEN, 0);

	/* Disable interrupt requests */
	if (mcbsp->irq) {
		MCBSP_WRITE(mcbsp, IRQEN, 0);

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
	if (!mcbsp_omap1())
		omap2_mcbsp_set_clks_src(mcbsp, MCBSP_CLKS_PRCM_SRC);

	spin_lock(&mcbsp->lock);
	if (mcbsp->free)
		dev_err(mcbsp->dev, "McBSP%d was not reserved\n", mcbsp->id);
	else
		mcbsp->free = true;
	mcbsp->reg_cache = NULL;
	spin_unlock(&mcbsp->lock);

	kfree(reg_cache);
}

/*
 * Here we start the McBSP, by enabling transmitter, receiver or both.
 * If no transmitter or receiver is active prior calling, then sample-rate
 * generator and frame sync are started.
 */
static void omap_mcbsp_start(struct omap_mcbsp *mcbsp, int stream)
{
	int tx = (stream == SNDRV_PCM_STREAM_PLAYBACK);
	int rx = !tx;
	int enable_srg = 0;
	u16 w;

	if (mcbsp->st_data)
		omap_mcbsp_st_start(mcbsp);

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

static void omap_mcbsp_stop(struct omap_mcbsp *mcbsp, int stream)
{
	int tx = (stream == SNDRV_PCM_STREAM_PLAYBACK);
	int rx = !tx;
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
		omap_mcbsp_st_stop(mcbsp);
}

#define max_thres(m)			(mcbsp->pdata->buffer_size)
#define valid_threshold(m, val)		((val) <= max_thres(m))
#define THRESHOLD_PROP_BUILDER(prop)					\
static ssize_t prop##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
									\
	return sysfs_emit(buf, "%u\n", mcbsp->prop);			\
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
	status = kstrtoul(buf, 0, &val);				\
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
static DEVICE_ATTR_RW(prop)

THRESHOLD_PROP_BUILDER(max_tx_thres);
THRESHOLD_PROP_BUILDER(max_rx_thres);

static const char * const dma_op_modes[] = {
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
			len += sysfs_emit_at(buf, len, "[%s] ", *s);
		else
			len += sysfs_emit_at(buf, len, "%s ", *s);
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static ssize_t dma_op_mode_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	int i;

	i = sysfs_match_string(dma_op_modes, buf);
	if (i < 0)
		return i;

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

static DEVICE_ATTR_RW(dma_op_mode);

static const struct attribute *additional_attrs[] = {
	&dev_attr_max_tx_thres.attr,
	&dev_attr_max_rx_thres.attr,
	&dev_attr_dma_op_mode.attr,
	NULL,
};

static const struct attribute_group additional_attr_group = {
	.attrs = (struct attribute **)additional_attrs,
};

/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
static int omap_mcbsp_init(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);
	struct resource *res;
	int ret;

	spin_lock_init(&mcbsp->lock);
	mcbsp->free = true;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	mcbsp->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mcbsp->io_base))
		return PTR_ERR(mcbsp->io_base);

	mcbsp->phys_base = res->start;
	mcbsp->reg_cache_size = resource_size(res);

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

	if (!pdev->dev.of_node) {
		res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "tx");
		if (!res) {
			dev_err(&pdev->dev, "invalid tx DMA channel\n");
			return -ENODEV;
		}
		mcbsp->dma_req[0] = res->start;
		mcbsp->dma_data[0].filter_data = &mcbsp->dma_req[0];

		res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "rx");
		if (!res) {
			dev_err(&pdev->dev, "invalid rx DMA channel\n");
			return -ENODEV;
		}
		mcbsp->dma_req[1] = res->start;
		mcbsp->dma_data[1].filter_data = &mcbsp->dma_req[1];
	} else {
		mcbsp->dma_data[0].filter_data = "tx";
		mcbsp->dma_data[1].filter_data = "rx";
	}

	mcbsp->dma_data[0].addr = omap_mcbsp_dma_reg_params(mcbsp,
						SNDRV_PCM_STREAM_PLAYBACK);
	mcbsp->dma_data[1].addr = omap_mcbsp_dma_reg_params(mcbsp,
						SNDRV_PCM_STREAM_CAPTURE);

	mcbsp->fclk = devm_clk_get(&pdev->dev, "fck");
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

		ret = devm_device_add_group(mcbsp->dev, &additional_attr_group);
		if (ret) {
			dev_err(mcbsp->dev,
				"Unable to create additional controls\n");
			return ret;
		}
	}

	return omap_mcbsp_st_init(pdev);
}

/*
 * Stream DMA parameters. DMA request line and port address are set runtime
 * since they are different between OMAP1 and later OMAPs
 */
static void omap_mcbsp_set_threshold(struct snd_pcm_substream *substream,
		unsigned int packet_size)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	int words;

	/* No need to proceed further if McBSP does not have FIFO */
	if (mcbsp->pdata->buffer_size == 0)
		return;

	/*
	 * Configure McBSP threshold based on either:
	 * packet_size, when the sDMA is in packet mode, or based on the
	 * period size in THRESHOLD mode, otherwise use McBSP threshold = 1
	 * for mono streams.
	 */
	if (packet_size)
		words = packet_size;
	else
		words = 1;

	/* Configure McBSP internal buffer usage */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		omap_mcbsp_set_tx_threshold(mcbsp, words);
	else
		omap_mcbsp_set_rx_threshold(mcbsp, words);
}

static int omap_mcbsp_hwrule_min_buffersize(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *buffer_size = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct omap_mcbsp *mcbsp = rule->private;
	struct snd_interval frames;
	int size;

	snd_interval_any(&frames);
	size = mcbsp->pdata->buffer_size;

	frames.min = size / channels->min;
	frames.integer = 1;
	return snd_interval_refine(buffer_size, &frames);
}

static int omap_mcbsp_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	int err = 0;

	if (!snd_soc_dai_active(cpu_dai))
		err = omap_mcbsp_request(mcbsp);

	/*
	 * OMAP3 McBSP FIFO is word structured.
	 * McBSP2 has 1024 + 256 = 1280 word long buffer,
	 * McBSP1,3,4,5 has 128 word long buffer
	 * This means that the size of the FIFO depends on the sample format.
	 * For example on McBSP3:
	 * 16bit samples: size is 128 * 2 = 256 bytes
	 * 32bit samples: size is 128 * 4 = 512 bytes
	 * It is simpler to place constraint for buffer and period based on
	 * channels.
	 * McBSP3 as example again (16 or 32 bit samples):
	 * 1 channel (mono): size is 128 frames (128 words)
	 * 2 channels (stereo): size is 128 / 2 = 64 frames (2 * 64 words)
	 * 4 channels: size is 128 / 4 = 32 frames (4 * 32 words)
	 */
	if (mcbsp->pdata->buffer_size) {
		/*
		* Rule for the buffer size. We should not allow
		* smaller buffer than the FIFO size to avoid underruns.
		* This applies only for the playback stream.
		*/
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			snd_pcm_hw_rule_add(substream->runtime, 0,
					    SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
					    omap_mcbsp_hwrule_min_buffersize,
					    mcbsp,
					    SNDRV_PCM_HW_PARAM_CHANNELS, -1);

		/* Make sure, that the period size is always even */
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 2);
	}

	return err;
}

static void omap_mcbsp_dai_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	int tx = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	int stream1 = tx ? SNDRV_PCM_STREAM_PLAYBACK : SNDRV_PCM_STREAM_CAPTURE;
	int stream2 = tx ? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;

	if (mcbsp->latency[stream2])
		cpu_latency_qos_update_request(&mcbsp->pm_qos_req,
					       mcbsp->latency[stream2]);
	else if (mcbsp->latency[stream1])
		cpu_latency_qos_remove_request(&mcbsp->pm_qos_req);

	mcbsp->latency[stream1] = 0;

	if (!snd_soc_dai_active(cpu_dai)) {
		omap_mcbsp_free(mcbsp);
		mcbsp->configured = 0;
	}
}

static int omap_mcbsp_dai_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct pm_qos_request *pm_qos_req = &mcbsp->pm_qos_req;
	int tx = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	int stream1 = tx ? SNDRV_PCM_STREAM_PLAYBACK : SNDRV_PCM_STREAM_CAPTURE;
	int stream2 = tx ? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK;
	int latency = mcbsp->latency[stream2];

	/* Prevent omap hardware from hitting off between FIFO fills */
	if (!latency || mcbsp->latency[stream1] < latency)
		latency = mcbsp->latency[stream1];

	if (cpu_latency_qos_request_active(pm_qos_req))
		cpu_latency_qos_update_request(pm_qos_req, latency);
	else if (latency)
		cpu_latency_qos_add_request(pm_qos_req, latency);

	return 0;
}

static int omap_mcbsp_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mcbsp->active++;
		omap_mcbsp_start(mcbsp, substream->stream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		omap_mcbsp_stop(mcbsp, substream->stream);
		mcbsp->active--;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_sframes_t omap_mcbsp_dai_delay(
			struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	u16 fifo_use;
	snd_pcm_sframes_t delay;

	/* No need to proceed further if McBSP does not have FIFO */
	if (mcbsp->pdata->buffer_size == 0)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		fifo_use = omap_mcbsp_get_tx_delay(mcbsp);
	else
		fifo_use = omap_mcbsp_get_rx_delay(mcbsp);

	/*
	 * Divide the used locations with the channel count to get the
	 * FIFO usage in samples (don't care about partial samples in the
	 * buffer).
	 */
	delay = fifo_use / substream->runtime->channels;

	return delay;
}

static int omap_mcbsp_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *cpu_dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	struct snd_dmaengine_dai_dma_data *dma_data;
	int wlen, channels, wpf;
	int pkt_size = 0;
	unsigned int format, div, framesize, master;
	unsigned int buffer_size = mcbsp->pdata->buffer_size;

	dma_data = snd_soc_dai_get_dma_data(cpu_dai, substream);
	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		wlen = 16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		wlen = 32;
		break;
	default:
		return -EINVAL;
	}
	if (buffer_size) {
		int latency;

		if (mcbsp->dma_op_mode == MCBSP_DMA_MODE_THRESHOLD) {
			int period_words, max_thrsh;
			int divider = 0;

			period_words = params_period_bytes(params) / (wlen / 8);
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				max_thrsh = mcbsp->max_tx_thres;
			else
				max_thrsh = mcbsp->max_rx_thres;
			/*
			 * Use sDMA packet mode if McBSP is in threshold mode:
			 * If period words less than the FIFO size the packet
			 * size is set to the number of period words, otherwise
			 * Look for the biggest threshold value which divides
			 * the period size evenly.
			 */
			divider = period_words / max_thrsh;
			if (period_words % max_thrsh)
				divider++;
			while (period_words % divider &&
				divider < period_words)
				divider++;
			if (divider == period_words)
				return -EINVAL;

			pkt_size = period_words / divider;
		} else if (channels > 1) {
			/* Use packet mode for non mono streams */
			pkt_size = channels;
		}

		latency = (buffer_size - pkt_size) / channels;
		latency = latency * USEC_PER_SEC /
			  (params->rate_num / params->rate_den);
		mcbsp->latency[substream->stream] = latency;

		omap_mcbsp_set_threshold(substream, pkt_size);
	}

	dma_data->maxburst = pkt_size;

	if (mcbsp->configured) {
		/* McBSP already configured by another stream */
		return 0;
	}

	regs->rcr2	&= ~(RPHASE | RFRLEN2(0x7f) | RWDLEN2(7));
	regs->xcr2	&= ~(RPHASE | XFRLEN2(0x7f) | XWDLEN2(7));
	regs->rcr1	&= ~(RFRLEN1(0x7f) | RWDLEN1(7));
	regs->xcr1	&= ~(XFRLEN1(0x7f) | XWDLEN1(7));
	format = mcbsp->fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	wpf = channels;
	if (channels == 2 && (format == SND_SOC_DAIFMT_I2S ||
			      format == SND_SOC_DAIFMT_LEFT_J)) {
		/* Use dual-phase frames */
		regs->rcr2	|= RPHASE;
		regs->xcr2	|= XPHASE;
		/* Set 1 word per (McBSP) frame for phase1 and phase2 */
		wpf--;
		regs->rcr2	|= RFRLEN2(wpf - 1);
		regs->xcr2	|= XFRLEN2(wpf - 1);
	}

	regs->rcr1	|= RFRLEN1(wpf - 1);
	regs->xcr1	|= XFRLEN1(wpf - 1);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		/* Set word lengths */
		regs->rcr2	|= RWDLEN2(OMAP_MCBSP_WORD_16);
		regs->rcr1	|= RWDLEN1(OMAP_MCBSP_WORD_16);
		regs->xcr2	|= XWDLEN2(OMAP_MCBSP_WORD_16);
		regs->xcr1	|= XWDLEN1(OMAP_MCBSP_WORD_16);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		/* Set word lengths */
		regs->rcr2	|= RWDLEN2(OMAP_MCBSP_WORD_32);
		regs->rcr1	|= RWDLEN1(OMAP_MCBSP_WORD_32);
		regs->xcr2	|= XWDLEN2(OMAP_MCBSP_WORD_32);
		regs->xcr1	|= XWDLEN1(OMAP_MCBSP_WORD_32);
		break;
	default:
		/* Unsupported PCM format */
		return -EINVAL;
	}

	/* In McBSP master modes, FRAME (i.e. sample rate) is generated
	 * by _counting_ BCLKs. Calculate frame size in BCLKs */
	master = mcbsp->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK;
	if (master == SND_SOC_DAIFMT_BP_FP) {
		div = mcbsp->clk_div ? mcbsp->clk_div : 1;
		framesize = (mcbsp->in_freq / div) / params_rate(params);

		if (framesize < wlen * channels) {
			printk(KERN_ERR "%s: not enough bandwidth for desired rate and "
					"channels\n", __func__);
			return -EINVAL;
		}
	} else
		framesize = wlen * channels;

	/* Set FS period and length in terms of bit clock periods */
	regs->srgr2	&= ~FPER(0xfff);
	regs->srgr1	&= ~FWID(0xff);
	switch (format) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		regs->srgr2	|= FPER(framesize - 1);
		regs->srgr1	|= FWID((framesize >> 1) - 1);
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		regs->srgr2	|= FPER(framesize - 1);
		regs->srgr1	|= FWID(0);
		break;
	}

	omap_mcbsp_config(mcbsp, &mcbsp->cfg_regs);
	mcbsp->wlen = wlen;
	mcbsp->configured = 1;

	return 0;
}

/*
 * This must be called before _set_clkdiv and _set_sysclk since McBSP register
 * cache is initialized here
 */
static int omap_mcbsp_dai_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				      unsigned int fmt)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	bool inv_fs = false;

	if (mcbsp->configured)
		return 0;

	mcbsp->fmt = fmt;
	memset(regs, 0, sizeof(*regs));
	/* Generic McBSP register settings */
	regs->spcr2	|= XINTM(3) | FREE;
	regs->spcr1	|= RINTM(3);
	/* RFIG and XFIG are not defined in 2430 and on OMAP3+ */
	if (!mcbsp->pdata->has_ccr) {
		regs->rcr2	|= RFIG;
		regs->xcr2	|= XFIG;
	}

	/* Configure XCCR/RCCR only for revisions which have ccr registers */
	if (mcbsp->pdata->has_ccr) {
		regs->xccr = DXENDLY(1) | XDMAEN | XDISABLE;
		regs->rccr = RFULL_CYCLE | RDMAEN | RDISABLE;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* 1-bit data delay */
		regs->rcr2	|= RDATDLY(1);
		regs->xcr2	|= XDATDLY(1);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* 0-bit data delay */
		regs->rcr2	|= RDATDLY(0);
		regs->xcr2	|= XDATDLY(0);
		regs->spcr1	|= RJUST(2);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* 1-bit data delay */
		regs->rcr2      |= RDATDLY(1);
		regs->xcr2      |= XDATDLY(1);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/* 0-bit data delay */
		regs->rcr2      |= RDATDLY(0);
		regs->xcr2      |= XDATDLY(0);
		/* Invert FS polarity configuration */
		inv_fs = true;
		break;
	default:
		/* Unsupported data format */
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		/* McBSP master. Set FS and bit clocks as outputs */
		regs->pcr0	|= FSXM | FSRM |
				   CLKXM | CLKRM;
		/* Sample rate generator drives the FS */
		regs->srgr2	|= FSGM;
		break;
	case SND_SOC_DAIFMT_BC_FP:
		/* McBSP slave. FS clock as output */
		regs->srgr2	|= FSGM;
		regs->pcr0	|= FSXM | FSRM;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* McBSP slave */
		break;
	default:
		/* Unsupported master/slave configuration */
		return -EINVAL;
	}

	/* Set bit clock (CLKX/CLKR) and FS polarities */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/*
		 * Normal BCLK + FS.
		 * FS active low. TX data driven on falling edge of bit clock
		 * and RX data sampled on rising edge of bit clock.
		 */
		regs->pcr0	|= FSXP | FSRP |
				   CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		regs->pcr0	|= CLKXP | CLKRP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		regs->pcr0	|= FSXP | FSRP;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}
	if (inv_fs)
		regs->pcr0 ^= FSXP | FSRP;

	return 0;
}

static int omap_mcbsp_dai_set_clkdiv(struct snd_soc_dai *cpu_dai,
				     int div_id, int div)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;

	if (div_id != OMAP_MCBSP_CLKGDV)
		return -ENODEV;

	mcbsp->clk_div = div;
	regs->srgr1	&= ~CLKGDV(0xff);
	regs->srgr1	|= CLKGDV(div - 1);

	return 0;
}

static int omap_mcbsp_dai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					 int clk_id, unsigned int freq,
					 int dir)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(cpu_dai);
	struct omap_mcbsp_reg_cfg *regs = &mcbsp->cfg_regs;
	int err = 0;

	if (mcbsp->active) {
		if (freq == mcbsp->in_freq)
			return 0;
		else
			return -EBUSY;
	}

	mcbsp->in_freq = freq;
	regs->srgr2 &= ~CLKSM;
	regs->pcr0 &= ~SCLKME;

	switch (clk_id) {
	case OMAP_MCBSP_SYSCLK_CLK:
		regs->srgr2	|= CLKSM;
		break;
	case OMAP_MCBSP_SYSCLK_CLKS_FCLK:
		if (mcbsp_omap1()) {
			err = -EINVAL;
			break;
		}
		err = omap2_mcbsp_set_clks_src(mcbsp,
					       MCBSP_CLKS_PRCM_SRC);
		break;
	case OMAP_MCBSP_SYSCLK_CLKS_EXT:
		if (mcbsp_omap1()) {
			err = 0;
			break;
		}
		err = omap2_mcbsp_set_clks_src(mcbsp,
					       MCBSP_CLKS_PAD_SRC);
		break;

	case OMAP_MCBSP_SYSCLK_CLKX_EXT:
		regs->srgr2	|= CLKSM;
		regs->pcr0	|= SCLKME;
		/*
		 * If McBSP is master but yet the CLKX/CLKR pin drives the SRG,
		 * disable output on those pins. This enables to inject the
		 * reference clock through CLKX/CLKR. For this to work
		 * set_dai_sysclk() _needs_ to be called after set_dai_fmt().
		 */
		regs->pcr0	&= ~CLKXM;
		break;
	case OMAP_MCBSP_SYSCLK_CLKR_EXT:
		regs->pcr0	|= SCLKME;
		/* Disable ouput on CLKR pin in master mode */
		regs->pcr0	&= ~CLKRM;
		break;
	default:
		err = -ENODEV;
	}

	return err;
}

static const struct snd_soc_dai_ops mcbsp_dai_ops = {
	.startup	= omap_mcbsp_dai_startup,
	.shutdown	= omap_mcbsp_dai_shutdown,
	.prepare	= omap_mcbsp_dai_prepare,
	.trigger	= omap_mcbsp_dai_trigger,
	.delay		= omap_mcbsp_dai_delay,
	.hw_params	= omap_mcbsp_dai_hw_params,
	.set_fmt	= omap_mcbsp_dai_set_dai_fmt,
	.set_clkdiv	= omap_mcbsp_dai_set_clkdiv,
	.set_sysclk	= omap_mcbsp_dai_set_dai_sysclk,
};

static int omap_mcbsp_probe(struct snd_soc_dai *dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_enable(mcbsp->dev);

	snd_soc_dai_init_dma_data(dai,
				  &mcbsp->dma_data[SNDRV_PCM_STREAM_PLAYBACK],
				  &mcbsp->dma_data[SNDRV_PCM_STREAM_CAPTURE]);

	return 0;
}

static int omap_mcbsp_remove(struct snd_soc_dai *dai)
{
	struct omap_mcbsp *mcbsp = snd_soc_dai_get_drvdata(dai);

	pm_runtime_disable(mcbsp->dev);

	return 0;
}

static struct snd_soc_dai_driver omap_mcbsp_dai = {
	.probe = omap_mcbsp_probe,
	.remove = omap_mcbsp_remove,
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = OMAP_MCBSP_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &mcbsp_dai_ops,
};

static const struct snd_soc_component_driver omap_mcbsp_component = {
	.name			= "omap-mcbsp",
	.legacy_dai_naming	= 1,
};

static struct omap_mcbsp_platform_data omap2420_pdata = {
	.reg_step = 4,
	.reg_size = 2,
};

static struct omap_mcbsp_platform_data omap2430_pdata = {
	.reg_step = 4,
	.reg_size = 4,
	.has_ccr = true,
};

static struct omap_mcbsp_platform_data omap3_pdata = {
	.reg_step = 4,
	.reg_size = 4,
	.has_ccr = true,
	.has_wakeup = true,
};

static struct omap_mcbsp_platform_data omap4_pdata = {
	.reg_step = 4,
	.reg_size = 4,
	.has_ccr = true,
	.has_wakeup = true,
};

static const struct of_device_id omap_mcbsp_of_match[] = {
	{
		.compatible = "ti,omap2420-mcbsp",
		.data = &omap2420_pdata,
	},
	{
		.compatible = "ti,omap2430-mcbsp",
		.data = &omap2430_pdata,
	},
	{
		.compatible = "ti,omap3-mcbsp",
		.data = &omap3_pdata,
	},
	{
		.compatible = "ti,omap4-mcbsp",
		.data = &omap4_pdata,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, omap_mcbsp_of_match);

static int asoc_mcbsp_probe(struct platform_device *pdev)
{
	struct omap_mcbsp_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct omap_mcbsp *mcbsp;
	const struct of_device_id *match;
	int ret;

	match = of_match_device(omap_mcbsp_of_match, &pdev->dev);
	if (match) {
		struct device_node *node = pdev->dev.of_node;
		struct omap_mcbsp_platform_data *pdata_quirk = pdata;
		int buffer_size;

		pdata = devm_kzalloc(&pdev->dev,
				     sizeof(struct omap_mcbsp_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		memcpy(pdata, match->data, sizeof(*pdata));
		if (!of_property_read_u32(node, "ti,buffer-size", &buffer_size))
			pdata->buffer_size = buffer_size;
		if (pdata_quirk)
			pdata->force_ick_on = pdata_quirk->force_ick_on;
	} else if (!pdata) {
		dev_err(&pdev->dev, "missing platform data.\n");
		return -EINVAL;
	}
	mcbsp = devm_kzalloc(&pdev->dev, sizeof(struct omap_mcbsp), GFP_KERNEL);
	if (!mcbsp)
		return -ENOMEM;

	mcbsp->id = pdev->id;
	mcbsp->pdata = pdata;
	mcbsp->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcbsp);

	ret = omap_mcbsp_init(pdev);
	if (ret)
		return ret;

	if (mcbsp->pdata->reg_size == 2) {
		omap_mcbsp_dai.playback.formats = SNDRV_PCM_FMTBIT_S16_LE;
		omap_mcbsp_dai.capture.formats = SNDRV_PCM_FMTBIT_S16_LE;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &omap_mcbsp_component,
					      &omap_mcbsp_dai, 1);
	if (ret)
		return ret;

	return sdma_pcm_platform_register(&pdev->dev, "tx", "rx");
}

static void asoc_mcbsp_remove(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);

	if (mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(mcbsp->id);

	if (cpu_latency_qos_request_active(&mcbsp->pm_qos_req))
		cpu_latency_qos_remove_request(&mcbsp->pm_qos_req);
}

static struct platform_driver asoc_mcbsp_driver = {
	.driver = {
			.name = "omap-mcbsp",
			.of_match_table = omap_mcbsp_of_match,
	},

	.probe = asoc_mcbsp_probe,
	.remove_new = asoc_mcbsp_remove,
};

module_platform_driver(asoc_mcbsp_driver);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@bitmer.com>");
MODULE_DESCRIPTION("OMAP I2S SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-mcbsp");
