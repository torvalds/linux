/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_GLYMUR_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_GLYMUR_H

#define MASTER_CRYPTO				0
#define MASTER_SOCCP_PROC			1
#define MASTER_QDSS_ETR				2
#define MASTER_QDSS_ETR_1			3
#define SLAVE_A1NOC_SNOC			4

#define MASTER_UFS_MEM				0
#define MASTER_USB3_2				1
#define MASTER_USB4_2				2
#define SLAVE_A2NOC_SNOC			3

#define MASTER_QSPI_0				0
#define MASTER_QUP_0				1
#define MASTER_QUP_1				2
#define MASTER_QUP_2				3
#define MASTER_SP				4
#define MASTER_SDCC_2				5
#define MASTER_SDCC_4				6
#define MASTER_USB2				7
#define MASTER_USB3_MP				8
#define SLAVE_A3NOC_SNOC			9

#define MASTER_USB3_0				0
#define MASTER_USB3_1				1
#define MASTER_USB4_0				2
#define MASTER_USB4_1				3
#define SLAVE_A4NOC_HSCNOC			4

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define MASTER_QUP_CORE_2			2
#define SLAVE_QUP_CORE_0			3
#define SLAVE_QUP_CORE_1			4
#define SLAVE_QUP_CORE_2			5

#define MASTER_CNOC_CFG				0
#define SLAVE_AHB2PHY_SOUTH			1
#define SLAVE_AHB2PHY_NORTH			2
#define SLAVE_AHB2PHY_2				3
#define SLAVE_AHB2PHY_3				4
#define SLAVE_AV1_ENC_CFG			5
#define SLAVE_CAMERA_CFG			6
#define SLAVE_CLK_CTL				7
#define SLAVE_CRYPTO_0_CFG			8
#define SLAVE_DISPLAY_CFG			9
#define SLAVE_GFX3D_CFG				10
#define SLAVE_IMEM_CFG				11
#define SLAVE_PCIE_0_CFG			12
#define SLAVE_PCIE_1_CFG			13
#define SLAVE_PCIE_2_CFG			14
#define SLAVE_PCIE_3A_CFG			15
#define SLAVE_PCIE_3B_CFG			16
#define SLAVE_PCIE_4_CFG			17
#define SLAVE_PCIE_5_CFG			18
#define SLAVE_PCIE_6_CFG			19
#define SLAVE_PCIE_RSCC				20
#define SLAVE_PDM				21
#define SLAVE_PRNG				22
#define SLAVE_QDSS_CFG				23
#define SLAVE_QSPI_0				24
#define SLAVE_QUP_0				25
#define SLAVE_QUP_1				26
#define SLAVE_QUP_2				27
#define SLAVE_SDCC_2				28
#define SLAVE_SDCC_4				29
#define SLAVE_SMMUV3_CFG			30
#define SLAVE_TCSR				31
#define SLAVE_TLMM				32
#define SLAVE_UFS_MEM_CFG			33
#define SLAVE_USB2				34
#define SLAVE_USB3_0				35
#define SLAVE_USB3_1				36
#define SLAVE_USB3_2				37
#define SLAVE_USB3_MP				38
#define SLAVE_USB4_0				39
#define SLAVE_USB4_1				40
#define SLAVE_USB4_2				41
#define SLAVE_VENUS_CFG				42
#define SLAVE_CNOC_PCIE_SLAVE_EAST_CFG		43
#define SLAVE_CNOC_PCIE_SLAVE_WEST_CFG		44
#define SLAVE_LPASS_QTB_CFG			45
#define SLAVE_CNOC_MNOC_CFG			46
#define SLAVE_NSP_QTB_CFG			47
#define SLAVE_PCIE_EAST_ANOC_CFG		48
#define SLAVE_PCIE_WEST_ANOC_CFG		49
#define SLAVE_QDSS_STM				50
#define SLAVE_TCU				51

#define MASTER_HSCNOC_CNOC			0
#define SLAVE_AOSS				1
#define SLAVE_IPC_ROUTER_CFG			2
#define SLAVE_SOCCP				3
#define SLAVE_TME_CFG				4
#define SLAVE_APPSS				5
#define SLAVE_CNOC_CFG				6
#define SLAVE_BOOT_IMEM				7
#define SLAVE_IMEM				8

#define MASTER_GPU_TCU				0
#define MASTER_PCIE_TCU				1
#define MASTER_SYS_TCU				2
#define MASTER_APPSS_PROC			3
#define MASTER_AGGRE_NOC_EAST			4
#define MASTER_GFX3D				5
#define MASTER_LPASS_GEM_NOC			6
#define MASTER_MNOC_HF_MEM_NOC			7
#define MASTER_MNOC_SF_MEM_NOC			8
#define MASTER_COMPUTE_NOC			9
#define MASTER_PCIE_EAST			10
#define MASTER_PCIE_WEST			11
#define MASTER_SNOC_SF_MEM_NOC			12
#define MASTER_WLAN_Q6				13
#define MASTER_GIC				14
#define SLAVE_HSCNOC_CNOC			15
#define SLAVE_LLCC				16
#define SLAVE_PCIE_EAST				17
#define SLAVE_PCIE_WEST				18

#define MASTER_LPIAON_NOC			0
#define SLAVE_LPASS_GEM_NOC			1

#define MASTER_LPASS_LPINOC			0
#define SLAVE_LPIAON_NOC_LPASS_AG_NOC		1

#define MASTER_LPASS_PROC			0
#define SLAVE_LPICX_NOC_LPIAON_NOC		1

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_AV1_ENC				0
#define MASTER_CAMNOC_HF			1
#define MASTER_CAMNOC_ICP			2
#define MASTER_CAMNOC_SF			3
#define MASTER_EVA				4
#define MASTER_MDP				5
#define MASTER_CDSP_HCP				6
#define MASTER_VIDEO				7
#define MASTER_VIDEO_CV_PROC			8
#define MASTER_VIDEO_V_PROC			9
#define MASTER_CNOC_MNOC_CFG			10
#define SLAVE_MNOC_HF_MEM_NOC			11
#define SLAVE_MNOC_SF_MEM_NOC			12
#define SLAVE_SERVICE_MNOC			13

#define MASTER_CPUCP				0
#define SLAVE_NSINOC_SYSTEM_NOC			1
#define SLAVE_SERVICE_NSINOC			2

#define MASTER_CDSP_PROC			0
#define SLAVE_NSP0_HSC_NOC			1

#define MASTER_OOBMSS_SP_PROC			0
#define SLAVE_OOBMSS_SNOC			1

#define MASTER_PCIE_EAST_ANOC_CFG		0
#define MASTER_PCIE_0				1
#define MASTER_PCIE_1				2
#define MASTER_PCIE_5				3
#define SLAVE_PCIE_EAST_MEM_NOC			4
#define SLAVE_SERVICE_PCIE_EAST_AGGRE_NOC	5

#define MASTER_HSCNOC_PCIE_EAST			0
#define MASTER_CNOC_PCIE_EAST_SLAVE_CFG		1
#define SLAVE_HSCNOC_PCIE_EAST_MS_MPU_CFG	2
#define SLAVE_SERVICE_PCIE_EAST			3
#define SLAVE_PCIE_0				4
#define SLAVE_PCIE_1				5
#define SLAVE_PCIE_5				6

#define MASTER_PCIE_WEST_ANOC_CFG		0
#define MASTER_PCIE_2				1
#define MASTER_PCIE_3A				2
#define MASTER_PCIE_3B				3
#define MASTER_PCIE_4				4
#define MASTER_PCIE_6				5
#define SLAVE_PCIE_WEST_MEM_NOC			6
#define SLAVE_SERVICE_PCIE_WEST_AGGRE_NOC	7

#define MASTER_HSCNOC_PCIE_WEST			0
#define MASTER_CNOC_PCIE_WEST_SLAVE_CFG		1
#define SLAVE_HSCNOC_PCIE_WEST_MS_MPU_CFG	2
#define SLAVE_SERVICE_PCIE_WEST			3
#define SLAVE_PCIE_2				4
#define SLAVE_PCIE_3A				5
#define SLAVE_PCIE_3B				6
#define SLAVE_PCIE_4				7
#define SLAVE_PCIE_6				8

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define MASTER_A3NOC_SNOC			2
#define MASTER_NSINOC_SNOC			3
#define MASTER_OOBMSS				4
#define SLAVE_SNOC_GEM_NOC_SF			5

#endif
