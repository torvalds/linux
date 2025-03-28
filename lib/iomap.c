// SPDX-License-Identifier: GPL-2.0
/*
 * Implement the default iomap interfaces
 *
 * (C) Copyright 2004 Linus Torvalds
 */
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/kmsan-checks.h>

#include <linux/export.h>

/*
 * Read/write from/to an (offsettable) iomem cookie. It might be a PIO
 * access or a MMIO access, these functions don't care. The info is
 * encoded in the hardware mapping set up by the mapping functions
 * (or the cookie itself, depending on implementation and hw).
 *
 * The generic routines don't assume any hardware mappings, and just
 * encode the PIO/MMIO as part of the cookie. They coldly assume that
 * the MMIO IO mappings are not in the low address range.
 *
 * Architectures for which this is not true can't use this generic
 * implementation and should do their own copy.
 */

#ifndef HAVE_ARCH_PIO_SIZE
/*
 * We encode the physical PIO addresses (0-0xffff) into the
 * pointer by offsetting them with a constant (0x10000) and
 * assuming that all the low addresses are always PIO. That means
 * we can do some sanity checks on the low bits, and don't
 * need to just take things for granted.
 */
#define PIO_OFFSET	0x10000UL
#define PIO_MASK	0x0ffffUL
#define PIO_RESERVED	0x40000UL
#endif

static void bad_io_access(unsigned long port, const char *access)
{
	static int count = 10;
	if (count) {
		count--;
		WARN(1, KERN_ERR "Bad IO access at port %#lx (%s)\n", port, access);
	}
}

/*
 * Ugly macros are a way of life.
 */
#define IO_COND(addr, is_pio, is_mmio) do {			\
	unsigned long port = (unsigned long __force)addr;	\
	if (port >= PIO_RESERVED) {				\
		is_mmio;					\
	} else if (port > PIO_OFFSET) {				\
		port &= PIO_MASK;				\
		is_pio;						\
	} else							\
		bad_io_access(port, #is_pio );			\
} while (0)

#ifndef pio_read16be
#define pio_read16be(port) swab16(inw(port))
#define pio_read32be(port) swab32(inl(port))
#endif

#ifndef mmio_read16be
#define mmio_read16be(addr) swab16(readw(addr))
#define mmio_read32be(addr) swab32(readl(addr))
#define mmio_read64be(addr) swab64(readq(addr))
#endif

/*
 * Here and below, we apply __no_kmsan_checks to functions reading data from
 * hardware, to ensure that KMSAN marks their return values as initialized.
 */
__no_kmsan_checks
unsigned int ioread8(const void __iomem *addr)
{
	IO_COND(addr, return inb(port), return readb(addr));
	return 0xff;
}
__no_kmsan_checks
unsigned int ioread16(const void __iomem *addr)
{
	IO_COND(addr, return inw(port), return readw(addr));
	return 0xffff;
}
__no_kmsan_checks
unsigned int ioread16be(const void __iomem *addr)
{
	IO_COND(addr, return pio_read16be(port), return mmio_read16be(addr));
	return 0xffff;
}
__no_kmsan_checks
unsigned int ioread32(const void __iomem *addr)
{
	IO_COND(addr, return inl(port), return readl(addr));
	return 0xffffffff;
}
__no_kmsan_checks
unsigned int ioread32be(const void __iomem *addr)
{
	IO_COND(addr, return pio_read32be(port), return mmio_read32be(addr));
	return 0xffffffff;
}
EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread16be);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread32be);

#ifdef CONFIG_64BIT
static u64 pio_read64_lo_hi(unsigned long port)
{
	u64 lo, hi;

	lo = inl(port);
	hi = inl(port + sizeof(u32));

	return lo | (hi << 32);
}

static u64 pio_read64_hi_lo(unsigned long port)
{
	u64 lo, hi;

	hi = inl(port + sizeof(u32));
	lo = inl(port);

	return lo | (hi << 32);
}

static u64 pio_read64be_lo_hi(unsigned long port)
{
	u64 lo, hi;

	lo = pio_read32be(port + sizeof(u32));
	hi = pio_read32be(port);

	return lo | (hi << 32);
}

static u64 pio_read64be_hi_lo(unsigned long port)
{
	u64 lo, hi;

	hi = pio_read32be(port);
	lo = pio_read32be(port + sizeof(u32));

	return lo | (hi << 32);
}

__no_kmsan_checks
u64 __ioread64_lo_hi(const void __iomem *addr)
{
	IO_COND(addr, return pio_read64_lo_hi(port), return readq(addr));
	return 0xffffffffffffffffULL;
}

__no_kmsan_checks
u64 __ioread64_hi_lo(const void __iomem *addr)
{
	IO_COND(addr, return pio_read64_hi_lo(port), return readq(addr));
	return 0xffffffffffffffffULL;
}

__no_kmsan_checks
u64 __ioread64be_lo_hi(const void __iomem *addr)
{
	IO_COND(addr, return pio_read64be_lo_hi(port),
		return mmio_read64be(addr));
	return 0xffffffffffffffffULL;
}

__no_kmsan_checks
u64 __ioread64be_hi_lo(const void __iomem *addr)
{
	IO_COND(addr, return pio_read64be_hi_lo(port),
		return mmio_read64be(addr));
	return 0xffffffffffffffffULL;
}

EXPORT_SYMBOL(__ioread64_lo_hi);
EXPORT_SYMBOL(__ioread64_hi_lo);
EXPORT_SYMBOL(__ioread64be_lo_hi);
EXPORT_SYMBOL(__ioread64be_hi_lo);

#endif /* CONFIG_64BIT */

#ifndef pio_write16be
#define pio_write16be(val,port) outw(swab16(val),port)
#define pio_write32be(val,port) outl(swab32(val),port)
#endif

#ifndef mmio_write16be
#define mmio_write16be(val,port) writew(swab16(val),port)
#define mmio_write32be(val,port) writel(swab32(val),port)
#define mmio_write64be(val,port) writeq(swab64(val),port)
#endif

void iowrite8(u8 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, outb(val,port), writeb(val, addr));
}
void iowrite16(u16 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, outw(val,port), writew(val, addr));
}
void iowrite16be(u16 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write16be(val,port), mmio_write16be(val, addr));
}
void iowrite32(u32 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, outl(val,port), writel(val, addr));
}
void iowrite32be(u32 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write32be(val,port), mmio_write32be(val, addr));
}
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite16be);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite32be);

#ifdef CONFIG_64BIT
static void pio_write64_lo_hi(u64 val, unsigned long port)
{
	outl(val, port);
	outl(val >> 32, port + sizeof(u32));
}

static void pio_write64_hi_lo(u64 val, unsigned long port)
{
	outl(val >> 32, port + sizeof(u32));
	outl(val, port);
}

static void pio_write64be_lo_hi(u64 val, unsigned long port)
{
	pio_write32be(val, port + sizeof(u32));
	pio_write32be(val >> 32, port);
}

static void pio_write64be_hi_lo(u64 val, unsigned long port)
{
	pio_write32be(val >> 32, port);
	pio_write32be(val, port + sizeof(u32));
}

void __iowrite64_lo_hi(u64 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write64_lo_hi(val, port),
		writeq(val, addr));
}

void __iowrite64_hi_lo(u64 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write64_hi_lo(val, port),
		writeq(val, addr));
}

void __iowrite64be_lo_hi(u64 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write64be_lo_hi(val, port),
		mmio_write64be(val, addr));
}

void __iowrite64be_hi_lo(u64 val, void __iomem *addr)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(&val, sizeof(val));
	IO_COND(addr, pio_write64be_hi_lo(val, port),
		mmio_write64be(val, addr));
}

EXPORT_SYMBOL(__iowrite64_lo_hi);
EXPORT_SYMBOL(__iowrite64_hi_lo);
EXPORT_SYMBOL(__iowrite64be_lo_hi);
EXPORT_SYMBOL(__iowrite64be_hi_lo);

#endif /* CONFIG_64BIT */

/*
 * These are the "repeat MMIO read/write" functions.
 * Note the "__raw" accesses, since we don't want to
 * convert to CPU byte order. We write in "IO byte
 * order" (we also don't have IO barriers).
 */
#ifndef mmio_insb
static inline void mmio_insb(const void __iomem *addr, u8 *dst, int count)
{
	while (--count >= 0) {
		u8 data = __raw_readb(addr);
		*dst = data;
		dst++;
	}
}
static inline void mmio_insw(const void __iomem *addr, u16 *dst, int count)
{
	while (--count >= 0) {
		u16 data = __raw_readw(addr);
		*dst = data;
		dst++;
	}
}
static inline void mmio_insl(const void __iomem *addr, u32 *dst, int count)
{
	while (--count >= 0) {
		u32 data = __raw_readl(addr);
		*dst = data;
		dst++;
	}
}
#endif

#ifndef mmio_outsb
static inline void mmio_outsb(void __iomem *addr, const u8 *src, int count)
{
	while (--count >= 0) {
		__raw_writeb(*src, addr);
		src++;
	}
}
static inline void mmio_outsw(void __iomem *addr, const u16 *src, int count)
{
	while (--count >= 0) {
		__raw_writew(*src, addr);
		src++;
	}
}
static inline void mmio_outsl(void __iomem *addr, const u32 *src, int count)
{
	while (--count >= 0) {
		__raw_writel(*src, addr);
		src++;
	}
}
#endif

void ioread8_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	IO_COND(addr, insb(port,dst,count), mmio_insb(addr, dst, count));
	/* KMSAN must treat values read from devices as initialized. */
	kmsan_unpoison_memory(dst, count);
}
void ioread16_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	IO_COND(addr, insw(port,dst,count), mmio_insw(addr, dst, count));
	/* KMSAN must treat values read from devices as initialized. */
	kmsan_unpoison_memory(dst, count * 2);
}
void ioread32_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	IO_COND(addr, insl(port,dst,count), mmio_insl(addr, dst, count));
	/* KMSAN must treat values read from devices as initialized. */
	kmsan_unpoison_memory(dst, count * 4);
}
EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(ioread32_rep);

void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(src, count);
	IO_COND(addr, outsb(port, src, count), mmio_outsb(addr, src, count));
}
void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(src, count * 2);
	IO_COND(addr, outsw(port, src, count), mmio_outsw(addr, src, count));
}
void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	/* Make sure uninitialized memory isn't copied to devices. */
	kmsan_check_memory(src, count * 4);
	IO_COND(addr, outsl(port, src,count), mmio_outsl(addr, src, count));
}
EXPORT_SYMBOL(iowrite8_rep);
EXPORT_SYMBOL(iowrite16_rep);
EXPORT_SYMBOL(iowrite32_rep);

#ifdef CONFIG_HAS_IOPORT_MAP
/* Create a virtual mapping cookie for an IO port range */
void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	if (port > PIO_MASK)
		return NULL;
	return (void __iomem *) (unsigned long) (port + PIO_OFFSET);
}

void ioport_unmap(void __iomem *addr)
{
	/* Nothing to do */
}
EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);
#endif /* CONFIG_HAS_IOPORT_MAP */

#ifdef CONFIG_PCI
/* Hide the details if this is a MMIO or PIO address space and just do what
 * you expect in the correct way. */
void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
	IO_COND(addr, /* nothing */, iounmap(addr));
}
EXPORT_SYMBOL(pci_iounmap);
#endif /* CONFIG_PCI */
