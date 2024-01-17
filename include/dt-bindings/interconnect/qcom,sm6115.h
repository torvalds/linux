/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SM6115_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SM6115_H

/* BIMC */
#define MASTER_AMPSS_M0				0
#define MASTER_SNOC_BIMC_RT			1
#define MASTER_SNOC_BIMC_NRT			2
#define SNOC_BIMC_MAS				3
#define MASTER_GRAPHICS_3D			4
#define MASTER_TCU_0				5
#define SLAVE_EBI_CH0				6
#define BIMC_SNOC_SLV				7

/* CNOC */
#define SNOC_CNOC_MAS				0
#define MASTER_QDSS_DAP				1
#define SLAVE_AHB2PHY_USB			2
#define SLAVE_APSS_THROTTLE_CFG			3
#define SLAVE_BIMC_CFG				4
#define SLAVE_BOOT_ROM				5
#define SLAVE_CAMERA_NRT_THROTTLE_CFG		6
#define SLAVE_CAMERA_RT_THROTTLE_CFG		7
#define SLAVE_CAMERA_CFG			8
#define SLAVE_CLK_CTL				9
#define SLAVE_RBCPR_CX_CFG			10
#define SLAVE_RBCPR_MX_CFG			11
#define SLAVE_CRYPTO_0_CFG			12
#define SLAVE_DCC_CFG				13
#define SLAVE_DDR_PHY_CFG			14
#define SLAVE_DDR_SS_CFG			15
#define SLAVE_DISPLAY_CFG			16
#define SLAVE_DISPLAY_THROTTLE_CFG		17
#define SLAVE_GPU_CFG				18
#define SLAVE_GPU_THROTTLE_CFG			19
#define SLAVE_HWKM_CORE				20
#define SLAVE_IMEM_CFG				21
#define SLAVE_IPA_CFG				22
#define SLAVE_LPASS				23
#define SLAVE_MAPSS				24
#define SLAVE_MDSP_MPU_CFG			25
#define SLAVE_MESSAGE_RAM			26
#define SLAVE_CNOC_MSS				27
#define SLAVE_PDM				28
#define SLAVE_PIMEM_CFG				29
#define SLAVE_PKA_CORE				30
#define SLAVE_PMIC_ARB				31
#define SLAVE_QDSS_CFG				32
#define SLAVE_QM_CFG				33
#define SLAVE_QM_MPU_CFG			34
#define SLAVE_QPIC				35
#define SLAVE_QUP_0				36
#define SLAVE_RPM				37
#define SLAVE_SDCC_1				38
#define SLAVE_SDCC_2				39
#define SLAVE_SECURITY				40
#define SLAVE_SNOC_CFG				41
#define SLAVE_TCSR				42
#define SLAVE_TLMM				43
#define SLAVE_USB3				44
#define SLAVE_VENUS_CFG				45
#define SLAVE_VENUS_THROTTLE_CFG		46
#define SLAVE_VSENSE_CTRL_CFG			47
#define SLAVE_SERVICE_CNOC			48

/* SNOC */
#define MASTER_CRYPTO_CORE0			0
#define MASTER_SNOC_CFG				1
#define MASTER_TIC				2
#define MASTER_ANOC_SNOC			3
#define BIMC_SNOC_MAS				4
#define MASTER_PIMEM				5
#define MASTER_QDSS_BAM				6
#define MASTER_QPIC				7
#define MASTER_QUP_0				8
#define MASTER_IPA				9
#define MASTER_QDSS_ETR				10
#define MASTER_SDCC_1				11
#define MASTER_SDCC_2				12
#define MASTER_USB3				13
#define SLAVE_APPSS				14
#define SNOC_CNOC_SLV				15
#define SLAVE_OCIMEM				16
#define SLAVE_PIMEM				17
#define SNOC_BIMC_SLV				18
#define SLAVE_SERVICE_SNOC			19
#define SLAVE_QDSS_STM				20
#define SLAVE_TCU				21
#define SLAVE_ANOC_SNOC				22

/* CLK Virtual */
#define MASTER_QUP_CORE_0			0
#define SLAVE_QUP_CORE_0			1

/* MMRT Virtual */
#define MASTER_CAMNOC_HF			0
#define MASTER_MDP_PORT0			1
#define SLAVE_SNOC_BIMC_RT			2

/* MMNRT Virtual */
#define MASTER_CAMNOC_SF			0
#define MASTER_VIDEO_P0				1
#define MASTER_VIDEO_PROC			2
#define SLAVE_SNOC_BIMC_NRT			3

#endif
