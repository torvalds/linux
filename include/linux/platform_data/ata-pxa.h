/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic PXA PATA driver
 *
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 */

#ifndef	__MACH_PATA_PXA_H__
#define	__MACH_PATA_PXA_H__

struct pata_pxa_pdata {
	/* PXA DMA DREQ<0:2> pin */
	uint32_t	dma_dreq;
	/* Register shift */
	uint32_t	reg_shift;
	/* IRQ flags */
	uint32_t	irq_flags;
};

#endif	/* __MACH_PATA_PXA_H__ */
