/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SDX75_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SDX75_H

#define MASTER_QPIC_CORE		0
#define MASTER_QUP_CORE_0		1
#define SLAVE_QPIC_CORE			2
#define SLAVE_QUP_CORE_0		3

#define MASTER_LLCC			0
#define SLAVE_EBI1			1

#define MASTER_CNOC_DC_NOC		0
#define SLAVE_LAGG_CFG			1
#define SLAVE_MCCC_MASTER		2
#define SLAVE_GEM_NOC_CFG		3
#define SLAVE_SNOOP_BWMON		4

#define MASTER_SYS_TCU			0
#define MASTER_APPSS_PROC		1
#define MASTER_GEM_NOC_CFG		2
#define MASTER_MSS_PROC			3
#define MASTER_ANOC_PCIE_GEM_NOC	4
#define MASTER_SNOC_SF_MEM_NOC		5
#define MASTER_GIC			6
#define MASTER_IPA_PCIE			7
#define SLAVE_GEM_NOC_CNOC		8
#define SLAVE_LLCC			9
#define SLAVE_MEM_NOC_PCIE_SNOC		10
#define SLAVE_SERVICE_GEM_NOC		11

#define MASTER_PCIE_0			0
#define MASTER_PCIE_1			1
#define MASTER_PCIE_2			2
#define SLAVE_ANOC_PCIE_GEM_NOC		3

#define MASTER_AUDIO			0
#define MASTER_GIC_AHB			1
#define MASTER_PCIE_RSCC		2
#define MASTER_QDSS_BAM			3
#define MASTER_QPIC			4
#define MASTER_QUP_0			5
#define MASTER_ANOC_SNOC		6
#define MASTER_GEM_NOC_CNOC		7
#define MASTER_GEM_NOC_PCIE_SNOC	8
#define MASTER_SNOC_CFG			9
#define MASTER_PCIE_ANOC_CFG		10
#define MASTER_CRYPTO			11
#define MASTER_IPA			12
#define MASTER_MVMSS			13
#define MASTER_EMAC_0			14
#define MASTER_EMAC_1			15
#define MASTER_QDSS_ETR			16
#define MASTER_QDSS_ETR_1		17
#define MASTER_SDCC_1			18
#define MASTER_SDCC_4			19
#define MASTER_USB3_0			20
#define SLAVE_ETH0_CFG			21
#define SLAVE_ETH1_CFG			22
#define SLAVE_AUDIO			23
#define SLAVE_CLK_CTL			24
#define SLAVE_CRYPTO_0_CFG		25
#define SLAVE_IMEM_CFG			26
#define SLAVE_IPA_CFG			27
#define SLAVE_IPC_ROUTER_CFG		28
#define SLAVE_CNOC_MSS			29
#define SLAVE_ICBDI_MVMSS_CFG		30
#define SLAVE_PCIE_0_CFG		31
#define SLAVE_PCIE_1_CFG		32
#define SLAVE_PCIE_2_CFG		33
#define SLAVE_PCIE_RSC_CFG		34
#define SLAVE_PDM			35
#define SLAVE_PRNG			36
#define SLAVE_QDSS_CFG			37
#define SLAVE_QPIC			38
#define SLAVE_QUP_0			39
#define SLAVE_SDCC_1			40
#define SLAVE_SDCC_4			41
#define SLAVE_SPMI_VGI_COEX		42
#define SLAVE_TCSR			43
#define SLAVE_TLMM			44
#define SLAVE_USB3			45
#define SLAVE_USB3_PHY_CFG		46
#define SLAVE_A1NOC_CFG			47
#define SLAVE_DDRSS_CFG			48
#define SLAVE_SNOC_GEM_NOC_SF		49
#define SLAVE_SNOC_CFG			50
#define SLAVE_PCIE_ANOC_CFG		51
#define SLAVE_IMEM			52
#define SLAVE_SERVICE_PCIE_ANOC		53
#define SLAVE_SERVICE_SNOC		54
#define SLAVE_PCIE_0			55
#define SLAVE_PCIE_1			56
#define SLAVE_PCIE_2			57
#define SLAVE_QDSS_STM			58
#define SLAVE_TCU			59

#endif
