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

#endif	/* _LINUX_IO_64_NONATOMIC_LO_HI_H_ */
