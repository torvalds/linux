#ifndef __ASM_SH64_IO_H
#define __ASM_SH64_IO_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/io.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 */

/*
 * Convention:
 *    read{b,w,l}/write{b,w,l} are for PCI,
 *    while in{b,w,l}/out{b,w,l} are for ISA
 * These may (will) be platform specific function.
 *
 * In addition, we have
 *   ctrl_in{b,w,l}/ctrl_out{b,w,l} for SuperH specific I/O.
 * which are processor specific. Address should be the result of
 * onchip_remap();
 */

#include <linux/compiler.h>
#include <asm/cache.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm-generic/iomap.h>

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#define page_to_bus page_to_phys

/*
 * Nothing overly special here.. instead of doing the same thing
 * over and over again, we just define a set of sh64_in/out functions
 * with an implicit size. The traditional read{b,w,l}/write{b,w,l}
 * mess is wrapped to this, as are the SH-specific ctrl_in/out routines.
 */
static inline unsigned char sh64_in8(const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}

static inline unsigned short sh64_in16(const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)addr;
}

static inline unsigned int sh64_in32(const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *)addr;
}

static inline unsigned long long sh64_in64(const volatile void __iomem *addr)
{
	return *(volatile unsigned long long __force *)addr;
}

static inline void sh64_out8(unsigned char b, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)addr = b;
	wmb();
}

static inline void sh64_out16(unsigned short b, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)addr = b;
	wmb();
}

static inline void sh64_out32(unsigned int b, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)addr = b;
	wmb();
}

static inline void sh64_out64(unsigned long long b, volatile void __iomem *addr)
{
	*(volatile unsigned long long __force *)addr = b;
	wmb();
}

#define readb(addr)		sh64_in8(addr)
#define readw(addr)		sh64_in16(addr)
#define readl(addr)		sh64_in32(addr)
#define readb_relaxed(addr)	sh64_in8(addr)
#define readw_relaxed(addr)	sh64_in16(addr)
#define readl_relaxed(addr)	sh64_in32(addr)

#define writeb(b, addr)		sh64_out8(b, addr)
#define writew(b, addr)		sh64_out16(b, addr)
#define writel(b, addr)		sh64_out32(b, addr)

#define ctrl_inb(addr)		sh64_in8(ioport_map(addr, 1))
#define ctrl_inw(addr)		sh64_in16(ioport_map(addr, 2))
#define ctrl_inl(addr)		sh64_in32(ioport_map(addr, 4))

#define ctrl_outb(b, addr)	sh64_out8(b, ioport_map(addr, 1))
#define ctrl_outw(b, addr)	sh64_out16(b, ioport_map(addr, 2))
#define ctrl_outl(b, addr)	sh64_out32(b, ioport_map(addr, 4))

#define ioread8(addr)		sh64_in8(addr)
#define ioread16(addr)		sh64_in16(addr)
#define ioread32(addr)		sh64_in32(addr)
#define iowrite8(b, addr)	sh64_out8(b, addr)
#define iowrite16(b, addr)	sh64_out16(b, addr)
#define iowrite32(b, addr)	sh64_out32(b, addr)

#define inb(addr)		ctrl_inb(addr)
#define inw(addr)		ctrl_inw(addr)
#define inl(addr)		ctrl_inl(addr)
#define outb(b, addr)		ctrl_outb(b, addr)
#define outw(b, addr)		ctrl_outw(b, addr)
#define outl(b, addr)		ctrl_outl(b, addr)

void outsw(unsigned long port, const void *addr, unsigned long count);
void insw(unsigned long port, void *addr, unsigned long count);
void outsl(unsigned long port, const void *addr, unsigned long count);
void insl(unsigned long port, void *addr, unsigned long count);

void memcpy_toio(void __iomem *to, const void *from, long count);
void memcpy_fromio(void *to, void __iomem *from, long count);

#define mmiowb()

#ifdef __KERNEL__

#ifdef CONFIG_SH_CAYMAN
extern unsigned long smsc_superio_virt;
#endif
#ifdef CONFIG_PCI
extern unsigned long pciio_virt;
#endif

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/SuperH mapping
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}

extern void * __ioremap(unsigned long phys_addr, unsigned long size,
			unsigned long flags);

static inline void * ioremap(unsigned long phys_addr, unsigned long size)
{
	return __ioremap(phys_addr, size, 1);
}

static inline void * ioremap_nocache (unsigned long phys_addr, unsigned long size)
{
	return __ioremap(phys_addr, size, 0);
}

extern void iounmap(void *addr);

unsigned long onchip_remap(unsigned long addr, unsigned long size, const char* name);
extern void onchip_unmap(unsigned long vaddr);

static __inline__ int check_signature(volatile void __iomem *io_addr,
			const unsigned char *signature, int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and RAM coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 *  - dma_cache_wback(start, size) writes back any dirty lines but does
 *    not invalidate the cache.  This can be used before DMA reads from
 *    memory,
 */

static __inline__ void dma_cache_wback_inv (unsigned long start, unsigned long size)
{
	unsigned long s = start & L1_CACHE_ALIGN_MASK;
	unsigned long e = (start + size) & L1_CACHE_ALIGN_MASK;

	for (; s <= e; s += L1_CACHE_BYTES)
		asm volatile ("ocbp	%0, 0" : : "r" (s));
}

static __inline__ void dma_cache_inv (unsigned long start, unsigned long size)
{
	// Note that caller has to be careful with overzealous
	// invalidation should there be partial cache lines at the extremities
	// of the specified range
	unsigned long s = start & L1_CACHE_ALIGN_MASK;
	unsigned long e = (start + size) & L1_CACHE_ALIGN_MASK;

	for (; s <= e; s += L1_CACHE_BYTES)
		asm volatile ("ocbi	%0, 0" : : "r" (s));
}

static __inline__ void dma_cache_wback (unsigned long start, unsigned long size)
{
	unsigned long s = start & L1_CACHE_ALIGN_MASK;
	unsigned long e = (start + size) & L1_CACHE_ALIGN_MASK;

	for (; s <= e; s += L1_CACHE_BYTES)
		asm volatile ("ocbwb	%0, 0" : : "r" (s));
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
#endif /* __ASM_SH64_IO_H */
