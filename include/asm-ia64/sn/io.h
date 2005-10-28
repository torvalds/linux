/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_SN_IO_H
#define _ASM_SN_IO_H
#include <linux/compiler.h>
#include <asm/intrinsics.h>

extern void * sn_io_addr(unsigned long port) __attribute_const__; /* Forward definition */
extern void __sn_mmiowb(void); /* Forward definition */

extern int num_cnodes;

#define __sn_mf_a()   ia64_mfa()

extern void sn_dma_flush(unsigned long);

#define __sn_inb ___sn_inb
#define __sn_inw ___sn_inw
#define __sn_inl ___sn_inl
#define __sn_outb ___sn_outb
#define __sn_outw ___sn_outw
#define __sn_outl ___sn_outl
#define __sn_readb ___sn_readb
#define __sn_readw ___sn_readw
#define __sn_readl ___sn_readl
#define __sn_readq ___sn_readq
#define __sn_readb_relaxed ___sn_readb_relaxed
#define __sn_readw_relaxed ___sn_readw_relaxed
#define __sn_readl_relaxed ___sn_readl_relaxed
#define __sn_readq_relaxed ___sn_readq_relaxed

/*
 * Convenience macros for setting/clearing bits using the above accessors
 */

#define __sn_setq_relaxed(addr, val) \
	writeq((__sn_readq_relaxed(addr) | (val)), (addr))
#define __sn_clrq_relaxed(addr, val) \
	writeq((__sn_readq_relaxed(addr) & ~(val)), (addr))

/*
 * The following routines are SN Platform specific, called when
 * a reference is made to inX/outX set macros.  SN Platform
 * inX set of macros ensures that Posted DMA writes on the
 * Bridge is flushed.
 *
 * The routines should be self explainatory.
 */

static inline unsigned int
___sn_inb (unsigned long port)
{
	volatile unsigned char *addr;
	unsigned char ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline unsigned int
___sn_inw (unsigned long port)
{
	volatile unsigned short *addr;
	unsigned short ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline unsigned int
___sn_inl (unsigned long port)
{
	volatile unsigned int *addr;
	unsigned int ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline void
___sn_outb (unsigned char val, unsigned long port)
{
	volatile unsigned char *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		__sn_mmiowb();
	}
}

static inline void
___sn_outw (unsigned short val, unsigned long port)
{
	volatile unsigned short *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		__sn_mmiowb();
	}
}

static inline void
___sn_outl (unsigned int val, unsigned long port)
{
	volatile unsigned int *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		__sn_mmiowb();
	}
}

/*
 * The following routines are SN Platform specific, called when 
 * a reference is made to readX/writeX set macros.  SN Platform 
 * readX set of macros ensures that Posted DMA writes on the 
 * Bridge is flushed.
 * 
 * The routines should be self explainatory.
 */

static inline unsigned char
___sn_readb (const volatile void __iomem *addr)
{
	unsigned char val;

	val = *(volatile unsigned char __force *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned short
___sn_readw (const volatile void __iomem *addr)
{
	unsigned short val;

	val = *(volatile unsigned short __force *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned int
___sn_readl (const volatile void __iomem *addr)
{
	unsigned int val;

	val = *(volatile unsigned int __force *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned long
___sn_readq (const volatile void __iomem *addr)
{
	unsigned long val;

	val = *(volatile unsigned long __force *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

/*
 * For generic and SN2 kernels, we have a set of fast access
 * PIO macros.	These macros are provided on SN Platform
 * because the normal inX and readX macros perform an
 * additional task of flushing Post DMA request on the Bridge.
 *
 * These routines should be self explainatory.
 */

static inline unsigned int
sn_inb_fast (unsigned long port)
{
	volatile unsigned char *addr = (unsigned char *)port;
	unsigned char ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned int
sn_inw_fast (unsigned long port)
{
	volatile unsigned short *addr = (unsigned short *)port;
	unsigned short ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned int
sn_inl_fast (unsigned long port)
{
	volatile unsigned int *addr = (unsigned int *)port;
	unsigned int ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned char
___sn_readb_relaxed (const volatile void __iomem *addr)
{
	return *(volatile unsigned char __force *)addr;
}

static inline unsigned short
___sn_readw_relaxed (const volatile void __iomem *addr)
{
	return *(volatile unsigned short __force *)addr;
}

static inline unsigned int
___sn_readl_relaxed (const volatile void __iomem *addr)
{
	return *(volatile unsigned int __force *) addr;
}

static inline unsigned long
___sn_readq_relaxed (const volatile void __iomem *addr)
{
	return *(volatile unsigned long __force *) addr;
}

struct pci_dev;

static inline int
sn_pci_set_vchan(struct pci_dev *pci_dev, unsigned long *addr, int vchan)
{

	if (vchan > 1) {
		return -1;
	}

	if (!(*addr >> 32))	/* Using a mask here would be cleaner */
		return 0;	/* but this generates better code */

	if (vchan == 1) {
		/* Set Bit 57 */
		*addr |= (1UL << 57);
	} else {
		/* Clear Bit 57 */
		*addr &= ~(1UL << 57);
	}

	return 0;
}

#endif	/* _ASM_SN_IO_H */
