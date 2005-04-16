/*
 * include/asm-sh/systemh/io.h
 *
 * Stupid I/O definitions for SystemH, cloned from SE7751.
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_SYSTEMH_IO_H
#define __ASM_SH_SYSTEMH_IO_H

extern unsigned char sh7751systemh_inb(unsigned long port);
extern unsigned short sh7751systemh_inw(unsigned long port);
extern unsigned int sh7751systemh_inl(unsigned long port);

extern void sh7751systemh_outb(unsigned char value, unsigned long port);
extern void sh7751systemh_outw(unsigned short value, unsigned long port);
extern void sh7751systemh_outl(unsigned int value, unsigned long port);

extern unsigned char sh7751systemh_inb_p(unsigned long port);
extern void sh7751systemh_outb_p(unsigned char value, unsigned long port);

extern void sh7751systemh_insb(unsigned long port, void *addr, unsigned long count);
extern void sh7751systemh_insw(unsigned long port, void *addr, unsigned long count);
extern void sh7751systemh_insl(unsigned long port, void *addr, unsigned long count);
extern void sh7751systemh_outsb(unsigned long port, const void *addr, unsigned long count);
extern void sh7751systemh_outsw(unsigned long port, const void *addr, unsigned long count);
extern void sh7751systemh_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned char sh7751systemh_readb(unsigned long addr);
extern unsigned short sh7751systemh_readw(unsigned long addr);
extern unsigned int sh7751systemh_readl(unsigned long addr);
extern void sh7751systemh_writeb(unsigned char b, unsigned long addr);
extern void sh7751systemh_writew(unsigned short b, unsigned long addr);
extern void sh7751systemh_writel(unsigned int b, unsigned long addr);

extern unsigned long sh7751systemh_isa_port2addr(unsigned long offset);

#endif /* __ASM_SH_SYSTEMH_IO_H */

