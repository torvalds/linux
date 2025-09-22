/*	$OpenBSD: ohci_fdt.c,v 1.3 2021/10/24 17:52:26 mpi Exp $ */

/*
 * Copyright (c) 2005, 2019 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

struct ohci_fdt_softc {
	struct ohci_softc	sc;
	int			sc_node;
	void			*sc_ih;
};

int	ohci_fdt_match(struct device *, void *, void *);
void	ohci_fdt_attach(struct device *, struct device *, void *);
int	ohci_fdt_detach(struct device *, int);

const struct cfattach ohci_fdt_ca = {
	sizeof(struct ohci_fdt_softc),
	ohci_fdt_match,
	ohci_fdt_attach,
	ohci_fdt_detach,
	ohci_activate
};

void	ohci_fdt_attach_deferred(struct device *);

int
ohci_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "generic-ohci");
}

void
ohci_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct ohci_fdt_softc *sc = (struct ohci_fdt_softc *)self;
	struct fdt_attach_args *faa = aux;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;

	if (faa->fa_nreg < 1) {
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
		goto out;
	}

	pinctrl_byname(sc->sc_node, "default");

	clock_enable_all(sc->sc_node);
	reset_deassert_all(sc->sc_node);

	/* Record what interrupts were enabled by SMM/BIOS. */
	sc->sc.sc_intre = bus_space_read_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_ENABLE);

	/* Disable interrupts, so we don't get any spurious ones. */
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OHCI_INTERRUPT_DISABLE,
	    OHCI_MIE);

	bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_DISABLE, OHCI_MIE);

	/* Map and establish the interrupt. */
	splassert(IPL_USB);
	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_USB,
	    ohci_intr, &sc->sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto disable_clocks;
	}
	printf(": ");

	strlcpy(sc->sc.sc_vendor, "Generic", sizeof(sc->sc.sc_vendor));

	/* Display revision. Legacy handover isn't needed as there's no smm */
	if (ohci_checkrev(&sc->sc) != USBD_NORMAL_COMPLETION) {
		goto disestablish_intr;
	}

	/* Ignore interrupts for now */
	sc->sc.sc_bus.dying = 1;

	config_defer(self, ohci_fdt_attach_deferred);

	return;

disestablish_intr:
	fdt_intr_disestablish(sc->sc_ih);
	sc->sc_ih = NULL;
disable_clocks:
	clock_disable_all(sc->sc_node);

	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	sc->sc.sc_size = 0;
out:
	return;
}

void
ohci_fdt_attach_deferred(struct device *self)
{
	struct ohci_fdt_softc *sc = (struct ohci_fdt_softc *)self;
	usbd_status r;
	int s;

	s = splusb();

	sc->sc.sc_bus.dying = 0;

	r = ohci_init(&sc->sc);

	splx(s);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, r);
		fdt_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
		clock_disable_all(sc->sc_node);
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
		return;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
}

int
ohci_fdt_detach(struct device *self, int flags)
{
	struct ohci_fdt_softc *sc = (struct ohci_fdt_softc *)self;
	int rv;

	rv = ohci_detach(self, flags);
	if (rv)
		return rv;

	if (sc->sc_ih != NULL) {
		fdt_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
	}

	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	clock_disable_all(sc->sc_node);
	return 0;
}
