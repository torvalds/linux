/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 CCU clock indices
 */
#ifndef __DT_BINDINGS_CLOCK_BT1_CCU_H
#define __DT_BINDINGS_CLOCK_BT1_CCU_H

#define CCU_CPU_PLL			0
#define CCU_SATA_PLL			1
#define CCU_DDR_PLL			2
#define CCU_PCIE_PLL			3
#define CCU_ETH_PLL			4

#define CCU_AXI_MAIN_CLK		0
#define CCU_AXI_DDR_CLK			1
#define CCU_AXI_SATA_CLK		2
#define CCU_AXI_GMAC0_CLK		3
#define CCU_AXI_GMAC1_CLK		4
#define CCU_AXI_XGMAC_CLK		5
#define CCU_AXI_PCIE_M_CLK		6
#define CCU_AXI_PCIE_S_CLK		7
#define CCU_AXI_USB_CLK			8
#define CCU_AXI_HWA_CLK			9
#define CCU_AXI_SRAM_CLK		10

#define CCU_SYS_SATA_REF_CLK		0
#define CCU_SYS_APB_CLK			1
#define CCU_SYS_GMAC0_TX_CLK		2
#define CCU_SYS_GMAC0_PTP_CLK		3
#define CCU_SYS_GMAC1_TX_CLK		4
#define CCU_SYS_GMAC1_PTP_CLK		5
#define CCU_SYS_XGMAC_REF_CLK		6
#define CCU_SYS_XGMAC_PTP_CLK		7
#define CCU_SYS_USB_CLK			8
#define CCU_SYS_PVT_CLK			9
#define CCU_SYS_HWA_CLK			10
#define CCU_SYS_UART_CLK		11
#define CCU_SYS_I2C1_CLK		12
#define CCU_SYS_I2C2_CLK		13
#define CCU_SYS_GPIO_CLK		14
#define CCU_SYS_TIMER0_CLK		15
#define CCU_SYS_TIMER1_CLK		16
#define CCU_SYS_TIMER2_CLK		17
#define CCU_SYS_WDT_CLK			18

#endif /* __DT_BINDINGS_CLOCK_BT1_CCU_H */
