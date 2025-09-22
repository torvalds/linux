/*	$OpenBSD: pciide_cy693_reg.h,v 1.9 2022/01/09 05:42:58 jsg Exp $	*/
/*	$NetBSD: pciide_cy693_reg.h,v 1.4 2000/05/15 08:46:01 bouyer Exp $	*/

/*
 * Copyright (c) 1998 Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _DEV_PCI_PCIIDE_CY693_REG_H_
#define _DEV_PCI_PCIIDE_CY693_REG_H_

/*
 * Registers definitions for Contaq/Cypress's CY82693U PCI IDE controller.
 * Available from http://www.cypress.com/japan/prodgate/chip/cy82c693.html
 * This chip has 2 PCI IDE functions, each of them has only one channel
 * So there's no primary/secondary distinction in the registers defs.
 */

/* IDE control register */
#define CY_CTRL 0x40
#define CY_CTRL_RETRY			0x00002000
#define CY_CTRL_SLAVE_PREFETCH		0x00000400
#define CY_CTRL_POSTWRITE		0x00000200
#define	CY_CTRL_PREFETCH(drive)		(0x00000100 << (2 * (drive)))
#define CY_CTRL_POSTWRITE_LENGTH_MASK	0x00000030
#define CY_CTRL_POSTWRITE_LENGTH_OFF    4
#define CY_CTRL_PREFETCH_LENGTH_MASK	0x00000003
#define CY_CTRL_PREFETCH_LENGTH_OFF	0

/* IDE addr setup control register */
#define CY_ADDR_CTRL 0x48
#define CY_ADDR_CTRL_SETUP_OFF(drive)  (4 * (drive))
#define CY_ADDR_CTRL_SETUP_MASK(drive) \
	(0x00000007 << CY_ADDR_CTRL_SETUP_OFF(drive))

/* command control register */
#define CY_CMD_CTRL 0x4c
#define CY_CMD_CTRL_IOW_PULSE_OFF(drive)	(12 + 16 * (drive))
#define CY_CMD_CTRL_IOW_REC_OFF(drive)		(8 + 16 * (drive))
#define CY_CMD_CTRL_IOR_PULSE_OFF(drive)	(4 + 16 * (drive))
#define CY_CMD_CTRL_IOR_REC_OFF(drive)		(0 + 16 * (drive))

static int8_t cy_pio_pulse[] = {9, 4, 3, 2, 2};
static int8_t cy_pio_rec[] =   {9, 7, 4, 2, 0};
#ifdef unused
static int8_t cy_dma_pulse[] = {7, 2, 2};
static int8_t cy_dma_rec[] =   {7, 1, 0};
#endif

/*
 * The cypress is quite weird: it uses 8-bit ISA registers to control
 * DMA modes.
 */

#define CY_DMA_ADDR 0x22
#define CY_DMA_SIZE 0x2

#define CY_DMA_IDX 0x00
#define CY_DMA_IDX_PRIMARY     0x30
#define CY_DMA_IDX_SECONDARY   0x31
#define CY_DMA_IDX_TIMEOUT     0x32

#define CY_DMA_DATA 0x01
/* Multiword DMA transfer, for CY_DMA_IDX_PRIMARY or CY_DMA_IDX_SECONDARY */
#define CY_DMA_DATA_MODE_MASK  0x03
#define CY_DMA_DATA_SINGLE     0x04

/* Private data */
struct pciide_cy {
	const struct cy82c693_handle *cy_handle;
	int cy_compatchan;
};

#endif	/* !_DEV_PCI_PCIIDE_CY693_REG_H_ */
