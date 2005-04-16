#ifndef _ASM_CRIS_IO_H
#define _ASM_CRIS_IO_H

#include <asm/page.h>   /* for __va, __pa */
#include <asm/arch/io.h>

/*
 * Change virtual addresses to physical addresses and vv.
 */

extern inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

extern inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}

extern void * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);

extern inline void * ioremap (unsigned long offset, unsigned long size)
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
 * differently. On the CRIS architecture, we just read/write the
 * memory location directly.
 */
#define readb(addr) (*(volatile unsigned char *) (addr))
#define readw(addr) (*(volatile unsigned short *) (addr))
#define readl(addr) (*(volatile unsigned int *) (addr))
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl

#define writeb(b,addr) ((*(volatile unsigned char *) (addr)) = (b))
#define writew(b,addr) ((*(volatile unsigned short *) (addr)) = (b))
#define writel(b,addr) ((*(volatile unsigned int *) (addr)) = (b))
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#define mmiowb()

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/*
 * Again, CRIS does not require mem IO specific function.
 */

#define eth_io_copy_and_sum(a,b,c,d)	eth_copy_and_sum((a),(void *)(b),(c),(d))

/* The following is junk needed for the arch-independent code but which
 * we never use in the CRIS port
 */

#define IO_SPACE_LIMIT 0xffff
#define inb(x) (0)
#define inw(x) (0)
#define inl(x) (0)
#define outb(x,y)
#define outw(x,y)
#define outl(x,y)
#define insb(x,y,z)
#define insw(x,y,z)
#define insl(x,y,z)
#define outsb(x,y,z)
#define outsw(x,y,z)
#define outsl(x,y,z)

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif
