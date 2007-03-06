/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/scatterlist.h
 *
 * Copyright (C) 2003  Paul Mundt
 *
 */
#ifndef __ASM_SH64_SCATTERLIST_H
#define __ASM_SH64_SCATTERLIST_H

#include <asm/types.h>

struct scatterlist {
    struct page * page; /* Location for highmem page, if any */
    unsigned int offset;/* for highmem, page offset */
    dma_addr_t dma_address;
    unsigned int length;
};

#define ISA_DMA_THRESHOLD (0xffffffff)

#endif /* !__ASM_SH64_SCATTERLIST_H */
