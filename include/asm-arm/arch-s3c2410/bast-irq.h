/* linux/include/asm-arm/arch-s3c2410/bast-irq.h
 *
 * (c) 2003,2004 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * Machine BAST - IRQ Number definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  14-Sep-2004 BJD  Fixed IRQ_USBOC definition
 *  06-Jan-2003 BJD  Linux 2.6.0 version
 */

#ifndef __ASM_ARCH_BASTIRQ_H
#define __ASM_ARCH_BASTIRQ_H

/* irq numbers to onboard peripherals */

#define IRQ_USBOC      IRQ_EINT18
#define IRQ_IDE0       IRQ_EINT16
#define IRQ_IDE1       IRQ_EINT17
#define IRQ_PCSERIAL1  IRQ_EINT15
#define IRQ_PCSERIAL2  IRQ_EINT14
#define IRQ_PCPARALLEL IRQ_EINT13
#define IRQ_ASIX       IRQ_EINT11
#define IRQ_DM9000     IRQ_EINT10
#define IRQ_ISA	       IRQ_EINT9
#define IRQ_SMALERT    IRQ_EINT8

#endif /* __ASM_ARCH_BASTIRQ_H */
