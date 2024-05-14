/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ACP Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "rpl_acp6x_chip_offset_byte.h"

#define ACP_DEVICE_ID 0x15E2
#define ACP6x_PHY_BASE_ADDRESS 0x1240000

#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK   0x00010001
#define ACP_PGFSM_CNTL_POWER_ON_MASK    1
#define ACP_PGFSM_CNTL_POWER_OFF_MASK   0
#define ACP_PGFSM_STATUS_MASK           3
#define ACP_POWERED_ON                  0
#define ACP_POWER_ON_IN_PROGRESS        1
#define ACP_POWERED_OFF                 2
#define ACP_POWER_OFF_IN_PROGRESS       3

#define DELAY_US        5
#define ACP_COUNTER     20000

/* time in ms for runtime suspend delay */
#define ACP_SUSPEND_DELAY_MS    2000

static inline u32 rpl_acp_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP6x_PHY_BASE_ADDRESS);
}

static inline void rpl_acp_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP6x_PHY_BASE_ADDRESS);
}
