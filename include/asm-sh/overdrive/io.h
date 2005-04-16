/*
 * include/asm-sh/io_od.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an STMicroelectronics Overdrive
 */

#ifndef _ASM_SH_IO_OD_H
#define _ASM_SH_IO_OD_H

extern unsigned char od_inb(unsigned long port);
extern unsigned short od_inw(unsigned long port);
extern unsigned int od_inl(unsigned long port);

extern void od_outb(unsigned char value, unsigned long port);
extern void od_outw(unsigned short value, unsigned long port);
extern void od_outl(unsigned int value, unsigned long port);

extern unsigned char od_inb_p(unsigned long port);
extern unsigned short od_inw_p(unsigned long port);
extern unsigned int od_inl_p(unsigned long port);
extern void od_outb_p(unsigned char value, unsigned long port);
extern void od_outw_p(unsigned short value, unsigned long port);
extern void od_outl_p(unsigned int value, unsigned long port);

extern void od_insb(unsigned long port, void *addr, unsigned long count);
extern void od_insw(unsigned long port, void *addr, unsigned long count);
extern void od_insl(unsigned long port, void *addr, unsigned long count);
extern void od_outsb(unsigned long port, const void *addr, unsigned long count);
extern void od_outsw(unsigned long port, const void *addr, unsigned long count);
extern void od_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned long od_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_OD_H */
