/*
 *  sst_dsp.c - Intel SST Driver for audio engine
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
 *
 *  This file contains all dsp controlling functions like firmware download,
 * setting/resetting dsp cores, etc
 */
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/dmaengine.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/compress_driver.h>
#include <asm/platform_sst_audio.h>
#include "../sst-mfld-platform.h"
#include "sst.h"
#include "../../common/sst-dsp.h"

void memcpy32_toio(void __iomem *dst, const void *src, int count)
{
	/* __iowrite32_copy uses 32-bit count values so divide by 4 for
	 * right count in words
	 */
	__iowrite32_copy(dst, src, count/4);
}

void memcpy32_fromio(void *dst, const void __iomem *src, int count)
{
	/* __iowrite32_copy uses 32-bit count values so divide by 4 for
	 * right count in words
	 */
	__iowrite32_copy(dst, src, count/4);
}

/**
 * intel_sst_reset_dsp_mrfld - Resetting SST DSP
 *
 * This resets DSP in case of MRFLD platfroms
 */
int intel_sst_reset_dsp_mrfld(struct intel_sst_drv *sst_drv_ctx)
{
	union config_status_reg_mrfld csr;

	dev_dbg(sst_drv_ctx->dev, "sst: Resetting the DSP in mrfld\n");
	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);

	dev_dbg(sst_drv_ctx->dev, "value:0x%llx\n", csr.full);

	csr.full |= 0x7;
	sst_shim_write64(sst_drv_ctx->shim, SST_CSR, csr.full);
	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);

	dev_dbg(sst_drv_ctx->dev, "value:0x%llx\n", csr.full);

	csr.full &= ~(0x1);
	sst_shim_write64(sst_drv_ctx->shim, SST_CSR, csr.full);

	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);
	dev_dbg(sst_drv_ctx->dev, "value:0x%llx\n", csr.full);
	return 0;
}

/**
 * sst_start_merrifield - Start the SST DSP processor
 *
 * This starts the DSP in MERRIFIELD platfroms
 */
int sst_start_mrfld(struct intel_sst_drv *sst_drv_ctx)
{
	union config_status_reg_mrfld csr;

	dev_dbg(sst_drv_ctx->dev, "sst: Starting the DSP in mrfld LALALALA\n");
	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);
	dev_dbg(sst_drv_ctx->dev, "value:0x%llx\n", csr.full);

	csr.full |= 0x7;
	sst_shim_write64(sst_drv_ctx->shim, SST_CSR, csr.full);

	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);
	dev_dbg(sst_drv_ctx->dev, "value:0x%llx\n", csr.full);

	csr.part.xt_snoop = 1;
	csr.full &= ~(0x5);
	sst_shim_write64(sst_drv_ctx->shim, SST_CSR, csr.full);

	csr.full = sst_shim_read64(sst_drv_ctx->shim, SST_CSR);
	dev_dbg(sst_drv_ctx->dev, "sst: Starting the DSP_merrifield:%llx\n",
			csr.full);
	return 0;
}

static int sst_validate_fw_image(struct intel_sst_drv *ctx, unsigned long size,
		struct fw_module_header **module, u32 *num_modules)
{
	struct sst_fw_header *header;
	const void *sst_fw_in_mem = ctx->fw_in_mem;

	dev_dbg(ctx->dev, "Enter\n");

	/* Read the header information from the data pointer */
	header = (struct sst_fw_header *)sst_fw_in_mem;
	dev_dbg(ctx->dev,
		"header sign=%s size=%x modules=%x fmt=%x size=%zx\n",
		header->signature, header->file_size, header->modules,
		header->file_format, sizeof(*header));

	/* verify FW */
	if ((strncmp(header->signature, SST_FW_SIGN, 4) != 0) ||
		(size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		dev_err(ctx->dev, "InvalidFW sign/filesize mismatch\n");
		return -EINVAL;
	}
	*num_modules = header->modules;
	*module = (void *)sst_fw_in_mem + sizeof(*header);

	return 0;
}

/*
 * sst_fill_memcpy_list - Fill the memcpy list
 *
 * @memcpy_list: List to be filled
 * @destn: Destination addr to be filled in the list
 * @src: Source addr to be filled in the list
 * @size: Size to be filled in the list
 *
 * Adds the node to the list after required fields
 * are populated in the node
 */
static int sst_fill_memcpy_list(struct list_head *memcpy_list,
			void *destn, const void *src, u32 size, bool is_io)
{
	struct sst_memcpy_list *listnode;

	listnode = kzalloc(sizeof(*listnode), GFP_KERNEL);
	if (listnode == NULL)
		return -ENOMEM;
	listnode->dstn = destn;
	listnode->src = src;
	listnode->size = size;
	listnode->is_io = is_io;
	list_add_tail(&listnode->memcpylist, memcpy_list);

	return 0;
}

/**
 * sst_parse_module_memcpy - Parse audio FW modules and populate the memcpy list
 *
 * @sst_drv_ctx		: driver context
 * @module		: FW module header
 * @memcpy_list	: Pointer to the list to be populated
 * Create the memcpy list as the number of block to be copied
 * returns error or 0 if module sizes are proper
 */
static int sst_parse_module_memcpy(struct intel_sst_drv *sst_drv_ctx,
		struct fw_module_header *module, struct list_head *memcpy_list)
{
	struct fw_block_info *block;
	u32 count;
	int ret_val = 0;
	void __iomem *ram_iomem;

	dev_dbg(sst_drv_ctx->dev, "module sign %s size %x blocks %x type %x\n",
			module->signature, module->mod_size,
			module->blocks, module->type);
	dev_dbg(sst_drv_ctx->dev, "module entrypoint 0x%x\n", module->entry_point);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {
		if (block->size <= 0) {
			dev_err(sst_drv_ctx->dev, "block size invalid\n");
			return -EINVAL;
		}
		switch (block->type) {
		case SST_IRAM:
			ram_iomem = sst_drv_ctx->iram;
			break;
		case SST_DRAM:
			ram_iomem = sst_drv_ctx->dram;
			break;
		case SST_DDR:
			ram_iomem = sst_drv_ctx->ddr;
			break;
		case SST_CUSTOM_INFO:
			block = (void *)block + sizeof(*block) + block->size;
			continue;
		default:
			dev_err(sst_drv_ctx->dev, "wrong ram type0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}

		ret_val = sst_fill_memcpy_list(memcpy_list,
				ram_iomem + block->ram_offset,
				(void *)block + sizeof(*block), block->size, 1);
		if (ret_val)
			return ret_val;

		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

/**
 * sst_parse_fw_memcpy - parse the firmware image & populate the list for memcpy
 *
 * @ctx			: pointer to drv context
 * @size		: size of the firmware
 * @fw_list		: pointer to list_head to be populated
 * This function parses the FW image and saves the parsed image in the list
 * for memcpy
 */
static int sst_parse_fw_memcpy(struct intel_sst_drv *ctx, unsigned long size,
				struct list_head *fw_list)
{
	struct fw_module_header *module;
	u32 count, num_modules;
	int ret_val;

	ret_val = sst_validate_fw_image(ctx, size, &module, &num_modules);
	if (ret_val)
		return ret_val;

	for (count = 0; count < num_modules; count++) {
		ret_val = sst_parse_module_memcpy(ctx, module, fw_list);
		if (ret_val)
			return ret_val;
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	return 0;
}

/**
 * sst_do_memcpy - function initiates the memcpy
 *
 * @memcpy_list: Pter to memcpy list on which the memcpy needs to be initiated
 *
 * Triggers the memcpy
 */
static void sst_do_memcpy(struct list_head *memcpy_list)
{
	struct sst_memcpy_list *listnode;

	list_for_each_entry(listnode, memcpy_list, memcpylist) {
		if (listnode->is_io == true)
			memcpy32_toio((void __iomem *)listnode->dstn,
					listnode->src, listnode->size);
		else
			memcpy(listnode->dstn, listnode->src, listnode->size);
	}
}

void sst_memcpy_free_resources(struct intel_sst_drv *sst_drv_ctx)
{
	struct sst_memcpy_list *listnode, *tmplistnode;

	/* Free the list */
	if (!list_empty(&sst_drv_ctx->memcpy_list)) {
		list_for_each_entry_safe(listnode, tmplistnode,
				&sst_drv_ctx->memcpy_list, memcpylist) {
			list_del(&listnode->memcpylist);
			kfree(listnode);
		}
	}
}

static int sst_cache_and_parse_fw(struct intel_sst_drv *sst,
		const struct firmware *fw)
{
	int retval = 0;

	sst->fw_in_mem = kzalloc(fw->size, GFP_KERNEL);
	if (!sst->fw_in_mem) {
		retval = -ENOMEM;
		goto end_release;
	}
	dev_dbg(sst->dev, "copied fw to %p", sst->fw_in_mem);
	dev_dbg(sst->dev, "phys: %lx", (unsigned long)virt_to_phys(sst->fw_in_mem));
	memcpy(sst->fw_in_mem, fw->data, fw->size);
	retval = sst_parse_fw_memcpy(sst, fw->size, &sst->memcpy_list);
	if (retval) {
		dev_err(sst->dev, "Failed to parse fw\n");
		kfree(sst->fw_in_mem);
		sst->fw_in_mem = NULL;
	}

end_release:
	release_firmware(fw);
	return retval;

}

void sst_firmware_load_cb(const struct firmware *fw, void *context)
{
	struct intel_sst_drv *ctx = context;

	dev_dbg(ctx->dev, "Enter\n");

	if (fw == NULL) {
		dev_err(ctx->dev, "request fw failed\n");
		return;
	}

	mutex_lock(&ctx->sst_lock);

	if (ctx->sst_state != SST_RESET ||
			ctx->fw_in_mem != NULL) {
		release_firmware(fw);
		mutex_unlock(&ctx->sst_lock);
		return;
	}

	dev_dbg(ctx->dev, "Request Fw completed\n");
	sst_cache_and_parse_fw(ctx, fw);
	mutex_unlock(&ctx->sst_lock);
}

/*
 * sst_request_fw - requests audio fw from kernel and saves a copy
 *
 * This function requests the SST FW from the kernel, parses it and
 * saves a copy in the driver context
 */
static int sst_request_fw(struct intel_sst_drv *sst)
{
	int retval = 0;
	const struct firmware *fw;

	retval = request_firmware(&fw, sst->firmware_name, sst->dev);
	if (fw == NULL) {
		dev_err(sst->dev, "fw is returning as null\n");
		return -EINVAL;
	}
	if (retval) {
		dev_err(sst->dev, "request fw failed %d\n", retval);
		return retval;
	}
	mutex_lock(&sst->sst_lock);
	retval = sst_cache_and_parse_fw(sst, fw);
	mutex_unlock(&sst->sst_lock);

	return retval;
}

/*
 * Writing the DDR physical base to DCCM offset
 * so that FW can use it to setup TLB
 */
static void sst_dccm_config_write(void __iomem *dram_base,
		unsigned int ddr_base)
{
	void __iomem *addr;
	u32 bss_reset = 0;

	addr = (void __iomem *)(dram_base + MRFLD_FW_DDR_BASE_OFFSET);
	memcpy32_toio(addr, (void *)&ddr_base, sizeof(u32));
	bss_reset |= (1 << MRFLD_FW_BSS_RESET_BIT);
	addr = (void __iomem *)(dram_base + MRFLD_FW_FEATURE_BASE_OFFSET);
	memcpy32_toio(addr, &bss_reset, sizeof(u32));

}

void sst_post_download_mrfld(struct intel_sst_drv *ctx)
{
	sst_dccm_config_write(ctx->dram, ctx->ddr_base);
	dev_dbg(ctx->dev, "config written to DCCM\n");
}

/**
 * sst_load_fw - function to load FW into DSP
 * Transfers the FW to DSP using dma/memcpy
 */
int sst_load_fw(struct intel_sst_drv *sst_drv_ctx)
{
	int ret_val = 0;
	struct sst_block *block;

	dev_dbg(sst_drv_ctx->dev, "sst_load_fw\n");

	if (sst_drv_ctx->sst_state !=  SST_RESET ||
			sst_drv_ctx->sst_state == SST_SHUTDOWN)
		return -EAGAIN;

	if (!sst_drv_ctx->fw_in_mem) {
		dev_dbg(sst_drv_ctx->dev, "sst: FW not in memory retry to download\n");
		ret_val = sst_request_fw(sst_drv_ctx);
		if (ret_val)
			return ret_val;
	}

	BUG_ON(!sst_drv_ctx->fw_in_mem);
	block = sst_create_block(sst_drv_ctx, 0, FW_DWNL_ID);
	if (block == NULL)
		return -ENOMEM;

	/* Prevent C-states beyond C6 */
	pm_qos_update_request(sst_drv_ctx->qos, 0);

	sst_drv_ctx->sst_state = SST_FW_LOADING;

	ret_val = sst_drv_ctx->ops->reset(sst_drv_ctx);
	if (ret_val)
		goto restore;

	sst_do_memcpy(&sst_drv_ctx->memcpy_list);

	/* Write the DRAM/DCCM config before enabling FW */
	if (sst_drv_ctx->ops->post_download)
		sst_drv_ctx->ops->post_download(sst_drv_ctx);

	/* bring sst out of reset */
	ret_val = sst_drv_ctx->ops->start(sst_drv_ctx);
	if (ret_val)
		goto restore;

	ret_val = sst_wait_timeout(sst_drv_ctx, block);
	if (ret_val) {
		dev_err(sst_drv_ctx->dev, "fw download failed %d\n" , ret_val);
		/* FW download failed due to timeout */
		ret_val = -EBUSY;

	}


restore:
	/* Re-enable Deeper C-states beyond C6 */
	pm_qos_update_request(sst_drv_ctx->qos, PM_QOS_DEFAULT_VALUE);
	sst_free_block(sst_drv_ctx, block);
	dev_dbg(sst_drv_ctx->dev, "fw load successful!!!\n");

	if (sst_drv_ctx->ops->restore_dsp_context)
		sst_drv_ctx->ops->restore_dsp_context();
	sst_drv_ctx->sst_state = SST_FW_RUNNING;
	return ret_val;
}

