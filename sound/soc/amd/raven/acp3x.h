#include "chip_offset_byte.h"

#define ACP3x_PHY_BASE_ADDRESS 0x1240000

static inline u32 rv_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP3x_PHY_BASE_ADDRESS);
}

static inline void rv_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP3x_PHY_BASE_ADDRESS);
}
