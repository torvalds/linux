/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SHIKRA_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SHIKRA_H

#define MASTER_QUP_CORE_0			0
#define SLAVE_QUP_CORE_0			1

#define SNOC_CNOC_MAS				0
#define MASTER_QDSS_DAP				1
#define SLAVE_AHB2PHY_USB			2
#define SLAVE_APSS_THROTTLE_CFG			3
#define SLAVE_AUDIO				4
#define SLAVE_BOOT_ROM				5
#define SLAVE_CAMERA_NRT_THROTTLE_CFG		6
#define SLAVE_CAMERA_CFG			7
#define SLAVE_CDSP_THROTTLE_CFG			8
#define SLAVE_CLK_CTL				9
#define SLAVE_DSP_CFG				10
#define SLAVE_RBCPR_CX_CFG			11
#define SLAVE_RBCPR_MX_CFG			12
#define SLAVE_CRYPTO_0_CFG			13
#define SLAVE_DDR_SS_CFG			14
#define SLAVE_DISPLAY_CFG			15
#define SLAVE_EMAC0_CFG				16
#define SLAVE_EMAC1_CFG				17
#define SLAVE_GPU_CFG				18
#define SLAVE_GPU_THROTTLE_CFG			19
#define SLAVE_HWKM				20
#define SLAVE_IMEM_CFG				21
#define SLAVE_MAPSS				22
#define SLAVE_MDSP_MPU_CFG			23
#define SLAVE_MESSAGE_RAM			24
#define SLAVE_MSS				25
#define SLAVE_PCIE_CFG				26
#define SLAVE_PDM				27
#define SLAVE_PIMEM_CFG				28
#define SLAVE_PKA_WRAPPER_CFG			29
#define SLAVE_PMIC_ARB				30
#define SLAVE_QDSS_CFG				31
#define SLAVE_QM_CFG				32
#define SLAVE_QM_MPU_CFG			33
#define SLAVE_QPIC				34
#define SLAVE_QUP_0				35
#define SLAVE_RPM				36
#define SLAVE_SDCC_1				37
#define SLAVE_SDCC_2				38
#define SLAVE_SECURITY				39
#define SLAVE_SNOC_CFG				40
#define SNOC_SF_THROTTLE_CFG			41
#define SLAVE_TLMM				42
#define SLAVE_TSCSS				43
#define SLAVE_USB2				44
#define SLAVE_USB3				45
#define SLAVE_VENUS_CFG				46
#define SLAVE_VENUS_THROTTLE_CFG		47
#define SLAVE_VSENSE_CTRL_CFG			48
#define SLAVE_SERVICE_CNOC			49

#define MASTER_LLCC				0
#define SLAVE_EBI_CH0				1

#define MASTER_GRAPHICS_3D			0
#define MASTER_MNOC_HF_MEM_NOC			1
#define MASTER_ANOC_PCIE_MEM_NOC		2
#define MASTER_SNOC_SF_MEM_NOC			3
#define MASTER_AMPSS_M0				4
#define MASTER_SYS_TCU				5
#define SLAVE_LLCC				6
#define SLAVE_MEMNOC_SNOC			7
#define SLAVE_MEM_NOC_PCIE_SNOC			8

#define MASTER_CAMNOC_SF			0
#define MASTER_VIDEO_P0				1
#define MASTER_VIDEO_PROC			2
#define SLAVE_MMNRT_VIRT			3

#define MASTER_CAMNOC_HF			0
#define MASTER_MDP_PORT0			1
#define MASTER_MMRT_VIRT			2
#define SLAVE_MM_MEMNOC				3

#define MASTER_SNOC_CFG				0
#define MASTER_TIC				1
#define MASTER_ANOC_SNOC			2
#define MASTER_MEMNOC_PCIE			3
#define MASTER_MEMNOC_SNOC			4
#define MASTER_PIMEM				5
#define MASTER_PCIE2_0				6
#define MASTER_QDSS_BAM				7
#define MASTER_QPIC				8
#define MASTER_QUP_0				9
#define CNOC_SNOC_MAS				10
#define MASTER_AUDIO				11
#define MASTER_EMAC_0				12
#define MASTER_EMAC_1				13
#define MASTER_QDSS_ETR				14
#define MASTER_SDCC_1				15
#define MASTER_SDCC_2				16
#define MASTER_USB2_0				17
#define MASTER_USB3				18
#define MASTER_CRYPTO_CORE0			19
#define SLAVE_APPSS				20
#define SLAVE_MCUSS				21
#define SLAVE_WCSS				22
#define SLAVE_MEMNOC_SF				23
#define SNOC_CNOC_SLV				24
#define SLAVE_BOOTIMEM				25
#define SLAVE_OCIMEM				26
#define SLAVE_PIMEM				27
#define SLAVE_SERVICE_SNOC			28
#define SLAVE_PCIE2_0				29
#define SLAVE_QDSS_STM				30
#define SLAVE_TCU				31
#define SLAVE_PCIE_MEMNOC			32
#define SLAVE_ANOC_SNOC				33

#endif
