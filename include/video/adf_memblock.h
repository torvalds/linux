/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIDEO_ADF_MEMBLOCK_H_
#define _VIDEO_ADF_MEMBLOCK_H_

struct dma_buf *adf_memblock_export(phys_addr_t base, size_t size, int flags);

#endif /* _VIDEO_ADF_MEMBLOCK_H_ */
