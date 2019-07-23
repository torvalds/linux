// SPDX-License-Identifier: GPL-2.0-only
/*
 * skl-sst.c - HDA DSP library functions for SKL platform
 *
 * Copyright (C) 2014-15, Intel Corporation.
 * Author:Rafal Redzimski <rafal.f.redzimski@intel.com>
 *	Jeeja KP <jeeja.kp@intel.com>
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/uuid.h>
#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "../common/sst-ipc.h"
#include "skl.h"

#define SKL_BASEFW_TIMEOUT	300
#define SKL_INIT_TIMEOUT	1000

/* Intel HD Audio SRAM Window 0*/
#define SKL_ADSP_SRAM0_BASE	0x8000

/* Firmware status window */
#define SKL_ADSP_FW_STATUS	SKL_ADSP_SRAM0_BASE
#define SKL_ADSP_ERROR_CODE	(SKL_ADSP_FW_STATUS + 0x4)

#define SKL_NUM_MODULES		1

static bool skl_check_fw_status(struct sst_dsp *ctx, u32 status)
{
	u32 cur_sts;

	cur_sts = sst_dsp_shim_read(ctx, SKL_ADSP_FW_STATUS) & SKL_FW_STS_MASK;

	return (cur_sts == status);
}

static int skl_transfer_firmware(struct sst_dsp *ctx,
		const void *basefw, u32 base_fw_size)
{
	int ret = 0;

	ret = ctx->cl_dev.ops.cl_copy_to_dmabuf(ctx, basefw, base_fw_size,
								true);
	if (ret < 0)
		return ret;

	ret = sst_dsp_register_poll(ctx,
			SKL_ADSP_FW_STATUS,
			SKL_FW_STS_MASK,
			SKL_FW_RFW_START,
			SKL_BASEFW_TIMEOUT,
			"Firmware boot");

	ctx->cl_dev.ops.cl_stop_dma(ctx);

	return ret;
}

#define SKL_ADSP_FW_BIN_HDR_OFFSET 0x284

static int skl_load_base_firmware(struct sst_dsp *ctx)
{
	int ret = 0, i;
	struct skl_dev *skl = ctx->thread_context;
	struct firmware stripped_fw;
	u32 reg;

	skl->boot_complete = false;
	init_waitqueue_head(&skl->boot_wait);

	if (ctx->fw == NULL) {
		ret = request_firmware(&ctx->fw, ctx->fw_name, ctx->dev);
		if (ret < 0) {
			dev_err(ctx->dev, "Request firmware failed %d\n", ret);
			return -EIO;
		}
	}

	/* prase uuids on first boot */
	if (skl->is_first_boot) {
		ret = snd_skl_parse_uuids(ctx, ctx->fw, SKL_ADSP_FW_BIN_HDR_OFFSET, 0);
		if (ret < 0) {
			dev_err(ctx->dev, "UUID parsing err: %d\n", ret);
			release_firmware(ctx->fw);
			skl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);
			return ret;
		}
	}

	/* check for extended manifest */
	stripped_fw.data = ctx->fw->data;
	stripped_fw.size = ctx->fw->size;

	skl_dsp_strip_extended_manifest(&stripped_fw);

	ret = skl_dsp_boot(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "Boot dsp core failed ret: %d\n", ret);
		goto skl_load_base_firmware_failed;
	}

	ret = skl_cldma_prepare(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "CL dma prepare failed : %d\n", ret);
		goto skl_load_base_firmware_failed;
	}

	/* enable Interrupt */
	skl_ipc_int_enable(ctx);
	skl_ipc_op_int_enable(ctx);

	/* check ROM Status */
	for (i = SKL_INIT_TIMEOUT; i > 0; --i) {
		if (skl_check_fw_status(ctx, SKL_FW_INIT)) {
			dev_dbg(ctx->dev,
				"ROM loaded, we can continue with FW loading\n");
			break;
		}
		mdelay(1);
	}
	if (!i) {
		reg = sst_dsp_shim_read(ctx, SKL_ADSP_FW_STATUS);
		dev_err(ctx->dev,
			"Timeout waiting for ROM init done, reg:0x%x\n", reg);
		ret = -EIO;
		goto transfer_firmware_failed;
	}

	ret = skl_transfer_firmware(ctx, stripped_fw.data, stripped_fw.size);
	if (ret < 0) {
		dev_err(ctx->dev, "Transfer firmware failed%d\n", ret);
		goto transfer_firmware_failed;
	} else {
		ret = wait_event_timeout(skl->boot_wait, skl->boot_complete,
					msecs_to_jiffies(SKL_IPC_BOOT_MSECS));
		if (ret == 0) {
			dev_err(ctx->dev, "DSP boot failed, FW Ready timed-out\n");
			ret = -EIO;
			goto transfer_firmware_failed;
		}

		dev_dbg(ctx->dev, "Download firmware successful%d\n", ret);
		skl->fw_loaded = true;
	}
	return 0;
transfer_firmware_failed:
	ctx->cl_dev.ops.cl_cleanup_controller(ctx);
skl_load_base_firmware_failed:
	skl_dsp_disable_core(ctx, SKL_DSP_CORE0_MASK);
	release_firmware(ctx->fw);
	ctx->fw = NULL;
	return ret;
}

static int skl_set_dsp_D0(struct sst_dsp *ctx, unsigned int core_id)
{
	int ret;
	struct skl_ipc_dxstate_info dx;
	struct skl_dev *skl = ctx->thread_context;
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);

	/* If core0 is being turned on, we need to load the FW */
	if (core_id == SKL_DSP_CORE0_ID) {
		ret = skl_load_base_firmware(ctx);
		if (ret < 0) {
			dev_err(ctx->dev, "unable to load firmware\n");
			return ret;
		}

		/* load libs as they are also lost on D3 */
		if (skl->lib_count > 1) {
			ret = ctx->fw_ops.load_library(ctx, skl->lib_info,
							skl->lib_count);
			if (ret < 0) {
				dev_err(ctx->dev, "reload libs failed: %d\n",
						ret);
				return ret;
			}

		}
	}

	/*
	 * If any core other than core 0 is being moved to D0, enable the
	 * core and send the set dx IPC for the core.
	 */
	if (core_id != SKL_DSP_CORE0_ID) {
		ret = skl_dsp_enable_core(ctx, core_mask);
		if (ret < 0)
			return ret;

		dx.core_mask = core_mask;
		dx.dx_mask = core_mask;

		ret = skl_ipc_set_dx(&skl->ipc, SKL_INSTANCE_ID,
					SKL_BASE_FW_MODULE_ID, &dx);
		if (ret < 0) {
			dev_err(ctx->dev, "Failed to set dsp to D0:core id= %d\n",
					core_id);
			skl_dsp_disable_core(ctx, core_mask);
		}
	}

	skl->cores.state[core_id] = SKL_DSP_RUNNING;

	return 0;
}

static int skl_set_dsp_D3(struct sst_dsp *ctx, unsigned int core_id)
{
	int ret;
	struct skl_ipc_dxstate_info dx;
	struct skl_dev *skl = ctx->thread_context;
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);

	dx.core_mask = core_mask;
	dx.dx_mask = SKL_IPC_D3_MASK;

	ret = skl_ipc_set_dx(&skl->ipc, SKL_INSTANCE_ID, SKL_BASE_FW_MODULE_ID, &dx);
	if (ret < 0)
		dev_err(ctx->dev, "set Dx core %d fail: %d\n", core_id, ret);

	if (core_id == SKL_DSP_CORE0_ID) {
		/* disable Interrupt */
		ctx->cl_dev.ops.cl_cleanup_controller(ctx);
		skl_cldma_int_disable(ctx);
		skl_ipc_op_int_disable(ctx);
		skl_ipc_int_disable(ctx);
	}

	ret = skl_dsp_disable_core(ctx, core_mask);
	if (ret < 0)
		return ret;

	skl->cores.state[core_id] = SKL_DSP_RESET;
	return ret;
}

static unsigned int skl_get_errorcode(struct sst_dsp *ctx)
{
	 return sst_dsp_shim_read(ctx, SKL_ADSP_ERROR_CODE);
}

/*
 * since get/set_module are called from DAPM context,
 * we don't need lock for usage count
 */
static int skl_get_module(struct sst_dsp *ctx, u16 mod_id)
{
	struct skl_module_table *module;

	list_for_each_entry(module, &ctx->module_list, list) {
		if (module->mod_info->mod_id == mod_id)
			return ++module->usage_cnt;
	}

	return -EINVAL;
}

static int skl_put_module(struct sst_dsp *ctx, u16 mod_id)
{
	struct skl_module_table *module;

	list_for_each_entry(module, &ctx->module_list, list) {
		if (module->mod_info->mod_id == mod_id)
			return --module->usage_cnt;
	}

	return -EINVAL;
}

static struct skl_module_table *skl_fill_module_table(struct sst_dsp *ctx,
						char *mod_name, int mod_id)
{
	const struct firmware *fw;
	struct skl_module_table *skl_module;
	unsigned int size;
	int ret;

	ret = request_firmware(&fw, mod_name, ctx->dev);
	if (ret < 0) {
		dev_err(ctx->dev, "Request Module %s failed :%d\n",
							mod_name, ret);
		return NULL;
	}

	skl_module = devm_kzalloc(ctx->dev, sizeof(*skl_module), GFP_KERNEL);
	if (skl_module == NULL) {
		release_firmware(fw);
		return NULL;
	}

	size = sizeof(*skl_module->mod_info);
	skl_module->mod_info = devm_kzalloc(ctx->dev, size, GFP_KERNEL);
	if (skl_module->mod_info == NULL) {
		release_firmware(fw);
		return NULL;
	}

	skl_module->mod_info->mod_id = mod_id;
	skl_module->mod_info->fw = fw;
	list_add(&skl_module->list, &ctx->module_list);

	return skl_module;
}

/* get a module from it's unique ID */
static struct skl_module_table *skl_module_get_from_id(
			struct sst_dsp *ctx, u16 mod_id)
{
	struct skl_module_table *module;

	if (list_empty(&ctx->module_list)) {
		dev_err(ctx->dev, "Module list is empty\n");
		return NULL;
	}

	list_for_each_entry(module, &ctx->module_list, list) {
		if (module->mod_info->mod_id == mod_id)
			return module;
	}

	return NULL;
}

static int skl_transfer_module(struct sst_dsp *ctx, const void *data,
			u32 size, u16 mod_id, u8 table_id, bool is_module)
{
	int ret, bytes_left, curr_pos;
	struct skl_dev *skl = ctx->thread_context;
	skl->mod_load_complete = false;

	bytes_left = ctx->cl_dev.ops.cl_copy_to_dmabuf(ctx, data, size, false);
	if (bytes_left < 0)
		return bytes_left;

	/* check is_module flag to load module or library */
	if (is_module)
		ret = skl_ipc_load_modules(&skl->ipc, SKL_NUM_MODULES, &mod_id);
	else
		ret = skl_sst_ipc_load_library(&skl->ipc, 0, table_id, false);

	if (ret < 0) {
		dev_err(ctx->dev, "Failed to Load %s with err %d\n",
				is_module ? "module" : "lib", ret);
		goto out;
	}

	/*
	 * if bytes_left > 0 then wait for BDL complete interrupt and
	 * copy the next chunk till bytes_left is 0. if bytes_left is
	 * is zero, then wait for load module IPC reply
	 */
	while (bytes_left > 0) {
		curr_pos = size - bytes_left;

		ret = skl_cldma_wait_interruptible(ctx);
		if (ret < 0)
			goto out;

		bytes_left = ctx->cl_dev.ops.cl_copy_to_dmabuf(ctx,
							data + curr_pos,
							bytes_left, false);
	}

	ret = wait_event_timeout(skl->mod_load_wait, skl->mod_load_complete,
				msecs_to_jiffies(SKL_IPC_BOOT_MSECS));
	if (ret == 0 || !skl->mod_load_status) {
		dev_err(ctx->dev, "Module Load failed\n");
		ret = -EIO;
	}

out:
	ctx->cl_dev.ops.cl_stop_dma(ctx);

	return ret;
}

static int
skl_load_library(struct sst_dsp *ctx, struct skl_lib_info *linfo, int lib_count)
{
	struct skl_dev *skl = ctx->thread_context;
	struct firmware stripped_fw;
	int ret, i;

	/* library indices start from 1 to N. 0 represents base FW */
	for (i = 1; i < lib_count; i++) {
		ret = skl_prepare_lib_load(skl, &skl->lib_info[i], &stripped_fw,
					SKL_ADSP_FW_BIN_HDR_OFFSET, i);
		if (ret < 0)
			goto load_library_failed;
		ret = skl_transfer_module(ctx, stripped_fw.data,
				stripped_fw.size, 0, i, false);
		if (ret < 0)
			goto load_library_failed;
	}

	return 0;

load_library_failed:
	skl_release_library(linfo, lib_count);
	return ret;
}

static int skl_load_module(struct sst_dsp *ctx, u16 mod_id, u8 *guid)
{
	struct skl_module_table *module_entry = NULL;
	int ret = 0;
	char mod_name[64]; /* guid str = 32 chars + 4 hyphens */

	snprintf(mod_name, sizeof(mod_name), "%s%pUL%s",
					     "intel/dsp_fw_", guid, ".bin");

	module_entry = skl_module_get_from_id(ctx, mod_id);
	if (module_entry == NULL) {
		module_entry = skl_fill_module_table(ctx, mod_name, mod_id);
		if (module_entry == NULL) {
			dev_err(ctx->dev, "Failed to Load module\n");
			return -EINVAL;
		}
	}

	if (!module_entry->usage_cnt) {
		ret = skl_transfer_module(ctx, module_entry->mod_info->fw->data,
				module_entry->mod_info->fw->size,
				mod_id, 0, true);
		if (ret < 0) {
			dev_err(ctx->dev, "Failed to Load module\n");
			return ret;
		}
	}

	ret = skl_get_module(ctx, mod_id);

	return ret;
}

static int skl_unload_module(struct sst_dsp *ctx, u16 mod_id)
{
	int usage_cnt;
	struct skl_dev *skl = ctx->thread_context;
	int ret = 0;

	usage_cnt = skl_put_module(ctx, mod_id);
	if (usage_cnt < 0) {
		dev_err(ctx->dev, "Module bad usage cnt!:%d\n", usage_cnt);
		return -EIO;
	}

	/* if module is used by others return, no need to unload */
	if (usage_cnt > 0)
		return 0;

	ret = skl_ipc_unload_modules(&skl->ipc,
			SKL_NUM_MODULES, &mod_id);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to UnLoad module\n");
		skl_get_module(ctx, mod_id);
		return ret;
	}

	return ret;
}

void skl_clear_module_cnt(struct sst_dsp *ctx)
{
	struct skl_module_table *module;

	if (list_empty(&ctx->module_list))
		return;

	list_for_each_entry(module, &ctx->module_list, list) {
		module->usage_cnt = 0;
	}
}
EXPORT_SYMBOL_GPL(skl_clear_module_cnt);

static void skl_clear_module_table(struct sst_dsp *ctx)
{
	struct skl_module_table *module, *tmp;

	if (list_empty(&ctx->module_list))
		return;

	list_for_each_entry_safe(module, tmp, &ctx->module_list, list) {
		list_del(&module->list);
		release_firmware(module->mod_info->fw);
	}
}

static const struct skl_dsp_fw_ops skl_fw_ops = {
	.set_state_D0 = skl_set_dsp_D0,
	.set_state_D3 = skl_set_dsp_D3,
	.load_fw = skl_load_base_firmware,
	.get_fw_errcode = skl_get_errorcode,
	.load_library = skl_load_library,
	.load_mod = skl_load_module,
	.unload_mod = skl_unload_module,
};

static struct sst_ops skl_ops = {
	.irq_handler = skl_dsp_sst_interrupt,
	.write = sst_shim32_write,
	.read = sst_shim32_read,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.free = skl_dsp_free,
};

static struct sst_dsp_device skl_dev = {
	.thread = skl_dsp_irq_thread_handler,
	.ops = &skl_ops,
};

int skl_sst_dsp_init(struct device *dev, void __iomem *mmio_base, int irq,
		const char *fw_name, struct skl_dsp_loader_ops dsp_ops,
		struct skl_dev **dsp)
{
	struct skl_dev *skl;
	struct sst_dsp *sst;
	int ret;

	ret = skl_sst_ctx_init(dev, irq, fw_name, dsp_ops, dsp, &skl_dev);
	if (ret < 0) {
		dev_err(dev, "%s: no device\n", __func__);
		return ret;
	}

	skl = *dsp;
	sst = skl->dsp;
	sst->addr.lpe = mmio_base;
	sst->addr.shim = mmio_base;
	sst->addr.sram0_base = SKL_ADSP_SRAM0_BASE;
	sst->addr.sram1_base = SKL_ADSP_SRAM1_BASE;
	sst->addr.w0_stat_sz = SKL_ADSP_W0_STAT_SZ;
	sst->addr.w0_up_sz = SKL_ADSP_W0_UP_SZ;

	sst_dsp_mailbox_init(sst, (SKL_ADSP_SRAM0_BASE + SKL_ADSP_W0_STAT_SZ),
			SKL_ADSP_W0_UP_SZ, SKL_ADSP_SRAM1_BASE, SKL_ADSP_W1_SZ);

	ret = skl_ipc_init(dev, skl);
	if (ret) {
		skl_dsp_free(sst);
		return ret;
	}

	sst->fw_ops = skl_fw_ops;

	return skl_dsp_acquire_irq(sst);
}
EXPORT_SYMBOL_GPL(skl_sst_dsp_init);

int skl_sst_init_fw(struct device *dev, struct skl_dev *skl)
{
	int ret;
	struct sst_dsp *sst = skl->dsp;

	ret = sst->fw_ops.load_fw(sst);
	if (ret < 0) {
		dev_err(dev, "Load base fw failed : %d\n", ret);
		return ret;
	}

	skl_dsp_init_core_state(sst);

	if (skl->lib_count > 1) {
		ret = sst->fw_ops.load_library(sst, skl->lib_info,
						skl->lib_count);
		if (ret < 0) {
			dev_err(dev, "Load Library failed : %x\n", ret);
			return ret;
		}
	}
	skl->is_first_boot = false;

	return 0;
}
EXPORT_SYMBOL_GPL(skl_sst_init_fw);

void skl_sst_dsp_cleanup(struct device *dev, struct skl_dev *skl)
{

	if (skl->dsp->fw)
		release_firmware(skl->dsp->fw);
	skl_clear_module_table(skl->dsp);
	skl_freeup_uuid_list(skl);
	skl_ipc_free(&skl->ipc);
	skl->dsp->ops->free(skl->dsp);
	if (skl->boot_complete) {
		skl->dsp->cl_dev.ops.cl_cleanup_controller(skl->dsp);
		skl_cldma_int_disable(skl->dsp);
	}
}
EXPORT_SYMBOL_GPL(skl_sst_dsp_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Skylake IPC driver");
