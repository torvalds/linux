/*
 * include/asm-sh/io_adx.h
 *
 * Copyright (C) 2001 A&D Co., Ltd.
 *
 * This file may be copied or modified under the terms of the GNU
 * General Public License.  See linux/COPYING for more information.
 *
 * IO functions for an A&D ADX Board
 */

#ifndef _ASM_SH_IO_ADX_H
#define _ASM_SH_IO_ADX_H

#include <asm/io_generic.h>

extern unsigned char adx_inb(unsigned long port);
extern unsigned short adx_inw(unsigned long port);
extern unsigned int adx_inl(unsigned long port);

extern void adx_outb(unsigned char value, unsigned long port);
extern void adx_outw(unsigned short value, unsigned long port);
extern void adx_outl(unsigned int value, unsigned long port);

extern unsigned char adx_inb_p(unsigned long port);
extern void adx_outb_p(unsigned char value, unsigned long port);

extern void adx_insb(unsigned long port, void *addr, unsigned long count);
extern void adx_insw(unsigned long port, void *addr, unsigned long count);
extern void adx_insl(unsigned long port, void *addr, unsigned long count);
extern void adx_outsb(unsigned long port, const void *addr, unsigned long count);
extern void adx_outsw(unsigned long port, const void *addr, unsigned long count);
extern void adx_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned char adx_readb(unsigned long addr);
extern unsigned short adx_readw(unsigned long addr);
extern unsigned int adx_readl(unsigned long addr);
extern void adx_writeb(unsigned char b, unsigned long addr);
extern void adx_writew(unsigned short b, unsigned long addr);
extern void adx_writel(unsigned int b, unsigned long addr);

extern void * adx_ioremap(unsigned long offset, unsigned long size);
extern void adx_iounmap(void *addr);

extern unsigned long adx_isa_port2addr(unsigned long offset);

extern void setup_adx(void);
extern void init_adx_IRQ(void);

#ifdef __WANT_IO_DEF

#define __inb		adx_inb
#define __inw		adx_inw
#define __inl		adx_inl
#define __outb		adx_outb
#define __outw		adx_outw
#define __outl		adx_outl

#define __inb_p		adx_inb_p
#define __inw_p		adx_inw
#define __inl_p		adx_inl
#define __outb_p	adx_outb_p
#define __outw_p	adx_outw
#define __outl_p	adx_outl

#define __insb		adx_insb
#define __insw		adx_insw
#define __insl		adx_insl
#define __outsb		adx_outsb
#define __outsw		adx_outsw
#define __outsl		adx_outsl

#define __readb		adx_readb
#define __readw		adx_readw
#define __readl		adx_readl
#define __writeb	adx_writeb
#define __writew	adx_writew
#define __writel	adx_writel

#define __isa_port2addr	adx_isa_port2addr
#define __ioremap	adx_ioremap
#define __iounmap	adx_iounmap

#endif

#endif /* _ASM_SH_IO_AANDD_H */
