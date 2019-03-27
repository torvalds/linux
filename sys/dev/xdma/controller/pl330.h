/*-
 * Copyright (c) 2017-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_XDMA_CONTROLLER_PL330_H_
#define _DEV_XDMA_CONTROLLER_PL330_H_

/* pl330 registers */
#define	DSR		0x000 /* DMA Manager Status */
#define	DPC		0x004 /* DMA Program Counter */
#define	INTEN		0x020 /* Interrupt Enable */
#define	INT_EVENT_RIS	0x024 /* Event-Interrupt Raw Status */
#define	INTMIS		0x028 /* Interrupt Status */
#define	INTCLR		0x02C /* Interrupt Clear */
#define	FSRD		0x030 /* Fault Status DMA Manager */
#define	FSRC		0x034 /* Fault Status DMA Channel */
#define	FTRD		0x038 /* Fault Type DMA Manager */
#define	FTR(n)		(0x040 + 0x04 * (n)) /* Fault type for DMA channel n */
#define	CSR(n)		(0x100 + 0x08 * (n)) /* Channel status for DMA channel n */
#define	CPC(n)		(0x104 + 0x08 * (n)) /* Channel PC for DMA channel n */
#define	SAR(n)		(0x400 + 0x20 * (n)) /* Source address for DMA channel n */
#define	DAR(n)		(0x404 + 0x20 * (n)) /* Destination address for DMA channel n */
#define	CCR(n)		(0x408 + 0x20 * (n)) /* Channel control for DMA channel n */
#define	 CCR_DST_BURST_SIZE_S	15
#define	 CCR_DST_BURST_SIZE_1	(0 << CCR_DST_BURST_SIZE_S)
#define	 CCR_DST_BURST_SIZE_2	(1 << CCR_DST_BURST_SIZE_S)
#define	 CCR_DST_BURST_SIZE_4	(2 << CCR_DST_BURST_SIZE_S)
#define	 CCR_SRC_BURST_SIZE_S	1
#define	 CCR_SRC_BURST_SIZE_1	(0 << CCR_SRC_BURST_SIZE_S)
#define	 CCR_SRC_BURST_SIZE_2	(1 << CCR_SRC_BURST_SIZE_S)
#define	 CCR_SRC_BURST_SIZE_4	(2 << CCR_SRC_BURST_SIZE_S)
#define	 CCR_DST_INC		(1 << 14)
#define	 CCR_SRC_INC		(1 << 0)
#define	 CCR_DST_PROT_CTRL_S	22
#define	 CCR_DST_PROT_PRIV	(1 << CCR_DST_PROT_CTRL_S)
#define	LC0(n)		(0x40C + 0x20 * (n)) /* Loop counter 0 for DMA channel n */
#define	LC1(n)		(0x410 + 0x20 * (n)) /* Loop counter 1 for DMA channel n */

#define	DBGSTATUS	0xD00 /* Debug Status */
#define	DBGCMD		0xD04 /* Debug Command */
#define	DBGINST0	0xD08 /* Debug Instruction-0 */
#define	DBGINST1	0xD0C /* Debug Instruction-1 */
#define	CR0		0xE00 /* Configuration Register 0 */
#define	CR1		0xE04 /* Configuration Register 1 */
#define	CR2		0xE08 /* Configuration Register 2 */
#define	CR3		0xE0C /* Configuration Register 3 */
#define	CR4		0xE10 /* Configuration Register 4 */
#define	CRD		0xE14 /* DMA Configuration */
#define	WD		0xE80 /* Watchdog Register */

#define	R_SAR	0
#define	R_CCR	1
#define	R_DAR	2

/*
 * 0xFE0- 0xFEC  periph_id_n RO  Configuration-dependent Peripheral Identification Registers
 * 0xFF0- 0xFFC  pcell_id_n RO   Configuration-dependent Component Identification Registers
 */

/* pl330 ISA */
#define	DMAADDH		0x54
#define	DMAADNH		0x5c
#define	DMAEND		0x00
#define	DMAFLUSHP	0x35
#define	DMAGO		0xa0
#define	DMAKILL		0x01
#define	DMALD		0x04
#define	DMALDP		0x25
#define	DMALP		0x20
#define	DMALPEND	0x28
#define	DMALPEND_NF	(1 << 4) /* DMALP started the loop */
/*
 * TODO: documentation miss opcode for infinite loop
 * #define	DMALPFE		0
 */
#define	DMAMOV		0xbc
#define	DMANOP		0x18
#define	DMARMB		0x12
#define	DMASEV		0x34
#define	DMAST		0x08
#define	DMASTP		0x29
#define	DMASTZ		0x0c
#define	DMAWFE		0x36
#define	DMAWFP		0x30
#define	DMAWMB		0x13

#endif /* !_DEV_XDMA_CONTROLLER_PL330_H_ */
