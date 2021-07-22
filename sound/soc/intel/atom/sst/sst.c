// SPDX-License-Identifier: GPL-2.0-only
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/async.h>
#include <linux/acpi.h>
#include <linux/sysfs.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"

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
	unsigned int size;
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
			"SST Driver capabilities missing for dev_id: %x",
			sst->dev_id);
		return -EINVAL;
	}
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

static ssize_t firmware_version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);

	if (ctx->fw_version.type == 0 && ctx->fw_version.major == 0 &&
	    ctx->fw_version.minor == 0 && ctx->fw_version.build == 0)
		return sprintf(buf, "FW not yet loaded\n");
	else
		return sprintf(buf, "v%02x.%02x.%02x.%02x\n",
			       ctx->fw_version.type, ctx->fw_version.major,
			       ctx->fw_version.minor, ctx->fw_version.build);

}

static DEVICE_ATTR_RO(firmware_version);

static const struct attribute *sst_fw_version_attrs[] = {
	&dev_attr_firmware_version.attr,
	NULL,
};

static const struct attribute_group sst_fw_version_attr_group = {
	.attrs = (struct attribute **)sst_fw_version_attrs,
};

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
	cpu_latency_qos_add_request(ctx->qos, PM_QOS_DEFAULT_VALUE);

	dev_dbg(ctx->dev, "Requesting FW %s now...\n", ctx->firmware_name);
	ret = request_firmware_nowait(THIS_MODULE, true, ctx->firmware_name,
				      ctx->dev, GFP_KERNEL, ctx, sst_firmware_load_cb);
	if (ret) {
		dev_err(ctx->dev, "Firmware download failed:%d\n", ret);
		goto do_free_mem;
	}

	ret = sysfs_create_group(&ctx->dev->kobj,
				 &sst_fw_version_attr_group);
	if (ret) {
		dev_err(ctx->dev,
			"Unable to create sysfs\n");
		goto err_sysfs;
	}

	sst_register(ctx->dev);
	return 0;
err_sysfs:
	sysfs_remove_group(&ctx->dev->kobj, &sst_fw_version_attr_group);

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
	sysfs_remove_group(&ctx->dev->kobj, &sst_fw_version_attr_group);
	flush_scheduled_work();
	destroy_workqueue(ctx->post_msg_wq);
	cpu_latency_qos_remove_request(ctx->qos);
	kfree(ctx->fw_sg_list.src);
	kfree(ctx->fw_sg_list.dst);
	ctx->fw_sg_list.list_len = 0;
	kfree(ctx->fw_in_mem);
	ctx->fw_in_mem = NULL;
	sst_memcpy_free_resources(ctx);
}
EXPORT_SYMBOL_GPL(sst_context_cleanup);

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

	return ret;
}

static int intel_sst_suspend(struct device *dev)
{
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);
	struct sst_fw_save *fw_save;
	int i, ret;

	/* check first if we are already in SW reset */
	if (ctx->sst_state == SST_RESET)
		return 0;

	/*
	 * check if any stream is active and running
	 * they should already by suspend by soc_suspend
	 */
	for (i = 1; i <= ctx->info.max_streams; i++) {
		struct stream_info *stream = &ctx->streams[i];

		if (stream->status == STREAM_RUNNING) {
			dev_err(dev, "stream %d is running, can't suspend, abort\n", i);
			return -EBUSY;
		}

		if (ctx->pdata->streams_lost_on_suspend) {
			stream->resume_status = stream->status;
			stream->resume_prev = stream->prev;
			if (stream->status != STREAM_UN_INIT)
				sst_free_stream(ctx, i);
		}
	}
	synchronize_irq(ctx->irq_num);
	flush_workqueue(ctx->post_msg_wq);

	/* Move the SST state to Reset */
	sst_set_fw_state_locked(ctx, SST_RESET);

	/* tell DSP we are suspending */
	if (ctx->ops->save_dsp_context(ctx))
		return -EBUSY;

	/* save the memories */
	fw_save = kzalloc(sizeof(*fw_save), GFP_KERNEL);
	if (!fw_save)
		return -ENOMEM;
	fw_save->iram = kvzalloc(ctx->iram_end - ctx->iram_base, GFP_KERNEL);
	if (!fw_save->iram) {
		ret = -ENOMEM;
		goto iram;
	}
	fw_save->dram = kvzalloc(ctx->dram_end - ctx->dram_base, GFP_KERNEL);
	if (!fw_save->dram) {
		ret = -ENOMEM;
		goto dram;
	}
	fw_save->sram = kvzalloc(SST_MAILBOX_SIZE, GFP_KERNEL);
	if (!fw_save->sram) {
		ret = -ENOMEM;
		goto sram;
	}

	fw_save->ddr = kvzalloc(ctx->ddr_end - ctx->ddr_base, GFP_KERNEL);
	if (!fw_save->ddr) {
		ret = -ENOMEM;
		goto ddr;
	}

	memcpy32_fromio(fw_save->iram, ctx->iram, ctx->iram_end - ctx->iram_base);
	memcpy32_fromio(fw_save->dram, ctx->dram, ctx->dram_end - ctx->dram_base);
	memcpy32_fromio(fw_save->sram, ctx->mailbox, SST_MAILBOX_SIZE);
	memcpy32_fromio(fw_save->ddr, ctx->ddr, ctx->ddr_end - ctx->ddr_base);

	ctx->fw_save = fw_save;
	ctx->ops->reset(ctx);
	return 0;
ddr:
	kvfree(fw_save->sram);
sram:
	kvfree(fw_save->dram);
dram:
	kvfree(fw_save->iram);
iram:
	kfree(fw_save);
	return ret;
}

static int intel_sst_resume(struct device *dev)
{
	struct intel_sst_drv *ctx = dev_get_drvdata(dev);
	struct sst_fw_save *fw_save = ctx->fw_save;
	struct sst_block *block;
	int i, ret = 0;

	if (!fw_save)
		return 0;

	sst_set_fw_state_locked(ctx, SST_FW_LOADING);

	/* we have to restore the memory saved */
	ctx->ops->reset(ctx);

	ctx->fw_save = NULL;

	memcpy32_toio(ctx->iram, fw_save->iram, ctx->iram_end - ctx->iram_base);
	memcpy32_toio(ctx->dram, fw_save->dram, ctx->dram_end - ctx->dram_base);
	memcpy32_toio(ctx->mailbox, fw_save->sram, SST_MAILBOX_SIZE);
	memcpy32_toio(ctx->ddr, fw_save->ddr, ctx->ddr_end - ctx->ddr_base);

	kvfree(fw_save->sram);
	kvfree(fw_save->dram);
	kvfree(fw_save->iram);
	kvfree(fw_save->ddr);
	kfree(fw_save);

	block = sst_create_block(ctx, 0, FW_DWNL_ID);
	if (block == NULL)
		return -ENOMEM;


	/* start and wait for ack */
	ctx->ops->start(ctx);
	ret = sst_wait_timeout(ctx, block);
	if (ret) {
		dev_err(ctx->dev, "fw download failed %d\n", ret);
		/* FW download failed due to timeout */
		ret = -EBUSY;

	} else {
		sst_set_fw_state_locked(ctx, SST_FW_RUNNING);
	}

	if (ctx->pdata->streams_lost_on_suspend) {
		for (i = 1; i <= ctx->info.max_streams; i++) {
			struct stream_info *stream = &ctx->streams[i];

			if (stream->resume_status != STREAM_UN_INIT) {
				dev_dbg(ctx->dev, "Re-allocing stream %d status %d prev %d\n",
					i, stream->resume_status,
					stream->resume_prev);
				sst_realloc_stream(ctx, i);
				stream->status = stream->resume_status;
				stream->prev = stream->resume_prev;
			}
		}
	}

	sst_free_block(ctx, block);
	return ret;
}

const struct dev_pm_ops intel_sst_pm = {
	.suspend = intel_sst_suspend,
	.resume = intel_sst_resume,
	.runtime_suspend = intel_sst_runtime_suspend,
};
EXPORT_SYMBOL_GPL(intel_sst_pm);
