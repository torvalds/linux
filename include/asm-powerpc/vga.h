#ifndef _ASM_POWERPC_VGA_H_
#define _ASM_POWERPC_VGA_H_

#ifdef __KERNEL__

/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */


#include <asm/io.h>


#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_MDA_CONSOLE)

#define VT_BUF_HAVE_RW
/*
 *  These are only needed for supporting VGA or MDA text mode, which use little
 *  endian byte ordering.
 *  In other cases, we can optimize by using native byte ordering and
 *  <linux/vt_buffer.h> has already done the right job for us.
 */

static inline void scr_writew(u16 val, volatile u16 *addr)
{
    st_le16(addr, val);
}

static inline u16 scr_readw(volatile const u16 *addr)
{
    return ld_le16(addr);
}

#define VT_BUF_HAVE_MEMCPYW
#define scr_memcpyw	memcpy

#endif /* !CONFIG_VGA_CONSOLE && !CONFIG_MDA_CONSOLE */

extern unsigned long vgacon_remap_base;

#ifdef __powerpc64__
#define VGA_MAP_MEM(x) ((unsigned long) ioremap((x), 0))
#else
#define VGA_MAP_MEM(x) (x + vgacon_remap_base)
#endif

#define vga_readb(x) (*(x))
#define vga_writeb(x,y) (*(y) = (x))

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_VGA_H_ */
