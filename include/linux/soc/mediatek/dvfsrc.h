/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef __MEDIATEK_DVFSRC_H
#define __MEDIATEK_DVFSRC_H

enum mtk_dvfsrc_cmd {
	MTK_DVFSRC_CMD_BW,
	MTK_DVFSRC_CMD_HRT_BW,
	MTK_DVFSRC_CMD_PEAK_BW,
	MTK_DVFSRC_CMD_OPP,
	MTK_DVFSRC_CMD_VCORE_LEVEL,
	MTK_DVFSRC_CMD_VSCP_LEVEL,
	MTK_DVFSRC_CMD_MAX,
};

#if IS_ENABLED(CONFIG_MTK_DVFSRC)

int mtk_dvfsrc_send_request(const struct device *dev, u32 cmd, u64 data);
int mtk_dvfsrc_query_info(const struct device *dev, u32 cmd, int *data);

#else

static inline int mtk_dvfsrc_send_request(const struct device *dev, u32 cmd, u64 data)
{ return -ENODEV; }

static inline int mtk_dvfsrc_query_info(const struct device *dev, u32 cmd, int *data)
{ return -ENODEV; }

#endif /* CONFIG_MTK_DVFSRC */

#endif
