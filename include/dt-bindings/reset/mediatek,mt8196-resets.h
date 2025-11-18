/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2025 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef _DT_BINDINGS_RESET_CONTROLLER_MT8196
#define _DT_BINDINGS_RESET_CONTROLLER_MT8196

/* PEXTP0 resets */
#define MT8196_PEXTP0_RST0_PCIE0_MAC		0
#define MT8196_PEXTP0_RST0_PCIE0_PHY		1

/* PEXTP1 resets */
#define MT8196_PEXTP1_RST0_PCIE1_MAC		0
#define MT8196_PEXTP1_RST0_PCIE1_PHY		1
#define MT8196_PEXTP1_RST0_PCIE2_MAC		2
#define MT8196_PEXTP1_RST0_PCIE2_PHY		3

/* UFS resets */
#define MT8196_UFSAO_RST0_UFS_MPHY		0
#define MT8196_UFSAO_RST1_UFS_UNIPRO		1
#define MT8196_UFSAO_RST1_UFS_CRYPTO		2
#define MT8196_UFSAO_RST1_UFSHCI		3

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT8196 */
