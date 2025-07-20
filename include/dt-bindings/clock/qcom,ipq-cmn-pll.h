/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_IPQ_CMN_PLL_H
#define _DT_BINDINGS_CLK_QCOM_IPQ_CMN_PLL_H

/* CMN PLL core clock. */
#define CMN_PLL_CLK			0

/* The output clocks from CMN PLL of IPQ9574. */
#define XO_24MHZ_CLK			1
#define SLEEP_32KHZ_CLK			2
#define PCS_31P25MHZ_CLK		3
#define NSS_1200MHZ_CLK			4
#define PPE_353MHZ_CLK			5
#define ETH0_50MHZ_CLK			6
#define ETH1_50MHZ_CLK			7
#define ETH2_50MHZ_CLK			8
#define ETH_25MHZ_CLK			9
#endif
