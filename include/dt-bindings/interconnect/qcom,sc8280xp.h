/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SC8280XP_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SC8280XP_H

/* aggre1_noc */
#define MASTER_QSPI_0			0
#define MASTER_QUP_1			1
#define MASTER_QUP_2			2
#define MASTER_A1NOC_CFG		3
#define MASTER_IPA			4
#define MASTER_EMAC_1			5
#define MASTER_SDCC_4			6
#define MASTER_UFS_MEM			7
#define MASTER_USB3_0			8
#define MASTER_USB3_1			9
#define MASTER_USB3_MP			10
#define MASTER_USB4_0			11
#define MASTER_USB4_1			12
#define SLAVE_A1NOC_SNOC		13
#define SLAVE_USB_NOC_SNOC		14
#define SLAVE_SERVICE_A1NOC		15

/* aggre2_noc */
#define MASTER_QDSS_BAM			0
#define MASTER_QUP_0			1
#define MASTER_A2NOC_CFG		2
#define MASTER_CRYPTO			3
#define MASTER_SENSORS_PROC		4
#define MASTER_SP			5
#define MASTER_EMAC			6
#define MASTER_PCIE_0			7
#define MASTER_PCIE_1			8
#define MASTER_PCIE_2A			9
#define MASTER_PCIE_2B			10
#define MASTER_PCIE_3A			11
#define MASTER_PCIE_3B			12
#define MASTER_PCIE_4			13
#define MASTER_QDSS_ETR			14
#define MASTER_SDCC_2			15
#define MASTER_UFS_CARD			16
#define SLAVE_A2NOC_SNOC		17
#define SLAVE_ANOC_PCIE_GEM_NOC		18
#define SLAVE_SERVICE_A2NOC		19

/* clk_virt */
#define MASTER_IPA_CORE			0
#define MASTER_QUP_CORE_0		1
#define MASTER_QUP_CORE_1		2
#define MASTER_QUP_CORE_2		3
#define SLAVE_IPA_CORE			4
#define SLAVE_QUP_CORE_0		5
#define SLAVE_QUP_CORE_1		6
#define SLAVE_QUP_CORE_2		7

/* config_noc */
#define MASTER_GEM_NOC_CNOC		0
#define MASTER_GEM_NOC_PCIE_SNOC	1
#define SLAVE_AHB2PHY_0			2
#define SLAVE_AHB2PHY_1			3
#define SLAVE_AHB2PHY_2			4
#define SLAVE_AOSS			5
#define SLAVE_APPSS			6
#define SLAVE_CAMERA_CFG		7
#define SLAVE_CLK_CTL			8
#define SLAVE_CDSP_CFG			9
#define SLAVE_CDSP1_CFG			10
#define SLAVE_RBCPR_CX_CFG		11
#define SLAVE_RBCPR_MMCX_CFG		12
#define SLAVE_RBCPR_MX_CFG		13
#define SLAVE_CPR_NSPCX			14
#define SLAVE_CRYPTO_0_CFG		15
#define SLAVE_CX_RDPM			16
#define SLAVE_DCC_CFG			17
#define SLAVE_DISPLAY_CFG		18
#define SLAVE_DISPLAY1_CFG		19
#define SLAVE_EMAC_CFG			20
#define SLAVE_EMAC1_CFG			21
#define SLAVE_GFX3D_CFG			22
#define SLAVE_HWKM			23
#define SLAVE_IMEM_CFG			24
#define SLAVE_IPA_CFG			25
#define SLAVE_IPC_ROUTER_CFG		26
#define SLAVE_LPASS			27
#define SLAVE_MX_RDPM			28
#define SLAVE_MXC_RDPM			29
#define SLAVE_PCIE_0_CFG		30
#define SLAVE_PCIE_1_CFG		31
#define SLAVE_PCIE_2A_CFG		32
#define SLAVE_PCIE_2B_CFG		33
#define SLAVE_PCIE_3A_CFG		34
#define SLAVE_PCIE_3B_CFG		35
#define SLAVE_PCIE_4_CFG		36
#define SLAVE_PCIE_RSC_CFG		37
#define SLAVE_PDM			38
#define SLAVE_PIMEM_CFG			39
#define SLAVE_PKA_WRAPPER_CFG		40
#define SLAVE_PMU_WRAPPER_CFG		41
#define SLAVE_QDSS_CFG			42
#define SLAVE_QSPI_0			43
#define SLAVE_QUP_0			44
#define SLAVE_QUP_1			45
#define SLAVE_QUP_2			46
#define SLAVE_SDCC_2			47
#define SLAVE_SDCC_4			48
#define SLAVE_SECURITY			49
#define SLAVE_SMMUV3_CFG		50
#define SLAVE_SMSS_CFG			51
#define SLAVE_SPSS_CFG			52
#define SLAVE_TCSR			53
#define SLAVE_TLMM			54
#define SLAVE_UFS_CARD_CFG		55
#define SLAVE_UFS_MEM_CFG		56
#define SLAVE_USB3_0			57
#define SLAVE_USB3_1			58
#define SLAVE_USB3_MP			59
#define SLAVE_USB4_0			60
#define SLAVE_USB4_1			61
#define SLAVE_VENUS_CFG			62
#define SLAVE_VSENSE_CTRL_CFG		63
#define SLAVE_VSENSE_CTRL_R_CFG		64
#define SLAVE_A1NOC_CFG			65
#define SLAVE_A2NOC_CFG			66
#define SLAVE_ANOC_PCIE_BRIDGE_CFG	67
#define SLAVE_DDRSS_CFG			68
#define SLAVE_CNOC_MNOC_CFG		69
#define SLAVE_SNOC_CFG			70
#define SLAVE_SNOC_SF_BRIDGE_CFG	71
#define SLAVE_IMEM			72
#define SLAVE_PIMEM			73
#define SLAVE_SERVICE_CNOC		74
#define SLAVE_PCIE_0			75
#define SLAVE_PCIE_1			76
#define SLAVE_PCIE_2A			77
#define SLAVE_PCIE_2B			78
#define SLAVE_PCIE_3A			79
#define SLAVE_PCIE_3B			80
#define SLAVE_PCIE_4			81
#define SLAVE_QDSS_STM			82
#define SLAVE_SMSS			83
#define SLAVE_TCU			84

/* dc_noc */
#define MASTER_CNOC_DC_NOC		0
#define SLAVE_LLCC_CFG			1
#define SLAVE_GEM_NOC_CFG		2

/* gem_noc */
#define MASTER_GPU_TCU			0
#define MASTER_PCIE_TCU			1
#define MASTER_SYS_TCU			2
#define MASTER_APPSS_PROC		3
#define MASTER_COMPUTE_NOC		4
#define MASTER_COMPUTE_NOC_1		5
#define MASTER_GEM_NOC_CFG		6
#define MASTER_GFX3D			7
#define MASTER_MNOC_HF_MEM_NOC		8
#define MASTER_MNOC_SF_MEM_NOC		9
#define MASTER_ANOC_PCIE_GEM_NOC	10
#define MASTER_SNOC_GC_MEM_NOC		11
#define MASTER_SNOC_SF_MEM_NOC		12
#define SLAVE_GEM_NOC_CNOC		13
#define SLAVE_LLCC			14
#define SLAVE_GEM_NOC_PCIE_CNOC		15
#define SLAVE_SERVICE_GEM_NOC_1		16
#define SLAVE_SERVICE_GEM_NOC_2		17
#define SLAVE_SERVICE_GEM_NOC		18

/* lpass_ag_noc */
#define MASTER_CNOC_LPASS_AG_NOC	0
#define MASTER_LPASS_PROC		1
#define SLAVE_LPASS_CORE_CFG		2
#define SLAVE_LPASS_LPI_CFG		3
#define SLAVE_LPASS_MPU_CFG		4
#define SLAVE_LPASS_TOP_CFG		5
#define SLAVE_LPASS_SNOC		6
#define SLAVE_SERVICES_LPASS_AML_NOC	7
#define SLAVE_SERVICE_LPASS_AG_NOC	8

/* mc_virt */
#define MASTER_LLCC			0
#define SLAVE_EBI1			1

/*mmss_noc */
#define MASTER_CAMNOC_HF		0
#define MASTER_MDP0			1
#define MASTER_MDP1			2
#define MASTER_MDP_CORE1_0		3
#define MASTER_MDP_CORE1_1		4
#define MASTER_CNOC_MNOC_CFG		5
#define MASTER_ROTATOR			6
#define MASTER_ROTATOR_1		7
#define MASTER_VIDEO_P0			8
#define MASTER_VIDEO_P1			9
#define MASTER_VIDEO_PROC		10
#define MASTER_CAMNOC_ICP		11
#define MASTER_CAMNOC_SF		12
#define SLAVE_MNOC_HF_MEM_NOC		13
#define SLAVE_MNOC_SF_MEM_NOC		14
#define SLAVE_SERVICE_MNOC		15

/* nspa_noc */
#define MASTER_CDSP_NOC_CFG		0
#define MASTER_CDSP_PROC		1
#define SLAVE_CDSP_MEM_NOC		2
#define SLAVE_NSP_XFR			3
#define SLAVE_SERVICE_NSP_NOC		4

/* nspb_noc */
#define MASTER_CDSPB_NOC_CFG		0
#define MASTER_CDSP_PROC_B		1
#define SLAVE_CDSPB_MEM_NOC		2
#define SLAVE_NSPB_XFR			3
#define SLAVE_SERVICE_NSPB_NOC		4

/* system_noc */
#define MASTER_A1NOC_SNOC		0
#define MASTER_A2NOC_SNOC		1
#define MASTER_USB_NOC_SNOC		2
#define MASTER_LPASS_ANOC		3
#define MASTER_SNOC_CFG			4
#define MASTER_PIMEM			5
#define MASTER_GIC			6
#define SLAVE_SNOC_GEM_NOC_GC		7
#define SLAVE_SNOC_GEM_NOC_SF		8
#define SLAVE_SERVICE_SNOC		9

#endif
