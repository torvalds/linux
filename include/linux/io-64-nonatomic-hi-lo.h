/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IO_64_NONATOMIC_HI_LO_H_
#define _LINUX_IO_64_NONATOMIC_HI_LO_H_

#include <linux/io.h>
#include <asm-generic/int-ll64.h>

static inline __u64 hi_lo_readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	high = readl(p + 1);
	low = readl(p);

	return low + ((u64)high << 32);
}

static inline void hi_lo_writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val >> 32, addr + 4);
	writel(val, addr);
}

static inline __u64 hi_lo_readq_relaxed(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	high = readl_relaxed(p + 1);
	low = readl_relaxed(p);

	return low + ((u64)high << 32);
}

static inline void hi_lo_writeq_relaxed(__u64 val, volatile void __iomem *addr)
{
	writel_relaxed(val >> 32, addr + 4);
	writel_relaxed(val, addr);
}

#ifndef readq
#define readq hi_lo_readq
#endif

#ifndef writeq
#define writeq hi_lo_writeq
#endif

#ifndef readq_relaxed
#define readq_relaxed hi_lo_readq_relaxed
#endif

#ifndef writeq_relaxed
#define writeq_relaxed hi_lo_writeq_relaxed
#endif

#ifndef ioread64_hi_lo
#define ioread64_hi_lo ioread64_hi_lo
static inline u64 ioread64_hi_lo(const void __iomem *addr)
{
	u32 low, high;

	high = ioread32(addr + sizeof(u32));
	low = ioread32(addr);

	return low + ((u64)high << 32);
}
#endif

#ifndef iowrite64_hi_lo
#define iowrite64_hi_lo iowrite64_hi_lo
static inline void iowrite64_hi_lo(u64 val, void __iomem *addr)
{
	iowrite32(val >> 32, addr + sizeof(u32));
	iowrite32(val, addr);
}
#endif

#ifndef ioread64be_hi_lo
#define ioread64be_hi_lo ioread64be_hi_lo
static inline u64 ioread64be_hi_lo(const void __iomem *addr)
{
	u32 low, high;

	high = ioread32be(addr);
	low = ioread32be(addr + sizeof(u32));

	return low + ((u64)high << 32);
}
#endif

#ifndef iowrite64be_hi_lo
#define iowrite64be_hi_lo iowrite64be_hi_lo
static inline void iowrite64be_hi_lo(u64 val, void __iomem *addr)
{
	iowrite32be(val >> 32, addr);
	iowrite32be(val, addr + sizeof(u32));
}
#endif

#ifndef ioread64
#define ioread64_is_nonatomic
#if defined(CONFIG_GENERIC_IOMAP) && defined(CONFIG_64BIT)
#define ioread64 __ioread64_hi_lo
#else
#define ioread64 ioread64_hi_lo
#endif
#endif

#ifndef iowrite64
#define iowrite64_is_nonatomic
#if defined(CONFIG_GENERIC_IOMAP) && defined(CONFIG_64BIT)
#define iowrite64 __iowrite64_hi_lo
#else
#define iowrite64 iowrite64_hi_lo
#endif
#endif

#ifndef ioread64be
#define ioread64be_is_nonatomic
#if defined(CONFIG_GENERIC_IOMAP) && defined(CONFIG_64BIT)
#define ioread64be __ioread64be_hi_lo
#else
#define ioread64be ioread64be_hi_lo
#endif
#endif

#ifndef iowrite64be
#define iowrite64be_is_nonatomic
#if defined(CONFIG_GENERIC_IOMAP) && defined(CONFIG_64BIT)
#define iowrite64be __iowrite64be_hi_lo
#else
#define iowrite64be iowrite64be_hi_lo
#endif
#endif

#endif	/* _LINUX_IO_64_NONATOMIC_HI_LO_H_ */
