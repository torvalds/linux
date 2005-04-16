/*
 * include/asm-sh/io_hd64465.h
 *
 * By Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc.
 *
 * Derived from io_hd64461.h, which bore the message:
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an HD64465 "Windows CE Intelligent Peripheral Controller".
 */

#ifndef _ASM_SH_IO_HD64465_H
#define _ASM_SH_IO_HD64465_H

extern unsigned char hd64465_inb(unsigned long port);
extern unsigned short hd64465_inw(unsigned long port);
extern unsigned int hd64465_inl(unsigned long port);

extern void hd64465_outb(unsigned char value, unsigned long port);
extern void hd64465_outw(unsigned short value, unsigned long port);
extern void hd64465_outl(unsigned int value, unsigned long port);

extern unsigned char hd64465_inb_p(unsigned long port);
extern void hd64465_outb_p(unsigned char value, unsigned long port);

extern unsigned long hd64465_isa_port2addr(unsigned long offset);
extern int hd64465_irq_demux(int irq);
/* Provision for generic secondary demux step -- used by PCMCIA code */
extern void hd64465_register_irq_demux(int irq,
		int (*demux)(int irq, void *dev), void *dev);
extern void hd64465_unregister_irq_demux(int irq);
/* Set this variable to 1 to see port traffic */
extern int hd64465_io_debug;
/* Map a range of ports to a range of kernel virtual memory.
 */
extern void hd64465_port_map(unsigned short baseport, unsigned int nports,
			     unsigned long addr, unsigned char shift);
extern void hd64465_port_unmap(unsigned short baseport, unsigned int nports);

#endif /* _ASM_SH_IO_HD64465_H */
