/* include/asm-arm/arch-lh7a40x/dma.h
 *
 *  Copyright (C) 2005 Marc Singer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

typedef enum {
	DMA_M2M0	= 0,
	DMA_M2M1	= 1,
	DMA_M2P0	= 2,	/* Tx */
	DMA_M2P1	= 3,	/* Rx */
	DMA_M2P2	= 4,	/* Tx */
	DMA_M2P3	= 5,	/* Rx */
	DMA_M2P4	= 6,	/* Tx - AC97 */
	DMA_M2P5	= 7,	/* Rx - AC97 */
	DMA_M2P6	= 8,	/* Tx */
	DMA_M2P7	= 9,	/* Rx */
} dma_device_t;

#define DMA_LENGTH_MAX		((64*1024) - 4) /* bytes */

#define DMAC_GCA		__REG(DMAC_PHYS + 0x2b80)
#define DMAC_GIR		__REG(DMAC_PHYS + 0x2bc0)

#define DMAC_GIR_MMI1		(1<<11)
#define DMAC_GIR_MMI0		(1<<10)
#define DMAC_GIR_MPI8		(1<<9)
#define DMAC_GIR_MPI9		(1<<8)
#define DMAC_GIR_MPI6		(1<<7)
#define DMAC_GIR_MPI7		(1<<6)
#define DMAC_GIR_MPI4		(1<<5)
#define DMAC_GIR_MPI5		(1<<4)
#define DMAC_GIR_MPI2		(1<<3)
#define DMAC_GIR_MPI3		(1<<2)
#define DMAC_GIR_MPI0		(1<<1)
#define DMAC_GIR_MPI1		(1<<0)

#define DMAC_M2P0		0x0000
#define DMAC_M2P1		0x0040
#define DMAC_M2P2		0x0080
#define DMAC_M2P3		0x00c0
#define DMAC_M2P4		0x0240
#define DMAC_M2P5		0x0200
#define DMAC_M2P6		0x02c0
#define DMAC_M2P7		0x0280
#define DMAC_M2P8		0x0340
#define DMAC_M2P9		0x0300
#define DMAC_M2M0		0x0100
#define DMAC_M2M1		0x0140

#define DMAC_P_PCONTROL(c)	__REG(DMAC_PHYS + (c) + 0x00)
#define DMAC_P_PINTERRUPT(c)	__REG(DMAC_PHYS + (c) + 0x04)
#define DMAC_P_PPALLOC(c)	__REG(DMAC_PHYS + (c) + 0x08)
#define DMAC_P_PSTATUS(c)	__REG(DMAC_PHYS + (c) + 0x0c)
#define DMAC_P_REMAIN(c)	__REG(DMAC_PHYS + (c) + 0x14)
#define DMAC_P_MAXCNT0(c)	__REG(DMAC_PHYS + (c) + 0x20)
#define DMAC_P_BASE0(c)		__REG(DMAC_PHYS + (c) + 0x24)
#define DMAC_P_CURRENT0(c)	__REG(DMAC_PHYS + (c) + 0x28)
#define DMAC_P_MAXCNT1(c)	__REG(DMAC_PHYS + (c) + 0x30)
#define DMAC_P_BASE1(c)		__REG(DMAC_PHYS + (c) + 0x34)
#define DMAC_P_CURRENT1(c)	__REG(DMAC_PHYS + (c) + 0x38)

#define DMAC_PCONTROL_ENABLE	(1<<4)

#define DMAC_PORT_USB		0
#define DMAC_PORT_SDMMC		1
#define DMAC_PORT_AC97_1	2
#define DMAC_PORT_AC97_2	3
#define DMAC_PORT_AC97_3	4
#define DMAC_PORT_UART1		6
#define DMAC_PORT_UART2		7
#define DMAC_PORT_UART3		8

#define DMAC_PSTATUS_CURRSTATE_SHIFT	4
#define DMAC_PSTATUS_CURRSTATE_MASK	0x3

#define DMAC_PSTATUS_NEXTBUF	 (1<<6)
#define DMAC_PSTATUS_STALLRINT	 (1<<0)

#define DMAC_INT_CHE		 (1<<3)
#define DMAC_INT_NFB		 (1<<1)
#define DMAC_INT_STALL		 (1<<0)
