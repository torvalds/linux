// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2022 Intel Corporation
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/platform_device.h>
#include <asm/unaligned.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"

/*
 * Register IO
 *
 * The sof_io_xyz() wrappers are typically referenced in snd_sof_dsp_ops
 * structures and cannot be inlined.
 */

void sof_io_write(struct snd_sof_dev *sdev, void __iomem *addr, u32 value)
{
	writel(value, addr);
}
EXPORT_SYMBOL(sof_io_write);

u32 sof_io_read(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readl(addr);
}
EXPORT_SYMBOL(sof_io_read);

void sof_io_write64(struct snd_sof_dev *sdev, void __iomem *addr, u64 value)
{
	writeq(value, addr);
}
EXPORT_SYMBOL(sof_io_write64);

u64 sof_io_read64(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readq(addr);
}
EXPORT_SYMBOL(sof_io_read64);

/*
 * IPC Mailbox IO
 */

void sof_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
		       void *message, size_t bytes)
{
	void __iomem *dest = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_toio(dest, message, bytes);
}
EXPORT_SYMBOL(sof_mailbox_write);

void sof_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
		      void *message, size_t bytes)
{
	void __iomem *src = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_fromio(message, src, bytes);
}
EXPORT_SYMBOL(sof_mailbox_read);

/*
 * Memory copy.
 */

int sof_block_write(struct snd_sof_dev *sdev, enum snd_sof_fw_blk_type blk_type,
		    u32 offset, void *src, size_t size)
{
	int bar = snd_sof_dsp_get_bar_index(sdev, blk_type);
	const u8 *src_byte = src;
	void __iomem *dest;
	u32 affected_mask;
	u32 tmp;
	int m, n;

	if (bar < 0)
		return bar;

	dest = sdev->bar[bar] + offset;

	m = size / 4;
	n = size % 4;

	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy(dest, src, m);

	if (n) {
		affected_mask = (1 << (8 * n)) - 1;

		/* first read the 32bit data of dest, then change affected
		 * bytes, and write back to dest. For unaffected bytes, it
		 * should not be changed
		 */
		tmp = ioread32(dest + m * 4);
		tmp &= ~affected_mask;

		tmp |= *(u32 *)(src_byte + m * 4) & affected_mask;
		iowrite32(tmp, dest + m * 4);
	}

	return 0;
}
EXPORT_SYMBOL(sof_block_write);

int sof_block_read(struct snd_sof_dev *sdev, enum snd_sof_fw_blk_type blk_type,
		   u32 offset, void *dest, size_t size)
{
	int bar = snd_sof_dsp_get_bar_index(sdev, blk_type);

	if (bar < 0)
		return bar;

	memcpy_fromio(dest, sdev->bar[bar] + offset, size);

	return 0;
}
EXPORT_SYMBOL(sof_block_read);
