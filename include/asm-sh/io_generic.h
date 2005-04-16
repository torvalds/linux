/*
 * include/asm-sh/io_generic.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Generic IO functions
 */

#ifndef _ASM_SH_IO_GENERIC_H
#define _ASM_SH_IO_GENERIC_H

extern unsigned long generic_io_base;

extern unsigned char generic_inb(unsigned long port);
extern unsigned short generic_inw(unsigned long port);
extern unsigned int generic_inl(unsigned long port);

extern void generic_outb(unsigned char value, unsigned long port);
extern void generic_outw(unsigned short value, unsigned long port);
extern void generic_outl(unsigned int value, unsigned long port);

extern unsigned char generic_inb_p(unsigned long port);
extern unsigned short generic_inw_p(unsigned long port);
extern unsigned int generic_inl_p(unsigned long port);
extern void generic_outb_p(unsigned char value, unsigned long port);
extern void generic_outw_p(unsigned short value, unsigned long port);
extern void generic_outl_p(unsigned int value, unsigned long port);

extern void generic_insb(unsigned long port, void *addr, unsigned long count);
extern void generic_insw(unsigned long port, void *addr, unsigned long count);
extern void generic_insl(unsigned long port, void *addr, unsigned long count);
extern void generic_outsb(unsigned long port, const void *addr, unsigned long count);
extern void generic_outsw(unsigned long port, const void *addr, unsigned long count);
extern void generic_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned char generic_readb(unsigned long addr);
extern unsigned short generic_readw(unsigned long addr);
extern unsigned int generic_readl(unsigned long addr);
extern void generic_writeb(unsigned char b, unsigned long addr);
extern void generic_writew(unsigned short b, unsigned long addr);
extern void generic_writel(unsigned int b, unsigned long addr);

extern void *generic_ioremap(unsigned long offset, unsigned long size);
extern void generic_iounmap(void *addr);

extern unsigned long generic_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_GENERIC_H */
