/* io.h: FRV I/O operations
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This gets interesting when talking to the PCI bus - the CPU is in big endian
 * mode, the PCI bus is little endian and the hardware in the middle can do
 * byte swapping
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/virtconvert.h>
#include <asm/string.h>
#include <asm/mb-regs.h>
#include <linux/delay.h>

/*
 * swap functions are sometimes needed to interface little-endian hardware
 */

static inline unsigned short _swapw(unsigned short v)
{
    return ((v << 8) | (v >> 8));
}

static inline unsigned long _swapl(unsigned long v)
{
    return ((v << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | (v >> 24));
}

//#define __iormb() asm volatile("membar")
//#define __iowmb() asm volatile("membar")

#define __raw_readb(addr) __builtin_read8((void *) (addr))
#define __raw_readw(addr) __builtin_read16((void *) (addr))
#define __raw_readl(addr) __builtin_read32((void *) (addr))

#define __raw_writeb(datum, addr) __builtin_write8((void *) (addr), datum)
#define __raw_writew(datum, addr) __builtin_write16((void *) (addr), datum)
#define __raw_writel(datum, addr) __builtin_write32((void *) (addr), datum)

static inline void io_outsb(unsigned int addr, const void *buf, int len)
{
	unsigned long __ioaddr = (unsigned long) addr;
	const uint8_t *bp = buf;

	while (len--)
		__builtin_write8((volatile void __iomem *) __ioaddr, *bp++);
}

static inline void io_outsw(unsigned int addr, const void *buf, int len)
{
	unsigned long __ioaddr = (unsigned long) addr;
	const uint16_t *bp = buf;

	while (len--)
		__builtin_write16((volatile void __iomem *) __ioaddr, (*bp++));
}

extern void __outsl_ns(unsigned int addr, const void *buf, int len);
extern void __outsl_sw(unsigned int addr, const void *buf, int len);
static inline void __outsl(unsigned int addr, const void *buf, int len, int swap)
{
	unsigned long __ioaddr = (unsigned long) addr;

	if (!swap)
		__outsl_ns(__ioaddr, buf, len);
	else
		__outsl_sw(__ioaddr, buf, len);
}

static inline void io_insb(unsigned long addr, void *buf, int len)
{
	uint8_t *bp = buf;

	while (len--)
		*bp++ = __builtin_read8((volatile void __iomem *) addr);
}

static inline void io_insw(unsigned long addr, void *buf, int len)
{
	uint16_t *bp = buf;

	while (len--)
		*bp++ = __builtin_read16((volatile void __iomem *) addr);
}

extern void __insl_ns(unsigned long addr, void *buf, int len);
extern void __insl_sw(unsigned long addr, void *buf, int len);
static inline void __insl(unsigned long addr, void *buf, int len, int swap)
{
	if (!swap)
		__insl_ns(addr, buf, len);
	else
		__insl_sw(addr, buf, len);
}

/*
 *	make the short names macros so specific devices
 *	can override them as required
 */

static inline void memset_io(volatile void __iomem *addr, unsigned char val, int count)
{
	memset((void __force *) addr, val, count);
}

static inline void memcpy_fromio(void *dst, volatile void __iomem *src, int count)
{
	memcpy(dst, (void __force *) src, count);
}

static inline void memcpy_toio(volatile void __iomem *dst, const void *src, int count)
{
	memcpy((void __force *) dst, src, count);
}

static inline uint8_t inb(unsigned long addr)
{
	return __builtin_read8((void *)addr);
}

static inline uint16_t inw(unsigned long addr)
{
	uint16_t ret = __builtin_read16((void *)addr);

	if (__is_PCI_IO(addr))
		ret = _swapw(ret);

	return ret;
}

static inline uint32_t inl(unsigned long addr)
{
	uint32_t ret = __builtin_read32((void *)addr);

	if (__is_PCI_IO(addr))
		ret = _swapl(ret);

	return ret;
}

static inline void outb(uint8_t datum, unsigned long addr)
{
	__builtin_write8((void *)addr, datum);
}

static inline void outw(uint16_t datum, unsigned long addr)
{
	if (__is_PCI_IO(addr))
		datum = _swapw(datum);
	__builtin_write16((void *)addr, datum);
}

static inline void outl(uint32_t datum, unsigned long addr)
{
	if (__is_PCI_IO(addr))
		datum = _swapl(datum);
	__builtin_write32((void *)addr, datum);
}

#define inb_p(addr)	inb(addr)
#define inw_p(addr)	inw(addr)
#define inl_p(addr)	inl(addr)
#define outb_p(x,addr)	outb(x,addr)
#define outw_p(x,addr)	outw(x,addr)
#define outl_p(x,addr)	outl(x,addr)

#define outsb(a,b,l)	io_outsb(a,b,l)
#define outsw(a,b,l)	io_outsw(a,b,l)
#define outsl(a,b,l)	__outsl(a,b,l,0)

#define insb(a,b,l)	io_insb(a,b,l)
#define insw(a,b,l)	io_insw(a,b,l)
#define insl(a,b,l)	__insl(a,b,l,0)

#define IO_SPACE_LIMIT	0xffffffff

static inline uint8_t readb(const volatile void __iomem *addr)
{
	return __builtin_read8((volatile uint8_t __force *) addr);
}

static inline uint16_t readw(const volatile void __iomem *addr)
{
	uint16_t ret =	__builtin_read16((volatile uint16_t __force *)addr);

	if (__is_PCI_MEM(addr))
		ret = _swapw(ret);
	return ret;
}

static inline uint32_t readl(const volatile void __iomem *addr)
{
	uint32_t ret =	__builtin_read32((volatile uint32_t __force *)addr);

	if (__is_PCI_MEM(addr))
		ret = _swapl(ret);

	return ret;
}

static inline void writeb(uint8_t datum, volatile void __iomem *addr)
{
	__builtin_write8((volatile uint8_t __force *) addr, datum);
	if (__is_PCI_MEM(addr))
		__flush_PCI_writes();
}

static inline void writew(uint16_t datum, volatile void __iomem *addr)
{
	if (__is_PCI_MEM(addr))
		datum = _swapw(datum);

	__builtin_write16((volatile uint16_t __force *) addr, datum);
	if (__is_PCI_MEM(addr))
		__flush_PCI_writes();
}

static inline void writel(uint32_t datum, volatile void __iomem *addr)
{
	if (__is_PCI_MEM(addr))
		datum = _swapl(datum);

	__builtin_write32((volatile uint32_t __force *) addr, datum);
	if (__is_PCI_MEM(addr))
		__flush_PCI_writes();
}


/* Values for nocacheflag and cmode */
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3

extern void __iomem *__ioremap(unsigned long physaddr, unsigned long size, int cacheflag);
extern void __iounmap(void __iomem *addr, unsigned long size);

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

static inline void __iomem *ioremap_nocache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

static inline void __iomem *ioremap_writethrough(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}

static inline void __iomem *ioremap_fullcache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

extern void iounmap(void __iomem *addr);

static inline void flush_write_buffers(void)
{
	__asm__ __volatile__ ("membar" : : :"memory");
}


/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* __KERNEL__ */

#endif /* _ASM_IO_H */
