/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GCC_KUNO_H
#define _DT_BINDINGS_CLK_QCOM_GCC_KUNO_H

/* GCC clocks */
#define GPLL0							0
#define GPLL0_OUT_EVEN						1
#define GPLL2							2
#define GPLL3							3
#define GPLL4							4
#define GPLL4_OUT_EVEN						5
#define GCC_EMAC0_AXI_CLK					7
#define GCC_EMAC0_PHY_AUX_CLK					8
#define GCC_EMAC0_PHY_AUX_CLK_SRC				9
#define GCC_EMAC0_PTP_CLK					10
#define GCC_EMAC0_PTP_CLK_SRC					11
#define GCC_EMAC0_RGMII_CLK					12
#define GCC_EMAC0_RGMII_CLK_SRC					13
#define GCC_EMAC0_SLV_AHB_CLK					14
#define GCC_EMAC_0_CLKREF_EN					15
#define GCC_GP1_CLK						16
#define GCC_GP1_CLK_SRC						17
#define GCC_GP2_CLK						18
#define GCC_GP2_CLK_SRC						19
#define GCC_GP3_CLK						20
#define GCC_GP3_CLK_SRC						21
#define GCC_PCIE_0_CLKREF_EN					22
#define GCC_PCIE_AUX_CLK					23
#define GCC_PCIE_AUX_CLK_SRC					24
#define GCC_PCIE_AUX_PHY_CLK_SRC				25
#define GCC_PCIE_CFG_AHB_CLK					26
#define GCC_PCIE_MSTR_AXI_CLK					27
#define GCC_PCIE_PIPE_CLK					28
#define GCC_PCIE_PIPE_CLK_SRC					29
#define GCC_PCIE_RCHNG_PHY_CLK					30
#define GCC_PCIE_RCHNG_PHY_CLK_SRC				31
#define GCC_PCIE_SLEEP_CLK					32
#define GCC_PCIE_SLV_AXI_CLK					33
#define GCC_PCIE_SLV_Q2A_AXI_CLK				34
#define GCC_PDM2_CLK						35
#define GCC_PDM2_CLK_SRC					36
#define GCC_PDM_AHB_CLK						37
#define GCC_PDM_XO4_CLK						38
#define GCC_QUPV3_WRAP0_CORE_2X_CLK				39
#define GCC_QUPV3_WRAP0_CORE_CLK				40
#define GCC_QUPV3_WRAP0_S0_CLK					41
#define GCC_QUPV3_WRAP0_S0_CLK_SRC				42
#define GCC_QUPV3_WRAP0_S1_CLK					43
#define GCC_QUPV3_WRAP0_S1_CLK_SRC				44
#define GCC_QUPV3_WRAP0_S2_CLK					45
#define GCC_QUPV3_WRAP0_S2_CLK_SRC				46
#define GCC_QUPV3_WRAP0_S3_CLK					47
#define GCC_QUPV3_WRAP0_S3_CLK_SRC				48
#define GCC_QUPV3_WRAP0_S4_CLK					49
#define GCC_QUPV3_WRAP0_S4_CLK_SRC				50
#define GCC_QUPV3_WRAP_0_M_AHB_CLK				51
#define GCC_QUPV3_WRAP_0_S_AHB_CLK				52
#define GCC_SDCC4_AHB_CLK					53
#define GCC_SDCC4_APPS_CLK					54
#define GCC_SDCC4_APPS_CLK_SRC					55
#define GCC_SNOC_CNOC_USB3_CLK					56
#define GCC_SYS_NOC_USB_SF_AXI_CLK				57
#define GCC_USB20_MASTER_CLK					58
#define GCC_USB20_MASTER_CLK_SRC				59
#define GCC_USB20_MOCK_UTMI_CLK					60
#define GCC_USB20_MOCK_UTMI_CLK_SRC				61
#define GCC_USB20_MOCK_UTMI_POSTDIV_CLK_SRC			62
#define GCC_USB20_SLEEP_CLK					63
#define GCC_USB2_CLKREF_EN					64
#define GCC_USB3_PRIM_CLKREF_EN					65
#define GCC_USB_PHY_CFG_AHB2PHY_CLK				66
#define GCC_BOOT_ROM_AHB_CLK					68

/* GCC power domains */
#define GCC_EMAC0_GDSC						0
#define GCC_PCIE_GDSC						1
#define GCC_USB20_GDSC						2

/* GCC resets */
#define GCC_EMAC0_BCR						0
#define GCC_PCIE_BCR						1
#define GCC_PCIE_LINK_DOWN_BCR					2
#define GCC_PCIE_NOCSR_COM_PHY_BCR				3
#define GCC_PCIE_PHY_BCR					4
#define GCC_PCIE_PHY_CFG_AHB_BCR				5
#define GCC_PCIE_PHY_COM_BCR					6
#define GCC_PCIE_PHY_NOCSR_COM_PHY_BCR				7
#define GCC_PDM_BCR						8
#define GCC_QUPV3_WRAPPER_0_BCR					9
#define GCC_QUSB2PHY_BCR					10
#define GCC_SDCC4_BCR						11
#define GCC_TCSR_PCIE_BCR					12
#define GCC_USB20_BCR						13
#define GCC_USB_PHY_CFG_AHB2PHY_BCR				14

#endif
