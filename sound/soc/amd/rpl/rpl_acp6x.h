/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ACP Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "rpl_acp6x_chip_offset_byte.h"

#define ACP_DEVICE_ID 0x15E2
#define ACP6x_PHY_BASE_ADDRESS 0x1240000

static inline u32 rpl_acp_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP6x_PHY_BASE_ADDRESS);
}

static inline void rpl_acp_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP6x_PHY_BASE_ADDRESS);
}
