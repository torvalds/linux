/*****************************************************************************
*
*        BF-533/2/1 Specific Declarations
*
****************************************************************************/
/*
 * File:         include/asm-blackfin/mach-bf533/dma.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_BLACKFIN_DMA_CHANNEL 12

#define CH_PPI          0
#define CH_SPORT0_RX    1
#define CH_SPORT0_TX    2
#define CH_SPORT1_RX    3
#define CH_SPORT1_TX    4
#define CH_SPI          5
#define CH_UART_RX      6
#define CH_UART_TX      7
#define CH_MEM_STREAM0_DEST     8	 /* TX */
#define CH_MEM_STREAM0_SRC      9	 /* RX */
#define CH_MEM_STREAM1_DEST     10	 /* TX */
#define CH_MEM_STREAM1_SRC      11	 /* RX */

extern int channel2irq(unsigned int channel);
extern struct dma_register *base_addr[];

#endif
