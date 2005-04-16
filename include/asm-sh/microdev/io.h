/*
 * linux/include/asm-sh/io_microdev.h
 *
 * Copyright (C) 2003 Sean McGoogan (Sean.McGoogan@superh.com)
 *
 * IO functions for the SuperH SH4-202 MicroDev board.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 */


#ifndef _ASM_SH_IO_MICRODEV_H
#define _ASM_SH_IO_MICRODEV_H

extern unsigned long microdev_isa_port2addr(unsigned long offset);

extern unsigned char microdev_inb(unsigned long port);
extern unsigned short microdev_inw(unsigned long port);
extern unsigned int microdev_inl(unsigned long port);

extern void microdev_outb(unsigned char value, unsigned long port);
extern void microdev_outw(unsigned short value, unsigned long port);
extern void microdev_outl(unsigned int value, unsigned long port);

extern unsigned char microdev_inb_p(unsigned long port);
extern unsigned short microdev_inw_p(unsigned long port);
extern unsigned int microdev_inl_p(unsigned long port);

extern void microdev_outb_p(unsigned char value, unsigned long port);
extern void microdev_outw_p(unsigned short value, unsigned long port);
extern void microdev_outl_p(unsigned int value, unsigned long port);

extern void microdev_insb(unsigned long port, void *addr, unsigned long count);
extern void microdev_insw(unsigned long port, void *addr, unsigned long count);
extern void microdev_insl(unsigned long port, void *addr, unsigned long count);

extern void microdev_outsb(unsigned long port, const void *addr, unsigned long count);
extern void microdev_outsw(unsigned long port, const void *addr, unsigned long count);
extern void microdev_outsl(unsigned long port, const void *addr, unsigned long count);

#if defined(CONFIG_PCI)
extern unsigned char  microdev_pci_inb(unsigned long port);
extern unsigned short microdev_pci_inw(unsigned long port);
extern unsigned long  microdev_pci_inl(unsigned long port);
extern void           microdev_pci_outb(unsigned char  data, unsigned long port);
extern void           microdev_pci_outw(unsigned short data, unsigned long port);
extern void           microdev_pci_outl(unsigned long  data, unsigned long port);
#endif

#endif /* _ASM_SH_IO_MICRODEV_H */

