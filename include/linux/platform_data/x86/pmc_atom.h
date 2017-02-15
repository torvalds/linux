/*
 * Intel Atom SOC Power Management Controller Header File
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef PMC_ATOM_H
#define PMC_ATOM_H

/* ValleyView Power Control Unit PCI Device ID */
#define	PCI_DEVICE_ID_VLV_PMC	0x0F1C
/* CherryTrail Power Control Unit PCI Device ID */
#define	PCI_DEVICE_ID_CHT_PMC	0x229C

/* PMC Memory mapped IO registers */
#define	PMC_BASE_ADDR_OFFSET	0x44
#define	PMC_BASE_ADDR_MASK	0xFFFFFE00
#define	PMC_MMIO_REG_LEN	0x100
#define	PMC_REG_BIT_WIDTH	32

/* BIOS uses FUNC_DIS to disable specific function */
#define	PMC_FUNC_DIS		0x34
#define	PMC_FUNC_DIS_2		0x38

/* CHT specific bits in FUNC_DIS2 register */
#define	BIT_FD_GMM		BIT(3)
#define	BIT_FD_ISH		BIT(4)

/* S0ix wake event control */
#define	PMC_S0IX_WAKE_EN	0x3C

#define	BIT_LPC_CLOCK_RUN		BIT(4)
#define	BIT_SHARED_IRQ_GPSC		BIT(5)
#define	BIT_ORED_DEDICATED_IRQ_GPSS	BIT(18)
#define	BIT_ORED_DEDICATED_IRQ_GPSC	BIT(19)
#define	BIT_SHARED_IRQ_GPSS		BIT(20)

#define	PMC_WAKE_EN_SETTING	~(BIT_LPC_CLOCK_RUN | \
				BIT_SHARED_IRQ_GPSC | \
				BIT_ORED_DEDICATED_IRQ_GPSS | \
				BIT_ORED_DEDICATED_IRQ_GPSC | \
				BIT_SHARED_IRQ_GPSS)

/* The timers accumulate time spent in sleep state */
#define	PMC_S0IR_TMR		0x80
#define	PMC_S0I1_TMR		0x84
#define	PMC_S0I2_TMR		0x88
#define	PMC_S0I3_TMR		0x8C
#define	PMC_S0_TMR		0x90
/* Sleep state counter is in units of of 32us */
#define	PMC_TMR_SHIFT		5

/* Power status of power islands */
#define	PMC_PSS			0x98

#define PMC_PSS_BIT_GBE			BIT(0)
#define PMC_PSS_BIT_SATA		BIT(1)
#define PMC_PSS_BIT_HDA			BIT(2)
#define PMC_PSS_BIT_SEC			BIT(3)
#define PMC_PSS_BIT_PCIE		BIT(4)
#define PMC_PSS_BIT_LPSS		BIT(5)
#define PMC_PSS_BIT_LPE			BIT(6)
#define PMC_PSS_BIT_DFX			BIT(7)
#define PMC_PSS_BIT_USH_CTRL		BIT(8)
#define PMC_PSS_BIT_USH_SUS		BIT(9)
#define PMC_PSS_BIT_USH_VCCS		BIT(10)
#define PMC_PSS_BIT_USH_VCCA		BIT(11)
#define PMC_PSS_BIT_OTG_CTRL		BIT(12)
#define PMC_PSS_BIT_OTG_VCCS		BIT(13)
#define PMC_PSS_BIT_OTG_VCCA_CLK	BIT(14)
#define PMC_PSS_BIT_OTG_VCCA		BIT(15)
#define PMC_PSS_BIT_USB			BIT(16)
#define PMC_PSS_BIT_USB_SUS		BIT(17)

/* CHT specific bits in PSS register */
#define	PMC_PSS_BIT_CHT_UFS		BIT(7)
#define	PMC_PSS_BIT_CHT_UXD		BIT(11)
#define	PMC_PSS_BIT_CHT_UXD_FD		BIT(12)
#define	PMC_PSS_BIT_CHT_UX_ENG		BIT(15)
#define	PMC_PSS_BIT_CHT_USB_SUS		BIT(16)
#define	PMC_PSS_BIT_CHT_GMM		BIT(17)
#define	PMC_PSS_BIT_CHT_ISH		BIT(18)
#define	PMC_PSS_BIT_CHT_DFX_MASTER	BIT(26)
#define	PMC_PSS_BIT_CHT_DFX_CLUSTER1	BIT(27)
#define	PMC_PSS_BIT_CHT_DFX_CLUSTER2	BIT(28)
#define	PMC_PSS_BIT_CHT_DFX_CLUSTER3	BIT(29)
#define	PMC_PSS_BIT_CHT_DFX_CLUSTER4	BIT(30)
#define	PMC_PSS_BIT_CHT_DFX_CLUSTER5	BIT(31)

/* These registers reflect D3 status of functions */
#define	PMC_D3_STS_0		0xA0

#define	BIT_LPSS1_F0_DMA	BIT(0)
#define	BIT_LPSS1_F1_PWM1	BIT(1)
#define	BIT_LPSS1_F2_PWM2	BIT(2)
#define	BIT_LPSS1_F3_HSUART1	BIT(3)
#define	BIT_LPSS1_F4_HSUART2	BIT(4)
#define	BIT_LPSS1_F5_SPI	BIT(5)
#define	BIT_LPSS1_F6_XXX	BIT(6)
#define	BIT_LPSS1_F7_XXX	BIT(7)
#define	BIT_SCC_EMMC		BIT(8)
#define	BIT_SCC_SDIO		BIT(9)
#define	BIT_SCC_SDCARD		BIT(10)
#define	BIT_SCC_MIPI		BIT(11)
#define	BIT_HDA			BIT(12)
#define	BIT_LPE			BIT(13)
#define	BIT_OTG			BIT(14)
#define	BIT_USH			BIT(15)
#define	BIT_GBE			BIT(16)
#define	BIT_SATA		BIT(17)
#define	BIT_USB_EHCI		BIT(18)
#define	BIT_SEC			BIT(19)
#define	BIT_PCIE_PORT0		BIT(20)
#define	BIT_PCIE_PORT1		BIT(21)
#define	BIT_PCIE_PORT2		BIT(22)
#define	BIT_PCIE_PORT3		BIT(23)
#define	BIT_LPSS2_F0_DMA	BIT(24)
#define	BIT_LPSS2_F1_I2C1	BIT(25)
#define	BIT_LPSS2_F2_I2C2	BIT(26)
#define	BIT_LPSS2_F3_I2C3	BIT(27)
#define	BIT_LPSS2_F4_I2C4	BIT(28)
#define	BIT_LPSS2_F5_I2C5	BIT(29)
#define	BIT_LPSS2_F6_I2C6	BIT(30)
#define	BIT_LPSS2_F7_I2C7	BIT(31)

#define	PMC_D3_STS_1		0xA4
#define	BIT_SMB			BIT(0)
#define	BIT_OTG_SS_PHY		BIT(1)
#define	BIT_USH_SS_PHY		BIT(2)
#define	BIT_DFX			BIT(3)

/* CHT specific bits in PMC_D3_STS_1 register */
#define	BIT_STS_GMM		BIT(1)
#define	BIT_STS_ISH		BIT(2)

/* PMC I/O Registers */
#define	ACPI_BASE_ADDR_OFFSET	0x40
#define	ACPI_BASE_ADDR_MASK	0xFFFFFE00
#define	ACPI_MMIO_REG_LEN	0x100

#define	PM1_CNT			0x4
#define	SLEEP_TYPE_MASK		0xFFFFECFF
#define	SLEEP_TYPE_S5		0x1C00
#define	SLEEP_ENABLE		0x2000

extern int pmc_atom_read(int offset, u32 *value);
extern int pmc_atom_write(int offset, u32 value);

#endif /* PMC_ATOM_H */
