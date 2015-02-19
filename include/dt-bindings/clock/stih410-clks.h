/*
 * This header provides constants clk index STMicroelectronics
 * STiH410 SoC.
 */
#ifndef _DT_BINDINGS_CLK_STIH410
#define _DT_BINDINGS_CLK_STIH410

#include "stih407-clks.h"

/* STiH410 introduces new clock outputs compared to STiH407 */

/* CLOCKGEN C0 */
#define CLK_TX_ICN_HADES	32
#define CLK_RX_ICN_HADES	33
#define CLK_ICN_REG_16		34
#define CLK_PP_HADES		35
#define CLK_CLUST_HADES		36
#define CLK_HWPE_HADES		37
#define CLK_FC_HADES		38

/* CLOCKGEN D0 */
#define CLK_PCMR10_MASTER	4
#define CLK_USB2_PHY		5

#endif
