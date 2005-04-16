/*
 * Alchemy Semi PB1100 Referrence Board
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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
#ifndef __ASM_PB1100_H
#define __ASM_PB1100_H

#define PB1100_IDENT          0xAE000000
#define BOARD_STATUS_REG      0xAE000004
  #define PB1100_ROM_SEL         (1<<15)
  #define PB1100_ROM_SIZ         (1<<14)
  #define PB1100_SWAP_BOOT       (1<<13)
  #define PB1100_FLASH_WP        (1<<12)
  #define PB1100_ROM_H_STS       (1<<11)
  #define PB1100_ROM_L_STS       (1<<10)
  #define PB1100_FLASH_H_STS      (1<<9)
  #define PB1100_FLASH_L_STS      (1<<8)
  #define PB1100_SRAM_SIZ         (1<<7)
  #define PB1100_TSC_BUSY         (1<<6)
  #define PB1100_PCMCIA_VS_MASK   (3<<4)
  #define PB1100_RS232_CD         (1<<3)
  #define PB1100_RS232_CTS        (1<<2)
  #define PB1100_RS232_DSR        (1<<1)
  #define PB1100_RS232_RI         (1<<0)

#define PB1100_IRDA_RS232     0xAE00000C
  #define PB1100_IRDA_FULL       (0<<14) /* full power */
  #define PB1100_IRDA_SHUTDOWN   (1<<14)
  #define PB1100_IRDA_TT         (2<<14) /* 2/3 power */
  #define PB1100_IRDA_OT         (3<<14) /* 1/3 power */
  #define PB1100_IRDA_FIR        (1<<13)

#define PCMCIA_BOARD_REG     0xAE000010
  #define PB1100_SD_WP1_RO       (1<<15) /* read only */
  #define PB1100_SD_WP0_RO       (1<<14) /* read only */
  #define PB1100_SD_PWR1         (1<<11) /* applies power to SD1 */
  #define PB1100_SD_PWR0         (1<<10) /* applies power to SD0 */
  #define PB1100_SEL_SD_CONN1     (1<<9)
  #define PB1100_SEL_SD_CONN0     (1<<8)
  #define PC_DEASSERT_RST         (1<<7)
  #define PC_DRV_EN               (1<<4)

#define PB1100_G_CONTROL      0xAE000014 /* graphics control */

#define PB1100_RST_VDDI       0xAE00001C
  #define PB1100_SOFT_RESET      (1<<15) /* clear to reset the board */
  #define PB1100_VDDI_MASK        (0x1F)

#define PB1100_LEDS           0xAE000018

/* 11:8 is 4 discreet LEDs. Clearing a bit illuminates the LED.
 * 7:0 is the LED Display's decimal points.
 */
#define PB1100_HEX_LED        0xAE000018

/* PCMCIA PB1100 specific defines */
#define PCMCIA_MAX_SOCK 0
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK+1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP) (((VCC)<<2) | ((VPP)<<0))

#endif /* __ASM_PB1100_H */
