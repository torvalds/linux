// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

/*
 * Hardware interface for audio DSPs via SPI
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>

#include <linux/device.h>
#include <sound/sof.h>
#include <uapi/sound/sof-fw.h>

#include "sof-priv.h"
#include "ops.h"
#include "intel.h"

/*
 * Memory copy.
 */

static void spi_block_write(struct snd_sof_dev *sdev, u32 offset, void *src,
			    size_t size)
{
	// use spi_write() to copy data to DSP
}

static void spi_block_read(struct snd_sof_dev *sdev, u32 offset, void *dest,
			   size_t size)
{
	// use spi_read() to copy data from DSP
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

	dev_info(sdev->dev, " Firmware info: version %d:%d-%s build %d on %s:%s\n",
		 v->major, v->minor, v->tag, v->build, v->date, v->time);

	return 0;
}

/*
 * IPC Mailbox IO
 */

static void spi_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
			      void *message, size_t bytes)
{
	void __iomem *dest = sdev->bar[sdev->mailbox_bar] + offset;

	//memcpy_toio(dest, message, bytes);
	/*
	 * this will copy to a local memory buffer that will be sent to DSP via
	 * SPI at next IPC
	 */
}

static void spi_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
			     void *message, size_t bytes)
{
	void __iomem *src = sdev->bar[sdev->mailbox_bar] + offset;

	//memcpy_fromio(message, src, bytes);
	/*
	 * this will copy from a local memory buffer that will be received from
	 * DSP via SPI at last IPC
	 */
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t spi_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	int ret = IRQ_NONE;

	// on SPI based devices this will likely come via a SoC GPIO IRQ

	// check if GPIO is assetred and if so run thread.

	return ret;
}

static irqreturn_t spi_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;

	// read SPI data into local buffer and determine IPC cmd or reply

	/*
	 * if reply. Handle Immediate reply from DSP Core and set DSP
	 * state to ready
	 */
	//snd_sof_ipc_reply(sdev, ipcx);

	/* if cmd, Handle messages from DSP Core */
	//snd_sof_ipc_msgs_rx(sdev);

	return IRQ_HANDLED;
}

static int spi_is_ready(struct snd_sof_dev *sdev)
{
	// use local variable to store DSP command state. either DSP is ready
	// for new cmd or still processing current cmd.

	return 1;
}

static int spi_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	u64 cmd = msg->header;

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
			dev_err(sdev->dev, "error: reply expected 0x%lx got 0x%x bytes\n",
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
	struct snd_sof_pdata *pdata = sdev->pdata;
	const struct sof_dev_desc *desc = pdata->desc;
	struct platform_device *pdev =
		container_of(sdev->parent, struct platform_device, dev);
	int ret = 0;

	/* get IRQ from Device tree or ACPI - register our IRQ */
	dev_dbg(sdev->dev, "using IRQ %d\n", sdev->ipc_irq);
	ret = request_threaded_irq(sdev->ipc_irq, spi_irq_handler,
				   spi_irq_thread, IRQF_SHARED, "AudioDSP",
				   sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		goto irq_err;
	}

	return ret;
}

static int spi_sof_remove(struct snd_sof_dev *sdev)
{
	free_irq(sdev->ipc_irq, sdev);
	return 0;
}

/* baytrail ops */
struct snd_sof_dsp_ops snd_sof_spi_ops = {
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
	.debug_map	= spi_debugfs,
	.debug_map_count	= ARRAY_SIZE(spi_debugfs),
	.dbg_dump	= spi_dump,

	/* module loading */
	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,
};
EXPORT_SYMBOL(snd_sof_spi_ops);

MODULE_LICENSE("Dual BSD/GPL");
