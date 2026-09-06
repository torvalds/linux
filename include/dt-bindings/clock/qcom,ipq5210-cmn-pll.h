/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_IPQ5210_CMN_PLL_H
#define _DT_BINDINGS_CLK_QCOM_IPQ5210_CMN_PLL_H

/* Parent clock */
#define IPQ5210_CMN_PLL_CLK		0

/* Fixed-rate clocks */
#define IPQ5210_XO_24MHZ_CLK		1
#define IPQ5210_SLEEP_32KHZ_CLK		2

/* Configurable divider clocks */
#define IPQ5210_NSS_CLK			3
#define IPQ5210_PPE_CLK			4
#define IPQ5210_PON_CLK			5
#define IPQ5210_EPHY_RAW_CLK		6

/* Gate clocks */
#define IPQ5210_PCS_31P25MHZ_CLK	7
#define IPQ5210_ETH0_50MHZ_CLK		8
#define IPQ5210_ETH1_50MHZ_CLK		9
#define IPQ5210_ETH2_50MHZ_CLK		10
#define IPQ5210_EPHY_50MHZ_CLK		11
#define IPQ5210_ETH_25MHZ_CLK		12

#endif
