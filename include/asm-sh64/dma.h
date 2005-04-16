#ifndef __ASM_SH64_DMA_H
#define __ASM_SH64_DMA_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/dma.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 */

#include <linux/mm.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#define MAX_DMA_CHANNELS	4

/*
 * SH5 can DMA in any memory area.
 *
 * The static definition is dodgy because it should limit
 * the highest DMA-able address based on the actual
 * Physical memory available. This is actually performed
 * at run time in defining the memory allowed to DMA_ZONE.
 */
#define MAX_DMA_ADDRESS		~(NPHYS_MASK)

#define DMA_MODE_READ		0
#define DMA_MODE_WRITE		1

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* __ASM_SH64_DMA_H */
