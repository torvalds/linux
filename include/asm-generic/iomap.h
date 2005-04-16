#ifndef __GENERIC_IO_H
#define __GENERIC_IO_H

#include <linux/linkage.h>

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
extern unsigned int fastcall ioread8(void __iomem *);
extern unsigned int fastcall ioread16(void __iomem *);
extern unsigned int fastcall ioread32(void __iomem *);

extern void fastcall iowrite8(u8, void __iomem *);
extern void fastcall iowrite16(u16, void __iomem *);
extern void fastcall iowrite32(u32, void __iomem *);

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
extern void fastcall ioread8_rep(void __iomem *port, void *buf, unsigned long count);
extern void fastcall ioread16_rep(void __iomem *port, void *buf, unsigned long count);
extern void fastcall ioread32_rep(void __iomem *port, void *buf, unsigned long count);

extern void fastcall iowrite8_rep(void __iomem *port, const void *buf, unsigned long count);
extern void fastcall iowrite16_rep(void __iomem *port, const void *buf, unsigned long count);
extern void fastcall iowrite32_rep(void __iomem *port, const void *buf, unsigned long count);

/* Create a virtual mapping cookie for an IO port range */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *);

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
struct pci_dev;
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);

#endif
