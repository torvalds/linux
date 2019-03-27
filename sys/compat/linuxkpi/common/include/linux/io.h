/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2015 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_IO_H_
#define	_LINUX_IO_H_

#include <machine/vm.h>
#include <sys/endian.h>
#include <sys/types.h>

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * XXX This is all x86 specific.  It should be bus space access.
 */

/* Access MMIO registers atomically without barriers and byte swapping. */

static inline uint8_t
__raw_readb(const volatile void *addr)
{
	return (*(const volatile uint8_t *)addr);
}
#define	__raw_readb(addr)	__raw_readb(addr)

static inline void
__raw_writeb(uint8_t v, volatile void *addr)
{
	*(volatile uint8_t *)addr = v;
}
#define	__raw_writeb(v, addr)	__raw_writeb(v, addr)

static inline uint16_t
__raw_readw(const volatile void *addr)
{
	return (*(const volatile uint16_t *)addr);
}
#define	__raw_readw(addr)	__raw_readw(addr)

static inline void
__raw_writew(uint16_t v, volatile void *addr)
{
	*(volatile uint16_t *)addr = v;
}
#define	__raw_writew(v, addr)	__raw_writew(v, addr)

static inline uint32_t
__raw_readl(const volatile void *addr)
{
	return (*(const volatile uint32_t *)addr);
}
#define	__raw_readl(addr)	__raw_readl(addr)

static inline void
__raw_writel(uint32_t v, volatile void *addr)
{
	*(volatile uint32_t *)addr = v;
}
#define	__raw_writel(v, addr)	__raw_writel(v, addr)

#ifdef __LP64__
static inline uint64_t
__raw_readq(const volatile void *addr)
{
	return (*(const volatile uint64_t *)addr);
}
#define	__raw_readq(addr)	__raw_readq(addr)

static inline void
__raw_writeq(uint64_t v, volatile void *addr)
{
	*(volatile uint64_t *)addr = v;
}
#define	__raw_writeq(v, addr)	__raw_writeq(v, addr)
#endif

#define	mmiowb()	barrier()

/* Access little-endian MMIO registers atomically with memory barriers. */

#undef readb
static inline uint8_t
readb(const volatile void *addr)
{
	uint8_t v;

	__compiler_membar();
	v = *(const volatile uint8_t *)addr;
	__compiler_membar();
	return (v);
}
#define	readb(addr)		readb(addr)

#undef writeb
static inline void
writeb(uint8_t v, volatile void *addr)
{
	__compiler_membar();
	*(volatile uint8_t *)addr = v;
	__compiler_membar();
}
#define	writeb(v, addr)		writeb(v, addr)

#undef readw
static inline uint16_t
readw(const volatile void *addr)
{
	uint16_t v;

	__compiler_membar();
	v = *(const volatile uint16_t *)addr;
	__compiler_membar();
	return (v);
}
#define	readw(addr)		readw(addr)

#undef writew
static inline void
writew(uint16_t v, volatile void *addr)
{
	__compiler_membar();
	*(volatile uint16_t *)addr = v;
	__compiler_membar();
}
#define	writew(v, addr)		writew(v, addr)

#undef readl
static inline uint32_t
readl(const volatile void *addr)
{
	uint32_t v;

	__compiler_membar();
	v = *(const volatile uint32_t *)addr;
	__compiler_membar();
	return (v);
}
#define	readl(addr)		readl(addr)

#undef writel
static inline void
writel(uint32_t v, volatile void *addr)
{
	__compiler_membar();
	*(volatile uint32_t *)addr = v;
	__compiler_membar();
}
#define	writel(v, addr)		writel(v, addr)

#undef readq
#undef writeq
#ifdef __LP64__
static inline uint64_t
readq(const volatile void *addr)
{
	uint64_t v;

	__compiler_membar();
	v = *(const volatile uint64_t *)addr;
	__compiler_membar();
	return (v);
}
#define	readq(addr)		readq(addr)

static inline void
writeq(uint64_t v, volatile void *addr)
{
	__compiler_membar();
	*(volatile uint64_t *)addr = v;
	__compiler_membar();
}
#define	writeq(v, addr)		writeq(v, addr)
#endif

/* Access little-endian MMIO registers atomically without memory barriers. */

#undef readb_relaxed
static inline uint8_t
readb_relaxed(const volatile void *addr)
{
	return (*(const volatile uint8_t *)addr);
}
#define	readb_relaxed(addr)	readb_relaxed(addr)

#undef writeb_relaxed
static inline void
writeb_relaxed(uint8_t v, volatile void *addr)
{
	*(volatile uint8_t *)addr = v;
}
#define	writeb_relaxed(v, addr)	writeb_relaxed(v, addr)

#undef readw_relaxed
static inline uint16_t
readw_relaxed(const volatile void *addr)
{
	return (*(const volatile uint16_t *)addr);
}
#define	readw_relaxed(addr)	readw_relaxed(addr)

#undef writew_relaxed
static inline void
writew_relaxed(uint16_t v, volatile void *addr)
{
	*(volatile uint16_t *)addr = v;
}
#define	writew_relaxed(v, addr)	writew_relaxed(v, addr)

#undef readl_relaxed
static inline uint32_t
readl_relaxed(const volatile void *addr)
{
	return (*(const volatile uint32_t *)addr);
}
#define	readl_relaxed(addr)	readl_relaxed(addr)

#undef writel_relaxed
static inline void
writel_relaxed(uint32_t v, volatile void *addr)
{
	*(volatile uint32_t *)addr = v;
}
#define	writel_relaxed(v, addr)	writel_relaxed(v, addr)

#undef readq_relaxed
#undef writeq_relaxed
#ifdef __LP64__
static inline uint64_t
readq_relaxed(const volatile void *addr)
{
	return (*(const volatile uint64_t *)addr);
}
#define	readq_relaxed(addr)	readq_relaxed(addr)

static inline void
writeq_relaxed(uint64_t v, volatile void *addr)
{
	*(volatile uint64_t *)addr = v;
}
#define	writeq_relaxed(v, addr)	writeq_relaxed(v, addr)
#endif

/* XXX On Linux ioread and iowrite handle both MMIO and port IO. */

#undef ioread8
static inline uint8_t
ioread8(const volatile void *addr)
{
	return (readb(addr));
}
#define	ioread8(addr)		ioread8(addr)

#undef ioread16
static inline uint16_t
ioread16(const volatile void *addr)
{
	return (readw(addr));
}
#define	ioread16(addr)		ioread16(addr)

#undef ioread16be
static inline uint16_t
ioread16be(const volatile void *addr)
{
	return (bswap16(readw(addr)));
}
#define	ioread16be(addr)	ioread16be(addr)

#undef ioread32
static inline uint32_t
ioread32(const volatile void *addr)
{
	return (readl(addr));
}
#define	ioread32(addr)		ioread32(addr)

#undef ioread32be
static inline uint32_t
ioread32be(const volatile void *addr)
{
	return (bswap32(readl(addr)));
}
#define	ioread32be(addr)	ioread32be(addr)

#undef iowrite8
static inline void
iowrite8(uint8_t v, volatile void *addr)
{
	writeb(v, addr);
}
#define	iowrite8(v, addr)	iowrite8(v, addr)

#undef iowrite16
static inline void
iowrite16(uint16_t v, volatile void *addr)
{
	writew(v, addr);
}
#define	iowrite16	iowrite16

#undef iowrite32
static inline void
iowrite32(uint32_t v, volatile void *addr)
{
	writel(v, addr);
}
#define	iowrite32(v, addr)	iowrite32(v, addr)

#undef iowrite32be
static inline void
iowrite32be(uint32_t v, volatile void *addr)
{
	writel(bswap32(v), addr);
}
#define	iowrite32be(v, addr)	iowrite32be(v, addr)

#if defined(__i386__) || defined(__amd64__)
static inline void
_outb(u_char data, u_int port)
{
	__asm __volatile("outb %0, %w1" : : "a" (data), "Nd" (port));
}
#endif

#if defined(__i386__) || defined(__amd64__) || defined(__powerpc__)
void *_ioremap_attr(vm_paddr_t phys_addr, unsigned long size, int attr);
#else
#define	_ioremap_attr(...) NULL
#endif

#define	ioremap_nocache(addr, size)					\
    _ioremap_attr((addr), (size), VM_MEMATTR_UNCACHEABLE)
#define	ioremap_wc(addr, size)						\
    _ioremap_attr((addr), (size), VM_MEMATTR_WRITE_COMBINING)
#define	ioremap_wb(addr, size)						\
    _ioremap_attr((addr), (size), VM_MEMATTR_WRITE_BACK)
#define	ioremap_wt(addr, size)						\
    _ioremap_attr((addr), (size), VM_MEMATTR_WRITE_THROUGH)
#define	ioremap(addr, size)						\
    _ioremap_attr((addr), (size), VM_MEMATTR_UNCACHEABLE)
void iounmap(void *addr);

#define	memset_io(a, b, c)	memset((a), (b), (c))
#define	memcpy_fromio(a, b, c)	memcpy((a), (b), (c))
#define	memcpy_toio(a, b, c)	memcpy((a), (b), (c))

static inline void
__iowrite32_copy(void *to, void *from, size_t count)
{
	uint32_t *src;
	uint32_t *dst;
	int i;

	for (i = 0, src = from, dst = to; i < count; i++, src++, dst++)
		__raw_writel(*src, dst);
}

static inline void
__iowrite64_copy(void *to, void *from, size_t count)
{
#ifdef __LP64__
	uint64_t *src;
	uint64_t *dst;
	int i;

	for (i = 0, src = from, dst = to; i < count; i++, src++, dst++)
		__raw_writeq(*src, dst);
#else
	__iowrite32_copy(to, from, count * 2);
#endif
}

enum {
	MEMREMAP_WB = 1 << 0,
	MEMREMAP_WT = 1 << 1,
	MEMREMAP_WC = 1 << 2,
};

static inline void *
memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	void *addr = NULL;

	if ((flags & MEMREMAP_WB) &&
	    (addr = ioremap_wb(offset, size)) != NULL)
		goto done;
	if ((flags & MEMREMAP_WT) &&
	    (addr = ioremap_wt(offset, size)) != NULL)
		goto done;
	if ((flags & MEMREMAP_WC) &&
	    (addr = ioremap_wc(offset, size)) != NULL)
		goto done;
done:
	return (addr);
}

static inline void
memunmap(void *addr)
{
	/* XXX May need to check if this is RAM */
	iounmap(addr);
}

#endif	/* _LINUX_IO_H_ */
