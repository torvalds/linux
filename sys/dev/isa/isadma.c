/*	$OpenBSD: isadma.c,v 1.39 2025/07/23 01:14:54 jsg Exp $	*/
/*	$NetBSD: isadma.c,v 1.32 1997/09/05 01:48:33 thorpej Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * Device driver for the ISA on-board DMA controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>
#include <dev/isa/isadmareg.h>

#ifdef __ISADMA_COMPAT
/* XXX ugly, but will go away soon... */
struct device *isa_dev;

bus_dmamap_t isadma_dmam[8];
#endif

/* Used by isa_malloc() */
#include <sys/malloc.h>
struct isa_mem {
	struct device *isadev;
	int chan;
	bus_size_t size;
	bus_addr_t addr;
	caddr_t kva;
	struct isa_mem *next;
} *isa_mem_head = 0;

/*
 * High byte of DMA address is stored in this DMAPG register for
 * the Nth DMA channel.
 */
static int dmapageport[2][4] = {
	{0x7, 0x3, 0x1, 0x2},
	{0xf, 0xb, 0x9, 0xa}
};

static u_int8_t dmamode[4] = {
	DMA37MD_READ | DMA37MD_SINGLE,
	DMA37MD_WRITE | DMA37MD_SINGLE,
	DMA37MD_READ | DMA37MD_SINGLE | DMA37MD_LOOP,
	DMA37MD_WRITE | DMA37MD_SINGLE | DMA37MD_LOOP
};

int isadmamatch(struct device *, void *, void *);
void isadmaattach(struct device *, struct device *, void *);

const struct cfattach isadma_ca = {
	sizeof(struct device), isadmamatch, isadmaattach
};

struct cfdriver isadma_cd = {
	NULL, "isadma", DV_DULL, CD_INDIRECT
};

int
isadmamatch(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;

	/* Sure we exist */
	ia->ia_iosize = 0;
	return (1);
}

void
isadmaattach(struct device *parent, struct device *self, void *aux)
{
#ifdef __ISADMA_COMPAT
	int i, sz;
	struct isa_softc *sc = (struct isa_softc *)parent;

	/* XXX ugly, but will go away soon... */
	isa_dev = parent;

	for (i = 0; i < 8; i++) {
		sz = (i & 4) ? 1 << 17 : 1 << 16;
		if ((bus_dmamap_create(sc->sc_dmat, sz, 1, sz, sz,
		    BUS_DMA_24BIT|BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW,
		    &isadma_dmam[i])) != 0)
			panic("isadmaattach: can not create DMA map");
	}
#endif

	/* XXX I'd like to map the DMA ports here, see isa.c why not... */

	printf("\n");
}

static inline void isa_dmaunmask(struct isa_softc *, int);
static inline void isa_dmamask(struct isa_softc *, int);

static inline void
isa_dmaunmask(struct isa_softc *sc, int chan)
{
	int ochan = chan & 3;

	/* set dma channel mode, and set dma channel mode */
	if ((chan & 4) == 0)
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h,
		    DMA1_SMSK, ochan | DMA37SM_CLEAR);
	else
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h,
		    DMA2_SMSK, ochan | DMA37SM_CLEAR);
}

static inline void
isa_dmamask(struct isa_softc *sc, int chan)
{
	int ochan = chan & 3;

	/* set dma channel mode, and set dma channel mode */
	if ((chan & 4) == 0) {
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h,
		    DMA1_SMSK, ochan | DMA37SM_SET);
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h,
		    DMA1_FFC, 0);
	} else {
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h,
		    DMA2_SMSK, ochan | DMA37SM_SET);
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h,
		    DMA2_FFC, 0);
	}
}

/*
 * isa_dmacascade(): program 8237 DMA controller channel to accept
 * external dma control by a board.
 */
void
isa_dmacascade(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	int ochan = chan & 3;

	if (chan < 0 || chan > 7) {
		printf("%s: bogus drq %d\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	if (ISA_DRQ_ISFREE(sc, chan) == 0) {
		printf("%s: DRQ %d is not free\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	ISA_DRQ_ALLOC(sc, chan);

	/* set dma channel mode, and set dma channel mode */
	if ((chan & 4) == 0)
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h,
		    DMA1_MODE, ochan | DMA37MD_CASCADE);
	else
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h,
		    DMA2_MODE, ochan | DMA37MD_CASCADE);

	isa_dmaunmask(sc, chan);
	return;

 lose:
	panic("isa_dmacascade");
}

int
isa_dmamap_create(struct device *isadev, int chan, bus_size_t size, int flags)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_size_t maxsize;

	if (chan < 0 || chan > 7) {
		printf("%s: bogus drq %d\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	if (chan & 4)
		maxsize = (1 << 17);
	else
		maxsize = (1 << 16);

	if (size > maxsize)
		return (EINVAL);

	if (ISA_DRQ_ISFREE(sc, chan) == 0) {
		printf("%s: drq %d is not free\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	ISA_DRQ_ALLOC(sc, chan);

	return (bus_dmamap_create(sc->sc_dmat, size, 1, size, maxsize,
	    flags, &sc->sc_dmamaps[chan]));

 lose:
	panic("isa_dmamap_create");
}

void
isa_dmamap_destroy(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;

	if (chan < 0 || chan > 7) {
		printf("%s: bogus drq %d\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	if (ISA_DRQ_ISFREE(sc, chan)) {
		printf("%s: drq %d is already free\n",
		    sc->sc_dev.dv_xname, chan);
		goto lose;
	}

	ISA_DRQ_FREE(sc, chan);

	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dmamaps[chan]);
	return;

 lose:
	panic("isa_dmamap_destroy");
}

/*
 * isa_dmastart(): program 8237 DMA controller channel and set it
 * in motion.
 */
int
isa_dmastart(struct device *isadev, int chan, void *addr, bus_size_t nbytes,
    struct proc *p, int flags, int busdmaflags)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_dmamap_t dmam;
	bus_addr_t dmaaddr;
	int waport;
	int ochan = chan & 3;
	int error;
#ifdef __ISADMA_COMPAT
	int compat = busdmaflags & BUS_DMA_BUS1;

	busdmaflags &= ~BUS_DMA_BUS1;
#endif /* __ISADMA_COMPAT */

	if (chan < 0 || chan > 7) {
		printf("%s: bogus drq %d\n", sc->sc_dev.dv_xname, chan);
		goto lose;
	}

#ifdef ISADMA_DEBUG
	printf("isa_dmastart: drq %d, addr %p, nbytes 0x%lx, p %p, "
	    "flags 0x%x, dmaflags 0x%x\n",
	    chan, addr, nbytes, p, flags, busdmaflags);
#endif

	if (chan & 4) {
		if (nbytes > (1 << 17) || nbytes & 1 || (u_long)addr & 1) {
			printf("%s: drq %d, nbytes 0x%lx, addr %p\n",
			    sc->sc_dev.dv_xname, chan, nbytes, addr);
			goto lose;
		}
	} else {
		if (nbytes > (1 << 16)) {
			printf("%s: drq %d, nbytes 0x%lx\n",
			    sc->sc_dev.dv_xname, chan, nbytes);
			goto lose;
		}
	}

	dmam = sc->sc_dmamaps[chan];
	if (dmam == NULL) {
#ifdef __ISADMA_COMPAT
		if (compat)
			dmam = sc->sc_dmamaps[chan] = isadma_dmam[chan];
		else
#endif /* __ISADMA_COMPAT */
		panic("isa_dmastart: no DMA map for chan %d", chan);
	}

	error = bus_dmamap_load(sc->sc_dmat, dmam, addr, nbytes, p,
	    busdmaflags);
	if (error)
		return (error);

#ifdef ISADMA_DEBUG
	__asm(".globl isa_dmastart_afterload ; isa_dmastart_afterload:");
#endif

	if (flags & DMAMODE_READ) {
		bus_dmamap_sync(sc->sc_dmat, dmam, 0, dmam->dm_mapsize,
		    BUS_DMASYNC_PREREAD);
		sc->sc_dmareads |= (1 << chan);
	} else {
		bus_dmamap_sync(sc->sc_dmat, dmam, 0, dmam->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
		sc->sc_dmareads &= ~(1 << chan);
	}

	dmaaddr = dmam->dm_segs[0].ds_addr;

#ifdef ISADMA_DEBUG
	printf("     dmaaddr 0x%lx\n", dmaaddr);

	__asm(".globl isa_dmastart_aftersync ; isa_dmastart_aftersync:");
#endif

	sc->sc_dmalength[chan] = nbytes;

	isa_dmamask(sc, chan);
	sc->sc_dmafinished &= ~(1 << chan);

	if ((chan & 4) == 0) {
		/* set dma channel mode */
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h, DMA1_MODE,
		    ochan | dmamode[flags]);

		/* send start address */
		waport = DMA1_CHN(ochan);
		bus_space_write_1(sc->sc_iot, sc->sc_dmapgh,
		    dmapageport[0][ochan], (dmaaddr >> 16) & 0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h, waport,
		    dmaaddr & 0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h, waport,
		    (dmaaddr >> 8) & 0xff);

		/* send count */
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h, waport + 1,
		    (--nbytes) & 0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_dma1h, waport + 1,
		    (nbytes >> 8) & 0xff);
	} else {
		/* set dma channel mode */
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h, DMA2_MODE,
		    ochan | dmamode[flags]);

		/* send start address */
		waport = DMA2_CHN(ochan);
		bus_space_write_1(sc->sc_iot, sc->sc_dmapgh,
		    dmapageport[1][ochan], (dmaaddr >> 16) & 0xff);
		dmaaddr >>= 1;
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h, waport,
		    dmaaddr & 0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h, waport,
		    (dmaaddr >> 8) & 0xff);

		/* send count */
		nbytes >>= 1;
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h, waport + 2,
		    (--nbytes) & 0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_dma2h, waport + 2,
		    (nbytes >> 8) & 0xff);
	}

	isa_dmaunmask(sc, chan);
	return (0);

 lose:
	panic("isa_dmastart");
}

void
isa_dmaabort(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;

	if (chan < 0 || chan > 7) {
		panic("isa_dmaabort: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}

	isa_dmamask(sc, chan);
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dmamaps[chan]);
	sc->sc_dmareads &= ~(1 << chan);
}

bus_size_t
isa_dmacount(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	int waport;
	bus_size_t nbytes;
	int ochan = chan & 3;

	if (chan < 0 || chan > 7) {
		panic("isa_dmacount: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}

	isa_dmamask(sc, chan);

	/*
	 * We have to shift the byte count by 1.  If we're in auto-initialize
	 * mode, the count may have wrapped around to the initial value.  We
	 * can't use the TC bit to check for this case, so instead we compare
	 * against the original byte count.
	 * If we're not in auto-initialize mode, then the count will wrap to
	 * -1, so we also handle that case.
	 */
	if ((chan & 4) == 0) {
		waport = DMA1_CHN(ochan);
		nbytes = bus_space_read_1(sc->sc_iot, sc->sc_dma1h,
		    waport + 1) + 1;
		nbytes += bus_space_read_1(sc->sc_iot, sc->sc_dma1h,
		    waport + 1) << 8;
		nbytes &= 0xffff;
	} else {
		waport = DMA2_CHN(ochan);
		nbytes = bus_space_read_1(sc->sc_iot, sc->sc_dma2h,
		    waport + 2) + 1;
		nbytes += bus_space_read_1(sc->sc_iot, sc->sc_dma2h,
		    waport + 2) << 8;
		nbytes <<= 1;
		nbytes &= 0x1ffff;
	}

	if (nbytes == sc->sc_dmalength[chan])
		nbytes = 0;

	isa_dmaunmask(sc, chan);
	return (nbytes);
}

int
isa_dmafinished(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;

	if (chan < 0 || chan > 7) {
		panic("isa_dmafinished: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}

	/* check that the terminal count was reached */
	if ((chan & 4) == 0)
		sc->sc_dmafinished |= bus_space_read_1(sc->sc_iot,
		    sc->sc_dma1h, DMA1_SR) & 0x0f;
	else
		sc->sc_dmafinished |= (bus_space_read_1(sc->sc_iot,
		    sc->sc_dma2h, DMA2_SR) & 0x0f) << 4;

	return ((sc->sc_dmafinished & (1 << chan)) != 0);
}

void
isa_dmadone(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_dmamap_t dmam;

	if (chan < 0 || chan > 7) {
		panic("isa_dmadone: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}

	dmam = sc->sc_dmamaps[chan];

	isa_dmamask(sc, chan);

	if (isa_dmafinished(isadev, chan) == 0)
		printf("%s: isa_dmadone: channel %d not finished\n",
		    sc->sc_dev.dv_xname, chan);

	bus_dmamap_sync(sc->sc_dmat, dmam, 0, dmam->dm_mapsize,
	    (sc->sc_dmareads & (1 << chan)) ? BUS_DMASYNC_POSTREAD :
	    BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->sc_dmat, dmam);
	sc->sc_dmareads &= ~(1 << chan);
}

int
isa_dmamem_alloc(struct device *isadev, int chan, bus_size_t size,
    bus_addr_t *addrp, int flags)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_dma_segment_t seg;
	int error, boundary, rsegs;

	if (chan < 0 || chan > 7) {
		panic("isa_dmamem_alloc: %s: bogus drq %d",
		    sc->sc_dev.dv_xname, chan);
	}

	boundary = (chan & 4) ? (1 << 17) : (1 << 16);

	size = round_page(size);

	error = bus_dmamem_alloc(sc->sc_dmat, size, NBPG, boundary,
	    &seg, 1, &rsegs, flags);
	if (error)
		return (error);

	*addrp = seg.ds_addr;
	return (0);
}

void
isa_dmamem_free(struct device *isadev, int chan, bus_addr_t addr,
    bus_size_t size)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_dma_segment_t seg;

	if (chan < 0 || chan > 7) {
		panic("isa_dmamem_free: %s: bogus drq %d",
		    sc->sc_dev.dv_xname, chan);
	}

	seg.ds_addr = addr;
	seg.ds_len = size;

	bus_dmamem_free(sc->sc_dmat, &seg, 1);
}

int
isa_dmamem_map(struct device *isadev, int chan, bus_addr_t addr,
    bus_size_t size, caddr_t *kvap, int flags)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	bus_dma_segment_t seg;

	if (chan < 0 || chan > 7) {
		panic("isa_dmamem_map: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}

	seg.ds_addr = addr;
	seg.ds_len = size;

	return (bus_dmamem_map(sc->sc_dmat, &seg, 1, size, kvap, flags));
}

void
isa_dmamem_unmap(struct device *isadev, int chan, caddr_t kva, size_t size)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;

	if (chan < 0 || chan > 7) {
		panic("isa_dmamem_unmap: %s: bogus drq %d",
		    sc->sc_dev.dv_xname, chan);
	}

	bus_dmamem_unmap(sc->sc_dmat, kva, size);
}

int
isa_drq_isfree(struct device *isadev, int chan)
{
	struct isa_softc *sc = (struct isa_softc *)isadev;
	if (chan < 0 || chan > 7) {
		panic("isa_drq_isfree: %s: bogus drq %d", sc->sc_dev.dv_xname,
		    chan);
	}
	return ISA_DRQ_ISFREE(sc, chan);
}

void *
isa_malloc(struct device *isadev, int chan, size_t size, int pool, int flags)
{
	bus_addr_t addr;
	caddr_t kva;
	int bflags;
	struct isa_mem *m;

	bflags = flags & M_NOWAIT ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;

	if (isa_dmamem_alloc(isadev, chan, size, &addr, bflags))
		return 0;
	if (isa_dmamem_map(isadev, chan, addr, size, &kva, bflags)) {
		isa_dmamem_free(isadev, chan, addr, size);
		return 0;
	}
	m = malloc(sizeof(*m), pool, flags);
	if (m == NULL) {
		isa_dmamem_unmap(isadev, chan, kva, size);
		isa_dmamem_free(isadev, chan, addr, size);
		return 0;
	}
	m->isadev = isadev;
	m->chan = chan;
	m->size = size;
	m->addr = addr;
	m->kva = kva;
	m->next = isa_mem_head;
	isa_mem_head = m;
	return (void *)kva;
}

void
isa_free(void *addr, int pool)
{
	struct isa_mem **mp, *m;
	caddr_t kva = (caddr_t)addr;

	for(mp = &isa_mem_head; *mp && (*mp)->kva != kva; mp = &(*mp)->next)
		;
	m = *mp;
	if (!m) {
		printf("isa_free: freeing unallocated memory\n");
		return;
	}
	*mp = m->next;
	isa_dmamem_unmap(m->isadev, m->chan, kva, m->size);
	isa_dmamem_free(m->isadev, m->chan, m->addr, m->size);
	free(m, pool, 0);
}
