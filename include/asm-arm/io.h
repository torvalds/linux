/*
 *  linux/include/asm-arm/io.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *  16-Sep-1996	RMK	Inlined the inx/outx functions & optimised for both
 *			constant addresses and variable addresses.
 *  04-Dec-1997	RMK	Moved a lot of this stuff to the new architecture
 *			specific IO header files.
 *  27-Mar-1999	PJB	Second parameter of memcpy_toio is const..
 *  04-Apr-1999	PJB	Added check_signature.
 *  12-Dec-1999	RMK	More cleanups
 *  18-Jun-2000 RMK	Removed virt_to_* and friends definitions
 *  05-Oct-2004 BJD     Moved memory string functions to use void __iomem
 */
#ifndef __ASM_ARM_IO_H
#define __ASM_ARM_IO_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/memory.h>

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
#define isa_virt_to_bus virt_to_phys
#define isa_page_to_bus page_to_phys
#define isa_bus_to_virt phys_to_virt

/*
 * Generic IO read/write.  These perform native-endian accesses.  Note
 * that some architectures will want to re-define __raw_{read,write}w.
 */
extern void __raw_writesb(void __iomem *addr, const void *data, int bytelen);
extern void __raw_writesw(void __iomem *addr, const void *data, int wordlen);
extern void __raw_writesl(void __iomem *addr, const void *data, int longlen);

extern void __raw_readsb(void __iomem *addr, void *data, int bytelen);
extern void __raw_readsw(void __iomem *addr, void *data, int wordlen);
extern void __raw_readsl(void __iomem *addr, void *data, int longlen);

#define __raw_writeb(v,a)	(__chk_io_ptr(a), *(volatile unsigned char __force  *)(a) = (v))
#define __raw_writew(v,a)	(__chk_io_ptr(a), *(volatile unsigned short __force *)(a) = (v))
#define __raw_writel(v,a)	(__chk_io_ptr(a), *(volatile unsigned int __force   *)(a) = (v))

#define __raw_readb(a)		(__chk_io_ptr(a), *(volatile unsigned char __force  *)(a))
#define __raw_readw(a)		(__chk_io_ptr(a), *(volatile unsigned short __force *)(a))
#define __raw_readl(a)		(__chk_io_ptr(a), *(volatile unsigned int __force   *)(a))

/*
 * Architecture ioremap implementation.
 */
extern void __iomem * __ioremap(unsigned long, size_t, unsigned long);
extern void __iounmap(void __iomem *addr);

/*
 * Bad read/write accesses...
 */
extern void __readwrite_bug(const char *fn);

/*
 * Now, pick up the machine-defined IO definitions
 */
#include <asm/arch/io.h>

#ifdef __io_pci
#warning machine class uses buggy __io_pci
#endif
#if defined(__arch_putb) || defined(__arch_putw) || defined(__arch_putl) || \
    defined(__arch_getb) || defined(__arch_getw) || defined(__arch_getl)
#warning machine class uses old __arch_putw or __arch_getw
#endif

/*
 *  IO port access primitives
 *  -------------------------
 *
 * The ARM doesn't have special IO access instructions; all IO is memory
 * mapped.  Note that these are defined to perform little endian accesses
 * only.  Their primary purpose is to access PCI and ISA peripherals.
 *
 * Note that for a big endian machine, this implies that the following
 * big endian mode connectivity is in place, as described by numerous
 * ARM documents:
 *
 *    PCI:  D0-D7   D8-D15 D16-D23 D24-D31
 *    ARM: D24-D31 D16-D23  D8-D15  D0-D7
 *
 * The machine specific io.h include defines __io to translate an "IO"
 * address to a memory address.
 *
 * Note that we prevent GCC re-ordering or caching values in expressions
 * by introducing sequence points into the in*() definitions.  Note that
 * __raw_* do not guarantee this behaviour.
 *
 * The {in,out}[bwl] macros are for emulating x86-style PCI/ISA IO space.
 */
#ifdef __io
#define outb(v,p)		__raw_writeb(v,__io(p))
#define outw(v,p)		__raw_writew((__force __u16) \
					cpu_to_le16(v),__io(p))
#define outl(v,p)		__raw_writel((__force __u32) \
					cpu_to_le32(v),__io(p))

#define inb(p)	({ __u8 __v = __raw_readb(__io(p)); __v; })
#define inw(p)	({ __u16 __v = le16_to_cpu((__force __le16) \
			__raw_readw(__io(p))); __v; })
#define inl(p)	({ __u32 __v = le32_to_cpu((__force __le32) \
			__raw_readl(__io(p))); __v; })

#define outsb(p,d,l)		__raw_writesb(__io(p),d,l)
#define outsw(p,d,l)		__raw_writesw(__io(p),d,l)
#define outsl(p,d,l)		__raw_writesl(__io(p),d,l)

#define insb(p,d,l)		__raw_readsb(__io(p),d,l)
#define insw(p,d,l)		__raw_readsw(__io(p),d,l)
#define insl(p,d,l)		__raw_readsl(__io(p),d,l)
#endif

#define outb_p(val,port)	outb((val),(port))
#define outw_p(val,port)	outw((val),(port))
#define outl_p(val,port)	outl((val),(port))
#define inb_p(port)		inb((port))
#define inw_p(port)		inw((port))
#define inl_p(port)		inl((port))

#define outsb_p(port,from,len)	outsb(port,from,len)
#define outsw_p(port,from,len)	outsw(port,from,len)
#define outsl_p(port,from,len)	outsl(port,from,len)
#define insb_p(port,to,len)	insb(port,to,len)
#define insw_p(port,to,len)	insw(port,to,len)
#define insl_p(port,to,len)	insl(port,to,len)

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, const volatile void __iomem *, size_t);
extern void _memcpy_toio(volatile void __iomem *, const void *, size_t);
extern void _memset_io(volatile void __iomem *, int, size_t);

#define mmiowb()

/*
 *  Memory access primitives
 *  ------------------------
 *
 * These perform PCI memory accesses via an ioremap region.  They don't
 * take an address as such, but a cookie.
 *
 * Again, this are defined to perform little endian accesses.  See the
 * IO port primitives for more information.
 */
#ifdef __mem_pci
#define readb(c) ({ __u8  __v = __raw_readb(__mem_pci(c)); __v; })
#define readw(c) ({ __u16 __v = le16_to_cpu((__force __le16) \
					__raw_readw(__mem_pci(c))); __v; })
#define readl(c) ({ __u32 __v = le32_to_cpu((__force __le32) \
					__raw_readl(__mem_pci(c))); __v; })
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)

#define readsb(p,d,l)		__raw_readsb(__mem_pci(p),d,l)
#define readsw(p,d,l)		__raw_readsw(__mem_pci(p),d,l)
#define readsl(p,d,l)		__raw_readsl(__mem_pci(p),d,l)

#define writeb(v,c)		__raw_writeb(v,__mem_pci(c))
#define writew(v,c)		__raw_writew((__force __u16) \
					cpu_to_le16(v),__mem_pci(c))
#define writel(v,c)		__raw_writel((__force __u32) \
					cpu_to_le32(v),__mem_pci(c))

#define writesb(p,d,l)		__raw_writesb(__mem_pci(p),d,l)
#define writesw(p,d,l)		__raw_writesw(__mem_pci(p),d,l)
#define writesl(p,d,l)		__raw_writesl(__mem_pci(p),d,l)

#define memset_io(c,v,l)	_memset_io(__mem_pci(c),(v),(l))
#define memcpy_fromio(a,c,l)	_memcpy_fromio((a),__mem_pci(c),(l))
#define memcpy_toio(c,a,l)	_memcpy_toio(__mem_pci(c),(a),(l))

#define eth_io_copy_and_sum(s,c,l,b) \
				eth_copy_and_sum((s),__mem_pci(c),(l),(b))

static inline int
check_signature(void __iomem *io_addr, const unsigned char *signature,
		int length)
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

#elif !defined(readb)

#define readb(c)			(__readwrite_bug("readb"),0)
#define readw(c)			(__readwrite_bug("readw"),0)
#define readl(c)			(__readwrite_bug("readl"),0)
#define writeb(v,c)			__readwrite_bug("writeb")
#define writew(v,c)			__readwrite_bug("writew")
#define writel(v,c)			__readwrite_bug("writel")

#define eth_io_copy_and_sum(s,c,l,b)	__readwrite_bug("eth_io_copy_and_sum")

#define check_signature(io,sig,len)	(0)

#endif	/* __mem_pci */

/*
 * If this architecture has ISA IO, then define the isa_read/isa_write
 * macros.
 */
#ifdef __mem_isa

#define isa_readb(addr)			__raw_readb(__mem_isa(addr))
#define isa_readw(addr)			__raw_readw(__mem_isa(addr))
#define isa_readl(addr)			__raw_readl(__mem_isa(addr))
#define isa_writeb(val,addr)		__raw_writeb(val,__mem_isa(addr))
#define isa_writew(val,addr)		__raw_writew(val,__mem_isa(addr))
#define isa_writel(val,addr)		__raw_writel(val,__mem_isa(addr))
#define isa_memset_io(a,b,c)		_memset_io(__mem_isa(a),(b),(c))
#define isa_memcpy_fromio(a,b,c)	_memcpy_fromio((a),__mem_isa(b),(c))
#define isa_memcpy_toio(a,b,c)		_memcpy_toio(__mem_isa((a)),(b),(c))

#define isa_eth_io_copy_and_sum(a,b,c,d) \
				eth_copy_and_sum((a),__mem_isa(b),(c),(d))

#else	/* __mem_isa */

#define isa_readb(addr)			(__readwrite_bug("isa_readb"),0)
#define isa_readw(addr)			(__readwrite_bug("isa_readw"),0)
#define isa_readl(addr)			(__readwrite_bug("isa_readl"),0)
#define isa_writeb(val,addr)		__readwrite_bug("isa_writeb")
#define isa_writew(val,addr)		__readwrite_bug("isa_writew")
#define isa_writel(val,addr)		__readwrite_bug("isa_writel")
#define isa_memset_io(a,b,c)		__readwrite_bug("isa_memset_io")
#define isa_memcpy_fromio(a,b,c)	__readwrite_bug("isa_memcpy_fromio")
#define isa_memcpy_toio(a,b,c)		__readwrite_bug("isa_memcpy_toio")

#define isa_eth_io_copy_and_sum(a,b,c,d) \
				__readwrite_bug("isa_eth_io_copy_and_sum")

#endif	/* __mem_isa */

/*
 * ioremap and friends.
 *
 * ioremap takes a PCI memory address, as specified in
 * Documentation/IO-mapping.txt.
 */
#ifndef __arch_ioremap
#define ioremap(cookie,size)		__ioremap(cookie,size,0)
#define ioremap_nocache(cookie,size)	__ioremap(cookie,size,0)
#define ioremap_cached(cookie,size)	__ioremap(cookie,size,L_PTE_CACHEABLE)
#define iounmap(cookie)			__iounmap(cookie)
#else
#define ioremap(cookie,size)		__arch_ioremap((cookie),(size),0)
#define ioremap_nocache(cookie,size)	__arch_ioremap((cookie),(size),0)
#define ioremap_cached(cookie,size)	__arch_ioremap((cookie),(size),L_PTE_CACHEABLE)
#define iounmap(cookie)			__arch_iounmap(cookie)
#endif

/*
 * io{read,write}{8,16,32} macros
 */
#ifndef ioread8
#define ioread8(p)	({ unsigned int __v = __raw_readb(p); __v; })
#define ioread16(p)	({ unsigned int __v = le16_to_cpu(__raw_readw(p)); __v; })
#define ioread32(p)	({ unsigned int __v = le32_to_cpu(__raw_readl(p)); __v; })

#define iowrite8(v,p)	__raw_writeb(v, p)
#define iowrite16(v,p)	__raw_writew(cpu_to_le16(v), p)
#define iowrite32(v,p)	__raw_writel(cpu_to_le32(v), p)

#define ioread8_rep(p,d,c)	__raw_readsb(p,d,c)
#define ioread16_rep(p,d,c)	__raw_readsw(p,d,c)
#define ioread32_rep(p,d,c)	__raw_readsl(p,d,c)

#define iowrite8_rep(p,s,c)	__raw_writesb(p,s,c)
#define iowrite16_rep(p,s,c)	__raw_writesw(p,s,c)
#define iowrite32_rep(p,s,c)	__raw_writesl(p,s,c)

extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *addr);
#endif

struct pci_dev;

extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *addr);

/*
 * can the hardware map this into one segment or not, given no other
 * constraints.
 */
#define BIOVEC_MERGEABLE(vec1, vec2)	\
	((bvec_to_phys((vec1)) + (vec1)->bv_len) == bvec_to_phys((vec2)))

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif	/* __KERNEL__ */
#endif	/* __ASM_ARM_IO_H */
