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

#define MAX_DMA_ADDRESS		0xffffffff

/* No DMA as the rest of the world see it */
#define MAX_DMA_CHANNELS	0

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

#if defined(CONFIG_PXA27x)

#define PXA_DMA_CHANNELS	32
#define PXA_DMA_NBCH(prio)	((prio == DMA_PRIO_LOW) ? 16 : 8)

typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 8,
	DMA_PRIO_LOW = 16
} pxa_dma_prio;

#elif defined(CONFIG_PXA25x)

#define PXA_DMA_CHANNELS	16
#define PXA_DMA_NBCH(prio)	((prio == DMA_PRIO_LOW) ? 8 : 4)

typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 4,
	DMA_PRIO_LOW = 8
} pxa_dma_prio;

#endif

/*
 * DMA registration
 */

int pxa_request_dma (char *name,
			 pxa_dma_prio prio,
			 void (*irq_handler)(int, void *, struct pt_regs *),
			 void *data);

void pxa_free_dma (int dma_ch);

#endif /* _ASM_ARCH_DMA_H */
