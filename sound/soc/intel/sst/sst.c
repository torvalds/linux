/*
 *  sst.c - Intel SST Driver for audio engine
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
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/async.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../sst-dsp.h"

MODULE_AUTHOR("Vinod Koul <vinod.koul@intel.com>");
MODULE_AUTHOR("Harsha Priya <priya.harsha@intel.com>");
MODULE_DESCRIPTION("Intel (R) SST(R) Audio Engine Driver");
MODULE_LICENSE("GPL v2");

static inline bool sst_is_process_reply(u32 msg_id)
{
	return ((msg_id & PROCESS_MSG) ? true : false);
}

static inline bool sst_validate_mailbox_size(unsigned int size)
{
	return ((size <= SST_MAILBOX_SIZE) ? true : false);
}

static irqreturn_t intel_sst_interrupt_mrfld(int irq, void *context)
{
	union interrupt_reg_mrfld isr;
	union ipc_header_mrfld header;
	union sst_imr_reg_mrfld imr;
	struct ipc_post *msg = NULL;
	unsigned int size = 0;
	struct intel_sst_drv *drv = (struct intel_sst_drv *) context;
	irqreturn_t retval = IRQ_HANDLED;

	/* Interrupt arrived, check src */
	isr.full = sst_shim_read64(drv->shim, SST_ISRX);

	if (isr.part.done_interrupt) {
		/* Clear done bit */
		spin_lock(&drv->ipc_spin_lock);
		header.full = sst_shim_read64(drv->shim,
					drv->ipc_reg.ipcx);
		header.p.header_high.part.done = 0;
		sst_shim_write64(drv->shim, drv->ipc_reg.ipcx, header.full);

		/* write 1 to clear status register */;
		isr.part.done_interrupt = 1;
		sst_shim_write64(drv->shim, SST_ISRX, isr.full);
		spin_unlock(&drv->ipc_spin_lock);

		/* we can send more messages to DSP so trigger work */
		queue_work(drv->post_msg_wq, &drv->ipc_post_msg_wq);
		retval = IRQ_HANDLED;
	}

	if (isr.part.busy_interrupt) {
		/* message from dsp so copy that */
		spin_lock(&drv->ipc_spin_lock);
		imr.full = sst_shim_read64(drv->shim, SST_IMRX);
		imr.part.busy_interrupt = 1;
		sst_shim_write64(drv->shim, SST_IMRX, imr.full);
		spin_unlock(&drv->ipc_spin_lock);
		header.full =  sst_shim_read64(drv->shim, drv->ipc_reg.ipcd);

		if (sst_create_ipc_msg(&msg, header.p.header_high.part.large)) {
			drv->ops->clear_interrupt(drv);
			return IRQ_HANDLED;
		}

		if (header.p.header_high.part.large) {
			size = header.p.header_low_payload;
			if (sst_validate_mailbox_size(size)) {
				memcpy_fromio(msg->mailbox_data,
					drv->mailbox + drv->mailbox_recv_offset, size);
			} else {
				dev_err(drv->dev,
					"Mailbox not copied, payload size is: %u\n", size);
				header.p.header_low_payload = 0;
			}
		}

		msg->mrfld_header = header;
		msg->is_process_reply =
			sst_is_process_reply(header.p.header_high.part.msg_id);
		spin_lock(&drv->rx_msg_lock);
		list_add_tail(&msg->node, &drv->rx_list);
		spin_unlock(&drv->rx_msg_lock);
		drv->ops->clear_interrupt(drv);
		retval = IRQ_WAKE_THREAD;
	}
	return retval;
}

static irqreturn_t intel_sst_irq_thread_mrfld(int irq, void *context)
{
	struct intel_sst_drv *drv = (struct intel_sst_drv *) context;
	struct ipc_post *__msg, *msg = NULL;
	unsigned long irq_flags;

	spin_lock_irqsave(&drv->rx_msg_lock, irq_flags);
	if (list_empty(&drv->rx_list)) {
		spin_unlock_irqrestore(&drv->rx_msg_lock, irq_flags);
		return IRQ_HANDLED;
	}

	list_for_each_entry_safe(msg, __msg, &drv->rx_list, node) {
		list_del(&msg->node);
		spin_unlock_irqrestore(&drv->rx_msg_lock, irq_flags);
		if (msg->is_process_reply)
			drv->ops->process_message(msg);
		else
			drv->ops->process_reply(drv, msg);

		if (msg->is_large)
			kfree(msg->mailbox_data);
		kfree(msg);
		spin_lock_irqsave(&drv->rx_msg_lock, irq_flags);
	}
	spin_unlock_irqrestore(&drv->rx_msg_lock, irq_flags);
	return IRQ_HANDLED;
}

static int sst_save_dsp_context_v2(struct intel_sst_drv *sst)
{
	int ret = 0;

	ret = sst_prepare_and_post_msg(sst, SST_TASK_ID_MEDIA, IPC_CMD,
			IPC_PREP_D3, PIPE_RSVD, 0, NULL, NULL,
			true, true, false, true);

	if (ret < 0) {
		dev_err(sst->dev, "not suspending FW!!, Err: %d\n", ret);
		return -EIO;
	}

	return 0;
}


static struct intel_sst_ops mrfld_ops = {
	.interrupt = intel_sst_interrupt_mrfld,
	.irq_thread = intel_sst_irq_thread_mrfld,
	.clear_interrupt = intel_sst_clear_intr_mrfld,
	.start = sst_start_mrfld,
	.reset = intel_sst_reset_dsp_mrfld,
	.post_message = sst_post_message_mrfld,
	.process_reply = sst_process_reply_mrfld,
	.save_dsp_context =  sst_save_dsp_context_v2,
	.alloc_stream = sst_alloc_stream_mrfld,
	.post_download = sst_post_download_mrfld,
};

int sst_driver_ops(struct intel_sst_drv *sst)
{

	switch (sst->dev_id) {
	case SST_MRFLD_PCI_ID:
	case SST_BYT_ACPI_ID:
	case SST_CHV_ACPI_ID:
		sst->tstamp = SST_TIME_STAMP_MRFLD;
		sst->ops = &mrfld_ops;
		return 0;

	default:
		dev_err(sst->dev,
			"SST Driver capablities missing for dev_id: %x", sst->dev_id);
		return -EINVAL;
	};
}

void sst_process_pending_msg(struct work_struct *work)
{
	struct intel_sst_drv *ctx = container_of(work,
			struct intel_sst_drv, ipc_post_msg_wq);

	ctx->ops->post_message(ctx, NULL, false);
}

static int sst_workqueue_init(struct intel_sst_drv *ctx)
{
	INIT_LIST_HEAD(&ctx->memcpy_list);
	INIT_LIST_HEAD(&ctx->rx_list);
	INIT_LIST_HEAD(&ctx->ipc_dispatch_list);
	INIT_LIST_HEAD(&ctx->block_list);
	INIT_WORK(&ctx->ipc_post_msg_wq, sst_process_pending_msg);
	init_waitqueue_head(&ctx->wait_queue);

	ctx->post_msg_wq =
		create_singlethread_workqueue("sst_post_msg_wq");
	if (!ctx->post_msg_wq)
		return -EBUSY;
	return 0;
}

static void sst_init_locks(struct intel_sst_drv *ctx)
{
	mutex_init(&ctx->sst_lock);
	spin_lock_init(&ctx->rx_msg_lock);
	spin_lock_init(&ctx->ipc_spin_lock);
	spin_lock_init(&ctx->block_lock);
}

int sst_alloc_drv_context(struct intel_sst_drv **ctx,
		struct device *dev, unsigned int dev_id)
{
	*ctx = devm_kzalloc(dev, sizeof(struct intel_sst_drv), GFP_KERNEL);
	if (!(*ctx))
		return -ENOMEM;

	(*ctx)->dev = dev;
	(*ctx)->dev_id = dev_id;

	return 0;
}
EXPORT_SYMBOL_GPL(sst_alloc_drv_context);

int sst_context_init(struct intel_sst_drv *ctx)
{
	int ret = 0, i;

	if (!ctx->pdata)
		return -EINVAL;

	if (!ctx->pdata->probe_data)
		return -EINVAL;

	memcpy(&ctx->info, ctx->pdata->probe_data, sizeof(ctx->info));

	ret = sst_driver_ops(ctx);
	if (ret != 0)
		return -EINVAL;

	sst_init_locks(ctx);
	sst_set_fw_state_locked(ctx, SST_RESET);

	/* pvt_id 0 reserved for async messages */
	ctx->pvt_id = 1;
	ctx->stream_cnt = 0;
	ctx->fw_in_mem = NULL;
	/* we use memcpy, so set to 0 */
	ctx->use_dma = 0;
	ctx->use_lli = 0;

	if (sst_workqueue_init(ctx))
		return -EINVAL;

	ctx->mailbox_recv_offset = ctx->pdata->ipc_info->mbox_recv_off;
	ctx->ipc_reg.ipcx = SST_IPCX + ctx->pdata->ipc_info->ipc_offset;
	ctx->ipc_reg.ipcd = SST_IPCD + ctx->pdata->ipc_info->ipc_offset;

	dev_info(ctx->dev, "Got drv data max stream %d\n",
				ctx->info.max_streams);

	for (i = 1; i <= ctx->info.max_streams; i++) {
		struct stream_info *stream = &ctx->streams[i];

		memset(stream, 0, sizeof(*stream));
		stream->pipe_id = PIPE_RSVD;
		mutex_init(&stream->lock);
	}

	/* Register the ISR */
	ret = devm_request_threaded_irq(ctx->dev, ctx->irq_num, ctx->ops->interrupt,
					ctx->ops->irq_thread, 0, SST_DRV_NAME,
					ctx);
	if (ret)
		goto do_free_mem;

	dev_dbg(ctx->dev, "Registered IRQ %#x\n", ctx->irq_num);

	/* default intr are unmasked so set this as masked */
	sst_shim_write64(ctx->shim, SST_IMRX, 0xFFFF0038);

	ctx->qos = devm_kzalloc(ctx->dev,
		sizeof(struct pm_qos_request), GFP_KERNEL);
	if (!ctx->qos) {
		ret = -ENOMEM;
		goto do_free_mem;
	}
	pm_qos_add_request(ctx->qos, PM_QOS_CPU_DMA_LATENCY,
				PM_QOS_DEFAULT_VALUE);

	dev_dbg(ctx->dev, "Requesting FW %s now...\n", ctx->firmware_name);
	ret = request_firmware_nowait(THIS_MODULE, true, ctx->firmware_name,
				      ctx->dev, GFP_KERNEL, ctx, sst_firmware_load_cb);
	if (ret) {
		dev_err(ctx->dev, "Firmware download failed:%d\n", ret);
		goto do_free_mem;
	}
	sst_register(ctx->dev);
	return 0;

do_free_mem:
	destroy_workqueue(ctx->post_msg_wq);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_context_init);

void sst_context_cleanup(struct intel_sst_drv *ctx)
{
	pm_runtime_get_noresume(ctx->dev);
	pm_runtime_disable(ctx->dev);
	sst_unregister(ctx->dev);
	sst_set_fw_state_locked(ctx, SST_SHUTDOWN);
	flush_scheduled_work();
	destroy_workqueue(ctx->post_msg_wq);
	pm_qos_remove_request(ctx->qos);
	kfree(ctx->fw_sg_list.src);
	kfree(ctx->fw_sg_list.dst);
	ctx->fw_sg_list.list_len = 0;
	kfree(ctx->fw_in_mem);
	ctx->fw_in_mem = NULL;
	sst_memcpy_free_resources(ctx);
	ctx = NULL;
}
EXPORT_SYMBOL_GPL(sst_context_cleanup);

static inline void sst_save_shim64(struct intel_sst_drv *ctx,
			    void __iomem *shim,
			    struct sst_shim_regs64 *shim_regs)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&ctx->ipc_spin_lock, irq_flags);

	shim_regs->imrx = sst_shim_read64(shim, SST_IMRX);
	shim_regs->csr = sst_shim_read64(shim, SST_CSR);


	spin_unlock_irqrestore(&ctx->ipc_spin_lock, irq_flags);
}

static inline void sst_restore_shim64(struct intel_sst_drv *ctx,
				      void __iomem *shim,
				      struct sst_shim_regs64 *shim_regs)
{
	unsigned long irq_flags;

	/*
	 * we only need to restore IMRX for this case, rest will be
	 * initialize by FW or driver when firmware is loaded
	 */
	spin_lock_irqsave(&ctx->ipc_spin_lock, irq_flags);
	sst_shim_write64(shim, SST_IMRX, shim_regs->imrx),
	sst_shim_write64(shim, SST_CSR, shim_regs->csr),
	spin_unlock_irqrestore(&ctx->ipc_spin_lock, irq_flags);
}

void sst_configure_runtime_pm(struct intel_sst_drv *ctx)
{
	pm_runtime_set_autosuspend_delay(ctx->dev, SST_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(ctx->dev);
	/*
	 * For acpi devices, the actual physical device state is
	 * initially active. So change the state to active before
	 * enabling the pm
	 */

	if (!acpi_disabled)
		pm_runtime_set_active(ctx->dev);

	pm_runtime_enable(ctx->dev);

	if (acpi_disabled)
		pm_runtime_set_active(ctx->dev);
	else
		pm_runtime_put_noidle(ctx->dev);

	sst_save_shim64(ctx, ctx->shim, ctx->shim_regs64);
}
EXPORT_SYMBOL_GPL(sst_configure_runtime_pm);

static int intel_sst_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (ctx->sst_state == SST_RESET) {
		dev_dbg(dev, "LPE is already in RESET state, No action\n");
		return 0;
	}
	/* save fw context */
	if (ctx->ops->save_dsp_context(ctx))
		return -EBUSY;

	/* Move the SST state to Reset */
	sst_set_fw_state_locked(ctx, SST_RESET);

	synchronize_irq(ctx->irq_num);
	flush_workqueue(ctx->post_msg_wq);

	ctx->ops->reset(ctx);
	/* save the shim registers because PMC doesn't save state */
	sst_save_shim64(ctx, ctx->shim, ctx->shim_regs64);

	return ret;
}

static int intel_sst_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (ctx->sst_state == SST_RESET) {
		ret = sst_load_fw(ctx);
		if (ret) {
			dev_err(dev, "FW download fail %d\n", ret);
			sst_set_fw_state_locked(ctx, SST_RESET);
		}
	}
	return ret;
}

const struct dev_pm_ops intel_sst_pm = {
	.runtime_suspend = intel_sst_runtime_suspend,
	.runtime_resume = intel_sst_runtime_resume,
};
EXPORT_SYMBOL_GPL(intel_sst_pm);
