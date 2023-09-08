/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Generic I/O port emulation.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __ASM_GENERIC_IO_H
#define __ASM_GENERIC_IO_H

#include <asm/page.h> /* I/O is all done through memory accesses */
#include <linux/string.h> /* for memset() and memcpy() */
#include <linux/types.h>
#include <linux/instruction_pointer.h>

#ifdef CONFIG_GENERIC_IOMAP
#include <asm-generic/iomap.h>
#endif

#include <asm/mmiowb.h>
#include <asm-generic/pci_iomap.h>

#ifndef __io_br
#define __io_br()      barrier()
#endif

/* prevent prefetching of coherent DMA data ahead of a dma-complete */
#ifndef __io_ar
#ifdef rmb
#define __io_ar(v)      rmb()
#else
#define __io_ar(v)      barrier()
#endif
#endif

/* flush writes to coherent DMA data before possibly triggering a DMA read */
#ifndef __io_bw
#ifdef wmb
#define __io_bw()      wmb()
#else
#define __io_bw()      barrier()
#endif
#endif

/* serialize device access against a spin_unlock, usually handled there. */
#ifndef __io_aw
#define __io_aw()      mmiowb_set_pending()
#endif

#ifndef __io_pbw
#define __io_pbw()     __io_bw()
#endif

#ifndef __io_paw
#define __io_paw()     __io_aw()
#endif

#ifndef __io_pbr
#define __io_pbr()     __io_br()
#endif

#ifndef __io_par
#define __io_par(v)     __io_ar(v)
#endif

/*
 * "__DISABLE_TRACE_MMIO__" flag can be used to disable MMIO tracing for
 * specific kernel drivers in case of excessive/unwanted logging.
 *
 * Usage: Add a #define flag at the beginning of the driver file.
 * Ex: #define __DISABLE_TRACE_MMIO__
 *     #include <...>
 *     ...
 */
#if IS_ENABLED(CONFIG_TRACE_MMIO_ACCESS) && !(defined(__DISABLE_TRACE_MMIO__))
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(rwmmio_write);
DECLARE_TRACEPOINT(rwmmio_post_write);
DECLARE_TRACEPOINT(rwmmio_read);
DECLARE_TRACEPOINT(rwmmio_post_read);

void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
		    unsigned long caller_addr, unsigned long caller_addr0);
void log_post_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
			 unsigned long caller_addr, unsigned long caller_addr0);
void log_read_mmio(u8 width, const volatile void __iomem *addr,
		   unsigned long caller_addr, unsigned long caller_addr0);
void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr,
			unsigned long caller_addr, unsigned long caller_addr0);

#else

static inline void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
				  unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_post_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
				       unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_read_mmio(u8 width, const volatile void __iomem *addr,
				 unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr,
				      unsigned long caller_addr, unsigned long caller_addr0) {}

#endif /* CONFIG_TRACE_MMIO_ACCESS */

/*
 * __raw_{read,write}{b,w,l,q}() access memory in native endianness.
 *
 * On some architectures memory mapped IO needs to be accessed differently.
 * On the simple architectures, we just read/write the memory location
 * directly.
 */

#ifndef __raw_readb
#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 __force *)addr;
}
#endif

#ifndef __raw_readw
#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 __force *)addr;
}
#endif

#ifndef __raw_readl
#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 __force *)addr;
}
#endif

#ifdef CONFIG_64BIT
#ifndef __raw_readq
#define __raw_readq __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	return *(const volatile u64 __force *)addr;
}
#endif
#endif /* CONFIG_64BIT */

#ifndef __raw_writeb
#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 value, volatile void __iomem *addr)
{
	*(volatile u8 __force *)addr = value;
}
#endif

#ifndef __raw_writew
#define __raw_writew __raw_writew
static inline void __raw_writew(u16 value, volatile void __iomem *addr)
{
	*(volatile u16 __force *)addr = value;
}
#endif

#ifndef __raw_writel
#define __raw_writel __raw_writel
static inline void __raw_writel(u32 value, volatile void __iomem *addr)
{
	*(volatile u32 __force *)addr = value;
}
#endif

#ifdef CONFIG_64BIT
#ifndef __raw_writeq
#define __raw_writeq __raw_writeq
static inline void __raw_writeq(u64 value, volatile void __iomem *addr)
{
	*(volatile u64 __force *)addr = value;
}
#endif
#endif /* CONFIG_64BIT */

/*
 * {read,write}{b,w,l,q}() access little endian memory and return result in
 * native endianness.
 */

#ifndef readb
#define readb readb
static inline u8 readb(const volatile void __iomem *addr)
{
	u8 val;

	log_read_mmio(8, addr, _THIS_IP_, _RET_IP_);
	__io_br();
	val = __raw_readb(addr);
	__io_ar(val);
	log_post_read_mmio(val, 8, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifndef readw
#define readw readw
static inline u16 readw(const volatile void __iomem *addr)
{
	u16 val;

	log_read_mmio(16, addr, _THIS_IP_, _RET_IP_);
	__io_br();
	val = __le16_to_cpu((__le16 __force)__raw_readw(addr));
	__io_ar(val);
	log_post_read_mmio(val, 16, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifndef readl
#define readl readl
static inline u32 readl(const volatile void __iomem *addr)
{
	u32 val;

	log_read_mmio(32, addr, _THIS_IP_, _RET_IP_);
	__io_br();
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	__io_ar(val);
	log_post_read_mmio(val, 32, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifdef CONFIG_64BIT
#ifndef readq
#define readq readq
static inline u64 readq(const volatile void __iomem *addr)
{
	u64 val;

	log_read_mmio(64, addr, _THIS_IP_, _RET_IP_);
	__io_br();
	val = __le64_to_cpu((__le64 __force)__raw_readq(addr));
	__io_ar(val);
	log_post_read_mmio(val, 64, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif
#endif /* CONFIG_64BIT */

#ifndef writeb
#define writeb writeb
static inline void writeb(u8 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 8, addr, _THIS_IP_, _RET_IP_);
	__io_bw();
	__raw_writeb(value, addr);
	__io_aw();
	log_post_write_mmio(value, 8, addr, _THIS_IP_, _RET_IP_);
}
#endif

#ifndef writew
#define writew writew
static inline void writew(u16 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 16, addr, _THIS_IP_, _RET_IP_);
	__io_bw();
	__raw_writew((u16 __force)cpu_to_le16(value), addr);
	__io_aw();
	log_post_write_mmio(value, 16, addr, _THIS_IP_, _RET_IP_);
}
#endif

#ifndef writel
#define writel writel
static inline void writel(u32 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
	__io_bw();
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	__io_aw();
	log_post_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
}
#endif

#ifdef CONFIG_64BIT
#ifndef writeq
#define writeq writeq
static inline void writeq(u64 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 64, addr, _THIS_IP_, _RET_IP_);
	__io_bw();
	__raw_writeq((u64 __force)__cpu_to_le64(value), addr);
	__io_aw();
	log_post_write_mmio(value, 64, addr, _THIS_IP_, _RET_IP_);
}
#endif
#endif /* CONFIG_64BIT */

/*
 * {read,write}{b,w,l,q}_relaxed() are like the regular version, but
 * are not guaranteed to provide ordering against spinlocks or memory
 * accesses.
 */
#ifndef readb_relaxed
#define readb_relaxed readb_relaxed
static inline u8 readb_relaxed(const volatile void __iomem *addr)
{
	u8 val;

	log_read_mmio(8, addr, _THIS_IP_, _RET_IP_);
	val = __raw_readb(addr);
	log_post_read_mmio(val, 8, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifndef readw_relaxed
#define readw_relaxed readw_relaxed
static inline u16 readw_relaxed(const volatile void __iomem *addr)
{
	u16 val;

	log_read_mmio(16, addr, _THIS_IP_, _RET_IP_);
	val = __le16_to_cpu((__le16 __force)__raw_readw(addr));
	log_post_read_mmio(val, 16, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifndef readl_relaxed
#define readl_relaxed readl_relaxed
static inline u32 readl_relaxed(const volatile void __iomem *addr)
{
	u32 val;

	log_read_mmio(32, addr, _THIS_IP_, _RET_IP_);
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	log_post_read_mmio(val, 32, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#if defined(readq) && !defined(readq_relaxed)
#define readq_relaxed readq_relaxed
static inline u64 readq_relaxed(const volatile void __iomem *addr)
{
	u64 val;

	log_read_mmio(64, addr, _THIS_IP_, _RET_IP_);
	val = __le64_to_cpu((__le64 __force)__raw_readq(addr));
	log_post_read_mmio(val, 64, addr, _THIS_IP_, _RET_IP_);
	return val;
}
#endif

#ifndef writeb_relaxed
#define writeb_relaxed writeb_relaxed
static inline void writeb_relaxed(u8 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 8, addr, _THIS_IP_, _RET_IP_);
	__raw_writeb(value, addr);
	log_post_write_mmio(value, 8, addr, _THIS_IP_, _RET_IP_);
}
#endif

#ifndef writew_relaxed
#define writew_relaxed writew_relaxed
static inline void writew_relaxed(u16 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 16, addr, _THIS_IP_, _RET_IP_);
	__raw_writew((u16 __force)cpu_to_le16(value), addr);
	log_post_write_mmio(value, 16, addr, _THIS_IP_, _RET_IP_);
}
#endif

#ifndef writel_relaxed
#define writel_relaxed writel_relaxed
static inline void writel_relaxed(u32 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	log_post_write_mmio(value, 32, addr, _THIS_IP_, _RET_IP_);
}
#endif

#if defined(writeq) && !defined(writeq_relaxed)
#define writeq_relaxed writeq_relaxed
static inline void writeq_relaxed(u64 value, volatile void __iomem *addr)
{
	log_write_mmio(value, 64, addr, _THIS_IP_, _RET_IP_);
	__raw_writeq((u64 __force)__cpu_to_le64(value), addr);
	log_post_write_mmio(value, 64, addr, _THIS_IP_, _RET_IP_);
}
#endif

/*
 * {read,write}s{b,w,l,q}() repeatedly access the same memory address in
 * native endianness in 8-, 16-, 32- or 64-bit chunks (@count times).
 */
#ifndef readsb
#define readsb readsb
static inline void readsb(const volatile void __iomem *addr, void *buffer,
			  unsigned int count)
{
	if (count) {
		u8 *buf = buffer;

		do {
			u8 x = __raw_readb(addr);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifndef readsw
#define readsw readsw
static inline void readsw(const volatile void __iomem *addr, void *buffer,
			  unsigned int count)
{
	if (count) {
		u16 *buf = buffer;

		do {
			u16 x = __raw_readw(addr);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifndef readsl
#define readsl readsl
static inline void readsl(const volatile void __iomem *addr, void *buffer,
			  unsigned int count)
{
	if (count) {
		u32 *buf = buffer;

		do {
			u32 x = __raw_readl(addr);
			*buf++ = x;
		} while (--count);
	}
}
#endif

#ifdef CONFIG_64BIT
#ifndef readsq
#define readsq readsq
static inline void readsq(const volatile void __iomem *addr, void *buffer,
			  unsigned int count)
{
	if (count) {
		u64 *buf = buffer;

		do {
			u64 x = __raw_readq(addr);
			*buf++ = x;
		} while (--count);
	}
}
#endif
#endif /* CONFIG_64BIT */

#ifndef writesb
#define writesb writesb
static inline void writesb(volatile void __iomem *addr, const void *buffer,
			   unsigned int count)
{
	if (count) {
		const u8 *buf = buffer;

		do {
			__raw_writeb(*buf++, addr);
		} while (--count);
	}
}
#endif

#ifndef writesw
#define writesw writesw
static inline void writesw(volatile void __iomem *addr, const void *buffer,
			   unsigned int count)
{
	if (count) {
		const u16 *buf = buffer;

		do {
			__raw_writew(*buf++, addr);
		} while (--count);
	}
}
#endif

#ifndef writesl
#define writesl writesl
static inline void writesl(volatile void __iomem *addr, const void *buffer,
			   unsigned int count)
{
	if (count) {
		const u32 *buf = buffer;

		do {
			__raw_writel(*buf++, addr);
		} while (--count);
	}
}
#endif

#ifdef CONFIG_64BIT
#ifndef writesq
#define writesq writesq
static inline void writesq(volatile void __iomem *addr, const void *buffer,
			   unsigned int count)
{
	if (count) {
		const u64 *buf = buffer;

		do {
			__raw_writeq(*buf++, addr);
		} while (--count);
	}
}
#endif
#endif /* CONFIG_64BIT */

#ifndef PCI_IOBASE
#define PCI_IOBASE ((void __iomem *)0)
#endif

#ifndef IO_SPACE_LIMIT
#define IO_SPACE_LIMIT 0xffff
#endif

/*
 * {in,out}{b,w,l}() access little endian I/O. {in,out}{b,w,l}_p() can be
 * implemented on hardware that needs an additional delay for I/O accesses to
 * take effect.
 */

#if !defined(inb) && !defined(_inb)
#define _inb _inb
static inline u8 _inb(unsigned long addr)
{
	u8 val;

	__io_pbr();
	val = __raw_readb(PCI_IOBASE + addr);
	__io_par(val);
	return val;
}
#endif

#if !defined(inw) && !defined(_inw)
#define _inw _inw
static inline u16 _inw(unsigned long addr)
{
	u16 val;

	__io_pbr();
	val = __le16_to_cpu((__le16 __force)__raw_readw(PCI_IOBASE + addr));
	__io_par(val);
	return val;
}
#endif

#if !defined(inl) && !defined(_inl)
#define _inl _inl
static inline u32 _inl(unsigned long addr)
{
	u32 val;

	__io_pbr();
	val = __le32_to_cpu((__le32 __force)__raw_readl(PCI_IOBASE + addr));
	__io_par(val);
	return val;
}
#endif

#if !defined(outb) && !defined(_outb)
#define _outb _outb
static inline void _outb(u8 value, unsigned long addr)
{
	__io_pbw();
	__raw_writeb(value, PCI_IOBASE + addr);
	__io_paw();
}
#endif

#if !defined(outw) && !defined(_outw)
#define _outw _outw
static inline void _outw(u16 value, unsigned long addr)
{
	__io_pbw();
	__raw_writew((u16 __force)cpu_to_le16(value), PCI_IOBASE + addr);
	__io_paw();
}
#endif

#if !defined(outl) && !defined(_outl)
#define _outl _outl
static inline void _outl(u32 value, unsigned long addr)
{
	__io_pbw();
	__raw_writel((u32 __force)cpu_to_le32(value), PCI_IOBASE + addr);
	__io_paw();
}
#endif

#include <linux/logic_pio.h>

#ifndef inb
#define inb _inb
#endif

#ifndef inw
#define inw _inw
#endif

#ifndef inl
#define inl _inl
#endif

#ifndef outb
#define outb _outb
#endif

#ifndef outw
#define outw _outw
#endif

#ifndef outl
#define outl _outl
#endif

#ifndef inb_p
#define inb_p inb_p
static inline u8 inb_p(unsigned long addr)
{
	return inb(addr);
}
#endif

#ifndef inw_p
#define inw_p inw_p
static inline u16 inw_p(unsigned long addr)
{
	return inw(addr);
}
#endif

#ifndef inl_p
#define inl_p inl_p
static inline u32 inl_p(unsigned long addr)
{
	return inl(addr);
}
#endif

#ifndef outb_p
#define outb_p outb_p
static inline void outb_p(u8 value, unsigned long addr)
{
	outb(value, addr);
}
#endif

#ifndef outw_p
#define outw_p outw_p
static inline void outw_p(u16 value, unsigned long addr)
{
	outw(value, addr);
}
#endif

#ifndef outl_p
#define outl_p outl_p
static inline void outl_p(u32 value, unsigned long addr)
{
	outl(value, addr);
}
#endif

/*
 * {in,out}s{b,w,l}{,_p}() are variants of the above that repeatedly access a
 * single I/O port multiple times.
 */

#ifndef insb
#define insb insb
static inline void insb(unsigned long addr, void *buffer, unsigned int count)
{
	readsb(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef insw
#define insw insw
static inline void insw(unsigned long addr, void *buffer, unsigned int count)
{
	readsw(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef insl
#define insl insl
static inline void insl(unsigned long addr, void *buffer, unsigned int count)
{
	readsl(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef outsb
#define outsb outsb
static inline void outsb(unsigned long addr, const void *buffer,
			 unsigned int count)
{
	writesb(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef outsw
#define outsw outsw
static inline void outsw(unsigned long addr, const void *buffer,
			 unsigned int count)
{
	writesw(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef outsl
#define outsl outsl
static inline void outsl(unsigned long addr, const void *buffer,
			 unsigned int count)
{
	writesl(PCI_IOBASE + addr, buffer, count);
}
#endif

#ifndef insb_p
#define insb_p insb_p
static inline void insb_p(unsigned long addr, void *buffer, unsigned int count)
{
	insb(addr, buffer, count);
}
#endif

#ifndef insw_p
#define insw_p insw_p
static inline void insw_p(unsigned long addr, void *buffer, unsigned int count)
{
	insw(addr, buffer, count);
}
#endif

#ifndef insl_p
#define insl_p insl_p
static inline void insl_p(unsigned long addr, void *buffer, unsigned int count)
{
	insl(addr, buffer, count);
}
#endif

#ifndef outsb_p
#define outsb_p outsb_p
static inline void outsb_p(unsigned long addr, const void *buffer,
			   unsigned int count)
{
	outsb(addr, buffer, count);
}
#endif

#ifndef outsw_p
#define outsw_p outsw_p
static inline void outsw_p(unsigned long addr, const void *buffer,
			   unsigned int count)
{
	outsw(addr, buffer, count);
}
#endif

#ifndef outsl_p
#define outsl_p outsl_p
static inline void outsl_p(unsigned long addr, const void *buffer,
			   unsigned int count)
{
	outsl(addr, buffer, count);
}
#endif

#ifndef CONFIG_GENERIC_IOMAP
#ifndef ioread8
#define ioread8 ioread8
static inline u8 ioread8(const volatile void __iomem *addr)
{
	return readb(addr);
}
#endif

#ifndef ioread16
#define ioread16 ioread16
static inline u16 ioread16(const volatile void __iomem *addr)
{
	return readw(addr);
}
#endif

#ifndef ioread32
#define ioread32 ioread32
static inline u32 ioread32(const volatile void __iomem *addr)
{
	return readl(addr);
}
#endif

#ifdef CONFIG_64BIT
#ifndef ioread64
#define ioread64 ioread64
static inline u64 ioread64(const volatile void __iomem *addr)
{
	return readq(addr);
}
#endif
#endif /* CONFIG_64BIT */

#ifndef iowrite8
#define iowrite8 iowrite8
static inline void iowrite8(u8 value, volatile void __iomem *addr)
{
	writeb(value, addr);
}
#endif

#ifndef iowrite16
#define iowrite16 iowrite16
static inline void iowrite16(u16 value, volatile void __iomem *addr)
{
	writew(value, addr);
}
#endif

#ifndef iowrite32
#define iowrite32 iowrite32
static inline void iowrite32(u32 value, volatile void __iomem *addr)
{
	writel(value, addr);
}
#endif

#ifdef CONFIG_64BIT
#ifndef iowrite64
#define iowrite64 iowrite64
static inline void iowrite64(u64 value, volatile void __iomem *addr)
{
	writeq(value, addr);
}
#endif
#endif /* CONFIG_64BIT */

#ifndef ioread16be
#define ioread16be ioread16be
static inline u16 ioread16be(const volatile void __iomem *addr)
{
	return swab16(readw(addr));
}
#endif

#ifndef ioread32be
#define ioread32be ioread32be
static inline u32 ioread32be(const volatile void __iomem *addr)
{
	return swab32(readl(addr));
}
#endif

#ifdef CONFIG_64BIT
#ifndef ioread64be
#define ioread64be ioread64be
static inline u64 ioread64be(const volatile void __iomem *addr)
{
	return swab64(readq(addr));
}
#endif
#endif /* CONFIG_64BIT */

#ifndef iowrite16be
#define iowrite16be iowrite16be
static inline void iowrite16be(u16 value, void volatile __iomem *addr)
{
	writew(swab16(value), addr);
}
#endif

#ifndef iowrite32be
#define iowrite32be iowrite32be
static inline void iowrite32be(u32 value, volatile void __iomem *addr)
{
	writel(swab32(value), addr);
}
#endif

#ifdef CONFIG_64BIT
#ifndef iowrite64be
#define iowrite64be iowrite64be
static inline void iowrite64be(u64 value, volatile void __iomem *addr)
{
	writeq(swab64(value), addr);
}
#endif
#endif /* CONFIG_64BIT */

#ifndef ioread8_rep
#define ioread8_rep ioread8_rep
static inline void ioread8_rep(const volatile void __iomem *addr, void *buffer,
			       unsigned int count)
{
	readsb(addr, buffer, count);
}
#endif

#ifndef ioread16_rep
#define ioread16_rep ioread16_rep
static inline void ioread16_rep(const volatile void __iomem *addr,
				void *buffer, unsigned int count)
{
	readsw(addr, buffer, count);
}
#endif

#ifndef ioread32_rep
#define ioread32_rep ioread32_rep
static inline void ioread32_rep(const volatile void __iomem *addr,
				void *buffer, unsigned int count)
{
	readsl(addr, buffer, count);
}
#endif

#ifdef CONFIG_64BIT
#ifndef ioread64_rep
#define ioread64_rep ioread64_rep
static inline void ioread64_rep(const volatile void __iomem *addr,
				void *buffer, unsigned int count)
{
	readsq(addr, buffer, count);
}
#endif
#endif /* CONFIG_64BIT */

#ifndef iowrite8_rep
#define iowrite8_rep iowrite8_rep
static inline void iowrite8_rep(volatile void __iomem *addr,
				const void *buffer,
				unsigned int count)
{
	writesb(addr, buffer, count);
}
#endif

#ifndef iowrite16_rep
#define iowrite16_rep iowrite16_rep
static inline void iowrite16_rep(volatile void __iomem *addr,
				 const void *buffer,
				 unsigned int count)
{
	writesw(addr, buffer, count);
}
#endif

#ifndef iowrite32_rep
#define iowrite32_rep iowrite32_rep
static inline void iowrite32_rep(volatile void __iomem *addr,
				 const void *buffer,
				 unsigned int count)
{
	writesl(addr, buffer, count);
}
#endif

#ifdef CONFIG_64BIT
#ifndef iowrite64_rep
#define iowrite64_rep iowrite64_rep
static inline void iowrite64_rep(volatile void __iomem *addr,
				 const void *buffer,
				 unsigned int count)
{
	writesq(addr, buffer, count);
}
#endif
#endif /* CONFIG_64BIT */
#endif /* CONFIG_GENERIC_IOMAP */

#ifdef __KERNEL__

#include <linux/vmalloc.h>
#define __io_virt(x) ((void __force *)(x))

/*
 * Change virtual addresses to physical addresses and vv.
 * These are pretty trivial
 */
#ifndef virt_to_phys
#define virt_to_phys virt_to_phys
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa((unsigned long)address);
}
#endif

#ifndef phys_to_virt
#define phys_to_virt phys_to_virt
static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}
#endif

/**
 * DOC: ioremap() and ioremap_*() variants
 *
 * Architectures with an MMU are expected to provide ioremap() and iounmap()
 * themselves or rely on GENERIC_IOREMAP.  For NOMMU architectures we provide
 * a default nop-op implementation that expect that the physical address used
 * for MMIO are already marked as uncached, and can be used as kernel virtual
 * addresses.
 *
 * ioremap_wc() and ioremap_wt() can provide more relaxed caching attributes
 * for specific drivers if the architecture choses to implement them.  If they
 * are not implemented we fall back to plain ioremap. Conversely, ioremap_np()
 * can provide stricter non-posted write semantics if the architecture
 * implements them.
 */
#ifndef CONFIG_MMU
#ifndef ioremap
#define ioremap ioremap
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	return (void __iomem *)(unsigned long)offset;
}
#endif

#ifndef iounmap
#define iounmap iounmap
static inline void iounmap(volatile void __iomem *addr)
{
}
#endif
#elif defined(CONFIG_GENERIC_IOREMAP)
#include <linux/pgtable.h>

/*
 * Arch code can implement the following two hooks when using GENERIC_IOREMAP
 * ioremap_allowed() return a bool,
 *   - true means continue to remap
 *   - false means skip remap and return directly
 * iounmap_allowed() return a bool,
 *   - true means continue to vunmap
 *   - false means skip vunmap and return directly
 */
#ifndef ioremap_allowed
#define ioremap_allowed ioremap_allowed
static inline bool ioremap_allowed(phys_addr_t phys_addr, size_t size,
				   unsigned long prot)
{
	return true;
}
#endif

#ifndef iounmap_allowed
#define iounmap_allowed iounmap_allowed
static inline bool iounmap_allowed(void *addr)
{
	return true;
}
#endif

void __iomem *ioremap_prot(phys_addr_t phys_addr, size_t size,
			   unsigned long prot);
void iounmap(volatile void __iomem *addr);

static inline void __iomem *ioremap(phys_addr_t addr, size_t size)
{
	/* _PAGE_IOREMAP needs to be supplied by the architecture */
	return ioremap_prot(addr, size, _PAGE_IOREMAP);
}
#endif /* !CONFIG_MMU || CONFIG_GENERIC_IOREMAP */

#ifndef ioremap_wc
#define ioremap_wc ioremap
#endif

#ifndef ioremap_wt
#define ioremap_wt ioremap
#endif

/*
 * ioremap_uc is special in that we do require an explicit architecture
 * implementation.  In general you do not want to use this function in a
 * driver and use plain ioremap, which is uncached by default.  Similarly
 * architectures should not implement it unless they have a very good
 * reason.
 */
#ifndef ioremap_uc
#define ioremap_uc ioremap_uc
static inline void __iomem *ioremap_uc(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

/*
 * ioremap_np needs an explicit architecture implementation, as it
 * requests stronger semantics than regular ioremap(). Portable drivers
 * should instead use one of the higher-level abstractions, like
 * devm_ioremap_resource(), to choose the correct variant for any given
 * device and bus. Portable drivers with a good reason to want non-posted
 * write semantics should always provide an ioremap() fallback in case
 * ioremap_np() is not available.
 */
#ifndef ioremap_np
#define ioremap_np ioremap_np
static inline void __iomem *ioremap_np(phys_addr_t offset, size_t size)
{
	return NULL;
}
#endif

#ifdef CONFIG_HAS_IOPORT_MAP
#ifndef CONFIG_GENERIC_IOMAP
#ifndef ioport_map
#define ioport_map ioport_map
static inline void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	port &= IO_SPACE_LIMIT;
	return (port > MMIO_UPPER_LIMIT) ? NULL : PCI_IOBASE + port;
}
#define ARCH_HAS_GENERIC_IOPORT_MAP
#endif

#ifndef ioport_unmap
#define ioport_unmap ioport_unmap
static inline void ioport_unmap(void __iomem *p)
{
}
#endif
#else /* CONFIG_GENERIC_IOMAP */
extern void __iomem *ioport_map(unsigned long port, unsigned int nr);
extern void ioport_unmap(void __iomem *p);
#endif /* CONFIG_GENERIC_IOMAP */
#endif /* CONFIG_HAS_IOPORT_MAP */

#ifndef CONFIG_GENERIC_IOMAP
#ifndef pci_iounmap
#define ARCH_WANTS_GENERIC_PCI_IOUNMAP
#endif
#endif

#ifndef xlate_dev_mem_ptr
#define xlate_dev_mem_ptr xlate_dev_mem_ptr
static inline void *xlate_dev_mem_ptr(phys_addr_t addr)
{
	return __va(addr);
}
#endif

#ifndef unxlate_dev_mem_ptr
#define unxlate_dev_mem_ptr unxlate_dev_mem_ptr
static inline void unxlate_dev_mem_ptr(phys_addr_t phys, void *addr)
{
}
#endif

#ifndef memset_io
#define memset_io memset_io
/**
 * memset_io	Set a range of I/O memory to a constant value
 * @addr:	The beginning of the I/O-memory range to set
 * @val:	The value to set the memory to
 * @count:	The number of bytes to set
 *
 * Set a range of I/O memory to a given value.
 */
static inline void memset_io(volatile void __iomem *addr, int value,
			     size_t size)
{
	memset(__io_virt(addr), value, size);
}
#endif

#ifndef memcpy_fromio
#define memcpy_fromio memcpy_fromio
/**
 * memcpy_fromio	Copy a block of data from I/O memory
 * @dst:		The (RAM) destination for the copy
 * @src:		The (I/O memory) source for the data
 * @count:		The number of bytes to copy
 *
 * Copy a block of data from I/O memory.
 */
static inline void memcpy_fromio(void *buffer,
				 const volatile void __iomem *addr,
				 size_t size)
{
	memcpy(buffer, __io_virt(addr), size);
}
#endif

#ifndef memcpy_toio
#define memcpy_toio memcpy_toio
/**
 * memcpy_toio		Copy a block of data into I/O memory
 * @dst:		The (I/O memory) destination for the copy
 * @src:		The (RAM) source for the data
 * @count:		The number of bytes to copy
 *
 * Copy a block of data to I/O memory.
 */
static inline void memcpy_toio(volatile void __iomem *addr, const void *buffer,
			       size_t size)
{
	memcpy(__io_virt(addr), buffer, size);
}
#endif

extern int devmem_is_allowed(unsigned long pfn);

#endif /* __KERNEL__ */

#endif /* __ASM_GENERIC_IO_H */
