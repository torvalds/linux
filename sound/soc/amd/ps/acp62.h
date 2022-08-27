/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PDM Driver
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
 */

#include <sound/acp62_chip_offset_byte.h>

#define ACP_DEVICE_ID 0x15E2

static inline u32 acp62_readl(void __iomem *base_addr)
{
	return readl(base_addr);
}

static inline void acp62_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr);
}
