/*	$FreeBSD$	*/
/*	$OpenBSD: apcdmareg.h,v 1.2 2003/06/02 18:53:18 jason Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Definitions for Sun APC DMA controller.
 */

/* APC DMA registers */
#define	APC_CSR		0x0010		/* control/status */
#define	APC_CVA		0x0020		/* capture virtual address */
#define	APC_CC		0x0024		/* capture count */
#define	APC_CNVA	0x0028		/* capture next virtual address */
#define	APC_CNC		0x002c		/* capture next count */
#define	APC_PVA		0x0030		/* playback virtual address */
#define	APC_PC		0x0034		/* playback count */
#define	APC_PNVA	0x0038		/* playback next virtual address */
#define	APC_PNC		0x003c		/* playback next count */

/*
 * APC DMA Register definitions
 */
#define	APC_CSR_RESET		0x00000001	/* reset */
#define	APC_CSR_CDMA_GO		0x00000004	/* capture dma go */
#define	APC_CSR_PDMA_GO		0x00000008	/* playback dma go */
#define	APC_CSR_CODEC_RESET	0x00000020	/* codec reset */
#define	APC_CSR_CPAUSE		0x00000040	/* capture dma pause */
#define	APC_CSR_PPAUSE		0x00000080	/* playback dma pause */
#define	APC_CSR_CMIE		0x00000100	/* capture pipe empty enb */
#define	APC_CSR_CMI		0x00000200	/* capture pipe empty intr */
#define	APC_CSR_CD		0x00000400	/* capture nva dirty */
#define	APC_CSR_CM		0x00000800	/* capture data lost */
#define	APC_CSR_PMIE		0x00001000	/* pb pipe empty intr enable */
#define	APC_CSR_PD		0x00002000	/* pb nva dirty */
#define	APC_CSR_PM		0x00004000	/* pb pipe empty */
#define	APC_CSR_PMI		0x00008000	/* pb pipe empty interrupt */
#define	APC_CSR_EIE		0x00010000	/* error interrupt enable */
#define	APC_CSR_CIE		0x00020000	/* capture intr enable */
#define	APC_CSR_PIE		0x00040000	/* playback intr enable */
#define	APC_CSR_GIE		0x00080000	/* general intr enable */
#define	APC_CSR_EI		0x00100000	/* error interrupt */
#define	APC_CSR_CI		0x00200000	/* capture interrupt */
#define	APC_CSR_PI		0x00400000	/* playback interrupt */
#define	APC_CSR_GI		0x00800000	/* general interrupt */

#define	APC_CSR_PLAY			( \
		APC_CSR_EI		| \
	 	APC_CSR_GIE		| \
		APC_CSR_PIE		| \
		APC_CSR_EIE		| \
		APC_CSR_PDMA_GO		| \
		APC_CSR_PMIE		)

#define	APC_CSR_CAPTURE			( \
		APC_CSR_EI		| \
	 	APC_CSR_GIE		| \
		APC_CSR_CIE		| \
		APC_CSR_EIE		| \
		APC_CSR_CDMA_GO	)

#define	APC_CSR_PLAY_PAUSE		(~( \
		APC_CSR_PPAUSE		| \
		APC_CSR_GI		| \
		APC_CSR_PI		| \
		APC_CSR_CI		| \
		APC_CSR_EI		| \
		APC_CSR_PMI		| \
		APC_CSR_PMIE		| \
		APC_CSR_CMI		| \
		APC_CSR_CMIE		) )

#define	APC_CSR_CAPTURE_PAUSE		(~( \
		APC_CSR_PPAUSE		| \
		APC_CSR_GI		| \
		APC_CSR_PI		| \
		APC_CSR_CI		| \
		APC_CSR_EI		| \
		APC_CSR_PMI		| \
		APC_CSR_PMIE		| \
		APC_CSR_CMI		| \
		APC_CSR_CMIE		) )

#define	APC_CSR_INTR_MASK		( \
		APC_CSR_GI		| \
		APC_CSR_PI		| \
		APC_CSR_CI		| \
		APC_CSR_EI		| \
		APC_CSR_PMI		| \
		APC_CSR_CMI		)
