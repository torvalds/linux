/*
 * file:         include/asm-blackfin/mach-bf548/dma.h
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

#define CH_SPORT0_RX		0
#define CH_SPORT0_TX		1
#define CH_SPORT1_RX		2
#define CH_SPORT1_TX		3
#define CH_SPI0			4
#define CH_SPI1			5
#define CH_UART0_RX 		6
#define CH_UART0_TX 		7
#define CH_UART1_RX 		8
#define CH_UART1_TX 		9
#define CH_ATAPI_RX		10
#define CH_ATAPI_TX		11
#define CH_EPPI0		12
#define CH_EPPI1		13
#define CH_EPPI2		14
#define CH_PIXC_IMAGE		15
#define CH_PIXC_OVERLAY		16
#define CH_PIXC_OUTPUT		17
#define CH_SPORT2_RX		18
#define CH_SPORT2_TX		19
#define CH_SPORT3_RX		20
#define CH_SPORT3_TX		21
#define CH_SDH			22
#define CH_NFC			22
#define CH_SPI2			23

#define CH_MEM_STREAM0_DEST	24
#define CH_MEM_STREAM0_SRC	25
#define CH_MEM_STREAM1_DEST	26
#define CH_MEM_STREAM1_SRC	27
#define CH_MEM_STREAM2_DEST	28
#define CH_MEM_STREAM2_SRC	29
#define CH_MEM_STREAM3_DEST	30
#define CH_MEM_STREAM3_SRC	31

#define MAX_BLACKFIN_DMA_CHANNEL 32

extern int channel2irq(unsigned int channel);
extern struct dma_register *base_addr[];
#endif
