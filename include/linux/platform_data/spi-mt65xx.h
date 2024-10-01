/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  MTK SPI bus driver definitions
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Leilk Liu <leilk.liu@mediatek.com>
 */

#ifndef ____LINUX_PLATFORM_DATA_SPI_MTK_H
#define ____LINUX_PLATFORM_DATA_SPI_MTK_H

/* Board specific platform_data */
struct mtk_chip_config {
	u32 sample_sel;
	u32 tick_delay;
};
#endif
