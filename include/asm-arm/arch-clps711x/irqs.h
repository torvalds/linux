/*
 *  linux/include/asm-arm/arch-clps711x/irqs.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Interrupts from INTSR1
 */
#define IRQ_CSINT			4
#define IRQ_EINT1			5
#define IRQ_EINT2			6
#define IRQ_EINT3			7
#define IRQ_TC1OI			8
#define IRQ_TC2OI			9
#define IRQ_RTCMI			10
#define IRQ_TINT			11
#define IRQ_UTXINT1			12
#define IRQ_URXINT1			13
#define IRQ_UMSINT			14
#define IRQ_SSEOTI			15

#define INT1_IRQS			(0x0000fff0)
#define INT1_ACK_IRQS			(0x00004f10)

/*
 * Interrupts from INTSR2
 */
#define IRQ_KBDINT			(16+0)	/* bit 0 */
#define IRQ_SS2RX			(16+1)	/* bit 1 */
#define IRQ_SS2TX			(16+2)	/* bit 2 */
#define IRQ_UTXINT2			(16+12)	/* bit 12 */
#define IRQ_URXINT2			(16+13)	/* bit 13 */

#define INT2_IRQS			(0x30070000)
#define INT2_ACK_IRQS			(0x00010000)

#define NR_IRQS                         30

