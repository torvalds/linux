// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//	    Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//	    Rander Wang <rander.wang@intel.com>
//          Keyon Jie <yang.jie@linux.intel.com>
//

/*
 * Hardware interface for generic Intel audio DSP HDA IP
 */

#include "../ops.h"
#include "hda.h"

static void hda_dsp_ipc_host_done(struct snd_sof_dev *sdev)
{
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
}

static void hda_dsp_ipc_dsp_done(struct snd_sof_dev *sdev)
{
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

int hda_dsp_ipc_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	/* send IPC message to DSP */
	sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data,
			  msg->msg_size);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCI,
			  HDA_DSP_REG_HIPCI_BUSY);

	return 0;
}

void hda_dsp_ipc_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply reply;
	struct sof_ipc_cmd_hdr *hdr;
	int ret = 0;

	/*
	 * Sometimes, there is unexpected reply ipc arriving. The reply
	 * ipc belongs to none of the ipcs sent from driver.
	 * In this case, the driver must ignore the ipc.
	 */
	if (!msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt raised!\n");
		return;
	}

	hdr = msg->msg_data;
	if (hdr->cmd == (SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CTX_SAVE)) {
		/*
		 * memory windows are powered off before sending IPC reply,
		 * so we can't read the mailbox for CTX_SAVE reply.
		 */
		reply.error = 0;
		reply.hdr.cmd = SOF_IPC_GLB_REPLY;
		reply.hdr.size = sizeof(reply);
		memcpy(msg->reply_data, &reply, sizeof(reply));
		goto out;
	}

	/* get IPC reply from DSP in the mailbox */
	sof_mailbox_read(sdev, sdev->host_box.offset, &reply,
			 sizeof(reply));

	if (reply.error < 0) {
		memcpy(msg->reply_data, &reply, sizeof(reply));
		ret = reply.error;
	} else {
		/* reply correct size ? */
		if (reply.hdr.size != msg->reply_size) {
			dev_err(sdev->dev, "error: reply expected %zu got %u bytes\n",
				msg->reply_size, reply.hdr.size);
			ret = -EINVAL;
		}

		/* read the message */
		if (msg->reply_size > 0)
			sof_mailbox_read(sdev, sdev->host_box.offset,
					 msg->reply_data, msg->reply_size);
	}

out:
	msg->reply_error = ret;

}

static bool hda_dsp_ipc_is_sof(uint32_t msg)
{
	return (msg & (HDA_DSP_IPC_PURGE_FW | 0xf << 9)) != msg ||
		(msg & HDA_DSP_IPC_PURGE_FW) != HDA_DSP_IPC_PURGE_FW;
}

/* IPC handler thread */
irqreturn_t hda_dsp_ipc_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u32 hipci;
	u32 hipcie;
	u32 hipct;
	u32 hipcte;
	u32 msg;
	u32 msg_ext;
	bool ipc_irq = false;

	/* read IPC status */
	hipcie = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
				  HDA_DSP_REG_HIPCIE);
	hipct = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCT);
	hipci = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCI);
	hipcte = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCTE);

	/* is this a reply message from the DSP */
	if (hipcie & HDA_DSP_REG_HIPCIE_DONE) {
		msg = hipci & HDA_DSP_REG_HIPCI_MSG_MASK;
		msg_ext = hipcie & HDA_DSP_REG_HIPCIE_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware response, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* mask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_DONE, 0);

		/*
		 * Make sure the interrupt thread cannot be preempted between
		 * waking up the sender and re-enabling the interrupt. Also
		 * protect against a theoretical race with sof_ipc_tx_message():
		 * if the DSP is fast enough to receive an IPC message, reply to
		 * it, and the host interrupt processing calls this function on
		 * a different core from the one, where the sending is taking
		 * place, the message might not yet be marked as expecting a
		 * reply.
		 */
		spin_lock_irq(&sdev->ipc_lock);

		/* handle immediate reply from DSP core - ignore ROM messages */
		if (hda_dsp_ipc_is_sof(msg)) {
			hda_dsp_ipc_get_reply(sdev);
			snd_sof_ipc_reply(sdev, msg);
		}

		/* wake up sleeper if we are loading code */
		if (sdev->code_loading)	{
			sdev->code_loading = 0;
			wake_up(&sdev->waitq);
		}

		/* set the done bit */
		hda_dsp_ipc_dsp_done(sdev);

		spin_unlock_irq(&sdev->ipc_lock);

		ipc_irq = true;
	}

	/* is this a new message from DSP */
	if (hipct & HDA_DSP_REG_HIPCT_BUSY) {
		msg = hipct & HDA_DSP_REG_HIPCT_MSG_MASK;
		msg_ext = hipcte & HDA_DSP_REG_HIPCTE_MSG_MASK;

		dev_vdbg(sdev->dev,
			 "ipc: firmware initiated, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/* mask BUSY interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_BUSY, 0);

		/* handle messages from DSP */
		if ((hipct & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			/* this is a PANIC message !! */
			snd_sof_dsp_panic(sdev, HDA_DSP_PANIC_OFFSET(msg_ext));
		} else {
			/* normal message - process normally */
			snd_sof_ipc_msgs_rx(sdev);
		}

		hda_dsp_ipc_host_done(sdev);

		ipc_irq = true;
	}

	if (!ipc_irq) {
		/*
		 * This interrupt is not shared so no need to return IRQ_NONE.
		 */
		dev_dbg_ratelimited(sdev->dev,
				    "nothing to do in IPC IRQ thread\n");
	}

	/* re-enable IPC interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIC,
				HDA_DSP_ADSPIC_IPC, HDA_DSP_ADSPIC_IPC);

	return IRQ_HANDLED;
}

/* is this IRQ for ADSP ? - we only care about IPC here */
irqreturn_t hda_dsp_ipc_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	int ret = IRQ_NONE;
	u32 irq_status;

	spin_lock(&sdev->hw_lock);

	/* store status */
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIS);
	dev_vdbg(sdev->dev, "irq handler: irq_status:0x%x\n", irq_status);

	/* invalid message ? */
	if (irq_status == 0xffffffff)
		goto out;

	/* IPC message ? */
	if (irq_status & HDA_DSP_ADSPIS_IPC) {
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

/* IPC Firmware ready */

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
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						inbox_offset,
						elem->size, "inbox",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DOWNBOX:
			outbox_offset =
				elem->offset + SRAM_WINDOW_OFFSET(elem->id);
			outbox_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						outbox_offset,
						elem->size, "outbox",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_TRACE:
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						elem->offset +
						SRAM_WINDOW_OFFSET
						(elem->id),
						elem->size, "etrace",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_DEBUG:
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						elem->offset +
						SRAM_WINDOW_OFFSET
						(elem->id),
						elem->size, "debug",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_STREAM:
			stream_offset =
				elem->offset + SRAM_WINDOW_OFFSET(elem->id);
			stream_size = elem->size;
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						elem->offset +
						SRAM_WINDOW_OFFSET
						(elem->id),
						elem->size, "stream",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_REGS:
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						elem->offset +
						SRAM_WINDOW_OFFSET
						(elem->id),
						elem->size, "regs",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
			break;
		case SOF_IPC_REGION_EXCEPTION:
			sdev->dsp_oops_offset = elem->offset +
						SRAM_WINDOW_OFFSET(elem->id);
			snd_sof_debugfs_io_item(sdev,
						sdev->bar[HDA_DSP_BAR] +
						elem->offset +
						SRAM_WINDOW_OFFSET
						(elem->id),
						elem->size, "exception",
						SOF_DEBUGFS_ACCESS_D0_ONLY);
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

/* check for ABI compatibility and create memory windows on first boot */
int hda_dsp_ipc_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct sof_ipc_fw_ready *fw_ready = &sdev->fw_ready;
	u32 offset;
	int ret;

	/* mailbox must be on 4k boundary */
	offset = HDA_DSP_MBOX_UPLINK_OFFSET;

	dev_dbg(sdev->dev, "ipc: DSP is ready 0x%8.8x offset 0x%x\n",
		msg_id, offset);

	/* no need to re-check version/ABI for subsequent boots */
	if (!sdev->first_boot)
		return 0;

	/* copy data from the DSP FW ready offset */
	sof_block_read(sdev, sdev->mmio_bar, offset, fw_ready,
		       sizeof(*fw_ready));

	/* make sure ABI version is compatible */
	ret = snd_sof_ipc_valid(sdev);
	if (ret < 0)
		return ret;

	/* now check for extended data */
	snd_sof_fw_parse_ext_data(sdev, sdev->mmio_bar,
				  HDA_DSP_MBOX_UPLINK_OFFSET +
				  sizeof(struct sof_ipc_fw_ready));

	ipc_get_windows(sdev);

	return 0;
}

void hda_ipc_msg_data(struct snd_sof_dev *sdev,
		      struct snd_pcm_substream *substream,
		      void *p, size_t sz)
{
	if (!substream || !sdev->stream_box.size) {
		sof_mailbox_read(sdev, sdev->dsp_box.offset, p, sz);
	} else {
		struct hdac_stream *hstream = substream->runtime->private_data;
		struct sof_intel_hda_stream *hda_stream;

		hda_stream = container_of(hstream,
					  struct sof_intel_hda_stream,
					  hda_stream.hstream);

		/* The stream might already be closed */
		if (hstream)
			sof_mailbox_read(sdev, hda_stream->stream.posn_offset,
					 p, sz);
	}
}

int hda_ipc_pcm_params(struct snd_sof_dev *sdev,
		       struct snd_pcm_substream *substream,
		       const struct sof_ipc_pcm_params_reply *reply)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct sof_intel_hda_stream *hda_stream;
	/* validate offset */
	size_t posn_offset = reply->posn_offset;

	hda_stream = container_of(hstream, struct sof_intel_hda_stream,
				  hda_stream.hstream);

	/* check for unaligned offset or overflow */
	if (posn_offset > sdev->stream_box.size ||
	    posn_offset % sizeof(struct sof_ipc_stream_posn) != 0)
		return -EINVAL;

	hda_stream->stream.posn_offset = sdev->stream_box.offset + posn_offset;

	dev_dbg(sdev->dev, "pcm: stream dir %d, posn mailbox offset is %zu",
		substream->stream, hda_stream->stream.posn_offset);

	return 0;
}
