/*	$OpenBSD: mb89352reg.h,v 1.5 2021/03/11 11:16:58 jsg Exp $	*/
/*	$NetBSD: mb89352reg.h,v 1.3 2003/08/07 16:31:02 agc Exp $	*/
/*	NecBSD: mb89352reg.h,v 1.3 1998/03/14 07:04:34 kmatsuda Exp 	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, Masaru Oki and Kouichi Matsuda.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)scsireg.h	8.1 (Berkeley) 6/10/93
 */

/*-
 * Copyright (c) 1996,97,98,99 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, Masaru Oki and Kouichi Matsuda.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)scsireg.h	8.1 (Berkeley) 6/10/93
 */
/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 *  Copyright (c) 1996, 1997, 1998
 *	Kouichi Matsuda. All rights reserved.
 */

/*
 * FUJITSU MB89352A SCSI Protocol Controller Hardware Description.
 */

/* Definitions, most of them have turned out to be unnecessary, but here they
 * are anyway.
 */

#define BDID		0x00	/* Bus Device ID (R/W) */
#define SCTL		0x01	/* SPC Control register (R/W) */
#define SCMD		0x02	/* Command Register (R/W) */
#define TMOD		0x03	/* Transmit Mode Register (synch models) */
#define INTS		0x04	/* Interrupt sense (R); Interrupt Reset (W) */
#define PSNS		0x05	/* Phase Sense (R); SPC Diagnostic Control (W) */
#define SSTS		0x06	/* SPC status (R/O) */
#define SERR		0x07	/* SPC error status (R/O) */
#define PCTL		0x08	/* Phase Control (R/W) */
#define MBC		0x09	/* Modified Byte Counter (R/O) */
#define DREG		0x0a	/* Data Register (R/W) */
#define TEMP		0x0b	/* Temporary Register (R/W) */
#define TCH		0x0c	/* Transfer Counter High (R/W) */
#define TCM		0x0d	/* Transfer Counter Middle (R/W) */
#define TCL		0x0e	/* Transfer Counter Low (R/W) */
#define EXBF		0x0f	/* External Buffer (synch models) */

/* What all the bits do */

/* SCSI_BDID */
/* SCSI selection/reselection ID (both target *and* initiator) */
#define SELID7		0x80
#define SELID6		0x40
#define SELID5		0x20
#define SELID4		0x10
#define SELID3		0x08
#define SELID2		0x04
#define SELID1		0x02
#define SELID0		0x01

/* SCSI_SCTL */
#define SCTL_DISABLE	0x80
#define SCTL_CTRLRST	0x40
#define SCTL_DIAG	0x20
#define SCTL_ABRT_ENAB	0x10
#define SCTL_PARITY_ENAB 0x08
#define SCTL_SEL_ENAB	0x04
#define SCTL_RESEL_ENAB	0x02
#define SCTL_INTR_ENAB	0x01

/* SCSI_SCMD */
#define SCMD_RST	0x10
#define SCMD_ICPT_XFR	0x08
#define SCMD_PROG_XFR	0x04
#define SCMD_PAD	0x01	/* if initiator */
#define SCMD_PERR_STOP	0x01	/* if target */
	/* command codes */
#define SCMD_BUS_REL	0x00
#define SCMD_SELECT	0x20
#define SCMD_RST_ATN	0x40
#define SCMD_SET_ATN	0x60
#define SCMD_XFR	0x80
#define SCMD_XFR_PAUSE	0xa0
#define SCMD_RST_ACK	0xc0
#define SCMD_SET_ACK	0xe0

/* SCSI_TMOD */
#define TMOD_SYNC	0x80

/* SCSI_INTS */
#define INTS_SEL	0x80
#define INTS_RESEL	0x40
#define INTS_DISCON	0x20
#define INTS_CMD_DONE	0x10
#define INTS_SRV_REQ	0x08
#define INTS_TIMEOUT	0x04
#define INTS_HARD_ERR	0x02
#define INTS_RST	0x01

/* SCSI_PSNS */
#define PSNS_REQ	0x80
#define PSNS_ACK	0x40
#define PSNS_ATN	0x20
#define PSNS_SEL	0x10
#define PSNS_BSY	0x08

/* PSNS */
#define REQI		0x80
#define ACKI		0x40
#define ATNI		0x20
#define SELI		0x10
#define BSYI		0x08
#define MSGI		0x04
#define CDI		0x02
#define IOI		0x01

/* Important! The 3 most significant bits of this register, in initiator mode,
 * represents the "expected" SCSI bus phase and can be used to trigger phase
 * mismatch and phase change interrupts.  But more important:  If there is a
 * phase mismatch the chip will not transfer any data!  This is actually a nice
 * feature as it gives us a bit more control over what is happening when we are
 * bursting data (in) through the FIFOs and the phase suddenly changes from
 * DATA IN to STATUS or MESSAGE IN.  The transfer will stop and wait for the
 * proper phase to be set in this register instead of dumping the bits into the
 * FIFOs.
 */
#if 0
#define REQO		0x80
#define ACKO		0x40
#define ATNO		0x20
#define SELO		0x10
#define BSYO		0x08
#endif
/* PCTL */
#define MSGO		0x04
#define CDO		0x02
#define IOO		0x01

/* Information transfer phases */
#define PH_DATAOUT	(0)
#define PH_DATAIN	(IOI)
#define PH_CMD		(CDI)
#define PH_STAT		(CDI | IOI)
#define PH_MSGOUT	(MSGI | CDI)
#define PH_MSGIN	(MSGI | CDI | IOI)

#define PH_MASK		(MSGI | CDI | IOI)

#define PH_INVALID	0xff

/* SCSI_SSTS */
#define SSTS_INITIATOR	0x80
#define SSTS_TARGET	0x40
#define SSTS_BUSY	0x20
#define SSTS_XFR	0x10
#define SSTS_ACTIVE	(SSTS_INITIATOR|SSTS_XFR)
#define SSTS_RST	0x08
#define SSTS_TCZERO	0x04
#define SSTS_DREG_FULL	0x02
#define SSTS_DREG_EMPTY	0x01

/* SCSI_SERR */
#define SERR_SCSI_PAR	0x80
#define SERR_SPC_PAR	0x40
#define SERR_TC_PAR	0x08
#define SERR_PHASE_ERR	0x04
#define SERR_SHORT_XFR	0x02
#define SERR_OFFSET	0x01

/* SCSI_PCTL */
#define PCTL_BFINT_ENAB	0x80
