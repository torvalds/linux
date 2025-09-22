/*	$OpenBSD: lance.c,v 1.13 2016/04/13 10:49:26 mpi Exp $	*/
/*	$NetBSD: lance.c,v 1.46 2012/02/02 19:43:03 tls Exp $	*/

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

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
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
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>

#ifdef DDB
#define	integrate
#define hide
#else
#define	integrate	static inline
#define hide		static
#endif

integrate struct mbuf *lance_get(struct lance_softc *, int, int);

int lance_mediachange(struct ifnet *);
void lance_mediastatus(struct ifnet *, struct ifmediareq *);

static inline u_int16_t ether_cmp(void *, void *);

void lance_stop(struct ifnet *, int);
int lance_ioctl(struct ifnet *, u_long, caddr_t);
void lance_watchdog(struct ifnet *);

struct cfdriver le_cd = {
	NULL, "le", DV_IFNET
};

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
	uint16_t *a = (uint16_t *)one;
	uint16_t *b = (uint16_t *)two;
	uint16_t diff;

#ifdef	__m68k__
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
	 * Most modern CPUs do better with a single expression.
	 * Note that short-cut evaluation is NOT helpful here,
	 * because it just makes the code longer, not faster!
	 */
	diff = (a[0] - b[0]) | (a[1] - b[1]) | (a[2] - b[2]);
#endif

	return (diff);
}

#define ETHER_CMP	ether_cmp

#ifdef LANCE_REVC_BUG
/* Make sure this is short-aligned, for ether_cmp(). */
static uint16_t bcast_enaddr[3] = { ~0, ~0, ~0 };
#endif

void
lance_config(struct lance_softc *sc)
{
	int i, nbuf;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	/* Initialize ifnet structure. */
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = sc->sc_start;
	ifp->if_ioctl = lance_ioctl;
	ifp->if_watchdog = lance_watchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
#ifdef LANCE_REVC_BUG
	ifp->if_flags &= ~IFF_MULTICAST;
#endif
	ifp->if_baudrate = IF_Mbps(10);

	/* Initialize ifmedia structures. */
	ifmedia_init(&sc->sc_ifmedia, 0, lance_mediachange, lance_mediastatus);
	if (sc->sc_supmedia != NULL) {
		for (i = 0; i < sc->sc_nsupmedia; i++)
			ifmedia_add(&sc->sc_ifmedia, sc->sc_supmedia[i],
			   0, NULL);
		ifmedia_set(&sc->sc_ifmedia, sc->sc_defaultmedia);
	} else {
		ifmedia_add(&sc->sc_ifmedia, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_ifmedia, IFM_ETHER|IFM_MANUAL);
	}

	if (sc->sc_memsize > 262144)
		sc->sc_memsize = 262144;

	switch (sc->sc_memsize) {
	case 8192:
		sc->sc_nrbuf = 4;
		sc->sc_ntbuf = 1;
		break;
	case 16384:
		sc->sc_nrbuf = 8;
		sc->sc_ntbuf = 2;
		break;
	case 32768:
		sc->sc_nrbuf = 16;
		sc->sc_ntbuf = 4;
		break;
	case 65536:
		sc->sc_nrbuf = 32;
		sc->sc_ntbuf = 8;
		break;
	case 131072:
		sc->sc_nrbuf = 64;
		sc->sc_ntbuf = 16;
		break;
	case 262144:
		sc->sc_nrbuf = 128;
		sc->sc_ntbuf = 32;
		break;
	default:
		/* weird memory size; cope with it */
		nbuf = sc->sc_memsize / LEBLEN;
		sc->sc_ntbuf = nbuf / 5;
		sc->sc_nrbuf = nbuf - sc->sc_ntbuf;
	}

	printf(": address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));
	printf("%s: %d receive buffers, %d transmit buffers\n",
	    sc->sc_dev.dv_xname, sc->sc_nrbuf, sc->sc_ntbuf);

	/* Make sure the chip is stopped. */
	lance_stop(ifp, 0);

	/* claim 802.1q capability */
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	sc->sc_rbufaddr = mallocarray(sc->sc_nrbuf, sizeof(int), M_DEVBUF,
	    M_WAITOK);
	sc->sc_tbufaddr = mallocarray(sc->sc_ntbuf, sizeof(int), M_DEVBUF,
	    M_WAITOK);
}

void
lance_reset(struct lance_softc *sc)
{
	int s;

	s = splnet();
	lance_init(sc);
	splx(s);
}

void
lance_stop(struct ifnet *ifp, int disable)
{
	struct lance_softc *sc = ifp->if_softc;

	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_STOP);
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
int
lance_init(struct lance_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int timo;
	u_long a;

	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_STOP);
	DELAY(100);

	/* Newer LANCE chips have a reset register */
	if (sc->sc_hwreset)
		(*sc->sc_hwreset)(sc);

	/* Set the correct byte swapping mode, etc. */
	(*sc->sc_wrcsr)(sc, LE_CSR3, sc->sc_conf3);

	/* Set up LANCE init block. */
	(*sc->sc_meminit)(sc);

	/* Give LANCE the physical address of its init block. */
	a = sc->sc_addr + LE_INITADDR(sc);
	(*sc->sc_wrcsr)(sc, LE_CSR1, a);
	(*sc->sc_wrcsr)(sc, LE_CSR2, a >> 16);

	/* Try to initialize the LANCE. */
	DELAY(100);
	(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INIT);

	/* Wait for initialization to finish. */
	for (timo = 100000; timo; timo--)
		if ((*sc->sc_rdcsr)(sc, LE_CSR0) & LE_C0_IDON)
			break;

	if ((*sc->sc_rdcsr)(sc, LE_CSR0) & LE_C0_IDON) {
		/* Start the LANCE. */
		(*sc->sc_wrcsr)(sc, LE_CSR0, LE_C0_INEA | LE_C0_STRT);
		ifp->if_flags |= IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		(*sc->sc_start)(ifp);
	} else
		printf("%s: controller failed to initialize\n",
		     sc->sc_dev.dv_xname);
	if (sc->sc_hwinit)
		(*sc->sc_hwinit)(sc);

	return (0);
}

/*
 * Routine to copy from mbuf chain to transmit buffer in
 * network buffer memory.
 */
int
lance_put(struct lance_softc *sc, int boff, struct mbuf *m)
{
	struct mbuf *n;
	int len, tlen = 0;

	for (; m; m = n) {
		len = m->m_len;
		if (len == 0) {
			n = m_free(m);
			continue;
		}
		(*sc->sc_copytobuf)(sc, mtod(m, void *), boff, len);
		boff += len;
		tlen += len;
		n = m_free(m);
	}
	if (tlen < LEMINSIZE) {
		(*sc->sc_zerobuf)(sc, boff, LEMINSIZE - tlen);
		tlen = LEMINSIZE;
	}
	return (tlen);
}

/*
 * Pull data off an interface.
 * Len is length of data, with local net header stripped.
 * We copy the data into mbufs.  When full cluster sized units are present
 * we copy into clusters.
 */
integrate struct mbuf *
lance_get(struct lance_softc *sc, int boff, int totlen)
{
	struct mbuf *m, *top, **mp;
	int len, pad;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);
	m->m_pkthdr.len = totlen;
	pad = ALIGN(sizeof(struct ether_header)) - sizeof(struct ether_header);
	m->m_data += pad;
	len = MHLEN - pad;
	top = NULL;
	mp = &top;

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				m_freem(top);
				return NULL;
			}
			len = MLEN;
		}
		if (totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (m->m_flags & M_EXT) {
				len = MCLBYTES;
				if (!top) {
					m->m_data += pad;
					len -= pad;
				}
			}
		}
		m->m_len = len = min(totlen, len);
		(*sc->sc_copyfrombuf)(sc, mtod(m, caddr_t), boff, len);
		boff += len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	return (top);
}

struct mbuf *
lance_read(struct lance_softc *sc, int boff, int len)
{
	struct mbuf *m;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ether_header *eh;

	if (len <= sizeof(struct ether_header) ||
	    len > ((ifp->if_capabilities & IFCAP_VLAN_MTU) ?
		ETHER_VLAN_ENCAP_LEN + ETHERMTU + sizeof(struct ether_header) :
		ETHERMTU + sizeof(struct ether_header))) {
#ifdef LEDEBUG
		printf("%s: invalid packet size %d; dropping\n",
		    sc->sc_dev.dv_xnam, len);
#endif
		ifp->if_ierrors++;
		return (NULL);
	}

	/* Pull packet off interface. */
	m = lance_get(sc, boff, len);
	if (m == NULL) {
		ifp->if_ierrors++;
		return (NULL);
	}

	eh = mtod(m, struct ether_header *);

#ifdef LANCE_REVC_BUG
	/*
	 * The old LANCE (Rev. C) chips have a bug which causes
	 * garbage to be inserted in front of the received packet.
	 * The work-around is to ignore packets with an invalid
	 * destination address (garbage will usually not match).
	 * Of course, this precludes multicast support...
	 */
	if (ETHER_CMP(eh->ether_dhost, sc->sc_arpcom.ac_enaddr) &&
	    ETHER_CMP(eh->ether_dhost, bcast_enaddr)) {
		m_freem(m);
		return (NULL);
	}
#endif

	/*
	 * Some lance device does not present IFF_SIMPLEX behavior on multicast
	 * packets.  Make sure to drop it if it is from ourselves.
	 */
	if (!ETHER_CMP(eh->ether_shost, sc->sc_arpcom.ac_enaddr)) {
		m_freem(m);
		return (NULL);
	}

	return (m);
}

void
lance_watchdog(struct ifnet *ifp)
{
	struct lance_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++ifp->if_oerrors;

	lance_reset(sc);
}

int
lance_mediachange(struct ifnet *ifp)
{
	struct lance_softc *sc = ifp->if_softc;

	if (sc->sc_mediachange)
		return ((*sc->sc_mediachange)(sc));
	return (0);
}

void
lance_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct lance_softc *sc = ifp->if_softc;

	if ((ifp->if_flags & IFF_UP) == 0)
		return;

	ifmr->ifm_status = IFM_AVALID;
	if (sc->sc_havecarrier)
		ifmr->ifm_status |= IFM_ACTIVE;

	if (sc->sc_mediastatus)
		(*sc->sc_mediastatus)(sc, ifmr);
}

/*
 * Process an ioctl request.
 */
int
lance_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lance_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			lance_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				lance_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				lance_stop(ifp, 0);
				ifp->if_flags &= ~IFF_RUNNING;
			}
		}

#ifdef LEDEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_debug = 1;
		else
			sc->sc_debug = 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			/*
			 * Multicast list has changed; set the hardware filter
			 * accordingly.
			 */
			lance_reset(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * Set up the logical address filter.
 */
void
lance_setladrf(struct arpcom *ac, uint16_t *af)
{
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	uint32_t crc;
	struct ether_multistep step;

	/*
	 * Set up multicast address filter by passing all multicast addresses
	 * through a crc generator, and then using the high order 6 bits as an
	 * index into the 64 bit logical address filter.  The high order bit
	 * selects the word, while the rest of the bits select the bit within
	 * the word.
	 */

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0)
		goto allmulti;

	af[0] = af[1] = af[2] = af[3] = 0x0000;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		crc = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN);

		/* Just want the 6 most significant bits. */
		crc >>= 26;

		/* Set the corresponding bit in the filter. */
		af[crc >> 4] |= 1 << (crc & 0xf);

		ETHER_NEXT_MULTI(step, enm);
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
	return;

allmulti:
	ifp->if_flags |= IFF_ALLMULTI;
	af[0] = af[1] = af[2] = af[3] = 0xffff;
}

/*
 * Routines for accessing the transmit and receive buffers.
 * The various CPU and adapter configurations supported by this
 * driver require three different access methods for buffers
 * and descriptors:
 *	(1) contig (contiguous data; no padding),
 *	(2) gap2 (two bytes of data followed by two bytes of padding),
 *	(3) gap16 (16 bytes of data followed by 16 bytes of padding).
 */

/*
 * contig: contiguous data with no padding.
 *
 * Buffers may have any alignment.
 */

void
lance_copytobuf_contig(struct lance_softc *sc, void *from, int boff, int len)
{
	uint8_t *buf = sc->sc_mem;

	/*
	 * Just call memcpy() to do the work.
	 */
	memcpy(buf + boff, from, len);
}

void
lance_copyfrombuf_contig(struct lance_softc *sc, void *to, int boff, int len)
{
	uint8_t *buf = sc->sc_mem;

	/*
	 * Just call memcpy() to do the work.
	 */
	memcpy(to, buf + boff, len);
}

void
lance_zerobuf_contig(struct lance_softc *sc, int boff, int len)
{
	uint8_t *buf = sc->sc_mem;

	/*
	 * Just let memset() do the work
	 */
	memset(buf + boff, 0, len);
}

#if 0
/*
 * Examples only; duplicate these and tweak (if necessary) in
 * machine-specific front-ends.
 */

/*
 * gap2: two bytes of data followed by two bytes of pad.
 *
 * Buffers must be 4-byte aligned.  The code doesn't worry about
 * doing an extra byte.
 */

void
lance_copytobuf_gap2(struct lance_softc *sc, void *fromv, int boff, int len)
{
	volatile void *buf = sc->sc_mem;
	void *from = fromv;
	volatile uint16_t *bptr;

	if (boff & 0x1) {
		/* handle unaligned first byte */
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*bptr = (*from++ << 8) | (*bptr & 0xff);
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 1) {
		*bptr = (from[1] << 8) | (from[0] & 0xff);
		bptr += 2;
		from += 2;
		len -= 2;
	}
	if (len == 1)
		*bptr = (uint16_t)*from;
}

void
lance_copyfrombuf_gap2(struct lance_softc *sc, void *tov, int boff, int len)
{
	volatile void *buf = sc->sc_mem;
	void *to = tov;
	volatile uint16_t *bptr;
	uint16_t tmp;

	if (boff & 0x1) {
		/* handle unaligned first byte */
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*to++ = (*bptr >> 8) & 0xff;
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 1) {
		tmp = *bptr;
		*to++ = tmp & 0xff;
		*to++ = (tmp >> 8) & 0xff;
		bptr += 2;
		len -= 2;
	}
	if (len == 1)
		*to = *bptr & 0xff;
}

void
lance_zerobuf_gap2(struct lance_softc *sc, int boff, int len)
{
	volatile void *buf = sc->sc_mem;
	volatile uint16_t *bptr;

	if ((unsigned int)boff & 0x1) {
		bptr = ((volatile uint16_t *)buf) + (boff - 1);
		*bptr &= 0xff;
		bptr += 2;
		len--;
	} else
		bptr = ((volatile uint16_t *)buf) + boff;
	while (len > 0) {
		*bptr = 0;
		bptr += 2;
		len -= 2;
	}
}

/*
 * gap16: 16 bytes of data followed by 16 bytes of pad.
 *
 * Buffers must be 32-byte aligned.
 */

void
lance_copytobuf_gap16(struct lance_softc *sc, void *fromv, int boff, int len)
{
	volatile uint8_t *buf = sc->sc_mem;
	void *from = fromv;
	uint8_t *bptr;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memcpy(bptr + boff, from, xfer);
		from += xfer;
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}

void
lance_copyfrombuf_gap16(struct lance_softc *sc, void *tov, int boff, int len)
{
	volatile uint8_t *buf = sc->sc_mem;
	void *to = tov;
	uint8_t *bptr;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memcpy(to, bptr + boff, xfer);
		to += xfer;
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}

void
lance_zerobuf_gap16(struct lance_softc *sc, int boff, int len)
{
	volatile uint8_t *buf = sc->sc_mem;
	uint8_t *bptr;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		memset(bptr + boff, 0, xfer);
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}
#endif /* Example only */
