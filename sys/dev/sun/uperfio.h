/*	$OpenBSD: uperfio.h,v 1.4 2005/06/03 10:53:58 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#define	UPERF_CNT0		1
#define	UPERF_CNT1		2

#define	UPERFSRC_SYSCK		1	/* system clock count */
#define	UPERFSRC_PRALL		2	/* # of p-requests, all srcs */
#define	UPERFSRC_PRP0		3	/* # of p-requests, processor 0 */
#define	UPERFSRC_PRU2S		4	/* # of p-requests, U2S */
#define	UPERFSRC_UPA128		5	/* # cycles UPA 128bit bus busy */
#define	UPERFSRC_UPA64		6	/* # cycles UPA 64bit bus busy */
#define	UPERFSRC_PIOS		7	/* # cycles stalled during PIO */
#define	UPERFSRC_MEMRI		8	/* # memory requests issued */
#define	UPERFSRC_MCBUSY		9	/* # cycles mem ctrlr busy */
#define	UPERFSRC_PXSH		10	/* # cyc stalled for pending xact scoreboard hit */
#define	UPERFSRC_P0CWMR		11	/* # coherent wr miss req, Proc0 */
#define	UPERFSRC_P1CWMR		12	/* # coherent wr miss req, Proc1 */
#define	UPERFSRC_CIT		13	/* # coherent intervention xacts */
#define	UPERFSRC_U2SDAT		14	/* # data xacts from U2S */
#define	UPERFSRC_CRXI		15	/* # coherent read xacts issued */
#define	UPERFSRC_RDP0		16	/* read requests from Proc0 */
#define	UPERFSRC_P0CRMR		17	/* # coherent rd miss req, Proc0 */
#define	UPERFSRC_P0PIO		18	/* PIO accesses from Proc 0 */
#define	UPERFSRC_MEMRC		19	/* # memory requests completed */
#define	UPERFSRC_P1RR		20	/* Proc 1 read requests */
#define	UPERFSRC_CRMP1		21	/* Proc 1 coherent read misses */
#define	UPERFSRC_PIOP1		22	/* Proc 1 PIO accesses */
#define	UPERFSRC_CWXI		23	/* coherent write xacts issued */
#define	UPERFSRC_RP0		24	/* read requests from Proc 0 */
#define	UPERFSRC_SDVRA		25	/* streaming dvma rds, bus A */
#define	UPERFSRC_SDVWA		26	/* streaming dvma wrs, bus A */
#define	UPERFSRC_CDVRA		27	/* consistent dvma rds, bus A */
#define	UPERFSRC_CDVWA		28	/* consistent dvma wrs, bus A */
#define	UPERFSRC_SBMA		29	/* streaming buffer misses, A */
#define	UPERFSRC_DVA		30	/* cycles A granted to dvma */
#define	UPERFSRC_DVWA		31	/* words xfered on bus A */
#define	UPERFSRC_PIOA		32	/* pio cycles on bus A */
#define	UPERFSRC_SDVRB		33	/* streaming dvma rds, bus A */
#define	UPERFSRC_SDVWB		34	/* streaming dvma wrs, bus A */
#define	UPERFSRC_CDVRB		35	/* consistent dvma rds, bus A */
#define	UPERFSRC_CDVWB		36	/* consistent dvma wrs, bus A */
#define	UPERFSRC_SBMB		37	/* streaming buffer misses, A */
#define	UPERFSRC_DVB		38	/* cycles A granted to dvma */
#define	UPERFSRC_DVWB		39	/* words xfered on bus A */
#define	UPERFSRC_PIOB		40	/* pio cycles on bus A */
#define	UPERFSRC_TLBMISS	41	/* tlb misses */
#define	UPERFSRC_NINTRS		42	/* number of interrupts */
#define	UPERFSRC_INACK		43	/* interrupt nacks on UPA */
#define	UPERFSRC_PIOR		44	/* PIO read xfers */
#define	UPERFSRC_PIOW		45	/* PIO write xfers */
#define	UPERFSRC_MERGE		46	/* merge buffer xacts */
#define	UPERFSRC_TBLA		47	/* dma retries for tblwalk, A */
#define	UPERFSRC_STCA		48	/* dma retries for tblwalk, A */
#define	UPERFSRC_TBLB		49	/* dma retries for tblwalk, B */
#define	UPERFSRC_STCB		50	/* dma retries for tblwalk, B */

struct uperf_io {
	int cnt_flags;
	int cnt_src0;
	int cnt_src1;
	u_int32_t cnt_val0;
	u_int32_t cnt_val1;
};

#define	UPIO_GCNTSRC	_IOWR('U', 0, struct uperf_io)	/* get cntr src */
#define	UPIO_SCNTSRC	_IOWR('U', 1, struct uperf_io)	/* set cntr src */
#define	UPIO_CLRCNT	_IOWR('U', 2, struct uperf_io)	/* clear cntrs */
#define	UPIO_GETCNT	_IOWR('U', 3, struct uperf_io)	/* get cntrs */
