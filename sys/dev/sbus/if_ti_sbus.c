/*	$OpenBSD: if_ti_sbus.c,v 1.5 2022/03/13 13:34:54 mpi Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>

#include <dev/pci/pcireg.h>

#include <dev/ic/tireg.h>
#include <dev/ic/tivar.h>

struct ti_sbus_softc {
	struct ti_softc		tsc_sc;
};

int	ti_sbus_match(struct device *, void *, void *);
void	ti_sbus_attach(struct device *, struct device *, void *);

const struct cfattach ti_sbus_ca = {
	sizeof(struct ti_sbus_softc), ti_sbus_match, ti_sbus_attach
};

int
ti_sbus_match(struct device *parent, void *match, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("SUNW,vge", sa->sa_name) == 0);
}

void
ti_sbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct ti_sbus_softc *tsc = (void *)self;
	struct ti_softc *sc = &tsc->tsc_sc;
	bus_space_handle_t ioh;

	/* Pass on the bus tags */
	sc->ti_btag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sa->sa_nintr < 1) {
                printf(": no interrupt\n");
                return;
        }

	if (sa->sa_nreg < 2) {
                printf(": only %d register sets\n", sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    sa->sa_reg[1].sbr_offset, sa->sa_reg[1].sbr_size,
	    0, 0, &sc->ti_bhandle)) {
		printf(": can't map registers\n");
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size,
	    0, 0, &ioh)) {
		printf(": can't map registers\n");
		goto unmap;
	}

	bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_NET, 0, ti_intr,
	    sc, self->dv_xname);

	bus_space_write_4(sa->sa_bustag, ioh, TI_PCI_CMDSTAT, 0x02000006);
	bus_space_write_4(sa->sa_bustag, ioh, TI_PCI_BIST, 0xffffffff);
	bus_space_write_4(sa->sa_bustag, ioh, TI_PCI_LOMEM, 0x00000400);

	bus_space_unmap(sa->sa_bustag, ioh, sa->sa_reg[0].sbr_size);

	sc->ti_sbus = 1;
	if (ti_attach(sc) == 0)
		return;

unmap:
	    bus_space_unmap(sa->sa_bustag, sc->ti_bhandle,
		sa->sa_reg[1].sbr_size);
}
