/*
 * linux/include/asm-arm/arch-ep93xx/io.h
 */

#define IO_SPACE_LIMIT		0xffffffff

#define __io(p)			((void __iomem *)(p))
#define __mem_pci(p)		(p)
