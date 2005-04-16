/*
 * include/asm-sh/hs7751rvoip/hs7751rvoip.h
 *
 * Modified version of io_se.h for the hs7751rvoip-specific functions.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an Renesas Technology sales HS7751RVOIP
 */

#ifndef _ASM_SH_IO_HS7751RVOIP_H
#define _ASM_SH_IO_HS7751RVOIP_H

#include <asm/io_generic.h>

extern unsigned char hs7751rvoip_inb(unsigned long port);
extern unsigned short hs7751rvoip_inw(unsigned long port);
extern unsigned int hs7751rvoip_inl(unsigned long port);

extern void hs7751rvoip_outb(unsigned char value, unsigned long port);
extern void hs7751rvoip_outw(unsigned short value, unsigned long port);
extern void hs7751rvoip_outl(unsigned int value, unsigned long port);

extern unsigned char hs7751rvoip_inb_p(unsigned long port);
extern void hs7751rvoip_outb_p(unsigned char value, unsigned long port);

extern void hs7751rvoip_insb(unsigned long port, void *addr, unsigned long count);
extern void hs7751rvoip_insw(unsigned long port, void *addr, unsigned long count);
extern void hs7751rvoip_insl(unsigned long port, void *addr, unsigned long count);
extern void hs7751rvoip_outsb(unsigned long port, const void *addr, unsigned long count);
extern void hs7751rvoip_outsw(unsigned long port, const void *addr, unsigned long count);
extern void hs7751rvoip_outsl(unsigned long port, const void *addr, unsigned long count);

extern void *hs7751rvoip_ioremap(unsigned long offset, unsigned long size);

extern unsigned long hs7751rvoip_isa_port2addr(unsigned long offset);

#endif /* _ASM_SH_IO_HS7751RVOIP_H */
