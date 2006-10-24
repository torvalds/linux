#ifndef __ASM_AVR32_IO_H
#define __ASM_AVR32_IO_H

#include <linux/string.h>

#ifdef __KERNEL__

#include <asm/addrspace.h>
#include <asm/byteorder.h>

/* virt_to_phys will only work when address is in P1 or P2 */
static __inline__ unsigned long virt_to_phys(volatile void *address)
{
	return PHYSADDR(address);
}

static __inline__ void * phys_to_virt(unsigned long address)
{
	return (void *)P1SEGADDR(address);
}

#define cached_to_phys(addr)	((unsigned long)PHYSADDR(addr))
#define uncached_to_phys(addr)	((unsigned long)PHYSADDR(addr))
#define phys_to_cached(addr)	((void *)P1SEGADDR(addr))
#define phys_to_uncached(addr)	((void *)P2SEGADDR(addr))

/*
 * Generic IO read/write.  These perform native-endian accesses.  Note
 * that some architectures will want to re-define __raw_{read,write}w.
 */
extern void __raw_writesb(unsigned int addr, const void *data, int bytelen);
extern void __raw_writesw(unsigned int addr, const void *data, int wordlen);
extern void __raw_writesl(unsigned int addr, const void *data, int longlen);

extern void __raw_readsb(unsigned int addr, void *data, int bytelen);
extern void __raw_readsw(unsigned int addr, void *data, int wordlen);
extern void __raw_readsl(unsigned int addr, void *data, int longlen);

static inline void writeb(unsigned char b, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *)addr = b;
}
static inline void writew(unsigned short b, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *)addr = b;
}
static inline void writel(unsigned int b, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *)addr = b;
}
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

static inline unsigned char readb(const volatile void __iomem *addr)
{
	return *(const volatile unsigned char __force *)addr;
}
static inline unsigned short readw(const volatile void __iomem *addr)
{
	return *(const volatile unsigned short __force *)addr;
}
static inline unsigned int readl(const volatile void __iomem *addr)
{
	return *(const volatile unsigned int __force *)addr;
}
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl

#define writesb(p, d, l)	__raw_writesb((unsigned int)p, d, l)
#define writesw(p, d, l)	__raw_writesw((unsigned int)p, d, l)
#define writesl(p, d, l)	__raw_writesl((unsigned int)p, d, l)

#define readsb(p, d, l)		__raw_readsb((unsigned int)p, d, l)
#define readsw(p, d, l)		__raw_readsw((unsigned int)p, d, l)
#define readsl(p, d, l)		__raw_readsl((unsigned int)p, d, l)


/*
 * io{read,write}{8,16,32} macros in both le (for PCI style consumers) and native be
 */
#ifndef ioread8

#define ioread8(p)	({ unsigned int __v = __raw_readb(p); __v; })

#define ioread16(p)	({ unsigned int __v = le16_to_cpu(__raw_readw(p)); __v; })
#define ioread16be(p)	({ unsigned int __v = be16_to_cpu(__raw_readw(p)); __v; })

#define ioread32(p)	({ unsigned int __v = le32_to_cpu(__raw_readl(p)); __v; })
#define ioread32be(p)	({ unsigned int __v = be32_to_cpu(__raw_readl(p)); __v; })

#define iowrite8(v,p)	__raw_writeb(v, p)

#define iowrite16(v,p)	__raw_writew(cpu_to_le16(v), p)
#define iowrite16be(v,p)	__raw_writew(cpu_to_be16(v), p)

#define iowrite32(v,p)	__raw_writel(cpu_to_le32(v), p)
#define iowrite32be(v,p)	__raw_writel(cpu_to_be32(v), p)

#define ioread8_rep(p,d,c)	__raw_readsb(p,d,c)
#define ioread16_rep(p,d,c)	__raw_readsw(p,d,c)
#define ioread32_rep(p,d,c)	__raw_readsl(p,d,c)

#define iowrite8_rep(p,s,c)	__raw_writesb(p,s,c)
#define iowrite16_rep(p,s,c)	__raw_writesw(p,s,c)
#define iowrite32_rep(p,s,c)	__raw_writesl(p,s,c)

#endif


/*
 * These two are only here because ALSA _thinks_ it needs them...
 */
static inline void memcpy_fromio(void * to, const volatile void __iomem *from,
				 unsigned long count)
{
	char *p = to;
	while (count) {
		count--;
		*p = readb(from);
		p++;
		from++;
	}
}

static inline void  memcpy_toio(volatile void __iomem *to, const void * from,
				unsigned long count)
{
	const char *p = from;
	while (count) {
		count--;
		writeb(*p, to);
		p++;
		to++;
	}
}

static inline void memset_io(volatile void __iomem *addr, unsigned char val,
			     unsigned long count)
{
	memset((void __force *)addr, val, count);
}

/*
 * Bad read/write accesses...
 */
extern void __readwrite_bug(const char *fn);

#define IO_SPACE_LIMIT	0xffffffff

/* Convert I/O port address to virtual address */
#define __io(p)		((void __iomem *)phys_to_uncached(p))

/*
 *  IO port access primitives
 *  -------------------------
 *
 * The AVR32 doesn't have special IO access instructions; all IO is memory
 * mapped. Note that these are defined to perform little endian accesses
 * only. Their primary purpose is to access PCI and ISA peripherals.
 *
 * Note that for a big endian machine, this implies that the following
 * big endian mode connectivity is in place.
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
#define outb(v, p)		__raw_writeb(v, __io(p))
#define outw(v, p)		__raw_writew(cpu_to_le16(v), __io(p))
#define outl(v, p)		__raw_writel(cpu_to_le32(v), __io(p))

#define inb(p)			__raw_readb(__io(p))
#define inw(p)			le16_to_cpu(__raw_readw(__io(p)))
#define inl(p)			le32_to_cpu(__raw_readl(__io(p)))

static inline void __outsb(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outb(*(u8 *)addr, port);
		addr++;
	}
}

static inline void __insb(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u8 *)addr = inb(port);
		addr++;
	}
}

static inline void __outsw(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outw(*(u16 *)addr, port);
		addr += 2;
	}
}

static inline void __insw(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u16 *)addr = inw(port);
		addr += 2;
	}
}

static inline void __outsl(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		outl(*(u32 *)addr, port);
		addr += 4;
	}
}

static inline void __insl(unsigned long port, void *addr, unsigned int count)
{
	while (count--) {
		*(u32 *)addr = inl(port);
		addr += 4;
	}
}

#define outsb(port, addr, count)	__outsb(port, addr, count)
#define insb(port, addr, count)		__insb(port, addr, count)
#define outsw(port, addr, count)	__outsw(port, addr, count)
#define insw(port, addr, count)		__insw(port, addr, count)
#define outsl(port, addr, count)	__outsl(port, addr, count)
#define insl(port, addr, count)		__insl(port, addr, count)

extern void __iomem *__ioremap(unsigned long offset, size_t size,
			       unsigned long flags);
extern void __iounmap(void __iomem *addr);

/*
 * ioremap	-   map bus memory into CPU space
 * @offset	bus address of the memory
 * @size	size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to make
 * bus memory CPU accessible via the readb/.../writel functions and
 * the other mmio helpers. The returned address is not guaranteed to
 * be usable directly as a virtual address.
 */
#define ioremap(offset, size)			\
	__ioremap((offset), (size), 0)

#define iounmap(addr)				\
	__iounmap(addr)

#define cached(addr) P1SEGADDR(addr)
#define uncached(addr) P2SEGADDR(addr)

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#define page_to_bus page_to_phys
#define bus_to_page phys_to_page

#define dma_cache_wback_inv(_start, _size)	\
	flush_dcache_region(_start, _size)
#define dma_cache_inv(_start, _size)		\
	invalidate_dcache_region(_start, _size)
#define dma_cache_wback(_start, _size)		\
	clean_dcache_region(_start, _size)

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)    __va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)   p

#endif /* __KERNEL__ */

#endif /* __ASM_AVR32_IO_H */
