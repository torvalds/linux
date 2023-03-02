/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Qualcomm SC8180x interconnect IDs
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SC8180X_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SC8180X_H

#define MASTER_A1NOC_CFG			0
#define MASTER_UFS_CARD				1
#define MASTER_UFS_GEN4				2
#define MASTER_UFS_MEM				3
#define MASTER_USB3				4
#define MASTER_USB3_1				5
#define MASTER_USB3_2				6
#define A1NOC_SNOC_SLV				7
#define SLAVE_SERVICE_A1NOC			8

#define MASTER_A2NOC_CFG			0
#define MASTER_QDSS_BAM				1
#define MASTER_QSPI_0				2
#define MASTER_QSPI_1				3
#define MASTER_QUP_0				4
#define MASTER_QUP_1				5
#define MASTER_QUP_2				6
#define MASTER_SENSORS_AHB			7
#define MASTER_CRYPTO_CORE_0			8
#define MASTER_IPA				9
#define MASTER_EMAC				10
#define MASTER_PCIE				11
#define MASTER_PCIE_1				12
#define MASTER_PCIE_2				13
#define MASTER_PCIE_3				14
#define MASTER_QDSS_ETR				15
#define MASTER_SDCC_2				16
#define MASTER_SDCC_4				17
#define A2NOC_SNOC_SLV				18
#define SLAVE_ANOC_PCIE_GEM_NOC			19
#define SLAVE_SERVICE_A2NOC			20

#define MASTER_CAMNOC_HF0_UNCOMP		0
#define MASTER_CAMNOC_HF1_UNCOMP		1
#define MASTER_CAMNOC_SF_UNCOMP			2
#define SLAVE_CAMNOC_UNCOMP			3

#define MASTER_NPU				0
#define SLAVE_CDSP_MEM_NOC			1

#define SNOC_CNOC_MAS				0
#define SLAVE_A1NOC_CFG				1
#define SLAVE_A2NOC_CFG				2
#define SLAVE_AHB2PHY_CENTER			3
#define SLAVE_AHB2PHY_EAST			4
#define SLAVE_AHB2PHY_WEST			5
#define SLAVE_AHB2PHY_SOUTH			6
#define SLAVE_AOP				7
#define SLAVE_AOSS				8
#define SLAVE_CAMERA_CFG			9
#define SLAVE_CLK_CTL				10
#define SLAVE_CDSP_CFG				11
#define SLAVE_RBCPR_CX_CFG			12
#define SLAVE_RBCPR_MMCX_CFG			13
#define SLAVE_RBCPR_MX_CFG			14
#define SLAVE_CRYPTO_0_CFG			15
#define SLAVE_CNOC_DDRSS			16
#define SLAVE_DISPLAY_CFG			17
#define SLAVE_EMAC_CFG				18
#define SLAVE_GLM				19
#define SLAVE_GRAPHICS_3D_CFG			20
#define SLAVE_IMEM_CFG				21
#define SLAVE_IPA_CFG				22
#define SLAVE_CNOC_MNOC_CFG			23
#define SLAVE_NPU_CFG				24
#define SLAVE_PCIE_0_CFG			25
#define SLAVE_PCIE_1_CFG			26
#define SLAVE_PCIE_2_CFG			27
#define SLAVE_PCIE_3_CFG			28
#define SLAVE_PDM				29
#define SLAVE_PIMEM_CFG				30
#define SLAVE_PRNG				31
#define SLAVE_QDSS_CFG				32
#define SLAVE_QSPI_0				33
#define SLAVE_QSPI_1				34
#define SLAVE_QUP_1				35
#define SLAVE_QUP_2				36
#define SLAVE_QUP_0				37
#define SLAVE_SDCC_2				38
#define SLAVE_SDCC_4				39
#define SLAVE_SECURITY				40
#define SLAVE_SNOC_CFG				41
#define SLAVE_SPSS_CFG				42
#define SLAVE_TCSR				43
#define SLAVE_TLMM_EAST				44
#define SLAVE_TLMM_SOUTH			45
#define SLAVE_TLMM_WEST				46
#define SLAVE_TSIF				47
#define SLAVE_UFS_CARD_CFG			48
#define SLAVE_UFS_MEM_0_CFG			49
#define SLAVE_UFS_MEM_1_CFG			50
#define SLAVE_USB3				51
#define SLAVE_USB3_1				52
#define SLAVE_USB3_2				53
#define SLAVE_VENUS_CFG				54
#define SLAVE_VSENSE_CTRL_CFG			55
#define SLAVE_SERVICE_CNOC			56

#define MASTER_CNOC_DC_NOC			0
#define SLAVE_GEM_NOC_CFG			1
#define SLAVE_LLCC_CFG				2

#define MASTER_AMPSS_M0				0
#define MASTER_GPU_TCU				1
#define MASTER_SYS_TCU				2
#define MASTER_GEM_NOC_CFG			3
#define MASTER_COMPUTE_NOC			4
#define MASTER_GRAPHICS_3D			5
#define MASTER_MNOC_HF_MEM_NOC			6
#define MASTER_MNOC_SF_MEM_NOC			7
#define MASTER_GEM_NOC_PCIE_SNOC		8
#define MASTER_SNOC_GC_MEM_NOC			9
#define MASTER_SNOC_SF_MEM_NOC			10
#define MASTER_ECC				11
#define SLAVE_MSS_PROC_MS_MPU_CFG		12
#define SLAVE_ECC				13
#define SLAVE_GEM_NOC_SNOC			14
#define SLAVE_LLCC				15
#define SLAVE_SERVICE_GEM_NOC			16
#define SLAVE_SERVICE_GEM_NOC_1			17

#define MASTER_LLCC				0
#define SLAVE_EBI_CH0				1

#define MASTER_CNOC_MNOC_CFG			0
#define MASTER_CAMNOC_HF0			1
#define MASTER_CAMNOC_HF1			2
#define MASTER_CAMNOC_SF			3
#define MASTER_MDP_PORT0			4
#define MASTER_MDP_PORT1			5
#define MASTER_ROTATOR				6
#define MASTER_VIDEO_P0				7
#define MASTER_VIDEO_P1				8
#define MASTER_VIDEO_PROC			9
#define SLAVE_MNOC_SF_MEM_NOC			10
#define SLAVE_MNOC_HF_MEM_NOC			11
#define SLAVE_SERVICE_MNOC			12

#define MASTER_SNOC_CFG				0
#define A1NOC_SNOC_MAS				1
#define A2NOC_SNOC_MAS				2
#define MASTER_GEM_NOC_SNOC			3
#define MASTER_PIMEM				4
#define MASTER_GIC				5
#define SLAVE_APPSS				6
#define SNOC_CNOC_SLV				7
#define SLAVE_SNOC_GEM_NOC_GC			8
#define SLAVE_SNOC_GEM_NOC_SF			9
#define SLAVE_OCIMEM				10
#define SLAVE_PIMEM				11
#define SLAVE_SERVICE_SNOC			12
#define SLAVE_PCIE_0				13
#define SLAVE_PCIE_1				14
#define SLAVE_PCIE_2				15
#define SLAVE_PCIE_3				16
#define SLAVE_QDSS_STM				17
#define SLAVE_TCU				18

#define MASTER_MNOC_HF_MEM_NOC_DISPLAY		0
#define MASTER_MNOC_SF_MEM_NOC_DISPLAY		1
#define SLAVE_LLCC_DISPLAY			2

#define MASTER_LLCC_DISPLAY			0
#define SLAVE_EBI_CH0_DISPLAY			1

#define MASTER_MDP_PORT0_DISPLAY		0
#define MASTER_MDP_PORT1_DISPLAY		1
#define MASTER_ROTATOR_DISPLAY			2
#define SLAVE_MNOC_SF_MEM_NOC_DISPLAY		3
#define SLAVE_MNOC_HF_MEM_NOC_DISPLAY		4

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define MASTER_QUP_CORE_2			2
#define SLAVE_QUP_CORE_0			3
#define SLAVE_QUP_CORE_1			4
#define SLAVE_QUP_CORE_2			5

#endif
