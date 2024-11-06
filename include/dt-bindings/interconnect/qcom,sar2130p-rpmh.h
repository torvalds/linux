/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2024, Linaro Ltd.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SAR2130P_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SAR2130P_H

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define SLAVE_QUP_CORE_0			2
#define SLAVE_QUP_CORE_1			3

#define MASTER_GEM_NOC_CNOC			0
#define MASTER_GEM_NOC_PCIE_SNOC		1
#define MASTER_QDSS_DAP				2
#define SLAVE_AHB2PHY_SOUTH			3
#define SLAVE_AOSS				4
#define SLAVE_CAMERA_CFG			5
#define SLAVE_CLK_CTL				6
#define SLAVE_CDSP_CFG				7
#define SLAVE_RBCPR_CX_CFG			8
#define SLAVE_RBCPR_MMCX_CFG			9
#define SLAVE_RBCPR_MXA_CFG			10
#define SLAVE_RBCPR_MXC_CFG			11
#define SLAVE_CPR_NSPCX				12
#define SLAVE_CRYPTO_0_CFG			13
#define SLAVE_CX_RDPM				14
#define SLAVE_DISPLAY_CFG			15
#define SLAVE_GFX3D_CFG				16
#define SLAVE_IMEM_CFG				17
#define SLAVE_IPC_ROUTER_CFG			18
#define SLAVE_LPASS				19
#define SLAVE_MX_RDPM				20
#define SLAVE_PCIE_0_CFG			21
#define SLAVE_PCIE_1_CFG			22
#define SLAVE_PDM				23
#define SLAVE_PIMEM_CFG				24
#define SLAVE_PRNG				25
#define SLAVE_QDSS_CFG				26
#define SLAVE_QSPI_0				27
#define SLAVE_QUP_0				28
#define SLAVE_QUP_1				29
#define SLAVE_SDCC_1				30
#define SLAVE_TCSR				31
#define SLAVE_TLMM				32
#define SLAVE_TME_CFG				33
#define SLAVE_USB3_0				34
#define SLAVE_VENUS_CFG				35
#define SLAVE_VSENSE_CTRL_CFG			36
#define SLAVE_WLAN_Q6_CFG			37
#define SLAVE_DDRSS_CFG				38
#define SLAVE_CNOC_MNOC_CFG			39
#define SLAVE_SNOC_CFG				40
#define SLAVE_IMEM				41
#define SLAVE_PIMEM				42
#define SLAVE_SERVICE_CNOC			43
#define SLAVE_PCIE_0				44
#define SLAVE_PCIE_1				45
#define SLAVE_QDSS_STM				46
#define SLAVE_TCU				47

#define MASTER_GPU_TCU				0
#define MASTER_SYS_TCU				1
#define MASTER_APPSS_PROC			2
#define MASTER_GFX3D				3
#define MASTER_MNOC_HF_MEM_NOC			4
#define MASTER_MNOC_SF_MEM_NOC			5
#define MASTER_COMPUTE_NOC			6
#define MASTER_ANOC_PCIE_GEM_NOC		7
#define MASTER_SNOC_GC_MEM_NOC			8
#define MASTER_SNOC_SF_MEM_NOC			9
#define MASTER_WLAN_Q6				10
#define SLAVE_GEM_NOC_CNOC			11
#define SLAVE_LLCC				12
#define SLAVE_MEM_NOC_PCIE_SNOC			13

#define MASTER_CNOC_LPASS_AG_NOC		0
#define MASTER_LPASS_PROC			1
#define SLAVE_LPASS_CORE_CFG			2
#define SLAVE_LPASS_LPI_CFG			3
#define SLAVE_LPASS_MPU_CFG			4
#define SLAVE_LPASS_TOP_CFG			5
#define SLAVE_LPASS_SNOC			6
#define SLAVE_SERVICES_LPASS_AML_NOC		7
#define SLAVE_SERVICE_LPASS_AG_NOC		8

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_CAMNOC_HF			0
#define MASTER_CAMNOC_ICP			1
#define MASTER_CAMNOC_SF			2
#define MASTER_LSR				3
#define MASTER_MDP				4
#define MASTER_CNOC_MNOC_CFG			5
#define MASTER_VIDEO				6
#define MASTER_VIDEO_CV_PROC			7
#define MASTER_VIDEO_PROC			8
#define MASTER_VIDEO_V_PROC			9
#define SLAVE_MNOC_HF_MEM_NOC			10
#define SLAVE_MNOC_SF_MEM_NOC			11
#define SLAVE_SERVICE_MNOC			12

#define MASTER_CDSP_NOC_CFG			0
#define MASTER_CDSP_PROC			1
#define SLAVE_CDSP_MEM_NOC			2
#define SLAVE_SERVICE_NSP_NOC			3

#define MASTER_PCIE_0				0
#define MASTER_PCIE_1				1
#define SLAVE_ANOC_PCIE_GEM_NOC			2

#define MASTER_GIC_AHB				0
#define MASTER_QDSS_BAM				1
#define MASTER_QSPI_0				2
#define MASTER_QUP_0				3
#define MASTER_QUP_1				4
#define MASTER_A2NOC_SNOC			5
#define MASTER_CNOC_DATAPATH			6
#define MASTER_LPASS_ANOC			7
#define MASTER_SNOC_CFG				8
#define MASTER_CRYPTO				9
#define MASTER_PIMEM				10
#define MASTER_GIC				11
#define MASTER_QDSS_ETR				12
#define MASTER_QDSS_ETR_1			13
#define MASTER_SDCC_1				14
#define MASTER_USB3_0				15
#define SLAVE_A2NOC_SNOC			16
#define SLAVE_SNOC_GEM_NOC_GC			17
#define SLAVE_SNOC_GEM_NOC_SF			18
#define SLAVE_SERVICE_SNOC			19

#endif
