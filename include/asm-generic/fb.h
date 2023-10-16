/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_FB_H_
#define __ASM_GENERIC_FB_H_

/*
 * Only include this header file from your architecture's <asm/fb.h>.
 */

#include <linux/io.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>

struct fb_info;

#ifndef pgprot_framebuffer
#define pgprot_framebuffer pgprot_framebuffer
static inline pgprot_t pgprot_framebuffer(pgprot_t prot,
					  unsigned long vm_start, unsigned long vm_end,
					  unsigned long offset)
{
	return pgprot_writecombine(prot);
}
#endif

#ifndef fb_is_primary_device
#define fb_is_primary_device fb_is_primary_device
static inline int fb_is_primary_device(struct fb_info *info)
{
	return 0;
}
#endif

/*
 * I/O helpers for the framebuffer. Prefer these functions over their
 * regular counterparts. The regular I/O functions provide in-order
 * access and swap bytes to/from little-endian ordering. Neither is
 * required for framebuffers. Instead, the helpers read and write
 * raw framebuffer data. Independent operations can be reordered for
 * improved performance.
 */

#ifndef fb_readb
static inline u8 fb_readb(const volatile void __iomem *addr)
{
	return __raw_readb(addr);
}
#define fb_readb fb_readb
#endif

#ifndef fb_readw
static inline u16 fb_readw(const volatile void __iomem *addr)
{
	return __raw_readw(addr);
}
#define fb_readw fb_readw
#endif

#ifndef fb_readl
static inline u32 fb_readl(const volatile void __iomem *addr)
{
	return __raw_readl(addr);
}
#define fb_readl fb_readl
#endif

#ifndef fb_readq
#if defined(__raw_readq)
static inline u64 fb_readq(const volatile void __iomem *addr)
{
	return __raw_readq(addr);
}
#define fb_readq fb_readq
#endif
#endif

#ifndef fb_writeb
static inline void fb_writeb(u8 b, volatile void __iomem *addr)
{
	__raw_writeb(b, addr);
}
#define fb_writeb fb_writeb
#endif

#ifndef fb_writew
static inline void fb_writew(u16 b, volatile void __iomem *addr)
{
	__raw_writew(b, addr);
}
#define fb_writew fb_writew
#endif

#ifndef fb_writel
static inline void fb_writel(u32 b, volatile void __iomem *addr)
{
	__raw_writel(b, addr);
}
#define fb_writel fb_writel
#endif

#ifndef fb_writeq
#if defined(__raw_writeq)
static inline void fb_writeq(u64 b, volatile void __iomem *addr)
{
	__raw_writeq(b, addr);
}
#define fb_writeq fb_writeq
#endif
#endif

#ifndef fb_memcpy_fromio
static inline void fb_memcpy_fromio(void *to, const volatile void __iomem *from, size_t n)
{
	memcpy_fromio(to, from, n);
}
#define fb_memcpy_fromio fb_memcpy_fromio
#endif

#ifndef fb_memcpy_toio
static inline void fb_memcpy_toio(volatile void __iomem *to, const void *from, size_t n)
{
	memcpy_toio(to, from, n);
}
#define fb_memcpy_toio fb_memcpy_toio
#endif

#ifndef fb_memset
static inline void fb_memset_io(volatile void __iomem *addr, int c, size_t n)
{
	memset_io(addr, c, n);
}
#define fb_memset fb_memset_io
#endif

#endif /* __ASM_GENERIC_FB_H_ */
