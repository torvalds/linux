/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_ASM_GENERIC_IO_H
#define _TOOLS_ASM_GENERIC_IO_H

#include <asm/barrier.h>
#include <asm/byteorder.h>

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/types.h>

#ifndef mmiowb_set_pending
#define mmiowb_set_pending() do { } while (0)
#endif

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

#ifndef _THIS_IP_
#define _THIS_IP_ 0
#endif

static inline void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
				  unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_post_write_mmio(u64 val, u8 width, volatile void __iomem *addr,
				       unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_read_mmio(u8 width, const volatile void __iomem *addr,
				 unsigned long caller_addr, unsigned long caller_addr0) {}
static inline void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr,
				      unsigned long caller_addr, unsigned long caller_addr0) {}

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

#ifndef __raw_readq
#define __raw_readq __raw_readq
static inline u64 __raw_readq(const volatile void __iomem *addr)
{
	return *(const volatile u64 __force *)addr;
}
#endif

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

#ifndef __raw_writeq
#define __raw_writeq __raw_writeq
static inline void __raw_writeq(u64 value, volatile void __iomem *addr)
{
	*(volatile u64 __force *)addr = value;
}
#endif

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

#endif /* _TOOLS_ASM_GENERIC_IO_H */
