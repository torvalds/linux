// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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
	if (hdr->cmd == (SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_CTX_SAVE) ||
	    hdr->cmd == (SOF_IPC_GLB_PM_MSG | SOF_IPC_PM_GATE)) {
		/*
		 * memory windows are powered off before sending IPC reply,
		 * so we can't read the mailbox for CTX_SAVE and PM_GATE
		 * replies.
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
		if (reply.hdr.size != msg->reply_size &&
			/* getter payload is never known upfront */
			!(reply.hdr.cmd & SOF_IPC_GLB_PROBE)) {
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

		/* handle immediate reply from DSP core */
		hda_dsp_ipc_get_reply(sdev);
		snd_sof_ipc_reply(sdev, msg);

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

	return IRQ_HANDLED;
}

/* Check if an IPC IRQ occurred */
bool hda_dsp_check_ipc_irq(struct snd_sof_dev *sdev)
{
	bool ret = false;
	u32 irq_status;

	/* store status */
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIS);
	dev_vdbg(sdev->dev, "irq handler: irq_status:0x%x\n", irq_status);

	/* invalid message ? */
	if (irq_status == 0xffffffff)
		goto out;

	/* IPC message ? */
	if (irq_status & HDA_DSP_ADSPIS_IPC)
		ret = true;

out:
	return ret;
}

int hda_dsp_ipc_get_mailbox_offset(struct snd_sof_dev *sdev)
{
	return HDA_DSP_MBOX_UPLINK_OFFSET;
}

int hda_dsp_ipc_get_window_offset(struct snd_sof_dev *sdev, u32 id)
{
	return SRAM_WINDOW_OFFSET(id);
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
