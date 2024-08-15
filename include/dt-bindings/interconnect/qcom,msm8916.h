/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Qualcomm interconnect IDs
 *
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_MSM8916_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_MSM8916_H

#define BIMC_SNOC_SLV			0
#define MASTER_JPEG			1
#define MASTER_MDP_PORT0		2
#define MASTER_QDSS_BAM			3
#define MASTER_QDSS_ETR			4
#define MASTER_SNOC_CFG			5
#define MASTER_VFE			6
#define MASTER_VIDEO_P0			7
#define SNOC_MM_INT_0			8
#define SNOC_MM_INT_1			9
#define SNOC_MM_INT_2			10
#define SNOC_MM_INT_BIMC		11
#define PCNOC_SNOC_SLV			12
#define SLAVE_APSS			13
#define SLAVE_CATS_128			14
#define SLAVE_OCMEM_64			15
#define SLAVE_IMEM			16
#define SLAVE_QDSS_STM			17
#define SLAVE_SRVC_SNOC			18
#define SNOC_BIMC_0_MAS			19
#define SNOC_BIMC_1_MAS			20
#define SNOC_INT_0			21
#define SNOC_INT_1			22
#define SNOC_INT_BIMC			23
#define SNOC_PCNOC_MAS			24
#define SNOC_QDSS_INT			25

#define BIMC_SNOC_MAS			0
#define MASTER_AMPSS_M0			1
#define MASTER_GRAPHICS_3D		2
#define MASTER_TCU0			3
#define MASTER_TCU1			4
#define SLAVE_AMPSS_L2			5
#define SLAVE_EBI_CH0			6
#define SNOC_BIMC_0_SLV			7
#define SNOC_BIMC_1_SLV			8

#define MASTER_BLSP_1			0
#define MASTER_DEHR			1
#define MASTER_LPASS			2
#define MASTER_CRYPTO_CORE0		3
#define MASTER_SDCC_1			4
#define MASTER_SDCC_2			5
#define MASTER_SPDM			6
#define MASTER_USB_HS			7
#define PCNOC_INT_0			8
#define PCNOC_INT_1			9
#define PCNOC_MAS_0			10
#define PCNOC_MAS_1			11
#define PCNOC_SLV_0			12
#define PCNOC_SLV_1			13
#define PCNOC_SLV_2			14
#define PCNOC_SLV_3			15
#define PCNOC_SLV_4			16
#define PCNOC_SLV_8			17
#define PCNOC_SLV_9			18
#define PCNOC_SNOC_MAS			19
#define SLAVE_BIMC_CFG			20
#define SLAVE_BLSP_1			21
#define SLAVE_BOOT_ROM			22
#define SLAVE_CAMERA_CFG		23
#define SLAVE_CLK_CTL			24
#define SLAVE_CRYPTO_0_CFG		25
#define SLAVE_DEHR_CFG			26
#define SLAVE_DISPLAY_CFG		27
#define SLAVE_GRAPHICS_3D_CFG		28
#define SLAVE_IMEM_CFG			29
#define SLAVE_LPASS			30
#define SLAVE_MPM			31
#define SLAVE_MSG_RAM			32
#define SLAVE_MSS			33
#define SLAVE_PDM			34
#define SLAVE_PMIC_ARB			35
#define SLAVE_PCNOC_CFG			36
#define SLAVE_PRNG			37
#define SLAVE_QDSS_CFG			38
#define SLAVE_RBCPR_CFG			39
#define SLAVE_SDCC_1			40
#define SLAVE_SDCC_2			41
#define SLAVE_SECURITY			42
#define SLAVE_SNOC_CFG			43
#define SLAVE_SPDM			44
#define SLAVE_TCSR			45
#define SLAVE_TLMM			46
#define SLAVE_USB_HS			47
#define SLAVE_VENUS_CFG			48
#define SNOC_PCNOC_SLV			49

#endif
