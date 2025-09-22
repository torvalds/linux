/*	$OpenBSD: sfcc.c,v 1.4 2022/12/27 21:13:25 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define SFCC_FLUSH64	0x200

struct sfcc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_line_size;
};

struct sfcc_softc *sfcc_sc;

int	sfcc_match(struct device *, void *, void *);
void	sfcc_attach(struct device *, struct device *, void *);

const struct cfattach sfcc_ca = {
	sizeof (struct sfcc_softc), sfcc_match, sfcc_attach
};

struct cfdriver sfcc_cd = {
	NULL, "sfcc", DV_DULL
};

void	sfcc_cache_wbinv_range(paddr_t, psize_t);

int
sfcc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "sifive,fu540-c000-ccache") ||
	    OF_is_compatible(faa->fa_node, "starfive,jh7100-ccache"));
}

void
sfcc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sfcc_softc *sc = (struct sfcc_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	sc->sc_line_size = OF_getpropint(faa->fa_node, "cache-block-size", 64);
	sfcc_sc = sc;

	/*
	 * Cache lines can only be flushed, so we use the same
	 * operation everywhere.
	 */
	cpu_dcache_wbinv_range = sfcc_cache_wbinv_range;
	cpu_dcache_inv_range = sfcc_cache_wbinv_range;
	cpu_dcache_wb_range = sfcc_cache_wbinv_range;
}

void
sfcc_cache_wbinv_range(paddr_t pa, psize_t len)
{
	struct sfcc_softc *sc = sfcc_sc;
	paddr_t end, mask;

	mask = sc->sc_line_size - 1;
	end = (pa + len + mask) & ~mask;
	pa &= ~mask;

	__asm volatile ("fence iorw,iorw" ::: "memory");
	while (pa != end) {
		bus_space_write_8(sc->sc_iot, sc->sc_ioh, SFCC_FLUSH64, pa);
		__asm volatile ("fence iorw,iorw" ::: "memory");
		pa += sc->sc_line_size;
	}
}
