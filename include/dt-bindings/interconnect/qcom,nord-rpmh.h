/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_NORD_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_NORD_H

#define MASTER_QSPI_0				0
#define MASTER_SAILSS_MD1			1
#define MASTER_QUP_3				2
#define SLAVE_A1NOC_SNOC			3

#define MASTER_QUP_2				0
#define MASTER_CRYPTO_CORE0			1
#define MASTER_CRYPTO_CORE1			2
#define MASTER_CRYPTO_CORE2			3
#define MASTER_SDCC_4				4
#define MASTER_UFS_MEM				5
#define MASTER_USB2				6
#define MASTER_USB3_0				7
#define MASTER_USB3_1				8
#define SLAVE_A1NOC_HSCNOC			9

#define MASTER_IPA				0
#define MASTER_SOCCP_AGGR_NOC			1
#define MASTER_QDSS_ETR				2
#define MASTER_QDSS_ETR_1			3
#define SLAVE_A2NOC_SNOC			4

#define MASTER_QUP_0				0
#define MASTER_QUP_1				1
#define MASTER_EMAC_0				2
#define MASTER_EMAC_1				3
#define SLAVE_A2NOC_HSCNOC			4

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define MASTER_QUP_CORE_2			2
#define MASTER_QUP_CORE_3			3
#define SLAVE_QUP_CORE_0			4
#define SLAVE_QUP_CORE_1			5
#define SLAVE_QUP_CORE_2			6
#define SLAVE_QUP_CORE_3			7

#define MASTER_CNOC_CFG				0
#define SLAVE_PS_ETH_0				1
#define SLAVE_PS_ETH_1				2
#define SLAVE_SHS_SERVER			3
#define SLAVE_AHB2PHY_0				4
#define SLAVE_AHB2PHY_1				5
#define SLAVE_AHB2PHY_2				6
#define SLAVE_AHB2PHY_3				7
#define SLAVE_AHB2PHY_ETH_0			8
#define SLAVE_AHB2PHY_ETH_1			9
#define SLAVE_CAMERA_CFG			10
#define SLAVE_CLK_CTL				11
#define SLAVE_CRYPTO_0_CFG			12
#define SLAVE_CRYPTO_1_CFG			13
#define SLAVE_CRYPTO_2_CFG			14
#define SLAVE_DISPLAY_1_CFG			15
#define SLAVE_DISPLAY_CFG			16
#define SLAVE_DPRX0				17
#define SLAVE_DPRX1				18
#define SLAVE_EVA_CFG				19
#define SLAVE_GFX3D_CFG				20
#define SLAVE_GFX3D_1_CFG			21
#define SLAVE_I2C				22
#define SLAVE_IMEM_CFG				23
#define SLAVE_MCW_PCIE				24
#define SLAVE_MM_RSCC				25
#define SLAVE_NE_CLK_CTL			26
#define SLAVE_NSPSS0_CFG			27
#define SLAVE_NSPSS1_CFG			28
#define SLAVE_NSPSS2_CFG			29
#define SLAVE_NSPSS3_CFG			30
#define SLAVE_NW_CLK_CTL			31
#define SLAVE_PRNG				32
#define SLAVE_QDSS_CFG				33
#define SLAVE_QSPI_0				34
#define SLAVE_QUP_0				35
#define SLAVE_QUP_3				36
#define SLAVE_QUP_1				37
#define SLAVE_QUP_2				38
#define SLAVE_SAFEDMA_CFG			39
#define SLAVE_SDCC_4				40
#define SLAVE_SE_CLK_CTL			41
#define SLAVE_TCSR				42
#define SLAVE_TLMM				43
#define SLAVE_TSC_CFG				44
#define SLAVE_UFS_MEM_CFG			45
#define SLAVE_USB2				46
#define SLAVE_USB3_0				47
#define SLAVE_USB3_1				48
#define SLAVE_VENUS_CFG				49
#define SLAVE_COMPUTENOC_CFG			50
#define SLAVE_PCIE_NOC_CFG			51
#define SLAVE_QTC_CFG				52
#define SLAVE_QDSS_STM				53
#define SLAVE_SYS_TCU0_CFG			54
#define SLAVE_SYS_TCU1_CFG			55
#define SLAVE_SYS_TCU2_CFG			56

#define MASTER_MM_RSCC				0
#define MASTER_HSCNOC_CNOC			1
#define SLAVE_AOSS				2
#define SLAVE_HBCU				3
#define SLAVE_IPA_CFG				4
#define SLAVE_IPC_ROUTER_CFG			5
#define SLAVE_SOCCP				6
#define SLAVE_TME_CFG				7
#define SLAVE_PCIE_DMA				8
#define SLAVE_CNOC_CFG				9
#define SLAVE_DDRSS_CFG				10
#define SLAVE_IMEM				11

#define MASTER_HPASS_PROC_0			0
#define MASTER_HPASS_PROC_1			1
#define MASTER_HPASS_PROC_2			2
#define SLAVE_HPASS_AGNOC_AUDIO			3

#define MASTER_GPU_TCU				0
#define MASTER_QTC_TCU				1
#define MASTER_SYS_TCU_0			2
#define MASTER_SYS_TCU_1			3
#define MASTER_SYS_TCU_2			4
#define MASTER_APPSS_PROC			5
#define MASTER_A1NOC_TILE_HSCNOC		6
#define MASTER_A2NOC_TILE_HSCNOC		7
#define MASTER_GFX3D				8
#define MASTER_GFX3D_1				9
#define MASTER_HPASS_ADAS_HSCNOC		10
#define MASTER_HPASS_AUDIO_HSCNOC		11
#define MASTER_MNOC_HF_MEM_NOC			12
#define MASTER_MNOC_SF_MEM_NOC			13
#define MASTER_NSP0_HSCNOC			14
#define MASTER_NSP1_HSCNOC			15
#define MASTER_NSP2_HSCNOC			16
#define MASTER_NSP3_HSCNOC			17
#define MASTER_ANOC_PCIE_GEM_NOC		18
#define MASTER_SAILSS_MD0_HSCNOC		19
#define MASTER_SNOC_SF_MEM_NOC			20
#define MASTER_GIC				21
#define SLAVE_HSCNOC_CNOC			22
#define SLAVE_LLCC				23
#define SLAVE_MEM_NOC_PCIE_SNOC			24

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_CAMNOC_HF			0
#define MASTER_CAMNOC_NRT_ICP_SF		1
#define MASTER_CAMNOC_RT_CDM_SF			2
#define MASTER_CAMNOC_SF			3
#define MASTER_DPRX0				4
#define MASTER_DPRX1				5
#define MASTER_MDP0				6
#define MASTER_MDP1				7
#define MASTER_VIDEO_CV_PROC			8
#define MASTER_VIDEO_EVA			9
#define MASTER_VIDEO_MVP0			10
#define MASTER_VIDEO_MVP1			11
#define MASTER_VIDEO_V_PROC			12
#define SLAVE_MNOC_HF_MEM_NOC			13
#define SLAVE_MNOC_SF_MEM_NOC			14

#define MASTER_NSP0_PROC			0
#define SLAVE_NSP0_HSC_NOC			1

#define MASTER_NSP1_PROC			0
#define SLAVE_NSP1_HSC_NOC			1

#define MASTER_NSP2_PROC			0
#define SLAVE_NSP2_HSC_NOC			1

#define MASTER_NSP3_PROC			0
#define SLAVE_NSP3_HSC_NOC			1

#define MASTER_PCIE_NOC_CFG			0
#define SLAVE_PCIE_AHB2PHY_CFG			1
#define SLAVE_PCIE_CFG_0			2
#define SLAVE_PCIE_CFG_1			3
#define SLAVE_PCIE_CFG_2			4
#define SLAVE_PCIE_CFG_3			5
#define SLAVE_PCIE_DMA_0_CFG			6
#define SLAVE_PCIE_DMA_1_CFG			7
#define SLAVE_PCIE_DMA_2_CFG			8

#define MASTER_PCIE_DMA_0			0
#define MASTER_PCIE_DMA_1			1
#define MASTER_PCIE_DMA_2			2
#define MASTER_PCIE_0				3
#define MASTER_PCIE_1				4
#define MASTER_PCIE_2				5
#define MASTER_PCIE_3				6
#define SLAVE_PCIE_HSCNOC			7
#define SLAVE_PCIE_OBNOC_DMA			8

#define MASTER_CNOC_PCIE_DMA			0
#define MASTER_ANOC_PCIE_HSCNOC			1
#define MASTER_PCIE_IBNOC_DMA			2
#define SLAVE_PCIE_DMA_0			3
#define SLAVE_PCIE_DMA_1			4
#define SLAVE_PCIE_DMA_2			5
#define SLAVE_PCIE_0				6
#define SLAVE_PCIE_1				7
#define SLAVE_PCIE_2				8
#define SLAVE_PCIE_3				9

#define MASTER_A1NOC_SNOC			0
#define MASTER_A2NOC_SNOC			1
#define MASTER_CNOC_SNOC			2
#define MASTER_NSINOC_SNOC			3
#define MASTER_SAFE_DMA				4
#define SLAVE_SNOC_HSCNOC_SF			5

#endif
