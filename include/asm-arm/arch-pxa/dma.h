/*
 *  linux/include/asm-arm/arch-pxa/dma.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/*
 * Descriptor structure for PXA's DMA engine
 * Note: this structure must always be aligned to a 16-byte boundary.
 */

typedef struct pxa_dma_desc {
	volatile u32 ddadr;	/* Points to the next descriptor + flags */
	volatile u32 dsadr;	/* DSADR value for the current transfer */
	volatile u32 dtadr;	/* DTADR value for the current transfer */
	volatile u32 dcmd;	/* DCMD value for the current transfer */
} pxa_dma_desc;

typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 1,
	DMA_PRIO_LOW = 2
} pxa_dma_prio;

#if defined(CONFIG_PXA27x)

#define PXA_DMA_CHANNELS	32

#define pxa_for_each_dma_prio(ch, prio)					\
for (									\
	ch = prio * 4;							\
	ch != (4 << prio) + 16;						\
	ch = (ch + 1 == (4 << prio)) ? (prio * 4 + 16) : (ch + 1)	\
)

#elif defined(CONFIG_PXA25x)

#define PXA_DMA_CHANNELS	16

#define pxa_for_each_dma_prio(ch, prio)					\
	for (ch = prio * 4; ch != (4 << prio); ch++)

#endif

/*
 * DMA registration
 */

int pxa_request_dma (char *name,
			 pxa_dma_prio prio,
			 void (*irq_handler)(int, void *),
			 void *data);

void pxa_free_dma (int dma_ch);

#endif /* _ASM_ARCH_DMA_H */
