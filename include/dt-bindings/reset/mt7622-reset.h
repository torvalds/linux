/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_RESET_CONTROLLER_MT7622
#define _DT_BINDINGS_RESET_CONTROLLER_MT7622

/* INFRACFG resets */
#define MT7622_INFRA_EMI_REG_RST		0
#define MT7622_INFRA_DRAMC0_A0_RST		1
#define MT7622_INFRA_APCIRQ_EINT_RST		3
#define MT7622_INFRA_APXGPT_RST			4
#define MT7622_INFRA_SCPSYS_RST			5
#define MT7622_INFRA_PMIC_WRAP_RST		7
#define MT7622_INFRA_IRRX_RST			9
#define MT7622_INFRA_EMI_RST			16
#define MT7622_INFRA_WED0_RST			17
#define MT7622_INFRA_DRAMC_RST			18
#define MT7622_INFRA_CCI_INTF_RST		19
#define MT7622_INFRA_TRNG_RST			21
#define MT7622_INFRA_SYSIRQ_RST			22
#define MT7622_INFRA_WED1_RST			25

/* PERICFG Subsystem resets */
#define MT7622_PERI_UART0_SW_RST		0
#define MT7622_PERI_UART1_SW_RST		1
#define MT7622_PERI_UART2_SW_RST		2
#define MT7622_PERI_UART3_SW_RST		3
#define MT7622_PERI_UART4_SW_RST		4
#define MT7622_PERI_BTIF_SW_RST			6
#define MT7622_PERI_PWM_SW_RST			8
#define MT7622_PERI_AUXADC_SW_RST		10
#define MT7622_PERI_DMA_SW_RST			11
#define MT7622_PERI_IRTX_SW_RST			13
#define MT7622_PERI_NFI_SW_RST			14
#define MT7622_PERI_THERM_SW_RST		16
#define MT7622_PERI_MSDC0_SW_RST		19
#define MT7622_PERI_MSDC1_SW_RST		20
#define MT7622_PERI_I2C0_SW_RST			22
#define MT7622_PERI_I2C1_SW_RST			23
#define MT7622_PERI_I2C2_SW_RST			24
#define MT7622_PERI_SPI0_SW_RST			33
#define MT7622_PERI_SPI1_SW_RST			34
#define MT7622_PERI_FLASHIF_SW_RST		36

/* TOPRGU resets */
#define MT7622_TOPRGU_INFRA_RST			0
#define MT7622_TOPRGU_ETHDMA_RST		1
#define MT7622_TOPRGU_DDRPHY_RST		6
#define MT7622_TOPRGU_INFRA_AO_RST		8
#define MT7622_TOPRGU_CONN_RST			9
#define MT7622_TOPRGU_APMIXED_RST		10
#define MT7622_TOPRGU_CONN_MCU_RST		12

/* PCIe/SATA Subsystem resets */
#define MT7622_SATA_PHY_REG_RST			12
#define MT7622_SATA_PHY_SW_RST			13
#define MT7622_SATA_AXI_BUS_RST			15
#define MT7622_PCIE1_CORE_RST			19
#define MT7622_PCIE1_MMIO_RST			20
#define MT7622_PCIE1_HRST			21
#define MT7622_PCIE1_USER_RST			22
#define MT7622_PCIE1_PIPE_RST			23
#define MT7622_PCIE0_CORE_RST			27
#define MT7622_PCIE0_MMIO_RST			28
#define MT7622_PCIE0_HRST			29
#define MT7622_PCIE0_USER_RST			30
#define MT7622_PCIE0_PIPE_RST			31

/* SSUSB Subsystem resets */
#define MT7622_SSUSB_PHY_PWR_RST		3
#define MT7622_SSUSB_MAC_PWR_RST		4

/* ETHSYS Subsystem resets */
#define MT7622_ETHSYS_SYS_RST			0
#define MT7622_ETHSYS_MCM_RST			2
#define MT7622_ETHSYS_HSDMA_RST			5
#define MT7622_ETHSYS_FE_RST			6
#define MT7622_ETHSYS_GMAC_RST			23
#define MT7622_ETHSYS_EPHY_RST			24
#define MT7622_ETHSYS_CRYPTO_RST		29
#define MT7622_ETHSYS_PPE_RST			31

#endif  /* _DT_BINDINGS_RESET_CONTROLLER_MT7622 */
