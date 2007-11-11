/*
 * include/asm-arm/arch-orion/orion.h
 *
 * Generic definitions of Orion SoC flavors:
 *  Orion-1, Orion-NAS, Orion-VoIP, and Orion-2.
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_ORION_H__
#define __ASM_ARCH_ORION_H__

/*******************************************************************************
 * Orion Address Map
 * Use the same mapping (1:1 virtual:physical) of internal registers and
 * PCI system (PCI+PCIE) for all machines.
 * Each machine defines the rest of its mapping (e.g. device bus flashes)
 ******************************************************************************/
#define ORION_REGS_BASE		0xf1000000
#define ORION_REGS_SIZE		SZ_1M

#define ORION_PCI_SYS_MEM_BASE	0xe0000000
#define ORION_PCIE_MEM_BASE	ORION_PCI_SYS_MEM_BASE
#define ORION_PCIE_MEM_SIZE	SZ_128M
#define ORION_PCI_MEM_BASE	(ORION_PCIE_MEM_BASE + ORION_PCIE_MEM_SIZE)
#define ORION_PCI_MEM_SIZE	SZ_128M

#define ORION_PCI_SYS_IO_BASE	0xf2000000
#define ORION_PCIE_IO_BASE	ORION_PCI_SYS_IO_BASE
#define ORION_PCIE_IO_SIZE	SZ_1M
#define ORION_PCIE_IO_REMAP	(ORION_PCIE_IO_BASE - ORION_PCI_SYS_IO_BASE)
#define ORION_PCI_IO_BASE	(ORION_PCIE_IO_BASE + ORION_PCIE_IO_SIZE)
#define ORION_PCI_IO_SIZE	SZ_1M
#define ORION_PCI_IO_REMAP	(ORION_PCI_IO_BASE - ORION_PCI_SYS_IO_BASE)
/* Relevant only for Orion-NAS */
#define ORION_PCIE_WA_BASE	0xf0000000
#define ORION_PCIE_WA_SIZE	SZ_16M

/*******************************************************************************
 * Supported Devices & Revisions
 ******************************************************************************/
/* Orion-1 (88F5181) */
#define MV88F5181_DEV_ID	0x5181
#define MV88F5181_REV_B1	3
/* Orion-NAS (88F5182) */
#define MV88F5182_DEV_ID	0x5182
#define MV88F5182_REV_A2	2
/* Orion-2 (88F5281) */
#define MV88F5281_DEV_ID	0x5281
#define MV88F5281_REV_D1	5
#define MV88F5281_REV_D2	6

/*******************************************************************************
 * Orion Registers Map
 ******************************************************************************/
#define ORION_DDR_REG_BASE	(ORION_REGS_BASE | 0x00000)
#define ORION_DEV_BUS_REG_BASE	(ORION_REGS_BASE | 0x10000)
#define ORION_BRIDGE_REG_BASE	(ORION_REGS_BASE | 0x20000)
#define ORION_PCI_REG_BASE	(ORION_REGS_BASE | 0x30000)
#define ORION_PCIE_REG_BASE	(ORION_REGS_BASE | 0x40000)
#define ORION_USB0_REG_BASE	(ORION_REGS_BASE | 0x50000)
#define ORION_ETH_REG_BASE	(ORION_REGS_BASE | 0x70000)
#define ORION_SATA_REG_BASE	(ORION_REGS_BASE | 0x80000)
#define ORION_USB1_REG_BASE	(ORION_REGS_BASE | 0xa0000)

#define ORION_DDR_REG(x)	(ORION_DDR_REG_BASE | (x))
#define ORION_DEV_BUS_REG(x)	(ORION_DEV_BUS_REG_BASE | (x))
#define ORION_BRIDGE_REG(x)	(ORION_BRIDGE_REG_BASE | (x))
#define ORION_PCI_REG(x)	(ORION_PCI_REG_BASE | (x))
#define ORION_PCIE_REG(x)	(ORION_PCIE_REG_BASE | (x))
#define ORION_USB0_REG(x)	(ORION_USB0_REG_BASE | (x))
#define ORION_USB1_REG(x)	(ORION_USB1_REG_BASE | (x))
#define ORION_ETH_REG(x)	(ORION_ETH_REG_BASE | (x))
#define ORION_SATA_REG(x)	(ORION_SATA_REG_BASE | (x))

/*******************************************************************************
 * Device Bus Registers
 ******************************************************************************/
#define MPP_0_7_CTRL		ORION_DEV_BUS_REG(0x000)
#define MPP_8_15_CTRL		ORION_DEV_BUS_REG(0x004)
#define MPP_16_19_CTRL		ORION_DEV_BUS_REG(0x050)
#define MPP_DEV_CTRL		ORION_DEV_BUS_REG(0x008)
#define MPP_RESET_SAMPLE	ORION_DEV_BUS_REG(0x010)
#define GPIO_OUT		ORION_DEV_BUS_REG(0x100)
#define GPIO_IO_CONF		ORION_DEV_BUS_REG(0x104)
#define GPIO_BLINK_EN		ORION_DEV_BUS_REG(0x108)
#define GPIO_IN_POL		ORION_DEV_BUS_REG(0x10c)
#define GPIO_DATA_IN		ORION_DEV_BUS_REG(0x110)
#define GPIO_EDGE_CAUSE		ORION_DEV_BUS_REG(0x114)
#define GPIO_EDGE_MASK		ORION_DEV_BUS_REG(0x118)
#define GPIO_LEVEL_MASK		ORION_DEV_BUS_REG(0x11c)
#define DEV_BANK_0_PARAM	ORION_DEV_BUS_REG(0x45c)
#define DEV_BANK_1_PARAM	ORION_DEV_BUS_REG(0x460)
#define DEV_BANK_2_PARAM	ORION_DEV_BUS_REG(0x464)
#define DEV_BANK_BOOT_PARAM	ORION_DEV_BUS_REG(0x46c)
#define DEV_BUS_CTRL		ORION_DEV_BUS_REG(0x4c0)
#define DEV_BUS_INT_CAUSE	ORION_DEV_BUS_REG(0x4d0)
#define DEV_BUS_INT_MASK	ORION_DEV_BUS_REG(0x4d4)
#define I2C_BASE		ORION_DEV_BUS_REG(0x1000)
#define UART0_BASE		ORION_DEV_BUS_REG(0x2000)
#define UART1_BASE		ORION_DEV_BUS_REG(0x2100)
#define GPIO_MAX		32

/***************************************************************************
 * Orion CPU Bridge Registers
 **************************************************************************/
#define CPU_CONF		ORION_BRIDGE_REG(0x100)
#define CPU_CTRL		ORION_BRIDGE_REG(0x104)
#define CPU_RESET_MASK		ORION_BRIDGE_REG(0x108)
#define CPU_SOFT_RESET		ORION_BRIDGE_REG(0x10c)
#define POWER_MNG_CTRL_REG	ORION_BRIDGE_REG(0x11C)
#define BRIDGE_CAUSE		ORION_BRIDGE_REG(0x110)
#define BRIDGE_MASK		ORION_BRIDGE_REG(0x114)
#define MAIN_IRQ_CAUSE		ORION_BRIDGE_REG(0x200)
#define MAIN_IRQ_MASK		ORION_BRIDGE_REG(0x204)
#define TIMER_CTRL		ORION_BRIDGE_REG(0x300)
#define TIMER_VAL(x)		ORION_BRIDGE_REG(0x314 + ((x) * 8))
#define TIMER_VAL_RELOAD(x)	ORION_BRIDGE_REG(0x310 + ((x) * 8))

#ifndef __ASSEMBLY__

/*******************************************************************************
 * Helpers to access Orion registers
 ******************************************************************************/
#include <asm/types.h>
#include <asm/io.h>

#define orion_read(r)		__raw_readl(r)
#define orion_write(r, val)	__raw_writel(val, r)

/*
 * These are not preempt safe. Locks, if needed, must be taken care by caller.
 */
#define orion_setbits(r, mask)	orion_write((r), orion_read(r) | (mask))
#define orion_clrbits(r, mask)	orion_write((r), orion_read(r) & ~(mask))

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARCH_ORION_H__ */
