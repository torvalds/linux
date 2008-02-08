/* MN10300 I/O port emulation and memory-mapped I/O
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <asm/page.h> /* I/O is all done through memory accesses */
#include <asm/cpu-regs.h>
#include <asm/cacheflush.h>

#define mmiowb() do {} while (0)

/*****************************************************************************/
/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 */
static inline u8 readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 *) addr;
}

static inline u16 readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 *) addr;
}

static inline u32 readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 *) addr;
}

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl

#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl

static inline void writeb(u8 b, volatile void __iomem *addr)
{
	*(volatile u8 *) addr = b;
}

static inline void writew(u16 b, volatile void __iomem *addr)
{
	*(volatile u16 *) addr = b;
}

static inline void writel(u32 b, volatile void __iomem *addr)
{
	*(volatile u32 *) addr = b;
}

#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

/*****************************************************************************/
/*
 * traditional input/output functions
 */
static inline u8 inb_local(unsigned long addr)
{
	return readb((volatile void __iomem *) addr);
}

static inline void outb_local(u8 b, unsigned long addr)
{
	return writeb(b, (volatile void __iomem *) addr);
}

static inline u8 inb(unsigned long addr)
{
	return readb((volatile void __iomem *) addr);
}

static inline u16 inw(unsigned long addr)
{
	return readw((volatile void __iomem *) addr);
}

static inline u32 inl(unsigned long addr)
{
	return readl((volatile void __iomem *) addr);
}

static inline void outb(u8 b, unsigned long addr)
{
	return writeb(b, (volatile void __iomem *) addr);
}

static inline void outw(u16 b, unsigned long addr)
{
	return writew(b, (volatile void __iomem *) addr);
}

static inline void outl(u32 b, unsigned long addr)
{
	return writel(b, (volatile void __iomem *) addr);
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x, addr)	outb((x), (addr))
#define outw_p(x, addr)	outw((x), (addr))
#define outl_p(x, addr)	outl((x), (addr))

static inline void insb(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u8 *buf = buffer;
		do {
			u8 x = inb(addr);
			*buf++ = x;
		} while (--count);
	}
}

static inline void insw(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u16 *buf = buffer;
		do {
			u16 x = inw(addr);
			*buf++ = x;
		} while (--count);
	}
}

static inline void insl(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u32 *buf = buffer;
		do {
			u32 x = inl(addr);
			*buf++ = x;
		} while (--count);
	}
}

static inline void outsb(unsigned long addr, const void *buffer, int count)
{
	if (count) {
		const u8 *buf = buffer;
		do {
			outb(*buf++, addr);
		} while (--count);
	}
}

static inline void outsw(unsigned long addr, const void *buffer, int count)
{
	if (count) {
		const u16 *buf = buffer;
		do {
			outw(*buf++, addr);
		} while (--count);
	}
}

extern void __outsl(unsigned long addr, const void *buffer, int count);
static inline void outsl(unsigned long addr, const void *buffer, int count)
{
	if ((unsigned long) buffer & 0x3)
		return __outsl(addr, buffer, count);

	if (count) {
		const u32 *buf = buffer;
		do {
			outl(*buf++, addr);
		} while (--count);
	}
}

#define ioread8(addr)		readb(addr)
#define ioread16(addr)		readw(addr)
#define ioread32(addr)		readl(addr)

#define iowrite8(v, addr)	writeb((v), (addr))
#define iowrite16(v, addr)	writew((v), (addr))
#define iowrite32(v, addr)	writel((v), (addr))

#define ioread8_rep(p, dst, count) \
	insb((unsigned long) (p), (dst), (count))
#define ioread16_rep(p, dst, count) \
	insw((unsigned long) (p), (dst), (count))
#define ioread32_rep(p, dst, count) \
	insl((unsigned long) (p), (dst), (count))

#define iowrite8_rep(p, src, count) \
	outsb((unsigned long) (p), (src), (count))
#define iowrite16_rep(p, src, count) \
	outsw((unsigned long) (p), (src), (count))
#define iowrite32_rep(p, src, count) \
	outsl((unsigned long) (p), (src), (count))


#define IO_SPACE_LIMIT 0xffffffff

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#define __io_virt(x) ((void *) (x))

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
static inline void pci_iounmap(struct pci_dev *dev, void __iomem *p)
{
}

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa(address);
}

static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

/*
 * Change "struct page" to physical address.
 */
static inline void *__ioremap(unsigned long offset, unsigned long size,
			      unsigned long flags)
{
	return (void *) offset;
}

static inline void *ioremap(unsigned long offset, unsigned long size)
{
	return (void *) offset;
}

/*
 * This one maps high address device memory and turns off caching for that
 * area.  it's useful if some control registers are in such an area and write
 * combining or read caching is not desirable:
 */
static inline void *ioremap_nocache(unsigned long offset, unsigned long size)
{
	return (void *) (offset | 0x20000000);
}

static inline void iounmap(void *addr)
{
}

static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return (void __iomem *) port;
}

static inline void ioport_unmap(void __iomem *p)
{
}

#define xlate_dev_kmem_ptr(p)	((void *) (p))
#define xlate_dev_mem_ptr(p)	((void *) (p))

/*
 * PCI bus iomem addresses must be in the region 0x80000000-0x9fffffff
 */
static inline unsigned long virt_to_bus(volatile void *address)
{
	return ((unsigned long) address) & ~0x20000000;
}

static inline void *bus_to_virt(unsigned long address)
{
	return (void *) address;
}

#define page_to_bus page_to_phys

#define memset_io(a, b, c)	memset(__io_virt(a), (b), (c))
#define memcpy_fromio(a, b, c)	memcpy((a), __io_virt(b), (c))
#define memcpy_toio(a, b, c)	memcpy(__io_virt(a), (b), (c))

#endif /* __KERNEL__ */

#endif /* _ASM_IO_H */
