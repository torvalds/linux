/*
 * Intel Baytrail SST DSP driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "sst-baytrail-ipc.h"

#define SST_BYT_FW_SIGNATURE_SIZE	4
#define SST_BYT_FW_SIGN			"$SST"

#define SST_BYT_IRAM_OFFSET	0xC0000
#define SST_BYT_DRAM_OFFSET	0x100000
#define SST_BYT_SHIM_OFFSET	0x140000

enum sst_ram_type {
	SST_BYT_IRAM	= 1,
	SST_BYT_DRAM	= 2,
	SST_BYT_CACHE	= 3,
};

struct dma_block_info {
	enum sst_ram_type	type;	/* IRAM/DRAM */
	u32			size;	/* Bytes */
	u32			ram_offset; /* Offset in I/DRAM */
	u32			rsvd;	/* Reserved field */
};

struct fw_header {
	unsigned char signature[SST_BYT_FW_SIGNATURE_SIZE];
	u32 file_size; /* size of fw minus this header */
	u32 modules; /*  # of modules */
	u32 file_format; /* version of header format */
	u32 reserved[4];
};

struct sst_byt_fw_module_header {
	unsigned char signature[SST_BYT_FW_SIGNATURE_SIZE];
	u32 mod_size; /* size of module */
	u32 blocks; /* # of blocks */
	u32 type; /* codec type, pp lib */
	u32 entry_point;
};

static int sst_byt_parse_module(struct sst_dsp *dsp, struct sst_fw *fw,
				struct sst_byt_fw_module_header *module)
{
	struct dma_block_info *block;
	struct sst_module *mod;
	struct sst_module_data block_data;
	struct sst_module_template template;
	int count;

	memset(&template, 0, sizeof(template));
	template.id = module->type;
	template.entry = module->entry_point;
	template.p.type = SST_MEM_DRAM;
	template.p.data_type = SST_DATA_P;
	template.s.type = SST_MEM_DRAM;
	template.s.data_type = SST_DATA_S;

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(dsp->dev, "block %d size invalid\n", count);
			return -EINVAL;
		}

		switch (block->type) {
		case SST_BYT_IRAM:
			block_data.offset = block->ram_offset +
					    dsp->addr.iram_offset;
			block_data.type = SST_MEM_IRAM;
			break;
		case SST_BYT_DRAM:
			block_data.offset = block->ram_offset +
					    dsp->addr.dram_offset;
			block_data.type = SST_MEM_DRAM;
			break;
		case SST_BYT_CACHE:
			block_data.offset = block->ram_offset +
					    (dsp->addr.fw_ext - dsp->addr.lpe);
			block_data.type = SST_MEM_CACHE;
			break;
		default:
			dev_err(dsp->dev, "wrong ram type 0x%x in block0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		block_data.size = block->size;
		block_data.data_type = SST_DATA_M;
		block_data.data = (void *)block + sizeof(*block);

		sst_module_insert_fixed_block(mod, &block_data);

		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

static int sst_byt_parse_fw_image(struct sst_fw *sst_fw)
{
	struct fw_header *header;
	struct sst_byt_fw_module_header *module;
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret, count;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->dma_buf;

	/* verify FW */
	if ((strncmp(header->signature, SST_BYT_FW_SIGN, 4) != 0) ||
	    (sst_fw->size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		dev_err(dsp->dev, "Invalid FW sign/filesize mismatch\n");
		return -EINVAL;
	}

	dev_dbg(dsp->dev,
		"header sign=%4s size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
		header->signature, header->file_size, header->modules,
		header->file_format, sizeof(*header));

	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret = sst_byt_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	return 0;
}

static void sst_byt_dump_shim(struct sst_dsp *sst)
{
	int i;
	u64 reg;

	for (i = 0; i <= 0xF0; i += 8) {
		reg = sst_dsp_shim_read64_unlocked(sst, i);
		if (reg)
			dev_dbg(sst->dev, "shim 0x%2.2x value 0x%16.16llx\n",
				i, reg);
	}

	for (i = 0x00; i <= 0xff; i += 4) {
		reg = readl(sst->addr.pci_cfg + i);
		if (reg)
			dev_dbg(sst->dev, "pci 0x%2.2x value 0x%8.8x\n",
				i, (u32)reg);
	}
}

static irqreturn_t sst_byt_irq(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	u64 isrx;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&sst->spinlock);

	isrx = sst_dsp_shim_read64_unlocked(sst, SST_ISRX);
	if (isrx & SST_ISRX_DONE) {
		/* ADSP has processed the message request from IA */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IPCX,
						    SST_BYT_IPCX_DONE, 0);
		ret = IRQ_WAKE_THREAD;
	}
	if (isrx & SST_BYT_ISRX_REQUEST) {
		/* mask message request from ADSP and do processing later */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
						    SST_BYT_IMRX_REQUEST,
						    SST_BYT_IMRX_REQUEST);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&sst->spinlock);

	return ret;
}

static void sst_byt_boot(struct sst_dsp *sst)
{
	int tries = 10;

	/* release stall and wait to unstall */
	sst_dsp_shim_update_bits64(sst, SST_CSR, SST_BYT_CSR_STALL, 0x0);
	while (tries--) {
		if (!(sst_dsp_shim_read64(sst, SST_CSR) &
		      SST_BYT_CSR_PWAITMODE))
			break;
		msleep(100);
	}
	if (tries < 0) {
		dev_err(sst->dev, "unable to start DSP\n");
		sst_byt_dump_shim(sst);
	}
}

static void sst_byt_reset(struct sst_dsp *sst)
{
	/* put DSP into reset, set reset vector and stall */
	sst_dsp_shim_update_bits64(sst, SST_CSR,
		SST_BYT_CSR_RST | SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL,
		SST_BYT_CSR_RST | SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL);

	udelay(10);

	/* take DSP out of reset and keep stalled for FW loading */
	sst_dsp_shim_update_bits64(sst, SST_CSR, SST_BYT_CSR_RST, 0);
}

struct sst_adsp_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

/* BYT test stuff */
static const struct sst_adsp_memregion byt_region[] = {
	{0xC0000, 0x100000, 8, SST_MEM_IRAM}, /* I-SRAM - 8 * 32kB */
	{0x100000, 0x140000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
};

static int sst_byt_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	sst->addr.lpe_base = pdata->lpe_base;
	sst->addr.lpe = ioremap(pdata->lpe_base, pdata->lpe_size);
	if (!sst->addr.lpe)
		return -ENODEV;

	/* ADSP PCI MMIO config space */
	sst->addr.pci_cfg = ioremap(pdata->pcicfg_base, pdata->pcicfg_size);
	if (!sst->addr.pci_cfg) {
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}

	/* SST Extended FW allocation */
	sst->addr.fw_ext = ioremap(pdata->fw_base, pdata->fw_size);
	if (!sst->addr.fw_ext) {
		iounmap(sst->addr.pci_cfg);
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}

	/* SST Shim */
	sst->addr.shim = sst->addr.lpe + sst->addr.shim_offset;

	sst_dsp_mailbox_init(sst, SST_BYT_MAILBOX_OFFSET + 0x204,
			     SST_BYT_IPC_MAX_PAYLOAD_SIZE,
			     SST_BYT_MAILBOX_OFFSET,
			     SST_BYT_IPC_MAX_PAYLOAD_SIZE);

	sst->irq = pdata->irq;

	return 0;
}

static int sst_byt_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	const struct sst_adsp_memregion *region;
	struct device *dev;
	int ret = -ENODEV, i, j, region_count;
	u32 offset, size;

	dev = sst->dev;

	switch (sst->id) {
	case SST_DEV_ID_BYT:
		region = byt_region;
		region_count = ARRAY_SIZE(byt_region);
		sst->addr.iram_offset = SST_BYT_IRAM_OFFSET;
		sst->addr.dram_offset = SST_BYT_DRAM_OFFSET;
		sst->addr.shim_offset = SST_BYT_SHIM_OFFSET;
		break;
	default:
		dev_err(dev, "failed to get mem resources\n");
		return ret;
	}

	ret = sst_byt_resource_map(sst, pdata);
	if (ret < 0) {
		dev_err(dev, "failed to map resources\n");
		return ret;
	}

	/*
	 * save the physical address of extended firmware block in the first
	 * 4 bytes of the mailbox
	 */
	memcpy_toio(sst->addr.lpe + SST_BYT_MAILBOX_OFFSET,
	       &pdata->fw_base, sizeof(u32));

	ret = dma_coerce_mask_and_coherent(sst->dma_dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* enable Interrupt from both sides */
	sst_dsp_shim_update_bits64(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits64(sst, SST_IMRD, 0x3, 0x0);

	/* register DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < region_count; i++) {
		offset = region[i].start;
		size = (region[i].end - region[i].start) / region[i].blocks;

		/* register individual memory blocks */
		for (j = 0; j < region[i].blocks; j++) {
			sst_mem_block_register(sst, offset, size,
					       region[i].type, NULL, j, sst);
			offset += size;
		}
	}

	return 0;
}

static void sst_byt_free(struct sst_dsp *sst)
{
	sst_mem_block_unregister_all(sst);
	iounmap(sst->addr.lpe);
	iounmap(sst->addr.pci_cfg);
	iounmap(sst->addr.fw_ext);
}

struct sst_ops sst_byt_ops = {
	.reset = sst_byt_reset,
	.boot = sst_byt_boot,
	.write = sst_shim32_write,
	.read = sst_shim32_read,
	.write64 = sst_shim32_write64,
	.read64 = sst_shim32_read64,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.irq_handler = sst_byt_irq,
	.init = sst_byt_init,
	.free = sst_byt_free,
	.parse_fw = sst_byt_parse_fw_image,
};
