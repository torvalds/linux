/*
 * Alchemy Semi PB1500 Referrence Board
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
#ifndef __ASM_PB1500_H
#define __ASM_PB1500_H


#define IDENT_BOARD_REG           0xAE000000
#define BOARD_STATUS_REG          0xAE000004
#define PCI_BOARD_REG             0xAE000010
#define PCMCIA_BOARD_REG          0xAE000010
  #define PC_DEASSERT_RST               0x80
  #define PC_DRV_EN                     0x10 
#define PB1500_G_CONTROL          0xAE000014
#define PB1500_RST_VDDI           0xAE00001C
#define PB1500_LEDS               0xAE000018
  
#define PB1500_HEX_LED            0xAF000004
#define PB1500_HEX_LED_BLANK      0xAF000008

/* PCMCIA PB1500 specific defines */
#define PCMCIA_MAX_SOCK 0
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK+1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP) (((VCC)<<2) | ((VPP)<<0))

#endif /* __ASM_PB1500_H */
