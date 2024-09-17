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

#include <sound/sof/ipc4/header.h>
#include <trace/events/sof_intel.h>
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

int hda_dsp_ipc4_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	struct sof_ipc4_msg *msg_data = msg->msg_data;

	/* send the message via mailbox */
	if (msg_data->data_size)
		sof_mailbox_write(sdev, sdev->host_box.offset, msg_data->data_ptr,
				  msg_data->data_size);

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCIE, msg_data->extension);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCI,
			  msg_data->primary | HDA_DSP_REG_HIPCI_BUSY);

	return 0;
}

void hda_dsp_ipc_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc_reply reply;
	struct sof_ipc_cmd_hdr *hdr;

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

		msg->reply_error = 0;
	} else {
		snd_sof_ipc_get_reply(sdev);
	}
}

irqreturn_t hda_dsp_ipc4_irq_thread(int irq, void *context)
{
	struct sof_ipc4_msg notification_data = {{ 0 }};
	struct snd_sof_dev *sdev = context;
	bool ipc_irq = false;
	u32 hipcie, hipct;

	hipcie = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCIE);
	if (hipcie & HDA_DSP_REG_HIPCIE_DONE) {
		/* DSP received the message */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_DONE, 0);
		hda_dsp_ipc_dsp_done(sdev);

		ipc_irq = true;
	}

	hipct = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCT);
	if (hipct & HDA_DSP_REG_HIPCT_BUSY) {
		/* Message from DSP (reply or notification) */
		u32 hipcte = snd_sof_dsp_read(sdev, HDA_DSP_BAR,
					      HDA_DSP_REG_HIPCTE);
		u32 primary = hipct & HDA_DSP_REG_HIPCT_MSG_MASK;
		u32 extension = hipcte & HDA_DSP_REG_HIPCTE_MSG_MASK;

		/* mask BUSY interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_BUSY, 0);

		if (primary & SOF_IPC4_MSG_DIR_MASK) {
			/* Reply received */
			if (likely(sdev->fw_state == SOF_FW_BOOT_COMPLETE)) {
				struct sof_ipc4_msg *data = sdev->ipc->msg.reply_data;

				data->primary = primary;
				data->extension = extension;

				spin_lock_irq(&sdev->ipc_lock);

				snd_sof_ipc_get_reply(sdev);
				snd_sof_ipc_reply(sdev, data->primary);

				spin_unlock_irq(&sdev->ipc_lock);
			} else {
				dev_dbg_ratelimited(sdev->dev,
						    "IPC reply before FW_READY: %#x|%#x\n",
						    primary, extension);
			}
		} else {
			/* Notification received */

			notification_data.primary = primary;
			notification_data.extension = extension;
			sdev->ipc->msg.rx_data = &notification_data;
			snd_sof_ipc_msgs_rx(sdev);
			sdev->ipc->msg.rx_data = NULL;
		}

		/* Let DSP know that we have finished processing the message */
		hda_dsp_ipc_host_done(sdev);

		ipc_irq = true;
	}

	if (!ipc_irq)
		/* This interrupt is not shared so no need to return IRQ_NONE. */
		dev_dbg_ratelimited(sdev->dev, "nothing to do in IPC IRQ thread\n");

	return IRQ_HANDLED;
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

		trace_sof_intel_ipc_firmware_response(sdev, msg, msg_ext);

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
		if (likely(sdev->fw_state == SOF_FW_BOOT_COMPLETE)) {
			spin_lock_irq(&sdev->ipc_lock);

			/* handle immediate reply from DSP core */
			hda_dsp_ipc_get_reply(sdev);
			snd_sof_ipc_reply(sdev, msg);

			/* set the done bit */
			hda_dsp_ipc_dsp_done(sdev);

			spin_unlock_irq(&sdev->ipc_lock);
		} else {
			dev_dbg_ratelimited(sdev->dev, "IPC reply before FW_READY: %#x\n",
					    msg);
		}

		ipc_irq = true;
	}

	/* is this a new message from DSP */
	if (hipct & HDA_DSP_REG_HIPCT_BUSY) {
		msg = hipct & HDA_DSP_REG_HIPCT_MSG_MASK;
		msg_ext = hipcte & HDA_DSP_REG_HIPCTE_MSG_MASK;

		trace_sof_intel_ipc_firmware_initiated(sdev, msg, msg_ext);

		/* mask BUSY interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
					HDA_DSP_REG_HIPCCTL,
					HDA_DSP_REG_HIPCCTL_BUSY, 0);

		/* handle messages from DSP */
		if ((hipct & SOF_IPC_PANIC_MAGIC_MASK) == SOF_IPC_PANIC_MAGIC) {
			struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
			bool non_recoverable = true;

			/*
			 * This is a PANIC message!
			 *
			 * If it is arriving during firmware boot and it is not
			 * the last boot attempt then change the non_recoverable
			 * to false as the DSP might be able to boot in the next
			 * iteration(s)
			 */
			if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS &&
			    hda->boot_iteration < HDA_FW_BOOT_ATTEMPTS)
				non_recoverable = false;

			snd_sof_dsp_panic(sdev, HDA_DSP_PANIC_OFFSET(msg_ext),
					  non_recoverable);
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
	struct sof_intel_hda_dev *hda = sdev->pdata->hw_pdata;
	bool ret = false;
	u32 irq_status;

	/* store status */
	irq_status = snd_sof_dsp_read(sdev, HDA_DSP_BAR, HDA_DSP_REG_ADSPIS);
	trace_sof_intel_hda_irq_ipc_check(sdev, irq_status);

	/* invalid message ? */
	if (irq_status == 0xffffffff)
		goto out;

	/* IPC message ? */
	if (irq_status & HDA_DSP_ADSPIS_IPC)
		ret = true;

	/* CLDMA message ? */
	if (irq_status & HDA_DSP_ADSPIS_CL_DMA) {
		hda->code_loading = 0;
		wake_up(&hda->waitq);
		ret = false;
	}

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

int hda_ipc_msg_data(struct snd_sof_dev *sdev,
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
					  hext_stream.hstream);

		/* The stream might already be closed */
		if (!hstream)
			return -ESTRPIPE;

		sof_mailbox_read(sdev, hda_stream->sof_intel_stream.posn_offset, p, sz);
	}

	return 0;
}

int hda_set_stream_data_offset(struct snd_sof_dev *sdev,
			       struct snd_pcm_substream *substream,
			       size_t posn_offset)
{
	struct hdac_stream *hstream = substream->runtime->private_data;
	struct sof_intel_hda_stream *hda_stream;

	hda_stream = container_of(hstream, struct sof_intel_hda_stream,
				  hext_stream.hstream);

	/* check for unaligned offset or overflow */
	if (posn_offset > sdev->stream_box.size ||
	    posn_offset % sizeof(struct sof_ipc_stream_posn) != 0)
		return -EINVAL;

	hda_stream->sof_intel_stream.posn_offset = sdev->stream_box.offset + posn_offset;

	dev_dbg(sdev->dev, "pcm: stream dir %d, posn mailbox offset is %zu",
		substream->stream, hda_stream->sof_intel_stream.posn_offset);

	return 0;
}
