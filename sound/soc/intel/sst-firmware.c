/*
 * Intel SST Firmware Loader
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pci.h>
#include <linux/acpi.h>

/* supported DMA engine drivers */
#include <linux/platform_data/dma-dw.h>
#include <linux/dma/dw.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"

#define SST_DMA_RESOURCES	2
#define SST_DSP_DMA_MAX_BURST	0x3
#define SST_HSW_BLOCK_ANY	0xffffffff

#define SST_HSW_MASK_DMA_ADDR_DSP 0xfff00000

struct sst_dma {
	struct sst_dsp *sst;

	struct dw_dma_chip *chip;

	struct dma_async_tx_descriptor *desc;
	struct dma_chan *ch;
};

static inline void sst_memcpy32(volatile void __iomem *dest, void *src, u32 bytes)
{
	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy((void *)dest, src, bytes/4);
}

static void sst_dma_transfer_complete(void *arg)
{
	struct sst_dsp *sst = (struct sst_dsp *)arg;

	dev_dbg(sst->dev, "DMA: callback\n");
}

static int sst_dsp_dma_copy(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	struct dma_async_tx_descriptor *desc;
	struct sst_dma *dma = sst->dma;

	if (dma->ch == NULL) {
		dev_err(sst->dev, "error: no DMA channel\n");
		return -ENODEV;
	}

	dev_dbg(sst->dev, "DMA: src: 0x%lx dest 0x%lx size %zu\n",
		(unsigned long)src_addr, (unsigned long)dest_addr, size);

	desc = dma->ch->device->device_prep_dma_memcpy(dma->ch, dest_addr,
		src_addr, size, DMA_CTRL_ACK);
	if (!desc){
		dev_err(sst->dev, "error: dma prep memcpy failed\n");
		return -EINVAL;
	}

	desc->callback = sst_dma_transfer_complete;
	desc->callback_param = sst;

	desc->tx_submit(desc);
	dma_wait_for_async_tx(desc);

	return 0;
}

/* copy to DSP */
int sst_dsp_dma_copyto(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	return sst_dsp_dma_copy(sst, dest_addr | SST_HSW_MASK_DMA_ADDR_DSP,
			src_addr, size);
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_copyto);

/* copy from DSP */
int sst_dsp_dma_copyfrom(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	return sst_dsp_dma_copy(sst, dest_addr,
		src_addr | SST_HSW_MASK_DMA_ADDR_DSP, size);
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_copyfrom);

/* remove module from memory - callers hold locks */
static void block_list_remove(struct sst_dsp *dsp,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	int err;

	/* disable each block  */
	list_for_each_entry(block, block_list, module_list) {

		if (block->ops && block->ops->disable) {
			err = block->ops->disable(block);
			if (err < 0)
				dev_err(dsp->dev,
					"error: cant disable block %d:%d\n",
					block->type, block->index);
		}
	}

	/* mark each block as free */
	list_for_each_entry_safe(block, tmp, block_list, module_list) {
		list_del(&block->module_list);
		list_move(&block->list, &dsp->free_block_list);
		dev_dbg(dsp->dev, "block freed %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);
	}
}

/* prepare the memory block to receive data from host - callers hold locks */
static int block_list_prepare(struct sst_dsp *dsp,
	struct list_head *block_list)
{
	struct sst_mem_block *block;
	int ret = 0;

	/* enable each block so that's it'e ready for data */
	list_for_each_entry(block, block_list, module_list) {

		if (block->ops && block->ops->enable && !block->users) {
			ret = block->ops->enable(block);
			if (ret < 0) {
				dev_err(dsp->dev,
					"error: cant disable block %d:%d\n",
					block->type, block->index);
				goto err;
			}
		}
	}
	return ret;

err:
	list_for_each_entry(block, block_list, module_list) {
		if (block->ops && block->ops->disable)
			block->ops->disable(block);
	}
	return ret;
}

static struct dw_dma_platform_data dw_pdata = {
	.is_private = 1,
	.chan_allocation_order = CHAN_ALLOCATION_ASCENDING,
	.chan_priority = CHAN_PRIORITY_ASCENDING,
};

static struct dw_dma_chip *dw_probe(struct device *dev, struct resource *mem,
	int irq)
{
	struct dw_dma_chip *chip;
	int err;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	chip->irq = irq;
	chip->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(chip->regs))
		return ERR_CAST(chip->regs);

	err = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(31));
	if (err)
		return ERR_PTR(err);

	chip->dev = dev;
	err = dw_dma_probe(chip, &dw_pdata);
	if (err)
		return ERR_PTR(err);

	return chip;
}

static void dw_remove(struct dw_dma_chip *chip)
{
	dw_dma_remove(chip);
}

static bool dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct sst_dsp *dsp = (struct sst_dsp *)param;

	return chan->device->dev == dsp->dma_dev;
}

int sst_dsp_dma_get_channel(struct sst_dsp *dsp, int chan_id)
{
	struct sst_dma *dma = dsp->dma;
	struct dma_slave_config slave;
	dma_cap_mask_t mask;
	int ret;

	/* The Intel MID DMA engine driver needs the slave config set but
	 * Synopsis DMA engine driver safely ignores the slave config */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_MEMCPY, mask);

	dma->ch = dma_request_channel(mask, dma_chan_filter, dsp);
	if (dma->ch == NULL) {
		dev_err(dsp->dev, "error: DMA request channel failed\n");
		return -EIO;
	}

	memset(&slave, 0, sizeof(slave));
	slave.direction = DMA_MEM_TO_DEV;
	slave.src_addr_width =
		slave.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave.src_maxburst = slave.dst_maxburst = SST_DSP_DMA_MAX_BURST;

	ret = dmaengine_slave_config(dma->ch, &slave);
	if (ret) {
		dev_err(dsp->dev, "error: unable to set DMA slave config %d\n",
			ret);
		dma_release_channel(dma->ch);
		dma->ch = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_get_channel);

void sst_dsp_dma_put_channel(struct sst_dsp *dsp)
{
	struct sst_dma *dma = dsp->dma;

	if (!dma->ch)
		return;

	dma_release_channel(dma->ch);
	dma->ch = NULL;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_put_channel);

int sst_dma_new(struct sst_dsp *sst)
{
	struct sst_pdata *sst_pdata = sst->pdata;
	struct sst_dma *dma;
	struct resource mem;
	const char *dma_dev_name;
	int ret = 0;

	/* configure the correct platform data for whatever DMA engine
	* is attached to the ADSP IP. */
	switch (sst->pdata->dma_engine) {
	case SST_DMA_TYPE_DW:
		dma_dev_name = "dw_dmac";
		break;
	case SST_DMA_TYPE_MID:
		dma_dev_name = "Intel MID DMA";
		break;
	default:
		dev_err(sst->dev, "error: invalid DMA engine %d\n",
			sst->pdata->dma_engine);
		return -EINVAL;
	}

	dma = devm_kzalloc(sst->dev, sizeof(struct sst_dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->sst = sst;

	memset(&mem, 0, sizeof(mem));

	mem.start = sst->addr.lpe_base + sst_pdata->dma_base;
	mem.end   = sst->addr.lpe_base + sst_pdata->dma_base + sst_pdata->dma_size - 1;
	mem.flags = IORESOURCE_MEM;

	/* now register DMA engine device */
	dma->chip = dw_probe(sst->dma_dev, &mem, sst_pdata->irq);
	if (IS_ERR(dma->chip)) {
		dev_err(sst->dev, "error: DMA device register failed\n");
		ret = PTR_ERR(dma->chip);
		goto err_dma_dev;
	}

	sst->dma = dma;
	sst->fw_use_dma = true;
	return 0;

err_dma_dev:
	devm_kfree(sst->dev, dma);
	return ret;
}
EXPORT_SYMBOL(sst_dma_new);

void sst_dma_free(struct sst_dma *dma)
{

	if (dma == NULL)
		return;

	if (dma->ch)
		dma_release_channel(dma->ch);

	if (dma->chip)
		dw_remove(dma->chip);

}
EXPORT_SYMBOL(sst_dma_free);

/* create new generic firmware object */
struct sst_fw *sst_fw_new(struct sst_dsp *dsp, 
	const struct firmware *fw, void *private)
{
	struct sst_fw *sst_fw;
	int err;

	if (!dsp->ops->parse_fw)
		return NULL;

	sst_fw = kzalloc(sizeof(*sst_fw), GFP_KERNEL);
	if (sst_fw == NULL)
		return NULL;

	sst_fw->dsp = dsp;
	sst_fw->private = private;
	sst_fw->size = fw->size;

	/* allocate DMA buffer to store FW data */
	sst_fw->dma_buf = dma_alloc_coherent(dsp->dma_dev, sst_fw->size,
				&sst_fw->dmable_fw_paddr, GFP_DMA | GFP_KERNEL);
	if (!sst_fw->dma_buf) {
		dev_err(dsp->dev, "error: DMA alloc failed\n");
		kfree(sst_fw);
		return NULL;
	}

	/* copy FW data to DMA-able memory */
	memcpy((void *)sst_fw->dma_buf, (void *)fw->data, fw->size);

	if (dsp->fw_use_dma) {
		err = sst_dsp_dma_get_channel(dsp, 0);
		if (err < 0)
			goto chan_err;
	}

	/* call core specific FW paser to load FW data into DSP */
	err = dsp->ops->parse_fw(sst_fw);
	if (err < 0) {
		dev_err(dsp->dev, "error: parse fw failed %d\n", err);
		goto parse_err;
	}

	if (dsp->fw_use_dma)
		sst_dsp_dma_put_channel(dsp);

	mutex_lock(&dsp->mutex);
	list_add(&sst_fw->list, &dsp->fw_list);
	mutex_unlock(&dsp->mutex);

	return sst_fw;

parse_err:
	if (dsp->fw_use_dma)
		sst_dsp_dma_put_channel(dsp);
chan_err:
	dma_free_coherent(dsp->dma_dev, sst_fw->size,
				sst_fw->dma_buf,
				sst_fw->dmable_fw_paddr);
	sst_fw->dma_buf = NULL;
	kfree(sst_fw);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_fw_new);

int sst_fw_reload(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret;

	dev_dbg(dsp->dev, "reloading firmware\n");

	/* call core specific FW paser to load FW data into DSP */
	ret = dsp->ops->parse_fw(sst_fw);
	if (ret < 0)
		dev_err(dsp->dev, "error: parse fw failed %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(sst_fw_reload);

void sst_fw_unload(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	struct sst_module *module, *mtmp;
	struct sst_module_runtime *runtime, *rtmp;

	dev_dbg(dsp->dev, "unloading firmware\n");

	mutex_lock(&dsp->mutex);

	/* check module by module */
	list_for_each_entry_safe(module, mtmp, &dsp->module_list, list) {
		if (module->sst_fw == sst_fw) {

			/* remove runtime modules */
			list_for_each_entry_safe(runtime, rtmp, &module->runtime_list, list) {

				block_list_remove(dsp, &runtime->block_list);
				list_del(&runtime->list);
				kfree(runtime);
			}

			/* now remove the module */
			block_list_remove(dsp, &module->block_list);
			list_del(&module->list);
			kfree(module);
		}
	}

	/* remove all scratch blocks */
	block_list_remove(dsp, &dsp->scratch_block_list);

	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_fw_unload);

/* free single firmware object */
void sst_fw_free(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_fw->list);
	mutex_unlock(&dsp->mutex);

	if (sst_fw->dma_buf)
		dma_free_coherent(dsp->dma_dev, sst_fw->size, sst_fw->dma_buf,
			sst_fw->dmable_fw_paddr);
	kfree(sst_fw);
}
EXPORT_SYMBOL_GPL(sst_fw_free);

/* free all firmware objects */
void sst_fw_free_all(struct sst_dsp *dsp)
{
	struct sst_fw *sst_fw, *t;

	mutex_lock(&dsp->mutex);
	list_for_each_entry_safe(sst_fw, t, &dsp->fw_list, list) {

		list_del(&sst_fw->list);
		dma_free_coherent(dsp->dev, sst_fw->size, sst_fw->dma_buf,
			sst_fw->dmable_fw_paddr);
		kfree(sst_fw);
	}
	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_fw_free_all);

/* create a new SST generic module from FW template */
struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	struct sst_module *sst_module;

	sst_module = kzalloc(sizeof(*sst_module), GFP_KERNEL);
	if (sst_module == NULL)
		return NULL;

	sst_module->id = template->id;
	sst_module->dsp = dsp;
	sst_module->sst_fw = sst_fw;
	sst_module->scratch_size = template->scratch_size;
	sst_module->persistent_size = template->persistent_size;

	INIT_LIST_HEAD(&sst_module->block_list);
	INIT_LIST_HEAD(&sst_module->runtime_list);

	mutex_lock(&dsp->mutex);
	list_add(&sst_module->list, &dsp->module_list);
	mutex_unlock(&dsp->mutex);

	return sst_module;
}
EXPORT_SYMBOL_GPL(sst_module_new);

/* free firmware module and remove from available list */
void sst_module_free(struct sst_module *sst_module)
{
	struct sst_dsp *dsp = sst_module->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_module->list);
	mutex_unlock(&dsp->mutex);

	kfree(sst_module);
}
EXPORT_SYMBOL_GPL(sst_module_free);

struct sst_module_runtime *sst_module_runtime_new(struct sst_module *module,
	int id, void *private)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_module_runtime *runtime;

	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (runtime == NULL)
		return NULL;

	runtime->id = id;
	runtime->dsp = dsp;
	runtime->module = module;
	INIT_LIST_HEAD(&runtime->block_list);

	mutex_lock(&dsp->mutex);
	list_add(&runtime->list, &module->runtime_list);
	mutex_unlock(&dsp->mutex);

	return runtime;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_new);

void sst_module_runtime_free(struct sst_module_runtime *runtime)
{
	struct sst_dsp *dsp = runtime->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&runtime->list);
	mutex_unlock(&dsp->mutex);

	kfree(runtime);
}
EXPORT_SYMBOL_GPL(sst_module_runtime_free);

static struct sst_mem_block *find_block(struct sst_dsp *dsp,
	struct sst_block_allocator *ba)
{
	struct sst_mem_block *block;

	list_for_each_entry(block, &dsp->free_block_list, list) {
		if (block->type == ba->type && block->offset == ba->offset)
			return block;
	}

	return NULL;
}

/* Block allocator must be on block boundary */
static int block_alloc_contiguous(struct sst_dsp *dsp,
	struct sst_block_allocator *ba, struct list_head *block_list)
{
	struct list_head tmp = LIST_HEAD_INIT(tmp);
	struct sst_mem_block *block;
	u32 block_start = SST_HSW_BLOCK_ANY;
	int size = ba->size, offset = ba->offset;

	while (ba->size > 0) {

		block = find_block(dsp, ba);
		if (!block) {
			list_splice(&tmp, &dsp->free_block_list);

			ba->size = size;
			ba->offset = offset;
			return -ENOMEM;
		}

		list_move_tail(&block->list, &tmp);
		ba->offset += block->size;
		ba->size -= block->size;
	}
	ba->size = size;
	ba->offset = offset;

	list_for_each_entry(block, &tmp, list) {

		if (block->offset < block_start)
			block_start = block->offset;

		list_add(&block->module_list, block_list);

		dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);
	}

	list_splice(&tmp, &dsp->used_block_list);
	return 0;
}

/* allocate first free DSP blocks for data - callers hold locks */
static int block_alloc(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	int ret = 0;

	if (ba->size == 0)
		return 0;

	/* find first free whole blocks that can hold module */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		if (ba->size > block->size)
			continue;

		ba->offset = block->offset;
		block->bytes_used = ba->size % block->size;
		list_add(&block->module_list, block_list);
		list_move(&block->list, &dsp->used_block_list);
		dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);
		return 0;
	}

	/* then find free multiple blocks that can hold module */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		/* do we span > 1 blocks */
		if (ba->size > block->size) {

			/* align ba to block boundary */
			ba->offset = block->offset;

			ret = block_alloc_contiguous(dsp, ba, block_list);
			if (ret == 0)
				return ret;

		}
	}

	/* not enough free block space */
	return -ENOMEM;
}

int sst_alloc_blocks(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	int ret;

	dev_dbg(dsp->dev, "block request 0x%x bytes at offset 0x%x type %d\n",
		ba->size, ba->offset, ba->type);

	mutex_lock(&dsp->mutex);

	ret = block_alloc(dsp, ba, block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: can't alloc blocks %d\n", ret);
		goto out;
	}

	/* prepare DSP blocks for module usage */
	ret = block_list_prepare(dsp, block_list);
	if (ret < 0)
		dev_err(dsp->dev, "error: prepare failed\n");

out:
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_alloc_blocks);

int sst_free_blocks(struct sst_dsp *dsp, struct list_head *block_list)
{
	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_free_blocks);

/* allocate memory blocks for static module addresses - callers hold locks */
static int block_alloc_fixed(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	u32 end = ba->offset + ba->size, block_end;
	int err;

	/* only IRAM/DRAM blocks are managed */
	if (ba->type != SST_MEM_IRAM && ba->type != SST_MEM_DRAM)
		return 0;

	/* are blocks already attached to this module */
	list_for_each_entry_safe(block, tmp, block_list, module_list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		block_end = block->offset + block->size;

		/* find block that holds section */
		if (ba->offset >= block->offset && end <= block_end)
			return 0;

		/* does block span more than 1 section */
		if (ba->offset >= block->offset && ba->offset < block_end) {

			/* align ba to block boundary */
			ba->size -= block_end - ba->offset;
			ba->offset = block_end;
			err = block_alloc_contiguous(dsp, ba, block_list);
			if (err < 0)
				return -ENOMEM;

			/* module already owns blocks */
			return 0;
		}
	}

	/* find first free blocks that can hold section in free list */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		block_end = block->offset + block->size;

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		/* find block that holds section */
		if (ba->offset >= block->offset && end <= block_end) {

			/* add block */
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->module_list, block_list);
			dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
				block->type, block->index, block->offset);
			return 0;
		}

		/* does block span more than 1 section */
		if (ba->offset >= block->offset && ba->offset < block_end) {

			/* add block */
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->module_list, block_list);
			/* align ba to block boundary */
			ba->size -= block_end - ba->offset;
			ba->offset = block_end;

			err = block_alloc_contiguous(dsp, ba, block_list);
			if (err < 0)
				return -ENOMEM;

			return 0;
		}
	}

	return -ENOMEM;
}

/* Load fixed module data into DSP memory blocks */
int sst_module_alloc_blocks(struct sst_module *module)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_fw *sst_fw = module->sst_fw;
	struct sst_block_allocator ba;
	int ret;

	ba.size = module->size;
	ba.type = module->type;
	ba.offset = module->offset;

	dev_dbg(dsp->dev, "block request 0x%x bytes at offset 0x%x type %d\n",
		ba.size, ba.offset, ba.type);

	mutex_lock(&dsp->mutex);

	/* alloc blocks that includes this section */
	ret = block_alloc_fixed(dsp, &ba, &module->block_list);
	if (ret < 0) {
		dev_err(dsp->dev,
			"error: no free blocks for section at offset 0x%x size 0x%x\n",
			module->offset, module->size);
		mutex_unlock(&dsp->mutex);
		return -ENOMEM;
	}

	/* prepare DSP blocks for module copy */
	ret = block_list_prepare(dsp, &module->block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: fw module prepare failed\n");
		goto err;
	}

	/* copy partial module data to blocks */
	if (dsp->fw_use_dma) {
		ret = sst_dsp_dma_copyto(dsp,
			dsp->addr.lpe_base + module->offset,
			sst_fw->dmable_fw_paddr + module->data_offset,
			module->size);
		if (ret < 0) {
			dev_err(dsp->dev, "error: module copy failed\n");
			goto err;
		}
	} else
		sst_memcpy32(dsp->addr.lpe + module->offset, module->data,
			module->size);

	mutex_unlock(&dsp->mutex);
	return ret;

err:
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_alloc_blocks);

/* Unload entire module from DSP memory */
int sst_module_free_blocks(struct sst_module *module)
{
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_free_blocks);

int sst_module_runtime_alloc_blocks(struct sst_module_runtime *runtime,
	int offset)
{
	struct sst_dsp *dsp = runtime->dsp;
	struct sst_module *module = runtime->module;
	struct sst_block_allocator ba;
	int ret;

	if (module->persistent_size == 0)
		return 0;

	ba.size = module->persistent_size;
	ba.type = SST_MEM_DRAM;

	mutex_lock(&dsp->mutex);

	/* do we need to allocate at a fixed address ? */
	if (offset != 0) {

		ba.offset = offset;

		dev_dbg(dsp->dev, "persistent fixed block request 0x%x bytes type %d offset 0x%x\n",
			ba.size, ba.type, ba.offset);

		/* alloc blocks that includes this section */
		ret = block_alloc_fixed(dsp, &ba, &runtime->block_list);

	} else {
		dev_dbg(dsp->dev, "persistent block request 0x%x bytes type %d\n",
			ba.size, ba.type);

		/* alloc blocks that includes this section */
		ret = block_alloc(dsp, &ba, &runtime->block_list);
	}
	if (ret < 0) {
		dev_err(dsp->dev,
		"error: no free blocks for runtime module size 0x%x\n",
			module->persistent_size);
		mutex_unlock(&dsp->mutex);
		return -ENOMEM;
	}
	runtime->persistent_offset = ba.offset;

	/* prepare DSP blocks for module copy */
	ret = block_list_prepare(dsp, &runtime->block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: runtime block prepare failed\n");
		goto err;
	}

	mutex_unlock(&dsp->mutex);
	return ret;

err:
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_alloc_blocks);

int sst_module_runtime_free_blocks(struct sst_module_runtime *runtime)
{
	struct sst_dsp *dsp = runtime->dsp;

	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &runtime->block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_free_blocks);

int sst_module_runtime_save(struct sst_module_runtime *runtime,
	struct sst_module_runtime_context *context)
{
	struct sst_dsp *dsp = runtime->dsp;
	struct sst_module *module = runtime->module;
	int ret = 0;

	dev_dbg(dsp->dev, "saving runtime %d memory at 0x%x size 0x%x\n",
		runtime->id, runtime->persistent_offset,
		module->persistent_size);

	context->buffer = dma_alloc_coherent(dsp->dma_dev,
		module->persistent_size,
		&context->dma_buffer, GFP_DMA | GFP_KERNEL);
	if (!context->buffer) {
		dev_err(dsp->dev, "error: DMA context alloc failed\n");
		return -ENOMEM;
	}

	mutex_lock(&dsp->mutex);

	if (dsp->fw_use_dma) {

		ret = sst_dsp_dma_get_channel(dsp, 0);
		if (ret < 0)
			goto err;

		ret = sst_dsp_dma_copyfrom(dsp, context->dma_buffer,
			dsp->addr.lpe_base + runtime->persistent_offset,
			module->persistent_size);
		sst_dsp_dma_put_channel(dsp);
		if (ret < 0) {
			dev_err(dsp->dev, "error: context copy failed\n");
			goto err;
		}
	} else
		sst_memcpy32(context->buffer, dsp->addr.lpe +
			runtime->persistent_offset,
			module->persistent_size);

err:
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_save);

int sst_module_runtime_restore(struct sst_module_runtime *runtime,
	struct sst_module_runtime_context *context)
{
	struct sst_dsp *dsp = runtime->dsp;
	struct sst_module *module = runtime->module;
	int ret = 0;

	dev_dbg(dsp->dev, "restoring runtime %d memory at 0x%x size 0x%x\n",
		runtime->id, runtime->persistent_offset,
		module->persistent_size);

	mutex_lock(&dsp->mutex);

	if (!context->buffer) {
		dev_info(dsp->dev, "no context buffer need to restore!\n");
		goto err;
	}

	if (dsp->fw_use_dma) {

		ret = sst_dsp_dma_get_channel(dsp, 0);
		if (ret < 0)
			goto err;

		ret = sst_dsp_dma_copyto(dsp,
			dsp->addr.lpe_base + runtime->persistent_offset,
			context->dma_buffer, module->persistent_size);
		sst_dsp_dma_put_channel(dsp);
		if (ret < 0) {
			dev_err(dsp->dev, "error: module copy failed\n");
			goto err;
		}
	} else
		sst_memcpy32(dsp->addr.lpe + runtime->persistent_offset,
			context->buffer, module->persistent_size);

	dma_free_coherent(dsp->dma_dev, module->persistent_size,
				context->buffer, context->dma_buffer);
	context->buffer = NULL;

err:
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_restore);

/* register a DSP memory block for use with FW based modules */
struct sst_mem_block *sst_mem_block_register(struct sst_dsp *dsp, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops, u32 index,
	void *private)
{
	struct sst_mem_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (block == NULL)
		return NULL;

	block->offset = offset;
	block->size = size;
	block->index = index;
	block->type = type;
	block->dsp = dsp;
	block->private = private;
	block->ops = ops;

	mutex_lock(&dsp->mutex);
	list_add(&block->list, &dsp->free_block_list);
	mutex_unlock(&dsp->mutex);

	return block;
}
EXPORT_SYMBOL_GPL(sst_mem_block_register);

/* unregister all DSP memory blocks */
void sst_mem_block_unregister_all(struct sst_dsp *dsp)
{
	struct sst_mem_block *block, *tmp;

	mutex_lock(&dsp->mutex);

	/* unregister used blocks */
	list_for_each_entry_safe(block, tmp, &dsp->used_block_list, list) {
		list_del(&block->list);
		kfree(block);
	}

	/* unregister free blocks */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		list_del(&block->list);
		kfree(block);
	}

	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_mem_block_unregister_all);

/* allocate scratch buffer blocks */
int sst_block_alloc_scratch(struct sst_dsp *dsp)
{
	struct sst_module *module;
	struct sst_block_allocator ba;
	int ret;

	mutex_lock(&dsp->mutex);

	/* calculate required scratch size */
	dsp->scratch_size = 0;
	list_for_each_entry(module, &dsp->module_list, list) {
		dev_dbg(dsp->dev, "module %d scratch req 0x%x bytes\n",
			module->id, module->scratch_size);
		if (dsp->scratch_size < module->scratch_size)
			dsp->scratch_size = module->scratch_size;
	}

	dev_dbg(dsp->dev, "scratch buffer required is 0x%x bytes\n",
		dsp->scratch_size);

	if (dsp->scratch_size == 0) {
		dev_info(dsp->dev, "no modules need scratch buffer\n");
		mutex_unlock(&dsp->mutex);
		return 0;
	}

	/* allocate blocks for module scratch buffers */
	dev_dbg(dsp->dev, "allocating scratch blocks\n");

	ba.size = dsp->scratch_size;
	ba.type = SST_MEM_DRAM;

	/* do we need to allocate at fixed offset */
	if (dsp->scratch_offset != 0) {

		dev_dbg(dsp->dev, "block request 0x%x bytes type %d at 0x%x\n",
			ba.size, ba.type, ba.offset);

		ba.offset = dsp->scratch_offset;

		/* alloc blocks that includes this section */
		ret = block_alloc_fixed(dsp, &ba, &dsp->scratch_block_list);

	} else {
		dev_dbg(dsp->dev, "block request 0x%x bytes type %d\n",
			ba.size, ba.type);

		ba.offset = 0;
		ret = block_alloc(dsp, &ba, &dsp->scratch_block_list);
	}
	if (ret < 0) {
		dev_err(dsp->dev, "error: can't alloc scratch blocks\n");
		mutex_unlock(&dsp->mutex);
		return ret;
	}

	ret = block_list_prepare(dsp, &dsp->scratch_block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: scratch block prepare failed\n");
		mutex_unlock(&dsp->mutex);
		return ret;
	}

	/* assign the same offset of scratch to each module */
	dsp->scratch_offset = ba.offset;
	mutex_unlock(&dsp->mutex);
	return dsp->scratch_size;
}
EXPORT_SYMBOL_GPL(sst_block_alloc_scratch);

/* free all scratch blocks */
void sst_block_free_scratch(struct sst_dsp *dsp)
{
	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &dsp->scratch_block_list);
	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_block_free_scratch);

/* get a module from it's unique ID */
struct sst_module *sst_module_get_from_id(struct sst_dsp *dsp, u32 id)
{
	struct sst_module *module;

	mutex_lock(&dsp->mutex);

	list_for_each_entry(module, &dsp->module_list, list) {
		if (module->id == id) {
			mutex_unlock(&dsp->mutex);
			return module;
		}
	}

	mutex_unlock(&dsp->mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_module_get_from_id);

struct sst_module_runtime *sst_module_runtime_get_from_id(
	struct sst_module *module, u32 id)
{
	struct sst_module_runtime *runtime;
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);

	list_for_each_entry(runtime, &module->runtime_list, list) {
		if (runtime->id == id) {
			mutex_unlock(&dsp->mutex);
			return runtime;
		}
	}

	mutex_unlock(&dsp->mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_get_from_id);

/* returns block address in DSP address space */
u32 sst_dsp_get_offset(struct sst_dsp *dsp, u32 offset,
	enum sst_mem_type type)
{
	switch (type) {
	case SST_MEM_IRAM:
		return offset - dsp->addr.iram_offset +
			dsp->addr.dsp_iram_offset;
	case SST_MEM_DRAM:
		return offset - dsp->addr.dram_offset +
			dsp->addr.dsp_dram_offset;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(sst_dsp_get_offset);
