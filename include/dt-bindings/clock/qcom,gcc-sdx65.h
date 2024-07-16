/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GCC_SDX65_H
#define _DT_BINDINGS_CLK_QCOM_GCC_SDX65_H

/* GCC clocks */
#define GPLL0							0
#define GPLL0_OUT_EVEN						1
#define GCC_AHB_PCIE_LINK_CLK					2
#define GCC_BLSP1_AHB_CLK					3
#define GCC_BLSP1_QUP1_I2C_APPS_CLK				4
#define GCC_BLSP1_QUP1_I2C_APPS_CLK_SRC				5
#define GCC_BLSP1_QUP1_SPI_APPS_CLK				6
#define GCC_BLSP1_QUP1_SPI_APPS_CLK_SRC				7
#define GCC_BLSP1_QUP2_I2C_APPS_CLK				8
#define GCC_BLSP1_QUP2_I2C_APPS_CLK_SRC				9
#define GCC_BLSP1_QUP2_SPI_APPS_CLK				10
#define GCC_BLSP1_QUP2_SPI_APPS_CLK_SRC				11
#define GCC_BLSP1_QUP3_I2C_APPS_CLK				12
#define GCC_BLSP1_QUP3_I2C_APPS_CLK_SRC				13
#define GCC_BLSP1_QUP3_SPI_APPS_CLK				14
#define GCC_BLSP1_QUP3_SPI_APPS_CLK_SRC				15
#define GCC_BLSP1_QUP4_I2C_APPS_CLK				16
#define GCC_BLSP1_QUP4_I2C_APPS_CLK_SRC				17
#define GCC_BLSP1_QUP4_SPI_APPS_CLK				18
#define GCC_BLSP1_QUP4_SPI_APPS_CLK_SRC				19
#define GCC_BLSP1_SLEEP_CLK					20
#define GCC_BLSP1_UART1_APPS_CLK				21
#define GCC_BLSP1_UART1_APPS_CLK_SRC				22
#define GCC_BLSP1_UART2_APPS_CLK				23
#define GCC_BLSP1_UART2_APPS_CLK_SRC				24
#define GCC_BLSP1_UART3_APPS_CLK				25
#define GCC_BLSP1_UART3_APPS_CLK_SRC				26
#define GCC_BLSP1_UART4_APPS_CLK				27
#define GCC_BLSP1_UART4_APPS_CLK_SRC				28
#define GCC_BOOT_ROM_AHB_CLK					29
#define GCC_CPUSS_AHB_CLK					30
#define GCC_CPUSS_AHB_CLK_SRC					31
#define GCC_CPUSS_AHB_POSTDIV_CLK_SRC				32
#define GCC_CPUSS_GNOC_CLK					33
#define GCC_GP1_CLK						34
#define GCC_GP1_CLK_SRC						35
#define GCC_GP2_CLK						36
#define GCC_GP2_CLK_SRC						37
#define GCC_GP3_CLK						38
#define GCC_GP3_CLK_SRC						39
#define GCC_PCIE_0_CLKREF_EN					40
#define GCC_PCIE_AUX_CLK					41
#define GCC_PCIE_AUX_CLK_SRC					42
#define GCC_PCIE_AUX_PHY_CLK_SRC				43
#define GCC_PCIE_CFG_AHB_CLK					44
#define GCC_PCIE_MSTR_AXI_CLK					45
#define GCC_PCIE_PIPE_CLK					46
#define GCC_PCIE_PIPE_CLK_SRC					47
#define GCC_PCIE_RCHNG_PHY_CLK					48
#define GCC_PCIE_RCHNG_PHY_CLK_SRC				49
#define GCC_PCIE_SLEEP_CLK					50
#define GCC_PCIE_SLV_AXI_CLK					51
#define GCC_PCIE_SLV_Q2A_AXI_CLK				52
#define GCC_PDM2_CLK						53
#define GCC_PDM2_CLK_SRC					54
#define GCC_PDM_AHB_CLK						55
#define GCC_PDM_XO4_CLK						56
#define GCC_RX1_USB2_CLKREF_EN					57
#define GCC_SDCC1_AHB_CLK					58
#define GCC_SDCC1_APPS_CLK					59
#define GCC_SDCC1_APPS_CLK_SRC					60
#define GCC_SPMI_FETCHER_AHB_CLK				61
#define GCC_SPMI_FETCHER_CLK					62
#define GCC_SPMI_FETCHER_CLK_SRC				63
#define GCC_SYS_NOC_CPUSS_AHB_CLK				64
#define GCC_USB30_MASTER_CLK					65
#define GCC_USB30_MASTER_CLK_SRC				66
#define GCC_USB30_MOCK_UTMI_CLK					67
#define GCC_USB30_MOCK_UTMI_CLK_SRC				68
#define GCC_USB30_MOCK_UTMI_POSTDIV_CLK_SRC			69
#define GCC_USB30_MSTR_AXI_CLK					70
#define GCC_USB30_SLEEP_CLK					71
#define GCC_USB30_SLV_AHB_CLK					72
#define GCC_USB3_PHY_AUX_CLK					73
#define GCC_USB3_PHY_AUX_CLK_SRC				74
#define GCC_USB3_PHY_PIPE_CLK					75
#define GCC_USB3_PHY_PIPE_CLK_SRC				76
#define GCC_USB3_PRIM_CLKREF_EN					77
#define GCC_USB_PHY_CFG_AHB2PHY_CLK				78
#define GCC_XO_DIV4_CLK						79
#define GCC_XO_PCIE_LINK_CLK					80

/* GCC resets */
#define GCC_BLSP1_QUP1_BCR					0
#define GCC_BLSP1_QUP2_BCR					1
#define GCC_BLSP1_QUP3_BCR					2
#define GCC_BLSP1_QUP4_BCR					3
#define GCC_BLSP1_UART1_BCR					4
#define GCC_BLSP1_UART2_BCR					5
#define GCC_BLSP1_UART3_BCR					6
#define GCC_BLSP1_UART4_BCR					7
#define GCC_PCIE_BCR						8
#define GCC_PCIE_LINK_DOWN_BCR					9
#define GCC_PCIE_NOCSR_COM_PHY_BCR				10
#define GCC_PCIE_PHY_BCR					11
#define GCC_PCIE_PHY_CFG_AHB_BCR				12
#define GCC_PCIE_PHY_COM_BCR					13
#define GCC_PCIE_PHY_NOCSR_COM_PHY_BCR				14
#define GCC_PDM_BCR						15
#define GCC_QUSB2PHY_BCR					16
#define GCC_SDCC1_BCR						17
#define GCC_SPMI_FETCHER_BCR					18
#define GCC_TCSR_PCIE_BCR					19
#define GCC_USB30_BCR						20
#define GCC_USB3_PHY_BCR					21
#define GCC_USB3PHY_PHY_BCR					22
#define GCC_USB_PHY_CFG_AHB2PHY_BCR				23

/* GCC power domains */
#define USB30_GDSC                                              0
#define PCIE_GDSC                                               1

#endif
