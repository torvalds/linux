#ifndef _H8300_IO_H
#define _H8300_IO_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/virtconvert.h>

#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#include <asm/regs306x.h>
#elif defined(CONFIG_H8S2678)
#include <asm/regs267x.h>
#else
#error UNKNOWN CPU TYPE
#endif


/*
 * These are for ISA/PCI shared memory _only_ and should never be used
 * on any other type of memory, including Zorro memory. They are meant to
 * access the bus in the bus byte order which is little-endian!.
 *
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the m68k architecture, we just read/write the
 * memory location directly.
 */
/* ++roman: The assignments to temp. vars avoid that gcc sometimes generates
 * two accesses to memory, which may be undesireable for some devices.
 */

/*
 * swap functions are sometimes needed to interface little-endian hardware
 */

static inline unsigned short _swapw(volatile unsigned short v)
{
#ifndef H8300_IO_NOSWAP
	unsigned short r;
	__asm__("xor.b %w0,%x0\n\t"
		"xor.b %x0,%w0\n\t"
		"xor.b %w0,%x0"
		:"=r"(r)
		:"0"(v));
	return r;
#else
	return v;
#endif
}

static inline unsigned long _swapl(volatile unsigned long v)
{
#ifndef H8300_IO_NOSWAP
	unsigned long r;
	__asm__("xor.b %w0,%x0\n\t"
		"xor.b %x0,%w0\n\t"
		"xor.b %w0,%x0\n\t"
		"xor.w %e0,%f0\n\t"
		"xor.w %f0,%e0\n\t"
		"xor.w %e0,%f0\n\t"
		"xor.b %w0,%x0\n\t"
		"xor.b %x0,%w0\n\t"
		"xor.b %w0,%x0"
		:"=r"(r)
		:"0"(v));
	return r;
#else
	return v;
#endif
}

#define readb(addr) \
    ({ unsigned char __v = \
     *(volatile unsigned char *)((unsigned long)(addr) & 0x00ffffff); \
     __v; })
#define readw(addr) \
    ({ unsigned short __v = \
     *(volatile unsigned short *)((unsigned long)(addr) & 0x00ffffff); \
     __v; })
#define readl(addr) \
    ({ unsigned long __v = \
     *(volatile unsigned long *)((unsigned long)(addr) & 0x00ffffff); \
     __v; })

#define writeb(b,addr) (void)((*(volatile unsigned char *) \
                             ((unsigned long)(addr) & 0x00ffffff)) = (b))
#define writew(b,addr) (void)((*(volatile unsigned short *) \
                             ((unsigned long)(addr) & 0x00ffffff)) = (b))
#define writel(b,addr) (void)((*(volatile unsigned long *) \
                             ((unsigned long)(addr) & 0x00ffffff)) = (b))
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

static inline int h8300_buswidth(unsigned int addr)
{
	return (*(volatile unsigned char *)ABWCR & (1 << ((addr >> 21) & 7))) == 0;
}

static inline void io_outsb(unsigned int addr, const void *buf, int len)
{
	volatile unsigned char  *ap_b = (volatile unsigned char *) addr;
	volatile unsigned short *ap_w = (volatile unsigned short *) addr;
	unsigned char *bp = (unsigned char *) buf;

	if(h8300_buswidth(addr) && (addr & 1)) {
		while (len--)
			*ap_w = *bp++;
	} else {
		while (len--)
			*ap_b = *bp++;
	}
}

static inline void io_outsw(unsigned int addr, const void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *) addr;
	unsigned short *bp = (unsigned short *) buf;
	while (len--)
		*ap = _swapw(*bp++);
}

static inline void io_outsl(unsigned int addr, const void *buf, int len)
{
	volatile unsigned long *ap = (volatile unsigned long *) addr;
	unsigned long *bp = (unsigned long *) buf;
	while (len--)
		*ap = _swapl(*bp++);
}

static inline void io_outsw_noswap(unsigned int addr, const void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *) addr;
	unsigned short *bp = (unsigned short *) buf;
	while (len--)
		*ap = *bp++;
}

static inline void io_outsl_noswap(unsigned int addr, const void *buf, int len)
{
	volatile unsigned long *ap = (volatile unsigned long *) addr;
	unsigned long *bp = (unsigned long *) buf;
	while (len--)
		*ap = *bp++;
}

static inline void io_insb(unsigned int addr, void *buf, int len)
{
	volatile unsigned char  *ap_b;
	volatile unsigned short *ap_w;
	unsigned char *bp = (unsigned char *) buf;

	if(h8300_buswidth(addr)) {
		ap_w = (volatile unsigned short *)(addr & ~1);
		while (len--)
			*bp++ = *ap_w & 0xff;
	} else {
		ap_b = (volatile unsigned char *)addr;
		while (len--)
			*bp++ = *ap_b;
	}
}

static inline void io_insw(unsigned int addr, void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *) addr;
	unsigned short *bp = (unsigned short *) buf;
	while (len--)
		*bp++ = _swapw(*ap);
}

static inline void io_insl(unsigned int addr, void *buf, int len)
{
	volatile unsigned long *ap = (volatile unsigned long *) addr;
	unsigned long *bp = (unsigned long *) buf;
	while (len--)
		*bp++ = _swapl(*ap);
}

static inline void io_insw_noswap(unsigned int addr, void *buf, int len)
{
	volatile unsigned short *ap = (volatile unsigned short *) addr;
	unsigned short *bp = (unsigned short *) buf;
	while (len--)
		*bp++ = *ap;
}

static inline void io_insl_noswap(unsigned int addr, void *buf, int len)
{
	volatile unsigned long *ap = (volatile unsigned long *) addr;
	unsigned long *bp = (unsigned long *) buf;
	while (len--)
		*bp++ = *ap;
}

/*
 *	make the short names macros so specific devices
 *	can override them as required
 */

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

#define mmiowb()

#define inb(addr)    ((h8300_buswidth(addr))?readw((addr) & ~1) & 0xff:readb(addr))
#define inw(addr)    _swapw(readw(addr))
#define inl(addr)    _swapl(readl(addr))
#define outb(x,addr) ((void)((h8300_buswidth(addr) && \
                      ((addr) & 1))?writew(x,(addr) & ~1):writeb(x,addr)))
#define outw(x,addr) ((void) writew(_swapw(x),addr))
#define outl(x,addr) ((void) writel(_swapl(x),addr))

#define inb_p(addr)    inb(addr)
#define inw_p(addr)    inw(addr)
#define inl_p(addr)    inl(addr)
#define outb_p(x,addr) outb(x,addr)
#define outw_p(x,addr) outw(x,addr)
#define outl_p(x,addr) outl(x,addr)

#define outsb(a,b,l) io_outsb(a,b,l)
#define outsw(a,b,l) io_outsw(a,b,l)
#define outsl(a,b,l) io_outsl(a,b,l)

#define insb(a,b,l) io_insb(a,b,l)
#define insw(a,b,l) io_insw(a,b,l)
#define insl(a,b,l) io_insl(a,b,l)

#define IO_SPACE_LIMIT 0xffffff


/* Values for nocacheflag and cmode */
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3

extern void *__ioremap(unsigned long physaddr, unsigned long size, int cacheflag);
extern void __iounmap(void *addr, unsigned long size);

static inline void *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void *ioremap_nocache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void *ioremap_writethrough(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
static inline void *ioremap_fullcache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

extern void iounmap(void *addr);

/* Nothing to do */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

/* H8/300 internal I/O functions */
static __inline__ unsigned char ctrl_inb(unsigned long addr)
{
	return *(volatile unsigned char*)addr;
}

static __inline__ unsigned short ctrl_inw(unsigned long addr)
{
	return *(volatile unsigned short*)addr;
}

static __inline__ unsigned long ctrl_inl(unsigned long addr)
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

static __inline__ void ctrl_outl(unsigned long b, unsigned long addr)
{
        *(volatile unsigned long*)addr = b;
}

/* Pages to physical address... */
#define page_to_phys(page)      ((page - mem_map) << PAGE_SHIFT)
#define page_to_bus(page)       ((page - mem_map) << PAGE_SHIFT)

/*
 * Macros used for converting between virtual and physical mappings.
 */
#define mm_ptov(vaddr)		((void *) (vaddr))
#define mm_vtop(vaddr)		((unsigned long) (vaddr))
#define phys_to_virt(vaddr)	((void *) (vaddr))
#define virt_to_phys(vaddr)	((unsigned long) (vaddr))

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

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

#endif /* _H8300_IO_H */
