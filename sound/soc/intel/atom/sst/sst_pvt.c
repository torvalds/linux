/*
 *  sst_pvt.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-14	Intel Corp
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
#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <sound/asound.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../../common/sst-dsp.h"

int sst_shim_write(void __iomem *addr, int offset, int value)
{
	writel(value, addr + offset);
	return 0;
}

u32 sst_shim_read(void __iomem *addr, int offset)
{
	return readl(addr + offset);
}

u64 sst_reg_read64(void __iomem *addr, int offset)
{
	u64 val = 0;

	memcpy_fromio(&val, addr + offset, sizeof(val));

	return val;
}

int sst_shim_write64(void __iomem *addr, int offset, u64 value)
{
	memcpy_toio(addr + offset, &value, sizeof(value));
	return 0;
}

u64 sst_shim_read64(void __iomem *addr, int offset)
{
	u64 val = 0;

	memcpy_fromio(&val, addr + offset, sizeof(val));
	return val;
}

void sst_set_fw_state_locked(
		struct intel_sst_drv *sst_drv_ctx, int sst_state)
{
	mutex_lock(&sst_drv_ctx->sst_lock);
	sst_drv_ctx->sst_state = sst_state;
	mutex_unlock(&sst_drv_ctx->sst_lock);
}

/*
 * sst_wait_interruptible - wait on event
 *
 * @sst_drv_ctx: Driver context
 * @block: Driver block to wait on
 *
 * This function waits without a timeout (and is interruptable) for a
 * given block event
 */
int sst_wait_interruptible(struct intel_sst_drv *sst_drv_ctx,
				struct sst_block *block)
{
	int retval = 0;

	if (!wait_event_interruptible(sst_drv_ctx->wait_queue,
				block->condition)) {
		/* event wake */
		if (block->ret_code < 0) {
			dev_err(sst_drv_ctx->dev,
				"stream failed %d\n", block->ret_code);
			retval = -EBUSY;
		} else {
			dev_dbg(sst_drv_ctx->dev, "event up\n");
			retval = 0;
		}
	} else {
		dev_err(sst_drv_ctx->dev, "signal interrupted\n");
		retval = -EINTR;
	}
	return retval;

}

/*
 * sst_wait_timeout - wait on event for timeout
 *
 * @sst_drv_ctx: Driver context
 * @block: Driver block to wait on
 *
 * This function waits with a timeout value (and is not interruptible) on a
 * given block event
 */
int sst_wait_timeout(struct intel_sst_drv *sst_drv_ctx, struct sst_block *block)
{
	int retval = 0;

	/*
	 * NOTE:
	 * Observed that FW processes the alloc msg and replies even
	 * before the alloc thread has finished execution
	 */
	dev_dbg(sst_drv_ctx->dev,
		"waiting for condition %x ipc %d drv_id %d\n",
		block->condition, block->msg_id, block->drv_id);
	if (wait_event_timeout(sst_drv_ctx->wait_queue,
				block->condition,
				msecs_to_jiffies(SST_BLOCK_TIMEOUT))) {
		/* event wake */
		dev_dbg(sst_drv_ctx->dev, "Event wake %x\n",
				block->condition);
		dev_dbg(sst_drv_ctx->dev, "message ret: %d\n",
				block->ret_code);
		retval = -block->ret_code;
	} else {
		block->on = false;
		dev_err(sst_drv_ctx->dev,
			"Wait timed-out condition:%#x, msg_id:%#x fw_state %#x\n",
			block->condition, block->msg_id, sst_drv_ctx->sst_state);
		sst_drv_ctx->sst_state = SST_RESET;

		retval = -EBUSY;
	}
	return retval;
}

/*
 * sst_create_ipc_msg - create a IPC message
 *
 * @arg: ipc message
 * @large: large or short message
 *
 * this function allocates structures to send a large or short
 * message to the firmware
 */
int sst_create_ipc_msg(struct ipc_post **arg, bool large)
{
	struct ipc_post *msg;

	msg = kzalloc(sizeof(struct ipc_post), GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;
	if (large) {
		msg->mailbox_data = kzalloc(SST_MAILBOX_SIZE, GFP_ATOMIC);
		if (!msg->mailbox_data) {
			kfree(msg);
			return -ENOMEM;
		}
	} else {
		msg->mailbox_data = NULL;
	}
	msg->is_large = large;
	*arg = msg;
	return 0;
}

/*
 * sst_create_block_and_ipc_msg - Creates IPC message and sst block
 * @arg: passed to sst_create_ipc_message API
 * @large: large or short message
 * @sst_drv_ctx: sst driver context
 * @block: return block allocated
 * @msg_id: IPC
 * @drv_id: stream id or private id
 */
int sst_create_block_and_ipc_msg(struct ipc_post **arg, bool large,
		struct intel_sst_drv *sst_drv_ctx, struct sst_block **block,
		u32 msg_id, u32 drv_id)
{
	int retval = 0;

	retval = sst_create_ipc_msg(arg, large);
	if (retval)
		return retval;
	*block = sst_create_block(sst_drv_ctx, msg_id, drv_id);
	if (*block == NULL) {
		kfree(*arg);
		return -ENOMEM;
	}
	return retval;
}

/*
 * sst_clean_stream - clean the stream context
 *
 * @stream: stream structure
 *
 * this function resets the stream contexts
 * should be called in free
 */
void sst_clean_stream(struct stream_info *stream)
{
	stream->status = STREAM_UN_INIT;
	stream->prev = STREAM_UN_INIT;
	mutex_lock(&stream->lock);
	stream->cumm_bytes = 0;
	mutex_unlock(&stream->lock);
}

int sst_prepare_and_post_msg(struct intel_sst_drv *sst,
		int task_id, int ipc_msg, int cmd_id, int pipe_id,
		size_t mbox_data_len, const void *mbox_data, void **data,
		bool large, bool fill_dsp, bool sync, bool response)
{
	struct ipc_post *msg = NULL;
	struct ipc_dsp_hdr dsp_hdr;
	struct sst_block *block;
	int ret = 0, pvt_id;

	pvt_id = sst_assign_pvt_id(sst);
	if (pvt_id < 0)
		return pvt_id;

	if (response)
		ret = sst_create_block_and_ipc_msg(
				&msg, large, sst, &block, ipc_msg, pvt_id);
	else
		ret = sst_create_ipc_msg(&msg, large);

	if (ret < 0) {
		test_and_clear_bit(pvt_id, &sst->pvt_id);
		return -ENOMEM;
	}

	dev_dbg(sst->dev, "pvt_id = %d, pipe id = %d, task = %d ipc_msg: %d\n",
		 pvt_id, pipe_id, task_id, ipc_msg);
	sst_fill_header_mrfld(&msg->mrfld_header, ipc_msg,
					task_id, large, pvt_id);
	msg->mrfld_header.p.header_low_payload = sizeof(dsp_hdr) + mbox_data_len;
	msg->mrfld_header.p.header_high.part.res_rqd = !sync;
	dev_dbg(sst->dev, "header:%x\n",
			msg->mrfld_header.p.header_high.full);
	dev_dbg(sst->dev, "response rqd: %x",
			msg->mrfld_header.p.header_high.part.res_rqd);
	dev_dbg(sst->dev, "msg->mrfld_header.p.header_low_payload:%d",
			msg->mrfld_header.p.header_low_payload);
	if (fill_dsp) {
		sst_fill_header_dsp(&dsp_hdr, cmd_id, pipe_id, mbox_data_len);
		memcpy(msg->mailbox_data, &dsp_hdr, sizeof(dsp_hdr));
		if (mbox_data_len) {
			memcpy(msg->mailbox_data + sizeof(dsp_hdr),
					mbox_data, mbox_data_len);
		}
	}

	if (sync)
		sst->ops->post_message(sst, msg, true);
	else
		sst_add_to_dispatch_list_and_post(sst, msg);

	if (response) {
		ret = sst_wait_timeout(sst, block);
		if (ret < 0)
			goto out;

		if (data && block->data) {
			*data = kmemdup(block->data, block->size, GFP_KERNEL);
			if (!*data) {
				ret = -ENOMEM;
				goto out;
			}
		}
	}
out:
	if (response)
		sst_free_block(sst, block);
	test_and_clear_bit(pvt_id, &sst->pvt_id);
	return ret;
}

int sst_pm_runtime_put(struct intel_sst_drv *sst_drv)
{
	int ret;

	pm_runtime_mark_last_busy(sst_drv->dev);
	ret = pm_runtime_put_autosuspend(sst_drv->dev);
	if (ret < 0)
		return ret;
	return 0;
}

void sst_fill_header_mrfld(union ipc_header_mrfld *header,
				int msg, int task_id, int large, int drv_id)
{
	header->full = 0;
	header->p.header_high.part.msg_id = msg;
	header->p.header_high.part.task_id = task_id;
	header->p.header_high.part.large = large;
	header->p.header_high.part.drv_id = drv_id;
	header->p.header_high.part.done = 0;
	header->p.header_high.part.busy = 1;
	header->p.header_high.part.res_rqd = 1;
}

void sst_fill_header_dsp(struct ipc_dsp_hdr *dsp, int msg,
					int pipe_id, int len)
{
	dsp->cmd_id = msg;
	dsp->mod_index_id = 0xff;
	dsp->pipe_id = pipe_id;
	dsp->length = len;
	dsp->mod_id = 0;
}

#define SST_MAX_BLOCKS 15
/*
 * sst_assign_pvt_id - assign a pvt id for stream
 *
 * @sst_drv_ctx : driver context
 *
 * this function assigns a private id for calls that dont have stream
 * context yet, should be called with lock held
 * uses bits for the id, and finds first free bits and assigns that
 */
int sst_assign_pvt_id(struct intel_sst_drv *drv)
{
	int local;

	spin_lock(&drv->block_lock);
	/* find first zero index from lsb */
	local = ffz(drv->pvt_id);
	dev_dbg(drv->dev, "pvt_id assigned --> %d\n", local);
	if (local >= SST_MAX_BLOCKS){
		spin_unlock(&drv->block_lock);
		dev_err(drv->dev, "PVT _ID error: no free id blocks ");
		return -EINVAL;
	}
	/* toggle the index */
	change_bit(local, &drv->pvt_id);
	spin_unlock(&drv->block_lock);
	return local;
}

void sst_init_stream(struct stream_info *stream,
		int codec, int sst_id, int ops, u8 slot)
{
	stream->status = STREAM_INIT;
	stream->prev = STREAM_UN_INIT;
	stream->ops = ops;
}

int sst_validate_strid(
		struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	if (str_id <= 0 || str_id > sst_drv_ctx->info.max_streams) {
		dev_err(sst_drv_ctx->dev,
			"SST ERR: invalid stream id : %d, max %d\n",
			str_id, sst_drv_ctx->info.max_streams);
		return -EINVAL;
	}

	return 0;
}

struct stream_info *get_stream_info(
		struct intel_sst_drv *sst_drv_ctx, int str_id)
{
	if (sst_validate_strid(sst_drv_ctx, str_id))
		return NULL;
	return &sst_drv_ctx->streams[str_id];
}

int get_stream_id_mrfld(struct intel_sst_drv *sst_drv_ctx,
		u32 pipe_id)
{
	int i;

	for (i = 1; i <= sst_drv_ctx->info.max_streams; i++)
		if (pipe_id == sst_drv_ctx->streams[i].pipe_id)
			return i;

	dev_dbg(sst_drv_ctx->dev, "no such pipe_id(%u)", pipe_id);
	return -1;
}

u32 relocate_imr_addr_mrfld(u32 base_addr)
{
	/* Get the difference from 512MB aligned base addr */
	/* relocate the base */
	base_addr = MRFLD_FW_VIRTUAL_BASE + (base_addr % (512 * 1024 * 1024));
	return base_addr;
}
EXPORT_SYMBOL_GPL(relocate_imr_addr_mrfld);

void sst_add_to_dispatch_list_and_post(struct intel_sst_drv *sst,
						struct ipc_post *msg)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&sst->ipc_spin_lock, irq_flags);
	list_add_tail(&msg->node, &sst->ipc_dispatch_list);
	spin_unlock_irqrestore(&sst->ipc_spin_lock, irq_flags);
	sst->ops->post_message(sst, NULL, false);
}
