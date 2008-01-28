/*
 * include/asm-arm/arch-orion/irqs.h
 *
 * IRQ definitions for Orion SoC
 *
 *  Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 *  This file is licensed under the terms of the GNU General Public
 *  License version 2. This program is licensed "as is" without any
 *  warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IRQS_H__
#define __ASM_ARCH_IRQS_H__

#include "orion.h"	/* need GPIO_MAX */

/*
 * Orion Main Interrupt Controller
 */
#define IRQ_ORION_BRIDGE	0
#define IRQ_ORION_DOORBELL_H2C	1
#define IRQ_ORION_DOORBELL_C2H	2
#define IRQ_ORION_UART0		3
#define IRQ_ORION_UART1		4
#define IRQ_ORION_I2C		5
#define IRQ_ORION_GPIO_0_7	6
#define IRQ_ORION_GPIO_8_15	7
#define IRQ_ORION_GPIO_16_23	8
#define IRQ_ORION_GPIO_24_31	9
#define IRQ_ORION_PCIE0_ERR	10
#define IRQ_ORION_PCIE0_INT	11
#define IRQ_ORION_USB1_CTRL	12
#define IRQ_ORION_DEV_BUS_ERR	14
#define IRQ_ORION_PCI_ERR	15
#define IRQ_ORION_USB_BR_ERR	16
#define IRQ_ORION_USB0_CTRL	17
#define IRQ_ORION_ETH_RX	18
#define IRQ_ORION_ETH_TX	19
#define IRQ_ORION_ETH_MISC	20
#define IRQ_ORION_ETH_SUM	21
#define IRQ_ORION_ETH_ERR	22
#define IRQ_ORION_IDMA_ERR	23
#define IRQ_ORION_IDMA_0	24
#define IRQ_ORION_IDMA_1	25
#define IRQ_ORION_IDMA_2	26
#define IRQ_ORION_IDMA_3	27
#define IRQ_ORION_CESA		28
#define IRQ_ORION_SATA		29
#define IRQ_ORION_XOR0		30
#define IRQ_ORION_XOR1		31

/*
 * Orion General Purpose Pins
 */
#define IRQ_ORION_GPIO_START	32
#define NR_GPIO_IRQS		GPIO_MAX

#define NR_IRQS			(IRQ_ORION_GPIO_START + NR_GPIO_IRQS)

#endif /* __ASM_ARCH_IRQS_H__ */
