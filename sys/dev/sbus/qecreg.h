/*	$OpenBSD: qecreg.h,v 1.2 2008/06/26 05:42:18 ray Exp $	*/
/*	$NetBSD: qecreg.h,v 1.2 1999/01/16 12:46:08 pk Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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

/*
 * Copyright (c) 1998 Theo de Raadt and Jason L. Wright.
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
 * 3. The name of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * QEC registers layout
 *-
struct qecregs {
	u_int32_t	qec_ctrl;	// control
	u_int32_t	qec_stat;	// status
	u_int32_t	qec_psize;	// packet size
	u_int32_t	qec_msize;	// local-mem size (64K)
	u_int32_t	qec_rsize;	// receive partition size
	u_int32_t	qec_tsize;	// transmit partition size
};
 */
#define QEC_QRI_CTRL	(0*4)
#define QEC_QRI_STAT	(1*4)
#define QEC_QRI_PSIZE	(2*4)
#define QEC_QRI_MSIZE	(3*4)
#define QEC_QRI_RSIZE	(4*4)
#define QEC_QRI_TSIZE	(5*4)

#define QEC_CTRL_MODEMASK	0xf0000000	/* QEC mode: */
#define  QEC_CTRL_MMODE		0x40000000	/*   MACE qec mode */
#define  QEC_CTRL_BMODE		0x10000000	/*   BE qec mode */
#define QEC_CTRL_EPAR		0x00000020	/* enable parity */
#define QEC_CTRL_ACNTRL		0x00000018	/* sbus arbitration control */
#define QEC_CTRL_B64		0x00000004	/* 64 byte dvma bursts */
#define QEC_CTRL_B32		0x00000002	/* 32 byte dvma bursts */
#define QEC_CTRL_B16		0x00000000	/* 16 byte dvma bursts */
#define QEC_CTRL_RESET		0x00000001	/* reset the qec */

#define QEC_STAT_TX		0x00000008	/* bigmac transmit irq */
#define QEC_STAT_RX		0x00000004	/* bigmac receive irq */
#define QEC_STAT_BM		0x00000002	/* bigmac qec irq */
#define QEC_STAT_ER		0x00000001	/* bigmac error irq */

#define QEC_PSIZE_2048		0x00		/* 2k packet size */
#define QEC_PSIZE_4096		0x01		/* 4k packet size */
#define QEC_PSIZE_6144		0x10		/* 6k packet size */
#define QEC_PSIZE_8192		0x11		/* 8k packet size */



/*
 * Transmit & receive buffer descriptor.
 */
struct qec_xd {
	volatile u_int32_t	xd_flags;	/* see below */
	volatile u_int32_t	xd_addr;	/* Buffer address (DMA) */
};
#define QEC_XD_OWN	0x80000000	/* ownership: 1=hw, 0=sw */
#define QEC_XD_SOP	0x40000000	/* start of packet marker (xmit) */
#define QEC_XD_EOP	0x20000000	/* end of packet marker (xmit) */
#define QEC_XD_UPDATE	0x10000000	/* being updated? */
#define QEC_XD_LENGTH	0x00001fff	/* packet length mask */
/* Descriptor ring size is fixed */
#define QEC_XD_RING_MAXSIZE	256		/* maximum ring size */
