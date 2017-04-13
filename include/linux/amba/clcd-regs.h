/*
 * David A Rusling
 *
 * Copyright (C) 2001 ARM Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef AMBA_CLCD_REGS_H
#define AMBA_CLCD_REGS_H

/*
 * CLCD Controller Internal Register addresses
 */
#define CLCD_TIM0		0x00000000
#define CLCD_TIM1 		0x00000004
#define CLCD_TIM2 		0x00000008
#define CLCD_TIM3 		0x0000000c
#define CLCD_UBAS 		0x00000010
#define CLCD_LBAS 		0x00000014

#define CLCD_PL110_IENB		0x00000018
#define CLCD_PL110_CNTL		0x0000001c
#define CLCD_PL110_STAT		0x00000020
#define CLCD_PL110_INTR 	0x00000024
#define CLCD_PL110_UCUR		0x00000028
#define CLCD_PL110_LCUR		0x0000002C

#define CLCD_PL111_CNTL		0x00000018
#define CLCD_PL111_IENB		0x0000001c
#define CLCD_PL111_RIS		0x00000020
#define CLCD_PL111_MIS		0x00000024
#define CLCD_PL111_ICR		0x00000028
#define CLCD_PL111_UCUR		0x0000002c
#define CLCD_PL111_LCUR		0x00000030

#define CLCD_PALL 		0x00000200
#define CLCD_PALETTE		0x00000200

#define TIM2_CLKSEL		(1 << 5)
#define TIM2_IVS		(1 << 11)
#define TIM2_IHS		(1 << 12)
#define TIM2_IPC		(1 << 13)
#define TIM2_IOE		(1 << 14)
#define TIM2_BCD		(1 << 26)

#define CNTL_LCDEN		(1 << 0)
#define CNTL_LCDBPP1		(0 << 1)
#define CNTL_LCDBPP2		(1 << 1)
#define CNTL_LCDBPP4		(2 << 1)
#define CNTL_LCDBPP8		(3 << 1)
#define CNTL_LCDBPP16		(4 << 1)
#define CNTL_LCDBPP16_565	(6 << 1)
#define CNTL_LCDBPP16_444	(7 << 1)
#define CNTL_LCDBPP24		(5 << 1)
#define CNTL_LCDBW		(1 << 4)
#define CNTL_LCDTFT		(1 << 5)
#define CNTL_LCDMONO8		(1 << 6)
#define CNTL_LCDDUAL		(1 << 7)
#define CNTL_BGR		(1 << 8)
#define CNTL_BEBO		(1 << 9)
#define CNTL_BEPO		(1 << 10)
#define CNTL_LCDPWR		(1 << 11)
#define CNTL_LCDVCOMP(x)	((x) << 12)
#define CNTL_LDMAFIFOTIME	(1 << 15)
#define CNTL_WATERMARK		(1 << 16)

/* ST Microelectronics variant bits */
#define CNTL_ST_1XBPP_444	0x0
#define CNTL_ST_1XBPP_5551	(1 << 17)
#define CNTL_ST_1XBPP_565	(1 << 18)
#define CNTL_ST_CDWID_12	0x0
#define CNTL_ST_CDWID_16	(1 << 19)
#define CNTL_ST_CDWID_18	(1 << 20)
#define CNTL_ST_CDWID_24	((1 << 19)|(1 << 20))
#define CNTL_ST_CEAEN		(1 << 21)
#define CNTL_ST_LCDBPP24_PACKED	(6 << 1)

#endif /* AMBA_CLCD_REGS_H */
