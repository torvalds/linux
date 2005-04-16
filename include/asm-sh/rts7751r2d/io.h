/*
 * include/asm-sh/io_rts7751r2d.h
 *
 * Modified version of io_se.h for the rts7751r2d-specific functions.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Renesas Technology sales RTS7751R2D
 */

#ifndef _ASM_SH_IO_RTS7751R2D_H
#define _ASM_SH_IO_RTS7751R2D_H

extern unsigned char rts7751r2d_inb(unsigned long port);
extern unsigned short rts7751r2d_inw(unsigned long port);
extern unsigned int rts7751r2d_inl(unsigned long port);

extern void rts7751r2d_outb(unsigned char value, unsigned long port);
extern void rts7751r2d_outw(unsigned short value, unsigned long port);
extern void rts7751r2d_outl(unsigned int value, unsigned long port);

extern unsigned char rts7751r2d_inb_p(unsigned long port);
extern void rts7751r2d_outb_p(unsigned char value, unsigned long port);

extern void rts7751r2d_insb(unsigned long port, void *addr, unsigned long count);
extern void rts7751r2d_insw(unsigned long port, void *addr, unsigned long count);
extern void rts7751r2d_insl(unsigned long port, void *addr, unsigned long count);
extern void rts7751r2d_outsb(unsigned long port, const void *addr, unsigned long count);
extern void rts7751r2d_outsw(unsigned long port, const void *addr, unsigned long count);
extern void rts7751r2d_outsl(unsigned long port, const void *addr, unsigned long count);

extern void *rts7751r2d_ioremap(unsigned long offset, unsigned long size);

extern unsigned long rts7751r2d_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_RTS7751R2D_H */
