/*
 *  include/asm-s390/io.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/io.h"
 */

#ifndef _S390_IO_H
#define _S390_IO_H

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#include <asm/page.h>

#define IO_SPACE_LIMIT 0xffffffff

#define __io_virt(x)            ((void *)(PAGE_OFFSET | (unsigned long)(x)))

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
static inline unsigned long virt_to_phys(volatile void * address)
{
	unsigned long real_address;
	asm volatile(
#ifndef __s390x__
		 "	lra	%0,0(%1)\n"
#else /* __s390x__ */
		 "	lrag	%0,0(%1)\n"
#endif /* __s390x__ */
		 "	jz	0f\n"
		 "	la	%0,0\n"
                 "0:"
		 : "=a" (real_address) : "a" (address) : "cc");
        return real_address;
}

static inline void * phys_to_virt(unsigned long address)
{
        return __io_virt(address);
}

extern void * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);

static inline void * ioremap (unsigned long offset, unsigned long size)
{
        return __ioremap(offset, size, 0);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
static inline void * ioremap_nocache (unsigned long offset, unsigned long size)
{
        return __ioremap(offset, size, 0);
}

extern void iounmap(void *addr);

/*
 * IO bus memory addresses are also 1:1 with the physical address
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently.
 */

#define readb(addr) (*(volatile unsigned char *) __io_virt(addr))
#define readw(addr) (*(volatile unsigned short *) __io_virt(addr))
#define readl(addr) (*(volatile unsigned int *) __io_virt(addr))
#define readq(addr) (*(volatile unsigned long long *) __io_virt(addr))

#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)
#define readq_relaxed(addr) readq(addr)
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_readq readq

#define writeb(b,addr) (*(volatile unsigned char *) __io_virt(addr) = (b))
#define writew(b,addr) (*(volatile unsigned short *) __io_virt(addr) = (b))
#define writel(b,addr) (*(volatile unsigned int *) __io_virt(addr) = (b))
#define writeq(b,addr) (*(volatile unsigned long long *) __io_virt(addr) = (b))
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq

#define memset_io(a,b,c)        memset(__io_virt(a),(b),(c))
#define memcpy_fromio(a,b,c)    memcpy((a),__io_virt(b),(c))
#define memcpy_toio(a,b,c)      memcpy(__io_virt(a),(b),(c))

#define inb_p(addr) readb(addr)
#define inb(addr) readb(addr)

#define outb(x,addr) ((void) writeb(x,addr))
#define outb_p(x,addr) outb(x,addr)

#define mmiowb()	do { } while (0)

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
