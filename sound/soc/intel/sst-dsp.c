/*
 * Intel Smart Sound Technology (SST) DSP Core Driver
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

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"

#define CREATE_TRACE_POINTS
#include <trace/events/intel-sst.h>

/* Internal generic low-level SST IO functions - can be overidden */
void sst_shim32_write(void __iomem *addr, u32 offset, u32 value)
{
	writel(value, addr + offset);
}
EXPORT_SYMBOL_GPL(sst_shim32_write);

u32 sst_shim32_read(void __iomem *addr, u32 offset)
{
	return readl(addr + offset);
}
EXPORT_SYMBOL_GPL(sst_shim32_read);

void sst_shim32_write64(void __iomem *addr, u32 offset, u64 value)
{
	memcpy_toio(addr + offset, &value, sizeof(value));
}
EXPORT_SYMBOL_GPL(sst_shim32_write64);

u64 sst_shim32_read64(void __iomem *addr, u32 offset)
{
	u64 val;

	memcpy_fromio(&val, addr + offset, sizeof(val));
	return val;
}
EXPORT_SYMBOL_GPL(sst_shim32_read64);

static inline void _sst_memcpy_toio_32(volatile u32 __iomem *dest,
	u32 *src, size_t bytes)
{
	int i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		writel(src[i], dest + i);
}

static inline void _sst_memcpy_fromio_32(u32 *dest,
	const volatile __iomem u32 *src, size_t bytes)
{
	int i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		dest[i] = readl(src + i);
}

void sst_memcpy_toio_32(struct sst_dsp *sst,
	void __iomem *dest, void *src, size_t bytes)
{
	_sst_memcpy_toio_32(dest, src, bytes);
}
EXPORT_SYMBOL_GPL(sst_memcpy_toio_32);

void sst_memcpy_fromio_32(struct sst_dsp *sst, void *dest,
	void __iomem *src, size_t bytes)
{
	_sst_memcpy_fromio_32(dest, src, bytes);
}
EXPORT_SYMBOL_GPL(sst_memcpy_fromio_32);

/* Public API */
void sst_dsp_shim_write(struct sst_dsp *sst, u32 offset, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);
	sst->ops->write(sst->addr.shim, offset, value);
	spin_unlock_irqrestore(&sst->spinlock, flags);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_write);

u32 sst_dsp_shim_read(struct sst_dsp *sst, u32 offset)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&sst->spinlock, flags);
	val = sst->ops->read(sst->addr.shim, offset);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return val;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_read);

void sst_dsp_shim_write64(struct sst_dsp *sst, u32 offset, u64 value)
{
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);
	sst->ops->write64(sst->addr.shim, offset, value);
	spin_unlock_irqrestore(&sst->spinlock, flags);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_write64);

u64 sst_dsp_shim_read64(struct sst_dsp *sst, u32 offset)
{
	unsigned long flags;
	u64 val;

	spin_lock_irqsave(&sst->spinlock, flags);
	val = sst->ops->read64(sst->addr.shim, offset);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return val;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_read64);

void sst_dsp_shim_write_unlocked(struct sst_dsp *sst, u32 offset, u32 value)
{
	sst->ops->write(sst->addr.shim, offset, value);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_write_unlocked);

u32 sst_dsp_shim_read_unlocked(struct sst_dsp *sst, u32 offset)
{
	return sst->ops->read(sst->addr.shim, offset);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_read_unlocked);

void sst_dsp_shim_write64_unlocked(struct sst_dsp *sst, u32 offset, u64 value)
{
	sst->ops->write64(sst->addr.shim, offset, value);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_write64_unlocked);

u64 sst_dsp_shim_read64_unlocked(struct sst_dsp *sst, u32 offset)
{
	return sst->ops->read64(sst->addr.shim, offset);
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_read64_unlocked);

int sst_dsp_shim_update_bits_unlocked(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value)
{
	bool change;
	unsigned int old, new;
	u32 ret;

	ret = sst_dsp_shim_read_unlocked(sst, offset);

	old = ret;
	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write_unlocked(sst, offset, new);

	return change;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_update_bits_unlocked);

int sst_dsp_shim_update_bits64_unlocked(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value)
{
	bool change;
	u64 old, new;

	old = sst_dsp_shim_read64_unlocked(sst, offset);

	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write64_unlocked(sst, offset, new);

	return change;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_update_bits64_unlocked);

int sst_dsp_shim_update_bits(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value)
{
	unsigned long flags;
	bool change;

	spin_lock_irqsave(&sst->spinlock, flags);
	change = sst_dsp_shim_update_bits_unlocked(sst, offset, mask, value);
	spin_unlock_irqrestore(&sst->spinlock, flags);
	return change;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_update_bits);

int sst_dsp_shim_update_bits64(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value)
{
	unsigned long flags;
	bool change;

	spin_lock_irqsave(&sst->spinlock, flags);
	change = sst_dsp_shim_update_bits64_unlocked(sst, offset, mask, value);
	spin_unlock_irqrestore(&sst->spinlock, flags);
	return change;
}
EXPORT_SYMBOL_GPL(sst_dsp_shim_update_bits64);

void sst_dsp_dump(struct sst_dsp *sst)
{
	if (sst->ops->dump)
		sst->ops->dump(sst);
}
EXPORT_SYMBOL_GPL(sst_dsp_dump);

void sst_dsp_reset(struct sst_dsp *sst)
{
	if (sst->ops->reset)
		sst->ops->reset(sst);
}
EXPORT_SYMBOL_GPL(sst_dsp_reset);

int sst_dsp_boot(struct sst_dsp *sst)
{
	if (sst->ops->boot)
		sst->ops->boot(sst);

	return 0;
}
EXPORT_SYMBOL_GPL(sst_dsp_boot);

int sst_dsp_wake(struct sst_dsp *sst)
{
	if (sst->ops->wake)
		return sst->ops->wake(sst);

	return 0;
}
EXPORT_SYMBOL_GPL(sst_dsp_wake);

void sst_dsp_sleep(struct sst_dsp *sst)
{
	if (sst->ops->sleep)
		sst->ops->sleep(sst);
}
EXPORT_SYMBOL_GPL(sst_dsp_sleep);

void sst_dsp_stall(struct sst_dsp *sst)
{
	if (sst->ops->stall)
		sst->ops->stall(sst);
}
EXPORT_SYMBOL_GPL(sst_dsp_stall);

void sst_dsp_ipc_msg_tx(struct sst_dsp *dsp, u32 msg)
{
	sst_dsp_shim_write_unlocked(dsp, SST_IPCX, msg | SST_IPCX_BUSY);
	trace_sst_ipc_msg_tx(msg);
}
EXPORT_SYMBOL_GPL(sst_dsp_ipc_msg_tx);

u32 sst_dsp_ipc_msg_rx(struct sst_dsp *dsp)
{
	u32 msg;

	msg = sst_dsp_shim_read_unlocked(dsp, SST_IPCX);
	trace_sst_ipc_msg_rx(msg);

	return msg;
}
EXPORT_SYMBOL_GPL(sst_dsp_ipc_msg_rx);

int sst_dsp_mailbox_init(struct sst_dsp *sst, u32 inbox_offset, size_t inbox_size,
	u32 outbox_offset, size_t outbox_size)
{
	sst->mailbox.in_base = sst->addr.lpe + inbox_offset;
	sst->mailbox.out_base = sst->addr.lpe + outbox_offset;
	sst->mailbox.in_size = inbox_size;
	sst->mailbox.out_size = outbox_size;
	return 0;
}
EXPORT_SYMBOL_GPL(sst_dsp_mailbox_init);

void sst_dsp_outbox_write(struct sst_dsp *sst, void *message, size_t bytes)
{
	u32 i;

	trace_sst_ipc_outbox_write(bytes);

	memcpy_toio(sst->mailbox.out_base, message, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_outbox_wdata(i, *(u32 *)(message + i));
}
EXPORT_SYMBOL_GPL(sst_dsp_outbox_write);

void sst_dsp_outbox_read(struct sst_dsp *sst, void *message, size_t bytes)
{
	u32 i;

	trace_sst_ipc_outbox_read(bytes);

	memcpy_fromio(message, sst->mailbox.out_base, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_outbox_rdata(i, *(u32 *)(message + i));
}
EXPORT_SYMBOL_GPL(sst_dsp_outbox_read);

void sst_dsp_inbox_write(struct sst_dsp *sst, void *message, size_t bytes)
{
	u32 i;

	trace_sst_ipc_inbox_write(bytes);

	memcpy_toio(sst->mailbox.in_base, message, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_inbox_wdata(i, *(u32 *)(message + i));
}
EXPORT_SYMBOL_GPL(sst_dsp_inbox_write);

void sst_dsp_inbox_read(struct sst_dsp *sst, void *message, size_t bytes)
{
	u32 i;

	trace_sst_ipc_inbox_read(bytes);

	memcpy_fromio(message, sst->mailbox.in_base, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_inbox_rdata(i, *(u32 *)(message + i));
}
EXPORT_SYMBOL_GPL(sst_dsp_inbox_read);

struct sst_dsp *sst_dsp_new(struct device *dev,
	struct sst_dsp_device *sst_dev, struct sst_pdata *pdata)
{
	struct sst_dsp *sst;
	int err;

	dev_dbg(dev, "initialising audio DSP id 0x%x\n", pdata->id);

	sst = devm_kzalloc(dev, sizeof(*sst), GFP_KERNEL);
	if (sst == NULL)
		return NULL;

	spin_lock_init(&sst->spinlock);
	mutex_init(&sst->mutex);
	sst->dev = dev;
	sst->dma_dev = pdata->dma_dev;
	sst->thread_context = sst_dev->thread_context;
	sst->sst_dev = sst_dev;
	sst->id = pdata->id;
	sst->irq = pdata->irq;
	sst->ops = sst_dev->ops;
	sst->pdata = pdata;
	INIT_LIST_HEAD(&sst->used_block_list);
	INIT_LIST_HEAD(&sst->free_block_list);
	INIT_LIST_HEAD(&sst->module_list);
	INIT_LIST_HEAD(&sst->fw_list);
	INIT_LIST_HEAD(&sst->scratch_block_list);

	/* Initialise SST Audio DSP */
	if (sst->ops->init) {
		err = sst->ops->init(sst, pdata);
		if (err < 0)
			return NULL;
	}

	/* Register the ISR */
	err = request_threaded_irq(sst->irq, sst->ops->irq_handler,
		sst_dev->thread, IRQF_SHARED, "AudioDSP", sst);
	if (err)
		goto irq_err;

	err = sst_dma_new(sst);
	if (err)
		dev_warn(dev, "sst_dma_new failed %d\n", err);

	return sst;

irq_err:
	if (sst->ops->free)
		sst->ops->free(sst);

	return NULL;
}
EXPORT_SYMBOL_GPL(sst_dsp_new);

void sst_dsp_free(struct sst_dsp *sst)
{
	free_irq(sst->irq, sst);
	if (sst->ops->free)
		sst->ops->free(sst);

	sst_dma_free(sst->dma);
}
EXPORT_SYMBOL_GPL(sst_dsp_free);

/* Module information */
MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("Intel SST Core");
MODULE_LICENSE("GPL v2");
