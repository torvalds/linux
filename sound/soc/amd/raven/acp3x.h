#include "chip_offset_byte.h"

#define ACP3x_PHY_BASE_ADDRESS 0x1240000
#define	ACP3x_I2S_MODE	0
#define	ACP3x_REG_START	0x1240000
#define	ACP3x_REG_END	0x1250200
#define I2S_MODE	0x04
#define	BT_TX_THRESHOLD 26
#define	BT_RX_THRESHOLD 25
#define ACP3x_POWER_ON 0x00
#define ACP3x_POWER_ON_IN_PROGRESS 0x01
#define ACP3x_POWER_OFF 0x02
#define ACP3x_POWER_OFF_IN_PROGRESS 0x03
#define ACP3x_SOFT_RESET__SoftResetAudDone_MASK	0x00010001

static inline u32 rv_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP3x_PHY_BASE_ADDRESS);
}

static inline void rv_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP3x_PHY_BASE_ADDRESS);
}
