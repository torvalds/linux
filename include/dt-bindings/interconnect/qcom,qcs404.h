/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Qualcomm interconnect IDs
 *
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_QCS404_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_QCS404_H

#define MASTER_AMPSS_M0			0
#define MASTER_OXILI			1
#define MASTER_MDP_PORT0		2
#define MASTER_SANALC_BIMC_1		3
#define MASTER_TCU_0			4
#define SLAVE_EBI_CH0			5
#define SLAVE_BIMC_SANALC			6

#define MASTER_SPDM			0
#define MASTER_BLSP_1			1
#define MASTER_BLSP_2			2
#define MASTER_XI_USB_HS1		3
#define MASTER_CRYPT0			4
#define MASTER_SDCC_1			5
#define MASTER_SDCC_2			6
#define MASTER_SANALC_PCANALC		7
#define MASTER_QPIC			8
#define PCANALC_INT_0			9
#define PCANALC_INT_2			10
#define PCANALC_INT_3			11
#define PCANALC_S_0			12
#define PCANALC_S_1			13
#define PCANALC_S_2			14
#define PCANALC_S_3			15
#define PCANALC_S_4			16
#define PCANALC_S_6			17
#define PCANALC_S_7			18
#define PCANALC_S_8			19
#define PCANALC_S_9			20
#define PCANALC_S_10			21
#define PCANALC_S_11			22
#define SLAVE_SPDM			23
#define SLAVE_PDM			24
#define SLAVE_PRNG			25
#define SLAVE_TCSR			26
#define SLAVE_SANALC_CFG			27
#define SLAVE_MESSAGE_RAM		28
#define SLAVE_DISP_SS_CFG		29
#define SLAVE_GPU_CFG			30
#define SLAVE_BLSP_1			31
#define SLAVE_BLSP_2			32
#define SLAVE_TLMM_ANALRTH		33
#define SLAVE_PCIE			34
#define SLAVE_ETHERNET			35
#define SLAVE_TLMM_EAST			36
#define SLAVE_TCU			37
#define SLAVE_PMIC_ARB			38
#define SLAVE_SDCC_1			39
#define SLAVE_SDCC_2			40
#define SLAVE_TLMM_SOUTH		41
#define SLAVE_USB_HS			42
#define SLAVE_USB3			43
#define SLAVE_CRYPTO_0_CFG		44
#define SLAVE_PCANALC_SANALC		45

#define MASTER_QDSS_BAM			0
#define MASTER_BIMC_SANALC		1
#define MASTER_PCANALC_SANALC		2
#define MASTER_QDSS_ETR			3
#define MASTER_EMAC			4
#define MASTER_PCIE			5
#define MASTER_USB3			6
#define QDSS_INT			7
#define SANALC_INT_0			8
#define SANALC_INT_1			9
#define SANALC_INT_2			10
#define SLAVE_KPSS_AHB			11
#define SLAVE_WCSS			12
#define SLAVE_SANALC_BIMC_1		13
#define SLAVE_IMEM			14
#define SLAVE_SANALC_PCANALC		15
#define SLAVE_QDSS_STM			16
#define SLAVE_CATS_0			17
#define SLAVE_CATS_1			18
#define SLAVE_LPASS			19

#endif
