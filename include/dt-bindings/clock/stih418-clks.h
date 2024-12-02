/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants clk index STMicroelectronics
 * STiH418 SoC.
 */
#ifndef _DT_BINDINGS_CLK_STIH418
#define _DT_BINDINGS_CLK_STIH418

#include "stih410-clks.h"

/* STiH418 introduces new clock outputs compared to STiH410 */

/* CLOCKGEN C0 */
#define CLK_PROC_BDISP_0        14
#define CLK_PROC_BDISP_1        15
#define CLK_TX_ICN_1            23
#define CLK_ETH_PHYREF          27
#define CLK_PP_HEVC             35
#define CLK_CLUST_HEVC          36
#define CLK_HWPE_HEVC           37
#define CLK_FC_HEVC             38
#define CLK_PROC_MIXER		39
#define CLK_PROC_SC		40
#define CLK_AVSP_HEVC		41

/* CLOCKGEN D2 */
#undef CLK_PIX_PIP
#undef CLK_PIX_GDP1
#undef CLK_PIX_GDP2
#undef CLK_PIX_GDP3
#undef CLK_PIX_GDP4

#define CLK_TMDS_HDMI_DIV2	5
#define CLK_VP9			47
#endif
