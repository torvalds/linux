/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_LSR_CC_SERAPH_H
#define _DT_BINDINGS_CLK_QCOM_LSR_CC_SERAPH_H

/* LSR_CC clocks */
#define LSR_CC_PLL0						0
#define LSR_CC_PLL1						1
#define LSR_CC_AHB_CLK						2
#define LSR_CC_AHB_CLK_SRC					3
#define LSR_CC_MVS0_CLK						4
#define LSR_CC_MVS0_CLK_SRC					5
#define LSR_CC_MVS0_FREERUN_CLK					6
#define LSR_CC_MVS0_SHIFT_CLK					7
#define LSR_CC_MVS0C_CLK					8
#define LSR_CC_MVS0C_CLK_SRC					9
#define LSR_CC_MVS0C_FREERUN_CLK				10
#define LSR_CC_MVS0C_SHIFT_CLK					11
#define LSR_CC_SLEEP_CLK					12
#define LSR_CC_SLEEP_CLK_SRC					13
#define LSR_CC_XO_CLK						14
#define LSR_CC_XO_CLK_SRC					15

/* LSR_CC resets */
#define LSR_CC_INTERFACE_BCR					0
#define LSR_CC_LSR_NOC_BCR					1
#define LSR_CC_MVS0_BCR						2
#define LSR_CC_MVS0C_BCR					3
#define LSR_CC_MVS0_FREERUN_CLK_ARES				4
#define LSR_CC_MVS0C_CLK_ARES					5
#define LSR_CC_MVS0C_FREERUN_CLK_ARES				6

#endif
