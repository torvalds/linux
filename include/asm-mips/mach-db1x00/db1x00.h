/*
 * AMD Alchemy DBAu1x00 Reference Boards
 *
 * Copyright 2001, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */
#ifndef __ASM_DB1X00_H
#define __ASM_DB1X00_H

#include <asm/mach-au1x00/au1xxx_psc.h>

#ifdef CONFIG_MIPS_DB1550

#define DBDMA_AC97_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_AC97_RX_CHAN	DSCR_CMD0_PSC1_RX
#define DBDMA_I2S_TX_CHAN	DSCR_CMD0_PSC3_TX
#define DBDMA_I2S_RX_CHAN	DSCR_CMD0_PSC3_RX

#define SPI_PSC_BASE		PSC0_BASE_ADDR
#define AC97_PSC_BASE		PSC1_BASE_ADDR
#define SMBUS_PSC_BASE		PSC2_BASE_ADDR
#define I2S_PSC_BASE		PSC3_BASE_ADDR

#define BCSR_KSEG1_ADDR 	0xAF000000
#define NAND_PHYS_ADDR		0x20000000

#else
#define BCSR_KSEG1_ADDR 0xAE000000
#endif

/*
 * Overlay data structure of the DBAu1x00 board registers.
 * Registers are located at physical 0E0000xx, KSEG1 0xAE0000xx.
 */
typedef volatile struct
{
	/*00*/	unsigned short whoami;
	unsigned short reserved0;
	/*04*/	unsigned short status;
	unsigned short reserved1;
	/*08*/	unsigned short switches;
	unsigned short reserved2;
	/*0C*/	unsigned short resets;
	unsigned short reserved3;
	/*10*/	unsigned short pcmcia;
	unsigned short reserved4;
	/*14*/	unsigned short specific;
	unsigned short reserved5;
	/*18*/	unsigned short leds;
	unsigned short reserved6;
	/*1C*/	unsigned short swreset;
	unsigned short reserved7;

} BCSR;


/*
 * Register/mask bit definitions for the BCSRs
 */
#define BCSR_WHOAMI_DCID		0x000F
#define BCSR_WHOAMI_CPLD		0x00F0
#define BCSR_WHOAMI_BOARD		0x0F00

#define BCSR_STATUS_PC0VS		0x0003
#define BCSR_STATUS_PC1VS		0x000C
#define BCSR_STATUS_PC0FI		0x0010
#define BCSR_STATUS_PC1FI		0x0020
#define BCSR_STATUS_FLASHBUSY		0x0100
#define BCSR_STATUS_ROMBUSY		0x0400
#define BCSR_STATUS_SWAPBOOT		0x2000
#define BCSR_STATUS_FLASHDEN		0xC000

#define BCSR_SWITCHES_DIP		0x00FF
#define BCSR_SWITCHES_DIP_1		0x0080
#define BCSR_SWITCHES_DIP_2		0x0040
#define BCSR_SWITCHES_DIP_3		0x0020
#define BCSR_SWITCHES_DIP_4		0x0010
#define BCSR_SWITCHES_DIP_5		0x0008
#define BCSR_SWITCHES_DIP_6		0x0004
#define BCSR_SWITCHES_DIP_7		0x0002
#define BCSR_SWITCHES_DIP_8		0x0001
#define BCSR_SWITCHES_ROTARY		0x0F00

#define BCSR_RESETS_PHY0		0x0001
#define BCSR_RESETS_PHY1		0x0002
#define BCSR_RESETS_DC			0x0004
#define BCSR_RESETS_FIR_SEL		0x2000
#define BCSR_RESETS_IRDA_MODE_MASK	0xC000
#define BCSR_RESETS_IRDA_MODE_FULL	0x0000
#define BCSR_RESETS_IRDA_MODE_OFF	0x4000
#define BCSR_RESETS_IRDA_MODE_2_3	0x8000
#define BCSR_RESETS_IRDA_MODE_1_3	0xC000

#define BCSR_PCMCIA_PC0VPP		0x0003
#define BCSR_PCMCIA_PC0VCC		0x000C
#define BCSR_PCMCIA_PC0DRVEN		0x0010
#define BCSR_PCMCIA_PC0RST		0x0080
#define BCSR_PCMCIA_PC1VPP		0x0300
#define BCSR_PCMCIA_PC1VCC		0x0C00
#define BCSR_PCMCIA_PC1DRVEN		0x1000
#define BCSR_PCMCIA_PC1RST		0x8000

#define BCSR_BOARD_PCIM66EN		0x0001
#define BCSR_BOARD_SD0_PWR		0x0040
#define BCSR_BOARD_SD1_PWR		0x0080
#define BCSR_BOARD_PCIM33		0x0100
#define BCSR_BOARD_GPIO200RST		0x0400
#define BCSR_BOARD_PCICFG		0x1000
#define BCSR_BOARD_SD0_WP		0x4000
#define BCSR_BOARD_SD1_WP		0x8000

#define BCSR_LEDS_DECIMALS		0x0003
#define BCSR_LEDS_LED0			0x0100
#define BCSR_LEDS_LED1			0x0200
#define BCSR_LEDS_LED2			0x0400
#define BCSR_LEDS_LED3			0x0800

#define BCSR_SWRESET_RESET		0x0080

/* PCMCIA DBAu1x00 specific defines */
#define PCMCIA_MAX_SOCK  1
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK + 1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP, SLOT)\
	((((VCC) << 2) | ((VPP) << 0)) << ((SLOT) * 8))

/*
 * NAND defines
 *
 * Timing values as described in databook, * ns value stripped of the
 * lower 2 bits.
 * These defines are here rather than an Au1550 generic file because
 * the parts chosen on another board may be different and may require
 * different timings.
 */
#define NAND_T_H		(18 >> 2)
#define NAND_T_PUL		(30 >> 2)
#define NAND_T_SU		(30 >> 2)
#define NAND_T_WH		(30 >> 2)

/* Bitfield shift amounts */
#define NAND_T_H_SHIFT		0
#define NAND_T_PUL_SHIFT	4
#define NAND_T_SU_SHIFT		8
#define NAND_T_WH_SHIFT		12

#define NAND_TIMING	(((NAND_T_H   & 0xF) << NAND_T_H_SHIFT)   | \
			 ((NAND_T_PUL & 0xF) << NAND_T_PUL_SHIFT) | \
			 ((NAND_T_SU  & 0xF) << NAND_T_SU_SHIFT)  | \
			 ((NAND_T_WH  & 0xF) << NAND_T_WH_SHIFT))
#define NAND_CS 	1

/* Should be done by YAMON */
#define NAND_STCFG	0x00400005 /* 8-bit NAND */
#define NAND_STTIME	0x00007774 /* valid for 396 MHz SD=2 only */
#define NAND_STADDR	0x12000FFF /* physical address 0x20000000 */

#endif /* __ASM_DB1X00_H */
