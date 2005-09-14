#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>

/*
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 */

 /*
  *  Bit simplified and optimized by Jan Hubicka
  *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999.
  *
  *  isa_memset_io, isa_memcpy_fromio, isa_memcpy_toio added,
  *  isa_read[wl] and isa_write[wl] fixed
  *  - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
  */

#define __SLOW_DOWN_IO "\noutb %%al,$0x80"

#ifdef REALLY_SLOW_IO
#define __FULL_SLOW_DOWN_IO __SLOW_DOWN_IO __SLOW_DOWN_IO __SLOW_DOWN_IO __SLOW_DOWN_IO
#else
#define __FULL_SLOW_DOWN_IO __SLOW_DOWN_IO
#endif

/*
 * Talk about misusing macros..
 */
#define __OUT1(s,x) \
static inline void out##s(unsigned x value, unsigned short port) {

#define __OUT2(s,s1,s2) \
__asm__ __volatile__ ("out" #s " %" s1 "0,%" s2 "1"

#define __OUT(s,s1,x) \
__OUT1(s,x) __OUT2(s,s1,"w") : : "a" (value), "Nd" (port)); } \
__OUT1(s##_p,x) __OUT2(s,s1,"w") __FULL_SLOW_DOWN_IO : : "a" (value), "Nd" (port));} \

#define __IN1(s) \
static inline RETURN_TYPE in##s(unsigned short port) { RETURN_TYPE _v;

#define __IN2(s,s1,s2) \
__asm__ __volatile__ ("in" #s " %" s2 "1,%" s1 "0"

#define __IN(s,s1,i...) \
__IN1(s) __IN2(s,s1,"w") : "=a" (_v) : "Nd" (port) ,##i ); return _v; } \
__IN1(s##_p) __IN2(s,s1,"w") __FULL_SLOW_DOWN_IO : "=a" (_v) : "Nd" (port) ,##i ); return _v; } \

#define __INS(s) \
static inline void ins##s(unsigned short port, void * addr, unsigned long count) \
{ __asm__ __volatile__ ("rep ; ins" #s \
: "=D" (addr), "=c" (count) : "d" (port),"0" (addr),"1" (count)); }

#define __OUTS(s) \
static inline void outs##s(unsigned short port, const void * addr, unsigned long count) \
{ __asm__ __volatile__ ("rep ; outs" #s \
: "=S" (addr), "=c" (count) : "d" (port),"0" (addr),"1" (count)); }

#define RETURN_TYPE unsigned char
__IN(b,"")
#undef RETURN_TYPE
#define RETURN_TYPE unsigned short
__IN(w,"")
#undef RETURN_TYPE
#define RETURN_TYPE unsigned int
__IN(l,"")
#undef RETURN_TYPE

__OUT(b,"b",char)
__OUT(w,"w",short)
__OUT(l,,int)

__INS(b)
__INS(w)
__INS(l)

__OUTS(b)
__OUTS(w)
__OUTS(l)

#define IO_SPACE_LIMIT 0xffff

#if defined(__KERNEL__) && __x86_64__

#include <linux/vmalloc.h>

#ifndef __i386__
/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}
#endif

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)    ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

#include <asm-generic/iomap.h>

extern void __iomem *__ioremap(unsigned long offset, unsigned long size, unsigned long flags);

static inline void __iomem * ioremap (unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, 0);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
extern void __iomem * ioremap_nocache (unsigned long offset, unsigned long size);
extern void iounmap(volatile void __iomem *addr);

/*
 * ISA I/O bus memory addresses are 1:1 with the physical address.
 */
#define isa_virt_to_bus virt_to_phys
#define isa_page_to_bus page_to_phys
#define isa_bus_to_virt phys_to_virt

/*
 * However PCI ones are not necessarily 1:1 and therefore these interfaces
 * are forbidden in portable PCI drivers.
 *
 * Allow them on x86 for legacy drivers, though.
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 */

static inline __u8 __readb(const volatile void __iomem *addr)
{
	return *(__force volatile __u8 *)addr;
}
static inline __u16 __readw(const volatile void __iomem *addr)
{
	return *(__force volatile __u16 *)addr;
}
static inline __u32 __readl(const volatile void __iomem *addr)
{
	return *(__force volatile __u32 *)addr;
}
static inline __u64 __readq(const volatile void __iomem *addr)
{
	return *(__force volatile __u64 *)addr;
}
#define readb(x) __readb(x)
#define readw(x) __readw(x)
#define readl(x) __readl(x)
#define readq(x) __readq(x)
#define readb_relaxed(a) readb(a)
#define readw_relaxed(a) readw(a)
#define readl_relaxed(a) readl(a)
#define readq_relaxed(a) readq(a)
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_readq readq

#define mmiowb()

#ifdef CONFIG_UNORDERED_IO
static inline void __writel(__u32 val, volatile void __iomem *addr)
{
	volatile __u32 __iomem *target = addr;
	asm volatile("movnti %1,%0"
		     : "=m" (*target)
		     : "r" (val) : "memory");
}

static inline void __writeq(__u64 val, volatile void __iomem *addr)
{
	volatile __u64 __iomem *target = addr;
	asm volatile("movnti %1,%0"
		     : "=m" (*target)
		     : "r" (val) : "memory");
}
#else
static inline void __writel(__u32 b, volatile void __iomem *addr)
{
	*(__force volatile __u32 *)addr = b;
}
static inline void __writeq(__u64 b, volatile void __iomem *addr)
{
	*(__force volatile __u64 *)addr = b;
}
#endif
static inline void __writeb(__u8 b, volatile void __iomem *addr)
{
	*(__force volatile __u8 *)addr = b;
}
static inline void __writew(__u16 b, volatile void __iomem *addr)
{
	*(__force volatile __u16 *)addr = b;
}
#define writeq(val,addr) __writeq((val),(addr))
#define writel(val,addr) __writel((val),(addr))
#define writew(val,addr) __writew((val),(addr))
#define writeb(val,addr) __writeb((val),(addr))
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq

void __memcpy_fromio(void*,unsigned long,unsigned);
void __memcpy_toio(unsigned long,const void*,unsigned);

static inline void memcpy_fromio(void *to, const volatile void __iomem *from, unsigned len)
{
	__memcpy_fromio(to,(unsigned long)from,len);
}
static inline void memcpy_toio(volatile void __iomem *to, const void *from, unsigned len)
{
	__memcpy_toio((unsigned long)to,from,len);
}

void memset_io(volatile void __iomem *a, int b, size_t c);

/*
 * ISA space is 'always mapped' on a typical x86 system, no need to
 * explicitly ioremap() it. The fact that the ISA IO space is mapped
 * to PAGE_OFFSET is pure coincidence - it does not mean ISA values
 * are physical addresses. The following constant pointer can be
 * used as the IO-area pointer (it can be iounmapped as well, so the
 * analogy with PCI is quite large):
 */
#define __ISA_IO_base ((char __iomem *)(PAGE_OFFSET))

#define isa_readb(a) readb(__ISA_IO_base + (a))
#define isa_readw(a) readw(__ISA_IO_base + (a))
#define isa_readl(a) readl(__ISA_IO_base + (a))
#define isa_writeb(b,a) writeb(b,__ISA_IO_base + (a))
#define isa_writew(w,a) writew(w,__ISA_IO_base + (a))
#define isa_writel(l,a) writel(l,__ISA_IO_base + (a))
#define isa_memset_io(a,b,c)		memset_io(__ISA_IO_base + (a),(b),(c))
#define isa_memcpy_fromio(a,b,c)	memcpy_fromio((a),__ISA_IO_base + (b),(c))
#define isa_memcpy_toio(a,b,c)		memcpy_toio(__ISA_IO_base + (a),(b),(c))


/*
 * Again, x86-64 does not require mem IO specific function.
 */

#define eth_io_copy_and_sum(a,b,c,d)		eth_copy_and_sum((a),(void *)(b),(c),(d))
#define isa_eth_io_copy_and_sum(a,b,c,d)	eth_copy_and_sum((a),(void *)(__ISA_IO_base + (b)),(c),(d))

/**
 *	check_signature		-	find BIOS signatures
 *	@io_addr: mmio address to check 
 *	@signature:  signature block
 *	@length: length of signature
 *
 *	Perform a signature comparison with the mmio address io_addr. This
 *	address should have been obtained by ioremap.
 *	Returns 1 on a match.
 */
 
static inline int check_signature(void __iomem *io_addr,
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

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#define flush_write_buffers() 

extern int iommu_bio_merge;
#define BIO_VMERGE_BOUNDARY iommu_bio_merge

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

#endif
