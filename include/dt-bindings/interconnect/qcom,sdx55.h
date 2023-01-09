/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Qualcomm SDX55 interconnect IDs
 *
 * Copyright (c) 2021, Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SDX55_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SDX55_H

#define MASTER_LLCC			0
#define SLAVE_EBI_CH0			1

#define MASTER_TCU_0			0
#define MASTER_SNOC_GC_MEM_NOC		1
#define MASTER_AMPSS_M0			2
#define SLAVE_LLCC			3
#define SLAVE_MEM_NOC_SNOC		4
#define SLAVE_MEM_NOC_PCIE_SNOC		5

#define MASTER_AUDIO			0
#define MASTER_BLSP_1			1
#define MASTER_QDSS_BAM			2
#define MASTER_QPIC			3
#define MASTER_SNOC_CFG			4
#define MASTER_SPMI_FETCHER		5
#define MASTER_ANOC_SNOC		6
#define MASTER_IPA			7
#define MASTER_MEM_NOC_SNOC		8
#define MASTER_MEM_NOC_PCIE_SNOC	9
#define MASTER_CRYPTO_CORE_0		10
#define MASTER_EMAC			11
#define MASTER_IPA_PCIE			12
#define MASTER_PCIE			13
#define MASTER_QDSS_ETR			14
#define MASTER_SDCC_1			15
#define MASTER_USB3			16
#define SLAVE_AOP			17
#define SLAVE_AOSS			18
#define SLAVE_APPSS			19
#define SLAVE_AUDIO			20
#define SLAVE_BLSP_1			21
#define SLAVE_CLK_CTL			22
#define SLAVE_CRYPTO_0_CFG		23
#define SLAVE_CNOC_DDRSS		24
#define SLAVE_ECC_CFG			25
#define SLAVE_EMAC_CFG			26
#define SLAVE_IMEM_CFG			27
#define SLAVE_IPA_CFG			28
#define SLAVE_CNOC_MSS			29
#define SLAVE_PCIE_PARF			30
#define SLAVE_PDM			31
#define SLAVE_PRNG			32
#define SLAVE_QDSS_CFG			33
#define SLAVE_QPIC			34
#define SLAVE_SDCC_1			35
#define SLAVE_SNOC_CFG			36
#define SLAVE_SPMI_FETCHER		37
#define SLAVE_SPMI_VGI_COEX		38
#define SLAVE_TCSR			39
#define SLAVE_TLMM			40
#define SLAVE_USB3			41
#define SLAVE_USB3_PHY_CFG		42
#define SLAVE_ANOC_SNOC			43
#define SLAVE_SNOC_MEM_NOC_GC		44
#define SLAVE_OCIMEM			45
#define SLAVE_SERVICE_SNOC		46
#define SLAVE_PCIE_0			47
#define SLAVE_QDSS_STM			48
#define SLAVE_TCU			49


#endif
