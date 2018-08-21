// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Jeeja KP <jeeja.kp@intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>

#include "../sof-priv.h"
#include "../ops.h"
#include "hda.h"

int hda_dsp_ipc_cmd_done(struct snd_sof_dev *sdev, int dir)
{
	if (dir == SOF_IPC_HOST_REPLY) {
		/*
		 * tell DSP cmd is done - clear busy
		 * interrupt and send reply msg to dsp
		 */
		snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
					       HDA_DSP_REG_HIPCT,
					       HDA_DSP_REG_HIPCT_BUSY,
					       HDA_DSP_REG_HIPCT_BUSY);

		/* unmask BUSY interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_BUSY,
					HDA_DSP_REG_HIPCCTL_BUSY);
	} else {
		/*
		 * set DONE bit - tell DSP we have received the reply msg
		 * from DSP, and processed it, don't send more reply to host
		 */
		snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
					       HDA_DSP_REG_HIPCIE,
					       HDA_DSP_REG_HIPCIE_DONE,
					       HDA_DSP_REG_HIPCIE_DONE);

		/* unmask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_DONE,
					HDA_DSP_REG_HIPCCTL_DONE);
	}

	return 0;
}

int hda_dsp_ipc_is_ready(struct snd_sof_dev *sdev)
{
	u64 busy, done;

	/* is DSP ready for next IPC command */
	busy = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCI);
	done = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCIE);
	if ((busy & HDA_DSP_REG_HIPCI_BUSY) ||
	    (done & HDA_DSP_REG_HIPCIE_DONE))
		return 0;

	return 1;
}

int hda_dsp_ipc_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	u32 cmd = msg->header;

	/* send IPC message to DSP */
	hda_dsp_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			      msg->msg_size);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCI,
			  cmd | HDA_DSP_REG_HIPCI_BUSY);

	return 0;
}

int hda_dsp_ipc_get_reply(struct snd_sof_dev *sdev,
			  struct snd_sof_ipc_msg *msg)
{
	struct sof_ipc_reply reply;
	int ret = 0;
	u32 size;

	/* get IPC reply from DSP in the mailbox */
	hda_dsp_mailbox_read(sdev, sdev->host_box.offset, &reply,
			     sizeof(reply));
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
		hda_dsp_mailbox_read(sdev, sdev->host_box.offset,
				     msg->reply_data, size);

	return ret;
}

/* IPC handler thread */
irqreturn_t hda_dsp_ipc_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	u32 hipci, hipcie, hipct, hipcte, msg = 0, msg_ext = 0;
	irqreturn_t ret = IRQ_NONE;
	int reply = -EINVAL;

	/* here we handle IPC interrupts only */
	if (!(sdev->irq_status & HDA_DSP_ADSPIS_IPC))
		return ret;

	/* read IPC status */
	hipcie = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_REG_HIPCIE);
	hipct = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCT);

	/* is this a reply message from the DSP */
	if (hipcie & HDA_DSP_REG_HIPCIE_DONE) {

		hipci = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					 HDA_DSP_REG_HIPCI);
		msg = hipci & HDA_DSP_REG_HIPCI_MSG_MASK;
		msg_ext = hipcie & HDA_DSP_REG_HIPCIE_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware response, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* mask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_DONE, 0);

		/* handle immediate reply from DSP core - ignore ROM messages */
		if (msg != 0x1004000)
			reply = snd_sof_ipc_reply(sdev, msg);

		/*
		 * handle immediate reply from DSP core. If the msg is
		 * found, set done bit in cmd_done which is called at the
		 * end of message processing function, else set it here
		 * because the done bit can't be set in cmd_done function
		 * which is triggered by msg
		 */
		if (reply)
			hda_dsp_ipc_cmd_done(sdev, SOF_IPC_DSP_REPLY);

		ret = IRQ_HANDLED;
	}

	/* is this a new message from DSP */
	if (hipct & HDA_DSP_REG_HIPCT_BUSY) {

		hipcte = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					  HDA_DSP_REG_HIPCTE);
		msg = hipct & HDA_DSP_REG_HIPCT_MSG_MASK;
		msg_ext = hipcte & HDA_DSP_REG_HIPCTE_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware initiated, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* handle messages from DSP */
		if ((hipct & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			/* this is a PANIC message !! */
			snd_sof_dsp_panic(sdev, HDA_DSP_PANIC_OFFSET(msg_ext));
		} else {
			/* normal message - process normally*/
			snd_sof_ipc_msgs_rx(sdev);
		}

		/* mask BUSY interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_BUSY, 0);

		ret = IRQ_HANDLED;
	}

	if (ret == IRQ_HANDLED) {
		/* reenable IPC interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
					HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);
	}

	/* wake up sleeper if we are loading code */
	if (sdev->code_loading)	{
		sdev->code_loading = 0;
		wake_up(&sdev->waitq);
	}

	return ret;
}

/* is this IRQ for ADSP ? - we only care about IPC here */
irqreturn_t hda_dsp_ipc_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)context;
	int ret = IRQ_NONE;

	spin_lock(&sdev->hw_lock);

	/* store status */
	sdev->irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					    HDA_DSP_REG_ADSPIS);

	/* invalid message ? */
	if (sdev->irq_status == 0xffffffff)
		goto out;

	/* IPC message ? */
	if (sdev->irq_status & HDA_DSP_ADSPIS_IPC) {
		/* disable IPC interrupt */
		snd_sof_dsp_update_bits_unlocked(sdev, HDA_DSP_BAR,
						 HDA_DSP_REG_ADSPIC,
						 HDA_DSP_ADSPIC_IPC, 0);
		ret = IRQ_WAKE_THREAD;
	}

out:
	spin_unlock(&sdev->hw_lock);
	return ret;
}

/*
 * IPC Firmware ready.
 */

static void ipc_get_windows(struct snd_sof_dev *sdev)
{
	struct sof_ipc_window_elem *elem;
	u32 outbox_offset = 0;
	u32 stream_offset = 0;
	u32 inbox_offset = 0;
	u32 outbox_size = 0;
	u32 stream_size = 0;
	u32 inbox_size = 0;
	int i;

	if (!sdev->info_window) {
		dev_err(sdev->dev, "error: have no window info\n");
		return;
	}

	for (i = 0; i < sdev->info_window->num_windows; i++) {
		elem = &sdev->info_window->window[i];

		switch (elem->type) {
		case SOF_IPC_REGION_UPBOX:
			inbox_offset =
				elem->offset + SRAM_WINDOW_OFFSET(elem->id);
			inbox_size = elem->size;
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    inbox_offset,
						    elem->size, "inbox");
			break;
		case SOF_IPC_REGION_DOWNBOX:
			outbox_offset =
				elem->offset + SRAM_WINDOW_OFFSET(elem->id);
			outbox_size = elem->size;
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    outbox_offset,
						    elem->size, "outbox");
			break;
		case SOF_IPC_REGION_TRACE:
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    elem->offset +
						    SRAM_WINDOW_OFFSET
						    (elem->id),
						    elem->size, "etrace");
			break;
		case SOF_IPC_REGION_DEBUG:
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    elem->offset +
						    SRAM_WINDOW_OFFSET
						    (elem->id),
						    elem->size, "debug");
			break;
		case SOF_IPC_REGION_STREAM:
			stream_offset =
				elem->offset + SRAM_WINDOW_OFFSET(elem->id);
			stream_size = elem->size;
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    elem->offset +
						    SRAM_WINDOW_OFFSET
						    (elem->id),
						    elem->size, "stream");
			break;
		case SOF_IPC_REGION_REGS:
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    elem->offset +
						    SRAM_WINDOW_OFFSET
						    (elem->id),
						    elem->size, "regs");
			break;
		case SOF_IPC_REGION_EXCEPTION:
			sdev->dsp_oops_offset = elem->offset +
						SRAM_WINDOW_OFFSET(elem->id);
			snd_sof_debugfs_create_item(sdev,
						    sdev->bar[HDA_DSP_BAR] +
						    elem->offset +
						    SRAM_WINDOW_OFFSET
						    (elem->id),
						    elem->size, "exception");
			break;
		default:
			dev_err(sdev->dev, "error: get illegal window info\n");
			return;
		}
	}

	if (outbox_size == 0 || inbox_size == 0) {
		dev_err(sdev->dev, "error: get illegal mailbox window\n");
		return;
	}

	snd_sof_dsp_mailbox_init(sdev, inbox_offset, inbox_size,
				 outbox_offset, outbox_size);
	sdev->stream_box.offset = stream_offset;
	sdev->stream_box.size = stream_size;

	dev_dbg(sdev->dev, " mailbox upstream 0x%x - size 0x%x\n",
		inbox_offset, inbox_size);
	dev_dbg(sdev->dev, " mailbox downstream 0x%x - size 0x%x\n",
		outbox_offset, outbox_size);
	dev_dbg(sdev->dev, " stream region 0x%x - size 0x%x\n",
		stream_offset, stream_size);
}

int hda_dsp_ipc_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_fw_ready *fw_ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &fw_ready->version;
	u32 offset;

	/* mailbox must be on 4k boundary */
	offset = HDA_DSP_MBOX_UPLINK_OFFSET;

	dev_dbg(sdev->dev, "ipc: DSP is ready 0x%8.8x offset 0x%x\n",
		msg_id, offset);

	/* copy data from the DSP FW ready offset */
	hda_dsp_block_read(sdev, offset, fw_ready,	sizeof(*fw_ready));
	dev_info(sdev->dev,
		 " Firmware info: version %d.%d-%s build %d on %s:%s\n",
		 v->major, v->minor, v->tag, v->build, v->date, v->time);

	/* now check for extended data */
	snd_sof_fw_parse_ext_data(sdev, HDA_DSP_MBOX_UPLINK_OFFSET +
				  sizeof(struct sof_ipc_fw_ready));

	ipc_get_windows(sdev);

	return 0;
}
