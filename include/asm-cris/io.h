#ifndef _ASM_CRIS_IO_H
#define _ASM_CRIS_IO_H

#include <asm/page.h>   /* for __va, __pa */
#include <asm/arch/io.h>
#include <linux/kernel.h>

struct cris_io_operations
{
	u32 (*read_mem)(void *addr, int size);
	void (*write_mem)(u32 val, int size, void *addr);
	u32 (*read_io)(u32 port, void *addr, int size, int count);
	void (*write_io)(u32 port, void *addr, int size, int count);
};

#ifdef CONFIG_PCI
extern struct cris_io_operations *cris_iops;
#else
#define cris_iops ((struct cris_io_operations*)NULL)
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 */

static inline unsigned long virt_to_phys(volatile void * address)
{
	return __pa(address);
}

static inline void * phys_to_virt(unsigned long address)
{
	return __va(address);
}

extern void __iomem * __ioremap(unsigned long offset, unsigned long size, unsigned long flags);
extern void __iomem * __ioremap_prot(unsigned long phys_addr, unsigned long size, pgprot_t prot);

static inline void __iomem * ioremap (unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size, 0);
}

extern void iounmap(volatile void * __iomem addr);

extern void __iomem * ioremap_nocache(unsigned long offset, unsigned long size);

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
#ifdef CONFIG_PCI
#define PCI_SPACE(x) ((((unsigned)(x)) & 0x10000000) == 0x10000000)
#else
#define PCI_SPACE(x) 0
#endif
static inline unsigned char readb(const volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		return cris_iops->read_mem((void*)addr, 1);
	else
		return *(volatile unsigned char __force *) addr;
}
static inline unsigned short readw(const volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		return cris_iops->read_mem((void*)addr, 2);
	else
		return *(volatile unsigned short __force *) addr;
}
static inline unsigned int readl(const volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		return cris_iops->read_mem((void*)addr, 4);
	else
		return *(volatile unsigned int __force *) addr;
}
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl

static inline void writeb(unsigned char b, volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		cris_iops->write_mem(b, 1, (void*)addr);
	else
		*(volatile unsigned char __force *) addr = b;
}
static inline void writew(unsigned short b, volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		cris_iops->write_mem(b, 2, (void*)addr);
	else
		*(volatile unsigned short __force *) addr = b;
}
static inline void writel(unsigned int b, volatile void __iomem *addr)
{
	if (PCI_SPACE(addr) && cris_iops)
		cris_iops->write_mem(b, 4, (void*)addr);
	else
		*(volatile unsigned int __force *) addr = b;
}
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#define mmiowb()

#define memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))


/* I/O port access. Normally there is no I/O space on CRIS but when
 * Cardbus/PCI is enabled the request is passed through the bridge.
 */

#define IO_SPACE_LIMIT 0xffff
#define inb(port) (cris_iops ? cris_iops->read_io(port,NULL,1,1) : 0)
#define inw(port) (cris_iops ? cris_iops->read_io(port,NULL,2,1) : 0)
#define inl(port) (cris_iops ? cris_iops->read_io(port,NULL,4,1) : 0)
#define insb(port,addr,count) (cris_iops ? cris_iops->read_io(port,addr,1,count) : 0)
#define insw(port,addr,count) (cris_iops ? cris_iops->read_io(port,addr,2,count) : 0)
#define insl(port,addr,count) (cris_iops ? cris_iops->read_io(port,addr,4,count) : 0)
#define outb(data,port) if (cris_iops) cris_iops->write_io(port,(void*)(unsigned)data,1,1)
#define outw(data,port) if (cris_iops) cris_iops->write_io(port,(void*)(unsigned)data,2,1)
#define outl(data,port) if (cris_iops) cris_iops->write_io(port,(void*)(unsigned)data,4,1)
#define outsb(port,addr,count) if(cris_iops) cris_iops->write_io(port,(void*)addr,1,count)
#define outsw(port,addr,count) if(cris_iops) cris_iops->write_io(port,(void*)addr,2,count)
#define outsl(port,addr,count) if(cris_iops) cris_iops->write_io(port,(void*)addr,3,count)

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
