/*
 *  cb710/cb710.h
 *
 *  Copyright by Michał Mirosław, 2008-2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef LINUX_CB710_DRIVER_H
#define LINUX_CB710_DRIVER_H

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>

struct cb710_slot;

typedef int (*cb710_irq_handler_t)(struct cb710_slot *);

/* per-virtual-slot structure */
struct cb710_slot {
	struct platform_device	pdev;
	void __iomem		*iobase;
	cb710_irq_handler_t	irq_handler;
};

/* per-device structure */
struct cb710_chip {
	struct pci_dev		*pdev;
	void __iomem		*iobase;
	unsigned		platform_id;
#ifdef CONFIG_CB710_DEBUG_ASSUMPTIONS
	atomic_t		slot_refs_count;
#endif
	unsigned		slot_mask;
	unsigned		slots;
	spinlock_t		irq_lock;
	struct cb710_slot	slot[0];
};

/* NOTE: cb710_chip.slots is modified only during device init/exit and
 * they are all serialized wrt themselves */

/* cb710_chip.slot_mask values */
#define CB710_SLOT_MMC		1
#define CB710_SLOT_MS		2
#define CB710_SLOT_SM		4

/* slot port accessors - so the logic is more clear in the code */
#define CB710_PORT_ACCESSORS(t) \
static inline void cb710_write_port_##t(struct cb710_slot *slot,	\
	unsigned port, u##t value)					\
{									\
	iowrite##t(value, slot->iobase + port);				\
}									\
									\
static inline u##t cb710_read_port_##t(struct cb710_slot *slot,		\
	unsigned port)							\
{									\
	return ioread##t(slot->iobase + port);				\
}									\
									\
static inline void cb710_modify_port_##t(struct cb710_slot *slot,	\
	unsigned port, u##t set, u##t clear)				\
{									\
	iowrite##t(							\
		(ioread##t(slot->iobase + port) & ~clear)|set,		\
		slot->iobase + port);					\
}

CB710_PORT_ACCESSORS(8)
CB710_PORT_ACCESSORS(16)
CB710_PORT_ACCESSORS(32)

void cb710_pci_update_config_reg(struct pci_dev *pdev,
	int reg, uint32_t and, uint32_t xor);
void cb710_set_irq_handler(struct cb710_slot *slot,
	cb710_irq_handler_t handler);

/* some device struct walking */

static inline struct cb710_slot *cb710_pdev_to_slot(
	struct platform_device *pdev)
{
	return container_of(pdev, struct cb710_slot, pdev);
}

static inline struct cb710_chip *cb710_slot_to_chip(struct cb710_slot *slot)
{
	return dev_get_drvdata(slot->pdev.dev.parent);
}

static inline struct device *cb710_slot_dev(struct cb710_slot *slot)
{
	return &slot->pdev.dev;
}

static inline struct device *cb710_chip_dev(struct cb710_chip *chip)
{
	return &chip->pdev->dev;
}

/* debugging aids */

#ifdef CONFIG_CB710_DEBUG
void cb710_dump_regs(struct cb710_chip *chip, unsigned dump);
#else
#define cb710_dump_regs(c, d) do {} while (0)
#endif

#define CB710_DUMP_REGS_MMC	0x0F
#define CB710_DUMP_REGS_MS	0x30
#define CB710_DUMP_REGS_SM	0xC0
#define CB710_DUMP_REGS_ALL	0xFF
#define CB710_DUMP_REGS_MASK	0xFF

#define CB710_DUMP_ACCESS_8	0x100
#define CB710_DUMP_ACCESS_16	0x200
#define CB710_DUMP_ACCESS_32	0x400
#define CB710_DUMP_ACCESS_ALL	0x700
#define CB710_DUMP_ACCESS_MASK	0x700

#endif /* LINUX_CB710_DRIVER_H */
/*
 *  cb710/sgbuf2.h
 *
 *  Copyright by Michał Mirosław, 2008-2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef LINUX_CB710_SG_H
#define LINUX_CB710_SG_H

#include <linux/highmem.h>
#include <linux/scatterlist.h>

/**
 * cb710_sg_miter_stop_writing - stop mapping iteration after writing
 * @miter: sg mapping iter to be stopped
 *
 * Description:
 *   Stops mapping iterator @miter.  @miter should have been started
 *   started using sg_miter_start().  A stopped iteration can be
 *   resumed by calling sg_miter_next() on it.  This is useful when
 *   resources (kmap) need to be released during iteration.
 *
 *   This is a convenience wrapper that will be optimized out for arches
 *   that don't need flush_kernel_dcache_page().
 *
 * Context:
 *   IRQ disabled if the SG_MITER_ATOMIC is set.  Don't care otherwise.
 */
static inline void cb710_sg_miter_stop_writing(struct sg_mapping_iter *miter)
{
	if (miter->page)
		flush_kernel_dcache_page(miter->page);
	sg_miter_stop(miter);
}

/*
 * 32-bit PIO mapping sg iterator
 *
 * Hides scatterlist access issues - fragment boundaries, alignment, page
 * mapping - for drivers using 32-bit-word-at-a-time-PIO (ie. PCI devices
 * without DMA support).
 *
 * Best-case reading (transfer from device):
 *   sg_miter_start();
 *   cb710_sg_dwiter_write_from_io();
 *   cb710_sg_miter_stop_writing();
 *
 * Best-case writing (transfer to device):
 *   sg_miter_start();
 *   cb710_sg_dwiter_read_to_io();
 *   sg_miter_stop();
 */

uint32_t cb710_sg_dwiter_read_next_block(struct sg_mapping_iter *miter);
void cb710_sg_dwiter_write_next_block(struct sg_mapping_iter *miter, uint32_t data);

/**
 * cb710_sg_dwiter_write_from_io - transfer data to mapped buffer from 32-bit IO port
 * @miter: sg mapping iter
 * @port: PIO port - IO or MMIO address
 * @count: number of 32-bit words to transfer
 *
 * Description:
 *   Reads @count 32-bit words from register @port and stores it in
 *   buffer iterated by @miter.  Data that would overflow the buffer
 *   is silently ignored.  Iterator is advanced by 4*@count bytes
 *   or to the buffer's end whichever is closer.
 *
 * Context:
 *   IRQ disabled if the SG_MITER_ATOMIC is set.  Don't care otherwise.
 */
static inline void cb710_sg_dwiter_write_from_io(struct sg_mapping_iter *miter,
	void __iomem *port, size_t count)
{
	while (count-- > 0)
		cb710_sg_dwiter_write_next_block(miter, ioread32(port));
}

/**
 * cb710_sg_dwiter_read_to_io - transfer data to 32-bit IO port from mapped buffer
 * @miter: sg mapping iter
 * @port: PIO port - IO or MMIO address
 * @count: number of 32-bit words to transfer
 *
 * Description:
 *   Writes @count 32-bit words to register @port from buffer iterated
 *   through @miter.  If buffer ends before @count words are written
 *   missing data is replaced by zeroes. @miter is advanced by 4*@count
 *   bytes or to the buffer's end whichever is closer.
 *
 * Context:
 *   IRQ disabled if the SG_MITER_ATOMIC is set.  Don't care otherwise.
 */
static inline void cb710_sg_dwiter_read_to_io(struct sg_mapping_iter *miter,
	void __iomem *port, size_t count)
{
	while (count-- > 0)
		iowrite32(cb710_sg_dwiter_read_next_block(miter), port);
}

#endif /* LINUX_CB710_SG_H */
