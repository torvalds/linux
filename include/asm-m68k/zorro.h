#ifndef _ASM_M68K_ZORRO_H
#define _ASM_M68K_ZORRO_H

#include <asm/raw_io.h>

#define z_readb raw_inb
#define z_readw raw_inw
#define z_readl raw_inl

#define z_writeb raw_outb
#define z_writew raw_outw
#define z_writel raw_outl

#define z_memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define z_memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define z_memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

static inline void *z_remap_nocache_ser(unsigned long physaddr,
					unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

static inline void *z_remap_nocache_nonser(unsigned long physaddr,
					   unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_NONSER);
}

static inline void *z_remap_writethrough(unsigned long physaddr,
					 unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
static inline void *z_remap_fullcache(unsigned long physaddr,
				      unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

#define z_unmap iounmap
#define z_iounmap iounmap
#define z_ioremap z_remap_nocache_ser

#endif /* _ASM_M68K_ZORRO_H */
