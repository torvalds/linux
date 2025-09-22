/*	$OpenBSD: bonito_irq.h,v 1.2 2010/05/08 21:59:56 miod Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Bonito interrupt assignments
 */

/*
 * Loongson 2F assignments
 */

#define	LOONGSON_INTR_GPIO0		0
#define	LOONGSON_INTR_GPIO1		1
#define	LOONGSON_INTR_GPIO2		2
#define	LOONGSON_INTR_GPIO3		3

/* pci interrupts */
#define	LOONGSON_INTR_PCIA		4
#define	LOONGSON_INTR_PCIB		5
#define	LOONGSON_INTR_PCIC		6
#define	LOONGSON_INTR_PCID		7

#define	LOONGSON_INTR_PCI_PARERR	8
#define	LOONGSON_INTR_PCI_SYSERR	9
#define	LOONGSON_INTR_DRAM_PARERR	10

/* non-PCI interrupts */
#define	LOONGSON_INTR_INT0		11
#define	LOONGSON_INTR_INT1		12
#define	LOONGSON_INTR_INT2		13
#define	LOONGSON_INTR_INT3		14

#define	LOONGSON_INTRMASK_GPIO0		0x00000001	/* can't interrupt */
#define	LOONGSON_INTRMASK_GPIO1		0x00000002
#define	LOONGSON_INTRMASK_GPIO2		0x00000004
#define	LOONGSON_INTRMASK_GPIO3		0x00000008

#define	LOONGSON_INTRMASK_GPIO		0x0000000f

/* pci interrupts */
#define	LOONGSON_INTRMASK_PCIA		0x00000010
#define	LOONGSON_INTRMASK_PCIB		0x00000020
#define	LOONGSON_INTRMASK_PCIC		0x00000040
#define	LOONGSON_INTRMASK_PCID		0x00000080

#define	LOONGSON_INTRMASK_PCI_PARERR	0x00000100
#define	LOONGSON_INTRMASK_PCI_SYSERR	0x00000200
#define	LOONGSON_INTRMASK_DRAM_PARERR	0x00000400

/* non-PCI interrupts */
#define	LOONGSON_INTRMASK_INT0		0x00000800
#define	LOONGSON_INTRMASK_INT1		0x00001000
#define	LOONGSON_INTRMASK_INT2		0x00002000
#define	LOONGSON_INTRMASK_INT3		0x00004000

#define	LOONGSON_INTRMASK_LVL0		0x00007800 /* not maskable in bonito */
#define	LOONGSON_INTRMASK_LVL4		0x000007ff

/*
 * Loongson 2E (Bonito64) assignments
 */

#define	BONITO_INTRMASK_MBOX		0x0000000f
#define	BONITO_INTR_MBOX		0
#define	BONITO_INTRMASK_DMARDY		0x00000010
#define	BONITO_INTRMASK_DMAEMPTY	0x00000020
#define	BONITO_INTRMASK_COPYRDY		0x00000040
#define	BONITO_INTRMASK_COPYEMPTY	0x00000080
#define	BONITO_INTRMASK_COPYERR		0x00000100
#define	BONITO_INTRMASK_PCIIRQ		0x00000200
#define	BONITO_INTRMASK_MASTERERR	0x00000400
#define	BONITO_INTRMASK_SYSTEMERR	0x00000800
#define	BONITO_INTRMASK_DRAMPERR	0x00001000
#define	BONITO_INTRMASK_RETRYERR	0x00002000
#define	BONITO_INTRMASK_GPIO		0x01ff0000
#define	BONITO_INTR_GPIO		16
#define	BONITO_INTRMASK_GPIN		0x7e000000
#define	BONITO_INTR_GPIN		25

/*
 * Bonito interrupt handling recipes:
 * - we have up to 32 interrupts at the Bonito level.
 * - systems with ISA devices also have 16 (well, 15) ISA interrupts with the
 *   usual 8259 pair. Bonito and ISA interrupts happen on two different levels.
 *
 * These arbitrary values may be changed as long as interrupt mask variables
 * use large enough integer types and always use the following macros to
 * handle interrupt masks.
 */

#define	INTPRI_BONITO		(INTPRI_CLOCK + 1)
#define	INTPRI_ISA		(INTPRI_BONITO + 1)

#define	BONITO_NDIRECT		32
#define	BONITO_NISA		16
#define	BONITO_NINTS		(BONITO_NDIRECT + BONITO_NISA)
#define	BONITO_ISA_IRQ(i)	((i) + BONITO_NDIRECT)
#define	BONITO_DIRECT_IRQ(i)	(i)
#define	BONITO_IRQ_IS_ISA(i)	((i) >= BONITO_NDIRECT)
#define	BONITO_IRQ_TO_ISA(i)	((i) - BONITO_NDIRECT)

#define	BONITO_DIRECT_MASK(imask)	((imask) & ((1L << BONITO_NDIRECT) - 1))
#define	BONITO_ISA_MASK(imask)		((imask) >> BONITO_NDIRECT)

extern struct intrhand *bonito_intrhand[BONITO_NINTS];
extern uint64_t bonito_intem;
extern uint64_t bonito_imask[NIPLS];
