/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SM8650_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SM8650_H

#define MASTER_QSPI_0				0
#define MASTER_QUP_1				1
#define MASTER_QUP_3				2
#define MASTER_SDCC_4				3
#define MASTER_UFS_MEM				4
#define MASTER_USB3_0				5
#define SLAVE_A1NOC_SNOC			6

#define MASTER_QDSS_BAM				0
#define MASTER_QUP_2				1
#define MASTER_CRYPTO				2
#define MASTER_IPA				3
#define MASTER_SP				4
#define MASTER_QDSS_ETR				5
#define MASTER_QDSS_ETR_1			6
#define MASTER_SDCC_2				7
#define SLAVE_A2NOC_SNOC			8

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define MASTER_QUP_CORE_2			2
#define SLAVE_QUP_CORE_0			3
#define SLAVE_QUP_CORE_1			4
#define SLAVE_QUP_CORE_2			5

#define MASTER_CNOC_CFG				0
#define SLAVE_AHB2PHY_SOUTH			1
#define SLAVE_AHB2PHY_NORTH			2
#define SLAVE_CAMERA_CFG			3
#define SLAVE_CLK_CTL				4
#define SLAVE_RBCPR_CX_CFG			5
#define SLAVE_CPR_HMX				6
#define SLAVE_RBCPR_MMCX_CFG			7
#define SLAVE_RBCPR_MXA_CFG			8
#define SLAVE_RBCPR_MXC_CFG			9
#define SLAVE_CPR_NSPCX				10
#define SLAVE_CRYPTO_0_CFG			11
#define SLAVE_CX_RDPM				12
#define SLAVE_DISPLAY_CFG			13
#define SLAVE_GFX3D_CFG				14
#define SLAVE_I2C				15
#define SLAVE_I3C_IBI0_CFG			16
#define SLAVE_I3C_IBI1_CFG			17
#define SLAVE_IMEM_CFG				18
#define SLAVE_CNOC_MSS				19
#define SLAVE_MX_2_RDPM				20
#define SLAVE_MX_RDPM				21
#define SLAVE_PCIE_0_CFG			22
#define SLAVE_PCIE_1_CFG			23
#define SLAVE_PCIE_RSCC				24
#define SLAVE_PDM				25
#define SLAVE_PRNG				26
#define SLAVE_QDSS_CFG				27
#define SLAVE_QSPI_0				28
#define SLAVE_QUP_3				29
#define SLAVE_QUP_1				30
#define SLAVE_QUP_2				31
#define SLAVE_SDCC_2				32
#define SLAVE_SDCC_4				33
#define SLAVE_SPSS_CFG				34
#define SLAVE_TCSR				35
#define SLAVE_TLMM				36
#define SLAVE_UFS_MEM_CFG			37
#define SLAVE_USB3_0				38
#define SLAVE_VENUS_CFG				39
#define SLAVE_VSENSE_CTRL_CFG			40
#define SLAVE_CNOC_MNOC_CFG			41
#define SLAVE_NSP_QTB_CFG			42
#define SLAVE_PCIE_ANOC_CFG			43
#define SLAVE_SERVICE_CNOC_CFG			44
#define SLAVE_QDSS_STM				45
#define SLAVE_TCU				46

#define MASTER_GEM_NOC_CNOC			0
#define MASTER_GEM_NOC_PCIE_SNOC		1
#define SLAVE_AOSS				2
#define SLAVE_IPA_CFG				3
#define SLAVE_IPC_ROUTER_CFG			4
#define SLAVE_TME_CFG				5
#define SLAVE_APPSS				6
#define SLAVE_CNOC_CFG				7
#define SLAVE_DDRSS_CFG				8
#define SLAVE_IMEM				9
#define SLAVE_SERVICE_CNOC			10
#define SLAVE_PCIE_0				11
#define SLAVE_PCIE_1				12

#define MASTER_GPU_TCU				0
#define MASTER_SYS_TCU				1
#define MASTER_UBWC_P_TCU			2
#define MASTER_APPSS_PROC			3
#define MASTER_GFX3D				4
#define MASTER_LPASS_GEM_NOC			5
#define MASTER_MSS_PROC				6
#define MASTER_MNOC_HF_MEM_NOC			7
#define MASTER_MNOC_SF_MEM_NOC			8
#define MASTER_COMPUTE_NOC			9
#define MASTER_ANOC_PCIE_GEM_NOC		10
#define MASTER_SNOC_SF_MEM_NOC			11
#define MASTER_UBWC_P				12
#define MASTER_GIC				13
#define SLAVE_GEM_NOC_CNOC			14
#define SLAVE_LLCC				15
#define SLAVE_MEM_NOC_PCIE_SNOC			16

#define MASTER_LPIAON_NOC			0
#define SLAVE_LPASS_GEM_NOC			1

#define MASTER_LPASS_LPINOC			0
#define SLAVE_LPIAON_NOC_LPASS_AG_NOC		1

#define MASTER_LPASS_PROC			0
#define SLAVE_LPICX_NOC_LPIAON_NOC		1

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_CAMNOC_HF			0
#define MASTER_CAMNOC_ICP			1
#define MASTER_CAMNOC_SF			2
#define MASTER_MDP				3
#define MASTER_CDSP_HCP				4
#define MASTER_VIDEO				5
#define MASTER_VIDEO_CV_PROC			6
#define MASTER_VIDEO_PROC			7
#define MASTER_VIDEO_V_PROC			8
#define MASTER_CNOC_MNOC_CFG			9
#define SLAVE_MNOC_HF_MEM_NOC			10
#define SLAVE_MNOC_SF_MEM_NOC			11
#define SLAVE_SERVICE_MNOC			12

#define MASTER_CDSP_PROC			0
#define SLAVE_CDSP_MEM_NOC			1

#define MASTER_PCIE_ANOC_CFG			0
#define MASTER_PCIE_0				1
#define MASTER_PCIE_1				2
#define SLAVE_ANOC_PCIE_GEM_NOC			3
#define SLAVE_SERVICE_PCIE_ANOC			4

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define SLAVE_SNOC_GEM_NOC_SF			2
#define MASTER_APSS_NOC				3

#endif
