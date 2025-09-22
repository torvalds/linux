/*	$OpenBSD: octrng.c,v 1.10 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2013 Paul Irofti <paul@irofti.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/octeonvar.h>
#include <machine/octeonreg.h>
#include <octeon/dev/iobusvar.h>

int	octrng_match(struct device *, void *, void *);
void	octrng_attach(struct device *, struct device *, void *);
void	octrng_rnd(void *arg);

#ifdef OCTRNG_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define OCTRNG_MAP_SIZE 8ULL

/* OCTRNG_ENTROPY_ADDR 0x8001400000000000ULL */
#define OCTRNG_MAJORDID 8ULL
#define OCTRNG_ENTROPY_ADDR (((uint64_t)1 << 48) | \
		((uint64_t)(OCTRNG_MAJORDID & 0x1f) << 43))
#define OCTRNG_ENTROPY_REG	0

#define OCTRNG_CONTROL_ADDR 0x0001180040000000ULL

#define OCTRNG_RESET (1ULL << 3)
#define OCTRNG_ENABLE_OUTPUT (1ULL << 1)
#define OCTRNG_ENABLE_ENTROPY (1ULL << 0)

struct octrng_softc {
	struct device sc_dev;
	struct timeout sc_to;
	struct iobus_attach_args *sc_io;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
};

const struct cfattach octrng_ca = {
	sizeof(struct octrng_softc), octrng_match, octrng_attach
};

struct cfdriver octrng_cd = {
	NULL, "octrng", DV_DULL
};


int
octrng_match(struct device *parent, void *match, void *aux)
{
	struct iobus_attach_args *aa = aux;
	struct cfdata *cf = match;

	/* XXX: check for board type */

	if (strcmp(aa->aa_name, cf->cf_driver->cd_name) != 0)
		return (0);

	return (1);
}

void
octrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct octrng_softc *sc = (void *)self;
	sc->sc_io = aux;

	uint64_t control_reg;

	sc->sc_iot = sc->sc_io->aa_bust;

	if (bus_space_map(sc->sc_iot, OCTEON_RNG_BASE, OCTRNG_MAP_SIZE, 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers");
	}

	control_reg = octeon_xkphys_read_8(OCTRNG_CONTROL_ADDR);
	control_reg |= (OCTRNG_ENABLE_OUTPUT | OCTRNG_ENABLE_ENTROPY);
	octeon_xkphys_write_8(OCTRNG_CONTROL_ADDR, control_reg);

	timeout_set(&sc->sc_to, octrng_rnd, sc);

	timeout_add_sec(&sc->sc_to, 5);

	printf("\n");
}

void
octrng_rnd(void *arg)
{
	struct octrng_softc *sc = arg;
	uint64_t value;

	value = bus_space_read_8(sc->sc_iot, sc->sc_ioh, OCTRNG_ENTROPY_REG);

	DPRINTF(("%#llX ", value));	/* WARNING: very verbose */

	enqueue_randomness(value);
	timeout_add_msec(&sc->sc_to, 10);
}
