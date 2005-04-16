/*
 * include/asm-sh/io_7751se.h
 *
 * Modified version of io_se.h for the 7751se-specific functions.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Hitachi SolutionEngine
 */

#ifndef _ASM_SH_IO_7751SE_H
#define _ASM_SH_IO_7751SE_H

extern unsigned char sh7751se_inb(unsigned long port);
extern unsigned short sh7751se_inw(unsigned long port);
extern unsigned int sh7751se_inl(unsigned long port);

extern void sh7751se_outb(unsigned char value, unsigned long port);
extern void sh7751se_outw(unsigned short value, unsigned long port);
extern void sh7751se_outl(unsigned int value, unsigned long port);

extern unsigned char sh7751se_inb_p(unsigned long port);
extern void sh7751se_outb_p(unsigned char value, unsigned long port);

extern void sh7751se_insb(unsigned long port, void *addr, unsigned long count);
extern void sh7751se_insw(unsigned long port, void *addr, unsigned long count);
extern void sh7751se_insl(unsigned long port, void *addr, unsigned long count);
extern void sh7751se_outsb(unsigned long port, const void *addr, unsigned long count);
extern void sh7751se_outsw(unsigned long port, const void *addr, unsigned long count);
extern void sh7751se_outsl(unsigned long port, const void *addr, unsigned long count);

extern unsigned char sh7751se_readb(unsigned long addr);
extern unsigned short sh7751se_readw(unsigned long addr);
extern unsigned int sh7751se_readl(unsigned long addr);
extern void sh7751se_writeb(unsigned char b, unsigned long addr);
extern void sh7751se_writew(unsigned short b, unsigned long addr);
extern void sh7751se_writel(unsigned int b, unsigned long addr);

extern unsigned long sh7751se_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_7751SE_H */
