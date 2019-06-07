/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IO_64_NONATOMIC_LO_HI_H_
#define _LINUX_IO_64_NONATOMIC_LO_HI_H_

#include <linux/io.h>
#include <asm-generic/int-ll64.h>

static inline __u64 lo_hi_readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

static inline void lo_hi_writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}

static inline __u64 lo_hi_readq_relaxed(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl_relaxed(p);
	high = readl_relaxed(p + 1);

	return low + ((u64)high << 32);
}

static inline void lo_hi_writeq_relaxed(__u64 val, volatile void __iomem *addr)
{
	writel_relaxed(val, addr);
	writel_relaxed(val >> 32, addr + 4);
}

#ifndef readq
#define readq lo_hi_readq
#endif

#ifndef writeq
#define writeq lo_hi_writeq
#endif

#ifndef readq_relaxed
#define readq_relaxed lo_hi_readq_relaxed
#endif

#ifndef writeq_relaxed
#define writeq_relaxed lo_hi_writeq_relaxed
#endif

#ifndef ioread64_lo_hi
#define ioread64_lo_hi ioread64_lo_hi
static inline u64 ioread64_lo_hi(void __iomem *addr)
{
	u32 low, high;

	low = ioread32(addr);
	high = ioread32(addr + sizeof(u32));

	return low + ((u64)high << 32);
}
#endif

#ifndef iowrite64_lo_hi
#define iowrite64_lo_hi iowrite64_lo_hi
static inline void iowrite64_lo_hi(u64 val, void __iomem *addr)
{
	iowrite32(val, addr);
	iowrite32(val >> 32, addr + sizeof(u32));
}
#endif

#ifndef ioread64be_lo_hi
#define ioread64be_lo_hi ioread64be_lo_hi
static inline u64 ioread64be_lo_hi(void __iomem *addr)
{
	u32 low, high;

	low = ioread32be(addr + sizeof(u32));
	high = ioread32be(addr);

	return low + ((u64)high << 32);
}
#endif

#ifndef iowrite64be_lo_hi
#define iowrite64be_lo_hi iowrite64be_lo_hi
static inline void iowrite64be_lo_hi(u64 val, void __iomem *addr)
{
	iowrite32be(val, addr + sizeof(u32));
	iowrite32be(val >> 32, addr);
}
#endif

#ifndef ioread64
#define ioread64_is_nonatomic
#define ioread64 ioread64_lo_hi
#endif

#ifndef iowrite64
#define iowrite64_is_nonatomic
#define iowrite64 iowrite64_lo_hi
#endif

#ifndef ioread64be
#define ioread64be_is_nonatomic
#define ioread64be ioread64be_lo_hi
#endif

#ifndef iowrite64be
#define iowrite64be_is_nonatomic
#define iowrite64be iowrite64be_lo_hi
#endif

#endif	/* _LINUX_IO_64_NONATOMIC_LO_HI_H_ */
