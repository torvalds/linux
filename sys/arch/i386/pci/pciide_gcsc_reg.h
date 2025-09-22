/*	$OpenBSD: pciide_gcsc_reg.h,v 1.2 2013/11/26 20:33:13 deraadt Exp $	*/
/*	$NetBSD: gcscide.c,v 1.6 2007/10/06 07:21:03 xtraeme Exp $	*/

/*-
 * Copyright (c) 2007 Juan Romero Pardines.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* 
 * 6.4 - ATA-5 Controller Register Definitions.
 */
#define GCSC_MSR_ATAC_BASE 		0x51300000
#define GCSC_ATAC_GLD_MSR_CAP 		(GCSC_MSR_ATAC_BASE + 0)
#define GCSC_ATAC_GLD_MSR_CONFIG 	(GCSC_MSR_ATAC_BASE + 0x01)
#define GCSC_ATAC_GLD_MSR_SMI 		(GCSC_MSR_ATAC_BASE + 0x02)
#define GCSC_ATAC_GLD_MSR_ERROR 	(GCSC_MSR_ATAC_BASE + 0x03)
#define GCSC_ATAC_GLD_MSR_PM 		(GCSC_MSR_ATAC_BASE + 0x04)
#define GCSC_ATAC_GLD_MSR_DIAG 		(GCSC_MSR_ATAC_BASE + 0x05)
#define GCSC_ATAC_IO_BAR 		(GCSC_MSR_ATAC_BASE + 0x08)
#define GCSC_ATAC_RESET 		(GCSC_MSR_ATAC_BASE + 0x10)
#define GCSC_ATAC_CH0D0_PIO 		(GCSC_MSR_ATAC_BASE + 0x20)
#define GCSC_ATAC_CH0D0_DMA 		(GCSC_MSR_ATAC_BASE + 0x21)
#define GCSC_ATAC_CH0D1_PIO 		(GCSC_MSR_ATAC_BASE + 0x22)
#define GCSC_ATAC_CH0D1_DMA 		(GCSC_MSR_ATAC_BASE + 0x23)
#define GCSC_ATAC_PCI_ABRTERR 		(GCSC_MSR_ATAC_BASE + 0x24)
#define GCSC_ATAC_BM0_CMD_PRIM 		0x00
#define GCSC_ATAC_BM0_STS_PRIM 		0x02
#define GCSC_ATAC_BM0_PRD 		0x04

/*
 * ATAC_CH0D0_DMA registers:
 *
 * PIO Format (bit 31): Format 1 allows independent control of command
 * and data per drive, while Format 0 selects the slowest speed
 * of the two drives.
 */
#define GCSC_ATAC_PIO_FORMAT		(1U << 31) /* PIO Mode Format 1 */
/*
 * DMA_SEL (bit 20): sets Ultra DMA mode (if enabled) or Multi-word
 * DMA mode (if disabled).
 */
#define GCSC_ATAC_DMA_SEL		(1 << 20)

/* PIO Format 1 settings */
static const uint32_t gcsc_pio_timings[] = {
	0xf7f4f7f4,	/* PIO Mode 0 */
	0x53f3f173,	/* PIO Mode 1 */
	0x13f18141,	/* PIO Mode 2 */
	0x51315131,	/* PIO Mode 3 */
	0x11311131	/* PIO Mode 4 */
};

static const uint32_t gcsc_mdma_timings[] = {
	0x7f0ffff3,	/* MDMA Mode 0 */
	0x7f035352,	/* MDMA Mode 1 */
	0x7f024241 	/* MDMA Mode 2 */
};

static const uint32_t gcsc_udma_timings[] = {
	0x7f7436a1,	/* Ultra DMA Mode 0 */
	0x7f733481,	/* Ultra DMA Mode 1 */
	0x7f723261,	/* Ultra DMA Mode 2 */
	0x7f713161,	/* Ultra DMA Mode 3 */
	0x7f703061	/* Ultra DMA Mode 4 */
};

