#ifndef __ASM_SH_IO_H
#define __ASM_SH_IO_H

/*
 * Convention:
 *    read{b,w,l}/write{b,w,l} are for PCI,
 *    while in{b,w,l}/out{b,w,l} are for ISA
 * These may (will) be platform specific function.
 * In addition we have 'pausing' versions: in{b,w,l}_p/out{b,w,l}_p
 * and 'string' versions: ins{b,w,l}/outs{b,w,l}
 * For read{b,w,l} and write{b,w,l} there are also __raw versions, which
 * do not have a memory barrier after them.
 *
 * In addition, we have 
 *   ctrl_in{b,w,l}/ctrl_out{b,w,l} for SuperH specific I/O.
 *   which are processor specific.
 */

/*
 * We follow the Alpha convention here:
 *  __inb expands to an inline function call (which calls via the mv)
 *  _inb  is a real function call (note ___raw fns are _ version of __raw)
 *  inb   by default expands to _inb, but the machine specific code may
 *        define it to __inb if it chooses.
 */

#include <asm/cache.h>
#include <asm/system.h>
#include <asm/addrspace.h>
#include <asm/machvec.h>
#include <linux/config.h>

/*
 * Depending on which platform we are running on, we need different
 * I/O functions.
 */

#ifdef __KERNEL__
/*
 * Since boards are able to define their own set of I/O routines through
 * their respective machine vector, we always wrap through the mv.
 *
 * Also, in the event that a board hasn't provided its own definition for
 * a given routine, it will be wrapped to generic code at run-time.
 */

# define __inb(p)	sh_mv.mv_inb((p))
# define __inw(p)	sh_mv.mv_inw((p))
# define __inl(p)	sh_mv.mv_inl((p))
# define __outb(x,p)	sh_mv.mv_outb((x),(p))
# define __outw(x,p)	sh_mv.mv_outw((x),(p))
# define __outl(x,p)	sh_mv.mv_outl((x),(p))

# define __inb_p(p)	sh_mv.mv_inb_p((p))
# define __inw_p(p)	sh_mv.mv_inw_p((p))
# define __inl_p(p)	sh_mv.mv_inl_p((p))
# define __outb_p(x,p)	sh_mv.mv_outb_p((x),(p))
# define __outw_p(x,p)	sh_mv.mv_outw_p((x),(p))
# define __outl_p(x,p)	sh_mv.mv_outl_p((x),(p))

# define __insb(p,b,c)	sh_mv.mv_insb((p), (b), (c))
# define __insw(p,b,c)	sh_mv.mv_insw((p), (b), (c))
# define __insl(p,b,c)	sh_mv.mv_insl((p), (b), (c))
# define __outsb(p,b,c)	sh_mv.mv_outsb((p), (b), (c))
# define __outsw(p,b,c)	sh_mv.mv_outsw((p), (b), (c))
# define __outsl(p,b,c)	sh_mv.mv_outsl((p), (b), (c))

# define __readb(a)	sh_mv.mv_readb((a))
# define __readw(a)	sh_mv.mv_readw((a))
# define __readl(a)	sh_mv.mv_readl((a))
# define __writeb(v,a)	sh_mv.mv_writeb((v),(a))
# define __writew(v,a)	sh_mv.mv_writew((v),(a))
# define __writel(v,a)	sh_mv.mv_writel((v),(a))

# define __ioremap(a,s)	sh_mv.mv_ioremap((a), (s))
# define __iounmap(a)	sh_mv.mv_iounmap((a))

# define __isa_port2addr(a)	sh_mv.mv_isa_port2addr(a)

# define inb		__inb
# define inw		__inw
# define inl		__inl
# define outb		__outb
# define outw		__outw
# define outl		__outl

# define inb_p		__inb_p
# define inw_p		__inw_p
# define inl_p		__inl_p
# define outb_p		__outb_p
# define outw_p		__outw_p
# define outl_p		__outl_p

# define insb		__insb
# define insw		__insw
# define insl		__insl
# define outsb		__outsb
# define outsw		__outsw
# define outsl		__outsl

# define __raw_readb	__readb
# define __raw_readw	__readw
# define __raw_readl	__readl
# define __raw_writeb	__writeb
# define __raw_writew	__writew
# define __raw_writel	__writel

/*
 * The platform header files may define some of these macros to use
 * the inlined versions where appropriate.  These macros may also be
 * redefined by userlevel programs.
 */
#ifdef __raw_readb
# define readb(a)	({ unsigned long r_ = __raw_readb((unsigned long)a); mb(); r_; })
#endif
#ifdef __raw_readw
# define readw(a)	({ unsigned long r_ = __raw_readw((unsigned long)a); mb(); r_; })
#endif
#ifdef __raw_readl
# define readl(a)	({ unsigned long r_ = __raw_readl((unsigned long)a); mb(); r_; })
#endif

#ifdef __raw_writeb
# define writeb(v,a)	({ __raw_writeb((v),(unsigned long)(a)); mb(); })
#endif
#ifdef __raw_writew
# define writew(v,a)	({ __raw_writew((v),(unsigned long)(a)); mb(); })
#endif
#ifdef __raw_writel
# define writel(v,a)	({ __raw_writel((v),(unsigned long)(a)); mb(); })
#endif

#define readb_relaxed(a) readb(a)
#define readw_relaxed(a) readw(a)
#define readl_relaxed(a) readl(a)

#define mmiowb()

/*
 * If the platform has PC-like I/O, this function converts the offset into
 * an address.
 */
static __inline__ unsigned long isa_port2addr(unsigned long offset)
{
	return __isa_port2addr(offset);
}

/*
 * This function provides a method for the generic case where a board-specific
 * isa_port2addr simply needs to return the port + some arbitrary port base.
 *
 * We use this at board setup time to implicitly set the port base, and
 * as a result, we can use the generic isa_port2addr.
 */
static inline void __set_io_port_base(unsigned long pbase)
{
	extern unsigned long generic_io_base;

	generic_io_base = pbase;
}

#define isa_readb(a) readb(isa_port2addr(a))
#define isa_readw(a) readw(isa_port2addr(a))
#define isa_readl(a) readl(isa_port2addr(a))
#define isa_writeb(b,a) writeb(b,isa_port2addr(a))
#define isa_writew(w,a) writew(w,isa_port2addr(a))
#define isa_writel(l,a) writel(l,isa_port2addr(a))
#define isa_memset_io(a,b,c) \
  memset((void *)(isa_port2addr((unsigned long)a)),(b),(c))
#define isa_memcpy_fromio(a,b,c) \
  memcpy((a),(void *)(isa_port2addr((unsigned long)(b))),(c))
#define isa_memcpy_toio(a,b,c) \
  memcpy((void *)(isa_port2addr((unsigned long)(a))),(b),(c))

/* We really want to try and get these to memcpy etc */
extern void memcpy_fromio(void *, unsigned long, unsigned long);
extern void memcpy_toio(unsigned long, const void *, unsigned long);
extern void memset_io(unsigned long, int, unsigned long);

/* SuperH on-chip I/O functions */
static __inline__ unsigned char ctrl_inb(unsigned long addr)
{
	return *(volatile unsigned char*)addr;
}

static __inline__ unsigned short ctrl_inw(unsigned long addr)
{
	return *(volatile unsigned short*)addr;
}

static __inline__ unsigned int ctrl_inl(unsigned long addr)
{
	return *(volatile unsigned long*)addr;
}

static __inline__ void ctrl_outb(unsigned char b, unsigned long addr)
{
	*(volatile unsigned char*)addr = b;
}

static __inline__ void ctrl_outw(unsigned short b, unsigned long addr)
{
	*(volatile unsigned short*)addr = b;
}

static __inline__ void ctrl_outl(unsigned int b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}

#define IO_SPACE_LIMIT 0xffffffff

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/SuperH mapping
 */
static __inline__ unsigned long virt_to_phys(volatile void * address)
{
	return PHYSADDR(address);
}

static __inline__ void * phys_to_virt(unsigned long address)
{
	return (void *)P1SEGADDR(address);
}

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#define page_to_bus page_to_phys

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 *
 * On SH, we have the whole physical address space mapped at all times
 * (as MIPS does), so "ioremap()" and "iounmap()" do not need to do
 * anything.  (This isn't true for all machines but we still handle
 * these cases with wired TLB entries anyway ...)
 *
 * We cheat a bit and always return uncachable areas until we've fixed
 * the drivers to handle caching properly.  
 */
static __inline__ void * ioremap(unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size);
}

static __inline__ void iounmap(void *addr)
{
	return __iounmap(addr);
}

#define ioremap_nocache(off,size) ioremap(off,size)

static __inline__ int check_signature(unsigned long io_addr,
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

#define dma_cache_wback_inv(_start,_size) \
    __flush_purge_region(_start,_size)
#define dma_cache_inv(_start,_size) \
    __flush_invalidate_region(_start,_size)
#define dma_cache_wback(_start,_size) \
    __flush_wback_region(_start,_size)

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

#endif /* __ASM_SH_IO_H */
