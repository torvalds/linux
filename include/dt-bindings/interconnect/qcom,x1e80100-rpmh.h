/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_X1E80100_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_X1E80100_H

#define MASTER_QSPI_0				0
#define MASTER_QUP_1				1
#define MASTER_SDCC_4				2
#define MASTER_UFS_MEM				3
#define SLAVE_A1NOC_SNOC			4

#define MASTER_QUP_0				0
#define MASTER_QUP_2				1
#define MASTER_CRYPTO				2
#define MASTER_SP				3
#define MASTER_QDSS_ETR				4
#define MASTER_QDSS_ETR_1			5
#define MASTER_SDCC_2				6
#define SLAVE_A2NOC_SNOC			7

#define MASTER_DDR_PERF_MODE			0
#define MASTER_QUP_CORE_0			1
#define MASTER_QUP_CORE_1			2
#define MASTER_QUP_CORE_2			3
#define SLAVE_DDR_PERF_MODE			4
#define SLAVE_QUP_CORE_0			5
#define SLAVE_QUP_CORE_1			6
#define SLAVE_QUP_CORE_2			7

#define MASTER_CNOC_CFG				0
#define SLAVE_AHB2PHY_SOUTH			1
#define SLAVE_AHB2PHY_NORTH			2
#define SLAVE_AHB2PHY_2				3
#define SLAVE_AV1_ENC_CFG			4
#define SLAVE_CAMERA_CFG			5
#define SLAVE_CLK_CTL				6
#define SLAVE_CRYPTO_0_CFG			7
#define SLAVE_DISPLAY_CFG			8
#define SLAVE_GFX3D_CFG				9
#define SLAVE_IMEM_CFG				10
#define SLAVE_IPC_ROUTER_CFG			11
#define SLAVE_PCIE_0_CFG			12
#define SLAVE_PCIE_1_CFG			13
#define SLAVE_PCIE_2_CFG			14
#define SLAVE_PCIE_3_CFG			15
#define SLAVE_PCIE_4_CFG			16
#define SLAVE_PCIE_5_CFG			17
#define SLAVE_PCIE_6A_CFG			18
#define SLAVE_PCIE_6B_CFG			19
#define SLAVE_PCIE_RSC_CFG			20
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
#define SLAVE_LPASS_QTB_CFG			43
#define SLAVE_CNOC_MNOC_CFG			44
#define SLAVE_NSP_QTB_CFG			45
#define SLAVE_QDSS_STM				46
#define SLAVE_TCU				47

#define MASTER_GEM_NOC_CNOC			0
#define MASTER_GEM_NOC_PCIE_SNOC		1
#define SLAVE_AOSS				2
#define SLAVE_TME_CFG				3
#define SLAVE_APPSS				4
#define SLAVE_CNOC_CFG				5
#define SLAVE_BOOT_IMEM				6
#define SLAVE_IMEM				7
#define SLAVE_PCIE_0				8
#define SLAVE_PCIE_1				9
#define SLAVE_PCIE_2				10
#define SLAVE_PCIE_3				11
#define SLAVE_PCIE_4				12
#define SLAVE_PCIE_5				13
#define SLAVE_PCIE_6A				14
#define SLAVE_PCIE_6B				15

#define MASTER_GPU_TCU				0
#define MASTER_PCIE_TCU				1
#define MASTER_SYS_TCU				2
#define MASTER_APPSS_PROC			3
#define MASTER_GFX3D				4
#define MASTER_LPASS_GEM_NOC			5
#define MASTER_MNOC_HF_MEM_NOC			6
#define MASTER_MNOC_SF_MEM_NOC			7
#define MASTER_COMPUTE_NOC			8
#define MASTER_ANOC_PCIE_GEM_NOC		9
#define MASTER_SNOC_SF_MEM_NOC			10
#define MASTER_GIC2				11
#define SLAVE_GEM_NOC_CNOC			12
#define SLAVE_LLCC				13
#define SLAVE_MEM_NOC_PCIE_SNOC			14
#define MASTER_MNOC_HF_MEM_NOC_DISP		15
#define MASTER_ANOC_PCIE_GEM_NOC_DISP		16
#define SLAVE_LLCC_DISP				17
#define MASTER_ANOC_PCIE_GEM_NOC_PCIE		18
#define SLAVE_LLCC_PCIE				19

#define MASTER_LPIAON_NOC			0
#define SLAVE_LPASS_GEM_NOC			1

#define MASTER_LPASS_LPINOC			0
#define SLAVE_LPIAON_NOC_LPASS_AG_NOC		1

#define MASTER_LPASS_PROC			0
#define SLAVE_LPICX_NOC_LPIAON_NOC		1

#define MASTER_LLCC				0
#define SLAVE_EBI1				1
#define MASTER_LLCC_DISP			2
#define SLAVE_EBI1_DISP				3
#define MASTER_LLCC_PCIE			4
#define SLAVE_EBI1_PCIE				5

#define MASTER_AV1_ENC				0
#define MASTER_CAMNOC_HF			1
#define MASTER_CAMNOC_ICP			2
#define MASTER_CAMNOC_SF			3
#define MASTER_EVA				4
#define MASTER_MDP				5
#define MASTER_VIDEO				6
#define MASTER_VIDEO_CV_PROC			7
#define MASTER_VIDEO_V_PROC			8
#define MASTER_CNOC_MNOC_CFG			9
#define SLAVE_MNOC_HF_MEM_NOC			10
#define SLAVE_MNOC_SF_MEM_NOC			11
#define SLAVE_SERVICE_MNOC			12
#define MASTER_MDP_DISP				13
#define SLAVE_MNOC_HF_MEM_NOC_DISP		14

#define MASTER_CDSP_PROC			0
#define SLAVE_CDSP_MEM_NOC			1

#define MASTER_PCIE_NORTH			0
#define MASTER_PCIE_SOUTH			1
#define SLAVE_ANOC_PCIE_GEM_NOC			2
#define MASTER_PCIE_NORTH_PCIE			3
#define MASTER_PCIE_SOUTH_PCIE			4
#define SLAVE_ANOC_PCIE_GEM_NOC_PCIE		5

#define MASTER_PCIE_3				0
#define MASTER_PCIE_4				1
#define MASTER_PCIE_5				2
#define SLAVE_PCIE_NORTH			3
#define MASTER_PCIE_3_PCIE			4
#define MASTER_PCIE_4_PCIE			5
#define MASTER_PCIE_5_PCIE			6
#define SLAVE_PCIE_NORTH_PCIE			7

#define MASTER_PCIE_0				0
#define MASTER_PCIE_1				1
#define MASTER_PCIE_2				2
#define MASTER_PCIE_6A				3
#define MASTER_PCIE_6B				4
#define SLAVE_PCIE_SOUTH			5
#define MASTER_PCIE_0_PCIE			6
#define MASTER_PCIE_1_PCIE			7
#define MASTER_PCIE_2_PCIE			8
#define MASTER_PCIE_6A_PCIE			9
#define MASTER_PCIE_6B_PCIE			10
#define SLAVE_PCIE_SOUTH_PCIE			11

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define MASTER_GIC1				2
#define MASTER_USB_NOC_SNOC			3
#define SLAVE_SNOC_GEM_NOC_SF			4

#define MASTER_AGGRE_USB_NORTH			0
#define MASTER_AGGRE_USB_SOUTH			1
#define SLAVE_USB_NOC_SNOC			2

#define MASTER_USB2				0
#define MASTER_USB3_MP				1
#define SLAVE_AGGRE_USB_NORTH			2

#define MASTER_USB3_0				0
#define MASTER_USB3_1				1
#define MASTER_USB3_2				2
#define MASTER_USB4_0				3
#define MASTER_USB4_1				4
#define MASTER_USB4_2				5
#define SLAVE_AGGRE_USB_SOUTH			6

#endif
