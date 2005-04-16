/*
 * linux/include/asm-arm/arch-pxa/io.h
 *
 * Copied from asm/arch/sa1100/io.h
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We don't actually have real ISA nor PCI buses, but there is so many
 * drivers out there that might just work if we fake them...
 */
#define __io(a)			((void __iomem *)(a))
#define __mem_pci(a)		(a)
#define __mem_isa(a)		(a)

#endif
