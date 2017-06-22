/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_IMX7_IOMUXC_GPR_H
#define __LINUX_IMX7_IOMUXC_GPR_H

#define IOMUXC_GPR0	0x00
#define IOMUXC_GPR1	0x04
#define IOMUXC_GPR2	0x08
#define IOMUXC_GPR3	0x0c
#define IOMUXC_GPR4	0x10
#define IOMUXC_GPR5	0x14
#define IOMUXC_GPR6	0x18
#define IOMUXC_GPR7	0x1c
#define IOMUXC_GPR8	0x20
#define IOMUXC_GPR9	0x24
#define IOMUXC_GPR10	0x28
#define IOMUXC_GPR11	0x2c
#define IOMUXC_GPR12	0x30
#define IOMUXC_GPR13	0x34
#define IOMUXC_GPR14	0x38
#define IOMUXC_GPR15	0x3c
#define IOMUXC_GPR16	0x40
#define IOMUXC_GPR17	0x44
#define IOMUXC_GPR18	0x48
#define IOMUXC_GPR19	0x4c
#define IOMUXC_GPR20	0x50
#define IOMUXC_GPR21	0x54
#define IOMUXC_GPR22	0x58

/* For imx7d iomux gpr register field define */
#define IMX7D_GPR1_IRQ_MASK			(0x1 << 12)
#define IMX7D_GPR1_ENET1_TX_CLK_SEL_MASK	(0x1 << 13)
#define IMX7D_GPR1_ENET2_TX_CLK_SEL_MASK	(0x1 << 14)
#define IMX7D_GPR1_ENET_TX_CLK_SEL_MASK		(0x3 << 13)
#define IMX7D_GPR1_ENET1_CLK_DIR_MASK		(0x1 << 17)
#define IMX7D_GPR1_ENET2_CLK_DIR_MASK		(0x1 << 18)
#define IMX7D_GPR1_ENET_CLK_DIR_MASK		(0x3 << 17)

#define IMX7D_GPR5_CSI_MUX_CONTROL_MIPI		(0x1 << 4)

#define IMX7D_GPR12_PCIE_PHY_REFCLK_SEL		BIT(5)

#define IMX7D_GPR22_PCIE_PHY_PLL_LOCKED		BIT(31)

#endif /* __LINUX_IMX7_IOMUXC_GPR_H */
