/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_ELIZA_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_ELIZA_H

#define MASTER_QSPI_0				0
#define MASTER_QUP_1				1
#define MASTER_UFS_MEM				2
#define MASTER_USB3_0				3
#define SLAVE_A1NOC_SNOC			4

#define MASTER_QUP_2				0
#define MASTER_CRYPTO				1
#define MASTER_IPA				2
#define MASTER_SOCCP_AGGR_NOC			3
#define MASTER_QDSS_ETR				4
#define MASTER_QDSS_ETR_1			5
#define MASTER_SDCC_1				6
#define MASTER_SDCC_2				7
#define SLAVE_A2NOC_SNOC			8

#define MASTER_QUP_CORE_1			0
#define MASTER_QUP_CORE_2			1
#define SLAVE_QUP_CORE_1			2
#define SLAVE_QUP_CORE_2			3

#define MASTER_CNOC_CFG				0
#define SLAVE_AHB2PHY_SOUTH			1
#define SLAVE_AHB2PHY_NORTH			2
#define SLAVE_CAMERA_CFG			3
#define SLAVE_CLK_CTL				4
#define SLAVE_CRYPTO_0_CFG			5
#define SLAVE_DISPLAY_CFG			6
#define SLAVE_GFX3D_CFG				7
#define SLAVE_I3C_IBI0_CFG			8
#define SLAVE_I3C_IBI1_CFG			9
#define SLAVE_IMEM_CFG				10
#define SLAVE_CNOC_MSS				11
#define SLAVE_PCIE_0_CFG			12
#define SLAVE_PRNG				13
#define SLAVE_QDSS_CFG				14
#define SLAVE_QSPI_0				15
#define SLAVE_QUP_1				16
#define SLAVE_QUP_2				17
#define SLAVE_SDCC_2				18
#define SLAVE_TCSR				19
#define SLAVE_TLMM				20
#define SLAVE_UFS_MEM_CFG			21
#define SLAVE_USB3_0				22
#define SLAVE_VENUS_CFG				23
#define SLAVE_VSENSE_CTRL_CFG			24
#define SLAVE_CNOC_MNOC_HF_CFG			25
#define SLAVE_CNOC_MNOC_SF_CFG			26
#define SLAVE_PCIE_ANOC_CFG			27
#define SLAVE_QDSS_STM				28
#define SLAVE_TCU				29

#define MASTER_GEM_NOC_CNOC			0
#define MASTER_GEM_NOC_PCIE_SNOC		1
#define SLAVE_AOSS				2
#define SLAVE_IPA_CFG				3
#define SLAVE_IPC_ROUTER_CFG			4
#define SLAVE_SOCCP				5
#define SLAVE_TME_CFG				6
#define SLAVE_APPSS				7
#define SLAVE_CNOC_CFG				8
#define SLAVE_DDRSS_CFG				9
#define SLAVE_BOOT_IMEM				10
#define SLAVE_IMEM				11
#define SLAVE_BOOT_IMEM_2			12
#define SLAVE_SERVICE_CNOC			13
#define SLAVE_PCIE_0				14
#define SLAVE_PCIE_1				15

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
#define MASTER_SNOC_SF_MEM_NOC			10
#define MASTER_WLAN_Q6				11
#define MASTER_GIC				12
#define SLAVE_GEM_NOC_CNOC			13
#define SLAVE_LLCC				14
#define SLAVE_MEM_NOC_PCIE_SNOC			15

#define MASTER_LPIAON_NOC			0
#define SLAVE_LPASS_GEM_NOC			1

#define MASTER_LPASS_LPINOC			0
#define SLAVE_LPIAON_NOC_LPASS_AG_NOC		1

#define MASTER_LPASS_PROC			0
#define SLAVE_LPICX_NOC_LPIAON_NOC		1

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_CAMNOC_NRT_ICP_SF		0
#define MASTER_CAMNOC_RT_CDM_SF			1
#define MASTER_CAMNOC_SF			2
#define MASTER_VIDEO_MVP			3
#define MASTER_VIDEO_V_PROC			4
#define MASTER_CNOC_MNOC_SF_CFG			5
#define MASTER_CAMNOC_HF			6
#define MASTER_MDP				7
#define MASTER_CNOC_MNOC_HF_CFG			8
#define SLAVE_MNOC_SF_MEM_NOC			9
#define SLAVE_SERVICE_MNOC_SF			10
#define SLAVE_MNOC_HF_MEM_NOC			11
#define SLAVE_SERVICE_MNOC_HF			12

#define MASTER_CDSP_PROC			0
#define SLAVE_CDSP_MEM_NOC			1

#define MASTER_PCIE_ANOC_CFG			0
#define MASTER_PCIE_0				1
#define MASTER_PCIE_1				2
#define SLAVE_ANOC_PCIE_GEM_NOC			3
#define SLAVE_SERVICE_PCIE_ANOC			4

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define MASTER_CNOC_SNOC			2
#define MASTER_NSINOC_SNOC			3
#define SLAVE_SNOC_GEM_NOC_SF			4

#endif
