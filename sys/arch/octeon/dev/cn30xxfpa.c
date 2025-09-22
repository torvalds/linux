/*	$OpenBSD: cn30xxfpa.c,v 1.11 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/vmparam.h>
#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxfpavar.h>
#include <octeon/dev/cn30xxfpareg.h>

struct cn30xxfpa_softc {
	int			sc_initialized;

	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;

	bus_dma_tag_t		sc_dmat;
};

void		cn30xxfpa_bootstrap(struct octeon_config *);
void		cn30xxfpa_reset(void);
void		cn30xxfpa_buf_dma_alloc(struct cn30xxfpa_buf *);

void		cn30xxfpa_init(struct cn30xxfpa_softc *);
#ifdef notyet
uint64_t	cn30xxfpa_iobdma(struct cn30xxfpa_softc *, int, int);
#endif

static struct cn30xxfpa_softc	cn30xxfpa_softc;

/* ---- global functions */

void
cn30xxfpa_bootstrap(struct octeon_config *mcp)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;

	sc->sc_regt = mcp->mc_iobus_bust;
	sc->sc_dmat = mcp->mc_iobus_dmat;

	cn30xxfpa_init(sc);
}

void
cn30xxfpa_reset(void)
{
	/* XXX */
}

int
cn30xxfpa_buf_init(int poolno, size_t size, size_t nelems,
    struct cn30xxfpa_buf **rfb)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;
	struct cn30xxfpa_buf *fb;
	int nsegs;
	paddr_t paddr;

	nsegs = 1/* XXX */;
	fb = malloc(sizeof(*fb) + sizeof(*fb->fb_dma_segs) * nsegs, M_DEVBUF,
	    M_WAITOK | M_ZERO);
	if (fb == NULL)
		return 1;
	fb->fb_poolno = poolno;
	fb->fb_size = size;
	fb->fb_nelems = nelems;
	fb->fb_len = size * nelems;
	fb->fb_dmat = sc->sc_dmat;
	fb->fb_dma_segs = (void *)(fb + 1);
	fb->fb_dma_nsegs = nsegs;

	cn30xxfpa_buf_dma_alloc(fb);

	for (paddr = fb->fb_paddr; paddr < fb->fb_paddr + fb->fb_len;
	    paddr += fb->fb_size)
		cn30xxfpa_buf_put_paddr(fb, paddr);

	*rfb = fb;

	return 0;
}

void *
cn30xxfpa_buf_get(struct cn30xxfpa_buf *fb)
{
	paddr_t paddr;
	vaddr_t addr;

	paddr = cn30xxfpa_buf_get_paddr(fb);
	if (paddr == 0)
		addr = 0;
	else
		addr = fb->fb_addr + (vaddr_t/* XXX */)(paddr - fb->fb_paddr);
	return (void *)addr;
}

void
cn30xxfpa_buf_dma_alloc(struct cn30xxfpa_buf *fb)
{
	int status;
	int nsegs;
	caddr_t va;

	status = bus_dmamap_create(fb->fb_dmat, fb->fb_len,
	    fb->fb_len / PAGE_SIZE,	/* # of segments */
	    fb->fb_len,			/* we don't use s/g for FPA buf */
	    PAGE_SIZE,			/* OCTEON hates >PAGE_SIZE boundary */
	    0, &fb->fb_dmah);
	if (status != 0)
		panic("%s failed", "bus_dmamap_create");

	status = bus_dmamem_alloc(fb->fb_dmat, fb->fb_len, CACHELINESIZE, 0,
	    fb->fb_dma_segs, fb->fb_dma_nsegs, &nsegs, 0);
	if (status != 0 || fb->fb_dma_nsegs != nsegs)
		panic("%s failed", "bus_dmamem_alloc");

	status = bus_dmamem_map(fb->fb_dmat, fb->fb_dma_segs, fb->fb_dma_nsegs,
	    fb->fb_len, &va, 0);
	if (status != 0)
		panic("%s failed", "bus_dmamem_map");

	status = bus_dmamap_load(fb->fb_dmat, fb->fb_dmah, va, fb->fb_len,
	    NULL,		/* kernel */
	    0);
	if (status != 0)
		panic("%s failed", "bus_dmamap_load");

	fb->fb_addr = (vaddr_t)va;
	fb->fb_paddr = fb->fb_dma_segs[0].ds_addr;
}

uint64_t
cn30xxfpa_query(int poolno)
{
	struct cn30xxfpa_softc *sc = &cn30xxfpa_softc;

	return bus_space_read_8(sc->sc_regt, sc->sc_regh,
	    FPA_QUE0_AVAILABLE_OFFSET + sizeof(uint64_t) * poolno);
}

/* ---- local functions */

void	cn30xxfpa_init_regs(struct cn30xxfpa_softc *);

void
cn30xxfpa_init(struct cn30xxfpa_softc *sc)
{
	if (sc->sc_initialized != 0)
		panic("%s: already initialized", __func__);
	sc->sc_initialized = 1;

	cn30xxfpa_init_regs(sc);
}

void
cn30xxfpa_init_regs(struct cn30xxfpa_softc *sc)
{
	int status;

	status = bus_space_map(sc->sc_regt, FPA_BASE, FPA_SIZE, 0,
	    &sc->sc_regh);
	if (status != 0)
		panic("%s: could not map FPA registers", __func__);

	bus_space_write_8(sc->sc_regt, sc->sc_regh, FPA_CTL_STATUS_OFFSET,
	    FPA_CTL_STATUS_ENB);
}
