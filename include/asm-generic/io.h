/* Generic I/O port emulation, based on MN10300 code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef __ASM_GENERIC_IO_H
#define __ASM_GENERIC_IO_H

#include <asm/page.h> /* I/O is all done through memory accesses */
#include <linux/types.h>

#ifdef CONFIG_GENERIC_IOMAP
#include <asm-generic/iomap.h>
#endif

#include <asm-generic/pci_iomap.h>

#ifndef mmiowb
#define mmiowb() do {} while (0)
#endif

/*****************************************************************************/
/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the simple architectures, we just read/write the
 * memory location directly.
 */
#ifndef __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 __force *) addr;
}
#endif

#ifndef __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 __force *) addr;
}
#endif

#ifndef __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 __force *) addr;
}
#endif

#define readb __raw_readb

#define readw readw
static inline u16 readw(const volatile void __iomem *addr)
{
	return __le16_to_cpu(__raw_readw(addr));
}

#define readl readl
static inline u32 readl(const volatile void __iomem *addr)
{
	return __le32_to_cpu(__raw_readl(addr));
}

#ifndef __raw_writeb
static inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	*(volatile u8 __force *) addr = b;
}
#endif

#ifndef __raw_writew
static inline void __raw_writew(u16 b, volatile void __iomem *addr)
{
	*(volatile u16 __force *) addr = b;
}
#endif

#ifndef __raw_writel
static inline void __raw_writel(u32 b, volatile void __iomem *addr)
{
	*(volatile u32 __force *) addr = b;
}
#endif

#define writeb __raw_writeb
#define writew(b,addr) __raw_writew(__cpu_to_le16(b),addr)
#define writel(b,addr) __raw_writel(__cpu_to_le32(b),addr)

#ifdef CONFIG_64BIT
#ifndef __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	return *(const volatile u64 __force *) addr;
}
#endif

#define readq readq
static inline u64 readq(const volatile void __iomem *addr)
{
	return __le64_to_cpu(__raw_readq(addr));
}

#ifndef __raw_writeq
static inline void __raw_writeq(u64 b, volatile void __iomem *addr)
{
	*(volatile u64 __force *) addr = b;
}
#endif

#define writeq(b, addr) __raw_writeq(__cpu_to_le64(b), addr)
#endif /* CONFIG_64BIT */

#ifndef PCI_IOBASE
#define PCI_IOBASE ((void __iomem *) 0)
#endif

/*****************************************************************************/
/*
 * traditional input/output functions
 */

static inline u8 inb(unsigned long addr)
{
	return readb(addr + PCI_IOBASE);
}

static inline u16 inw(unsigned long addr)
{
	return readw(addr + PCI_IOBASE);
}

static inline u32 inl(unsigned long addr)
{
	return readl(addr + PCI_IOBASE);
}

static inline void outb(u8 b, unsigned long addr)
{
	writeb(b, addr + PCI_IOBASE);
}

static inline void outw(u16 b, unsigned long addr)
{
	writew(b, addr + PCI_IOBASE);
}

static inline void outl(u32 b, unsigned long addr)
{
	writel(b, addr + PCI_IOBASE);
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x, addr)	outb((x), (addr))
#define outw_p(x, addr)	outw((x), (addr))
#define outl_p(x, addr)	outl((x), (addr))

#ifndef insb
static inline void insb(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u8 *buf = buffer;
		do {
			u8 x = __raw_readb(addr + PCI_IOBASE);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifndef insw
static inline void insw(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u16 *buf = buffer;
		do {
			u16 x = __raw_readw(addr + PCI_IOBASE);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifndef insl
static inline void insl(unsigned long addr, void *buffer, int count)
{
	if (count) {
		u32 *buf = buffer;
		do {
			u32 x = __raw_readl(addr + PCI_IOBASE);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifndef outsb
static inline void outsb(unsigned long addr, const void *buffer, int count)
{
	if (count) {
		const u8 *buf = buffer;
		do {
			__raw_writeb(*buf++, addr + PCI_IOBASE);
		} while (--count);
	}
}
#endif

#ifndef outsw
static inline void outsw(unsigned long addr, const void *buffer, int count)
{
	if (count) {
		const u16 *buf = buffer;
		do {
			__raw_writew(*buf++, addr + PCI_IOBASE);
		} while (--count);
	}
}
#endif

#ifndef outsl
static inline void outsl(unsigned long addr, const void *buffer, int count)
{
	if (count) {
		const u32 *buf = buffer;
		do {
			__raw_writel(*buf++, addr + PCI_IOBASE);
		} while (--count);
	}
}
#endif

#ifndef CONFIG_GENERIC_IOMAP
#define ioread8(addr)		readb(addr)
#define ioread16(addr)		readw(addr)
#define ioread16be(addr)	__be16_to_cpu(__raw_readw(addr))
#define ioread32(addr)		readl(addr)
#define ioread32be(addr)	__be32_to_cpu(__raw_readl(addr))

#define iowrite8(v, addr)	writeb((v), (addr))
#define iowrite16(v, addr)	writew((v), (addr))
#define iowrite16be(v, addr)	__raw_writew(__cpu_to_be16(v), addr)
#define iowrite32(v, addr)	writel((v), (addr))
#define iowrite32be(v, addr)	__raw_writel(__cpu_to_be32(v), addr)

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
#endif /* CONFIG_GENERIC_IOMAP */

#ifndef IO_SPACE_LIMIT
#define IO_SPACE_LIMIT 0xffff
#endif

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#define __io_virt(x) ((void __force *) (x))

#ifndef CONFIG_GENERIC_IOMAP
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);

#ifndef pci_iounmap
static inline void pci_iounmap(struct pci_dev *dev, void __iomem *p)
{
}
#endif
#endif /* CONFIG_GENERIC_IOMAP */

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
#ifndef virt_to_phys
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa((unsigned long)address);
}

static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}
#endif

/*
 * Change "struct page" to physical address.
 *
 * This implementation is for the no-MMU case only... if you have an MMU
 * you'll need to provide your own definitions.
 */
#ifndef CONFIG_MMU
static inline void __iomem *ioremap(phys_addr_t offset, unsigned long size)
{
	return (void __iomem*) (unsigned long)offset;
}

#define __ioremap(offset, size, flags)	ioremap(offset, size)

#ifndef ioremap_nocache
#define ioremap_nocache ioremap
#endif

#ifndef ioremap_wc
#define ioremap_wc ioremap_nocache
#endif

static inline void iounmap(void __iomem *addr)
{
}
#endif /* CONFIG_MMU */

#ifdef CONFIG_HAS_IOPORT_MAP
#ifndef CONFIG_GENERIC_IOMAP
static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return (void __iomem *) port;
}

static inline void ioport_unmap(void __iomem *p)
{
}
#else /* CONFIG_GENERIC_IOMAP */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *p);
#endif /* CONFIG_GENERIC_IOMAP */
#endif /* CONFIG_HAS_IOPORT_MAP */

#ifndef xlate_dev_kmem_ptr
#define xlate_dev_kmem_ptr(p)	p
#endif
#ifndef xlate_dev_mem_ptr
#define xlate_dev_mem_ptr(p)	__va(p)
#endif

#ifdef CONFIG_VIRT_TO_BUS
#ifndef virt_to_bus
static inline unsigned long virt_to_bus(volatile void *address)
{
	return ((unsigned long) address);
}

static inline void *bus_to_virt(unsigned long address)
{
	return (void *) address;
}
#endif
#endif

#ifndef memset_io
#define memset_io(a, b, c)	memset(__io_virt(a), (b), (c))
#endif

#ifndef memcpy_fromio
#define memcpy_fromio(a, b, c)	memcpy((a), __io_virt(b), (c))
#endif
#ifndef memcpy_toio
#define memcpy_toio(a, b, c)	memcpy(__io_virt(a), (b), (c))
#endif

#endif /* __KERNEL__ */

#endif /* __ASM_GENERIC_IO_H */
