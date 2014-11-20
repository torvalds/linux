/*
 * Intel Haswell SST DSP driver
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "sst-haswell-ipc.h"

#include <trace/events/hswadsp.h>

#define SST_HSW_FW_SIGNATURE_SIZE	4
#define SST_HSW_FW_SIGN			"$SST"
#define SST_HSW_FW_LIB_SIGN		"$LIB"

#define SST_WPT_SHIM_OFFSET	0xFB000
#define SST_LP_SHIM_OFFSET	0xE7000
#define SST_WPT_IRAM_OFFSET	0xA0000
#define SST_LP_IRAM_OFFSET	0x80000

#define SST_SHIM_PM_REG		0x84

#define SST_HSW_IRAM	1
#define SST_HSW_DRAM	2
#define SST_HSW_REGS	3

struct dma_block_info {
	__le32 type;		/* IRAM/DRAM */
	__le32 size;		/* Bytes */
	__le32 ram_offset;	/* Offset in I/DRAM */
	__le32 rsvd;		/* Reserved field */
} __attribute__((packed));

struct fw_module_info {
	__le32 persistent_size;
	__le32 scratch_size;
} __attribute__((packed));

struct fw_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* FW signature */
	__le32 file_size;		/* size of fw minus this header */
	__le32 modules;		/*  # of modules */
	__le32 file_format;	/* version of header format */
	__le32 reserved[4];
} __attribute__((packed));

struct fw_module_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* module signature */
	__le32 mod_size;	/* size of module */
	__le32 blocks;	/* # of blocks */
	__le16 padding;
	__le16 type;	/* codec type, pp lib */
	__le32 entry_point;
	struct fw_module_info info;
} __attribute__((packed));

static void hsw_free(struct sst_dsp *sst);

static int hsw_parse_module(struct sst_dsp *dsp, struct sst_fw *fw,
	struct fw_module_header *module)
{
	struct dma_block_info *block;
	struct sst_module *mod;
	struct sst_module_data block_data;
	struct sst_module_template template;
	int count;
	void __iomem *ram;

	/* TODO: allowed module types need to be configurable */
	if (module->type != SST_HSW_MODULE_BASE_FW
		&& module->type != SST_HSW_MODULE_PCM_SYSTEM
		&& module->type != SST_HSW_MODULE_PCM
		&& module->type != SST_HSW_MODULE_PCM_REFERENCE
		&& module->type != SST_HSW_MODULE_PCM_CAPTURE
		&& module->type != SST_HSW_MODULE_LPAL)
		return 0;

	dev_dbg(dsp->dev, "new module sign 0x%s size 0x%x blocks 0x%x type 0x%x\n",
		module->signature, module->mod_size,
		module->blocks, module->type);
	dev_dbg(dsp->dev, " entrypoint 0x%x\n", module->entry_point);
	dev_dbg(dsp->dev, " persistent 0x%x scratch 0x%x\n",
		module->info.persistent_size, module->info.scratch_size);

	memset(&template, 0, sizeof(template));
	template.id = module->type;
	template.entry = module->entry_point;
	template.p.size = module->info.persistent_size;
	template.p.type = SST_MEM_DRAM;
	template.p.data_type = SST_DATA_P;
	template.s.size = module->info.scratch_size;
	template.s.type = SST_MEM_DRAM;
	template.s.data_type = SST_DATA_S;

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(dsp->dev,
				"error: block %d size invalid\n", count);
			sst_module_free(mod);
			return -EINVAL;
		}

		switch (block->type) {
		case SST_HSW_IRAM:
			ram = dsp->addr.lpe;
			block_data.offset =
				block->ram_offset + dsp->addr.iram_offset;
			block_data.type = SST_MEM_IRAM;
			break;
		case SST_HSW_DRAM:
			ram = dsp->addr.lpe;
			block_data.offset = block->ram_offset;
			block_data.type = SST_MEM_DRAM;
			break;
		default:
			dev_err(dsp->dev, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			sst_module_free(mod);
			return -EINVAL;
		}

		block_data.size = block->size;
		block_data.data_type = SST_DATA_M;
		block_data.data = (void *)block + sizeof(*block);
		block_data.data_offset = block_data.data - fw->dma_buf;

		dev_dbg(dsp->dev, "copy firmware block %d type 0x%x "
			"size 0x%x ==> ram %p offset 0x%x\n",
			count, block->type, block->size, ram,
			block->ram_offset);

		sst_module_insert_fixed_block(mod, &block_data);

		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

static int hsw_parse_fw_image(struct sst_fw *sst_fw)
{
	struct fw_header *header;
	struct sst_module *scratch;
	struct fw_module_header *module;
	struct sst_dsp *dsp = sst_fw->dsp;
	struct sst_hsw *hsw = sst_fw->private;
	int ret, count;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->dma_buf;

	/* verify FW */
	if ((strncmp(header->signature, SST_HSW_FW_SIGN, 4) != 0) ||
		(sst_fw->size != header->file_size + sizeof(*header))) {
		dev_err(dsp->dev, "error: invalid fw sign/filesize mismatch\n");
		return -EINVAL;
	}

	dev_dbg(dsp->dev, "header size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
		header->file_size, header->modules,
		header->file_format, sizeof(*header));

	/* parse each module */
	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {

		/* module */
		ret = hsw_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	/* allocate persistent/scratch mem regions */
	scratch = sst_mem_block_alloc_scratch(dsp);
	if (scratch == NULL)
		return -ENOMEM;

	sst_hsw_set_scratch_module(hsw, scratch);

	return 0;
}

static irqreturn_t hsw_irq(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	u32 isr;
	int ret = IRQ_NONE;

	spin_lock(&sst->spinlock);

	/* Interrupt arrived, check src */
	isr = sst_dsp_shim_read_unlocked(sst, SST_ISRX);
	if (isr & SST_ISRX_DONE) {
		trace_sst_irq_done(isr,
			sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Done interrupt before return */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, SST_IMRX_DONE);
		ret = IRQ_WAKE_THREAD;
	}

	if (isr & SST_ISRX_BUSY) {
		trace_sst_irq_busy(isr,
			sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Busy interrupt before return */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, SST_IMRX_BUSY);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&sst->spinlock);
	return ret;
}

static void hsw_boot(struct sst_dsp *sst)
{
	/* select SSP1 19.2MHz base clock, SSP clock 0, turn off Low Power Clock */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CSR,
		SST_CSR_S1IOCS | SST_CSR_SBCS1 | SST_CSR_LPCS, 0x0);

	/* stall DSP core, set clk to 192/96Mhz */
	sst_dsp_shim_update_bits_unlocked(sst,
		SST_CSR, SST_CSR_STALL | SST_CSR_DCS_MASK,
		SST_CSR_STALL | SST_CSR_DCS(4));

	/* Set 24MHz MCLK, prevent local clock gating, enable SSP0 clock */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CLKCTL,
		SST_CLKCTL_MASK | SST_CLKCTL_DCPLCG | SST_CLKCTL_SCOE0,
		SST_CLKCTL_MASK | SST_CLKCTL_DCPLCG | SST_CLKCTL_SCOE0);

	/* disable DMA finish function for SSP0 & SSP1 */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CSR2, SST_CSR2_SDFD_SSP1,
		SST_CSR2_SDFD_SSP1);

	/* enable DMA engine 0,1 all channels to access host memory */
	sst_dsp_shim_update_bits_unlocked(sst, SST_HMDC,
		SST_HMDC_HDDA1(0xff) | SST_HMDC_HDDA0(0xff),
		SST_HMDC_HDDA1(0xff) | SST_HMDC_HDDA0(0xff));

	/* disable all clock gating */
	writel(0x0, sst->addr.pci_cfg + SST_VDRTCTL2);

	/* set DSP to RUN */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CSR, SST_CSR_STALL, 0x0);
}

static void hsw_reset(struct sst_dsp *sst)
{
	/* put DSP into reset and stall */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CSR,
		SST_CSR_RST | SST_CSR_STALL, SST_CSR_RST | SST_CSR_STALL);

	/* keep in reset for 10ms */
	mdelay(10);

	/* take DSP out of reset and keep stalled for FW loading */
	sst_dsp_shim_update_bits_unlocked(sst, SST_CSR,
		SST_CSR_RST | SST_CSR_STALL, SST_CSR_STALL);
}

struct sst_adsp_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

/* lynx point ADSP mem regions */
static const struct sst_adsp_memregion lp_region[] = {
	{0x00000, 0x40000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
	{0x40000, 0x80000, 8, SST_MEM_DRAM}, /* D-SRAM1 - 8 * 32kB */
	{0x80000, 0xE0000, 12, SST_MEM_IRAM}, /* I-SRAM - 12 * 32kB */
};

/* wild cat point ADSP mem regions */
static const struct sst_adsp_memregion wpt_region[] = {
	{0x00000, 0xA0000, 20, SST_MEM_DRAM}, /* D-SRAM0,D-SRAM1,D-SRAM2 - 20 * 32kB */
	{0xA0000, 0xF0000, 10, SST_MEM_IRAM}, /* I-SRAM - 10 * 32kB */
};

static int hsw_acpi_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	/* ADSP DRAM & IRAM */
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

	/* SST Shim */
	sst->addr.shim = sst->addr.lpe + sst->addr.shim_offset;
	return 0;
}

struct sst_sram_shift {
	u32 dev_id;	/* SST Device IDs  */
	u32 iram_shift;
	u32 dram_shift;
};

static const struct sst_sram_shift sram_shift[] = {
	{SST_DEV_ID_LYNX_POINT, 6, 16}, /* lp */
	{SST_DEV_ID_WILDCAT_POINT, 2, 12}, /* wpt */
};
static u32 hsw_block_get_bit(struct sst_mem_block *block)
{
	u32 bit = 0, shift = 0, index;
	struct sst_dsp *sst = block->dsp;

	for (index = 0; index < ARRAY_SIZE(sram_shift); index++) {
		if (sram_shift[index].dev_id == sst->id)
			break;
	}

	if (index < ARRAY_SIZE(sram_shift)) {
		switch (block->type) {
		case SST_MEM_DRAM:
			shift = sram_shift[index].dram_shift;
			break;
		case SST_MEM_IRAM:
			shift = sram_shift[index].iram_shift;
			break;
		default:
			shift = 0;
		}
	} else
		shift = 0;

	bit = 1 << (block->index + shift);

	return bit;
}

/*dummy read a SRAM block.*/
static void sst_mem_block_dummy_read(struct sst_mem_block *block)
{
	u32 size;
	u8 tmp_buf[4];
	struct sst_dsp *sst = block->dsp;

	size = block->size > 4 ? 4 : block->size;
	memcpy_fromio(tmp_buf, sst->addr.lpe + block->offset, size);
}

/* enable 32kB memory block - locks held by caller */
static int hsw_block_enable(struct sst_mem_block *block)
{
	struct sst_dsp *sst = block->dsp;
	u32 bit, val;

	if (block->users++ > 0)
		return 0;

	dev_dbg(block->dsp->dev, " enabled block %d:%d at offset 0x%x\n",
		block->type, block->index, block->offset);

	val = readl(sst->addr.pci_cfg + SST_VDRTCTL0);
	bit = hsw_block_get_bit(block);
	writel(val & ~bit, sst->addr.pci_cfg + SST_VDRTCTL0);

	/* wait 18 DSP clock ticks */
	udelay(10);

	/*add a dummy read before the SRAM block is written, otherwise the writing may miss bytes sometimes.*/
	sst_mem_block_dummy_read(block);
	return 0;
}

/* disable 32kB memory block - locks held by caller */
static int hsw_block_disable(struct sst_mem_block *block)
{
	struct sst_dsp *sst = block->dsp;
	u32 bit, val;

	if (--block->users > 0)
		return 0;

	dev_dbg(block->dsp->dev, " disabled block %d:%d at offset 0x%x\n",
		block->type, block->index, block->offset);

	val = readl(sst->addr.pci_cfg + SST_VDRTCTL0);
	bit = hsw_block_get_bit(block);
	writel(val | bit, sst->addr.pci_cfg + SST_VDRTCTL0);

	return 0;
}

static struct sst_block_ops sst_hsw_ops = {
	.enable = hsw_block_enable,
	.disable = hsw_block_disable,
};

static int hsw_enable_shim(struct sst_dsp *sst)
{
	int tries = 10;
	u32 reg;

	/* enable shim */
	reg = readl(sst->addr.pci_cfg + SST_SHIM_PM_REG);
	writel(reg & ~0x3, sst->addr.pci_cfg + SST_SHIM_PM_REG);

	/* check that ADSP shim is enabled */
	while (tries--) {
		reg = sst_dsp_shim_read_unlocked(sst, SST_CSR);
		if (reg != 0xffffffff)
			return 0;

		msleep(1);
	}

	return -ENODEV;
}

static int hsw_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	const struct sst_adsp_memregion *region;
	struct device *dev;
	int ret = -ENODEV, i, j, region_count;
	u32 offset, size;

	dev = sst->dma_dev;

	switch (sst->id) {
	case SST_DEV_ID_LYNX_POINT:
		region = lp_region;
		region_count = ARRAY_SIZE(lp_region);
		sst->addr.iram_offset = SST_LP_IRAM_OFFSET;
		sst->addr.shim_offset = SST_LP_SHIM_OFFSET;
		break;
	case SST_DEV_ID_WILDCAT_POINT:
		region = wpt_region;
		region_count = ARRAY_SIZE(wpt_region);
		sst->addr.iram_offset = SST_WPT_IRAM_OFFSET;
		sst->addr.shim_offset = SST_WPT_SHIM_OFFSET;
		break;
	default:
		dev_err(dev, "error: failed to get mem resources\n");
		return ret;
	}

	ret = hsw_acpi_resource_map(sst, pdata);
	if (ret < 0) {
		dev_err(dev, "error: failed to map resources\n");
		return ret;
	}

	/* enable the DSP SHIM */
	ret = hsw_enable_shim(sst);
	if (ret < 0) {
		dev_err(dev, "error: failed to set DSP D0 and reset SHIM\n");
		return ret;
	}

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(31));
	if (ret)
		return ret;

	/* Enable Interrupt from both sides */
	sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits_unlocked(sst, SST_IMRD,
		(0x3 | 0x1 << 16 | 0x3 << 21), 0x0);

	/* register DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < region_count; i++) {
		offset = region[i].start;
		size = (region[i].end - region[i].start) / region[i].blocks;

		/* register individual memory blocks */
		for (j = 0; j < region[i].blocks; j++) {
			sst_mem_block_register(sst, offset, size,
				region[i].type, &sst_hsw_ops, j, sst);
			offset += size;
		}
	}

	/* set default power gating control, enable power gating control for all blocks. that is,
	can't be accessed, please enable each block before accessing. */
	writel(0xffffffff, sst->addr.pci_cfg + SST_VDRTCTL0);

	return 0;
}

static void hsw_free(struct sst_dsp *sst)
{
	sst_mem_block_unregister_all(sst);
	iounmap(sst->addr.lpe);
	iounmap(sst->addr.pci_cfg);
}

struct sst_ops haswell_ops = {
	.reset = hsw_reset,
	.boot = hsw_boot,
	.write = sst_shim32_write,
	.read = sst_shim32_read,
	.write64 = sst_shim32_write64,
	.read64 = sst_shim32_read64,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.irq_handler = hsw_irq,
	.init = hsw_init,
	.free = hsw_free,
	.parse_fw = hsw_parse_fw_image,
};
