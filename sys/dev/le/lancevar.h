/*	$NetBSD: lancevar.h,v 1.10 2005/12/11 12:21:27 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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

/* $FreeBSD$ */

#ifndef _DEV_LE_LANCEVAR_H_
#define	_DEV_LE_LANCEVAR_H_

extern devclass_t le_devclass;

struct lance_softc {
	struct ifnet	*sc_ifp;
	struct ifmedia	sc_media;
	struct mtx	sc_mtx;
	struct callout	sc_wdog_ch;
	int		sc_wdog_timer;

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
	uint16_t	(*sc_rdcsr)(struct lance_softc *, uint16_t);
	void	(*sc_wrcsr)(struct lance_softc *, uint16_t, uint16_t);
	void	(*sc_hwreset)(struct lance_softc *);
	void	(*sc_hwinit)(struct lance_softc *);
	int	(*sc_hwintr)(struct lance_softc *);
	void	(*sc_nocarrier)(struct lance_softc *);
	int	(*sc_mediachange)(struct lance_softc *);
	void	(*sc_mediastatus)(struct lance_softc *, struct ifmediareq *);

	/*
	 * Media-supported by this interface.  If this is NULL,
	 * the only supported media is assumed to be "manual".
	 */
	const int	*sc_supmedia;
	int	sc_nsupmedia;
	int	sc_defaultmedia;

	uint16_t	sc_conf3;	/* CSR3 value */

	void	*sc_mem;		/* base address of RAM - CPU's view */
	bus_addr_t	sc_addr;	/* base address of RAM - LANCE's view */

	bus_size_t	sc_memsize;	/* size of RAM */

	int	sc_nrbuf;	/* number of receive buffers */
	int	sc_ntbuf;	/* number of transmit buffers */
	int	sc_last_rd;
	int	sc_first_td;
	int	sc_last_td;
	int	sc_no_td;

	int	sc_initaddr;
	int	sc_rmdaddr;
	int	sc_tmdaddr;
	int	sc_rbufaddr;
	int	sc_tbufaddr;

	uint8_t	sc_enaddr[ETHER_ADDR_LEN];

	void	(*sc_meminit)(struct lance_softc *);
	void	(*sc_start_locked)(struct lance_softc *);

	int	sc_flags;
#define	LE_ALLMULTI	(1 << 0)
#define	LE_BSWAP	(1 << 1)
#define	LE_CARRIER	(1 << 2)
#define	LE_DEBUG	(1 << 3)
#define	LE_PROMISC	(1 << 4)
};

#define	LE_LOCK_INIT(_sc, _name)					\
	mtx_init(&(_sc)->sc_mtx, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	LE_LOCK_INITIALIZED(_sc)	mtx_initialized(&(_sc)->sc_mtx)
#define	LE_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	LE_UNLOCK(_sc)			mtx_unlock(&(_sc)->sc_mtx)
#define	LE_LOCK_ASSERT(_sc, _what)	mtx_assert(&(_sc)->sc_mtx, (_what))
#define	LE_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->sc_mtx)

/*
 * Unfortunately, manual byte swapping is only necessary for the PCnet-PCI
 * variants but not for the original LANCE or ILACC so we cannot do this
 * with #ifdefs resolved at compile time.
 */
#define	LE_HTOLE16(v)	(((sc)->sc_flags & LE_BSWAP) ? htole16(v) : (v))
#define	LE_HTOLE32(v)	(((sc)->sc_flags & LE_BSWAP) ? htole32(v) : (v))
#define	LE_LE16TOH(v)	(((sc)->sc_flags & LE_BSWAP) ? le16toh(v) : (v))
#define	LE_LE32TOH(v)	(((sc)->sc_flags & LE_BSWAP) ? le32toh(v) : (v))

int lance_config(struct lance_softc *, const char*, int);
void lance_attach(struct lance_softc *);
void lance_detach(struct lance_softc *);
void lance_suspend(struct lance_softc *);
void lance_resume(struct lance_softc *);
void lance_init_locked(struct lance_softc *);
int lance_put(struct lance_softc *, int, struct mbuf *);
struct mbuf *lance_get(struct lance_softc *, int, int);
void lance_setladrf(struct lance_softc *, u_int16_t *);

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

/*
 * Compare two Ether/802 addresses for equality, inlined and
 * unrolled for speed.  Use this like memcmp().
 *
 * XXX: Add <machine/inlines.h> for stuff like this?
 * XXX: or maybe add it to libkern.h instead?
 *
 * "I'd love to have an inline assembler version of this."
 * XXX: Who wanted that? mycroft?  I wrote one, but this
 * version in C is as good as hand-coded assembly. -gwr
 *
 * Please do NOT tweak this without looking at the actual
 * assembly code generated before and after your tweaks!
 */
static inline uint16_t
ether_cmp(void *one, void *two)
{
	uint16_t *a = (u_short *)one;
	uint16_t *b = (u_short *)two;
	uint16_t diff;

#ifdef	m68k
	/*
	 * The post-increment-pointer form produces the best
	 * machine code for m68k.  This was carefully tuned
	 * so it compiles to just 8 short (2-byte) op-codes!
	 */
	diff  = *a++ - *b++;
	diff |= *a++ - *b++;
	diff |= *a++ - *b++;
#else
	/*
	 * Most modern CPUs do better with a single expresion.
	 * Note that short-cut evaluation is NOT helpful here,
	 * because it just makes the code longer, not faster!
	 */
	diff = (a[0] - b[0]) | (a[1] - b[1]) | (a[2] - b[2]);
#endif

	return (diff);
}

#endif /* _DEV_LE_LANCEVAR_H_ */
