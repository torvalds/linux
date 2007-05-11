/*
 * file:         include/asm-blackfin/mach-bf537/dma.h
 * based on:
 * author:
 *
 * created:
 * description:
 *	system mmr register map
 * rev:
 *
 * modified:
 *
 *
 * bugs:         enter bugs at http://blackfin.uclinux.org/
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2, or (at your option)
 * any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; see the file copying.
 * if not, write to the free software foundation,
 * 59 temple place - suite 330, boston, ma 02111-1307, usa.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_BLACKFIN_DMA_CHANNEL 16

#define CH_PPI 			    0
#define CH_EMAC_RX 		    1
#define CH_EMAC_TX 		    2
#define CH_SPORT0_RX 		3
#define CH_SPORT0_TX 		4
#define CH_SPORT1_RX 		5
#define CH_SPORT1_TX 		6
#define CH_SPI 			    7
#define CH_UART0_RX 		8
#define CH_UART0_TX 		9
#define CH_UART1_RX 		10
#define CH_UART1_TX 		11

#define CH_MEM_STREAM0_DEST	12	 /* TX */
#define CH_MEM_STREAM0_SRC  	13	 /* RX */
#define CH_MEM_STREAM1_DEST	14	 /* TX */
#define CH_MEM_STREAM1_SRC 	15	 /* RX */

#endif
