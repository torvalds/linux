// SPDX-License-Identifier: GPL-2.0-only
/*
 * cnl-sst.c - DSP library functions for CNL platform
 *
 * Copyright (C) 2016-17, Intel Corporation.
 *
 * Author: Guneshwor Singh <guneshwor.o.singh@intel.com>
 *
 * Modified from:
 *	HDA DSP library functions for SKL platform
 *	Copyright (C) 2014-15, Intel Corporation.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/device.h>

#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "../common/sst-ipc.h"
#include "cnl-sst-dsp.h"
#include "skl.h"

#define CNL_FW_ROM_INIT		0x1
#define CNL_FW_INIT		0x5
#define CNL_IPC_PURGE		0x01004000
#define CNL_INIT_TIMEOUT	300
#define CNL_BASEFW_TIMEOUT	3000

#define CNL_ADSP_SRAM0_BASE	0x80000

/* Firmware status window */
#define CNL_ADSP_FW_STATUS	CNL_ADSP_SRAM0_BASE
#define CNL_ADSP_ERROR_CODE	(CNL_ADSP_FW_STATUS + 0x4)

#define CNL_INSTANCE_ID		0
#define CNL_BASE_FW_MODULE_ID	0
#define CNL_ADSP_FW_HDR_OFFSET	0x2000
#define CNL_ROM_CTRL_DMA_ID	0x9

static int cnl_prepare_fw(struct sst_dsp *ctx, const void *fwdata, u32 fwsize)
{

	int ret, stream_tag;

	stream_tag = ctx->dsp_ops.prepare(ctx->dev, 0x40, fwsize, &ctx->dmab);
	if (stream_tag <= 0) {
		dev_err(ctx->dev, "dma prepare failed: 0%#x\n", stream_tag);
		return stream_tag;
	}

	ctx->dsp_ops.stream_tag = stream_tag;
	memcpy(ctx->dmab.area, fwdata, fwsize);

	/* purge FW request */
	sst_dsp_shim_write(ctx, CNL_ADSP_REG_HIPCIDR,
			   CNL_ADSP_REG_HIPCIDR_BUSY | (CNL_IPC_PURGE |
			   ((stream_tag - 1) << CNL_ROM_CTRL_DMA_ID)));

	ret = cnl_dsp_enable_core(ctx, SKL_DSP_CORE0_MASK);
	if (ret < 0) {
		dev_err(ctx->dev, "dsp boot core failed ret: %d\n", ret);
		ret = -EIO;
		goto base_fw_load_failed;
	}

	/* enable interrupt */
	cnl_ipc_int_enable(ctx);
	cnl_ipc_op_int_enable(ctx);

	ret = sst_dsp_register_poll(ctx, CNL_ADSP_FW_STATUS, CNL_FW_STS_MASK,
				    CNL_FW_ROM_INIT, CNL_INIT_TIMEOUT,
				    "rom load");
	if (ret < 0) {
		dev_err(ctx->dev, "rom init timeout, ret: %d\n", ret);
		goto base_fw_load_failed;
	}

	return 0;

base_fw_load_failed:
	ctx->dsp_ops.cleanup(ctx->dev, &ctx->dmab, stream_tag);
	cnl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);

	return ret;
}

static int sst_transfer_fw_host_dma(struct sst_dsp *ctx)
{
	int ret;

	ctx->dsp_ops.trigger(ctx->dev, true, ctx->dsp_ops.stream_tag);
	ret = sst_dsp_register_poll(ctx, CNL_ADSP_FW_STATUS, CNL_FW_STS_MASK,
				    CNL_FW_INIT, CNL_BASEFW_TIMEOUT,
				    "firmware boot");

	ctx->dsp_ops.trigger(ctx->dev, false, ctx->dsp_ops.stream_tag);
	ctx->dsp_ops.cleanup(ctx->dev, &ctx->dmab, ctx->dsp_ops.stream_tag);

	return ret;
}

static int cnl_load_base_firmware(struct sst_dsp *ctx)
{
	struct firmware stripped_fw;
	struct skl_dev *cnl = ctx->thread_context;
	int ret, i;

	if (!ctx->fw) {
		ret = request_firmware(&ctx->fw, ctx->fw_name, ctx->dev);
		if (ret < 0) {
			dev_err(ctx->dev, "request firmware failed: %d\n", ret);
			goto cnl_load_base_firmware_failed;
		}
	}

	/* parse uuids if first boot */
	if (cnl->is_first_boot) {
		ret = snd_skl_parse_uuids(ctx, ctx->fw,
					  CNL_ADSP_FW_HDR_OFFSET, 0);
		if (ret < 0)
			goto cnl_load_base_firmware_failed;
	}

	stripped_fw.data = ctx->fw->data;
	stripped_fw.size = ctx->fw->size;
	skl_dsp_strip_extended_manifest(&stripped_fw);

	for (i = 0; i < BXT_FW_ROM_INIT_RETRY; i++) {
		ret = cnl_prepare_fw(ctx, stripped_fw.data, stripped_fw.size);
		if (!ret)
			break;
		dev_dbg(ctx->dev, "prepare firmware failed: %d\n", ret);
	}

	if (ret < 0)
		goto cnl_load_base_firmware_failed;

	ret = sst_transfer_fw_host_dma(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "transfer firmware failed: %d\n", ret);
		cnl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);
		goto cnl_load_base_firmware_failed;
	}

	ret = wait_event_timeout(cnl->boot_wait, cnl->boot_complete,
				 msecs_to_jiffies(SKL_IPC_BOOT_MSECS));
	if (ret == 0) {
		dev_err(ctx->dev, "FW ready timed-out\n");
		cnl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);
		ret = -EIO;
		goto cnl_load_base_firmware_failed;
	}

	cnl->fw_loaded = true;

	return 0;

cnl_load_base_firmware_failed:
	dev_err(ctx->dev, "firmware load failed: %d\n", ret);
	release_firmware(ctx->fw);
	ctx->fw = NULL;

	return ret;
}

static int cnl_set_dsp_D0(struct sst_dsp *ctx, unsigned int core_id)
{
	struct skl_dev *cnl = ctx->thread_context;
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);
	struct skl_ipc_dxstate_info dx;
	int ret;

	if (!cnl->fw_loaded) {
		cnl->boot_complete = false;
		ret = cnl_load_base_firmware(ctx);
		if (ret < 0) {
			dev_err(ctx->dev, "fw reload failed: %d\n", ret);
			return ret;
		}

		cnl->cores.state[core_id] = SKL_DSP_RUNNING;
		return ret;
	}

	ret = cnl_dsp_enable_core(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "enable dsp core %d failed: %d\n",
			core_id, ret);
		goto err;
	}

	if (core_id == SKL_DSP_CORE0_ID) {
		/* enable interrupt */
		cnl_ipc_int_enable(ctx);
		cnl_ipc_op_int_enable(ctx);
		cnl->boot_complete = false;

		ret = wait_event_timeout(cnl->boot_wait, cnl->boot_complete,
					 msecs_to_jiffies(SKL_IPC_BOOT_MSECS));
		if (ret == 0) {
			dev_err(ctx->dev,
				"dsp boot timeout, status=%#x error=%#x\n",
				sst_dsp_shim_read(ctx, CNL_ADSP_FW_STATUS),
				sst_dsp_shim_read(ctx, CNL_ADSP_ERROR_CODE));
			goto err;
		}
	} else {
		dx.core_mask = core_mask;
		dx.dx_mask = core_mask;

		ret = skl_ipc_set_dx(&cnl->ipc, CNL_INSTANCE_ID,
				     CNL_BASE_FW_MODULE_ID, &dx);
		if (ret < 0) {
			dev_err(ctx->dev, "set_dx failed, core: %d ret: %d\n",
				core_id, ret);
			goto err;
		}
	}
	cnl->cores.state[core_id] = SKL_DSP_RUNNING;

	return 0;
err:
	cnl_dsp_disable_core(ctx, core_mask);

	return ret;
}

static int cnl_set_dsp_D3(struct sst_dsp *ctx, unsigned int core_id)
{
	struct skl_dev *cnl = ctx->thread_context;
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);
	struct skl_ipc_dxstate_info dx;
	int ret;

	dx.core_mask = core_mask;
	dx.dx_mask = SKL_IPC_D3_MASK;

	ret = skl_ipc_set_dx(&cnl->ipc, CNL_INSTANCE_ID,
			     CNL_BASE_FW_MODULE_ID, &dx);
	if (ret < 0) {
		dev_err(ctx->dev,
			"dsp core %d to d3 failed; continue reset\n",
			core_id);
		cnl->fw_loaded = false;
	}

	/* disable interrupts if core 0 */
	if (core_id == SKL_DSP_CORE0_ID) {
		skl_ipc_op_int_disable(ctx);
		skl_ipc_int_disable(ctx);
	}

	ret = cnl_dsp_disable_core(ctx, core_mask);
	if (ret < 0) {
		dev_err(ctx->dev, "disable dsp core %d failed: %d\n",
			core_id, ret);
		return ret;
	}

	cnl->cores.state[core_id] = SKL_DSP_RESET;

	return ret;
}

static unsigned int cnl_get_errno(struct sst_dsp *ctx)
{
	return sst_dsp_shim_read(ctx, CNL_ADSP_ERROR_CODE);
}

static const struct skl_dsp_fw_ops cnl_fw_ops = {
	.set_state_D0 = cnl_set_dsp_D0,
	.set_state_D3 = cnl_set_dsp_D3,
	.load_fw = cnl_load_base_firmware,
	.get_fw_errcode = cnl_get_errno,
};

static struct sst_ops cnl_ops = {
	.irq_handler = cnl_dsp_sst_interrupt,
	.write = sst_shim32_write,
	.read = sst_shim32_read,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.free = cnl_dsp_free,
};

#define CNL_IPC_GLB_NOTIFY_RSP_SHIFT	29
#define CNL_IPC_GLB_NOTIFY_RSP_MASK	0x1
#define CNL_IPC_GLB_NOTIFY_RSP_TYPE(x)	(((x) >> CNL_IPC_GLB_NOTIFY_RSP_SHIFT) \
					& CNL_IPC_GLB_NOTIFY_RSP_MASK)

static irqreturn_t cnl_dsp_irq_thread_handler(int irq, void *context)
{
	struct sst_dsp *dsp = context;
	struct skl_dev *cnl = sst_dsp_get_thread_context(dsp);
	struct sst_generic_ipc *ipc = &cnl->ipc;
	struct skl_ipc_header header = {0};
	u32 hipcida, hipctdr, hipctdd;
	int ipc_irq = 0;

	/* here we handle ipc interrupts only */
	if (!(dsp->intr_status & CNL_ADSPIS_IPC))
		return IRQ_NONE;

	hipcida = sst_dsp_shim_read_unlocked(dsp, CNL_ADSP_REG_HIPCIDA);
	hipctdr = sst_dsp_shim_read_unlocked(dsp, CNL_ADSP_REG_HIPCTDR);
	hipctdd = sst_dsp_shim_read_unlocked(dsp, CNL_ADSP_REG_HIPCTDD);

	/* reply message from dsp */
	if (hipcida & CNL_ADSP_REG_HIPCIDA_DONE) {
		sst_dsp_shim_update_bits(dsp, CNL_ADSP_REG_HIPCCTL,
			CNL_ADSP_REG_HIPCCTL_DONE, 0);

		/* clear done bit - tell dsp operation is complete */
		sst_dsp_shim_update_bits_forced(dsp, CNL_ADSP_REG_HIPCIDA,
			CNL_ADSP_REG_HIPCIDA_DONE, CNL_ADSP_REG_HIPCIDA_DONE);

		ipc_irq = 1;

		/* unmask done interrupt */
		sst_dsp_shim_update_bits(dsp, CNL_ADSP_REG_HIPCCTL,
			CNL_ADSP_REG_HIPCCTL_DONE, CNL_ADSP_REG_HIPCCTL_DONE);
	}

	/* new message from dsp */
	if (hipctdr & CNL_ADSP_REG_HIPCTDR_BUSY) {
		header.primary = hipctdr;
		header.extension = hipctdd;
		dev_dbg(dsp->dev, "IPC irq: Firmware respond primary:%x",
						header.primary);
		dev_dbg(dsp->dev, "IPC irq: Firmware respond extension:%x",
						header.extension);

		if (CNL_IPC_GLB_NOTIFY_RSP_TYPE(header.primary)) {
			/* Handle Immediate reply from DSP Core */
			skl_ipc_process_reply(ipc, header);
		} else {
			dev_dbg(dsp->dev, "IPC irq: Notification from firmware\n");
			skl_ipc_process_notification(ipc, header);
		}
		/* clear busy interrupt */
		sst_dsp_shim_update_bits_forced(dsp, CNL_ADSP_REG_HIPCTDR,
			CNL_ADSP_REG_HIPCTDR_BUSY, CNL_ADSP_REG_HIPCTDR_BUSY);

		/* set done bit to ack dsp */
		sst_dsp_shim_update_bits_forced(dsp, CNL_ADSP_REG_HIPCTDA,
			CNL_ADSP_REG_HIPCTDA_DONE, CNL_ADSP_REG_HIPCTDA_DONE);
		ipc_irq = 1;
	}

	if (ipc_irq == 0)
		return IRQ_NONE;

	cnl_ipc_int_enable(dsp);

	/* continue to send any remaining messages */
	schedule_work(&ipc->kwork);

	return IRQ_HANDLED;
}

static struct sst_dsp_device cnl_dev = {
	.thread = cnl_dsp_irq_thread_handler,
	.ops = &cnl_ops,
};

static void cnl_ipc_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	struct skl_ipc_header *header = (struct skl_ipc_header *)(&msg->tx.header);

	if (msg->tx.size)
		sst_dsp_outbox_write(ipc->dsp, msg->tx.data, msg->tx.size);
	sst_dsp_shim_write_unlocked(ipc->dsp, CNL_ADSP_REG_HIPCIDD,
				    header->extension);
	sst_dsp_shim_write_unlocked(ipc->dsp, CNL_ADSP_REG_HIPCIDR,
				header->primary | CNL_ADSP_REG_HIPCIDR_BUSY);
}

static bool cnl_ipc_is_dsp_busy(struct sst_dsp *dsp)
{
	u32 hipcidr;

	hipcidr = sst_dsp_shim_read_unlocked(dsp, CNL_ADSP_REG_HIPCIDR);

	return (hipcidr & CNL_ADSP_REG_HIPCIDR_BUSY);
}

static int cnl_ipc_init(struct device *dev, struct skl_dev *cnl)
{
	struct sst_generic_ipc *ipc;
	int err;

	ipc = &cnl->ipc;
	ipc->dsp = cnl->dsp;
	ipc->dev = dev;

	ipc->tx_data_max_size = CNL_ADSP_W1_SZ;
	ipc->rx_data_max_size = CNL_ADSP_W0_UP_SZ;

	err = sst_ipc_init(ipc);
	if (err)
		return err;

	/*
	 * overriding tx_msg and is_dsp_busy since
	 * ipc registers are different for cnl
	 */
	ipc->ops.tx_msg = cnl_ipc_tx_msg;
	ipc->ops.tx_data_copy = skl_ipc_tx_data_copy;
	ipc->ops.is_dsp_busy = cnl_ipc_is_dsp_busy;

	return 0;
}

int cnl_sst_dsp_init(struct device *dev, void __iomem *mmio_base, int irq,
		     const char *fw_name, struct skl_dsp_loader_ops dsp_ops,
		     struct skl_dev **dsp)
{
	struct skl_dev *cnl;
	struct sst_dsp *sst;
	int ret;

	ret = skl_sst_ctx_init(dev, irq, fw_name, dsp_ops, dsp, &cnl_dev);
	if (ret < 0) {
		dev_err(dev, "%s: no device\n", __func__);
		return ret;
	}

	cnl = *dsp;
	sst = cnl->dsp;
	sst->fw_ops = cnl_fw_ops;
	sst->addr.lpe = mmio_base;
	sst->addr.shim = mmio_base;
	sst->addr.sram0_base = CNL_ADSP_SRAM0_BASE;
	sst->addr.sram1_base = CNL_ADSP_SRAM1_BASE;
	sst->addr.w0_stat_sz = CNL_ADSP_W0_STAT_SZ;
	sst->addr.w0_up_sz = CNL_ADSP_W0_UP_SZ;

	sst_dsp_mailbox_init(sst, (CNL_ADSP_SRAM0_BASE + CNL_ADSP_W0_STAT_SZ),
			     CNL_ADSP_W0_UP_SZ, CNL_ADSP_SRAM1_BASE,
			     CNL_ADSP_W1_SZ);

	ret = cnl_ipc_init(dev, cnl);
	if (ret) {
		skl_dsp_free(sst);
		return ret;
	}

	cnl->boot_complete = false;
	init_waitqueue_head(&cnl->boot_wait);

	return skl_dsp_acquire_irq(sst);
}
EXPORT_SYMBOL_GPL(cnl_sst_dsp_init);

int cnl_sst_init_fw(struct device *dev, struct skl_dev *skl)
{
	int ret;
	struct sst_dsp *sst = skl->dsp;

	ret = skl->dsp->fw_ops.load_fw(sst);
	if (ret < 0) {
		dev_err(dev, "load base fw failed: %d", ret);
		return ret;
	}

	skl_dsp_init_core_state(sst);

	skl->is_first_boot = false;

	return 0;
}
EXPORT_SYMBOL_GPL(cnl_sst_init_fw);

void cnl_sst_dsp_cleanup(struct device *dev, struct skl_dev *skl)
{
	if (skl->dsp->fw)
		release_firmware(skl->dsp->fw);

	skl_freeup_uuid_list(skl);
	cnl_ipc_free(&skl->ipc);

	skl->dsp->ops->free(skl->dsp);
}
EXPORT_SYMBOL_GPL(cnl_sst_dsp_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Cannonlake IPC driver");
