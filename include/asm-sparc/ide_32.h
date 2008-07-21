/* ide.h: SPARC PCI specific IDE glue.
 *
 * Copyright (C) 1997  David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998  Eddie C. Dost   (ecd@skynet.be)
 * Adaptation from sparc64 version to sparc by Pete Zaitcev.
 */

#ifndef _SPARC_IDE_H
#define _SPARC_IDE_H

#ifdef __KERNEL__

#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/psr.h>

#undef  MAX_HWIFS
#define MAX_HWIFS	2

#define __ide_insl(data_reg, buffer, wcount) \
	__ide_insw(data_reg, buffer, (wcount)<<1)
#define __ide_outsl(data_reg, buffer, wcount) \
	__ide_outsw(data_reg, buffer, (wcount)<<1)

/* On sparc, I/O ports and MMIO registers are accessed identically.  */
#define __ide_mm_insw	__ide_insw
#define __ide_mm_insl	__ide_insl
#define __ide_mm_outsw	__ide_outsw
#define __ide_mm_outsl	__ide_outsl

static inline void __ide_insw(void __iomem *port, void *dst, u32 count)
{
	/* unsigned long end = (unsigned long)dst + (count << 1); */ /* P3 */
	u16 *ps = dst;
	u32 *pi;

	if(((unsigned long)ps) & 0x2) {
		*ps++ = __raw_readw(port);
		count--;
	}
	pi = (u32 *)ps;
	while(count >= 2) {
		u32 w;

		w  = __raw_readw(port) << 16;
		w |= __raw_readw(port);
		*pi++ = w;
		count -= 2;
	}
	ps = (u16 *)pi;
	if(count)
		*ps++ = __raw_readw(port);

	/* __flush_dcache_range((unsigned long)dst, end); */ /* P3 see hme */
}

static inline void __ide_outsw(void __iomem *port, const void *src, u32 count)
{
	/* unsigned long end = (unsigned long)src + (count << 1); */
	const u16 *ps = src;
	const u32 *pi;

	if(((unsigned long)src) & 0x2) {
		__raw_writew(*ps++, port);
		count--;
	}
	pi = (const u32 *)ps;
	while(count >= 2) {
		u32 w;

		w = *pi++;
		__raw_writew((w >> 16), port);
		__raw_writew(w, port);
		count -= 2;
	}
	ps = (const u16 *)pi;
	if(count)
		__raw_writew(*ps, port);

	/* __flush_dcache_range((unsigned long)src, end); */ /* P3 see hme */
}

#endif /* __KERNEL__ */

#endif /* _SPARC_IDE_H */
