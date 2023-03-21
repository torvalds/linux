/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_NPU_CC_SM8150_H
#define _DT_BINDINGS_CLK_QCOM_NPU_CC_SM8150_H

/* NPU_CC clocks */
#define NPU_CC_PLL0					0
#define NPU_CC_PLL1					1
#define NPU_CC_ARMWIC_CORE_CLK				2
#define NPU_CC_BTO_CORE_CLK				3
#define NPU_CC_BWMON_CLK				4
#define NPU_CC_CAL_DP_CDC_CLK				5
#define NPU_CC_CAL_DP_CLK				6
#define NPU_CC_CAL_DP_CLK_SRC				7
#define NPU_CC_COMP_NOC_AXI_CLK				8
#define NPU_CC_CONF_NOC_AHB_CLK				9
#define NPU_CC_NPU_CORE_APB_CLK				10
#define NPU_CC_NPU_CORE_ATB_CLK				11
#define NPU_CC_NPU_CORE_CLK				12
#define NPU_CC_NPU_CORE_CLK_SRC				13
#define NPU_CC_NPU_CORE_CTI_CLK				14
#define NPU_CC_NPU_CPC_CLK				15
#define NPU_CC_NPU_CPC_TIMER_CLK			16
#define NPU_CC_PERF_CNT_CLK				17
#define NPU_CC_QTIMER_CORE_CLK				18
#define NPU_CC_SLEEP_CLK				19
#define NPU_CC_XO_CLK					20

/* NPU_CC power domains */
#define NPU_CORE_GDSC					0

/* NPU_CC resets */
#define NPU_CC_CAL_DP_BCR				0
#define NPU_CC_NPU_CORE_BCR				1

#endif
