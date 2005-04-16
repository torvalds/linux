/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
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
 * Register definitions for the UART part of the Philips SAA9730 chip.
 *
 */

#ifndef SAA9730_UART_H
#define SAA9730_UART_H

/* The SAA9730 UART register map, as seen via the PCI bus */

#define SAA9730_UART_REGS_ADDR	0x21800

struct uart_saa9730_regmap {
	volatile unsigned char Thr_Rbr;
	volatile unsigned char Ier;
	volatile unsigned char Iir_Fcr;
	volatile unsigned char Lcr;
	volatile unsigned char Mcr;
	volatile unsigned char Lsr;
	volatile unsigned char Msr;
	volatile unsigned char Scr;
	volatile unsigned char BaudDivLsb;
	volatile unsigned char BaudDivMsb;
	volatile unsigned char Junk0;
	volatile unsigned char Junk1;
	volatile unsigned int Config;		/* 0x2180c */
	volatile unsigned int TxStart;		/* 0x21810 */
	volatile unsigned int TxLength;		/* 0x21814 */
	volatile unsigned int TxCounter;	/* 0x21818 */
	volatile unsigned int RxStart;		/* 0x2181c */
	volatile unsigned int RxLength;		/* 0x21820 */
	volatile unsigned int RxCounter;	/* 0x21824 */
};
typedef volatile struct uart_saa9730_regmap t_uart_saa9730_regmap;

/*
 * Only a subset of the UART control bits are defined here,
 * enough to make the serial debug port work.
 */

#define SAA9730_LCR_DATA8	0x03

#define SAA9730_MCR_DTR		0x01
#define SAA9730_MCR_RTS		0x02

#define SAA9730_LSR_DR		0x01
#define SAA9730_LSR_THRE	0x20

#endif /* !(SAA9730_UART_H) */
