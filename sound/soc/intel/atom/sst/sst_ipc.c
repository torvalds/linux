/*
 *  sst_ipc.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-14 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/intel-mid.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../../common/sst-dsp.h"

struct sst_block *sst_create_block(struct intel_sst_drv *ctx,
					u32 msg_id, u32 drv_id)
{
	struct sst_block *msg = NULL;

	dev_dbg(ctx->dev, "Enter\n");
	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return NULL;
	msg->condition = false;
	msg->on = true;
	msg->msg_id = msg_id;
	msg->drv_id = drv_id;
	spin_lock_bh(&ctx->block_lock);
	list_add_tail(&msg->node, &ctx->block_list);
	spin_unlock_bh(&ctx->block_lock);

	return msg;
}

/*
 * while handling the interrupts, we need to check for message status and
 * then if we are blocking for a message
 *
 * here we are unblocking the blocked ones, this is based on id we have
 * passed and search that for block threads.
 * We will not find block in two cases
 *  a) when its small message and block in not there, so silently ignore
 *  them
 *  b) when we are actually not able to find the block (bug perhaps)
 *
 *  Since we have bit of small messages we can spam kernel log with err
 *  print on above so need to keep as debug prints which should be enabled
 *  via dynamic debug while debugging IPC issues
 */
int sst_wake_up_block(struct intel_sst_drv *ctx, int result,
		u32 drv_id, u32 ipc, void *data, u32 size)
{
	struct sst_block *block = NULL;

	dev_dbg(ctx->dev, "Enter\n");

	spin_lock_bh(&ctx->block_lock);
	list_for_each_entry(block, &ctx->block_list, node) {
		dev_dbg(ctx->dev, "Block ipc %d, drv_id %d\n", block->msg_id,
							block->drv_id);
		if (block->msg_id == ipc && block->drv_id == drv_id) {
			dev_dbg(ctx->dev, "free up the block\n");
			block->ret_code = result;
			block->data = data;
			block->size = size;
			block->condition = true;
			spin_unlock_bh(&ctx->block_lock);
			wake_up(&ctx->wait_queue);
			return 0;
		}
	}
	spin_unlock_bh(&ctx->block_lock);
	dev_dbg(ctx->dev,
		"Block not found or a response received for a short msg for ipc %d, drv_id %d\n",
		ipc, drv_id);
	return -EINVAL;
}

int sst_free_block(struct intel_sst_drv *ctx, struct sst_block *freed)
{
	struct sst_block *block = NULL, *__block;

	dev_dbg(ctx->dev, "Enter\n");
	spin_lock_bh(&ctx->block_lock);
	list_for_each_entry_safe(block, __block, &ctx->block_list, node) {
		if (block == freed) {
			pr_debug("pvt_id freed --> %d\n", freed->drv_id);
			/* toggle the index position of pvt_id */
			list_del(&freed->node);
			spin_unlock_bh(&ctx->block_lock);
			kfree(freed->data);
			freed->data = NULL;
			kfree(freed);
			return 0;
		}
	}
	spin_unlock_bh(&ctx->block_lock);
	dev_err(ctx->dev, "block is already freed!!!\n");
	return -EINVAL;
}

int sst_post_message_mrfld(struct intel_sst_drv *sst_drv_ctx,
		struct ipc_post *ipc_msg, bool sync)
{
	struct ipc_post *msg = ipc_msg;
	union ipc_header_mrfld header;
	unsigned int loop_count = 0;
	int retval = 0;
	unsigned long irq_flags;

	dev_dbg(sst_drv_ctx->dev, "Enter: sync: %d\n", sync);
	spin_lock_irqsave(&sst_drv_ctx->ipc_spin_lock, irq_flags);
	header.full = sst_shim_read64(sst_drv_ctx->shim, SST_IPCX);
	if (sync) {
		while (header.p.header_high.part.busy) {
			if (loop_count > 25) {
				dev_err(sst_drv_ctx->dev,
					"sst: Busy wait failed, cant send this msg\n");
				retval = -EBUSY;
				goto out;
			}
			cpu_relax();
			loop_count++;
			header.full = sst_shim_read64(sst_drv_ctx->shim, SST_IPCX);
		}
	} else {
		if (list_empty(&sst_drv_ctx->ipc_dispatch_list)) {
			/* queue is empty, nothing to send */
			spin_unlock_irqrestore(&sst_drv_ctx->ipc_spin_lock, irq_flags);
			dev_dbg(sst_drv_ctx->dev,
					"Empty msg queue... NO Action\n");
			return 0;
		}

		if (header.p.header_high.part.busy) {
			spin_unlock_irqrestore(&sst_drv_ctx->ipc_spin_lock, irq_flags);
			dev_dbg(sst_drv_ctx->dev, "Busy not free... post later\n");
			return 0;
		}

		/* copy msg from list */
		msg = list_entry(sst_drv_ctx->ipc_dispatch_list.next,
				struct ipc_post, node);
		list_del(&msg->node);
	}
	dev_dbg(sst_drv_ctx->dev, "sst: Post message: header = %x\n",
				msg->mrfld_header.p.header_high.full);
	dev_dbg(sst_drv_ctx->dev, "sst: size = 0x%x\n",
			msg->mrfld_header.p.header_low_payload);

	if (msg->mrfld_header.p.header_high.part.large)
		memcpy_toio(sst_drv_ctx->mailbox + SST_MAILBOX_SEND,
			msg->mailbox_data,
			msg->mrfld_header.p.header_low_payload);

	sst_shim_write64(sst_drv_ctx->shim, SST_IPCX, msg->mrfld_header.full);

out:
	spin_unlock_irqrestore(&sst_drv_ctx->ipc_spin_lock, irq_flags);
	kfree(msg->mailbox_data);
	kfree(msg);
	return retval;
}

void intel_sst_clear_intr_mrfld(struct intel_sst_drv *sst_drv_ctx)
{
	union interrupt_reg_mrfld isr;
	union interrupt_reg_mrfld imr;
	union ipc_header_mrfld clear_ipc;
	unsigned long irq_flags;

	spin_lock_irqsave(&sst_drv_ctx->ipc_spin_lock, irq_flags);
	imr.full = sst_shim_read64(sst_drv_ctx->shim, SST_IMRX);
	isr.full = sst_shim_read64(sst_drv_ctx->shim, SST_ISRX);

	/* write 1 to clear*/
	isr.part.busy_interrupt = 1;
	sst_shim_write64(sst_drv_ctx->shim, SST_ISRX, isr.full);

	/* Set IA done bit */
	clear_ipc.full = sst_shim_read64(sst_drv_ctx->shim, SST_IPCD);

	clear_ipc.p.header_high.part.busy = 0;
	clear_ipc.p.header_high.part.done = 1;
	clear_ipc.p.header_low_payload = IPC_ACK_SUCCESS;
	sst_shim_write64(sst_drv_ctx->shim, SST_IPCD, clear_ipc.full);
	/* un mask busy interrupt */
	imr.part.busy_interrupt = 0;
	sst_shim_write64(sst_drv_ctx->shim, SST_IMRX, imr.full);
	spin_unlock_irqrestore(&sst_drv_ctx->ipc_spin_lock, irq_flags);
}


/*
 * process_fw_init - process the FW init msg
 *
 * @msg: IPC message mailbox data from FW
 *
 * This function processes the FW init msg from FW
 * marks FW state and prints debug info of loaded FW
 */
static void process_fw_init(struct intel_sst_drv *sst_drv_ctx,
			void *msg)
{
	struct ipc_header_fw_init *init =
		(struct ipc_header_fw_init *)msg;
	int retval = 0;

	dev_dbg(sst_drv_ctx->dev, "*** FW Init msg came***\n");
	if (init->result) {
		sst_set_fw_state_locked(sst_drv_ctx, SST_RESET);
		dev_err(sst_drv_ctx->dev, "FW Init failed, Error %x\n",
				init->result);
		retval = init->result;
		goto ret;
	}

ret:
	sst_wake_up_block(sst_drv_ctx, retval, FW_DWNL_ID, 0 , NULL, 0);
}

static void process_fw_async_msg(struct intel_sst_drv *sst_drv_ctx,
			struct ipc_post *msg)
{
	u32 msg_id;
	int str_id;
	u32 data_size, i;
	void *data_offset;
	struct stream_info *stream;
	union ipc_header_high msg_high;
	u32 msg_low, pipe_id;

	msg_high = msg->mrfld_header.p.header_high;
	msg_low = msg->mrfld_header.p.header_low_payload;
	msg_id = ((struct ipc_dsp_hdr *)msg->mailbox_data)->cmd_id;
	data_offset = (msg->mailbox_data + sizeof(struct ipc_dsp_hdr));
	data_size =  msg_low - (sizeof(struct ipc_dsp_hdr));

	switch (msg_id) {
	case IPC_SST_PERIOD_ELAPSED_MRFLD:
		pipe_id = ((struct ipc_dsp_hdr *)msg->mailbox_data)->pipe_id;
		str_id = get_stream_id_mrfld(sst_drv_ctx, pipe_id);
		if (str_id > 0) {
			dev_dbg(sst_drv_ctx->dev,
				"Period elapsed rcvd for pipe id 0x%x\n",
				pipe_id);
			stream = &sst_drv_ctx->streams[str_id];
			if (stream->period_elapsed)
				stream->period_elapsed(stream->pcm_substream);
			if (stream->compr_cb)
				stream->compr_cb(stream->compr_cb_param);
		}
		break;

	case IPC_IA_DRAIN_STREAM_MRFLD:
		pipe_id = ((struct ipc_dsp_hdr *)msg->mailbox_data)->pipe_id;
		str_id = get_stream_id_mrfld(sst_drv_ctx, pipe_id);
		if (str_id > 0) {
			stream = &sst_drv_ctx->streams[str_id];
			if (stream->drain_notify)
				stream->drain_notify(stream->drain_cb_param);
		}
		break;

	case IPC_IA_FW_ASYNC_ERR_MRFLD:
		dev_err(sst_drv_ctx->dev, "FW sent async error msg:\n");
		for (i = 0; i < (data_size/4); i++)
			print_hex_dump(KERN_DEBUG, NULL, DUMP_PREFIX_NONE,
					16, 4, data_offset, data_size, false);
		break;

	case IPC_IA_FW_INIT_CMPLT_MRFLD:
		process_fw_init(sst_drv_ctx, data_offset);
		break;

	case IPC_IA_BUF_UNDER_RUN_MRFLD:
		pipe_id = ((struct ipc_dsp_hdr *)msg->mailbox_data)->pipe_id;
		str_id = get_stream_id_mrfld(sst_drv_ctx, pipe_id);
		if (str_id > 0)
			dev_err(sst_drv_ctx->dev,
				"Buffer under-run for pipe:%#x str_id:%d\n",
				pipe_id, str_id);
		break;

	default:
		dev_err(sst_drv_ctx->dev,
			"Unrecognized async msg from FW msg_id %#x\n", msg_id);
	}
}

void sst_process_reply_mrfld(struct intel_sst_drv *sst_drv_ctx,
		struct ipc_post *msg)
{
	unsigned int drv_id;
	void *data;
	union ipc_header_high msg_high;
	u32 msg_low;
	struct ipc_dsp_hdr *dsp_hdr;

	msg_high = msg->mrfld_header.p.header_high;
	msg_low = msg->mrfld_header.p.header_low_payload;

	dev_dbg(sst_drv_ctx->dev, "IPC process message header %x payload %x\n",
			msg->mrfld_header.p.header_high.full,
			msg->mrfld_header.p.header_low_payload);

	drv_id = msg_high.part.drv_id;

	/* Check for async messages first */
	if (drv_id == SST_ASYNC_DRV_ID) {
		/*FW sent async large message*/
		process_fw_async_msg(sst_drv_ctx, msg);
		return;
	}

	/* FW sent short error response for an IPC */
	if (msg_high.part.result && drv_id && !msg_high.part.large) {
		/* 32-bit FW error code in msg_low */
		dev_err(sst_drv_ctx->dev, "FW sent error response 0x%x", msg_low);
		sst_wake_up_block(sst_drv_ctx, msg_high.part.result,
			msg_high.part.drv_id,
			msg_high.part.msg_id, NULL, 0);
		return;
	}

	/*
	 * Process all valid responses
	 * if it is a large message, the payload contains the size to
	 * copy from mailbox
	 **/
	if (msg_high.part.large) {
		data = kmemdup((void *)msg->mailbox_data, msg_low, GFP_KERNEL);
		if (!data)
			return;
		/* Copy command id so that we can use to put sst to reset */
		dsp_hdr = (struct ipc_dsp_hdr *)data;
		dev_dbg(sst_drv_ctx->dev, "cmd_id %d\n", dsp_hdr->cmd_id);
		if (sst_wake_up_block(sst_drv_ctx, msg_high.part.result,
				msg_high.part.drv_id,
				msg_high.part.msg_id, data, msg_low))
			kfree(data);
	} else {
		sst_wake_up_block(sst_drv_ctx, msg_high.part.result,
				msg_high.part.drv_id,
				msg_high.part.msg_id, NULL, 0);
	}

}
