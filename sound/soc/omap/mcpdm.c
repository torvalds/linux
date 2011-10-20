/*
 * mcpdm.c  --	McPDM interface driver
 *
 * Author: Jorge Eduardo Candelaria <x0107209@ti.com>
 * Copyright (C) 2009 - Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>

#include "mcpdm.h"

static struct omap_mcpdm *mcpdm;

static inline void omap_mcpdm_write(u16 reg, u32 val)
{
	__raw_writel(val, mcpdm->io_base + reg);
}

static inline int omap_mcpdm_read(u16 reg)
{
	return __raw_readl(mcpdm->io_base + reg);
}

static void omap_mcpdm_reg_dump(void)
{
	dev_dbg(mcpdm->dev, "***********************\n");
	dev_dbg(mcpdm->dev, "IRQSTATUS_RAW:  0x%04x\n",
			omap_mcpdm_read(MCPDM_IRQSTATUS_RAW));
	dev_dbg(mcpdm->dev, "IRQSTATUS:	0x%04x\n",
			omap_mcpdm_read(MCPDM_IRQSTATUS));
	dev_dbg(mcpdm->dev, "IRQENABLE_SET:  0x%04x\n",
			omap_mcpdm_read(MCPDM_IRQENABLE_SET));
	dev_dbg(mcpdm->dev, "IRQENABLE_CLR:  0x%04x\n",
			omap_mcpdm_read(MCPDM_IRQENABLE_CLR));
	dev_dbg(mcpdm->dev, "IRQWAKE_EN: 0x%04x\n",
			omap_mcpdm_read(MCPDM_IRQWAKE_EN));
	dev_dbg(mcpdm->dev, "DMAENABLE_SET: 0x%04x\n",
			omap_mcpdm_read(MCPDM_DMAENABLE_SET));
	dev_dbg(mcpdm->dev, "DMAENABLE_CLR:  0x%04x\n",
			omap_mcpdm_read(MCPDM_DMAENABLE_CLR));
	dev_dbg(mcpdm->dev, "DMAWAKEEN:	0x%04x\n",
			omap_mcpdm_read(MCPDM_DMAWAKEEN));
	dev_dbg(mcpdm->dev, "CTRL:	0x%04x\n",
			omap_mcpdm_read(MCPDM_CTRL));
	dev_dbg(mcpdm->dev, "DN_DATA:  0x%04x\n",
			omap_mcpdm_read(MCPDM_DN_DATA));
	dev_dbg(mcpdm->dev, "UP_DATA: 0x%04x\n",
			omap_mcpdm_read(MCPDM_UP_DATA));
	dev_dbg(mcpdm->dev, "FIFO_CTRL_DN: 0x%04x\n",
			omap_mcpdm_read(MCPDM_FIFO_CTRL_DN));
	dev_dbg(mcpdm->dev, "FIFO_CTRL_UP:  0x%04x\n",
			omap_mcpdm_read(MCPDM_FIFO_CTRL_UP));
	dev_dbg(mcpdm->dev, "DN_OFFSET:	0x%04x\n",
			omap_mcpdm_read(MCPDM_DN_OFFSET));
	dev_dbg(mcpdm->dev, "***********************\n");
}

/*
 * Takes the McPDM module in and out of reset state.
 * Uplink and downlink can be reset individually.
 */
static void omap_mcpdm_reset_capture(int reset)
{
	int ctrl = omap_mcpdm_read(MCPDM_CTRL);

	if (reset)
		ctrl |= SW_UP_RST;
	else
		ctrl &= ~SW_UP_RST;

	omap_mcpdm_write(MCPDM_CTRL, ctrl);
}

static void omap_mcpdm_reset_playback(int reset)
{
	int ctrl = omap_mcpdm_read(MCPDM_CTRL);

	if (reset)
		ctrl |= SW_DN_RST;
	else
		ctrl &= ~SW_DN_RST;

	omap_mcpdm_write(MCPDM_CTRL, ctrl);
}

/*
 * Enables the transfer through the PDM interface to/from the Phoenix
 * codec by enabling the corresponding UP or DN channels.
 */
void omap_mcpdm_start(int stream)
{
	int ctrl = omap_mcpdm_read(MCPDM_CTRL);

	if (stream)
		ctrl |= mcpdm->up_channels;
	else
		ctrl |= mcpdm->dn_channels;

	omap_mcpdm_write(MCPDM_CTRL, ctrl);
}

/*
 * Disables the transfer through the PDM interface to/from the Phoenix
 * codec by disabling the corresponding UP or DN channels.
 */
void omap_mcpdm_stop(int stream)
{
	int ctrl = omap_mcpdm_read(MCPDM_CTRL);

	if (stream)
		ctrl &= ~mcpdm->up_channels;
	else
		ctrl &= ~mcpdm->dn_channels;

	omap_mcpdm_write(MCPDM_CTRL, ctrl);
}

/*
 * Configures McPDM uplink for audio recording.
 * This function should be called before omap_mcpdm_start.
 */
int omap_mcpdm_capture_open(struct omap_mcpdm_link *uplink)
{
	int irq_mask = 0;
	int ctrl;

	if (!uplink)
		return -EINVAL;

	mcpdm->uplink = uplink;

	/* Enable irq request generation */
	irq_mask |= uplink->irq_mask & MCPDM_UPLINK_IRQ_MASK;
	omap_mcpdm_write(MCPDM_IRQENABLE_SET, irq_mask);

	/* Configure uplink threshold */
	if (uplink->threshold > UP_THRES_MAX)
		uplink->threshold = UP_THRES_MAX;

	omap_mcpdm_write(MCPDM_FIFO_CTRL_UP, uplink->threshold);

	/* Configure DMA controller */
	omap_mcpdm_write(MCPDM_DMAENABLE_SET, DMA_UP_ENABLE);

	/* Set pdm out format */
	ctrl = omap_mcpdm_read(MCPDM_CTRL);
	ctrl &= ~PDMOUTFORMAT;
	ctrl |= uplink->format & PDMOUTFORMAT;

	/* Uplink channels */
	mcpdm->up_channels = uplink->channels & (PDM_UP_MASK | PDM_STATUS_MASK);

	omap_mcpdm_write(MCPDM_CTRL, ctrl);

	return 0;
}

/*
 * Configures McPDM downlink for audio playback.
 * This function should be called before omap_mcpdm_start.
 */
int omap_mcpdm_playback_open(struct omap_mcpdm_link *downlink)
{
	int irq_mask = 0;
	int ctrl;

	if (!downlink)
		return -EINVAL;

	mcpdm->downlink = downlink;

	/* Enable irq request generation */
	irq_mask |= downlink->irq_mask & MCPDM_DOWNLINK_IRQ_MASK;
	omap_mcpdm_write(MCPDM_IRQENABLE_SET, irq_mask);

	/* Configure uplink threshold */
	if (downlink->threshold > DN_THRES_MAX)
		downlink->threshold = DN_THRES_MAX;

	omap_mcpdm_write(MCPDM_FIFO_CTRL_DN, downlink->threshold);

	/* Enable DMA request generation */
	omap_mcpdm_write(MCPDM_DMAENABLE_SET, DMA_DN_ENABLE);

	/* Set pdm out format */
	ctrl = omap_mcpdm_read(MCPDM_CTRL);
	ctrl &= ~PDMOUTFORMAT;
	ctrl |= downlink->format & PDMOUTFORMAT;

	/* Downlink channels */
	mcpdm->dn_channels = downlink->channels & (PDM_DN_MASK | PDM_CMD_MASK);

	omap_mcpdm_write(MCPDM_CTRL, ctrl);

	return 0;
}

/*
 * Cleans McPDM uplink configuration.
 * This function should be called when the stream is closed.
 */
int omap_mcpdm_capture_close(struct omap_mcpdm_link *uplink)
{
	int irq_mask = 0;

	if (!uplink)
		return -EINVAL;

	/* Disable irq request generation */
	irq_mask |= uplink->irq_mask & MCPDM_UPLINK_IRQ_MASK;
	omap_mcpdm_write(MCPDM_IRQENABLE_CLR, irq_mask);

	/* Disable DMA request generation */
	omap_mcpdm_write(MCPDM_DMAENABLE_CLR, DMA_UP_ENABLE);

	/* Clear Downlink channels */
	mcpdm->up_channels = 0;

	mcpdm->uplink = NULL;

	return 0;
}

/*
 * Cleans McPDM downlink configuration.
 * This function should be called when the stream is closed.
 */
int omap_mcpdm_playback_close(struct omap_mcpdm_link *downlink)
{
	int irq_mask = 0;

	if (!downlink)
		return -EINVAL;

	/* Disable irq request generation */
	irq_mask |= downlink->irq_mask & MCPDM_DOWNLINK_IRQ_MASK;
	omap_mcpdm_write(MCPDM_IRQENABLE_CLR, irq_mask);

	/* Disable DMA request generation */
	omap_mcpdm_write(MCPDM_DMAENABLE_CLR, DMA_DN_ENABLE);

	/* clear Downlink channels */
	mcpdm->dn_channels = 0;

	mcpdm->downlink = NULL;

	return 0;
}

static irqreturn_t omap_mcpdm_irq_handler(int irq, void *dev_id)
{
	struct omap_mcpdm *mcpdm_irq = dev_id;
	int irq_status;

	irq_status = omap_mcpdm_read(MCPDM_IRQSTATUS);

	/* Acknowledge irq event */
	omap_mcpdm_write(MCPDM_IRQSTATUS, irq_status);

	if (irq & MCPDM_DN_IRQ_FULL) {
		dev_err(mcpdm_irq->dev, "DN FIFO error %x\n", irq_status);
		omap_mcpdm_reset_playback(1);
		omap_mcpdm_playback_open(mcpdm_irq->downlink);
		omap_mcpdm_reset_playback(0);
	}

	if (irq & MCPDM_DN_IRQ_EMPTY) {
		dev_err(mcpdm_irq->dev, "DN FIFO error %x\n", irq_status);
		omap_mcpdm_reset_playback(1);
		omap_mcpdm_playback_open(mcpdm_irq->downlink);
		omap_mcpdm_reset_playback(0);
	}

	if (irq & MCPDM_DN_IRQ) {
		dev_dbg(mcpdm_irq->dev, "DN write request\n");
	}

	if (irq & MCPDM_UP_IRQ_FULL) {
		dev_err(mcpdm_irq->dev, "UP FIFO error %x\n", irq_status);
		omap_mcpdm_reset_capture(1);
		omap_mcpdm_capture_open(mcpdm_irq->uplink);
		omap_mcpdm_reset_capture(0);
	}

	if (irq & MCPDM_UP_IRQ_EMPTY) {
		dev_err(mcpdm_irq->dev, "UP FIFO error %x\n", irq_status);
		omap_mcpdm_reset_capture(1);
		omap_mcpdm_capture_open(mcpdm_irq->uplink);
		omap_mcpdm_reset_capture(0);
	}

	if (irq & MCPDM_UP_IRQ) {
		dev_dbg(mcpdm_irq->dev, "UP write request\n");
	}

	return IRQ_HANDLED;
}

int omap_mcpdm_request(void)
{
	int ret;

	clk_enable(mcpdm->clk);

	spin_lock(&mcpdm->lock);

	if (!mcpdm->free) {
		dev_err(mcpdm->dev, "McPDM interface is in use\n");
		spin_unlock(&mcpdm->lock);
		ret = -EBUSY;
		goto err;
	}
	mcpdm->free = 0;

	spin_unlock(&mcpdm->lock);

	/* Disable lines while request is ongoing */
	omap_mcpdm_write(MCPDM_CTRL, 0x00);

	ret = request_irq(mcpdm->irq, omap_mcpdm_irq_handler,
				0, "McPDM", (void *)mcpdm);
	if (ret) {
		dev_err(mcpdm->dev, "Request for McPDM IRQ failed\n");
		goto err;
	}

	return 0;

err:
	clk_disable(mcpdm->clk);
	return ret;
}

void omap_mcpdm_free(void)
{
	spin_lock(&mcpdm->lock);
	if (mcpdm->free) {
		dev_err(mcpdm->dev, "McPDM interface is already free\n");
		spin_unlock(&mcpdm->lock);
		return;
	}
	mcpdm->free = 1;
	spin_unlock(&mcpdm->lock);

	clk_disable(mcpdm->clk);

	free_irq(mcpdm->irq, (void *)mcpdm);
}

/* Enable/disable DC offset cancelation for the analog
 * headset path (PDM channels 1 and 2).
 */
int omap_mcpdm_set_offset(int offset1, int offset2)
{
	int offset;

	if ((offset1 > DN_OFST_MAX) || (offset2 > DN_OFST_MAX))
		return -EINVAL;

	offset = (offset1 << DN_OFST_RX1) | (offset2 << DN_OFST_RX2);

	/* offset cancellation for channel 1 */
	if (offset1)
		offset |= DN_OFST_RX1_EN;
	else
		offset &= ~DN_OFST_RX1_EN;

	/* offset cancellation for channel 2 */
	if (offset2)
		offset |= DN_OFST_RX2_EN;
	else
		offset &= ~DN_OFST_RX2_EN;

	omap_mcpdm_write(MCPDM_DN_OFFSET, offset);

	return 0;
}

int __devinit omap_mcpdm_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = 0;

	mcpdm = kzalloc(sizeof(struct omap_mcpdm), GFP_KERNEL);
	if (!mcpdm) {
		ret = -ENOMEM;
		goto exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no resource\n");
		goto err_resource;
	}

	spin_lock_init(&mcpdm->lock);
	mcpdm->free = 1;
	mcpdm->io_base = ioremap(res->start, resource_size(res));
	if (!mcpdm->io_base) {
		ret = -ENOMEM;
		goto err_resource;
	}

	mcpdm->irq = platform_get_irq(pdev, 0);

	mcpdm->clk = clk_get(&pdev->dev, "pdm_ck");
	if (IS_ERR(mcpdm->clk)) {
		ret = PTR_ERR(mcpdm->clk);
		dev_err(&pdev->dev, "unable to get pdm_ck: %d\n", ret);
		goto err_clk;
	}

	mcpdm->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcpdm);

	return 0;

err_clk:
	iounmap(mcpdm->io_base);
err_resource:
	kfree(mcpdm);
exit:
	return ret;
}

int omap_mcpdm_remove(struct platform_device *pdev)
{
	struct omap_mcpdm *mcpdm_ptr = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	clk_put(mcpdm_ptr->clk);

	iounmap(mcpdm_ptr->io_base);

	mcpdm_ptr->clk = NULL;
	mcpdm_ptr->free = 0;
	mcpdm_ptr->dev = NULL;

	kfree(mcpdm_ptr);

	return 0;
}

