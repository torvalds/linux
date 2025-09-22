/*     $OpenBSD: bcm2835_dmac.c,v 1.5 2024/10/17 05:10:53 jsg Exp $ */

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
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

/*-
 * Copyright (c) 2014 Jared D. McNeill <jmcneill@invisible.ca>
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <dev/ic/bcm2835_dmac.h>

#define BCMDMAC_CHANNELMASK ((1 << 12) - 1)
#define DEVNAME(sc) ((sc)->sc_dev.dv_xname)

struct bcmdmac_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	int sc_fa_node;

	struct mutex sc_lock;
	struct bcmdmac_channel *sc_channels;
	int sc_nchannels;
	uint32_t sc_channelmask;
};

static struct bcmdmac_softc *bcmdmac_sc = NULL;

struct bcmdmac_channel {
	struct bcmdmac_softc *ch_sc;
	void *ch_ih;
	uint8_t ch_index;
	void (*ch_callback)(uint32_t, uint32_t, void *);
	void *ch_callbackarg;
	uint32_t ch_debug;
};

int bcmdmac_match(struct device *, void *, void *);
void bcmdmac_attach(struct device *, struct device *, void *);

const struct cfattach bcmdmac_ca = {
	sizeof(struct bcmdmac_softc),
	bcmdmac_match,
	bcmdmac_attach,
};

struct cfdriver bcmdmac_cd = { NULL, "bcmdmac", DV_DULL };

/* utilities */
enum bcmdmac_type
bcmdmac_channel_type(struct bcmdmac_channel *ch)
{
	if (ISSET(ch->ch_debug, DMAC_DEBUG_LITE))
		return BCMDMAC_TYPE_LITE;
	else
		return BCMDMAC_TYPE_NORMAL;
}

int
bcmdmac_channel_used(struct bcmdmac_channel *ch)
{
	return ch->ch_callback != NULL;
}

void
bcmdmac_write(struct bcmdmac_softc *sc, bus_size_t offset, uint32_t value)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, offset, value);
}

uint32_t
bcmdmac_read(struct bcmdmac_softc *sc, bus_size_t offset)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, offset);
}

/* driver handles */
int
bcmdmac_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2835-dma");
}

void
bcmdmac_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmdmac_softc *sc = (struct bcmdmac_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct bcmdmac_channel *ch;
	uint32_t val;
	int index;
	bus_addr_t addr;
	bus_size_t size;

	if (bcmdmac_sc) {
		printf(": already attached\n");
		return;
	}
	bcmdmac_sc = sc;

	sc->sc_iot = faa->fa_iot;
	sc->sc_fa_node = faa->fa_node;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	addr = faa->fa_reg[0].addr;
	size = faa->fa_reg[0].size;
	if (bus_space_map(sc->sc_iot, addr, size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_channelmask =
	    OF_getpropint(faa->fa_node, "brcm,dma-channel-mask", -1);
	sc->sc_channelmask &= BCMDMAC_CHANNELMASK;

	mtx_init(&sc->sc_lock, IPL_SCHED);

	sc->sc_nchannels = 32 - __builtin_clz(sc->sc_channelmask);
	sc->sc_channels = malloc(sizeof(*sc->sc_channels) * sc->sc_nchannels,
	    M_DEVBUF, M_WAITOK);

	printf(":");
	for (index = 0; index < sc->sc_nchannels; index++) {
		ch = &sc->sc_channels[index];
		ch->ch_sc = sc;
		ch->ch_index = index;
		ch->ch_callback = NULL;
		ch->ch_callbackarg = NULL;
		ch->ch_ih = NULL;
		if (!ISSET(sc->sc_channelmask, (1 << index)))
			continue;

		printf(" DMA%d", index);

		ch->ch_debug = bcmdmac_read(sc, DMAC_DEBUG(index));

		val = bcmdmac_read(sc, DMAC_CS(index));
		val |= DMAC_CS_RESET;
		bcmdmac_write(sc, DMAC_CS(index), val);
	}
	printf("\n");
}

int
bcmdmac_intr(void *arg)
{
	struct bcmdmac_channel *ch = arg;
	struct bcmdmac_softc *sc = ch->ch_sc;
	uint32_t cs, ce;

	cs = bcmdmac_read(sc, DMAC_CS(ch->ch_index));
	bcmdmac_write(sc, DMAC_CS(ch->ch_index), cs);
	cs &= DMAC_CS_INT | DMAC_CS_END | DMAC_CS_ERROR;

	ce = bcmdmac_read(sc, DMAC_DEBUG(ch->ch_index));
	ce &= DMAC_DEBUG_READ_ERROR | DMAC_DEBUG_FIFO_ERROR
	    | DMAC_DEBUG_READ_LAST_NOT_SET_ERROR;
	bcmdmac_write(sc, DMAC_DEBUG(ch->ch_index), ce);

	if (ch->ch_callback)
		ch->ch_callback(cs, ce, ch->ch_callbackarg);

	return 1;
}

struct bcmdmac_channel *
bcmdmac_alloc(enum bcmdmac_type type, int ipl,
	    void (*cb)(uint32_t, uint32_t, void *), void *cbarg)
{
	struct bcmdmac_softc *sc = bcmdmac_sc;
	struct bcmdmac_channel *ch = NULL;
	int index;

	if (sc == NULL)
		return NULL;

	mtx_enter(&sc->sc_lock);
	for (index = 0; index < sc->sc_nchannels; index++) {
		if (!ISSET(sc->sc_channelmask, (1 << index)))
			continue;
		if (bcmdmac_channel_type(&sc->sc_channels[index]) != type)
			continue;
		if (bcmdmac_channel_used(&sc->sc_channels[index]))
			continue;

		ch = &sc->sc_channels[index];
		ch->ch_callback = cb;
		ch->ch_callbackarg = cbarg;
		break;
	}
	mtx_leave(&sc->sc_lock);

	if (ch == NULL)
		return NULL;

	KASSERT(ch->ch_ih == NULL);

	ch->ch_ih = fdt_intr_establish_idx(sc->sc_fa_node, ch->ch_index, ipl,
					   bcmdmac_intr, ch, sc->sc_dev.dv_xname);

	if (ch->ch_ih == NULL) {
		printf("%s: failed to establish interrupt for DMA%d\n",
		       DEVNAME(sc), ch->ch_index);
		ch->ch_callback = NULL;
		ch->ch_callbackarg = NULL;
		ch = NULL;
	}

	return ch;
}

void
bcmdmac_free(struct bcmdmac_channel *ch)
{
	struct bcmdmac_softc *sc = ch->ch_sc;
	uint32_t val;

	bcmdmac_halt(ch);

	/* reset chip */
	val = bcmdmac_read(sc, DMAC_CS(ch->ch_index));
	val |= DMAC_CS_RESET;
	val &= ~DMAC_CS_ACTIVE;
	bcmdmac_write(sc, DMAC_CS(ch->ch_index), val);

	mtx_enter(&sc->sc_lock);
	fdt_intr_disestablish(ch->ch_ih);
	ch->ch_ih = NULL;
	ch->ch_callback = NULL;
	ch->ch_callbackarg = NULL;
	mtx_leave(&sc->sc_lock);
}

void
bcmdmac_set_conblk_addr(struct bcmdmac_channel *ch, bus_addr_t addr)
{
	struct bcmdmac_softc *sc = ch->ch_sc;

	bcmdmac_write(sc, DMAC_CONBLK_AD(ch->ch_index), addr);
}

int
bcmdmac_transfer(struct bcmdmac_channel *ch)
{
	struct bcmdmac_softc *sc = ch->ch_sc;
	uint32_t val;

	val = bcmdmac_read(sc, DMAC_CS(ch->ch_index));
	if (ISSET(val, DMAC_CS_ACTIVE))
		return EBUSY;

	val |= DMAC_CS_ACTIVE;
	bcmdmac_write(sc, DMAC_CS(ch->ch_index), val);

	return 0;
}

void
bcmdmac_halt(struct bcmdmac_channel *ch)
{
	struct bcmdmac_softc *sc = ch->ch_sc;
	uint32_t val;

	/* pause DMA */
	val = bcmdmac_read(sc, DMAC_CS(ch->ch_index));
	val &= ~DMAC_CS_ACTIVE;
	bcmdmac_write(sc, DMAC_CS(ch->ch_index), val);

	/* XXX wait for paused state */

	/* end descriptor chain */
	bcmdmac_write(sc, DMAC_NEXTCONBK(ch->ch_index), 0);

	/* resume DMA, which then stops */
	val |= DMAC_CS_ACTIVE | DMAC_CS_ABORT;
	bcmdmac_write(sc, DMAC_CS(ch->ch_index), val);
}
