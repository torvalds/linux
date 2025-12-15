/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_KAANAPALI_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_KAANAPALI_H

#define MASTER_QSPI_0				0
#define MASTER_CRYPTO				1
#define MASTER_QUP_1				2
#define MASTER_SDCC_4				3
#define MASTER_UFS_MEM				4
#define MASTER_USB3				5
#define MASTER_QUP_2				6
#define MASTER_QUP_3				7
#define MASTER_QUP_4				8
#define MASTER_IPA				9
#define MASTER_SOCCP_PROC			10
#define MASTER_SP				11
#define MASTER_QDSS_ETR				12
#define MASTER_QDSS_ETR_1			13
#define MASTER_SDCC_2				14
#define SLAVE_A1NOC_SNOC			15
#define SLAVE_A2NOC_SNOC			16

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define MASTER_QUP_CORE_2			2
#define MASTER_QUP_CORE_3			3
#define MASTER_QUP_CORE_4			4
#define SLAVE_QUP_CORE_0			5
#define SLAVE_QUP_CORE_1			6
#define SLAVE_QUP_CORE_2			7
#define SLAVE_QUP_CORE_3			8
#define SLAVE_QUP_CORE_4			9

#define MASTER_CNOC_CFG				0
#define SLAVE_AHB2PHY_SOUTH			1
#define SLAVE_AHB2PHY_NORTH			2
#define SLAVE_CAMERA_CFG			3
#define SLAVE_CLK_CTL				4
#define SLAVE_CRYPTO_0_CFG			5
#define SLAVE_DISPLAY_CFG			6
#define SLAVE_EVA_CFG				7
#define SLAVE_GFX3D_CFG				8
#define SLAVE_I2C				9
#define SLAVE_I3C_IBI0_CFG			10
#define SLAVE_I3C_IBI1_CFG			11
#define SLAVE_IMEM_CFG				12
#define SLAVE_IPC_ROUTER_CFG			13
#define SLAVE_CNOC_MSS				14
#define SLAVE_PCIE_CFG				15
#define SLAVE_PRNG				16
#define SLAVE_QDSS_CFG				17
#define SLAVE_QSPI_0				18
#define SLAVE_QUP_1				19
#define SLAVE_QUP_2				20
#define SLAVE_QUP_3				21
#define SLAVE_QUP_4				22
#define SLAVE_SDCC_2				23
#define SLAVE_SDCC_4				24
#define SLAVE_SPSS_CFG				25
#define SLAVE_TCSR				26
#define SLAVE_TLMM				27
#define SLAVE_UFS_MEM_CFG			28
#define SLAVE_USB3				29
#define SLAVE_VENUS_CFG				30
#define SLAVE_VSENSE_CTRL_CFG			31
#define SLAVE_CNOC_MNOC_CFG			32
#define SLAVE_PCIE_ANOC_CFG			33
#define SLAVE_QDSS_STM				34
#define SLAVE_TCU				35

#define MASTER_GEM_NOC_CNOC			0
#define MASTER_GEM_NOC_PCIE_SNOC		1
#define SLAVE_AOSS				2
#define SLAVE_IPA_CFG				3
#define SLAVE_IPC_ROUTER_FENCE			4
#define SLAVE_SOCCP				5
#define SLAVE_TME_CFG				6
#define SLAVE_APPSS				7
#define SLAVE_CNOC_CFG				8
#define SLAVE_DDRSS_CFG				9
#define SLAVE_BOOT_IMEM				10
#define SLAVE_IMEM				11
#define SLAVE_PCIE_0				12

#define MASTER_GPU_TCU				0
#define MASTER_SYS_TCU				1
#define MASTER_APPSS_PROC			2
#define MASTER_GFX3D				3
#define MASTER_LPASS_GEM_NOC			4
#define MASTER_MSS_PROC				5
#define MASTER_MNOC_HF_MEM_NOC			6
#define MASTER_MNOC_SF_MEM_NOC			7
#define MASTER_COMPUTE_NOC			8
#define MASTER_ANOC_PCIE_GEM_NOC		9
#define MASTER_QPACE				10
#define MASTER_SNOC_SF_MEM_NOC			11
#define MASTER_WLAN_Q6				12
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
#define MASTER_CAMNOC_NRT_ICP_SF		1
#define MASTER_CAMNOC_RT_CDM_SF			2
#define MASTER_CAMNOC_SF			3
#define MASTER_MDP				4
#define MASTER_MDSS_DCP				5
#define MASTER_CDSP_HCP				6
#define MASTER_VIDEO_CV_PROC			7
#define MASTER_VIDEO_EVA			8
#define MASTER_VIDEO_MVP			9
#define MASTER_VIDEO_V_PROC			10
#define MASTER_CNOC_MNOC_CFG			11
#define SLAVE_MNOC_HF_MEM_NOC			12
#define SLAVE_MNOC_SF_MEM_NOC			13
#define SLAVE_SERVICE_MNOC			14

#define MASTER_CDSP_PROC			0
#define SLAVE_CDSP_MEM_NOC			1

#define MASTER_PCIE_ANOC_CFG			0
#define MASTER_PCIE_0				1
#define SLAVE_ANOC_PCIE_GEM_NOC			2
#define SLAVE_SERVICE_PCIE_ANOC			3

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define MASTER_APSS_NOC				2
#define MASTER_CNOC_SNOC			3
#define SLAVE_SNOC_GEM_NOC_SF			4

#endif
