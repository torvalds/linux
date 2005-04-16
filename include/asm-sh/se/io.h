/*
 * include/asm-sh/io_se.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Hitachi SolutionEngine
 */

#ifndef _ASM_SH_IO_SE_H
#define _ASM_SH_IO_SE_H

extern unsigned char se_inb(unsigned long port);
extern unsigned short se_inw(unsigned long port);
extern unsigned int se_inl(unsigned long port);

extern void se_outb(unsigned char value, unsigned long port);
extern void se_outw(unsigned short value, unsigned long port);
extern void se_outl(unsigned int value, unsigned long port);

extern unsigned char se_inb_p(unsigned long port);
extern void se_outb_p(unsigned char value, unsigned long port);

extern void se_insb(unsigned long port, void *addr, unsigned long count);
extern void se_insw(unsigned long port, void *addr, unsigned long count);
extern void se_insl(unsigned long port, void *addr, unsigned long count);
extern void se_outsb(unsigned long port, const void *addr, unsigned long count);
extern void se_outsw(unsigned long port, const void *addr, unsigned long count);
extern void se_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned long se_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_SE_H */
