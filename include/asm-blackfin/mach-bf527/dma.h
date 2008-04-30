/*
 * file:        include/asm-blackfin/mach-bf527/dma.h
 * based on:	include/asm-blackfin/mach-bf537/dma.h
 * author:	Michael Hennerich (michael.hennerich@analog.com)
 *
 * created:
 * description:
 *	system DMA map
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

#define CH_PPI 			0	/* PPI receive/transmit or NFC */
#define CH_EMAC_RX 		1	/* Ethernet MAC receive or HOSTDP */
#define CH_EMAC_HOSTDP 		1	/* Ethernet MAC receive or HOSTDP */
#define CH_EMAC_TX 		2	/* Ethernet MAC transmit or NFC */
#define CH_SPORT0_RX 		3	/* SPORT0 receive */
#define CH_SPORT0_TX 		4	/* SPORT0 transmit */
#define CH_SPORT1_RX 		5	/* SPORT1 receive */
#define CH_SPORT1_TX 		6	/* SPORT1 transmit */
#define CH_SPI 			7	/* SPI transmit/receive */
#define CH_UART0_RX 		8	/* UART0 receive */
#define CH_UART0_TX 		9	/* UART0 transmit */
#define CH_UART1_RX 		10	/* UART1 receive */
#define CH_UART1_TX 		11	/* UART1 transmit */

#define CH_MEM_STREAM0_DEST	12	/* TX */
#define CH_MEM_STREAM0_SRC  	13	/* RX */
#define CH_MEM_STREAM1_DEST	14	/* TX */
#define CH_MEM_STREAM1_SRC 	15	/* RX */

#if defined(CONFIG_BF527_NAND_D_PORTF)
#define CH_NFC			CH_PPI	/* PPI receive/transmit or NFC */
#elif defined(CONFIG_BF527_NAND_D_PORTH)
#define CH_NFC			CH_EMAC_TX /* PPI receive/transmit or NFC */
#endif

#endif
