/*	$NetBSD: pcscpreg.h,v 1.2 2008/04/28 20:23:55 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Izumi Tsutsui.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* $FreeBSD$ */

#ifndef _AM53C974_H_
#define	_AM53C974_H_

/*
 * Am53c974 DMA engine registers
 */

#define	DMA_CMD		0x40 		/* Command */
#define	 DMACMD_RSVD	0xFFFFFF28	/* reserved */
#define	 DMACMD_DIR	0x00000080	/* Transfer Direction (read:1) */
#define	 DMACMD_INTE	0x00000040	/* DMA Interrupt Enable	*/
#define	 DMACMD_MDL	0x00000010	/* Map to Memory Description List */
#define	 DMACMD_DIAG	0x00000004	/* Diagnostic */
#define	 DMACMD_CMD	0x00000003	/* Command Code Bit */
#define	  DMACMD_IDLE	0x00000000	/*  Idle */
#define	  DMACMD_BLAST	0x00000001	/*  Blast */
#define	  DMACMD_ABORT	0x00000002	/*  Abort */
#define	  DMACMD_START	0x00000003	/*  Start */

#define	DMA_STC		0x44		/* Start Transfer Count */
#define	DMA_SPA		0x48		/* Start Physical Address */
#define	DMA_WBC		0x4C		/* Working Byte Counter */
#define	DMA_WAC		0x50		/* Working Address Counter */

#define	DMA_STAT	0x54		/* Status Register */
#define	 DMASTAT_RSVD	0xFFFFFF80	/* reserved */
#define	 DMASTAT_PABT	0x00000040	/* PCI master/target Abort */
#define	 DMASTAT_BCMP	0x00000020	/* BLAST Complete */
#define	 DMASTAT_SINT	0x00000010	/* SCSI Interrupt */
#define	 DMASTAT_DONE	0x00000008	/* DMA Transfer Terminated */
#define	 DMASTAT_ABT	0x00000004	/* DMA Transfer Aborted */
#define	 DMASTAT_ERR	0x00000002	/* DMA Transfer Error */
#define	 DMASTAT_PWDN	0x00000001	/* Power Down Indicator */

#define	DMA_SMDLA	0x58	/* Starting Memory Descpritor List Address */
#define	DMA_WMAC	0x5C	/* Working MDL Counter */
#define	DMA_SBAC	0x70	/* SCSI Bus and Control */

#endif /* _AM53C974_H_ */
