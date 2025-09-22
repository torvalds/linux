/*	$OpenBSD: mvgicp.c,v 1.6 2024/08/05 18:39:34 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct mvgicp_softc {
	struct device		  sc_dev;
	bus_space_tag_t		  sc_iot;
	bus_space_handle_t	  sc_ioh;
	paddr_t			  sc_addr;

	uint32_t		  sc_spi_ranges[4];
	void			**sc_spi;
	uint32_t		  sc_nspi;

	struct interrupt_controller sc_ic;
	struct interrupt_controller *sc_parent_ic;
};

int	mvgicp_match(struct device *, void *, void *);
void	mvgicp_attach(struct device *, struct device *, void *);

void *	mvgicp_intr_establish(void *, uint64_t *, uint64_t *,
	    int, struct cpu_info *, int (*)(void *), void *, char *);
void	mvgicp_intr_disestablish(void *);
void	mvgicp_intr_barrier(void *);

const struct cfattach mvgicp_ca = {
	sizeof(struct mvgicp_softc), mvgicp_match, mvgicp_attach
};

struct cfdriver mvgicp_cd = {
	NULL, "mvgicp", DV_DULL
};

int
mvgicp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,ap806-gicp");
}

void
mvgicp_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvgicp_softc *sc = (struct mvgicp_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct interrupt_controller *ic;
	int node;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	OF_getpropintarray(faa->fa_node, "marvell,spi-ranges",
	    sc->sc_spi_ranges, sizeof(sc->sc_spi_ranges));
	sc->sc_nspi = sc->sc_spi_ranges[1] + sc->sc_spi_ranges[3];
	sc->sc_spi = mallocarray(sc->sc_nspi, sizeof(void *),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* XXX: Hack to retrieve the physical address (from a CPU PoV). */
	if (!pmap_extract(pmap_kernel(), sc->sc_ioh, &sc->sc_addr)) {
		printf(": cannot retrieve msi addr\n");
		return;
	}

	extern int fdt_intr_get_parent(int);
	node = fdt_intr_get_parent(faa->fa_node);
	extern LIST_HEAD(, interrupt_controller) interrupt_controllers;
	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_node == node)
			break;
	}
	sc->sc_parent_ic = ic;

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish_msi = mvgicp_intr_establish;
	sc->sc_ic.ic_disestablish = mvgicp_intr_disestablish;
	sc->sc_ic.ic_barrier = mvgicp_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");
}

void *
mvgicp_intr_establish(void *self, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvgicp_softc *sc = (struct mvgicp_softc *)self;
	struct interrupt_controller *ic = sc->sc_parent_ic;
	struct machine_intr_handle *ih;
	uint32_t interrupt[3];
	uint32_t flags;
	void *cookie;
	int i, spi;

	if (ic == NULL)
		return NULL;

	for (i = 0; i < sc->sc_nspi; i++) {
		if (sc->sc_spi[i] == NULL) {
			spi = i;
			break;
		}
	}
	if (i == sc->sc_nspi)
		return NULL;

	flags = *data;

	*addr = sc->sc_addr;
	*data = spi;

	/* Convert to GIC interrupt source. */
	for (i = 0; i < nitems(sc->sc_spi_ranges); i += 2) {
		if (spi < sc->sc_spi_ranges[i + 1]) {
			spi += sc->sc_spi_ranges[i];
			break;
		}
		spi -= sc->sc_spi_ranges[i + 1];
	}
	if (i == nitems(sc->sc_spi_ranges))
		return NULL;

	interrupt[0] = 0;
	interrupt[1] = spi - 32;
	interrupt[2] = flags;
	cookie = ic->ic_establish(ic->ic_cookie, interrupt, level,
	    ci, func, arg, name);
	if (cookie == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = cookie;

	sc->sc_spi[*data] = ih;
	return &sc->sc_spi[*data];
}

void
mvgicp_intr_disestablish(void *cookie)
{
	struct machine_intr_handle *ih = *(void **)cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	ic->ic_disestablish(ih->ih_ih);
	free(ih, M_DEVBUF, sizeof(*ih));
	*(void **)cookie = NULL;
}

void
mvgicp_intr_barrier(void *cookie)
{
	struct machine_intr_handle *ih = *(void **)cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	ic->ic_barrier(ih->ih_ih);
}
