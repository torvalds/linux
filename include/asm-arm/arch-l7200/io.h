/*
 * linux/include/asm-arm/arch-l7200/io.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  03-21-2000	SJH	Created from linux/include/asm-arm/arch-nexuspci/io.h
 *  08-31-2000	SJH	Added in IO functions necessary for new drivers
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

/*
 * There are not real ISA nor PCI buses, so we fake it.
 */
#define __io_pci(a)		((void __iomem *)(PCIO_BASE + (a)))
#define __mem_pci(a)		(a)
#define __mem_isa(a)		(a)

#define __ioaddr(p)             __io_pci(p)

/*
 * Generic virtual read/write
 */
#define __arch_getb(a)          (*(volatile unsigned char *)(a))
#define __arch_getl(a)          (*(volatile unsigned int  *)(a))

static inline unsigned int __arch_getw(unsigned long a)
{
	unsigned int value;
	__asm__ __volatile__("ldr%?h    %0, [%1, #0]    @ getw"
		: "=&r" (value)
		: "r" (a));
	return value;
}

#define __arch_putb(v,a)        (*(volatile unsigned char *)(a) = (v))
#define __arch_putl(v,a)        (*(volatile unsigned int  *)(a) = (v))

static inline void __arch_putw(unsigned int value, unsigned long a)
{
        __asm__ __volatile__("str%?h    %0, [%1, #0]    @ putw"
                : : "r" (value), "r" (a));
}

/*
 * Translated address IO functions
 *
 * IO address has already been translated to a virtual address
 */
#define outb_t(v,p)		(*(volatile unsigned char *)(p) = (v))
#define inb_t(p)		(*(volatile unsigned char *)(p))
#define outw_t(v,p)		(*(volatile unsigned int *)(p) = (v))
#define inw_t(p)		(*(volatile unsigned int *)(p))
#define outl_t(v,p)		(*(volatile unsigned long *)(p) = (v))
#define inl_t(p)		(*(volatile unsigned long *)(p))

/*
 * FIXME - These are to allow for linking. On all the other
 *         ARM platforms, the entire IO space is contiguous.
 *         The 7200 has three separate IO spaces. The below
 *         macros will eventually become more involved. Use
 *         with caution and don't be surprised by kernel oopses!!!
 */
#define inb(p)		 	inb_t(p)
#define inw(p)	 		inw_t(p)
#define inl(p)	 		inl_t(p)
#define outb(v,p)		outb_t(v,p)
#define outw(v,p)		outw_t(v,p)
#define outl(v,p)		outl_t(v,p)

#endif
