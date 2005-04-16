/*
 *  linux/include/asm-arm/arch-camelot/irqs.h
 *
 *  Copyright (C) 2001 Altera Corporation
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

/* Use the Excalibur chip definitions */
#define INT_CTRL00_TYPE
#include "asm/arch/int_ctrl00.h"


#define IRQ_PLD0                     INT_MS_P0_OFST
#define IRQ_PLD1                     INT_MS_P1_OFST
#define IRQ_PLD2                     INT_MS_P2_OFST
#define IRQ_PLD3                     INT_MS_P3_OFST
#define IRQ_PLD4                     INT_MS_P4_OFST
#define IRQ_PLD5                     INT_MS_P5_OFST
#define IRQ_EXT                      INT_MS_IP_OFST
#define IRQ_UART                     INT_MS_UA_OFST
#define IRQ_TIMER0                   INT_MS_T0_OFST
#define IRQ_TIMER1                   INT_MS_T1_OFST
#define IRQ_PLL                      INT_MS_PLL_OFST
#define IRQ_EBI                      INT_MS_EBI_OFST
#define IRQ_STRIPE_BRIDGE            INT_MS_PLL_OFST
#define IRQ_AHB_BRIDGE               INT_MS_PLL_OFST
#define IRQ_COMMRX                   INT_MS_CR_OFST
#define IRQ_COMMTX                   INT_MS_CT_OFST
#define IRQ_FAST_COMM                INT_MS_FC_OFST

#define NR_IRQS                         (INT_MS_FC_OFST + 1)

