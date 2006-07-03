#ifndef __UM_IO_H
#define __UM_IO_H

#include "asm/page.h"

#define IO_SPACE_LIMIT 0xdeadbeef /* Sure hope nothing uses this */

static inline int inb(unsigned long i) { return(0); }
static inline void outb(char c, unsigned long i) { }

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa((void *) address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
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

static inline void writeb(unsigned char b, volatile void __iomem *addr)
{
	*(volatile unsigned char __force *) addr = b;
}
static inline void writew(unsigned short b, volatile void __iomem *addr)
{
	*(volatile unsigned short __force *) addr = b;
}
static inline void writel(unsigned int b, volatile void __iomem *addr)
{
	*(volatile unsigned int __force *) addr = b;
}
static inline void writeq(unsigned int b, volatile void __iomem *addr)
{
	*(volatile unsigned long long __force *) addr = b;
}
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq

#endif
