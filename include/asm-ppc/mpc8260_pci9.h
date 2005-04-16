/* include/asm-ppc/mpc8260_pci9.h
 *
 * Undefine the PCI read* and in* macros so we can define them as functions
 * that implement the workaround for the MPC8260 device erratum PCI 9.
 *
 * This header file should only be included at the end of include/asm-ppc/io.h
 * and never included directly anywhere else.
 *
 * Author:  andy_lowe@mvista.com
 *
 * 2003 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _PPC_IO_H
#error "Do not include mpc8260_pci9.h directly."
#endif

#ifdef __KERNEL__
#ifndef __CONFIG_8260_PCI9_DEFS
#define __CONFIG_8260_PCI9_DEFS

#undef readb
#undef readw
#undef readl
#undef insb
#undef insw
#undef insl
#undef inb
#undef inw
#undef inl
#undef insw_ns
#undef insl_ns
#undef memcpy_fromio

extern int readb(volatile unsigned char *addr);
extern int readw(volatile unsigned short *addr);
extern unsigned readl(volatile unsigned *addr);
extern void insb(unsigned port, void *buf, int ns);
extern void insw(unsigned port, void *buf, int ns);
extern void insl(unsigned port, void *buf, int nl);
extern int inb(unsigned port);
extern int inw(unsigned port);
extern unsigned inl(unsigned port);
extern void insw_ns(unsigned port, void *buf, int ns);
extern void insl_ns(unsigned port, void *buf, int nl);
extern void *memcpy_fromio(void *dest, unsigned long src, size_t count);

#endif /* !__CONFIG_8260_PCI9_DEFS */
#endif /* __KERNEL__ */
