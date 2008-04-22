/*
 * AMD Alchemy Semi PB1550 Referrence Board
 * Board Registers defines.
 *
 * Copyright 2004 Embedded Edge LLC.
 * Copyright 2005 Ralf Baechle (ralf@linux-mips.org)
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
#ifndef __ASM_PB1550_H
#define __ASM_PB1550_H

#include <linux/types.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#define DBDMA_AC97_TX_CHAN DSCR_CMD0_PSC1_TX
#define DBDMA_AC97_RX_CHAN DSCR_CMD0_PSC1_RX
#define DBDMA_I2S_TX_CHAN DSCR_CMD0_PSC3_TX
#define DBDMA_I2S_RX_CHAN DSCR_CMD0_PSC3_RX

#define SPI_PSC_BASE        PSC0_BASE_ADDR
#define AC97_PSC_BASE       PSC1_BASE_ADDR
#define SMBUS_PSC_BASE      PSC2_BASE_ADDR
#define I2S_PSC_BASE        PSC3_BASE_ADDR

#define BCSR_PHYS_ADDR 0xAF000000

typedef volatile struct
{
	/*00*/	u16 whoami;
		u16 reserved0;
	/*04*/	u16 status;
		u16 reserved1;
	/*08*/	u16 switches;
		u16 reserved2;
	/*0C*/	u16 resets;
		u16 reserved3;
	/*10*/	u16 pcmcia;
		u16 reserved4;
	/*14*/	u16 pci;
		u16 reserved5;
	/*18*/	u16 leds;
		u16 reserved6;
	/*1C*/	u16 system;
		u16 reserved7;

} BCSR;

static BCSR * const bcsr = (BCSR *)BCSR_PHYS_ADDR;

/*
 * Register bit definitions for the BCSRs
 */
#define BCSR_WHOAMI_DCID	0x000F
#define BCSR_WHOAMI_CPLD	0x00F0
#define BCSR_WHOAMI_BOARD	0x0F00

#define BCSR_STATUS_PCMCIA0VS	0x0003
#define BCSR_STATUS_PCMCIA1VS	0x000C
#define BCSR_STATUS_PCMCIA0FI	0x0010
#define BCSR_STATUS_PCMCIA1FI	0x0020
#define BCSR_STATUS_SWAPBOOT	0x0040
#define BCSR_STATUS_SRAMWIDTH	0x0080
#define BCSR_STATUS_FLASHBUSY	0x0100
#define BCSR_STATUS_ROMBUSY	0x0200
#define BCSR_STATUS_USBOTGID	0x0800
#define BCSR_STATUS_U0RXD	0x1000
#define BCSR_STATUS_U1RXD	0x2000
#define BCSR_STATUS_U3RXD	0x8000

#define BCSR_SWITCHES_OCTAL	0x00FF
#define BCSR_SWITCHES_DIP_1	0x0080
#define BCSR_SWITCHES_DIP_2	0x0040
#define BCSR_SWITCHES_DIP_3	0x0020
#define BCSR_SWITCHES_DIP_4	0x0010
#define BCSR_SWITCHES_DIP_5	0x0008
#define BCSR_SWITCHES_DIP_6	0x0004
#define BCSR_SWITCHES_DIP_7	0x0002
#define BCSR_SWITCHES_DIP_8	0x0001
#define BCSR_SWITCHES_ROTARY	0x0F00

#define BCSR_RESETS_PHY0	0x0001
#define BCSR_RESETS_PHY1	0x0002
#define BCSR_RESETS_DC		0x0004
#define BCSR_RESETS_WSC		0x2000
#define BCSR_RESETS_SPISEL	0x4000
#define BCSR_RESETS_DMAREQ	0x8000

#define BCSR_PCMCIA_PC0VPP	0x0003
#define BCSR_PCMCIA_PC0VCC	0x000C
#define BCSR_PCMCIA_PC0DRVEN	0x0010
#define BCSR_PCMCIA_PC0RST	0x0080
#define BCSR_PCMCIA_PC1VPP	0x0300
#define BCSR_PCMCIA_PC1VCC	0x0C00
#define BCSR_PCMCIA_PC1DRVEN	0x1000
#define BCSR_PCMCIA_PC1RST	0x8000

#define BCSR_PCI_M66EN		0x0001
#define BCSR_PCI_M33		0x0100
#define BCSR_PCI_EXTERNARB	0x0200
#define BCSR_PCI_GPIO200RST	0x0400
#define BCSR_PCI_CLKOUT		0x0800
#define BCSR_PCI_CFGHOST	0x1000

#define BCSR_LEDS_DECIMALS	0x00FF
#define BCSR_LEDS_LED0		0x0100
#define BCSR_LEDS_LED1		0x0200
#define BCSR_LEDS_LED2		0x0400
#define BCSR_LEDS_LED3		0x0800

#define BCSR_SYSTEM_VDDI	0x001F
#define BCSR_SYSTEM_POWEROFF	0x4000
#define BCSR_SYSTEM_RESET	0x8000

#define PCMCIA_MAX_SOCK 1
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK+1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP, SLOT)\
	((((VCC)<<2) | ((VPP)<<0)) << ((SLOT)*8))

#if defined(CONFIG_MTD_PB1550_BOOT) && defined(CONFIG_MTD_PB1550_USER)
#define PB1550_BOTH_BANKS
#elif defined(CONFIG_MTD_PB1550_BOOT) && !defined(CONFIG_MTD_PB1550_USER)
#define PB1550_BOOT_ONLY
#elif !defined(CONFIG_MTD_PB1550_BOOT) && defined(CONFIG_MTD_PB1550_USER)
#define PB1550_USER_ONLY
#endif

/* Timing values as described in databook, * ns value stripped of
 * lower 2 bits.
 * These defines are here rather than an SOC1550 generic file because
 * the parts chosen on another board may be different and may require
 * different timings.
 */
#define NAND_T_H			(18 >> 2)
#define NAND_T_PUL			(30 >> 2)
#define NAND_T_SU			(30 >> 2)
#define NAND_T_WH			(30 >> 2)

/* Bitfield shift amounts */
#define NAND_T_H_SHIFT		0
#define NAND_T_PUL_SHIFT	4
#define NAND_T_SU_SHIFT		8
#define NAND_T_WH_SHIFT		12

#define NAND_TIMING	((NAND_T_H   & 0xF)	<< NAND_T_H_SHIFT)   | \
			((NAND_T_PUL & 0xF)	<< NAND_T_PUL_SHIFT) | \
			((NAND_T_SU  & 0xF)	<< NAND_T_SU_SHIFT)  | \
			((NAND_T_WH  & 0xF)	<< NAND_T_WH_SHIFT)

#define NAND_CS 1

/* should be done by yamon */
#define NAND_STCFG  0x00400005 /* 8-bit NAND */
#define NAND_STTIME 0x00007774 /* valid for 396MHz SD=2 only */
#define NAND_STADDR 0x12000FFF /* physical address 0x20000000 */

#endif /* __ASM_PB1550_H */
