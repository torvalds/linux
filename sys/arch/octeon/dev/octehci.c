/*	$OpenBSD: octehci.c,v 1.3 2017/07/25 11:01:28 jmatthew Exp $ */

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

#include <sys/rwlock.h>
#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

struct octehci_softc {
	struct ehci_softc	sc_ehci;

	void			*sc_ih;
};

int		octehci_match(struct device *, void *, void *);
void		octehci_attach(struct device *, struct device *, void *);

const struct cfattach octehci_ca = {
	sizeof(struct octehci_softc), octehci_match, octehci_attach,
};

struct cfdriver octehci_cd = {
	NULL, "ehci", DV_DULL
};

int
octehci_match(struct device *parent, void *match, void *aux)
{
	struct octuctl_attach_args *aa = aux;
	return (OF_is_compatible(aa->aa_node, "cavium,octeon-6335-ehci"));
}

void
octehci_attach(struct device *parent, struct device *self, void *aux)
{
	struct octehci_softc *sc = (struct octehci_softc *)self;
	struct octuctl_attach_args *aa = aux;
	uint64_t port_ctl;
	int rc;
	int s;

	sc->sc_ehci.iot = aa->aa_bust;
	sc->sc_ehci.sc_bus.pipe_size = sizeof(struct usbd_pipe);
	sc->sc_ehci.sc_bus.dmatag = aa->aa_dmat;

	rc = bus_space_map(sc->sc_ehci.iot, aa->aa_reg.addr, aa->aa_reg.size,
	    0, &sc->sc_ehci.ioh);
	KASSERT(rc == 0);

	port_ctl = bus_space_read_8(aa->aa_octuctl_bust, aa->aa_ioh,
	    UCTL_EHCI_CTL);
	port_ctl &= ~UCTL_EHCI_CTL_L2C_ADDR_MSB_MASK;
	port_ctl |= (1 << UCTL_EHCI_CTL_L2C_DESC_EMOD_SHIFT);
	port_ctl |= (1 << UCTL_EHCI_CTL_L2C_BUFF_EMOD_SHIFT);
	port_ctl |= UCTL_EHCI_CTL_EHCI_64B_ADDR_EN;
	bus_space_write_8(aa->aa_octuctl_bust, aa->aa_ioh, UCTL_EHCI_CTL,
	    port_ctl);

	s = splhardusb();
	sc->sc_ehci.sc_offs = EREAD1(&sc->sc_ehci, EHCI_CAPLENGTH);
	EOWRITE2(&sc->sc_ehci, EHCI_USBINTR, 0);

	sc->sc_ehci.sc_id_vendor = 0;
	strlcpy(sc->sc_ehci.sc_vendor, "Octeon", sizeof(sc->sc_ehci.sc_vendor));

	sc->sc_ih = octeon_intr_establish(CIU_INT_USB, IPL_USB | IPL_MPSAFE,
	    ehci_intr, (void *)&sc->sc_ehci, sc->sc_ehci.sc_bus.bdev.dv_xname);
	KASSERT(sc->sc_ih != NULL);

	rc = ehci_init(&sc->sc_ehci);
	if (rc != USBD_NORMAL_COMPLETION) {
		printf(": init failed, error=%d\n", rc);
		octeon_intr_disestablish(sc->sc_ih);
		bus_space_unmap(sc->sc_ehci.iot, sc->sc_ehci.ioh,
		    aa->aa_reg.size);
		splx(s);
		return;
	}

	printf("\n");
	if (rc == 0)
		config_found(self, &sc->sc_ehci.sc_bus, usbctlprint);
	splx(s);
}
