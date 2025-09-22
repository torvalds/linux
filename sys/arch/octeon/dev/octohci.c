/*	$OpenBSD: octohci.c,v 1.4 2019/01/07 03:41:06 dlg Exp $ */

/*
 * Copyright (c) 2015 Jonathan Matthew  <jmatthew@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>

#include <octeon/dev/octuctlreg.h>
#include <octeon/dev/octuctlvar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

struct octohci_softc {
	struct ohci_softc	sc_ohci;

	void			*sc_ih;
	uint64_t		sc_reg_size;
};

int		octohci_match(struct device *, void *, void *);
void		octohci_attach(struct device *, struct device *, void *);
void		octohci_attach_deferred(struct device *);

const struct cfattach octohci_ca = {
	sizeof(struct octohci_softc), octohci_match, octohci_attach,
};

struct cfdriver octohci_cd = {
	NULL, "ohci", DV_DULL
};

int
octohci_match(struct device *parent, void *match, void *aux)
{
	struct octuctl_attach_args *aa = aux;
	return (OF_is_compatible(aa->aa_node, "cavium,octeon-6335-ohci"));
}

void
octohci_attach(struct device *parent, struct device *self, void *aux)
{
	struct octohci_softc *sc = (struct octohci_softc *)self;
	struct octuctl_attach_args *aa = aux;
	uint64_t port_ctl;
	int rc;
	int s;

	sc->sc_ohci.iot = aa->aa_bust;
	sc->sc_ohci.sc_bus.pipe_size = sizeof(struct usbd_pipe);
	sc->sc_ohci.sc_bus.dmatag = aa->aa_dmat;

	rc = bus_space_map(sc->sc_ohci.iot, aa->aa_reg.addr, aa->aa_reg.size,
	    0, &sc->sc_ohci.ioh);
	KASSERT(rc == 0);
	sc->sc_reg_size = aa->aa_reg.size;

	port_ctl = bus_space_read_8(aa->aa_octuctl_bust, aa->aa_ioh,
	    UCTL_OHCI_CTL);
	port_ctl &= ~UCTL_OHCI_CTL_L2C_ADDR_MSB_MASK;
	port_ctl |= (1 << UCTL_OHCI_CTL_L2C_DESC_EMOD_SHIFT);
	port_ctl |= (1 << UCTL_OHCI_CTL_L2C_BUFF_EMOD_SHIFT);
	bus_space_write_8(aa->aa_octuctl_bust, aa->aa_ioh, UCTL_OHCI_CTL,
	    port_ctl);

	s = splusb();

	sc->sc_ohci.sc_id_vendor = 0;
	strlcpy(sc->sc_ohci.sc_vendor, "Octeon", sizeof(sc->sc_ohci.sc_vendor));

	sc->sc_ih = octeon_intr_establish(CIU_INT_USB, IPL_USB, ohci_intr,
	    (void *)&sc->sc_ohci, sc->sc_ohci.sc_bus.bdev.dv_xname);
	KASSERT(sc->sc_ih != NULL);

	printf(", ");

	if ((ohci_checkrev(&sc->sc_ohci) != USBD_NORMAL_COMPLETION) ||
	    (ohci_handover(&sc->sc_ohci) != USBD_NORMAL_COMPLETION))
		goto failed;

	/* ignore interrupts for now */
	sc->sc_ohci.sc_bus.dying = 1;
	config_defer(self, octohci_attach_deferred);

	splx(s);
	return;

failed:
	octeon_intr_disestablish(sc->sc_ih);
	bus_space_unmap(sc->sc_ohci.iot, sc->sc_ohci.ioh, sc->sc_reg_size);
	splx(s);
	return;
}

void
octohci_attach_deferred(struct device *self)
{
	struct octohci_softc *sc = (struct octohci_softc *)self;
	usbd_status r;
	int s;

	s = splusb();
	sc->sc_ohci.sc_bus.dying = 0;

	r = ohci_init(&sc->sc_ohci);
	splx(s);

	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n",
		    sc->sc_ohci.sc_bus.bdev.dv_xname, r);
		octeon_intr_disestablish(sc->sc_ih);
		bus_space_unmap(sc->sc_ohci.iot, sc->sc_ohci.ioh,
		    sc->sc_reg_size);
	} else {
		config_found(self, &sc->sc_ohci.sc_bus, usbctlprint);
	}
}
