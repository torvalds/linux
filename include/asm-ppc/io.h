#ifdef __KERNEL__
#ifndef _PPC_IO_H
#define _PPC_IO_H

#include <linux/string.h>
#include <linux/types.h>

#include <asm/page.h>
#include <asm/byteorder.h>
#include <asm/synch.h>
#include <asm/mmu.h>

#define SIO_CONFIG_RA	0x398
#define SIO_CONFIG_RD	0x399

#define SLOW_DOWN_IO

#define PMAC_ISA_MEM_BASE 	0
#define PMAC_PCI_DRAM_OFFSET 	0
#define CHRP_ISA_IO_BASE 	0xf8000000
#define CHRP_ISA_MEM_BASE 	0xf7000000
#define CHRP_PCI_DRAM_OFFSET 	0
#define PREP_ISA_IO_BASE 	0x80000000
#define PREP_ISA_MEM_BASE 	0xc0000000
#define PREP_PCI_DRAM_OFFSET 	0x80000000

#if defined(CONFIG_4xx)
#include <asm/ibm4xx.h>
#elif defined(CONFIG_8xx)
#include <asm/mpc8xx.h>
#elif defined(CONFIG_8260)
#include <asm/mpc8260.h>
#elif !defined(CONFIG_PCI)
#define _IO_BASE	0
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET 0
#else /* Everyone else */
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
#define PCI_DRAM_OFFSET	pci_dram_offset
#endif /* Platform-dependent I/O */

#define ___IO_BASE ((void __iomem *)_IO_BASE)
extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;

/*
 * 8, 16 and 32 bit, big and little endian I/O operations, with barrier.
 *
 * Read operations have additional twi & isync to make sure the read
 * is actually performed (i.e. the data has come back) before we start
 * executing any following instructions.
 */
extern inline int in_8(const volatile unsigned char __iomem *addr)
{
	int ret;

	__asm__ __volatile__(
		"sync; lbz%U1%X1 %0,%1;\n"
		"twi 0,%0,0;\n"
		"isync" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_8(volatile unsigned char __iomem *addr, int val)
{
	__asm__ __volatile__("stb%U0%X0 %1,%0; eieio" : "=m" (*addr) : "r" (val));
}

extern inline int in_le16(const volatile unsigned short __iomem *addr)
{
	int ret;

	__asm__ __volatile__("sync; lhbrx %0,0,%1;\n"
			     "twi 0,%0,0;\n"
			     "isync" : "=r" (ret) :
			      "r" (addr), "m" (*addr));
	return ret;
}

extern inline int in_be16(const volatile unsigned short __iomem *addr)
{
	int ret;

	__asm__ __volatile__("sync; lhz%U1%X1 %0,%1;\n"
			     "twi 0,%0,0;\n"
			     "isync" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le16(volatile unsigned short __iomem *addr, int val)
{
	__asm__ __volatile__("sync; sthbrx %1,0,%2" : "=m" (*addr) :
			      "r" (val), "r" (addr));
}

extern inline void out_be16(volatile unsigned short __iomem *addr, int val)
{
	__asm__ __volatile__("sync; sth%U0%X0 %1,%0" : "=m" (*addr) : "r" (val));
}

extern inline unsigned in_le32(const volatile unsigned __iomem *addr)
{
	unsigned ret;

	__asm__ __volatile__("sync; lwbrx %0,0,%1;\n"
			     "twi 0,%0,0;\n"
			     "isync" : "=r" (ret) :
			     "r" (addr), "m" (*addr));
	return ret;
}

extern inline unsigned in_be32(const volatile unsigned __iomem *addr)
{
	unsigned ret;

	__asm__ __volatile__("sync; lwz%U1%X1 %0,%1;\n"
			     "twi 0,%0,0;\n"
			     "isync" : "=r" (ret) : "m" (*addr));
	return ret;
}

extern inline void out_le32(volatile unsigned __iomem *addr, int val)
{
	__asm__ __volatile__("sync; stwbrx %1,0,%2" : "=m" (*addr) :
			     "r" (val), "r" (addr));
}

extern inline void out_be32(volatile unsigned __iomem *addr, int val)
{
	__asm__ __volatile__("sync; stw%U0%X0 %1,%0" : "=m" (*addr) : "r" (val));
}
#if defined (CONFIG_8260_PCI9)
#define readb(addr) in_8((volatile u8 *)(addr))
#define writeb(b,addr) out_8((volatile u8 *)(addr), (b))
#else
static inline __u8 readb(const volatile void __iomem *addr)
{
	return in_8(addr);
}
static inline void writeb(__u8 b, volatile void __iomem *addr)
{
	out_8(addr, b);
}
#endif

#if defined (CONFIG_8260_PCI9)
/* Use macros if PCI9 workaround enabled */
#define readw(addr) in_le16((volatile u16 *)(addr))
#define readl(addr) in_le32((volatile u32 *)(addr))
#define writew(b,addr) out_le16((volatile u16 *)(addr),(b))
#define writel(b,addr) out_le32((volatile u32 *)(addr),(b))
#else
static inline __u16 readw(const volatile void __iomem *addr)
{
	return in_le16(addr);
}
static inline __u32 readl(const volatile void __iomem *addr)
{
	return in_le32(addr);
}
static inline void writew(__u16 b, volatile void __iomem *addr)
{
	out_le16(addr, b);
}
static inline void writel(__u32 b, volatile void __iomem *addr)
{
	out_le32(addr, b);
}
#endif /* CONFIG_8260_PCI9 */

#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)

static inline __u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(__force volatile __u8 *)(addr);
}
static inline __u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(__force volatile __u16 *)(addr);
}
static inline __u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(__force volatile __u32 *)(addr);
}
static inline void __raw_writeb(__u8 b, volatile void __iomem *addr)
{
	*(__force volatile __u8 *)(addr) = b;
}
static inline void __raw_writew(__u16 b, volatile void __iomem *addr)
{
	*(__force volatile __u16 *)(addr) = b;
}
static inline void __raw_writel(__u32 b, volatile void __iomem *addr)
{
	*(__force volatile __u32 *)(addr) = b;
}

#define mmiowb()

/*
 * The insw/outsw/insl/outsl macros don't do byte-swapping.
 * They are only used in practice for transferring buffers which
 * are arrays of bytes, and byte-swapping is not appropriate in
 * that case.  - paulus
 */
#define insb(port, buf, ns)	_insb((port)+___IO_BASE, (buf), (ns))
#define outsb(port, buf, ns)	_outsb((port)+___IO_BASE, (buf), (ns))
#define insw(port, buf, ns)	_insw_ns((port)+___IO_BASE, (buf), (ns))
#define outsw(port, buf, ns)	_outsw_ns((port)+___IO_BASE, (buf), (ns))
#define insl(port, buf, nl)	_insl_ns((port)+___IO_BASE, (buf), (nl))
#define outsl(port, buf, nl)	_outsl_ns((port)+___IO_BASE, (buf), (nl))

#define readsb(a, b, n)		_insb((a), (b), (n))
#define readsw(a, b, n)		_insw_ns((a), (b), (n))
#define readsl(a, b, n)		_insl_ns((a), (b), (n))
#define writesb(a, b, n)	_outsb((a),(b),(n))
#define writesw(a, b, n)	_outsw_ns((a),(b),(n))
#define writesl(a, b, n)	_outsl_ns((a),(b),(n))


/*
 * On powermacs and 8xx we will get a machine check exception 
 * if we try to read data from a non-existent I/O port. Because
 * the machine check is an asynchronous exception, it isn't
 * well-defined which instruction SRR0 will point to when the
 * exception occurs.
 * With the sequence below (twi; isync; nop), we have found that
 * the machine check occurs on one of the three instructions on
 * all PPC implementations tested so far.  The twi and isync are
 * needed on the 601 (in fact twi; sync works too), the isync and
 * nop are needed on 604[e|r], and any of twi, sync or isync will
 * work on 603[e], 750, 74xx.
 * The twi creates an explicit data dependency on the returned
 * value which seems to be needed to make the 601 wait for the
 * load to finish.
 */

#define __do_in_asm(name, op)				\
extern __inline__ unsigned int name(unsigned int port)	\
{							\
	unsigned int x;					\
	__asm__ __volatile__(				\
		"sync\n"				\
		"0:"	op "	%0,0,%1\n"		\
		"1:	twi	0,%0,0\n"		\
		"2:	isync\n"			\
		"3:	nop\n"				\
		"4:\n"					\
		".section .fixup,\"ax\"\n"		\
		"5:	li	%0,-1\n"		\
		"	b	4b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align	2\n"			\
		"	.long	0b,5b\n"		\
		"	.long	1b,5b\n"		\
		"	.long	2b,5b\n"		\
		"	.long	3b,5b\n"		\
		".previous"				\
		: "=&r" (x)				\
		: "r" (port + ___IO_BASE));		\
	return x;					\
}

#define __do_out_asm(name, op)				\
extern __inline__ void name(unsigned int val, unsigned int port) \
{							\
	__asm__ __volatile__(				\
		"sync\n"				\
		"0:" op " %0,0,%1\n"			\
		"1:	sync\n"				\
		"2:\n"					\
		".section __ex_table,\"a\"\n"		\
		"	.align	2\n"			\
		"	.long	0b,2b\n"		\
		"	.long	1b,2b\n"		\
		".previous"				\
		: : "r" (val), "r" (port + ___IO_BASE));	\
}

__do_out_asm(outb, "stbx")
#if defined (CONFIG_8260_PCI9)
/* in asm cannot be defined if PCI9 workaround is used */
#define inb(port)		in_8((port)+___IO_BASE)
#define inw(port)		in_le16((port)+___IO_BASE)
#define inl(port)		in_le32((port)+___IO_BASE)
__do_out_asm(outw, "sthbrx")
__do_out_asm(outl, "stwbrx")
#else
__do_in_asm(inb, "lbzx")
__do_in_asm(inw, "lhbrx")
__do_in_asm(inl, "lwbrx")
__do_out_asm(outw, "sthbrx")
__do_out_asm(outl, "stwbrx")

#endif

#define inb_p(port)		inb((port))
#define outb_p(val, port)	outb((val), (port))
#define inw_p(port)		inw((port))
#define outw_p(val, port)	outw((val), (port))
#define inl_p(port)		inl((port))
#define outl_p(val, port)	outl((val), (port))

extern void _insb(const volatile u8 __iomem *addr, void *buf, long count);
extern void _outsb(volatile u8 __iomem *addr,const void *buf,long count);
extern void _insw_ns(const volatile u16 __iomem *addr, void *buf, long count);
extern void _outsw_ns(volatile u16 __iomem *addr, const void *buf, long count);
extern void _insl_ns(const volatile u32 __iomem *addr, void *buf, long count);
extern void _outsl_ns(volatile u32 __iomem *addr, const void *buf, long count);


#define IO_SPACE_LIMIT ~0

#if defined (CONFIG_8260_PCI9)
#define memset_io(a,b,c)       memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)   memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)     memcpy((void *)(a),(b),(c))
#else
static inline void memset_io(volatile void __iomem *addr, unsigned char val, int count)
{
	memset((void __force *)addr, val, count);
}
static inline void memcpy_fromio(void *dst,const volatile void __iomem *src, int count)
{
	memcpy(dst, (void __force *) src, count);
}
static inline void memcpy_toio(volatile void __iomem *dst, const void *src, int count)
{
	memcpy((void __force *) dst, src, count);
}
#endif

/*
 * Map in an area of physical address space, for accessing
 * I/O devices etc.
 */
extern void __iomem *__ioremap(phys_addr_t address, unsigned long size,
		       unsigned long flags);
extern void __iomem *ioremap(phys_addr_t address, unsigned long size);
#ifdef CONFIG_44x
extern void __iomem *ioremap64(unsigned long long address, unsigned long size);
#endif
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
extern void iounmap(volatile void __iomem *addr);
extern unsigned long iopa(unsigned long addr);
extern void io_block_mapping(unsigned long virt, phys_addr_t phys,
			     unsigned int size, int flags);

/*
 * The PCI bus is inherently Little-Endian.  The PowerPC is being
 * run Big-Endian.  Thus all values which cross the [PCI] barrier
 * must be endian-adjusted.  Also, the local DRAM has a different
 * address from the PCI point of view, thus buffer addresses also
 * have to be modified [mapped] appropriately.
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
        if (address == (void *)0)
		return 0;
        return (unsigned long)address - KERNELBASE + PCI_DRAM_OFFSET;
}

extern inline void * bus_to_virt(unsigned long address)
{
        if (address == 0)
		return NULL;
        return (void *)(address - PCI_DRAM_OFFSET + KERNELBASE);
}

/*
 * Change virtual addresses to physical addresses and vv, for
 * addresses in the area where the kernel has the RAM mapped.
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long) address - KERNELBASE;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) (address + KERNELBASE);
}

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define page_to_bus(page)	(page_to_phys(page) + PCI_DRAM_OFFSET)

/* Enforce in-order execution of data I/O.
 * No distinction between read/write on PPC; use eieio for all three.
 */
#define iobarrier_rw() eieio()
#define iobarrier_r()  eieio()
#define iobarrier_w()  eieio()

/*
 * Here comes the ppc implementation of the IOMAP 
 * interfaces.
 */
static inline unsigned int ioread8(void __iomem *addr)
{
	return readb(addr);
}

static inline unsigned int ioread16(void __iomem *addr)
{
	return readw(addr);
}

static inline unsigned int ioread32(void __iomem *addr)
{
	return readl(addr);
}

static inline void iowrite8(u8 val, void __iomem *addr)
{
	writeb(val, addr);
}

static inline void iowrite16(u16 val, void __iomem *addr)
{
	writew(val, addr);
}

static inline void iowrite32(u32 val, void __iomem *addr)
{
	writel(val, addr);
}

static inline void ioread8_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insb(addr, dst, count);
}

static inline void ioread16_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insw_ns(addr, dst, count);
}

static inline void ioread32_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insl_ns(addr, dst, count);
}

static inline void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsb(addr, src, count);
}

static inline void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsw_ns(addr, src, count);
}

static inline void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsl_ns(addr, src, count);
}

/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);

#endif /* _PPC_IO_H */

#ifdef CONFIG_8260_PCI9
#include <asm/mpc8260_pci9.h>
#endif

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

/* access ports */
#define setbits32(_addr, _v) out_be32((_addr), in_be32(_addr) |  (_v))
#define clrbits32(_addr, _v) out_be32((_addr), in_be32(_addr) & ~(_v))

#define setbits16(_addr, _v) out_be16((_addr), in_be16(_addr) |  (_v))
#define clrbits16(_addr, _v) out_be16((_addr), in_be16(_addr) & ~(_v))

#define setbits8(_addr, _v) out_8((_addr), in_8(_addr) |  (_v))
#define clrbits8(_addr, _v) out_8((_addr), in_8(_addr) & ~(_v))

#endif /* __KERNEL__ */
