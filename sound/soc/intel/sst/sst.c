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
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/intel-mid.h>
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

static struct intel_sst_ops mrfld_ops = {
	.interrupt = intel_sst_interrupt_mrfld,
	.irq_thread = intel_sst_irq_thread_mrfld,
	.clear_interrupt = intel_sst_clear_intr_mrfld,
	.start = sst_start_mrfld,
	.reset = intel_sst_reset_dsp_mrfld,
	.post_message = sst_post_message_mrfld,
	.process_reply = sst_process_reply_mrfld,
	.alloc_stream = sst_alloc_stream_mrfld,
	.post_download = sst_post_download_mrfld,
};

int sst_driver_ops(struct intel_sst_drv *sst)
{

	switch (sst->pci_id) {
	case SST_MRFLD_PCI_ID:
		sst->tstamp = SST_TIME_STAMP_MRFLD;
		sst->ops = &mrfld_ops;
		return 0;

	default:
		dev_err(sst->dev,
			"SST Driver capablities missing for pci_id: %x", sst->pci_id);
		return -EINVAL;
	};
}

void sst_process_pending_msg(struct work_struct *work)
{
	struct intel_sst_drv *ctx = container_of(work,
			struct intel_sst_drv, ipc_post_msg_wq);

	ctx->ops->post_message(ctx, NULL, false);
}

/*
* intel_sst_probe - PCI probe function
*
* @pci:	PCI device structure
* @pci_id: PCI device ID structure
*
*/
static int intel_sst_probe(struct pci_dev *pci,
			const struct pci_device_id *pci_id)
{
	int i, ret = 0;
	struct intel_sst_drv *sst_drv_ctx;
	struct intel_sst_ops *ops;
	struct sst_platform_info *sst_pdata = pci->dev.platform_data;
	int ddr_base;

	dev_dbg(&pci->dev, "Probe for DID %x\n", pci->device);
	sst_drv_ctx = devm_kzalloc(&pci->dev, sizeof(*sst_drv_ctx), GFP_KERNEL);
	if (!sst_drv_ctx)
		return -ENOMEM;

	sst_drv_ctx->dev = &pci->dev;
	sst_drv_ctx->pci_id = pci->device;
	if (!sst_pdata)
		return -EINVAL;

	sst_drv_ctx->pdata = sst_pdata;
	if (!sst_drv_ctx->pdata->probe_data)
		return -EINVAL;

	memcpy(&sst_drv_ctx->info, sst_drv_ctx->pdata->probe_data,
					sizeof(sst_drv_ctx->info));

	if (0 != sst_driver_ops(sst_drv_ctx))
		return -EINVAL;

	ops = sst_drv_ctx->ops;
	mutex_init(&sst_drv_ctx->sst_lock);

	/* pvt_id 0 reserved for async messages */
	sst_drv_ctx->pvt_id = 1;
	sst_drv_ctx->stream_cnt = 0;
	sst_drv_ctx->fw_in_mem = NULL;

	/* we use memcpy, so set to 0 */
	sst_drv_ctx->use_dma = 0;
	sst_drv_ctx->use_lli = 0;

	INIT_LIST_HEAD(&sst_drv_ctx->memcpy_list);
	INIT_LIST_HEAD(&sst_drv_ctx->ipc_dispatch_list);
	INIT_LIST_HEAD(&sst_drv_ctx->block_list);
	INIT_LIST_HEAD(&sst_drv_ctx->rx_list);

	sst_drv_ctx->post_msg_wq =
		create_singlethread_workqueue("sst_post_msg_wq");
	if (!sst_drv_ctx->post_msg_wq) {
		ret = -EINVAL;
		goto do_free_drv_ctx;
	}
	INIT_WORK(&sst_drv_ctx->ipc_post_msg_wq, sst_process_pending_msg);
	init_waitqueue_head(&sst_drv_ctx->wait_queue);

	spin_lock_init(&sst_drv_ctx->ipc_spin_lock);
	spin_lock_init(&sst_drv_ctx->block_lock);
	spin_lock_init(&sst_drv_ctx->rx_msg_lock);

	dev_info(sst_drv_ctx->dev, "Got drv data max stream %d\n",
				sst_drv_ctx->info.max_streams);
	for (i = 1; i <= sst_drv_ctx->info.max_streams; i++) {
		struct stream_info *stream = &sst_drv_ctx->streams[i];

		memset(stream, 0, sizeof(*stream));
		stream->pipe_id = PIPE_RSVD;
		mutex_init(&stream->lock);
	}

	/* Init the device */
	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(sst_drv_ctx->dev,
			"device can't be enabled. Returned err: %d\n", ret);
		goto do_free_mem;
	}
	sst_drv_ctx->pci = pci_dev_get(pci);
	ret = pci_request_regions(pci, SST_DRV_NAME);
	if (ret)
		goto do_free_mem;

	/* map registers */
	/* DDR base */
	if (sst_drv_ctx->pci_id == SST_MRFLD_PCI_ID) {
		sst_drv_ctx->ddr_base = pci_resource_start(pci, 0);
		/* check that the relocated IMR base matches with FW Binary */
		ddr_base = relocate_imr_addr_mrfld(sst_drv_ctx->ddr_base);
		if (!sst_drv_ctx->pdata->lib_info) {
			dev_err(sst_drv_ctx->dev, "lib_info pointer NULL\n");
			ret = -EINVAL;
			goto do_release_regions;
		}
		if (ddr_base != sst_drv_ctx->pdata->lib_info->mod_base) {
			dev_err(sst_drv_ctx->dev,
					"FW LSP DDR BASE does not match with IFWI\n");
			ret = -EINVAL;
			goto do_release_regions;
		}
		sst_drv_ctx->ddr_end = pci_resource_end(pci, 0);

		sst_drv_ctx->ddr = pcim_iomap(pci, 0,
					pci_resource_len(pci, 0));
		if (!sst_drv_ctx->ddr) {
			ret = -EINVAL;
			goto do_release_regions;
		}
		dev_dbg(sst_drv_ctx->dev, "sst: DDR Ptr %p\n", sst_drv_ctx->ddr);
	} else {
		sst_drv_ctx->ddr = NULL;
	}

	/* SHIM */
	sst_drv_ctx->shim_phy_add = pci_resource_start(pci, 1);
	sst_drv_ctx->shim = pcim_iomap(pci, 1, pci_resource_len(pci, 1));
	if (!sst_drv_ctx->shim) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(sst_drv_ctx->dev, "SST Shim Ptr %p\n", sst_drv_ctx->shim);

	/* Shared SRAM */
	sst_drv_ctx->mailbox_add = pci_resource_start(pci, 2);
	sst_drv_ctx->mailbox = pcim_iomap(pci, 2, pci_resource_len(pci, 2));
	if (!sst_drv_ctx->mailbox) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(sst_drv_ctx->dev, "SRAM Ptr %p\n", sst_drv_ctx->mailbox);

	/* IRAM */
	sst_drv_ctx->iram_end = pci_resource_end(pci, 3);
	sst_drv_ctx->iram_base = pci_resource_start(pci, 3);
	sst_drv_ctx->iram = pcim_iomap(pci, 3, pci_resource_len(pci, 3));
	if (!sst_drv_ctx->iram) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(sst_drv_ctx->dev, "IRAM Ptr %p\n", sst_drv_ctx->iram);

	/* DRAM */
	sst_drv_ctx->dram_end = pci_resource_end(pci, 4);
	sst_drv_ctx->dram_base = pci_resource_start(pci, 4);
	sst_drv_ctx->dram = pcim_iomap(pci, 4, pci_resource_len(pci, 4));
	if (!sst_drv_ctx->dram) {
		ret = -EINVAL;
		goto do_release_regions;
	}
	dev_dbg(sst_drv_ctx->dev, "DRAM Ptr %p\n", sst_drv_ctx->dram);


	sst_set_fw_state_locked(sst_drv_ctx, SST_RESET);
	sst_drv_ctx->irq_num = pci->irq;
	/* Register the ISR */
	ret = devm_request_threaded_irq(&pci->dev, pci->irq,
		sst_drv_ctx->ops->interrupt,
		sst_drv_ctx->ops->irq_thread, 0, SST_DRV_NAME,
		sst_drv_ctx);
	if (ret)
		goto do_release_regions;
	dev_dbg(sst_drv_ctx->dev, "Registered IRQ 0x%x\n", pci->irq);

	/* default intr are unmasked so set this as masked */
	if (sst_drv_ctx->pci_id == SST_MRFLD_PCI_ID)
		sst_shim_write64(sst_drv_ctx->shim, SST_IMRX, 0xFFFF0038);

	pci_set_drvdata(pci, sst_drv_ctx);
	pm_runtime_set_autosuspend_delay(sst_drv_ctx->dev, SST_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(sst_drv_ctx->dev);
	pm_runtime_allow(sst_drv_ctx->dev);
	pm_runtime_put_noidle(sst_drv_ctx->dev);
	sst_register(sst_drv_ctx->dev);
	sst_drv_ctx->qos = devm_kzalloc(&pci->dev,
		sizeof(struct pm_qos_request), GFP_KERNEL);
	if (!sst_drv_ctx->qos) {
		ret = -ENOMEM;
		goto do_release_regions;
	}
	pm_qos_add_request(sst_drv_ctx->qos, PM_QOS_CPU_DMA_LATENCY,
				PM_QOS_DEFAULT_VALUE);

	return ret;

do_release_regions:
	pci_release_regions(pci);
do_free_mem:
	destroy_workqueue(sst_drv_ctx->post_msg_wq);
do_free_drv_ctx:
	dev_err(sst_drv_ctx->dev, "Probe failed with %d\n", ret);
	return ret;
}

/**
* intel_sst_remove - PCI remove function
*
* @pci:	PCI device structure
*
* This function is called by OS when a device is unloaded
* This frees the interrupt etc
*/
static void intel_sst_remove(struct pci_dev *pci)
{
	struct intel_sst_drv *sst_drv_ctx = pci_get_drvdata(pci);

	pm_runtime_get_noresume(sst_drv_ctx->dev);
	pm_runtime_forbid(sst_drv_ctx->dev);
	sst_unregister(sst_drv_ctx->dev);
	pci_dev_put(sst_drv_ctx->pci);
	sst_set_fw_state_locked(sst_drv_ctx, SST_SHUTDOWN);

	flush_scheduled_work();
	destroy_workqueue(sst_drv_ctx->post_msg_wq);
	pm_qos_remove_request(sst_drv_ctx->qos);
	kfree(sst_drv_ctx->fw_sg_list.src);
	kfree(sst_drv_ctx->fw_sg_list.dst);
	sst_drv_ctx->fw_sg_list.list_len = 0;
	kfree(sst_drv_ctx->fw_in_mem);
	sst_drv_ctx->fw_in_mem = NULL;
	sst_memcpy_free_resources(sst_drv_ctx);
	sst_drv_ctx = NULL;
	pci_release_regions(pci);
	pci_set_drvdata(pci, NULL);
}

/* PCI Routines */
static struct pci_device_id intel_sst_ids[] = {
	{ PCI_VDEVICE(INTEL, SST_MRFLD_PCI_ID), 0},
	{ 0, }
};

static struct pci_driver sst_driver = {
	.name = SST_DRV_NAME,
	.id_table = intel_sst_ids,
	.probe = intel_sst_probe,
	.remove = intel_sst_remove,
};

module_pci_driver(sst_driver);
