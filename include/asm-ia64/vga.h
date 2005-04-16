/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *	(c) 1999 Asit Mallick <asit.k.mallick@intel.com>
 *	(c) 1999 Don Dugger <don.dugger@intel.com>
 */

#ifndef __ASM_IA64_VGA_H_
#define __ASM_IA64_VGA_H_

/*
 * On the PC, we can just recalculate addresses and then access the
 * videoram directly without any black magic.
 */

#define VGA_MAP_MEM(x)	((unsigned long) ioremap((x), 0))

#define vga_readb(x)	(*(x))
#define vga_writeb(x,y)	(*(y) = (x))

#endif /* __ASM_IA64_VGA_H_ */
