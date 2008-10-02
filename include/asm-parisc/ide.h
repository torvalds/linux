/*
 *  linux/include/asm-parisc/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the PARISC architecture specific IDE code.
 */

#ifndef __ASM_PARISC_IDE_H
#define __ASM_PARISC_IDE_H

#ifdef __KERNEL__

#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#define ide_request_region(from,extent,name)	request_region((from), (extent), (name))
#define ide_release_region(from,extent)		release_region((from), (extent))
/* Generic I/O and MEMIO string operations.  */

#define __ide_insw	insw
#define __ide_insl	insl
#define __ide_outsw	outsw
#define __ide_outsl	outsl

static __inline__ void __ide_mm_insw(void __iomem *port, void *addr, u32 count)
{
	while (count--) {
		*(u16 *)addr = __raw_readw(port);
		addr += 2;
	}
}

static __inline__ void __ide_mm_insl(void __iomem *port, void *addr, u32 count)
{
	while (count--) {
		*(u32 *)addr = __raw_readl(port);
		addr += 4;
	}
}

static __inline__ void __ide_mm_outsw(void __iomem *port, void *addr, u32 count)
{
	while (count--) {
		__raw_writew(*(u16 *)addr, port);
		addr += 2;
	}
}

static __inline__ void __ide_mm_outsl(void __iomem *port, void *addr, u32 count)
{
	while (count--) {
		__raw_writel(*(u32 *)addr, port);
		addr += 4;
	}
}

#endif /* __KERNEL__ */

#endif /* __ASM_PARISC_IDE_H */
