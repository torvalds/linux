// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/*
 * Hardware interface for audio DSPs via SPI
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <sound/sof.h>
#include <uapi/sound/sof/fw.h>
#include "intel/shim.h"
#include "sof-priv.h"
#include "hw-spi.h"
#include "ops.h"

/*
 * Memory copy.
 */

static void spi_block_read(struct snd_sof_dev *sdev, u32 offset, void *dest,
			   size_t size)
{
	u8 *buf;
	int ret;

	if (offset) {
		buf = kmalloc(size + offset, GFP_KERNEL);
		if (!buf) {
			dev_err(sdev->dev, "error: buffer allocation failed\n");
			return;
		}
	} else {
		buf = dest;
	}

	ret = spi_read(to_spi_device(sdev->dev), buf, size + offset);
	if (ret < 0)
		dev_err(sdev->dev, "error: SPI read failed: %d\n", ret);

	if (offset) {
		memcpy(dest, buf + offset, size);
		kfree(buf);
	}
}

static void spi_block_write(struct snd_sof_dev *sdev, u32 offset, void *src,
			    size_t size)
{
	int ret;
	u8 *buf;

	if (offset) {
		buf = kmalloc(size + offset, GFP_KERNEL);
		if (!buf) {
			dev_err(sdev->dev, "error: buffer allocation failed\n");
			return;
		}

		/* Use Read-Modify-Wwrite */
		ret = spi_read(to_spi_device(sdev->dev), buf, size + offset);
		if (ret < 0) {
			dev_err(sdev->dev, "error: SPI read failed: %d\n", ret);
			goto free;
		}

		memcpy(buf + offset, src, size);
	} else {
		buf = src;
	}

	ret = spi_write(to_spi_device(sdev->dev), buf, size + offset);
	if (ret < 0)
		dev_err(sdev->dev, "error: SPI write failed: %d\n", ret);

free:
	if (offset)
		kfree(buf);
}

/*
 * IPC Firmware ready.
 */
static int spi_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_fw_ready *fw_ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &fw_ready->version;

	dev_dbg(sdev->dev, "ipc: DSP is ready 0x%8.8x\n", msg_id);

	// read local buffer with SPI data

	dev_info(sdev->dev, "Firmware info: version %d:%d-%s build %d on %s:%s\n",
		 v->major, v->minor, v->tag, v->build, v->date, v->time);

	return 0;
}

/*
 * IPC Mailbox IO
 */

static void spi_mailbox_write(struct snd_sof_dev *sdev __maybe_unused,
			      u32 offset __maybe_unused,
			      void *message __maybe_unused,
			      size_t bytes __maybe_unused)
{
	/*
	 * this will copy to a local memory buffer that will be sent to DSP via
	 * SPI at next IPC
	 */
}

static void spi_mailbox_read(struct snd_sof_dev *sdev __maybe_unused,
			     u32 offset __maybe_unused,
			     void *message, size_t bytes)
{
	memset(message, 0, bytes);

	/*
	 * this will copy from a local memory buffer that was received from
	 * DSP via SPI at last IPC
	 */
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

/*
 * If the handler only has to wake up the thread, we might use the standard one
 * as well
 */
static irqreturn_t spi_irq_handler(int irq __maybe_unused, void *context)
{
	const struct snd_sof_dev *sdev = context;
	const struct platform_device *pdev =
		container_of(sdev->parent, struct platform_device, dev);
	struct snd_sof_pdata *sof_pdata = dev_get_platdata(&pdev->dev);
	struct sof_spi_dev *sof_spi =
		(struct sof_spi_dev *)sof_pdata->hw_pdata;

	// on SPI based devices this will likely come via a SoC GPIO IRQ

	// check if GPIO is assetred and if so run thread.
	if (sof_spi->gpio >= 0 &&
	    gpio_get_value(sof_spi->gpio) == sof_spi->active)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static irqreturn_t spi_irq_thread(int irq __maybe_unused, void *context __maybe_unused)
{
	// read SPI data into local buffer and determine IPC cmd or reply

	/*
	 * if reply. Handle Immediate reply from DSP Core and set DSP
	 * state to ready
	 */

	/* if cmd, Handle messages from DSP Core */

	return IRQ_HANDLED;
}

static int spi_is_ready(struct snd_sof_dev *sdev __maybe_unused)
{
	// use local variable to store DSP command state. either DSP is ready
	// for new cmd or still processing current cmd.

	return 1;
}

static int spi_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	/* send the message */
	spi_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);

	return 0;
}

static int spi_get_reply(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct sof_ipc_reply reply;
	int ret = 0;
	u32 size;

	/* get reply */
	spi_mailbox_read(sdev, sdev->host_box.offset, &reply, sizeof(reply));
	if (reply.error < 0) {
		size = sizeof(reply);
		ret = reply.error;
	} else {
		/* reply correct size ? */
		if (reply.hdr.size != msg->reply_size) {
			dev_err(sdev->dev, "error: reply expected 0x%zx got 0x%x bytes\n",
				msg->reply_size, reply.hdr.size);
			size = msg->reply_size;
			ret = -EINVAL;
		} else {
			size = reply.hdr.size;
		}
	}

	/* read the message */
	if (msg->msg_data && size > 0)
		spi_mailbox_read(sdev, sdev->host_box.offset, msg->reply_data,
				 size);

	return ret;
}

/*
 * Probe and remove.
 */

static int spi_sof_probe(struct snd_sof_dev *sdev)
{
	struct platform_device *pdev =
		container_of(sdev->parent, struct platform_device, dev);
	struct snd_sof_pdata *sof_pdata = dev_get_platdata(&pdev->dev);
	struct sof_spi_dev *sof_spi =
                (struct sof_spi_dev *)sof_pdata->hw_pdata;
	/* get IRQ from Device tree or ACPI - register our IRQ */
	struct irq_data *irqd;
	struct spi_device *spi = to_spi_device(pdev->dev.parent);
	unsigned int irq_trigger, irq_sense;
	int ret;

	sdev->ipc_irq = spi->irq;
	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	irqd = irq_get_irq_data(sdev->ipc_irq);
	if (!irqd)
		return -EINVAL;

	irq_trigger = irqd_get_trigger_type(irqd);
	irq_sense = irq_trigger & IRQ_TYPE_SENSE_MASK;
	sof_spi->active = irq_sense == IRQ_TYPE_EDGE_RISING ||
		irq_sense == IRQ_TYPE_LEVEL_HIGH;

	ret = devm_request_threaded_irq(sdev->dev, sdev->ipc_irq,
					spi_irq_handler, spi_irq_thread,
					irq_trigger | IRQF_ONESHOT,
					"AudioDSP", sdev);
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);

	return ret;
}

static int spi_sof_remove(struct snd_sof_dev *sdev)
{
	return 0;
}

static int spi_cmd_done(struct snd_sof_dev *sof_dev __maybe_unused, int dir __maybe_unused)
{
	return 0;
}

/* SPI SOF ops */
const struct snd_sof_dsp_ops snd_sof_spi_ops = {
	/* device init */
	.probe		= spi_sof_probe,
	.remove		= spi_sof_remove,

	/* Block IO */
	.block_read	= spi_block_read,
	.block_write	= spi_block_write,

	/* doorbell */
	.irq_handler	= spi_irq_handler,
	.irq_thread	= spi_irq_thread,

	/* mailbox */
	.mailbox_read	= spi_mailbox_read,
	.mailbox_write	= spi_mailbox_write,

	/* ipc */
	.send_msg	= spi_send_msg,
	.get_reply	= spi_get_reply,
	.fw_ready	= spi_fw_ready,
	.is_ready	= spi_is_ready,
	.cmd_done	= spi_cmd_done,

	/* debug */
	.debug_map	= NULL/*spi_debugfs*/,
	.debug_map_count = 0/*ARRAY_SIZE(spi_debugfs)*/,
	.dbg_dump	= NULL/*spi_dump*/,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,
};
EXPORT_SYMBOL(snd_sof_spi_ops);

const struct sof_intel_dsp_desc spi_chip_info = {
	.cores_num = 2,
	.cores_mask = 0x3,
	.ops = &snd_sof_spi_ops,
};
EXPORT_SYMBOL(spi_chip_info);

MODULE_LICENSE("Dual BSD/GPL");
