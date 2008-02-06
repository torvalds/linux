/*
 * include/asm-v850/io.h -- Misc I/O operations
 *
 *  Copyright (C) 2001,02,03,04,05  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03,04,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_IO_H__
#define __V850_IO_H__

#define IO_SPACE_LIMIT 0xFFFFFFFF

#define readb(addr) \
  ({ unsigned char __v = (*(volatile unsigned char *) (addr)); __v; })
#define readw(addr) \
  ({ unsigned short __v = (*(volatile unsigned short *) (addr)); __v; })
#define readl(addr) \
  ({ unsigned long __v = (*(volatile unsigned long *) (addr)); __v; })

#define readb_relaxed(a) readb(a)
#define readw_relaxed(a) readw(a)
#define readl_relaxed(a) readl(a)

#define writeb(val, addr) \
  (void)((*(volatile unsigned char *) (addr)) = (val))
#define writew(val, addr) \
  (void)((*(volatile unsigned short *) (addr)) = (val))
#define writel(val, addr) \
  (void)((*(volatile unsigned int *) (addr)) = (val))

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#define inb(addr)	readb (addr)
#define inw(addr)	readw (addr)
#define inl(addr)	readl (addr)
#define outb(x, addr)	((void) writeb (x, addr))
#define outw(x, addr)	((void) writew (x, addr))
#define outl(x, addr)	((void) writel (x, addr))

#define inb_p(port)		inb((port))
#define outb_p(val, port)	outb((val), (port))
#define inw_p(port)		inw((port))
#define outw_p(val, port)	outw((val), (port))
#define inl_p(port)		inl((port))
#define outl_p(val, port)	outl((val), (port))

static inline void insb (unsigned long port, void *dst, unsigned long count)
{
	unsigned char *p = dst;
	while (count--)
		*p++ = inb (port);
}
static inline void insw (unsigned long port, void *dst, unsigned long count)
{
	unsigned short *p = dst;
	while (count--)
		*p++ = inw (port);
}
static inline void insl (unsigned long port, void *dst, unsigned long count)
{
	unsigned long *p = dst;
	while (count--)
		*p++ = inl (port);
}

static inline void
outsb (unsigned long port, const void *src, unsigned long count)
{
	const unsigned char *p = src;
	while (count--)
		outb (*p++, port);
}
static inline void
outsw (unsigned long port, const void *src, unsigned long count)
{
	const unsigned short *p = src;
	while (count--)
		outw (*p++, port);
}
static inline void
outsl (unsigned long port, const void *src, unsigned long count)
{
	const unsigned long *p = src;
	while (count--)
		outl (*p++, port);
}


/* Some places try to pass in an loff_t for PHYSADDR (?!), so we cast it to
   long before casting it to a pointer to avoid compiler warnings.  */
#define ioremap(physaddr, size)	((void __iomem *)(unsigned long)(physaddr))
#define iounmap(addr)		((void)0)

#define ioremap_nocache(physaddr, size)		ioremap (physaddr, size)
#define ioremap_writethrough(physaddr, size)	ioremap (physaddr, size)
#define ioremap_fullcache(physaddr, size)	ioremap (physaddr, size)

#define ioread8(addr)		readb (addr)
#define ioread16(addr)		readw (addr)
#define ioread32(addr)		readl (addr)
#define iowrite8(val, addr)	writeb (val, addr)
#define iowrite16(val, addr)	writew (val, addr)
#define iowrite32(val, addr)	writel (val, addr)

#define mmiowb()

#define page_to_phys(page)      ((page - mem_map) << PAGE_SHIFT)
#if 0
/* This is really stupid; don't define it.  */
#define page_to_bus(page)       page_to_phys (page)
#endif

/* Conversion between virtual and physical mappings.  */
#define phys_to_virt(addr)	((void *)__phys_to_virt (addr))
#define virt_to_phys(addr)	((unsigned long)__virt_to_phys (addr))

#define memcpy_fromio(dst, src, len) memcpy (dst, (void *)src, len)
#define memcpy_toio(dst, src, len) memcpy ((void *)dst, src, len)

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* __V850_IO_H__ */
