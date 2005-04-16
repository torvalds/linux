/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf GmbH
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2004, 2005  MIPS Technologies, Inc.  All rights reserved.
 *	Author:	Maciej W. Rozycki <macro@mips.com>
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/bug.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/processor.h>

#include <mangle-port.h>

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

/*
 * Raw operations are never swapped in software.  Otoh values that raw
 * operations are working on may or may not have been swapped by the bus
 * hardware.  An example use would be for flash memory that's used for
 * execute in place.
 */
# define __raw_ioswabb(x)	(x)
# define __raw_ioswabw(x)	(x)
# define __raw_ioswabl(x)	(x)
# define __raw_ioswabq(x)	(x)

/*
 * Sane hardware offers swapping of PCI/ISA I/O space accesses in hardware;
 * less sane hardware forces software to fiddle with this...
 */
#if defined(CONFIG_SWAP_IO_SPACE)

# define ioswabb(x)		(x)
# ifdef CONFIG_SGI_IP22
/*
 * IP22 seems braindead enough to swap 16bits values in hardware, but
 * not 32bits.  Go figure... Can't tell without documentation.
 */
#  define ioswabw(x)		(x)
# else
#  define ioswabw(x)		le16_to_cpu(x)
# endif
# define ioswabl(x)		le32_to_cpu(x)
# define ioswabq(x)		le64_to_cpu(x)

#else

# define ioswabb(x)		(x)
# define ioswabw(x)		(x)
# define ioswabl(x)		(x)
# define ioswabq(x)		(x)

#endif

/*
 * Native bus accesses never swapped.
 */
#define bus_ioswabb(x)		(x)
#define bus_ioswabw(x)		(x)
#define bus_ioswabl(x)		(x)
#define bus_ioswabq(x)		(x)

#define __bus_ioswabq		bus_ioswabq

#define IO_SPACE_LIMIT 0xffff

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * on any sane architecture; generic code does not use this assumption.
 */
extern const unsigned long mips_io_port_base;

#define set_io_port_base(base)	\
	do { * (unsigned long *) &mips_io_port_base = (base); } while (0)

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 *
 */

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (mips_io_port_base));

#ifdef CONF_SLOWDOWN_IO
#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif
#else
#define SLOW_DOWN_IO
#endif

/*
 *     virt_to_phys    -       map virtual addresses to physical
 *     @address: address to remap
 *
 *     The returned physical address is the physical (CPU) mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses directly mapped or allocated via kmalloc.
 *
 *     This function does not give bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long)address - PAGE_OFFSET;
}

/*
 *     phys_to_virt    -       map physical address to virtual
 *     @address: address to remap
 *
 *     The returned virtual address is a current CPU mapping for
 *     the memory address given. It is only valid to use this function on
 *     addresses that have a kernel mapping
 *
 *     This function does not handle bus mappings for DMA transfers. In
 *     almost all conceivable cases a device driver should not be using
 *     this function
 */
static inline void * phys_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET);
}

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
static inline unsigned long isa_virt_to_bus(volatile void * address)
{
	return (unsigned long)address - PAGE_OFFSET;
}

static inline void * isa_bus_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET);
}

#define isa_page_to_bus page_to_phys

/*
 * However PCI ones are not necessarily 1:1 and therefore these interfaces
 * are forbidden in portable PCI drivers.
 *
 * Allow them for x86 for legacy drivers, though.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is mapped
 * for the processor.  This implies the assumption that there is only
 * one of these busses.
 */
extern unsigned long isa_slot_offset;

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void * __ioremap(phys_t offset, phys_t size, unsigned long flags);
extern void __iounmap(volatile void __iomem *addr);

static inline void * __ioremap_mode(phys_t offset, unsigned long size,
	unsigned long flags)
{
	if (cpu_has_64bit_addresses) {
		u64 base = UNCAC_BASE;

		/*
		 * R10000 supports a 2 bit uncached attribute therefore
		 * UNCAC_BASE may not equal IO_BASE.
		 */
		if (flags == _CACHE_UNCACHED)
			base = (u64) IO_BASE;
		return (void *) (unsigned long) (base + offset);
	}

	return __ioremap(offset, size, flags);
}

/*
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 */
#define ioremap(offset, size)						\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)

/*
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In paticular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 */
#define ioremap_nocache(offset, size)					\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED)

/*
 * These two are MIPS specific ioremap variant.  ioremap_cacheable_cow
 * requests a cachable mapping, ioremap_uncached_accelerated requests a
 * mapping using the uncached accelerated mode which isn't supported on
 * all processors.
 */
#define ioremap_cacheable_cow(offset, size)				\
	__ioremap_mode((offset), (size), _CACHE_CACHABLE_COW)
#define ioremap_uncached_accelerated(offset, size)			\
	__ioremap_mode((offset), (size), _CACHE_UNCACHED_ACCELERATED)

static inline void iounmap(volatile void __iomem *addr)
{
	if (cpu_has_64bit_addresses)
		return;

	__iounmap(addr);
}


#define __BUILD_MEMORY_SINGLE(pfx, bwlq, type, irq)			\
									\
static inline void pfx##write##bwlq(type val,				\
				    volatile void __iomem *mem)		\
{									\
	volatile type *__mem;						\
	type __val;							\
									\
	__mem = (void *)__swizzle_addr_##bwlq((unsigned long)(mem));	\
									\
	__val = pfx##ioswab##bwlq(val);					\
									\
	if (sizeof(type) != sizeof(u64) || sizeof(u64) == sizeof(long))	\
		*__mem = __val;						\
	else if (cpu_has_64bits) {					\
		unsigned long __flags;					\
		type __tmp;						\
									\
		if (irq)						\
			local_irq_save(__flags);			\
		__asm__ __volatile__(					\
			".set	mips3"		"\t\t# __writeq""\n\t"	\
			"dsll32	%L0, %L0, 0"			"\n\t"	\
			"dsrl32	%L0, %L0, 0"			"\n\t"	\
			"dsll32	%M0, %M0, 0"			"\n\t"	\
			"or	%L0, %L0, %M0"			"\n\t"	\
			"sd	%L0, %2"			"\n\t"	\
			".set	mips0"				"\n"	\
			: "=r" (__tmp)					\
			: "0" (__val), "m" (*__mem));			\
		if (irq)						\
			local_irq_restore(__flags);			\
	} else								\
		BUG();							\
}									\
									\
static inline type pfx##read##bwlq(volatile void __iomem *mem)		\
{									\
	volatile type *__mem;						\
	type __val;							\
									\
	__mem = (void *)__swizzle_addr_##bwlq((unsigned long)(mem));	\
									\
	if (sizeof(type) != sizeof(u64) || sizeof(u64) == sizeof(long))	\
		__val = *__mem;						\
	else if (cpu_has_64bits) {					\
		unsigned long __flags;					\
									\
		local_irq_save(__flags);				\
		__asm__ __volatile__(					\
			".set	mips3"		"\t\t# __readq"	"\n\t"	\
			"ld	%L0, %1"			"\n\t"	\
			"dsra32	%M0, %L0, 0"			"\n\t"	\
			"sll	%L0, %L0, 0"			"\n\t"	\
			".set	mips0"				"\n"	\
			: "=r" (__val)					\
			: "m" (*__mem));				\
		local_irq_restore(__flags);				\
	} else {							\
		__val = 0;						\
		BUG();							\
	}								\
									\
	return pfx##ioswab##bwlq(__val);				\
}

#define __BUILD_IOPORT_SINGLE(pfx, bwlq, type, p, slow)			\
									\
static inline void pfx##out##bwlq##p(type val, unsigned long port)	\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	port = __swizzle_addr_##bwlq(port);				\
	__addr = (void *)(mips_io_port_base + port);			\
									\
	__val = pfx##ioswab##bwlq(val);					\
									\
	if (sizeof(type) != sizeof(u64)) {				\
		*__addr = __val;					\
		slow;							\
	} else								\
		BUILD_BUG();						\
}									\
									\
static inline type pfx##in##bwlq##p(unsigned long port)			\
{									\
	volatile type *__addr;						\
	type __val;							\
									\
	port = __swizzle_addr_##bwlq(port);				\
	__addr = (void *)(mips_io_port_base + port);			\
									\
	if (sizeof(type) != sizeof(u64)) {				\
		__val = *__addr;					\
		slow;							\
	} else {							\
		__val = 0;						\
		BUILD_BUG();						\
	}								\
									\
	return pfx##ioswab##bwlq(__val);				\
}

#define __BUILD_MEMORY_PFX(bus, bwlq, type)				\
									\
__BUILD_MEMORY_SINGLE(bus, bwlq, type, 1)

#define __BUILD_IOPORT_PFX(bus, bwlq, type)				\
									\
__BUILD_IOPORT_SINGLE(bus, bwlq, type, ,)				\
__BUILD_IOPORT_SINGLE(bus, bwlq, type, _p, SLOW_DOWN_IO)

#define BUILDIO(bwlq, type)						\
									\
__BUILD_MEMORY_PFX(, bwlq, type)					\
__BUILD_MEMORY_PFX(__raw_, bwlq, type)					\
__BUILD_MEMORY_PFX(bus_, bwlq, type)					\
__BUILD_IOPORT_PFX(, bwlq, type)					\
__BUILD_IOPORT_PFX(__raw_, bwlq, type)

#define __BUILDIO(bwlq, type)						\
									\
__BUILD_MEMORY_SINGLE(__bus_, bwlq, type, 0)

BUILDIO(b, u8)
BUILDIO(w, u16)
BUILDIO(l, u32)
BUILDIO(q, u64)

__BUILDIO(q, u64)

#define readb_relaxed			readb
#define readw_relaxed			readw
#define readl_relaxed			readl
#define readq_relaxed			readq

/*
 * Some code tests for these symbols
 */
#define readq				readq
#define writeq				writeq

#define __BUILD_MEMORY_STRING(bwlq, type)				\
									\
static inline void writes##bwlq(volatile void __iomem *mem, void *addr,	\
				unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		__raw_write##bwlq(*__addr, mem);			\
		__addr++;						\
	}								\
}									\
									\
static inline void reads##bwlq(volatile void __iomem *mem, void *addr,	\
			       unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __raw_read##bwlq(mem);			\
		__addr++;						\
	}								\
}

#define __BUILD_IOPORT_STRING(bwlq, type)				\
									\
static inline void outs##bwlq(unsigned long port, void *addr,		\
			      unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		__raw_out##bwlq(*__addr, port);				\
		__addr++;						\
	}								\
}									\
									\
static inline void ins##bwlq(unsigned long port, void *addr,		\
			     unsigned int count)			\
{									\
	volatile type *__addr = addr;					\
									\
	while (count--) {						\
		*__addr = __raw_in##bwlq(port);				\
		__addr++;						\
	}								\
}

#define BUILDSTRING(bwlq, type)						\
									\
__BUILD_MEMORY_STRING(bwlq, type)					\
__BUILD_IOPORT_STRING(bwlq, type)

BUILDSTRING(b, u8)
BUILDSTRING(w, u16)
BUILDSTRING(l, u32)
BUILDSTRING(q, u64)


/* Depends on MIPS II instruction set */
#define mmiowb() asm volatile ("sync" ::: "memory")

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/*
 * Memory Mapped I/O
 */
#define ioread8(addr)		readb(addr)
#define ioread16(addr)		readw(addr)
#define ioread32(addr)		readl(addr)

#define iowrite8(b,addr)	writeb(b,addr)
#define iowrite16(w,addr)	writew(w,addr)
#define iowrite32(l,addr)	writel(l,addr)

#define ioread8_rep(a,b,c)	readsb(a,b,c)
#define ioread16_rep(a,b,c)	readsw(a,b,c)
#define ioread32_rep(a,b,c)	readsl(a,b,c)

#define iowrite8_rep(a,b,c)	writesb(a,b,c)
#define iowrite16_rep(a,b,c)	writesw(a,b,c)
#define iowrite32_rep(a,b,c)	writesl(a,b,c)

/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);

/*
 * ISA space is 'always mapped' on currently supported MIPS systems, no need
 * to explicitly ioremap() it. The fact that the ISA IO space is mapped
 * to PAGE_OFFSET is pure coincidence - it does not mean ISA values
 * are physical addresses. The following constant pointer can be
 * used as the IO-area pointer (it can be iounmapped as well, so the
 * analogy with PCI is quite large):
 */
#define __ISA_IO_base ((char *)(isa_slot_offset))

#define isa_readb(a)		readb(__ISA_IO_base + (a))
#define isa_readw(a)		readw(__ISA_IO_base + (a))
#define isa_readl(a)		readl(__ISA_IO_base + (a))
#define isa_readq(a)		readq(__ISA_IO_base + (a))
#define isa_writeb(b,a)		writeb(b,__ISA_IO_base + (a))
#define isa_writew(w,a)		writew(w,__ISA_IO_base + (a))
#define isa_writel(l,a)		writel(l,__ISA_IO_base + (a))
#define isa_writeq(q,a)		writeq(q,__ISA_IO_base + (a))
#define isa_memset_io(a,b,c)	memset_io(__ISA_IO_base + (a),(b),(c))
#define isa_memcpy_fromio(a,b,c) memcpy_fromio((a),__ISA_IO_base + (b),(c))
#define isa_memcpy_toio(a,b,c)	memcpy_toio(__ISA_IO_base + (a),(b),(c))

/*
 * We don't have csum_partial_copy_fromio() yet, so we cheat here and
 * just copy it. The net code will then do the checksum later.
 */
#define eth_io_copy_and_sum(skb,src,len,unused) memcpy_fromio((skb)->data,(src),(len))
#define isa_eth_io_copy_and_sum(a,b,c,d) eth_copy_and_sum((a),(b),(c),(d))

/*
 *     check_signature         -       find BIOS signatures
 *     @io_addr: mmio address to check
 *     @signature:  signature block
 *     @length: length of signature
 *
 *     Perform a signature comparison with the mmio address io_addr. This
 *     address should have been obtained by ioremap.
 *     Returns 1 on a match.
 */
static inline int check_signature(char __iomem *io_addr,
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
 *  - dma_cache_wback_inv(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_wback(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 */
#ifdef CONFIG_DMA_NONCOHERENT

extern void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*_dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#define dma_cache_wback_inv(start, size)	_dma_cache_wback_inv(start,size)
#define dma_cache_wback(start, size)		_dma_cache_wback(start,size)
#define dma_cache_inv(start, size)		_dma_cache_inv(start,size)

#else /* Sane hardware */

#define dma_cache_wback_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_wback(start,size)	\
	do { (void) (start); (void) (size); } while (0)
#define dma_cache_inv(start,size)	\
	do { (void) (start); (void) (size); } while (0)

#endif /* CONFIG_DMA_NONCOHERENT */

/*
 * Read a 32-bit register that requires a 64-bit read cycle on the bus.
 * Avoid interrupt mucking, just adjust the address for 4-byte access.
 * Assume the addresses are 8-byte aligned.
 */
#ifdef __MIPSEB__
#define __CSR_32_ADJUST 4
#else
#define __CSR_32_ADJUST 0
#endif

#define csr_out32(v,a) (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST) = (v))
#define csr_in32(a)    (*(volatile u32 *)((unsigned long)(a) + __CSR_32_ADJUST))

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* _ASM_IO_H */
