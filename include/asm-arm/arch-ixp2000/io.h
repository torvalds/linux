/*
 * linux/include/asm-arm/arch-ixp2000/io.h
 *
 * Original Author: Naeem M Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002  Intel Corp.
 * Copyrgiht (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/hardware.h>

#define IO_SPACE_LIMIT		0xffffffff
#define __mem_pci(a)		(a)

/*
 * The A? revisions of the IXP2000s assert byte lanes for PCI I/O
 * transactions the other way round (MEM transactions don't have this
 * issue), so if we want to support those models, we need to override
 * the standard I/O functions.
 *
 * B0 and later have a bit that can be set to 1 to get the proper
 * behavior for I/O transactions, which then allows us to use the
 * standard I/O functions.  This is what we do if the user does not
 * explicitly ask for support for pre-B0.
 */
#ifdef CONFIG_IXP2000_SUPPORT_BROKEN_PCI_IO
#define ___io(p)		((void __iomem *)((p)+IXP2000_PCI_IO_VIRT_BASE))

#define alignb(addr)		(void __iomem *)((unsigned long)(addr) ^ 3)
#define alignw(addr)		(void __iomem *)((unsigned long)(addr) ^ 2)

#define outb(v,p)		__raw_writeb((v),alignb(___io(p)))
#define outw(v,p)		__raw_writew((v),alignw(___io(p)))
#define outl(v,p)		__raw_writel((v),___io(p))

#define inb(p)		({ unsigned int __v = __raw_readb(alignb(___io(p))); __v; })
#define inw(p)		\
	({ unsigned int __v = (__raw_readw(alignw(___io(p)))); __v; })
#define inl(p)		\
	({ unsigned int __v = (__raw_readl(___io(p))); __v; })

#define outsb(p,d,l)		__raw_writesb(alignb(___io(p)),d,l)
#define outsw(p,d,l)		__raw_writesw(alignw(___io(p)),d,l)
#define outsl(p,d,l)		__raw_writesl(___io(p),d,l)

#define insb(p,d,l)		__raw_readsb(alignb(___io(p)),d,l)
#define insw(p,d,l)		__raw_readsw(alignw(___io(p)),d,l)
#define insl(p,d,l)		__raw_readsl(___io(p),d,l)

#define __is_io_address(p)	((((unsigned long)(p)) & ~(IXP2000_PCI_IO_SIZE - 1)) == IXP2000_PCI_IO_VIRT_BASE)

#define ioread8(p)						\
	({							\
		unsigned int __v;				\
								\
		if (__is_io_address(p)) {			\
			__v = __raw_readb(alignb(p));		\
		} else {					\
			__v = __raw_readb(p);			\
		}						\
								\
		__v;						\
	})							\

#define ioread16(p)						\
	({							\
		unsigned int __v;				\
								\
		if (__is_io_address(p)) {			\
			__v = __raw_readw(alignw(p));		\
		} else {					\
			__v = le16_to_cpu(__raw_readw(p));	\
		}						\
								\
		__v;						\
	})

#define ioread32(p)						\
	({							\
		unsigned int __v;				\
								\
		if (__is_io_address(p)) {			\
			__v = __raw_readl(p);			\
		} else {					\
			__v = le32_to_cpu(__raw_readl(p));	\
		}						\
								\
		 __v;						\
	})

#define iowrite8(v,p)						\
	({							\
		if (__is_io_address(p)) {			\
			__raw_writeb((v), alignb(p));		\
		} else {					\
			__raw_writeb((v), p);			\
		}						\
	})

#define iowrite16(v,p)						\
	({							\
		if (__is_io_address(p)) {			\
			__raw_writew((v), alignw(p));		\
		} else {					\
			__raw_writew(cpu_to_le16(v), p);	\
		}						\
	})

#define iowrite32(v,p)						\
	({							\
		if (__is_io_address(p)) {			\
			__raw_writel((v), p);			\
		} else {					\
			__raw_writel(cpu_to_le32(v), p);	\
		}						\
	})

#define ioport_map(port, nr)	___io(port)

#define ioport_unmap(addr)
#else
#define __io(p)			((void __iomem *)((p)+IXP2000_PCI_IO_VIRT_BASE))
#endif


#ifdef CONFIG_ARCH_IXDP2X01
/*
 * This is an ugly hack but the CS8900 on the 2x01's does not sit in any sort
 * of "I/O space" and is just direct mapped into a 32-bit-only addressable
 * bus. The address space for this bus is such that we can't really easily
 * make it contiguous to the PCI I/O address range, and it also does not
 * need swapping like PCI addresses do (IXDP2x01 is a BE platform).
 * B/C of this we can't use the standard in/out functions and need to
 * runtime check if the incoming address is a PCI address or for
 * the CS89x0.
 */
#undef inw
#undef outw
#undef insw
#undef outsw

#include <asm/mach-types.h>

static inline void insw(u32 ptr, void *buf, int length)
{
	register volatile u32 *port = (volatile u32 *)ptr;

	/*
	 * Is this cycle meant for the CS8900?
	 */
	if ((machine_is_ixdp2401() || machine_is_ixdp2801()) && 
		(((u32)port >= (u32)IXDP2X01_CS8900_VIRT_BASE) &&
		 ((u32)port <= (u32)IXDP2X01_CS8900_VIRT_END))) {
		u8 *buf8 = (u8*)buf;
		register u32 tmp32;

		do {
			tmp32 = *port;
			*buf8++ = (u8)tmp32;
			*buf8++ = (u8)(tmp32 >> 8);
		} while(--length);

		return;
	}

	__raw_readsw(alignw(___io(ptr)),buf,length);
}

static inline void outsw(u32 ptr, void *buf, int length)
{
	register volatile u32 *port = (volatile u32 *)ptr;

	/*
	 * Is this cycle meant for the CS8900?
	 */
	if ((machine_is_ixdp2401() || machine_is_ixdp2801()) && 
		(((u32)port >= (u32)IXDP2X01_CS8900_VIRT_BASE) &&
		 ((u32)port <= (u32)IXDP2X01_CS8900_VIRT_END))) {
		register u32 tmp32;
		u8 *buf8 = (u8*)buf;
		do {
			tmp32 = *buf8++;
			tmp32 |= (*buf8++) << 8;
			*port = tmp32;
		} while(--length);
		return;
	}

	__raw_writesw(alignw(___io(ptr)),buf,length);
}


static inline u16 inw(u32 ptr)
{
	register volatile u32 *port = (volatile u32 *)ptr;

	/*
	 * Is this cycle meant for the CS8900?
	 */
	if ((machine_is_ixdp2401() || machine_is_ixdp2801()) && 
		(((u32)port >= (u32)IXDP2X01_CS8900_VIRT_BASE) &&
		 ((u32)port <= (u32)IXDP2X01_CS8900_VIRT_END))) {
		return (u16)(*port);  
	}

	return __raw_readw(alignw(___io(ptr)));
}

static inline void outw(u16 value, u32 ptr)
{
	register volatile u32 *port = (volatile u32 *)ptr;

	if ((machine_is_ixdp2401() || machine_is_ixdp2801()) && 
		(((u32)port >= (u32)IXDP2X01_CS8900_VIRT_BASE) &&
		 ((u32)port <= (u32)IXDP2X01_CS8900_VIRT_END))) {
		*port = value;  
		return;
	}

	__raw_writew((value),alignw(___io(ptr)));
}
#endif	/* IXDP2x01 */

#endif
