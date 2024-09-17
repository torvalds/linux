/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 BAIKAL ELECTRONICS, JSC
 *
 * Baikal-T1 CCU reset indices
 */
#ifndef __DT_BINDINGS_RESET_BT1_CCU_H
#define __DT_BINDINGS_RESET_BT1_CCU_H

#define CCU_AXI_MAIN_RST		0
#define CCU_AXI_DDR_RST			1
#define CCU_AXI_SATA_RST		2
#define CCU_AXI_GMAC0_RST		3
#define CCU_AXI_GMAC1_RST		4
#define CCU_AXI_XGMAC_RST		5
#define CCU_AXI_PCIE_M_RST		6
#define CCU_AXI_PCIE_S_RST		7
#define CCU_AXI_USB_RST			8
#define CCU_AXI_HWA_RST			9
#define CCU_AXI_SRAM_RST		10

#define CCU_SYS_SATA_REF_RST		0
#define CCU_SYS_APB_RST			1
#define CCU_SYS_DDR_FULL_RST		2
#define CCU_SYS_DDR_INIT_RST		3
#define CCU_SYS_PCIE_PCS_PHY_RST	4
#define CCU_SYS_PCIE_PIPE0_RST		5
#define CCU_SYS_PCIE_CORE_RST		6
#define CCU_SYS_PCIE_PWR_RST		7
#define CCU_SYS_PCIE_STICKY_RST		8
#define CCU_SYS_PCIE_NSTICKY_RST	9
#define CCU_SYS_PCIE_HOT_RST		10

#endif /* __DT_BINDINGS_RESET_BT1_CCU_H */
