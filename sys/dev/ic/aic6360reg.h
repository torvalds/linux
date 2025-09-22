/*	$OpenBSD: aic6360reg.h,v 1.2 2003/11/08 19:17:28 jmc Exp $	*/
/*	$NetBSD: aic6360.c,v 1.52 1996/12/10 21:27:51 thorpej Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1996 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Copyright (c) 1994 Jarle Greipsland
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@tfs.com) and
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu).  Thanks a million!
 */

/* Definitions, most of them have turned out to be unnecessary, but here they
 * are anyway.
 */

/* AIC6360 definitions */
#define SCSISEQ		0x00	/* SCSI sequence control */
#define SXFRCTL0	0x01	/* SCSI transfer control 0 */
#define SXFRCTL1	0x02	/* SCSI transfer control 1 */
#define SCSISIG		0x03	/* SCSI signal in/out */
#define SCSIRATE	0x04	/* SCSI rate control */
#define SCSIID		0x05	/* SCSI ID */
#define SELID		0x05	/* Selection/Reselection ID */
#define SCSIDAT		0x06	/* SCSI Latched Data */
#define SCSIBUS		0x07	/* SCSI Data Bus*/
#define STCNT0		0x08	/* SCSI transfer count */
#define STCNT1		0x09
#define STCNT2		0x0a
#define CLRSINT0	0x0b	/* Clear SCSI interrupts 0 */
#define SSTAT0		0x0b	/* SCSI interrupt status 0 */
#define CLRSINT1	0x0c	/* Clear SCSI interrupts 1 */
#define SSTAT1		0x0c	/* SCSI status 1 */
#define SSTAT2		0x0d	/* SCSI status 2 */
#define SCSITEST	0x0e	/* SCSI test control */
#define SSTAT3		0x0e	/* SCSI status 3 */
#define CLRSERR		0x0f	/* Clear SCSI errors */
#define SSTAT4		0x0f	/* SCSI status 4 */
#define SIMODE0		0x10	/* SCSI interrupt mode 0 */
#define SIMODE1		0x11	/* SCSI interrupt mode 1 */
#define DMACNTRL0	0x12	/* DMA control 0 */
#define DMACNTRL1	0x13	/* DMA control 1 */
#define DMASTAT		0x14	/* DMA status */
#define FIFOSTAT	0x15	/* FIFO status */
#define DMADATA		0x16	/* DMA data */
#define DMADATAL	0x16	/* DMA data low byte */
#define DMADATAH	0x17	/* DMA data high byte */
#define BRSTCNTRL	0x18	/* Burst Control */
#define DMADATALONG	0x18
#define PORTA		0x1a	/* Port A */
#define PORTB		0x1b	/* Port B */
#define REV		0x1c	/* Revision (001 for 6360) */
#define STACK		0x1d	/* Stack */
#define TEST		0x1e	/* Test register */
#define ID		0x1f	/* ID register */

#define IDSTRING "(C)1991ADAPTECAIC6360           "

/* What all the bits do */

/* SCSISEQ */
#define TEMODEO		0x80
#define ENSELO		0x40
#define ENSELI		0x20
#define ENRESELI	0x10
#define ENAUTOATNO	0x08
#define ENAUTOATNI	0x04
#define ENAUTOATNP	0x02
#define SCSIRSTO	0x01

/* SXFRCTL0 */
#define SCSIEN		0x80
#define DMAEN		0x40
#define CHEN		0x20
#define CLRSTCNT	0x10
#define SPIOEN		0x08
#define CLRCH		0x02

/* SXFRCTL1 */
#define BITBUCKET	0x80
#define SWRAPEN		0x40
#define ENSPCHK		0x20
#define STIMESEL1	0x10
#define STIMESEL0	0x08
#define STIMO_256ms	0x00
#define STIMO_128ms	0x08
#define STIMO_64ms	0x10
#define STIMO_32ms	0x18
#define ENSTIMER	0x04
#define BYTEALIGN	0x02

/* SCSISIG (in) */
#define CDI		0x80
#define IOI		0x40
#define MSGI		0x20
#define ATNI		0x10
#define SELI		0x08
#define BSYI		0x04
#define REQI		0x02
#define ACKI		0x01

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
/* SCSISIG (out) */
#define CDO		0x80
#define IOO		0x40
#define MSGO		0x20
#define ATNO		0x10
#define SELO		0x08
#define BSYO		0x04
#define REQO		0x02
#define ACKO		0x01

/* Information transfer phases */
#define PH_DATAOUT	(0)
#define PH_DATAIN	(IOI)
#define PH_CMD		(CDI)
#define PH_STAT		(CDI | IOI)
#define PH_MSGOUT	(MSGI | CDI)
#define PH_MSGIN	(MSGI | CDI | IOI)

#define PH_MASK		(MSGI | CDI | IOI)

#define	PH_INVALID	0xff

/* SCSIRATE */
#define SXFR2		0x40
#define SXFR1		0x20
#define SXFR0		0x10
#define SOFS3		0x08
#define SOFS2		0x04
#define SOFS1		0x02
#define SOFS0		0x01

/* SCSI ID */
#define OID2		0x40
#define OID1		0x20
#define OID0		0x10
#define OID_S		4	/* shift value */
#define TID2		0x04
#define TID1		0x02
#define TID0		0x01
#define SCSI_ID_MASK	0x7

/* SCSI selection/reselection ID (both target *and* initiator) */
#define SELID7		0x80
#define SELID6		0x40
#define SELID5		0x20
#define SELID4		0x10
#define SELID3		0x08
#define SELID2		0x04
#define SELID1		0x02
#define SELID0		0x01

/* CLRSINT0                      Clears what? (interrupt and/or status bit) */
#define SETSDONE	0x80
#define CLRSELDO	0x40	/* I */
#define CLRSELDI	0x20	/* I+ */
#define CLRSELINGO	0x10	/* I */
#define CLRSWRAP	0x08	/* I+S */
#define CLRSDONE	0x04	/* I+S */
#define CLRSPIORDY	0x02	/* I */
#define CLRDMADONE	0x01	/* I */

/* SSTAT0                          Howto clear */
#define TARGET		0x80
#define SELDO		0x40	/* Selfclearing */
#define SELDI		0x20	/* Selfclearing when CLRSELDI is set */
#define SELINGO		0x10	/* Selfclearing */
#define SWRAP		0x08	/* CLRSWAP */
#define SDONE		0x04	/* Not used in initiator mode */
#define SPIORDY		0x02	/* Selfclearing (op on SCSIDAT) */
#define DMADONE		0x01	/* Selfclearing (all FIFOs empty & T/C */

/* CLRSINT1                      Clears what? */
#define CLRSELTIMO	0x80	/* I+S */
#define CLRATNO		0x40
#define CLRSCSIRSTI	0x20	/* I+S */
#define CLRBUSFREE	0x08	/* I+S */
#define CLRSCSIPERR	0x04	/* I+S */
#define CLRPHASECHG	0x02	/* I+S */
#define CLRREQINIT	0x01	/* I+S */

/* SSTAT1                       How to clear?  When set?*/
#define SELTO		0x80	/* C		select out timeout */
#define ATNTARG		0x40	/* Not used in initiator mode */
#define SCSIRSTI	0x20	/* C		RST asserted */
#define PHASEMIS	0x10	/* Selfclearing */
#define BUSFREE		0x08	/* C		bus free condition */
#define SCSIPERR	0x04	/* C		parity error on inbound data */
#define PHASECHG	0x02	/* C	     phase in SCSISIG doesn't match */
#define REQINIT		0x01	/* C or ACK	asserting edge of REQ */

/* SSTAT2 */
#define SOFFSET		0x20
#define SEMPTY		0x10
#define SFULL		0x08
#define SFCNT2		0x04
#define SFCNT1		0x02
#define SFCNT0		0x01

/* SCSITEST */
#define SCTESTU		0x08
#define SCTESTD		0x04
#define STCTEST		0x01

/* SSTAT3 */
#define SCSICNT3	0x80
#define SCSICNT2	0x40
#define SCSICNT1	0x20
#define SCSICNT0	0x10
#define OFFCNT3		0x08
#define OFFCNT2		0x04
#define OFFCNT1		0x02
#define OFFCNT0		0x01

/* CLRSERR */
#define CLRSYNCERR	0x04
#define CLRFWERR	0x02
#define CLRFRERR	0x01

/* SSTAT4 */
#define SYNCERR		0x04
#define FWERR		0x02
#define FRERR		0x01

/* SIMODE0 */
#define ENSELDO		0x40
#define ENSELDI		0x20
#define ENSELINGO	0x10
#define	ENSWRAP		0x08
#define ENSDONE		0x04
#define ENSPIORDY	0x02
#define ENDMADONE	0x01

/* SIMODE1 */
#define ENSELTIMO	0x80
#define ENATNTARG	0x40
#define ENSCSIRST	0x20
#define ENPHASEMIS	0x10
#define ENBUSFREE	0x08
#define ENSCSIPERR	0x04
#define ENPHASECHG	0x02
#define ENREQINIT	0x01

/* DMACNTRL0 */
#define ENDMA		0x80
#define B8MODE		0x40
#define DMA		0x20
#define DWORDPIO	0x10
#define WRITE		0x08
#define INTEN		0x04
#define RSTFIFO		0x02
#define SWINT		0x01

/* DMACNTRL1 */
#define PWRDWN		0x80
#define ENSTK32		0x40
#define STK4		0x10
#define STK3		0x08
#define STK2		0x04
#define STK1		0x02
#define STK0		0x01

/* DMASTAT */
#define ATDONE		0x80
#define WORDRDY		0x40
#define INTSTAT		0x20
#define DFIFOFULL	0x10
#define DFIFOEMP	0x08
#define DFIFOHF		0x04
#define DWORDRDY	0x02

/* BRSTCNTRL */
#define BON3		0x80
#define BON2		0x40
#define BON1		0x20
#define BON0		0x10
#define BOFF3		0x08
#define BOFF2		0x04
#define BOFF1		0x02
#define BOFF0		0x01

/* TEST */
#define BOFFTMR		0x40
#define BONTMR		0x20
#define STCNTH		0x10
#define STCNTM		0x08
#define STCNTL		0x04
#define SCSIBLK		0x02
#define DMABLK		0x01

