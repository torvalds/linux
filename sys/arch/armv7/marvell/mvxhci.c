/* $OpenBSD: mvxhci.c,v 1.4 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <armv7/marvell/mvmbusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/xhcireg.h>
#include <dev/usb/xhcivar.h>

#define MVXHCI_READ(sc, reg) \
	bus_space_read_4((sc)->sc.iot, (sc)->mbus_ioh, (reg))
#define MVXHCI_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc.iot, (sc)->mbus_ioh, (reg), (val))

#define MVXHCI_NWINDOW			4
#define MVXHCI_CTRL(x)			(0x0 + ((x) << 3))
#define MVXHCI_BASE(x)			(0x4 + ((x) << 3))

#define MVXHCI_TARGET(target)		(((target) & 0xf) << 4)
#define MVXHCI_ATTR(attr)		(((attr) & 0xff) << 8)
#define MVXHCI_BASEADDR(base)		((base) & 0xffff0000)
#define MVXHCI_SIZE(size)		(((size) - 1) & 0xffff0000)
#define MVXHCI_WINEN			(1 << 0)

struct mvxhci_softc {
	struct xhci_softc	sc;
	int			sc_node;
	bus_space_handle_t	mbus_ioh;
	void			*sc_ih;
};

void	mvxhci_wininit(struct mvxhci_softc *);

int	mvxhci_match(struct device *, void *, void *);
void	mvxhci_attach(struct device *, struct device *, void *);

const struct cfattach mvxhci_ca = {
	sizeof (struct mvxhci_softc), mvxhci_match, mvxhci_attach
};

struct cfdriver mvxhci_cd = {
	NULL, "mvxhci", DV_DULL
};

void
mvxhci_wininit(struct mvxhci_softc *sc)
{
	int i;

	if (mvmbus_dram_info == NULL)
		panic("%s: mbus dram information not set up", __func__);

	for (i = 0; i < MVXHCI_NWINDOW; i++) {
		MVXHCI_WRITE(sc, MVXHCI_CTRL(i), 0);
		MVXHCI_WRITE(sc, MVXHCI_BASE(i), 0);
	}

	for (i = 0; i < mvmbus_dram_info->numcs; i++) {
		struct mbus_dram_window *win = &mvmbus_dram_info->cs[i];

		MVXHCI_WRITE(sc, MVXHCI_CTRL(i),
		    MVXHCI_WINEN |
		    MVXHCI_TARGET(mvmbus_dram_info->targetid) |
		    MVXHCI_ATTR(win->attr) |
		    MVXHCI_SIZE(win->size));
		MVXHCI_WRITE(sc, MVXHCI_BASE(i), MVXHCI_BASEADDR(win->base));
	}
}

int
mvxhci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-375-xhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-380-xhci");
}

void
mvxhci_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvxhci_softc *sc = (struct mvxhci_softc *)self;
	struct fdt_attach_args *faa = aux;
	int error;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc.iot = faa->fa_iot;
	sc->sc.sc_bus.dmatag = faa->fa_dmat;
	sc->sc.sc_size = faa->fa_reg[0].size;

	if (bus_space_map(sc->sc.iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc.ioh)) {
		printf(": can't map registers\n");
		return;
	}

	clock_enable_all(faa->fa_node);
	reset_deassert_all(sc->sc_node);

	if (bus_space_map(sc->sc.iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->mbus_ioh)) {
		printf(": can't map registers\n");
		goto unmap;
	}

	/* Set up MBUS windows. */
	mvxhci_wininit(sc);

	bus_space_unmap(sc->sc.iot, sc->mbus_ioh, faa->fa_reg[1].size);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_USB,
	    xhci_intr, &sc->sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	strlcpy(sc->sc.sc_vendor, "Marvell", sizeof(sc->sc.sc_vendor));
	if ((error = xhci_init(&sc->sc)) != 0) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, error);
		goto disestablish_ret;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);

	/* Now that the stack is ready, config' the HC and enable interrupts. */
	xhci_config(&sc->sc);

	return;

disestablish_ret:
	arm_intr_disestablish_fdt(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
}
