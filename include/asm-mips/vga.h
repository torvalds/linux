/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */
#ifndef _ASM_VGA_H
#define _ASM_VGA_H

#include <asm/byteorder.h>

/*
 *	On the PC, we can just recalculate addresses and then
 *	access the videoram directly without any black magic.
 */

#define VGA_MAP_MEM(x)	(0xb0000000L + (unsigned long)(x))

#define vga_readb(x)	(*(x))
#define vga_writeb(x,y)	(*(y) = (x))

#define VT_BUF_HAVE_RW
/*
 *  These are only needed for supporting VGA or MDA text mode, which use little
 *  endian byte ordering.
 *  In other cases, we can optimize by using native byte ordering and
 *  <linux/vt_buffer.h> has already done the right job for us.
 */

#undef scr_writew
#undef scr_readw

static inline void scr_writew(u16 val, volatile u16 *addr)
{
	*addr = cpu_to_le16(val);
}

static inline u16 scr_readw(volatile const u16 *addr)
{
	return le16_to_cpu(*addr);
}

#define scr_memcpyw(d, s, c) memcpy(d, s, c)
#define scr_memmovew(d, s, c) memmove(d, s, c)
#define VT_BUF_HAVE_MEMCPYW
#define VT_BUF_HAVE_MEMMOVEW

#endif /* _ASM_VGA_H */
