/* linux/include/asm-arm/arch-s3c2410/vr1000-irq.h
 *
 * (c) 2003,2004 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * Machine VR1000 - IRQ Number definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  06-Jan-2003 BJD  Linux 2.6.0 version
 *  19-Mar-2004 BJD  Updates for VR1000
 */

#ifndef __ASM_ARCH_VR1000IRQ_H
#define __ASM_ARCH_VR1000IRQ_H

/* irq numbers to onboard peripherals */

#define IRQ_USBOC	     IRQ_EINT19
#define IRQ_IDE0	     IRQ_EINT16
#define IRQ_IDE1	     IRQ_EINT17
#define IRQ_VR1000_SERIAL    IRQ_EINT12
#define IRQ_VR1000_DM9000A   IRQ_EINT10
#define IRQ_VR1000_DM9000N   IRQ_EINT9
#define IRQ_SMALERT	     IRQ_EINT8

#endif /* __ASM_ARCH_VR1000IRQ_H */
