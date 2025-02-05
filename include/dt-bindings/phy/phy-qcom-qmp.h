/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Qualcomm QMP PHY constants
 *
 * Copyright (C) 2022 Linaro Limited
 */

#ifndef _DT_BINDINGS_PHY_QMP
#define _DT_BINDINGS_PHY_QMP

/* QMP USB4-USB3-DP clocks */
#define QMP_USB43DP_USB3_PIPE_CLK	0
#define QMP_USB43DP_DP_LINK_CLK		1
#define QMP_USB43DP_DP_VCO_DIV_CLK	2

/* QMP USB4-USB3-DP PHYs */
#define QMP_USB43DP_USB3_PHY		0
#define QMP_USB43DP_DP_PHY		1

/* QMP PCIE PHYs */
#define QMP_PCIE_PIPE_CLK		0
#define QMP_PCIE_PHY_AUX_CLK		1

#endif /* _DT_BINDINGS_PHY_QMP */
