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
#ifdef CONFIG_DEBUG_SG
    unsigned long sg_magic;
#endif
    unsigned long page_link;
    unsigned int offset;/* for highmem, page offset */
    dma_addr_t dma_address;
    unsigned int length;
};

#define ISA_DMA_THRESHOLD (0xffffffff)

#endif /* !__ASM_SH64_SCATTERLIST_H */
