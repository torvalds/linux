/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
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

#ifndef _HDAC_REG_H_
#define _HDAC_REG_H_

/****************************************************************************
 * HDA Controller Register Set
 ****************************************************************************/
#define HDAC_GCAP	0x00	/* 2 - Global Capabilities*/
#define HDAC_VMIN	0x02	/* 1 - Minor Version */
#define HDAC_VMAJ	0x03	/* 1 - Major Version */
#define	HDAC_OUTPAY	0x04	/* 2 - Output Payload Capability */
#define HDAC_INPAY	0x06	/* 2 - Input Payload Capability */
#define HDAC_GCTL	0x08	/* 4 - Global Control */
#define HDAC_WAKEEN	0x0c	/* 2 - Wake Enable */
#define HDAC_STATESTS	0x0e	/* 2 - State Change Status */
#define HDAC_GSTS	0x10	/* 2 - Global Status */
#define HDAC_OUTSTRMPAY	0x18	/* 2 - Output Stream Payload Capability */
#define HDAC_INSTRMPAY	0x1a	/* 2 - Input Stream Payload Capability */
#define HDAC_INTCTL	0x20	/* 4 - Interrupt Control */
#define HDAC_INTSTS	0x24	/* 4 - Interrupt Status */
#define HDAC_WALCLK	0x30	/* 4 - Wall Clock Counter */
#define HDAC_SSYNC	0x38	/* 4 - Stream Synchronization */
#define HDAC_CORBLBASE	0x40	/* 4 - CORB Lower Base Address */
#define HDAC_CORBUBASE	0x44	/* 4 - CORB Upper Base Address */
#define HDAC_CORBWP	0x48	/* 2 - CORB Write Pointer */
#define HDAC_CORBRP	0x4a	/* 2 - CORB Read Pointer */
#define HDAC_CORBCTL	0x4c	/* 1 - CORB Control */
#define HDAC_CORBSTS	0x4d	/* 1 - CORB Status */
#define HDAC_CORBSIZE	0x4e	/* 1 - CORB Size */
#define HDAC_RIRBLBASE	0x50	/* 4 - RIRB Lower Base Address */
#define HDAC_RIRBUBASE	0x54	/* 4 - RIRB Upper Base Address */
#define HDAC_RIRBWP	0x58	/* 2 - RIRB Write Pointer */
#define HDAC_RINTCNT	0x5a	/* 2 - Response Interrupt Count */
#define HDAC_RIRBCTL	0x5c	/* 1 - RIRB Control */
#define HDAC_RIRBSTS	0x5d	/* 1 - RIRB Status */
#define HDAC_RIRBSIZE	0x5e	/* 1 - RIRB Size */
#define HDAC_ICOI	0x60	/* 4 - Immediate Command Output Interface */
#define HDAC_ICII	0x64	/* 4 - Immediate Command Input Interface */
#define HDAC_ICIS	0x68	/* 2 - Immediate Command Status */
#define HDAC_DPIBLBASE	0x70	/* 4 - DMA Position Buffer Lower Base */
#define HDAC_DPIBUBASE	0x74	/* 4 - DMA Position Buffer Upper Base */
#define HDAC_SDCTL0	0x80	/* 3 - Stream Descriptor Control */
#define HDAC_SDCTL1	0x81	/* 3 - Stream Descriptor Control */
#define HDAC_SDCTL2	0x82	/* 3 - Stream Descriptor Control */
#define HDAC_SDSTS	0x83	/* 1 - Stream Descriptor Status */
#define HDAC_SDLPIB	0x84	/* 4 - Link Position in Buffer */
#define HDAC_SDCBL	0x88	/* 4 - Cyclic Buffer Length */
#define HDAC_SDLVI	0x8C	/* 2 - Last Valid Index */
#define HDAC_SDFIFOS	0x90	/* 2 - FIFOS */
#define HDAC_SDFMT	0x92	/* 2 - fmt */
#define HDAC_SDBDPL	0x98	/* 4 - Buffer Descriptor Pointer Lower Base */
#define HDAC_SDBDPU	0x9C	/* 4 - Buffer Descriptor Pointer Upper Base */

#define _HDAC_ISDOFFSET(n, iss, oss)	(0x80 + ((n) * 0x20))
#define _HDAC_ISDCTL(n, iss, oss)	(0x00 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDSTS(n, iss, oss)	(0x03 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDPICB(n, iss, oss)	(0x04 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDCBL(n, iss, oss)	(0x08 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDLVI(n, iss, oss)	(0x0c + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDFIFOD(n, iss, oss)	(0x10 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDFMT(n, iss, oss)	(0x12 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDBDPL(n, iss, oss)	(0x18 + _HDAC_ISDOFFSET(n, iss, oss))
#define _HDAC_ISDBDPU(n, iss, oss)	(0x1c + _HDAC_ISDOFFSET(n, iss, oss))

#define _HDAC_OSDOFFSET(n, iss, oss)	(0x80 + ((iss) * 0x20) + ((n) * 0x20))
#define _HDAC_OSDCTL(n, iss, oss)	(0x00 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDSTS(n, iss, oss)	(0x03 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDPICB(n, iss, oss)	(0x04 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDCBL(n, iss, oss)	(0x08 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDLVI(n, iss, oss)	(0x0c + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDFIFOD(n, iss, oss)	(0x10 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDFMT(n, iss, oss)	(0x12 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDBDPL(n, iss, oss)	(0x18 + _HDAC_OSDOFFSET(n, iss, oss))
#define _HDAC_OSDBDPU(n, iss, oss)	(0x1c + _HDAC_OSDOFFSET(n, iss, oss))

#define _HDAC_BSDOFFSET(n, iss, oss)	(0x80 + ((iss) * 0x20) + ((oss) * 0x20) + ((n) * 0x20))
#define _HDAC_BSDCTL(n, iss, oss)	(0x00 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDSTS(n, iss, oss)	(0x03 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDPICB(n, iss, oss)	(0x04 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDCBL(n, iss, oss)	(0x08 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDLVI(n, iss, oss)	(0x0c + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDFIFOD(n, iss, oss)	(0x10 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDFMT(n, iss, oss)	(0x12 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDBDPL(n, iss, oss)	(0x18 + _HDAC_BSDOFFSET(n, iss, oss))
#define _HDAC_BSDBDBU(n, iss, oss)	(0x1c + _HDAC_BSDOFFSET(n, iss, oss))

/****************************************************************************
 * HDA Controller Register Fields
 ****************************************************************************/

/* GCAP - Global Capabilities */
#define HDAC_GCAP_64OK			0x0001
#define HDAC_GCAP_NSDO_MASK		0x0006
#define HDAC_GCAP_NSDO_SHIFT		1
#define HDAC_GCAP_BSS_MASK		0x00f8
#define HDAC_GCAP_BSS_SHIFT		3
#define HDAC_GCAP_ISS_MASK		0x0f00
#define HDAC_GCAP_ISS_SHIFT		8
#define HDAC_GCAP_OSS_MASK		0xf000
#define HDAC_GCAP_OSS_SHIFT		12

#define HDAC_GCAP_NSDO_1SDO		0x00
#define HDAC_GCAP_NSDO_2SDO		0x02
#define HDAC_GCAP_NSDO_4SDO		0x04

#define HDAC_GCAP_BSS(gcap)						\
	(((gcap) & HDAC_GCAP_BSS_MASK) >> HDAC_GCAP_BSS_SHIFT)
#define HDAC_GCAP_ISS(gcap)						\
	(((gcap) & HDAC_GCAP_ISS_MASK) >> HDAC_GCAP_ISS_SHIFT)
#define HDAC_GCAP_OSS(gcap)						\
	(((gcap) & HDAC_GCAP_OSS_MASK) >> HDAC_GCAP_OSS_SHIFT)
#define HDAC_GCAP_NSDO(gcap)						\
	(((gcap) & HDAC_GCAP_NSDO_MASK) >> HDAC_GCAP_NSDO_SHIFT)

/* GCTL - Global Control */
#define HDAC_GCTL_CRST			0x00000001
#define HDAC_GCTL_FCNTRL		0x00000002
#define HDAC_GCTL_UNSOL			0x00000100

/* WAKEEN - Wake Enable */
#define HDAC_WAKEEN_SDIWEN_MASK		0x7fff
#define HDAC_WAKEEN_SDIWEN_SHIFT	0

/* STATESTS - State Change Status */
#define HDAC_STATESTS_SDIWAKE_MASK	0x7fff
#define HDAC_STATESTS_SDIWAKE_SHIFT	0

#define HDAC_STATESTS_SDIWAKE(statests, n)				\
    (((((statests) & HDAC_STATESTS_SDIWAKE_MASK) >>			\
    HDAC_STATESTS_SDIWAKE_SHIFT) >> (n)) & 0x0001)

/* GSTS - Global Status */
#define HDAC_GSTS_FSTS			0x0002

/* INTCTL - Interrut Control */
#define HDAC_INTCTL_SIE_MASK		0x3fffffff
#define HDAC_INTCTL_SIE_SHIFT		0
#define HDAC_INTCTL_CIE			0x40000000
#define HDAC_INTCTL_GIE			0x80000000

/* INTSTS - Interrupt Status */
#define HDAC_INTSTS_SIS_MASK		0x3fffffff
#define HDAC_INTSTS_SIS_SHIFT		0
#define HDAC_INTSTS_CIS			0x40000000
#define HDAC_INTSTS_GIS			0x80000000

/* SSYNC - Stream Synchronization */
#define HDAC_SSYNC_SSYNC_MASK		0x3fffffff
#define HDAC_SSYNC_SSYNC_SHIFT		0

/* CORBWP - CORB Write Pointer */
#define HDAC_CORBWP_CORBWP_MASK		0x00ff
#define HDAC_CORBWP_CORBWP_SHIFT	0

/* CORBRP - CORB Read Pointer */
#define HDAC_CORBRP_CORBRP_MASK		0x00ff
#define HDAC_CORBRP_CORBRP_SHIFT	0
#define HDAC_CORBRP_CORBRPRST		0x8000

/* CORBCTL - CORB Control */
#define HDAC_CORBCTL_CMEIE		0x01
#define HDAC_CORBCTL_CORBRUN		0x02

/* CORBSTS - CORB Status */
#define HDAC_CORBSTS_CMEI		0x01

/* CORBSIZE - CORB Size */
#define HDAC_CORBSIZE_CORBSIZE_MASK	0x03
#define HDAC_CORBSIZE_CORBSIZE_SHIFT	0
#define HDAC_CORBSIZE_CORBSZCAP_MASK	0xf0
#define HDAC_CORBSIZE_CORBSZCAP_SHIFT	4

#define HDAC_CORBSIZE_CORBSIZE_2	0x00
#define HDAC_CORBSIZE_CORBSIZE_16	0x01
#define HDAC_CORBSIZE_CORBSIZE_256	0x02

#define HDAC_CORBSIZE_CORBSZCAP_2	0x10
#define HDAC_CORBSIZE_CORBSZCAP_16	0x20
#define HDAC_CORBSIZE_CORBSZCAP_256	0x40

#define HDAC_CORBSIZE_CORBSIZE(corbsize)				\
    (((corbsize) & HDAC_CORBSIZE_CORBSIZE_MASK) >> HDAC_CORBSIZE_CORBSIZE_SHIFT)

/* RIRBWP - RIRB Write Pointer */
#define HDAC_RIRBWP_RIRBWP_MASK		0x00ff
#define HDAC_RIRBWP_RIRBWP_SHIFT	0
#define HDAC_RIRBWP_RIRBWPRST		0x8000

/* RINTCTN - Response Interrupt Count */
#define HDAC_RINTCNT_MASK		0x00ff
#define HDAC_RINTCNT_SHIFT		0

/* RIRBCTL - RIRB Control */
#define HDAC_RIRBCTL_RINTCTL		0x01
#define HDAC_RIRBCTL_RIRBDMAEN		0x02
#define HDAC_RIRBCTL_RIRBOIC		0x04

/* RIRBSTS - RIRB Status */
#define HDAC_RIRBSTS_RINTFL		0x01
#define HDAC_RIRBSTS_RIRBOIS		0x04

/* RIRBSIZE - RIRB Size */
#define HDAC_RIRBSIZE_RIRBSIZE_MASK	0x03
#define HDAC_RIRBSIZE_RIRBSIZE_SHIFT	0
#define HDAC_RIRBSIZE_RIRBSZCAP_MASK	0xf0
#define HDAC_RIRBSIZE_RIRBSZCAP_SHIFT	4

#define HDAC_RIRBSIZE_RIRBSIZE_2	0x00
#define HDAC_RIRBSIZE_RIRBSIZE_16	0x01
#define HDAC_RIRBSIZE_RIRBSIZE_256	0x02

#define HDAC_RIRBSIZE_RIRBSZCAP_2	0x10
#define HDAC_RIRBSIZE_RIRBSZCAP_16	0x20
#define HDAC_RIRBSIZE_RIRBSZCAP_256	0x40

#define HDAC_RIRBSIZE_RIRBSIZE(rirbsize)				\
    (((rirbsize) & HDAC_RIRBSIZE_RIRBSIZE_MASK) >> HDAC_RIRBSIZE_RIRBSIZE_SHIFT)

/* DPLBASE - DMA Position Lower Base Address */
#define HDAC_DPLBASE_DPLBASE_MASK	0xffffff80
#define HDAC_DPLBASE_DPLBASE_SHIFT	7
#define HDAC_DPLBASE_DPLBASE_DMAPBE	0x00000001

/* SDCTL - Stream Descriptor Control */
#define HDAC_SDCTL_SRST			0x000001
#define HDAC_SDCTL_RUN			0x000002
#define HDAC_SDCTL_IOCE			0x000004
#define HDAC_SDCTL_FEIE			0x000008
#define HDAC_SDCTL_DEIE			0x000010
#define HDAC_SDCTL2_STRIPE_MASK		0x03
#define HDAC_SDCTL2_STRIPE_SHIFT	0
#define HDAC_SDCTL2_TP			0x04
#define HDAC_SDCTL2_DIR			0x08
#define HDAC_SDCTL2_STRM_MASK		0xf0
#define HDAC_SDCTL2_STRM_SHIFT		4

#define HDAC_SDSTS_DESE			(1 << 4)
#define HDAC_SDSTS_FIFOE		(1 << 3)
#define HDAC_SDSTS_BCIS			(1 << 2)

#endif
