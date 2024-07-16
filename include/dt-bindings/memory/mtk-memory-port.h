/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#ifndef __DT_BINDINGS_MEMORY_MTK_MEMORY_PORT_H_
#define __DT_BINDINGS_MEMORY_MTK_MEMORY_PORT_H_

#define MTK_LARB_NR_MAX			32

#define MTK_M4U_ID(larb, port)		(((larb) << 5) | (port))
#define MTK_M4U_TO_LARB(id)		(((id) >> 5) & 0x1f)
#define MTK_M4U_TO_PORT(id)		((id) & 0x1f)

#define MTK_IFAIOMMU_PERI_ID(port)	MTK_M4U_ID(0, port)

#endif
