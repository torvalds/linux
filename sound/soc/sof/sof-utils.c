// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2022 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//

#include <asm/unaligned.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <sound/memalloc.h>
#include <linux/module.h>
#include "sof-utils.h"

/*
 * Generic buffer page table creation.
 * Take the each physical page address and drop the least significant unused
 * bits from each (based on PAGE_SIZE). Then pack valid page address bits
 * into compressed page table.
 */

int snd_sof_create_page_table(struct device *dev,
			      struct snd_dma_buffer *dmab,
			      unsigned char *page_table, size_t size)
{
	int i, pages;

	pages = snd_sgbuf_aligned_pages(size);

	dev_dbg(dev, "generating page table for %p size 0x%zx pages %d\n",
		dmab->area, size, pages);

	for (i = 0; i < pages; i++) {
		/*
		 * The number of valid address bits for each page is 20.
		 * idx determines the byte position within page_table
		 * where the current page's address is stored
		 * in the compressed page_table.
		 * This can be calculated by multiplying the page number by 2.5.
		 */
		u32 idx = (5 * i) >> 1;
		u32 pfn = snd_sgbuf_get_addr(dmab, i * PAGE_SIZE) >> PAGE_SHIFT;
		u8 *pg_table;

		pg_table = (u8 *)(page_table + idx);

		/*
		 * pagetable compression:
		 * byte 0     byte 1     byte 2     byte 3     byte 4     byte 5
		 * ___________pfn 0__________ __________pfn 1___________  _pfn 2...
		 * .... ....  .... ....  .... ....  .... ....  .... ....  ....
		 * It is created by:
		 * 1. set current location to 0, PFN index i to 0
		 * 2. put pfn[i] at current location in Little Endian byte order
		 * 3. calculate an intermediate value as
		 *    x = (pfn[i+1] << 4) | (pfn[i] & 0xf)
		 * 4. put x at offset (current location + 2) in LE byte order
		 * 5. increment current location by 5 bytes, increment i by 2
		 * 6. continue to (2)
		 */
		if (i & 1)
			put_unaligned_le32((pg_table[0] & 0xf) | pfn << 4,
					   pg_table);
		else
			put_unaligned_le32(pfn, pg_table);
	}

	return pages;
}
EXPORT_SYMBOL(snd_sof_create_page_table);

MODULE_LICENSE("Dual BSD/GPL");
