#ifndef _ASM_IA64_IO_H
#define _ASM_IA64_IO_H

/*
 * This file contains the definitions for the emulated IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated to
 * (a) handle it all in a way that makes gcc able to optimize it as
 * well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */

/* We don't use IO slowdowns on the ia64, but.. */
#define __SLOW_DOWN_IO	do { } while (0)
#define SLOW_DOWN_IO	do { } while (0)

#define __IA64_UNCACHED_OFFSET	RGN_BASE(RGN_UNCACHED)

/*
 * The legacy I/O space defined by the ia64 architecture supports only 65536 ports, but
 * large machines may have multiple other I/O spaces so we can't place any a priori limit
 * on IO_SPACE_LIMIT.  These additional spaces are described in ACPI.
 */
#define IO_SPACE_LIMIT		0xffffffffffffffffUL

#define MAX_IO_SPACES_BITS		8
#define MAX_IO_SPACES			(1UL << MAX_IO_SPACES_BITS)
#define IO_SPACE_BITS			24
#define IO_SPACE_SIZE			(1UL << IO_SPACE_BITS)

#define IO_SPACE_NR(port)		((port) >> IO_SPACE_BITS)
#define IO_SPACE_BASE(space)		((space) << IO_SPACE_BITS)
#define IO_SPACE_PORT(port)		((port) & (IO_SPACE_SIZE - 1))

#define IO_SPACE_SPARSE_ENCODING(p)	((((p) >> 2) << 12) | ((p) & 0xfff))

struct io_space {
	unsigned long mmio_base;	/* base in MMIO space */
	int sparse;
};

extern struct io_space io_space[];
extern unsigned int num_io_spaces;

# ifdef __KERNEL__

/*
 * All MMIO iomem cookies are in region 6; anything less is a PIO cookie:
 *	0xCxxxxxxxxxxxxxxx	MMIO cookie (return from ioremap)
 *	0x000000001SPPPPPP	PIO cookie (S=space number, P..P=port)
 *
 * ioread/writeX() uses the leading 1 in PIO cookies (PIO_OFFSET) to catch
 * code that uses bare port numbers without the prerequisite pci_iomap().
 */
#define PIO_OFFSET		(1UL << (MAX_IO_SPACES_BITS + IO_SPACE_BITS))
#define PIO_MASK		(PIO_OFFSET - 1)
#define PIO_RESERVED		__IA64_UNCACHED_OFFSET
#define HAVE_ARCH_PIO_SIZE

#include <asm/intrinsics.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm-generic/iomap.h>

/*
 * Change virtual addresses to physical addresses and vv.
 */
static inline unsigned long
virt_to_phys (volatile void *address)
{
	return (unsigned long) address - PAGE_OFFSET;
}

static inline void*
phys_to_virt (unsigned long address)
{
	return (void *) (address + PAGE_OFFSET);
}

#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
extern u64 kern_mem_attribute (unsigned long phys_addr, unsigned long size);
extern int valid_phys_addr_range (unsigned long addr, size_t count); /* efi.c */
extern int valid_mmap_phys_addr_range (unsigned long pfn, size_t count);

/*
 * The following two macros are deprecated and scheduled for removal.
 * Please use the PCI-DMA interface defined in <asm/pci.h> instead.
 */
#define bus_to_virt	phys_to_virt
#define virt_to_bus	virt_to_phys
#define page_to_bus	page_to_phys

# endif /* KERNEL */

/*
 * Memory fence w/accept.  This should never be used in code that is
 * not IA-64 specific.
 */
#define __ia64_mf_a()	ia64_mfa()

/**
 * ___ia64_mmiowb - I/O write barrier
 *
 * Ensure ordering of I/O space writes.  This will make sure that writes
 * following the barrier will arrive after all previous writes.  For most
 * ia64 platforms, this is a simple 'mf.a' instruction.
 *
 * See Documentation/DocBook/deviceiobook.tmpl for more information.
 */
static inline void ___ia64_mmiowb(void)
{
	ia64_mfa();
}

static inline void*
__ia64_mk_io_addr (unsigned long port)
{
	struct io_space *space;
	unsigned long offset;

	space = &io_space[IO_SPACE_NR(port)];
	port = IO_SPACE_PORT(port);
	if (space->sparse)
		offset = IO_SPACE_SPARSE_ENCODING(port);
	else
		offset = port;

	return (void *) (space->mmio_base | offset);
}

#define __ia64_inb	___ia64_inb
#define __ia64_inw	___ia64_inw
#define __ia64_inl	___ia64_inl
#define __ia64_outb	___ia64_outb
#define __ia64_outw	___ia64_outw
#define __ia64_outl	___ia64_outl
#define __ia64_readb	___ia64_readb
#define __ia64_readw	___ia64_readw
#define __ia64_readl	___ia64_readl
#define __ia64_readq	___ia64_readq
#define __ia64_readb_relaxed	___ia64_readb
#define __ia64_readw_relaxed	___ia64_readw
#define __ia64_readl_relaxed	___ia64_readl
#define __ia64_readq_relaxed	___ia64_readq
#define __ia64_writeb	___ia64_writeb
#define __ia64_writew	___ia64_writew
#define __ia64_writel	___ia64_writel
#define __ia64_writeq	___ia64_writeq
#define __ia64_mmiowb	___ia64_mmiowb

/*
 * For the in/out routines, we need to do "mf.a" _after_ doing the I/O access to ensure
 * that the access has completed before executing other I/O accesses.  Since we're doing
 * the accesses through an uncachable (UC) translation, the CPU will execute them in
 * program order.  However, we still need to tell the compiler not to shuffle them around
 * during optimization, which is why we use "volatile" pointers.
 */

static inline unsigned int
___ia64_inb (unsigned long port)
{
	volatile unsigned char *addr = __ia64_mk_io_addr(port);
	unsigned char ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

static inline unsigned int
___ia64_inw (unsigned long port)
{
	volatile unsigned short *addr = __ia64_mk_io_addr(port);
	unsigned short ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

static inline unsigned int
___ia64_inl (unsigned long port)
{
	volatile unsigned int *addr = __ia64_mk_io_addr(port);
	unsigned int ret;

	ret = *addr;
	__ia64_mf_a();
	return ret;
}

static inline void
___ia64_outb (unsigned char val, unsigned long port)
{
	volatile unsigned char *addr = __ia64_mk_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

static inline void
___ia64_outw (unsigned short val, unsigned long port)
{
	volatile unsigned short *addr = __ia64_mk_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

static inline void
___ia64_outl (unsigned int val, unsigned long port)
{
	volatile unsigned int *addr = __ia64_mk_io_addr(port);

	*addr = val;
	__ia64_mf_a();
}

static inline void
__insb (unsigned long port, void *dst, unsigned long count)
{
	unsigned char *dp = dst;

	while (count--)
		*dp++ = platform_inb(port);
}

static inline void
__insw (unsigned long port, void *dst, unsigned long count)
{
	unsigned short *dp = dst;

	while (count--)
		*dp++ = platform_inw(port);
}

static inline void
__insl (unsigned long port, void *dst, unsigned long count)
{
	unsigned int *dp = dst;

	while (count--)
		*dp++ = platform_inl(port);
}

static inline void
__outsb (unsigned long port, const void *src, unsigned long count)
{
	const unsigned char *sp = src;

	while (count--)
		platform_outb(*sp++, port);
}

static inline void
__outsw (unsigned long port, const void *src, unsigned long count)
{
	const unsigned short *sp = src;

	while (count--)
		platform_outw(*sp++, port);
}

static inline void
__outsl (unsigned long port, const void *src, unsigned long count)
{
	const unsigned int *sp = src;

	while (count--)
		platform_outl(*sp++, port);
}

/*
 * Unfortunately, some platforms are broken and do not follow the IA-64 architecture
 * specification regarding legacy I/O support.  Thus, we have to make these operations
 * platform dependent...
 */
#define __inb		platform_inb
#define __inw		platform_inw
#define __inl		platform_inl
#define __outb		platform_outb
#define __outw		platform_outw
#define __outl		platform_outl
#define __mmiowb	platform_mmiowb

#define inb(p)		__inb(p)
#define inw(p)		__inw(p)
#define inl(p)		__inl(p)
#define insb(p,d,c)	__insb(p,d,c)
#define insw(p,d,c)	__insw(p,d,c)
#define insl(p,d,c)	__insl(p,d,c)
#define outb(v,p)	__outb(v,p)
#define outw(v,p)	__outw(v,p)
#define outl(v,p)	__outl(v,p)
#define outsb(p,s,c)	__outsb(p,s,c)
#define outsw(p,s,c)	__outsw(p,s,c)
#define outsl(p,s,c)	__outsl(p,s,c)
#define mmiowb()	__mmiowb()

/*
 * The address passed to these functions are ioremap()ped already.
 *
 * We need these to be machine vectors since some platforms don't provide
 * DMA coherence via PIO reads (PCI drivers and the spec imply that this is
 * a good idea).  Writes are ok though for all existing ia64 platforms (and
 * hopefully it'll stay that way).
 */
static inline unsigned char
___ia64_readb (const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}

static inline unsigned short
___ia64_readw (const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)addr;
}

static inline unsigned int
___ia64_readl (const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *) addr;
}

static inline unsigned long
___ia64_readq (const volatile void __iomem *addr)
{
	return *(volatile unsigned long __force *) addr;
}

static inline void
__writeb (unsigned char val, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *) addr = val;
}

static inline void
__writew (unsigned short val, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *) addr = val;
}

static inline void
__writel (unsigned int val, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *) addr = val;
}

static inline void
__writeq (unsigned long val, volatile void __iomem *addr)
{
	*(volatile unsigned long __force *) addr = val;
}

#define __readb		platform_readb
#define __readw		platform_readw
#define __readl		platform_readl
#define __readq		platform_readq
#define __readb_relaxed	platform_readb_relaxed
#define __readw_relaxed	platform_readw_relaxed
#define __readl_relaxed	platform_readl_relaxed
#define __readq_relaxed	platform_readq_relaxed

#define readb(a)	__readb((a))
#define readw(a)	__readw((a))
#define readl(a)	__readl((a))
#define readq(a)	__readq((a))
#define readb_relaxed(a)	__readb_relaxed((a))
#define readw_relaxed(a)	__readw_relaxed((a))
#define readl_relaxed(a)	__readl_relaxed((a))
#define readq_relaxed(a)	__readq_relaxed((a))
#define __raw_readb	readb
#define __raw_readw	readw
#define __raw_readl	readl
#define __raw_readq	readq
#define __raw_readb_relaxed	readb_relaxed
#define __raw_readw_relaxed	readw_relaxed
#define __raw_readl_relaxed	readl_relaxed
#define __raw_readq_relaxed	readq_relaxed
#define writeb(v,a)	__writeb((v), (a))
#define writew(v,a)	__writew((v), (a))
#define writel(v,a)	__writel((v), (a))
#define writeq(v,a)	__writeq((v), (a))
#define __raw_writeb	writeb
#define __raw_writew	writew
#define __raw_writel	writel
#define __raw_writeq	writeq

#ifndef inb_p
# define inb_p		inb
#endif
#ifndef inw_p
# define inw_p		inw
#endif
#ifndef inl_p
# define inl_p		inl
#endif

#ifndef outb_p
# define outb_p		outb
#endif
#ifndef outw_p
# define outw_p		outw
#endif
#ifndef outl_p
# define outl_p		outl
#endif

# ifdef __KERNEL__

extern void __iomem * ioremap(unsigned long offset, unsigned long size);
extern void __iomem * ioremap_nocache (unsigned long offset, unsigned long size);
extern void iounmap (volatile void __iomem *addr);

/*
 * String version of IO memory access ops:
 */
extern void memcpy_fromio(void *dst, const volatile void __iomem *src, long n);
extern void memcpy_toio(volatile void __iomem *dst, const void *src, long n);
extern void memset_io(volatile void __iomem *s, int c, long n);

# endif /* __KERNEL__ */

/*
 * Enabling BIO_VMERGE_BOUNDARY forces us to turn off I/O MMU bypassing.  It is said that
 * BIO-level virtual merging can give up to 4% performance boost (not verified for ia64).
 * On the other hand, we know that I/O MMU bypassing gives ~8% performance improvement on
 * SPECweb-like workloads on zx1-based machines.  Thus, for now we favor I/O MMU bypassing
 * over BIO-level virtual merging.
 */
extern unsigned long ia64_max_iommu_merge_mask;
#if 1
#define BIO_VMERGE_BOUNDARY	0
#else
/*
 * It makes no sense at all to have this BIO_VMERGE_BOUNDARY macro here.  Should be
 * replaced by dma_merge_mask() or something of that sort.  Note: the only way
 * BIO_VMERGE_BOUNDARY is used is to mask off bits.  Effectively, our definition gets
 * expanded into:
 *
 *	addr & ((ia64_max_iommu_merge_mask + 1) - 1) == (addr & ia64_max_iommu_vmerge_mask)
 *
 * which is precisely what we want.
 */
#define BIO_VMERGE_BOUNDARY	(ia64_max_iommu_merge_mask + 1)
#endif

#endif /* _ASM_IA64_IO_H */
