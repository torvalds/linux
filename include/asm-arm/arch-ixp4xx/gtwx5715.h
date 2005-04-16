/*
 * include/asm-arm/arch-ixp4xx/gtwx5715.h
 *
 * Gemtek GTWX5715 Gateway (Linksys WRV54G)
 *
 * Copyright 2004 (c) George T. Joseph
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif
#include "irqs.h"

#define GTWX5715_GPIO0	0
#define GTWX5715_GPIO1	1
#define GTWX5715_GPIO2	2
#define GTWX5715_GPIO3	3
#define GTWX5715_GPIO4	4
#define GTWX5715_GPIO5	5
#define GTWX5715_GPIO6	6
#define GTWX5715_GPIO7	7
#define GTWX5715_GPIO8	8
#define GTWX5715_GPIO9	9
#define GTWX5715_GPIO10	10
#define GTWX5715_GPIO11	11
#define GTWX5715_GPIO12	12
#define GTWX5715_GPIO13	13
#define GTWX5715_GPIO14	14

#define GTWX5715_GPIO0_IRQ			IRQ_IXP4XX_GPIO0
#define GTWX5715_GPIO1_IRQ			IRQ_IXP4XX_GPIO1
#define GTWX5715_GPIO2_IRQ			IRQ_IXP4XX_GPIO2
#define GTWX5715_GPIO3_IRQ			IRQ_IXP4XX_GPIO3
#define GTWX5715_GPIO4_IRQ			IRQ_IXP4XX_GPIO4
#define GTWX5715_GPIO5_IRQ			IRQ_IXP4XX_GPIO5
#define GTWX5715_GPIO6_IRQ			IRQ_IXP4XX_GPIO6
#define GTWX5715_GPIO7_IRQ			IRQ_IXP4XX_GPIO7
#define GTWX5715_GPIO8_IRQ			IRQ_IXP4XX_GPIO8
#define GTWX5715_GPIO9_IRQ			IRQ_IXP4XX_GPIO9
#define GTWX5715_GPIO10_IRQ		IRQ_IXP4XX_GPIO10
#define GTWX5715_GPIO11_IRQ		IRQ_IXP4XX_GPIO11
#define GTWX5715_GPIO12_IRQ		IRQ_IXP4XX_GPIO12
#define GTWX5715_GPIO13_IRQ		IRQ_IXP4XX_SW_INT1
#define GTWX5715_GPIO14_IRQ		IRQ_IXP4XX_SW_INT2


#define	GTWX5715_FLASH_BASE	IXP4XX_EXP_BUS_CS0_BASE_PHYS
#define	GTWX5715_FLASH_SIZE	(0x00800000)

/* PCI controller GPIO to IRQ pin mappings

			INTA	INTB
SLOT 0	10		11
SLOT 1	11		10

*/

#define	GTWX5715_PCI_SLOT0_DEVID	0
#define	GTWX5715_PCI_SLOT0_INTA_GPIO	GTWX5715_GPIO10
#define	GTWX5715_PCI_SLOT0_INTB_GPIO	GTWX5715_GPIO11
#define	GTWX5715_PCI_SLOT0_INTA_IRQ	GTWX5715_GPIO10_IRQ
#define	GTWX5715_PCI_SLOT0_INTB_IRQ	GTWX5715_GPIO11_IRQ

#define	GTWX5715_PCI_SLOT1_DEVID	1
#define	GTWX5715_PCI_SLOT1_INTA_GPIO	GTWX5715_GPIO11
#define	GTWX5715_PCI_SLOT1_INTB_GPIO	GTWX5715_GPIO10
#define	GTWX5715_PCI_SLOT1_INTA_IRQ	GTWX5715_GPIO11_IRQ
#define	GTWX5715_PCI_SLOT1_INTB_IRQ	GTWX5715_GPIO10_IRQ

#define GTWX5715_PCI_SLOT_COUNT			2
#define GTWX5715_PCI_INT_PIN_COUNT		2

/*
 * GPIO 5,6,7 and12 are hard wired to the Kendin KS8995M Switch
 * and operate as an SPI type interface.  The details of the interface
 * are available on Kendin/Micrel's web site.
 */

#define GTWX5715_KSSPI_SELECT	GTWX5715_GPIO5
#define GTWX5715_KSSPI_TXD		GTWX5715_GPIO6
#define GTWX5715_KSSPI_CLOCK	GTWX5715_GPIO7
#define GTWX5715_KSSPI_RXD		GTWX5715_GPIO12

/*
 * The "reset" button is wired to GPIO 3.
 * The GPIO is brought "low" when the button is pushed.
 */

#define GTWX5715_BUTTON_GPIO	GTWX5715_GPIO3
#define GTWX5715_BUTTON_IRQ	GTWX5715_GPIO3_IRQ

/*
 *  Board Label      Front Label
 *  LED1             Power
 *  LED2             Wireless-G
 *  LED3             not populated but could be
 *  LED4             Internet
 *  LED5 - LED8      Controlled by KS8995M Switch
 *  LED9             DMZ
 */

#define GTWX5715_LED1_GPIO		GTWX5715_GPIO2
#define GTWX5715_LED2_GPIO		GTWX5715_GPIO9
#define GTWX5715_LED3_GPIO		GTWX5715_GPIO8
#define GTWX5715_LED4_GPIO		GTWX5715_GPIO1
#define GTWX5715_LED9_GPIO		GTWX5715_GPIO4
