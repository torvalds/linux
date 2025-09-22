/*	$OpenBSD: if_le_ioasic.c,v 1.20 2025/06/29 15:55:22 miod Exp $	*/
/*	$NetBSD: if_le_ioasic.c,v 1.18 2001/11/13 06:26:10 lukem Exp $	*/

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * LANCE on DEC IOCTL ASIC.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

#include <dev/tc/if_levar.h>
#include <dev/tc/tcvar.h>
#include <dev/tc/ioasicreg.h>
#include <dev/tc/ioasicvar.h>

#ifdef __alpha__
#include <machine/rpb.h>
#endif /* __alpha__ */

struct le_ioasic_softc {
	struct	am7990_softc sc_am7990;	/* glue to MI code */
	struct	lereg1 *sc_r1;		/* LANCE registers */
	/* XXX must match with le_softc of if_levar.h XXX */

	bus_dma_tag_t sc_dmat;		/* bus dma tag */
	bus_dmamap_t sc_dmamap;		/* bus dmamap */
};

int  le_ioasic_match(struct device *, void *, void *);
void le_ioasic_attach(struct device *, struct device *, void *);

const struct cfattach le_ioasic_ca = {
	sizeof(struct le_softc), le_ioasic_match, le_ioasic_attach
};

void le_ioasic_copytobuf_gap2(struct lance_softc *, void *, int, int);
void le_ioasic_copyfrombuf_gap2(struct lance_softc *, void *, int, int);
void le_ioasic_copytobuf_gap16(struct lance_softc *, void *, int, int);
void le_ioasic_copyfrombuf_gap16(struct lance_softc *, void *, int, int);
void le_ioasic_zerobuf_gap16(struct lance_softc *, int, int);

#ifdef __alpha__
#ifdef DEC_3000_500
int	le_ioasic_ifmedia_change(struct lance_softc *);
void	le_ioasic_ifmedia_status(struct lance_softc *, struct ifmediareq *);
void	le_ioasic_nocarrier(struct lance_softc *);
#endif
#endif /* __alpha__ */

int
le_ioasic_match(struct device *parent, void *match, void *aux)
{
	struct ioasicdev_attach_args *d = aux;

	if (strncmp("PMAD-BA ", d->iada_modname, TC_ROM_LLEN) != 0)
		return 0;

	return 1;
}

/* IOASIC LANCE DMA needs 128KB boundary aligned 128KB chunk */
#define	LE_IOASIC_MEMSIZE	(128*1024)
#define	LE_IOASIC_MEMALIGN	(128*1024)

void
le_ioasic_attach(struct device *parent, struct device *self, void *aux)
{
	struct le_ioasic_softc *sc = (void *)self;
	struct ioasicdev_attach_args *d = aux;
	struct lance_softc *le = &sc->sc_am7990.lsc;
	bus_space_tag_t ioasic_bst;
	bus_space_handle_t ioasic_bsh;
	bus_dma_tag_t dmat;
	bus_dma_segment_t seg;
	tc_addr_t tca;
	u_int32_t ssr;
	int rseg;
	caddr_t le_iomem;

	ioasic_bst = ((struct ioasic_softc *)parent)->sc_bst;
	ioasic_bsh = ((struct ioasic_softc *)parent)->sc_bsh;
	dmat = sc->sc_dmat = ((struct ioasic_softc *)parent)->sc_dmat;
	/*
	 * Allocate a DMA area for the chip.
	 */
	if (bus_dmamem_alloc(dmat, LE_IOASIC_MEMSIZE, LE_IOASIC_MEMALIGN,
	    0, &seg, 1, &rseg, BUS_DMA_NOWAIT)) {
		printf("can't allocate DMA area for LANCE\n");
		return;
	}
	if (bus_dmamem_map(dmat, &seg, rseg, LE_IOASIC_MEMSIZE,
	    &le_iomem, BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) {
		printf("can't map DMA area for LANCE\n");
		bus_dmamem_free(dmat, &seg, rseg);
		return;
	}
	/*
	 * Create and load the DMA map for the DMA area.
	 */
	if (bus_dmamap_create(dmat, LE_IOASIC_MEMSIZE, 1,
	    LE_IOASIC_MEMSIZE, 0, BUS_DMA_NOWAIT, &sc->sc_dmamap)) {
		printf("can't create DMA map\n");
		goto bad;
	}
	if (bus_dmamap_load(dmat, sc->sc_dmamap,
	    le_iomem, LE_IOASIC_MEMSIZE, NULL, BUS_DMA_NOWAIT)) {
		printf("can't load DMA map\n");
		goto bad;
	}
	/*
	 * Bind 128KB buffer with IOASIC DMA.
	 */
	tca = IOASIC_DMA_ADDR(sc->sc_dmamap->dm_segs[0].ds_addr);
	bus_space_write_4(ioasic_bst, ioasic_bsh, IOASIC_LANCE_DMAPTR, tca);
	ssr = bus_space_read_4(ioasic_bst, ioasic_bsh, IOASIC_CSR);
	ssr |= IOASIC_CSR_DMAEN_LANCE;
	bus_space_write_4(ioasic_bst, ioasic_bsh, IOASIC_CSR, ssr);

	sc->sc_r1 = (struct lereg1 *)
		TC_DENSE_TO_SPARSE(TC_PHYS_TO_UNCACHED(d->iada_addr));
	le->sc_mem = (void *)TC_PHYS_TO_UNCACHED(le_iomem);
	le->sc_copytodesc = le_ioasic_copytobuf_gap2;
	le->sc_copyfromdesc = le_ioasic_copyfrombuf_gap2;
	le->sc_copytobuf = le_ioasic_copytobuf_gap16;
	le->sc_copyfrombuf = le_ioasic_copyfrombuf_gap16;
	le->sc_zerobuf = le_ioasic_zerobuf_gap16;

#ifdef __alpha__
#ifdef DEC_3000_500
	/*
	 * On non-300 DEC 3000 models, both AUI and UTP are available.
	 */
	if (cputype == ST_DEC_3000_500) {
		static const uint64_t media[] = {
			IFM_ETHER | IFM_10_T,
			IFM_ETHER | IFM_10_5,
			IFM_ETHER | IFM_AUTO
		};
		le->sc_mediachange = le_ioasic_ifmedia_change;
		le->sc_mediastatus = le_ioasic_ifmedia_status;
		le->sc_supmedia = media;
		le->sc_nsupmedia = nitems(media);
		le->sc_defaultmedia = IFM_ETHER | IFM_AUTO;
		le->sc_nocarrier = le_ioasic_nocarrier;
	}
#endif
#endif /* __alpha__ */

	dec_le_common_attach(&sc->sc_am7990,
	    (u_char *)((struct ioasic_softc *)parent)->sc_base
	        + IOASIC_SLOT_2_START);

	ioasic_intr_establish(parent, d->iada_cookie, IPL_NET,
	    am7990_intr, sc, self->dv_xname);
	return;

 bad:
	bus_dmamem_unmap(dmat, le_iomem, LE_IOASIC_MEMSIZE);
	bus_dmamem_free(dmat, &seg, rseg);
}

/*
 * Special memory access functions needed by ioasic-attached LANCE
 * chips.
 */

/*
 * gap2: two bytes of data followed by two bytes of pad.
 *
 * Buffers must be 4-byte aligned.  The code doesn't worry about
 * doing an extra byte.
 */

void
le_ioasic_copytobuf_gap2(struct lance_softc *sc, void *fromv,
    int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t from = fromv;
	volatile u_int16_t *bptr;

	if (boff & 0x1) {
		/* handle unaligned first byte */
		bptr = ((volatile u_int16_t *)buf) + (boff - 1);
		*bptr = (*from++ << 8) | (*bptr & 0xff);
		bptr += 2;
		len--;
	} else
		bptr = ((volatile u_int16_t *)buf) + boff;
	while (len > 1) {
		*bptr = (from[1] << 8) | (from[0] & 0xff);
		bptr += 2;
		from += 2;
		len -= 2;
	}
	if (len == 1)
		*bptr = (u_int16_t)*from;
}

void
le_ioasic_copyfrombuf_gap2(struct lance_softc *sc, void *tov,
    int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t to = tov;
	volatile u_int16_t *bptr;
	u_int16_t tmp;

	if (boff & 0x1) {
		/* handle unaligned first byte */
		bptr = ((volatile u_int16_t *)buf) + (boff - 1);
		*to++ = (*bptr >> 8) & 0xff;
		bptr += 2;
		len--;
	} else
		bptr = ((volatile u_int16_t *)buf) + boff;
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

/*
 * gap16: 16 bytes of data followed by 16 bytes of pad.
 *
 * Buffers must be 32-byte aligned.
 */

void
le_ioasic_copytobuf_gap16(struct lance_softc *sc, void *fromv,
    int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t from = fromv;
	caddr_t bptr;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;

	/*
	 * Dispose of boff so destination of subsequent copies is
	 * 16-byte aligned.
	 */
	if (boff) {
		int xfer;
		xfer = min(len, 16 - boff);
		bcopy(from, bptr + boff, xfer);
		from += xfer;
		bptr += 32;
		len -= xfer;
	}

	/* Destination of  copies is now 16-byte aligned. */
	if (len >= 16)
		switch ((u_long)from & (sizeof(u_int32_t) -1)) {
		case 2:
			/*  Ethernet headers make this the dominant case. */
		do {
			u_int32_t *dst = (u_int32_t*)bptr;
			u_int16_t t0;
			u_int32_t t1,  t2, t3, t4;

			/* read from odd-16-bit-aligned, cached src */
			t0 = *(u_int16_t*)from;
			t1 = *(u_int32_t*)(from+2);
			t2 = *(u_int32_t*)(from+6);
			t3 = *(u_int32_t*)(from+10);
			t4 = *(u_int16_t*)(from+14);

			/* DMA buffer is uncached on mips */
			dst[0] =         t0 |  (t1 << 16);
			dst[1] = (t1 >> 16) |  (t2 << 16);
			dst[2] = (t2 >> 16) |  (t3 << 16);
			dst[3] = (t3 >> 16) |  (t4 << 16);

			from += 16;
			bptr += 32;
			len -= 16;
		} while (len >= 16);
		break;

		case 0:
		do {
			u_int32_t *src = (u_int32_t*)from;
			u_int32_t *dst = (u_int32_t*)bptr;
			u_int32_t t0, t1, t2, t3;

			t0 = src[0]; t1 = src[1]; t2 = src[2]; t3 = src[3];
			dst[0] = t0; dst[1] = t1; dst[2] = t2; dst[3] = t3;

			from += 16;
			bptr += 32;
			len -= 16;
		} while (len >= 16);
		break;

		default:
		/* Does odd-aligned case ever happen? */
		do {
			bcopy(from, bptr, 16);
			from += 16;
			bptr += 32;
			len -= 16;
		} while (len >= 16);
		break;
	}
	if (len)
		bcopy(from, bptr, len);
}

void
le_ioasic_copyfrombuf_gap16(struct lance_softc *sc, void *tov,
    int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t to = tov;
	caddr_t bptr;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;

	/* Dispose of boff. source of copy is subsequently 16-byte aligned. */
	if (boff) {
		int xfer;
		xfer = min(len, 16 - boff);
		bcopy(bptr+boff, to, xfer);
		to += xfer;
		bptr += 32;
		len -= xfer;
	}
	if (len >= 16)
	switch ((u_long)to & (sizeof(u_int32_t) -1)) {
	case 2:
		/*
		 * to is aligned to an odd 16-bit boundary.  Ethernet headers
		 * make this the dominant case (98% or more).
		 */
		do {
			u_int32_t *src = (u_int32_t*)bptr;
			u_int32_t t0, t1, t2, t3;

			/* read from uncached aligned DMA buf */
			t0 = src[0]; t1 = src[1]; t2 = src[2]; t3 = src[3];

			/* write to odd-16-bit-word aligned dst */
			*(u_int16_t *) (to+0)  = (u_short)  t0;
			*(u_int32_t *) (to+2)  = (t0 >> 16) |  (t1 << 16);
			*(u_int32_t *) (to+6)  = (t1 >> 16) |  (t2 << 16);
			*(u_int32_t *) (to+10) = (t2 >> 16) |  (t3 << 16);
			*(u_int16_t *) (to+14) = (t3 >> 16);
			bptr += 32;
			to += 16;
			len -= 16;
		} while (len > 16);
		break;
	case 0:
		/* 32-bit aligned aligned copy. Rare. */
		do {
			u_int32_t *src = (u_int32_t*)bptr;
			u_int32_t *dst = (u_int32_t*)to;
			u_int32_t t0, t1, t2, t3;

			t0 = src[0]; t1 = src[1]; t2 = src[2]; t3 = src[3];
			dst[0] = t0; dst[1] = t1; dst[2] = t2; dst[3] = t3;
			to += 16;
			bptr += 32;
			len -= 16;
		} while (len  > 16);
		break;

	/* XXX Does odd-byte-aligned case ever happen? */
	default:
		do {
			bcopy(bptr, to, 16);
			to += 16;
			bptr += 32;
			len -= 16;
		} while (len  > 16);
		break;
	}
	if (len)
		bcopy(bptr, to, len);
}

void
le_ioasic_zerobuf_gap16(struct lance_softc *sc, int boff, int len)
{
	volatile caddr_t buf = sc->sc_mem;
	caddr_t bptr;
	int xfer;

	bptr = buf + ((boff << 1) & ~0x1f);
	boff &= 0xf;
	xfer = min(len, 16 - boff);
	while (len > 0) {
		bzero(bptr + boff, xfer);
		bptr += 32;
		boff = 0;
		len -= xfer;
		xfer = min(len, 16);
	}
}

#ifdef __alpha__
#ifdef DEC_3000_500
int
le_ioasic_ifmedia_change(struct lance_softc *lsc)
{
	struct le_ioasic_softc *sc = (struct le_ioasic_softc *)lsc;
	struct ifmedia *ifm = &sc->sc_am7990.lsc.sc_ifmedia;
	bus_space_tag_t ioasic_bst =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bst;
	bus_space_handle_t ioasic_bsh =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bsh;
	u_int32_t ossr, ssr;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return EINVAL;

	ossr = ssr = bus_space_read_4(ioasic_bst, ioasic_bsh, IOASIC_CSR);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_10_5:
		ssr &= ~IOASIC_CSR_ETHERNET_UTP;
		break;
	case IFM_10_T:
		ssr |= IOASIC_CSR_ETHERNET_UTP;
		break;
	case IFM_AUTO:
		break;
	default:
		return EINVAL;
	}

	if (ossr != ssr)
		bus_space_write_4(ioasic_bst, ioasic_bsh, IOASIC_CSR, ssr);

	return 0;
}

void
le_ioasic_ifmedia_status(struct lance_softc *lsc, struct ifmediareq *req)
{
	struct le_ioasic_softc *sc = (struct le_ioasic_softc *)lsc;
	bus_space_tag_t ioasic_bst =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bst;
	bus_space_handle_t ioasic_bsh =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bsh;
	u_int32_t ssr;

	ssr = bus_space_read_4(ioasic_bst, ioasic_bsh, IOASIC_CSR);

	if (ssr & IOASIC_CSR_ETHERNET_UTP)
		req->ifm_active = IFM_ETHER | IFM_10_T;
	else
		req->ifm_active = IFM_ETHER | IFM_10_5;
}

void
le_ioasic_nocarrier(struct lance_softc *lsc)
{
	struct le_ioasic_softc *sc = (struct le_ioasic_softc *)lsc;
	bus_space_tag_t ioasic_bst =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bst;
	bus_space_handle_t ioasic_bsh =
	    ((struct ioasic_softc *)sc->sc_am7990.lsc.sc_dev.dv_parent)->sc_bsh;
	u_int32_t ossr, ssr;

	ossr = ssr = bus_space_read_4(ioasic_bst, ioasic_bsh, IOASIC_CSR);

	if (ssr & IOASIC_CSR_ETHERNET_UTP) {
		switch (IFM_SUBTYPE(lsc->sc_ifmedia.ifm_media)) {
		case IFM_10_5:
		case IFM_AUTO:
			printf("%s: lost carrier on UTP port"
			    ", switching to AUI port\n", lsc->sc_dev.dv_xname);
			ssr ^= IOASIC_CSR_ETHERNET_UTP;
			break;
		}
	} else {
		switch (IFM_SUBTYPE(lsc->sc_ifmedia.ifm_media)) {
		case IFM_10_T:
		case IFM_AUTO:
			printf("%s: lost carrier on AUI port"
			    ", switching to UTP port\n", lsc->sc_dev.dv_xname);
			ssr ^= IOASIC_CSR_ETHERNET_UTP;
			break;
		}
	}

	if (ossr != ssr)
		bus_space_write_4(ioasic_bst, ioasic_bsh, IOASIC_CSR, ssr);
}
#endif
#endif /* __alpha__ */
