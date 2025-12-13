/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_ASM_X86_IO_H
#define _TOOLS_ASM_X86_IO_H

#include <linux/compiler.h>
#include <linux/types.h>
#include "special_insns.h"

#define build_mmio_read(name, size, type, reg, barrier) \
static inline type name(const volatile void __iomem *addr) \
{ type ret; asm volatile("mov" size " %1,%0":reg (ret) \
:"m" (*(volatile type __force *)addr) barrier); return ret; }

#define build_mmio_write(name, size, type, reg, barrier) \
static inline void name(type val, volatile void __iomem *addr) \
{ asm volatile("mov" size " %0,%1": :reg (val), \
"m" (*(volatile type __force *)addr) barrier); }

build_mmio_read(readb, "b", unsigned char, "=q", :"memory")
build_mmio_read(readw, "w", unsigned short, "=r", :"memory")
build_mmio_read(readl, "l", unsigned int, "=r", :"memory")

build_mmio_read(__readb, "b", unsigned char, "=q", )
build_mmio_read(__readw, "w", unsigned short, "=r", )
build_mmio_read(__readl, "l", unsigned int, "=r", )

build_mmio_write(writeb, "b", unsigned char, "q", :"memory")
build_mmio_write(writew, "w", unsigned short, "r", :"memory")
build_mmio_write(writel, "l", unsigned int, "r", :"memory")

build_mmio_write(__writeb, "b", unsigned char, "q", )
build_mmio_write(__writew, "w", unsigned short, "r", )
build_mmio_write(__writel, "l", unsigned int, "r", )

#define readb readb
#define readw readw
#define readl readl
#define readb_relaxed(a) __readb(a)
#define readw_relaxed(a) __readw(a)
#define readl_relaxed(a) __readl(a)
#define __raw_readb __readb
#define __raw_readw __readw
#define __raw_readl __readl

#define writeb writeb
#define writew writew
#define writel writel
#define writeb_relaxed(v, a) __writeb(v, a)
#define writew_relaxed(v, a) __writew(v, a)
#define writel_relaxed(v, a) __writel(v, a)
#define __raw_writeb __writeb
#define __raw_writew __writew
#define __raw_writel __writel

#ifdef __x86_64__

build_mmio_read(readq, "q", u64, "=r", :"memory")
build_mmio_read(__readq, "q", u64, "=r", )
build_mmio_write(writeq, "q", u64, "r", :"memory")
build_mmio_write(__writeq, "q", u64, "r", )

#define readq_relaxed(a)	__readq(a)
#define writeq_relaxed(v, a)	__writeq(v, a)

#define __raw_readq		__readq
#define __raw_writeq		__writeq

/* Let people know that we have them */
#define readq			readq
#define writeq			writeq

#endif /* __x86_64__ */

#include <asm-generic/io.h>

/**
 * iosubmit_cmds512 - copy data to single MMIO location, in 512-bit units
 * @dst: destination, in MMIO space (must be 512-bit aligned)
 * @src: source
 * @count: number of 512 bits quantities to submit
 *
 * Submit data from kernel space to MMIO space, in units of 512 bits at a
 * time.  Order of access is not guaranteed, nor is a memory barrier
 * performed afterwards.
 *
 * Warning: Do not use this helper unless your driver has checked that the CPU
 * instruction is supported on the platform.
 */
static inline void iosubmit_cmds512(void __iomem *dst, const void *src,
				    size_t count)
{
	const u8 *from = src;
	const u8 *end = from + count * 64;

	while (from < end) {
		movdir64b(dst, from);
		from += 64;
	}
}

#endif /* _TOOLS_ASM_X86_IO_H */
