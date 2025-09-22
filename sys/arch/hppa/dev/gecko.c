/*	$OpenBSD: gecko.c,v 1.2 2022/03/13 08:04:38 mpi Exp $	*/

/*
 * Copyright (c) 2007 Mark Kettenis
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
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/iomod.h>
#include <machine/pdc.h>

#include <hppa/dev/cpudevs.h>

struct gecko_softc {
	struct device		sc_dv;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t 	sc_ioh;
};

int	gecko_match(struct device *, void *, void *);
void	gecko_attach(struct device *, struct device *, void *);

const struct cfattach gecko_ca = {
	sizeof(struct gecko_softc), gecko_match, gecko_attach
};

struct cfdriver gecko_cd = {
	NULL, "gecko", DV_DULL
};

int
gecko_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_type.iodc_type != HPPA_TYPE_BCPORT ||
	    ca->ca_type.iodc_sv_model != HPPA_BCPORT_PORT)
		return (0);

	if (ca->ca_type.iodc_model == 0x50 &&
	    ca->ca_type.iodc_revision == 0x00)
		return (1);

	return (0);
}

void
gecko_attach(struct device *parent, struct device *self, void *aux)
{
	struct gecko_softc *sc = (struct gecko_softc *)self;
	struct confargs *ca = aux, nca;
	bus_space_handle_t ioh;
	volatile struct iomod *regs;

	sc->sc_iot = ca->ca_iot;
	if (bus_space_map(sc->sc_iot, ca->ca_hpa, IOMOD_HPASIZE, 0,
	    &sc->sc_ioh)) {
		printf(": can't map IO space\n");
		return;
	}
	regs = bus_space_vaddr(ca->ca_iot, ioh);

#if 1
	printf(": %x-%x", regs->io_io_low, regs->io_io_high);
#endif

	printf("\n");

	nca = *ca;
	nca.ca_hpamask = HPPA_IOBEGIN;
	pdc_scanbus(self, &nca, MAXMODBUS, regs->io_io_low);
}
