/*
 * include/asm-v850/scatterlist.h
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SCATTERLIST_H__
#define __V850_SCATTERLIST_H__

#include <asm/types.h>

struct scatterlist {
	struct page	*page;
	unsigned	offset;
	dma_addr_t	dma_address;
	unsigned	length;
};

#define ISA_DMA_THRESHOLD	(~0UL)

#endif /* __V850_SCATTERLIST_H__ */
