#ifndef _ASM_POWERPC_IO_H
#define _ASM_POWERPC_IO_H
#ifdef __KERNEL__

/* 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Check of existence of legacy devices */
extern int check_legacy_ioport(unsigned long base_port);
#define PNPBIOS_BASE	0xf000	/* only relevant for PReP */

#ifndef CONFIG_PPC64
#include <asm-ppc/io.h>
#else

#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/byteorder.h>
#include <asm/paca.h>
#include <asm/synch.h>
#include <asm/delay.h>

#include <asm-generic/iomap.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO

/*
 *
 * Low level MMIO accessors
 *
 * This provides the non-bus specific accessors to MMIO. Those are PowerPC
 * specific and thus shouldn't be used in generic code. The accessors
 * provided here are:
 *
 *	in_8, in_le16, in_be16, in_le32, in_be32, in_le64, in_be64
 *	out_8, out_le16, out_be16, out_le32, out_be32, out_le64, out_be64
 *	_insb, _insw_ns, _insl_ns, _outsb, _outsw_ns, _outsl_ns
 *
 * Those operate directly on a kernel virtual address. Note that the prototype
 * for the out_* accessors has the arguments in opposite order from the usual
 * linux PCI accessors. Unlike those, they take the address first and the value
 * next.
 *
 * Note: I might drop the _ns suffix on the stream operations soon as it is
 * simply normal for stream operations to not swap in the first place.
 *
 */

#define IO_SET_SYNC_FLAG()	do { get_paca()->io_sync = 1; } while(0)

#define DEF_MMIO_IN(name, type, insn)					\
static inline type name(const volatile type __iomem *addr)		\
{									\
	type ret;							\
	__asm__ __volatile__("sync;" insn ";twi 0,%0,0;isync"		\
 		: "=r" (ret) : "r" (addr), "m" (*addr));		\
	return ret;							\
}

#define DEF_MMIO_OUT(name, type, insn)					\
static inline void name(volatile type __iomem *addr, type val)		\
{									\
	__asm__ __volatile__("sync;" insn				\
 		: "=m" (*addr) : "r" (val), "r" (addr));		\
	IO_SET_SYNC_FLAG();					\
}


#define DEF_MMIO_IN_BE(name, size, insn) \
	DEF_MMIO_IN(name, u##size, __stringify(insn)"%U2%X2 %0,%2")
#define DEF_MMIO_IN_LE(name, size, insn) \
	DEF_MMIO_IN(name, u##size, __stringify(insn)" %0,0,%1")

#define DEF_MMIO_OUT_BE(name, size, insn) \
	DEF_MMIO_OUT(name, u##size, __stringify(insn)"%U0%X0 %1,%0")
#define DEF_MMIO_OUT_LE(name, size, insn) \
	DEF_MMIO_OUT(name, u##size, __stringify(insn)" %1,0,%2")

DEF_MMIO_IN_BE(in_8,     8, lbz);
DEF_MMIO_IN_BE(in_be16, 16, lhz);
DEF_MMIO_IN_BE(in_be32, 32, lwz);
DEF_MMIO_IN_BE(in_be64, 64, ld);
DEF_MMIO_IN_LE(in_le16, 16, lhbrx);
DEF_MMIO_IN_LE(in_le32, 32, lwbrx);

DEF_MMIO_OUT_BE(out_8,     8, stb);
DEF_MMIO_OUT_BE(out_be16, 16, sth);
DEF_MMIO_OUT_BE(out_be32, 32, stw);
DEF_MMIO_OUT_BE(out_be64, 64, std);
DEF_MMIO_OUT_LE(out_le16, 16, sthbrx);
DEF_MMIO_OUT_LE(out_le32, 32, stwbrx);

/* There is no asm instructions for 64 bits reverse loads and stores */
static inline u64 in_le64(const volatile u64 __iomem *addr)
{
	return le64_to_cpu(in_be64(addr));
}

static inline void out_le64(volatile u64 __iomem *addr, u64 val)
{
	out_be64(addr, cpu_to_le64(val));
}

/*
 * Low level IO stream instructions are defined out of line for now
 */
extern void _insb(const volatile u8 __iomem *addr, void *buf, long count);
extern void _outsb(volatile u8 __iomem *addr,const void *buf,long count);
extern void _insw_ns(const volatile u16 __iomem *addr, void *buf, long count);
extern void _outsw_ns(volatile u16 __iomem *addr, const void *buf, long count);
extern void _insl_ns(const volatile u32 __iomem *addr, void *buf, long count);
extern void _outsl_ns(volatile u32 __iomem *addr, const void *buf, long count);

/* The _ns naming is historical and will be removed. For now, just #define
 * the non _ns equivalent names
 */
#define _insw	_insw_ns
#define _insl	_insl_ns
#define _outsw	_outsw_ns
#define _outsl	_outsl_ns

/*
 *
 * PCI and standard ISA accessors
 *
 * Those are globally defined linux accessors for devices on PCI or ISA
 * busses. They follow the Linux defined semantics. The current implementation
 * for PowerPC is as close as possible to the x86 version of these, and thus
 * provides fairly heavy weight barriers for the non-raw versions
 *
 * In addition, they support a hook mechanism when CONFIG_PPC_INDIRECT_IO
 * allowing the platform to provide its own implementation of some or all
 * of the accessors.
 */

extern unsigned long isa_io_base;
extern unsigned long pci_io_base;


/*
 * Non ordered and non-swapping "raw" accessors
 */

static inline unsigned char __raw_readb(const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}
static inline unsigned short __raw_readw(const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)addr;
}
static inline unsigned int __raw_readl(const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *)addr;
}
static inline unsigned long __raw_readq(const volatile void __iomem *addr)
{
	return *(volatile unsigned long __force *)addr;
}
static inline void __raw_writeb(unsigned char v, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)addr = v;
}
static inline void __raw_writew(unsigned short v, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)addr = v;
}
static inline void __raw_writel(unsigned int v, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)addr = v;
}
static inline void __raw_writeq(unsigned long v, volatile void __iomem *addr)
{
	*(volatile unsigned long __force *)addr = v;
}


/*
 *
 * PCI PIO and MMIO accessors.
 *
 */

#include <asm/eeh.h>

/* Shortcut to the MMIO argument pointer */
#define PCI_IO_ADDR	volatile void __iomem *

/* Indirect IO address tokens:
 *
 * When CONFIG_PPC_INDIRECT_IO is set, the platform can provide hooks
 * on all IOs.
 *
 * To help platforms who may need to differenciate MMIO addresses in
 * their hooks, a bitfield is reserved for use by the platform near the
 * top of MMIO addresses (not PIO, those have to cope the hard way).
 *
 * This bit field is 12 bits and is at the top of the IO virtual
 * addresses PCI_IO_INDIRECT_TOKEN_MASK.
 *
 * The kernel virtual space is thus:
 *
 *  0xD000000000000000		: vmalloc
 *  0xD000080000000000		: PCI PHB IO space
 *  0xD000080080000000		: ioremap
 *  0xD0000fffffffffff		: end of ioremap region
 *
 * Since the top 4 bits are reserved as the region ID, we use thus
 * the next 12 bits and keep 4 bits available for the future if the
 * virtual address space is ever to be extended.
 *
 * The direct IO mapping operations will then mask off those bits
 * before doing the actual access, though that only happen when
 * CONFIG_PPC_INDIRECT_IO is set, thus be careful when you use that
 * mechanism
 */

#ifdef CONFIG_PPC_INDIRECT_IO
#define PCI_IO_IND_TOKEN_MASK	0x0fff000000000000ul
#define PCI_IO_IND_TOKEN_SHIFT	48
#define PCI_FIX_ADDR(addr)						\
	((PCI_IO_ADDR)(((unsigned long)(addr)) & ~PCI_IO_IND_TOKEN_MASK))
#define PCI_GET_ADDR_TOKEN(addr)					\
	(((unsigned long)(addr) & PCI_IO_IND_TOKEN_MASK) >> 		\
		PCI_IO_IND_TOKEN_SHIFT)
#define PCI_SET_ADDR_TOKEN(addr, token) 				\
do {									\
	unsigned long __a = (unsigned long)(addr);			\
	__a &= ~PCI_IO_IND_TOKEN_MASK;					\
	__a |= ((unsigned long)(token)) << PCI_IO_IND_TOKEN_SHIFT;	\
	(addr) = (void __iomem *)__a;					\
} while(0)
#else
#define PCI_FIX_ADDR(addr) (addr)
#endif

/* The "__do_*" operations below provide the actual "base" implementation
 * for each of the defined acccessor. Some of them use the out_* functions
 * directly, some of them still use EEH, though we might change that in the
 * future. Those macros below provide the necessary argument swapping and
 * handling of the IO base for PIO.
 *
 * They are themselves used by the macros that define the actual accessors
 * and can be used by the hooks if any.
 *
 * Note that PIO operations are always defined in terms of their corresonding
 * MMIO operations. That allows platforms like iSeries who want to modify the
 * behaviour of both to only hook on the MMIO version and get both. It's also
 * possible to hook directly at the toplevel PIO operation if they have to
 * be handled differently
 */
#define __do_writeb(val, addr)	out_8(PCI_FIX_ADDR(addr), val)
#define __do_writew(val, addr)	out_le16(PCI_FIX_ADDR(addr), val)
#define __do_writel(val, addr)	out_le32(PCI_FIX_ADDR(addr), val)
#define __do_writeq(val, addr)	out_le64(PCI_FIX_ADDR(addr), val)
#define __do_writew_be(val, addr) out_be16(PCI_FIX_ADDR(addr), val)
#define __do_writel_be(val, addr) out_be32(PCI_FIX_ADDR(addr), val)
#define __do_writeq_be(val, addr) out_be64(PCI_FIX_ADDR(addr), val)
#define __do_readb(addr)	eeh_readb(PCI_FIX_ADDR(addr))
#define __do_readw(addr)	eeh_readw(PCI_FIX_ADDR(addr))
#define __do_readl(addr)	eeh_readl(PCI_FIX_ADDR(addr))
#define __do_readq(addr)	eeh_readq(PCI_FIX_ADDR(addr))
#define __do_readw_be(addr)	eeh_readw_be(PCI_FIX_ADDR(addr))
#define __do_readl_be(addr)	eeh_readl_be(PCI_FIX_ADDR(addr))
#define __do_readq_be(addr)	eeh_readq_be(PCI_FIX_ADDR(addr))

#define __do_outb(val, port)	writeb(val,(PCI_IO_ADDR)pci_io_base+port);
#define __do_outw(val, port)	writew(val,(PCI_IO_ADDR)pci_io_base+port);
#define __do_outl(val, port)	writel(val,(PCI_IO_ADDR)pci_io_base+port);
#define __do_inb(port)		readb((PCI_IO_ADDR)pci_io_base + port);
#define __do_inw(port)		readw((PCI_IO_ADDR)pci_io_base + port);
#define __do_inl(port)		readl((PCI_IO_ADDR)pci_io_base + port);

#define __do_readsb(a, b, n)	eeh_readsb(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsw(a, b, n)	eeh_readsw(PCI_FIX_ADDR(a), (b), (n))
#define __do_readsl(a, b, n)	eeh_readsl(PCI_FIX_ADDR(a), (b), (n))
#define __do_writesb(a, b, n)	_outsb(PCI_FIX_ADDR(a),(b),(n))
#define __do_writesw(a, b, n)	_outsw(PCI_FIX_ADDR(a),(b),(n))
#define __do_writesl(a, b, n)	_outsl(PCI_FIX_ADDR(a),(b),(n))

#define __do_insb(p, b, n)	readsb((PCI_IO_ADDR)pci_io_base+(p), (b), (n))
#define __do_insw(p, b, n)	readsw((PCI_IO_ADDR)pci_io_base+(p), (b), (n))
#define __do_insl(p, b, n)	readsl((PCI_IO_ADDR)pci_io_base+(p), (b), (n))
#define __do_outsb(p, b, n)	writesb((PCI_IO_ADDR)pci_io_base+(p),(b),(n))
#define __do_outsw(p, b, n)	writesw((PCI_IO_ADDR)pci_io_base+(p),(b),(n))
#define __do_outsl(p, b, n)	writesl((PCI_IO_ADDR)pci_io_base+(p),(b),(n))

#define __do_memset_io(addr, c, n)	eeh_memset_io(PCI_FIX_ADDR(addr), c, n)
#define __do_memcpy_fromio(dst, src, n)	eeh_memcpy_fromio(dst, \
						PCI_FIX_ADDR(src), n)
#define __do_memcpy_toio(dst, src, n)	eeh_memcpy_toio(PCI_FIX_ADDR(dst), \
						src, n)

#ifdef CONFIG_PPC_INDIRECT_IO
#define DEF_PCI_HOOK(x)		x
#else
#define DEF_PCI_HOOK(x)		NULL
#endif

/* Structure containing all the hooks */
extern struct ppc_pci_io {

#define DEF_PCI_AC_RET(name, ret, at, al)	ret (*name) at;
#define DEF_PCI_AC_NORET(name, at, al)		void (*name) at;

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

} ppc_pci_io;

/* The inline wrappers */
#define DEF_PCI_AC_RET(name, ret, at, al)			\
static inline ret name at					\
{								\
	if (DEF_PCI_HOOK(ppc_pci_io.name) != NULL)		\
		return ppc_pci_io.name al;			\
	return __do_##name al;					\
}

#define DEF_PCI_AC_NORET(name, at, al)				\
static inline void name at					\
{								\
	if (DEF_PCI_HOOK(ppc_pci_io.name) != NULL)		\
		ppc_pci_io.name al;				\
	else							\
		__do_##name al;					\
}

#include <asm/io-defs.h>

#undef DEF_PCI_AC_RET
#undef DEF_PCI_AC_NORET

/* Some drivers check for the presence of readq & writeq with
 * a #ifdef, so we make them happy here.
 */
#define readq	readq
#define writeq	writeq

/* Nothing to do for cache stuff x*/

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)


/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

/*
 * We don't do relaxed operations yet, at least not with this semantic
 */
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)
#define readq_relaxed(addr) readq(addr)

/*
 * Enforce synchronisation of stores vs. spin_unlock
 * (this does it explicitely, though our implementation of spin_unlock
 * does it implicitely too)
 */
static inline void mmiowb(void)
{
	unsigned long tmp;

	__asm__ __volatile__("sync; li %0,0; stb %0,%1(13)"
	: "=&r" (tmp) : "i" (offsetof(struct paca_struct, io_sync))
	: "memory");
}

static inline void iosync(void)
{
        __asm__ __volatile__ ("sync" : : : "memory");
}

/* Enforce in-order execution of data I/O.
 * No distinction between read/write on PPC; use eieio for all three.
 * Those are fairly week though. They don't provide a barrier between
 * MMIO and cacheable storage nor do they provide a barrier vs. locks,
 * they only provide barriers between 2 __raw MMIO operations and
 * possibly break write combining.
 */
#define iobarrier_rw() eieio()
#define iobarrier_r()  eieio()
#define iobarrier_w()  eieio()


/*
 * output pause versions need a delay at least for the
 * w83c105 ide controller in a p610.
 */
#define inb_p(port)             inb(port)
#define outb_p(val, port)       (udelay(1), outb((val), (port)))
#define inw_p(port)             inw(port)
#define outw_p(val, port)       (udelay(1), outw((val), (port)))
#define inl_p(port)             inl(port)
#define outl_p(val, port)       (udelay(1), outl((val), (port)))


#define IO_SPACE_LIMIT ~(0UL)


/**
 * ioremap     -   map bus memory into CPU space
 * @address:   bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * We provide a few variations of it:
 *
 * * ioremap is the standard one and provides non-cacheable guarded mappings
 *   and can be hooked by the platform via ppc_md
 *
 * * ioremap_flags allows to specify the page flags as an argument and can
 *   also be hooked by the platform via ppc_md
 *
 * * ioremap_nocache is identical to ioremap
 *
 * * iounmap undoes such a mapping and can be hooked
 *
 * * __ioremap_explicit (and the pending __iounmap_explicit) are low level
 *   functions to create hand-made mappings for use only by the PCI code
 *   and cannot currently be hooked.
 *
 * * __ioremap is the low level implementation used by ioremap and
 *   ioremap_flags and cannot be hooked (but can be used by a hook on one
 *   of the previous ones)
 *
 * * __iounmap, is the low level implementation used by iounmap and cannot
 *   be hooked (but can be used by a hook on iounmap)
 *
 */
extern void __iomem *ioremap(unsigned long address, unsigned long size);
extern void __iomem *ioremap_flags(unsigned long address, unsigned long size,
				   unsigned long flags);
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
extern void iounmap(void __iomem *addr);

extern void __iomem *__ioremap(unsigned long address, unsigned long size,
			       unsigned long flags);
extern void __iounmap(void __iomem *addr);

extern int __ioremap_explicit(unsigned long p_addr, unsigned long v_addr,
		     	      unsigned long size, unsigned long flags);
extern int __iounmap_explicit(void __iomem *start, unsigned long size);

extern void __iomem * reserve_phb_iospace(unsigned long size);


/*
 * When CONFIG_PPC_INDIRECT_IO is set, we use the generic iomap implementation
 * which needs some additional definitions here. They basically allow PIO
 * space overall to be 1GB. This will work as long as we never try to use
 * iomap to map MMIO below 1GB which should be fine on ppc64
 */
#define HAVE_ARCH_PIO_SIZE		1
#define PIO_OFFSET			0x00000000UL
#define PIO_MASK			0x3fffffffUL
#define PIO_RESERVED			0x40000000UL

#define mmio_read16be(addr)		readw_be(addr)
#define mmio_read32be(addr)		readl_be(addr)
#define mmio_write16be(val, addr)	writew_be(val, addr)
#define mmio_write32be(val, addr)	writel_be(val, addr)
#define mmio_insb(addr, dst, count)	readsb(addr, dst, count)
#define mmio_insw(addr, dst, count)	readsw(addr, dst, count)
#define mmio_insl(addr, dst, count)	readsl(addr, dst, count)
#define mmio_outsb(addr, src, count)	writesb(addr, src, count)
#define mmio_outsw(addr, src, count)	writesw(addr, src, count)
#define mmio_outsl(addr, src, count)	writesl(addr, src, count)

/**
 *	virt_to_phys	-	map virtual addresses to physical
 *	@address: address to remap
 *
 *	The returned physical address is the physical (CPU) mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses directly mapped or allocated via kmalloc.
 *
 *	This function does not give bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa((unsigned long)address);
}

/**
 *	phys_to_virt	-	map physical address to virtual
 *	@address: address to remap
 *
 *	The returned virtual address is a current CPU mapping for
 *	the memory address given. It is only valid to use this function on
 *	addresses that have a kernel mapping
 *
 *	This function does not handle bus mappings for DMA transfers. In
 *	almost all conceivable cases a device driver should not be using
 *	this function
 */
static inline void * phys_to_virt(unsigned long address)
{
	return (void *)__va(address);
}

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

/* We do NOT want virtual merging, it would put too much pressure on
 * our iommu allocator. Instead, we want drivers to be smart enough
 * to coalesce sglists that happen to have been mapped in a contiguous
 * way by the iommu
 */
#define BIO_VMERGE_BOUNDARY	0

#endif /* __KERNEL__ */

#endif /* CONFIG_PPC64 */
#endif /* _ASM_POWERPC_IO_H */
