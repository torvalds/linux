/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright 2020 Advanced Micro Devices, Inc.
 */

#include "rn_chip_offset_byte.h"

#define ACP_PHY_BASE_ADDRESS 0x1240000
#define	ACP_REG_START	0x1240000
#define	ACP_REG_END	0x1250200

#define ACP_DEVICE_ID 0x15E2
#define ACP_POWER_ON 0x00
#define ACP_POWER_ON_IN_PROGRESS 0x01
#define ACP_POWER_OFF 0x02
#define ACP_POWER_OFF_IN_PROGRESS 0x03
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001

#define ACP_PGFSM_CNTL_POWER_ON_MASK    0x01
#define ACP_PGFSM_CNTL_POWER_OFF_MASK   0x00
#define ACP_PGFSM_STATUS_MASK           0x03
#define ACP_POWERED_ON                  0x00
#define ACP_POWER_ON_IN_PROGRESS        0x01
#define ACP_POWERED_OFF                 0x02
#define ACP_POWER_OFF_IN_PROGRESS       0x03

#define ACP_ERROR_MASK 0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF

static inline u32 rn_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP_PHY_BASE_ADDRESS);
}

static inline void rn_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP_PHY_BASE_ADDRESS);
}
