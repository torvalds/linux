/*
 * include/asm-xtensa/dma.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_DMA_H
#define _XTENSA_DMA_H

#include <linux/config.h>
#include <asm/io.h>		/* need byte IO */
#include <xtensa/config/core.h>

/*
 * This is only to be defined if we have PC-like DMA.
 * By default this is not true on an Xtensa processor,
 * however on boards with a PCI bus, such functionality
 * might be emulated externally.
 *
 * NOTE:  there still exists driver code that assumes
 * this is defined, eg. drivers/sound/soundcard.c (as of 2.4).
 */
#define MAX_DMA_CHANNELS	8

/*
 * The maximum virtual address to which DMA transfers
 * can be performed on this platform.
 *
 * NOTE: This is board (platform) specific, not processor-specific!
 *
 * NOTE: This assumes DMA transfers can only be performed on
 *	the section of physical memory contiguously mapped in virtual
 *	space for the kernel.  For the Xtensa architecture, this
 *	means the maximum possible size of this DMA area is
 *	the size of the statically mapped kernel segment
 *	(XCHAL_KSEG_{CACHED,BYPASS}_SIZE), ie. 128 MB.
 *
 * NOTE: When the entire KSEG area is DMA capable, we substract
 *	one from the max address so that the virt_to_phys() macro
 *	works correctly on the address (otherwise the address
 *	enters another area, and virt_to_phys() may not return
 *	the value desired).
 */
#define MAX_DMA_ADDRESS		(PAGE_OFFSET + XCHAL_KSEG_CACHED_SIZE - 1)

/* Reserve and release a DMA channel */
extern int request_dma(unsigned int dmanr, const char * device_id);
extern void free_dma(unsigned int dmanr);

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif


#endif
