/*	$OpenBSD: lancevar.h,v 1.3 2015/09/11 13:02:28 stsp Exp $	*/
/*	$NetBSD: lancevar.h,v 1.15 2012/02/02 19:43:03 tls Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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

struct lance_softc {
	struct	device sc_dev;		/* base device glue */
	struct	arpcom sc_arpcom;	/* Ethernet common part */
	struct	ifmedia sc_ifmedia;	/* our supported media */

	/*
	 * Memory functions:
	 *
	 *	copy to/from descriptor
	 *	copy to/from buffer
	 *	zero bytes in buffer
	 */
	void	(*sc_copytodesc)(struct lance_softc *, void *, int, int);
	void	(*sc_copyfromdesc)(struct lance_softc *, void *, int, int);
	void	(*sc_copytobuf)(struct lance_softc *, void *, int, int);
	void	(*sc_copyfrombuf)(struct lance_softc *, void *, int, int);
	void	(*sc_zerobuf)(struct lance_softc *, int, int);

	/*
	 * Machine-dependent functions:
	 *
	 *	read/write CSR
	 *	hardware reset hook - may be NULL
	 *	hardware init hook - may be NULL
	 *	no carrier hook - may be NULL
	 *	media change hook - may be NULL
	 */
	uint16_t (*sc_rdcsr)(struct lance_softc *, uint16_t);
	void	(*sc_wrcsr)(struct lance_softc *, uint16_t, uint16_t);
	void	(*sc_hwreset)(struct lance_softc *);
	void	(*sc_hwinit)(struct lance_softc *);
	void	(*sc_nocarrier)(struct lance_softc *);
	int	(*sc_mediachange)(struct lance_softc *);
	void	(*sc_mediastatus)(struct lance_softc *, struct ifmediareq *);

	/*
	 * Media-supported by this interface.  If this is NULL,
	 * the only supported media is assumed to be "manual".
	 */
	const uint64_t	*sc_supmedia;
	int		sc_nsupmedia;
	uint64_t	sc_defaultmedia;

	/* PCnet bit to use software selection of a port */
	int	sc_initmodemedia;

	int	sc_havecarrier;	/* carrier status */

	uint16_t sc_conf3;	/* CSR3 value */
	uint16_t sc_saved_csr0;/* Value of csr0 at time of interrupt */

	void	*sc_mem;	/* base address of RAM -- CPU's view */
	u_long	sc_addr;	/* base address of RAM -- LANCE's view */

	u_long	sc_memsize;	/* size of RAM */

	int	sc_nrbuf;	/* number of receive buffers */
	int	sc_ntbuf;	/* number of transmit buffers */
	int	sc_last_rd;
	int	sc_first_td, sc_last_td, sc_no_td;

	int	sc_initaddr;
	int	sc_rmdaddr;
	int	sc_tmdaddr;
	int	*sc_rbufaddr;
	int	*sc_tbufaddr;

#ifdef LEDEBUG
	int	sc_debug;
#endif
	uint8_t sc_enaddr[ETHER_ADDR_LEN];

	void (*sc_meminit)(struct lance_softc *);
	void (*sc_start)(struct ifnet *);
};

extern struct cfdriver le_cd;

void lance_config(struct lance_softc *);
void lance_reset(struct lance_softc *);
int lance_init(struct lance_softc *);
int lance_put(struct lance_softc *, int, struct mbuf *);
struct mbuf *lance_read(struct lance_softc *, int, int);
void lance_setladrf(struct arpcom *, uint16_t *);

/*
 * The following functions are only useful on certain CPU/bus
 * combinations.  They should be written in assembly language for
 * maximum efficiency, but machine-independent versions are provided
 * for drivers that have not yet been optimized.
 */
void lance_copytobuf_contig(struct lance_softc *, void *, int, int);
void lance_copyfrombuf_contig(struct lance_softc *, void *, int, int);
void lance_zerobuf_contig(struct lance_softc *, int, int);

#if 0	/* Example only - see lance.c */
void lance_copytobuf_gap2(struct lance_softc *, void *, int, int);
void lance_copyfrombuf_gap2(struct lance_softc *, void *, int, int);
void lance_zerobuf_gap2(struct lance_softc *, int, int);

void lance_copytobuf_gap16(struct lance_softc *, void *, int, int);
void lance_copyfrombuf_gap16(struct lance_softc *, void *, int, int);
void lance_zerobuf_gap16(struct lance_softc *, int, int);
#endif /* Example only */
