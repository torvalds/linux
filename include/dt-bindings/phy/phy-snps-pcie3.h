/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#ifndef _DT_BINDINGS_PHY_SNPS_PCIE3
#define _DT_BINDINGS_PHY_SNPS_PCIE3

/*
 * pcie30_phy_mode[2:0]
 * bit2: aggregation
 * bit1: bifurcation for port 1
 * bit0: bifurcation for port 0
 */
#define PHY_MODE_PCIE_AGGREGATION 4	/* PCIe3x4 */
#define PHY_MODE_PCIE_NANBNB	0	/* P1:PCIe3x2  +  P0:PCIe3x2 */
#define PHY_MODE_PCIE_NANBBI	1	/* P1:PCIe3x2  +  P0:PCIe3x1*2 */
#define PHY_MODE_PCIE_NABINB	2	/* P1:PCIe3x1*2 + P0:PCIe3x2 */
#define PHY_MODE_PCIE_NABIBI	3	/* P1:PCIe3x1*2 + P0:PCIe3x1*2 */

#endif /* _DT_BINDINGS_PHY_SNPS_PCIE3 */
