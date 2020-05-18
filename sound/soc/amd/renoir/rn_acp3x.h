/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright 2020 Advanced Micro Devices, Inc.
 */

#include "rn_chip_offset_byte.h"

#define ACP_PHY_BASE_ADDRESS 0x1240000
#define ACP_DEVICE_ID 0x15E2

static inline u32 rn_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP_PHY_BASE_ADDRESS);
}

static inline void rn_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP_PHY_BASE_ADDRESS);
}
