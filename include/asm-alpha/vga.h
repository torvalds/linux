/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#define VT_BUF_HAVE_RW
#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYW

extern inline void scr_writew(u16 val, volatile u16 *addr)
{
	if (__is_ioaddr(addr))
		__raw_writew(val, (volatile u16 __iomem *) addr);
	else
		*addr = val;
}

extern inline u16 scr_readw(volatile const u16 *addr)
{
	if (__is_ioaddr(addr))
		return __raw_readw((volatile const u16 __iomem *) addr);
	else
		return *addr;
}

extern inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	if (__is_ioaddr(s))
		memsetw_io((u16 __iomem *) s, c, count);
	else
		memsetw(s, c, count);
}

/* Do not trust that the usage will be correct; analyze the arguments.  */
extern void scr_memcpyw(u16 *d, const u16 *s, unsigned int count);

/* ??? These are currently only used for downloading character sets.  As
   such, they don't need memory barriers.  Is this all they are intended
   to be used for?  */
#define vga_readb(a)	readb((u8 __iomem *)(a))
#define vga_writeb(v,a)	writeb(v, (u8 __iomem *)(a))

#define VGA_MAP_MEM(x,s)	((unsigned long) ioremap(x, s))

#endif
