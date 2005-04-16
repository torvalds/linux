/*
 * include/asm-sh/io_hd64461.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an HD64461
 */

#ifndef _ASM_SH_IO_HD64461_H
#define _ASM_SH_IO_HD64461_H

extern unsigned char hd64461_inb(unsigned long port);
extern unsigned short hd64461_inw(unsigned long port);
extern unsigned int hd64461_inl(unsigned long port);

extern void hd64461_outb(unsigned char value, unsigned long port);
extern void hd64461_outw(unsigned short value, unsigned long port);
extern void hd64461_outl(unsigned int value, unsigned long port);

extern unsigned char hd64461_inb_p(unsigned long port);
extern void hd64461_outb_p(unsigned char value, unsigned long port);

extern void hd64461_insb(unsigned long port, void *addr, unsigned long count);
extern void hd64461_insw(unsigned long port, void *addr, unsigned long count);
extern void hd64461_insl(unsigned long port, void *addr, unsigned long count);

extern void hd64461_outsb(unsigned long port, const void *buffer, unsigned long count);
extern void hd64461_outsw(unsigned long port, const void *buffer, unsigned long count);
extern void hd64461_outsl(unsigned long port, const void *buffer, unsigned long count);

extern unsigned short hd64461_readw(unsigned long addr);
extern void hd64461_writew(unsigned short b, unsigned long addr);


extern int hd64461_irq_demux(int irq);
extern void hd64461_register_irq_demux(int irq,
		int (*demux)(int irq, void *dev), void *dev);
extern void hd64461_unregister_irq_demux(int irq);

#endif /* _ASM_SH_IO_HD64461_H */
