/*	$OpenBSD: bba.c,v 1.12 2022/10/26 20:19:09 kn Exp $	*/
/* $NetBSD: bba.c,v 1.38 2011/06/04 01:27:57 tsutsui Exp $ */
/*
 * Copyright (c) 2011 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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

/* maxine/alpha baseboard audio (bba) */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/ic/am7930reg.h>
#include <dev/ic/am7930var.h>

#include <dev/tc/tcvar.h>
#include <dev/tc/ioasicreg.h>
#include <dev/tc/ioasicvar.h>

#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (am7930debug) printf x
#else
#define DPRINTF(x)
#endif  /* AUDIO_DEBUG */

#define BBA_MAX_DMA_SEGMENTS	16
#define BBA_DMABUF_SIZE		(BBA_MAX_DMA_SEGMENTS*IOASIC_DMA_BLOCKSIZE)
#define BBA_DMABUF_ALIGN	IOASIC_DMA_BLOCKSIZE
#define BBA_DMABUF_BOUNDARY	0

struct bba_mem {
	struct bba_mem *next;
	bus_addr_t addr;
	bus_size_t size;
	void *kva;
};

struct bba_dma_state {
	bus_dmamap_t dmam;		/* DMA map */
	size_t size;
	int active;
	int curseg;			/* current segment in DMA buffer */
	void (*intr)(void *);		/* higher-level audio handler */
	void *intr_arg;
};

struct bba_softc {
	struct am7930_softc sc_am7930;		/* glue to MI code */

	bus_space_tag_t sc_bst;			/* IOASIC bus tag/handle */
	bus_space_handle_t sc_bsh;
	bus_dma_tag_t sc_dmat;
	bus_space_handle_t sc_codec_bsh;	/* codec bus space handle */

	struct bba_mem *sc_mem_head;		/* list of buffers */

	struct bba_dma_state sc_tx_dma_state;
	struct bba_dma_state sc_rx_dma_state;
};

int	bba_match(struct device *, void *, void *);
void	bba_attach(struct device *, struct device *, void *);

struct cfdriver bba_cd = {
	NULL, "bba", DV_DULL
};

const struct cfattach bba_ca = {
	sizeof(struct bba_softc), bba_match, bba_attach
};

/*
 * Define our interface into the am7930 MI driver.
 */

uint8_t	 bba_codec_iread(struct am7930_softc *, int);
uint16_t bba_codec_iread16(struct am7930_softc *, int);
void	 bba_codec_iwrite(struct am7930_softc *, int, uint8_t);
void	 bba_codec_iwrite16(struct am7930_softc *, int, uint16_t);
void	 bba_onopen(struct am7930_softc *);
void	 bba_onclose(struct am7930_softc *);

struct am7930_glue bba_glue = {
	bba_codec_iread,
	bba_codec_iwrite,
	bba_codec_iread16,
	bba_codec_iwrite16,
	bba_onopen,
	bba_onclose,
	24
};

/*
 * Define our interface to the higher level audio driver.
 */

int	bba_round_blocksize(void *, int);
int	bba_halt_output(void *);
int	bba_halt_input(void *);
void	*bba_allocm(void *, int, size_t, int, int);
void	bba_freem(void *, void *, int);
size_t	bba_round_buffersize(void *, int, size_t);
int	bba_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	bba_trigger_input(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);

const struct audio_hw_if bba_hw_if = {
	.open = am7930_open,
	.close = am7930_close,
	.set_params = am7930_set_params,
	.round_blocksize = bba_round_blocksize,
	.commit_settings = am7930_commit_settings,
	.halt_output = bba_halt_output,
	.halt_input = bba_halt_input,
	.set_port = am7930_set_port,
	.get_port = am7930_get_port,
	.query_devinfo = am7930_query_devinfo,
	.allocm = bba_allocm,
	.freem = bba_freem,
	.round_buffersize = bba_round_buffersize,
	.trigger_output = bba_trigger_output,
	.trigger_input = bba_trigger_input,
};

int	bba_intr(void *);
void	bba_reset(struct bba_softc *, int);
void	bba_codec_dwrite(struct am7930_softc *, int, uint8_t);
uint8_t	bba_codec_dread(struct am7930_softc *, int);

int
bba_match(struct device *parent, void *vcf, void *aux)
{
	struct ioasicdev_attach_args *ia = aux;

	if (strcmp(ia->iada_modname, "isdn") != 0 &&
	    strcmp(ia->iada_modname, "AMD79c30") != 0)
		return 0;

	return 1;
}

void
bba_attach(struct device *parent, struct device *self, void *aux)
{
	struct ioasicdev_attach_args *ia = aux;
	struct bba_softc *sc = (struct bba_softc *)self;
	struct ioasic_softc *iosc = (struct ioasic_softc *)parent;

	sc->sc_bst = iosc->sc_bst;
	sc->sc_bsh = iosc->sc_bsh;
	sc->sc_dmat = iosc->sc_dmat;

	/* get the bus space handle for codec */
	if (bus_space_subregion(sc->sc_bst, sc->sc_bsh,
	    ia->iada_offset, 0, &sc->sc_codec_bsh)) {
		printf(": unable to map device\n");
		return;
	}

	printf("\n");

	bba_reset(sc,1);

	/*
	 * Set up glue for MI code early; we use some of it here.
	 */
	sc->sc_am7930.sc_glue = &bba_glue;

	/*
	 *  MI initialisation.  We will be doing DMA.
	 */
	am7930_init(&sc->sc_am7930, AUDIOAMD_DMA_MODE);

	ioasic_intr_establish(parent, ia->iada_cookie, IPL_AUDIO,
	    bba_intr, sc, self->dv_xname);

	audio_attach_mi(&bba_hw_if, sc, NULL, self);
}

void
bba_onopen(struct am7930_softc *sc)
{
}

void
bba_onclose(struct am7930_softc *sc)
{
}

void
bba_reset(struct bba_softc *sc, int reset)
{
	uint32_t ssr;

	/* disable any DMA and reset the codec */
	ssr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR);
	ssr &= ~(IOASIC_CSR_DMAEN_ISDN_T | IOASIC_CSR_DMAEN_ISDN_R);
	if (reset)
		ssr &= ~IOASIC_CSR_ISDN_ENABLE;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);
	DELAY(10);	/* 400ns required for codec to reset */

	/* initialise DMA pointers */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_DMAPTR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_NEXTPTR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_DMAPTR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_NEXTPTR, 0);

	/* take out of reset state */
	if (reset) {
		ssr |= IOASIC_CSR_ISDN_ENABLE;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);
	}

}

void *
bba_allocm(void *v, int direction, size_t size, int mtype, int flags)
{
	struct bba_softc *sc = v;
	bus_dma_segment_t seg;
	int rseg;
	caddr_t kva;
	struct bba_mem *m;
	int w;
	int state;

	DPRINTF(("bba_allocm: size = %zu\n", size));
	state = 0;
	w = (flags & M_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;

	if (bus_dmamem_alloc(sc->sc_dmat, size, BBA_DMABUF_ALIGN,
	    BBA_DMABUF_BOUNDARY, &seg, 1, &rseg, w)) {
		printf("%s: can't allocate DMA buffer\n",
		    sc->sc_am7930.sc_dev.dv_xname);
		goto bad;
	}
	state |= 1;

	if (bus_dmamem_map(sc->sc_dmat, &seg, rseg, size,
	    &kva, w | BUS_DMA_COHERENT)) {
		printf("%s: can't map DMA buffer\n",
		    sc->sc_am7930.sc_dev.dv_xname);
		goto bad;
	}
	state |= 2;

	m = malloc(sizeof(struct bba_mem), mtype, flags | M_CANFAIL);
	if (m == NULL)
		goto bad;
	m->addr = seg.ds_addr;
	m->size = seg.ds_len;
	m->kva = kva;
	m->next = sc->sc_mem_head;
	sc->sc_mem_head = m;

	return (void *)kva;

bad:
	if (state & 2)
		bus_dmamem_unmap(sc->sc_dmat, kva, size);
	if (state & 1)
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
	return NULL;
}

void
bba_freem(void *v, void *ptr, int mtype)
{
	struct bba_softc *sc = v;
	struct bba_mem **mp, *m;
	bus_dma_segment_t seg;
	void *kva;

	kva = (void *)ptr;
	for (mp = &sc->sc_mem_head; *mp && (*mp)->kva != kva; mp = &(*mp)->next)
		continue;
	m = *mp;
	if (m == NULL) {
		printf("bba_freem: freeing unallocated memory\n");
		return;
	}
	*mp = m->next;
	bus_dmamem_unmap(sc->sc_dmat, kva, m->size);

	seg.ds_addr = m->addr;
	seg.ds_len = m->size;
	bus_dmamem_free(sc->sc_dmat, &seg, 1);
	free(m, mtype, 0);
}

size_t
bba_round_buffersize(void *v, int direction, size_t size)
{

	DPRINTF(("bba_round_buffersize: size=%zu\n", size));
	return size > BBA_DMABUF_SIZE ? BBA_DMABUF_SIZE :
	    roundup(size, IOASIC_DMA_BLOCKSIZE);
}

int
bba_halt_output(void *v)
{
	struct bba_softc *sc = v;
	struct bba_dma_state *d;
	uint32_t ssr;

	mtx_enter(&audio_lock);
	d = &sc->sc_tx_dma_state;
	/* disable any DMA */
	ssr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR);
	ssr &= ~IOASIC_CSR_DMAEN_ISDN_T;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_DMAPTR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_NEXTPTR, 0);
	mtx_leave(&audio_lock);

	if (d->active) {
		bus_dmamap_sync(sc->sc_dmat, d->dmam, 0, d->size,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, d->dmam);
		bus_dmamap_destroy(sc->sc_dmat, d->dmam);
		d->active = 0;
	}

	return 0;
}

int
bba_halt_input(void *v)
{
	struct bba_softc *sc = v;
	struct bba_dma_state *d;
	uint32_t ssr;

	mtx_enter(&audio_lock);
	d = &sc->sc_rx_dma_state;
	/* disable any DMA */
	ssr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR);
	ssr &= ~IOASIC_CSR_DMAEN_ISDN_R;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_DMAPTR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_NEXTPTR, 0);
	mtx_leave(&audio_lock);

	if (d->active) {
		bus_dmamap_sync(sc->sc_dmat, d->dmam, 0, d->size,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, d->dmam);
		bus_dmamap_destroy(sc->sc_dmat, d->dmam);
		d->active = 0;
	}

	return 0;
}

int
bba_trigger_output(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct bba_softc *sc = v;
	struct bba_dma_state *d;
	uint32_t ssr;
	tc_addr_t phys, nphys;
	int state;

	DPRINTF(("bba_trigger_output: sc=%p start=%p end=%p blksize=%d intr=%p(%p)\n",
	    sc, start, end, blksize, intr, arg));
	d = &sc->sc_tx_dma_state;
	state = 0;

	/* disable any DMA */
	ssr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR);
	ssr &= ~IOASIC_CSR_DMAEN_ISDN_T;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);

	d->size = (vaddr_t)end - (vaddr_t)start;
	if (bus_dmamap_create(sc->sc_dmat, d->size,
	    BBA_MAX_DMA_SEGMENTS, IOASIC_DMA_BLOCKSIZE,
	    BBA_DMABUF_BOUNDARY, BUS_DMA_NOWAIT, &d->dmam)) {
		printf("bba_trigger_output: can't create DMA map\n");
		goto bad;
	}
	state |= 1;

	if (bus_dmamap_load(sc->sc_dmat, d->dmam, start, d->size, NULL,
	    BUS_DMA_WRITE | BUS_DMA_NOWAIT)) {
		printf("bba_trigger_output: can't load DMA map\n");
		goto bad;
	}
	bus_dmamap_sync(sc->sc_dmat, d->dmam, 0, d->size, BUS_DMASYNC_PREWRITE);
	state |= 2;

	d->intr = intr;
	d->intr_arg = arg;
	d->curseg = 1;

	/* get physical address of buffer start */
	phys = (tc_addr_t)d->dmam->dm_segs[0].ds_addr;
	nphys = (tc_addr_t)d->dmam->dm_segs[1 % d->dmam->dm_nsegs].ds_addr;

	/* setup DMA pointer */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_DMAPTR,
	    IOASIC_DMA_ADDR(phys));
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_X_NEXTPTR,
	    IOASIC_DMA_ADDR(nphys));

	/* kick off DMA */
	mtx_enter(&audio_lock);
	ssr |= IOASIC_CSR_DMAEN_ISDN_T;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);

	d->active = 1;
	mtx_leave(&audio_lock);
	return 0;

bad:
	if (state & 2)
		bus_dmamap_unload(sc->sc_dmat, d->dmam);
	if (state & 1)
		bus_dmamap_destroy(sc->sc_dmat, d->dmam);
	return 1;
}

int
bba_trigger_input(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct bba_softc *sc = v;
	struct bba_dma_state *d;
	uint32_t ssr;
	tc_addr_t phys, nphys;
	int state;

	DPRINTF(("bba_trigger_input: sc=%p start=%p end=%p blksize=%d intr=%p(%p)\n",
	    sc, start, end, blksize, intr, arg));
	d = &sc->sc_rx_dma_state;
	state = 0;

	/* disable any DMA */
	ssr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR);
	ssr &= ~IOASIC_CSR_DMAEN_ISDN_R;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);

	d->size = (vaddr_t)end - (vaddr_t)start;
	if (bus_dmamap_create(sc->sc_dmat, d->size,
	    BBA_MAX_DMA_SEGMENTS, IOASIC_DMA_BLOCKSIZE,
	    BBA_DMABUF_BOUNDARY, BUS_DMA_NOWAIT, &d->dmam)) {
		printf("bba_trigger_input: can't create DMA map\n");
		goto bad;
	}
	state |= 1;

	if (bus_dmamap_load(sc->sc_dmat, d->dmam, start, d->size, NULL,
	    BUS_DMA_READ | BUS_DMA_NOWAIT)) {
		printf("bba_trigger_input: can't load DMA map\n");
		goto bad;
	}
	bus_dmamap_sync(sc->sc_dmat, d->dmam, 0, d->size, BUS_DMASYNC_PREREAD);
	state |= 2;

	d->intr = intr;
	d->intr_arg = arg;
	d->curseg = 1;

	/* get physical address of buffer start */
	phys = (tc_addr_t)d->dmam->dm_segs[0].ds_addr;
	nphys = (tc_addr_t)d->dmam->dm_segs[1 % d->dmam->dm_nsegs].ds_addr;

	/* setup DMA pointer */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_DMAPTR,
	    IOASIC_DMA_ADDR(phys));
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_ISDN_R_NEXTPTR,
	    IOASIC_DMA_ADDR(nphys));

	/* kick off DMA */
	mtx_enter(&audio_lock);
	ssr |= IOASIC_CSR_DMAEN_ISDN_R;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, IOASIC_CSR, ssr);

	d->active = 1;
	mtx_leave(&audio_lock);
	return 0;

bad:
	if (state & 2)
		bus_dmamap_unload(sc->sc_dmat, d->dmam);
	if (state & 1)
		bus_dmamap_destroy(sc->sc_dmat, d->dmam);
	return 1;
}

int
bba_intr(void *v)
{
	struct bba_softc *sc = v;
	struct bba_dma_state *d;
	tc_addr_t nphys;
	int mask;

	mtx_enter(&audio_lock);

	mask = bus_space_read_4(sc->sc_bst, sc->sc_bsh, IOASIC_INTR);

	if (mask & IOASIC_INTR_ISDN_TXLOAD) {
		d = &sc->sc_tx_dma_state;
		d->curseg = (d->curseg+1) % d->dmam->dm_nsegs;
		nphys = (tc_addr_t)d->dmam->dm_segs[d->curseg].ds_addr;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    IOASIC_ISDN_X_NEXTPTR, IOASIC_DMA_ADDR(nphys));
		if (d->intr != NULL)
			(*d->intr)(d->intr_arg);
	}
	if (mask & IOASIC_INTR_ISDN_RXLOAD) {
		d = &sc->sc_rx_dma_state;
		d->curseg = (d->curseg+1) % d->dmam->dm_nsegs;
		nphys = (tc_addr_t)d->dmam->dm_segs[d->curseg].ds_addr;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    IOASIC_ISDN_R_NEXTPTR, IOASIC_DMA_ADDR(nphys));
		if (d->intr != NULL)
			(*d->intr)(d->intr_arg);
	}

	mtx_leave(&audio_lock);

	return 0;
}

int
bba_round_blocksize(void *v, int blk)
{
	return IOASIC_DMA_BLOCKSIZE;
}


/* indirect write */
void
bba_codec_iwrite(struct am7930_softc *sc, int reg, uint8_t val)
{
	DPRINTF(("bba_codec_iwrite(): sc=%p, reg=%02x, val=%02x\n", sc, reg, val));
	bba_codec_dwrite(sc, AM7930_DREG_CR, reg);
	bba_codec_dwrite(sc, AM7930_DREG_DR, val);
}


void
bba_codec_iwrite16(struct am7930_softc *sc, int reg, uint16_t val)
{
	DPRINTF(("bba_codec_iwrite16(): sc=%p, reg=%02x, val=%04x\n", sc, reg, val));
	bba_codec_dwrite(sc, AM7930_DREG_CR, reg);
	bba_codec_dwrite(sc, AM7930_DREG_DR, val);
	bba_codec_dwrite(sc, AM7930_DREG_DR, val >> 8);
}


/* indirect read */
uint8_t
bba_codec_iread(struct am7930_softc *sc, int reg)
{
	uint8_t val;

	DPRINTF(("bba_codec_iread(): sc=%p, reg=%02x\n", sc, reg));
	bba_codec_dwrite(sc, AM7930_DREG_CR, reg);
	val = bba_codec_dread(sc, AM7930_DREG_DR);

	DPRINTF(("read 0x%02x (%d)\n", val, val));

	return val;
}

uint16_t
bba_codec_iread16(struct am7930_softc *sc, int reg)
{
	uint16_t val;

	DPRINTF(("bba_codec_iread16(): sc=%p, reg=%02x\n", sc, reg));
	bba_codec_dwrite(sc, AM7930_DREG_CR, reg);
	val = bba_codec_dread(sc, AM7930_DREG_DR);
	val |= bba_codec_dread(sc, AM7930_DREG_DR) << 8;

	return val;
}


/* direct write */
void
bba_codec_dwrite(struct am7930_softc *asc, int reg, uint8_t val)
{
	struct bba_softc *sc = (struct bba_softc *)asc;

#if defined(__alpha__)
	bus_space_write_4(sc->sc_bst, sc->sc_codec_bsh, reg << 2, val << 8);
#else
	bus_space_write_4(sc->sc_bst, sc->sc_codec_bsh, reg << 6, val);
#endif
}

/* direct read */
uint8_t
bba_codec_dread(struct am7930_softc *asc, int reg)
{
	struct bba_softc *sc = (struct bba_softc *)asc;

#if defined(__alpha__)
	return (bus_space_read_4(sc->sc_bst, sc->sc_codec_bsh, reg << 2) >> 8) &
	    0xff;
#else
	return bus_space_read_4(sc->sc_bst, sc->sc_codec_bsh, reg << 6) & 0xff;
#endif
}
