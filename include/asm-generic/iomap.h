/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GENERIC_IO_H
#define __GENERIC_IO_H

#include <linux/linkage.h>
#include <asm/byteorder.h>

/*
 * These are the "generic" interfaces for doing new-style
 * memory-mapped or PIO accesses. Architectures may do
 * their own arch-optimized versions, these just act as
 * wrappers around the old-style IO register access functions:
 * read[bwl]/write[bwl]/in[bwl]/out[bwl]
 *
 * Don't include this directly, include it from <asm/io.h>.
 */

/*
 * Read/write from/to an (offsettable) iomem cookie. It might be a PIO
 * access or a MMIO access, these functions don't care. The info is
 * encoded in the hardware mapping set up by the mapping functions
 * (or the cookie itself, depending on implementation and hw).
 *
 * The generic routines just encode the PIO/MMIO as part of the
 * cookie, and coldly assume that the MMIO IO mappings are not
 * in the low address range. Architectures for which this is not
 * true can't use this generic implementation.
 */
extern unsigned int ioread8(const void __iomem *);
extern unsigned int ioread16(const void __iomem *);
extern unsigned int ioread16be(const void __iomem *);
extern unsigned int ioread32(const void __iomem *);
extern unsigned int ioread32be(const void __iomem *);

extern u64 __ioread64_lo_hi(const void __iomem *addr);
extern u64 __ioread64_hi_lo(const void __iomem *addr);
extern u64 __ioread64be_lo_hi(const void __iomem *addr);
extern u64 __ioread64be_hi_lo(const void __iomem *addr);

extern void iowrite8(u8, void __iomem *);
extern void iowrite16(u16, void __iomem *);
extern void iowrite16be(u16, void __iomem *);
extern void iowrite32(u32, void __iomem *);
extern void iowrite32be(u32, void __iomem *);

extern void __iowrite64_lo_hi(u64 val, void __iomem *addr);
extern void __iowrite64_hi_lo(u64 val, void __iomem *addr);
extern void __iowrite64be_lo_hi(u64 val, void __iomem *addr);
extern void __iowrite64be_hi_lo(u64 val, void __iomem *addr);

/*
 * "string" versions of the above. Note that they
 * use native byte ordering for the accesses (on
 * the assumption that IO and memory agree on a
 * byte order, and CPU byteorder is irrelevant).
 *
 * They do _not_ update the port address. If you
 * want MMIO that copies stuff laid out in MMIO
 * memory across multiple ports, use "memcpy_toio()"
 * and friends.
 */
extern void ioread8_rep(const void __iomem *port, void *buf, unsigned long count);
extern void ioread16_rep(const void __iomem *port, void *buf, unsigned long count);
extern void ioread32_rep(const void __iomem *port, void *buf, unsigned long count);

extern void iowrite8_rep(void __iomem *port, const void *buf, unsigned long count);
extern void iowrite16_rep(void __iomem *port, const void *buf, unsigned long count);
extern void iowrite32_rep(void __iomem *port, const void *buf, unsigned long count);

#ifdef CONFIG_HAS_IOPORT_MAP
/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);
#endif

#ifndef ioremap_wc
#define ioremap_wc ioremap
#endif

#ifndef ioremap_wt
#define ioremap_wt ioremap
#endif

#ifndef ioremap_np
/* See the comment in asm-generic/io.h about ioremap_np(). */
#define ioremap_np ioremap_np
static inline void __iomem *ioremap_np(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

#include <asm-generic/pci_iomap.h>

#endif
