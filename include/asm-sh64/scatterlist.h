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

/* These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->length)

#define ISA_DMA_THRESHOLD (0xffffffff)

#endif /* !__ASM_SH64_SCATTERLIST_H */
