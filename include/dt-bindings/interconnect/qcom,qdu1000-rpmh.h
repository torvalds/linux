/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_QDU1000_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_QDU1000_H

#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define SLAVE_QUP_CORE_0			2
#define SLAVE_QUP_CORE_1			3

#define MASTER_SYS_TCU				0
#define MASTER_APPSS_PROC			1
#define MASTER_GEMNOC_ECPRI_DMA			2
#define MASTER_FEC_2_GEMNOC			3
#define MASTER_ANOC_PCIE_GEM_NOC		4
#define MASTER_SNOC_GC_MEM_NOC			5
#define MASTER_SNOC_SF_MEM_NOC			6
#define MASTER_MSS_PROC				7
#define SLAVE_GEM_NOC_CNOC			8
#define SLAVE_LLCC				9
#define SLAVE_GEMNOC_MODEM_CNOC			10
#define SLAVE_MEM_NOC_PCIE_SNOC			11

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_GIC_AHB				0
#define MASTER_QDSS_BAM				1
#define MASTER_QPIC				2
#define MASTER_QSPI_0				3
#define MASTER_QUP_0				4
#define MASTER_QUP_1				5
#define MASTER_SNOC_CFG				6
#define MASTER_ANOC_SNOC			7
#define MASTER_ANOC_GSI				8
#define MASTER_GEM_NOC_CNOC			9
#define MASTER_GEMNOC_MODEM_CNOC		10
#define MASTER_GEM_NOC_PCIE_SNOC		11
#define MASTER_CRYPTO				12
#define MASTER_ECPRI_GSI			13
#define MASTER_PIMEM				14
#define MASTER_SNOC_ECPRI_DMA			15
#define MASTER_GIC				16
#define MASTER_PCIE				17
#define MASTER_QDSS_ETR				18
#define MASTER_QDSS_ETR_1			19
#define MASTER_SDCC_1				20
#define MASTER_USB3				21
#define SLAVE_AHB2PHY_SOUTH			22
#define SLAVE_AHB2PHY_NORTH			23
#define SLAVE_AHB2PHY_EAST			24
#define SLAVE_AOSS				25
#define SLAVE_CLK_CTL				26
#define SLAVE_RBCPR_CX_CFG			27
#define SLAVE_RBCPR_MX_CFG			28
#define SLAVE_CRYPTO_0_CFG			29
#define SLAVE_ECPRI_CFG				30
#define SLAVE_IMEM_CFG				31
#define SLAVE_IPC_ROUTER_CFG			32
#define SLAVE_CNOC_MSS				33
#define SLAVE_PCIE_CFG				34
#define SLAVE_PDM				35
#define SLAVE_PIMEM_CFG				36
#define SLAVE_PRNG				37
#define SLAVE_QDSS_CFG				38
#define SLAVE_QPIC				40
#define SLAVE_QSPI_0				41
#define SLAVE_QUP_0				42
#define SLAVE_QUP_1				43
#define SLAVE_SDCC_2				44
#define SLAVE_SMBUS_CFG				45
#define SLAVE_SNOC_CFG				46
#define SLAVE_TCSR				47
#define SLAVE_TLMM				48
#define SLAVE_TME_CFG				49
#define SLAVE_TSC_CFG				50
#define SLAVE_USB3_0				51
#define SLAVE_VSENSE_CTRL_CFG			52
#define SLAVE_A1NOC_SNOC			53
#define SLAVE_ANOC_SNOC_GSI			54
#define SLAVE_DDRSS_CFG				55
#define SLAVE_ECPRI_GEMNOC			56
#define SLAVE_SNOC_GEM_NOC_GC			57
#define SLAVE_SNOC_GEM_NOC_SF			58
#define SLAVE_MODEM_OFFLINE			59
#define SLAVE_ANOC_PCIE_GEM_NOC			60
#define SLAVE_IMEM				61
#define SLAVE_PIMEM				62
#define SLAVE_SERVICE_SNOC			63
#define SLAVE_ETHERNET_SS			64
#define SLAVE_PCIE_0				65
#define SLAVE_QDSS_STM				66
#define SLAVE_TCU				67

#endif
