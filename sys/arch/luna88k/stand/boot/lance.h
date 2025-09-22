/*	$OpenBSD: lance.h,v 1.1 2013/10/28 22:13:12 miod Exp $	*/
/*	$NetBSD: lance.h,v 1.1 2013/01/13 14:10:55 tsutsui Exp $ */

/*-
 * Copyright (c) 1982, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * @(#)if_lereg.h	8.2 (Berkeley) 10/30/93
 */

#define	LEMTU		1518
#define	LEMINSIZE	60	/* should be 64 if mode DTCR is set */
#define	LERBUF		8
#define	LERBUFLOG2	3
#define	LE_RLEN		(LERBUFLOG2 << 13)
#define	LE_NEXTRMD(x)	(((x) + 1) & (LERBUF - 1))
#define	LETBUF		1
#define	LETBUFLOG2	0
#define	LE_TLEN		(LETBUFLOG2 << 13)
#define	LE_NEXTTMD(x)	(((x) + 1) & (LETBUF - 1))

/* Local Area Network Controller for Ethernet (LANCE) registers */
struct lereg {
	volatile uint16_t	ler_rdp;	/* register data port */
	uint16_t unused;
	volatile uint16_t	ler_rap;	/* register address port */
};

/*
 * lance memory
 */

/* receive message descriptors. bits/hadr are byte order dependent. */
struct	lermd_v {
	volatile uint16_t rmd0;		/* low address of packet */
#if BYTE_ORDER == BIG_ENDIAN
	volatile uint8_t  rmd1_bits;	/* descriptor bits */
	volatile uint8_t  rmd1_hadr;	/* high address of packet */
#else
	volatile uint8_t  rmd1_hadr;	/* high address of packet */
	volatile uint8_t  rmd1_bits;	/* descriptor bits */
#endif
	volatile int16_t  rmd2;		/* buffer byte count */
	volatile uint16_t rmd3;		/* message byte count */
};

/* transmit message descriptors */
struct	letmd_v {
	volatile uint16_t tmd0;		/* low address of packet */
#if BYTE_ORDER == BIG_ENDIAN
	volatile uint8_t  tmd1_bits;	/* descriptor bits */
	volatile uint8_t  tmd1_hadr;	/* high address of packet */
#else
	volatile uint8_t  tmd1_hadr;	/* high address of packet */
	volatile uint8_t  tmd1_bits;	/* descriptor bits */
#endif
	volatile int16_t  tmd2;		/* buffer byte count */
	volatile uint16_t tmd3;		/* transmit error bits */
};

struct lemem {
	/* initialization block */
	volatile uint16_t	lem_mode;	/* mode */
	volatile uint16_t	lem_padr[3];	/* physical address */
	volatile uint16_t	lem_ladrf[4];	/* logical address filter */
	volatile uint16_t	lem_rdra;	/* receive descriptor addr */
	volatile uint16_t	lem_rlen;	/* rda high and ring size */
	volatile uint16_t	lem_tdra;	/* transmit descriptor addr */
	volatile uint16_t	lem_tlen;	/* tda high and ring size */
	uint16_t		lem_pad0[4];
	struct lermd_v		lem_rmd[LERBUF];
	struct letmd_v		lem_tmd[LETBUF];
	volatile uint8_t	lem_rbuf[LERBUF][LEMTU];
	volatile uint8_t	lem_tbuf[LETBUF][LEMTU];
};

struct le_softc {
	struct lereg *sc_reg;
	struct lemem *sc_mem;
	uint8_t sc_enaddr[6];
	int sc_curtmd;
	int sc_currmd;
};
