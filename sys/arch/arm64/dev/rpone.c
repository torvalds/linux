/*	$OpenBSD: rpone.c,v 1.1 2025/08/20 21:40:34 kettenis Exp $ */

/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#define RP1_MSIX_BAR		(PCI_MAPREG_START + 0x00)
#define RP1_PERIPH_BAR		(PCI_MAPREG_START + 0x04)

#define RP1_MSIX_VECTORS	61

#define MSIX_CFG(x)		(0x108008 + 4 * (x))
#define MSIX_CFG_SET(x)		(MSIX_CFG(x) + 0x800)
#define MSIX_CFG_CLR(x)		(MSIX_CFG(x) + 0xc00)
#define  MSIX_CFG_ENABLE	(1 << 0)
#define  MSIX_CFG_TEST		(1 << 1)
#define  MSIX_CFG_IACK		(1 << 2)
#define  MSIX_CFG_IACK_EN	(1 << 3)

struct rpone_vector {
	int			(*rv_func)(void *);
	void			*rv_arg;
	int			rv_vec;
	int			rv_type;
	pci_intr_handle_t	rv_ih;
	void			*rv_sc;
};

struct rpone_softc {
	struct device		sc_dev;
	pci_chipset_tag_t	sc_pc;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct bus_space	sc_bus;
	bus_addr_t		sc_base;
	bus_size_t		sc_size;

	struct rpone_vector	*sc_vec;
	int			sc_nvec;
	struct interrupt_controller sc_ic;
};

int	rpone_match(struct device *, void *, void *);
void	rpone_attach(struct device *, struct device *, void *);

const struct cfattach rpone_ca = {
	sizeof(struct rpone_softc), rpone_match, rpone_attach,
};

struct cfdriver rpone_cd = {
	NULL, "rpone", DV_DULL
};

static const struct pci_matchid rpone_devices[] = {
	{ PCI_VENDOR_RPI,	PCI_PRODUCT_RPI_RP1 },
};

void	rpone_late(struct device *);
void 	*rpone_intr_establish(void *, int *, int, struct cpu_info *,
	     int (*)(void *), void *, char *);
int	rpone_bs_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	     bus_space_handle_t *);
int	rpone_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

int
rpone_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, rpone_devices, nitems(rpone_devices)));
}

void
rpone_attach(struct device *parent, struct device *self, void *aux)
{
	struct rpone_softc *sc = (struct rpone_softc *)self;
	struct pci_attach_args *pa = aux;
	struct fdt_attach_args faa;
	pcireg_t memtype;
	int node, vec;

	sc->sc_pc = pa->pa_pc;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, RP1_PERIPH_BAR);
	if (pci_mapreg_map(pa, RP1_PERIPH_BAR, memtype, 0,
	    &sc->sc_iot, &sc->sc_ioh, &sc->sc_base, &sc->sc_size, 0) != 0) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_nvec = pci_intr_msix_count(pa);
	if (sc->sc_nvec == 0) {
		printf(": no msix vectors\n");
		return;
	}
	sc->sc_vec = mallocarray(sc->sc_nvec, sizeof(struct rpone_vector),
	    M_DEVBUF, M_WAITOK);
	for (vec = 0; vec < sc->sc_nvec; vec++) {
		if (pci_intr_map_msix(pa, vec, &sc->sc_vec[vec].rv_ih)) {
			printf(": can't map msix vector\n");
			return;
		}
	}

	printf("\n");

	node = PCITAG_NODE(pa->pa_tag);
	if (node == 0) {
		printf("%s: can't find device tree node\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_ic.ic_node = node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = rpone_intr_establish;
	sc->sc_ic.ic_barrier = intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	memcpy(&sc->sc_bus, sc->sc_iot, sizeof(sc->sc_bus));
	sc->sc_bus.bus_private = sc;
	sc->sc_bus._space_map = rpone_bs_map;

	memset(&faa, 0, sizeof(faa));
	faa.fa_node = node;
	faa.fa_iot = &sc->sc_bus;
	faa.fa_dmat = pa->pa_dmat;
	faa.fa_acells = 3;
	faa.fa_scells = 2;
	config_found(self, &faa, NULL);
}

int
rpone_intr(void *arg)
{
	struct rpone_vector *rv = arg;
	struct rpone_softc *sc = rv->rv_sc;
	int handled;

	handled = rv->rv_func(rv->rv_arg);

	/* ACK level-triggered interrupts. */
	if (rv->rv_type == 4) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		     MSIX_CFG_SET(rv->rv_vec), MSIX_CFG_IACK);
	}

	return handled;
}

void *
rpone_intr_establish(void *cookie, int *cells, int level,
     struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct rpone_softc *sc = cookie;
	struct rpone_vector *rv;
	uint32_t vec = cells[0];
	uint32_t type = cells[1];

	if (vec >= sc->sc_nvec)
		return NULL;

	rv = &sc->sc_vec[vec];
	rv->rv_func = func;
	rv->rv_arg = arg;
	rv->rv_vec = vec;
	rv->rv_type = type;
	rv->rv_sc = sc;

	if (type == 4) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    MSIX_CFG_SET(vec), MSIX_CFG_IACK_EN);
	} else {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    MSIX_CFG_CLR(vec), MSIX_CFG_IACK_EN);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    MSIX_CFG_SET(vec), MSIX_CFG_ENABLE);

	return pci_intr_establish(sc->sc_pc, rv->rv_ih, level,
	    rpone_intr, rv, name);
}

int
rpone_bs_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
	     int flag, bus_space_handle_t *bshp)
{
	struct rpone_softc *sc = t->bus_private;

	if (bpa >= sc->sc_size)
		return ENXIO;

	return bus_space_map(sc->sc_iot, bpa + sc->sc_base, size, flag, bshp);
}
